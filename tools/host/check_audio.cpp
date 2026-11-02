/*
 * Does the sound transport hand the sound service the right samples?
 *
 * The transport fills a buffer with the samples the emulator produced and
 * gives the buffer to the service when it is full. Each ear has two
 * buffers: one is played while the other is filled, so the samples the
 * service is reading are never the ones being written.
 *
 * What has to be right:
 *
 *   - the service is told that the console is making a sound at all;
 *   - the buffer holds the emulator's samples, in order, and the sample
 *     after the last one of a buffer is the first one of the next;
 *   - the samples are made louder, because the APU's own output is about
 *     a twentieth of full scale and nothing comes out of a speaker at
 *     that level;
 *   - the buffer is given to the service when it is full, and not before;
 *   - the buffer the service is playing is not written to while it is
 *     being played;
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

/* The transport's buffer is two frames long. */
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

/* How many of the first `count` samples of a buffer are not what was
 * expected. */
static int countWrong(const s16* buffer, int base, int count)
{
    int wrong = 0;

    for (int i = 0; i < count; i++) {
        if (buffer[i] != scaled(base + i))
            wrong++;
    }

    return wrong;
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
    printf("\nEach ear has two buffers, and each buffer has a channel of\n"
           "      its own, so the address never changes.\n");

    if (stats.channelLeftAlt < 0 || stats.channelRightAlt < 0) {
        printf("FAIL: the ears have only one buffer each, so a buffer that\n"
               "      is being played has to be written to.\n");
        failures++;
    }
    else {
        printf("  The left ear is on channels %d and %d, the right on %d and %d.\n",
               stats.channelLeft, stats.channelLeftAlt, stats.channelRight,
               stats.channelRightAlt);
    }

    if (stats.channelLeft == stats.channelLeftAlt ||
        stats.channelLeft == stats.channelRight ||
        stats.channelLeftAlt == stats.channelRight ||
        stats.channelLeftAlt == stats.channelRightAlt ||
        stats.channelRight == stats.channelRightAlt) {
        printf("FAIL: two buffers are sharing a channel.\n");
        failures++;
    }

    /* ---------------------------------------------------------------- */
    printf("\nA buffer is given to the service when it is full.\n");

    int callsBefore = hostCsndCalls;

    /* One frame: the buffer is half full and nothing should be sent. */
    fillSamples(1000);
    audio_submit(left, right, FRAME_SAMPLES);
    if (hostCsndCalls != callsBefore) {
        printf("FAIL: the service was given %d buffers for half a buffer of\n"
               "      samples.\n", hostCsndCalls - callsBefore);
        failures++;
    }
    else {
        printf("  Half a buffer: nothing sent.\n");
    }

    /* The second frame fills the buffer. */
    fillSamples(2000);
    audio_submit(left, right, FRAME_SAMPLES);
    if (hostCsndCalls != callsBefore + 2) {
        printf("FAIL: the service was given %d buffers for a full buffer of\n"
               "      samples; there should have been one for each ear.\n",
               hostCsndCalls - callsBefore);
        failures++;
    }
    else {
        printf("  A full buffer: one for each ear.\n");
    }

    audio_get_stats(&stats);
    if (stats.blocks != 1)
        fail("the wrong number of buffers was sent", (long long)stats.blocks, 1);
    if (stats.blockFrames != BLOCK_SAMPLES)
        fail("the buffer is the wrong size", stats.blockFrames, BLOCK_SAMPLES);

    /* ---------------------------------------------------------------- */
    printf("\nThe buffer holds the emulator's samples, in order.\n");

    {
        const s16* buffer = (const s16*)hostCsndData[stats.channelLeft];

        if (!buffer) {
            printf("FAIL: the service was never given a buffer to play.\n");
            failures++;
        }
        else {
            /* The first half of the buffer is the first frame, made
             * louder. */
            int wrong = countWrong(buffer, 1000, FRAME_SAMPLES);

            /* The second half is the second frame. */
            wrong += countWrong(buffer + FRAME_SAMPLES, 2000, FRAME_SAMPLES);

            if (wrong) {
                printf("FAIL: %d samples in the buffer are not the samples\n"
                       "      the emulator produced.\n", wrong);
                failures++;
            }
            else {
                printf("  The buffer is the emulator's samples, in order.\n");
            }

            /* The right ear is a buffer of its own. */
            const s16* rbuffer = (const s16*)hostCsndData[stats.channelRight];
            if (rbuffer) {
                int rwrong = 0;
                for (int i = 0; i < FRAME_SAMPLES; i++) {
                    if (rbuffer[i] != scaled(-(1000 + i)))
                        rwrong++;
                }
                if (rwrong) {
                    printf("FAIL: %d samples in the right ear are not the\n"
                           "      samples the emulator produced.\n", rwrong);
                    failures++;
                }
                else {
                    printf("  The right ear has its own buffer, with its own\n"
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
        const s16* buffer = (const s16*)hostCsndData[stats.channelLeft];
        int peak = 0;

        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int v = buffer[i] < 0 ? -buffer[i] : buffer[i];
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
    printf("\nThe buffer the service is playing is not written to.\n");

    /*
     * This is the one that matters. Two more frames fill the ear's second
     * buffer, and that buffer is handed to the service to play. The first
     * buffer is still being played at that moment, so the first buffer
     * must still hold the first two frames and not the two that were just
     * handed over.
     *
     * A transport with one buffer per ear cannot pass this: it would be
     * writing over the samples the service is reading, and what came out
     * would be a mixture of two frames of sound.
     */
    {
        fillSamples(3000);
        audio_submit(left, right, FRAME_SAMPLES);
        fillSamples(4000);
        audio_submit(left, right, FRAME_SAMPLES);

        const s16* playing = (const s16*)hostCsndData[stats.channelLeft];
        const s16* fresh = (const s16*)hostCsndData[stats.channelLeftAlt];
        int wrong = 0;

        if (!playing || !fresh) {
            printf("FAIL: the second buffer was never given to the service.\n");
            failures++;
        }
        else {
            /* The buffer that is being played still holds the frames it
             * was given. */
            wrong += countWrong(playing, 1000, FRAME_SAMPLES);
            wrong += countWrong(playing + FRAME_SAMPLES, 2000, FRAME_SAMPLES);

            /* And the new buffer holds the two frames that were just
             * handed over. */
            wrong += countWrong(fresh, 3000, FRAME_SAMPLES);
            wrong += countWrong(fresh + FRAME_SAMPLES, 4000, FRAME_SAMPLES);

            if (wrong) {
                printf("FAIL: %d samples are not where they should be: the\n"
                       "      buffer being played was written to, or the new\n"
                       "      buffer was not filled.\n", wrong);
                failures++;
            }
            else {
                printf("  The buffer being played was left alone, and the new\n"
                       "  buffer holds the two frames that were just given to\n"
                       "  it.\n");
            }
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
        /* The buffers take turns, so these two frames went into the first
         * buffer of the left ear. */
        const s16* buffer = (const s16*)hostCsndData[stats.channelLeft];
        int peak = 0;

        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int v = buffer[i] < 0 ? -buffer[i] : buffer[i];
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
        int channels[4];

        channels[0] = stats.channelLeft;
        channels[1] = stats.channelLeftAlt;
        channels[2] = stats.channelRight;
        channels[3] = stats.channelRightAlt;

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
        for (int i = 0; i < 4; i++) {
            if (channels[i] >= 0 && hostCsndVolSet[channels[i]] != 0)
                muted = 0;
        }
        if (muted) {
            printf("  Every channel was turned down to nothing.\n");
        }
        else {
            printf("FAIL: muting did not turn every channel down.\n");
            failures++;
        }

        audio_set_muted(false);

        int restored = 1;
        for (int i = 0; i < 4; i++) {
            if (channels[i] >= 0 && hostCsndVolSet[channels[i]] == 0)
                restored = 0;
        }
        if (restored) {
            printf("  Every channel was turned back up.\n");
        }
        else {
            printf("FAIL: unmuting did not turn every channel back up.\n");
            failures++;
        }
    }

    audio_exit();

    if (failures) {
        printf("FAIL: %d problems.\n", failures);
        return 1;
    }

    printf("PASS: the transport handed the service %llu frames in %llu buffers, "
           "%d frames each, %d ms behind.\n",
           stats.frames, stats.blocks, stats.blockFrames, stats.latencyMs);
    return 0;
}
