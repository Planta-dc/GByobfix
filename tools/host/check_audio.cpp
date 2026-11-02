/*
 * Does the sound transport hand the sound service the right samples?
 *
 * The transport fills a block with the samples the emulator produced and
 * gives the block to the service when it is full. The blocks follow one
 * another, so the service plays the emulator's samples in one piece.
 *
 * What has to be right:
 *
 *   - the service is told that the console is making a sound at all;
 *   - the block holds the emulator's samples, in order, and the sample
 *     after the last one of a block is the first one of the next;
 *   - the samples are made louder, because the APU's own output is about
 *     a twentieth of full scale and nothing comes out of a speaker at
 *     that level;
 *   - the block is given to the service when it is full, and not before;
 *   - the test tone replaces the samples, and muting turns the volume
 *     down.
 *
 * Run it: make -C tools/host check
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"

/* What the shim recorded about the sound service. */
extern void* hostCsndData[32];
extern u32 hostCsndSize[32];
extern u32 hostCsndFlags[32];
extern int hostCsndCalls;
extern int hostCsndChn[8];
extern int hostCsndChnCount;
extern u32 hostCsndVolSet[32];
extern int hostCsndExecCount;
extern int hostCsndPlaybackOn;

static int failures = 0;

static void fail(const char* what, long long got, long long expected)
{
    printf("FAIL: %s: got %lld, expected %lld\n", what, got, expected);
    failures++;
}

/* One frame's worth of samples, at the rate the APU produces them. */
#define FRAME_SAMPLES 836

/* The transport's block is two frames long. */
#define BLOCK_SAMPLES (FRAME_SAMPLES * 2)

/* The gain the transport applies. */
#define GAIN 8

static s16 left[FRAME_SAMPLES];
static s16 right[FRAME_SAMPLES];

/*
 * What the transport makes of a sample.
 *
 * The samples are made louder, and a sample that is louder than the scale
 * can go is cut off at the end of it: a sample past the end of the scale
 * is a click.
 */
static s16 scaled(int value)
{
    int v = value * GAIN;

    if (v > 32767)
        v = 32767;
    else if (v < -32768)
        v = -32768;

    return (s16)v;
}

/*
 * The samples of a frame.
 *
 * Each frame starts at a different number, so that one frame can be told
 * from the next: the transport has to hand the service the samples of
 * the frame that was given to it, and not the frame before it.
 */
static void fillSamples(int base)
{
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        left[i] = (s16)(base + i);
        right[i] = (s16)(-(base + i));
    }
}

