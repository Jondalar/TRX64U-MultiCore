// T180 D4 and D1 (docs/specs/done/T180-media-housekeeping.md, scenarios S1 and S7): every path that lets go of a writable medium writes back everything
// pending for it first, completely; a write-back that fails keeps the medium mounted, stops the action and shows the error; a mount on an
// occupied slot lets the old medium go the same way. The machine is the A1200's record (a floppy window, a hard disk image and a folder disk) on
// the model of its core; the reset paths run on a copy of the record whose hold and release do nothing, so that only the base's own write-back
// is seen (the Amiga's hold also writes the disks, which would hide a missing one).
#include <functional>
#include <map>
#include "folderhd_bench.h"
#include "boot.h"
#include "engines.h"
#include "menuctl.h"
#include "keyboard.h"
#include "video.h"

using namespace bench;

typedef std::map<std::string, std::vector<uint8_t>> FileMap;

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

static FileMap host_files(FakeFs &fs, const std::string &root)
{
    FileMap m;
    std::string prefix = lower(root) + "/";
    for (auto &kv : fs.files) {
        if (kv.first.compare(0, prefix.size(), prefix) == 0) {
            std::string rel = fs.names[kv.first].substr(prefix.size());
            if (rel.compare(0, 6, "trx64/") == 0) continue;
            m[rel] = kv.second;
        }
    }
    return m;
}

// T180 D4: the hook of a core with a media engine of its own (apple2, atari800, cpc, the drive library, snes, neogeo): a fake one that counts and can refuse
static int g_hook_calls;
static bool g_hook_fail, g_hook_pend;
static int fake_flush(CoreCtx *, char *msg, int n)
{
    g_hook_calls++;
    if (g_hook_fail) {
        snprintf(msg, n, "ENG: the fake engine could not write back. It stays mounted.");
        return 1;
    }
    return 0;
}
static bool fake_pending(CoreCtx *) { return g_hook_pend; }

static int g_holds;
static int hold_nop(CoreCtx *, char *, int) { g_holds++; return 0; }
static int nop(CoreCtx *, char *, int) { return 0; }
static CoreDesc quiet_desc()
{
    CoreDesc d = amiga_desc;
    d.init = hold_nop;
    d.release = nop;
    d.warm_reset = nop;
    d.cold_start = 0;
    d.reset_ms = 1;
    d.nroms = 0;
    d.romset = 0;
    d.media_flush = fake_flush;
    d.media_pending = fake_pending;
    return d;
}

static int g_on, g_off;
static void saving_hook(CoreCtx *, bool on) { (on ? g_on : g_off)++; }

struct Host : MenuHost {
    int returns = 0;
    void info(int, char *out, int n) { snprintf(out, n, "-"); }
    bool return_to_host(char *, int) { returns++; return true; }
};

// What is on the media: the two image files and the folder as the host sees it
struct Snap {
    std::vector<uint8_t> adf, hdf;
    FileMap hd;
    bool operator==(const Snap &o) const { return adf == o.adf && hdf == o.hdf && hd == o.hd; }
};

