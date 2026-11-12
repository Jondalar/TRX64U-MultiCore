// T180 D10 (#40): the home folder of a core that is not the C64 (the A1200 descriptor). The application adds "Home Directory" and "Enter Home on Startup" to every core
// (home_extend), so "Set as home folder" in the F3 Library lands in that core's own .cfg, and with "Enter Home on Startup" = Yes the next F3 opens there; with No it opens on "/".
// A description that declares the items itself (the TRX64U menu) keeps its own and gets none added.
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

static const char *CFG = "/sd/cores/home.cfg";

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st; Host host; MenuCtl *m = 0;
    explicit Rig(const CoreDesc *d, FakeFs *shared = 0)
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, CFG, 0);
        st->load();
        c.settings = st;
    }
    void menu() { m = new MenuCtl(&c, &host); m->open(); }
    std::string left(int i) { char l[300], r[100]; bool a; m->row(i, l, 300, r, 100, &a); return l; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    bool go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return false; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); return true; }
};

static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcasecmp(d.settings[i].key, key) == 0;
    return n;
}

int main()
{
    static CoreDesc gd;
    static Setting gs[SETTINGS_MAX];
    CHECK_EQ(count_key(amiga1200_desc, HOME_DIR_KEY), 0);
    CHECK_EQ(count_key(amiga1200_desc, HOME_ENTER_KEY), 0);
    CHECK(!home_extend(&amiga1200_desc, &gd, gs));
    CHECK_EQ(gd.nsettings, amiga1200_desc.nsettings + 3);
    CHECK_EQ(count_key(gd, HOME_DIR_KEY), 1);
    CHECK_EQ(count_key(gd, HOME_ENTER_KEY), 1);

    {   // a description that declares the items itself keeps them: no second pair (the C64 / TRX64U menu)
        static CoreDesc d1, d2;
        static Setting s1[SETTINGS_MAX], s2[SETTINGS_MAX];
        CHECK(!home_extend(&amiga1200_desc, &d1, s1));
        CHECK(!home_extend(&d1, &d2, s2));
        CHECK_EQ(d2.nsettings, d1.nsettings);
        CHECK_EQ(count_key(d2, HOME_DIR_KEY), 1);
        CHECK_EQ(count_key(d2, HOME_ENTER_KEY), 1);
    }

    FakeFs world;
    world.put("/usb0/games/sub/x.adf", std::vector<uint8_t>(10, 1));
    world.put("/usb0/other/y.adf", std::vector<uint8_t>(10, 2));
    {   // Set as home folder: the sheet of a folder row, in /usb0/games
        Rig a(&gd, &world);
        a.menu();
        a.m->key(KEY_F3);
        CHECK_STR(a.m->title(), "/");
        a.go_to("usb0/"); a.m->key(KEY_RETURN);
        a.go_to("games/"); a.m->key(KEY_RETURN);
        CHECK_STR(a.m->title(), "/usb0/games");
        a.go_to("sub/");
        a.m->key(KEY_RIGHT);                                         // the sheet of the folder
        CHECK(a.m->chooser());
        a.go_to("Set as home folder");
        a.m->key(KEY_RETURN);
        CHECK_STR(a.m->status(), "Home folder is /usb0/games");
        CHECK(!a.m->status_bad());
        CHECK(a.fs.text(CFG).find("Home Directory=/usb0/games") != std::string::npos);
        CHECK(a.fs.text(CFG).find("until the core restarts") == std::string::npos);
        world = a.fs;                                                // the .cfg stays: the next start reads it
    }
    {   // the next start with "Enter Home on Startup" = Yes: F3 opens in the home folder
        Rig b(&gd, &world);
        CHECK_STR(b.st->text(b.st->find(HOME_DIR_KEY)), "/usb0/games");
        OpResult r;
        OpIn in[2] = { in_str("key", HOME_ENTER_KEY), in_str("value", "Enabled") };
        CHECK_EQ(ops_call(&b.c, "settings.set", in, 2, &r), OPE_OK);
        CHECK(b.fs.text(CFG).find("Enter Home on Startup=Enabled") != std::string::npos);
        b.menu();
        b.m->key(KEY_F3);
        CHECK_STR(b.m->title(), "/usb0/games");
        CHECK(b.find_row("sub/") >= 0);
        world = b.fs;
    }
    {   // negative control: with No the value is still stored, and F3 opens on "/"
        Rig n(&gd, &world);
        OpResult r;
        OpIn in[2] = { in_str("key", HOME_ENTER_KEY), in_str("value", "Disabled") };
        CHECK_EQ(ops_call(&n.c, "settings.set", in, 2, &r), OPE_OK);
        CHECK_STR(n.st->text(n.st->find(HOME_DIR_KEY)), "/usb0/games");
        n.menu();
        n.m->key(KEY_F3);
        CHECK_STR(n.m->title(), "/");
    }
    {   // T187 D7, T212 D1: both items are Storage on F7 (APP), not in the machine's F5, and F8 does not list them
        Rig f(&gd, &world);
        f.menu();
        f.m->key(KEY_F5);
        CHECK(f.find_row("Home directory") < 0 && f.find_row("Enter home on startup") < 0);
        f.m->key(KEY_F7);
        CHECK(f.find_row("Home directory") >= 0 && f.find_row("Enter home on startup") >= 0);
        CHECK(f.find_row("Storage") >= 0 && f.find_row("Storage") < f.find_row("Home directory"));   // the heading of the routed block
        f.m->key(KEY_F8);
        CHECK(f.find_row("Home directory") < 0 && f.find_row("Enter home on startup") < 0);
    }
    {   // T212 S9: Home Directory and Enter Home on Startup take the host's value (CFG_USERIF_HOME_DIR, CFG_USERIF_START_HOME) as default; Menu opens at has no host counterpart
        struct HostFlash : FlashLayer {
            bool get(const char *, const char *key, char *val, int n)
            {
                if (!strcmp(key, HOME_DIR_KEY)) { snprintf(val, n, "/usb0/host"); return true; }
                if (!strcmp(key, HOME_ENTER_KEY)) { snprintf(val, n, "Enabled"); return true; }
                return false;
            }
        } hf;
        FakeFs hfs;
        Settings hst(gd.settings, gd.nsettings, gd.store, &hfs, CFG, &hf);
        hst.load();
        int di = hst.find(HOME_DIR_KEY), ei = hst.find(HOME_ENTER_KEY), ri = hst.find(REOPEN_KEY);
        CHECK(di >= 0 && ei >= 0 && ri >= 0);
        CHECK_STR(hst.text(di), "/usb0/host");
        CHECK_EQ(hst.value(ei), 1);
        CHECK(hst.layer(di) == Settings::L_HOST && hst.layer(ei) == Settings::L_HOST && hst.layer(ri) == Settings::L_DEFAULT);
        CHECK_EQ(hst.value(ri), 0);
        CHECK(hst.set_text(di, "/usb0/own"));                          // the core's own value wins, and Reset to host defaults brings the host's back
        CHECK_STR(hst.text(di), "/usb0/own");
        CHECK(hst.reset_to_host());
        CHECK_STR(hst.text(di), "/usb0/host");
    }
    DONE("test_home");
}
