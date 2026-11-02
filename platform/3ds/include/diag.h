/*
 * The port's own numbers: on the screen, on the game picture and in a file.
 *
 * What this is for
 * -----------------
 * The port has never run on a console, so the only way to find out what it
 * is really doing is to ask it and to have the answer in a form that can
 * be carried back: a strip on the game picture that can be read at a
 * glance, a panel of numbers for the detail, and a file on the card for
 * the whole story of a session.
 *
 * The numbers are of the sound, of the frame clock and of the emulator.
 * The sound is the interesting one: there are three things that can go
 * wrong between the APU and the speaker - the APU can make no sound, the
 * transport can lose it, and the service can refuse to play it - and they
 * look exactly the same from the outside. The test tone tells them apart.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where the log is written on the card. */
#define DIAG_LOG_PATH "/gameyob.log"

/* The most lines the panel can show. */
#define DIAG_MAX_LINES 12

/* The frame clock's numbers.
 *
 * The window is a second long: the worst frame and the late frames are
 * counted over it, so that a fault an hour ago does not sit in the
 * numbers for ever.
 */
typedef struct {
    uint64_t frames;   /* the frames since the program started        */
    uint32_t avgUs;    /* the average frame time, in microseconds     */
    uint32_t lastUs;   /* the last frame time                          */
    uint32_t worstUs;  /* the worst frame of the last window          */
    uint32_t lateFrames; /* the late frames of the last window         */
    uint32_t lateTotal;  /* the late frames since the program started  */
    float fps;         /* the rate the frames are coming at            */
} diag_frame_stats;

/* Prepare the diagnostics. Nothing is written until logging is turned on. */
void diagInit(void);
void diagExit(void);

/* Whether the log is being written, and where it is written to. */
bool diagLogging(void);
void diagSetLogging(bool on);
const char *diagLogPath(void);

/*
 * A line in the log.
 *
 * The lines are buffered and written to the card in pieces, so that the
 * program never does a card write inside a frame. The format string is
 * checked by the compiler: without that, a %u where a double was passed
 * compiles without a word and prints nonsense, and this log is the only
 * evidence there is of what the program was doing.
 */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void diagLog(const char *fmt, ...);

/*
 * A line that is on the card at once.
 *
 * For the things that are worth having even if the program falls over:
 * the start, the settings being changed, the errors.
 */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void diagLogUrgent(const char *fmt, ...);

/*
 * Write the buffer out, but only when it is worth a card write.
 *
 * Safe to call every frame.
 */
void diagFlushSoft(void);

/* Write the buffer out now. */
void diagFlush(void);

/*
 * The frame clock's numbers, measured.
 *
 * Called once a frame, at the end of one: this is where a frame ends, so
 * this is where its time is measured.
 */
void diagTick(void);

/* Everything there is to say, written out in full. */
void diagDump(void);

/* The frame clock's numbers. */
void diagGetFrameStats(diag_frame_stats *out);

/*
 * The lines of the panel.
 *
 * The panel is redrawn every frame, so the lines are made fresh every
 * time and the array belongs to this module. The count is put in the
 * argument.
 */
const char *const *diagLines(int *count);

/* Whether the panel is on the screen. The little panel with the sound's
 * numbers stays out of the way while it is. */
bool diagPanelOpen(void);
void diagSetPanelOpen(bool open);

#ifdef __cplusplus
}
#endif
