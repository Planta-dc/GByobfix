/*
 * The 3DS renderer.
 *
 * What this file does
 * -------------------
 * The Game Boy draws a picture one scanline at a time. The old 3DS port drew
 * each of those scanlines straight into the 3DS framebuffer, one pixel at a
 * time, with three byte stores per pixel into a transposed framebuffer whose
 * rows are 720 bytes apart. That is 23040 pixels a frame, each one a cache
 * miss, done in the middle of the emulator's hot loop.
 *
 * This file splits the work in two:
 *
 *   - while the emulator runs, drawBackground() and drawSprite() fill in
 *     four small per-scanline buffers of palette indices, and
 *     compositeScanline() reduces them to the 160 pixels that are actually
 *     visible. Nothing here touches the framebuffer, and nothing here
 *     allocates.
 *
 *   - once the frame is finished, gfxBlitGameFrame() scales the finished
 *     160x144 picture into the 3DS framebuffer, in 8x8 blocks so that the
 *     transposed framebuffer is written a cache line at a time.
 *
 * The picture is identical to the one the old port drew. That is not an
 * accident and it is checked: tools/host/check_render.c runs both
 * implementations over randomised Game Boy states and compares every pixel.
 *
 * The indices
 * -----------
 * The scanline buffers hold a palette index, not a colour:
 *
 *   0..31   background palette 0..7, colour 0..3
 *   32..63  sprite palette 0..7, colour 0..3
 *
 * The mapping from index to colour lives in palColour[], which is rebuilt
 * whenever a palette register changes. The old port kept a pointer to a
 * pointer to a colour for every one of those 64 entries and followed it for
 * every pixel it drew; this one does a single load from a flat table at the
 * end, when the picture is scaled.
 *
 * The old port's quirks are kept on purpose
 * ------------------------------------------
 * The old renderer had known bugs: it drew DMG sprites in the wrong order,
 * it split the screen incorrectly when the window started, and it showed
 * stale pixels when the background was switched off. Fixing them is an
 * accuracy change, not a performance change, and it needs a screen to look
 * at. Until then this renderer draws exactly what the old one drew, so that
 * the two can be compared pixel for pixel.
 */

#include <3ds.h>
#include <string.h>
#include "gbgfx.h"
#include "gameboy.h"
#include "3dsgfx.h"
#include "menu.h"
#include "inputhelper.h"
#include "frameclock.h"
#include "audio.h"

/* public variables */

bool probingForBorder;

int interruptWaitMode;
int scaleMode;
int scaleFilter;
u8 gfxMask;
volatile int loadedBorderType;
bool customBorderExists;
bool sgbBorderLoaded;

/* private variables */

int lastGameScreen = -1;

u32 bgPalettes[8][4];
u32 sprPalettes[8][4];

bool bgPalettesModified[8];
bool sprPalettesModified[8];

/* Whether a palette is currently permuted by a Game Boy palette register.
 *
 * On a plain Game Boy the four shades of grey are fixed and the palette
 * register says which shade each colour number is. The old port expressed
 * that by pointing each palette entry at a different colour; here the
 * permutation is applied when the table is built. The flags exist because
 * the permutation only takes effect once the register has been written,
 * which is what the old port did too. */
static bool bgPalettesPermuted[8];
static bool sprPalettesPermuted[8];

/* The finished picture: 160x144 palette indices, one byte per pixel. */
u8 gameFrame[GB_WIDTH * GB_HEIGHT];

/* Index -> colour. Packed the way the framebuffer wants it: B, G, R.
 * The last entry is black: see the header. */
u32 palColour[65];

/* The index that means "nothing has been drawn here yet". It is black,
 * and it is transparent like every other colour zero. */
#define PAL_NEVER_DRAWN 64

/* Per-scanline working buffers.
 *
 * bgIndex and bgIndexLow are the two background layers, sprIndex and
 * sprIndexLow the two sprite layers. A sprite in front of the background
 * goes in sprIndex, one behind it in sprIndexLow, and the same is true of
 * the background's own priority bit.
 *
 * The buffers are 256 bytes, not 160, because a sprite can hang off the
 * left or right edge and the old port wrapped it into the next line rather
 * than clipping it. The wrapping is kept; see the note about quirks above.
 *
 * bgIndexLow is deliberately not cleared between scanlines: the old port
 * did not clear it either, and the difference is visible when the
 * background is switched off. */
