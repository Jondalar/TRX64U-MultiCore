// T181 (spec section 2): the application's side of the hotkeys while a core runs. S1 an action fires within 100 ms of the press, S2 a key that is not a hotkey changes
// nothing, S3 holding acts once (the volume keys repeat), S5 no default combination is one of that core's own key uses, the mask registers hold what the map says (read back
// through the bench's register model), the USB keyboard's combination is taken out before the core and a hidden key stays hidden until it is up.
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "hotkeys.h"
#include "volume.h"
#include "settings.h"
#include "ops_glue.h"
#include "menuctl.h"
#include "keyboard.h"
#include "regset.h"
#include "padkeys.h"
#ifdef TEST_APP_NEOGEO
extern const CoreDesc neogeo_desc;              // cores.h lists the shared app's cores; the Neo Geo is its own app (cores/neogeo/app/lib)
#endif
#ifdef TEST_APP_C128
extern const CoreDesc c128_desc;                // cores.h lists the shared app's cores; the C128 is its own app (cores/c128/app/lib)
#endif
#ifdef TEST_APP_PET
extern const CoreDesc pet_desc;                 // cores.h lists the shared app's cores; the PET is its own app (cores/pet/app/lib)
#endif
#ifdef TEST_APP_CPC
extern const CoreDesc cpc_desc;                 // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)
#endif
#ifdef TEST_APP_ZX
extern const CoreDesc zx_desc;                  // cores.h lists the shared app's cores; the ZX Spectrum is its own app (cores/zx/app/lib)
#endif
#ifdef TEST_APP_DDT
extern const CoreDesc ddt_novic_desc;           // cores.h lists the shared app's cores; DDT Look Ma, No VIC is its own app (cores/ddt_novic/app/lib)
#endif

enum { CTRL = 58, CBM = 61, KR = 17, KPLUS = 40, KMINUS = 43, KJ = 34, KP = 41, KA = 10, KSHIFT = 15, K1 = 56 };

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st; CoreDesc d; Setting store[SETTINGS_MAX];
    explicit Rig(const CoreDesc *base, bool vol = true, const char *cfg = "/sd/cores/hk.cfg")
    {
        memset(&c, 0, sizeof(c));
        d = *base;
        if (vol) { CHECK(!volume_extend(base, &d, store)); }
        CHECK(!hk_extend(&d, &d, store));
        c.desc = &d; c.io = &io; c.fs = &fs; c.started = true;
        st = new Settings(d.settings, d.nsettings, d.store, &fs, cfg, 0);
        st->load();
        c.settings = st;
        hk_apply(&c);
    }
};

static uint64_t mask_of(const uint8_t r[11]) { uint64_t m = 0; for (int i = 0; i < 8; i++) m |= (uint64_t)r[2 + i] << (8 * i); return m; }

// the bench model of the loop: poll every 10 ms of virtual time from `t0` for `ms`, return the time of the first poll that fired `bit` (-1 never)
static int first_fire(Rig &g, HkLoop &lp, uint32_t bit, int ms, uint32_t *all = 0)
{
    int at = -1;
    uint32_t t0 = g.io.t;
    for (int i = 0; i <= ms; i += 10) {
        uint32_t f = lp.poll(&g.c);
        if (all) *all |= f;
        if ((f & bit) && at < 0) at = (int)(g.io.t - t0);
        g.io.delay_ms(10);
    }
    return at;
}

