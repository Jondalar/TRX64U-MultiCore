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
#include "apple2_nib.h"
#include "atari_atr.h"
#include "amiga_regs.h"
#include "cpc_regs.h"

typedef std::vector<uint8_t> Bytes;

const CoreDesc *const core_table[] = { &atari800_desc, &apple2_desc, &cpc_desc, &amiga_desc, &amiga1200_desc, 0 };

// a file's bytes, empty when there is none (operator[] would insert an empty entry that FakeFs::list then trips over)
static Bytes bytes_of(FakeFs &fs, const std::string &path)
{
    auto it = fs.files.find(lower(path));
    return it == fs.files.end() ? Bytes() : it->second;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static bool all_zero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i]) return false; return true; }
static int popcount8(unsigned v) { int n = 0; for (; v; v >>= 1) n += v & 1; return n; }

// ---- Apple II: what DOS 3.3 reads -------------------------------------------------------------------------------------------------------------
static void check_apple(const Bytes &img)
{
    CHECK_EQ(img.size(), 143360u);
    if (img.size() != 143360u) return;
    auto sec = [&](int t, int s) { return &img[(size_t)t * 4096 + (size_t)s * 256]; };
    const uint8_t *v = sec(17, 0);
    CHECK_EQ(v[1], 17);                                   // the catalog starts at track 17, sector 15
    CHECK_EQ(v[2], 15);
    CHECK_EQ(v[3], 3);                                    // DOS 3.3
    CHECK_EQ(v[6], 254);                                  // volume 254
    CHECK_EQ(v[0x27], 122);                               // T/S pairs per list sector
    CHECK_EQ(v[0x34], 35);
    CHECK_EQ(v[0x35], 16);
    CHECK_EQ(v[0x36] | v[0x37] << 8, 256);                // bytes per sector
    // the catalog chain, followed as DOS follows it: 15 sectors, 15 down to 1, then 0/0; no entry in use
    int t = v[1], s = v[2], n = 0, last = 16;
    while (t != 0 && n < 40) {
        CHECK_EQ(t, 17);
        CHECK(s >= 1 && s < last);
        last = s;
        const uint8_t *c = sec(t, s);
        for (int e = 0; e < 7; e++) CHECK_EQ(c[0x0B + 35 * e], 0);   // first byte of an entry: 0 = never used
        t = c[1];
        s = c[2];
        n++;
    }
    CHECK_EQ(n, 15);
    CHECK_EQ(last, 1);
    CHECK_EQ(s, 0);
    // the free sector bitmap: four bytes a track, sectors 15..8 then 7..0; tracks 0-2 and 17 are in use
    int freeblocks = 0;
    for (int tr = 0; tr < 35; tr++) {
        const uint8_t *b = v + 0x38 + 4 * tr;
        int f = popcount8(b[0]) + popcount8(b[1]);
        bool used = tr <= 2 || tr == 17;
        CHECK_EQ(f, used ? 0 : 16);
        CHECK_EQ(b[2] | b[3], 0);
        freeblocks += f;
    }
    CHECK_EQ(freeblocks, 496);
    CHECK(all_zero(v + 0x38 + 4 * 35, 256 - 0x38 - 4 * 35));
    // nothing else on the disk
    for (int tr = 0; tr < 35; tr++) {
        for (int sc = 0; sc < 16; sc++) {
            if (tr == 17 && sc <= 15) continue;
            CHECK(all_zero(sec(tr, sc), 256));
        }
    }
    // the drive can serve it: every track nibblizes (DOS order) and reads back as the same sectors
    bool rt = true;
    for (int tr = 0; tr < 35 && rt; tr++) {
        uint8_t ring[A2N_TRACK], back[A2N_TRACK_DATA];
        a2n_nibblize_track(sec(tr, 0), A2_ORDER_DOS, tr, ring);
        rt = a2n_denibblize_track(ring, A2_ORDER_DOS, tr, back) >= 0 && !memcmp(back, sec(tr, 0), A2N_TRACK_DATA);
    }
    CHECK(rt);
}

