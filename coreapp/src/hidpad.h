// A gamepad's HID report descriptor, read to the HID 1.11 specification (T156 P1). Generic: nothing in here knows a vendor. The parser
// looks for a Generic Desktop Game Pad (5) or Joystick (4) application collection and in it for the Button page (9) fields, the hat switch
// (0x39), the X and Y axes (0x30, 0x31) and the four D-pad usages (0x90-0x93); a decode then turns one input report into a button word, the
// four directions and nothing else. Pure: no heap, no board.
//
//   buttons  bit n of PadRaw::buttons is Button n+1 of the Button page (n < 32)
//   dpad     bit 0 up, 1 down, 2 left, 3 right: from the hat switch, from the D-pad usages and from X / Y with a dead zone (T156 Q2: the analog
//            stick is a D-pad too; recommendation, Alex not yet asked)
#ifndef COREAPP_HIDPAD_H
#define COREAPP_HIDPAD_H

#include <stdint.h>

#define HIDPAD_RUNS 4
#define HIDPAD_DPAD_UP    0x01
#define HIDPAD_DPAD_DOWN  0x02
#define HIDPAD_DPAD_LEFT  0x04
#define HIDPAD_DPAD_RIGHT 0x08
// An axis counts as a direction when it is further from its centre than this share (per 256) of the half range: 40 %.
#define HIDPAD_DEADZONE_256 102

struct HidField {
    bool ok;
    bool sign;                  // logical minimum below zero
    uint8_t size;               // bits
    uint16_t bit;               // offset in the report, behind the report ID byte
    int32_t lmin, lmax;
};

struct HidBtnRun {
    uint16_t bit;               // offset of the first button
    uint8_t count;
    uint8_t first;              // its number on the Button page (1 based)
};

struct HidPadLayout {
    bool valid;                 // a pad with at least two buttons and a hat, X/Y or D-pad usages was found
    bool has_ids;               // the descriptor uses report IDs: byte 0 of a report is the ID
    uint8_t report_id;          // the report the fields are in (when has_ids)
    uint16_t bits;              // the report's length in bits, behind the ID
    uint16_t need;              // where the last field used ends, in bits: a shorter report is refused
    HidBtnRun runs[HIDPAD_RUNS];
    uint8_t nruns;
    HidField hat, x, y;
    HidField dp[4];             // D-pad up, down, left, right usages
};

struct PadRaw {
    uint32_t buttons;
    uint8_t dpad;
    bool operator==(const PadRaw &o) const { return buttons == o.buttons && dpad == o.dpad; }
    bool operator!=(const PadRaw &o) const { return !(*this == o); }
};

// Reads the descriptor; false (and layout->valid false) when it holds no gamepad.
bool hidpad_parse(const uint8_t *desc, int len, HidPadLayout *layout);

// One input report (the bytes the interrupt endpoint delivered, the ID byte first when the descriptor has report IDs). False: a report of
// another ID or one too short for the fields; *out is not touched then.
bool hidpad_decode(const HidPadLayout &l, const uint8_t *data, int len, PadRaw *out);

#endif