static u8 bgIndex[256];

/* The low background layer starts out as "never drawn", which is black.
 * The old renderer kept the colours themselves and its buffer was full of
 * zeroes until the background was drawn for the first time. */
static u8 bgIndexLow[256];
static u8 sprIndex[256];
static u8 sprIndexLow[256];

/* Private functions */
static void drawBackground(int scanline);
static void drawSprite(int scanline, int spriteNum);
static void compositeScanline(int scanline);

static void updateBgPalette(int paletteid);
static void updateBgPaletteDMG();
static void updateSprPalette(int paletteid);
static void updateSprPaletteDMG(int paletteid);
static void rebuildPaletteTable();
static void refreshModifiedPalettes(void);

/* The frame clock: the port's own idea of when a frame is due. */
static FrameClock frameClock;

/* How many frames since the sound's numbers were last printed. */
static int statsCounter;

/*
 * The sound's numbers.
 *
 * The point of printing them is that a dropout should not be invisible.
 * The transport counts how often the emulator fell behind the sound
 * service, how often it had to throw samples away, and how often it had
 * to correct the difference between the console's clock and the sound
 * service's clock. A count that climbs is a warning that something is
 * wrong; a count that stays at zero is the promise that nothing is.
 *
 * Once a second, on the console, which is on the screen the game is not
 * on.
 */
static void printAudioStats(void)
{
    audio_stats stats;
    audio_get_stats(&stats);

    printf("sound: %llu frames, %llu underruns, %llu drops, %llu corrections, %d ms behind\n",
           (unsigned long long)stats.frames,
           (unsigned long long)stats.underruns,
           (unsigned long long)stats.drops,
           (unsigned long long)stats.corrections,
           stats.latencyMs);
}


/* Function definitions */

void doAtVBlank(void (*func)(void)) {
    func();
}

void initGFX()
{
    bgPalettes[0][0] = RGB24(255, 255, 255);
    bgPalettes[0][1] = RGB24(192, 192, 192);
    bgPalettes[0][2] = RGB24(94, 94, 94);
    bgPalettes[0][3] = RGB24(0, 0, 0);
    sprPalettes[0][0] = RGB24(255, 255, 255);
    sprPalettes[0][1] = RGB24(192, 192, 192);
    sprPalettes[0][2] = RGB24(94, 94, 94);
    sprPalettes[0][3] = RGB24(0, 0, 0);
    sprPalettes[1][0] = RGB24(255, 255, 255);
    sprPalettes[1][1] = RGB24(192, 192, 192);
    sprPalettes[1][2] = RGB24(94, 94, 94);
    sprPalettes[1][3] = RGB24(0, 0, 0);

    memset(bgPalettesModified, 0, sizeof(bgPalettesModified));
    memset(sprPalettesModified, 0, sizeof(sprPalettesModified));
    memset(bgPalettesPermuted, 0, sizeof(bgPalettesPermuted));
    memset(sprPalettesPermuted, 0, sizeof(sprPalettesPermuted));

    /* The low background layer starts again: a pixel that has never been
     * drawn is black. The old renderer never reset its colours either, so
     * this only happens when the emulator is started. */
    memset(bgIndexLow, PAL_NEVER_DRAWN, sizeof(bgIndexLow));

    /* And so does the picture, or the screen would come up white before
     * the first frame has been drawn. */
    memset(gameFrame, PAL_NEVER_DRAWN, sizeof(gameFrame));

    rebuildPaletteTable();
}

void refreshGFX() {
    for (int i=0; i<8; i++) {
        bgPalettesModified[i] = true;
        sprPalettesModified[i] = true;
    }
}

void clearGFX() {
}

/* Start the frame clock. Called once, when the program starts: the clock
 * counts from the moment the emulator starts running, not from the moment
 * the first frame is finished. */
void gfxInitFrameClock(void) {
    frameClockInit(&frameClock);
}

void drawScanline(int scanline)
{
}

/*
 * The emulator calls this at the end of every visible scanline. Everything
 * here is index arithmetic and small tables; no pixel is written to the
 * framebuffer.
 */
