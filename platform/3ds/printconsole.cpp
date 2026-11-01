// Bits & pieces adapted from libnds's console.c

#include <3ds.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <3ds/services/gspgpu.h>
#include <sys/iosupport.h>
#include "default_font_bin.h"
#include "3dsgfx.h"
#include "printconsole.h"
#include "menu.h"

const u16 TEXT_COLOR = rgb565(0xff, 0xff, 0xff);
const u16 BG_COLOR = rgb565(0x00, 0x00, 0x00);

/*
 * The console this port draws with.
 *
 * It is libctru's PrintConsole, filled in by consoleInitBottom(). The name
 * used to be currentConsole, which is the name libctru uses for its own
 * console pointer. Both are variables in the global namespace, and a C++
 * variable in the global namespace is not mangled, so the two would have had
 * the same symbol name and the link would have failed with "multiple
 * definition". Nothing outside this file refers to it, so it is static and
 * the name cannot collide with anything.
 */
static PrintConsole* activeConsole = NULL;

/* Whether the console has been put away for the graphical interface. */
static bool consoleSuspended = false;

void consoleSetSuspended(bool suspended) {
    consoleSuspended = suspended;
}

static PrintConsole mainConsole;

/* The screen the console is on, and whether it has to be redrawn. */
gfxScreen_t consoleScreen = GFX_BOTTOM;
bool consoleRedraw = false;

/*
 * The colour the text is drawn in.
 *
 * The escape sequences that change it are handled here rather than by
 * libctru's console, because libctru's console draws into a tiled
 * framebuffer of its own and this one draws into the real one.
 */
static u16 textColor = rgb565(0xff, 0xff, 0xff);

/* The sixteen colours, in the order the escape sequences number them. */
static const u16 consoleColours[16] = {
    rgb565(0x00, 0x00, 0x00), rgb565(0xaa, 0x00, 0x00),
    rgb565(0x00, 0xaa, 0x00), rgb565(0xaa, 0x55, 0x00),
    rgb565(0x00, 0x00, 0xaa), rgb565(0xaa, 0x00, 0xaa),
    rgb565(0x00, 0xaa, 0xaa), rgb565(0xaa, 0xaa, 0xaa),
    rgb565(0x55, 0x55, 0x55), rgb565(0xff, 0x55, 0x55),
    rgb565(0x55, 0xff, 0x55), rgb565(0xff, 0xff, 0x55),
    rgb565(0x55, 0x55, 0xff), rgb565(0xff, 0x55, 0xff),
    rgb565(0x55, 0xff, 0xff), rgb565(0xff, 0xff, 0xff)
};

static void consoleCls(char);

/*
 * Scroll the text up by one row.
 *
 * The framebuffer is turned on its side, so a row of text on the screen
 * is a column of the framebuffer, and the framebuffer's rows run across
 * the screen. Scrolling up therefore means moving each column of the
 * framebuffer along by one row of text, which is a move within each
 * framebuffer row.
 */
static void consoleScroll(void) {
    u8* fb = (u8*)activeConsole->frameBuffer;
    int rows = activeConsole->consoleWidth * CHAR_WIDTH;
    int stride = GFX_FB_STRIDE * GFX_BYTES_PER_PIXEL;

    for (int x = 0; x < rows; x++) {
        u8* row = fb + x * stride;
        memmove(row + CHAR_HEIGHT * GFX_BYTES_PER_PIXEL, row,
                stride - CHAR_HEIGHT * GFX_BYTES_PER_PIXEL);
        memset(row, 0, CHAR_HEIGHT * GFX_BYTES_PER_PIXEL);
    }
}

/*
 * Take the colour out of an escape sequence.
 *
 * The numbers are the colours: 30 to 37 are the eight dark ones, 90 to 97
 * are the same eight in the bright version, and 0 or 39 puts it back to
 * white. Anything else is ignored.
 */
static void consoleSetColour(const char* seq) {
    int param = 0;
    bool haveNumber = false;

    for (const char* p = seq; *p && *p != 'm'; p++) {
        if (*p >= '0' && *p <= '9') {
            param = param * 10 + (*p - '0');
            haveNumber = true;
        }
        else if (*p != '[' && *p != ';') {
            return;
        }
    }

    if (!haveNumber)
        return;

    int index;
    if (param >= 30 && param <= 37)
        index = param - 30;
    else if (param >= 90 && param <= 97)
        index = param - 90 + 8;
    else if (param == 0 || param == 39)
        index = 15;    /* back to white */
    else
        return;

    textColor = consoleColours[index];
}

