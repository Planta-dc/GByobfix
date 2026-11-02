/*
 * The sound transport: GameYob's APU to the 3DS sound service.
 *
 * What this is, and what it is not
 * -------------------------------
 * This is a routing and timing layer. It takes the samples GameYob's own
 * APU produces and hands them to the sound service at the rate the service
 * expects, so that what comes out of the speaker is what the APU computed
 * and nothing is lost on the way.
 *
 * It is not a sample accurate emulation of the Game Boy's sound hardware.
 * The APU that produces the samples is GameYob's, unchanged; the part that
 * is played is still a simplification of the real thing (the duty cycle
 * edges are quantised to the sampling interval, for instance). This file
 * does not make any of that better, and it does not pretend to: it moves
 * samples from one place to another without losing them.
 *
 * The design
 * ----------
 * This is the original port's design, which is simpler than the ring that
 * was here before it and does not depend on the clock:
 *
 *   - the sound is handed over in blocks, one block for each ear;
 *   - when a block is full it is given to the service to play, and the
 *     service is told to play it once;
 *   - the next block is filled while that one is playing, and is given
 *     to the service when it is full.
 *
 * The blocks follow one another without a gap: the sample after the last
 * one of a block is the first one of the next, because the emulator
 * produced them in that order. So the sound the service plays is the
 * sound the emulator made, in one piece.
 *
 * The ring that was here before worked out where the service was in its
 * buffer from the console's clock, which is an assumption about the
 * service's rate. If the assumption is wrong the writer and the service
 * drift apart, and the sound goes to pieces in a way that is very hard
 * to see. Nothing here is timed from the clock, so there is nothing to
 * drift.
 *
 * Two things the original did that are not done here, because they are
 * why the original made no sound at all:
 *
 *   - it never told the service that the console was making a sound.
 *     The command was written out and never called it. The service will
 *     sit on a channel that is set up perfectly and say nothing;
 *   - it played its samples on channels eight and nine, which are the
 *     tone and noise channels. The samples go on the channels below
 *     eight, which are the ones that play recorded sound.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds/allocator/linear.h>
#include <3ds/os.h>
#include <3ds/services/csnd.h>

#include "audio.h"

/*
 * The rate everything below runs at.
 *
 * GameYob's APU is sampled every 84 Game Boy cycles, which is 49932.57 Hz.
 * The sound service takes an integer rate, and 49932 is the closest one to
 * it. The difference is 11 parts per million, which is under half a
 * sample over a block.
 */
#define AUDIO_RATE 49932u

/* Samples the APU produces in one Game Boy frame: 70224 cycles / 84. */
#define AUDIO_FRAMES_PER_GB_FRAME 836

/* The two ears of the Game Boy's stereo output. */
#define AUDIO_EARS 2

/* The pan each ear's channel is played at: all the way over. */
#define AUDIO_PAN_LEFT (-1.0f)
#define AUDIO_PAN_RIGHT (1.0f)

/*
 * The size of a block, in frames.
 *
 * The original used eight, which is 134 ms of delay between the game and
 * the speaker - the sound would be a seventh of a second behind the
 * picture. Two frames is 33 ms: long enough that the service is not
 * being restarted every frame, short enough that the sound is with the
 * picture.
 */
#define AUDIO_BLOCK_FRAMES 2

/* The samples in a block. */
#define AUDIO_BLOCK_SAMPLES (AUDIO_FRAMES_PER_GB_FRAME * AUDIO_BLOCK_FRAMES)

/*
 * The gain.
 *
 * The APU's own output is small: its loudest sample is about 1680, which
 * is a twentieth of full scale, and at that level nothing comes out of a
 * 3DS's speaker. So the samples are made louder on the way to the
 * service.
 *
 * The APU is not changed: this is the transport's gain, and the APU
 * computes exactly what it always computed. Eight times puts the loudest
 * sample at about two fifths of full scale, which is loud enough to hear
 * and quiet enough not to hurt.
 */
#define AUDIO_GAIN 8

/* The channels to try. The service hands out which channels an
 * application may use; the ones below eight are the ones that play
 * recorded sound, and the ones above are the tone and noise channels. */
