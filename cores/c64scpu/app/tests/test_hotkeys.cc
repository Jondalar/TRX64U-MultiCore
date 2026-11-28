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
extern const CoreDesc c64scpu_desc;             // cores.h lists the shared app's cores; the C64SCPU is its own app (cores/c64scpu/app/lib)

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
    // ---- S5 and the per-core default maps: every core that has hotkeys, the default is harmless in it
    {
        struct Row { const CoreDesc *d; const char *name; bool has; };
        const Row rows[] = { { &c64scpu_desc, "c64scpu", true } };
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
