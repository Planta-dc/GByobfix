/*
 * A stand-in for the 3DS SDK.
 *
 * The platform's own files are written against the 3DS SDK, and there is
 * no way to run them on a development machine without one. This is the
 * part of the SDK the 3DS platform uses, written so that the platform's
 * files can be compiled and run on a host.
 *
 * It is not an emulator and it does not try to be: the graphics calls
 * write into a piece of memory that the tests look at, the sound calls do
 * nothing, and the clock is a counter the test drives. What it is for is
 * checking that the renderer draws the same picture as the renderer it
 * replaced, that the blit puts the picture where it is supposed to go,
 * and that the frame clock keeps its promise about when a frame is due.
 *
 * The screen is laid out the way the real one is: the framebuffer holds
 * the picture turned on its side, so a column of the picture is
 * contiguous in memory and the columns run down it.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#ifdef __cplusplus
extern "C" {
#endif

typedef u32 Result;

#define R_SUCCESS(r) ((r) == 0)
#define R_FAILED(r)  ((r) != 0)

typedef enum {
    GFX_TOP = 0,
    GFX_BOTTOM = 1,
} gfxScreen_t;

typedef enum {
    GFX_LEFT = 0,
    GFX_RIGHT = 1,
} gfx3dSide_t;

/* Colours. The real SDK packs 24 bit colours into 16 bit ones, and the
 * port's framebuffer is 16 bit. */
#define RGB8_to_565(r,g,b)  (((b)>>3)&0x1f)|((((g)>>2)&0x3f)<<5)|((((r)>>3)&0x1f)<<11)

/* The sizes the real SDK reports. The framebuffer is 240 pixels wide and
 * 400 or 320 tall, which is the picture on its side. */
#define GSP_SCREEN_WIDTH 240
#define GSP_SCREEN_HEIGHT_TOP 400
#define GSP_SCREEN_HEIGHT_TOP_2X 800
#define GSP_SCREEN_HEIGHT_BOTTOM 320

/* The framebuffer of each screen, two of them so that the swap can be
 * seen. The tests look at these directly. */
u8* hostGetFramebuffer(gfxScreen_t screen, int buffer);
int hostGetBufferIndex(gfxScreen_t screen);

void gfxInitDefault(void);
void gfxSetWide(bool enable);
u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width, u16* height);
void gfxSwapBuffers(void);
void gfxFlushBuffers(void);

/* Writing the framebuffer out of the cache. */
Result GSPGPU_FlushDataCache(const void* adr, u32 size);

/* The clock. The tests drive it: svcSleepThread moves it forward by the
 * time that was asked for, plus hostSleepOvershoot, which is how a real
 * kernel behaves and how the frame clock has to cope with. */
extern u64 hostTick;

/* How far the clock jumps each time it is read. The frame clock spins at
 * the end of a frame, and the spin has to make the clock move, or it would
 * never end. */
extern u64 hostTickPerRead;

/* How late a sleep wakes up. A real kernel never wakes up exactly when it
 * is asked to. */
extern u64 hostSleepOvershoot;

u64 svcGetSystemTick(void);
void svcSleepThread(s64 ns);

/* The sound service. Nothing here plays anything. */
typedef enum {
    CSND_ENCODING_PCM8 = 0,
    CSND_ENCODING_PCM16,
    CSND_ENCODING_ADPCM,
    CSND_ENCODING_PSG,
} CSND_ENCODING;

#define CSND_TIMER(n) (0x3FEC3FC / ((u32)(n)))

#define SOUND_LINEAR_INTERP (1u << 6)
#define SOUND_REPEAT        (1u << 10)
#define SOUND_ONE_SHOT      (2u << 10)
#define SOUND_FORMAT_16BIT  (1u << 12)
#define SOUND_ENABLE        (1u << 14)

Result csndInit(void);
Result csndPlaySound(int chn, u32 flags, u32 sampleRate, float vol, float pan,
                     void* data0, void* data1, u32 size);
void CSND_SetVol(u32 channel, u32 chnVolumes, u32 capVolumes);
void CSND_SetPlayState(u32 channel, u32 value);
void CSND_SetPlayStateR(u32 channel, u32 value);
Result CSND_FlushDataCache(const void* adr, u32 size);

/* What the service says about a channel. The real one is a union of the
 * channel's own registers; this is only the part the transport reads. */
typedef union {
    u32 value[3];
    struct {
        u8 active;
        u8 _pad1;
        u16 _pad2;
        s16 adpcmSample;
        u8 adpcmIndex;
        u8 _pad3;
        u32 unknownZero;
    };
} CSND_ChnInfo;

Result csndGetState(u32 channel, CSND_ChnInfo* out);

/* The channels the service handed out. */
extern u32 csndChannels;

static inline u32 CSND_VOL(float vol, float pan)
{
    float rpan = (pan + 1) / 2;
    u32 lvol = vol * (1 - rpan) * 0x8000;
    u32 rvol = vol * rpan * 0x8000;
    return lvol | (rvol << 16);
}

/* Memory. The real SDK hands out linear memory; here it is ordinary
 * memory, which is all the tests need. */
void* linearAlloc(size_t size);
void linearFree(void* ptr);

#ifdef __cplusplus
}
#endif
