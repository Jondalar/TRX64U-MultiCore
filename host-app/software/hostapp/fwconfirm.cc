#include "fwconfirm.h"

void fwc_open(FwConfirm *c, uint32_t now)
{
    c->state = FWC_ASK1;
    c->t_key = now;
    c->t_arm = 0;
}

static FwcEvent cancel(FwConfirm *c)
{
    c->state = FWC_CANCELLED;
    return FWCE_ABORT;
}

FwcEvent fwc_key(FwConfirm *c, FwcKey k, uint32_t now)
{
    if (c->state != FWC_ASK1 && c->state != FWC_ASK2) {
        return FWCE_NOTHING;
    }
    c->t_key = now;
    if (c->state == FWC_ASK1) {
        if (k == FWCK_Y) {
            c->state = FWC_ASK2;
            c->t_arm = now;
            return FWCE_REDRAW;
        }
        return cancel(c);                       // RETURN, ESC, RUN/STOP, anything else: No is the default
    }
    // ASK2
    if (now - c->t_arm < FWC_MIN_GAP_MS) {
        return FWCE_NOTHING;                    // the repeat of the held Y, or a hand that was already on the key
    }
    if (k == FWCK_RETURN) {
        c->state = FWC_STARTED;
        return FWCE_GO;
    }
    return cancel(c);
}

FwcEvent fwc_tick(FwConfirm *c, uint32_t now)
{
    if (c->state == FWC_ASK1 && now - c->t_key >= FWC_IDLE_MS) {
        return cancel(c);
    }
    if (c->state == FWC_ASK2 && now - c->t_arm >= FWC_ARM_MS) {
        return cancel(c);
    }
    return FWCE_NOTHING;
}

int fwc_left(const FwConfirm *c, uint32_t now)
{
    if (c->state != FWC_ASK2) {
        return 0;
    }
    uint32_t el = now - c->t_arm;
    return el >= FWC_ARM_MS ? 0 : (int)((FWC_ARM_MS - el + 999) / 1000);
}
