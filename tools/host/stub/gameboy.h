/*
 * Just enough of the emulator's Gameboy class for the renderer's tests.
 *
 * The real class is in platform/common/include/gameboy.h and pulls in the
 * whole emulator. The renderer only touches the video memory, the high
 * memory (which is where the OAM and the registers live) and the two
 * palettes, so this is a copy of those parts with the same layout.
 */
#pragma once

#include <3ds.h>
#include <string.h>

#define GB  0
#define CGB 1

class Gameboy;

extern Gameboy* gameboy;

class Gameboy {
public:
    u8 vram[2][0x2000];
    u8 highram[0x1000];
    u8* hram;      // The OAM and the registers: highram + 0xE00.
    u8* ioRam;     // The registers: highram + 0xF00.
    u8 bgPaletteData[0x40];
    u8 sprPaletteData[0x40];
    int gbMode;

    Gameboy()
        : hram(highram + 0xe00), ioRam(highram + 0xf00), gbMode(GB)
    {
        memset(vram, 0, sizeof(vram));
        memset(highram, 0, sizeof(highram));
        memset(bgPaletteData, 0, sizeof(bgPaletteData));
        memset(sprPaletteData, 0, sizeof(sprPaletteData));
    }
};
