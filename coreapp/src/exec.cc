#include "exec.h"
#include <stdio.h>
#include <string.h>

Executor::Executor(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c)
    : os_(os), lock_(lock), reg_(reg), c_(c), qmutex_(os->mutex_new()), wake_(os->sem_new()), head_(0), count_(0), nfree_(EXEC_SLOTS),
      task_(0), running_(false)
{
    memset(slot_, 0, sizeof(slot_));
    for (int i = 0; i < EXEC_SLOTS; i++) {
        slot_[i].done = os->sem_new();
        free_[i] = (uint8_t)i;
    }
}

bool Executor::start(const char *name, int above_ui, uint32_t stack_words)
{
    running_ = true;
    if (!os_->task_new(name, &Executor::main, this, above_ui, stack_words)) {
        running_ = false;
        return false;
    }
    while (!task_) {                          // a call before the task has said who it is would run on its caller: start() returns when the task is there
        os_->sleep_ms(1);
    }
    return true;
}

void Executor::stop()
{
    running_ = false;
    os_->give(wake_);
}

void Executor::main(void *self)
{
    static_cast<Executor *>(self)->loop();
}

void Executor::loop()
{
    task_ = os_->self();
    while (running_) {
        os_->take(wake_, OS_FOREVER);
        for (;;) {
            os_->lock(qmutex_);
            if (!count_) {
                os_->unlock(qmutex_);
                break;
            }
            Slot &s = slot_[ring_[head_]];
            os_->unlock(qmutex_);
            execute(s);
        }
    }
}

// The oldest request: handler under the core lock, answer, free the slot.
void Executor::execute(Slot &s)
{
    lock_->lock();
    reg_->run(c_, s.def, s.args, s.out);
    lock_->unlock();
    os_->lock(qmutex_);
    head_ = (head_ + 1) % EXEC_SLOTS;
    count_--;
    os_->unlock(qmutex_);
    os_->give(s.done);
}

int Executor::call(const char *name, const OpIn *in, int nin, OpResult *r)
{
    const OpDef *d = reg_->find_op(name);
    if (!d) {
        return reg_->call(c_, name, in, nin, r);                  // names the missing operation
    }
    OpArgs a;
    r->msg[0] = 0;
    r->more = false;
    r->ndata = 0;
    r->code = reg_->bind(d, in, nin, &a, r->msg, sizeof(r->msg));
    if (r->code != OPE_OK) {
        return r->code;                                          // refused here: the handler never runs
    }
    if ((task_ && os_->self() == task_) || lock_->mine()) {
        return reg_->run(c_, d, a, r);                            // the executor's own task, or a task that holds the core lock (the start hook, a poll): queued, it would wait for itself
    }
    if (!running_ || !task_) {
        lock_->lock();
        reg_->run(c_, d, a, r);
        lock_->unlock();
        return r->code;
    }
    os_->lock(qmutex_);
    if (!nfree_) {
        os_->unlock(qmutex_);
        snprintf(r->msg, sizeof(r->msg), "%s: the executor queue is full", name);
        return r->code = OPE_BUSY;
    }
    uint8_t si = free_[--nfree_];
    Slot &s = slot_[si];
    s.def = d;
    s.args = a;
    s.out = r;
    ring_[(head_ + count_) % EXEC_SLOTS] = si;
    count_++;
    os_->unlock(qmutex_);
    os_->give(wake_);
    os_->take(s.done, OS_FOREVER);
    os_->lock(qmutex_);
    free_[nfree_++] = si;
    os_->unlock(qmutex_);
    return r->code;
}
