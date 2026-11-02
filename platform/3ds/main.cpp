#include <3ds.h>
#include <stdio.h>
#include <cwchar>
#include "gbgfx.h"
#include "soundengine.h"
#include "inputhelper.h"
#include "gameboy.h"
#include "console.h"
#include "gbs.h"
#include "romfile.h"
#include "menu.h"
#include "gbmanager.h"
#include "printconsole.h"
#include "3dsgfx.h"
#include "audio.h"

int main(int argc, char* argv[])
{
    srvInit();

	aptInit();
	hidInit();
	gfxInitDefault();

	/* The top screen is 400 by 240 in the normal mode, which is the one
	 * the picture is laid out for. The wide mode is the eight hundred
	 * pixel one: there the framebuffer grows from 240 by 400 to 240 by
	 * 800, so the picture would have to be laid out for eight hundred
	 * columns instead of four hundred. It is left off. */
	gfxSetWide(false);

	/* The screens are given a framebuffer of sixteen bits a pixel rather
	 * than twenty-four. The interface is drawn in that format, and the
	 * picture is a third less for the bus to carry: the blit that draws
	 * it writes two bytes a pixel instead of three. */
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	fsInit();

    consoleInitBottom();

    /* The sound service. If it cannot be started the emulator runs
     * silently, because there is no point in refusing to start over it,
     * but it says so: a game with no sound and no explanation is a
     * thing that is very hard to report and very easy to fix. */
    if (!audio_init())
        printf("The sound service would not start. There will be no sound.\n");

    fs_init();
    mgr_init();

	initInput();
    setMenuDefaults();
    readConfigFile();

    /* The interface says the name of the program in the header of the
     * file browser, so there is nothing to print here. */
    mgr_selectRom();

    /* The renderer and the frame clock. The clock starts here, so that the
     * time spent in the file chooser is not counted as emulation time. */
    initGFX();
    gfxInitFrameClock();

    for (;;) {
        mgr_runFrame();
        mgr_updateVBlank();
    }

	return 0;
}
