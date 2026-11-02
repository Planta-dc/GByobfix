#include <stdio.h>
#include <stdarg.h>
#include "error.h"
#include "inputhelper.h"

#ifdef _3DS
#include "ui3ds.h"
#endif

void fatalerr(const char* format, ...) {
    va_list args;
    va_start(args, format);

#ifdef _3DS
    /* The 3DS shows the error as a panel, so that it does not look like
     * the text console the interface replaced. */
    char message[256];

    vsnprintf(message, sizeof message, format, args);
    va_end(args);

    uiDrawMessage("Error", message, "Please restart GameYob.", NULL);
#else
    vprintf(format, args);
    va_end(args);

    printf("\n\nPlease restart GameYob.\n");
#endif

    while(1) {
        system_checkPolls();
        system_waitForVBlank();
    }
}
