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
#include "nes_regs.h"
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
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
    DONE("padmenu");
}
