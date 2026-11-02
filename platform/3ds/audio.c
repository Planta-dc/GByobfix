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
 * This is the original port's design:
 *
 *   - each ear has two buffers;
 *   - the service plays one of them while the other is being filled;
 *   - when the one being filled is full, it is handed over to be played,
 *     and the one that was being played is filled instead.
 *
 * The buffer the service is reading is never the one being written to, so
 * the samples that come out are the samples that went in, in one piece.
 * A transport with one buffer per ear cannot do that: it would be
 * writing over the samples the service is reading, and what came out
 * would be a mixture of two frames of sound.
 *
 * Each buffer has a channel of its own, so the address the service is
 * given for a buffer never changes. The channel that is handed a buffer
 * is the one that played it a block ago, so it has been idle since and
 * the service is not asked to stop something in the middle of it.
 *
 * Nothing here is worked out from the clock, so nothing here can drift.
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

/* The rate and the block size are in audio.h, so that the rest of the
 * port can ask the transport what it is doing. */

/* The two ears of the Game Boy's stereo output. */
#define AUDIO_EARS 2

/* The buffers each ear has: the one being played and the one being
 * filled. */
#define AUDIO_BUFFERS 2

/* The pan each ear's channel is played at: all the way over. */
#define AUDIO_PAN_LEFT (-1.0f)
#define AUDIO_PAN_RIGHT (1.0f)

/*
 * The size of a buffer, in frames.
 *
 * The original used eight, which is 134 ms of delay between the game and
 * the speaker - the sound would be a seventh of a second behind the
 * picture. Two frames is 33 ms: long enough that the service is not
 * being restarted every frame, short enough that the sound is with the
 * picture.
 */
#define AUDIO_BLOCK_FRAMES 2

/* The samples in a buffer. */
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
 * recorded sound, and the ones from eight up are the tone and noise
 * channels, which are not what these samples are. */
#define AUDIO_CHANNEL_FIRST 0
#define AUDIO_CHANNEL_LAST 7

/* The volume each ear's channel is played at, so that muting and
 * unmuting can put back exactly what was there before. */
static u32 earVolume[AUDIO_EARS];

/* The buffers, and the channel each one is played on. A channel that is
 * -1 is not used. */
static s16* buffer[AUDIO_EARS][AUDIO_BUFFERS];
static int channel[AUDIO_EARS][AUDIO_BUFFERS];

/* Which buffer is being filled, and how far into it the samples have
 * gone. */
static int fillBuffer[AUDIO_EARS];
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

/* Whether there is only one channel for both ears, in which case the two
 * ears are mixed into it, and which ear's buffers that channel plays. */
static bool mono;
static int primaryEar = 0;

/* Frames the emulator has handed over. */
static unsigned long long statFrames;

/* The buffers that have been given to the service. */
static unsigned long long statBlocks;

/*
 * The flags for a channel: sixteen bit samples, played once, on this
 * channel, at the sample rate.
 *
 * The repeat mode is in bits eleven and ten: two is one shot, which plays
 * the buffer and stops. That is what the original port did, and it is
 * what makes the buffers follow one another.
 */
static u32 channelFlags(int chn)
{
    return SOUND_FORMAT_16BIT | SOUND_ONE_SHOT | SOUND_ENABLE |
           SOUND_CHANNEL(chn) | (blockTimer << 16);
}

/*
 * Give the service a buffer to play.
 *
 * The command is written into the service's queue and sent when the queue
 * is executed. The queue is not waited for: the buffer is 33 ms long and
 * it is given to the service 33 ms before the one before it ends, so
 * there is plenty of time, and waiting would stall the frame.
 */
