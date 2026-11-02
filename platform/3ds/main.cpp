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
#include "diag.h"
#include "hud.h"

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

    /* The diagnostics. They are prepared here so that the log can be
     * written from the first frame, and they are not written to until
     * they are asked for. */
    diagInit();

	initInput();
    setMenuDefaults();
    readConfigFile();

    /* The test tone, if the settings say it was on. The setting is
     * remembered, so the tone has to be started again here or the menu
     * would say one thing and the speaker another. */
    int testTone = getMenuOption("Sound Test");
    if (testTone == 1)
        audio_set_test_tone(440);
    else if (testTone == 2)
        audio_set_test_tone(1000);

    /* The status strip on the game picture, if the settings say it was
     * on, for the same reason. */
    hudSetEnabled(getMenuOption("Status Strip") != 0);

    /* The size of the picture. The original port drew it at its own size
     * in the middle of the screen, and that is what the setting says
     * when it has never been changed. */
    gfxSetPictureScaled(getMenuOption("Picture Size") != 0);

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

        /* The test tone, when there is nothing else to play it with. */
        audio_pump_test_tone();

        /* The frame time, and the line a second in the log. This is here
         * because this is where a frame ends. */
        diagTick();

        /* The log, written out when it is worth a card write. A card
         * write costs more than a frame, so it is not done every time
         * there is something to write. */
        diagFlushSoft();
    }

	return 0;
}
