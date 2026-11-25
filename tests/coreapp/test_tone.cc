// T189 S7: the generic "Tone" setting. Every core gets it after Volume (apply = TONE_OP, group audio, WHEN_LIVE, three values, default Flat); a change reaches
// CoreIo::master_tone with 0 / 1 / 2, the .cfg keeps the value ("Tone=Bass boost") and a new session writes it again (regset_apply); a core with its own Tone item keeps
// it, a core with no_tone gets none, own_volume does not matter (the C64 line gets it, T189 D5).
#include <string>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "menuctl.h"
#include "regset.h"
#include "ops.h"
#include "ops_glue.h"
#include "volume.h"
#include "tone.h"

#ifdef TEST_APP_NES                      // the NES's app (run.sh nesapp): the item on its core; the shared unit keeps the Amiga's
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#endif

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st;
    explicit Rig(const CoreDesc *d, FakeFs *shared = 0)
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/tone.cfg", 0);
        st->load();
        c.settings = st;
    }
};
static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, key) == 0;
    return n;
}
static int lastt(FakeIo &io) { return io.tone_log.empty() ? -1 : io.tone_log.back().v; }   // the host register's last byte, -1: never written
static void set_tone(Rig &r, const char *value)
{
    OpIn in[2] = { in_str("key", TONE_KEY), in_str("value", value) };
    OpResult res;
    CHECK_EQ(ops_call(&r.c, "settings.set", in, 2, &res), 0);
}

int main()
{
#ifndef TEST_APP_NES
    CHECK_EQ(count_key(amiga_desc, TONE_KEY), 0);                    // no descriptor declares it: the application adds it
#else
    CHECK_EQ(count_key(nes_desc, TONE_KEY), 0);                      // no descriptor declares it: the application adds it
    static CoreDesc gd, vd, od, nd, dd, dx;
    static Setting gs[SETTINGS_MAX], vs[SETTINGS_MAX], os[SETTINGS_MAX], ns[SETTINGS_MAX], xs[SETTINGS_MAX];
    CHECK(!tone_extend(&nes_desc, &gd, gs));
    CHECK_EQ(gd.nsettings, nes_desc.nsettings + 1);
    CHECK_EQ(count_key(gd, TONE_KEY), 1);
    menu_place_groups(menu_place_base(), gs, gd.nsettings);          // D32: the group of a base row comes from the base UI TOML, not from tone_extend
    {   const Setting &t = gd.settings[gd.nsettings - 1];
        CHECK(tone_is_generic(t) && !t.bind);
        CHECK_EQ(t.when, WHEN_LIVE);
        CHECK_EQ(t.nvalues, TONE_STEPS);
        CHECK_EQ(t.def, 0);
        CHECK_STR(t.values[0], "Flat");
        CHECK_STR(t.values[1], "Bass boost");
        CHECK_STR(t.values[2], "Treble boost");
        CHECK_STR(t.group, "audio");
    }
    // beside Volume: both items, Tone after it
    CHECK(!volume_extend(&nes_desc, &vd, vs));
    CHECK(!tone_extend(&vd, &vd, vs));
    CHECK_EQ(vd.nsettings, nes_desc.nsettings + 2);
    CHECK(volume_is_generic(vd.settings[vd.nsettings - 2]) && tone_is_generic(vd.settings[vd.nsettings - 1]));
    // own_volume (the C64 line) still gets Tone; no_tone gets none; a core's own "Tone" is kept
    CoreDesc own = nes_desc; own.own_volume = true;
    CHECK(!tone_extend(&own, &od, os));
    CHECK_EQ(od.nsettings, nes_desc.nsettings + 1);
    CoreDesc nt = nes_desc; nt.no_tone = true;
    CHECK(!tone_extend(&nt, &nd, ns));
    CHECK_EQ(nd.nsettings, nes_desc.nsettings);
    {
        static const char *const two[] = { "Low", "High" };
        static Setting ds[SETTINGS_MAX];
        for (int i = 0; i < nes_desc.nsettings; i++) ds[i] = nes_desc.settings[i];
        ds[nes_desc.nsettings] = Setting{ TONE_KEY, two, 2, 0, WHEN_LIVE, 0, 0 };
        dd = nes_desc; dd.settings = ds; dd.nsettings = nes_desc.nsettings + 1;
        CHECK(!tone_extend(&dd, &dx, xs));
        CHECK_EQ(dx.nsettings, dd.nsettings);
        CHECK_EQ(count_key(dx, TONE_KEY), 1);
    }
    {
        Rig g(&gd);
        int gi = g.st->find(TONE_KEY);
        CHECK(gi >= 0);
        if (gi < 0) DONE("test_tone");
        CHECK_EQ(g.st->value(gi), 0);                                // default Flat
        struct Host : MenuHost { void info(int, char *out, int) { out[0] = 0; } bool return_to_host(char *, int) { return false; } } host;
        MenuCtl gm(&g.c, &host);
        gm.open();
        set_tone(g, "Bass boost");
        CHECK_EQ(lastt(g.io), 1);
        CHECK(g.fs.text("/sd/cores/tone.cfg").find("Tone=Bass boost") != std::string::npos);
        set_tone(g, "Treble boost");
        CHECK_EQ(lastt(g.io), 2);
        // a new session writes the stored value at its start
        Rig g2(&gd, &g.fs);
        CHECK_EQ(g2.st->value(g2.st->find(TONE_KEY)), 2);
        regset_apply(&g2.c, true);
        CHECK_EQ(lastt(g2.io), 2);
        set_tone(g2, "Flat");
        CHECK_EQ(lastt(g2.io), 0);
    }
#endif
    DONE("test_tone");
}