#define AUDIO_CHANNEL_FIRST 0
#define AUDIO_CHANNEL_LAST 16

/* The volume each ear's channel is played at, so that muting and
 * unmuting can put back exactly what was there before. */
static u32 earVolume[AUDIO_EARS];

/* The block each ear is being filled, and how far into it the samples
 * have gone. */
static s16* block[AUDIO_EARS];
static int channel[AUDIO_EARS];
static unsigned fillPos[AUDIO_EARS];

/* The sample rate, as the service wants it: a timer value, not a rate. */
static u32 blockTimer;

/* Whether the emulator has handed samples over since the last time the
 * test tone was pumped. */
static bool submitted;

/* The loudest sample handed over in the last block, and how loud the last
 * block was as a percentage of full scale. The peak on its own says
 * whether there was a sound; the level says how much of one there was,
 * which is the difference between a quiet game and a silent one. */
static int lastPeak;
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

static bool running;
static bool muted;

/* Frames the emulator has handed over. */
static unsigned long long statFrames;

/* The blocks that have been given to the service. */
static unsigned long long statBlocks;

/*
 * The flags for the channel: sixteen bit samples, played once, on this
 * channel, at the sample rate.
 *
 * The repeat mode is in bits eleven and ten: two is one shot, which plays
 * the block and stops. That is what the original port did, and it is what
 * makes the blocks follow one another.
 */
static u32 channelFlags(int chn)
{
    return SOUND_FORMAT_16BIT | SOUND_ONE_SHOT | SOUND_ENABLE |
           SOUND_CHANNEL(chn) | (blockTimer << 16);
}

/*
 * Give the service a block to play.
 *
 * The command is written into the service's queue and sent when the queue
 * is executed. The queue is not waited for: the block is 33 ms long and
 * it is given to the service 33 ms before the one before it ends, so
 * there is plenty of time, and waiting would stall the frame.
 */
static void playBlock(int ear)
{
    if (channel[ear] < 0)
        return;

    u32 address = (u32)osConvertVirtToPhys(block[ear]);

    CSND_SetChnRegs(channelFlags(channel[ear]),
                     address, address,
                     AUDIO_BLOCK_SAMPLES * sizeof(s16),
                     earVolume[ear], 0);
}

bool audio_init(void)
{
    if (running)
        return true;

    if (csndInit() != 0)
        return false;

    blockTimer = CSND_TIMER(AUDIO_RATE);
    if (blockTimer < 0x42)
        blockTimer = 0x42;
    else if (blockTimer > 0xFFFF)
        blockTimer = 0xFFFF;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        block[ear] = linearAlloc(AUDIO_BLOCK_SAMPLES * sizeof(s16));
        if (!block[ear]) {
            audio_exit();
            return false;
        }
        memset(block[ear], 0, AUDIO_BLOCK_SAMPLES * sizeof(s16));
        channel[ear] = -1;
        fillPos[ear] = 0;
    }

    /* The service reads the block over the bus, so it has to be told that
     * it is empty and not holding whatever was in memory before. */
    for (int ear = 0; ear < AUDIO_EARS; ear++)
        CSND_FlushDataCache(block[ear], AUDIO_BLOCK_SAMPLES * sizeof(s16));

    /* The service has a switch that says whether the console is making
     * any sound at all. Nothing turns it on by itself: a channel can be
     * set up, given its samples and be playing perfectly well, and the
     * service will sit on it and say nothing.
     *
     * The original port had this command written out and never called it,
     * which is the most likely reason nothing ever came out of the
     * speaker. */
    CSND_SetPlayStateR(0, 1);
    csndExecCmds(true);

    /* The channels are taken from the ones the service handed out, in
     * order, because the service decides which ones an application may
     * use and a channel it has not handed out cannot be played on. One
     * for each ear, panned to its own side. */
    int next = 0;
    for (int chn = AUDIO_CHANNEL_FIRST; chn <= AUDIO_CHANNEL_LAST && next < AUDIO_EARS; chn++) {
        if (!(csndChannels & (1u << chn)))
            continue;

        int ear = next;
        float pan = (ear == 0) ? AUDIO_PAN_LEFT : AUDIO_PAN_RIGHT;

        channel[ear] = chn;
        earVolume[ear] = CSND_VOL(1.0f, pan);
        next++;
    }

    if (next == 0) {
        /* The service handed out no channels at all. There is nothing to
         * play on. */
        audio_exit();
        return false;
    }

    if (next < AUDIO_EARS) {
        /* Only one channel. That is mono, and it is better than nothing:
         * the two ears are mixed into it. */
        channel[1] = -1;
    }

    /* The channels are set up to play the first block, which is empty
     * until the emulator has produced something. */
    for (int ear = 0; ear < AUDIO_EARS; ear++)
        playBlock(ear);

    csndExecCmds(true);

    /* What the service was given, so that a report of "there is no sound"
     * says something more than "there is no sound". The console is thirty
     * columns wide, so this is put on two lines that fit. */
    printf("sound: channels %08x\n", (unsigned)csndChannels);
    printf("playing on %d and %d\n", channel[0], channel[1]);

    statFrames = 0;
    statBlocks = 0;
    submitted = false;

    running = true;
    return true;
}

