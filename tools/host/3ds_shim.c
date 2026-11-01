/*
 * The host stand-in for the 3DS SDK. See 3ds.h.
 */

#include "3ds.h"
#include "3ds/os.h"

#define TOP_BUFFERS 2
#define BOTTOM_BUFFERS 2

/* The framebuffers are 16 bit: the port puts RGB565 in them. */
#define TOP_SIZE (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_TOP_2X * 2)
#define BOTTOM_SIZE (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_BOTTOM * 2)

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

/* The top screen is put into wide mode by the port. The stand-in has only
 * one shape for it, which is the wide one. */
void gfxSetWide(bool enable)
{
    (void)enable;
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

    /* The framebuffer is 240 pixels wide and holds the picture on its
     * side, so it is as many rows as the screen is wide. */
    if (screen == GFX_TOP) {
        if (width) *width = GSP_SCREEN_WIDTH;
        if (height) *height = GSP_SCREEN_HEIGHT_TOP_2X;
        return topBuffer[topIndex];
    }

    if (width) *width = GSP_SCREEN_WIDTH;
    if (height) *height = GSP_SCREEN_HEIGHT_BOTTOM;
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

/* What the sound service was told to play, so that a test can look at
 * the samples the transport is actually handing over. The real service
 * keeps this to itself; here it is kept in the open. */
void* hostCsndData[32];
void* hostCsndLoop[32];
u32 hostCsndSize[32];
u32 hostCsndFlags[32];
u32 hostCsndRate[32];
float hostCsndVol[32];
float hostCsndPan[32];
int hostCsndCalls;

/* The channels that were asked for, in the order they were asked for. A
 * test can find the ear it wants without having to know which channel
 * number the port chose. */
int hostCsndChn[8];
int hostCsndChnCount;

/* The volume that was set on each channel, and how many times the queue
 * of commands was executed. The commands only reach the service when the
 * queue is executed, so a test can tell the difference between a volume
 * that was asked for and a volume that was applied. */
u32 hostCsndVolSet[32];
int hostCsndVolSetCount;
int hostCsndExecCount;

Result csndPlaySound(int chn, u32 flags, u32 sampleRate, float vol, float pan,
                     void* data0, void* data1, u32 size)
{
    if (chn >= 0 && chn < 32) {
        hostCsndData[chn] = data0;
        hostCsndLoop[chn] = data1;
        hostCsndSize[chn] = size;
        hostCsndFlags[chn] = flags;
        hostCsndRate[chn] = sampleRate;
        hostCsndVol[chn] = vol;
        hostCsndPan[chn] = pan;
    }
    if (hostCsndChnCount < 8)
        hostCsndChn[hostCsndChnCount] = chn;
    hostCsndChnCount++;
    hostCsndCalls++;
    return 0;
}

void CSND_SetVol(u32 channel, u32 chnVolumes, u32 capVolumes)
{
    (void)capVolumes;
    if (channel < 32) {
        hostCsndVolSet[channel] = chnVolumes;
        hostCsndVolSetCount++;
    }
}

Result csndExecCmds(bool waitDone)
{
    (void)waitDone;
    hostCsndExecCount++;
    return 0;
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
