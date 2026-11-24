// T192.3 D30: the menu opens where it was left when the core's "Menu opens at" says "Last screen" (area, tab, row by its ident, the Library's folder and file, the scroll of the
// list); "Home (F1)" (the default) opens on Home as before. What is gone falls back: the file to the folder's first row, the folder to the nearest one above that lists.
// Same functions as the board: MenuCtl::open / key / leave, the generic setting of home_extend stored through settings.set.
#include <string>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "menuctl.h"
#include "keyboard.h"
#include "home.h"
#include "ops_glue.h"

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *, int) { return false; }
};

static const char *CFG = "/sd/cores/reopen.cfg";

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st; Host host; MenuCtl *m = 0;
    explicit Rig(const CoreDesc *d)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, CFG, 0);
        st->load();
        c.settings = st;
    }
    void menu() { m = new MenuCtl(&c, &host); m->open(); }
    void set(const char *v) { OpResult r; OpIn in[2] = { in_str("key", REOPEN_KEY), in_str("value", v) }; CHECK_EQ(ops_call(&c, "settings.set", in, 2, &r), OPE_OK); }
    std::string left(int i) { char l[300], r[100]; bool a; m->row(i, l, 300, r, 100, &a); return l; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    bool go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return false; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); return true; }
    std::string cur() { return m->selected() < m->rows() ? left(m->selected()) : ""; }
    void close() { CHECK_EQ(m->key(KEY_BREAK), MenuCtl::R_CLOSE); }
};

int main()
{
    static CoreDesc gd;
    static Setting gs[SETTINGS_MAX];
    CHECK(!home_extend(&amiga_desc, &gd, gs));
    {   // the setting: one generic item, two values, default Home, listed on F8 with the other generic ones
        int n = 0;
        for (int i = 0; i < gd.nsettings; i++) n += !strcasecmp(gd.settings[i].key, REOPEN_KEY);
        CHECK_EQ(n, 1);
        Rig f(&gd);
        CHECK(!home_reopen_last(&f.c));
        f.menu();
        f.m->key(KEY_F7);                                           // T212 D1: the app's settings (APP)
        CHECK(f.find_row("Menu opens at") > 0);
        f.set("Last screen");
        CHECK(home_reopen_last(&f.c));
        CHECK(f.fs.text(CFG).find("Menu opens at=Last screen") != std::string::npos);
    }
    {   // Home (F1), the default: the menu opens on Home wherever it was left
        Rig h(&gd);
        h.menu();
        h.m->key(KEY_F5);
        h.go_to("Chipset");
        h.close();
        h.m->open();
        CHECK_EQ(h.m->area(), 0);
        CHECK_EQ(h.m->tab(), 0);
    }
    {   // Last screen: area, tab and row come back; the status line is cleared (D29)
        Rig l(&gd);
        l.set("Last screen");
        l.menu();
        l.m->key(KEY_F5);
        int area = l.m->area(), tab = l.m->tab();
        CHECK(area != 0 && tab == 0);
        l.go_to("Chipset");
        l.m->key(KEY_RIGHT);                                        // a setting changed: a status line stands
        CHECK(l.m->status()[0]);
        l.go_to("Chipset");
        l.close();
        l.m->open();
        CHECK_EQ(l.m->area(), area);
        CHECK_EQ(l.m->tab(), tab);
        CHECK_STR(l.cur().c_str(), "Chipset");
        CHECK_STR(l.m->status(), "");
        // a row after the first one, on another tab, and Home itself (area 0) is a place too
        l.m->key(KEY_F1);
        l.close();
        l.m->open();
        CHECK_EQ(l.m->area(), 0);
        // switched off again: Home
        l.m->key(KEY_F7);
        l.set("Home (F1)");
        l.close();
        l.m->open();
        CHECK_EQ(l.m->area(), 0);
    }
    {   // the scroll of the list: the screen's note comes back through scroll_first(), and the revision tells the screen to take it
        Rig s(&gd);
        s.set("Last screen");
        s.menu();
        s.m->key(KEY_F5);
        s.go_to("Chipset");
        s.m->note_scroll(3);
        uint32_t rev = s.m->scroll_rev();
        s.close();
        s.m->open();
        CHECK_EQ(s.m->scroll_first(), 3);
        CHECK(s.m->scroll_rev() != rev);
    }
    FakeFs world;
    world.put("/usb0/games/sub/x.adf", std::vector<uint8_t>(10, 1));
    world.put("/usb0/games/alpha.adf", std::vector<uint8_t>(10, 1));
    world.put("/usb0/games/beta.adf", std::vector<uint8_t>(10, 1));
    world.put("/usb0/other/y.adf", std::vector<uint8_t>(10, 2));
    auto library = [&](Rig &r) {
        r.c.fs = &r.fs;
        r.menu();
        r.m->key(KEY_F3);
        r.go_to("usb0/"); r.m->key(KEY_RETURN);
        r.go_to("games/"); r.m->key(KEY_RETURN);
        r.go_to("beta.adf");
    };
    {   // the Library: the folder and the file come back
        Rig b(&gd); b.fs = world; b.set("Last screen");
        library(b);
        CHECK_STR(b.m->title(), "/usb0/games");
        b.close();
        b.m->open();
        CHECK_STR(b.m->title(), "/usb0/games");
        CHECK_STR(b.cur().c_str(), "beta.adf");
        CHECK_STR(b.m->status(), "");
    }
    {   // the file is gone: the folder's first row
        Rig b(&gd); b.fs = world; b.set("Last screen");
        library(b);
        b.close();
        CHECK(b.fs.remove("/usb0/games/beta.adf"));
        b.m->open();
        CHECK_STR(b.m->title(), "/usb0/games");
        CHECK_EQ(b.m->selected(), 0);
    }
    {   // the folder is gone: the nearest one above that lists
        Rig b(&gd); b.fs = world; b.set("Last screen");
        library(b);
        b.go_to("sub/"); b.m->key(KEY_RETURN);
        CHECK_STR(b.m->title(), "/usb0/games/sub");
        b.close();
        CHECK(b.fs.remove("/usb0/games/sub/x.adf"));
        CHECK(b.fs.remove("/usb0/games/alpha.adf"));
        CHECK(b.fs.remove("/usb0/games/beta.adf"));
        b.m->open();
        CHECK_STR(b.m->title(), "/usb0");
    }
    DONE("test_reopen");
}
