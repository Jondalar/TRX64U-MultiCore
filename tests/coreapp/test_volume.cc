// T160 S2 / T164 WP11 / WP18: the "Volume" setting and the menu's SHIFT+ / SHIFT- keys. Every core gets the one generic item (apply = VOLUME_OP) from the application, the
// A1200 included (its own VOL_CFG register and gain stage are gone, WP18): a step reaches CoreIo::master_volume with the byte of the table, the .cfg keeps the value
// ("Volume=110%") and a new session writes it again; the ends clamp; the keys of both keyboards (the C64's '{' and '}', a USB keyboard's '+' and '_', the bare '-')
// work on every page (the bare '+' and '-' not in the F3 Library, where they page: T180 D7); a core with own_volume gets no item and the keys do nothing.
#include <string>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "menuctl.h"
#include "regset.h"
#include "keyboard.h"
#include "volume.h"

#ifdef TEST_APP_NES                      // the NES's app (run.sh nesapp): the cases on its core; the shared unit keeps the table and the Amiga's
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#endif

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *, int) { return false; }
};

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st;
    explicit Rig(const CoreDesc *d, FakeFs *shared = 0)
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/vol.cfg", 0);
        st->load();
        c.settings = st;
    }
};

static int last_write(FakeIo &io, uint32_t reg)
{
    int v = -1;
    for (auto &w : io.log) if (w.reg == reg) v = w.v;
    return v;
}
static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, key) == 0;
    return n;
}
static int lastv(FakeIo &io) { return io.vol_log.empty() ? -1 : io.vol_log.back().v; }   // the host register's last byte, -1: never written
int main()
{
    Host host;
    // no descriptor declares the item, no core has a register for it: the application adds it
#ifndef TEST_APP_NES
    CHECK_EQ(count_key(amiga_desc, VOLUME_KEY), 0);
#else
    CHECK_EQ(count_key(nes_desc, VOLUME_KEY), 0);
#endif

    // ---- T164 WP11: the host's master volume for every core. The application adds "Volume" (apply = VOLUME_OP) to a core that has none; the step reaches
    // CoreIo::master_volume with the byte of the table; a core with own_volume gets none.
    static CoreDesc gd, ad, od;
    static Setting gs[SETTINGS_MAX], as[SETTINGS_MAX], os[SETTINGS_MAX];
#ifndef TEST_APP_NES
    CHECK_EQ(volume_byte(0), 0);
    CHECK_EQ(volume_byte(VOLUME_DEFAULT), 128);                       // 100 % is unity
    CHECK_EQ(volume_byte(VOLUME_STEPS - 1), 255);                     // 200 % is the top, saturating
    CHECK_EQ(volume_byte(1), 6);
    CHECK_EQ(volume_byte(22), 141);                                   // 110 %
    CHECK(!volume_extend(&amiga_desc, &ad, as));                  // the Amiga gets the same item as every core (the A1200: a12_base)
    CHECK_EQ(ad.nsettings, amiga_desc.nsettings + 1);
    CHECK_EQ(count_key(ad, VOLUME_KEY), 1);
    {   const Setting &av = ad.settings[ad.nsettings - 1];
        CHECK(volume_is_generic(av) && !av.bind);
    }
#else
    CHECK(!volume_extend(&nes_desc, &gd, gs));
    CHECK_EQ(gd.nsettings, nes_desc.nsettings + 1);
    CHECK_EQ(count_key(gd, VOLUME_KEY), 1);
    {   // a core that declares a "Volume" item itself (a register of its own) keeps it: no second one (the guard in volume_extend; no shipped core uses it since WP18)
        static const char *const two[] = { "Low", "High" };
        static Setting ds[SETTINGS_MAX], xs[SETTINGS_MAX];
        static CoreDesc dd, dx;
        for (int i = 0; i < nes_desc.nsettings; i++) ds[i] = nes_desc.settings[i];
        ds[nes_desc.nsettings] = Setting{ VOLUME_KEY, two, 2, 0, WHEN_LIVE, 0, 0 };
        dd = nes_desc; dd.settings = ds; dd.nsettings = nes_desc.nsettings + 1;
        CHECK(!volume_extend(&dd, &dx, xs));
        CHECK_EQ(dx.nsettings, dd.nsettings);
        CHECK(dx.settings == dd.settings);
        CHECK_EQ(count_key(dx, VOLUME_KEY), 1);
    }
    CoreDesc own = nes_desc;
    own.own_volume = true;                                            // the C64's mixer (P4)
    CHECK(!volume_extend(&own, &od, os));
    CHECK_EQ(od.nsettings, nes_desc.nsettings);
    {
        Rig g(&gd);
        int gi = g.st->find(VOLUME_KEY);
        CHECK(gi >= 0);
        if (gi < 0) DONE("test_volume");                              // the item was not added: nothing below can run
        const Setting &gsv = g.st->item(gi);
        CHECK(volume_is_generic(gsv) && !gsv.bind);
        CHECK_EQ(gsv.when, WHEN_LIVE);
        CHECK_EQ(gsv.nvalues, VOLUME_STEPS);
        CHECK_EQ(g.st->value(gi), VOLUME_DEFAULT);
        MenuCtl gm(&g.c, &host);
        gm.open();
        // three up, one down from the first page, then the same on every page the rail has: the byte reaches the host's register, the toast shows
        gm.key('{'); gm.key('{'); gm.key('{'); gm.key('}');
        CHECK_STR(gsv.values[g.st->value(gi)], "110%");
        CHECK(!g.io.vol_log.empty() && g.io.vol_log.back().v == 141);
        CHECK(g.fs.text("/sd/cores/vol.cfg").find("Volume=110%") != std::string::npos);
        CHECK(std::string(gm.toast_text()).find("110%") != std::string::npos);
        // a new session writes the stored volume at its start
        Rig g2(&gd, &g.fs);
        g.io.vol_log.clear();
        regset_apply(&g2.c, true);
        CHECK(!g2.io.vol_log.empty() && g2.io.vol_log.back().v == 141);
        // the USB keys, the ends, no write at the end
        g.io.vol_log.clear();
        gm.key('+'); CHECK_EQ(lastv(g.io), -1);                       // this core opens on its F3 Library: there the bare '+' pages (T180 D7, tests/coreapp/test_f3.cc)
        gm.key(KEY_F1);                                               // on every other page the bare keys are the volume
        gm.key('+'); CHECK_EQ(lastv(g.io), 147);
        gm.key('_'); CHECK_EQ(lastv(g.io), 141);
        for (int i = 0; i < 40; i++) gm.key('}');
        CHECK_EQ(lastv(g.io), 0);
        CHECK(std::string(gm.toast_text()).find("silent") != std::string::npos);
        size_t nv = g.io.vol_log.size();
        gm.key('}');
        CHECK_EQ(g.io.vol_log.size(), nv);
        for (int i = 0; i < 40; i++) gm.key('{');
        CHECK_EQ(lastv(g.io), 255);
        CHECK(std::string(gm.toast_text()).find("maximum") != std::string::npos);
        // the feedback is visible on every page, not only on F5 / Settings: after each page key a step shows its line
        const int fkeys[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };
        int pages = 0;
        for (int k : fkeys) {
            gm.key(k);
            g.io.t += 5000;                                           // the earlier line has run out
            gm.toast_expire();
            CHECK_STR(gm.toast_text(), "");
            gm.key('}');
            CHECK(std::string(gm.toast_text()).find("Volume ") == 0);
            pages++;
        }
        CHECK(pages == 8);
        // the line runs out by itself
        g.io.t += 1999;
        CHECK(!gm.toast_expire());
        CHECK(std::string(gm.toast_text()).find("Volume ") == 0);
        g.io.t += 2;
        CHECK(gm.toast_expire());
        CHECK_STR(gm.toast_text(), "");
        // closing the menu takes it away at once
        gm.key('{');
        CHECK(gm.toast_text()[0] != 0);
        gm.toast_clear();
        CHECK_STR(gm.toast_text(), "");
    }
    {
        Rig o(&od);                                                   // a core with its own mixer: the keys do nothing, no register, no line
        MenuCtl om(&o.c, &host);
        om.open();
        om.key('{'); om.key('}'); om.key('+');
        CHECK(o.io.vol_log.empty());
        CHECK_STR(om.toast_text(), "");
    }
#endif
#ifndef TEST_APP_NES
    {
        Rig a1(&ad);                                                  // the A1200: the host's register, no register of its own (WP18)
        MenuCtl m1(&a1.c, &host);
        m1.open();
        m1.key('{');
        CHECK(!a1.io.vol_log.empty() && a1.io.vol_log.back().v == volume_byte(VOLUME_DEFAULT + 1));
        CHECK(std::string(m1.toast_text()).find("105%") != std::string::npos);
        for (auto &w : a1.io.log) CHECK(w.reg != 0x0A9);              // the old VOL_CFG address is never written
    }
#endif
    (void)gd; (void)ad; (void)od; (void)gs; (void)as; (void)os;
    DONE("test_volume");
}
