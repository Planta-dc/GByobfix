/*
 * The host stand-in for the 3DS SDK. See 3ds.h.
 */

#include "3ds.h"
#include "3ds/os.h"

#define TOP_BUFFERS 2
#define BOTTOM_BUFFERS 2

/* The framebuffers are 16 bit: the port puts RGB565 in them. */
#define TOP_SIZE (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_TOP * 2)
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

/* The port leaves the wide mode off, so the stand-in only has to know the
 * normal shape of the top screen: 240 by 400, which is a screen 400
 * across and 240 down. */
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
        if (height) *height = GSP_SCREEN_HEIGHT_TOP;
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

/* The channels the service hands out. On the console this is whatever
 * the service decided to give the application; here every channel is
 * available, so the port behaves the same as it would on a console that
 * handed out all of them. */
u32 csndChannels = 0xFFFFFFFF;

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

/* What the service would say about a channel. On the console this is the
 * channel's own registers; here the only thing that can be said is
 * whether the channel was set up to play at all. */
Result csndGetState(u32 channel, CSND_ChnInfo* out)
{
    if (!out)
        return -1;
    if (channel >= 32)
        return -1;

    memset(out, 0, sizeof *out);
    out->active = (hostCsndFlags[channel] & SOUND_ENABLE) ? 1 : 0;
    return 0;
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

/* The switch that says whether the console is making any sound at all.
 * The port throws it before it sets the channels up; here it is only
 * worth remembering that it was thrown. */
int hostCsndPlaybackOn;

/*
 * The addresses the transport has asked for.
 *
 * The service wants a physical address, which is thirty two bits, and a
 * pointer on a development machine does not fit in that. So the pointers
 * are kept here and the transport is given a number that stands for one.
 * There are only ever a handful of them: the blocks the transport fills.
 */
#define HOST_ADDRESS_TABLE 64
static void* hostAddresses[HOST_ADDRESS_TABLE];
static int hostAddressCount;

u32 osConvertVirtToPhys(void* ptr)
{
    for (int i = 0; i < hostAddressCount; i++) {
        if (hostAddresses[i] == ptr)
            return (u32)(i + 1);
    }

    if (hostAddressCount >= HOST_ADDRESS_TABLE)
        return 0;

    hostAddresses[hostAddressCount] = ptr;
    return (u32)(hostAddressCount++ + 1);
}

static void* hostAddress(u32 address)
{
    if (address == 0 || address > (u32)hostAddressCount)
        return NULL;
    return hostAddresses[address - 1];
}

/*
 * Setting a channel up.
 *
 * The transport does this itself rather than going through
 * csndPlaySound(), so that the commands for both ears can be sent in one
 * go and the frame is not held up waiting for the service.
 *
 * The flags carry the channel and the timer, and the timer is what the
 * rate was turned into, so the rate can be worked back out. The volume
 * is a pair of numbers, one for each speaker, and the pan is what those
 * two say about each other.
 */
void CSND_SetChnRegs(u32 flags, u32 physaddr0, u32 physaddr1, u32 totalbytesize,
                     u32 chnVolumes, u32 capVolumes)
{
    (void)physaddr1;
    (void)capVolumes;

    u32 channel = flags & 0x1F;
    if (channel >= 32)
        return;

    u32 timer = (flags >> 16) & 0xFFFF;
    float left = (chnVolumes & 0xFFFF) / 32768.0f;
    float right = (chnVolumes >> 16) / 32768.0f;
    float sum = left + right;

    hostCsndFlags[channel] = flags;
    hostCsndData[channel] = hostAddress(physaddr0);
    hostCsndSize[channel] = totalbytesize;
    hostCsndVolSet[channel] = chnVolumes;
    hostCsndRate[channel] = timer ? (0x3FEC3FC / timer) : 0;
    /* The volume of a channel is a pair of numbers, one for each
     * speaker, and the loudest of the two is how loud the channel is.
     * The pan is what the two say about each other. */
    hostCsndVol[channel] = left > right ? left : right;
    hostCsndPan[channel] = sum > 0.0f ? (right - left) / sum : 0.0f;

    /* The channels that were set up, in the order they were set up, so
     * that a test can find the ear it wants without having to know which
     * channel number the port chose. */
    int seen = 0;
    for (int i = 0; i < hostCsndChnCount && i < 8; i++) {
        if (hostCsndChn[i] == (int)channel)
            seen = 1;
    }
    if (!seen) {
        if (hostCsndChnCount < 8)
            hostCsndChn[hostCsndChnCount] = (int)channel;
        hostCsndChnCount++;
    }

    hostCsndCalls++;
}

void CSND_SetPlayStateR(u32 channel, u32 value)
{
    (void)channel;
    if (value)
        hostCsndPlaybackOn = 1;
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