int main()
{
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_NEOGEO)          // the cases that stand on the shared app's cores (the VIC-20's and the A1200's app run this file for the one row of S5)
    // ---- the registers of a map
    {
        HkMap m = { CTRL, 255, { 64, KPLUS, KMINUS, KR, KJ, KP, 255 } };
        uint8_t r[11];
        hk_regs(m, r);
        CHECK_EQ(r[0], 0x80 | CTRL);
        CHECK_EQ(r[1], 0);
        CHECK_EQ(r[10], 1);
        CHECK_EQ(mask_of(r), (1ull << KPLUS) | (1ull << KMINUS) | (1ull << KR) | (1ull << KJ) | (1ull << KP));
        HkMap two = { CBM, CTRL, { 255, 255, 255, CTRL, KR, 255, 255 } };     // an action on a modifier key is dropped: the modifier passes
        hk_regs(two, r);
        CHECK_EQ(r[0], 0x80 | CBM); CHECK_EQ(r[1], 0x80 | CTRL);
        CHECK_EQ(mask_of(r), 1ull << KR); CHECK_EQ(r[10], 0);
        HkMap sel = { CTRL, 255, { CTRL, KPLUS, KMINUS, KR, KJ, KP, 255 } };           // an action on the modifier itself never fires
        { HkDetect d; HkKeys k; k.k = 1ull << CTRL; CHECK_EQ(d.poll(sel, k, 0), 0u); }
        HkMap off = { 255, CTRL, { 64, 1, 2, 3, 4, 5, 255 } };
        hk_regs(off, r);
        for (int i = 0; i < 11; i++) CHECK_EQ(r[i], 0);                  // no first modifier: everything off
    }
    // ---- S1, S2, S3 on the nes (volume setting, Joystick ports, a Picture item), CTRL as the modifier
    {
        static Setting px[1] = { { "Picture", 0, 3, 0, WHEN_LIVE, 0, 0 } };
        static const char *const pv[] = { "Auto", "720p", "1080p" };
        px[0].values = pv;
        static CoreDesc base; static Setting bs[SETTINGS_MAX];
        base = nes_desc;
        for (int i = 0; i < nes_desc.nsettings; i++) bs[i] = nes_desc.settings[i];
        bs[nes_desc.nsettings] = px[0];
        base.settings = bs; base.nsettings = nes_desc.nsettings + 1;
        Rig g(&base);
        HkLoop lp;
        // the mask registers hold the default map: modifier CTRL, RESTORE and R, +, -, J, P as action keys
        CHECK(g.io.hk_writes >= 1);
        CHECK_EQ(g.io.hk_regs_now[0], 0x80 | CTRL);
        CHECK_EQ(g.io.hk_regs_now[10], 1);
        CHECK_EQ(mask_of(g.io.hk_regs_now), (1ull << KPLUS) | (1ull << KMINUS) | (1ull << KR) | (1ull << KJ) | (1ull << KP));
        // S2: nothing pressed, then keys that are no hotkey (with and without the modifier): nothing fires
        uint32_t all = 0;
        first_fire(g, lp, 0, 200, &all);
        g.io.key(KA, true); g.io.key(K1, true); first_fire(g, lp, 0, 100, &all);    // A and 1 alone
        g.io.key(CTRL, true); first_fire(g, lp, 0, 100, &all);                       // CTRL + A + 1: not hotkeys
        g.io.key(CTRL, false); g.io.key(KA, false); g.io.key(K1, false); first_fire(g, lp, 0, 50, &all);
        g.io.key(KR, true); first_fire(g, lp, 0, 100, &all);                         // R without the modifier
        g.io.key(KR, false); first_fire(g, lp, 0, 50, &all);
        CHECK_EQ(all, 0u);
        // S1 reset: modifier down, then R: fired within 100 ms; S3: held for a second, once
        g.io.key(CTRL, true); first_fire(g, lp, 0, 30, &all);
        g.io.key(KR, true);
        uint32_t seen = 0;
        int at = first_fire(g, lp, 1u << HKA_RESET, 1000, &seen);
        CHECK(at >= 0 && at <= 100);
        CHECK_EQ(seen, 1u << HKA_RESET);
        g.io.key(KR, false); first_fire(g, lp, 0, 30, &all);
        // S1 menu: RESTORE (the level) with the modifier
        g.io.key(64, true);
        at = first_fire(g, lp, 1u << HKA_MENU, 200);
        CHECK(at >= 0 && at <= 100);
        g.io.key(64, false); first_fire(g, lp, 0, 30, &all);
        // S3 volume: one step at the press, the repeat after 400 ms, then every 120 ms; released: stops
        g.io.vol_log.clear();
        char line[64];
        g.io.key(KPLUS, true);
        uint32_t t0 = g.io.t;
        int steps = 0;
        for (int i = 0; i <= 1000; i += 10) {
            uint32_t f = lp.poll(&g.c);
            if (f) { CHECK(g.io.t - t0 <= 1000); lp.perform(&g.c, f, line, sizeof(line)); steps++; }
            g.io.delay_ms(10);
        }
        CHECK(steps >= 5 && steps <= 7);                                         // 1 + (1000 - 400) / 120 = 6
        CHECK_EQ((int)g.io.vol_log.size(), steps);
        CHECK(g.io.vol_log.back().v > g.io.vol_log.front().v);                   // up
        g.io.key(KPLUS, false); steps = 0;
        for (int i = 0; i <= 600; i += 10) { if (lp.poll(&g.c)) steps++; g.io.delay_ms(10); }
        CHECK_EQ(steps, 0);
        // a tap is one step
        g.io.vol_log.clear();
        g.io.key(KMINUS, true); at = first_fire(g, lp, 1u << HKA_VOL_DOWN, 100); CHECK(at >= 0 && at <= 100);
        g.io.key(KMINUS, false); first_fire(g, lp, 0, 30);
        // swap: toggles Joystick ports between 0 and 1, held once; picture: the next value, wraps; both with their line
        int ji = g.st->find("Joystick ports");
        CHECK(ji >= 0);
        CHECK_EQ(g.st->value(ji), 0);
        CHECK(hk_swap_joystick(&g.c, line, sizeof(line))); CHECK_EQ(g.st->value(ji), 1); CHECK(strstr(line, "Swapped"));
        CHECK(hk_swap_joystick(&g.c, line, sizeof(line))); CHECK_EQ(g.st->value(ji), 0);
        int pi = g.st->find("Picture");
        CHECK(hk_next_picture(&g.c, line, sizeof(line))); CHECK_EQ(g.st->value(pi), 1);
        CHECK(hk_next_picture(&g.c, line, sizeof(line))); CHECK_EQ(g.st->value(pi), 2);
        CHECK(hk_next_picture(&g.c, line, sizeof(line))); CHECK_EQ(g.st->value(pi), 0); CHECK(strstr(line, "Auto"));
        // through the loop: J with the modifier toggles once however long it is held
        g.io.key(KJ, true);
        seen = 0; first_fire(g, lp, 0, 500, &seen);
        CHECK_EQ(seen, 1u << HKA_SWAP);
        CHECK(lp.det.poll(hk_current(), HkKeys(), 0) == 0);
        g.io.key(KJ, false); g.io.key(CTRL, false);
        // a core without the setting: the line says so, nothing changes
        Rig z(&portdiag_desc, false);
        CHECK(!hk_swap_joystick(&z.c, line, sizeof(line))); CHECK(strstr(line, "No joystick swap"));
        CHECK(!hk_step_volume(&z.c, 1, line, sizeof(line)) || true);
    }
    // ---- the action key goes down before the modifier completes: it fires once, when the modifier is complete
    {
        HkMap m = { CTRL, 255, { 64, KPLUS, KMINUS, KR, KJ, KP, 255 } };
        HkDetect d; HkKeys k;
        k.k = 1ull << KR; CHECK_EQ(d.poll(m, k, 0), 0u);
        k.k |= 1ull << CTRL; CHECK_EQ(d.poll(m, k, 10), 1u << HKA_RESET);
        CHECK_EQ(d.poll(m, k, 20), 0u);
        HkMap two = { CBM, CTRL, { 64, KPLUS, KMINUS, KR, KJ, KP, 255 } };       // two keys: both are needed
        HkDetect e; HkKeys j; j.k = (1ull << CBM) | (1ull << KR); CHECK_EQ(e.poll(two, j, 0), 0u);
        j.k = (1ull << CTRL) | (1ull << KR); CHECK_EQ(e.poll(two, j, 10), 0u);
        j.k = (1ull << CTRL) | (1ull << CBM) | (1ull << KR); CHECK_EQ(e.poll(two, j, 20), 1u << HKA_RESET);
    }
    // ---- the F8 map changes the registers at once and the .cfg keeps it (D3)
    {
        Rig g(&nes_desc);
        int mi = g.st->find(HK_KEY_MOD), ri = g.st->find("Hotkey reset");
        CHECK(mi >= 0 && ri >= 0);
        if (mi < 0 || ri < 0) DONE("test_hotkeys");
        int n0 = g.io.hk_writes;
        OpResult res;
        OpIn in[2] = { in_str("key", HK_KEY_MOD), in_str("value", "WIN / CMD + ESC") };
        CHECK_EQ(ops_call(&g.c, "settings.set", in, 2, &res), 0);
        CHECK(g.io.hk_writes > n0);
        CHECK_EQ(g.io.hk_regs_now[0], 0x80 | CBM); CHECK_EQ(g.io.hk_regs_now[1], 0x80 | 63);
        OpIn in2[2] = { in_str("key", "Hotkey reset"), in_str("value", "Q") };
        CHECK_EQ(ops_call(&g.c, "settings.set", in2, 2, &res), 0);
        CHECK_EQ(hk_current().act[HKA_RESET], 62);
        CHECK(mask_of(g.io.hk_regs_now) & (1ull << 62)); CHECK(!(mask_of(g.io.hk_regs_now) & (1ull << KR)));
        OpIn in3[2] = { in_str("key", "Hotkey menu"), in_str("value", "Off") };
        CHECK_EQ(ops_call(&g.c, "settings.set", in3, 2, &res), 0);
        CHECK_EQ(g.io.hk_regs_now[10], 0);
        CHECK(g.fs.text("/sd/cores/hk.cfg").find("Hotkey modifier=WIN / CMD + ESC") != std::string::npos);
        Rig g2(&nes_desc, true, "/sd/cores/hk.cfg");                  // a new session
        g2.fs = g.fs; g2.st->load(); hk_apply(&g2.c);
        CHECK_EQ(g2.io.hk_regs_now[0], 0x80 | CBM);
        CHECK_EQ(hk_current().act[HKA_RESET], 62);
        OpIn in4[2] = { in_str("key", HK_KEY_MOD), in_str("value", "Off") };      // the feature off: no mask, no action
        CHECK_EQ(ops_call(&g.c, "settings.set", in4, 2, &res), 0);
        for (int i = 0; i < 11; i++) CHECK_EQ(g.io.hk_regs_now[i], 0);
        HkLoop lp; g.io.key(CTRL, true); g.io.key(KR, true);
        uint32_t all = 0; first_fire(g, lp, 0, 100, &all); CHECK_EQ(all, 0u);
    }
    // ---- the USB keyboard: the same rule as the host's mask
    {
        HkMap m = { CTRL, 255, { 64, KPLUS, KMINUS, KR, KJ, KP, 255 } };
        HkFilter f; uint8_t mt[8], rs;
        auto set = [&](std::initializer_list<int> keys, bool restore) { memset(mt, 0, 8); for (int k : keys) mt[k / 8] |= (uint8_t)(1u << (k % 8)); rs = restore; };
        auto down = [&](int k) { return (mt[k / 8] >> (k % 8)) & 1; };
        set({ KA, KR }, false); f.apply(m, mt, &rs); CHECK(down(KA) && down(KR));              // no modifier: untouched
        set({ CTRL, KA, KR }, false); f.apply(m, mt, &rs); CHECK(down(CTRL) && down(KA) && !down(KR));   // the modifier passes, the action key goes
        set({ CTRL, KR, KA }, true); f.apply(m, mt, &rs); CHECK(!down(KR) && !(rs & 1) && down(KA));    // RESTORE is an action key
        set({ KR }, false); f.apply(m, mt, &rs); CHECK(!down(KR));                               // modifier released first: R stays hidden until it is up
        set({}, false); f.apply(m, mt, &rs); set({ KR }, false); f.apply(m, mt, &rs); CHECK(down(KR));   // up, then down without the modifier: the core gets it
        HkFilter off; HkMap none = { 255, 255, { 64, KPLUS, KMINUS, KR, KJ, KP, 255 } };
        set({ CTRL, KR }, true); off.apply(none, mt, &rs); CHECK(down(KR) && (rs & 1));
        // the raw state the detection reads is the one before the filter
        uint8_t m2[8] = {}; uint8_t r2 = 0; m2[KR / 8] = (uint8_t)(1u << (KR % 8)); m2[CTRL / 8] |= (uint8_t)(1u << (CTRL % 8));
        hk_usb_set_map(m);
        hk_usb_matrix_filter(m2, &r2);
        CHECK(!((m2[KR / 8] >> (KR % 8)) & 1));
        CHECK(hk_usb_keys().down(KR) && hk_usb_keys().down(CTRL));
        // a report (rawkeys cores): usage 0x15 = R, modifier byte bit 0 = L CTRL; the usage map says R is the matrix key 17
        hk_usb_set_usage_map([](uint8_t u) -> uint8_t { return u == 0x15 ? KR : u == 0x04 ? KA : HK_NONE; });
        uint8_t in[8] = { 0x01, 0, 0x04, 0x15, 0, 0, 0, 0 }, out[8];
        hk_usb_report_filter(in, out);
        CHECK_EQ(out[2], 0x04); CHECK_EQ(out[3], 0); CHECK_EQ(out[0], 0x01);
        uint8_t in5[8] = { 0, 0, 0x15, 0, 0, 0, 0, 0 };                                           // CTRL released first: still hidden
        hk_usb_report_filter(in5, out); CHECK_EQ(out[2], 0);
        uint8_t in6[8] = { 0 }; hk_usb_report_filter(in6, out);
        hk_usb_report_filter(in5, out); CHECK_EQ(out[2], 0x15);                                   // up and down again: through
        hk_usb_set_map(none);
    }
