// The mouse library (T104 5.2 `mouse_sink`, T104.1 5.6): the one way a USB mouse reaches a core, for every core that has a mouse. The USB
// driver (usb_hid.cc, one hook per report) calls mouse_sink_usb_report() with the pointer's motion and buttons; the core's descriptor names
// the backend (MouseSinkSpec::kind) that turns them into what the machine has:
//   MOUSE_COUNTERS  deltas into the core's registers (Amiga): the input side calls mouse_sink_motion() from its own task, the poll calls
//                   mouse_sink_pace(), which writes at most one pair per interval. Seven numbers and a pacer: the deltas accumulate clamped
//                   to +-clamp, one pair per interval_ms is sent clamped to +-step with the rest kept, the buttons only when they changed,
//                   then DX, then DY (the write of DY sends the pair). Pure: CoreIo only; the accumulators are guarded by a critical
//                   section the board supplies.
//   MOUSE_POT1351   a 1351's position on the POT lines (C64): the library keeps the position, the board's MousePots writes it to the pot
//                   hardware and the buttons to joystick port 1.
// A core without a mouse record gets none: the report is taken and dropped, the application's UI never sees a mouse (keys only).
#ifndef COREAPP_MOUSESINK_H
#define COREAPP_MOUSESINK_H

#include "ctx.h"

enum MouseKind : uint8_t { MOUSE_COUNTERS = 0, MOUSE_POT1351 = 1 };

struct MouseSinkSpec {
    uint16_t reg_dx, reg_dy, reg_btn;     // reg_dy: this write sends the pair (MOUSE_COUNTERS only)
    int16_t clamp;                        // 4000: the accumulator
    int16_t step;                         // 127: one pair
    uint8_t interval_ms;                  // 2
    MouseKind kind;                       // the backend; MOUSE_POT1351 uses none of the fields above
};

// The critical section around the accumulators (the input task and the poll task share them). The board sets it once; null: none (benches).
class MouseCrit {
public:
    virtual ~MouseCrit() { }
    virtual void enter() = 0;
    virtual void leave() = 0;
};
void mouse_sink_set_crit(MouseCrit *crit);

// The 1351 backend's board side: the POT lines and joystick port 1. x / y are the pointer position in counts (the board scales it to the
// pot value; a 1351 steps the pot by two per count); joy is the port-1 byte, active low (left 0x10, right 0x01, middle 0x02, 0x1F released).
// Called from the USB task. Null (benches, a board without pots): the 1351 backend writes nothing.
class MousePots {
public:
    virtual ~MousePots() { }
    virtual void position(int16_t x, int16_t y) = 0;
    virtual void buttons(uint8_t joy) = 0;
};
void mouse_sink_set_pots(MousePots *pots);

// The core's sink: made known once, when the engines are set up for the core. Until then motion is dropped.
void mouse_sink_configure(const MouseSinkSpec *spec);

// motion in pixels (the USB driver has applied the sensitivity and the acceleration it reads through u64_get_usb_hid_config_value, defined in mousesink.cc), buttons b0 left, b1 right, b2 middle. Any task.
void mouse_sink_motion(int dx, int dy, uint8_t buttons);
// The USB driver's hook (usb_hid_set_mouse_sink): one call per mouse report, HID button order. Always true: the report is consumed, so the
// driver's own handling (port-1 output, cursor keys, menu navigation by mouse) does not run while this is registered.
bool mouse_sink_usb_report(int dx, int dy, uint8_t buttons);
// The poll's side. Returns true when it wrote a pair..
bool mouse_sink_pace(CoreCtx *c);
// Forgets everything: a new core, a new test. (Not the scaling below: it is the person's setting, applied at the core's start. The application's own mouse settings, baseset.h, are kept there.)
void mouse_sink_reset();

// T190: the person's mouse sensitivity (1 .. 16, 8 = 1.0) and acceleration (0 Off, 1 Adaptive) of a core that declares the items itself (the Amiga cores), set by the operations
// mouse.sens / mouse.accel (the core's settings, applied at its start and on every change). The USB driver reads them through
// u64_get_usb_hid_config_value (mousesink.cc, CFG_MOUSE_SENSITIVITY 0x5D / CFG_MOUSE_ACCELERATION 0x5E) and scales every report before the sink.
void mouse_sink_set_sensitivity(int value);
void mouse_sink_set_acceleration(int value);
// The value for the driver's key (0x5D, 0x5E); false: nobody has set it (every other core answers from the application's settings, baseset.h) or another key.
bool mouse_sink_scaling(int cfg_key, int *value);
// A new process: no value set (tests).
void mouse_sink_scaling_forget();

#endif
