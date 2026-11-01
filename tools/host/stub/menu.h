/*
 * The parts of the menu header the platform's own files use.
 *
 * The real menu.h is in platform/common/include and pulls in the whole
 * emulator's menu, which needs the game's save files and the disk.
 */
#pragma once

#include <3ds.h>

/* Which screen the game is on: 0 for the top one, 1 for the bottom one. */
extern int gameScreen;

/* Fast forward: the emulator runs as many frames as it can. */
extern bool fastForwardMode;
extern bool fastForwardKey;
