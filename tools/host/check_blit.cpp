/*
 * Does the blit put the picture where it is supposed to go?
 *
 * The blit scales a 160x144 picture of palette indices into the 3DS
 * framebuffer, which is turned on its side, in blocks so that it does not
 * miss the cache once per pixel.
 *
 * Three things have to be right:
 *
 *   - every pixel of the picture has to land on the right pixel of the
 *     screen, at every scale;
 *   - the area around the picture has to be left alone, so that the black
 *     border around the game stays black;
 *   - a picture that hangs off the edge of the screen has to be cut off,
 *     not folded back onto the screen.
 *
 * Run it: make -C tools/host check
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "3dsgfx.h"
#include "menu.h"
#include "gameboy.h"


/* The picture's colours, so that a pixel can be traced back to the index
 * it came from. The framebuffer is 16 bit, so the colour is a packed
 * RGB565 one, and the multiplier puts a bit in each of the three
 * channels. */
static u16 indexColour(u8 index)
{
    return (u16)(0x0201u * (index + 1));
}

static void setPalette(void)
{
    for (int i = 0; i < 65; i++)
        palColour[i] = indexColour((u8)i);
}

/* The picture: the indices run through the palette over and over, so that
 * a pixel landing in the wrong place is noticed. */
static void fillPicture(void)
{
    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++)
        gameFrame[i] = (u8)(i % 64);
}

/* The pixel of the framebuffer at a position on the screen. */
static u16 pixelAt(const u8* framebuffer, int x, int y)
{
    int offset = (x * 240 + (239 - y)) * 2;
    return (u16)(framebuffer[offset] | ((u32)framebuffer[offset + 1] << 8));
}

/* The index of the source pixel that a destination pixel should hold, or
 * -1 if the destination pixel is outside the picture. */
static int sourceAt(int dx, int dy, int originX, int originY, int num, int den)
{
    /* The column. */
    int sourceX = -1;
    for (int x = 0; x < GB_WIDTH; x++) {
        int from = originX + (x * num) / den;
        int to = originX + ((x + 1) * num) / den;
        if (dx >= from && dx < to) {
            sourceX = x;
            break;
        }
    }
    if (sourceX < 0)
        return -1;

    /* The row. */
    int sourceY = -1;
    for (int y = 0; y < GB_HEIGHT; y++) {
        int from = originY + (y * num) / den;
        int to = originY + ((y + 1) * num) / den;
        if (dy >= from && dy < to) {
            sourceY = y;
            break;
        }
    }
    if (sourceY < 0)
        return -1;

    return sourceY * GB_WIDTH + sourceX;
}

static int failures = 0;

static void checkGeometry(int screenWidth, int screenHeight,
                          int originX, int originY, int num, int den)
{
    static u8 framebuffer[400 * 240 * 2];
    static int source[400 * 240];

    memset(framebuffer, 0, sizeof(framebuffer));

    /* Where each pixel of the screen comes from. */
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++)
            source[y * 400 + x] = sourceAt(x, y, originX, originY, num, den);
    }

    gfxBlitGameFrame(framebuffer, screenWidth, screenHeight, gameFrame,
                     originX, originY, num, den);

    int wrongPlace = 0;
    int paintedOutside = 0;
    int missing = 0;

    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            int from = source[y * 400 + x];
            u16 colour = pixelAt(framebuffer, x, y);

            if (from < 0) {
                /* Nothing should be painted outside the picture. */
                if (colour != 0)
                    paintedOutside++;
                continue;
            }

            u16 expected = indexColour(gameFrame[from]);
            if (colour != expected) {
                if (wrongPlace < 3)
                    printf("  %dx%d at %d,%d: expected %04x, got %04x\n",
                           num, den, x, y, expected, colour);
                wrongPlace++;
            }
            else if (colour == 0) {
                missing++;
            }
        }
    }

    if (wrongPlace || paintedOutside || missing) {
        printf("FAIL %dx%d at %d,%d on a %dx%d screen: %d in the wrong place, "
               "%d painted outside the picture, %d missing\n",
               num, den, originX, originY, screenWidth, screenHeight,
               wrongPlace, paintedOutside, missing);
        failures++;
    }
}

int main(void)
{
    setPalette();
    fillPicture();

    /* The scales the port uses, and the ones it might use. */
    int scales[][2] = { {1, 1}, {3, 2}, {2, 1}, {5, 4}, {4, 3} };

    for (int s = 0; s < (int)(sizeof(scales) / sizeof(scales[0])); s++) {
        int num = scales[s][0];
        int den = scales[s][1];

        int width = GB_WIDTH * num / den;
        int height = GB_HEIGHT * num / den;

        /* The middle of the screen, which is where the port puts it. */
        checkGeometry(400, 240, (400 - width) / 2, (240 - height) / 2, num, den);

        /* The top left corner, and hanging off each edge. */
        checkGeometry(400, 240, 0, 0, num, den);
        checkGeometry(400, 240, -width / 2, 0, num, den);
        checkGeometry(400, 240, 400 - width / 2, 0, num, den);
        checkGeometry(400, 240, 0, -height / 2, num, den);
        checkGeometry(400, 240, 0, 240 - height / 2, num, den);
        checkGeometry(400, 240, -width, -height, num, den);
        checkGeometry(400, 240, 400, 240, num, den);

        /* The bottom screen is a different shape. */
        int bottomWidth = GB_WIDTH * num / den;
        checkGeometry(320, 240, (320 - bottomWidth) / 2, (240 - height) / 2, num, den);
    }

    if (failures) {
        printf("FAIL: %d geometries are wrong.\n", failures);
        return 1;
    }

    printf("PASS: every geometry puts the picture in the right place and "
           "leaves the rest of the screen alone.\n");
    return 0;
}
