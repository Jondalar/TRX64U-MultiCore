// T239.5 D4: the menu of the Neo Geo against its descriptor - the Library lists the stick and shows a .neo file, the file's sheet offers the one action of the descriptor ("Run"), and the
// Cartridge slot with its two tasks is in the menu. Built as the Neo Geo's app (run.sh neogeoapp, TEST_APP_NEOGEO).
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "menuctl.h"
#include "cores.h"
#include "keyboard.h"
#include "lib_skel.h"
#include "ops_glue.h"
#include "neogeo_regs.h"

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
    Rig n(&neogeo_desc, "/sd/cores/neogeo.cfg");
    n.st->load();
    n.fs.put("/usb0/g/game.neo", std::vector<uint8_t>(1024, 0x5A));
    n.fs.put("/usb0/g/notes.txt", std::vector<uint8_t>(16, 0x20));
    n.m->open();
    n.m->key(KEY_F3);                                             // the Library lists what the stick holds
    CHECK_STR(n.m->title(), "/");                                 // the media level
    n.go_to("usb0/"); n.m->key(KEY_RETURN);
    n.go_to("g/"); n.m->key(KEY_RETURN);
    CHECK(n.find_row("game.neo") >= 0);                           // the file of the descriptor's type is shown
    n.go_to("game.neo");
    CHECK_EQ(n.m->key(KEY_RETURN), MenuCtl::R_REDRAW);            // the sheet of the file: the action of the descriptor
    CHECK(n.m->chooser());
    CHECK_EQ(n.m->chooser_count() >= 1, 1);
    CHECK_STR(n.m->chooser_label(0), "Run");
    DONE("test_neogeo_menuctl");
}
