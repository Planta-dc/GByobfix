/*
 * Does the APU make a sound, and does it come out of the right ear?
 *
 * The APU was the one part of the port that no test had ever run. The
 * renderer is compared against the renderer it replaced, the blit is
 * checked against where the picture is supposed to land, the frame clock
 * is checked against a clock that does not lie, and the transport is
 * checked against a clock that does not lie either - but the samples
 * going into the transport were never looked at. A transport that is
 * fed silence passes every one of those tests.
 *
 * So this test runs the APU: it writes the sound registers the way a
 * game would, runs a frame of it, and then looks at the samples that
 * came out and at the samples the transport handed to the sound service.
 *
 * The APU is GameYob's own. Nothing here changes it.
 */

#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gameboy.h"
#include "soundengine.h"
#include "audio.h"

/* What the shim recorded about the sound service. */
extern void* hostCsndData[32];
extern void* hostCsndLoop[32];
extern u32 hostCsndSize[32];
extern u32 hostCsndRate[32];
extern float hostCsndVol[32];
extern float hostCsndPan[32];
extern int hostCsndCalls;
extern int hostCsndChn[8];
extern int hostCsndChnCount;
extern u32 hostCsndVolSet[32];
extern int hostCsndVolSetCount;
extern int hostCsndExecCount;

/* The console's clock, which the test drives. */
extern u64 hostTick;

#define GB_CYCLES_PER_FRAME 70224
#define SAMPLES_PER_FRAME (GB_CYCLES_PER_FRAME / 0x54)

/* The console's clock, in ticks, for one frame of a Game Boy. */
#define FRAME_TICKS 4488918ULL

/* How many frames to run. The ring the transport loops over holds 8192
 * frames and a Game Boy frame is 836 samples, so this is enough for the
 * ring to be filled all the way round. */
#define FRAMES_TO_RUN 12

/* The loudest sample in a run. */
static int peakOf(const s16* samples, int count)
{
    int peak = 0;
    for (int i = 0; i < count; i++) {
        int v = samples[i] < 0 ? -samples[i] : samples[i];
        if (v > peak)
            peak = v;
    }
    return peak;
}

/*
 * A game writes the sound registers like this: the master switch on,
 * the two output terminals at full volume, the channels routed to the
 * terminals, and then a channel triggered with a frequency.
 *
 * The trigger is a write to NR14, and a write is not the same thing as
 * a value sitting in memory: the emulator calls the sound engine with
 * the value that was written, and that is what starts the channel.
 * Leaving the bit set in memory does nothing, because the sound engine
 * reads the registers back through readIO, which hands back a masked
 * value with the trigger bit clear.
 *
 * routing says which terminals the channel goes to: bit 0 is the first
 * terminal and bit 4 is the second.
 */
static void programSound(Gameboy* gb, SoundEngine* se, int volume, u8 routing)
{
    /* NR52: the sound is on, and the channel is enabled. */
    gb->ioRam[0x26] = 0x80 | 0x01;
    /* NR50: both terminals at the loudest they go. */
    gb->ioRam[0x24] = 0x77;
    /* NR51: which terminals the channel goes to. */
    gb->ioRam[0x25] = routing;

    /* NR11: the duty cycle, half a square wave. */
    gb->ioRam[0x11] = 0x80;
    /* NR12: the volume. */
    gb->ioRam[0x12] = (u8)(volume << 4);
    /* NR13: the low byte of the frequency. */
    gb->ioRam[0x13] = 0x00;

    se->init();

    /* NR14: the high byte of the frequency, and the bit that starts the
     * channel. This is the write the emulator would make. */
    gb->ioRam[0x14] = 0x80;
    se->handleSoundRegister(0x14, 0x80);
}

/* Run a number of frames of the APU and hand the samples to the
 * transport, the way the emulator's frame does. */
static void runFrames(SoundEngine* se, int frames)
{
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < SAMPLES_PER_FRAME; i++)
            se->updateSound(0x54);

        se->soundUpdateVBlank();

        /* The console's clock moves on by a frame. */
        hostTick += FRAME_TICKS;
    }
}