void audio_exit(void)
{
    bool started = false;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        if (block[ear]) {
            if (channel[ear] >= 0) {
                /* Command one: this stops the channel playing. It is not
                 * the same as the switch above, which stops everything
                 * and throws the channel's registers away. */
                CSND_SetPlayState(channel[ear], 0);
                started = true;
            }
            linearFree(block[ear]);
            block[ear] = NULL;
        }
        channel[ear] = -1;
    }

    /* The commands above are only written into the service's queue.
     * They are not sent until the queue is executed, and without this
     * the service keeps playing the block that has just been freed. */
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

            float pan = (ear == 0) ? AUDIO_PAN_LEFT : AUDIO_PAN_RIGHT;
            earVolume[ear] = muted ? 0 : CSND_VOL(1.0f, pan);

            /* Command nine: this changes the volume without touching
             * where the channel is playing, so the block that is playing
             * now goes quiet as well. */
            CSND_SetVol(channel[ear], earVolume[ear], 0);
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
 * Hand a block of stereo samples to the transport.
 *
 * The samples go into the block that is being filled, made louder on the
 * way. When the block is full it is given to the service to play and the
 * next one is started.
 */
static void transportWrite(const int16_t* left, const int16_t* right, unsigned frames)
{
    if (!running)
        return;

    int peakLeft = 0;
    int peakRight = 0;
    u64 sumSquares = 0;
    bool send = false;

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        const int16_t* src;
        unsigned done = 0;

        if (channel[ear] < 0) {
            /* One channel only: the two ears are mixed into it. */
            if (ear != 0)
                continue;
            src = NULL;
        }
        else {
            src = ear ? right : left;
        }

        while (done < frames) {
            unsigned space = AUDIO_BLOCK_SAMPLES - fillPos[ear];
            unsigned chunk = (frames - done < space) ? frames - done : space;
            s16* dst = block[ear] + fillPos[ear];
            int peak = 0;

            if (testToneHz > 0) {
                /* The tone is already at full scale, so it is not made
                 * louder. */
                for (unsigned i = 0; i < chunk; i++)
                    dst[i] = toneSample();
            }
            else if (src) {
                for (unsigned i = 0; i < chunk; i++) {
                    int sample = (int)src[done + i] * AUDIO_GAIN;

                    /* The gain can take a sample past the end of the
                     * scale, and a sample that is past the end is a
                     * click. */
                    if (sample > 32767)
                        sample = 32767;
                    else if (sample < -32768)
                        sample = -32768;

                    dst[i] = (s16)sample;
                }
            }
            else {
                /* Mono: the two ears mixed together. */
                for (unsigned i = 0; i < chunk; i++) {
                    int sample = ((int)left[done + i] + (int)right[done + i]) / 2 * AUDIO_GAIN;

                    if (sample > 32767)
                        sample = 32767;
                    else if (sample < -32768)
                        sample = -32768;

                    dst[i] = (s16)sample;
                }
            }

            /* What is going to the speaker, measured on the way out. */
            for (unsigned i = 0; i < chunk; i++) {
                int value = dst[i];

                if (value < 0)
                    value = -value;
                if (value > peak)
                    peak = value;

                sumSquares += (u64)(unsigned)value * (u64)(unsigned)value;
            }

            if (ear == 0) {
                if (peak > peakLeft)
                    peakLeft = peak;
            }
            else {
                if (peak > peakRight)
                    peakRight = peak;
            }

            fillPos[ear] += chunk;
            done += chunk;

            if (fillPos[ear] >= AUDIO_BLOCK_SAMPLES) {
                fillPos[ear] = 0;

                /* The block is full: the service is told to play it, and
                 * the samples are on the card's way to the hardware. */
                playBlock(ear);
                CSND_FlushDataCache(block[ear],
                                    AUDIO_BLOCK_SAMPLES * sizeof(s16));
                if (ear == 0)
                    statBlocks++;
                send = true;
            }
        }
    }

    /* The commands are sent once, for both ears, and the service is not
     * waited for: it has a whole block's worth of time before the next
     * one is due. */
    if (send)
        csndExecCmds(false);

    /* How loud the block was, as a percentage of full scale. The root is
     * worked out without the maths library: the mean square of the
     * samples is the square of the level, so the level is its root. */
    if (frames) {
        u64 count = (u64)frames * (channel[1] >= 0 ? 2 : 1);
        u32 rms = integerRoot(count ? sumSquares / count : 0);
        lastLevel = (int)((u64)rms * 100 / 32767);
    }

    lastPeakLeft = peakLeft;
    lastPeakRight = peakRight;
    lastPeak = peakLeft > peakRight ? peakLeft : peakRight;

    statFrames += frames;
}

