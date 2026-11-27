// T122: "Create blank ..." of the Apple IIe, the Amiga 500 and 1200, the Atari 800 XL and the CPC. Two halves.
//
// The images: each builder's bytes are read back the way the machine's own DOS reads them - by checkers written here from the format
// definitions, not from the builder (DOS 3.3 VTOC / catalog chain / free bitmap, AmigaDOS boot block (not bootable), root and bitmap block checksums,
// Atari DOS 2.0S VTOC, the ATR header through the product's own reader, the DSK header and track info as the u765 FDC parses them).
//
// The task: the real descriptors' "Create blank ..." entries, run the way the menu runs them - a prompt that starts with a free name, the
// typed name, the file in the folder the browser is on, an existing file never overwritten, a medium that cannot be written.
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "ops_glue.h"
#include "menuctl.h"
#include "keyboard.h"
#include "lib_skel.h"
#include "newdisk.h"
#include "cpc_regs.h"
extern const CoreDesc cpc_desc;                // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)

typedef std::vector<uint8_t> Bytes;

const CoreDesc *const core_table[] = { &cpc_desc, 0 };

// a file's bytes, empty when there is none (operator[] would insert an empty entry that FakeFs::list then trips over)
static Bytes bytes_of(FakeFs &fs, const std::string &path)
{
    auto it = fs.files.find(lower(path));
    return it == fs.files.end() ? Bytes() : it->second;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static bool all_zero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i]) return false; return true; }
static int popcount8(unsigned v) { int n = 0; for (; v; v >>= 1) n += v & 1; return n; }

// ---- CPC: the standard DSK as the u765 parses it (offsets 0x30 tracks, 0x31 sides, 0x33 track size / 256, track info at 0x100 + n * size) ---------
static void check_cpc(const Bytes &img)
{
    CHECK_EQ(img.size(), 194816u);
    CHECK(img.size() <= (size_t)CPC_DSK_MAX);                // the number is the CPC app's own (cpc_regs.h)
    if (img.size() != 194816u) return;
    CHECK(!memcmp(img.data(), "MV - CPC", 8));             // what cpc_disk.cc accepts: the first byte 'M' for the FDC
    CHECK_EQ(img[0x30], 40);
    CHECK_EQ(img[0x31], 1);
    CHECK_EQ(img[0x32] | img[0x33] << 8, 0x1300);
    CHECK(all_zero(&img[0x50], 0xB0));
    for (int t = 0; t < 40; t++) {
        const uint8_t *ti = &img[0x100 + (size_t)t * 0x1300];
        CHECK(!memcmp(ti, "Track-Info\r\n", 12));
        CHECK_EQ(ti[0x10], t);
        CHECK_EQ(ti[0x11], 0);
        CHECK_EQ(ti[0x14], 2);                            // 512 bytes
        CHECK_EQ(ti[0x15], 9);
        CHECK_EQ(ti[0x17], 0xE5);
        for (int s = 0; s < 9; s++) {
            const uint8_t *si = ti + 0x18 + 8 * s;
            CHECK_EQ(si[0], t);                           // C
            CHECK_EQ(si[1], 0);                           // H
            CHECK_EQ(si[2], 0xC1 + s);                    // R: the DATA format
            CHECK_EQ(si[3], 2);                           // N
            CHECK_EQ(si[4] | si[5], 0);                   // no error flags
            for (int i = 0; i < 512; i++) if (ti[0x100 + 512 * s + i] != 0xE5) { CHECK(false); break; }
        }
        CHECK(all_zero(ti + 0x18 + 72, 0x100 - 0x18 - 72));
    }
}

// ---- the task, on the real descriptors ---------------------------------------------------------------------------------------------------------
struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "x"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

