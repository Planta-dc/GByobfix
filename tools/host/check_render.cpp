/*
 * Does the new renderer draw the same picture as the one it replaced?
 *
 * The two renderers are run over the same randomised Game Boy states and
 * every pixel of the picture is compared. Any difference is a difference
 * in what the player sees, so the test fails on the first one.
 *
 * The states are randomised rather than taken from a game because the
 * interesting cases are the odd ones: the window starting in the middle of
 * a line, a sprite hanging off the edge of the screen, the background
 * switched off with the sprite layer left over from the line before.
 *
 * Run it: make -C tools/host check
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gameboy.h"
#include "menu.h"
#include "3dsgfx.h"

/* The renderer that replaced the old one. */
void initGFX(void);
void refreshGFX(void);
void drawScanline_P2(int scanline);

/* The renderer that was replaced. */
namespace ref {
    void renderFrame(Gameboy* gb, u8* framebuffer);
    void resetState();
}

/* A random number generator of its own, so that the test does not depend
 * on the library's. */
static u32 rngState = 1;

static u32 nextRandom(void)
{
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

/* Put a random Game Boy state together. */
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

/* Draw one frame with the new renderer into a framebuffer. */
static void renderNew(Gameboy* gb, u8* framebuffer)
{
    gameboy = gb;

    initGFX();
    refreshGFX();

    for (int scanline = 0; scanline < 144; scanline++)
        drawScanline_P2(scanline);

    /* The same place the old renderer drew: the middle of the top screen,
     * at one to one. */
    gfxBlitGameFrame(framebuffer, 400, 240, gameFrame, 120, 48, 1, 1);
}

int main(int argc, char* argv[])
{
    int trials = 200;
    if (argc > 1)
        trials = atoi(argv[1]);

    static u8 reference[400 * 240 * 3];
    static u8 replacement[400 * 240 * 2];

    int differences = 0;
    int firstX = -1, firstY = -1, firstTrial = -1;
    u32 firstRef = 0, firstNew = 0;

    Gameboy gb;

    for (int trial = 0; trial < trials; trial++) {
        bool cgb = (trial % 3) != 0;
        randomState(&gb, (u32)trial + 1, cgb);

        memset(reference, 0, sizeof(reference));
        memset(replacement, 0, sizeof(replacement));

        /* Both renderers keep the state of the scanlines they have drawn,
         * and that state is not the same in two different programs, so it
         * is put back to the start before each frame. */
        ref::resetState();

        ref::renderFrame(&gb, reference);
        renderNew(&gb, replacement);

        for (int y = 48; y < 48 + 144; y++) {
            for (int x = 120; x < 120 + 160; x++) {
                /* The framebuffer holds the picture on its side. The
                 * old renderer draws 24 bit pixels and the port's
                 * renderer draws 16 bit ones, so the old one is packed
                 * into 16 bits before the two are compared. */
                int offset = (x * 240 + (239 - y));
                u32 refPixel = (reference[offset * 3 + 2] << 16) |
                               (reference[offset * 3 + 1] << 8) |
                               reference[offset * 3];
                u32 newPixel = ((u32)replacement[offset * 2 + 1] << 8) |
                               replacement[offset * 2];

                u16 refPacked = (u16)(((refPixel >> 16 & 0xff) >> 3) << 11) |
                                (u16)(((refPixel >> 8 & 0xff) >> 2) << 5) |
                                (u16)((refPixel & 0xff) >> 3);

                if (refPacked != newPixel) {
                    if (!differences) {
                        firstX = x;
                        firstY = y;
                        firstRef = refPacked;
                        firstNew = newPixel;
                        firstTrial = trial;
                    }
                    differences++;
                }
            }
        }
    }

    if (differences) {
        printf("FAIL: %d pixels differ over %d frames.\n", differences, trials);
        printf("  first difference at %d,%d in frame %d: old %06x new %06x\n",
               firstX, firstY, firstTrial, firstRef, firstNew);
        return 1;
    }

    printf("PASS: %d frames, %d pixels each, all identical.\n",
           trials, 160 * 144);
    return 0;
}