/*
 * The emulator's samples, handed to the transport.
 *
 * This is the way the game gets its sound out, and it is also how the
 * transport knows that the emulator is producing something: the test tone
 * is only pumped when it is not.
 */
void audio_submit(const int16_t* left, const int16_t* right, unsigned frames)
{
    submitted = true;
    transportWrite(left, right, frames);
}

void audio_get_stats(audio_stats* out)
{
    if (!out)
        return;

    out->running = running;
    out->frames = statFrames;
    out->blocks = statBlocks;
    out->peak = lastPeak;
    out->peakLeft = lastPeakLeft;
    out->peakRight = lastPeakRight;
    out->level = lastLevel;
    out->channelMask = running ? csndChannels : 0;
    out->channelLeft = channel[0];
    out->channelRight = channel[1];
    out->testToneHz = testToneHz;
    out->gain = AUDIO_GAIN;

    /* The size of a block is how far the sound is behind the picture:
     * the emulator fills a block and the service plays it afterwards. */
    out->blockFrames = AUDIO_BLOCK_SAMPLES;
    out->latencyMs = (int)((u64)AUDIO_BLOCK_SAMPLES * 1000 / AUDIO_RATE);
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
 * the only signal in the transport. That is deliberate - the point of the
 * tone is to show what the transport and the service do on their own, and
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
 * Keep the test tone going.
 *
 * The tone is written with the samples the emulator hands over, so with no
 * game running, or with the game paused, there would be nothing to write
 * it with. The port calls this every frame so that the tone is there to
 * be heard on its own.
 *
 * When the emulator is handing samples over the tone is being written
 * already, and this does nothing: writing it twice would play it at twice
 * the speed.
 */
void audio_pump_test_tone(void)
{
    static s16 silence[AUDIO_FRAMES_PER_GB_FRAME];

    if (!running || testToneHz <= 0)
        return;

    if (submitted) {
        /* The emulator handed samples over this frame, so the tone was
         * written along with them. */
        submitted = false;
        return;
    }

    /* The tone is written by the transport itself, so the samples the
     * emulator hands over are not used: the tone is the only signal. */
    transportWrite(silence, silence, AUDIO_FRAMES_PER_GB_FRAME);
}
