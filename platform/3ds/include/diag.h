/*
 * The port's own numbers: on the screen and in a file.
 *
 * What this is for
 * -----------------
 * The port has never run on a console, so the only way to find out what it
 * is really doing is to ask it and to have the answer in a form that can
 * be carried back: a screen that can be read, and a file on the card that
 * can be sent.
 *
 * So there are two things here. The diagnostics panel is a screen of
 * numbers, live, that the port draws over the interface. The log is a file
 * on the card with the same numbers in it, a line a second, so that
 * something that happens once in ten minutes is still there afterwards.
 *
 * The numbers are of the sound, of the frame clock and of the emulator.
 * The sound is the interesting one: there are three things that can go
 * wrong between the APU and the speaker - the APU can make no sound, the
 * transport can lose it, and the service can refuse to play it - and they
 * look exactly the same from the outside. The test tone tells them apart.
 */
#pragma once

#include <stdbool.h>

/* Where the log is written on the card. */
#define DIAG_LOG_PATH "/gameyob.log"

/* The most lines the panel can show. */
#define DIAG_MAX_LINES 12

/* Prepare the diagnostics. Nothing is written until logging is turned on. */
void diagInit(void);
void diagExit(void);

/* Whether the log is being written, and where it is written to. */
bool diagLogging(void);
void diagSetLogging(bool on);
const char *diagLogPath(void);

/* A line in the log. Does nothing when the log is off. */
void diagLog(const char *fmt, ...);

/* Called once a frame. Measures the frame time, and writes a line a
 * second to the log while the log is on. */
void diagTick(void);

/* Everything there is to say, written out in full. */
void diagDump(void);

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
