// T159 D2: case LED 1 as the machine's drive LED. The gateware (the A1200 build) drives the "drive A activity" input of host/case_leds.v with
// "a drive motor runs, or the hard disk is accessed"; the application only selects that source once. The setting "LED Load Indicator" binds
// the operation led.load (0 On, 1 Off): On writes the selector LED 1 = drive A activity, LED 2 = On, once; Off writes nothing, except that it
// takes the selector back to On / On once if On wrote it (a latched selector cannot be unset, and LED 1 must not follow a drive the user no longer
// wants shown). A core without that setting never calls it and nothing is written.
#ifndef COREAPP_LEDLOAD_H
#define COREAPP_LEDLOAD_H

#include "ctx.h"

// The selector values of host/case_leds.v: LED 1 in the low nibble, LED 2 in the high nibble.
enum { LED_SRC_ON = 0, LED_SRC_DRIVE_A_ACT = 4 };

struct LedLoad {
    bool drive;            // the selector written by led.load is "LED 1 = drive A activity" (so Off has something to undo)
};

void ledload_set(CoreCtx *c, bool on);

#endif
