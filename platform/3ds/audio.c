/*
 * The sound transport. See audio.h for what this is and what it is not.
 *
 * The service is asked to loop over a ring of samples, once for each ear.
 * The emulator writes into the ring ahead of the position the service has
 * reached, which is worked out from the clock.
 */

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <3ds/allocator/linear.h>
#include <3ds/os.h>
#include <3ds/services/csnd.h>

#include "audio.h"

/* The service plays one sample per channel, so the two ears go to two
 * channels. */
#define AUDIO_EARS 2

/* Where each ear's channel is played.
 *
 * The service is given a volume and a pan, and the pan is what decides
 * which speaker a channel comes out of: -1 is all the way to the left
 * and +1 all the way to the right.
 *
 * A pan of 0, which is the middle, is not the same as "straight
 * ahead". The service turns a volume and a pan into a pair of volumes,
 * one for each speaker, so a pan of 0 gives each speaker half of the
 * volume. A channel played with a pan of 0 comes out of both ears at
 * half the volume, and with a channel for each ear that means both ears
 * hear a mixture of the two at half the volume: mono, and quiet.
 */
#define AUDIO_PAN_LEFT (-1.0f)
#define AUDIO_PAN_RIGHT (1.0f)

/* The channels to try. The service hands out the channels an
 * application may use, and a channel it has not handed out cannot be
 * played on: csndPlaySound says so and does nothing. Rather than guess
 * which ones are free, they are tried in turn until two of them work.
 */
#define AUDIO_CHANNEL_FIRST 0
#define AUDIO_CHANNEL_LAST 16

/* How much audio is kept in flight. At 49932 Hz, 8192 frames are 164 ms. */
#define AUDIO_RING_FRAMES 8192

/* How far ahead of the service the emulator writes.
 *
 * The lower bound is the latency of the sound: the service is always that
 * many frames behind what the emulator has produced. The upper bound is
 * how close the writer may get to the samples the service has not played
 * yet before it has to start dropping them.
 *
 * The band between them is 6 thousand frames, which is 125 ms. A
 * difference between the console's clock and the sound service's clock of
 * one part in a hundred thousand moves the write position by half a frame
 * a second, so the band is not going to be left often. */
#define AUDIO_FILL_MIN 1024
#define AUDIO_FILL_MAX (AUDIO_RING_FRAMES - 1024)

/* The ring holds the samples the service loops over. One extra sample is
 * needed at the front: the service plays the buffer once from the start
 * before it starts looping, and the loop has to start at the second
 * sample. */
#define AUDIO_BUFFER_FRAMES (AUDIO_RING_FRAMES + 1)

static s16* ring[AUDIO_EARS];
static int channel[AUDIO_EARS];
static bool running;
static bool muted;

/* The volume each ear's channel is played at, so that muting and
 * unmuting can put back exactly what was there before. */
static u32 earVolume[AUDIO_EARS];

/* Frames the emulator has written into the ring. */
static u64 written;

/* The clock at which the service was told to play. */
static u64 playStart;

/* The last sample written, so that the transport can repeat it when the
 * emulator falls behind. */
static s16 lastLeft;
static s16 lastRight;

static unsigned long long statFrames;
static unsigned long long statUnderruns;
static unsigned long long statDrops;
static unsigned long long statCorrections;

/*
 * How many frames the service has played.
 *
 * The service is told to play at AUDIO_RATE, and the console's clock says
 * how long it has been playing, so the position follows from the two. The
 * service's own clock is not exactly the console's clock, and the two
 * rates are not exactly the rate that was asked for; the difference is
 * small, it does not accumulate, and the sample correction below takes it
 * out of the signal.
 */
static u64 readPosition(void)
{
    u64 elapsed = svcGetSystemTick() - playStart;
    u64 seconds = elapsed / SYSCLOCK_ARM11;
    u64 rest = elapsed % SYSCLOCK_ARM11;

    return seconds * AUDIO_RATE + (rest * AUDIO_RATE) / SYSCLOCK_ARM11;
}

bool audio_init(void)
{
    if (running)
        return true;

    if (csndInit() != 0)
        return false;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        ring[ear] = linearAlloc(AUDIO_BUFFER_FRAMES * sizeof(s16));
        if (!ring[ear]) {
            audio_exit();
            return false;
        }
        memset(ring[ear], 0, AUDIO_BUFFER_FRAMES * sizeof(s16));
        channel[ear] = -1;
    }

    /* The service plays the buffer from the start once and then loops from
     * the second sample onwards, so the ring is the loop and the sample in
     * front of it is the one that is played first.
     *
     * Each ear gets its own channel, panned to its own side. The service
     * is given a volume and a pan and works out a volume for each speaker
     * from them, so the pan has to be all the way over for the ear to get
     * the whole of the signal.
     *
     * The channels are tried in turn because the service decides which
     * ones an application may use, and a channel it has not handed out
     * cannot be played on. csndPlaySound says so and does nothing, so
     * the first two that answer are the ones to use. */
    int next = 0;
    for (int chn = AUDIO_CHANNEL_FIRST; chn <= AUDIO_CHANNEL_LAST && next < AUDIO_EARS; chn++) {
        int ear = next;
        float pan = (ear == 0) ? AUDIO_PAN_LEFT : AUDIO_PAN_RIGHT;

        Result r = csndPlaySound(chn,
                                  SOUND_REPEAT | SOUND_FORMAT_16BIT | SOUND_ENABLE,
                                  AUDIO_RATE, 1.0f, pan,
                                  ring[ear], ring[ear] + 1,
                                  AUDIO_BUFFER_FRAMES * sizeof(s16));
        if (R_FAILED(r))
            continue;

        channel[ear] = chn;
        earVolume[ear] = CSND_VOL(1.0f, pan);
        next++;
    }

    if (next < AUDIO_EARS) {
        /* Not enough channels to play both ears. */
        audio_exit();
        return false;
    }

    playStart = svcGetSystemTick();
    /* The writer starts a little way ahead of the service, so that the
     * service has something to play straight away. The ring is full of
     * silence at this point. */
    written = AUDIO_FILL_MIN;
    lastLeft = 0;
    lastRight = 0;

    statFrames = 0;
    statUnderruns = 0;
    statDrops = 0;
    statCorrections = 0;

    running = true;
    return true;
}

