// T190 (GitHub #51): the "Mouse Sensitivity" and "Mouse Acceleration" of the Amiga cores. They are the core's own rows (group "input", operations mouse.sens / mouse.accel, cores/amiga.cc);
// the application adds no second pair (T239.3 D25). The operations set the mouse sink's scaling (mousesink.h). The USB driver scales every report with the two values it reads through
// u64_get_usb_hid_config_value (usb_hid.cc process_mouse_report, defined in baseset.cc: the mouse table's scaling first, then baseset_usb_config) and hands the result to the mouse sink; this
// test runs that driver sequence on the real HidMouseInterpreter (host-app/.../hid_decoder.h, its containers stubbed in tests/coreapp/hidstub), into the Amiga's counters.
// tests/coreapp/test_mousescale.py reads usb_hid.cc / baseset.cc for the parts that cannot be built here.
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "mousesink.h"
#include "regset.h"
#include "ops_glue.h"
#include "hid_decoder.h"
#include "baseset.h"

extern "C" int u64_get_usb_hid_config_value(int key, int default_value);

// usb_hid_get_config_value(key, default) as the driver sees it: the application's answer, else the default
static int cfg_value(int key, int def)
{
    return u64_get_usb_hid_config_value(key, def);
}

// the descriptor of a core with the application's own settings added (what run.cc's menu_build does last)
static const CoreDesc *with_base(const CoreDesc *d)
{
    CoreDesc *o = new CoreDesc();
    Setting *store = new Setting[SETTINGS_MAX];
    CHECK(!baseset_extend(d, o, store));
    return o;
}

// The sequence of process_mouse_report for one report (lines "int sensitivity = ..." to the sink call), usb_hid_apply_pointer_sensitivity /
// usb_hid_apply_pointer_acceleration inlined from usb_hid.cc.
struct Driver {
    int sens_setting = -1, rem_x = 0, rem_y = 0, ema = 0, scale = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
    void report(int raw_x, int raw_y, int *ox, int *oy)
    {
        int sensitivity = cfg_value(0x5D, 8);              // CFG_MOUSE_SENSITIVITY
        int acceleration = cfg_value(0x5E, 0);             // CFG_MOUSE_ACCELERATION
        int mx = raw_x, my = raw_y;
        if (sens_setting != sensitivity) { rem_x = rem_y = 0; sens_setting = sensitivity; }
        mx = HidMouseInterpreter::clampDelta(HidMouseInterpreter::scaleFixedWithRemainder(raw_x, HidMouseInterpreter::computeSensitivityScaleFactor(sensitivity), rem_x), 63);
        my = HidMouseInterpreter::clampDelta(HidMouseInterpreter::scaleFixedWithRemainder(raw_y, HidMouseInterpreter::computeSensitivityScaleFactor(sensitivity), rem_y), 63);
        if (acceleration == 1) {                           // MOUSE_ACCELERATION_ADAPTIVE
            ema = HidMouseInterpreter::updateAdaptiveAccelerationEma(ema, raw_x, raw_y);
            scale = HidMouseInterpreter::limitScaleStep(scale, HidMouseInterpreter::computeAdaptiveAccelerationScale(ema), 32);
            mx = HidMouseInterpreter::clampDelta(HidMouseInterpreter::scaleFixed(mx, scale), 63);
            my = HidMouseInterpreter::clampDelta(HidMouseInterpreter::scaleFixed(my, scale), 63);
        } else {
            ema = 0;
            scale = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
        }
        *ox = mx; *oy = my;
        CHECK(mouse_sink_usb_report(mx, my, 0));
    }
};

// What the core's counters received for the reports since the last call.
static int core_dx(AmigaRig &r)
{
    int sx = 0;
    size_t from = 0;
    for (int i = 0; i < 100; i++) {
        r.io.delay_ms(2);
        if (!mouse_sink_pace(&r.c)) break;
    }
    for (size_t i = from; i < r.io.log.size(); i++) if (r.io.log[i].reg == AM_MOUSE_DX) sx += (int8_t)r.io.log[i].v;
    r.io.log.clear();
    return sx;
}

static void set_value(AmigaRig &r, const char *key, int idx)
{
    int i = r.st->find(key);
    CHECK(i >= 0);
    r.st->set(i, idx);
    regset_apply_setting(&r.c, i);                         // what the menu does for a WHEN_LIVE item
}

static int one_report(AmigaRig &r, int dx)
{
    mouse_sink_reset();
    r.io.log.clear();
    Driver d;
    int ox, oy;
    d.report(dx, 0, &ox, &oy);
    CHECK_EQ(core_dx(r), ox);                              // the sink passes on exactly what the driver scaled
    return ox;
}

