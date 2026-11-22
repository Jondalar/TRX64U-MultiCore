// A contributor's poll (T104.1 D6, Q9): one operation called every period_ms on a task of its own, with the menu open or closed.
// It does not wait for the menu loop or for the executor; the only thing it shares with them is the core lock, which it holds while
// the operation runs. Whether it also runs while the machine is frozen is the contributor's declaration (poll_when_frozen): a frozen
// C64 asks its drive for nothing, an Amiga under an Overlay menu does.
#ifndef COREAPP_SERVICE_H
#define COREAPP_SERVICE_H

#include "exec.h"
#include "ctx.h"
#include "libpoll.h"

// May the poll run now? Not before the core was released, not while the machine is frozen unless the contributor says so.
bool poll_allowed(const CoreCtx *c, bool when_frozen);

// A poll that keeps having work asks for the processor back at once; after this long in a row it sleeps one period instead, because the
// task is above the menu loop's priority and the loop flips the picture.
#define BURST_MAX_MS 20

#define POLL_OP_MAX 48                                     // an operation name and its NUL; a longer one is not polled
#define POLL_IDLE_MS 100                                   // a slot nobody armed looks again after this long

class PollTask {
public:
    PollTask(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c, const char *op, uint16_t period_ms, bool when_frozen);
    // A slot: idle until arm(). The operation is looked up by name at every run, never held by pointer: the registry moves its entries when a
    // contributor is dropped.
    PollTask(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c);
    // false: the name is too long. arm() is for a slot that is not armed (a free one); the caller holds the core lock or knows the slot is idle.
    bool arm(const char *op, uint16_t period_ms, bool when_frozen);
    void disarm() { armed_ = false; }                      // the caller holds the core lock: the run that was in progress has ended
    bool armed() const { return armed_; }

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
    char name_[POLL_OP_MAX];
    volatile bool armed_;
    volatile uint16_t period_;
    volatile bool when_frozen_;
    uint32_t last_, spin_until_, burst_from_;
    bool bursting_;
    volatile bool running_;
    static void main(void *self);
};

// The polls of the libraries (T239.3 S3): a fixed number of slots, each a PollTask on a task of its own that is made when the slot is first used and
// stays (a task that returns is deleted on the board, and a slot is used again by the next library). The loader adds a library's poll when it
// registers and drops it when the library goes (libpoll.h); run.cc adds the polls of the libraries that were loaded before the core started.
#define LIB_POLLS_MAX 16
#define LIB_POLL_OWNER_MAX 32

class LibPolls : public LibPollSink {
public:
    // stack_words: every slot gets a task of that stack when it is first armed (the board); 0: no tasks, the caller steps the slots (tests with a clock
    // of their own).
    LibPolls(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c, uint32_t stack_words);
    ~LibPolls();
    bool add(const char *owner, const char *op, uint16_t period_ms, char *why, int whylen) override;
    void drop(const char *owner) override;
    int count() const;                                     // polls running
    PollTask *slot(int i) const { return i >= 0 && i < LIB_POLLS_MAX ? slot_[i].t : 0; }

private:
    struct Slot {
        PollTask *t;
        char owner[LIB_POLL_OWNER_MAX];
        bool used, started;
    };
    OsPort *os_;
    CoreLock *lock_;
    const OpRegistry *reg_;
    CoreCtx *c_;
    uint32_t stack_words_;
    Slot slot_[LIB_POLLS_MAX];
};

#endif
