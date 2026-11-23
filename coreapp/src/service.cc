#include "service.h"
#include <stdio.h>
#include <string.h>

bool poll_allowed(const CoreCtx *c, bool when_frozen)
{
    return c->started && (!c->frozen || when_frozen);
}

PollTask::PollTask(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c, const char *op, uint16_t period_ms, bool when_frozen)
    : os_(os), lock_(lock), reg_(reg), c_(c), armed_(false), period_(period_ms ? period_ms : 1), when_frozen_(when_frozen),
      last_(os->now_ms() - period_), spin_until_(os->now_ms()), burst_from_(0), bursting_(false), running_(false), op_(0), gen_(0)
{
    name_[0] = 0;
    if (op && reg->find_op(op)) {                      // as before: an operation that is not registered when the poll is made is never polled
        armed_ = arm(op, period_ms, when_frozen);
    }
}

PollTask::PollTask(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c)
    : os_(os), lock_(lock), reg_(reg), c_(c), armed_(false), period_(POLL_IDLE_MS), when_frozen_(false), last_(os->now_ms()),
      spin_until_(os->now_ms()), burst_from_(0), bursting_(false), running_(false), op_(0), gen_(0)
{
    name_[0] = 0;
}

bool PollTask::arm(const char *op, uint16_t period_ms, bool when_frozen)
{
    if (!op || !op[0] || strlen(op) >= POLL_OP_MAX) {
        return false;
    }
    armed_ = false;
    op_ = 0;
    strcpy(name_, op);
    period_ = period_ms ? period_ms : 1;
    when_frozen_ = when_frozen;
    last_ = os_->now_ms() - period_;
    spin_until_ = os_->now_ms();
    bursting_ = false;
    armed_ = true;                                     // last: a step that sees it sees the rest
    return true;
}

uint32_t PollTask::step()
{
    if (!armed_) {
        return POLL_IDLE_MS;
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
    // what the unlocked look above saw may be stale: the menu froze the machine while this task waited for the lock, or the library went.
    // The registry moves its entries when a contributor is dropped, so the name is looked up again whenever its generation changed.
    if (armed_ && (!op_ || gen_ != reg_->generation())) {
        op_ = reg_->find_op(name_);
        gen_ = reg_->generation();
    }
    const OpDef *op = armed_ ? op_ : 0;
    if (!op || !poll_allowed(c_, when_frozen_)) {
        lock_->unlock();
        return period_;
    }
    bool known = reg_->bind(op, 0, 0, &none, r.msg, sizeof(r.msg)) == OPE_OK;
    if (known) {
        reg_->run(c_, op, none, &r);
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

// ---- the polls of the libraries ---------------------------------------------------------------------------------------------------

LibPolls::LibPolls(OsPort *os, CoreLock *lock, const OpRegistry *reg, CoreCtx *c, uint32_t stack_words)
    : os_(os), lock_(lock), reg_(reg), c_(c), stack_words_(stack_words)
{
    memset(slot_, 0, sizeof(slot_));
}

LibPolls::~LibPolls()
{
    for (int i = 0; i < LIB_POLLS_MAX; i++) {
        if (slot_[i].t && !slot_[i].started) {             // a task that runs keeps its slot: on the board this object is never destroyed
            delete slot_[i].t;
        }
    }
}

bool LibPolls::add(const char *owner, const char *op, uint16_t period_ms, bool when_frozen, char *why, int whylen)
{
    if (!owner || !op || !op[0]) {
        snprintf(why, whylen, "a poll with no owner or no operation");
        return false;
    }
    if (period_ms < 1) {
        snprintf(why, whylen, "poll \"%s\" has no period", op);
        return false;
    }
    if (strlen(op) >= POLL_OP_MAX || strlen(owner) >= LIB_POLL_OWNER_MAX) {
        snprintf(why, whylen, "poll \"%s\": the name is too long", op);
        return false;
    }
    const OpDef *o = reg_->find_op(op);
    if (!o) {
        snprintf(why, whylen, "poll operation \"%s\" is not registered", op);
        return false;
    }
    if (o->nargs != 0) {
        snprintf(why, whylen, "poll operation \"%s\" takes arguments; a poll takes none", op);
        return false;
    }
    int k = -1;                                            // the same owner again: its poll is replaced, in its own slot, and only once the new one is secured
    for (int i = 0; i < LIB_POLLS_MAX && k < 0; i++) {
        if (slot_[i].used && !strcmp(slot_[i].owner, owner)) {
            k = i;
        }
    }
    for (int i = 0; i < LIB_POLLS_MAX && k < 0; i++) {
        if (!slot_[i].used) {
            k = i;
        }
    }
    if (k < 0) {
        snprintf(why, whylen, "poll \"%s\" has no room (%d polls at most)", op, LIB_POLLS_MAX);
        return false;
    }
    Slot &s = slot_[k];
    if (!s.t) {
        s.t = new PollTask(os_, lock_, reg_, c_);
    }
    if (stack_words_ && !s.started) {
        if (!s.t->start("Lib poll", 1, stack_words_)) {
            snprintf(why, whylen, "poll \"%s\": the task did not start", op);
            return false;
        }
        s.started = true;
    }
    s.t->disarm();
    s.t->arm(op, period_ms, when_frozen);                  // the library's declaration: true keeps it running while the menu has the machine frozen
    snprintf(s.owner, sizeof(s.owner), "%s", owner);
    s.used = true;
    return true;
}

void LibPolls::drop(const char *owner)
{
    for (int i = 0; i < LIB_POLLS_MAX; i++) {
        if (slot_[i].used && !strcmp(slot_[i].owner, owner)) {
            slot_[i].t->disarm();
            slot_[i].used = false;
            slot_[i].owner[0] = 0;
        }
    }
}

int LibPolls::count() const
{
    int n = 0;
    for (int i = 0; i < LIB_POLLS_MAX; i++) {
        n += slot_[i].used;
    }
    return n;
}
