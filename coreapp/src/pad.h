// The pad service (T156 P1, P2): USB gamepads as the core's own controllers. The USB driver (usb_hid.cc, hook usb_hid_set_pad_sink) hands
// over every pad it finds with its report descriptor (hidpad.h reads it), one call per input report, one when it goes away. This service keeps
// up to two pads in plug order (player 1 = the first one plugged; a pad that leaves frees its place, the next one takes the lowest free
// player; T156 Q1, recommendation, Alex not yet asked), turns a report into the core's button word through the map, and writes it into the
// core's register for that player (PadSpec::reg): on every change, and as zero when the pad leaves (S5).
//
// A SOURCE is what a pad offers: ids 0..31 are Button 1..32 of the HID Button page, 32..35 the four directions (D-pad, hat switch or stick).
// A MAP says for every button of the core which source drives it. The default map (D3: the SNES layout printed on most pads) is the core's;
// a custom map is recorded with the "Map buttons" action (one press per core button) and kept in the core's .cfg ("Pad 1 map", "Pad 2 map").
// The core sees the word as bit n = its button n (PadSpec::names order); the register is a 16 bit pair written low byte first.
#ifndef COREAPP_PAD_H
#define COREAPP_PAD_H

#include <stdint.h>
#include "ctx.h"
#include "mousesink.h"

#define PAD_PLAYERS      2
#define PAD_SRC_UP       32
#define PAD_SRC_DOWN     33
#define PAD_SRC_LEFT     34
#define PAD_SRC_RIGHT    35
#define PAD_SRC_NONE     0xFF
#define PAD_BUTTONS_MAX  16
// D3, the default map: the SNES layout printed on most pads (A right, B bottom, X top, Y left). A descriptor names a button by its POSITION on the pad
// (a role); which Button number a position has depends on the pad, so the role is resolved with the numbering of the pad's VID:PID (pad.cc,
// pad_numbering_for): a table of pads read on the board, and for every other pad the generic numbering, which is an ASSUMPTION (1 bottom, 2 right,
// 3 left, 4 top, 5 L, 6 R, 7 Select, 8 Start: an Xbox-style pad's A B X Y LB RB Back Start). Proven so far: 2DC8:6001 (8BitDo SN30 Pro, D-input),
// board 2026-10-29.
#define PAD_ROLE_RIGHT   0x40
#define PAD_ROLE_BOTTOM  0x41
#define PAD_ROLE_LEFT    0x42
#define PAD_ROLE_TOP     0x43
#define PAD_ROLE_L       0x44
#define PAD_ROLE_R       0x45
#define PAD_ROLE_SELECT  0x46
#define PAD_ROLE_START   0x47
#define PAD_D3_BOTTOM    PAD_ROLE_BOTTOM
#define PAD_D3_RIGHT     PAD_ROLE_RIGHT
#define PAD_D3_LEFT      PAD_ROLE_LEFT
#define PAD_D3_TOP       PAD_ROLE_TOP
#define PAD_D3_L         PAD_ROLE_L
#define PAD_D3_R         PAD_ROLE_R
#define PAD_D3_SELECT    PAD_ROLE_SELECT
#define PAD_D3_START     PAD_ROLE_START

struct PadNumbering {
    uint16_t vid, pid;                           // 0:0 is the generic entry
    const char *name;
    uint8_t src[8];                              // the source id (Button number - 1) of the roles RIGHT BOTTOM LEFT TOP L R SELECT START
};
// The numbering for a pad: its own entry, else the generic one (never null).
const PadNumbering *pad_numbering_for(uint16_t vid, uint16_t pid);
#define PAD_MODE_DEFAULT 0
#define PAD_MODE_CUSTOM  1

struct PadSpec {
    uint16_t reg[PAD_PLAYERS];                   // low byte of the player's 16 bit button register; the high byte is the next address
    uint8_t nbuttons;                            // the core's buttons, bit n of the word
    const char *const *names;                    // nbuttons labels, for the prompts of the mapping ("A", "Start", "Up")
    const uint8_t *def_map;                      // nbuttons entries: a source id, PAD_SRC_NONE or a role (PAD_ROLE_*, resolved per pad)
};

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

// The operations and states of the base (pad_ops.cc): pad.set_map(value) for the setting "Pad map"; menu.pad_map_1 / _2 which the menu answers with
// the mapping wizard; states pad.1 / pad.2 ("<name> buttons=<hex> dpad=<hex>" or "-").
void pad_register(OpRegistry &r);
// The mapping wizard (padwiz.cc, T192.3): operations pad.map.begin / next / back / cancel / tick, menu.pad_map_1 / _2 (they begin it), states pad.map.active / title / left / right / sub.
void padwiz_register(OpRegistry &r);

#endif
