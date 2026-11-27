// T156 D5: "Map buttons" in the real MenuCtl on the SNES and NES descriptors: the prompt names the core's buttons one by one, the pad's next press is the
// source, RETURN keeps a button's entry, the left arrow goes back, RUN/STOP and ESC cancel without saving, a pulled pad cancels; at the end the map is in the
// .cfg ("Pad N map"), the setting "Pad map" is Custom, and the register word follows the new map. The .cfg round trip is in test_pad.cc.
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "menuctl.h"
#include "ops_glue.h"
#include "cores.h"
#ifdef TEST_APP_NES                      // the NES's app (run.sh nesapp) has its own core only
#include "nes_regs.h"
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#elif defined(TEST_APP_SNES)             // the SNES's app (run.sh snesapp, the unit snes_padmenu) has its own core only
#include "snes_regs.h"
extern const CoreDesc snes_desc;         // cores.h lists the shared app's cores; the SNES is its own app (cores/snes/app/lib)
#else
#error "test_padmenu runs on a core's menu: build it as a core's app (run.sh <core>app, TEST_APP_<CORE>); the shared app has no core of its own left (T239.3 D61)"
#endif
#include "keyboard.h"
#include "pad.h"

typedef std::vector<uint8_t> V;
// the Switch-Pro-like report of test_pad.cc: Button n is bit n-1 of the first two bytes, hat in byte 2, X Y in bytes 3 and 4
static const V d_hat = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x81, 0x02,
    0x95, 0x02, 0x81, 0x01,
    0x05, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x75, 0x04, 0x95, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x95, 0x01, 0x81, 0x01,
    0x26, 0xFF, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,
    0xC0 };
static V rep(uint16_t buttons, uint8_t hat = 15) { return V{ (uint8_t)buttons, (uint8_t)(buttons >> 8), hat, 0x80, 0x80, 0x80, 0x80, 0 }; }

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "-"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

static int k1, k2;

