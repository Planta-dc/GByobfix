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

/*
 * The first row to draw, so that the selection stays on the screen.
 *
 * The list is longer than the screen, so it scrolls under the selection
 * instead of the selection running off the bottom.
 */
static int topFor(int sel, int numRows) {
	int top = sel - UI_MAX_ROWS + 1;

	if (top + UI_MAX_ROWS > numRows)
		top = numRows - UI_MAX_ROWS;
	if (top < 0)
		top = 0;

	return top;
}

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
	scr_browser("GameYob", path, ptrs.empty() ? NULL : &ptrs[0],
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
	int top = topFor(selRow < 0 ? 0 : selRow, numRows);

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
	m.hints = NULL;
	m.n_hints = 0;

	uiSetScreen(uiScreen());
	scr_menu(&m);
	ui_present();
}

/*
 * The cheats.
 *
 * The rows are the ones the menu is made of: the name of the cheat and a
 * switch for it. There can be more cheats than fit on the screen, so the
 * list scrolls under the selection.
 */
void uiDrawCheats(const char* const* names, const uint8_t* enabled,
		  int num, int sel) {
	std::vector<ui_row> rows(num);

	for (int i = 0; i < num; i++) {
		rows[i].label = names[i];
		rows[i].kind = UI_ROW_TOGGLE;
		rows[i].value = enabled[i] ? "on" : "off";
		rows[i].disabled = false;
	}

	char status[32];
	snprintf(status, sizeof status, "%d of %d", sel + 1, num);

	const char *const hints[] = {
		"D-Pad move   Left/Right turns the cheat on and off",
		"B back   L/R pages",
	};

	ui_menu m;

	m.title = "Cheats";
	m.status = status;
	m.tabs = NULL;
	m.n_tabs = 0;
	m.tab = 0;
	m.rows = rows.empty() ? NULL : &rows[0];
	m.n_rows = num;
	m.sel = sel;
	m.top = topFor(sel, num);
	m.title_active = false;
	m.note = NULL;
	m.hints = hints;
	m.n_hints = 2;

	uiSetScreen(uiScreen());
	scr_menu(&m);
	ui_present();
}

/*
 * The key mapping.
 *
 * The first row is the configuration and the ones after it are the
 * buttons and what each of them does. There are more buttons than fit on
 * the screen, so the table scrolls under the selection.
 */
void uiDrawKeyConfig(const char* configName, const char* const* buttons,
		     const char* const* functions, int num, int sel,
		     const char* note) {
	std::vector<ui_row> rows(num + 1);

	rows[0].label = "Configuration";
	rows[0].kind = UI_ROW_CHOICE;
	rows[0].value = configName;
	rows[0].disabled = false;

	for (int i = 0; i < num; i++) {
		rows[i + 1].label = buttons[i];
		rows[i + 1].kind = UI_ROW_CHOICE;
		rows[i + 1].value = functions[i];
		rows[i + 1].disabled = false;
	}

	const char *const hints[] = {
		"D-Pad move   Left/Right changes the function",
		"X new   Y delete   B back",
	};

	ui_menu m;

	m.title = "Button Mapping";
	m.status = "";
	m.tabs = NULL;
	m.n_tabs = 0;
	m.tab = 0;
	m.rows = rows.empty() ? NULL : &rows[0];
	m.n_rows = num + 1;
	m.sel = sel;
	m.top = topFor(sel, num + 1);
	m.title_active = false;
	m.note = note;
	m.hints = hints;
	m.n_hints = 2;

	uiSetScreen(uiScreen());
	scr_menu(&m);
	ui_present();
}

/*
 * A message.
 *
 * The title is in the header, the two lines are in the panel and the
 * hint is at the bottom, where the key hints usually are.
 */
void uiDrawMessage(const char* title, const char* line1, const char* line2,
		   const char* hint) {
	uiSetScreen(uiScreen());
	scr_message(title, line1, line2, hint);
	ui_present();
}

/*
 * The sound's numbers.
 *
 * They go on the screen the game is not on, which is where the text
 * console's numbers used to be, but in a panel rather than in a page of
 * text.
 */
void uiDrawStats(const char* line1, const char* line2, const char* line3) {
	uiSetScreen(uiScreen());
	scr_stats(line1, line2, line3);
	ui_present();
}
