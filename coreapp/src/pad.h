// The pad service (T156 P1, P2): USB gamepads as the core's own controllers. The USB driver (usb_hid.cc, hook usb_hid_set_pad_sink) hands
// over every pad it finds with its report descriptor (hidpad.h reads it), one call per input report, one when it goes away. This service keeps
// up to two pads in plug order (player 1 = the first one plugged; a pad that leaves frees its place, the next one takes the lowest free
// player; T156 Q1, recommendation, Alex not yet asked), turns a report into the core's button word through the map, and writes it into the
// core's register for that player (PadSpec::reg; PAD_OUT_JOY: the host's joystick lines of that port): on every change, and as zero when the pad leaves (S5).
//
// A SOURCE is what a pad offers: ids 0..31 are Button 1..32 of the HID Button page, 32..35 the four directions (D-pad, hat switch or stick).
// A MAP says for every button of the core which source drives it. The default map (D3: the SNES layout printed on most pads) is the core's;
// a custom map is recorded with the "Map buttons" action (one press per core button) and kept in the core's .cfg ("Pad 1 map", "Pad 2 map").
// The core sees the word as bit n = its button n (PadSpec::names order); the register is a 16 bit pair written low byte first (PAD_OUT_REGS).
#ifndef COREAPP_PAD_H
#define COREAPP_PAD_H

#include <stdint.h>
#include "ctx.h"
#include "mouse_api.h"
#include "pad_api.h"

struct PadNumbering {
    uint16_t vid, pid;                           // 0:0 is the generic entry
    const char *name;
    uint8_t src[8];                              // the source id (Button number - 1) of the roles RIGHT BOTTOM LEFT TOP L R SELECT START
};
// The numbering for a pad: its own entry, else the generic one (never null).
const PadNumbering *pad_numbering_for(uint16_t vid, uint16_t pid);
#define PAD_MODE_DEFAULT 0
#define PAD_MODE_CUSTOM  1

// A spec's default map for a pad (vid = pid = 0: none plugged, the generic numbering): roles resolved, nbuttons entries.
void pad_default_map(const PadSpec *spec, uint16_t vid, uint16_t pid, uint8_t *out);

// The USB driver's side. All three run in the USB task. attach: false when the descriptor holds no gamepad (the driver then leaves the
// interface alone); true: the pad is player n (pad_player_of). `key` is the driver's own identity of the interface.
bool pad_attach(const void *key, const char *name, uint16_t vid, uint16_t pid, const uint8_t *desc, int len);
void pad_report(const void *key, const uint8_t *data, int len);
void pad_detach(const void *key);
int  pad_player_of(const void *key);             // 0/1, or -1
// The report descriptor the pad enumerated with, kept for REST (states pad.1.descriptor / pad.2.descriptor) and for a second console dump a while after
// the plug, so that the 4 KB console ring cannot lose it. pad_desc_dumps(): how many times it was printed.
const uint8_t *pad_descriptor(int player, int *len);
int  pad_desc_dumps(int player);
const char *pad_name(int player);
uint16_t pad_vid(int player);
uint16_t pad_pid(int player);                // the name the pad enumerated with, "" when no pad is that player

// The critical section around the shared state (the USB task and the poll task); null (benches): none. The mouse sink's type, the board's one.
void pad_set_crit(MouseCrit *crit);

// A core starts or restarts: its spec (null: no pad support, reports are taken and dropped), the maps from the .cfg, the mode. Forgets what was
// sent so that the next pace writes both registers.
void pad_configure(const PadSpec *spec);
void pad_reset();

// Makes the service the one of this core: a no-op while the core's spec is the one in use, else pad_configure() and pad_load_maps() (the pace and the
// menu call it first, whichever comes first after the core started).
void pad_ensure(CoreCtx *c);

// T225: how many of the spec's buttons the target in force has (the others are never set and the mapping wizard does not ask for them; 0 = all) and
// the target's type number (stored with a per-pad map). pad_nbuttons() is the count in force.
void pad_set_target(int type, int nbuttons);
int  pad_nbuttons();
int  pad_target_type();
// The slot (0..1, in plug order) of the pad that serves `player`: the player itself, except in a by_id spec, where it is the first slot in use.
int  pad_slot(int player);
// By-id specs: keeps the map in force for the pad of `player` under "Pad map <VID:PID>" with the target type; false when no pad is there or the file cannot be written.
bool pad_save_id_map(Settings *s, int player);

// T228 R10 (operation pad.port): which of the spec's two registers the first pad drives, 0 or 1; the second pad (a players spec) drives the other one. A
// change releases what the old register held and writes the words again at the next pace. 0 after pad_configure (a core that never sets it: as before).
void pad_set_port(int index);
int  pad_port();
// What the mapping dialog of a by_id spec calls its port: "port 1" / "port 2" for the joystick output (the port in force), else "port 2" (T225's one port).
const char *pad_port_name();

// The poll's side: writes the words that changed. True when it wrote.
bool pad_pace(CoreCtx *c);

// ---- the maps ----
void pad_set_mode(int mode);                     // PAD_MODE_DEFAULT / PAD_MODE_CUSTOM
int  pad_mode();
bool pad_custom_valid(int player);
const uint8_t *pad_active_map(int player);       // nbuttons sources of the map in force
// The .cfg's text of a custom map: two hex digits per button of the core. Parse returns false when it does not fit the core (wrong length, a
// source above 35 that is not "none") - the map is then not taken.
bool pad_map_to_text(const uint8_t *map, int n, char *out, int outn);
bool pad_map_from_text(const char *text, int n, uint8_t *map);
bool pad_set_custom(int player, const uint8_t *map);   // n = the spec's nbuttons
void pad_clear_custom(int player);
// Loads "Pad 1 map" / "Pad 2 map" of the core's settings into the service and the mode from setting "Pad map"; saves.
void pad_load_maps(Settings *s);
bool pad_save_map(Settings *s, int player);

// The word a source state gives under a map (exported for the tests).
uint16_t pad_word(const PadSpec *spec, const uint8_t *map, uint32_t buttons, uint8_t dpad);
// The raw state of a player's pad (tests, the menu).
bool pad_state(int player, uint32_t *buttons, uint8_t *dpad);

// ---- the capture of the mapping wizard ----
// Start waits until the pad is released, then reports the lowest source that goes down next. poll: true and *src when one was pressed.
void pad_capture_start(int player);
bool pad_capture_poll(uint8_t *src);
void pad_capture_stop();
bool pad_capturing();
const char *pad_src_name(uint8_t src, char *buf, int n);   // "Button 3", "Up"

// The operations and states of the base (pad_ops.cc): pad.set_map(value) for the setting "Pad map"; pad.port(value) (pad_set_port); pad.poll (pad_pace);
// menu.pad_map_1 / _2 which the menu answers with
// the mapping wizard; states pad.1 / pad.2 ("<name> buttons=<hex> dpad=<hex>" or "-").
void pad_register(OpRegistry &r);
// The mapping wizard (padwiz.cc, T192.3): operations pad.map.begin / next / back / cancel / tick, menu.pad_map_1 / _2 (they begin it), states pad.map.active / title / left / right / sub.
void padwiz_register(OpRegistry &r);
// Starts the wizard for a player (what menu.pad_map_1 does); T225: for a by_id spec the one port. OPE_FAILED with a message in `o` when no pad is there.
int pad_wizard_begin(CoreCtx *c, int player, OpOut &o);

#endif