struct Rig {
    FakeIo io; FakeFs fs; Host host; CoreCtx c; Settings *st; MenuCtl *m;
    explicit Rig(const CoreDesc *d, const char *cfg)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        st->load();
        c.settings = st;
        pad_configure(d->pad);
        pad_load_maps(st);
        m = new MenuCtl(&c, &host);
        m->open();
    }
    ~Rig() { pad_detach(&k1); pad_detach(&k2); delete m; delete st; }
    std::string left(int i) { char l[200], r[100]; bool a; m->row(i, l, 200, r, 100, &a); return l; }
    std::string right(int i) { char l[200], r[100]; bool a; m->row(i, l, 200, r, 100, &a); return r; }
    std::string sub(int i) { char l[200], r[100], s[300]; bool a; m->row(i, l, 200, r, 100, &a, s, 300); return s; }
    // the row with this text, in any area and tab; the cursor on it
    bool goto_row(const char *text)
    {
        static const int fkeys[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };
        for (int a = 0; a < m->tree().nareas; a++) {
            m->key(fkeys[m->tree().a[a].key[1] - '1']);
            for (int t = 0; t < m->tree().a[a].ntabs; t++) {
                m->key('1' + t);
                for (int i = 0; i < m->rows(); i++) {
                    if (left(i) == text) {
                        while (m->selected() > i) m->key(KEY_UP);
                        while (m->selected() < i) m->key(KEY_DOWN);
                        return true;
                    }
                }
            }
        }
        return false;
    }
    void report(const void *key, const V &v) { pad_report(key, v.data(), (int)v.size()); }
    // the person presses a button of the pad and lets go; the run loop's pass in between
    bool press(const void *key, uint16_t buttons, uint8_t hat = 15)
    {
        report(key, rep(buttons, hat));
        bool changed = m->map_tick();
        report(key, rep(0));
        m->map_tick();
        return changed;
    }
    std::string cfg() { auto &f = fs.files["/sd/cores/snes/snes.cfg"]; return std::string(f.begin(), f.end()); }
};

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
#ifndef TEST_APP_NES                     // the SNES's cases; the NES's is its unit nes_padmenu (below)
    const char *cfgp = "/sd/cores/snes/snes.cfg";
    // ---- no pad: the task says so and the wizard does not start
    {
        Rig r(&snes_desc, cfgp);
        CHECK(r.goto_row("Map buttons, pad 1"));
        CHECK(r.goto_row("Map buttons, pad 2"));
        r.m->key(KEY_RETURN);
        CHECK(!r.m->mapping());
        CHECK_STR(r.m->status(), "No pad is connected as pad 2");
        CHECK(r.m->status_bad());
    }
    // ---- the whole walk through SNES player 1: B Y Select Start Up Down Left Right A X L R
    {
        Rig r(&snes_desc, cfgp);
        CHECK(pad_attach(&k1, "SN30", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(r.goto_row("Map buttons, pad 1"));
        r.m->key(KEY_RETURN);
        CHECK(r.m->mapping());
        CHECK_EQ(r.m->rows(), 1);
        CHECK_STR(r.m->title(), "Map buttons");
        CHECK_STR(r.left(0).c_str(), "Press the pad button for B");
        CHECK_STR(r.right(0).c_str(), "1 of 12");
        CHECK(r.sub(0).find("RUN/STOP cancels") != std::string::npos);
        CHECK(r.sub(0).find("Now: Button 1") != std::string::npos);         // the default map's source for B
        CHECK_STR(ops_state(&r.c, "pad.map.active"), "1");                  // T192.3 S2: the wizard is the module's: its state and its texts are the operations' and states' of pad.map.*
        CHECK_STR(ops_state(&r.c, "pad.map.left"), "Press the pad button for B");
        CHECK_STR(ops_state(&r.c, "pad.map.right"), "1 of 12");
        CHECK_STR(ops_state(&r.c, "pad.map.title"), "Map buttons");
        r.m->key(KEY_DOWN);                                                  // other keys do nothing while mapping
        CHECK(r.m->mapping());
        CHECK(!r.m->map_tick());                                             // nothing pressed: nothing to do
        // the person's pad numbers its buttons the other way round: B = Button 9, Y = 10, Select = 11, Start = 12; the D-pad on the hat; A = 1, X = 2, L = 3, R = 4
        uint16_t b[12] = { 0x0100, 0x0200, 0x0400, 0x0800, 0, 0, 0, 0, 0x0001, 0x0002, 0x0004, 0x0008 };
        uint8_t hat[12] = { 15, 15, 15, 15, 0, 4, 6, 2, 15, 15, 15, 15 };    // Up Down Left Right on the hat
        for (int i = 0; i < 12; i++) {
            std::string want = std::string("Press the pad button for ") + snes_desc.pad->names[i];
            CHECK_STR(r.left(0).c_str(), want.c_str());
            CHECK(r.m->mapping());
            CHECK(r.press(&k1, b[i], hat[i]));
        }
        CHECK(!r.m->mapping());
        CHECK(ops_state(&r.c, "pad.map.active") == 0);
        CHECK(std::string(r.m->status()).find("12 buttons mapped and saved") != std::string::npos);
        CHECK(!r.m->status_bad());
        CHECK_EQ(pad_mode(), PAD_MODE_CUSTOM);
        CHECK(pad_custom_valid(0));
        CHECK(!pad_custom_valid(1));
        uint8_t want[12] = { 8, 9, 10, 11, PAD_SRC_UP, PAD_SRC_DOWN, PAD_SRC_LEFT, PAD_SRC_RIGHT, 0, 1, 2, 3 };
        CHECK(!memcmp(pad_active_map(0), want, 12));
        CHECK(r.cfg().find("Pad 1 map=08090A0B2021222300010203") != std::string::npos);
        CHECK(r.cfg().find("Pad map=Custom") != std::string::npos);
        CHECK_EQ(r.st->value(r.st->find("Pad map")), 1);
        // the new map is in force: Button 9 is SNES B (bit 0), Button 1 is SNES A (bit 8)
        {
            V p = rep(0x0100);
            r.report(&k1, p);
            pad_pace(&r.c);
            CHECK_EQ(r.io.regs[SNES_PAD1] | r.io.regs[SNES_PAD1 + 1] << 8, 1);
            p = rep(0x0001);
            r.report(&k1, p);
            pad_pace(&r.c);
            CHECK_EQ(r.io.regs[SNES_PAD1] | r.io.regs[SNES_PAD1 + 1] << 8, 1 << 8);
        }
        // the next start of the application: the file brings the map and the mode back
        Settings st2(snes_desc.settings, snes_desc.nsettings, snes_desc.store, &r.fs, cfgp, 0);
        st2.load();
        pad_configure(snes_desc.pad);
        pad_load_maps(&st2);
        CHECK_EQ(pad_mode(), PAD_MODE_CUSTOM);
        CHECK(!memcmp(pad_active_map(0), want, 12));
    }
    // ---- left arrow goes back one; RETURN keeps the entry; a button can be mapped again
    {
        Rig r(&snes_desc, cfgp);
        CHECK(pad_attach(&k1, "SN30", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(r.goto_row("Map buttons, pad 1"));
        r.m->key(KEY_RETURN);
        CHECK(r.press(&k1, 0x0100));                                          // B = Button 9
        CHECK(r.press(&k1, 0x0200));                                          // Y = Button 10
        CHECK_STR(r.left(0).c_str(), "Press the pad button for Select");
        r.m->key('`');                                                        // the C64's left-arrow key (keymap_normal gives '`'): back to Y
        CHECK_STR(r.left(0).c_str(), "Press the pad button for Y");
        CHECK_STR(r.right(0).c_str(), "2 of 12");
        CHECK(r.press(&k1, 0x0400));                                          // Y = Button 11 this time
        r.m->key(KEY_BACK); r.m->key(KEY_BACK); r.m->key(KEY_BACK);           // back past the first one stays on the first
        CHECK_STR(r.left(0).c_str(), "Press the pad button for B");
        CHECK_STR(r.right(0).c_str(), "1 of 12");
        CHECK(r.sub(0).find("Now: Button 9") != std::string::npos);          // what RETURN keeps is shown: B's entry of this walk
        r.m->key(KEY_RETURN);                                                 // keep B's entry
        CHECK_STR(r.left(0).c_str(), "Press the pad button for Y");
        CHECK(r.press(&k1, 0x0400));
        for (int i = 2; i < 11; i++) r.m->key(KEY_RETURN);                    // keep Select .. X
        CHECK_STR(r.left(0).c_str(), "Press the pad button for R");
        CHECK(r.m->mapping());
        r.m->key(KEY_RETURN);                                                 // keep the last one: done
        CHECK(!r.m->mapping());
        const uint8_t *m = pad_active_map(0);
        CHECK_EQ(m[0], 8);                                                    // B kept: the entry it got earlier in this walk (Button 9), not the default
        CHECK_EQ(m[1], 10);                                                   // Y = Button 11
        CHECK_EQ(m[2], 6);                                                    // Select: the generic numbering, Button 7
        CHECK_EQ(m[11], 5);                                                   // R: Button 6
        CHECK_EQ(pad_mode(), PAD_MODE_CUSTOM);
    }
    // ---- RUN/STOP and ESC cancel: nothing is saved, the mode stays
    {
        for (int k = 0; k < 2; k++) {
            Rig r(&snes_desc, cfgp);
            r.fs.files.erase("/sd/cores/snes/snes.cfg");
            CHECK(pad_attach(&k1, "SN30", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
            CHECK(r.goto_row("Map buttons, pad 1"));
            r.m->key(KEY_RETURN);
            CHECK(r.press(&k1, 0x0100));
            CHECK(r.press(&k1, 0x0200));
            CHECK_EQ(r.m->key(k ? KEY_ESCAPE : KEY_BREAK), MenuCtl::R_REDRAW);        // the menu itself stays open: only the mapping ends
            CHECK(!r.m->mapping());
            CHECK(std::string(r.m->status()).find("nothing was saved") != std::string::npos);
            CHECK(!pad_custom_valid(0));
            CHECK_EQ(pad_mode(), PAD_MODE_DEFAULT);
            CHECK(r.fs.files.find("/sd/cores/snes/snes.cfg") == r.fs.files.end());
            CHECK(!pad_capturing());
        }
    }
    // ---- the pad is pulled in the middle: the mapping ends, nothing is saved
    {
        Rig r(&snes_desc, cfgp);
        r.fs.files.erase("/sd/cores/snes/snes.cfg");
        CHECK(pad_attach(&k1, "SN30", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(r.goto_row("Map buttons, pad 1"));
        r.m->key(KEY_RETURN);
        CHECK(r.press(&k1, 0x0100));
        pad_detach(&k1);
        CHECK(r.m->map_tick());
        CHECK(!r.m->mapping());
        CHECK(r.m->status_bad());
        CHECK(std::string(r.m->status()).find("pad is gone") != std::string::npos);
        CHECK(!pad_custom_valid(0));
        CHECK(r.fs.files.find("/sd/cores/snes/snes.cfg") == r.fs.files.end());
    }
#else                                    // the NES's app (run.sh nesapp): the case on its core
    // ---- NES player 2 (eight buttons), with two pads: each player's map is its own
    {
        Rig r(&nes_desc, "/sd/cores/nes/nes.cfg");
        CHECK(pad_attach(&k1, "first", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(pad_attach(&k2, "second", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(r.goto_row("Map buttons, pad 2"));
        r.m->key(KEY_RETURN);
        CHECK_STR(r.right(0).c_str(), "1 of 8");
        CHECK(r.sub(0).find("Pad 2.") != std::string::npos);
        // a press on the first pad does not answer pad 2's question
        r.report(&k1, rep(0x0100));
        CHECK(!r.m->map_tick());
        r.report(&k1, rep(0));
        for (int i = 0; i < 8; i++) CHECK(r.press(&k2, (uint16_t)(0x0010 << i)));     // Buttons 5..12 for A B Select Start Up Down Left Right
        CHECK(!r.m->mapping());
        CHECK(pad_custom_valid(1));
        CHECK(!pad_custom_valid(0));
        CHECK_EQ(pad_active_map(1)[0], 4);
        CHECK_EQ(pad_active_map(1)[7], 11);
        { uint8_t g[8]; pad_default_map(nes_desc.pad, 0, 0, g); CHECK(!memcmp(pad_active_map(0), g, 8)); }                             // player 1 keeps the default
        auto &f = r.fs.files["/sd/cores/nes/nes.cfg"];
        std::string t(f.begin(), f.end());
        CHECK(t.find("Pad 2 map=0405060708090A0B") != std::string::npos);
        CHECK(t.find("Pad 1 map") == std::string::npos);
    }
#endif
    DONE("padmenu");
}
