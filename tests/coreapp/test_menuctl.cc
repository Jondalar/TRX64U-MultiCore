// T93 S3, S4, S5, D8: the keys of the menu against the Atari descriptor and a model of its core - pages, tasks that close the menu,
// settings (live vs at the next reset, written to the .cfg at once), the file browser with its extension filter and action chooser,
// the closing keys, a core that cannot close its menu before it has a program, "Reset to host defaults", "Return to host menu".
#include <string>
#include "check.h"
#include "sio_model.h"
#include "boot.h"
#include "menuctl.h"
#include "cores.h"
#include "atari_media.h"
#include "keyboard.h"
#include "lib_skel.h"
#include "ops_glue.h"

struct Host : MenuHost {
    int returns = 0; bool can = false;
    void info(int w, char *out, int n) { snprintf(out, n, "value %d", w); }
    bool return_to_host(char *msg, int n) { returns++; snprintf(msg, n, "no ICAP loader"); return can; }
};

static std::vector<uint8_t> atr()
{
    std::vector<uint8_t> v(16, 0);
    v[0] = 0x96; v[1] = 2; v[2] = 0x80; v[3] = 0x02; v[4] = 128;
    v.insert(v.end(), 80 * 128, 0);
    return v;
}

struct Rig {
    SioModel io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    Rig(const CoreDesc *d, const char *cfg = "/usb0/cores/atari800.cfg")
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
        fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
        fs.put("/usb0/games/a.atr", atr());
        fs.put("/usb0/games/b.ATR", atr());
        fs.put_text("/usb0/games/notes.txt", "x");
        fs.put("/usb0/games/p.xex", std::vector<uint8_t>{0xFF, 0xFF, 0, 6, 0, 6, 0x60});
        fs.put("/usb0/top.car", std::vector<uint8_t>(8192, 1));
        fs.put_text("/sd/readme.txt", "hi");
        m = new MenuCtl(&c, &host);
    }
    std::string left(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return l; }
    std::string right(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return r; }
    std::string sub(int i) { char l[100], r[100], s[100]; bool a; m->row(i, l, 100, r, 100, &a, s, 100); return s; }
    bool active(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return a; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    void go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); }
};