void audio_exit(void)
{
    if (!running && !ring[0])
        return;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        if (ring[ear]) {
            if (channel[ear] >= 0)
                CSND_SetPlayState(channel[ear], 0);
            linearFree(ring[ear]);
            ring[ear] = NULL;
        }
    }

    /* The commands above are only written into the service's queue.
     * They are not sent until the queue is executed, and without this
     * the service keeps playing the ring that has just been freed. */
    if (running)
        csndExecCmds(true);

    running = false;
}

void audio_set_muted(bool newMuted)
{
    if (newMuted == muted)
        return;

    muted = newMuted;

    if (running) {
        for (int ear = 0; ear < AUDIO_EARS; ear++) {
            if (channel[ear] < 0)
                continue;
            CSND_SetVol(channel[ear], muted ? 0 : earVolume[ear], 0);
        }

        /* Same again: the volume change is queued, and it only reaches
         * the service when the queue is executed. */
        csndExecCmds(true);
    }
}

bool audio_is_muted(void)
{
    return muted;
}

void audio_submit(const int16_t* left, const int16_t* right, unsigned frames)
{
    if (!running)
        return;

    u64 read = readPosition();

    /* How far ahead of the service the writer is, in frames. This is the
     * latency of the sound: the service is that many frames behind what
     * the emulator has produced. */
    s64 fill = (s64)(written - read);

    if (fill <= 0)
        statUnderruns++;

    if (fill > AUDIO_FILL_MAX) {
        /* The writer is about to run into samples the service has not
         * played yet. Frames are dropped rather than allowed to overwrite
         * them: the alternative is to play the wrong audio. The frames
         * that are dropped are the newest ones, so the sound falls behind
         * the picture rather than jumping ahead of it. */
        u64 excess = (u64)(fill - AUDIO_FILL_MAX);
        if (excess > frames)
            excess = frames;
        statDrops += excess;
        frames -= (unsigned)excess;
    }
    else if (fill < AUDIO_FILL_MIN) {
        /* The writer has fallen behind the service, which is about to play
         * samples that have not been written. The last sample is repeated
         * so that the two keep the same distance apart. One sample of
         * difference is 30 millionths of a second. */
        u64 extra = (u64)(AUDIO_FILL_MIN - fill);
        if (extra > AUDIO_RING_FRAMES)
            extra = AUDIO_RING_FRAMES;

        for (u64 i = 0; i < extra; i++) {
            u64 index = written % AUDIO_RING_FRAMES;
            ring[0][1 + index] = lastLeft;
            ring[1][1 + index] = lastRight;
            written++;
        }
        statCorrections += extra;
    }

    if (!frames)
        return;

    /* Write the samples into the ring. The ring wraps, so this is at most
     * two runs. */
    u64 index = written % AUDIO_RING_FRAMES;
    unsigned first = (unsigned)(AUDIO_RING_FRAMES - index);
    if (first > frames)
        first = frames;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        const s16* src = ear ? right : left;
        s16* dst = ring[ear] + 1 + index;

        memcpy(dst, src, first * sizeof(s16));
        if (first < frames)
            memcpy(ring[ear] + 1, src + first, (frames - first) * sizeof(s16));
    }

    lastLeft = left[frames - 1];
    lastRight = right[frames - 1];

    written += frames;
    statFrames += frames;

    /* The service reads the samples over the bus, so the cache has to be
     * told about them. Only the part that was written is flushed. */
    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        s16* dst = ring[ear] + 1 + index;
        CSND_FlushDataCache(dst, first * sizeof(s16));
        if (first < frames)
            CSND_FlushDataCache(ring[ear] + 1, (frames - first) * sizeof(s16));
    }
}

void audio_get_stats(audio_stats* out)
{
    if (!out)
        return;

    out->running = running;
    out->frames = statFrames;
    out->underruns = statUnderruns;
    out->drops = statDrops;
    out->corrections = statCorrections;

    if (running) {
        s64 fill = (s64)(written - readPosition());
        if (fill < 0)
            fill = 0;
        out->fillFrames = (int)fill;
        out->latencyMs = (int)((fill * 1000) / AUDIO_RATE);
    }
    else {
        out->fillFrames = 0;
        out->latencyMs = 0;
    }
}
