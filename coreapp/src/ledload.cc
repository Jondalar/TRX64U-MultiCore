#include "ledload.h"
#include "engines.h"

// The high nibble (LED 1, zero-based as svc_board.h; output 2 of host/case_leds.v): the bring-up meaning (host/top.v) is "transmitters running and the picture locked", which no selector source is; a core
// that is running has both, so On.
void ledload_set(CoreCtx *c, bool on)
{
    LedLoad &l = engines_of(c)->led;
    if (on && !l.drive) {
        c->io->case_led_select((uint8_t)((LED_SRC_ON << 4) | LED_SRC_DRIVE_A_ACT));
        l.drive = true;
    } else if (!on && l.drive) {                 // back to the steady state, then it stays out of it
        c->io->case_led_select((uint8_t)((LED_SRC_ON << 4) | LED_SRC_ON));
        l.drive = false;
    }
}
