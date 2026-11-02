/*
 * The sound transport. See audio.h for what this is and what it is not.
 *
 * The service is asked to loop over a ring of samples, once for each ear.
 * The emulator writes into the ring ahead of the position the service has
 * reached, which is worked out from the clock.
 */

#include <3ds.h>
#include <stdio.h>
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
 * which ones are free, the ones the service handed out are looked at.
 *
 * The channels below eight are the ones that play recorded sound; the
 * ones above are the tone and noise channels. The sound here is
 * recorded, so the lower ones are preferred, but the service may hand
 * out only the upper ones, and then those are used.
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

/* The ring holds the samples the service loops over. The loop does not
 * start at the beginning of the buffer: the service plays the buffer
 * once from the start before it starts looping, so the loop has to be
 * inside the buffer it plays first.
 *
 * The loop starts two frames in, which puts the address the service is
 * given for it on a four byte boundary. The service reads the samples
 * over the bus, and the bus reads them a word at a time, so an address
 * that is not a multiple of four is an address the service may not be
 * able to read at all.
 */
#define AUDIO_LOOP_OFFSET_FRAMES 2

/* The buffer holds the ring and the frames in front of it that the loop
 * starts in. */
#define AUDIO_BUFFER_FRAMES (AUDIO_RING_FRAMES + AUDIO_LOOP_OFFSET_FRAMES)

/* The rate the service will really play at.
 *
 * The service is told a rate and turns it into a timer, and the timer is
 * a whole number of ticks, so the rate it plays at is not the rate that
 * was asked for. Asking for 49932 Hz gives a timer of 1342, and
 * 67027964 ticks a second divided by 1342 is 49946 Hz.
 *
 * The transport works out where the service has got to from the console's
 * clock, so it has to use the rate the service really plays at. Using
 * the rate that was asked for makes the two drift apart at about
 * fourteen frames a second, which is nothing to begin with and then,
 * after about seven minutes, the writer catches up with the samples the
 * service has not played yet and starts throwing them away.
 *
 * The rate is kept as the fraction the hardware actually uses, so that
 * there is no rounding error either.
 */
#define AUDIO_RATE_NUM CSND_TIMER(1)
#define AUDIO_RATE_DEN CSND_TIMER(AUDIO_RATE)

static s16* ring[AUDIO_EARS];
static int channel[AUDIO_EARS];
static bool running;
static bool muted;

/* The loudest sample handed over in the last block. It says whether the
 * APU is making a sound at all, which is worth knowing when the speaker
 * says nothing. */
static int lastPeak;

/* The same, for each ear, and how loud the block was as a percentage of
 * full scale. The peak on its own says whether there was a sound; the
 * level says how much of one there was, which is the difference between a
 * quiet game and a silent one. */
static int lastPeakLeft;
static int lastPeakRight;
static int lastLevel;

/*
 * The integer square root.
 *
 * The maths library has one of these, but the library is a megabyte of
 * code the port would carry for the sake of one number in a diagnostic,
 * and this is the classic bit by bit version: twenty lines and no library.
 */
static u32 integerRoot(u64 value)
{
    u64 remainder = 0;
    u64 root = 0;
    int i;

    for (i = 0; i < 32; i++) {
        root <<= 1;
        remainder = (remainder << 2) | (value >> 62);
        value <<= 2;

        if (root < remainder) {
            root++;
            remainder -= root + 1;
        }
    }

    return (u32)(root >> 1);
}

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
 * The test tone.
 *
 * A tone of a known frequency and a known level, played through the same
 * transport as the game's sound. It is what tells the two apart: if the
 * tone comes out of the speaker, the transport and the service are working
 * and whatever is wrong is in the APU; if it does not, the fault is in the
 * transport.
 *
 * The tone is a sine at full scale, which is as loud as the transport can
 * play. The APU's own output is about a twentieth of that, so a game that
 * is quiet and a game that is silent look the same until this is played.
 */
static int testToneHz;
static u32 testToneStep; /* the phase step per sample, in 1/65536 of a cycle */
static u32 testTonePhase;

/* How many blocks have been handed over, and how many had been by the last
 * time the tone was pumped. They are what tells the pump that the tone has
 * already been written this frame. */
static u64 submitCount;
static u64 pumpedCount;

