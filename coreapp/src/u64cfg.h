// The C64U's own hardware configuration, written by the core application (T104.2 UI, Alex 2026-10-28 decision 5): video, audio mixer, SID sockets and
// addressing, joystick, turbo, serial bus, LEDs, power. Gideon's U64Config::effectuate_settings() reads a settings store and writes the registers of the
// U64 block; this is the same sequence reading this application's settings (the core's .cfg, the menu.toml groups video, audio, sid, input, expert).
//
// One operation per group, each takes the setting's value (ignored: the group is written as a whole, as Gideon's effectuate does, so a value changed by
// REST, by the menu or at the start gives the same registers). PROVENANCE: vendor/gideon/software/u64/u64_config.cc (Gideon Zweijtzer, GPL-3.0), the
// register sequences and tables of its effectuate_settings functions; the keys are the item names of its stores.
#ifndef COREAPP_U64CFG_H
#define COREAPP_U64CFG_H

#include "ctx.h"
#include "ops.h"

void u64cfg_register(OpRegistry &r);

// The mixer's registers are write only: the freeze silences the SIDs through them and gives them back from the settings (Gideon u64_mute_sids / unmute).
void u64cfg_mute_sids(void);
// T228 R21 (FW u64_config.cc:1277-1283): CTRL+P / CTRL+N held at power-on = PAL / NTSC for this start only (the setting is overridden, never stored). Once per start.
void u64cfg_power_on_keys(CoreCtx *c);
void u64cfg_restore_mixer(CoreCtx *c);

// The command interface's palette and SID map commands (uci.cc through UciHost): what 0 reads the 16 colours, 1 sets all, 2 sets colour idx from rgb[0], 3 resets; the SID map bytes.
void u64cfg_palette(CoreCtx *c, int what, uint8_t idx, uint8_t (*rgb)[3]);
int u64cfg_sid_map(CoreCtx *c, uint8_t *data);

// Which SID sits in a socket (the last detection, 0 none, 1 6581, 2 8580, ...) and the names for the screen.
const char *u64cfg_sid_type_name(int type);

// The SID player's autoconfig (T228 R21 item 7, FW U64Config::SidAutoConfig): `count` SIDs (1 to 8) the tune asks for, each with its base (0x40 = $D400)
// and type (1 6581, 2 8580, 3 either) are mapped on the sockets and the emulated SIDs, the mixer follows. The machine must be stopped. False: no mapping
// was found (every SID is left unmapped). True also when the setting "SID Player Autoconfig" is off (nothing is done then).
bool u64cfg_sid_autoconfig(CoreCtx *c, int count, const uint8_t *base, const uint8_t *type);

#endif
