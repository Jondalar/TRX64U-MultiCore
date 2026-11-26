// The executor and the core lock (T104.1 D2, Q9). Every operation runs, in the order it was queued, on one task; whoever asked
// waits for the result. A contributor's poll runs on a task of its own (service.h). The one thing they share is the core: whoever
// touches it holds the CoreLock. Written against OsPort, so the same code runs on FreeRTOS (run.cc) and on threads (tests/coreapp).
#ifndef COREAPP_EXEC_H
#define COREAPP_EXEC_H

#include <stdint.h>
#include "ops.h"

class OsPort {
public:
    virtual ~OsPort() { }
    virtual void *mutex_new() = 0;
    virtual void lock(void *m) = 0;
    virtual void unlock(void *m) = 0;
    virtual void *sem_new() = 0;                           // binary, starts at 0: a give is remembered once
    virtual void give(void *s) = 0;
    virtual bool take(void *s, uint32_t ms) = 0;           // ms = OS_FOREVER waits for ever
    // A task above the UI's priority by `above_ui` steps (0: the UI's own). fn returns when the task is to end.
    virtual bool task_new(const char *name, void (*fn)(void *), void *arg, int above_ui, uint32_t stack_words) = 0;
    virtual void *self() = 0;                              // an id of the calling task
    virtual uint32_t now_ms() = 0;
    virtual void sleep_ms(uint32_t ms) = 0;                // 0: give the processor to a task that is ready
};
#define OS_FOREVER 0xFFFFFFFFu

// The lock of the core's registers, windows and state. An operation holds it while its handler runs, a poll while its handler
// runs; the menu loop does not take it.
class CoreLock {
public:
    explicit CoreLock(OsPort *os) : os_(os), m_(os->mutex_new()), owner_(0) { }
    void lock() { os_->lock(m_); owner_ = os_->self(); }
    void unlock() { owner_ = 0; os_->unlock(m_); }
    // Does the calling task hold the lock now? Only the holder can see its own id here, so no other task reads it as true (the lock is not recursive: a call that may
    // come from a holder or from another task takes it only when this is false).
    bool mine() { return owner_ && owner_ == os_->self(); }
private:
    OsPort *os_;
    void *m_;
    void *volatile owner_;
};

#define EXEC_SLOTS 8

class Executor {
public:
    Executor(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c);

    bool start(const char *name, int above_ui, uint32_t stack_words);
    void stop();                                           // the task ends after the request it is on (tests)

    // Checks the arguments in the calling task, queues the request and waits for the answer. Before start(), and when the executor's
    // own task asks (an operation that calls an operation), the handler runs at once on the calling task.
    int call(const char *name, const OpIn *in, int nin, OpResult *r);
    int queued() const { return count_; }

private:
    struct Slot {
        const OpDef *def;
        OpArgs args;
        OpResult *out;
        void *done;
    };
    OsPort *os_;
    CoreLock *lock_;
    const OpRegistry *reg_;
    CoreCtx *c_;
    void *qmutex_;
    void *wake_;
    Slot slot_[EXEC_SLOTS];
    uint8_t ring_[EXEC_SLOTS];                             // slot numbers in the order they were queued
    uint8_t free_[EXEC_SLOTS];
    int head_, count_, nfree_;
    void *volatile task_;
    volatile bool running_;

    static void main(void *self);
    void loop();
    void execute(Slot &s);
};

#endif