// ---- Amiga: what AmigaDOS reads -----------------------------------------------------------------------------------------------------------------
static uint32_t carry_sum(const uint8_t *p, int longs)
{
    uint64_t s = 0;
    for (int i = 0; i < longs; i++) s += be32(p + 4 * i);
    while (s >> 32) s = (s & 0xFFFFFFFFu) + (s >> 32);
    return (uint32_t)s;
}
static uint32_t wrap_sum(const uint8_t *p, int longs)
{
    uint32_t s = 0;
    for (int i = 0; i < longs; i++) s += be32(p + 4 * i);
    return s;
}
static void check_adf(const Bytes &img, const char *name)
{
    CHECK_EQ(img.size(), 901120u);
    CHECK_EQ(img.size(), (size_t)AM_ADF_SIZE);
    if (img.size() != 901120u) return;
    auto blk = [&](int b) { return &img[(size_t)b * 512]; };
    const uint8_t *bb = blk(0);
    CHECK(bb[0] == 'D' && bb[1] == 'O' && bb[2] == 'S' && bb[3] == 0);   // OFS
    CHECK_EQ(be32(bb + 4), 0u);                           // not Install'ed: checksum 0 ...
    CHECK(carry_sum(bb, 256) != 0xFFFFFFFFu);             // ... so the Kickstart's rule for a bootable block fails and it runs nothing
    CHECK_EQ(be32(bb + 8), 880u);
    CHECK(all_zero(bb + 12, 1024 - 12));                  // no boot code
    const uint8_t *r = blk(880);
    CHECK_EQ(be32(r), 2u);                                // T_HEADER
    CHECK_EQ(be32(r + 4), 0u);
    CHECK_EQ(be32(r + 8), 0u);
    CHECK_EQ(be32(r + 12), 72u);                          // hash table size
    CHECK_EQ(be32(r + 16), 0u);
    CHECK_EQ(wrap_sum(r, 128), 0u);                       // the root block checksum
    CHECK(all_zero(r + 24, 72 * 4));                      // an empty hash table
    CHECK_EQ(be32(r + 4 * 78), 0xFFFFFFFFu);              // bitmap valid
    CHECK_EQ(be32(r + 4 * 79), 881u);
    CHECK(all_zero(r + 4 * 80, 24 * 4));
    CHECK_EQ(be32(r + 4 * 104), 0u);
    CHECK_EQ((int)r[4 * 108], (int)strlen(name));
    CHECK(!memcmp(r + 4 * 108 + 1, name, strlen(name)));
    CHECK(all_zero(r + 4 * 108 + 1 + strlen(name), 31 - strlen(name)));
    CHECK_EQ(be32(r + 4 * 124), 0u);
    CHECK_EQ(be32(r + 4 * 125), 0u);
    CHECK_EQ(be32(r + 4 * 126), 0u);
    CHECK_EQ(be32(r + 4 * 127), 1u);                      // ST_ROOT
    const uint8_t *bm = blk(881);
    CHECK_EQ(wrap_sum(bm, 128), 0u);                      // the bitmap block checksum
    int freeblocks = 0;
    for (int b = 2; b < 1760; b++) {                      // block b is bit (b - 2) % 32 of long 1 + (b - 2) / 32
        bool fr = (be32(bm + 4 + 4 * ((b - 2) / 32)) >> ((b - 2) % 32)) & 1;
        CHECK_EQ(fr, !(b == 880 || b == 881));
        freeblocks += fr;
    }
    CHECK_EQ(freeblocks, 1756);
    for (int b = 1760; b < 32 * 55 + 2; b++) CHECK(!((be32(bm + 4 + 4 * ((b - 2) / 32)) >> ((b - 2) % 32)) & 1));   // no block past the end
    for (int b = 0; b < 1760; b++) {
        if (b == 0 || b == 1 || b == 880 || b == 881) continue;
        if (!all_zero(blk(b), 512)) { CHECK(false); break; }
    }
}

// ---- Atari: DOS 2.0S through the product's ATR reader ----------------------------------------------------------------------------------------
static void check_atr(const Bytes &img)
{
    CHECK_EQ(img.size(), 92176u);
    FakeFs fs;
    fs.put("/usb0/t.atr", img);
    char err[100] = "";
    AtariDisk *d = atr_open(&fs, "/usb0/t.atr", err, sizeof(err));
    CHECK(d != 0);
    if (!d) return;
    CHECK_EQ(d->sectors(), 720u);
    CHECK_EQ(d->image_sector_size(), 128);
    uint8_t s[256];
    auto rd = [&](int n) { memset(s, 0xEE, sizeof(s)); bool ok = d->read((uint32_t)n, s); CHECK(ok); return ok; };
    rd(360);
    CHECK_EQ(s[0], 2);                                    // DOS 2.0S
    CHECK_EQ(s[1] | s[2] << 8, 707);                      // sectors on the disk, free sectors
    CHECK_EQ(s[3] | s[4] << 8, 707);
    CHECK(all_zero(s + 5, 5));
    int freecount = 0;
    for (int n = 0; n < 720; n++) {
        bool fr = (s[10 + n / 8] >> (7 - n % 8)) & 1;
        bool used = n <= 3 || (n >= 360 && n <= 368);
        CHECK_EQ(fr, !used);
        freecount += fr;
    }
    CHECK_EQ(freecount, 707);
    CHECK(all_zero(s + 100, 28));
    uint8_t vt[128];
    memcpy(vt, s, 128);
    for (int n = 1; n <= 3; n++) { rd(n); CHECK(all_zero(s, 128)); }
    for (int n = 361; n <= 368; n++) { rd(n); CHECK(all_zero(s, 128)); }         // an empty directory: no entry in use
    for (int n = 4; n <= 720; n++) { if (n >= 360 && n <= 368) continue; rd(n); if (!all_zero(s, 128)) { CHECK(false); break; } }
    delete d;
    // the header itself, byte by byte
    CHECK(img[0] == 0x96 && img[1] == 0x02);
    CHECK_EQ(img[2] | img[3] << 8 | img[6] << 16, 92160 / 16);
    CHECK_EQ(img[4] | img[5] << 8, 128);
}