/* A quarter of a sine wave, in sixty four steps. The rest of the wave is
 * made out of this by mirroring it, so there is no maths in the frame
 * loop and no library to link for it. */
static const s16 toneQuarter[64] = {
         0,    736,   1472,   2207,   2941,   3672,   4402,   5129,
      5853,   6573,   7289,   8001,   8709,   9410,  10107,  10797,
     11481,  12157,  12827,  13488,  14142,  14787,  15423,  16050,
     16667,  17274,  17871,  18457,  19032,  19595,  20147,  20686,
     21213,  21727,  22229,  22716,  23190,  23650,  24096,  24528,
     24944,  25346,  25732,  26103,  26458,  26797,  27120,  27426,
     27716,  27990,  28246,  28486,  28708,  28913,  29101,  29271,
     29424,  29558,  29675,  29774,  29856,  29919,  29964,  29991,
};

/* The next sample of the tone. */
static s16 toneSample(void)
{
    u32 phase = testTonePhase;
    unsigned quadrant = (phase >> 14) & 3;
    unsigned step = (phase >> 8) & 0x3F;
    s16 value;

    testTonePhase = phase + testToneStep;

    if (quadrant == 0)
        value = toneQuarter[step];
    else if (quadrant == 1)
        value = toneQuarter[63 - step];
    else if (quadrant == 2)
        value = -toneQuarter[step];
    else
        value = -toneQuarter[63 - step];

    return value;
}

/*
 * How many frames the service has played.
 *
 * The service is told to play at AUDIO_RATE, and the console's clock says
 * how long it has been playing, so the position follows from the two. The
 * service's own clock is not exactly the console's clock, and the rate it
 * plays at is not exactly the rate that was asked for, so the rate it
 * really plays at is used here. See AUDIO_RATE_NUM above.
 */
static u64 readPosition(void)
{
    u64 elapsed = svcGetSystemTick() - playStart;
    u64 seconds = elapsed / SYSCLOCK_ARM11;
    u64 rest = elapsed % SYSCLOCK_ARM11;

    return seconds * AUDIO_RATE_NUM / AUDIO_RATE_DEN +
           (rest * AUDIO_RATE_NUM) / (AUDIO_RATE_DEN * (u64)SYSCLOCK_ARM11);
}

/*
 * Where in the buffer the sample for a position goes.
 *
 * The ring is the loop the service is given, and it sits at the end of the
 * buffer: the frames in front of it are the ones the service plays once
 * before it starts looping.
 */
static u64 bufferIndex(u64 position)
{
    return AUDIO_LOOP_OFFSET_FRAMES + (position % AUDIO_RING_FRAMES);
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

    /* The service reads the ring over the bus, so it has to be told that
     * the ring is empty and not holding whatever was in memory before. */
    for (int ear = 0; ear < AUDIO_EARS; ear++)
        CSND_FlushDataCache(ring[ear], AUDIO_BUFFER_FRAMES * sizeof(s16));

    /* The service has a switch that says whether the console is making
     * any sound at all. Nothing turns it on by itself: a channel can be
     * set up, given its samples and be playing perfectly well, and the
     * service will sit on it and say nothing.
     *
     * csndPlaySound() does not throw the switch. The upstream port had
     * the command written out and never called it, which is the most
     * likely reason nothing ever came out of the speaker. */
    CSND_SetPlayStateR(0, 1);
    csndExecCmds(true);

    /* The service plays the buffer from the start once and then loops
     * from the loop point onwards, so the ring is the loop and the frames
     * in front of it are the ones that are played first.
     *
     * Each ear gets its own channel, panned to its own side. The service
     * is given a volume and a pan and works out a volume for each speaker
     * from them, so the pan has to be all the way over for the ear to get
     * the whole of the signal.
     *
     * The channels are taken from the ones the service handed out, in
     * order, because the service decides which ones an application may
     * use and a channel it has not handed out cannot be played on. */
    int next = 0;
    for (int chn = AUDIO_CHANNEL_FIRST; chn <= AUDIO_CHANNEL_LAST && next < AUDIO_EARS; chn++) {
        if (!(csndChannels & (1u << chn)))
            continue;

        int ear = next;
        float pan = (ear == 0) ? AUDIO_PAN_LEFT : AUDIO_PAN_RIGHT;

        Result r = csndPlaySound(chn,
                                  SOUND_REPEAT | SOUND_FORMAT_16BIT | SOUND_ENABLE,
                                  AUDIO_RATE, 1.0f, pan,
                                  ring[ear], ring[ear] + AUDIO_LOOP_OFFSET_FRAMES,
                                  AUDIO_BUFFER_FRAMES * sizeof(s16));
        if (R_FAILED(r))
            continue;

        channel[ear] = chn;
        earVolume[ear] = CSND_VOL(1.0f, pan);
        next++;
    }

    if (next < AUDIO_EARS) {
        /* Only one channel. That is mono, and it is better than nothing:
         * the left ear is played and the right one is not. */
        if (next == 0) {
            audio_exit();
            return false;
        }

        channel[1] = -1;
    }

    /* What the service was given, so that a report of "there is no sound"
     * says something more than "there is no sound". The console is thirty
     * columns wide, so this is put on two lines that fit. */
    printf("sound: channels %08x\n", (unsigned)csndChannels);
    printf("playing on %d and %d\n", channel[0], channel[1]);

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

    bool started = false;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        if (ring[ear]) {
            if (channel[ear] >= 0) {
                CSND_SetPlayState(channel[ear], 0);
                started = true;
            }
            linearFree(ring[ear]);
            ring[ear] = NULL;
        }
        channel[ear] = -1;
    }

    /* The commands above are only written into the service's queue.
     * They are not sent until the queue is executed, and without this
     * the service keeps playing the ring that has just been freed.
     *
     * This has to happen even when the transport never started properly,
     * because a channel that was started and then not stopped goes on
     * playing a buffer that is no longer there. */
    if (started)
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

