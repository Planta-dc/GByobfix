#pragma once

#include <3ds.h>

/* The Game Boy's picture. */
#define GB_WIDTH 160
#define GB_HEIGHT 144

/* The 3DS screens, in the orientation they are held in. The top screen is
 * 400x240 and the bottom one 320x240.
 *
 * The top screen has a second mode, the wide one, where it is 800x240:
 * the framebuffer is 240 by 800 there instead of 240 by 400. The port
 * leaves it off, so the numbers here are the ones the picture is laid out
 * for, and the screen's real size is asked of the SDK at runtime. */
#define TOP_SCREEN_WIDTH 400
#define TOP_SCREEN_HEIGHT 240
#define BOTTOM_SCREEN_WIDTH 320
#define BOTTOM_SCREEN_HEIGHT 240

/*
 * The framebuffer is rotated.
 *
 * The display controller reads the framebuffer a row at a time, 240 pixels
 * wide, and the screen it drives is 240 pixels wide and 400 tall. The
 * console is held sideways, so the picture on the screen is 400 across and
 * 240 down, and the framebuffer holds it turned on its side: a column of
 * the picture is contiguous in memory, and the columns run down memory.
 *
 * A picture pixel (x across, y down) is therefore at
 *
 *     (x * 240 + (239 - y)) * 2
 *
 * and a row of the picture is 480 bytes apart from the next one. This is
 * why the picture is scaled in blocks rather than a line at a time.
 */
#define GFX_FB_STRIDE 240

/*
 * The framebuffer is 16 bits per pixel.
 *
 * The 3DS can be given a framebuffer of 24 bits a pixel or 16. Sixteen is
 * what the interface is drawn in, and it is a third less for the bus to
 * carry: the picture is the same size on the screen, but the blit that
 * draws it writes two bytes a pixel instead of three, and the whole screen
 * is a third smaller in memory.
 */
#define GFX_BYTES_PER_PIXEL 2

void drawPixel(u8* framebuffer, int x, int y, u32 color);

/* The framebuffer the game is drawn into: the current one of the screen the
 * game is on. */
u8* gfxGetGameFramebuffer(void);

/* Clear the screen the game is on, so that the area around the picture is
 * black rather than whatever was there before. */
void gfxClearScreenFrame(void);

/*
 * Scale the finished picture into the framebuffer.
 *
 * src is GB_WIDTH x GB_HEIGHT palette indices, dest is a 3DS framebuffer,
 * and the picture is drawn at (originX, originY) scaled by num/den. The
 * picture is scaled in 8x8 blocks: the framebuffer is transposed, so a
 * block that is eight pixels wide and eight tall can be read in eight
 * cache lines and written in eight more, instead of the sixteen thousand
 * cache misses a line at a time would cost.
 *
 * Pixels that fall outside the screen are skipped, not clamped: the
 * picture is not allowed to smear over the edge of the display.
 */
void gfxBlitGameFrame(u8* dest, int destWidth, int destHeight,
                      const u8* src, int originX, int originY, int num, int den);

/*
 * The palette the blit reads: 64 colours, packed blue, green, red.
 *
 * The sixty-fifth entry is black. It is what a pixel looks like when
 * nothing has ever been drawn on it: the old renderer kept the colours
 * themselves in its scanline buffers, and the ones it had never written
 * were still black. This renderer keeps indices instead, and the index 64
 * means "never drawn".
 */
extern u16 palColour[65];

/* The finished picture, as palette indices. */
extern u8 gameFrame[GB_WIDTH * GB_HEIGHT];

/* Put the finished picture on the screen and show it. */
void gfxPresentGameFrame(void);

/*
 * Whether the picture is scaled up to fill the screen or drawn at its own
 * size in the middle of it.
 *
 * The original port drew it at its own size, and that is the default here.
 * The scaled picture is for the screens where a hundred and sixty pixels
 * across is too small to be worth looking at.
 */
void gfxSetPictureScaled(bool scaled);
bool gfxPictureScaled(void);

/* Where the picture is on the screen: its left edge, its top edge, and
 * how big it is. The picture is put on the screen at the end of every
 * frame, so this says where the last one went. */
void gfxPictureRect(int *x, int *y, int *width, int *height);

/* Start the frame clock. */
void gfxInitFrameClock(void);

/* Expand one row of tile data into eight palette indices.
 *
 * The low byte of the row holds the low bit of each pixel and the high
 * byte the high bit, most significant bit first. A tile that is flipped
 * horizontally reads its bits the other way round. */
static inline void expandTileRow(u32 low, u32 high, bool flipX, u8* out)
{
    if (flipX) {
        for (int j = 0; j < 8; j++) {
            int shift = j;
            out[j] = (u8)(((low >> shift) & 1) | (((high >> shift) & 1) << 1));
        }
    }
    else {
        for (int j = 0; j < 8; j++) {
            int shift = 7 - j;
            out[j] = (u8)(((low >> shift) & 1) | (((high >> shift) & 1) << 1));
        }
    }
}

inline u32 RGB24(int red, int green, int blue) {
    return red<<16 | green<<8 | blue;
}

/* The colour the framebuffer is written in: five bits of red, six of green
 * and five of blue. The SDK has the conversion; this is just a name for it
 * that reads the same as RGB24 above. */
inline u16 rgb565(int red, int green, int blue) {
    return RGB8_to_565(red, green, blue);
}