int main()
{
    const CoreDesc *both[1] = { with_base(&AM_REC) };               // the A1200 is the app of its own (a12app): the same checks run on its record there

    // ---- S1: the rows, in both Amiga cores, the cores' own rows (group "input", operations mouse.sens / mouse.accel), Gideon's range and defaults; the application adds no second pair
    for (int k = 0; k < 1; k++) {
        const CoreDesc *d = both[k];
        const CoreDesc *own = &AM_REC;
        CHECK_EQ(d->nsettings - own->nsettings, 4);        // the application's look rows only (baseset.h): the mouse rows are the core's
        int ns = -1, na = -1, cnt = 0;
        for (int i = 0; i < d->nsettings; i++) {
            if (!strcmp(d->settings[i].key, "Mouse Sensitivity")) { ns = i; cnt++; }
            if (!strcmp(d->settings[i].key, "Mouse Acceleration")) { na = i; cnt++; }
        }
        CHECK_EQ(cnt, 2);
        CHECK(ns >= 0 && na >= 0);
        if (ns < 0 || na < 0) continue;
        const Setting &s = d->settings[ns], &a = d->settings[na];
        CHECK_EQ(s.nvalues, 16);
        CHECK_STR(s.values[0], "1");
        CHECK_STR(s.values[15], "16");
        CHECK_STR(s.values[s.def], "8");                   // default 8 = 1:1
        CHECK_EQ(s.base, 1);
        CHECK_EQ(baseset_kind(s), 0);
        CHECK_STR(s.group, "input");
        CHECK_STR(s.apply, "mouse.sens");
        CHECK_EQ(s.when, WHEN_LIVE);
        CHECK_EQ(a.nvalues, 2);
        CHECK_STR(a.values[0], "Off");
        CHECK_STR(a.values[1], "Adaptive");
        CHECK_EQ(a.def, 0);
        CHECK_EQ(baseset_kind(a), 0);
        CHECK_STR(a.group, "input");
        CHECK_STR(a.apply, "mouse.accel");
        CHECK_EQ(a.when, WHEN_LIVE);
    }

    // ---- S2: a model report of +10 counts arrives as each setting says
    {
        FakeFs fs;
        AmigaRig r(&fs, both[0]);
        baseset_attach(&r.c, 0);
        regset_apply(&r.c, true);                          // the start: the defaults go in
        CHECK_EQ(one_report(r, 10), 10);                   // 8 = 1.0
        set_value(r, "Mouse Sensitivity", 15);             // 16 = 2.0
        CHECK_EQ(one_report(r, 10), 20);
        set_value(r, "Mouse Sensitivity", 3);              // 4 = 0.5
        CHECK_EQ(one_report(r, 10), 5);
        set_value(r, "Mouse Sensitivity", 0);              // 1 = 0.125: 1.25 counts, the remainder is kept
        CHECK_EQ(one_report(r, 10), 1);
        {
            mouse_sink_reset();
            Driver d;
            int ox, oy, sum = 0;
            for (int i = 0; i < 8; i++) { d.report(1, 0, &ox, &oy); sum += ox; }
            CHECK_EQ(sum, 1);                              // 8 slow counts at 0.125 are one count, not lost
        }
        set_value(r, "Mouse Sensitivity", 7);
        // acceleration: Off leaves a fast stream as it is, Adaptive does not
        mouse_sink_reset();
        r.io.log.clear();
        Driver off, on;
        int ox, oy, sum_off = 0, sum_on = 0;
        for (int i = 0; i < 12; i++) { off.report(30, 0, &ox, &oy); sum_off += ox; }
        CHECK_EQ(sum_off, 360);
        set_value(r, "Mouse Acceleration", 1);
        for (int i = 0; i < 12; i++) { on.report(30, 0, &ox, &oy); sum_on += ox; }
        CHECK(sum_on != sum_off);
        set_value(r, "Mouse Acceleration", 0);
        Driver off2;
        sum_off = 0;
        for (int i = 0; i < 12; i++) { off2.report(30, 0, &ox, &oy); sum_off += ox; }
        CHECK_EQ(sum_off, 360);                            // and Off again is Off
    }

    // ---- no context: the driver's own defaults (sensitivity 8, acceleration off)
    {
        baseset_attach(0, 0);
        CHECK_EQ(cfg_value(0x5D, 8), 8);
        CHECK_EQ(cfg_value(0x5E, 0), 0);
        CHECK_EQ(cfg_value(0x55, 5), 5);
    }

    // ---- S2b: both cores answer the driver from their own settings
    for (int k = 0; k < 1; k++) {
        FakeFs fs;
        AmigaRig r(&fs, both[k]);
        baseset_attach(&r.c, 0);
        regset_apply(&r.c, true);
        set_value(r, "Mouse Sensitivity", 11);             // 12 = 1.5
        CHECK_EQ(one_report(r, 10), 15);
    }

    // ---- S3: the restart applies the stored values (a new process: the .cfg read, the start applies every setting)
    for (int k = 0; k < 1; k++) {
        const CoreDesc *d = both[k];
        FakeFs fs;
        {
            Settings s1(d->settings, d->nsettings, d->store, &fs, "/sd/cores/amiga.cfg", 0);
            s1.load();
            CHECK(s1.set(s1.find("Mouse Sensitivity"), 3));       // 4
            CHECK(s1.set(s1.find("Mouse Acceleration"), 1));
        }
        Settings s2(d->settings, d->nsettings, d->store, &fs, "/sd/cores/amiga.cfg", 0);
        s2.load();
        AmigaIo io;
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.settings = &s2;
        baseset_attach(&c, 0);
        regset_apply(&c, true);
        CHECK_EQ(cfg_value(0x5D, 8), 4);
        CHECK_EQ(cfg_value(0x5E, 0), 1);
    }
    baseset_attach(0, 0);
    DONE("mousescale");
}
