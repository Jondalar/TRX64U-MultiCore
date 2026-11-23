// The table "pad": what the generic application asks of the gamepad library (pad.cc, hidpad.cc, pad_ops.cc, padwiz.cc, xinput.cc, usb_xinput.cc) and the types a core's description uses to name a pad
// (PadSpec). The generic code finds the table through libtab_table("pad") (libtab.h) and calls no function of the library; an app linked without it has no table and no pad: no operation pad.*,
// no pace. Size first; within a major members are only appended. The header ships in the SDK (include/), so a library or a core's app can read the table.
#ifndef COREAPP_PAD_API_H
#define COREAPP_PAD_API_H

#include <stdint.h>
#include "ops.h"

struct CoreCtx;

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

// Where the words go (PadSpec::out). REGS: a 16 bit pair per player in the core's register block (io->wr, low byte first). JOY (T228 R10): the host's joystick
// lines (CoreIo::joy_lines): reg[] are joystick ports (0 = port 1, 1 = port 2), the word's bits 0..4 are up, down, left, right, fire (at most 5 buttons).
#define PAD_OUT_REGS 0
#define PAD_OUT_JOY  1

struct PadSpec {
    uint16_t reg[PAD_PLAYERS];                   // low byte of the player's 16 bit button register; the high byte is the next address
    uint8_t nbuttons;                            // the core's buttons, bit n of the word
    const char *const *names;                    // nbuttons labels, for the prompts of the mapping ("A", "Start", "Up")
    const uint8_t *def_map;                      // nbuttons entries: a source id, PAD_SRC_NONE or a role (PAD_ROLE_*, resolved per pad)
    // T225 (the Amiga cores' game port 2): ONE port, fed by the first pad that is plugged (whichever player slot it has); its map is kept per pad,
    // keyed by VID:PID, in the core's .cfg ("Pad map 2DC8:6001" = <target type digit><nbuttons hex pairs>), so that the same pad finds it again on any
    // USB socket. The port is reg[pad_port()] (reg[0] unless the operation pad.port picks reg[1]; the Amiga's reg[1] is 0 and never picked). `id_loaded`
    // (may be null) is called, from pad_pace, when a pad with a stored map appears: the stored target type.
    uint8_t by_id;
    void (*id_loaded)(CoreCtx *c, int type);
    uint8_t out;                                 // PAD_OUT_REGS (0: the C++ cores) or PAD_OUT_JOY (T228 R10: a menu record's [pad], descrec.h)
};

#define PAD_API_NAME "pad"

typedef struct PadApi {
    uint32_t size;
    void (*register_ops)(OpRegistry &r);       // the base's pad operations and states (pad.set_map, pad.port, pad.poll, pad.map.*, menu.pad_map_1 / _2, pad.1, pad.2 ...), for every core
    bool (*pace)(CoreCtx *c);                  // the poll's side: writes the words that changed; true when it wrote
    void (*reset)(void);                       // the core's registers are at zero again (Reload core): the next pace writes both words
} PadApi;

#endif