int main()
{
    int failures = 0;

    Gameboy gb;
    SoundEngine se(&gb);

    printf("The APU, and the samples it produces.\n\n");

    /* ---------------------------------------------------------------- */
    printf("A channel that is switched on makes a sound.\n");

    /* The channel goes to both terminals, so both ears get it. */
    programSound(&gb, &se, 15, 0x11);

    if (!audio_init()) {
        printf("FAIL: the transport would not start.\n");
        return 1;
    }

    runFrames(&se, FRAMES_TO_RUN);

    if (hostCsndCalls < 2) {
        printf("FAIL: the sound service was given %d buffers, not one for each ear.\n",
               hostCsndCalls);
        failures++;
    }
    else {
        printf("  The service was given %d buffers, one for each ear.\n",
               hostCsndCalls);
        for (int ear = 0; ear < 2; ear++) {
            int chn = hostCsndChn[ear];
            printf("  Ear %d: channel %d, rate %u Hz, volume %.2f, pan %.2f, %u bytes.\n",
                   ear, chn, hostCsndRate[chn], hostCsndVol[chn], hostCsndPan[chn],
                   hostCsndSize[chn]);
        }
    }

    const s16* left = (const s16*)hostCsndData[hostCsndChn[0]];
    const s16* right = (const s16*)hostCsndData[hostCsndChn[1]];

    if (!left || !right) {
        printf("FAIL: the service was never given a buffer to play.\n");
        return 1;
    }

    /* The first frames are played once and the rest loop, so the loop
     * starts two frames in: the service reads the samples over the bus
     * and the address it is given for the loop has to be a multiple of
     * four bytes. */
    int loopFrames = (int)(hostCsndSize[hostCsndChn[0]] / sizeof(s16)) - 2;

    int leftPeak = peakOf(left + 2, loopFrames);
    int rightPeak = peakOf(right + 2, loopFrames);

    printf("  Left ear:  peak %d.\n", leftPeak);
    printf("  Right ear: peak %d.\n", rightPeak);

    if (leftPeak == 0 || rightPeak == 0) {
        printf("FAIL: the APU produced silence. The sound service would have\n"
               "      nothing to play.\n");
        failures++;
    }
    else {
        printf("  The APU made a sound.\n");
    }

    /* ---------------------------------------------------------------- */
    printf("\nThe sound is loud enough to hear.\n");

    if (leftPeak < 1000 || rightPeak < 1000) {
        printf("FAIL: the loudest sample is %d. That is too quiet to hear.\n",
               leftPeak > rightPeak ? leftPeak : rightPeak);
        failures++;
    }
    else {
        printf("  The loudest sample is %d, which is %d%% of full scale.\n",
               leftPeak > rightPeak ? leftPeak : rightPeak,
               (leftPeak > rightPeak ? leftPeak : rightPeak) * 100 / 32767);
    }

    /* ---------------------------------------------------------------- */
    printf("\nEach ear is played on its own side.\n");

    /* A pan of zero is not "straight ahead": the service gives each
     * speaker half of the volume, so both ears hear a mixture of the
     * two channels at half the volume. */
    for (int ear = 0; ear < 2; ear++) {
        int chn = hostCsndChn[ear];
        float pan = hostCsndPan[chn];

        if (pan > -0.9f && pan < 0.9f) {
            printf("FAIL: ear %d is played with a pan of %.2f, so it comes out\n"
                   "      of both speakers at half the volume.\n", ear, pan);
            failures++;
        }
    }

    if (hostCsndPan[hostCsndChn[0]] >= 0.0f) {
        printf("FAIL: the left ear is panned to the right.\n");
        failures++;
    }
    if (hostCsndPan[hostCsndChn[1]] <= 0.0f) {
        printf("FAIL: the right ear is panned to the left.\n");
        failures++;
    }

    if (hostCsndVol[hostCsndChn[0]] != 1.0f ||
        hostCsndVol[hostCsndChn[1]] != 1.0f) {
        printf("FAIL: the channels are not played at full volume.\n");
        failures++;
    }

    if (!failures) {
        printf("  The left ear is on the left and the right ear is on the right,\n"
               "  each at the full volume.\n");
    }

    /* ---------------------------------------------------------------- */
    printf("\nA channel that goes to one terminal only comes out of one ear.\n");

    {
        Gameboy one;
        SoundEngine se2(&one);
        /* To the first terminal only. */
        programSound(&one, &se2, 15, 0x01);
        runFrames(&se2, FRAMES_TO_RUN);

        int l = peakOf(left + 2, loopFrames);
        int r = peakOf(right + 2, loopFrames);
        printf("  To the first terminal: left %d, right %d.\n", l, r);

        if (l == 0 || r != 0) {
            printf("FAIL: a channel on the first terminal should be on the left only.\n");
            failures++;
        }
    }

    {
        Gameboy two;
        SoundEngine se3(&two);
        /* To the second terminal only. */
        programSound(&two, &se3, 15, 0x10);
        runFrames(&se3, FRAMES_TO_RUN);

        int l = peakOf(left + 2, loopFrames);
        int r = peakOf(right + 2, loopFrames);
        printf("  To the second terminal: left %d, right %d.\n", l, r);

        if (r == 0 || l != 0) {
            printf("FAIL: a channel on the second terminal should be on the right only.\n");
            failures++;
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\nMuting silences the sound, and unmuting brings it back.\n");

    {
        int execsBefore = hostCsndExecCount;

        audio_set_muted(true);

        if (hostCsndExecCount == execsBefore) {
            printf("FAIL: the volume was changed but the commands were never sent\n"
                   "      to the service, so the sound would carry on.\n");
            failures++;
        }
        else {
            printf("  The volume change was sent to the service.\n");
        }

        int muted = 1;
        for (int ear = 0; ear < 2; ear++) {
            if (hostCsndVolSet[hostCsndChn[ear]] != 0)
                muted = 0;
        }
        if (muted) {
            printf("  Both ears were turned down to nothing.\n");
        }
        else {
            printf("FAIL: muting did not turn both ears down.\n");
            failures++;
        }

        audio_set_muted(false);

        int restored = 1;
        for (int ear = 0; ear < 2; ear++) {
            if (hostCsndVolSet[hostCsndChn[ear]] == 0)
                restored = 0;
        }
        if (restored) {
            printf("  Both ears were turned back up.\n");
        }
        else {
            printf("FAIL: unmuting did not turn both ears back up.\n");
            failures++;
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\nA channel that is switched off makes no sound.\n");

    {
        Gameboy quiet;
        SoundEngine se4(&quiet);
        programSound(&quiet, &se4, 0, 0x11);
        runFrames(&se4, FRAMES_TO_RUN);

        int q = peakOf(left + 2, loopFrames);
        if (q != 0) {
            printf("FAIL: a channel with its volume at zero still produced %d.\n", q);
            failures++;
        }
        else {
            printf("  It made nothing, as it should.\n");
        }
    }

    printf("\n");
    if (failures) {
        printf("FAIL: %d checks failed.\n", failures);
        return 1;
    }

    printf("PASS: the APU makes a sound, and the transport is given it.\n");
    return 0;
}
