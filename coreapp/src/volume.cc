#include "volume.h"
#include "ops.h"
#include "ops_glue.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const g_names[VOLUME_STEPS] = {
    "0%", "5%", "10%", "15%", "20%", "25%", "30%", "35%", "40%", "45%", "50%", "55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100%",
    "105%", "110%", "115%", "120%", "125%", "130%", "135%", "140%", "145%", "150%", "155%", "160%", "165%", "170%", "175%", "180%", "185%", "190%", "195%", "200%" };

uint8_t volume_byte(int step)
{
    if (step < 0) step = 0;
    if (step > VOLUME_STEPS - 1) step = VOLUME_STEPS - 1;
    int v = (step * 5 * 128 + 50) / 100;                   // round(percent * 1.28)
    return (uint8_t)(v > 255 ? 255 : v);
}

const char *volume_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    if (d->own_volume) {
        return 0;
    }
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, VOLUME_KEY)) {
            return 0;                                      // the core declares its own Volume item (a register of its own)
        }
    }
    if (d->nsettings + 1 > SETTINGS_MAX) {
        return "settings: no room for the Volume item the application adds";
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    Setting &p = store[d->nsettings];
    p = Setting();
    p.key = VOLUME_KEY;
    p.values = g_names;
    p.nvalues = VOLUME_STEPS;
    p.def = VOLUME_DEFAULT;
    p.when = WHEN_LIVE;
    p.apply = VOLUME_OP;
    out->settings = store;
    out->nsettings = d->nsettings + 1;
    return 0;
}

bool volume_is_generic(const Setting &s)
{
    return s.apply && !strcmp(s.apply, VOLUME_OP);
}

static int op_volume(CoreCtx *c, const OpArgs &a, OpOut &)
{
    c->io->master_volume(volume_byte(a.i(0)));
    return OPE_OK;
}
static const ArgSpec volume_args[] = { ARG_INT("value", 0, VOLUME_STEPS - 1) };

// T160 / T192.3 leak 7: one step of the core's "Volume" setting (VOLUME_KEY with a register binding, or the application's own item), clamped at both ends. The key table
// (menukeytab.cc: '{' '+' up, '}' '_' '-' down) calls it with the step; the answer's text is the line the menu shows ("Volume 105%"). A core that has no such setting answers
// "unsupported" and the key goes on to the next row (a node's own letter may have '+').
static int op_change(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (!c->desc || !c->settings) {
        return OPE_UNSUPPORTED;
    }
    int i = c->settings->find(VOLUME_KEY);
    if (i < 0 || !(c->desc->settings[i].bind || volume_is_generic(c->desc->settings[i]))) {
        return OPE_UNSUPPORTED;
    }
    const Setting &s = c->desc->settings[i];
    int v = c->settings->value(i) + a.i(0);
    if (v < 0) v = 0;
    if (v > s.nvalues - 1) v = s.nvalues - 1;
    if (v != c->settings->value(i)) {
        OpIn in[2] = { in_str("key", s.key), in_str("value", s.values[v]) };
        OpResult r;
        if (ops_call(c, "settings.set", in, 2, &r) != 0) {
            snprintf(o.msg, o.msglen, "%s", r.msg);
            return OPE_FAILED;
        }
    }
    snprintf(o.msg, o.msglen, "Volume %s%s", s.values[v], v == 0 ? " (silent)" : v == s.nvalues - 1 ? " (maximum)" : "");
    return OPE_OK;
}
static const ArgSpec change_args[] = { ARG_INT("value", -2, 2) };

void volume_register(OpRegistry &r)
{
    r.add_op(VOLUME_OP, volume_args, 1, op_volume);
    r.add_op("volume.change", change_args, 1, op_change);
}
