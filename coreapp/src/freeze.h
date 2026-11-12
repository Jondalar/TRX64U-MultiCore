// Freeze like the C64 (T93 D9, D7, S4): the machine stops, the menu has the pixel layer, and leaving the menu gives the machine
// exactly back. Pure: the machine, the layer and the keys are FreezeOps, so tests/coreapp/ runs the sequence against a fake core
// whose PAUSED bit rises after a delay and a key that stays down.
#ifndef COREAPP_FREEZE_H
#define COREAPP_FREEZE_H

#include "desc.h"

// The descriptor's PAUSED register, read the way its hardware wants it (FreezeDesc::word). Board and benches both come through here.
uint8_t freeze_status(CoreIo *io, const FreezeDesc &d);

class FreezeOps {
public:
    virtual ~FreezeOps() { }
    virtual void stop(bool on) = 0;                 // C64_STOP
    virtual uint8_t status() = 0;                   // the descriptor's PAUSED register
    virtual void layer_to_menu() = 0;               // the core's picture off, the menu on
    virtual void layer_to_core() = 0;               // the menu off, the core's layer set back
    virtual bool wait_keys_idle(uint32_t timeout_ms) = 0;    // blocks until no key is down; false on timeout
    virtual uint32_t now_ms() = 0;
    virtual void delay_ms(uint32_t ms) = 0;
    virtual void log(const char *text) = 0;
};

class Freeze {
public:
    // debounce_ms: how long the matrix has to stay idle before the machine runs again (D7: measured on the board, 40 is a start)
    Freeze(const FreezeDesc &d, FreezeOps *ops, uint32_t debounce_ms = 40) : d_(d), ops_(ops), debounce_(debounce_ms), frozen_(false) { }

    // Stops the machine and waits for PAUSED, at most d.timeout_ms (then says so and goes on: S4), then gives the layer to the menu.
    // Returns true when PAUSED was seen.
    bool enter();
    // The layer back to the core, then waits until the keys have been idle for the debounce time (the key that closed the menu is
    // never typed into the machine), then releases the machine.
    // keep_stopped: the layer goes back to the core but C64_STOP stays (Pause: the machine stands still under its own picture).
    void leave(bool keep_stopped = false);
    bool frozen() const { return frozen_; }
    uint32_t debounce() const { return debounce_; }

private:
    FreezeDesc d_;
    FreezeOps *ops_;
    uint32_t debounce_;
    bool frozen_;
};

#endif