int main()
{
    // --- boot the machine first so tasks have something to act on
    Rig r(&atari800_desc);
    r.st->load();
    struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
    CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
    r.m->open();
    CHECK_EQ(r.m->area(), 0);
    CHECK_EQ(r.m->tab(), 0);
    CHECK_STR(r.m->title(), "Home");
    // the rows of every tab are the whitelist tree's, in its order (T104 5.8: F-key area, number tab)
    for (int a = 0; a < r.m->tree().nareas; a++) {
        r.m->key(KEY_F1 + 0);                                     // start from a known area each time
        static const int fkeys[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };
        int key = fkeys[r.m->tree().a[a].key[1] - '1'];
        CHECK_EQ(r.m->key(key), MenuCtl::R_REDRAW);
        CHECK_EQ(r.m->area(), a);
        for (int b = 0; b < r.m->tree().a[a].ntabs; b++) {
            if (r.m->tree().a[a].tabs[b].page) continue;             // T212: a page (the drive list) has no number
            CHECK_EQ(r.m->key('1' + b), MenuCtl::R_REDRAW);
            CHECK_EQ(r.m->tab(), b);
            const MenuTab &tb = r.m->tree().a[a].tabs[b];
            if (tb.page) continue;                                   // T212: a page (the drive list) has no number
            bool browser = tb.count == 1 && r.m->tree().e[tb.first].kind == EK_BROWSER;
            if (!browser) CHECK_EQ(r.m->rows(), tb.count);
        }
    }
    // an area the core does not have, and a tab number that is not printed: both do nothing
    CHECK_EQ(r.m->key(KEY_F2), MenuCtl::R_NONE);
    CHECK_EQ(r.m->key(KEY_F4), MenuCtl::R_NONE);
    CHECK_EQ(r.m->key('6'), MenuCtl::R_NONE);
    // --- tasks: Reset closes the menu and resets; a refused task keeps it open with the reason (T212 D7: the reset tasks stand on Home)
    r.m->key(KEY_F1);
    r.io.events.clear();
    r.go_to("Reset");
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK_STR(r.io.events.front().c_str(), "mode_reset");
    CHECK_STR(r.io.events.back().c_str(), "mode_unreset");
    r.fs.files.erase("/usb0/roms/atari800/atarixl.rom");
    r.go_to("Reload ROMs, cold start");
    r.io.events.clear();
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);          // not closed
    CHECK(r.m->status_bad() && strstr(r.m->status(), "ATARIXL.ROM") != 0);
    CHECK(r.io.events.empty());                                  // and the machine was not touched
    r.fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
    // RETURN on an empty drive row opens its list (T212 D5), it is no task
    r.m->key(KEY_F1);
    r.go_to("D2:");
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    r.m->key(KEY_ESCAPE);
    // media rows show what is in them: on Home, below the reset tasks
    r.m->key(KEY_F1);
    CHECK(r.right(r.find_row("D1:")).find("Empty") == 0);

    // --- the closing keys
    for (int k : { KEY_BREAK, KEY_ESCAPE, KEY_F10, KEY_SCRLOCK, KEY_MENU }) {
        r.m->open();
        CHECK_EQ(r.m->key(k), MenuCtl::R_CLOSE);
    }
    CHECK_EQ(r.m->key('x'), MenuCtl::R_NONE);

    // --- settings: right/left/return change, the .cfg is written at once, live items apply at once, reset-time items wait
    r.m->open();
    r.m->key(KEY_F5); r.m->key('1');                              // T205 D1: the core's own tab
    CHECK(r.left(0) == "RAM size");
    CHECK(r.right(0) == "64 KB" && r.sub(0) == "" && r.m->row_reset(0));       // T187 D9: one line, the ring marks it
    r.io.regs[A8_RAM_SEL] = 0;
    CHECK_EQ(r.m->key(KEY_RIGHT), MenuCtl::R_REDRAW);
    CHECK(r.right(0) == "128 KB" && r.sub(0) == "" && r.m->row_reset(0));       // T187 D9: one line, the ring marks it
    CHECK_EQ(r.io.regs[A8_RAM_SEL], 0);                           // written at the next reset, not now
    CHECK(r.m->status() != 0 && strstr(r.m->status(), "next reset") != 0);
    CHECK(r.fs.text("/usb0/cores/atari800.cfg").find("RAM size=128 KB") != std::string::npos);
    r.m->key(KEY_DOWN); r.m->key(KEY_DOWN);                       // Joystick ports (live)
    CHECK(r.left(r.m->selected()) == "Joystick ports");
    r.io.regs[A8_FLAGS] = 0;
    r.m->key(KEY_RETURN);
    CHECK_EQ(r.io.regs[A8_FLAGS] & A8_FLAG_SWAP, A8_FLAG_SWAP);   // live: in the register now
    r.m->key(KEY_LEFT);                                           // back
    CHECK_EQ(r.io.regs[A8_FLAGS] & A8_FLAG_SWAP, 0);
    r.m->key(KEY_DOWN);                                           // CPU speed
    r.m->key(KEY_LEFT);                                           // wraps to 8x
    CHECK_EQ(r.io.regs[A8_THROTTLE], 8);
    // a write protected card: the value holds, the screen says it cannot be saved
    r.fs.read_only = true;
    r.m->key(KEY_RIGHT);
    CHECK(r.m->status_bad() && strstr(r.m->status(), "cannot be saved") != 0);
    CHECK_EQ(r.io.regs[A8_THROTTLE], 1);
    r.fs.read_only = false;
    // Reset to host defaults: the cursor only selects, RETURN does it (Alex, board 2026-10-16: moving
    // onto the row fired it, which is the bug - "Einmal return reicht")
    r.m->key(KEY_F8);
    r.go_to("Reset to host defaults");
    CHECK(r.fs.has("/usb0/cores/atari800.cfg"));                  // arriving on the row does nothing
    r.m->key(KEY_UP);                                            // (T187 D8: the reset is the last row of F8, so away is up)
    r.m->key(KEY_DOWN);
    CHECK(r.fs.has("/usb0/cores/atari800.cfg"));                  // and neither does moving away and back
    r.m->key(KEY_RETURN);
    CHECK(r.fs.has("/usb0/cores/atari800.cfg"));                  // RETURN asks first (the firmware asks "Are you sure to clear settings?")
    r.m->key(KEY_ESCAPE);
    CHECK(r.fs.has("/usb0/cores/atari800.cfg"));                  // <- says no
    r.m->key(KEY_RETURN);
    r.m->key(KEY_RETURN);
    CHECK(!r.fs.has("/usb0/cores/atari800.cfg"));                 // the second RETURN says yes
    CHECK_EQ(r.st->value(0), 0);
    CHECK_EQ(r.io.regs[A8_THROTTLE], 1);
    // T212 D7: Home has no information lines any more (Core, Network, Media went; Build, ROMs, Libraries stand in F8)
    r.m->key(KEY_F1);
    CHECK(r.find_row("Core") < 0 && r.find_row("Media") < 0);
    r.m->key(KEY_F8);
    CHECK(!r.active(r.find_row("Build")));
    // Return to host menu: asks the application; a failure stays on screen
    r.m->key(KEY_F8);
    r.go_to("Return to host menu");
    CHECK_EQ(r.host.returns, 0);                                  // arriving on the row does not leave the core
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK_EQ(r.host.returns, 1);
    CHECK(r.m->status_bad() && strstr(r.m->status(), "no ICAP") != 0);

    // --- the file browser: the Library opens on the media level like the host's File Browser (T104.2, Alex 2026-10-29): every medium is a row
    { MediumInfo mi; memset(&mi, 0, sizeof(mi)); mi.ready = true; mi.writable = true; snprintf(mi.size, sizeof(mi.size), "512 MiB"); snprintf(mi.contents, sizeof(mi.contents), "FAT32  'STICK'"); r.fs.media["usb0"] = mi; }
    r.m->key(KEY_F3);
    CHECK_STR(r.m->title(), "/");
    CHECK(r.find_row("../") < 0);
    CHECK(r.find_row("usb0/") >= 0 && r.find_row("sd/") >= 0);
    auto brief = [&](const char *row) { MenuCtl::BrowseRow br; r.m->browse_row(r.find_row(row), &br); return std::string(br.sub); };
    CHECK(brief("usb0/") == "512 MiB  FAT32  'STICK'");               // what the medium is, as the host's File Browser says it
    CHECK(brief("sd/") == "Medium");                                  // the board cannot tell: the word
    CHECK(r.m->media_level());
    {                                                                 // the info panel of the host's File Browser: Size, Contents, Access of the medium under the cursor
        MediumInfo mi;
        CHECK(r.m->medium_at(r.find_row("usb0/"), &mi) && mi.ready && mi.writable);
        CHECK_STR(mi.size, "512 MiB");
        CHECK_STR(mi.contents, "FAT32  'STICK'");
        CHECK(!r.m->medium_at(r.find_row("sd/"), &mi));                 // no answer from the board
    }
    CHECK(r.find_row("[Recent]/") < 0 && r.find_row("[Favorites]/") < 0);     // T180 D3: no [Recent] and no [Favorites] in the Library, in any core
    CHECK(r.find_row("[Home]/") < 0);                                         // [Home] only once a home folder is set
    r.m->key(KEY_LEFT);                                               // nothing above the media level
    CHECK_STR(r.m->title(), "/");
    r.go_to("sd/"); r.m->key(KEY_RETURN);
    CHECK_STR(r.m->title(), "/sd");
    CHECK(r.find_row("../") == 0);
    r.go_to("../"); r.m->key(KEY_RETURN);                              // RETURN on ../ at the top of a medium: the media level
    CHECK_STR(r.m->title(), "/");
    r.go_to("usb0/"); r.m->key(KEY_RETURN);
    CHECK_STR(r.m->title(), "/usb0");
    if (g_fails) {
    DONE("test_menuctl");                                      // the media level is wrong: what follows stands on it
    }
    CHECK(r.find_row("../") == 0);
    CHECK(r.find_row("top.car") >= 0);
    CHECK(r.find_row("roms/") >= 0);
    r.go_to("games/"); r.m->key(KEY_RETURN);
    CHECK_STR(r.m->title(), "/usb0/games");
    CHECK(r.find_row("a.atr") >= 0 && r.find_row("b.ATR") >= 0 && r.find_row("p.xex") >= 0);
    CHECK(r.find_row("notes.txt") >= 0);                          // every file is listed: the ones no action knows still have View, Copy, Rename, Delete
    // RETURN on a file opens its sheet: the type's actions (an .atr has five: the boot first, then the swap - T108 D6), then the eight of every file
    r.go_to("a.atr");
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK(r.m->chooser());
    CHECK_EQ(r.m->rows(), 5 + 8);
    CHECK(r.left(0) == "Mount D1: and cold start");
    CHECK(r.left(1) == "Mount D1:");
    CHECK(r.left(2) == "Mount D2:");
    CHECK(r.left(5) == "View" && r.left(12) == "Delete");       // T180 D3: the sheet ends with Delete, no Add to Favorites after it
    r.m->key(KEY_ESCAPE);                                          // Esc leaves the chooser first, the menu stays
    CHECK(!r.m->chooser());
    r.m->key(KEY_RETURN);
    r.m->key(KEY_DOWN);
    r.io.events.clear();
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);             // Mount D1:: the menu stays up and the machine is not reset
    CHECK(a8_state(&r.c)->disk[0] != 0 && a8_state(&r.c)->disk[1] == 0);
    CHECK(r.io.events.empty());
    CHECK(!r.m->chooser());
    r.m->key(KEY_RETURN);
    r.m->key(KEY_DOWN); r.m->key(KEY_DOWN);
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);             // Mount D2:: the menu stays up
    CHECK(a8_state(&r.c)->disk[1] != 0 && a8_state(&r.c)->disk[0] != 0);
    CHECK(!r.m->chooser());
    CHECK(strstr(r.m->status(), "a.atr") != 0);
    // an .xex has one action: its sheet opens like any, the first row runs it and closes the menu
    r.go_to("p.xex");
    r.io.events.clear();
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK(r.m->chooser());
    CHECK_EQ(r.m->rows(), 1 + 8);
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK_STR(r.io.events.back().c_str(), "mode_unreset");
    {   // T212 D11: the mounted cartridge's list has Eject first; it removes the cartridge with a cold start and closes the menu (no task on Home for it)
        a8_state(&r.c)->cart_mode = 0x21;                           // a cartridge is in (its insertion is test_atari_media)
        snprintf(a8_state(&r.c)->cart_name, sizeof(a8_state(&r.c)->cart_name), "g.car");
        r.m->open(); r.m->key(KEY_F1);
        CHECK(r.find_row("Remove cartridge, cold start") < 0);
        r.go_to("Cartridge");
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK_STR(r.m->tab_id(), "drive");
        CHECK(r.left(0) == "Eject");
        r.io.events.clear();
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_CLOSE);
        CHECK(a8_state(&r.c)->cart_mode == 0);
        CHECK(!r.io.events.empty());                               // the cold start touched the machine
    }
    // a refused file: the reason, the menu stays (the Library opens again in the folder of the last file)
    r.fs.put("/usb0/games/junk.car", std::vector<uint8_t>(5000, 0));
    r.m->open(); r.m->key(KEY_F3);
    CHECK_STR(r.m->title(), "/usb0/games");
    r.go_to("junk.car");
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK(r.m->chooser());
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK(r.m->status_bad() && strstr(r.m->status(), "5000") != 0);
    CHECK(r.m->chooser());                                         // the refusal leaves the sheet up; LEFT leaves it first
    r.m->key(KEY_LEFT);
    CHECK(!r.m->chooser());
    // up again: LEFT, and at the top of a medium LEFT goes to the media level (T104.2)
    r.m->key(KEY_LEFT);
    CHECK_STR(r.m->title(), "/usb0");
    r.m->key(KEY_LEFT);
    CHECK_STR(r.m->title(), "/");                                  // the top of a medium: up to the media level, no message about a top
    CHECK(r.m->status() == 0 || strstr(r.m->status(), "top of the medium") == 0);

    // --- "Move to...": the file lands in the folder the picker shows, to the exact path - into the current folder and into a subfolder (board 2026-11-01:
    // files moved with the target picked "landed one level too high"). The picker's row 0 is "Move here"; a folder row is entered, never taken.
    {
        // T180 D6: the Library's file operations are registered by the common part (ops_register_base) for every core: the Atari descriptor carries none of its own
        auto has = [&](const char *p) { uint64_t sz; bool d; return r.fs.stat(p, &sz, &d) && !d; };
        auto move_via_sheet = [&](const char *file) {
            r.go_to(file);
            r.m->key(KEY_RETURN);                                    // the sheet of the file
            r.go_to("Move to...");
            r.m->key(KEY_RETURN);                                    // the folder picker, on the folder the Library is in
            CHECK(r.m->picking());
        };
        auto finish = [&]() { r.m->key(KEY_RETURN); int n = 0; while (r.m->tick() && ++n < 100) { } };
        r.fs.mkdir("/usb0/games/sub");
        r.m->key(KEY_F3);
        while (strcmp(r.m->title(), "/")) r.m->key(KEY_LEFT);
        r.go_to("usb0/"); r.m->key(KEY_RETURN);
        r.go_to("games/"); r.m->key(KEY_RETURN);
        CHECK_STR(r.m->title(), "/usb0/games");
        // into a subfolder: DOWN to "sub" (rows: Move here, .., sub), RETURN enters it, row 0 takes it
        move_via_sheet("notes.txt");
        r.m->key(KEY_DOWN); r.m->key(KEY_DOWN);
        r.m->key(KEY_RETURN);
        CHECK(r.m->picking() && has("/usb0/games/notes.txt") && !has("/usb0/games/sub/notes.txt"));      // entering moved nothing
        finish();
        CHECK(!r.m->picking() && has("/usb0/games/sub/notes.txt") && !has("/usb0/games/notes.txt") && !has("/usb0/notes.txt"));
        // into the folder above: LEFT in the picker, row 0 takes it
        while (strcmp(r.m->title(), "/")) r.m->key(KEY_LEFT);
        r.go_to("usb0/"); r.m->key(KEY_RETURN);
        r.go_to("games/"); r.m->key(KEY_RETURN);
        move_via_sheet("p.xex");
        r.m->key(KEY_LEFT);                                              // the picker shows /usb0
        finish();
        CHECK(has("/usb0/p.xex") && !has("/usb0/games/p.xex") && !has("/p.xex"));
    }

    // --- spec 5.14: media that mounts after the menu opened. The browser's directory is read again, the
    // selection follows its entry by name, and an unchanged directory reports nothing (the negative control).
    Rig u(&atari800_desc);
    u.st->load();
    u.m->open();
    u.m->key(KEY_F3);
    u.go_to("usb0/"); u.m->key(KEY_RETURN);                       // /usb0, listed before the file is there
    CHECK_STR(u.m->tree().a[u.m->area()].tabs[u.m->tab()].name, "Browse");
    CHECK(u.find_row("late.atr") < 0);
    CHECK(!u.m->relist());                                        // nothing changed: no redraw
    u.go_to("top.car");
    std::string sel_before = u.left(u.m->selected());
    u.fs.put("/usb0/late.atr", atr());                            // the stick finishes mounting
    CHECK(u.m->relist());
    CHECK(u.find_row("late.atr") >= 0);
    std::string sel_after = u.left(u.m->selected());
    CHECK_STR(sel_after.c_str(), sel_before.c_str());             // the cursor stayed on its entry
    CHECK(!u.m->relist());
    u.m->key(KEY_F1);                                             // off the browser: nothing to read again
    u.fs.put("/usb0/later.atr", atr());
    CHECK(!u.m->relist());
    // --- T239.3 D41: a folder that a job still fetches shows "Loading ..." as its last line, is read again at once while it loads, and fills when the job ends; a failure says its reason there
    {
        Rig w(&atari800_desc);
        w.st->load();
        w.m->open();
        w.m->key(KEY_F3);
        w.fs.put_size("/usb0/slow/one.atr", 100);
        w.fs.slow_dir = "/usb0/slow";
        w.fs.slow_n = 2;
        w.go_to("usb0/"); w.m->key(KEY_RETURN);
        w.go_to("slow/"); w.m->key(KEY_RETURN);                       // the folder is entered while its job runs
        CHECK(w.find_row("one.atr") < 0);
        CHECK(w.find_row("Loading ...") >= 0);                        // the wait line, no entry
        CHECK(w.m->rows() == 2);                                      // ".." and the line
        CHECK(!w.m->relist());                                        // second read: still loading, the line stays and nothing changed: no redraw
        CHECK(w.find_row("Loading ...") >= 0);
        CHECK(w.m->relist());                                         // the job has ended: the folder fills, the line is gone
        CHECK(w.find_row("one.atr") >= 0);
        CHECK(w.find_row("Loading ...") < 0);
        CHECK(!w.m->relist());
        // the other ending: the job failed, its reason is the last line and stands on the status row once
        w.fs.slow_n = 1;
        w.fs.slow_why = "The server gave no files";
        w.m->key(KEY_LEFT);
        w.go_to("slow/"); w.m->key(KEY_RETURN);
        CHECK(w.find_row("Loading ...") >= 0);
        CHECK(w.m->relist());                                         // loading ended with the reason
        CHECK(w.find_row("The server gave no files") >= 0);
        CHECK(w.find_row("Loading ...") < 0);
        CHECK(w.find_row("one.atr") < 0);
        int lists = w.fs.slow_asked;
        CHECK(!w.m->relist());                                        // a failed folder is asked once: no new read, the reason stays
        CHECK(!w.m->relist());
        CHECK_EQ(w.fs.slow_asked, lists);
        CHECK(w.find_row("The server gave no files") >= 0);
        w.m->key(KEY_LEFT);                                           // out and in again: asked again, the folder fills
        w.go_to("slow/"); w.m->key(KEY_RETURN);
        CHECK(w.find_row("one.atr") >= 0);
    }
    // --- the media level is the way between media in every layout: RETURN on ../ and LEFT at the top of a medium lead to it, a medium's row opens it
    {
        Rig w(&atari800_desc);
        w.st->load();
        w.m->open();
        w.m->key(KEY_F3);
        CHECK(!w.m->screen());                                    // this core's Library is the plain list
        CHECK_STR(w.m->title(), "/");                             // it opens on the media level
        CHECK(lib_medium(w.m, "SD"));
        CHECK_STR(w.m->title(), "/sd");
        CHECK(w.find_row("readme.txt") >= 0);
        w.m->key(KEY_LEFT);                                       // LEFT at the top of the medium
        CHECK_STR(w.m->title(), "/");
        CHECK(lib_medium(w.m, "usb0"));                           // and the other one
        CHECK_STR(w.m->title(), "/usb0");
    }
    // T256 D13 (review): goto_screen with the menu closed lands on its place: open() takes the goto, it does not start at Home
    {
        Rig w(&atari800_desc);
        w.st->load();
        w.c.menu_open = false;
        CHECK(w.m->goto_when_open("F3"));
        CHECK(!w.m->goto_when_open("F9"));                        // no such place: refused at once
        w.m->say_when_open("Opening x ...", false);
        w.c.menu_open = true;
        w.m->open();
        CHECK_EQ(w.m->area(), 1);                                 // F3, not Home
        CHECK_STR(w.m->status(), "Opening x ...");
        w.m->open();                                              // taken once: the next open is at Home again
        CHECK_EQ(w.m->area(), 0);
        w.m->key(KEY_F2);
        CHECK(w.m->goto_when_open("F3"));                         // open menu: at once
        CHECK_EQ(w.m->area(), 1);
    }
    // T239.3 D41 (review): a file the medium is still fetching: the action stays pending, the status row says so, pump() opens again every 300 ms; leaving the folder cancels
    {
        Rig w(&atari800_desc);
        w.st->load();
        w.m->open();
        w.m->key(KEY_F3);
        w.go_to("usb0/"); w.m->key(KEY_RETURN);
        w.go_to("games/"); w.m->key(KEY_RETURN);
        w.fs.loading_path = "/usb0/games/notes.txt";
        w.fs.loading_n = 3;
        w.go_to("notes.txt");
        w.m->key(KEY_RIGHT);                                      // View: the file is not there yet
        CHECK(!w.m->viewer() && w.m->action_pending());
        CHECK_STR(w.m->status(), "Downloading the file: open it again in a moment");
        CHECK_EQ(w.fs.loading_opens, 1);
        w.io.t += 100;
        w.m->pump(true);
        CHECK_EQ(w.fs.loading_opens, 1);                          // 300 ms have not passed
        w.io.t += 300;
        w.m->pump(true);
        CHECK(w.fs.loading_opens == 2 && w.m->action_pending() && !w.m->viewer());
        w.io.t += 300;
        w.m->pump(true);
        CHECK(w.fs.loading_opens == 3 && w.m->action_pending());
        w.io.t += 300;
        w.m->pump(true);                                         // the file is there: the action runs once
        CHECK(w.m->viewer() && !w.m->action_pending());
        CHECK_EQ(w.fs.loading_opens, 3);
        w.m->key(KEY_LEFT);                                       // closes the viewer
        w.fs.loading_n = 5;
        w.fs.loading_opens = 0;
        w.m->key(KEY_RIGHT);
        CHECK(w.m->action_pending() && w.fs.loading_opens == 1);
        w.m->key(KEY_LEFT);                                       // leaves the folder: cancelled
        w.io.t += 300;
        w.m->pump(true);
        CHECK(!w.m->action_pending() && w.fs.loading_opens == 1 && !w.m->viewer());
        w.io.t += 300;
        w.m->pump(true);
        CHECK_EQ(w.fs.loading_opens, 1);                          // nothing is asked for any more
    }
    // the same for "Copy to...": the job waits for the source with the cancel at hand, then copies it once
    {
        Rig w(&atari800_desc);
        w.st->load();
        w.fs.mkdir("/usb0/games/sub");
        w.m->open();
        w.m->key(KEY_F3);
        w.go_to("usb0/"); w.m->key(KEY_RETURN);
        w.go_to("games/"); w.m->key(KEY_RETURN);
        w.fs.loading_path = "/usb0/games/notes.txt";
        w.fs.loading_n = 2;
        w.go_to("notes.txt");
        w.m->key(KEY_RETURN);
        w.go_to("Copy to...");
        w.m->key(KEY_RETURN);
        CHECK(w.m->picking());
        w.m->key(KEY_DOWN); w.m->key(KEY_DOWN);
        w.m->key(KEY_RETURN);                                     // into sub
        w.m->key(KEY_RETURN);                                     // take it
        int n = 0;
        while (w.m->tick() && ++n < 1000) {
            w.io.t += 100;
        }
        uint64_t sz; bool d;
        CHECK(w.fs.stat("/usb0/games/sub/notes.txt", &sz, &d) && !d && sz == 1);
        CHECK_EQ(w.fs.loading_opens, 2);                          // asked twice while it was fetched, opened once after
    }
    DONE("test_menuctl");
}