/*
 * Hand a block of samples to the ring.
 *
 * This is where the transport's bookkeeping happens: how far ahead of the
 * service the writer is, and what to do when the writer is too far ahead
 * or too far behind. The test tone goes through the same path, because
 * the point of it is to show what the transport does, not to go around it.
 */
static void transportWrite(const int16_t* left, const int16_t* right,
                           unsigned frames, bool tone)
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
            u64 index = bufferIndex(written);
            ring[0][index] = lastLeft;
            ring[1][index] = lastRight;
            written++;
        }

        /* The repeated samples are read over the bus like any others. */
        for (int ear = 0; ear < AUDIO_EARS; ear++)
            CSND_FlushDataCache(ring[ear] + bufferIndex(written - extra),
                                extra * sizeof(s16));

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

    if (tone) {
        /* The tone is made in the left ear's ring and copied to the right
         * one: both ears get the same signal and the channels are panned
         * to the sides. */
        s16* dst = ring[0] + AUDIO_LOOP_OFFSET_FRAMES + index;

        for (unsigned i = 0; i < first; i++)
            dst[i] = toneSample();
        if (first < frames) {
            s16* wrap = ring[0] + AUDIO_LOOP_OFFSET_FRAMES;
            for (unsigned i = 0; i < frames - first; i++)
                wrap[i] = toneSample();
        }

        memcpy(ring[1] + AUDIO_LOOP_OFFSET_FRAMES + index, dst,
               first * sizeof(s16));
        if (first < frames)
            memcpy(ring[1] + AUDIO_LOOP_OFFSET_FRAMES,
                   ring[0] + AUDIO_LOOP_OFFSET_FRAMES,
                   (frames - first) * sizeof(s16));
    }
    else {
        for (int ear = 0; ear < AUDIO_EARS; ear++) {
            const s16* src = ear ? right : left;
            s16* dst = ring[ear] + AUDIO_LOOP_OFFSET_FRAMES + index;

            memcpy(dst, src, first * sizeof(s16));
            if (first < frames)
                memcpy(ring[ear] + AUDIO_LOOP_OFFSET_FRAMES, src + first,
                       (frames - first) * sizeof(s16));
        }
    }

    /* The loudest sample in the block, so that a report of "there is no
     * sound" can say whether the APU made one. */
    int peak = 0;
    int peakLeft = 0;
    int peakRight = 0;
    u64 sumSquares = 0;

    if (tone) {
        /* The tone is at full scale, so there is nothing to measure. */
        peak = 30000;
        peakLeft = 30000;
        peakRight = 30000;
        /* A sine at full scale is at about minus three decibels, so its
         * level is about two thirds of full scale. */
        lastLevel = 65;
    }
    else {
        for (unsigned i = 0; i < frames; i++) {
            int l = left[i];
            int r = right[i];
            if (l < 0) l = -l;
            if (r < 0) r = -r;
            if (l > peakLeft) peakLeft = l;
            if (r > peakRight) peakRight = r;
            sumSquares += (u64)left[i] * (u64)left[i] +
                          (u64)right[i] * (u64)right[i];
        }

        peak = peakLeft > peakRight ? peakLeft : peakRight;

        /* How loud the block was, as a percentage of full scale. The root
         * is worked out without the maths library: the mean square of the
         * two ears is the square of the level, so the level is its root.
         */
        if (frames) {
            u64 mean = sumSquares / (frames * 2);
            u32 rms = integerRoot(mean);
            lastLevel = (int)((u64)rms * 100 / 32767);
        }
    }

    lastPeak = peak;
    lastPeakLeft = peakLeft;
    lastPeakRight = peakRight;

    lastLeft = left[frames - 1];
    lastRight = right[frames - 1];

    written += frames;
    statFrames += frames;

    /* The service reads the samples over the bus, so the cache has to be
     * told about them. Only the part that was written is flushed. */
    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        s16* dst = ring[ear] + AUDIO_LOOP_OFFSET_FRAMES + index;
        CSND_FlushDataCache(dst, first * sizeof(s16));
        if (first < frames)
            CSND_FlushDataCache(ring[ear] + AUDIO_LOOP_OFFSET_FRAMES,
                                (frames - first) * sizeof(s16));
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
    out->peak = lastPeak;
    out->peakLeft = lastPeakLeft;
    out->peakRight = lastPeakRight;
    out->level = lastLevel;
    out->channelMask = running ? csndChannels : 0;
    out->channelLeft = channel[0];
    out->channelRight = channel[1];
    out->testToneHz = testToneHz;

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

