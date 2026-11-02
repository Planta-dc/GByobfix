/*
 * The diagnostics: the port's own numbers, on the screen, on the game
 * picture and in a file.
 *
 * See diag.h for what this is for.
 */
#include "diag.h"

#include <3ds.h>
#include <3ds/os.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "3dsgfx.h"
#include "frameclock.h"
#include "gbmanager.h"
#include "gameboy.h"
#include "io.h"
#include "menu.h"

/*
 * The log.
 *
 * It is a file on the card, in the root of it, so that it can be found
 * without knowing where the games are.
 *
 * The lines are buffered in memory and written to the card in pieces.
 * That is not an optimisation, it is the whole point: a write of a
 * kilobyte or two to a slow card has been measured at tens of
 * milliseconds - GBabe's port measured 44 ms for 1.5 KiB on one - and
 * that is longer than a frame, so writing every line as it happens
 * would cost a dropped frame and a hitch in the sound every time.
 *
 * So the buffer is 8 KiB, which is about twelve seconds of lines, and it
 * is written when it is three quarters full or when five seconds have
 * passed. The things that are worth having even if the program falls
 * over - the start, a setting being changed, an error - are written at
 * once instead.
 *
 * The file stops growing at 384 KiB and says so in its last line: an
 * unbounded log on a card is a card filling up silently, and the first
 * minutes are the interesting ones.
 */
#define LOG_BUF 8192
#define LOG_MAX_BYTES (384 * 1024)
#define LOG_FLUSH_TICKS (SYSCLOCK_ARM11 * 5)

static FileHandle *logFile;
static bool logging;
static char logBuffer[LOG_BUF];
static size_t logUsed;
static size_t logWritten;
static u64 logStartTick;
static u64 logLastWrite;
static bool logTruncated;

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
static u32 worstPeriodUs;
static u32 lateFrames;
static u32 lateTotal;

/* Whether the diagnostics panel is on the screen. */
static bool panelOpen;

/* The panel's lines. The panel is redrawn every frame, so they are made
 * fresh every time and the array belongs to this module. */
static char lineText[DIAG_MAX_LINES][64];
static const char *linePointers[DIAG_MAX_LINES];

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
    logUsed = 0;
    logWritten = 0;
    logTruncated = false;

    lastTick = 0;
    frameCount = 0;
    lastPeriodUs = 0;
    avgPeriodUs = 0;
    worstPeriodUs = 0;
    lateFrames = 0;
    lateTotal = 0;
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

/* Seconds since the log started. */
static double secondsSinceStart(void)
{
    return (double)(svcGetSystemTick() - logStartTick) / SYSCLOCK_ARM11;
}

/* Put what is in the buffer on the card. */
static void writeOut(void)
{
    if (!logFile || logUsed == 0)
        return;

    file_write_buffered(logBuffer, 1, logUsed, logFile);
    file_flush(logFile);

    logWritten += logUsed;
    logUsed = 0;
    logLastWrite = svcGetSystemTick();
}

/*
 * Open or close the log.
 *
 * The file is started again from the beginning every time the log is
 * turned on, so that the numbers in it are all from one run. If it
 * cannot be opened the log is simply off: there is nowhere to put it.
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

        logUsed = 0;
        logWritten = 0;
        logTruncated = false;
        logStartTick = svcGetSystemTick();
        logLastWrite = logStartTick;
        logging = true;

        /* A log has to say which program wrote it: one version number
         * covers a lot of builds. */
        diagLogUrgent("# GameYob 3DS %s debug log\n", VERSION_STRING);
        diagLogUrgent("# console time %llu\n", (unsigned long long)osGetTime());
    }
    else {
        logging = false;

        if (logFile) {
            writeOut();
            file_flush(logFile);
            file_close(logFile);
            logFile = NULL;
        }
    }
}

/*
 * Add a line to the buffer.
 *
 * One whole line at a time, so that a timestamp always starts a line.
 */
static void appendLine(const char *text, bool urgent)
{
    char entry[320];
    int n;

    if (!logFile || !logging || logTruncated)
        return;

    n = snprintf(entry, sizeof entry, "[%9.3f] %s\n", secondsSinceStart(),
                 text);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof entry)
        n = (int)sizeof entry - 1;

    if (logUsed + (size_t)n > sizeof logBuffer) {
        writeOut();
        if (logTruncated)
            return;
    }

    memcpy(logBuffer + logUsed, entry, (size_t)n);
    logUsed += (size_t)n;

    if (logWritten + logUsed > LOG_MAX_BYTES) {
        /* Say why the file stops here, rather than just ending. */
        writeOut();
        logTruncated = true;

        file_printf(logFile, "[%9.3f] stopped: %u KiB limit reached\n",
                    secondsSinceStart(),
                    (unsigned)(LOG_MAX_BYTES / 1024));
        file_flush(logFile);
        return;
    }

    if (urgent)
        writeOut();
}

