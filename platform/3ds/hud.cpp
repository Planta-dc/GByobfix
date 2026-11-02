/*
 * The status strip on the game picture.
 *
 * See hud.h for what this is for.
 *
 * The strip is drawn into the framebuffer the picture was just drawn into,
 * so it cannot fall behind the picture and does not need a present of its
 * own. It covers the top of the picture, which is the price of it.
 *
 * The framebuffer is on its side (see 3dsgfx.h), so a row of the strip is
 * a column of memory and a column of the strip is 480 bytes apart from
 * the next one. The strip is therefore put together in a buffer of its
 * own, where it is written a pixel at a time, and then copied across in
 * blocks of eight rows, the same way the picture is scaled: a block of
 * eight pixels is sixteen bytes and stays inside one cache line.
 */
#include "hud.h"

#include <stdio.h>
#include <string.h>

#include "3dsgfx.h"
#include "audio.h"
#include "diag.h"
#include "uigfx.h"
#include "uiscreens.h"

/* The strip: two lines of the small font. */
#define HUD_LINES 2
#define HUD_LINE_H 13
#define HUD_H (HUD_LINES * HUD_LINE_H)

/* The widest the strip can be: the bottom screen is the narrower one. */
#define HUD_W_MAX 320

static bool enabled;

/* The strip is put together here and then copied across. */
static u16 buffer[HUD_W_MAX * HUD_H];

void hudSetEnabled(bool on)
{
    enabled = on;
}

bool hudEnabled(void)
{
    return enabled;
}

static void plot(int x, int y, u16 c, int a)
{
    if (x < 0 || y < 0 || x >= HUD_W_MAX || y >= HUD_H || a <= 0)
        return;

    u16 *p = &buffer[y * HUD_W_MAX + x];
    *p = (a >= 256) ? c : ui_blend(*p, c, a);
}

/* One glyph, from the interface's own font. */
static void drawGlyph(const ui_font *f, const ui_glyph *g, int x, int y, u16 c)
{
    const unsigned char *bits = f->bits + g->off;

    for (int j = 0; j < g->h; j++) {
        for (int i = 0; i < g->w; i++) {
            unsigned idx = (unsigned)j * g->w + i;
            unsigned nib = (idx & 1) ? (bits[idx >> 1] >> 4)
                                     : (bits[idx >> 1] & 0x0F);
            if (nib)
                plot(x + g->x + i, y + g->y + j, c, (int)nib * 17);
        }
    }
}

/* A line of text. Returns where it ended. */
static int drawText(const ui_font *f, int x, int y, const char *s, u16 c)
{
    for (; *s != '\0'; s++) {
        int index = (*s < 32 || *s > 126) ? ('?' - 32) : (*s - 32);
        const ui_glyph *g = &f->g[index];

        drawGlyph(f, g, x, y, c);
        x += g->adv;

        if (x >= HUD_W_MAX)
            break;
    }

    return x;
}

/*
 * The strip.
 *
 * The first line is the frames: the rate, which is the one number that is
 * good or bad and so gets the colour, the time a frame takes, and the
 * worst frame of the last second.
 *
 * The second line is the sound: how loud the last block was, how loud the
 * loudest sample in it was, and how big a block is.
 */
void hudDraw(u16 *dest, int destWidth, int destHeight, int x, int y,
             int width, int height)
{
    diag_frame_stats frames;
    audio_stats audio;
    char rate[16];
    char rest[48];
    char sound[64];
    int stripH;
    int cx;

    if (!enabled || dest == NULL || width <= 0 || height <= 0)
        return;

    /* The buffer the strip is put together in is as wide as the widest
     * screen, and the picture is never wider than that. */
    if (width > HUD_W_MAX)
        width = HUD_W_MAX;

    stripH = (height < HUD_H) ? height : HUD_H;

    /*
     * Where the strip goes.
     *
     * The picture at its own size is a hundred and forty four pixels
     * high in a screen of two hundred and forty, so there is room above
     * it, and the strip goes there rather than over the game. When the
     * picture fills the screen there is no room, and the strip is drawn
     * over the top of it, which is the price of it.
     */
    if (y >= HUD_H)
        y -= HUD_H;

    diagGetFrameStats(&frames);
    audio_get_stats(&audio);

    snprintf(rate, sizeof rate, "%4.1f", (double)frames.fps);
    snprintf(rest, sizeof rest, "fps %4.1fms W%4.1f",
             (double)frames.avgUs / 1000.0, (double)frames.worstUs / 1000.0);
    /* The picture can be as narrow as a hundred and sixty pixels, which
     * is about twenty five characters of this font, so the line is kept
     * short. */
    snprintf(sound, sizeof sound, "snd %d%% pk%d blk%d",
             audio.level, audio.peak, audio.blockFrames);

    /* The background. The strip covers the picture rather than being
     * blended into it, so that the numbers are always readable over
     * whatever the game is drawing. */
    for (int row = 0; row < stripH; row++) {
        u16 *rowStart = &buffer[row * HUD_W_MAX];

        for (int col = 0; col < width; col++)
            rowStart[col] = C_BG;
    }

    u16 rateColour = (frames.fps >= 55.0f) ? C_GOOD : C_BAD;

    cx = drawText(&ui_font_small, 4, 1, rate, rateColour);
    drawText(&ui_font_small, cx, 1, rest, C_TEXT);
    drawText(&ui_font_small, 4, 1 + HUD_LINE_H, sound, C_TEXT);

    /* Across to the framebuffer. It is on its side, so a row of the strip
     * is a column of memory and the rows run the other way. */
    for (int col = 0; col < width; col++) {
        for (int row = 0; row < stripH; row += 8) {
            int rows = (row + 8 <= stripH) ? 8 : stripH - row;
            u16 *p = (u16 *)dest + (x + col) * GFX_FB_STRIDE +
                     (GFX_FB_STRIDE - 1 - (y + row));

            for (int r = 0; r < rows; r++)
                p[-r] = buffer[(row + r) * HUD_W_MAX + col];
        }
    }
}
