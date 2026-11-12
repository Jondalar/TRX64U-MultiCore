#include "service.h"
#include <stdio.h>

bool poll_allowed(const CoreCtx *c, bool when_frozen)
{
    return c->started && (!c->frozen || when_frozen);
}

PollTask::PollTask(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c, const char *op, uint16_t period_ms, bool when_frozen)
    : os_(os), lock_(lock), reg_(reg), c_(c), op_(reg->find_op(op)), period_(period_ms ? period_ms : 1), when_frozen_(when_frozen),
      last_(os->now_ms() - period_), spin_until_(os->now_ms()), burst_from_(0), bursting_(false), running_(false)
{
}

uint32_t PollTask::step()
{
    if (!op_) {
        return period_;
    }
    uint32_t now = os_->now_ms();
    if (!poll_allowed(c_, when_frozen_)) {
        return period_;
    }
    uint32_t since = now - last_;
    if (since < period_) {
        return (int32_t)(spin_until_ - now) > 0 ? 0 : period_ - since;
    }
    last_ = now;
    OpArgs none;
    OpResult r;
    lock_->lock();
    if (!poll_allowed(c_, when_frozen_)) {            // the menu froze the machine while this task waited for the lock: the check above is stale
        lock_->unlock();
        return period_;
    }
    bool known = reg_->bind(op_, 0, 0, &none, r.msg, sizeof(r.msg)) == OPE_OK;
    if (known) {
        reg_->run(c_, op_, none, &r);
    }
    lock_->unlock();
    if (known && r.more) {
        if (!bursting_) {
            bursting_ = true;
            burst_from_ = now;
        }
        if (now - burst_from_ >= BURST_MAX_MS) {          // the menu loop and the picture are lower: they get a turn
            bursting_ = false;
            spin_until_ = now;
            return period_;
        }
        spin_until_ = now + 5;
        return 0;
    }
    bursting_ = false;
    return (int32_t)(spin_until_ - now) > 0 ? 0 : period_;
}

void PollTask::main(void *self)
{
    PollTask *t = static_cast<PollTask *>(self);
    while (t->running_) {
        t->os_->sleep_ms(t->step());
    }
}

bool PollTask::start(const char *name, int above_ui, uint32_t stack_words)
{
    running_ = true;
    if (!os_->task_new(name, &PollTask::main, this, above_ui, stack_words)) {
        running_ = false;
        return false;
    }
    return true;
}
