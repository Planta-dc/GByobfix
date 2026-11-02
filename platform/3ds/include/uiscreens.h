/*
 * What the interface looks like: the file browser and the menu.
 *
 * Pure drawing, no state beyond layout constants: the caller owns the
 * selection, the scroll offset and the settings.
 *
 * The layout language - dark panels, an accent colour, value pills, a
 * footer of key hints - comes from GBabe, the other Game Boy emulator for
 * the 3DS, which is MIT licensed.
 */
#pragma once

#include <stdbool.h>

#include "uigfx.h"

#define UI_HEADER_H 30

/*
 * The interface's colours.
 *
 * They are here rather than in the drawing file because the status strip
 * on the game picture uses the same ones: the strip is drawn over the
 * picture with the same palette the panels are drawn with, so that the
 * two look like one program.
 */
#define C_BG      UI_RGB(14, 17, 23)
#define C_HEADER  UI_RGB(23, 28, 39)
#define C_PANEL   UI_RGB(24, 30, 41)
#define C_PANEL2  UI_RGB(31, 38, 52)
#define C_LINE    UI_RGB(44, 53, 70)
#define C_TEXT    UI_RGB(236, 239, 244)
#define C_DIM     UI_RGB(139, 149, 167)
#define C_ACCENT  UI_RGB(78, 141, 255)
#define C_ACCENT2 UI_RGB(143, 107, 255)
#define C_SEL     UI_RGB(30, 45, 74) /* accent over panel, flattened */
#define C_GOOD    UI_RGB(94, 200, 140)
#define C_BAD     UI_RGB(235, 110, 110)

/* How many rows of a list fit on the screen. */
#define UI_MAX_ROWS 7

/* ---- file browser ------------------------------------------------------- */

#define UI_LIST_Y     60
#define UI_LIST_PITCH 21
#define UI_LIST_H     19
#define UI_LIST_ROWS  7

/* `right` is the version tag in the header. The path may be long; its middle is
 * elided so the tail stays readable. */
void scr_browser(const char *right, const char *path, const char *const *names,
		 const uint8_t *isdir, int n, int sel, int top, bool ok,
		 bool canQuit);

/* Maps a y coordinate to a list row (absolute, i.e. top + row) or -1. */
#define UI_PATH_Y 36
#define UI_PATH_H 22

int scr_browser_hit(int y, int top, int n);

/* ---- menu --------------------------------------------------------------- */

typedef enum
{
	UI_ROW_ACTION, /* something happens when it is picked */
	UI_ROW_CHOICE, /* one of a list of values */
	UI_ROW_TOGGLE, /* on / off pill */
} ui_row_kind;

typedef struct
{
	const char *label;
	const char *value; /* shown on the right for CHOICE/TOGGLE */
	ui_row_kind kind;
	bool disabled;     /* shown greyed out, and cannot be picked */
} ui_row;

typedef struct
{
	const char *title;  /* the name of the menu */
	const char *status; /* right-hand side, e.g. "paused" */
	const char *const *tabs;
	int n_tabs;
	int tab;
	const ui_row *rows;
	int n_rows;
	int top;    /* the first row that is drawn, when the list is longer
		     * than the screen */
	int sel;    /* the selected row, counted from the first row */
	bool title_active; /* the page name is what is selected */
	const char *note; /* grey line under the rows, may be NULL */
	/* The key hints in the footer. The usual ones are used when this is
	 * NULL. */
	const char *const *hints;
	int n_hints;
} ui_menu;

void scr_menu(const ui_menu *m);

/* -1 when the point is not on any tab, otherwise the tab index. */
int scr_menu_hit_tab(int x, int y, const char *const *tabs, int n_tabs);
/* Row index under y, or -1. */
int scr_menu_hit_row(int y, int n_rows);

/* ---- messages ----------------------------------------------------------- */

void scr_message(const char *title, const char *l1, const char *l2,
		 const char *hint);

/* A small panel with the sound's numbers, in the corner of the screen. */
void scr_stats(const char *l1, const char *l2, const char *l3);

/*
 * The diagnostics: a panel of lines.
 *
 * The lines are the port's own numbers - the sound, the frame clock, the
 * emulator - and there is one line for each thing worth watching. The
 * panel is meant to be read while the game is running, so it is drawn in
 * the small font and the hint at the bottom says what the keys do.
 */
void scr_diagnostics(const char *title, const char *const *lines, int n,
		     const char *hint);

/* Flat background only. */
void scr_blank(void);
