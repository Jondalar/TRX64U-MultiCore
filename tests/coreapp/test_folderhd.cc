// T128: a folder on the stick as an Amiga hard disk (spec docs/specs/T128-folder-hdd.md section 2, scenarios S1-S8 and S10; S9 is in fmfs64/).
// The Amiga side is folderhd_bench.h: an independent reader and writer of FFS volumes that shares no code with the synthesiser.
#include "folderhd_bench.h"
#include "boardprintf.h"                                   // every snprintf of the product is checked against the firmware's printf
#include "folderhd_impl.h"
#include "winslot.h"
#include <time.h>

using namespace bench;

// ---- fixtures ----
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
static uint16_t fatdate(int y, int m, int d) { return (uint16_t)(((y - 1980) << 9) | (m << 5) | d); }
static uint16_t fattime(int h, int mi, int s) { return (uint16_t)((h << 11) | (mi << 5) | (s / 2)); }

// the Amiga's day count (since 1978-01-01) of a calendar date, own formula (not the one of the product)
static uint32_t amiga_days(int y, int m, int d)
{
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t days = 0;
    for (int yy = 1978; yy < y; yy++) days += ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0) ? 366 : 365;
    for (int mm = 1; mm < m; mm++) days += dm[mm - 1] + ((mm == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 1 : 0);
    return days + (uint32_t)(d - 1);
}

static void put_dated(FakeFs &fs, const std::string &path, const std::vector<uint8_t> &data, uint16_t fd = 0xFFFF, uint16_t ft = 0xFFFF, uint8_t attr = 0)
{
    fs.put(path, data);
    FakeFs::Meta m;
    m.fdate = fd != 0xFFFF ? fd : fatdate(2020, 6, 15);
    m.ftime = ft != 0xFFFF ? ft : fattime(10, 20, 30);
    m.attr = attr;
    fs.meta[lower(path)] = m;
}

// the tree of S1: 3 directories, 40 files
static void make_tree(FakeFs &fs, const std::string &root)
{
    fs.mkdir((root + "/Empty").c_str());
    for (int i = 0; i < 40; i++) {
        const char *dir = i < 14 ? "/C" : (i < 28 ? "/S" : "");
        char nm[64];
        snprintf(nm, sizeof(nm), "%s/file%02d.%s", dir, i, i % 3 ? "dat" : "txt");
        put_dated(fs, root + nm, pat((uint32_t)i, (size_t)((i * 997 + 13) % 6000)));
    }
}

struct Env {
    FakeFs *fs;
    AmigaRig *r;
    bool own;
    explicit Env(FakeFs *f, const CoreDesc *d = &amiga1200_desc, bool sectors = true) : fs(f), own(false)
    {
        r = new AmigaRig(f, d);
        r->io.regs[AM_FEAT] = sectors ? 0x01 : 0x00;
    }
    ~Env() { delete r; }
    int mount(int unit, const std::string &folder, const char *gb, char *msg, int n)
    {
        std::string spec = folder + "/" + gb;
        return sec_mount_folder(&r->c, unit, spec.c_str(), msg, n);
    }
    FolderDisk *disk(int unit) { return (FolderDisk *)r->c.eng->sec.unit[unit].file; }
};

static int r_unit_present(Env &e, int unit = 0) { return e.r->io.regs[AM_HDF_MOUNT + 8 * unit] & 1; }

static uint32_t size_reg(AmigaRig &r, int unit)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)r.io.regs[AM_HDF_MOUNT + 8 * unit + 4 + i] << (8 * i);
    return v;
}

// whole host tree: lower-cased path below root -> bytes (directories: marked)
static std::map<std::string, std::vector<uint8_t>> host_files(FakeFs &fs, const std::string &root)
{
    std::map<std::string, std::vector<uint8_t>> m;
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

// FOLDERHD_DUMP=<dir>: writes the disk as <dir>/<name>.img for tests/coreapp/folderhd_amitools.py (spec section 5)
static void maybe_dump(Env &e, int unit, const char *name)
{
    const char *d = getenv("FOLDERHD_DUMP");
    if (!d) return;
    char path[300];
    snprintf(path, sizeof(path), "%s/%s.img", d, name);
    FILE *f = fopen(path, "wb");
    Dev dev(*e.r, unit);
    uint8_t b[512];
    uint32_t total = size_reg(*e.r, unit);
    for (uint32_t l = 0; f && l < total; l++) { dev.read(l, b); fwrite(b, 1, 512, f); }
    if (f) fclose(f);
}

// S1 + S2 + S3 helper: all the reader's checks and a byte compare against the host tree. Returns the number of problems.
static int verify_volume(Env &e, int unit, FakeFs &fs, const std::string &root, const char *what)
{
    Dev dev(*e.r, unit);
    Reader rd(dev);
    bool ok = rd.check_all();
    for (auto &er : rd.errs) printf("  %s: reader: %s\n", what, er.c_str());
    int bad = ok ? 0 : 1;
    auto host = host_files(fs, root);
    std::set<std::string> seen;
    for (auto &en : rd.ents) {
        if (en.dir) continue;
        auto it = host.end();
        for (auto h = host.begin(); h != host.end(); ++h) if (fold(h->first) == fold(en.path)) it = h;
        if (it == host.end()) { printf("  %s: Amiga file %s has no host file\n", what, en.path.c_str()); bad++; continue; }
        seen.insert(it->first);
        if (en.size != it->second.size()) { printf("  %s: size of %s: %u vs %zu\n", what, en.path.c_str(), en.size, it->second.size()); bad++; continue; }
        if (rd.content(en) != it->second) { printf("  %s: content of %s differs\n", what, en.path.c_str()); bad++; }
    }
    for (auto &h : host) {
        if (h.first.size() > 5 && !strcasecmp(h.first.c_str() + h.first.size() - 5, ".uaem")) continue;
        if (!seen.count(h.first)) { printf("  %s: host file %s is not on the Amiga side\n", what, h.first.c_str()); bad++; }
    }
    return bad;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
static void s1_mount()
{
    FakeFs fs;
    make_tree(fs, "/usb0/hd");
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "1", msg, sizeof(msg)), 0);
    if (g_fails) printf("  msg: %s\n", msg);
    Dev dev(*e.r, 0);
    uint8_t s0[512];
    CHECK(dev.read(0, s0));
    CHECK(!memcmp(s0, "RDSK", 4));
    Nano n = nano_parse(s0);                                  // the gateware's own check, copied from nanomig.v
    CHECK(n.present);
    CHECK_EQ(n.sec, 63);
    CHECK_EQ(n.heads, 16);
    uint32_t sectors = size_reg(*e.r, 0);                     // HDF_SECT = 1: sectors
    CHECK(sectors > 0);
    CHECK(n.cyl * n.sec * n.heads >= sectors);
    CHECK_EQ(be(s0, 16), n.cyl);                              // the whole longword, not only its low 16 bits
    CHECK_EQ(n.cyl * 1008, sectors);                          // the announced size is whole cylinders
    CHECK_EQ(r_unit_present(e), 1);
    CHECK_STR(sec_name(&e.r->c, 0), "hd (folder)");
    // CHS arithmetic of the gateware (nanomig.v:696) reaches the last sector announced, and not beyond
    CHECK_EQ(nano_lba(n, n.cyl - 1, n.heads - 1, n.sec), sectors - 1);
    uint8_t last[512];
    CHECK(dev.read(sectors - 1, last));
#ifndef TEST_APP_A1200                   // the A500 is in the shared app only
    // the A500 counts bytes
    {
        FakeFs fs2;
        make_tree(fs2, "/usb0/hd");
        Env e2(&fs2, &amiga_desc, false);
        CHECK_EQ(e2.mount(0, "/usb0/hd", "1", msg, sizeof(msg)), 0);
        uint32_t bytes = size_reg(*e2.r, 0);
        CHECK_EQ((uint64_t)bytes, (uint64_t)sectors * 512);
        CHECK(bytes % 512 == 0);
        // more than the 32 bit register holds is not offered: 5 GB are refused with the reason
        CHECK(e2.mount(1, "/usb0/hd", "5", msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "can back") != 0);
        CHECK_EQ(r_unit_present(e2, 1), 0);
        CHECK_EQ(e2.mount(1, "/usb0/hd", "3", msg, sizeof(msg)), 0);       // 3 GB are
        CHECK(size_reg(*e2.r, 1) < 0xFFFFFFFFu);
    }
#endif
}


