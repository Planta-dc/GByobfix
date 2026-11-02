/*
 * The diagnostics: the port's own numbers, on the screen and in a file.
 *
 * See diag.h for what this is for.
 */
#include "diag.h"

#include <3ds.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "frameclock.h"
#include "gbmanager.h"
#include "gameboy.h"
#include "io.h"
#include "menu.h"

/*
 * The log.
 *
 * It is a file on the card, in the root of it, so that it can be found
 * without knowing where the games are. It is written only while the log is
 * on, and it is written a line at a time: the card is slow, and a flush
 * per line would be felt in the frame time.
 */
static FileHandle *logFile;
static bool logging;

/* The panel's lines. The panel is redrawn every frame, so they are made
 * fresh every time and the array belongs to this module. */
static char lineText[DIAG_MAX_LINES][64];
static const char *linePointers[DIAG_MAX_LINES];

/*
 * The frame time.
 *
 * The port keeps its own clock, which decides when a frame is due. What
 * this measures is the other thing: how long the frame actually took. A
 * frame that takes longer than its time is a frame that was late, and the
 * number of those says whether the emulator is keeping up with the
 * console.
 */
static u64 lastTick;
static u64 frameCount;
static u32 lastPeriodUs;
static u32 avgPeriodUs;
static u32 maxPeriodUs;
static u32 lateFrames;

/* Whether the diagnostics panel is on the screen. */
static bool panelOpen;

/*
 * What the sound service says about the channels.
 *
 * This is kept between calls rather than asked for every time: the panel
 * is redrawn every frame, and asking the service means a round trip to
 * the sound hardware, which is more than the panel needs. The numbers it
 * gives change slowly, so they are asked for a few times a second.
 */
static audio_hardware hardware;
static u64 hardwareAge;

/* The target: one Game Boy frame is 16.7427 ms. A frame that takes longer
 * than this by more than the clock's own slack is late. */
#define FRAME_TIME_US 16743
#define LATE_US (FRAME_TIME_US + 2000)

void diagInit(void)
{
    logging = false;
    logFile = NULL;
    lastTick = 0;
    frameCount = 0;
    lastPeriodUs = 0;
    avgPeriodUs = 0;
    maxPeriodUs = 0;
    lateFrames = 0;
    panelOpen = false;
    hardwareAge = 0;
}

void diagExit(void)
{
    diagSetLogging(false);
}

bool diagLogging(void)
{
    return logging;
}

const char *diagLogPath(void)
{
    return DIAG_LOG_PATH;
}

/*
 * Open or close the log.
 *
 * The file is started again from the beginning every time the log is
 * turned on, so that the numbers in it are all from one run.
 */
void diagSetLogging(bool on)
{
    if (on == logging)
        return;

    if (on) {
        /* The card is not a disk: the file is written and closed as it is
         * written, so a log that was not closed is still there. The old
         * one is removed first, or the new numbers would be written over
         * the old ones and the file would end up with both. */
        if (file_exists(DIAG_LOG_PATH))
            fs_deleteFile(DIAG_LOG_PATH);

        logFile = file_open(DIAG_LOG_PATH, "w");
        if (!logFile)
            return;

        logging = true;

        diagLog("GameYob 3DS diagnostics\n");
        diagLog("log started\n");
    }
    else {
        logging = false;

        if (logFile) {
            file_close(logFile);
            logFile = NULL;
        }
    }
}

/* A line in the log. */
void diagLog(const char *fmt, ...)
{
    char buffer[512];
    va_list args;

    if (!logging || !logFile)
        return;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);

    file_printf(logFile, "%s", buffer);
}

/*
 * The frame time, and the line a second.
 *
 * The heartbeat is what makes the log worth having: a fault that happens
 * once in a while is not there to be seen at the moment it happens, but it
 * is there in the line after it.
 */
void diagTick(void)
{
    u64 now = svcGetSystemTick();

    if (lastTick) {
        u64 period = now - lastTick;
        u32 microseconds = (u32)(frameClockTicksToNs(period) / 1000);

        lastPeriodUs = microseconds;

        /* A running average, so that one slow frame does not decide what
         * the average is. */
        if (avgPeriodUs == 0)
            avgPeriodUs = microseconds;
        else
            avgPeriodUs += (microseconds - avgPeriodUs) / 16;

        if (microseconds > maxPeriodUs)
            maxPeriodUs = microseconds;

        if (microseconds > LATE_US)
            lateFrames++;
    }

    lastTick = now;
    frameCount++;

    /* The heartbeat: once a second, which is once every sixty frames. */
    if (logging && logFile && (frameCount % 60) == 0) {
        audio_stats stats;
        audio_get_stats(&stats);

        diagLog("%6llu f  %5u us  late %u  |  sound %s  ch %d/%d  "
                "fill %d (%d ms)  peak %d  level %d%%  under %llu  drop %llu\n",
                (unsigned long long)frameCount, avgPeriodUs, lateFrames,
                stats.running ? "on" : "off", stats.channelLeft,
                stats.channelRight, stats.fillFrames, stats.latencyMs,
                stats.peak, stats.level, (unsigned long long)stats.underruns,
                (unsigned long long)stats.drops);
    }
}