static void newRow() {
    activeConsole->cursorY++;

    if (activeConsole->cursorY >= activeConsole->consoleHeight) {
        /* The text has run off the bottom, so it moves up a row. It used
         * to clear the screen instead, which threw away everything that
         * was already on it. */
        consoleScroll();
        activeConsole->cursorY = activeConsole->consoleHeight - 1;
    }
}

void consoleDrawChar(char c) {
    int x = activeConsole->cursorX*CHAR_WIDTH;
    int y = activeConsole->cursorY*CHAR_HEIGHT;

    for (int cy=0; cy<CHAR_HEIGHT; cy++) {
        for (int cx=0; cx<CHAR_WIDTH; cx+=2) {
            u8 byte = default_font_bin[c*CHAR_WIDTH*CHAR_HEIGHT/2 + cy*4 + cx/2];
            if (byte&0xf)
                drawPixel((u8*)activeConsole->frameBuffer, x+cx, y+cy, textColor);
            else
                drawPixel((u8*)activeConsole->frameBuffer,  x+cx, y+cy, BG_COLOR);
            if (byte>>4)
                drawPixel((u8*)activeConsole->frameBuffer,  x+cx+1, y+cy, textColor);
            else
                drawPixel((u8*)activeConsole->frameBuffer,  x+cx+1, y+cy, BG_COLOR);
        }
    }
    consoleRedraw = true;
}
static void consolePrintChar(char c) {
	if (c == 0)
        return;

	if(activeConsole->cursorX >= activeConsole->consoleWidth) {
		activeConsole->cursorX  = 0;

		newRow();
	}

	switch(c) {
		/*
		The only special characters we will handle are tab (\t), carriage return (\r), line feed (\n)
		and backspace (\b).
		Carriage return & line feed will function the same: go to next line and put cursor at the beginning.
		For everything else, use VT sequences.

		Reason: VT sequences are more specific to the task of cursor placement.
		The special escape sequences \b \f & \v are archaic and non-portable.
		*/
        // Backspace
		case 8:
			activeConsole->cursorX--;

			if(activeConsole->cursorX < 0) {
				if(activeConsole->cursorY > 0) {
					activeConsole->cursorX = activeConsole->consoleWidth - 1;
					activeConsole->cursorY--;
				} else {
					activeConsole->cursorX = 0;
				}
			}

            consoleDrawChar(' ');
			break;

            // Tab
		case 9:
			activeConsole->cursorX += 4 - (activeConsole->cursorX)%4;
			break;
            // Newline
		case 10:
			newRow();
            // Carriage return
		case 13:
			activeConsole->cursorX = 0;
			break;
		default:
            consoleDrawChar(c);
			activeConsole->cursorX++;
			break;
	}
}

static void consoleCls(char mode) {

	int i = 0;
	int colTemp,rowTemp;

	switch (mode)
	{
	case '0':
		{
			colTemp = activeConsole->cursorX ;
			rowTemp = activeConsole->cursorY ;

			while(i++ < ((activeConsole->consoleHeight * activeConsole->consoleWidth) - (rowTemp * activeConsole->consoleWidth + colTemp)))
				consolePrintChar(' ');

			activeConsole->cursorX  = colTemp;
			activeConsole->cursorY  = rowTemp;
			break;
		}
	case '1':
		{
			colTemp = activeConsole->cursorX ;
			rowTemp = activeConsole->cursorY ;

			activeConsole->cursorY  = 0;
			activeConsole->cursorX  = 0;

			while (i++ < (rowTemp * activeConsole->consoleWidth + colTemp))
				consolePrintChar(' ');

			activeConsole->cursorX  = colTemp;
			activeConsole->cursorY  = rowTemp;
			break;
		}
	case '2':
		{
		/* The console covers the whole screen, so clearing it is clearing
		 * the framebuffer. Printing a space for every character would do
		 * the same job with eight thousand times as many writes. */
		u8* fb = (u8*)activeConsole->frameBuffer;
		size_t size = (size_t)activeConsole->consoleWidth * CHAR_WIDTH *
		              GFX_FB_STRIDE * GFX_BYTES_PER_PIXEL;
		memset(fb, 0, size);

		activeConsole->cursorY  = 0;
		activeConsole->cursorX  = 0;
		break;
		}
	}
}

