// Bits & pieces adapted from libnds's console.c

#include <3ds.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/iosupport.h>
#include "default_font_bin.h"
#include "3dsgfx.h"
#include "printconsole.h"
#include "menu.h"

const u32 TEXT_COLOR = RGB24(0xff, 0xff, 0xff);
const u32 BG_COLOR = RGB24(0x00, 0x00, 0x00);

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

static PrintConsole mainConsole;

/* The screen the console is on, and whether it has to be redrawn. */
gfxScreen_t consoleScreen = GFX_BOTTOM;
bool consoleRedraw = false;

static void consoleCls(char);

static void newRow() {
    activeConsole->cursorY++;

    if (activeConsole->cursorY >= activeConsole->consoleHeight) {
        consoleCls('2');
        /*
        activeConsole->cursorY--;

        for (int j=activeConsole->consoleHeight-2; j>=0; j--) {
            for (int y=0; y<CHAR_HEIGHT; y++) {
                for (int x=0; x<activeConsole->consoleWidth*CHAR_WIDTH; x++) {
                    drawPixel((u8*)activeConsole->frameBuffer, x, j*CHAR_HEIGHT+y,
                            getPixel((u8*)activeConsole->frameBuffer, x, (j+1)*CHAR_HEIGHT+y));
                }
            }
        }
        */
    }
}

void consoleDrawChar(char c) {
    int x = activeConsole->cursorX*CHAR_WIDTH;
    int y = activeConsole->cursorY*CHAR_HEIGHT;

    for (int cy=0; cy<CHAR_HEIGHT; cy++) {
        for (int cx=0; cx<CHAR_WIDTH; cx+=2) {
            u8 byte = default_font_bin[c*CHAR_WIDTH*CHAR_HEIGHT/2 + cy*4 + cx/2];
            if (byte&0xf)
                drawPixel((u8*)activeConsole->frameBuffer, x+cx, y+cy, TEXT_COLOR);
            else
                drawPixel((u8*)activeConsole->frameBuffer,  x+cx, y+cy, BG_COLOR);
            if (byte>>4)
                drawPixel((u8*)activeConsole->frameBuffer,  x+cx+1, y+cy, TEXT_COLOR);
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
			activeConsole->cursorY  = 0;
			activeConsole->cursorX  = 0;

			while(i++ < activeConsole->consoleHeight * activeConsole->consoleWidth)
				consolePrintChar(' ');

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
	int intensity = 0;

	if(!tmp || len<=0) return -1;

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
						siscanf(escapeseq,"[%d;%dm", &parameter, &intensity);

						//only handle 30-37,39 and intensity for the color changes
						parameter -= 30;

						//39 is the reset code
						if(parameter == 9){
							parameter = 15;
						}
						else if(parameter > 8){
							parameter -= 2;
						}
						else if(intensity){
							parameter += 8;
						}
                        /*
						if(parameter < 16 && parameter >= 0){
							activeConsole->fontCurPal = parameter << 12;
						}
                        */

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

    if (framebuffer != lastFramebuffer && lastFramebuffer) {
        int size = (activeConsole->consoleWidth*CHAR_WIDTH *
                activeConsole->consoleHeight*CHAR_HEIGHT) * 3;
        memcpy(framebuffer, lastFramebuffer, size);
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