/* Everything there is to say, written out in full. */
void diagDump(void)
{
    char block[2048];
    audio_stats stats;
    audio_hardware fresh;
    int used = 0;

/* Add a line to the block, and do not run off the end of it. */
#define APPEND(...)                                                           \
    do {                                                                      \
        if (used >= 0 && used < (int)sizeof block)                             \
            used += snprintf(block + used, sizeof block - used, __VA_ARGS__);  \
    } while (0)

    if (!logging || !logFile)
        return;

    audio_get_stats(&stats);
    audio_read_hardware(&fresh);

    APPEND("\n--- dump %llu frames ---\n", (unsigned long long)frameCount);

    APPEND("sound service %s\n", stats.running ? "running" : "stopped");
    APPEND("channel mask %08x  left %d  right %d\n", stats.channelMask,
           stats.channelLeft, stats.channelRight);
    APPEND("test tone %d Hz\n", stats.testToneHz);
    APPEND("ring %d frames  %d ms\n", stats.fillFrames, stats.latencyMs);
    APPEND("peak %d  left %d  right %d  level %d%%\n", stats.peak,
           stats.peakLeft, stats.peakRight, stats.level);
    APPEND("frames %llu  underruns %llu  drops %llu  corrections %llu\n",
           (unsigned long long)stats.frames,
           (unsigned long long)stats.underruns,
           (unsigned long long)stats.drops,
           (unsigned long long)stats.corrections);

    if (fresh.readable) {
        for (int ear = 0; ear < 2; ear++) {
            APPEND("ear %d  active %d  regs %08x %08x %08x\n", ear,
                   fresh.active[ear], fresh.reg[ear][0], fresh.reg[ear][1],
                   fresh.reg[ear][2]);
        }
    }
    else {
        APPEND("the service would not say anything about the channels\n");
    }

    APPEND("frame period %u us  last %u us  max %u us  late %u\n", avgPeriodUs,
           lastPeriodUs, maxPeriodUs, lateFrames);
    APPEND("paused %s\n", mgr_isPaused() ? "yes" : "no");

    file_printf(logFile, "%s", block);

#undef APPEND
}

/*
 * The panel's lines.
 *
 * The numbers are put together here rather than in the menu, because this
 * is the only place that knows about all of them.
 */
const char *const *diagLines(int *count)
{
    audio_stats stats;
    char romName[32];
    char romDetail[8];
    int n = 0;

    audio_get_stats(&stats);

    /* The service is asked a few times a second, not every frame. */
    if (frameCount - hardwareAge >= 15) {
        hardwareAge = frameCount;
        audio_read_hardware(&hardware);
    }

    if (gameboy && gameboy->isRomLoaded())
        gameboy->getRomInfo(romName, sizeof romName, romDetail,
                            sizeof romDetail);
    else
        strcpy(romName, "no rom");

    snprintf(lineText[n], sizeof lineText[n], "sound    %s%s%s   ch %d/%d",
             stats.running ? "running" : "stopped",
             audio_is_muted() ? " muted" : "",
             soundDisabled ? " apu off" : "",
             stats.channelLeft, stats.channelRight);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "mask     %08x  active %d/%d",
             stats.channelMask, hardware.active[0], hardware.active[1]);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "regs     L %08x  R %08x",
             hardware.reg[0][0], hardware.reg[1][0]);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "ring     %d frames  %d ms",
             stats.fillFrames, stats.latencyMs);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "level    %d%%  peak %d",
             stats.level, stats.peak);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "under %llu  drop %llu  corr %llu",
             (unsigned long long)stats.underruns,
             (unsigned long long)stats.drops,
             (unsigned long long)stats.corrections);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "frames   %llu",
             (unsigned long long)stats.frames);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "tone     %s",
             stats.testToneHz ? "on" : "off");
    n++;

    snprintf(lineText[n], sizeof lineText[n], "frame    %u us  max %u us",
             avgPeriodUs, maxPeriodUs);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "late     %u of %llu", lateFrames,
             (unsigned long long)frameCount);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "rom      %s", romName);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "log      %s  %s",
             DIAG_LOG_PATH, logging ? "on" : "off");
    n++;

    for (int i = 0; i < n; i++)
        linePointers[i] = lineText[i];

    *count = n;
    return linePointers;
}

bool diagPanelOpen(void)
{
    return panelOpen;
}

void diagSetPanelOpen(bool open)
{
    panelOpen = open;
}
