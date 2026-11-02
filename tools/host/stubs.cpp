/*
 * The globals the platform's own files expect the emulator to provide.
 *
 * They are here rather than in the tests so that every test links the
 * same way.
 */

#include "gameboy.h"
#include "menu.h"
#include "console.h"

/* The headers of the platform's own files, so that the definitions below
 * are linked the same way the real ones are. */
#include "diag.h"
#include "hud.h"

#include <stdarg.h>

Gameboy* gameboy = NULL;
int gameScreen = 0;
bool fastForwardMode = false;
bool fastForwardKey = false;
bool soundDisabled = false;

/* The sound's numbers, which the renderer puts in a panel on the screen.
 * There is no screen here, so they go nowhere. */
void uiDrawStats(const char* line1, const char* line2, const char* line3)
{
    (void)line1;
    (void)line2;
    (void)line3;
}

/* Whether the diagnostics panel is on the screen. There is no screen
 * here, so it never is. */
bool diagPanelOpen(void)
{
    return false;
}

/* The status strip on the game picture. There is no picture here, so it
 * goes nowhere. */
void hudDraw(u16* dest, int destWidth, int destHeight, int x, int y,
             int width, int height)
{
    (void)dest;
    (void)destWidth;
    (void)destHeight;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

/* The APU's console output. It is noise for a test, so it goes nowhere
 * except a counter that says it happened. */
int printLogCount = 0;

void printLog(const char* format, ...)
{
    (void)format;
    printLogCount++;
}

int dbgChanOn[4]; int dbgChanVol[4]; int dbgTo1[4]; int dbgTo2[4];
int dbgSO1; int dbgSO2; int dbgPol0; int dbgFreq0; int dbgCount;