// ---------------------------------------------------------------------------------------------------------------------------------------
static void s2_valid_volume()
{
    FakeFs fs;
    make_tree(fs, "/usb0/hd");
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    CHECK_EQ(verify_volume(e, 0, fs, "/usb0/hd", "S2"), 0);
    Dev dev(*e.r, 0);
    Reader rd(dev);
    CHECK(rd.check_all());
    int dirs = 0, files = 0;
    for (auto &en : rd.ents) (en.dir ? dirs : files)++;
    CHECK_EQ(files, 40);
    CHECK_EQ(dirs, 3);                                       // C, S, Empty
#ifndef TEST_APP_A1200                   // the A500 is in the shared app only
    // the A500 core sees the same volume
    FakeFs fs2;
    make_tree(fs2, "/usb0/hd");
    Env e2(&fs2, &amiga_desc, false);
    CHECK_EQ(e2.mount(0, "/usb0/hd", "1", msg, sizeof(msg)), 0);
    CHECK_EQ(verify_volume(e2, 0, fs2, "/usb0/hd", "S2/A500"), 0);
#endif
    maybe_dump(e, 0, "s2");
}

// ---------------------------------------------------------------------------------------------------------------------------------------
static void s3_content()
{
    SparseFs fs;
    const uint64_t sizes[7] = { 0, 1, 511, 512, 513, 72 * 512, 72 * 512 + 1 };
    for (int i = 0; i < 7; i++) {
        char nm[64];
        snprintf(nm, sizeof(nm), "/usb0/hd/s%d.bin", i);
        put_dated(fs, nm, pat((uint32_t)(100 + i), (size_t)sizes[i]));
    }
    // 73 data blocks and more: an extension block; 72*512+1 bytes is the first size that needs it
    put_dated(fs, "/usb0/hd/ext.bin", pat(7, 144 * 512 + 5));                       // 145 data blocks, 2 extension blocks
    // two files of 3 GiB + 16: the second lies beyond the 4 GiB mark of the volume
    const uint64_t BIG = 3 * GiB + 16;
    std::vector<uint8_t> m1 = pat(0x11, 16), m3 = pat(0x33, 16), n1 = pat(0x21, 16), n3 = pat(0x43, 16);
    for (int k = 0; k < 2; k++) {
        std::string big = k ? "/usb0/hd/big2.bin" : "/usb0/hd/big.bin";
        fs.put_big(big, BIG);
        FakeFs::Meta m; m.fdate = fatdate(2020, 1, 1); m.ftime = 0;
        fs.meta[lower(big)] = m;
        CoreFile *f = fs.open(big.c_str(), true);
        CHECK(f->write(1 * GiB, k ? n1.data() : m1.data(), 16));
        CHECK(f->write(3 * GiB, k ? n3.data() : m3.data(), 16));
        fs.close(f);
    }
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "7", msg, sizeof(msg)), 0);
    if (g_fails) printf("  msg: %s\n", msg);
    Dev dev(*e.r, 0);
    Reader rd(dev);
    bool ok = rd.check_all();
    for (auto &er : rd.errs) printf("  S3: reader: %s\n", er.c_str());
    CHECK(ok);
    for (int i = 0; i < 7; i++) {
        char nm[64];
        snprintf(nm, sizeof(nm), "s%d.bin", i);
        const Ent *en = rd.find(nm);
        CHECK(en != 0);
        if (!en) continue;
        CHECK_EQ(en->size, sizes[i]);
        CHECK_EQ(en->data.size(), (sizes[i] + 511) / 512);
        CHECK_EQ(en->exts.size(), sizes[i] > 72 * 512 ? 1 : 0);
        std::vector<uint8_t> want = pat((uint32_t)(100 + i), (size_t)sizes[i]);
        CHECK(rd.content(*en) == want);
    }
    const Ent *ex = rd.find("ext.bin");
    CHECK(ex && ex->exts.size() == 2 && ex->data.size() == 145);
    CHECK(ex && rd.content(*ex) == pat(7, 144 * 512 + 5));
    for (int k = 0; k < 2; k++) {
        const Ent *bg = rd.find(k ? "big2.bin" : "big.bin");
        CHECK(bg != 0);
        if (!bg) continue;
        CHECK_EQ(bg->size, (uint32_t)BIG);                                          // 3 GiB + 16 still fits the 32 bit size
        CHECK_EQ(bg->data.size(), (BIG + 511) / 512);
        uint8_t blk[512];
        rd.block_of(*bg, (uint32_t)(1 * GiB / 512), blk);
        CHECK(!memcmp(blk, k ? n1.data() : m1.data(), 16));
        rd.block_of(*bg, (uint32_t)(3 * GiB / 512), blk);
        CHECK(!memcmp(blk, k ? n3.data() : m3.data(), 16));
        bool rest = true;
        for (int i = 16; i < 512; i++) rest &= blk[i] == 0;                        // the last block is 16 bytes long: the rest is zero
        CHECK(rest);
        rd.block_of(*bg, (uint32_t)(2 * GiB / 512), blk);
        bool zero = true;
        for (int i = 0; i < 512; i++) zero &= blk[i] == 0;
        CHECK(zero);
        // the data of the second file is beyond byte 4 GiB of the volume
        if (k) CHECK((uint64_t)(bg->data[(uint32_t)(3 * GiB / 512)] + rd.base) * 512 > 4 * GiB);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
static void s4_names()
{
    FakeFs fs;
    const std::string R = "/usb0/hd";
    put_dated(fs, R + "/Readme.TXT", pat(1, 100));
    put_dated(fs, R + "/a%25b", pat(2, 10));                                         // 'a%b'
    put_dated(fs, R + "/tab%09x", pat(3, 10));                                       // 'tab' 9 'x'
    put_dated(fs, R + "/q%2Fr", pat(4, 10));                                         // a '/' in an Amiga name: left out
    put_dated(fs, R + "/a:b", pat(5, 10));                                           // ':' in an Amiga name: left out
    put_dated(fs, R + "/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmn", pat(6, 10));      // 40 bytes
    put_dated(fs, R + "/Longname_for_the_collision_test_01.txt", pat(7, 10));
    put_dated(fs, R + "/Longname_for_the_collision_test_02.txt", pat(8, 10));
    put_dated(fs, R + "/\xC3\x9C" "ber.txt", pat(9, 10));                               // Über.txt
    put_dated(fs, R + "/\xE6\x97\xA5\xE6\x9C\xAC" ".txt", pat(10, 10));                 // not Latin-1: left out
    put_dated(fs, R + "/ro.txt", pat(11, 10), 0xFFFF, 0xFFFF, FS_ATTR_RDONLY);
    put_dated(fs, R + "/.DS_Store", pat(12, 10));
    put_dated(fs, R + "/hid.txt", pat(13, 10), 0xFFFF, 0xFFFF, FS_ATTR_HIDDEN);
    put_dated(fs, R + "/sys.txt", pat(14, 10), 0xFFFF, 0xFFFF, FS_ATTR_SYSTEM);
    put_dated(fs, R + "/Old80.txt", pat(15, 10), fatdate(1980, 1, 1), fattime(0, 0, 0));
    put_dated(fs, R + "/Leap.txt", pat(16, 10), fatdate(2024, 2, 29), fattime(23, 59, 58));
    put_dated(fs, R + "/Far.txt", pat(17, 10), fatdate(2107, 12, 31), fattime(0, 0, 0));
    put_dated(fs, R + "/doc.txt", pat(18, 10));
    fs.put_text(R + "/doc.txt.uaem", "----rw-d 2020-05-17 12:34:56.78 hello there\n");
    put_dated(fs, R + "/a/Foo", pat(19, 10));
    put_dated(fs, R + "/b/foo", pat(20, 10));
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, R, "0.05", msg, sizeof(msg)), 0);
    if (g_fails) printf("  msg: %s\n", msg);
    maybe_dump(e, 0, "s4");
    Dev dev(*e.r, 0);
    Reader rd(dev);
    bool ok = rd.check_all();
    for (auto &er : rd.errs) printf("  S4: reader: %s\n", er.c_str());
    CHECK(ok);
    std::set<std::string> got;
    for (auto &en : rd.ents) {
        got.insert(en.path);
        std::string nm = en.path.substr(en.path.rfind('/') == std::string::npos ? 0 : en.path.rfind('/') + 1);
        CHECK(nm.size() <= 30);
    }
    const char *want[] = { "Readme.TXT", "a%b", "tab\tx", "ABCDEFGHIJKLMNOPQRSTUVWXYZa~01", "Longname_for_the_collision_~01", "Longname_for_the_collision_~02",
                           "\xDC" "ber.txt", "ro.txt", "Old80.txt", "Leap.txt", "Far.txt", "doc.txt", "a", "b", "a/Foo", "b/foo" };
    for (const char *w : want) CHECK(got.count(w) == 1);
    CHECK_EQ(got.size(), sizeof(want) / sizeof(want[0]));
    // the colliding pair is numbered in the order of the host names
    {
        const Ent *a = rd.find("Longname_for_the_collision_~01"), *b = rd.find("Longname_for_the_collision_~02");
        CHECK(a && b);
        if (a && b) {
            CHECK(rd.content(*a) == pat(7, 10));
            CHECK(rd.content(*b) == pat(8, 10));
        }
    }
    // protection: the read only file is denied W and D (bits 2 and 0), the others are all allowed
    {
        const Ent *ro = rd.find("ro.txt"), *pl = rd.find("Readme.TXT"), *dc = rd.find("doc.txt");
        CHECK(ro && ro->prot == 0x05);
        CHECK(pl && pl->prot == 0);
        CHECK(dc && dc->prot == 0x02 && dc->comment == "hello there");                // the side file: 'e' denied, a comment
        CHECK(dc && dc->days == amiga_days(2020, 5, 17) && dc->mins == 12 * 60 + 34 && dc->ticks == 56 * 50 + 39);
    }
    // dates: days, minutes, ticks of the FAT time (2 s resolution)
    {
        const Ent *o = rd.find("Old80.txt"), *l = rd.find("Leap.txt"), *f = rd.find("Far.txt");
        CHECK(o && o->days == amiga_days(1980, 1, 1) && o->mins == 0 && o->ticks == 0);
        CHECK(l && l->days == amiga_days(2024, 2, 29) && l->mins == 23 * 60 + 59 && l->ticks == 58 * 50);
        CHECK(f && f->days == amiga_days(2107, 12, 31) && f->mins == 0 && f->ticks == 0);
    }
    // the report names what was left out
    {
        const char *rep = e.disk(0)->report();
        CHECK(strstr(rep, "q%2Fr") != 0);
        CHECK(strstr(rep, "a:b") != 0);
        CHECK(strstr(rep, "hid.txt") != 0 || strstr(rep, ".DS_Store") != 0);
    }
    // stable across two mounts, and the same on the other unit
    {
        CHECK_EQ(e.mount(1, R, "1", msg, sizeof(msg)), 0);
        Dev dev1(*e.r, 1);
        Reader r1(dev1);
        CHECK(r1.check_all());
        CHECK_EQ(r1.ents.size(), rd.ents.size());
        for (auto &en : rd.ents) {
            const Ent *o = r1.find(en.path);
            CHECK(o != 0 && o->blk == en.blk && o->size == en.size);
        }
    }
    // FS-UAE's escapes (od-fs/fsdb_host.cpp), from the Amiga name to the host name
    {
        struct { const char *an; const char *host; } t[] = {
            { "AUX", "AU%58" }, { "con.txt", "co%6e.txt" }, { "LPT1", "LP%541" }, { "name.", "name%2e" }, { "a b ", "a b%20" }, { "x.uaem", "x.uae%6d" },
            { "Wild*Card?", "Wild%2aCard%3f" }, { "a/b", "a%2fb" }, { "50%", "50%25" }, { "\xDC" "ber", "\xC3\x9C" "ber" }, { "COMMAND", "COMMAND" }, { "x\x01y", "x%01y" },
        };
        for (auto &c : t) {
            char out[128];
            int n = folderhd_aname_to_host((const uint8_t *)c.an, (int)strlen(c.an), out, sizeof(out));
            CHECK(n > 0 && !strcmp(out, c.host));
            if (n <= 0 || strcmp(out, c.host)) printf("  %s -> %s (want %s)\n", c.an, n > 0 ? out : "(fail)", c.host);
            uint8_t back[64];
            int bn = folderhd_host_to_aname(c.host, back, sizeof(back));
            CHECK(bn == (int)strlen(c.an) && !memcmp(back, c.an, (size_t)bn));
        }
    }
}


