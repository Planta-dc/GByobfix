#pragma once
#include "console.h"
#include "io.h"

extern bool consoleInitialized;
extern bool consoleDebugOutput;
extern int gbcModeOption;
extern bool gbaModeOption;
extern int sgbModeOption;
extern int biosEnabled;
extern bool soundDisabled;
extern bool hyperSound;
extern bool customBordersEnabled;
extern bool sgbBordersEnabled;
extern bool autoSavingEnabled;
extern bool printerEnabled;
extern int singleScreenMode;
extern int stateNum;
extern int gameScreen;

extern bool hblankDisabled;
extern bool windowDisabled;

extern PrintConsole* menuConsole;

/*
 * The menu.
 *
 * The options and the pages are declared here so that a platform can draw
 * them however it likes: the 3DS draws them as panels of colour and text
 * rather than as a text console.
 */
struct MenuOption {
    const char* name;
    void (*function)(int);
    int numValues;
    const char* values[10];
    int defaultSelection;
    int platforms;

    bool enabled;
    int selection;
};
struct SubMenu {
    const char *name;
    int numOptions;
    MenuOption options[10];

    int selection;
};

extern SubMenu menuList[];
extern const int numMenus;

/* Which platforms this build of the port is: an option is only shown when
 * its platform bit is in here. */
extern const int MENU_BITMASK;

/* Which page and which option are selected, and -1 for the page itself. */
extern int menu;
extern int option;

/* The message shown at the bottom of the menu, if any. */
extern char printMessage[33];

extern bool fpsOutput;
extern bool timeOutput;

extern int rumbleStrength;



void setMenuDefaults();

void displayMenu();
void closeMenu(); // updateScreens may need to be called after this
bool isMenuOn();

void redrawMenu();
void updateMenu();
void printMenuMessage(const char* s);

void displaySubMenu(void (*updateFunc)());
void closeSubMenu();

int getMenuOption(const char* name);
void setMenuOption(const char* name, int value);
void enableMenuOption(const char* name);
void disableMenuOption(const char* name);

void menuParseConfig(char* line);
void menuPrintConfig(FileHandle* file);

