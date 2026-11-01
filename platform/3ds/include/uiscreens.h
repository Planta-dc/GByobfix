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
} ui_menu;

void scr_menu(const ui_menu *m);

/* -1 when the point is not on any tab, otherwise the tab index. */
int scr_menu_hit_tab(int x, int y, const char *const *tabs, int n_tabs);
/* Row index under y, or -1. */
int scr_menu_hit_row(int y, int n_rows);

/* ---- messages ----------------------------------------------------------- */

void scr_message(const char *title, const char *l1, const char *l2,
		 const char *hint);

/* Flat background only. */
void scr_blank(void);