// ---- CPC: the standard DSK as the u765 parses it (offsets 0x30 tracks, 0x31 sides, 0x33 track size / 256, track info at 0x100 + n * size) ---------
static void check_cpc(const Bytes &img)
{
    CHECK_EQ(img.size(), 194816u);
    CHECK(img.size() <= (size_t)CPC_DSK_MAX);
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
    case ND_APPLE_DSK: check_apple(b); break;
    case ND_ADF:       check_adf(b, vol); break;
    case ND_ATR:       check_atr(b); break;
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
        r.m->key(KEY_F7);
        CHECK(r.pick(k.label));
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
        CHECK(r.pick(k.label));
        r.m->key(KEY_RETURN);
        r.m->key(KEY_RETURN);
        for (int i = 0; i < 12; i++) r.m->key(KEY_BACK);
        for (const char *p = "Tmp"; *p; p++) r.m->key(*p);
        r.m->key(KEY_RETURN);
        finish(r);
        CHECK(r.m->status_bad());
        CHECK(strstr(r.m->status(), "exists") != 0);
        // the prompt of the next one starts with the free name of that folder
        CHECK(r.pick(k.label));
        r.m->key(KEY_RETURN);
        r.m->key(KEY_RETURN);
        CHECK_STR(r.prompt_text().c_str(), (std::string(k.prompt) + "|blank_").c_str());
        r.m->key(KEY_ESCAPE);
        // from the list of the media there is no folder of the menu: the first stick is where the folder browser starts, never /sd
        r.m->key(KEY_F3);
        CHECK(lib_nowhere(r.m));
        CHECK_STR(r.m->title(), "/");
        r.m->key(KEY_F7);
        CHECK(r.pick(k.label));
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
        Bytes b(ND_APPLE_BYTES);
        CHECK_EQ(nd_apple_dos33(b.data(), (uint32_t)b.size()), ND_APPLE_BYTES);
        check_apple(b);
        CHECK_EQ(nd_apple_dos33(b.data(), ND_APPLE_BYTES - 1), 0u);
    }
    {
        Bytes b(ND_ADF_BYTES, 0x77);
        CHECK_EQ(nd_adf_ofs(b.data(), (uint32_t)b.size(), "Work"), ND_ADF_BYTES);
        check_adf(b, "Work");
        CHECK_EQ(nd_adf_ofs(b.data(), ND_ADF_BYTES - 1, "Work"), 0u);
        // a name AmigaDOS cannot hold: ':' and '/' go, 30 characters at most, empty becomes "Empty"
        nd_adf_ofs(b.data(), (uint32_t)b.size(), "a:b/c");
        check_adf(b, "abc");
        nd_adf_ofs(b.data(), (uint32_t)b.size(), "0123456789012345678901234567890123");
        check_adf(b, "012345678901234567890123456789");
        nd_adf_ofs(b.data(), (uint32_t)b.size(), "");
        check_adf(b, "Empty");
    }
    {
        Bytes b(ND_ATR_BYTES, 0x77);
        CHECK_EQ(nd_atr_dos2(b.data(), (uint32_t)b.size()), ND_ATR_BYTES);
        check_atr(b);
        CHECK_EQ(nd_atr_dos2(b.data(), ND_ATR_BYTES - 1), 0u);
    }
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
    const Case cases[] = {
        { &apple2_desc,    "Create blank DSK", "Name of the new DSK", ".dsk", 0, ND_APPLE_DSK },
        { &amiga_desc,     "Create blank ADF", "Name of the new ADF", ".adf", 0, ND_ADF },
        { &amiga1200_desc, "Create blank ADF", "Name of the new ADF", ".adf", 0, ND_ADF },
        { &atari800_desc,  "Create blank ATR", "Name of the new ATR", ".atr", 0, ND_ATR },
        { &cpc_desc,       "Create blank DSK", "Name of the new DSK", ".dsk", 0, ND_CPC_DSK },
    };
    for (const Case &k : cases) run_case(k);

    // ---- the operation refuses what it cannot make ----
    {
        CoreRig r(&atari800_desc);
        OpResult res;
        OpIn in[3] = { in_str("path", "x"), in_str("format", "nope"), in_str("name", "") };
        CHECK(ops_call(&r.c, "disk.create", in, 3, &res) != 0);        // not a format of the list
        CHECK(r.fs.files.empty());
    }
    DONE("newdisk");
}