// ---------------------------------------------------------------------------------------------------------------------------------------
typedef std::map<std::string, std::vector<uint8_t>> FileMap;

// the Amiga's view of a mounted unit: path -> bytes, plus the directories
static FileMap view_map(Env &e, int unit, std::set<std::string> *dirs, const char *what)
{
    Dev dev(*e.r, unit);
    Reader rd(dev);
    bool ok = rd.check_all();
    for (auto &er : rd.errs) printf("  %s: reader: %s\n", what, er.c_str());
    CHECK(ok);
    FileMap m;
    for (auto &en : rd.ents) {
        if (en.dir) { if (dirs) dirs->insert(en.path); }
        else m[en.path] = rd.content(en);
    }
    return m;
}

static bool same_maps(const FileMap &a, const FileMap &b, const char *what)
{
    bool same = true;
    for (auto &kv : a) {
        auto it = b.find(kv.first);
        if (it == b.end()) { printf("  %s: %s only on the left (%zu bytes)\n", what, kv.first.c_str(), kv.second.size()); same = false; }
        else if (it->second != kv.second) {
            size_t i = 0;
            while (i < kv.second.size() && i < it->second.size() && kv.second[i] == it->second[i]) i++;
            printf("  %s: %s differs: %zu vs %zu bytes, first difference at %zu\n", what, kv.first.c_str(), kv.second.size(), it->second.size(), i);
            same = false;
        }
    }
    for (auto &kv : b) if (!a.count(kv.first)) { printf("  %s: %s only on the right (%zu bytes)\n", what, kv.first.c_str(), kv.second.size()); same = false; }
    return same;
}

static std::string scratch_of(int unit) { return "/usb0/trx64/folderhd" + std::to_string(unit) + ".scratch"; }
static int leftovers(FakeFs &fs)            // temp files of the fold-back below <root>/trx64
{
    int n = 0;
    for (auto &kv : fs.files) if (kv.first.compare(0, 11, "/usb0/trx64") == 0 && kv.first.find(".fb") != std::string::npos) n++;
    return n;
}