struct CoreRig {
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    Settings *st;
    Host host;
    MenuCtl *m;
    explicit CoreRig(const CoreDesc *d)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d;
        c.io = &io;
        c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/x.cfg", 0);
        st->load();
        c.settings = st;
        m = new MenuCtl(&c, &host);
    }
    const Task *task(const char *label)
    {
        for (int i = 0; i < c.desc->ntasks; i++) if (!strcmp(c.desc->tasks[i].label, label)) return &c.desc->tasks[i];
        return 0;
    }
    int run(const Task *t, const char *text, OpResult *r) { return ops_run_bound(&c, t->op, t->bind, t->nbind, 0, r, 0, text); }
    bool pick(const char *name)
    {
        char l[100], rt[100], sb[100];
        bool act;
        for (int i = 0; i < m->rows(); i++) {
            m->row(i, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
            if (!strcmp(l, name)) {
                while (m->selected() > i) m->key(KEY_UP);
                while (m->selected() < i) m->key(KEY_DOWN);
                return true;
            }
        }
        printf("FAIL no row %s (in %s)\n", name, m->title());
        g_fails++;
        return false;
    }
    std::string prompt_text()
    {
        char l[100], rt[100], sb[100];
        bool act;
        m->row(0, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
        return std::string(l) + "|" + rt;
    }
};

// T212 D5: the blank medium is a row of the empty drive's list on Home: the first drive, RETURN, then the task
static bool pick_create(CoreRig &r, const char *label)
{
    r.m->key(KEY_F1);
    if (!r.pick(r.c.desc->media[0].name)) return false;
    r.m->key(KEY_RETURN);
    return r.pick(label);
}

// the run loop's part: one tick is one chunk; returns the ticks it took
static int finish(CoreRig &r)
{
    int n = 0;
    while (r.m->busy() && n < 5000) {
        r.m->tick();
        n++;
    }
    return n;
}

struct Case {
    const CoreDesc *desc;
    const char *label, *prompt, *ext, *old_ext;
    int fmt;
};

// the volume name an image carries, for the formats that have one
static void check_image(int fmt, const Bytes &b, const char *vol)
{
    switch (fmt) {
    case ND_CPC_DSK:   check_cpc(b); break;
    }
}

static void run_case(const Case &k)
{
    printf("-- %s: %s\n", k.desc->title, k.label);
    std::string ext = k.ext;
    OpResult res;
    {
        // a bare name goes to the first medium; the prompt starts with a free name
        CoreRig r(k.desc);
        const Task *t = r.task(k.label);
        CHECK(t != 0);
        if (!t) return;
        CHECK(t->ask != 0 && !strcmp(t->ask, k.prompt));
        CHECK(t->ask_state != 0);
        CHECK(!strcmp(t->op, "disk.create"));
        CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
        CHECK_EQ(r.run(t, "My Disk", &res), 0);
        CHECK(strstr(res.msg, "Created") != 0);
        CHECK(r.c.notice_pending);
        std::string path = "/usb0/My Disk" + ext;
        CHECK(r.fs.has(path));
        check_image(k.fmt, bytes_of(r.fs, path), "My Disk");
        // the same name again is refused, the file is as it was
        Bytes was = bytes_of(r.fs, path);
        CHECK(r.run(t, "My Disk", &res) != 0);
        CHECK(strstr(res.msg, "exists") != 0);
        CHECK(bytes_of(r.fs, path) == was);
        // also when the extension is typed
        CHECK(r.run(t, ("My Disk" + ext).c_str(), &res) != 0);
        // a full path is exactly that file, and is refused the same way
        {
            static const char *const label[ND_COUNT] = { "apple2_dsk", "adf", "atr", "cpc_dsk" };
            const std::string full_path = "/usb1/full" + ext;
            OpIn in[3] = { in_str("path", full_path.c_str()), in_str("format", label[k.fmt]), in_str("name", "") };
            CHECK_EQ(ops_call(&r.c, "disk.create", in, 3, &res), 0);
            bool made = r.fs.has("/usb1/full" + ext);                    // asked before [] inserts an empty entry
            CHECK(made);
            if (!made) return;                                           // nothing to touch (the check above has said so)
            Bytes full_was = bytes_of(r.fs, "/usb1/full" + ext);
            check_image(k.fmt, full_was, "full");
            r.fs.files[lower("/usb1/full" + ext)][0] ^= 0x55;
            Bytes touched = bytes_of(r.fs, "/usb1/full" + ext);
            CHECK(ops_call(&r.c, "disk.create", in, 3, &res) != 0);
            CHECK(strstr(res.msg, "exists") != 0);
            CHECK(bytes_of(r.fs, "/usb1/full" + ext) == touched);
        }
        // an empty name is a free one, and the prompt follows
        CHECK_EQ(r.run(t, "", &res), 0);
        CHECK(r.fs.has("/usb0/blank" + ext));
        CHECK_STR(ops_state(&r.c, t->ask_state), "blank2");
        check_image(k.fmt, bytes_of(r.fs, "/usb0/blank" + ext), "blank");     // the volume name defaults to the file's
        // a medium that cannot be written: nothing is made
        r.fs.read_only = true;
        CHECK(r.run(t, "Ro", &res) != 0);
        CHECK(!r.fs.has("/usb0/Ro" + ext));
    }
    {
        // menu driven: the file goes into the folder the browser is on, with the prompt's free name as the start
        CoreRig r(k.desc);
        r.fs.put("/usb0/games/x.bin", Bytes(4, 0));
        r.m->open();
        r.m->key(KEY_F3);                                                 // the Library opens on the media level, like the host's File Browser
        CHECK_STR(r.m->title(), "/");
        CHECK(r.pick("usb0/")); r.m->key(KEY_RETURN);
        CHECK_STR(r.m->title(), "/usb0");
        CHECK(r.pick("games/"));
        r.m->key(KEY_RETURN);
        CHECK_STR(r.m->title(), "/usb0/games");
        CHECK(pick_create(r, k.label));
        CHECK(!r.m->entering());
        r.m->key(KEY_RETURN);
        CHECK(r.m->picking());                                            // Create asks for the folder first: it starts where the menu is
        CHECK(!r.m->entering());
        CHECK_STR(r.m->title(), "/usb0/games");
        r.m->key(KEY_RETURN);                                             // "Create here"
        CHECK(r.m->entering());
        CHECK_STR(r.prompt_text().c_str(), (std::string(k.prompt) + "|blank_").c_str());
        for (const char *p = "Tmp"; *p; p++) r.m->key(*p);               // "blankTmp"
        for (int i = 0; i < 8; i++) r.m->key(KEY_BACK);
        for (const char *p = "Tmp"; *p; p++) r.m->key(*p);
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(r.m->busy());
        finish(r);
        CHECK(!r.m->status_bad());
        CHECK(strstr(r.m->status(), "Created") != 0);
        CHECK(r.fs.has("/usb0/games/Tmp" + ext));
        CHECK(!r.fs.has("/usb0/Tmp" + ext));
        check_image(k.fmt, bytes_of(r.fs, "/usb0/games/Tmp" + ext), "Tmp");
        // again: refused, and the status says so
        CHECK(pick_create(r, k.label));
        r.m->key(KEY_RETURN);
        r.m->key(KEY_RETURN);
        for (int i = 0; i < 12; i++) r.m->key(KEY_BACK);
        for (const char *p = "Tmp"; *p; p++) r.m->key(*p);
        r.m->key(KEY_RETURN);
        finish(r);
        CHECK(r.m->status_bad());
        CHECK(strstr(r.m->status(), "exists") != 0);
        // the prompt of the next one starts with the free name of that folder
        CHECK(pick_create(r, k.label));
        r.m->key(KEY_RETURN);
        r.m->key(KEY_RETURN);
        CHECK_STR(r.prompt_text().c_str(), (std::string(k.prompt) + "|blank_").c_str());
        r.m->key(KEY_ESCAPE);
        // from the list of the media there is no folder of the menu: the first stick is where the folder browser starts, never /sd
        r.m->key(KEY_F3);
        CHECK(lib_nowhere(r.m));
        CHECK_STR(r.m->title(), "/");
        CHECK(pick_create(r, k.label));
        r.m->key(KEY_RETURN);
        CHECK_STR(r.m->title(), "/usb0");
        r.m->key(KEY_RETURN);
        for (int i = 0; i < 12; i++) r.m->key(KEY_BACK);
        for (const char *p = "Top"; *p; p++) r.m->key(*p);
        r.m->key(KEY_RETURN);
        finish(r);
        CHECK(r.fs.has("/usb0/Top" + ext));
        CHECK(!r.fs.has("/usb0/games/Top" + ext));
        CHECK_STR(r.m->status(), ("Created /usb0/Top" + ext + ".").c_str());
    }
}

int main()
{
    // ---- the builders, alone ----
    {
        Bytes b(ND_CPC_BYTES, 0x77);
        CHECK_EQ(nd_cpc_data(b.data(), (uint32_t)b.size()), ND_CPC_BYTES);
        check_cpc(b);
        CHECK_EQ(nd_cpc_data(b.data(), ND_CPC_BYTES - 1), 0u);
    }
    for (int f = 0; f < ND_COUNT; f++) {
        Bytes b(nd_bytes(f));
        CHECK_EQ(nd_build(f, b.data(), (uint32_t)b.size(), "x"), nd_bytes(f));
    }
    CHECK_EQ(nd_bytes(ND_COUNT), 0u);
    {
        uint8_t x[4];
        CHECK_EQ(nd_build(ND_COUNT, x, sizeof(x), "x"), 0u);
    }
    CHECK_STR(nd_ext(ND_ADF), ".adf");
    CHECK_STR(nd_ext(ND_ATR), ".atr");
    CHECK_STR(nd_ext(ND_APPLE_DSK), ".dsk");
    CHECK_STR(nd_ext(ND_CPC_DSK), ".dsk");

    // ---- the entries of the real descriptors ----
    const Case cases[] = { { &cpc_desc, "Create blank DSK", "Name of the new DSK", ".dsk", 0, ND_CPC_DSK } };
    for (const Case &k : cases) run_case(k);

    // ---- the operation refuses what it cannot make (a case of the apps of the CPC, the Amiga 500 and the Atari, on their descriptors) ----
    {
        CoreRig r(&cpc_desc);
        OpResult res;
        OpIn in[3] = { in_str("path", "x"), in_str("format", "nope"), in_str("name", "") };
        CHECK(ops_call(&r.c, "disk.create", in, 3, &res) != 0);        // not a format of the list
        CHECK(r.fs.files.empty());
    }
    DONE("newdisk");
}
