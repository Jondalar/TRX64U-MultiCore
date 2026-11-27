// T93 S3, T239.5 D4: the menu of the NES against its descriptor and a model of its core - a core that waits for a program cannot close its menu and opens on the browser, the Library lists the
// stick, "Run" on a .nes file closes the menu and starts the machine. Built as the NES's app (run.sh nesapp, TEST_APP_NES).
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "menuctl.h"
#include "cores.h"
#include "keyboard.h"
#include "lib_skel.h"
#include "ops_glue.h"

extern const CoreDesc nes_desc;                 // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)

struct Host : MenuHost {
    void info(int w, char *out, int n) { snprintf(out, n, "value %d", w); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no ICAP loader"); return false; }
};

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    Rig(const CoreDesc *d, const char *cfg)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        m = new MenuCtl(&c, &host);
    }
    int find_row(const char *s) { char l[100], r[100]; bool a; for (int i = 0; i < m->rows(); i++) { m->row(i, l, 100, r, 100, &a); if (!strcmp(l, s)) return i; } return -1; }
    void go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); }
};

int main()
{
    struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
    // --- a core that waits for a program cannot close its menu, and opens on the browser
    Rig n(&nes_desc, "/sd/cores/nes.cfg");
    n.st->load();
    CHECK_EQ(boot_start(&n.c, &ui), BOOT_WAITING_PROGRAM);
    n.m->open();
    CHECK(n.m->tree().a[n.m->area()].tabs[n.m->tab()].count == 1);   // it opens on the browser
    CHECK_STR(n.m->tree().a[n.m->area()].tabs[n.m->tab()].name, "Browse");
    CHECK(strstr(n.m->status(), ".nes") != 0);
    for (int k : { KEY_BREAK, KEY_ESCAPE, KEY_F10, KEY_SCRLOCK }) {
        CHECK_EQ(n.m->key(k), MenuCtl::R_REDRAW);
        CHECK(!n.m->closable());
    }
    std::vector<uint8_t> g(16 + 16384 + 8192, 0);
    memcpy(g.data(), "NES\x1A", 4); g[4] = 1; g[5] = 1;
    n.fs.put("/usb0/g/game.nes", g);
    n.m->open();                                                  // the Library lists what the stick holds now
    CHECK_STR(n.m->title(), "/");                                 // the media level
    n.go_to("usb0/"); n.m->key(KEY_RETURN);
    n.go_to("g/"); n.m->key(KEY_RETURN);
    CHECK_EQ(n.m->rows(), 2);                                     // ".." and game.nes
    n.go_to("game.nes");
    CHECK_EQ(n.m->key(KEY_RETURN), MenuCtl::R_REDRAW);            // the sheet: Run first, then the eight of every file
    CHECK(n.m->chooser());
    CHECK_EQ(n.m->key(KEY_RETURN), MenuCtl::R_CLOSE);             // Run: closes
    CHECK(n.m->closable());
    CHECK(n.c.program_loaded && n.c.started);
    CHECK_EQ(n.io.reset_releases, 1);
    DONE("test_nes_menuctl");
}
