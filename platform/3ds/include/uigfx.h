/*
 * A small software drawing kit for the interface.
 *
 * Everything the interface needs is drawn into a 320x240 canvas in ordinary
 * memory, which is then copied to the screen in one go. The 3DS GPU is not
 * involved: the interface is redrawn only when something changes, so a plain
 * rectangle and text rasteriser costs far less than setting up a texture
 * pipeline would, and it keeps the whole interface in one file with no GPU
 * state to get wrong.
 *
 * Text is anti-aliased from pre-baked 4-bit coverage atlases (uifont.cpp).
 *
 * The drawing kit, the fonts and the screens come from GBabe, the other
 * Game Boy emulator for the 3DS, which is MIT licensed. Only the look was
 * taken; this is the port's own code, and it draws GameYob's own menu.
 */
#pragma once

#include <3ds.h>

#include <stdbool.h>
#include <stdint.h>

#define UI_W 320
#define UI_H 240

/* One glyph: top-left relative to the drawing origin, size, advance, and
 * offset into the font's coverage data. */
typedef struct
{
	int8_t x, y;
	uint8_t w, h;
	uint8_t adv;
	uint32_t off;
} ui_glyph;

typedef struct
{
	int height; /* line height */
	int ascent;  /* pixels above the baseline */
	const ui_glyph *g;
	const unsigned char *bits;
} ui_font;

extern const ui_font ui_font_small, ui_font_body, ui_font_bold, ui_font_title,
	ui_font_big;

#define UI_RGB(r, g, b) \
	((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

extern uint16_t ui_canvas[UI_W * UI_H];

void ui_clear(uint16_t c);
void ui_fill(int x, int y, int w, int h, uint16_t c);
void ui_fill_alpha(int x, int y, int w, int h, uint16_t c, int alpha);
void ui_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void ui_rrect(int x, int y, int w, int h, int r, uint16_t c);
void ui_rrect_outline(int x, int y, int w, int h, int r, int t, uint16_t c);
void ui_circle(int cx, int cy, int r, uint16_t c);
/* dir: -1 left, 1 right, 2 up, -2 down */
void ui_arrow(int cx, int cy, int size, int dir, uint16_t c);

int ui_text_w(const ui_font *f, const char *s);
/* Draws s with its top-left at (x, y), clipped to max_w pixels with "..."
 * when max_w > 0. Returns the x after the text. */
int ui_text(const ui_font *f, int x, int y, const char *s, uint16_t c,
	    int max_w);
void ui_text_center(const ui_font *f, int cx, int y, const char *s, uint16_t c);
void ui_text_right(const ui_font *f, int rx, int y, const char *s, uint16_t c);
/* Vertically centred in a box of height h starting at y. */
int ui_text_vc(const ui_font *f, int x, int y, int h, const char *s, uint16_t c,
	       int max_w);

/*
 * The screen the interface is on.
 *
 * The game is on the other one, so the interface can be on either. The
 * canvas is 320 wide and the top screen is 400, so on the top screen the
 * canvas sits in the middle.
 */
void uiSetScreen(gfxScreen_t screen);
gfxScreen_t uiGetScreen(void);

/*
 * Copy the canvas to the screen.
 *
 * The screen is not swapped here: the port swaps once a frame, where the
 * game picture and the console are as well, so that the picture is not put
 * on the screen and then taken off it again in the same frame.
 */
void ui_present(void);
