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
extern const CoreDesc atari800_desc;            // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)

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
        const Row rows[] = { { &atari800_desc, "atari800", true } };
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
    DONE("test_hotkeys");
}
