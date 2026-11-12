#include "regset.h"
#include "ops_glue.h"

uint8_t regset_part(const CoreCtx *c, int i)
{
    const Setting &s = c->desc->settings[i];
    if (!s.bind) {
        return 0;
    }
    int v = c->settings->value(i);
    unsigned b = s.bind->bytes ? s.bind->bytes[v] : (unsigned)v;
    return (uint8_t)((b & s.bind->mask) << s.bind->shift);
}

uint8_t regset_value(const CoreCtx *c, uint16_t reg)
{
    uint8_t out = 0;
    for (int i = 0; i < c->desc->nsettings; i++) {
        const Setting &s = c->desc->settings[i];
        if (s.bind && s.bind->reg == reg) {
            out |= regset_part(c, i);
        }
    }
    return out;
}

void regset_commit(CoreCtx *c, uint16_t reg)
{
    c->io->wr(reg, regset_value(c, reg));
}

void regset_apply_setting(CoreCtx *c, int i)
{
    const Setting &s = c->desc->settings[i];
    if (s.kind == 3) {                                  // a selected file: `apply` took it when it was chosen, the core reads the path where it needs it
        return;
    }
    if (s.apply) {
        ops_set(c, s.apply, c->settings->value(i) + s.base);
    } else if (s.bind) {
        regset_commit(c, s.bind->reg);
    }
}

void regset_apply(CoreCtx *c, bool all)
{
    const CoreDesc *d = c->desc;
    uint16_t done[SETTINGS_MAX];
    int ndone = 0;
    for (int i = 0; i < d->nsettings; i++) {
        const Setting &s = d->settings[i];
        if (!all && s.when != WHEN_RESET) {
            continue;
        }
        if (s.bind) {
            bool seen = false;
            for (int k = 0; k < ndone; k++) {
                seen |= done[k] == s.bind->reg;
            }
            if (seen) {
                continue;
            }
            if (ndone < SETTINGS_MAX) {
                done[ndone++] = s.bind->reg;
            }
        }
        regset_apply_setting(c, i);
    }
    for (int i = 0; i < d->nreset_writes; i++) {
        c->io->wr(d->reset_writes[i].reg, d->reset_writes[i].value);
    }
}