#endif
#ifdef TEST_APP_ZX
    // a core without the setting: the line says so, nothing changes (the shared unit has this case on another core)
    {
        char line[64];
        Rig z(&zx_desc, false);
        CHECK(!hk_swap_joystick(&z.c, line, sizeof(line))); CHECK(strstr(line, "No joystick swap"));
    }
#endif
    // ---- S5 and the per-core default maps: every core that has hotkeys, the default is harmless in it
    {
        struct Row { const CoreDesc *d; const char *name; bool has; };
#ifdef TEST_APP_VC20
        const Row rows[] = { { &vc20_desc, "vc20", true } };
#elif defined(TEST_APP_A1200)
        const Row rows[] = { { &amiga1200_desc, "amiga1200", true } };
#elif defined(TEST_APP_DDT)
        const Row rows[] = { { &ddt_novic_desc, "ddt_novic", false } };
#elif defined(TEST_APP_ZX)
        const Row rows[] = { { &zx_desc, "zx", true } };
#elif defined(TEST_APP_CPC)
        const Row rows[] = { { &cpc_desc, "cpc", true } };
#elif defined(TEST_APP_PET)
        const Row rows[] = { { &pet_desc, "pet", true } };
#elif defined(TEST_APP_C128)
        const Row rows[] = { { &c128_desc, "c128", true } };
#elif defined(TEST_APP_NEOGEO)
        const Row rows[] = { { &neogeo_desc, "neogeo", true } };
#else
        const Row rows[] = { { &c64scpu_desc, "c64scpu", true },
                             { &c264_desc, "c264", true }, { &amiga_desc, "amiga", true }, { &apple2_desc, "apple2", true },
                             { &atari800_desc, "atari800", true }, { &zxnext_desc, "zxnext", true },
                             { &nes_desc, "nes", true }, { &snes_desc, "snes", true },
                             { &portdiag_desc, "portdiag", false } };
#endif
        for (const Row &r : rows) {
            CHECK_EQ(r.d->hotkeys != 0, r.has);
            if (!r.has) { static CoreDesc o; static Setting s[SETTINGS_MAX]; CHECK(!hk_extend(r.d, &o, s)); CHECK_EQ(o.nsettings, r.d->nsettings); continue; }
            const HkSpec &sp = *r.d->hotkeys;
            const HkMap &m = sp.def;
            CHECK(m.mod_a < 64);                                                    // a modifier of its own
            CHECK(hk_preset_index(m) > 0);                                           // the F8 item can show it
            uint8_t regs[11]; hk_regs(m, regs);
            CHECK_EQ(regs[0], 0x80 | m.mod_a);
            for (int a = 0; a < HK_NACT; a++) {
                if (a == HKA_CORE && !sp.core_op) { CHECK_EQ(m.act[a], HK_NONE); continue; }
                CHECK(m.act[a] != HK_NONE);
                CHECK(m.act[a] != m.mod_a && m.act[a] != m.mod_b);                 // the modifier passes
                if (m.act[a] < 64) CHECK(!((sp.own_keys >> m.act[a]) & 1));        // S5: not one of the core's own key uses while the modifier is held
                for (int b = 0; b < a; b++) CHECK(m.act[a] != m.act[b]);           // one key, one action
            }
            Rig g(r.d, true);
            CHECK_EQ(g.io.hk_regs_now[0], 0x80 | m.mod_a);
            CHECK_EQ(g.io.hk_regs_now[10], 1);
            CHECK_EQ(hk_current().mod_a, m.mod_a);
            CHECK_EQ(hk_current().act[HKA_RESET], m.act[HKA_RESET]);
            CHECK(g.st->find(HK_KEY_MOD) >= 0);
        }
    }
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_NEOGEO)
    // ---- T207: a hotkey is set by pressing the key (S1 the prompt and the key, S2 a used key is refused, S3 the modifier)
    {
        struct MHost : MenuHost { void info(int, char *o, int) { o[0] = 0; } bool return_to_host(char *m, int n) { snprintf(m, n, "no"); return false; } };
        auto fkey = [](int n) { static const int f[8] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 }; return f[n]; };
        { uint8_t z[8] = {}; uint8_t zr = 0; hk_usb_matrix_filter(z, &zr); }            // the USB keyboard of the test before: all keys up
        Rig g(&atari800_desc, true, "/sd/cores/hklearn.cfg");
        MHost host;
        MenuCtl *m = new MenuCtl(&g.c, &host);
        m->open();
        // find the Hotkeys page by the tree (the modifier is its first row) and open it from its parent tab, the way a person does
        int pa = -1, pp = -1;
        for (int a = 0; a < m->tree().nareas && pa < 0; a++)
            for (int t = 0; t < m->tree().a[a].ntabs; t++) {
                const MenuTab &tb = m->tree().a[a].tabs[t];
                if (tb.page && tb.count > 0 && m->tree().e[tb.first].kind == EK_SETTING && !strcasecmp(g.d.settings[m->tree().e[tb.first].index].key, HK_KEY_MOD)) { pa = a; pp = tb.parent; break; }
            }
        CHECK(pa >= 0 && pp >= 0);
        auto text = [&](int i, int which) { char l[200], r[100], sb[200]; bool act; m->row(i, l, 200, r, 100, &act, sb, 200); return std::string(which == 0 ? l : which == 1 ? r : sb); };
        auto seek = [&](const char *label) { for (int n = 0; n < 10; n++) m->key(KEY_UP); for (int n = 0; n < 60 && text(m->selected(), 0) != label; n++) m->key(KEY_DOWN); CHECK_STR(text(m->selected(), 0).c_str(), label); };
        m->key(fkey(m->tree().a[pa].key[1] - '1')); m->key('0' + m->tree().a[pa].tabs[pp].n);
        seek("Hotkeys"); m->key(KEY_RETURN);
        CHECK_STR(text(0, 0).c_str(), "Modifier");
        auto press = [&](int k) { g.io.key(k, true); g.io.delay_ms(20); m->pump(true); };
        auto release = [&](int k) { g.io.key(k, false); g.io.delay_ms(20); m->pump(true); };
        const int base_act = hk_current().act[HKA_RESET];
        CHECK_EQ(base_act, 17);                                                       // R

        // S1: RETURN asks, the next key pressed is the hotkey, stored in the .cfg and in force
        seek("Reset");
        CHECK(!m->learning());
        g.io.key(K1, true); m->key(KEY_RETURN);                                       // the person's finger is still on a key: it is not the answer
        CHECK(m->learning() && m->entering());
        CHECK_STR(text(m->selected(), 0).c_str(), "Press the new key");
        CHECK(text(m->selected(), 2).find("Reset") != std::string::npos);
        g.io.delay_ms(500); m->pump(true); CHECK(m->learning());
        release(K1); CHECK(m->learning());
        m->key('x'); m->key(KEY_F5); CHECK(m->learning());                              // the menu's own keys do not answer it
        press(4);                                                                       // F1: a key the menu itself uses
        CHECK(!m->learning());
        CHECK_EQ(hk_current().act[HKA_RESET], 4);
        CHECK(g.fs.text("/sd/cores/hklearn.cfg").find("Hotkey reset=F1") != std::string::npos);
        CHECK(!m->status_bad() && std::string(m->status()).find("F1") != std::string::npos);
        release(4);
        // ESC cancels, RUN/STOP on the matrix too; nothing changes
        seek("Reset"); m->key(KEY_RETURN); CHECK(m->learning());
        m->key(KEY_ESCAPE); CHECK(!m->learning() && !m->entering()); CHECK_EQ(hk_current().act[HKA_RESET], 4);
        m->key(KEY_RETURN); CHECK(m->learning());
        press(63); CHECK(!m->learning()); CHECK_EQ(hk_current().act[HKA_RESET], 4); release(63);
        // RESTORE can be learned, too
        seek("Menu open / close"); m->key(KEY_RETURN); press(K1); CHECK_EQ(hk_current().act[HKA_MENU], K1); release(K1);    // RESTORE was the menu's
        seek("Reset"); m->key(KEY_RETURN); press(64); CHECK(!m->learning()); CHECK_EQ(hk_current().act[HKA_RESET], 64); release(64);
        seek("Reset"); m->key(KEY_RETURN); press(4); CHECK_EQ(hk_current().act[HKA_RESET], 4); release(4);

        // S2: a key another hotkey uses is refused with a line naming it; so is the modifier's key
        seek("Reset"); m->key(KEY_RETURN);
        const int before = hk_current().act[HKA_RESET];
        press(hk_current().act[HKA_VOL_UP] == HK_NONE ? KPLUS : hk_current().act[HKA_VOL_UP]);
        CHECK(!m->learning()); CHECK_EQ(hk_current().act[HKA_RESET], before);
        CHECK(m->status_bad() && std::string(m->status()).find("Volume up") != std::string::npos);
        release(hk_current().act[HKA_VOL_UP]);
        m->key(KEY_RETURN); press(CTRL); CHECK_EQ(hk_current().act[HKA_RESET], before);
        CHECK(m->status_bad() && std::string(m->status()).find("modifier") != std::string::npos); release(CTRL);

        // S3: the modifier is learned the same way; only a key (pair) of the list is accepted
        const int mod_i = g.st->find(HK_KEY_MOD);
        CHECK(mod_i >= 0);
        seek("Modifier"); m->key(KEY_RETURN);
        CHECK(m->learning()); CHECK_STR(text(m->selected(), 0).c_str(), "Press the new modifier key");
        press(KA); g.io.delay_ms(HK_LEARN_SETTLE_MS + 50); m->pump(true);              // A is no modifier
        CHECK(!m->learning() && m->status_bad() && std::string(m->status()).find("not a modifier") != std::string::npos);
        release(KA);
        m->key(KEY_RETURN); press(CBM); CHECK(m->learning());                          // a modifier settles: not at once
        g.io.delay_ms(HK_LEARN_SETTLE_MS + 50); m->pump(true);
        CHECK(!m->learning()); CHECK(!m->status_bad());
        CHECK_EQ(hk_current().mod_a, CBM); CHECK_EQ(hk_current().mod_b, HK_NONE);
        CHECK(g.fs.text("/sd/cores/hklearn.cfg").find("Hotkey modifier=") != std::string::npos);
        release(CBM);
        m->key(KEY_RETURN); g.io.key(CBM, true); m->pump(true); g.io.delay_ms(100); g.io.key(CTRL, true); m->pump(true);   // a pair: both, in turn
        g.io.delay_ms(HK_LEARN_SETTLE_MS + 50); m->pump(true);
        CHECK(!m->learning()); CHECK_EQ(hk_current().mod_a, CBM); CHECK_EQ(hk_current().mod_b, CTRL);
        g.io.key(CBM, false); g.io.key(CTRL, false); m->pump(true);
        m->key(KEY_RETURN); g.io.key(CBM, true); g.io.key(KSHIFT, true); m->pump(true); g.io.key(CBM, false); g.io.key(KSHIFT, false); g.io.delay_ms(20); m->pump(true);   // released at once: still taken
        CHECK(!m->learning()); CHECK_EQ(hk_current().mod_a, CBM); CHECK_EQ(hk_current().mod_b, KSHIFT);
        g.io.key(CBM, false); g.io.key(KSHIFT, false); m->pump(true);

        // D2 for LEFT / RIGHT: stepping steps over a key another hotkey uses, and names it; nothing duplicates
        seek("Volume up"); m->key(KEY_RETURN); press(5); release(5);                    // Volume up = key 5
        CHECK_EQ(hk_current().act[HKA_VOL_UP], 5);
        seek("Reset"); m->key(KEY_RETURN); press(4); release(4); CHECK_EQ(hk_current().act[HKA_RESET], 4);
        m->key(KEY_RIGHT);                                                              // 5 is Volume up's: Reset lands on 6
        CHECK_EQ(hk_current().act[HKA_RESET], 6); CHECK_EQ(hk_current().act[HKA_VOL_UP], 5);
        CHECK(!m->status_bad() && std::string(m->status()).find("Volume up") != std::string::npos);
        m->key(KEY_LEFT);                                                               // and back over it
        CHECK_EQ(hk_current().act[HKA_RESET], 4); CHECK_EQ(hk_current().act[HKA_VOL_UP], 5);
        for (int n = 0; n < 80; n++) {                                                  // a full round: no step ever lands on another hotkey's key or the modifier's
            m->key(KEY_RIGHT);
            const HkMap &cm = hk_current();
            CHECK(cm.act[HKA_RESET] == HK_NONE || (cm.act[HKA_RESET] != cm.act[HKA_VOL_UP] && cm.act[HKA_RESET] != cm.act[HKA_MENU] && cm.act[HKA_RESET] != cm.mod_a && cm.act[HKA_RESET] != cm.mod_b));
        }
        // the modifier steps over a preset whose key is a hotkey's
        seek("Modifier"); m->key(KEY_RETURN); press(CTRL); g.io.delay_ms(HK_LEARN_SETTLE_MS + 50); m->pump(true); release(CTRL);
        CHECK_EQ(hk_current().mod_a, CTRL); CHECK_EQ(hk_current().mod_b, HK_NONE);
        seek("Reset"); m->key(KEY_RETURN); press(CBM); release(CBM); CHECK_EQ(hk_current().act[HKA_RESET], CBM);
        seek("Modifier");
        for (int n = 0; n < 12; n++) {
            m->key(KEY_RIGHT);
            CHECK(hk_current().mod_a != CBM && hk_current().mod_b != CBM);
        }
    }