static void s5_straight_write()
{
    FakeFs fs;
    make_tree(fs, "/usb0/hd");
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    FileMap before = host_files(fs, "/usb0/hd");
    Dev dev(*e.r, 0);
    Reader rd(dev);
    CHECK(rd.check_all());
    const Ent *f = rd.find("file28.dat");                                    // 3929 bytes: 8 blocks, the last one partial
    CHECK(f && f->data.size() == 8);
    if (!f) return;
    std::vector<uint8_t> nb = pat(777, 512);
    CHECK(dev.write(rd.base + f->data[2], nb.data()));                       // a data block, header unchanged
    FileMap after;
    // nothing has reached the host: the write is kept and read back from there (the metadata that owns the block is not known yet, see 5.5 no. 2)
    CHECK(host_files(fs, "/usb0/hd") == before);
    FhStats st = e.disk(0)->stats();
    CHECK_EQ(st.delta_writes, 1);
    uint8_t back[512];
    CHECK(dev.read(rd.base + f->data[2], back) && !memcmp(back, nb.data(), 512));
    // the last, partial block likewise
    std::vector<uint8_t> tail = pat(778, 512);
    CHECK(dev.write(rd.base + f->data[7], tail.data()));
    CHECK(host_files(fs, "/usb0/hd") == before);
    CHECK(dev.read(rd.base + f->data[7], back) && !memcmp(back, tail.data(), 512));
    // the fold-back finds a file with its header as it was and puts exactly those two blocks into the host file, in place (nothing is rewritten)
    fs.mutations = 0;
    CHECK(e.disk(0)->sync());
    after = host_files(fs, "/usb0/hd");
    CHECK(after["file28.dat"].size() == 3929);
    CHECK(!memcmp(after["file28.dat"].data() + 2 * 512, nb.data(), 512));
    CHECK(!memcmp(after["file28.dat"].data() + 7 * 512, tail.data(), 3929 - 7 * 512));
    {
        FileMap exp = before;
        memcpy(exp["file28.dat"].data() + 2 * 512, nb.data(), 512);
        memcpy(exp["file28.dat"].data() + 7 * 512, tail.data(), 3929 - 7 * 512);
        CHECK(after == exp);                                                       // every other file, every other byte
    }
    CHECK_EQ(e.disk(0)->stats().patched, 1);
    CHECK_EQ(e.disk(0)->stats().rewritten, 0);
    CHECK(!fs.has(scratch_of(0)) || fs.files[lower(scratch_of(0))].empty());
    CHECK_EQ(leftovers(fs), 0);

    // CONTROL of the scenario: on a read only medium the same write changes no byte of the file; the Amiga still reads what it wrote (D8)
    FakeFs ro;
    make_tree(ro, "/usb0/hd");
    ro.read_only = true;
    Env e2(&ro);
    CHECK_EQ(e2.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    CHECK(strstr(sec_name(&e2.r->c, 0), "read only, changes lost at eject") != 0);
    CHECK(!e2.disk(0)->host_writable());
    FileMap b2 = host_files(ro, "/usb0/hd");
    Dev dev2(*e2.r, 0);
    Reader rd2(dev2);
    CHECK(rd2.check_all());
    const Ent *f2 = rd2.find("file28.dat");
    CHECK(f2 != 0);
    if (f2) {
        CHECK(dev2.write(rd2.base + f2->data[2], nb.data()));
        CHECK(host_files(ro, "/usb0/hd") == b2);
        CHECK(dev2.read(rd2.base + f2->data[2], back) && !memcmp(back, nb.data(), 512));
        CHECK_EQ(e2.disk(0)->stats().ro_writes, 1);
        CHECK(e2.disk(0)->sync());
        CHECK(host_files(ro, "/usb0/hd") == b2);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
static void make_s6(FakeFs &fs)
{
    const std::string R = "/usb0/hd";
    put_dated(fs, R + "/A", pat(1, 5000));
    put_dated(fs, R + "/B", pat(2, 3000));
    put_dated(fs, R + "/C", pat(3, 700));
    put_dated(fs, R + "/E", pat(4, 900));
    put_dated(fs, R + "/F", pat(5, 200));
    fs.mkdir((R + "/empty").c_str());
    put_dated(fs, R + "/Keep/k.txt", pat(6, 1500));
}

// what the Amiga does in S6; fills the expected host tree
static void s6_ops(Driver &dr, FileMap &expect, std::set<std::string> &dirs)
{
    uint32_t root = dr.root;
    std::vector<uint8_t> nw = pat(10, 3072);
    dr.create_file(root, "new.txt", nw);
    expect["new.txt"] = nw;
    std::vector<uint8_t> ap = pat(11, 100 * 1024);
    dr.append(dr.resolve("A"), ap);
    expect["A"] = pat(1, 5000);
    expect["A"].insert(expect["A"].end(), ap.begin(), ap.end());
    dr.truncate(dr.resolve("B"), 10);
    {
        std::vector<uint8_t> b0 = pat(2, 3000);
        expect["B"].assign(b0.begin(), b0.begin() + 10);
    }
    dr.rename(root, dr.resolve("C"), root, "D");
    expect["D"] = pat(3, 700);
    expect.erase("C");
    dr.remove(root, dr.resolve("E"));
    expect.erase("E");
    uint32_t d2 = dr.mkdir(root, "dir2");
    dr.create_file(d2, "inner.bin", pat(12, 700));
    expect["dir2/inner.bin"] = pat(12, 700);
    dirs.insert("dir2");
    dr.mkdir(root, "newempty");                                                    // a directory with nothing in it exists on the host only if it was made
    dirs.insert("newempty");
    dr.rmdir(root, dr.resolve("empty"));
    dirs.erase("empty");
    dr.flush();
}

static void s6_fold_with(bool first_fit);
static void s6_fold() { s6_fold_with(false); s6_fold_with(true); first_fit_flag() = false; }

static void s6_fold_with(bool first_fit)
{
    first_fit_flag() = first_fit;
    printf("-- S6 %s\n", first_fit ? "first fit" : "next fit");
    FakeFs fs;
    make_s6(fs);
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.5", msg, sizeof(msg)), 0);
    FileMap expect = host_files(fs, "/usb0/hd");
    std::set<std::string> dirs = { "Keep", "empty" };
    Dev dev(*e.r, 0);
    Driver dr(dev);
    s6_ops(dr, expect, dirs);
    // the Amiga sees what it did
    std::set<std::string> vdirs;
    FileMap view = view_map(e, 0, &vdirs, "S6 view");
    CHECK(same_maps(view, expect, "S6 view"));
    CHECK(vdirs == dirs);
    CHECK(e.disk(0)->stats().overlay_blocks > 0);
    // nothing has reached the host yet
    CHECK(!fs.has("/usb0/hd/new.txt") && fs.has("/usb0/hd/E") && fs.has("/usb0/hd/C"));
    fs.mutations = 0;
    CHECK(e.disk(0)->sync());
    CHECK(fs.mutations > 5);
    FileMap host = host_files(fs, "/usb0/hd");
    CHECK(same_maps(host, expect, "S6 host"));
    CHECK(fs.is_dir("/usb0/hd/dir2") && !fs.is_dir("/usb0/hd/empty") && fs.is_dir("/usb0/hd/Keep") && fs.is_dir("/usb0/hd/newempty"));
    CHECK(!fs.has(scratch_of(0)) || fs.files[lower(scratch_of(0))].empty());       // the free area's scratch file is gone
    CHECK_EQ(leftovers(fs), 0);
    FhStats st = e.disk(0)->stats();
    CHECK_EQ(st.created, 2);                                                       // new.txt, dir2/inner.bin
    CHECK_EQ(st.rewritten, 2);                                                     // A, B
    CHECK_EQ(st.deleted, 1);
    CHECK_EQ(st.rmdirs, 1);
    CHECK_EQ(st.mkdirs, 2);
    CHECK_EQ(st.renamed, 1);
    // the Amiga's view is the same after the fold-back, a second mount of the changed folder shows the same, and passes S2
    CHECK(same_maps(view_map(e, 0, 0, "S6 after"), expect, "S6 after"));
    CHECK_EQ(e.mount(1, "/usb0/hd", "0.5", msg, sizeof(msg)), 0);
    std::set<std::string> d2;
    CHECK(same_maps(view_map(e, 1, &d2, "S6 remount"), expect, "S6 remount"));
    CHECK(d2 == dirs);
    CHECK_EQ(verify_volume(e, 1, fs, "/usb0/hd", "S6 remount"), 0);
    // folding again changes nothing on the host (the index took over what the host holds)
    {
        uint8_t rb[512];
        Dev d0(*e.r, 0);
        CHECK(d0.read(dr.base + dr.root, rb));
        CHECK(d0.write(dr.base + dr.root, rb));                                  // a write: the disk is dirty again
        fs.mutations = 0;
        CHECK(e.disk(0)->sync());
        CHECK_EQ(fs.mutations, 0);
    }
    // more of the same on the same mount: the index follows (a file made earlier is rewritten, renamed, deleted)
    {
        Dev d0(*e.r, 0);
        Driver dr2(d0);
        uint32_t nt = dr2.resolve("new.txt");
        CHECK(nt != 0);
        std::vector<uint8_t> ap = pat(13, 2000);
        dr2.append(nt, ap);
        expect["new.txt"].insert(expect["new.txt"].end(), ap.begin(), ap.end());
        dr2.rename(dr2.root, dr2.resolve("D"), dr2.resolve("dir2"), "moved.dat");
        expect["dir2/moved.dat"] = expect["D"];
        expect.erase("D");
        dr2.remove(dr2.resolve("dir2"), dr2.resolve("dir2/inner.bin"));
        expect.erase("dir2/inner.bin");
        dr2.flush();
        CHECK(same_maps(view_map(e, 0, 0, "S6 second"), expect, "S6 second"));
        CHECK(e.disk(0)->sync());
        FileMap h2 = host_files(fs, "/usb0/hd");
        CHECK(same_maps(h2, expect, "S6 second host"));
        CHECK(same_maps(view_map(e, 0, 0, "S6 second after"), expect, "S6 second after"));
    }
}

// protection bits, comments and names the host cannot hold (FS-UAE's rule) go to the host and come back
static void s6_names_props()
{
    FakeFs fs;
    make_s6(fs);
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    Dev dev(*e.r, 0);
    Driver dr(dev);
    uint32_t root = dr.root;
    dr.create_file(root, "Wild*Card?", pat(20, 100));
    dr.create_file(root, "\xDC" "ber.txt", pat(21, 100));
    dr.create_file(root, "AUX", pat(22, 100));
    dr.create_file(root, "dot.", pat(23, 100));
    uint32_t p = dr.create_file(root, "prot.txt", pat(24, 300), 0x0F);               // all of rwed denied
    dr.set_comment(p, "a comment");
    dr.set_protect(dr.resolve("F"), 0x40 | 0x04);                                     // script bit, W denied
    dr.flush();
    CHECK(e.disk(0)->sync());
    CHECK(fs.has("/usb0/hd/Wild%2aCard%3f"));
    CHECK(fs.has("/usb0/hd/\xC3\x9C" "ber.txt"));
    CHECK(fs.has("/usb0/hd/AU%58"));
    CHECK(fs.has("/usb0/hd/dot%2e"));
    CHECK(fs.has("/usb0/hd/prot.txt.uaem"));
    std::string u = fs.text("/usb0/hd/prot.txt.uaem");
    CHECK(u.compare(0, 9, "-------- ") == 0 || u.compare(0, 8, "--------") == 0);
    CHECK(u.find("a comment") != std::string::npos);
    std::string uf = fs.text("/usb0/hd/F.uaem");
    CHECK(uf.compare(0, 8, "-s--r-ed") == 0);
    CHECK(!fs.has("/usb0/hd/A.uaem"));                                                // default protection: no file
    // remount: names, protection, comment and the date of the .uaem come back
    CHECK_EQ(e.mount(1, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    Dev d1(*e.r, 1);
    Reader rd(d1);
    CHECK(rd.check_all());
    CHECK(rd.find("Wild*Card?") && rd.find("\xDC" "ber.txt") && rd.find("AUX") && rd.find("dot."));
    const Ent *pe = rd.find("prot.txt"), *fe = rd.find("F");
    CHECK(pe && pe->prot == 0x0F && pe->comment == "a comment");
    CHECK(fe && fe->prot == (0x40 | 0x04));
    // the Amiga removes the protection again: the .uaem goes
    {
        Driver d2(dev);
        d2.set_protect(d2.resolve("F"), 0);
        d2.flush();
        CHECK(e.disk(0)->sync());
        CHECK(!fs.has("/usb0/hd/F.uaem"));
    }
    // a file renamed together with its .uaem
    {
        Driver d3(dev);
        d3.rename(d3.root, d3.resolve("prot.txt"), d3.root, "moved.txt");
        d3.flush();
        CHECK(e.disk(0)->sync());
        CHECK(fs.has("/usb0/hd/moved.txt.uaem") && !fs.has("/usb0/hd/prot.txt.uaem") && fs.has("/usb0/hd/moved.txt"));
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// S7: the medium fails after the n-th host write of the fold-back, for every n
struct Loss {
    FileMap old_tree, new_tree;
};

static int s7_examine(Env &e2, FakeFs &fs, const FileMap &old_t, const FileMap &new_t, int *window_hits, const char *tag)
{
    int bad = 0;
    FakeFs &f = fs;
    f.mut_budget = -1;
    f.gone = false;
    f.read_only = false;
    char msg[240];
    if (e2.mount(1, "/usb0/hd", "0.05", msg, sizeof(msg)) != 0) {
        printf("  %s: the folder does not mount: %s\n", tag, msg);
        return 1;
    }
    Dev dev(*e2.r, 1);
    Reader rd(dev);
    bool ok = rd.check_all();
    for (auto &er : rd.errs) printf("  %s: reader: %s\n", tag, er.c_str());
    if (!ok) bad++;
    char em[64];
    sec_eject(&e2.r->c, 1, em, sizeof(em));
    FileMap host = host_files(fs, "/usb0/hd");
    std::set<std::string> keys;
    for (auto &kv : old_t) keys.insert(kv.first);
    for (auto &kv : new_t) keys.insert(kv.first);
    for (auto &k : keys) {
        bool in_old = old_t.count(k), in_new = new_t.count(k), here = host.count(k);
        if (!here) {
            if (in_old && in_new) {
                if (old_t.at(k) != new_t.at(k)) (*window_hits)++;                          // the remove .. rename window of a rewritten file
                else { printf("  %s: %s is gone\n", tag, k.c_str()); bad++; }
            }
            continue;
        }
        const std::vector<uint8_t> &h = host[k];
        bool is_old = in_old && h == old_t.at(k), is_new = in_new && h == new_t.at(k);
        if (!is_old && !is_new) { printf("  %s: %s is neither its old nor its new bytes (%zu bytes)\n", tag, k.c_str(), h.size()); bad++; }
    }
    if (!host.count("C") && !host.count("D")) { printf("  %s: C/D: the renamed file is nowhere\n", tag); bad++; }
    return bad;
}

static void s7_with(bool first_fit);
static void s7_loss() { s7_with(false); s7_with(true); first_fit_flag() = false; }

static void s7_with(bool first_fit)
{
    first_fit_flag() = first_fit;
    FileMap old_t, new_t;
    int M = 0;
    {   // how many host operations a fold-back of the S6 changes takes
        FakeFs fs;
        make_s6(fs);
        Env e(&fs);
        char msg[240];
        CHECK_EQ(e.mount(0, "/usb0/hd", "0.05", msg, sizeof(msg)), 0);
        old_t = host_files(fs, "/usb0/hd");
        new_t = old_t;
        std::set<std::string> dirs;
        Dev dev(*e.r, 0);
        Driver dr(dev);
        s6_ops(dr, new_t, dirs);
        fs.mutations = 0;
        CHECK(e.disk(0)->sync());
        M = fs.mutations;
    }
    CHECK(M > 10);
    int total_bad = 0, window = 0, runs = 0;
    FakeFs fs;
    Env e(&fs);
    for (int mode = 0; mode < 2; mode++) {
        int win_mode = 0;
        for (int n = 0; n <= M; n++) {
            char em[64];
            sec_eject(&e.r->c, 0, em, sizeof(em));
            fs = FakeFs();
            make_s6(fs);
            char msg[240];
            if (e.mount(0, "/usb0/hd", "0.05", msg, sizeof(msg)) != 0) { CHECK(false); return; }
            Dev dev(*e.r, 0);
            Driver dr(dev);
            FileMap tmp = old_t;
            std::set<std::string> dirs;
            s6_ops(dr, tmp, dirs);
            fs.mutations = 0;
            fs.mut_budget = n;
            fs.budget_mode = mode;
            bool done = e.disk(0)->sync();
            char tag[64];
            snprintf(tag, sizeof(tag), "S7 mode %d n=%d", mode, n);
            if (n < M - 2) CHECK(!done);                                                    // it reported the failure (the last operations are clean-up)
            int bad = s7_examine(e, fs, old_t, new_t, &win_mode, tag);
            total_bad += bad;
            // the data is still in the overlay: when the medium is back the next try brings the folder to the new state
            fs.mut_budget = -1;
            fs.gone = false;
            CHECK(e.disk(0)->sync());
            FileMap h2 = host_files(fs, "/usb0/hd");
            if (!same_maps(h2, new_t, tag)) { printf("  %s: the retry did not reach the new tree\n", tag); total_bad++; }
            runs++;
        }
        window += win_mode;
        CHECK_EQ(win_mode, 2);                                                              // exactly the two rewritten files have a window, one cut each
    }
    CHECK_EQ(total_bad, 0);
    printf("S7 (%s): %d cuts over %d host operations, %d windows (remove .. rename of a rewritten file)\n", first_fit ? "first fit" : "next fit", runs, M, window);
}

// ---------------------------------------------------------------------------------------------------------------------------------------
static void s8_limits()
{
    char msg[300];
    // 16,385 entries: refused with the count and the limit; the unit that was mounted is untouched
    {
        FakeFs fs;
        make_tree(fs, "/usb0/good");
        for (int i = 0; i < 16385; i++) {
            char nm[64];
            snprintf(nm, sizeof(nm), "/usb0/big/f%05d", i);
            fs.put(nm, std::vector<uint8_t>(1, 0));
        }
        Env e(&fs);
        CHECK_EQ(e.mount(0, "/usb0/good", "0.1", msg, sizeof(msg)), 0);
        std::string name_before = sec_name(&e.r->c, 0);
        uint32_t size_before = size_reg(*e.r, 0);
        CHECK(e.mount(0, "/usb0/big", "1", msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "16385") != 0 && strstr(msg, "16384") != 0);
        CHECK_STR(sec_name(&e.r->c, 0), name_before.c_str());
        CHECK_EQ(size_reg(*e.r, 0), size_before);
        CHECK_EQ(verify_volume(e, 0, fs, "/usb0/good", "S8 untouched"), 0);
    }
    // exactly 16,384 entries mount; the memory stays inside the caps (counted by the allocator of the feature)
    {
        FakeFs fs;
        for (int i = 0; i < 16384; i++) {
            char nm[64];
            snprintf(nm, sizeof(nm), "/usb0/big/f%05d.txt", i);
            fs.put(nm, std::vector<uint8_t>(1, 0));
        }
        uint32_t base = folderhd_heap_now();
        folderhd_heap_reset_peak();
        Env e(&fs);
        CHECK_EQ(e.mount(0, "/usb0/big", "1", msg, sizeof(msg)), 0);
        uint32_t now = folderhd_heap_now() - base, peak = folderhd_heap_peak() - base;
        printf("S8: 16384 entries: index %u bytes, %u bytes held after the mount, peak %u bytes during it\n", e.disk(0)->stats().index_bytes, now, peak);
        CHECK(e.disk(0)->stats().index_bytes <= FH_INDEX_BYTES);
        CHECK(now <= FH_INDEX_BYTES + 64 * 1024);                                      // the index and the object, nothing else
        CHECK(peak <= 3u * 1024 * 1024 + 512 * 1024);                                  // the listing of one folder of this size is the most it ever takes
    }
    // an index that does not fit its budget (long names are cut and need the host name too)
    {
        FakeFs fs;
        for (int i = 0; i < 16000; i++) {
            char nm[96];
            snprintf(nm, sizeof(nm), "/usb0/long/a_host_name_longer_than_thirty_%05d", i);
            fs.put(nm, std::vector<uint8_t>(1, 0));
        }
        Env e(&fs);
        CHECK(e.mount(0, "/usb0/long", "1", msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "index") != 0 && strstr(msg, "1048576") != 0);
        CHECK_EQ(r_unit_present(e, 0), 0);
    }
    // a file of 4 GiB, a name of 300 characters: left out and named in the report
    {
        SparseFs fs;
        put_dated(fs, "/usb0/h/small.txt", pat(1, 100));
        fs.put_big("/usb0/h/huge.bin", 4 * GiB);
        put_dated(fs, "/usb0/h/" + std::string(300, 'x'), pat(2, 100));
        Env e(&fs);
        CHECK_EQ(e.mount(0, "/usb0/h", "1", msg, sizeof(msg)), 0);
        const char *rep = e.disk(0)->report();
        CHECK(strstr(rep, "huge.bin") != 0);
        CHECK(strstr(rep, "xxxxxxxxxx") != 0);
        Dev dev(*e.r, 0);
        Reader rd(dev);
        CHECK(rd.check_all());
        CHECK(rd.find("small.txt") && !rd.find("huge.bin") && rd.ents.size() == 1);
    }
    // the overlay: more metadata than the heap cap is kept, in the scratch file, and nothing is lost
    {
        FakeFs fs;
        make_s6(fs);
        Env e(&fs);
        CHECK_EQ(e.mount(0, "/usb0/hd", "1", msg, sizeof(msg)), 0);
        Dev dev(*e.r, 0);
        Driver dr(dev);
        FileMap expect = host_files(fs, "/usb0/hd");
        uint32_t d = dr.mkdir(dr.root, "many");
        for (int i = 0; i < 1400; i++) {
            char nm[32];
            snprintf(nm, sizeof(nm), "f%04d", i);
            std::vector<uint8_t> data = pat((uint32_t)i, (size_t)(i % 7) * 100);
            dr.create_file(d, nm, data);
            expect[std::string("many/") + nm] = data;
            if (i % 100 == 99) dr.flush();
        }
        dr.flush();
        const FhStats &st = e.disk(0)->stats();
        CHECK(st.overlay_blocks <= FH_OVERLAY_BLOCKS);
        CHECK(st.overlay_blocks == FH_OVERLAY_BLOCKS);                                 // it filled up ...
        CHECK_EQ(st.dropped_writes, 0);                                                // ... and the rest went to the scratch file
        CHECK(view_map(e, 0, 0, "S8 spill") == expect);
        CHECK(e.disk(0)->sync());
        CHECK(host_files(fs, "/usb0/hd") == expect);
        CHECK(view_map(e, 0, 0, "S8 spill after") == expect);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// S10: a big fold-back never holds the poll longer than its slice
struct Clock { AmigaIo *io; };
static void tick_cb(void *a, uint32_t us) { ((AmigaIo *)a)->advance(us); }

static void s10_slices()
{
    FakeFs fs;
    make_s6(fs);
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "1", msg, sizeof(msg)), 0);
    Dev dev(*e.r, 0);
    Driver dr(dev);
    std::vector<uint8_t> big = pat(50, 40u * 1024 * 1024);
    dr.create_file(dr.root, "big.bin", big);
    dr.flush();
    fs.tick = tick_cb;
    fs.tick_arg = &e.r->io;
    fs.write_ms = 5;                                                                    // 5 ms for 32 KiB written
    AmigaIo &io = e.r->io;
    // the poll as engines_poll runs it: serve what is posted, then the idle work; a read arrives every 5 ms
    uint32_t worst = 0, slices = 0;
    uint32_t t_end = io.now_ms() + 200000;
    io.delay_ms(1100);                                                                  // idle for a second: the fold-back may start
    while (io.now_ms() < t_end) {
        uint32_t t_post = io.now_ms();
        io.post(0, false, 1008 + 5);
        uint32_t before = io.now_ms();
        sec_idle(&e.r->c);                                                              // the slice that runs while the request waits
        sec_service(&e.r->c, 16);
        uint32_t wait = io.now_ms() - t_post;
        if (io.now_ms() > before) slices++;
        if (wait > worst) worst = wait;
        io.delay_ms(5);
        if (!e.disk(0)->dirty()) break;
    }
    printf("S10: 40 MB folded back in %u slices, the longest wait of a request %u ms (slice %u ms)\n", slices, worst, FH_SLICE_MS);
    CHECK(!e.disk(0)->dirty());
    CHECK(slices > 50);                                                                 // many slices, not one go
    CHECK(worst <= FH_SLICE_MS);
    CHECK(host_files(fs, "/usb0/hd")["big.bin"] == big);
}


// S11 (discussion #35, point 3): a floppy mounted or ejected while a folder is HD0 does not fold the folder back. The fold-back is the idle work (S10);
// a swap of a disk image must not wait for it (the Amiga's hard disk requests are not served meanwhile - "locks for 10 s" when 40 MB are pending).
static void s11_floppy_swap_leaves_the_folder()
{
    FakeFs fs;
    make_s6(fs);
    for (int i = 0; i < 4; i++) {
        char nm[40];
        snprintf(nm, sizeof(nm), "/usb0/disk%d.adf", i);
        fs.put(nm, pat((uint32_t)(200 + i), AM_ADF_SIZE));
    }
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "1", msg, sizeof(msg)), 0);
    Dev dev(*e.r, 0);
    Driver dr(dev);
    std::vector<uint8_t> big = pat(51, 40u * 1024 * 1024);
    dr.create_file(dr.root, "big.bin", big);
    dr.flush();
    fs.tick = tick_cb;
    fs.tick_arg = &e.r->io;
    fs.write_ms = 5;
    AmigaIo &io = e.r->io;
    uint32_t t0 = io.now_ms();
    for (int i = 0; i < 4; i++) {                                                        // four mounts in a row: the 2nd to 4th eject the one before
        char nm[40];
        snprintf(nm, sizeof(nm), "/usb0/disk%d.adf", i);
        CHECK_EQ(win_mount(&e.r->c, 0, nm, msg, sizeof(msg)), 0);
    }
    CHECK_EQ(win_eject(&e.r->c, 0, msg, sizeof(msg)), 0);
    uint32_t took = io.now_ms() - t0;
    printf("S11: four floppy mounts and an eject with 40 MB pending on the folder: %u ms, folder dirty %d, big.bin on the host %d\n", took, (int)e.disk(0)->dirty(),
           (int)fs.has("/usb0/hd/big.bin"));
    CHECK(!fs.has("/usb0/hd/big.bin"));                                                  // nothing was folded back by the swaps
    CHECK(e.disk(0)->dirty());
    CHECK(took < 2000);
    // and the idle work still brings it to the host afterwards
    io.delay_ms(1100);
    for (int i = 0; i < 100000 && e.disk(0)->dirty(); i++) {
        sec_idle(&e.r->c);
        io.delay_ms(1);
    }
    CHECK(!e.disk(0)->dirty());
    CHECK(host_files(fs, "/usb0/hd")["big.bin"] == big);
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// Things that are not a scenario of their own but that the design depends on
static void s5b_unlinked_not_straight()
{
    FakeFs fs;
    make_s6(fs);
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    Dev dev(*e.r, 0);
    Driver dr(dev);
    Reader rd(dev);
    CHECK(rd.check_all());
    const Ent *a = rd.find("A");
    CHECK(a && a->data.size() == 10);
    if (!a) return;
    FileMap before = host_files(fs, "/usb0/hd");
    // the Amiga deletes A (the chain is written), then another file gets one of its blocks: that write must not reach A's host file
    dr.remove(dr.root, dr.resolve("A"));
    dr.flush();
    std::vector<uint8_t> nb = pat(901, 512);
    CHECK(dev.write(rd.base + a->data[3], nb.data()));
    CHECK(host_files(fs, "/usb0/hd") == before);
    CHECK_EQ(e.disk(0)->stats().straight_writes, 0);
    uint8_t back[512];
    CHECK(dev.read(rd.base + a->data[3], back) && !memcmp(back, nb.data(), 512));
    CHECK(e.disk(0)->sync());
    FileMap after = host_files(fs, "/usb0/hd");
    CHECK(!after.count("A"));
    // a file whose header the Amiga rewrote (a new size) is not written straight either
    const Ent *b = rd.find("B");
    CHECK(b != 0);
    if (b) {
        Driver d2(dev);
        d2.truncate(d2.resolve("B"), 1500);
        d2.flush();
        uint32_t s0 = e.disk(0)->stats().straight_writes;
        CHECK(dev.write(rd.base + b->data[0], nb.data()));
        CHECK_EQ(e.disk(0)->stats().straight_writes, s0);
    }
}

// A writer that frees blocks and writes data into them before it writes the metadata that frees them (first fit): the host files of the files that gave the blocks up stay
// as they are until the fold-back, which knows who owns what by then.
static void s5c_first_fit()
{
    first_fit_flag() = true;
    FakeFs fs;
    make_s6(fs);
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    FileMap expect = host_files(fs, "/usb0/hd");
    FileMap before = expect;
    Dev dev(*e.r, 0);
    Driver dr(dev);
    dr.truncate(dr.resolve("B"), 10);                                // frees B's blocks 1 .. 5 (in the writer's cache only)
    dr.remove(dr.root, dr.resolve("E"));                             // frees E's blocks
    std::vector<uint8_t> n1 = pat(61, 6000), n2 = pat(62, 1500);
    dr.create_file(dr.root, "n1.bin", n1);                           // first fit: its data goes into the blocks just freed, before any metadata is written
    dr.create_file(dr.root, "n2.bin", n2);
    CHECK(dr.data_writes > 10);
    CHECK(host_files(fs, "/usb0/hd") == before);                     // not a byte of B or E, nor of any other file, has changed
    CHECK_EQ(e.disk(0)->stats().straight_writes, 0);
    dr.flush();
    expect["B"].resize(10);
    expect.erase("E");
    expect["n1.bin"] = n1;
    expect["n2.bin"] = n2;
    CHECK(same_maps(view_map(e, 0, 0, "S5c view"), expect, "S5c view"));
    CHECK(host_files(fs, "/usb0/hd") == before);
    CHECK(e.disk(0)->sync());
    CHECK(same_maps(host_files(fs, "/usb0/hd"), expect, "S5c host"));
    first_fit_flag() = false;
}

// With the Amiga's bitmap flag invalid the disk is in the middle of a change and who owns a block is not known: data blocks written in place stay in the overlay until it is valid.
static void s5d_patch_waits_for_a_valid_disk()
{
    FakeFs fs;
    make_tree(fs, "/usb0/hd");
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    Dev dev(*e.r, 0);
    Reader rd(dev);
    CHECK(rd.check_all());
    const Ent *f = rd.find("file28.dat");
    CHECK(f != 0);
    if (!f) return;
    FileMap before = host_files(fs, "/usb0/hd");
    uint8_t rb[512];
    CHECK(dev.read(rd.base + rd.root, rb));
    put(rb, 78, 0);                                                                   // "bitmap invalid"
    Driver::seal(rb);
    CHECK(dev.write(rd.base + rd.root, rb));
    std::vector<uint8_t> nb = pat(555, 512);
    CHECK(dev.write(rd.base + f->data[1], nb.data()));
    CHECK(e.disk(0)->sync());                                                         // the machine is held: it runs although the flag is invalid
    CHECK(host_files(fs, "/usb0/hd") == before);
    uint8_t back[512];
    CHECK(dev.read(rd.base + f->data[1], back) && !memcmp(back, nb.data(), 512));      // the Amiga still reads what it wrote
    put(rb, 78, 0xFFFFFFFFu);
    Driver::seal(rb);
    CHECK(dev.write(rd.base + rd.root, rb));
    CHECK(e.disk(0)->sync());
    FileMap after = host_files(fs, "/usb0/hd");
    memcpy(before["file28.dat"].data() + 512, nb.data(), 512);
    CHECK(after == before);
}

static void s6_links_and_abort()
{
    FakeFs fs;
    make_s6(fs);
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    Dev dev(*e.r, 0);
    Driver dr(dev);
    FileMap expect = host_files(fs, "/usb0/hd");
    // a hard link the Amiga makes is not saved (the host has none): the entry is left out, the log says so
    {
        uint32_t lk = dr.alloc();
        uint8_t *hb = dr.blk(lk);
        memset(hb, 0, 512);
        put(hb, 0, 2); put(hb, 1, lk);
        put(hb, 105, dr.days);
        dr.setname(hb, "LinkToA");
        put(hb, 127, 0xFFFFFFFCu);                       // ST_LINKFILE (-4)
        Driver::seal(hb); dr.touch(lk);
        dr.link(dr.root, lk);
        dr.flush();
        CHECK(e.disk(0)->sync());
        CHECK(host_files(fs, "/usb0/hd") == expect);
        CHECK(!fs.has("/usb0/hd/LinkToA"));
    }
    // a write while the changes are collected drops that run; the next run gets everything
    {
        Driver d2(dev);
        std::vector<uint8_t> n1 = pat(31, 1000), n2 = pat(32, 2000);
        d2.create_file(d2.root, "one.bin", n1);
        expect["one.bin"] = n1;
        d2.flush();
        fh::Impl *m = e.disk(0)->p;
        m->force = true;
        for (int k = 0; k < 40 && m->phase != fh::PH_MATERIAL; k++) m->fold_step();     // in the middle of the run, before anything on the host depends on it
        CHECK(m->phase != fh::PH_IDLE);
        Driver d3(dev);
        d3.create_file(d3.root, "two.bin", n2);
        expect["two.bin"] = n2;
        d3.flush();
        CHECK(e.disk(0)->sync());
        CHECK(e.disk(0)->stats().aborted >= 1);
        CHECK(host_files(fs, "/usb0/hd") == expect);
        CHECK_EQ(leftovers(fs), 0);
    }
    // the Amiga's bitmap flag says "in the middle of a change": no fold-back is started (until the flag is valid)
    {
        Driver d4(dev);
        std::vector<uint8_t> n3 = pat(33, 700);
        d4.create_file(d4.root, "three.bin", n3);                                      // the root now says "bitmap invalid" on the disk
        for (uint32_t b : d4.dirty) {                                                  // everything but the root goes to the disk, then the root with the new entry, flag still 0
            if (b == d4.root) continue;
            if (std::find(d4.pages.begin(), d4.pages.end(), b) != d4.pages.end()) Driver::seal_bm(d4.blk(b));
            dev.write(d4.base + b, d4.blk(b));
        }
        uint8_t rb[512];
        memcpy(rb, d4.blk(d4.root), 512);
        put(rb, 78, 0);
        Driver::seal(rb);
        CHECK(dev.write(d4.base + d4.root, rb));
        e.r->io.delay_ms(1500);
        fs.mutations = 0;
        for (int k = 0; k < 20; k++) sec_idle(&e.r->c);
        CHECK_EQ(fs.mutations, 0);                                                     // nothing is folded back while the Amiga is in the middle of a change
        CHECK(!fs.has("/usb0/hd/three.bin"));
        put(rb, 78, 0xFFFFFFFFu);                                                      // the flag is valid again
        Driver::seal(rb);
        CHECK(dev.write(d4.base + d4.root, rb));
        e.r->io.delay_ms(1500);
        for (int k = 0; k < 4000 && e.disk(0)->dirty(); k++) { sec_idle(&e.r->c); e.r->io.delay_ms(5); }
        CHECK(fs.has("/usb0/hd/three.bin"));
    }
}

static void eject_and_ops()
{
    FakeFs fs;
    make_s6(fs);
    uint32_t base = folderhd_heap_now();
    {
        Env e(&fs);
        // through the operation the menu binds (Task "Mount folder as HD0"): the path is the folder and the size in GB
        OpResult res;
        OpIn in[2] = { in_str("path", "/usb0/hd/0.25"), in_int("unit", 0) };
        CHECK_EQ(ops_call(&e.r->c, "sectors.mount_folder", in, 2, &res), 0);
        CHECK_STR(ops_state(&e.r->c, "amiga.hd0.image"), "hd (folder)");
        e.r->c.media_dir[0] = 0;
        CHECK_STR(ops_state(&e.r->c, "sectors.folder.offer"), "1");                    // no folder chosen yet: the plain suggestion
        snprintf(e.r->c.media_dir, sizeof(e.r->c.media_dir), "%s", "/usb0/hd");
        CHECK_STR(ops_state(&e.r->c, "sectors.folder.offer"), "1");                    // the suggestion
        fs.free_space = 300ull << 20;                                                  // a medium with 300 MB free: 0.27 GB at most ((300 - 16 reserve) MB + the tree)
        const char *off = ops_state(&e.r->c, "sectors.folder.offer");
        CHECK(off && !strncmp(off, "0.2", 3));
        OpIn big[2] = { in_str("path", "/usb0/hd/2"), in_int("unit", 1) };
        CHECK(ops_call(&e.r->c, "sectors.mount_folder", big, 2, &res) != 0);           // more than the medium backs
        CHECK(strstr(res.msg, "can back") != 0);
        OpIn bad[2] = { in_str("path", "/usb0/hd/abc"), in_int("unit", 1) };
        CHECK(ops_call(&e.r->c, "sectors.mount_folder", bad, 2, &res) != 0);
        OpIn rt[2] = { in_str("path", "/usb0"), in_int("unit", 1) };
        CHECK(ops_call(&e.r->c, "sectors.mount_folder", rt, 2, &res) != 0);
        // a write, then eject: the folder holds it, nothing of the object is left
        Dev dev(*e.r, 0);
        Driver dr(dev);
        dr.create_file(dr.root, "ej.bin", pat(40, 5000));
        dr.flush();
        CHECK(!fs.has("/usb0/hd/ej.bin"));
        OpIn ej = in_int("unit", 0);
        CHECK_EQ(ops_call(&e.r->c, "sectors.eject", &ej, 1, &res), 0);
        CHECK(fs.has("/usb0/hd/ej.bin") && fs.files[lower("/usb0/hd/ej.bin")] == pat(40, 5000));
        CHECK_EQ(r_unit_present(e, 0), 0);
        CHECK(sec_name(&e.r->c, 0) == 0);
    }
    CHECK_EQ(folderhd_heap_now(), base);                                               // everything the feature held is given back
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// Renames that need the limbo (a name that is taken, names swapped, a case change), a folder renamed with its content, empty files, stale leftovers
static void s6_renames()
{
    FakeFs fs;
    make_s6(fs);
    fs.put("/usb0/trx64/folderhd0.scratch", std::vector<uint8_t>(777, 1));             // what a cut left behind
    fs.put("/usb0/trx64/folderhd0.fb3", std::vector<uint8_t>(77, 1));
    fs.put("/usb0/trx64/folderhd0.bk3", std::vector<uint8_t>(55, 2));                  // the old content of a file whose replace was cut: kept
    Env e(&fs);
    char msg[240];
    CHECK_EQ(e.mount(0, "/usb0/hd", "0.1", msg, sizeof(msg)), 0);
    CHECK(!fs.has("/usb0/trx64/folderhd0.scratch") && !fs.has("/usb0/trx64/folderhd0.fb3"));      // stale by construction: gone at the mount
    CHECK(fs.has("/usb0/trx64/folderhd0.bk3") && strstr(e.disk(0)->report(), "interrupted replace") != 0);
    FileMap expect = host_files(fs, "/usb0/hd");
    Dev dev(*e.r, 0);
    Driver dr(dev);
    uint32_t root = dr.root;
    // a folder renamed, its file inside
    dr.rename(root, dr.resolve("Keep"), root, "Kept");
    expect["Kept/k.txt"] = expect["Keep/k.txt"];
    expect.erase("Keep/k.txt");
    // E deleted, F takes its name
    dr.remove(root, dr.resolve("E"));
    dr.rename(root, dr.resolve("F"), root, "E");
    expect["E"] = expect["F"];
    expect.erase("F");
    // A and B swap names through a third one, as a shell does
    dr.rename(root, dr.resolve("A"), root, "T");
    dr.rename(root, dr.resolve("B"), root, "A");
    dr.rename(root, dr.resolve("T"), root, "B");
    std::vector<uint8_t> a0 = expect["A"];
    expect["A"] = expect["B"];
    expect["B"] = a0;
    // a change of case only
    dr.rename(root, dr.resolve("C"), root, "c");
    expect["c"] = expect["C"];
    expect.erase("C");
    // an empty file, and a file made empty
    dr.create_file(root, "zero.bin", std::vector<uint8_t>());
    expect["zero.bin"] = std::vector<uint8_t>();
    dr.flush();
    CHECK(same_maps(view_map(e, 0, 0, "S6 renames view"), expect, "S6 renames view"));
    CHECK(e.disk(0)->sync());
    FileMap host = host_files(fs, "/usb0/hd");
    CHECK(same_maps(host, expect, "S6 renames host"));
    CHECK(fs.is_dir("/usb0/hd/Kept") && !fs.is_dir("/usb0/hd/Keep"));
    bool cased = false;
    for (auto &kv : fs.names) if (kv.second == "/usb0/hd/c") cased = true;
    CHECK(cased);
    CHECK_EQ(leftovers(fs), 0);
    {
        Driver d2(dev);
        d2.truncate(d2.resolve("A"), 0);
        d2.flush();
        expect["A"].clear();
        CHECK(e.disk(0)->sync());
        CHECK(same_maps(host_files(fs, "/usb0/hd"), expect, "S6 truncate to zero"));
        CHECK(same_maps(view_map(e, 0, 0, "S6 zero view"), expect, "S6 zero view"));
    }
    // a folder the Amiga deletes while the medium keeps a file the disk never showed in it: the file stays, the run does not repeat for ever
    {
        fs.put("/usb0/hd/Kept/.DS_Store", std::vector<uint8_t>(30, 5));
        Driver d3(dev);
        d3.remove(d3.resolve("Kept"), d3.resolve("Kept/k.txt"));
        d3.rmdir(d3.root, d3.resolve("Kept"));
        d3.flush();
        expect.erase("Kept/k.txt");
        CHECK(e.disk(0)->sync());
        CHECK(fs.has("/usb0/hd/Kept/.DS_Store") && !fs.has("/usb0/hd/Kept/k.txt"));
        CHECK(!e.disk(0)->dirty());
        CHECK_EQ(e.disk(0)->stats().fold_errors, 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// D6: the free area is as large as the medium can back, and no larger
struct NoFreeFs : FakeFs {
    bool free_bytes(const char *, uint64_t *) { return false; }
};

static void d6_backing()
{
    char msg[240];
    FakeIo io;
    {
        FakeFs fs;
        make_s6(fs);
        fs.free_space = 80ull << 20;                                                 // 80 MB free, 16 MB are never promised
        FolderDisk::Cfg cfg = { &fs, &io, 0, "/usb0/hd", 1ull << 30, 65535 };
        FolderDisk *d = FolderDisk::mount(cfg, msg, sizeof(msg));
        CHECK(d != 0);
        if (d) {
            uint64_t sz = d->size();
            CHECK(sz <= (64ull << 20) + (3ull << 20));                              // the free area fits what the medium has left (cylinder 0 and rounding apart)
            CHECK(sz >= (60ull << 20));
            delete d;
        }
        fs.free_space = 0;                                                           // a full medium: no free area, the volume is the tree
        d = FolderDisk::mount(cfg, msg, sizeof(msg));
        CHECK(d != 0);
        if (d) {
            CHECK(d->size() <= (4ull << 20));
            delete d;
        }
        fs.free_space = 1ull << 40;
        cfg.want_bytes = 50ull << 20;                                                // less than asked for the tree + more room: the person's size wins
        d = FolderDisk::mount(cfg, msg, sizeof(msg));
        CHECK(d != 0 && d->size() <= (51ull << 20) && d->size() >= (49ull << 20));
        delete d;
        cfg.want_bytes = 1 << 20;                                                    // a size the tree does not fit: refused with the size it needs
        cfg.want_bytes = 100;
        d = FolderDisk::mount(cfg, msg, sizeof(msg));
        CHECK(d == 0);
        CHECK(strstr(msg, "at least") != 0);
    }
    {
        NoFreeFs fs;
        make_s6(fs);
        FolderDisk::Cfg cfg = { &fs, &io, 0, "/usb0/hd", 1ull << 30, 65535 };
        FolderDisk *d = FolderDisk::mount(cfg, msg, sizeof(msg));
        CHECK(d != 0 && d->size() <= (4ull << 20));                                  // free space unknown: nothing is promised
        delete d;
    }
}

int main()
{
    setvbuf(stdout, 0, _IOLBF, 0);                               // a control that crashes the program has to leave its red checks behind
    const char *only = getenv("FH_ONLY");                       // FH_ONLY=s3 runs one scenario (debugging)
    struct { const char *name; void (*fn)(); } all[] = {
        { "s1", s1_mount }, { "s2", s2_valid_volume }, { "s3", s3_content }, { "s4", s4_names }, { "s5", s5_straight_write },
        { "s5b", s5b_unlinked_not_straight }, { "s5c", s5c_first_fit }, { "s5d", s5d_patch_waits_for_a_valid_disk }, { "s6", s6_fold }, { "s6n", s6_names_props }, { "s6l", s6_links_and_abort }, { "s6r", s6_renames }, { "eject", eject_and_ops }, { "d6", d6_backing },
        { "s7", s7_loss }, { "s8", s8_limits }, { "s10", s10_slices }, { "s11", s11_floppy_swap_leaves_the_folder },
    };
    for (auto &t : all) {
        if (only && strcmp(only, t.name)) continue;
        clock_t c0 = clock();
        t.fn();
        if (getenv("FH_TIME")) printf("time %s %.1f s\n", t.name, (double)(clock() - c0) / CLOCKS_PER_SEC);
    }
    DONE("test_folderhd");
}