static void consoleClearLine(char mode) {
	int i = 0;
	int colTemp;

	switch (mode)
	{
	case '0':
		{
			colTemp = activeConsole->cursorX ;

			while(i++ < (activeConsole->consoleWidth - colTemp)) {
				consolePrintChar(' ');
			}

			activeConsole->cursorX  = colTemp;

			break;
		}
	case '1':
		{
			colTemp = activeConsole->cursorX ;

			activeConsole->cursorX  = 0;

			while(i++ < ((activeConsole->consoleWidth - colTemp)-2)) {
				consolePrintChar(' ');
			}

			activeConsole->cursorX  = colTemp;

			break;
		}
	case '2':
		{
			colTemp = activeConsole->cursorX ;

			activeConsole->cursorX  = 0;

			while(i++ < activeConsole->consoleWidth) {
				consolePrintChar(' ');
			}

			activeConsole->cursorX  = colTemp;

			break;
		}
	default:
		{
			colTemp = activeConsole->cursorX ;

			while(i++ < (activeConsole->consoleWidth - colTemp)) {
				consolePrintChar(' ');
			}

			activeConsole->cursorX  = colTemp;

			break;
		}
	}
}

/*
 * The write end of the console's device, which is how printf() gets to the
 * screen.
 *
 * The second argument is a void* because that is what newlib's devoptab_t
 * says it is. The port had it as an int, which is a different function type,
 * and a devoptab_t that holds a function of the wrong type does not compile
 * against the real newlib header. It is static for the same reason the rest
 * of this file is: libctru exports a con_write of its own, and a file-scope
 * function in C++ is mangled but a static one is not exported at all.
 */
static ssize_t con_write(struct _reent *r, void *fd, const char *ptr, size_t len) {

	char chr;

	uint i, count = 0;
	char *tmp = (char*)ptr;

	if(!tmp || len<=0) return -1;

	/* While the graphical interface is up the console says nothing, but
	 * it still swallows the text, so that whatever prints does not end
	 * up in the middle of the interface later. */
	if (consoleSuspended) return len;

	i = 0;

	while(i<len) {

		chr = *(tmp++);
		i++; count++;

		if ( chr == 0x1b && *tmp == '[' ) {
			bool escaping = true;
			char *escapeseq	= tmp;
			int escapelen = 0;

			do {
				chr = *(tmp++);
				i++; count++; escapelen++;
				int parameter;

				switch (chr) {
					/////////////////////////////////////////
					// Cursor directional movement
					/////////////////////////////////////////
					case 'A':
						siscanf(escapeseq,"[%dA", &parameter);
						activeConsole->cursorY  =  (activeConsole->cursorY  - parameter) < 0 ? 0 : activeConsole->cursorY  - parameter;
						escaping = false;
						break;
					case 'B':
						siscanf(escapeseq,"[%dB", &parameter);
						activeConsole->cursorY  =  (activeConsole->cursorY  + parameter) > activeConsole->consoleHeight - 1 ? activeConsole->consoleHeight - 1 : activeConsole->cursorY  + parameter;
						escaping = false;
						break;
					case 'C':
						siscanf(escapeseq,"[%dC", &parameter);
						activeConsole->cursorX  =  (activeConsole->cursorX  + parameter) > activeConsole->consoleWidth - 1 ? activeConsole->consoleWidth - 1 : activeConsole->cursorX  + parameter;
						escaping = false;
						break;
					case 'D':
						siscanf(escapeseq,"[%dD", &parameter);
						activeConsole->cursorX  =  (activeConsole->cursorX  - parameter) < 0 ? 0 : activeConsole->cursorX  - parameter;
						escaping = false;
						break;
						/////////////////////////////////////////
						// Cursor position movement
						/////////////////////////////////////////
					case 'H':
					case 'f':
						siscanf(escapeseq,"[%d;%df", &activeConsole->cursorY , &activeConsole->cursorX );
						escaping = false;
						break;
						/////////////////////////////////////////
						// Screen clear
						/////////////////////////////////////////
					case 'J':
						consoleCls(escapeseq[escapelen-2]);
						escaping = false;
						break;
						/////////////////////////////////////////
						// Line clear
						/////////////////////////////////////////
					case 'K':
						consoleClearLine(escapeseq[escapelen-2]);
						escaping = false;
						break;
						/////////////////////////////////////////
						// Save cursor position
						/////////////////////////////////////////
					case 's':
						activeConsole->prevCursorX  = activeConsole->cursorX ;
						activeConsole->prevCursorY  = activeConsole->cursorY ;
						escaping = false;
						break;
						/////////////////////////////////////////
						// Load cursor position
						/////////////////////////////////////////
					case 'u':
						activeConsole->cursorX  = activeConsole->prevCursorX ;
						activeConsole->cursorY  = activeConsole->prevCursorY ;
						escaping = false;
						break;
						/////////////////////////////////////////
						// Color scan codes
						/////////////////////////////////////////
					case 'm':
						/* The colour is worked out from the number in the
						 * sequence. The old code read it with a scanf that
						 * wanted two numbers and left one of them unset when
						 * there was only one, and then put the colour into a
						 * field libctru's console does not have. */
						consoleSetColour(escapeseq);

						escaping = false;
						break;
				}
			} while (escaping);
			continue;
		}

		consolePrintChar(chr);
	}

	return count;
}

