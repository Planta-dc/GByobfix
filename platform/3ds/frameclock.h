/*
 * The frame clock: the 3DS port's own time base.
 *
 * Why this exists
 * ---------------
 * A Game Boy frame is 70224 cycles at 4194304 Hz, which is 16.743 ms, so the
 * Game Boy runs at 59.7275 frames a second. The 3DS screen runs at exactly
 * 60. Pacing the emulator on the display's VBlank - which is what the port
 * did before - therefore runs every game 0.46 % fast. One frame of that is
 * invisible; a game that saves state or syncs to the music is not, and the
 * error never goes away because it is built into the pacing.
 *
 * So the port keeps its own clock. It does not ask "how long has the last
 * frame taken" and it does not add the frame time to a running total: it
 * knows how many frames have been presented and computes the deadline of
 * frame N from the epoch. A frame that runs long cannot push the next one
 * out, and a frame that runs short cannot pull the next one in.
 *
 * The unit
 * --------
 * svcGetSystemTick() counts at SYSCLOCK_ARM11 Hz (268111856). One Game Boy
 * frame is
 *
 *     70224 * SYSCLOCK_ARM11 / 4194304  =  4489162.2... ticks
 *
 * which is not a whole number of ticks, so the deadline is kept as a
 * rational, NUM/DEN ticks per frame:
 *
 *     FRAME_TICK_NUM = 70224 * SYSCLOCK_ARM11 / 2^18  (after cancelling)
 *     FRAME_TICK_DEN = 2^14
 *
 * The division truncates, so deadline(N) is up to one tick (3.7 ns) early.
 * That error is bounded rather than accumulating, because the deadline is a
 * function of N and not a running sum: after ten hours the frame that should
 * be due at T is at most one tick early, exactly as the first one was.
 *
 * Integer overflow was the other trap here. dt * rate_num * FRAME_SCALE
 * overflows 64 bits after one second of frame time, so nothing here
 * multiplies a duration by a rate; the only product is the frame number
 * times the numerator, which stays inside 64 bits for 250 million frames
 * (four years).
 */

#pragma once

#include <3ds.h>
#include <3ds/os.h>

/* Ticks in one Game Boy frame, as a rational. See the comment above. */
#define FRAME_TICK_NUM 73546433499ULL
#define FRAME_TICK_DEN 16384ULL

/* Ticks in one Game Boy frame, rounded down, for code that only needs an
 * estimate (the audio transport, for instance). */
#define FRAME_TICKS ((u64)(FRAME_TICK_NUM / FRAME_TICK_DEN))

typedef struct {
    u64 epoch;  /* the tick at which frame 0 came due */
    u64 frame;  /* frames scheduled since the epoch */
} FrameClock;

/*
 * Ticks to nanoseconds.
 *
 * A tick is 1/268111856 of a second, so the answer is ticks * 1e9 /
 * 268111856. Multiplying the ticks by a billion overflows 64 bits after
 * about sixty seconds' worth of ticks, so the ticks are split into whole
 * seconds and the rest first. The rest is smaller than the tick rate, so
 * its product is smaller than 2.7e17 and cannot overflow.
 */
static inline s64 frameClockTicksToNs(u64 ticks)
{
    const u64 nanoseconds = 1000000000ULL;
    u64 seconds = ticks / SYSCLOCK_ARM11;
    u64 rest = ticks % SYSCLOCK_ARM11;

    return (s64)(seconds * nanoseconds + (rest * nanoseconds) / SYSCLOCK_ARM11);
}

/* The tick at which the given frame is due. */
static inline u64 frameClockDeadline(const FrameClock* clock, u64 frame)
{
    return clock->epoch + (frame * FRAME_TICK_NUM) / FRAME_TICK_DEN;
}

static inline void frameClockInit(FrameClock* clock)
{
    clock->epoch = svcGetSystemTick();
    clock->frame = 0;
}

/* Make the next frame due now, without changing how many frames have been
 * presented. Used when the emulator changes speed (fast forward, or a pause
 * that lasted longer than a frame): without this the clock would try to
 * catch up on all the frames it missed, at full speed, before it went back
 * to waiting. */
static inline void frameClockResync(FrameClock* clock)
{
    clock->epoch = svcGetSystemTick() - (clock->frame * FRAME_TICK_NUM) / FRAME_TICK_DEN;
}

/* Sleep until the next frame is due, then count it.
 *
 * The sleep is done in two parts: most of the wait is handed to the kernel,
 * and the last FRAME_SPIN_NS are spun. The kernel's sleep granularity is not
 * known from here, and a sleep that overshoots is a frame that starts late;
 * spinning the tail keeps the deadline accurate without keeping the ARM11
 * busy for the whole frame. FRAME_SPIN_NS is a guess and has not been
 * measured on hardware - it is one of the things the first real run has to
 * check. */
#define FRAME_SPIN_NS 400000

static inline void frameClockWaitNext(FrameClock* clock)
{
    u64 now = svcGetSystemTick();
    u64 due = frameClockDeadline(clock, clock->frame);

    if (due > now) {
        u64 ticks = due - now;
        s64 ns = frameClockTicksToNs(ticks);

        if (ns > FRAME_SPIN_NS) {
            svcSleepThread(ns - FRAME_SPIN_NS);
            now = svcGetSystemTick();
        }
        while (now < due)
            now = svcGetSystemTick();
    }

    clock->frame++;
}
