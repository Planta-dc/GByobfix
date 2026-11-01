/* Part of the host stand-in for the 3DS SDK. See 3ds.h. */
#pragma once
#include <3ds.h>

/* The sound service. The real header declares all of this; the parts the
 * port uses are here so that the transport can be compiled and run on a
 * host. */
Result csndInit(void);
Result csndExecCmds(bool waitDone);
Result csndPlaySound(int chn, u32 flags, u32 sampleRate, float vol, float pan,
                     void* data0, void* data1, u32 size);
void CSND_SetVol(u32 channel, u32 chnVolumes, u32 capVolumes);
void CSND_SetPlayState(u32 channel, u32 value);
Result CSND_FlushDataCache(const void* adr, u32 size);
