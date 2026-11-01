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

    printf("GameYob 3DS\n\n");

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
