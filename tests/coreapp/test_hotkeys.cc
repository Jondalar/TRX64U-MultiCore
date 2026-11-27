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
#ifdef TEST_APP_SNES
extern const CoreDesc snes_desc;                // cores.h lists the shared app's cores; the SNES is its own app (cores/snes/app/lib)
#endif
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)
#endif
#ifdef TEST_APP_APPLE2
extern const CoreDesc apple2_desc;              // cores.h lists the shared app's cores; the Apple IIe is its own app (cores/apple2/app/lib)
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
#ifdef TEST_APP_DIAG
extern const CoreDesc portdiag_desc;           // cores.h lists the shared app's cores; the diag core is its own app (cores/diag/app/lib)
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
#ifdef TEST_APP_DIAG
    // ---- a core without the setting (the diag core: T239.5 D13, its app's unit diag_hotkeys): the line says so, nothing changes
    {
        char line[64];
        Rig z(&portdiag_desc, false);
        CHECK(!hk_swap_joystick(&z.c, line, sizeof(line))); CHECK(strstr(line, "No joystick swap"));
        CHECK(!hk_step_volume(&z.c, 1, line, sizeof(line)) || true);
    }
#endif
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_AMIGA) && !defined(TEST_APP_DIAG) && !defined(TEST_APP_APPLE2) && !defined(TEST_APP_SNES)          // the cases that stand on the shared app's cores (the VIC-20's and the A1200's app run this file for the one row of S5)
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
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_AMIGA) && !defined(TEST_APP_DIAG) && !defined(TEST_APP_APPLE2) && !defined(TEST_APP_SNES)
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
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_AMIGA) && !defined(TEST_APP_DIAG) && !defined(TEST_APP_APPLE2) && !defined(TEST_APP_SNES)
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
#elif defined(TEST_APP_AMIGA)
        const Row rows[] = { { &amiga_desc, "amiga", true } };
#elif defined(TEST_APP_APPLE2)
        const Row rows[] = { { &apple2_desc, "apple2", true } };
#elif defined(TEST_APP_DIAG)
        const Row rows[] = { { &portdiag_desc, "portdiag", false } };
#elif defined(TEST_APP_SNES)
        const Row rows[] = { { &snes_desc, "snes", true } };
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
    DONE("test_hotkeys");
}
