/*
 * The globals the platform's own files expect the emulator to provide.
 *
 * They are here rather than in the tests so that every test links the
 * same way.
 */

#include "gameboy.h"
#include "menu.h"

Gameboy* gameboy = NULL;
int gameScreen = 0;
bool fastForwardMode = false;
bool fastForwardKey = false;
