// T135 D2: the two keys that bring the menu at power-on, read straight from the C64U's matrix registers: C= on the keyboard and the fire button
// of joystick port 1. No USB keyboard (Q2). A sample takes a few register accesses; the latch keeps what any sample saw, so a key held while
// the machine starts is not lost between two looks. Must not run while Keyboard_C64's timer scan runs (hostui.cc creates that after the decision).
#ifndef HOST_MENUKEYS_H
#define HOST_MENUKEYS_H

#include <stdint.h>

void menukeys_sample(void);          // one look at both keys, latched
bool menukeys_cbm(void);
bool menukeys_fire(void);
uint32_t menukeys_samples(void);     // how many looks so far (for the console line)

#endif