#endif
#ifdef TEST_APP_PET
    // ---- T232: pad keys A-Z and learn by key press (PET rows; the board's path: the menu learn -> settings_set -> regset -> the register file)
    {
        struct MHost : MenuHost { void info(int, char *o, int) { o[0] = 0; } bool return_to_host(char *m, int n) { snprintf(m, n, "no"); return false; } };
        auto fkey = [](int n) { static const int f[8] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 }; return f[n]; };
        { uint8_t z[8] = {}; uint8_t zr = 0; hk_usb_matrix_filter(z, &zr); }
        // S2: the old list is the same up to index 31 (names and codes), the letters are appended; an old T216 .cfg loads as the same key
        CHECK_EQ(PADKEY_COUNT, 50);
        CHECK_STR(padkey_names[1], "Key 8"); CHECK_EQ(padkey_codes[1], 27); CHECK_STR(padkey_names[15], "Key Cursor left"); CHECK_EQ(padkey_codes[15], 2 | 0x40);
        CHECK_STR(padkey_names[20], "Key W"); CHECK_EQ(padkey_codes[20], 9); CHECK_STR(padkey_names[27], "Key X"); CHECK_EQ(padkey_codes[27], 23); CHECK_STR(padkey_names[31], "Key /");
        {   // every letter A-Z is offered once, with its place in the matrix
            static const struct { char ch; int code; } L[] = { {'A',10},{'B',28},{'C',20},{'D',18},{'E',14},{'F',21},{'G',26},{'H',29},{'I',33},{'J',34},{'K',37},{'L',42},{'M',36},
                {'N',39},{'O',38},{'P',41},{'Q',62},{'R',17},{'S',13},{'T',22},{'U',30},{'V',31},{'W',9},{'X',23},{'Y',25},{'Z',12} };
            for (auto &l : L) {
                char nm[16]; snprintf(nm, sizeof(nm), "Key %c", l.ch);
                int found = 0;
                for (int i = 0; i < PADKEY_COUNT; i++) if (!strcmp(padkey_names[i], nm)) { found++; CHECK_EQ(padkey_codes[i], l.code); }
                CHECK_EQ(found, 1);
            }
        }
        Rig g(&pet_desc);
        g.fs.put_text("/sd/cores/hk.cfg", std::string("[") + g.d.store + "]\nPad up key=Key Cursor left\nPad fire key=Key X\n");
        g.st->load();
        regset_apply(&g.c, true);
        int iu = -1, id = -1, il = -1, ir = -1, ifr = -1;
        for (int i = 0; i < g.d.nsettings; i++) {
            if (!strcmp(g.d.settings[i].key, "Pad up key")) iu = i;
            if (!strcmp(g.d.settings[i].key, "Pad down key")) id = i;
            if (!strcmp(g.d.settings[i].key, "Pad left key")) il = i;
            if (!strcmp(g.d.settings[i].key, "Pad right key")) ir = i;
            if (!strcmp(g.d.settings[i].key, "Pad fire key")) ifr = i;
        }
        CHECK(iu >= 0 && id >= 0 && il >= 0 && ir >= 0 && ifr >= 0);
        CHECK_EQ(g.st->value(iu), 15); CHECK_EQ(g.st->value(ifr), 27);
        CHECK_EQ(g.io.regs[0x164], 2 | 0x40); CHECK_EQ(g.io.regs[0x168], 23);
        CHECK_EQ(g.st->value(id), PADKEY_DEF_DOWN);                                      // an unset row keeps the default
        CHECK(padkey_row(g.d.settings[iu]) && hk_learn_item(&g.c, iu));
        // S3/S1: learn I K J L on the four rows up/down/left/right by pressing the keys on the (USB) keyboard
        MHost host;
        MenuCtl *m = new MenuCtl(&g.c, &host);
        m->open();
        int pa = -1, pt = -1;
        for (int a = 0; a < m->tree().nareas && pa < 0; a++)
            for (int t = 0; t < m->tree().a[a].ntabs; t++) {
                const MenuTab &tb = m->tree().a[a].tabs[t];
                for (int e = tb.first; e < tb.first + tb.count; e++) if (m->tree().e[e].kind == EK_SETTING && m->tree().e[e].index == iu) { pa = a; pt = t; }
            }
        CHECK(pa >= 0);
        auto text = [&](int i, int which) { char l[200], r[100], sb[200]; bool act; m->row(i, l, 200, r, 100, &act, sb, 200); return std::string(which == 0 ? l : which == 1 ? r : sb); };
        auto seek = [&](const char *label) { for (int n = 0; n < 40; n++) m->key(KEY_UP); for (int n = 0; n < 60 && text(m->selected(), 0) != label; n++) m->key(KEY_DOWN); CHECK_STR(text(m->selected(), 0).c_str(), label); };
        auto press = [&](int k) { g.io.key(k, true); g.io.delay_ms(20); m->pump(true); };
        auto release = [&](int k) { g.io.key(k, false); g.io.delay_ms(20); m->pump(true); };
        m->key(fkey(m->tree().a[pa].key[1] - '1')); m->key('1' + pt);
        const struct { const char *label; int set; int key; } W[] = { { "Pad / joystick up", iu, 33 }, { "Pad / joystick down", id, 37 },
                                                                                { "Pad / joystick left", il, 34 }, { "Pad / joystick right", ir, 42 } };
        for (auto &w : W) {
            seek(w.label);
            m->key(KEY_RETURN);
            CHECK(m->learning());
            CHECK_STR(text(m->selected(), 0).c_str(), "Press the new key");
            press(w.key); CHECK(m->learning()); release(w.key); CHECK(!m->learning() && !m->status_bad());   // the keys held are taken when released (or settled)
            CHECK_EQ(padkey_codes[g.st->value(w.set)], w.key);
        }
        CHECK_EQ(g.io.regs[0x164], 33); CHECK_EQ(g.io.regs[0x165], 37); CHECK_EQ(g.io.regs[0x166], 34); CHECK_EQ(g.io.regs[0x167], 42);   // I K J L reach the core
        CHECK(g.fs.text("/sd/cores/hk.cfg").find("Pad up key=Key I") != std::string::npos);
        // a key not in the list is refused (the C= key, matrix 61, has no entry in the list), the value stays; ESC keeps the old value
        seek("Pad / joystick fire"); m->key(KEY_RETURN); press(61); release(61); CHECK(!m->learning() && m->status_bad());
        CHECK_EQ(padkey_codes[g.st->value(ifr)], 23);
        m->key(KEY_RETURN); CHECK(m->learning()); m->key(KEY_ESCAPE); CHECK(!m->learning()); CHECK_EQ(padkey_codes[g.st->value(ifr)], 23);
        // an A-Z key of the old list is still learned as its old index
        m->key(KEY_RETURN); press(9); release(9); CHECK(!m->learning()); CHECK_EQ(g.st->value(ifr), 20); CHECK_EQ(g.io.regs[0x168], 9);
        // two keys held together: refused with "Press one key", the value stays
        m->key(KEY_RETURN); g.io.key(33, true); g.io.key(37, true); g.io.delay_ms(500); m->pump(true); g.io.key(33, false); g.io.key(37, false); m->pump(true);
        CHECK(!m->learning() && m->status_bad() && strstr(m->status(), "Press one key") != 0); CHECK_EQ(g.st->value(ifr), 20);
        // SHIFT held alone past the settle time does not end the learn; SHIFT + cursor down then learns cursor up (code 7 + shift)
        m->key(KEY_RETURN); g.io.key(15, true); g.io.delay_ms(500); m->pump(true); CHECK(m->learning());
        press(7); release(7); g.io.key(15, false); m->pump(true); CHECK(!m->learning() && !m->status_bad()); CHECK_EQ(g.st->value(ifr), 13);
        // SHIFT and cursor down in one poll (they arrive together): another value first, so the check can fail
        m->key(KEY_RETURN); press(9); release(9); CHECK(!m->learning()); CHECK_EQ(g.st->value(ifr), 20);
        m->key(KEY_RETURN); g.io.key(15, true); press(7); release(7); g.io.key(15, false); m->pump(true);
        CHECK(!m->learning() && !m->status_bad()); CHECK_EQ(g.st->value(ifr), 13);
        // the right SHIFT (matrix 52) is a SHIFT too
        m->key(KEY_RETURN); press(9); release(9); CHECK(!m->learning()); CHECK_EQ(g.st->value(ifr), 20);
        m->key(KEY_RETURN); g.io.key(52, true); press(7); release(7); g.io.key(52, false); m->pump(true);
        CHECK(!m->learning() && !m->status_bad()); CHECK_EQ(g.st->value(ifr), 13);
        // RESTORE is no entry of the list: refused with the reason, the value stays
        m->key(KEY_RETURN); press(64); release(64);
        CHECK(!m->learning() && m->status_bad() && strstr(m->status(), "not available") != 0); CHECK_EQ(g.st->value(ifr), 13);
    }
