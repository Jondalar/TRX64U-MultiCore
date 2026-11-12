// T180 D2 and D5 (docs/specs/T180-media-housekeeping.md, scenarios S2, S3 and S8): "Persist medium mounts" keeps the mounted media in the core's .cfg and the next start
// mounts them before the core is released; a medium that is gone leaves its slot empty with a note; a media set is the same keys in <core folder>/media/<name>.mnt, and
// loading one writes everything back, mounts what the set holds, unmounts the rest and resets the core once. The machine is the A1200's record on the model of its core:
// four floppy slots, two hard disk units (an image and a folder disk), with a .cfg and a core folder in the fake file system. A "restart" is a new application run on the
// same files: new engines, new settings read from the .cfg, boot_start.
#include <map>
#include "folderhd_bench.h"
#include "boot.h"
#include "engines.h"
#include "mediamounts.h"
#include "menuctl.h"
#include "keyboard.h"
#include "video.h"

using namespace bench;

#define CFG "/usb0/cores/amiga/amiga.cfg"
#define SETDIR "/usb0/cores/amiga/configs"

static std::vector<uint8_t> pat(uint32_t seed, size_t n)
{
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        v[i] = (uint8_t)(x >> 24);
    }
    return v;
}

static void make_tree(FakeFs &fs, const std::string &root)
{
    fs.mkdir((root + "/Empty").c_str());
    for (int i = 0; i < 40; i++) {
        char nm[64];
        snprintf(nm, sizeof(nm), "%s/file%02d.%s", i < 14 ? "/C" : (i < 28 ? "/S" : ""), i, i % 3 ? "dat" : "txt");
        fs.put(root + nm, pat((uint32_t)i, (size_t)((i * 997 + 13) % 6000)));
    }
}

// what the core saw when the application released it: the mount registers of DF0 and HD0
static int g_holds, g_releases;
static int g_first_df0, g_first_hd0, g_first_hd1;
static AmigaIo *g_io;
static int hold_nop(CoreCtx *, char *, int) { g_holds++; return 0; }
static int release_hook(CoreCtx *, char *, int)
{
    if (g_releases++ == 0 && g_io) {
        g_first_df0 = g_io->regs[AM_FLOPPY_MOUNT] & 1;
        g_first_hd0 = g_io->regs[AM_HDF_MOUNT] & 1;
        g_first_hd1 = g_io->regs[AM_HDF_MOUNT + 8] & 1;
    }
    return 0;
}
static int nop(CoreCtx *, char *, int) { return 0; }

static CoreDesc make_desc()
{
    CoreDesc d = amiga1200_desc;
    d.init = hold_nop;
    d.release = release_hook;
    d.warm_reset = nop;
    d.cold_start = 0;
    d.reset_ms = 1;
    d.nroms = 0;
    d.romset = 0;
    static Setting items[SETTINGS_MAX];
    CoreDesc ext;
    const char *why = persist_extend(&d, &ext, items);
    if (why) {
        printf("FAIL persist_extend: %s\n", why);
        g_fails++;
    }
    return ext;
}

struct Ui : BootUi {
    std::vector<std::string> msgs;
    int clears = 0;
    void message(const char *, const char *text, bool) { msgs.push_back(text); }
    void clear() { clears++; }
    void tick() { }
    bool said(const char *s) const { for (auto &m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
};

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "-"); }
    bool return_to_host(char *, int) { return true; }
};

