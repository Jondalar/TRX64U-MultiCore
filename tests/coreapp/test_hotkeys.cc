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
#ifdef TEST_APP_APPLE2
extern const CoreDesc apple2_desc;              // cores.h lists the shared app's cores; the Apple IIe is its own app (cores/apple2/app/lib)
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
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_APPLE2)          // the cases that stand on the shared app's cores (the VIC-20's and the A1200's app run this file for the one row of S5)
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
#endif
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_APPLE2)
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
#endif
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_APPLE2)
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
    // ---- S5 and the per-core default maps: every core that has hotkeys, the default is harmless in it
    {
        struct Row { const CoreDesc *d; const char *name; bool has; };
#ifdef TEST_APP_VC20
        const Row rows[] = { { &vc20_desc, "vc20", true } };
#elif defined(TEST_APP_APPLE2)
        const Row rows[] = { { &apple2_desc, "apple2", true } };
#else
        const Row rows[] = { { 0, "", false } };          // the shared app has no real core left (T239.3 D61): no row; every core's S5 is its app's unit <core>_hotkeys
#endif
        for (const Row &r : rows) {
            if (!r.d) continue;
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
    DONE("test_hotkeys");
}