/*
 * What the service says about the channels.
 *
 * The transport knows what it asked for; this asks the service what it got.
 * A channel can be set up perfectly and still be sitting there doing
 * nothing, and the only way to tell the difference is to read the
 * service's own idea of the channel back.
 */
void audio_read_hardware(audio_hardware* out)
{
    if (!out)
        return;

    memset(out, 0, sizeof *out);

    if (!running)
        return;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        CSND_ChnInfo info;
        Result result;

        if (channel[ear] < 0)
            continue;

        result = csndGetState((u32)channel[ear], &info);
        if (R_FAILED(result))
            continue;

        out->readable = true;
        out->active[ear] = info.active;
        out->reg[ear][0] = info.value[0];
        out->reg[ear][1] = info.value[1];
        out->reg[ear][2] = info.value[2];
    }
}

/*
 * The test tone.
 *
 * Turning it on stops the game's own sound from being played: the tone is
 * the only signal in the ring. That is deliberate - the point of the tone
 * is to show what the transport and the service do on their own, and
 * mixing the game's sound into it would hide the answer.
 */
void audio_set_test_tone(int hz)
{
    if (hz < 0)
        hz = 0;

    testToneHz = hz;
    testTonePhase = 0;

    if (hz > 0) {
        /* The phase step per sample, in 1/65536 of a cycle. */
        testToneStep = (u32)((u64)hz * 65536 / AUDIO_RATE);
    }
    else {
        testToneStep = 0;
    }
}

int audio_test_tone_hz(void)
{
    return testToneHz;
}


/*
 * Hand a block of stereo samples to the transport.
 *
 * When the test tone is on it is what is played, and what the APU made is
 * not: there is one signal in the ring at a time.
 */
void audio_submit(const int16_t* left, const int16_t* right, unsigned frames)
{
    transportWrite(left, right, frames, testToneHz > 0);
    submitCount++;
}

/*
 * Keep the test tone going when there is nothing else to play it.
 *
 * The tone is written with the samples the emulator hands over, so with no
 * game running - or with the game paused - there would be nothing to write
 * it with and the tone would stop after the ring had been played out. The
 * port calls this every frame so that the tone is there to be heard even
 * with nothing else going on.
 */
void audio_pump_test_tone(void)
{
    if (!running || testToneHz <= 0)
        return;

    /* If the emulator has already handed over its samples this frame, the
     * tone went into the ring with them. */
    if (submitCount != pumpedCount)
        return;

    pumpedCount = submitCount;
    transportWrite(NULL, NULL, AUDIO_FRAMES_PER_GB_FRAME, true);
}
