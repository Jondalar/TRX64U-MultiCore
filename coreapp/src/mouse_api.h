// The table "mouse": what the generic application asks of the USB mouse library (mousesink.cc) and the types a core's description uses to name a mouse. The generic code finds the table
// through libtab_table("mouse") (libtab.h) and calls no function of the library; an app linked without the library has no table and no mouse: the report is dropped by the USB driver's own
// path, and the application's settings answer the driver (baseset.cc). Size first; within a major members are only appended. The header ships in the SDK (include/), so a library or a core's app can read the table.
#ifndef COREAPP_MOUSE_API_H
#define COREAPP_MOUSE_API_H

#include <stdint.h>

struct CoreCtx;

enum MouseKind : uint8_t { MOUSE_COUNTERS = 0, MOUSE_POT1351 = 1 };

struct MouseSinkSpec {
    uint16_t reg_dx, reg_dy, reg_btn;     // reg_dy: this write sends the pair (MOUSE_COUNTERS only)
    int16_t clamp;                        // 4000: the accumulator
    int16_t step;                         // 127: one pair
    uint8_t interval_ms;                  // 2
    MouseKind kind;                       // the backend; MOUSE_POT1351 uses none of the fields above
};

// The critical section around the accumulators (the input task and the poll task share them; each library sets its own: mouse_board.cc here, pad_board.cc for the pad service; both use portENTER_CRITICAL). The board sets it once; null: none (benches).
class MouseCrit {
public:
    virtual ~MouseCrit() { }
    virtual void enter() = 0;
    virtual void leave() = 0;
};

// The 1351 backend's board side: the POT lines and joystick port 1. x / y are the pointer position in counts (the board scales it to the
// pot value; a 1351 steps the pot by two per count); joy is the port-1 byte, active low (left 0x10, right 0x01, middle 0x02, 0x1F released).
// Called from the USB task. Null (benches, a board without pots): the 1351 backend writes nothing.
class MousePots {
public:
    virtual ~MousePots() { }
    virtual void position(int16_t x, int16_t y) = 0;
    virtual void buttons(uint8_t joy) = 0;
};

#define MOUSE_API_NAME "mouse"

typedef struct MouseApi {
    uint32_t size;
    void (*configure)(const MouseSinkSpec *spec);      // the core's sink, made known once when the engines are set up for the core (null: no mouse). Until then motion is dropped
    void (*reset)(void);                               // forgets everything: a new core (not the scaling below, which is the person's setting)
    bool (*pace)(CoreCtx *c);                          // the poll's side: true when it wrote a pair
    void (*set_sensitivity)(int value);                // operation mouse.sens: 1 .. 16, 8 = 1.0
    void (*set_acceleration)(int value);               // operation mouse.accel: 0 Off, 1 Adaptive
    bool (*scaling)(int cfg_key, int *value);          // the value for the USB driver's key (0x5D, 0x5E) when a core declared it; false: nobody has set it, or another key
} MouseApi;

#endif
