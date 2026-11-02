/*
 * The graphical interface of the 3DS port.
 *
 * The menu and the file browser are drawn as panels of colour and text on
 * the screen the text console would have used. The text console is put out
 * of the way while they are up, so that the two do not fight over the same
 * screen.
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "uiscreens.h"

/* The file browser.
 *
 * `names` are the entries of the current directory and `flags` says what
 * each of them is: see FLAG_DIRECTORY in filechooser.h.
 */
void uiDrawFileList(const char* path, const std::vector<std::string>& names,
		    const std::vector<int>& flags, int numFiles, int sel,
		    int top, bool canQuit);

/* The menu, as it stands right now. */
void uiDrawMenu(void);

/* The cheats.
 *
 * `names` are the cheats and `enabled` says which of them are on. The
 * selection is a cheat, and the list scrolls to keep it on the screen.
 */
void uiDrawCheats(const char* const* names, const uint8_t* enabled,
		  int num, int sel);

/* The key mapping.
 *
 * The first row is the configuration and the ones after it are the
 * buttons and what each of them does. `sel` is the row that is selected:
 * zero for the configuration, one more than the button for a button.
 */
void uiDrawKeyConfig(const char* configName, const char* const* buttons,
		     const char* const* functions, int num, int sel,
		     const char* note);

/* A message: a title, up to two lines of text and a line of hints. */
void uiDrawMessage(const char* title, const char* line1, const char* line2,
		   const char* hint);

/* The sound's numbers, in a panel in the corner of the screen. */
void uiDrawStats(const char* line1, const char* line2, const char* line3);
