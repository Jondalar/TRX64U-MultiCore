// The C128's extra keys from a USB keyboard (T113 Q8, D7): the HID usage code (keyboard page 0x07) of a key -> its cell in the extra matrix
// columns K0-K2 ($D02F bits 0-2, the rows are the CIA1 PB lines) or its bit in XSPECIAL. Pure; the register writes are c128_xkeys_apply.
// The application delivers the usages down to c128_xkeys_apply through the descriptor's rawkeys record in held mode (rawkeys.h, c128.cc; T141 P2).
#ifndef COREAPP_C128_KEYS_H
#define COREAPP_C128_KEYS_H

#include "io.h"

struct C128XKey {
    int8_t col;                           // 0..2 = K0..K2, -1: not an extra matrix key
    int8_t row;                           // 0..7
};

// The matrix cell of the extra key a HID usage code stands for; false: the code is not one (a letter, a digit, F1-F8 ... stay the C64U keyboard's).
bool c128_xkey_for(uint8_t usage, C128XKey *out);
// The XSPECIAL bit (C128_XS_*) of the code, 0: none. CAPS LOCK, 40/80 DISPLAY, the monitor switch.
uint8_t c128_xspecial_for(uint8_t usage);

// The three XKEYS bytes and the XSPECIAL byte for the codes held down right now (n usage codes), written to the core's page.
void c128_xkeys_compose(const uint8_t *down, int n, uint8_t xkeys[3], uint8_t *xspecial);
void c128_xkeys_apply(CoreIo *io, const uint8_t *down, int n);

#endif
