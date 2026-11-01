/*
 * How much faster is the new renderer than the old one?
 *
 * Both are run over the same randomised Game Boy states, and the time each
 * one takes to draw a frame is reported.
 *
 * This is a development machine, not a 3DS, so the numbers are not the
 * numbers the console will see. What they are good for is the shape of the
 * result: how much of the frame's time went into the drawing, and how much
 * of the old drawing was the part that has been taken out.
 *
 * Run it: make -C tools/host bench
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gameboy.h"
#include "menu.h"
#include "3dsgfx.h"

void initGFX(void);
void refreshGFX(void);
void drawScanline_P2(int scanline);

namespace ref {
    void renderFrame(Gameboy* gb, u8* framebuffer);
    void resetState();
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

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 0x2000; j++)
            gb->vram[i][j] = (u8)nextRandom();
    for (int i = 0; i < 0x40; i++) {
        gb->bgPaletteData[i] = (u8)nextRandom();
        gb->sprPaletteData[i] = (u8)nextRandom();
    }
    for (int i = 0; i < 0xA0; i++)
        gb->hram[i] = (u8)nextRandom();

    u8 control = (u8)(nextRandom() & 0x7F);
    switch (seed % 6) {
        case 0: control |= 0x01; break;
        case 1: control |= 0x02; break;
        case 2: control |= 0x04; break;
        case 3: control |= 0x08 | 0x10; break;
        case 4: control |= 0x20 | 0x40; break;
        default: control |= 0x80; break;
    }
    gb->ioRam[0x40] = control;
    gb->ioRam[0x42] = (u8)nextRandom();
    gb->ioRam[0x43] = (u8)nextRandom();
    gb->ioRam[0x4A] = (u8)(nextRandom() % 160);
    gb->ioRam[0x4B] = (u8)(nextRandom() % 12);
    gb->ioRam[0x47] = (u8)nextRandom();
    gb->ioRam[0x48] = (u8)nextRandom();
    gb->ioRam[0x4D] = 0xFF;
    gb->ioRam[0x68] = (u8)(nextRandom() & 0xF8);
    gb->ioRam[0x6A] = (u8)(nextRandom() & 0xF8);
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

int main(int argc, char* argv[])
{
    int frames = 300;
    if (argc > 1)
        frames = atoi(argv[1]);

    static u8 framebuffer[400 * 240 * 2];

    Gameboy gb;

    /* The states are made once and used for both renderers, so that they
     * are doing the same work. */
    static Gameboy states[64];
    for (int i = 0; i < 64; i++)
        randomState(&states[i], (u32)i + 1, (i % 3) != 0);

    /* The old renderer. */
    double start = now();
    for (int frame = 0; frame < frames; frame++) {
        Gameboy* state = &states[frame % 64];
        ref::resetState();
        ref::renderFrame(state, framebuffer);
    }
    double oldTime = now() - start;

    /* The new renderer, without the scaling. */
    gameboy = &states[0];
    initGFX();
    refreshGFX();

    start = now();
    for (int frame = 0; frame < frames; frame++) {
        gameboy = &states[frame % 64];
        for (int scanline = 0; scanline < 144; scanline++)
            drawScanline_P2(scanline);
    }
    double newTime = now() - start;

    /* The new renderer, with the scaling that puts it on the screen. */
    start = now();
    for (int frame = 0; frame < frames; frame++) {
        gameboy = &states[frame % 64];
        for (int scanline = 0; scanline < 144; scanline++)
            drawScanline_P2(scanline);
        gfxBlitGameFrame(framebuffer, 400, 240, gameFrame, 80, 12, 3, 2);
    }
    double newTimeWithBlit = now() - start;

    /* The scaling on its own. */
    start = now();
    for (int frame = 0; frame < frames; frame++)
        gfxBlitGameFrame(framebuffer, 400, 240, gameFrame, 80, 12, 3, 2);
    double blitTime = now() - start;

    double oldMs = oldTime * 1000.0 / frames;
    double newMs = newTime * 1000.0 / frames;
    double withBlitMs = newTimeWithBlit * 1000.0 / frames;

    printf("Old renderer:            %8.4f ms a frame\n", oldMs);
    printf("New renderer:            %8.4f ms a frame\n", newMs);
    printf("New renderer and blit:   %8.4f ms a frame\n", withBlitMs);
    printf("The blit on its own:     %8.4f ms a frame\n", blitTime * 1000.0 / frames);
    printf("The new renderer is %.2f times faster, %.2f times with the blit.\n",
           oldTime / newTime, oldTime / newTimeWithBlit);
    printf("A frame is 16.74 ms, so the old renderer used %.1f %% of it and "
           "the new one %.1f %%.\n",
           oldMs / 16.742706 * 100.0, withBlitMs / 16.742706 * 100.0);

    return 0;
}