static void playBuffer(int ear, int buf)
{
    int chn = channel[ear][buf];

    if (chn < 0)
        return;

    u32 address = (u32)osConvertVirtToPhys(buffer[ear][buf]);

    CSND_SetChnRegs(channelFlags(chn),
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
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
            buffer[ear][buf] = linearAlloc(AUDIO_BLOCK_SAMPLES * sizeof(s16));
            if (!buffer[ear][buf]) {
                audio_exit();
                return false;
            }
            memset(buffer[ear][buf], 0, AUDIO_BLOCK_SAMPLES * sizeof(s16));
            channel[ear][buf] = -1;
        }
        fillBuffer[ear] = 0;
        fillPos[ear] = 0;
    }

    /* The service reads the buffers over the bus, so it has to be told
     * that they are empty and not holding whatever was in memory
     * before. */
    for (int ear = 0; ear < AUDIO_EARS; ear++)
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++)
            CSND_FlushDataCache(buffer[ear][buf], AUDIO_BLOCK_SAMPLES * sizeof(s16));

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

    /* The channels the service handed out, in order. The service decides
     * which ones an application may use, and a channel it has not handed
     * out cannot be played on, so they are asked for rather than
     * assumed. */
    int available[AUDIO_EARS * AUDIO_BUFFERS];
    int count = 0;

    for (int chn = AUDIO_CHANNEL_FIRST;
         chn <= AUDIO_CHANNEL_LAST &&
         count < (int)(sizeof available / sizeof available[0]);
         chn++) {
        if (csndChannels & (1u << chn))
            available[count++] = chn;
    }

    if (count == 0) {
        /* The service handed out no channels at all. There is nothing to
         * play on. */
        audio_exit();
        return false;
    }

    /* Each ear's first buffer gets a channel of its own, then each ear's
     * second buffer. Each buffer wants a channel of its own, so that the
     * address the service is given for a buffer never changes. */
    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
            int index = ear * AUDIO_BUFFERS + buf;

            channel[ear][buf] = (index < count) ? available[index] : -1;
        }
    }

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        float pan = (ear == 0) ? AUDIO_PAN_LEFT : AUDIO_PAN_RIGHT;

        earVolume[ear] = CSND_VOL(1.0f, pan);
    }

    /* An ear with one channel plays both of its buffers on it: the
     * address changes from one buffer to the next, and the service reads
     * the address again when the channel is told to play. */
    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        if (channel[ear][1] < 0)
            channel[ear][1] = channel[ear][0];
    }

    /* An ear with no channel at all is played on the other ear's
     * channel, and the two ears are mixed into it. That is worse than
     * stereo, but it is better than silence. */
    if (channel[0][0] < 0 || channel[1][0] < 0) {
        mono = true;

        if (channel[0][0] >= 0)
            primaryEar = 0;
        else
            primaryEar = 1;
    }

    /* The channels are set up to play their first buffer, which is empty
     * until the emulator has produced something. */
    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
            if (channel[ear][buf] < 0)
                continue;

            playBuffer(ear, buf);

            /* Command one: this starts the channel playing. The command
             * above sets the channel's registers and the switch turns the
             * sound on, but this is the command that says "play", and
             * there is no harm in saying it. */
            CSND_SetPlayState(channel[ear][buf], 1);
        }
    }

    csndExecCmds(true);

    /* What the service was given, so that a report of "there is no sound"
     * says something more than "there is no sound". The console is thirty
     * columns wide, so this is put on two lines that fit. */
    printf("sound: channels %08x\n", (unsigned)csndChannels);
    printf("playing on %d %d %d %d\n", channel[0][0], channel[0][1],
           channel[1][0], channel[1][1]);

    statFrames = 0;
    statBlocks = 0;
    submitted = false;

    running = true;
    return true;
}

void audio_exit(void)
{
    bool started = false;

    /* The channels are told to stop first.
     *
     * Command one: this stops the channel playing. It is not the same as
     * the switch above, which stops everything and throws the channel's
     * registers away. */
    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
            if (buffer[ear][buf] && channel[ear][buf] >= 0) {
                CSND_SetPlayState(channel[ear][buf], 0);
                started = true;
            }
        }
    }

    /* The commands above are only written into the service's queue. They
     * are not sent until the queue is executed, and until then the
     * service is still playing the buffer that is about to be given back.
     * So the queue is executed and waited for before the memory goes
     * away. */
    if (started)
        csndExecCmds(true);

    for (int ear = 0; ear < AUDIO_EARS; ear++) {
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
            if (buffer[ear][buf]) {
                linearFree(buffer[ear][buf]);
                buffer[ear][buf] = NULL;
            }
            channel[ear][buf] = -1;
        }
    }

    running = false;
    mono = false;
    primaryEar = 0;
}