// One run of the application on a file system: the descriptor with the Persist item, the settings read from the .cfg, boot_start.
struct R {
    FakeFs &fs;
    CoreDesc desc;
    AmigaRig *r;
    Ui ui;
    Host host;
    Video *vid;
    MenuCtl *m = 0;
    explicit R(FakeFs &f) : fs(f)
    {
        desc = make_desc();
        r = new AmigaRig(&fs, &desc);
        r->io.regs[AM_FEAT] = 0x01;
        delete r->st;
        r->st = new Settings(desc.settings, desc.nsettings, desc.store, &fs, CFG, 0);
        r->st->load();
        r->c.settings = r->st;
        snprintf(r->c.cfg_path, sizeof(r->c.cfg_path), "%s", CFG);
        vid = new Video(desc.video, &r->px, &r->io);
        r->c.video = vid;
        g_io = &r->io;
        g_holds = g_releases = 0;
        g_first_df0 = g_first_hd0 = g_first_hd1 = -1;
    }
    ~R() { delete m; delete vid; delete r; }
    CoreCtx *c() { return &r->c; }
    void boot() { CHECK_EQ(boot_start(c(), &ui), BOOT_RUNNING); }
    int set(const char *key, const char *value)
    {
        OpIn in[2] = { in_str("key", key), in_str("value", value) };
        OpResult res;
        return ops_call(c(), "settings.set", in, 2, &res);
    }
    void persist(const char *v) { CHECK_EQ(set(PERSIST_KEY, v), OPE_OK); }
    bool mounted_df(int n) { return win_name(c(), n) != 0; }
    const char *df(int n) { const char *s = win_name(c(), n); return s ? s : "(empty)"; }
    const char *hd(int n) { const char *s = sec_name(c(), n); return s ? s : "(empty)"; }
    const char *spec(int idx) { const char *s = mounts_slot_spec(c(), idx); return s ? s : "(empty)"; }
    uint64_t hd_size(int n) { return c()->eng->sec.unit[n].size; }
    void open_menu() { m = new MenuCtl(c(), &host); m->open(); }
    std::string left(int i) { char l[300], rr[200]; bool a; m->row(i, l, 300, rr, 200, &a); return l; }
    std::string right(int i) { char l[200], rr[200]; bool a; m->row(i, l, 200, rr, 200, &a); return rr; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    bool go_to(const char *s)
    {
        int row = find_row(s);
        if (row < 0) { printf("FAIL no row %s\n", s); g_fails++; return false; }
        while (m->selected() > row) m->key(KEY_UP);
        while (m->selected() < row) m->key(KEY_DOWN);
        return true;
    }
    void type(const char *text) { for (const char *p = text; *p; p++) m->key((unsigned char)*p); }
};

static const OpIn *name_in(const char *name)
{
    static OpIn in[1];
    in[0] = in_str("name", name);
    return in;
}

static bool has(const std::string &text, const char *s) { return text.find(s) != std::string::npos; }
static int count(const std::string &text, const char *s)
{
    int n = 0;
    for (size_t p = text.find(s); p != std::string::npos; p = text.find(s, p + 1)) n++;
    return n;
}

static void world(FakeFs &fs)
{
    fs.put("/usb0/a.adf", pat(1, AM_ADF_SIZE));
    fs.put("/usb0/b.adf", pat(2, AM_ADF_SIZE));
    fs.put("/usb0/h.hdf", pat(3, 64 * 512));
    fs.put("/usb0/g.hdf", pat(4, 64 * 512));
    make_tree(fs, "/usb0/hd");
    make_tree(fs, "/usb0/hd2");
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    char msg[240];

    // ---- S2: persist on: DF0 and an HD0 folder (and HD1 an image), restart: all back, same paths and sizes, there when the core is released
    {
        FakeFs fs;
        world(fs);
        uint64_t size0;
        std::string spec0, spec1, specd;
        {
            R a(fs);
            a.persist("Yes");
            a.boot();
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(sec_mount_folder(a.c(), 0, "/usb0/hd/0.1", msg, sizeof(msg)), 0);
            CHECK_EQ(sec_mount(a.c(), 1, "/usb0/h.hdf", msg, sizeof(msg)), 0);
            std::string cfg = fs.text(CFG);
            CHECK(has(cfg, "Persist medium mounts=Yes"));
            CHECK(has(cfg, "Mount DF0=/usb0/a.adf"));
            CHECK(has(cfg, "Mount HD0=folder /usb0/hd, 0.1 GB"));        // the folder keeps the size as it was typed
            CHECK(has(cfg, "Mount HD1=/usb0/h.hdf"));
            CHECK(!has(cfg, "Mount DF1"));
            size0 = a.hd_size(0);
            spec0 = a.spec(0);
            specd = a.spec(4);
            spec1 = a.spec(5);
        }
        {
            R b(fs);                                                     // the next start: nothing mounted yet
            CHECK(!b.mounted_df(0) && !b.c()->eng);
            b.boot();
            CHECK_STR(b.df(0), "a.adf");
            CHECK_STR(b.hd(0), "hd (folder)");
            CHECK_STR(b.hd(1), "h.hdf");
            CHECK_EQ((long long)b.hd_size(0), (long long)size0);         // the same size, not the default GB
            CHECK_STR(b.spec(0), spec0.c_str());
            CHECK_STR(b.spec(4), specd.c_str());
            CHECK_STR(b.spec(5), spec1.c_str());
            CHECK_EQ(g_releases, 1);
            CHECK_EQ(g_first_df0, 1);                                    // the core saw them at its first cycle: mounted before the release
            CHECK_EQ(g_first_hd0, 1);
            CHECK_EQ(g_first_hd1, 1);
            CHECK(b.ui.said("Mounting the folder..."));                  // not a black screen while the tree is read
            CHECK(b.ui.clears >= 1);
            CHECK(!b.c()->notice_pending);
            printf("S2: DF0, HD0 folder (%llu bytes) and HD1 are back at the first cycle\n", (unsigned long long)size0);
        }
        {   // a change is stored when it happens: replace, eject, and the next start follows
            R a(fs);
            a.boot();
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/b.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(sec_eject(a.c(), 1, msg, sizeof(msg)), 0);
            CHECK_EQ(win_mount(a.c(), 2, "/usb0/a.adf", msg, sizeof(msg)), 0);
            std::string cfg = fs.text(CFG);
            CHECK(has(cfg, "Mount DF0=/usb0/b.adf") && !has(cfg, "Mount DF0=/usb0/a.adf"));
            CHECK(!has(cfg, "Mount HD1"));
            CHECK(has(cfg, "Mount DF2=/usb0/a.adf"));
            CHECK_EQ(count(cfg, "Mount "), 3);                           // DF0, DF2, HD0
            R b(fs);
            b.boot();
            CHECK_STR(b.df(0), "b.adf");
            CHECK_STR(b.df(2), "a.adf");
            CHECK_STR(b.hd(1), "(empty)");
            CHECK_STR(b.hd(0), "hd (folder)");
            CHECK(!b.mounted_df(1));
        }
    }

    // ---- S2: persist off (the default): nothing stored, nothing mounted; switching it off removes the keys; a stale key does not act
    {
        FakeFs fs;
        world(fs);
        {
            R a(fs);
            a.boot();
            CHECK_EQ(a.set(PERSIST_KEY, "No"), OPE_OK);
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(sec_mount(a.c(), 1, "/usb0/h.hdf", msg, sizeof(msg)), 0);
            CHECK(!has(fs.text(CFG), "Mount "));
        }
        {
            R b(fs);
            b.boot();
            CHECK(!b.mounted_df(0));
            CHECK_STR(b.hd(1), "(empty)");
            // switched on later: what is mounted now is stored at once; switched off: gone again
            CHECK_EQ(win_mount(b.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
            b.persist("Yes");
            CHECK(has(fs.text(CFG), "Mount DF0=/usb0/a.adf"));
            b.persist("No");
            CHECK(!has(fs.text(CFG), "Mount "));
            CHECK(has(fs.text(CFG), "Persist medium mounts=No"));
        }
        {   // keys in the file with the item off (an older file, an edit): they do not act
            fs.put_text(CFG, std::string("[") + make_desc().store + "]\nMount DF0=/usb0/a.adf\nMount HD1=/usb0/h.hdf\n");
            R c(fs);
            c.boot();
            CHECK(!c.mounted_df(0));
            CHECK_STR(c.hd(1), "(empty)");
            CHECK(!c.c()->notice_pending);
        }
    }

    // ---- the item in the menu: a row of F7's Storage block (T212; T187 D7); RETURN switches it, the next mount is stored
    {
        FakeFs fs;
        world(fs);
        R a(fs);
        a.boot();
        a.open_menu();
        a.m->key(KEY_F7);                                                // T212 D1: Storage is a block of the app's settings (F7, APP)
        CHECK(a.find_row("Persist medium mounts") > 0 && a.find_row("Chip RAM") < 0);
        CHECK(a.find_row("Storage") >= 0 && a.find_row("Storage") < a.find_row("Persist medium mounts"));   // under its heading
        CHECK_STR(a.right(a.find_row("Persist medium mounts")).c_str(), "No");
        a.go_to("Persist medium mounts");
        a.m->key(KEY_RETURN);
        CHECK_STR(a.right(a.find_row("Persist medium mounts")).c_str(), "Yes");
        CHECK(has(fs.text(CFG), "Persist medium mounts=Yes"));
        CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK(has(fs.text(CFG), "Mount DF0=/usb0/a.adf"));
        a.m->key(KEY_RETURN);                                            // and off again: the keys go
        CHECK_STR(a.right(a.find_row("Persist medium mounts")).c_str(), "No");
        CHECK(!has(fs.text(CFG), "Mount "));
        a.m->key(KEY_F8);
        CHECK(a.find_row("Persist medium mounts") < 0);
    }

    // ---- S3: a stored medium that is gone: the slot stays empty with a note, the core starts, the key stays for the next start
    {
        FakeFs fs;
        world(fs);
        {
            R a(fs);
            a.persist("Yes");
            a.boot();
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(win_mount(a.c(), 1, "/usb0/b.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(sec_mount(a.c(), 1, "/usb0/h.hdf", msg, sizeof(msg)), 0);
            CHECK_EQ(sec_mount_folder(a.c(), 0, "/usb0/hd/0.1", msg, sizeof(msg)), 0);
        }
        fs.files.erase("/usb0/a.adf");                                   // a file deleted
        fs.files.erase("/usb0/h.hdf");
        for (auto it = fs.files.begin(); it != fs.files.end();) {        // and the folder: its stick is gone
            if (it->first.compare(0, 9, "/usb0/hd/") == 0) it = fs.files.erase(it); else ++it;
        }
        fs.dirs.erase("/usb0/hd");
        fs.dirs.erase("/usb0/hd/empty");
        {
            R b(fs);
            b.boot();
            CHECK_EQ(g_releases, 1);                                     // the core started
            CHECK(!b.mounted_df(0));                                     // empty
            CHECK_STR(b.df(1), "b.adf");                                 // the rest came
            CHECK_STR(b.hd(1), "(empty)");
            CHECK_STR(b.hd(0), "(empty)");
            CHECK_EQ(g_first_df0, 0);
            CHECK(b.c()->notice_pending);                                // the note, shown when the menu opens
            CHECK(strstr(b.c()->notice, "DF0") && strstr(b.c()->notice, "HD1") && strstr(b.c()->notice, "HD0"));
            CHECK(!strstr(b.c()->notice, "DF1"));
            b.open_menu();
            CHECK(strstr(b.m->status(), "DF0") != 0 && b.m->status_bad());
            std::string cfg = fs.text(CFG);                              // nothing was forgotten: the stick may come back
            CHECK(has(cfg, "Mount DF0=/usb0/a.adf") && has(cfg, "Mount HD1=/usb0/h.hdf") && has(cfg, "Mount HD0=folder /usb0/hd, 0.1 GB"));
            CHECK_EQ(win_mount(b.c(), 3, "/usb0/b.adf", msg, sizeof(msg)), 0);   // a mount by hand elsewhere does not touch the other keys
            CHECK(has(fs.text(CFG), "Mount DF0=/usb0/a.adf") && has(fs.text(CFG), "Mount DF3=/usb0/b.adf"));
            CHECK_EQ(win_eject(b.c(), 0, msg, sizeof(msg)), 0);          // an eject of the empty slot is the person's word: the key goes
            CHECK(!has(fs.text(CFG), "Mount DF0"));
        }
        printf("S3: DF0, HD0 and HD1 missing: empty with a note, the core released once, the keys kept\n");
    }

    // ---- S8: a media set: saved from the media menu, the slots changed, loaded: as saved, the core reset once
    {
        FakeFs fs;
        world(fs);
        R a(fs);
        a.persist("Yes");
        a.boot();
        CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(win_mount(a.c(), 1, "/usb0/b.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount_folder(a.c(), 0, "/usb0/hd/0.1", msg, sizeof(msg)), 0);
        uint64_t size0 = a.hd_size(0);
        a.open_menu();
        a.m->key(KEY_F1);                                                // T205 D1: the configs are rows of Home (the Summary tab is gone)
        CHECK(a.find_row("Save as config...") >= 0 && a.find_row("Load config...") >= 0);
        CHECK_STR(a.right(a.find_row("Load config...")).c_str(), "none saved");
        // nothing to load yet
        a.go_to("Load config...");
        CHECK_EQ(a.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(!a.m->picking() && a.m->status_bad());
        // save it: the name prompt of New folder, a name with a space
        a.go_to("Save as config...");
        a.m->key(KEY_RETURN);
        CHECK(a.m->entering());
        a.type("WB32 Install");
        CHECK_EQ(a.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(!a.m->entering());
        CHECK(!a.m->status_bad() && strstr(a.m->status(), "WB32 Install") && strstr(a.m->status(), "saved"));
        std::string set = fs.text(SETDIR "/WB32 Install.cfg");
        CHECK(has(set, "Mount DF0=/usb0/a.adf") && has(set, "Mount DF1=/usb0/b.adf") && has(set, "Mount HD0=folder /usb0/hd, 0.1 GB"));
        CHECK_EQ(count(set, "Mount "), 3);                               // the set holds what is mounted: three slots
        CHECK_STR(a.right(a.find_row("Load config...")).c_str(), "1 saved");
        // a second name, empty and a bad one are refused
        a.go_to("Save as config...");
        a.m->key(KEY_RETURN);
        CHECK_EQ(a.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(a.m->status_bad() && strstr(a.m->status(), "name"));
        // change every slot
        CHECK_EQ(win_mount(a.c(), 0, "/usb0/b.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(win_eject(a.c(), 1, msg, sizeof(msg)), 0);
        CHECK_EQ(win_mount(a.c(), 3, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_eject(a.c(), 0, msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount(a.c(), 1, "/usb0/g.hdf", msg, sizeof(msg)), 0);
        int holds = g_holds, releases = g_releases;
        std::string cfg_plain = fs.text(CFG);                            // T198 D4: the core's own .cfg is not written by a load
        CHECK(has(set, "[") && count(set, "Persist medium mounts=") == 1);   // D6: the config is a full copy of the settings
        // load it from the list
        a.go_to("Load config...");
        CHECK_STR(a.right(a.find_row("Load config...")).c_str(), "1 saved");
        a.m->key(KEY_RETURN);
        CHECK(a.m->picking());
        CHECK_EQ(a.m->rows(), 1);
        CHECK_STR(a.left(0).c_str(), "WB32 Install");
        CHECK_EQ(a.m->key(KEY_RETURN), MenuCtl::R_CLOSE);                // the core was reset: the menu closes, the machine runs
        CHECK(!a.m->picking());
        CHECK_STR(a.df(0), "a.adf");
        CHECK_STR(a.df(1), "b.adf");
        CHECK(!a.mounted_df(2) && !a.mounted_df(3));                     // what the set leaves empty is unmounted
        CHECK_STR(a.hd(0), "hd (folder)");
        CHECK_STR(a.hd(1), "(empty)");
        CHECK_EQ((long long)a.hd_size(0), (long long)size0);
        CHECK_EQ(g_releases - releases, 1);                              // the core was reset, once
        CHECK_EQ(g_holds - holds, 1);
        CHECK(a.m->status_bad() == false && strstr(a.m->status(), "loaded"));
        CHECK(fs.text(CFG) == cfg_plain);                              // D4: the core's own .cfg is as it was
        CHECK_STR(a.c()->config, "WB32 Install");                        // D5: from here the settings and the mounts live in the config
        std::string cfg = fs.text(SETDIR "/WB32 Install.cfg");
        CHECK(has(cfg, "Mount DF0=/usb0/a.adf") && has(cfg, "Mount DF1=/usb0/b.adf") && has(cfg, "Mount HD0=folder /usb0/hd, 0.1 GB"));
        CHECK(!has(cfg, "Mount HD1") && !has(cfg, "Mount DF3"));
        printf("S8: saved set loaded: slots as saved, %d reset(s)\n", g_releases - releases);

        // a set naming a medium that is not there: that slot is empty with a note, the rest is mounted, the core resets
        CHECK_EQ(win_mount(a.c(), 2, "/usb0/b.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount(a.c(), 1, "/usb0/h.hdf", msg, sizeof(msg)), 0);
        a.go_to("Save as config...");
        a.m->key(KEY_RETURN);
        a.type("Gone");
        a.m->key(KEY_RETURN);
        CHECK_EQ(win_eject(a.c(), 1, msg, sizeof(msg)), 0);
        CHECK_EQ(win_eject(a.c(), 2, msg, sizeof(msg)), 0);
        fs.files.erase("/usb0/b.adf");
        CHECK_EQ(win_mount(a.c(), 1, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_eject(a.c(), 0, msg, sizeof(msg)), 0);
        releases = g_releases;
        OpResult res;
        CHECK_EQ(ops_call(a.c(), "config.load", name_in("Gone"), 1, &res), OPE_OK);
        CHECK(strstr(res.msg, "DF1") && strstr(res.msg, "DF2") && strstr(res.msg, "Left empty"));
        CHECK(!strstr(res.msg, "DF3"));
        CHECK_STR(a.df(0), "a.adf");
        CHECK(!a.mounted_df(1) && !a.mounted_df(2));                     // DF1 held a.adf before: now empty, not what it was
        CHECK_STR(a.hd(0), "hd (folder)");
        CHECK_STR(a.hd(1), "h.hdf");
        CHECK_EQ(g_releases - releases, 1);
        // no such set; no name
        CHECK_EQ(ops_call(a.c(), "config.load", name_in("Nope"), 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "Nope"));
        // a write-back that fails: nothing is changed (not even the slots before the one that cannot be written) and the core is not reset
        fs.put("/usb0/b.adf", pat(2, AM_ADF_SIZE));
        CHECK_EQ(win_mount(a.c(), 3, "/usb0/b.adf", msg, sizeof(msg)), 0);
        a.r->io.ddr[AM_W_ADF + 3 * AM_ADF_STRIDE + 3 * AM_ADF_TRACK_BYTES] = 0x77;
        a.r->io.log_track(3, 3);
        a.r->io.delay_ms(10);
        win_service(a.c());
        CHECK(win_pending(a.c()));
        releases = g_releases;
        fs.sync_fails = true;
        CHECK_EQ(ops_call(a.c(), "config.load", name_in("WB32 Install"), 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "DF3") && strstr(res.msg, "stays mounted"));
        CHECK_STR(a.df(0), "a.adf");
        CHECK(!a.mounted_df(1));                                         // the set would have mounted b.adf here
        CHECK_STR(a.df(3), "b.adf");
        CHECK_STR(a.hd(1), "h.hdf");
        CHECK_EQ(g_releases - releases, 0);
        fs.sync_fails = false;
    }

    // ---- T180 #38: floppy images with a FAT long name (80 and 255 characters), picked in the Library and mounted into DF0: the whole path reaches the mount, the status
    // line shows the cut form, a path that fits the .cfg is stored and comes back at the next start, one that does not is mounted all the same and not stored
    {
        std::string n80, n255;
        for (int i = 0; i < 76; i++) n80 += (char)('a' + i % 26);
        n80 += ".adf";
        for (int i = 0; i < 251; i++) n255 += (char)('a' + (i * 7) % 26);
        n255 += ".adf";
        FakeFs fs;
        world(fs);
        fs.put("/usb0/long/" + n80, pat(8, AM_ADF_SIZE));
        fs.put("/usb0/long/" + n255, pat(9, AM_ADF_SIZE));
        std::string p80 = "/usb0/long/" + n80, p255 = "/usb0/long/" + n255;
        {
            R a(fs);
            a.persist("Yes");
            a.boot();
            a.open_menu();
            for (int pass = 0; pass < 2; pass++) {
                const std::string &nm = pass ? n255 : n80;
                a.m->key(KEY_F3);
                for (int i = 0; i < 12 && strcmp(a.m->title(), "/"); i++) a.m->key(KEY_LEFT);
                if (a.go_to("usb0/")) a.m->key(KEY_RETURN);
                if (a.go_to("long/")) a.m->key(KEY_RETURN);
                if (!a.go_to(nm.c_str())) {
                    continue;
                }
                a.m->key(KEY_RETURN);                                    // the sheet of the file
                CHECK(a.m->chooser());
                if (a.go_to("Mount DF0")) {
                    CHECK_EQ(a.m->key(KEY_RETURN) == MenuCtl::R_NONE, false);
                }
                CHECK(!a.m->status_bad());
                CHECK(a.mounted_df(0));
                std::string shown = a.df(0);                             // the status line: the start of the name, "..", its end
                CHECK(shown.size() < 48 && shown.find("..") != std::string::npos && shown.substr(shown.size() - 4) == ".adf");
                CHECK(shown.compare(0, 20, nm, 0, 20) == 0);
                std::vector<uint8_t> want = pat(pass ? 9 : 8, AM_ADF_SIZE);        // the bytes in the core's memory are those of this file, not of another one
                CHECK(!memcmp(&a.r->io.ddr[AM_W_ADF], want.data(), 4096));
                std::string cfg = fs.text(CFG);
                if (pass == 0) {
                    CHECK(has(cfg, ("Mount DF0=" + p80).c_str()));       // 90 characters: stored
                } else {
                    CHECK(!has(cfg, "Mount DF0="));                      // 267: mounted, not stored (the value of a .cfg line holds 199), and the 80 one it replaced is gone
                }
            }
        }
        {
            R b(fs);                                                     // the next start has what was stored: the 255 one was not, so DF0 is empty
            b.boot();
            CHECK(!b.mounted_df(0));
            CHECK_EQ(win_mount(b.c(), 0, p80.c_str(), msg, sizeof(msg)), 0);
            R c(fs);
            c.boot();
            CHECK(c.mounted_df(0));                                      // the 80 one comes back by its whole path
            CHECK_STR(c.spec(0), p80.c_str());
        }
    }

    // ---- T212 (docs/specs/T212-core-menu-areas.md S1-S11): Home is the session and its quick actions, F7 the app's settings, F5 the machine's, F8 the status
    {
        FakeFs fs;
        world(fs);
        R a(fs);
        a.boot();
        a.open_menu();
        char foot[240], buf[200];
        // S1: no information lines on Home; the cursor walks Reset / Reload, the six drives and the two config rows, every row takes it
        CHECK_STR(a.m->tab_id(), "home");
        CHECK_STR(a.left(0).c_str(), "Reset Amiga");
        CHECK_STR(a.left(1).c_str(), "Reload Kickstart, Reset");
        const char *gone[] = { "Core", "Application", "Network", "Media", "ROMs", "Libraries", "Build", "Eject DF0", "Create blank ADF" };
        for (const char *g : gone) CHECK(a.find_row(g) < 0);
        const char *drives[] = { "DF0", "DF1", "DF2", "DF3", "HD0", "HD1" };
        for (int i = 0; i < 6; i++) CHECK_EQ(a.find_row(drives[i]), 2 + i);
        CHECK(a.find_row("Save as config...") == 8 && a.find_row("Load config...") == 9);        // S5
        for (int i = 0; i < a.m->tree().a[0].tabs[0].count; i++) {
            const MenuEntry &e = a.m->tree().e[a.m->tree().a[0].tabs[0].first + i];
            CHECK(e.kind == EK_TASK || e.kind == EK_MEDIA || e.kind == EK_SAVE_SET || e.kind == EK_LOAD_SET);      // nothing the cursor walks does nothing
        }
        int n = a.m->rows();
        a.m->key(KEY_UP);
        for (int i = 1; i < n; i++) { a.m->key(KEY_DOWN); CHECK_EQ(a.m->selected(), i); }
        // S6: the machine line from the setting values; an empty drive says how to fill it (R11)
        a.m->machine_line(buf, sizeof(buf));
        CHECK_STR(buf, "AGA - Chip RAM 2M");
        CHECK_STR(a.right(a.find_row("DF0")).c_str(), "Empty - RETURN to mount");
        // S3: RETURN on an empty drive opens its list: mount from the Library, create blank; S11: the foot names its keys
        CHECK(a.go_to("DF0"));
        a.m->key(KEY_RETURN);
        CHECK_STR(a.m->tab_id(), "drive");
        CHECK_STR(a.m->title(), "DF0");
        CHECK_EQ(a.m->rows(), 2);
        CHECK_STR(a.left(0).c_str(), "Mount from Library...");
        CHECK_STR(a.left(1).c_str(), "Create blank ADF");
        a.m->footer_legend(foot, sizeof(foot));
        CHECK_STR(foot, "UP/DOWN  CHOOSE   RETURN  DO IT   ESC  BACK");
        a.m->key(KEY_ESCAPE);
        CHECK_STR(a.m->tab_id(), "home");
        CHECK(a.go_to("HD0"));
        a.m->key(KEY_RETURN);
        CHECK(a.left(0) == "Mount from Library..." && a.left(1) == "Mount folder as HD0" && a.m->rows() == 2);
        a.m->key(KEY_ESCAPE);
        // S3: Mount from Library: the Library lists the folders and the types of the drive only; RETURN on a file mounts it into that drive
        CHECK(a.go_to("DF0"));
        a.m->key(KEY_RETURN);
        a.m->key(KEY_RETURN);
        CHECK_EQ(a.m->mount_slot(), 0);
        CHECK_STR(a.m->tree().a[a.m->area()].key, "F3");
        a.m->footer_legend(foot, sizeof(foot));
        if (a.find_row("usb0/") >= 0) { CHECK(a.go_to("usb0/")); a.m->key(KEY_RETURN); }
        CHECK(a.find_row("a.adf") >= 0 && a.find_row("b.adf") >= 0 && a.find_row("h.hdf") < 0);
        CHECK(a.go_to("a.adf"));
        CHECK_EQ(a.m->mount_slot(), 0);                              // the footer of the mount browser is drawn by the F3 layout (seen in ue2emu, S11)
        a.m->key(KEY_RETURN);
        CHECK(a.mounted_df(0));
        CHECK_EQ(a.m->mount_slot(), -1);
        // S2: a mounted drive: eject, replace; RETURN on Eject ejects and says so
        a.m->key(KEY_F1);
        CHECK(a.right(a.find_row("DF0")).find("a.adf") != std::string::npos);
        CHECK(a.go_to("DF0"));
        a.m->key(KEY_RETURN);
        CHECK(a.left(0) == "Eject" && a.left(1) == "Replace from Library..." && a.m->rows() == 2);
        a.m->key(KEY_RETURN);
        CHECK(!a.mounted_df(0));
        CHECK(strstr(a.m->toast_text(), "DF0 ejected") != 0);
        CHECK_STR(a.m->tab_id(), "home");
        // S4: Reset and Reload are the core's own reset tasks and close the menu
        CHECK(a.go_to("Reload Kickstart, Reset"));
        MenuCtl::Result rr = a.m->key(KEY_RETURN);
        if (rr != MenuCtl::R_CLOSE) { a.m->dialog(buf, sizeof(buf)); rr = a.m->key(KEY_RETURN); }       // a confirmation first
        CHECK_EQ(rr, MenuCtl::R_CLOSE);
        a.m->open();
        // S7: F7 is APP: the generic app's settings; the Quick list is gone
        a.m->key(KEY_F7);
        CHECK_STR(a.m->tree().a[a.m->area()].name, "APP");
        const char *app[] = { "Storage", "Persist medium mounts" };       // this rig extends the persist item only (home_extend and hk_extend: test_home, test_hotkeys)
        for (const char *g : app) CHECK(a.find_row(g) >= 0);
        CHECK(a.find_row("Chip RAM") < 0 && a.find_row("Eject DF0") < 0 && a.find_row("Reset Amiga") < 0);
        // S8: F5 is the machine: no storage, menu or hotkey rows
        a.m->key(KEY_F5);
        CHECK(a.find_row("Chip RAM") >= 0);
        const char *not_machine[] = { "Persist medium mounts", "Menu opens at", "Hotkeys", "Home directory" };
        for (const char *g : not_machine) CHECK(a.find_row(g) < 0);
        // S10: F8 shows Build, ROMs and Libraries below the reset row
        a.m->key(KEY_F8);
        CHECK(a.find_row("Build") > a.find_row("Reset to host defaults") && a.find_row("ROMs") > a.find_row("Build") && a.find_row("Libraries") > a.find_row("ROMs"));
        CHECK(a.find_row("Core") < 0 && a.find_row("Media") < 0);
        a.m->footer_legend(foot, sizeof(foot));
        CHECK(strstr(foot, "RETURN") != 0);
    }

    DONE("test_media_mounts");
}
