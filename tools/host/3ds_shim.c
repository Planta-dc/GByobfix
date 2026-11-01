/*
 * The host stand-in for the 3DS SDK. See 3ds.h.
 */

#include "3ds.h"
#include "3ds/os.h"

#define TOP_BUFFERS 2
#define BOTTOM_BUFFERS 2

#define TOP_SIZE (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_TOP_2X * 3)
#define BOTTOM_SIZE (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_BOTTOM * 3)

static u8 topBuffer[TOP_BUFFERS][TOP_SIZE];
static u8 bottomBuffer[BOTTOM_BUFFERS][BOTTOM_SIZE];
static int topIndex;
static int bottomIndex;

u64 hostTick = 0;
u64 hostTickPerRead = 0;
u64 hostSleepOvershoot = 0;

u8* hostGetFramebuffer(gfxScreen_t screen, int buffer)
{
    if (screen == GFX_TOP)
        return topBuffer[buffer & 1];
    return bottomBuffer[buffer & 1];
}

int hostGetBufferIndex(gfxScreen_t screen)
{
    return (screen == GFX_TOP) ? topIndex : bottomIndex;
}

void gfxInitDefault(void)
{
    topIndex = 0;
    bottomIndex = 0;
    memset(topBuffer, 0, sizeof(topBuffer));
    memset(bottomBuffer, 0, sizeof(bottomBuffer));
}

u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width, u16* height)
{
    (void)side;

    if (screen == GFX_TOP) {
        if (width) *width = GSP_SCREEN_HEIGHT_TOP_2X;
        if (height) *height = GSP_SCREEN_WIDTH;
        return topBuffer[topIndex];
    }

    if (width) *width = GSP_SCREEN_HEIGHT_BOTTOM;
    if (height) *height = GSP_SCREEN_WIDTH;
    return bottomBuffer[bottomIndex];
}

void gfxSwapBuffers(void)
{
    topIndex ^= 1;
    bottomIndex ^= 1;
}

void gfxFlushBuffers(void)
{
}

Result GSPGPU_FlushDataCache(const void* adr, u32 size)
{
    (void)adr; (void)size;
    return 0;
}

u64 svcGetSystemTick(void)
{
    hostTick += hostTickPerRead;
    return hostTick;
}

void svcSleepThread(s64 ns)
{
    /* The real kernel never sleeps for exactly the time asked for; it
     * always wakes a little late. The tests use that to check that the
     * frame clock does not accumulate the overshoot. */
    u64 ticks = (u64)((ns * (s64)SYSCLOCK_ARM11) / 1000000000);
    hostTick += ticks + hostSleepOvershoot;
}

Result csndInit(void)
{
    return 0;
}

Result csndPlaySound(int chn, u32 flags, u32 sampleRate, float vol, float pan,
                     void* data0, void* data1, u32 size)
{
    (void)chn; (void)flags; (void)sampleRate; (void)vol; (void)pan;
    (void)data0; (void)data1; (void)size;
    return 0;
}

void CSND_SetVol(u32 channel, u32 chnVolumes, u32 capVolumes)
{
    (void)channel; (void)chnVolumes; (void)capVolumes;
}

void CSND_SetPlayState(u32 channel, u32 value)
{
    (void)channel; (void)value;
}

Result CSND_FlushDataCache(const void* adr, u32 size)
{
    (void)adr; (void)size;
    return 0;
}

void* linearAlloc(size_t size)
{
    return malloc(size);
}

void linearFree(void* ptr)
{
    free(ptr);
}
