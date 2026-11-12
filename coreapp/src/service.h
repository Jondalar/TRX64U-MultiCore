// A contributor's poll (T104.1 D6, Q9): one operation called every period_ms on a task of its own, with the menu open or closed.
// It does not wait for the menu loop or for the executor; the only thing it shares with them is the core lock, which it holds while
// the operation runs. Whether it also runs while the machine is frozen is the contributor's declaration (poll_when_frozen): a frozen
// C64 asks its drive for nothing, an Amiga under an Overlay menu does.
#ifndef COREAPP_SERVICE_H
#define COREAPP_SERVICE_H

#include "exec.h"
#include "ctx.h"

// May the poll run now? Not before the core was released, not while the machine is frozen unless the contributor says so.
bool poll_allowed(const CoreCtx *c, bool when_frozen);

// A poll that keeps having work asks for the processor back at once; after this long in a row it sleeps one period instead, because the
// task is above the menu loop's priority and the loop flips the picture.
#define BURST_MAX_MS 20

class PollTask {
public:
    PollTask(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c, const char *op, uint16_t period_ms, bool when_frozen);

    // Runs the poll when it is due and allowed. Returns how many ms to wait before the next step; 0 means "again at once" (a poll that
    // had work asks for the processor back, not for a tick of sleep: frames come in bursts, sector after sector).
    uint32_t step();

    bool start(const char *name, int above_ui, uint32_t stack_words);
    void stop() { running_ = false; }                      // the task ends after its next wake-up (tests)

private:
    OsPort *os_;
    CoreLock *lock_;
    const OpRegistry *reg_;
    CoreCtx *c_;
    const OpDef *op_;
    uint16_t period_;
    bool when_frozen_;
    uint32_t last_, spin_until_, burst_from_;
    bool bursting_;
    volatile bool running_;
    static void main(void *self);
};

#endif
