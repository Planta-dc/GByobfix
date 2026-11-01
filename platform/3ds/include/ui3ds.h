/*
 * The graphical interface of the 3DS port.
 *
 * The menu and the file browser are drawn as panels of colour and text on
 * the screen the text console would have used. The text console is put out
 * of the way while they are up, so that the two do not fight over the same
 * screen.
 */
#pragma once

#include <string>
#include <vector>

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
