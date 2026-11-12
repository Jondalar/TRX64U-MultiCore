// The "Volume" setting every core application has (T160 D1, T164 WP11): the master volume of the host, a gain on the core's audio before HDMI and I2S
// (host/trx_gain.v). The application adds the item to the core's own settings when it starts, as it does Picture, so no descriptor declares it; the menu's
// SHIFT+ / SHIFT- step it (menuctl.cc change_volume). A core that declares a "Volume" with a register binding of its own (the A1200 until its copy of the
// gain goes) keeps that one, and a core with CoreDesc::own_volume (the C64 with its mixer) gets none.
#ifndef COREAPP_VOLUME_H
#define COREAPP_VOLUME_H

#include "desc.h"
#include "ctx.h"

#define VOLUME_OP "volume.apply"
#define VOLUME_STEPS 41                // 0 % .. 200 % in steps of 5 %
#define VOLUME_DEFAULT 20              // 100 %, the byte 128: unity

// The byte the host's register takes for a step (round(percent * 1.28), 200 % is 255).
uint8_t volume_byte(int step);

// out = *d with the Volume item after d's own settings (and the Picture item the caller made); `store` holds the merged array (SETTINGS_MAX items) and has
// to outlive `out`. A descriptor with its own Volume or own_volume is copied as it is (null). Returns null, or the reason when there is no room.
const char *volume_extend(const CoreDesc *d, CoreDesc *out, Setting *store);

// True for the item volume_extend adds (the apply operation is ours), false for a descriptor's own.
bool volume_is_generic(const Setting &s);

// The operation behind the item: writes the host's register through CoreIo::master_volume.
void volume_register(OpRegistry &r);

#endif