void audio_set_muted(bool newMuted)
{
    if (newMuted == muted)
        return;

    muted = newMuted;

    if (running) {
        for (int ear = 0; ear < AUDIO_EARS; ear++) {
            for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
                if (channel[ear][buf] < 0)
                    continue;

                /* Command nine: this changes the volume without touching
                 * where the channel is playing, so the buffer that is
                 * playing now goes quiet as well. */
                CSND_SetVol(channel[ear][buf], muted ? 0 : earVolume[ear], 0);
            }
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
 * The samples go into the buffer that is being filled, made louder on the
 * way. When the buffer is full it is handed to the service to play, and
 * the buffer that was being played is filled instead.
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

        if (channel[ear][0] < 0)
            continue;

        if (mono) {
            /* One channel for both ears: the two ears are mixed into
             * it, and only the ear that has the channel runs its loop,
             * so that the buffer is written once and not twice. */
            if (ear != primaryEar)
                continue;

            src = NULL;
        }
        else {
            src = ear ? right : left;
        }

        while (done < frames) {
            unsigned space = AUDIO_BLOCK_SAMPLES - fillPos[ear];
            unsigned chunk = (frames - done < space) ? frames - done : space;
            s16* dst = buffer[ear][fillBuffer[ear]] + fillPos[ear];
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

            if (mono) {
                /* With one channel for both ears, the same samples come
                 * out of both. */
                if (peak > peakLeft)
                    peakLeft = peak;
                if (peak > peakRight)
                    peakRight = peak;
            }
            else if (ear == 0) {
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
                int filled = fillBuffer[ear];

                fillPos[ear] = 0;

                /* The buffer that was just filled is handed to the
                 * service, and the buffer that was being played is
                 * filled instead. The service is reading one and the
                 * emulator is writing the other, so nothing is written
                 * over what is being read. */
                playBuffer(ear, filled);
                CSND_FlushDataCache(buffer[ear][filled],
                                    AUDIO_BLOCK_SAMPLES * sizeof(s16));

                fillBuffer[ear] = 1 - filled;

                if (ear == 0)
                    statBlocks++;
                send = true;
            }
        }
    }

    /* The commands are sent once, for both ears, and the service is not
     * waited for: it has a whole buffer's worth of time before the next
     * one is due. */
    if (send)
        csndExecCmds(false);

    /* How loud the block was, as a percentage of full scale. The root is
     * worked out without the maths library: the mean square of the
     * samples is the square of the level, so the level is its root. */
    if (frames) {
        u64 count = (u64)frames * (mono ? 1 : 2);
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
    out->channelLeft = channel[0][0];
    out->channelRight = channel[1][0];
    out->channelLeftAlt = channel[0][1];
    out->channelRightAlt = channel[1][1];
    out->mono = mono;
    out->channelsUsed = 0;
    for (int ear = 0; ear < AUDIO_EARS; ear++)
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++)
            if (channel[ear][buf] >= 0)
                out->channelsUsed |= 1u << channel[ear][buf];
    out->testToneHz = testToneHz;
    out->gain = AUDIO_GAIN;

    /* The size of a buffer is how far the sound is behind the picture:
     * the emulator fills a buffer and the service plays it afterwards. */
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
        for (int buf = 0; buf < AUDIO_BUFFERS; buf++) {
            CSND_ChnInfo info;
            Result result;

            if (channel[ear][buf] < 0)
                continue;

            result = csndGetState((u32)channel[ear][buf], &info);
            if (R_FAILED(result))
                continue;

            out->readable = true;
            out->active[ear][buf] = info.active;
            out->reg[ear][buf][0] = info.value[0];
            out->reg[ear][buf][1] = info.value[1];
            out->reg[ear][buf][2] = info.value[2];
        }
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
