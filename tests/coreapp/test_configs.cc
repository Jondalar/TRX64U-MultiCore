// T198 (docs/specs/T198-per-core-configs.md, scenarios S1, S7, S8, S10, S11, S14, S15): a config is a file of the core folder that holds a full copy of the settings and the
// mounts; the host names one in the hand-over page and the core starts with it (the settings are read from its file, the core's own .cfg is not touched); the running core saves
// and loads configs. The machine is the A1200's record on the model of its core (as test_media_mounts.cc), with the fake file system's switches for a medium that is read only.
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
#define CFGDIR "/usb0/cores/amiga/configs"
#define MEDIADIR "/usb0/cores/amiga/media"

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
    explicit R(FakeFs &f, const char *config = 0, bool folder = true) : fs(f)
    {
        desc = make_desc();
        r = new AmigaRig(&fs, &desc);
        r->io.regs[AM_FEAT] = 0x01;
        delete r->st;
        if (folder) {
            snprintf(r->c.cfg_path, sizeof(r->c.cfg_path), "%s", CFG);
        }
        config_start(&r->c, config);                                     // what run.cc does with the hand-over page's name, before the settings are read
        r->st = new Settings(desc.settings, desc.nsettings, desc.store, &fs, ctx_settings_path(&r->c), 0);
        r->st->load();
        r->c.settings = r->st;
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


#define WP "Floppy write protect (next reset)"

static int files_in(FakeFs &fs, const char *prefix)
{
    int n = 0;
    std::string p = lower(prefix);
    for (auto &kv : fs.files) {
        n += kv.first.compare(0, p.size(), p) == 0;
    }
    return n;
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    char msg[400];

    // ---- S1: save. DF0 = Install.adf (here a.adf), HD0 a folder disk, a changed setting.
    {
        FakeFs fs;
        world(fs);
        R a(fs);
        a.boot();
        CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount_folder(a.c(), 0, "/usb0/hd/0.1", msg, sizeof(msg)), 0);
        CHECK_EQ(a.set(WP, "Yes"), OPE_OK);
        CHECK_EQ(config_save(a.c(), "Workbench 3.1 HD", msg, sizeof(msg)), 0);
        std::string t = fs.text(CFGDIR "/Workbench 3.1 HD.cfg");
        CHECK(has(t, "Mount DF0=/usb0/a.adf") && has(t, "Mount HD0=folder /usb0/hd, 0.1 GB"));
        CHECK_EQ(count(t, "Mount "), 2);                                  // every mounted slot, nothing else
        CHECK(has(t, WP "=Yes"));                                         // the changed setting ...
        CHECK(has(t, "Chip RAM (at next reset)="));                       // ... and the unchanged ones: a full copy (D6)
        CHECK(strstr(msg, "saved: 2 media") && strstr(msg, "settings"));
        printf("S1: saved: %s\n", msg);
        // refused names: nothing is written, the reason is said
        const char *bad[] = { "", "a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", "0123456789012345678901234567890123", ".hidden", "trailing " };
        int before = files_in(fs, CFGDIR);
        for (const char *nm : bad) {
            msg[0] = 0;
            CHECK_EQ(config_save(a.c(), nm, msg, sizeof(msg)), 1);
            CHECK(msg[0] != 0);
        }
        CHECK_EQ(files_in(fs, CFGDIR), before);
        CHECK_EQ(config_save(a.c(), "01234567890123456789012345678901", msg, sizeof(msg)), 0);   // 32 characters fit
        // negative control: a read only card gives the error line and no file
        fs.read_only = true;
        CHECK_EQ(config_save(a.c(), "Second", msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "cannot be saved") != 0);
        CHECK(!fs.has(CFGDIR "/Second.cfg"));
        std::string again = fs.text(CFGDIR "/Workbench 3.1 HD.cfg");
        CHECK_EQ(config_save(a.c(), "Workbench 3.1 HD", msg, sizeof(msg)), 1);        // an existing one is not damaged by a failed save
        CHECK(fs.text(CFGDIR "/Workbench 3.1 HD.cfg") == again);
        fs.read_only = false;
        // review 2026-11-11: a save over an existing config writes beside it and swaps; a failure midway leaves the old one and 
        {
            int nfiles = files_in(fs, CFGDIR);
            std::string old = fs.text(CFGDIR "/Workbench 3.1 HD.cfg");
            CHECK_EQ(a.set(WP, "No"), OPE_OK);
            fs.mut_budget = 1;                                                      // the new file is written, the swap cannot happen
            CHECK_EQ(config_save(a.c(), "Workbench 3.1 HD", msg, sizeof(msg)), 1);
            fs.mut_budget = -1;
            CHECK(fs.text(CFGDIR "/Workbench 3.1 HD.cfg") == old);
            CHECK(fs.has(CFGDIR "/Workbench 3.1 HD.tmp"));                          // the budget is spent, so the cleanup could not remove the temp either: it is no config
            CHECK(!fs.has(CFGDIR "/Workbench 3.1 HD.bak"));
            CHECK_EQ(config_save(a.c(), "Workbench 3.1 HD", msg, sizeof(msg)), 0);
            CHECK(fs.text(CFGDIR "/Workbench 3.1 HD.cfg") != old && has(fs.text(CFGDIR "/Workbench 3.1 HD.cfg"), WP "=No"));
            CHECK_EQ(files_in(fs, CFGDIR), nfiles);
        }
        // review 2026-11-11 (power loss): (a) between the two renames only X.bak + X.tmp are left: the list brings the old config back; (b) a stale X.bak beside X.cfg does not
        // stop a save; (c) a restore rename that fails is said
        {
            const char *X = CFGDIR "/Workbench 3.1 HD.cfg", *XB = CFGDIR "/Workbench 3.1 HD.bak", *XT = CFGDIR "/Workbench 3.1 HD.tmp";
            std::string old = fs.text(X);
            fs.put(XB, std::vector<uint8_t>(old.begin(), old.end()));
            fs.put(XT, std::vector<uint8_t>(3, 'x'));
            CHECK(fs.remove(X));
            CHECK(!fs.has(X));
            ConfigList l;
            configs_list(a.c(), &l);
            CHECK(configlist_find(&l, "Workbench 3.1 HD") >= 0);
            CHECK(fs.text(X) == old && !fs.has(XB) && !fs.has(XT));
            fs.put(XB, std::vector<uint8_t>(4, 'y'));                                // a stale .bak beside the config
            CHECK_EQ(a.set(WP, "Yes"), OPE_OK);
            CHECK_EQ(config_save(a.c(), "Workbench 3.1 HD", msg, sizeof(msg)), 0);
            CHECK(has(fs.text(X), WP "=Yes") && !fs.has(XB));
            CHECK_EQ(a.set(WP, "No"), OPE_OK);
            fs.put(XB, std::vector<uint8_t>(4, 'y'));
            fs.mut_budget = 3;                                                       // tmp written, remove(.bak), rename old -> .bak, then nothing: the swap and the restore fail
            CHECK_EQ(config_save(a.c(), "Workbench 3.1 HD", msg, sizeof(msg)), 1);
            fs.mut_budget = -1;
            CHECK(strstr(msg, "could not be put back") != 0);
            configs_list(a.c(), &l);                                                 // and the next list brings it back
            CHECK(fs.has(X) && !fs.has(XB));
            printf("power loss: %s\n", msg);
        }
        // D11: at most 16
        for (int i = 0; i < 14; i++) {
            char nm[16];
            snprintf(nm, sizeof(nm), "Cfg %02d", i);
            CHECK_EQ(config_save(a.c(), nm, msg, sizeof(msg)), 0);
        }
        CHECK_EQ(config_save(a.c(), "One too many", msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "16") != 0 && !fs.has(CFGDIR "/One too many.cfg"));
        CHECK_EQ(config_save(a.c(), "Cfg 03", msg, sizeof(msg)), 0);                  // saving over one of the 16 is not a 17th
    }

    // ---- S7: the core starts with the config: settings and mounts are in force at the first release; the core's own .cfg is not written (D4); changes go to the config (D5)
    std::string plain_cfg;
    {
        FakeFs fs;
        world(fs);
        {
            R a(fs);
            a.boot();
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(win_mount(a.c(), 1, "/usb0/b.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(a.set(WP, "Yes"), OPE_OK);
            CHECK_EQ(config_save(a.c(), "Demo collection", msg, sizeof(msg)), 0);
            CHECK_EQ(a.set(WP, "No"), OPE_OK);                                // the plain .cfg says No and mounts nothing (persist is off)
        }
        plain_cfg = fs.text(CFG);
        CHECK(has(plain_cfg, WP "=No"));
        {
            R b(fs, "Demo collection");
            CHECK_STR(b.c()->config, "Demo collection");
            b.boot();
            CHECK_EQ(g_first_df0, 1);                                         // the first frame already shows DF0
            CHECK_STR(b.df(0), "a.adf");
            CHECK_STR(b.df(1), "b.adf");
            int wp = b.c()->settings->find(WP);
            CHECK_STR(b.c()->settings->label(wp), "Yes");                     // the config's value, not the .cfg's
            CHECK(!b.c()->notice_pending);
            CHECK(fs.text(CFG) == plain_cfg);                                 // D4: not written by the start
            CHECK_EQ(b.set(WP, "No"), OPE_OK);                                // D5: a change goes into the config
            CHECK(fs.text(CFG) == plain_cfg);
            CHECK(has(fs.text(CFGDIR "/Demo collection.cfg"), WP "=No"));
            CHECK_EQ(win_eject(b.c(), 1, msg, sizeof(msg)), 0);               // ... and so does a medium
            CHECK(!has(fs.text(CFGDIR "/Demo collection.cfg"), "Mount DF1"));
            CHECK(fs.text(CFG) == plain_cfg);
            printf("S7: started with the config: DF0/DF1 at the first release, .cfg untouched\n");
        }
        {   // no name (a page without the field, an old host, a stale page): a plain start
            R c(fs, "");
            c.boot();
            CHECK_STR(c.c()->config, "");
            CHECK(!c.mounted_df(0));
            int wp = c.c()->settings->find(WP);
            CHECK_STR(c.c()->settings->label(wp), "No");
        }
        {   // a name that is not there: plain, with a line on the screen
            R d(fs, "Gone for good");
            CHECK_STR(d.c()->config, "");
            CHECK(d.c()->notice_pending && strstr(d.c()->notice, "Gone for good") && strstr(d.c()->notice, "plain"));
            d.boot();
            CHECK(!d.mounted_df(0));
        }
    }

    // ---- S8: a medium of the config is gone: the core starts, the rest is mounted, the message names the file and what to do, the config file is not changed (D12)
    {
        FakeFs fs;
        world(fs);
        std::string before;
        {
            R a(fs);
            a.boot();
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(win_mount(a.c(), 1, "/usb0/b.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(config_save(a.c(), "Two disks", msg, sizeof(msg)), 0);
        }
        fs.files.erase(lower("/usb0/b.adf"));
        before = fs.text(CFGDIR "/Two disks.cfg");
        plain_cfg = fs.text(CFG);
        {
            R b(fs, "Two disks");
            b.boot();
            CHECK_STR(b.df(0), "a.adf");
            CHECK(!b.mounted_df(1));
            CHECK(b.c()->notice_pending && strstr(b.c()->notice, "/usb0/b.adf") && strstr(b.c()->notice, "DF1") && strstr(b.c()->notice, "put the medium back"));
            printf("S8: %s\n", b.c()->notice);
        }
        CHECK(fs.text(CFGDIR "/Two disks.cfg") == before);                   // negative control: no slot emptied in the file
        CHECK(has(fs.text(CFGDIR "/Two disks.cfg"), "Mount DF1=/usb0/b.adf"));
        CHECK(fs.text(CFG) == plain_cfg);
    }

    // ---- S10: the running core lists and loads the configs of its own folder; the list is the host's list (configlist), the load acts like a start
    {
        FakeFs fs;
        world(fs);
        {
            R a(fs);
            a.boot();
            CHECK_EQ(win_mount(a.c(), 0, "/usb0/b.adf", msg, sizeof(msg)), 0);
            CHECK_EQ(a.set(WP, "Yes"), OPE_OK);
            CHECK_EQ(config_save(a.c(), "Zeta", msg, sizeof(msg)), 0);
            CHECK_EQ(config_save(a.c(), "alpha", msg, sizeof(msg)), 0);
            CHECK_EQ(a.set(WP, "No"), OPE_OK);
        }
        fs.put_text(CFGDIR "/notes.txt", "foreign");                        // a foreign file is no config
        R a(fs);
        a.boot();
        plain_cfg = fs.text(CFG);
        ConfigList cl;
        CHECK_EQ(configs_list(a.c(), &cl), 2);
        ConfigList host;                                                       // what the host builds from the same entries
        configlist_clear(&host);
        static FsEntry ent[32];
        int ne = fs.list(CFGDIR, ent, 32);
        for (int i = 0; i < ne; i++) configlist_add(&host, ent[i].name, ent[i].dir, false);
        configlist_sort(&host);
        CHECK_EQ(host.n, cl.n);
        for (int i = 0; i < cl.n; i++) CHECK_STR(host.name[i], cl.name[i]);
        CHECK_STR(cl.name[0], "alpha");
        CHECK_STR(cl.name[1], "Zeta");
        int releases = g_releases;
        CHECK_EQ(config_load(a.c(), "Zeta", msg, sizeof(msg)), 0);
        CHECK_STR(a.df(0), "b.adf");
        CHECK_STR(a.c()->settings->label(a.c()->settings->find(WP)), "Yes");
        CHECK_EQ(g_releases - releases, 1);                                   // one reset
        CHECK(fs.text(CFG) == plain_cfg);                                      // the core's own .cfg is not written
        CHECK_STR(ops_state(a.c(), "config.active"), "Zeta");
        CHECK_EQ(config_load(a.c(), "Nope", msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "Nope") != 0);
        printf("S10: loaded: %s\n", msg);
    }

    // ---- S11: a media set of before T198 is listed as a config, starts and loads; the .mnt stays; a config of the same name hides it
    {
        FakeFs fs;
        world(fs);
        fs.put_text(MEDIADIR "/Old Set.mnt", "[Amiga 1200 Settings]\nMount DF0=/usb0/a.adf\n");
        fs.put_text(MEDIADIR "/Both.mnt", "[Amiga 1200 Settings]\nMount DF0=/usb0/b.adf\n");
        fs.put_text(CFGDIR "/Both.cfg", "[Amiga 1200 Settings]\nMount DF0=/usb0/a.adf\n");
        {
            R a(fs);
            a.boot();
            ConfigList cl;
            CHECK_EQ(configs_list(a.c(), &cl), 2);
            CHECK_STR(cl.name[0], "Both");
            CHECK(!cl.legacy[0]);
            CHECK_STR(cl.name[1], "Old Set");
            CHECK(cl.legacy[1]);
            CHECK_EQ(config_load(a.c(), "Old Set", msg, sizeof(msg)), 0);
            CHECK_STR(a.df(0), "a.adf");
            CHECK(fs.has(CFGDIR "/Old Set.cfg") && fs.has(MEDIADIR "/Old Set.mnt"));   // converted by a copy
            ConfigList l2;
            CHECK_EQ(configs_list(a.c(), &l2), 2);                                      // still two names, no duplicate
        }
        fs.files.erase(lower(CFGDIR "/Old Set.cfg"));
        R b(fs, "Old Set");                                                           // the host names a set that only exists as a .mnt
        b.boot();
        CHECK_STR(b.df(0), "a.adf");
        CHECK(fs.has(CFGDIR "/Old Set.cfg"));
        printf("S11: media set listed, converted by copy, started and loaded\n");
    }

    // ---- S14: no core folder (not started from a .cor): no list, no save, a start with a name is plain
    {
        FakeFs fs;
        world(fs);
        R a(fs, 0, false);
        a.boot();
        ConfigList cl;
        CHECK_EQ(configs_list(a.c(), &cl), -1);
        CHECK_EQ(config_save(a.c(), "X", msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "no folder") != 0);
        a.open_menu();
        a.m->key(KEY_F1);                                                       // T205 D1: the config rows are on Home
        a.go_to("Load config...");
        a.m->key(KEY_RETURN);
        CHECK(a.m->status_bad() && strstr(a.m->status(), "no folder, so no configs"));
        R b(fs, "Whatever", false);
        CHECK_STR(b.c()->config, "");
        CHECK(b.c()->notice_pending && strstr(b.c()->notice, "no folder"));
        printf("S14: %s\n", a.m->status());
    }

    // ---- S15: many files: the list is capped at 16 and read once per generation (not per key or per state poll)
    {
        FakeFs fs;
        world(fs);
        for (int i = 0; i < 40; i++) {
            char nm[64];
            snprintf(nm, sizeof(nm), CFGDIR "/Config %02d.cfg", i);
            fs.put_text(nm, "[Amiga 1200 Settings]\n");
        }
        R a(fs);
        a.boot();
        int reads0 = configs_list_reads();
        const ConfigList *cl;
        CHECK_EQ(configs_cached(a.c(), &cl), CONFIGS_MAX);
        CHECK_EQ(cl->more, 24);
        for (int i = 0; i < 50; i++) {
            CHECK_STR(ops_state(a.c(), "config.count"), "16");
        }
        a.open_menu();
        a.m->key(KEY_F1);                                                       // T205 D1: the config rows are on Home
        for (int i = 0; i < 20; i++) {
            a.m->key(KEY_DOWN);
        }
        a.go_to("Load config...");
        a.m->key(KEY_RETURN);
        CHECK(a.m->picking());
        CHECK_EQ(a.m->rows(), CONFIGS_MAX);
        for (int i = 0; i < 30; i++) {
            a.m->key(KEY_DOWN);
        }
        CHECK_EQ(configs_list_reads() - reads0, 2);                            // configs/ and media/, once
        printf("S15: 40 files, list of %d, %d directory reads for 50 state polls and 30 keys\n", CONFIGS_MAX, configs_list_reads() - reads0);
    }

    DONE("test_configs");
}
