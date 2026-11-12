#include "fmtconfirm.h"
#include <string.h>
#include <strings.h>

static bool expired(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

void fmtc_open(FmtConfirm *c, const char *medium, uint32_t now)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->name, medium, FMTC_NAME_MAX - 1);
    c->state = FMTC_TYPING;
    c->t_key = now;
}

static FmtcEvent cancel(FmtConfirm *c)
{
    c->state = FMTC_CANCELLED;
    return FMTC_ABORT;
}

FmtcEvent fmtc_key(FmtConfirm *c, FmtcKey k, int ch, uint32_t now)
{
    if (c->state != FMTC_TYPING && c->state != FMTC_ARMED) {
        return FMTC_NOTHING;
    }
    c->t_key = now;
#ifdef FMT_NO_CONFIRM
    c->state = FMTC_STARTED;                        // bench control only (-DFMT_NO_CONFIRM): no name, no second RETURN
    return FMTC_GO;
#endif
    bool repeat = k == FMTK_RETURN && c->have_ret && !expired(now, c->t_ret, FMTC_REPEAT_MS);
    if (k == FMTK_RETURN) {
        c->t_ret = now;
        c->have_ret = true;
    }
    if (c->state == FMTC_ARMED) {
        if (k != FMTK_RETURN) {
            return cancel(c);                       // any other key
        }
        if (repeat || !expired(now, c->t_arm, FMTC_MIN_GAP_MS)) {
            return FMTC_NOTHING;                    // the held key, or the same press arriving again
        }
        if (expired(now, c->t_arm, FMTC_ARM_MS)) {
            return cancel(c);
        }
        c->state = FMTC_STARTED;
        return FMTC_GO;
    }
    // TYPING
    c->msg = 0;
    switch (k) {
    case FMTK_ESCAPE:
        return cancel(c);
    case FMTK_CHAR:
        if (ch >= 0x20 && ch < 0x7F && c->n < FMTC_NAME_MAX - 1) {
            c->typed[c->n++] = (char)ch;
            c->typed[c->n] = 0;
        }
        return FMTC_REDRAW;
    case FMTK_BACKSPACE:
        if (c->n > 0) {
            c->typed[--c->n] = 0;
        }
        return FMTC_REDRAW;
    case FMTK_RETURN:
        if (repeat) {
            return FMTC_NOTHING;
        }
        if (c->n == (int)strlen(c->name) && !strcasecmp(c->typed, c->name)) {
            c->state = FMTC_ARMED;
            c->t_arm = now;
            return FMTC_REDRAW;
        }
        c->msg = c->n ? "That is not the name of the medium. Nothing was written." : "Type the name of the medium first.";
        return FMTC_REDRAW;
    default:
        return FMTC_NOTHING;                        // a key that is not text: ignored while typing
    }
}

FmtcEvent fmtc_tick(FmtConfirm *c, uint32_t now, bool medium_present)
{
    if (c->state != FMTC_TYPING && c->state != FMTC_ARMED) {
        return FMTC_NOTHING;
    }
    if (!medium_present) {
        return cancel(c);
    }
    if (c->state == FMTC_ARMED) {
        if (expired(now, c->t_arm, FMTC_ARM_MS)) {
            return cancel(c);
        }
        return FMTC_REDRAW;                         // the seconds on the page; the caller redraws only when the number changed
    }
    if (expired(now, c->t_key, FMTC_IDLE_MS)) {
        return cancel(c);
    }
    return FMTC_NOTHING;
}

int fmtc_arm_left(const FmtConfirm *c, uint32_t now)
{
    if (c->state != FMTC_ARMED) {
        return 0;
    }
    uint32_t el = (uint32_t)(now - c->t_arm);
    return el >= FMTC_ARM_MS ? 0 : (int)((FMTC_ARM_MS - el + 999) / 1000);
}
