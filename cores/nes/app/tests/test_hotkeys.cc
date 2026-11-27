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
extern const CoreDesc nes_desc;                 // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)

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
    // ---- S5 and the per-core default maps: every core that has hotkeys, the default is harmless in it
    {
        struct Row { const CoreDesc *d; const char *name; bool has; };
        const Row rows[] = { { &nes_desc, "nes", true } };
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
    DONE("test_hotkeys");
}
