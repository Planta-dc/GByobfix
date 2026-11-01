/*
 * Does the frame clock keep its promise?
 *
 * The port does not pace the emulator on the display's vertical blank, the
 * way it used to, because the Game Boy runs at 59.7275 frames a second and
 * the screen at 60, so every game would run 0.46 % fast. Instead the port
 * keeps its own clock: it knows how many frames it has shown and works out
 * when the next one is due from the time it started.
 *
 * The promise is that a frame which runs long does not push the next one
 * out, that a frame which runs short does not pull the next one in, and
 * that the error does not grow with the number of frames.
 *
 * The clock is driven by the stand-in for the SDK, which can be told to
 * wake up late and to move a step at a time.
 *
 * Run it: make -C tools/host check
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "frameclock.h"

extern u64 hostTick;
extern u64 hostTickPerRead;
extern u64 hostSleepOvershoot;

static int failures = 0;

static void fail(const char* what, long long got, long long expected,
                 long long allowed)
{
    printf("FAIL: %s: got %lld, expected %lld (allowed %lld off)\n",
           what, got, expected, allowed);
    failures++;
}

/* Run the clock for a while and see how far it drifts from the ideal.
 *
 * The ideal is the frame number times the exact length of a frame. The
 * clock is allowed to be a little late, because the kernel's sleep is not
 * exact, but the error must not grow with the number of frames. */
static long long runClock(u64 frames, u64 overshoot, u64 tickPerRead)
{
    hostTick = 1000;
    hostSleepOvershoot = overshoot;
    hostTickPerRead = tickPerRead;

    FrameClock clock;
    frameClockInit(&clock);

    long long worst = 0;

    for (u64 frame = 0; frame < frames; frame++) {
        u64 due = frameClockDeadline(&clock, frame);
        u64 ideal = clock.epoch + (frame * FRAME_TICK_NUM) / FRAME_TICK_DEN;

        if (due != ideal)
            fail("the deadline is not the ideal one", (long long)due,
                 (long long)ideal, 1);

        frameClockWaitNext(&clock);

        /* The frame should have started at its deadline, give or take the
         * spin's step and the sleep's overshoot. */
        u64 started = hostTick;
        long long error = (long long)started - (long long)ideal;
        if (error < 0)
            error = -error;

        if (error > worst)
            worst = error;
    }

    return worst;
}

int main(void)
{
    /* The length of a frame, in ticks and in nanoseconds. */
    double ticks = (double)FRAME_TICK_NUM / (double)FRAME_TICK_DEN;
    double milliseconds = ticks * 1000.0 / (double)SYSCLOCK_ARM11;
    double framesPerSecond = (double)SYSCLOCK_ARM11 / ticks;

    printf("A frame is %.3f ticks, %.6f ms, %.4f frames a second.\n",
           ticks, milliseconds, framesPerSecond);

    /* The Game Boy's frame rate is 4194304 / 70224 = 59.7275. */
    if (framesPerSecond < 59.72 || framesPerSecond > 59.74)
        fail("the frame rate is wrong", (long long)(framesPerSecond * 1000),
             59727, 5);

    /* A perfect clock: the error is the spin's step and nothing else. */
    long long perfect = runClock(3600, 0, 64);
    if (perfect > 64)
        fail("a perfect clock drifted", perfect, 0, 64);

    /* A clock that wakes up late every time. The error must not grow with
     * the number of frames: 3600 frames that each woke up a millisecond
     * late would be three and a half seconds late if the error were
     * added up. */
    long long late = runClock(3600, 268112, 64);
    if (late > 268112 + 64)
        fail("the late sleeps were added up", late, 0, 268112 + 64);

    /* The tick to nanosecond conversion has to survive the whole range. */
    for (u64 ticks = 0; ticks < (1ULL << 40); ticks = ticks * 3 + 1) {
        s64 ns = frameClockTicksToNs(ticks);
        /* The same sum, worked out the long way. */
        long long expected = (long long)(ticks / SYSCLOCK_ARM11) * 1000000000LL +
                             (long long)((ticks % SYSCLOCK_ARM11) * 1000000000ULL / SYSCLOCK_ARM11);
        long long difference = (long long)ns - expected;
        if (difference < 0)
            difference = -difference;
        if (difference > 2)
            fail("the tick to nanosecond conversion is wrong", (long long)ns,
                 expected, 2);
    }

    /* After a resync the next frame is due at once, and the clock does not
     * try to catch up on the frames it missed. */
    hostTick = 5000000;
    hostSleepOvershoot = 0;
    hostTickPerRead = 64;

    FrameClock clock;
    frameClockInit(&clock);
    for (int i = 0; i < 100; i++)
        frameClockWaitNext(&clock);

    /* Pretend the emulator was paused for a second. */
    hostTick += SYSCLOCK_ARM11;
    frameClockResync(&clock);

    u64 before = hostTick;
    frameClockWaitNext(&clock);
    if (hostTick - before > 1000)
        fail("the clock did not resync", (long long)(hostTick - before), 0, 1000);

    if (failures) {
        printf("FAIL: %d problems.\n", failures);
        return 1;
    }

    printf("PASS: the frame clock keeps its time: %lld ticks over 3600 frames "
           "with a perfect clock, %lld with sleeps that wake up a "
           "millisecond late.\n", perfect, late);
    return 0;
}
