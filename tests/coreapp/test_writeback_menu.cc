// T154 S2 in the menu: the question when "Save Changed Cartridge" is Ask and the cartridge changed - the first thing on the screen when the menu opens,
// RETURN saves, the left arrow keeps the file as it is and the question does not come again for that change; Auto writes at the open, Off asks nothing.
// The menu is the real MenuCtl on the Atari descriptor and its model; the medium is a raw image with a fake source.
#include <string>
#include "check.h"
#include "sio_model.h"
#include "boot.h"
#include "menuctl.h"
#include "cores.h"
#include "keyboard.h"
#include "writeback.h"
#include "ops_glue.h"

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "-"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};
struct Port : WbPort {
    FakeIo *io;
    uint32_t ticks_ms() { return io->t; }
    void enter() { }
    void leave() { }
    void irq_attach(uint8_t, uint8_t (*)(uint8_t)) { }
    void irq_enable(uint8_t, bool) { }
};
static int src_read(void *u, uint32_t off, void *buf, uint32_t n) { memcpy(buf, (uint8_t *)u + off, n); return 0; }

struct Rig {
    SioModel io; FakeFs fs; PxRec px; Host host; Port port; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    std::vector<uint8_t> ram = std::vector<uint8_t>(2048, 0);
    WbRaw raw;
    Rig()
    {
        const CoreDesc *d = &atari800_desc;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/usb0/cores/atari800.cfg", 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
        fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
        st->load();
        struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
        if (boot_start(&c, &ui) != BOOT_RUNNING) { printf("FAIL boot\n"); g_fails++; }
        port.io = &io;
        wb_attach(&c, &port);                                       // first: the service still points at the previous rig's (freed) port until it is
        wb_unregister();
        raw.read = src_read; raw.user = ram.data(); raw.size = (uint32_t)ram.size();
        WbMedium w;
        memset(&w, 0, sizeof(w));
        snprintf(w.path, sizeof(w.path), "/usb0/saves/game.sav");
        snprintf(w.name, sizeof(w.name), "game.sav");
        w.write = wb_raw_write; w.user = &raw; w.may_create = true;
        wb_register(w);
        m = new MenuCtl(&c, &host);
    }
    std::string left(int i) { char l[200], r[100]; bool a; m->row(i, l, 200, r, 100, &a); return l; }
    std::string right(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return r; }
    void change() { ram[5]++; wb_mark(5, 1); io.delay_ms(10); }
    const std::vector<uint8_t> &file() { return fs.files[lower("/usb0/saves/game.sav")]; }
};

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);                            // a crash against a broken product still leaves its red lines
    // Ask: the question is the first row, RETURN writes
    {
        Rig r;
        wb_set_mode(WB_ASK);
        r.change();
        r.m->open();
        r.m->held_open();
        CHECK(r.m->flow_asking());
        CHECK(ops_state(&r.c, "writeback.ask") != 0 && std::string(ops_state(&r.c, "writeback.ask")).find("game.sav changed") == 0);     // T192.3 S3: the question is the module's state
        if (r.m->flow_asking()) {
            CHECK_EQ(r.m->rows(), 1);
            CHECK(r.left(0).find("game.sav changed") != std::string::npos);
            CHECK(r.right(0).find("RETURN") != std::string::npos);
            char foot[100];
            r.m->footer_legend(foot, sizeof(foot));
            CHECK(std::string(foot).find("YES") != std::string::npos);
            CHECK_EQ(r.m->key('x'), MenuCtl::R_NONE);              // another key is swallowed: the question stays
            CHECK(r.m->flow_asking());
            CHECK(r.file().empty());
        }
        r.m->key(KEY_RETURN);
        CHECK(!r.m->flow_asking());
        CHECK(ops_state(&r.c, "writeback.ask") == 0);
        CHECK_EQ(r.file().size(), 2048);
        CHECK_EQ(r.file()[5], 1);
        CHECK(!wb_pending());
    }
    // Ask, left arrow (ESC): the file is as it was and the question does not come again
    {
        Rig r;
        wb_set_mode(WB_ASK);
        r.change();
        r.m->open();
        r.m->held_open();
        CHECK(r.m->flow_asking());
        r.m->key(KEY_ESCAPE);
        CHECK(!r.m->flow_asking());
        CHECK(!r.fs.has("/usb0/saves/game.sav"));
        CHECK(!wb_pending());
        r.m->open();
        r.m->held_open();
        CHECK(!r.m->flow_asking());
    }
    // a function key leaves the question unanswered: it comes back at the next open
    {
        Rig r;
        wb_set_mode(WB_ASK);
        r.change();
        r.m->open();
        r.m->held_open();
        r.m->key(KEY_F2);
        CHECK(!r.m->flow_asking());
        CHECK(wb_pending());
        r.m->open();
        r.m->held_open();
        CHECK(r.m->flow_asking());
    }
    // Auto writes at the open without asking; Off asks nothing and writes nothing
    {
        Rig r;
        wb_set_mode(WB_AUTO);
        r.change();
        r.m->open();
        r.m->held_open();
        CHECK(!r.m->flow_asking());
        CHECK_EQ(r.file().size(), 2048);
        Rig o;
        wb_set_mode(WB_OFF);
        o.change();
        o.m->open();
        o.m->held_open();
        CHECK(!o.m->flow_asking());
        CHECK(!o.fs.has("/usb0/saves/game.sav"));
    }
    DONE("writeback_menu");
}
