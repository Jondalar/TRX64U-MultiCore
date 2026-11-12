#include "freeze.h"
#include <stdio.h>

uint8_t freeze_status(CoreIo *io, const FreezeDesc &d)
{
    if (d.word) {                                      // byte 0 of the word latches it; the byte wanted comes from that latch
        (void)io->rd(d.reg_status & ~3u);
    }
    return io->rd(d.reg_status);
}

bool Freeze::enter()
{
    if (frozen_) {
        return true;
    }
    ops_->stop(true);
    uint32_t t0 = ops_->now_ms();
    bool seen = !d_.mask;
    while (d_.mask && !(seen = (ops_->status() & d_.mask) != 0)) {      // mask 0: a core with no machine, nothing to wait for
        if ((uint32_t)(ops_->now_ms() - t0) > d_.timeout_ms) {
            char m[100];
            snprintf(m, sizeof(m), "[core] Freeze: no PAUSED after %u ms (status %02x), the menu opens anyway", d_.timeout_ms, ops_->status());
            ops_->log(m);
            break;
        }
        ops_->delay_ms(1);
    }
    ops_->layer_to_menu();
    frozen_ = true;
    return seen;
}

void Freeze::leave(bool keep_stopped)
{
    if (!frozen_) {
        return;
    }
    ops_->layer_to_core();
    // the matrix reaches the core in hardware: the machine must not see the key that closed the menu
    if (!ops_->wait_keys_idle(2000)) {
        ops_->log("[core] Freeze: a key is still down after 2 s, the machine runs anyway");
    }
    ops_->delay_ms(debounce_);
    if (!keep_stopped) {
        ops_->stop(false);
    }
    frozen_ = false;
}
