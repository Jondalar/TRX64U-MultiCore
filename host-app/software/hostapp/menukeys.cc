#include "menukeys.h"
#include "u64.h"
#include "u2p.h"

// keyboard_c64.cc: the joystick port the matrix line reads (0 = port 2, 1 = port 1). Writing U64II_KEYB_JOY selects the port, reading it returns the joystick.
extern uint8_t keyb_joy_select;
extern uint8_t wasd_to_joy;

static bool latch_cbm, latch_fire;
static uint32_t n_samples;

// The registers and their order are Keyboard_C64::scan()'s (vendor/gideon/software/io/c64/keyboard_c64.cc), read there for this:
//   joystick: ROW := FF, COL := FF (keyboard deselected), JOY := port, read JOY twice; the five low bits are up, down, left, right, fire; low = pressed
//   keyboard: COL := ~(1 << y) selects column y, a low bit x in ROW = key (y, x) down; modifier_map has C= at y 7, x 5 (index 0x3D)
void menukeys_sample(void)
{
    MATRIX_WASD_TO_JOY = 0;               // as scan(): the WASD-as-joystick mapping must not feed the lines
    BLING_RX_FLAGS = 0x01;                // and the bling board's shift lock stays out of the matrix
    U64II_KEYB_ROW = 0xFF;
    U64II_KEYB_ROW = 0xFF;
    U64II_KEYB_COL = 0xFF;
    U64II_KEYB_COL = 0xFF;
    U64II_KEYB_JOY = 1;                   // port 1
    uint8_t j = U64II_KEYB_JOY;
    j = U64II_KEYB_JOY;
    U64II_KEYB_JOY = keyb_joy_select;
    if (!(j & 0x10)) {
        latch_fire = true;
    }
    uint8_t col = 0x7F, r;                // column 7
    U64II_KEYB_COL = 0xFF;
    U64II_KEYB_COL = col;
    U64II_KEYB_COL = col;
    do {
        r = U64II_KEYB_ROW;
        U64II_KEYB_COL = col;
    } while (r != U64II_KEYB_ROW);
    if (!(r & 0x20)) {
        latch_cbm = true;
    }
    U64II_KEYB_COL = 0xFF;
    BLING_RX_FLAGS = 0x00;
    MATRIX_WASD_TO_JOY = wasd_to_joy;
    n_samples++;
}

bool menukeys_cbm(void) { return latch_cbm; }
bool menukeys_fire(void) { return latch_fire; }
uint32_t menukeys_samples(void) { return n_samples; }
