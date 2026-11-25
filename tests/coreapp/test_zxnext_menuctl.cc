// T239.5 D4: the menu of the ZX Spectrum Next against its descriptor - the Library lists the stick, a .img file's sheet offers "Mount SD image and reset" first and "Mount SD image", the first one mounts it
// (the SD size register is in sectors, the card is present) and closes the menu. Built as the Next's app (run.sh zxnextapp, TEST_APP_ZXNEXT).
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "menuctl.h"
#include "cores.h"
#include "keyboard.h"
#include "lib_skel.h"
#include "ops_glue.h"
#include "zxnext_regs.h"

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
    Rig n(&zxnext_desc, "/sd/cores/zxnext.cfg");
    n.st->load();
    n.io.regs[ZXN_FEAT] = 0x01;
    n.fs.put("/usb0/g/next.img", std::vector<uint8_t>(512 * 64, 0x5A));
    n.m->open();
    n.m->key(KEY_F3);                                             // the Library lists what the stick holds
    CHECK_STR(n.m->title(), "/");                                 // the media level
    n.go_to("usb0/"); n.m->key(KEY_RETURN);
    n.go_to("g/"); n.m->key(KEY_RETURN);
    CHECK_EQ(n.m->rows(), 2);                                     // ".." and next.img
    n.go_to("next.img");
    CHECK_EQ(n.m->key(KEY_RETURN), MenuCtl::R_REDRAW);            // the sheet of the file: the two actions of the descriptor
    CHECK(n.m->chooser());
    CHECK_EQ(n.m->chooser_count() >= 2, 1);
    CHECK_STR(n.m->chooser_label(0), "Mount SD image and reset");
    CHECK_STR(n.m->chooser_label(1), "Mount SD image");
    CHECK_EQ(n.m->key(KEY_RETURN), MenuCtl::R_CLOSE);             // the first: mounts, resets, closes
    CHECK(n.m->closable());
    CHECK_EQ(n.io.regs[ZXN_SD_MOUNT], 1);
    uint32_t sz = 0; for (int i = 0; i < 4; i++) sz |= (uint32_t)n.io.regs[ZXN_SD_MOUNT + 4 + i] << (8 * i);
    CHECK_EQ(sz, 64u);
    DONE("test_zxnext_menuctl");
}
