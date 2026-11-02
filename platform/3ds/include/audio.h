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
 * it. The difference is 11 parts per million, which is under half a
 * sample over a block.
 */
#define AUDIO_RATE 49932u

/* Samples the APU produces in one Game Boy frame: 70224 cycles / 84. */
#define AUDIO_FRAMES_PER_GB_FRAME 836

typedef struct {
    bool running;          /* the sound service was started                */
    unsigned long long frames;  /* stereo frames handed over                */
    unsigned long long blocks;  /* blocks given to the service              */
    int latencyMs;         /* how far behind the speaker the sound is     */
    int blockFrames;       /* the size of a block, in frames              */
    int peak;              /* the loudest sample of the last block         */
    int peakLeft;          /* the loudest sample of each ear               */
    int peakRight;
    int level;             /* how loud the last block was, as a percentage */
    unsigned int channelMask; /* the channels the service handed out      */
    unsigned int channelsUsed; /* the channels the transport is playing on */
    int channelLeft;       /* the channel each ear is played on            */
    int channelRight;
    int channelLeftAlt;    /* the ear's second buffer, on a channel of
                              its own, or -1                              */
    int channelRightAlt;
    bool mono;             /* both ears are on one channel                */
    int testToneHz;        /* the test tone's frequency, 0 when there is
                              none                                       */
    int gain;              /* the gain the transport applies               */
} audio_stats;

/*
 * What the sound service itself says about the channels.
 *
 * The transport knows what it asked for; this is what the service says it
 * got. The two are not the same thing: a channel can be set up perfectly
 * and still be sitting there doing nothing, and the only way to tell the
 * difference is to ask the service.
 */
typedef struct {
    bool readable;            /* the service could be asked at all        */
    int active[2][2];         /* whether each channel is playing:
                                 [ear][buffer]                           */
    unsigned int reg[2][2][3];/* the channel's own registers, as the
                                 service has them                        */
} audio_hardware;

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

/* What the sound service says about the two channels the transport uses.
 * This asks the service, so it is not cheap: it is for the diagnostics
 * panel and the log, not for the frame loop. */
void audio_read_hardware(audio_hardware* out);

/*
 * A test tone, so that the service can be told apart from the APU.
 *
 * The tone goes through the same transport as the game's sound and is
 * played at full scale, which is exactly what the APU does not do. When
 * it is on, the samples the APU makes are not played: there is one signal
 * in the transport at a time, and mixing the two would change the timing
 * that the transport exists to keep.
 *
 * A frequency of zero turns it off.
 */
void audio_set_test_tone(int hz);
int audio_test_tone_hz(void);

/*
 * Keep the test tone going.
 *
 * The tone is written with the samples the emulator hands over, so with no
 * game running, or with the game paused, there would be nothing to write
 * it with. The port calls this every frame so that the tone is there to
 * be heard on its own. It does nothing when the emulator is handing
 * samples over, because then the tone is being written already.
 */
void audio_pump_test_tone(void);

#ifdef __cplusplus
}
#endif
