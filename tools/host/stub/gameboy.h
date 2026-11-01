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

/* The Game Boy's clock, in cycles a second. The real gameboy.h defines
 * this, and the APU needs it. */
#define clockSpeed 4194304

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

    /* The APU reads and writes the sound registers through these. The
     * real readIO masks a few of them back to ones, the way the hardware
     * does; the ones that matter to the sound test are the ones the APU
     * reads back after it has written them. */
    u8 readIO(u8 ioReg)
    {
        switch (ioReg)
        {
            case 0x10: return ioRam[ioReg] | 0x80;
            case 0x11: return ioRam[ioReg] | 0x3F;
            case 0x13: return 0xFF;
            case 0x14: return ioRam[ioReg] | 0xBF;
            case 0x16: return ioRam[ioReg] | 0x3F;
            case 0x18: return 0xFF;
            case 0x19: return ioRam[ioReg] | 0xBF;
            case 0x1C: return ioRam[ioReg] | 0x3F;
            case 0x1E: return 0xFF;
            case 0x1F: return ioRam[ioReg] | 0xBF;
            case 0x21: return 0xFF;
            case 0x22: return ioRam[ioReg] | 0x3F;
            case 0x24: return ioRam[ioReg] | 0x7F;
            default:   return ioRam[ioReg];
        }
    }

    inline void setSoundChannel(int which) { ioRam[0x26] |= which; }
    inline void clearSoundChannel(int which) { ioRam[0x26] &= ~which; }
};
