// Pad and joystick as keys (T216 P4, GitHub #80): a generic service of the core application, usable by every core that takes the host's C64 key matrix
// (the large cores' `c64_keys`). The core says which of the pad's buttons press a key; the person chooses the key per button in the core's settings.
//   - the buttons are the pad service's (pad.h: PadSpec names, the USB pads' button words written to the core's register pair) and the host's joystick
//     ports (the core reads c64_joy1 / c64_joy2 itself); a core with this service puts all of them on the same five buttons: up, down, left, right, fire
//   - a key is named by its place in the host's key matrix, `col * 8 + row` (b5:0), b6 = C64 SHIFT as well (a cursor LEFT is cursor RIGHT + SHIFT); 0xFF none
//   - the core ORs the key into the matrix before its own keyboard translation (cores/pet/rtl/trx/pet_padkeys.v is the PET's copy of the block), so the machine
//     sees a typed key and the core's own keyboard mapping applies
// A core declares: PADKEY_BIND for the five registers (one byte each, the key code), PADKEY_ROW for the five settings, and a PadSpec for the pad service.
// Nothing here needs a source file of its own: the lists are static in this header.
#ifndef COREAPP_PADKEYS_H
#define COREAPP_PADKEYS_H

#include "desc.h"
#include <string.h>
#include <stdio.h>

// the keys the menu offers, shown as "Key 8", "Key Space" (T216, Alex 2026-11-15: "Key 8"; None stays): the digits (games use them), the cursor keys, space and return, all letters A-Z, the arithmetic keys, stop
static const char *const padkey_names[] = {
    "None", "Key 8", "Key 2", "Key 4", "Key 6", "Key 5", "Key 0", "Key 1", "Key 3", "Key 7", "Key 9",
    "Key Space", "Key Return", "Key Cursor up", "Key Cursor down", "Key Cursor left", "Key Cursor right", "Key Home", "Key Del", "Key Stop",
    "Key W", "Key A", "Key S", "Key D", "Key Q", "Key E", "Key Z", "Key X", "Key +", "Key -", "Key *", "Key /",
    // T232: the other 18 letters (W A S D Q E Z X above are not repeated); appended, so every index, stored .cfg value and default above stays
    "Key B", "Key C", "Key F", "Key G", "Key H", "Key I", "Key J", "Key K", "Key L", "Key M", "Key N", "Key O", "Key P", "Key R", "Key T", "Key U", "Key V", "Key Y",
};
static const uint8_t padkey_codes[] = {
    0xFF, 27, 59, 11, 19, 16, 35, 56, 8, 24, 32,
    60, 1, 7 | 0x40, 7, 2 | 0x40, 2, 51, 0, 63,
    9, 10, 13, 18, 62, 14, 12, 23, 40, 43, 49, 55,
    28, 20, 21, 26, 29, 33, 34, 37, 42, 36, 39, 38, 41, 17, 22, 30, 31, 25,
};
#define PADKEY_COUNT   ((int)(sizeof(padkey_codes) / sizeof(padkey_codes[0])))
// the indices of the default keys: the number pad's 8 2 4 6 for the four directions and SPACE for fire, as games on the digit keys take them
#define PADKEY_DEF_UP    1
#define PADKEY_DEF_DOWN  2
#define PADKEY_DEF_LEFT  3
#define PADKEY_DEF_RIGHT 4
#define PADKEY_DEF_FIRE  11
// the register binding of one button's key: the code is the byte written
#define PADKEY_BIND(name, reg) static const RegBind name = { reg, 0, 0xFF, padkey_codes }
// the setting of one button's key (a row of the core's table): key text, label, the binding, the default index
#define PADKEY_ROW(key, label, bind, def) { key, padkey_names, PADKEY_COUNT, def, WHEN_LIVE, 0, &bind, 0, "input", 0, 0, label }

// T232: a setting is a pad-key row when it is the PADKEY_ROW shape (the lists above are static per file, so the row is told by its content)
static inline bool padkey_row(const Setting &s)
{
    return s.kind == 0 && s.nvalues == PADKEY_COUNT && s.bind && s.bind->bytes && s.bind->mask == 0xFF && memcmp(s.bind->bytes, padkey_codes, sizeof(padkey_codes)) == 0;
}
#define PADKEY_NOT_AVAILABLE "That key is not available - use A-Z, 0-9, Space or a cursor key"
// T232: learn a key by pressing it. `down` = the keys down as the hotkey learn reads them (bit = matrix index col*8+row, the USB keyboard's raw matrix, SHIFT = bit 15 / 52).
// Returns the row's new value (an index of the list), or -1 with the reason in `line` (not one key, or a key that is not available).
static inline int padkey_learn_value(uint64_t down, char *line, int n)
{
    const uint64_t shifts = (1ull << 15) | (1ull << 52);
    const bool shift = (down & shifts) != 0;
    uint64_t keys = down & ~shifts;
    line[0] = 0;
    int nk = 0, k = -1;
    for (int i = 0; i < 64; i++) {
        if ((keys >> i) & 1) {
            nk++;
            k = i;
        }
    }
    if (nk == 0) {
        snprintf(line, n, "No key");
        return -1;
    }
    if (nk != 1) {
        snprintf(line, n, "Press one key");
        return -1;
    }
    for (int pass = 0; pass < 2; pass++) {                           // SHIFT + a cursor key is the other cursor key (code | 0x40); else SHIFT is ignored
        const int want = k | ((pass == 0 && shift) ? 0x40 : 0);
        for (int i = 1; i < PADKEY_COUNT; i++) {
            if (padkey_codes[i] == want) {
                return i;
            }
        }
    }
    snprintf(line, n, "%s", PADKEY_NOT_AVAILABLE);
    return -1;
}

#endif
