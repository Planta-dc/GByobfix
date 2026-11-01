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
 * The sound service plays one sample at a time on a channel, and a channel
 * is mono: the two ears of the Game Boy's stereo output have to go to two
 * channels. Each channel is given a ring of samples and asked to loop over
 * it, so the service never runs dry and never has to be restarted.
 *
 * The service has no way of saying where it is in the buffer, so the
 * position is worked out from the clock: the service is playing at a known
 * rate, and the clock says how long it has been playing. The emulator
 * writes ahead of that position, which is what gives the sound its
 * latency, and the ring is big enough that the write never catches up with
 * the read.
 *
 * Two things can go wrong, and both are counted rather than hidden:
 *
 *   - the emulator falls behind and the service reaches samples that have
 *     not been written yet (an underrun), and
 *   - the emulator runs ahead and would overwrite samples the service has
 *     not played yet (a drop).
 *
 * Neither is fixed by guessing: the transport corrects the difference by
 * dropping or repeating a single sample, which is a discontinuity of 30
 * millionths of a second. If the console's clock and the sound service's
 * clock disagree about the rate, that is what happens, and the counters
 * say how often.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The transport is written in C, so that it can be built with the same
 * rules as the rest of the port, and the names have to say so. */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * The rate everything below runs at.
 *
 * GameYob's APU is sampled every 84 Game Boy cycles, which is 49932.57 Hz.
 * The sound service takes an integer rate, and 49932 is the closest one to
 * it. The difference is 11 parts per million, which the sample correction
 * below takes care of.
 */
#define AUDIO_RATE 49932u

/* Samples the APU produces in one Game Boy frame: 70224 cycles / 84. */
#define AUDIO_FRAMES_PER_GB_FRAME 836

typedef struct {
    bool running;          /* the sound service was started                */
    unsigned long long frames;      /* stereo frames handed over          */
    unsigned long long underruns;   /* times the emulator fell behind      */
    unsigned long long drops;       /* samples thrown away                 */
    unsigned long long corrections; /* single samples dropped or repeated  */
    int latencyMs;        /* how far ahead of the speaker the emulator is */
    int fillFrames;       /* the same, in frames                          */
} audio_stats;

/* Start the sound service. Returns false if it could not be started, in
 * which case the emulator runs silently. */
bool audio_init(void);

/* Stop the sound service and give the memory back. */
void audio_exit(void);

/* Mute and unmute. Muting does not stop the emulator from producing
 * samples; it turns the volume down, so that unmuting does not have to
 * catch up. */
void audio_set_muted(bool muted);
bool audio_is_muted(void);

/* Hand a block of stereo samples to the transport. */
void audio_submit(const int16_t* left, const int16_t* right, unsigned frames);

/* What the transport has been doing. */
void audio_get_stats(audio_stats* out);

#ifdef __cplusplus
}
#endif