void drawScanline_P2(int scanline) {
    refreshModifiedPalettes();

    /* The old port cleared the high background layer, the front sprite
     * layer and the back sprite layer here, and left the low background
     * layer alone. */
    memset(bgIndex, 0, sizeof(bgIndex));
    memset(sprIndex, 0, sizeof(sprIndex));
    memset(sprIndexLow, 0, sizeof(sprIndexLow));

    if (gameboy->ioRam[0x40] & 0x2) { // Sprites enabled
        /* Sprites are drawn from the last one to the first, so that the
         * lowest OAM index is the one that ends up on top. */
        for (int i=39; i>=0; i--) {
            drawSprite(scanline, i);
        }
    }

    drawBackground(scanline);

    compositeScanline(scanline);
}

/*
 * The background: the scrolling tile map, and the window on top of it.
 *
 * The old port worked out the destination of every pixel of every tile with
 * a shift, a mask and a table lookup, and then looked the colour up
 * through two levels of pointer. Here the eight pixels of a tile row come
 * out of two bytes of video memory and a 256 entry table, and the colour is
 * looked up once per pixel at the end of the frame instead of once per pixel
 * here.
 */
static void drawBackground(int scanline) {
    const u8* ioRam = gameboy->ioRam;
    const u8* vram0 = gameboy->vram[0];
    const u8* vram1 = gameboy->vram[1];
    const int cgb = (gameboy->gbMode == CGB);

    int tileSigned;
    int bgMapAddr;
    int winMapAddr;

    if (ioRam[0x40] & 0x10) {	// Tile Data location
        tileSigned = 0;
    }
    else {
        tileSigned = 1;
    }

    if (ioRam[0x40] & 0x8) {		// Tile Map location
        bgMapAddr = 0x1C00;
    }
    else {
        bgMapAddr = 0x1800;
    }
    if (ioRam[0x40] & 0x40)
        winMapAddr = 0x1C00;
    else
        winMapAddr = 0x1800;

    int winX = ioRam[0x4B]-7;
    int winY = ioRam[0x4A];
    bool drawingWindow = scanline >= winY && (ioRam[0x40] & 0x20);

    int bgOn = 1;
    if (!cgb && (ioRam[0x40] & 1) == 0)
        bgOn = 0;

    if (bgOn) {
        u8 scrollX = ioRam[0x43];
        int scrollY = ioRam[0x42];
        // The y position (measured in tiles)
        int tileY = ((scanline+scrollY)&0xFF)/8;
        // The y position within the tile (0-7)
        int pixelY = (scanline+scrollY)%8;

        int numTilesX = 20;
        if (drawingWindow)
            numTilesX = (winX+7)/8;
        int startTile = scrollX/8;
        int endTile = (startTile+numTilesX+1)&31;

        for (int i=startTile; i!=endTile; i=(i+1)&31)
        {
            int mapAddr = bgMapAddr+i+(tileY*32);
            // This is the tile id.
            int tileNum = vram0[mapAddr];
            if (tileSigned)
                tileNum = ((s8)tileNum)+256;

            int flipX = 0, flipY = 0;
            int bank = 0;
            int paletteid = 0;
            int priority = 0;

            if (cgb)
            {
                u8 attr = vram1[mapAddr];
                flipX = attr & 0x20;
                flipY = attr & 0x40;
                bank = (attr & 0x8) ? 1 : 0;
                paletteid = attr & 0x7;
                priority = attr & 0x80;
            }

            // This is the tile's Y position to be read (0-7)
            int rowY = pixelY;
            if (flipY)
                rowY = 7-rowY;

            const u8* tileRow = &gameboy->vram[bank][(tileNum<<4)+(rowY<<1)];
            u32 low = tileRow[0];
            u32 high = tileRow[1];

            /* The eight pixels of this tile, as palette indices. The table
             * turns a byte of tile data into four two bit indices, so a
             * tile row costs two lookups instead of sixteen bit tests. */
            u8 pixels[8];
            expandTileRow(low, high, flipX, pixels);

            /* Where the tile starts on the screen. The old port wrapped
             * this to a byte, so a tile that hangs over the right edge
             * continues at the left one. */
            int writeX = (i*8 - scrollX) & 0xFF;
            int count = 8;
            if (writeX > 248)
                count = 256 - writeX;

            /* The index carries the palette the tile asked for, so that the
             * colour can be looked up once, at the end of the frame. */
            int base = paletteid * 4;

            if (priority) {
                for (int x = 0; x < count; x++) {
                    bgIndex[writeX+x] = base + pixels[x];
                    bgIndexLow[writeX+x] = base + pixels[x];
                }
                for (int x = count; x < 8; x++) {
                    bgIndex[x-count] = base + pixels[x];
                    bgIndexLow[x-count] = base + pixels[x];
                }
            }
            else {
                for (int x = 0; x < count; x++)
                    bgIndexLow[writeX+x] = base + pixels[x];
                for (int x = count; x < 8; x++)
                    bgIndexLow[x-count] = base + pixels[x];
            }
        }
    }

    // Draw window

    if (drawingWindow)
    {
        int tileY = (scanline-winY)/8;
        int pixelY = (scanline-winY)%8;

        int endTile = 21-winX/8;
        for (int i=0; i<endTile; i++)
        {
            int mapAddr = winMapAddr+i+(tileY*32);
            // This is the tile id.
            int tileNum = vram0[mapAddr];
            if (tileSigned)
                tileNum = ((s8)tileNum)+128+0x80;

            int flipX = 0, flipY = 0;
            int bank = 0;
            int paletteid = 0;
            int priority = 0;

            if (cgb)
            {
                u8 attr = vram1[mapAddr];
                flipX = attr & 0x20;
                flipY = attr & 0x40;
                bank = (attr & 0x8) ? 1 : 0;
                paletteid = attr & 0x7;
                priority = attr & 0x80;
            }

            int rowY = pixelY;
            if (flipY)
                rowY = 7-rowY;

            const u8* tileRow = &gameboy->vram[bank][(tileNum<<4)+(rowY<<1)];
            u32 low = tileRow[0];
            u32 high = tileRow[1];

            u8 pixels[8];
            expandTileRow(low, high, flipX, pixels);

            /* The window is not flipped horizontally in the old port, and
             * its pixels are not wrapped either - they start at winX and
             * run off the end of the screen if the window is too wide. */
            int writeX = i*8 + winX;

            int base = paletteid * 4;

            if (priority) {
                for (int x = 0; x < 8; x++) {
                    bgIndex[writeX+x] = base + pixels[x];
                    bgIndexLow[writeX+x] = base + pixels[x];
                }
            }
            else {
                for (int x = 0; x < 8; x++) {
                    bgIndexLow[writeX+x] = base + pixels[x];
                    /* The old port cleared the high layer here, but not in
                     * the scrolling background. */
                    bgIndex[writeX+x] = 0;
                }
            }
        }
    }
}

