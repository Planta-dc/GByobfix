#pragma once

#include <3ds/gfx.h>
#include <3ds/console.h>

/*
 * The console's own state.
 *
 * The console is libctru's PrintConsole, with two things added: the screen
 * it is on, and the flag that says it has to be redrawn. They are here
 * rather than in the struct because libctru owns the struct, and libctru's
 * console.h and this file are both included by the emulator's own code,
 * which means the name PrintConsole can only mean one thing.
 */
extern gfxScreen_t consoleScreen;
extern bool consoleRedraw;

void consoleInitBottom();
void consoleSetScreen(gfxScreen_t screen);
void consoleCheckFramebuffers();

/*
 * Called after the screens have been swapped.
 *
 * libctru keeps two framebuffers per screen and hands out the one that is
 * not on the display. The console draws into that one, so when the
 * buffers are swapped the console's text is suddenly in the buffer that is
 * no longer being drawn into. This copies the console's part of the screen
 * into the buffer that is now current, so that the text does not blink.
 */
void consoleAfterSwap();

const int CHAR_WIDTH = 8;
const int CHAR_HEIGHT = 8;