void diagLog(const char *fmt, ...)
{
    char text[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    appendLine(text, false);
}

void diagLogUrgent(const char *fmt, ...)
{
    char text[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    appendLine(text, true);
}

/*
 * Write the buffer out, but only when it is worth a card write.
 *
 * The buffer is 8 KiB and a second of lines is about half a kilobyte, so
 * waiting until it is three quarters full means twelve seconds of lines
 * and one write, instead of a write a second and twelve times as many
 * hitches. Five seconds is the other end of it: if there is only a little
 * in the buffer, it goes out every five seconds anyway rather than
 * waiting for a crash to take it.
 */
void diagFlushSoft(void)
{
    u64 now = svcGetSystemTick();

    if (!logFile || logUsed == 0)
        return;

    if (logUsed * 4 < sizeof logBuffer * 3 &&
        now - logLastWrite < LOG_FLUSH_TICKS)
        return;

    writeOut();
}

void diagFlush(void)
{
    writeOut();
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

        if (microseconds > worstPeriodUs)
            worstPeriodUs = microseconds;

        if (microseconds > LATE_US) {
            lateFrames++;
            lateTotal++;
        }
    }

    lastTick = now;
    frameCount++;

    /* The window closes every second. The worst frame and the late frames
     * are counted over it, so that a fault an hour ago does not sit in
     * the numbers for ever. */
    if ((frameCount % 60) == 0) {
        worstPeriodUs = 0;
        lateFrames = 0;
    }

    /* The heartbeat: once a second, which is once every sixty frames. */
    if (logging && logFile && (frameCount % 60) == 0) {
        audio_stats stats;
        audio_get_stats(&stats);

        diagLog("%6llu f  %5u us  late %u  |  sound %s  ch %d,%d/%d,%d  "
                "blk %d (%d ms)  peak %d  level %d%%  gain %d\n",
                (unsigned long long)frameCount, avgPeriodUs, lateFrames,
                stats.running ? "on" : "off", stats.channelLeft,
                stats.channelLeftAlt, stats.channelRight,
                stats.channelRightAlt, stats.blockFrames, stats.latencyMs,
                stats.peak, stats.level, stats.gain);
    }
}

void diagGetFrameStats(diag_frame_stats *out)
{
    if (!out)
        return;

    out->frames = frameCount;
    out->avgUs = avgPeriodUs;
    out->lastUs = lastPeriodUs;
    out->worstUs = worstPeriodUs;
    out->lateFrames = lateFrames;
    out->lateTotal = lateTotal;
    out->fps = avgPeriodUs ? (1000000.0f / (float)avgPeriodUs) : 0.0f;
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
    APPEND("channel mask %08x  in use %08x\n", stats.channelMask,
           stats.channelsUsed);
    APPEND("channels  left %d %d   right %d %d   %s\n", stats.channelLeft,
           stats.channelLeftAlt, stats.channelRight, stats.channelRightAlt,
           stats.mono ? "mono" : "stereo");
    APPEND("test tone %d Hz\n", stats.testToneHz);
    APPEND("block %d frames  %d ms\n", stats.blockFrames, stats.latencyMs);
    APPEND("peak %d  left %d  right %d  level %d%%\n", stats.peak,
           stats.peakLeft, stats.peakRight, stats.level);
    APPEND("frames %llu  blocks %llu  gain %d\n",
           (unsigned long long)stats.frames,
           (unsigned long long)stats.blocks, stats.gain);

    if (fresh.readable) {
        for (int ear = 0; ear < 2; ear++) {
            for (int buf = 0; buf < 2; buf++) {
                APPEND("ear %d buffer %d  active %d  regs %08x %08x %08x\n",
                       ear, buf, fresh.active[ear][buf],
                       fresh.reg[ear][buf][0], fresh.reg[ear][buf][1],
                       fresh.reg[ear][buf][2]);
            }
        }
    }
    else {
        APPEND("the service would not say anything about the channels\n");
    }

    APPEND("frame period %u us  last %u us  max %u us  late %u of %u\n",
           avgPeriodUs, lastPeriodUs, worstPeriodUs, lateFrames, lateTotal);
    APPEND("paused %s\n", mgr_isPaused() ? "yes" : "no");

    file_printf(logFile, "%s", block);
    file_flush(logFile);

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

    /* The four channels: the two buffers of each ear, and each buffer's
     * own channel. */
    snprintf(lineText[n], sizeof lineText[n], "sound    %s%s%s  ch %d,%d/%d,%d",
             stats.running ? "running" : "stopped",
             audio_is_muted() ? " muted" : "",
             soundDisabled ? " apu off" : "",
             stats.channelLeft, stats.channelLeftAlt, stats.channelRight,
             stats.channelRightAlt);
    n++;

    /* Whether the service says each channel is playing. A channel that
     * is set up perfectly and is not playing is the thing to look for. */
    snprintf(lineText[n], sizeof lineText[n], "mask     %08x  act %d%d/%d%d",
             stats.channelMask, hardware.active[0][0], hardware.active[0][1],
             hardware.active[1][0], hardware.active[1][1]);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "regs     L %08x  R %08x",
             hardware.reg[0][0][0], hardware.reg[1][0][0]);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "block    %d frames  %d ms",
             stats.blockFrames, stats.latencyMs);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "level    %d%%  peak %d",
             stats.level, stats.peak);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "blocks   %llu  gain %d",
             (unsigned long long)stats.blocks, stats.gain);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "frames   %llu",
             (unsigned long long)stats.frames);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "tone     %s",
             stats.testToneHz ? "on" : "off");
    n++;

    snprintf(lineText[n], sizeof lineText[n], "frame    %u us  max %u us",
             avgPeriodUs, worstPeriodUs);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "late     %u of %llu", lateFrames,
             (unsigned long long)lateTotal);
    n++;

    snprintf(lineText[n], sizeof lineText[n], "rom      %s", romName);
    n++;

    /* Where the picture is on the screen. A picture in the wrong place
     * is easy to see and hard to describe, and this says exactly where
     * it went. */
    {
        int px, py, pw, ph;
        gfxPictureRect(&px, &py, &pw, &ph);
        snprintf(lineText[n], sizeof lineText[n], "picture  %dx%d at %d,%d",
                 pw, ph, px, py);
        n++;
    }

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
