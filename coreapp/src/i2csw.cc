#include "i2csw.h"
#include "ops.h"
#include "picture.h"
#include "settings.h"
#include <string.h>
#include <strings.h>

#ifndef VIDEOMODE_NO_BOARD                     // the native builds (tests, menusim) define it: no I2C driver there
#include "i2c_drv.h"

// On: T242 D1. Off: as before T242, one command parks the scanner and the application keeps the buses (host/app_i2c.v: want, kb_pause, own); with
// `scanning` false the unlocks hand nothing back. Both inside the lock: a task in the middle of a two-transaction read (the EDID) must not lose the buses
// between the two.
static void board_scan(bool on)
{
    if (!i2c || !i2c->i2c_lock("I2C switches")) {
        return;
    }
    if (on) {
        i2c->enable_scan(true, false);
    } else {
        i2c->enable_scan(false, false);
        i2c->set_channel(I2C_CHANNEL_1V8);
        i2c->i2c_probe(0x40);                  // the first command claims the buses; 0x40 is the port expander the init writes
    }
    i2c->i2c_unlock();
}

static void board_park_wait(int ticks)
{
    if (i2c) {
        i2c->set_park_wait(ticks);
    }
}

// Before T242 the init wrote the first three (T242 D5 took them out); PR #112 set both ports to input before it enabled the scanner. On the 1.8 V bus, as
// both did.
static void board_expander(int which)
{
    if (!i2c || !which || !i2c->i2c_lock("I2C switches")) {
        return;
    }
    i2c->set_channel(I2C_CHANNEL_1V8);
    if (which == 1) {
        i2c->i2c_write_byte(0x42, 0x06, 0x00);
        i2c->i2c_write_byte(0x42, 0x07, 0xFF);
        i2c->i2c_write_byte(0x42, 0x02, 0x00);
    } else {
        i2c->i2c_write_byte(0x42, 0x06, 0xFF);
        i2c->i2c_write_byte(0x42, 0x07, 0xFF);
    }
    i2c->i2c_unlock();
}

I2cSwHw i2csw_hw = { board_scan, board_park_wait, board_expander };
#else
static void no_scan(bool) { }
static void no_park_wait(int) { }
static void no_expander(int) { }
I2cSwHw i2csw_hw = { no_scan, no_park_wait, no_expander };
#endif

static const char *const g_onoff[2] = { "Off", "On" };
static const char *const g_wait[2] = { "None", "2 ticks" };
static const char *const g_exp[3] = { "None", "Before T242", "PR 112" };

struct I2cSwRow {
    const char *key;
    const char *const *values;
    int n;
    int def;                                   // T242's behaviour
    const char *op;
};
static const I2cSwRow g_rows[I2CSW_N] = {
    { I2CSW_KEY_SCAN, g_onoff, 2, 1, I2CSW_OP_SCAN },
    { I2CSW_KEY_POLL, g_onoff, 2, 1, I2CSW_OP_POLL },
    { I2CSW_KEY_WAIT, g_wait, 2, 0, I2CSW_OP_WAIT },
    { I2CSW_KEY_EXP, g_exp, 3, 0, I2CSW_OP_EXP },
};

const char *i2csw_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    if (!desc_has_capability(d, I2CSW_CAP)) {
        return 0;
    }
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, I2CSW_KEY_SCAN)) {
            return 0;                          // added already
        }
    }
    if (d->nsettings + I2CSW_N > SETTINGS_MAX) {
        return "settings: no room for the I2C switches the application adds";
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    for (int k = 0; k < I2CSW_N; k++) {
        Setting &p = store[d->nsettings + k];
        p = Setting();
        p.key = g_rows[k].key;
        p.values = g_rows[k].values;
        p.nvalues = g_rows[k].n;
        p.def = g_rows[k].def;
        p.when = WHEN_LIVE;
        p.apply = g_rows[k].op;
    }
    out->settings = store;
    out->nsettings = d->nsettings + I2CSW_N;
    return 0;
}

static int op_scan(CoreCtx *, const OpArgs &a, OpOut &)
{
    i2csw_hw.scan(a.i(0) != 0);
    return OPE_OK;
}

static int op_poll(CoreCtx *, const OpArgs &a, OpOut &)
{
    picture_set_poll(a.i(0) != 0);
    return OPE_OK;
}

static int op_wait(CoreCtx *, const OpArgs &a, OpOut &)
{
    i2csw_hw.park_wait(a.i(0) ? 2 : 0);
    return OPE_OK;
}

static int op_expander(CoreCtx *, const OpArgs &a, OpOut &)
{
    i2csw_hw.expander(a.i(0));
    return OPE_OK;
}

static const ArgSpec two_args[] = { ARG_INT("value", 0, 1) };
static const ArgSpec three_args[] = { ARG_INT("value", 0, 2) };

void i2csw_register(OpRegistry &r)
{
    r.contributor("i2csw");
    r.add_op(I2CSW_OP_SCAN, two_args, 1, op_scan);
    r.add_op(I2CSW_OP_POLL, two_args, 1, op_poll);
    r.add_op(I2CSW_OP_WAIT, two_args, 1, op_wait);
    r.add_op(I2CSW_OP_EXP, three_args, 1, op_expander);
}
