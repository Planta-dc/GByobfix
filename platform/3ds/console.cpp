#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include "console.h"
#include "menu.h"
#include "printconsole.h"
#include "3dsgfx.h"
#include "gbgfx.h"
#include "inputhelper.h"

volatile int consoleSelectedRow;


bool isConsoleOn() {
    return true;
}

void clearConsole() {
	iprintf("\x1b[2J");
}

PrintConsole* getDefaultConsole() {
    return consoleCurrent();
}

void updateScreens(bool waitToFinish) {
    consoleSetScreen((gfxScreen_t)!gameScreen);
    checkBorder();
}


void consoleSetPosColor(int x, int y, int color) {

}

void consoleSetLineColor(int line, int color) {

}

void iprintfColored(int palette, const char* format, ...) {
    va_list args;

    /* The console is told to change colour with an escape sequence. The
     * palette is numbered the way the sequences number colours: the first
     * eight are the dark ones and the second eight are the same eight in
     * the bright version. */
    printf("\x1b[%dm", 30 + (palette & 7) + ((palette & 8) ? 60 : 0));

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    /* And back to the colour everything else is. */
    printf("\x1b[0m");
}
void printLog(const char* format, ...) {
    if (consoleDebugOutput) {
        va_list args;
        va_start(args, format);

        vprintf(format, args);
        va_end(args);
    }
}
void printAndWait(const char* format, ...) {
    va_list args;
    va_start(args, format);

    vprintf(format, args);
    va_end(args);

    inputUpdateVBlank();
    while (!keyJustPressed(KEY_A)) {
        system_checkPolls();
        inputUpdateVBlank();
    }
}

int checkRumble() {
    return 0;
}

int checkCamera() {
    return 0;
}


void disableSleepMode() {

}
void enableSleepMode() {

}

void setPrintConsole(PrintConsole* console) {

}
PrintConsole* getPrintConsole() {
    return consoleCurrent();
}