/*
 * One sprite.
 *
 * The old port looked up the sprite's colour through a pointer table for
 * every pixel it drew, and wrote the colour and the index to two different
 * arrays. This writes the index only, and the colour is looked up once per
 * pixel when the picture is scaled.
 */
static void drawSprite(int scanline, int spriteNum)
{
    // The sprite's number, times 4 (each uses 4 bytes)
    spriteNum *= 4;

    const u8* hram = gameboy->hram;
    int y = (hram[spriteNum]-16);
    int height;
    if (gameboy->ioRam[0x40] & 0x4)
        height = 16;
    else
        height = 8;

    if (scanline < y || scanline >= y+height)
        return;

    int x = (hram[spriteNum+1]-8);
    int tileNum = hram[spriteNum+2];
    int flags = hram[spriteNum+3];
    int flipX = (flags & 0x20);
    int flipY = (flags & 0x40);
    int priority = !(flags & 0x80);
    int bank = 0;
    int paletteid;

    if (gameboy->gbMode == CGB)
    {
        bank = (flags & 0x8)>>3;
        paletteid = flags & 0x7;
    }
    else
    {
        paletteid = (flags & 0x10)>>4;
    }

    if (height == 16) {
        tileNum &= ~1;

        if (scanline-y >= 8)
            tileNum++;
    }

    //u8* tile = &memory[0]+((tileNum)*16)+0x8000;//tileAddr;
    int pixelY = (scanline-y)%8;

    if (flipY)
    {
        pixelY = 7-pixelY;
        if (height == 16)
            tileNum = tileNum^1;
    }

    const u8* tileRow = &gameboy->vram[bank][(tileNum<<4)+(pixelY<<1)];
    u32 low = tileRow[0];
    u32 high = tileRow[1];

    u8 pixels[8];
    /* The sprite's flip is applied to the destination index below, so the
     * row itself is expanded the normal way round. */
    expandTileRow(low, high, false, pixels);

    u8* idxDest = (priority ? sprIndex : sprIndexLow);
    int base = 32 + paletteid*4;

    for (int j=0; j<8; j++)
    {
        int color = pixels[j];
        if (color != 0)
        {
            int destX = (flipX ? (x+(7-j)) : (x+j)) & 0xFF;
            idxDest[destX] = base + color;
        }
    }
}

