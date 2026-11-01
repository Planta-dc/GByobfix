/*
 * The bridge between GameYob's menu and file browser and the drawing kit.
 *
 * The menu and the browser are GameYob's own: this file only puts them on
 * the screen. The drawing itself is in uigfx.cpp (the rasteriser),
 * uifont.cpp (the fonts) and uiscreens.cpp (the panels), which come from
 * GBabe, the other Game Boy emulator for the 3DS, which is MIT licensed.
 */
#include "ui3ds.h"

#include <stdio.h>

#include "filechooser.h"
#include "menu.h"
#include "printconsole.h"
#include "uigfx.h"
#include "uiscreens.h"

/* How many rows of the menu fit on the screen. The same number as the
 * browser: the layout was drawn for one screen. */
#define UI_VISIBLE_ROWS 7

/*
 * The interface is drawn on the screen the text console is not using, so
 * that the game picture and the interface are always on different screens.
 */
static gfxScreen_t uiScreen(void) {
	return (gfxScreen_t)!gameScreen;
}

/*
 * The file browser.
 */
void uiDrawFileList(const char* path, const std::vector<std::string>& names,
		    const std::vector<int>& flags, int numFiles, int sel,
		    int top, bool canQuit) {
	/* The browser draws from arrays of pointers and flags, so the
	 * strings have to be gathered first. The list is short enough that
	 * this is cheaper than keeping a second copy of it in step. */
	std::vector<const char*> ptrs;
	std::vector<uint8_t> isdir;

	ptrs.reserve(numFiles);
	isdir.reserve(numFiles);

	for (int i = 0; i < numFiles && i < (int)names.size(); i++) {
		ptrs.push_back(names[i].c_str());
		isdir.push_back((flags[i] & FLAG_DIRECTORY) != 0);
	}

	uiSetScreen(uiScreen());
	scr_browser("", path, ptrs.empty() ? NULL : &ptrs[0],
		    isdir.empty() ? NULL : &isdir[0], (int)ptrs.size(), sel, top,
		    true, canQuit);
	ui_present();
}

/*
 * The menu.
 */
void uiDrawMenu(void) {
	const SubMenu* sub = &menuList[menu];

	ui_row rows[10];
	int numRows = 0;
	int selRow = -1;

	for (int i = 0; i < sub->numOptions && numRows < 10; i++) {
		const MenuOption* opt = &sub->options[i];

		/* An option that is not for this platform is not shown at
		 * all, exactly as in the text menu. */
		if (!(opt->platforms & MENU_BITMASK))
			continue;

		rows[numRows].label = opt->name;
		rows[numRows].disabled = !opt->enabled;

		if (opt->numValues == 0) {
			rows[numRows].kind = UI_ROW_ACTION;
			rows[numRows].value = "";
		}
		else {
			rows[numRows].kind = UI_ROW_CHOICE;
			rows[numRows].value = opt->values[opt->selection];
		}

		if (i == option)
			selRow = numRows;

		numRows++;
	}

	/* Only as many rows as fit are drawn, and the selection is kept in
	 * sight by scrolling the list under it. */
	int top = 0;

	if (selRow >= UI_VISIBLE_ROWS)
		top = selRow - UI_VISIBLE_ROWS + 1;
	if (top + UI_VISIBLE_ROWS > numRows)
		top = numRows - UI_VISIBLE_ROWS;
	if (top < 0)
		top = 0;

	/* The name of the page is shown between arrows when the page itself
	 * is what is selected, which is how the text menu shows that the
	 * left and right buttons will change pages. */
	char title[64];

	if (option == -1)
		snprintf(title, sizeof title, "< %s >", sub->name);
	else
		snprintf(title, sizeof title, "%s", sub->name);

	ui_menu m;

	m.title = title;
	m.status = printMessage[0] != '\0' ? printMessage : "";
	m.tabs = NULL;
	m.n_tabs = 0;
	m.tab = 0;
	m.rows = rows;
	m.n_rows = numRows;
	m.sel = selRow;
	m.top = top;
	m.title_active = (option == -1);
	m.note = NULL;

	uiSetScreen(uiScreen());
	scr_menu(&m);
	ui_present();
}