struct R {
    FakeFs fs;
    CoreDesc desc;
    AmigaRig *r;
    Host host;
    Video *vid;
    MenuCtl *m = 0;
    R()
    {
        fs.put("/usb0/a.adf", pat(1, AM_ADF_SIZE));
        fs.put("/usb0/b.adf", pat(2, AM_ADF_SIZE));
        fs.put("/usb0/h.hdf", pat(3, 64 * 512));
        fs.put("/usb0/g.hdf", pat(4, 64 * 512));
        make_tree(fs, "/usb0/hd");
        make_tree(fs, "/usb0/hd2");
        desc = quiet_desc();
        r = new AmigaRig(&fs, &desc);
        r->io.regs[AM_FEAT] = 0x01;
        engines_of(&r->c);
        r->c.saving = saving_hook;
        g_on = g_off = g_holds = 0;
        vid = new Video(desc.video, &r->px, &r->io);
        r->c.video = vid;
    }
    ~R() { delete m; delete vid; delete r; }
    CoreCtx *c() { return &r->c; }
    void mount_all()
    {
        char msg[240];
        CHECK_EQ(win_mount(c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount(c(), 0, "/usb0/h.hdf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount_folder(c(), 1, "/usb0/hd/0.1", msg, sizeof(msg)), 0);
    }
    // writes on all three that no sync has taken to their files yet
    uint32_t folder_block = 0;
    void dirty_all()
    {
        r->io.ddr[AM_W_ADF + 3 * AM_ADF_TRACK_BYTES] = 0x77;
        r->io.log_track(0, 3);
        r->io.delay_ms(10);
        win_service(c());                                           // marks the track; it is not due yet
        std::vector<uint8_t> blk = pat(900, 512);
        memcpy(&r->io.ddr[AM_W_STAGE], blk.data(), 512);
        r->io.post(0, true, 5);
        CHECK_EQ(sec_service(c(), 16), 1);
        Dev dev(*r, 1);
        Reader rd(dev);
        CHECK(rd.check_all());
        const Ent *f = rd.find("file28.dat");
        CHECK(f && f->data.size() == 8);
        if (f) {
            std::vector<uint8_t> nb = pat(777, 512);
            CHECK(dev.write(rd.base + f->data[2], nb.data()));
        }
        CHECK(win_pending(c()) && sec_pending(c()));
        CHECK(sec_mounted_unsynced(0) && folder()->dirty());
    }
    bool sec_mounted_unsynced(int u) { return r->c.eng->sec.unit[u].mounted && r->c.eng->sec.unit[u].unsynced; }
    FolderDisk *folder() { return (FolderDisk *)r->c.eng->sec.unit[1].file; }
    Snap snap()
    {
        Snap s;
        s.adf = fs.files["/usb0/a.adf"];
        s.hdf = fs.files["/usb0/h.hdf"];
        s.hd = host_files(fs, "/usb0/hd");
        return s;
    }
    bool clean() { return !win_pending(c()) && !sec_pending(c()); }
    void open_menu()
    {
        m = new MenuCtl(c(), &host);
        m->open();
    }
    void to_return_row()
    {
        m->key(KEY_F8);
        for (int i = 0; i < m->rows(); i++) {
            char l[100], rr[100];
            bool a;
            m->row(i, l, 100, rr, 100, &a);
            if (!strcmp(l, "Return to host menu")) {
                while (m->selected() > i) m->key(KEY_UP);
                while (m->selected() < i) m->key(KEY_DOWN);
                return;
            }
        }
        printf("FAIL no row Return to host menu\n");
        g_fails++;
    }
};

// the state after a full sync of the same writes: what every exit path has to leave
static Snap reference(Snap *before = 0)
{
    R x;
    x.mount_all();
    x.dirty_all();
    if (before) *before = x.snap();
    char msg[240];
    CHECK(win_flush_all(x.c(), msg, sizeof(msg)));
    CHECK(sec_sync_all(x.c(), msg, sizeof(msg)));
    CHECK(x.clean());
    return x.snap();
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    char msg[240];
    Snap before;
    Snap ref = reference(&before);
    CHECK(!(ref == before));                                         // the writes are not in the files until a sync
    CHECK(ref.adf != before.adf && ref.hd != before.hd);

    // ---- S7: every exit path leaves the files and the folder as a full sync does
    struct Path {
        const char *name;
        std::function<int(R &)> run;
        bool leaves_mounted;
    };
    std::vector<Path> paths = {
        { "eject", [&](R &x) { return win_eject(x.c(), 0, msg, sizeof(msg)) | sec_eject(x.c(), 0, msg, sizeof(msg)) | sec_eject(x.c(), 1, msg, sizeof(msg)); }, false },
        { "replace (D1)", [&](R &x) {
              return win_mount(x.c(), 0, "/usb0/b.adf", msg, sizeof(msg)) | sec_mount(x.c(), 0, "/usb0/g.hdf", msg, sizeof(msg)) |
                     sec_mount_folder(x.c(), 1, "/usb0/hd2/0.1", msg, sizeof(msg)); }, false },
        { "cold reset", [&](R &x) { return boot_cold_reset(x.c(), msg, sizeof(msg)); }, true },
        { "warm reset", [&](R &x) { return boot_warm_reset(x.c(), msg, sizeof(msg)); }, true },
        { "reload ROMs", [&](R &x) { return boot_reload(x.c(), msg, sizeof(msg)); }, true },
        { "restart core", [&](R &x) { return boot_restart(x.c(), msg, sizeof(msg)); }, true },
        { "return to host", [&](R &x) {
              x.open_menu();
              x.to_return_row();
              x.m->key(KEY_RETURN);
              return x.host.returns == 1 ? 0 : 1; }, true },
    };
    for (auto &p : paths) {
        R x;
        x.mount_all();
        x.dirty_all();
        int syncs = x.fs.syncs;
        int rc = p.run(x);
        if (rc) printf("  %s: %s\n", p.name, msg);
        CHECK_EQ(rc, 0);
        Snap after = x.snap();
        if (!(after == ref)) {
            printf("  %s: adf %d hdf %d folder %d\n", p.name, (int)(after.adf == ref.adf), (int)(after.hdf == ref.hdf), (int)(after.hd == ref.hd));
        }
        CHECK(after == ref);                                         // byte-identical to the full sync, files and folder
        CHECK(x.clean());
        CHECK(x.fs.syncs > syncs);                                   // the hard disk image was synced too
        if (!p.leaves_mounted && !strcmp(p.name, "eject")) {
            CHECK_EQ(x.fs.open_files, 0);
            CHECK(!win_name(x.c(), 0) && !sec_name(x.c(), 0) && !sec_name(x.c(), 1));
        }
        CHECK(g_on >= 1 && g_on == g_off);                           // the picture said "Saving media..." and took it away (a reset says it once for all, an eject once per medium)
        if (p.leaves_mounted) CHECK_EQ(g_on, 1);
    }

    // ---- T180 D4: the core's own engine is asked on every exit; when it refuses, the exit stops with its reason and the base's media are written anyway
    for (auto &p : paths) {
        if (!strcmp(p.name, "eject") || !strcmp(p.name, "replace (D1)")) continue;      // those are the base's own slots: an engine has its own eject and mount
        {
            R x;
            x.mount_all();
            g_hook_calls = 0;
            g_hook_fail = false;
            CHECK_EQ(p.run(x), 0);
            CHECK_EQ(g_hook_calls, 1);                                   // asked once, as part of the common write-back
        }
        {
            R x;
            x.mount_all();
            x.dirty_all();
            g_hook_calls = 0;
            g_hook_fail = true;
            g_holds = 0;
            msg[0] = 0;
            int rc = p.run(x);
            g_hook_fail = false;
            CHECK(rc != 0);
            if (strcmp(p.name, "return to host")) CHECK(strstr(msg, "ENG:") != 0);
            CHECK_EQ(g_hook_calls, 1);
            CHECK_EQ(g_holds, 0);                                        // the machine was not held
            if (!strcmp(p.name, "return to host")) {
                CHECK_EQ(x.host.returns, 0);
                CHECK(x.m->status_bad() && strstr(x.m->status(), "ENG:") != 0);
            }
            CHECK(x.clean());                                            // the floppy, the hard disk and the folder were written anyway
            CHECK(x.snap() == ref);
        }
    }
    {   // the picture says "Saving media..." for an engine that has something to write, and not otherwise
        R x;
        x.mount_all();
        g_hook_pend = true;
        CHECK_EQ(boot_cold_reset(x.c(), msg, sizeof(msg)), 0);
        CHECK_EQ(g_on, 1);
        CHECK_EQ(g_off, 1);
        g_hook_pend = false;
        g_on = g_off = 0;
        CHECK_EQ(boot_cold_reset(x.c(), msg, sizeof(msg)), 0);
        CHECK_EQ(g_on, 0);
    }
    g_hook_calls = 0;

    // ---- nothing pending: nothing is said and nothing is written
    {
        R x;
        x.mount_all();
        int w = x.fs.mutations, s = x.fs.syncs;
        CHECK_EQ(boot_cold_reset(x.c(), msg, sizeof(msg)), 0);
        CHECK_EQ(g_on, 0);
        CHECK_EQ(x.fs.mutations, w);
        CHECK_EQ(x.fs.syncs, s);
    }

    // ---- S7: a write-back made to fail keeps the medium mounted, stops the action and tells why
    {
        // the floppy and the hard disk image: the medium refuses the sync
        R x;
        x.mount_all();
        x.dirty_all();
        x.fs.sync_fails = true;
        CHECK(win_eject(x.c(), 0, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "DF0") && strstr(msg, "stays mounted"));
        CHECK_STR(win_name(x.c(), 0), "a.adf");
        CHECK_EQ(x.r->io.regs[AM_FLOPPY_MOUNT], 1);                  // the core still has the disk
        CHECK(x.c()->eng->win.slot[0].file != 0 && win_pending(x.c()));
        int open = x.fs.open_files;
        CHECK(win_mount(x.c(), 0, "/usb0/b.adf", msg, sizeof(msg)) != 0);        // D1: a replacement does not drop the disk either
        CHECK_STR(win_name(x.c(), 0), "a.adf");
        CHECK_EQ(x.fs.open_files, open);                              // and the new file is not left open
        CHECK(sec_eject(x.c(), 0, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "h.hdf") && strstr(msg, "stays mounted"));
        CHECK_STR(sec_name(x.c(), 0), "h.hdf");
        CHECK(sec_mount(x.c(), 0, "/usb0/g.hdf", msg, sizeof(msg)) != 0);
        CHECK_STR(sec_name(x.c(), 0), "h.hdf");
        CHECK_EQ(x.fs.open_files, open);
        CHECK(media_writeback(x.c(), WB_WHY_RESET, msg, sizeof(msg)) != 0);   // the common path says no as well
        CHECK(strstr(msg, "DF0") != 0);
        x.open_menu();
        x.to_return_row();
        x.m->key(KEY_RETURN);
        CHECK_EQ(x.host.returns, 0);                                  // Return to host stays
        CHECK(x.m->status_bad() && strstr(x.m->status(), "DF0") != 0);
        g_holds = 0;
        CHECK(boot_cold_reset(x.c(), msg, sizeof(msg)) != 0);
        CHECK(boot_warm_reset(x.c(), msg, sizeof(msg)) != 0);
        CHECK(boot_reload(x.c(), msg, sizeof(msg)) != 0);
        CHECK(boot_restart(x.c(), msg, sizeof(msg)) != 0);
        CHECK_EQ(g_holds, 0);                                         // the machine was never held
        CHECK_EQ(g_on, g_off);
        // the medium is back: the same actions go through and the files are those of the full sync
        x.fs.sync_fails = false;
        CHECK_EQ(win_eject(x.c(), 0, msg, sizeof(msg)), 0);
        CHECK_EQ(sec_eject(x.c(), 0, msg, sizeof(msg)), 0);
        CHECK_EQ(sec_eject(x.c(), 1, msg, sizeof(msg)), 0);
        Snap after = x.snap();
        CHECK(after == ref);
    }
    {
        // the folder disk: the host medium is full, the fold-back fails
        R x;
        x.mount_all();
        x.dirty_all();
        Snap pre = x.snap();
        x.fs.mut_budget = 0;
        CHECK(sec_eject(x.c(), 1, msg, sizeof(msg)) != 0);
        if (!strstr(msg, "hd (folder)")) printf("  msg: %s\n", msg);
        CHECK(strstr(msg, "hd (folder)") && strstr(msg, "stays mounted"));
        CHECK_STR(sec_name(x.c(), 1), "hd (folder)");                 // still mounted ...
        CHECK(x.folder() != 0 && x.folder()->dirty());                // ... with what it holds
        CHECK_EQ(x.r->io.regs[AM_HDF_MOUNT + 8] & 1, 1);
        CHECK(sec_mount_folder(x.c(), 1, "/usb0/hd2/0.1", msg, sizeof(msg)) != 0);   // D1: the replacement stops too
        CHECK_STR(sec_name(x.c(), 1), "hd (folder)");
        CHECK(boot_cold_reset(x.c(), msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "stays mounted") != 0);                     // the first medium that could not be written back is named (the floppy too, the budget is for everything)
        x.fs.mut_budget = -1;                                         // room again
        CHECK_EQ(sec_eject(x.c(), 1, msg, sizeof(msg)), 0);
        CHECK(x.snap().hd == ref.hd);                                 // nothing was lost on the way
    }

    // ---- S1: ten swaps of a disk in a row: the old medium is closed each time (one handle), and each swap takes the same time
    {
        R x;
        x.fs.write_ms = 5;
        x.fs.tick = [](void *arg, uint32_t us) { ((AmigaIo *)arg)->advance(us); };
        x.fs.tick_arg = &x.r->io;
        CHECK_EQ(win_mount(x.c(), 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK_EQ(sec_mount(x.c(), 0, "/usb0/h.hdf", msg, sizeof(msg)), 0);
        int base = x.fs.open_files;
        CHECK_EQ(base, 2);
        std::vector<uint32_t> took, took_h;
        for (int i = 0; i < 10; i++) {
            x.r->io.ddr[AM_W_ADF + (i % 80) * AM_ADF_TRACK_BYTES] ^= 0x55;     // a track of the disk that is in changes
            x.r->io.log_track(0, i % 80);
            x.r->io.delay_ms(10);
            win_service(x.c());
            uint64_t t0 = x.r->io.us;
            CHECK_EQ(win_mount(x.c(), 0, i % 2 ? "/usb0/a.adf" : "/usb0/b.adf", msg, sizeof(msg)), 0);
            took.push_back((uint32_t)(x.r->io.us - t0));
            CHECK_EQ(x.fs.open_files, base);                          // no handle is left behind
            t0 = x.r->io.us;
            CHECK_EQ(sec_mount(x.c(), 0, i % 2 ? "/usb0/h.hdf" : "/usb0/g.hdf", msg, sizeof(msg)), 0);
            took_h.push_back((uint32_t)(x.r->io.us - t0));
            CHECK_EQ(x.fs.open_files, base);
        }
        for (int i = 1; i < 10; i++) {
            CHECK_EQ(took[i], took[1]);                               // the same time every swap: nothing grows
            CHECK_EQ(took_h[i], took_h[1]);
        }
        CHECK(!x.c()->eng->win.slot[0].ndirty);
        printf("S1: ten swaps, %u  us each, %d handles open throughout\n", (unsigned)took[1], x.fs.open_files);
    }
    DONE("test_media_writeback");
}