/*
 * Reduce the four layers to the 160 pixels that are shown.
 *
 * The order is the old port's: the background's priority layer wins, then a
 * sprite in front of the background, then the background's other layer, and
 * finally a sprite behind it.
 */
static void compositeScanline(int scanline) {
    u8* dest = &gameFrame[scanline * GB_WIDTH];

    for (int i = 0; i < GB_WIDTH; i++)
    {
        /* The index holds the palette in the upper bits and the colour in
         * the lower two, so "is this pixel transparent" is a test on the
         * colour alone. */
        u8 index;
        if ((bgIndex[i] & 3) != 0)
            index = bgIndex[i];
        else if ((sprIndex[i] & 3) != 0)
            index = sprIndex[i];
        else if ((sprIndexLow[i] & 3) == 0 || (bgIndexLow[i] & 3) != 0)
            index = bgIndexLow[i];
        else
            index = sprIndexLow[i];

        dest[i] = index;
    }
}

void drawScreen()
{
    gfxPresentGameFrame();

    /* The sound's numbers, once a second. */
    if (++statsCounter >= 60) {
        statsCounter = 0;
        printAudioStats();
    }

    /* Fast forward runs as fast as the console can, so the next frame is
     * due now rather than at the end of its time. The clock is told, so
     * that it does not try to catch up on the frames it skipped once fast
     * forward is over. */
    if (fastForwardMode || fastForwardKey)
        frameClockResync(&frameClock);

    frameClockWaitNext(&frameClock);
}

void displayIcon(int iconid) {
}

void selectBorder() {
}

int loadBorder(const char* filename) {
    return 0;
}

void checkBorder() {
    if (lastGameScreen != gameScreen) {
        lastGameScreen = gameScreen;
        gfxClearScreenFrame();
    }
}

void refreshScaleMode() {
}



// SGB stub functions
void refreshSgbPalette() {
}
void setSgbMask(int mask) {
}
void setSgbTiles(u8* src, u8 flags) {
}
void setSgbMap(u8* src) {
}



void writeVram(u16 addr, u8 val) {
}
void writeVram16(u16 addr, u16 src) {
}
void writeHram(u16 addr, u8 val) {
}
void handleVideoRegister(u8 ioReg, u8 val) {
    switch(ioReg) {
        case 0x47:
            if (gameboy->gbMode == GB)
                bgPalettesModified[0] = true;
            break;
        case 0x48:
            if (gameboy->gbMode == GB)
                sprPalettesModified[0] = true;
            break;
        case 0x49:
            if (gameboy->gbMode == GB)
                sprPalettesModified[1] = true;
            break;
        case 0x69:
            if (gameboy->gbMode == CGB)
                bgPalettesModified[(gameboy->ioRam[0x68]/8)&7] = true;
            break;
        case 0x6B:
            if (gameboy->gbMode == CGB)
                sprPalettesModified[(gameboy->ioRam[0x6A]/8)&7] = true;
            break;
        default:
            break;
    }
}

/*
 * Rebuild the colours of the palettes that changed.
 *
 * The old port kept a pointer to a pointer to a colour for each of the 64
 * palette entries and followed it for every pixel it drew. Here the
 * pointers are gone: the colours are packed once, when they change, into
 * the table the renderer and the blit read.
 */