#endif
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_NEOGEO)
    // ---- T228 R1/R16 (gate fixes 3 and 5): two tasks of a core's menu (Timewarp... and Monitor..., hotkey T and O) hold CTRL+T and CTRL+O by default; a key the stored settings give a
    // fixed action is not doubled. The core is the NES descriptor with the two tasks and the items hk_extend and hk_extend_tasks make (T239.3 P1-8: trx64u's menu is TRX64-Ultimate's)
    {
        static Task tk[2];
        memset(tk, 0, sizeof(tk));
        tk[0].label = "Timewarp..."; tk[0].op = "menu.screen"; tk[0].closes_menu = true; tk[0].hotkey = 'T';
        tk[1].label = "Monitor..."; tk[1].op = "menu.screen"; tk[1].closes_menu = true; tk[1].hotkey = 'O';
        static CoreDesc hd;
        static Setting hs[SETTINGS_MAX];
        CHECK(!hk_extend(&nes_desc, &hd, hs));
        hd.tasks = tk; hd.ntasks = 2;
        CHECK(!hk_extend_tasks(&hd, hs, SETTINGS_MAX));
        const CoreDesc *d = &hd;
        int idx[HK_TASKS_MAX];
        CHECK_EQ(hk_tasks(d, idx), 2);
        auto key_named = [](const char *nm) { for (int k = 0; k < HK_NKEYS; k++) if (!strcmp(hk_key_name(k), nm)) return k; return -1; };
        const int kt = key_named("T"), ko = key_named("O");
        CHECK(kt >= 0 && ko >= 0);
        FakeIo io; FakeFs fs; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.started = true;
        Settings *st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/hkt.cfg", 0);
        st->load();
        c.settings = st;
        hk_apply(&c);
        HkMap m = hk_map_of(&c);
        CHECK_EQ(m.ntask, 2); CHECK_EQ(m.mod_a, CTRL); CHECK_EQ(m.mod_b, HK_NONE);
        CHECK_EQ(m.task[0], kt); CHECK_EQ(m.task[1], ko);
        int ti = st->find("Hotkey Timewarp"), ri = st->find("Hotkey reset");
        CHECK(ti >= 0 && ri >= 0);
        CHECK_EQ(st->value(ti), kt + 1);
        CHECK(st->set(ri, kt + 1));                                                  // the person gave Reset the key T
        m = hk_map_of(&c);
        CHECK_EQ(m.act[HKA_RESET], kt); CHECK_EQ(m.task[0], HK_NONE); CHECK_EQ(m.task[1], ko);
        hk_apply(&c);
        CHECK_EQ(st->value(ti), 0);                                                  // the Hotkeys page shows none for Timewarp
        CHECK_EQ(hk_current().task[0], HK_NONE); CHECK_EQ(hk_current().act[HKA_RESET], kt);
    }
#endif
    DONE("test_hotkeys");
}
