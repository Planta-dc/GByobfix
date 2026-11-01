/*
 * A scratch program for working out why the two renderers disagree.
 *
 * It renders one frame with each of them and prints, for the scanlines
 * that differ, what each layer held and what the final pixel was.
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "gameboy.h"
#include "menu.h"
#include "3dsgfx.h"

void initGFX(void);
void refreshGFX(void);
void drawScanline_P2(int scanline);

namespace ref {
    void renderFrame(Gameboy* gb, u8* framebuffer);
    extern u8 bgPixels[256];
    extern u8 spritePixels[256];
    extern u8 spritePixelsLow[256];
    extern u8 bgPixelsLow[256];
    extern u32 bgPalettes[8][4];
    extern u32 sprPalettes[8][4];
}

static u32 rngState = 1;
static u32 nextRandom(void)
{
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

static void randomState(Gameboy* gb, u32 seed, bool cgb)
{
    rngState = seed * 2654435761u + 1;

    gb->gbMode = cgb ? CGB : GB;

    /* The video memory: tile data and both tile maps. */
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 0x2000; j++)
            gb->vram[i][j] = (u8)nextRandom();

    /* The palettes. */
    for (int i = 0; i < 0x40; i++) {
        gb->bgPaletteData[i] = (u8)nextRandom();
        gb->sprPaletteData[i] = (u8)nextRandom();
    }

    /* The object attribute memory: forty sprites of four bytes. */
    for (int i = 0; i < 0xA0; i++)
        gb->hram[i] = (u8)nextRandom();

    /* The registers. The control register decides what is drawn at all, so
     * it is given a mixture of settings rather than a random byte. */
    u8 control = (u8)(nextRandom() & 0x7F);
    switch (seed % 6) {
        case 0: control |= 0x01; break;              /* The background off. */
        case 1: control |= 0x02; break;              /* The sprites off. */
        case 2: control |= 0x04; break;              /* Big sprites. */
        case 3: control |= 0x08 | 0x10; break;       /* The other map and tiles. */
        case 4: control |= 0x20 | 0x40; break;       /* The window, on the other map. */
        default: control |= 0x80; break;             /* Everything on. */
    }
    gb->ioRam[0x40] = control;

    /* The scroll registers. */
    gb->ioRam[0x42] = (u8)nextRandom();
    gb->ioRam[0x43] = (u8)nextRandom();

    /* The window's position. The window's horizontal position is offset by
     * seven, and a value below seven makes the old port write before the
     * start of the line, so those are worth hitting. */
    gb->ioRam[0x4A] = (u8)(nextRandom() % 160);
    gb->ioRam[0x4B] = (u8)(nextRandom() % 12);

    /* The Game Boy's palette registers, which say which shade of grey each
     * colour number is. */
    gb->ioRam[0x47] = (u8)nextRandom();
    gb->ioRam[0x48] = (u8)nextRandom();

    /* The registers the renderer does not use, but which the emulator
     * would have set to something. */
    gb->ioRam[0x4D] = 0xFF;
    gb->ioRam[0x68] = (u8)(nextRandom() & 0xF8);
    gb->ioRam[0x6A] = (u8)(nextRandom() & 0xF8);
}

int main(int argc, char* argv[])
{
    int trial = (argc > 1) ? atoi(argv[1]) : 1;

    static u8 reference[400 * 240 * 3];
    static u8 replacement[400 * 240 * 3];

    Gameboy gb;
    randomState(&gb, (u32)trial + 1, (trial % 3) != 0);

    printf("LCDC=%02x SCX=%02x SCY=%02x WX=%02x WY=%02x BGpal=%02x SPRpal=%02x mode=%d\n",
           gb.ioRam[0x40], gb.ioRam[0x43], gb.ioRam[0x42], gb.ioRam[0x4B], gb.ioRam[0x4A],
           gb.ioRam[0x47], gb.ioRam[0x48], gb.gbMode);

    memset(reference, 0, sizeof(reference));
    memset(replacement, 0, sizeof(replacement));

    ref::renderFrame(&gb, reference);

    gameboy = &gb;
    initGFX();
    refreshGFX();
    for (int scanline = 0; scanline < 144; scanline++)
        drawScanline_P2(scanline);
    gfxBlitGameFrame(replacement, 400, 240, gameFrame, 120, 48, 1, 1);

    printf("reference bgPalettes[0]: %06x %06x %06x %06x\n",
           ref::bgPalettes[0][0], ref::bgPalettes[0][1], ref::bgPalettes[0][2], ref::bgPalettes[0][3]);
    printf("new      palColour[0..3]: %06x %06x %06x %06x\n",
           palColour[0], palColour[1], palColour[2], palColour[3]);

    int shown = 0;
    for (int y = 48; y < 48 + 144 && shown < 20; y++) {
        for (int x = 120; x < 120 + 160; x++) {
            int offset = (x * 240 + (239 - y)) * 3;
            u32 refPixel = (reference[offset + 2] << 16) | (reference[offset + 1] << 8) | reference[offset];
            u32 newPixel = (replacement[offset + 2] << 16) | (replacement[offset + 1] << 8) | replacement[offset];
            if (refPixel == newPixel)
                continue;

            int line = y - 48;
            int col = x - 120;
            printf("x=%d y=%d (line %d col %d)\n", x, y, line, col);
            printf("  reference: bg=%d spr=%d bgLow=%d sprLow=%d\n",
                   ref::bgPixels[col], ref::spritePixels[col],
                   ref::bgPixelsLow[col], ref::spritePixelsLow[col]);
            printf("  new index: %d   colours %06x vs %06x\n",
                   gameFrame[line * 160 + col], refPixel, newPixel);
            shown++;
            break;
        }
    }
    return 0;
}
