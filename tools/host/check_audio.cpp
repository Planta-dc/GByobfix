/*
 * Does the sound transport keep its distance from the sound service?
 *
 * The transport writes the samples into a ring that the sound service
 * loops over, and it works out where the service has got to from the
 * console's clock. Three things have to be right:
 *
 *   - the emulator must stay ahead of the service, or the service plays
 *     silence;
 *   - it must not run so far ahead that it overwrites samples the service
 *     has not played yet;
 *   - the two clocks do not agree exactly, and the difference must be
 *     taken out of the signal rather than accumulated.
 *
 * The stand-in for the SDK lets the test move the clock by hand, so all
 * three can be arranged.
 *
 * Run it: make -C tools/host check
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"

extern u64 hostTick;
extern u64 hostTickPerRead;
extern u64 hostSleepOvershoot;

static int failures = 0;

static void fail(const char* what, long long got, long long expected)
{
    printf("FAIL: %s: got %lld, expected %lld\n", what, got, expected);
    failures++;
}

/* The number of ticks in one Game Boy frame. */
#define FRAME_TICKS 4488918ULL

/* One frame's worth of samples, at the rate the APU produces them. */
#define FRAME_SAMPLES 836

static s16 left[FRAME_SAMPLES];
static s16 right[FRAME_SAMPLES];

static void fillSamples(void)
{
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        left[i] = (s16)(i * 37);
        right[i] = (s16)(-i * 11);
    }
}

int main(void)
{
    fillSamples();

    hostTick = 0;
    hostTickPerRead = 0;
    hostSleepOvershoot = 0;

    if (!audio_init()) {
        printf("FAIL: the sound transport would not start.\n");
        return 1;
    }

    audio_stats stats;

    /* A minute of frames, with the clock and the emulator in step. */
    for (int frame = 0; frame < 3600; frame++) {
        audio_submit(left, right, FRAME_SAMPLES);
        hostTick += FRAME_TICKS;
    }

    audio_get_stats(&stats);

    if (!stats.running)
        fail("the transport is not running", 0, 1);
    if (stats.frames != (unsigned long long)FRAME_SAMPLES * 3600)
        fail("the wrong number of frames were played", (long long)stats.frames,
             (long long)FRAME_SAMPLES * 3600);
    if (stats.underruns != 0)
        fail("the emulator fell behind", (long long)stats.underruns, 0);
    if (stats.drops != 0)
        fail("samples were thrown away", (long long)stats.drops, 0);
    if (stats.corrections != 0)
        fail("the clocks were corrected", (long long)stats.corrections, 0);

    /* The emulator must be about the margin ahead of the service. */
    if (stats.fillFrames < 1000 || stats.fillFrames > 1100)
        fail("the distance to the service is wrong", stats.fillFrames, 1024);
    if (stats.latencyMs < 18 || stats.latencyMs > 24)
        fail("the latency is wrong", stats.latencyMs, 20);

    /* Now the emulator runs too slowly: it gives the service less time
     * than it produces samples for. The transport has to make the
     * distance up again, and it has to say so. */
    unsigned long long before = stats.corrections;
    for (int frame = 0; frame < 600; frame++) {
        audio_submit(left, right, FRAME_SAMPLES);
        hostTick += FRAME_TICKS + 200;   /* The clock runs a little fast. */
    }
    audio_get_stats(&stats);
    if (stats.corrections <= before)
        fail("a slow clock was not corrected", (long long)stats.corrections,
             (long long)before + 1);

    /* And now the emulator runs too quickly, which is what fast forward
     * does: it produces ten frames of samples for every frame of clock,
     * and has to throw some away. */
    before = stats.drops;
    for (int frame = 0; frame < 10; frame++) {
        for (int i = 0; i < 10; i++)
            audio_submit(left, right, FRAME_SAMPLES);
        hostTick += FRAME_TICKS;
    }
    audio_get_stats(&stats);
    if (stats.drops <= before)
        fail("a fast clock did not drop anything", (long long)stats.drops,
             (long long)before + 1);

    /* Whatever happened, the transport must still be inside the band it
     * is supposed to keep: not so far ahead that it overwrites what the
     * service has not played, and not so far behind that the service runs
     * out. After a burst of fast forward it is at the far end of the
     * band, which is where it should be. */
    if (stats.fillFrames < 1000 || stats.fillFrames > 7168)
        fail("the distance to the service is wrong", stats.fillFrames, 1024);

    audio_exit();

    if (failures) {
        printf("FAIL: %d problems.\n", failures);
        return 1;
    }

    printf("PASS: the transport kept its distance: %llu frames, %llu underruns, "
           "%llu drops, %llu corrections, %d ms behind.\n",
           stats.frames, stats.underruns, stats.drops, stats.corrections,
           stats.latencyMs);
    return 0;
}
