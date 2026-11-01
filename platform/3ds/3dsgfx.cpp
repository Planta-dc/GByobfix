/*
 * Framebuffer helpers and the blit.
 *
 * The 3DS framebuffer is rotated (see 3dsgfx.h), so writing a line of the
 * picture into it costs one cache miss per pixel. The blit therefore works
 * in 8x8 blocks: the block is read from the picture eight bytes at a time
 * and written to the framebuffer one column at a time, so both the read and
 * the write stay inside a cache line.
 */

#include <3ds.h>
#include <string.h>
#include <3ds/services/gspgpu.h>
#include "3dsgfx.h"
#include "menu.h"

/* The picture is scaled by this much when it is put on the screen. 3/2
 * gives 240x216, which fills the height of the screen and leaves a border
 * on the sides. */
#define BLIT_SCALE_NUM 3
#define BLIT_SCALE_DEN 2

/* The block the blit works in. */
#define BLIT_TILE 8

void drawPixel(u8* framebuffer, int x, int y, u32 color) {
    u16* ptr = (u16*)framebuffer + (x*GFX_FB_STRIDE + (GFX_FB_STRIDE - 1 - y));

    *ptr = (u16)color;
}

u8* gfxGetGameFramebuffer(void) {
    u16 width, height;
    gfxScreen_t screen = gameScreen ? GFX_BOTTOM : GFX_TOP;
    return gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);
}

int gfxGetGameScreenWidth(void) {
    return gameScreen ? BOTTOM_SCREEN_WIDTH : TOP_SCREEN_WIDTH;
}

int gfxGetGameScreenHeight(void) {
    return TOP_SCREEN_HEIGHT;
}

void gfxClearScreenFrame(void) {
    u8* framebuffer = gfxGetGameFramebuffer();
    size_t size = gameScreen ? BOTTOM_FRAMEBUFFER_SIZE : TOP_FRAMEBUFFER_SIZE;

    memset(framebuffer, 0, size);
}

void gfxBlitGameFrame(u8* dest, int destWidth, int destHeight,
                      const u8* src, int originX, int originY, int num, int den)
{
    for (int ty = 0; ty < GB_HEIGHT; ty += BLIT_TILE) {
        int rows = (ty + BLIT_TILE <= GB_HEIGHT) ? BLIT_TILE : GB_HEIGHT - ty;

        /* The destination rows the rows of this block cover. */
        int rowStart[BLIT_TILE + 1];
        for (int r = 0; r <= rows; r++)
            rowStart[r] = originY + ((ty + r) * num) / den;

        for (int tx = 0; tx < GB_WIDTH; tx += BLIT_TILE) {
            int cols = (tx + BLIT_TILE <= GB_WIDTH) ? BLIT_TILE : GB_WIDTH - tx;

            /* Read the block. The picture is a row at a time, so this is
             * one cache line per row of the block. */
            u8 tile[BLIT_TILE][BLIT_TILE];
            for (int r = 0; r < rows; r++)
                memcpy(tile[r], src + (ty + r) * GB_WIDTH + tx, cols);

            for (int c = 0; c < cols; c++) {
                int sx = tx + c;
                int dx0 = originX + (sx * num) / den;
                int dx1 = originX + ((sx + 1) * num) / den;

                for (int dx = dx0; dx < dx1; dx++) {
                    if (dx < 0 || dx >= destWidth)
                        continue;   /* off the side: skip it, do not clamp */

                    int yTop = rowStart[rows] - 1;
                    int yBot = rowStart[0];
                    if (yTop < 0 || yBot >= destHeight)
                        continue;   /* the whole column is off the screen */

                    int yFrom = (yBot < 0) ? 0 : yBot;
                    int yTo = (yTop >= destHeight) ? destHeight - 1 : yTop;
                    if (yFrom > yTo)
                        continue;

                    /* The framebuffer runs bottom up, so the pointer moves
                     * forward as the row number falls. */
                    u16* p = (u16*)dest + (dx * GFX_FB_STRIDE + (GFX_FB_STRIDE - 1 - yTo));

                    int r = rows - 1;
                    while (r > 0 && rowStart[r] > yTo)
                        r--;

                    for (int y = yTo; y >= yFrom; y--) {
                        while (r > 0 && rowStart[r] > y)
                            r--;

                        *p++ = palColour[tile[r][c]];
                    }
                }
            }
        }
    }
}

void gfxPresentGameFrame(void) {
    u8* framebuffer = gfxGetGameFramebuffer();
    int width = gfxGetGameScreenWidth();
    int height = gfxGetGameScreenHeight();
    int scaledWidth = GB_WIDTH * BLIT_SCALE_NUM / BLIT_SCALE_DEN;
    int scaledHeight = GB_HEIGHT * BLIT_SCALE_NUM / BLIT_SCALE_DEN;

    /* The framebuffers are doubled, and each one holds the picture from
     * two frames ago. The picture does not cover the whole screen, so
     * without this the area around it would show the old picture. */
    gfxClearScreenFrame();

    gfxBlitGameFrame(framebuffer, width, height, gameFrame,
                     (width - scaledWidth) / 2, (height - scaledHeight) / 2,
                     BLIT_SCALE_NUM, BLIT_SCALE_DEN);

    /* The display controller reads the framebuffer over the bus, and the
     * processor's copy of it is in its cache, so what was drawn has to be
     * written out before the frame is shown. Without this the screen
     * shows the picture as it was, or as it was a few frames ago. */
    GSPGPU_FlushDataCache(framebuffer,
                          gameScreen ? BOTTOM_FRAMEBUFFER_SIZE : TOP_FRAMEBUFFER_SIZE);

    /* The screen is not swapped here. The swap is done once per frame,
     * where the console is as well, because a second swap in the same
     * frame puts the other framebuffer on the screen: the one the picture
     * was not drawn into. */
}