int main(void)
{
    if (!audio_init()) {
        printf("FAIL: the sound transport would not start.\n");
        return 1;
    }

    audio_stats stats;
    audio_get_stats(&stats);

    /* ---------------------------------------------------------------- */
    printf("The service is told that the console is making a sound.\n");

    if (!hostCsndPlaybackOn) {
        printf("FAIL: the switch that starts the sound was never thrown. A\n"
               "      channel can be set up perfectly and the service will\n"
               "      still say nothing.\n");
        failures++;
    }
    else {
        printf("  The switch was thrown.\n");
    }

    if (!stats.running)
        fail("the transport is not running", 0, 1);
    if (stats.channelLeft < 0 || stats.channelRight < 0)
        fail("the ears have no channel to play on", 0, 1);

    /* ---------------------------------------------------------------- */
    printf("\nA block is given to the service when it is full.\n");

    int callsBefore = hostCsndCalls;

    /* One frame: the block is half full and nothing should be sent. */
    fillSamples(1000);
    audio_submit(left, right, FRAME_SAMPLES);
    if (hostCsndCalls != callsBefore) {
        printf("FAIL: the service was given %d blocks for half a block of\n"
               "      samples.\n", hostCsndCalls - callsBefore);
        failures++;
    }
    else {
        printf("  Half a block: nothing sent.\n");
    }

    /* The second frame fills the block. */
    fillSamples(2000);
    audio_submit(left, right, FRAME_SAMPLES);
    if (hostCsndCalls != callsBefore + 2) {
        printf("FAIL: the service was given %d blocks for a full block of\n"
               "      samples; there should have been one for each ear.\n",
               hostCsndCalls - callsBefore);
        failures++;
    }
    else {
        printf("  A full block: one for each ear.\n");
    }

    audio_get_stats(&stats);
    if (stats.blocks != 1)
        fail("the wrong number of blocks was sent", (long long)stats.blocks, 1);
    if (stats.blockFrames != BLOCK_SAMPLES)
        fail("the block is the wrong size", stats.blockFrames, BLOCK_SAMPLES);

    /* ---------------------------------------------------------------- */
    printf("\nThe block holds the emulator's samples, in order.\n");

    {
        int chn = hostCsndChn[0];
        const s16* block = (const s16*)hostCsndData[chn];

        if (!block) {
            printf("FAIL: the service was never given a block to play.\n");
            failures++;
        }
        else {
            int wrong = 0;

            /* The first half of the block is the first frame, made
             * louder. */
            for (int i = 0; i < FRAME_SAMPLES; i++) {
                if (block[i] != scaled(1000 + i))
                    wrong++;
            }

            /* The second half is the second frame. */
            for (int i = 0; i < FRAME_SAMPLES; i++) {
                if (block[FRAME_SAMPLES + i] != scaled(2000 + i))
                    wrong++;
            }

            if (wrong) {
                printf("FAIL: %d samples in the block are not the samples\n"
                       "      the emulator produced.\n", wrong);
                failures++;
            }
            else {
                printf("  The block is the emulator's samples, in order.\n");
            }

            /* The right ear is a block of its own. */
            const s16* rblock = (const s16*)hostCsndData[hostCsndChn[1]];
            if (rblock) {
                int rwrong = 0;
                for (int i = 0; i < FRAME_SAMPLES; i++) {
                    if (rblock[i] != scaled(-(1000 + i)))
                        rwrong++;
                }
                if (rwrong) {
                    printf("FAIL: %d samples in the right ear are not the\n"
                           "      samples the emulator produced.\n", rwrong);
                    failures++;
                }
                else {
                    printf("  The right ear has its own block, with its own\n"
                           "  samples.\n");
                }
            }
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\nThe samples are made louder.\n");

    /* The APU's loudest sample is about 1680. At that level nothing comes
     * out of a speaker, so the transport makes the samples louder. */
    {
        int chn = hostCsndChn[0];
        const s16* block = (const s16*)hostCsndData[chn];
        int peak = 0;

        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int v = block[i] < 0 ? -block[i] : block[i];
            if (v > peak)
                peak = v;
        }

        int wanted = scaled(2000 + FRAME_SAMPLES - 1);
        if (wanted < 0)
            wanted = -wanted;
        if (peak != wanted) {
            printf("FAIL: the loudest sample is %d, expected %d.\n", peak,
                   wanted);
            failures++;
        }
        else {
            printf("  The loudest sample is %d, which is %d%% of full scale.\n",
                   peak, peak * 100 / 32767);
        }

        if (stats.gain != GAIN)
            fail("the gain is wrong", stats.gain, GAIN);
    }

    /* ---------------------------------------------------------------- */
    printf("\nThe blocks follow one another.\n");

    /* Two more frames: the block is filled again and sent again, and it
     * must hold these two frames and not the two before them. */
    {
        fillSamples(3000);
        audio_submit(left, right, FRAME_SAMPLES);
        fillSamples(4000);
        audio_submit(left, right, FRAME_SAMPLES);

        int chn = hostCsndChn[0];
        const s16* block = (const s16*)hostCsndData[chn];
        int wrong = 0;

        for (int i = 0; i < FRAME_SAMPLES; i++) {
            if (block[i] != scaled(3000 + i))
                wrong++;
            if (block[FRAME_SAMPLES + i] != scaled(4000 + i))
                wrong++;
        }

        if (wrong) {
            printf("FAIL: %d samples in the block are not the samples of\n"
                   "      the frames that were just handed over.\n", wrong);
            failures++;
        }
        else {
            printf("  The block holds the two frames that were just given\n"
                   "  to it, and not the ones before.\n");
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\nThe test tone replaces the samples.\n");

    audio_set_test_tone(440);
    fillSamples(5000);
    audio_submit(left, right, FRAME_SAMPLES);
    fillSamples(6000);
    audio_submit(left, right, FRAME_SAMPLES);

    {
        int chn = hostCsndChn[0];
        const s16* block = (const s16*)hostCsndData[chn];
        int peak = 0;

        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int v = block[i] < 0 ? -block[i] : block[i];
            if (v > peak)
                peak = v;
        }

        /* The tone is a sine at nearly full scale, so it is much louder
         * than the APU's output. */
        if (peak < 29000) {
            printf("FAIL: the loudest sample of the tone is %d; the tone\n"
                   "      should be at full scale.\n", peak);
            failures++;
        }
        else {
            printf("  The tone is at full scale: %d.\n", peak);
        }
    }

    audio_set_test_tone(0);

    /* ---------------------------------------------------------------- */
    printf("\nMuting turns the volume down, and unmuting puts it back.\n");

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

    audio_exit();

    if (failures) {
        printf("FAIL: %d problems.\n", failures);
        return 1;
    }

    printf("PASS: the transport handed the service %llu frames in %llu blocks, "
           "%d frames each, %d ms behind.\n",
           stats.frames, stats.blocks, stats.blockFrames, stats.latencyMs);
    return 0;
}