static void refreshModifiedPalettes(void) {
    bool dirty = false;

    for (int i=0; i<8; i++) {
        if (bgPalettesModified[i]) {
            if (gameboy->gbMode == GB)
                updateBgPaletteDMG();
            else
                updateBgPalette(i);
            bgPalettesModified[i] = false;
            dirty = true;
        }
        if (sprPalettesModified[i]) {
            if (gameboy->gbMode == GB)
                updateSprPaletteDMG(i);
            else
                updateSprPalette(i);
            sprPalettesModified[i] = false;
            dirty = true;
        }
    }

    if (dirty)
        rebuildPaletteTable();
}

/* Pack a colour the way the framebuffer stores it.
 *
 * The framebuffer holds three bytes a pixel, blue, green and red, and the
 * blit writes the low byte of the colour first. So the colour is kept as
 * red in the top byte, green in the middle and blue at the bottom, and the
 * three bytes can be copied out without being shuffled. */
static inline u32 packColour(u32 rgb24) {
    return rgb24;
}

/*
 * Fill in the 64 entry table of colours.
 *
 * On a Game Boy Color the palette registers pick the four colours of a
 * palette directly. On a plain Game Boy the registers pick which of four
 * fixed shades of grey each colour number is, so the table is built through
 * that permutation - which is what the old port's pointer table did.
 */
static void rebuildPaletteTable(void) {
    if (gameboy->gbMode == CGB) {
        for (int p = 0; p < 8; p++) {
            for (int i = 0; i < 4; i++) {
                palColour[p*4+i] = packColour(bgPalettes[p][i]);
                palColour[32+p*4+i] = packColour(sprPalettes[p][i]);
            }
        }
    }
    else {
        u8 bgVal = gameboy->ioRam[0x47];
        u8 bgPerm[4] = {(u8)(bgVal&3), (u8)((bgVal>>2)&3),
                        (u8)((bgVal>>4)&3), (u8)(bgVal>>6)};
        for (int p = 0; p < 8; p++) {
            for (int i = 0; i < 4; i++)
                palColour[p*4+i] = packColour(bgPalettes[p][bgPalettesPermuted[p] ? bgPerm[i] : i]);
        }

        for (int p = 0; p < 8; p++) {
            u8 val = gameboy->ioRam[0x48+p];
            u8 perm[4] = {(u8)(val&3), (u8)((val>>2)&3),
                          (u8)((val>>4)&3), (u8)(val>>6)};
            for (int i = 0; i < 4; i++)
                palColour[32+p*4+i] = packColour(sprPalettes[p][sprPalettesPermuted[p] ? perm[i] : i]);
        }
    }
}

void updateBgPalette(int paletteid)
{
    int multiplier = 8;
    int i;
    for (i=0; i<4; i++)
    {
        int red = (gameboy->bgPaletteData[(paletteid*8)+(i*2)]&0x1F)*multiplier;
        int green = (((gameboy->bgPaletteData[(paletteid*8)+(i*2)]&0xE0) >> 5) |
                ((gameboy->bgPaletteData[(paletteid*8)+(i*2)+1]) & 0x3) << 3)*multiplier;
        int blue = ((gameboy->bgPaletteData[(paletteid*8)+(i*2)+1] >> 2) & 0x1F)*multiplier;
        bgPalettes[paletteid][i] = RGB24(red, green, blue);
    }
}

void updateBgPaletteDMG()
{
    /* The old port pointed the four entries of the background palette at
     * the four shades of grey in the order the palette register asks for.
     * The colours themselves do not change; only the order they are looked
     * up in, so the permutation is recorded and applied when the table is
     * built. */
    bgPalettesPermuted[0] = true;
}

void updateSprPalette(int paletteid)
{
    int multiplier = 8;
    int i;
    for (i=0; i<4; i++)
    {
        int red = (gameboy->sprPaletteData[(paletteid*8)+(i*2)]&0x1F)*multiplier;
        int green = (((gameboy->sprPaletteData[(paletteid*8)+(i*2)]&0xE0) >> 5) |
                ((gameboy->sprPaletteData[(paletteid*8)+(i*2)+1]) & 0x3) << 3)*multiplier;
        int blue = ((gameboy->sprPaletteData[(paletteid*8)+(i*2)+1] >> 2) & 0x1F)*multiplier;
        sprPalettes[paletteid][i] = RGB24(red, green, blue);
    }
}

void updateSprPaletteDMG(int paletteid)
{
    sprPalettesPermuted[paletteid] = true;
}