/*
 * The console's device, which newlib's stdout and stderr are pointed at.
 *
 * The table newlib keeps is "const devoptab_t *devoptab_list[]", so the
 * entries have to be const devoptab_t, and the write_r member has to be a
 * function of the type newlib says it is. The order of the members is
 * newlib's, not ours.
 */
static const devoptab_t dotab_stdout = {
	"con",
	0,
	NULL,
	NULL,
	con_write,
	NULL,
	NULL,
	NULL
};

PrintConsole* consoleCurrent(void) {
    return activeConsole;
}

void consoleInitBottom() {
    activeConsole = &mainConsole;
    activeConsole->cursorX = 0;
    activeConsole->cursorY = 0;
    activeConsole->consoleWidth = BOTTOM_SCREEN_WIDTH / CHAR_WIDTH;
    activeConsole->consoleHeight = BOTTOM_SCREEN_HEIGHT / CHAR_HEIGHT;
    consoleRedraw = true;

    consoleScreen = GFX_BOTTOM;
    consoleCheckFramebuffers();

    devoptab_list[STD_OUT] = &dotab_stdout;
    devoptab_list[STD_ERR] = &dotab_stdout;

    setvbuf(stdout, NULL , _IONBF, 0);
    setvbuf(stderr, NULL , _IONBF, 0);
}

void consoleSetScreen(gfxScreen_t screen) {
    if (screen != consoleScreen) {
        consoleCls('2');
        if (screen == 0) {
            activeConsole->consoleWidth = TOP_SCREEN_WIDTH / CHAR_WIDTH;
            activeConsole->consoleHeight = TOP_SCREEN_HEIGHT / CHAR_HEIGHT;
        }
        else {
            activeConsole->consoleWidth = BOTTOM_SCREEN_WIDTH / CHAR_WIDTH;
            activeConsole->consoleHeight = BOTTOM_SCREEN_HEIGHT / CHAR_HEIGHT;
        }
        consoleScreen = screen;
        consoleCheckFramebuffers();
        consoleCls('2');
    }
}

/*
 * The framebuffer the console draws into.
 *
 * libctru hands out the framebuffer that is not on the display, and the
 * display shows the other one when the buffers are swapped. The console
 * therefore draws into the current one, and consoleAfterSwap() copies its
 * part of the screen into the other one when the buffers are swapped.
 */
static u8* lastFramebuffer = NULL;

void consoleCheckFramebuffers() {
    u16 width, height;
    u8* framebuffer = gfxGetFramebuffer(consoleScreen, GFX_LEFT, &width, &height);
    activeConsole->frameBuffer = (u16*)framebuffer;
    lastFramebuffer = framebuffer;
    consoleRedraw = false;
}

void consoleAfterSwap() {
    u16 width, height;
    u8* framebuffer = gfxGetFramebuffer(consoleScreen, GFX_LEFT, &width, &height);

    /* The copy is made even while the console is suspended: the graphical
     * interface is drawn into the same framebuffer, and without the copy
     * the screen would go back to what it held two frames ago. */
    if (framebuffer != lastFramebuffer && lastFramebuffer) {
        int size = activeConsole->consoleWidth * CHAR_WIDTH *
                   GFX_FB_STRIDE * GFX_BYTES_PER_PIXEL;
        memcpy(framebuffer, lastFramebuffer, size);

        /* The copy is in the processor's cache, and the display controller
         * reads the framebuffer over the bus, so it has to be written out
         * or the text appears a frame late. */
        GSPGPU_FlushDataCache(framebuffer, size);
    }

    lastFramebuffer = framebuffer;
    activeConsole->frameBuffer = (u16*)framebuffer;
}

void consoleFlush() {
    fflush(stdout);

    /* The display controller reads the framebuffer over the bus, so what
     * has been drawn into it has to be flushed out of the cache before it
     * is shown. */
    gfxFlushBuffers();
}

int consoleGetWidth() {
    return activeConsole->consoleWidth;
}
int consoleGetHeight() {
    return activeConsole->consoleHeight;
}
