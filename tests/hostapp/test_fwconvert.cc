// Host test of the .cfw/.ue2 -> .cor conversion (host-app/software/hostapp/fwconvert.cc, T137 S1, D9) over a RAM disk: the bytes equal what tools/cor_pack.py
// pack_large writes for the same parts (mkconvtest.py makes both), the .cor reads back through the host's own reader, and every way to go wrong leaves no
// ".part" and no changed target behind.
//   test_fwconvert <dir of mkconvtest.py>
#include "fwconvert.h"
#include "sha256.h"
#include "known_fw.h"
#include <stdio.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
typedef std::vector<uint8_t> Bytes;

struct RamFs : FoFs {
    struct H { std::string path; size_t pos; bool w; };
    std::map<std::string, Bytes> files;
    std::set<std::string> dirs;
    std::map<std::string, uint64_t> cap;                 // volume (first component, lower case) -> capacity
    uint64_t bytes_written;
    int64_t fail_write_after;                            // -1 = never; else the write that would pass this many bytes fails
    bool corrupt_part;                                   // flips a bit in everything written to a ".part" file
    std::string fail_remove;                             // a path remove() refuses
    bool fail_mkdir;
    int mkdirs;
    int renames;
    std::vector<H *> open;

    RamFs() : bytes_written(0), fail_write_after(-1), corrupt_part(false), fail_mkdir(false), mkdirs(0), renames(0) { }
    static std::string vol(const std::string &p)
    {
        size_t e = p.find('/', 1);
        std::string v = p.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        for (size_t i = 0; i < v.size(); i++) v[i] = tolower(v[i]);
        return v;
    }
    uint64_t used(const std::string &v)
    {
        uint64_t u = 0;
        for (std::map<std::string, Bytes>::iterator i = files.begin(); i != files.end(); ++i)
            if (vol(i->first) == v) u += i->second.size();
        return u;
    }
    bool stat(const char *path, FoStat *st)
    {
        std::string p = path;
        if (files.count(p)) { st->dir = false; st->size = files[p].size(); return true; }
        if (dirs.count(p)) { st->dir = true; st->size = 0; return true; }
        return false;
    }
    int dir_entries(const char *path)
    {
        std::string p = std::string(path) + "/";
        int n = 0;
        for (std::map<std::string, Bytes>::iterator i = files.begin(); i != files.end(); ++i) if (!i->first.compare(0, p.size(), p)) n++;
        for (std::set<std::string>::iterator i = dirs.begin(); i != dirs.end(); ++i) if (!i->compare(0, p.size(), p)) n++;
        return n;
    }
    bool free_bytes(const char *path, uint64_t *b)
    {
        std::string v = vol(path);
        if (!cap.count(v)) return false;
        *b = cap[v] - used(v);
        return true;
    }
    bool same_volume(const char *a, const char *b) { return vol(a) == vol(b); }
    void *open_read(const char *path)
    {
        if (!files.count(path)) return 0;
        H *h = new H; h->path = path; h->pos = 0; h->w = false; return h;
    }
    static std::string parent(const std::string &p) { return p.substr(0, p.rfind('/')); }
    void *open_write(const char *path)
    {
        if (!dirs.count(parent(path))) return 0;             // a FAT volume does not make the folder
        files[path] = Bytes();
        H *h = new H; h->path = path; h->pos = 0; h->w = true; return h;
    }
    int read(void *hh, void *buf, uint32_t n)
    {
        H *h = (H *)hh;
        Bytes &b = files[h->path];
        size_t k = b.size() - h->pos < n ? b.size() - h->pos : n;
        memcpy(buf, &b[0] + h->pos, k);
        h->pos += k;
        return (int)k;
    }
    bool write(void *hh, const void *buf, uint32_t n)
    {
        H *h = (H *)hh;
        std::string v = vol(h->path);
        if (fail_write_after >= 0 && (int64_t)(bytes_written + n) > fail_write_after) return false;
        if (used(v) + n > cap[v]) return false;
        Bytes &b = files[h->path];
        size_t at = b.size();
        b.insert(b.end(), (const uint8_t *)buf, (const uint8_t *)buf + n);
        if (corrupt_part && h->path.size() > 5 && !h->path.compare(h->path.size() - 5, 5, ".part")) b[at] ^= 0x10;
        bytes_written += n;
        return true;
    }
    bool close(void *hh) { delete (H *)hh; return true; }
    bool remove(const char *path)
    {
        if (fail_remove == path) return false;
        std::string p = path;
        if (files.erase(p)) return true;
        return dirs.erase(p) > 0;
    }
    bool rename(const char *from, const char *to)
    {
        if (!files.count(from) || files.count(to) || vol(from) != vol(to)) return false;
        files[to] = files[from];
        files.erase(from);
        renames++;
        return true;
    }
    bool make_dir(const char *path)
    {
        if (fail_mkdir || !dirs.count(parent(path)) || files.count(path)) return false;
        dirs.insert(path);
        mkdirs++;
        return true;
    }
    int parts()
    {
        int n = 0;
        for (std::map<std::string, Bytes>::iterator i = files.begin(); i != files.end(); ++i)
            if (i->first.size() > 5 && !i->first.compare(i->first.size() - 5, 5, ".part")) n++;
        return n;
    }
};


static std::string dir;
static Bytes load(const std::string &name)
{
    Bytes b;
    FILE *f = fopen((dir + "/" + name).c_str(), "rb");
    if (!f) { printf("FAIL cannot read %s\n", name.c_str()); fails++; return b; }
    fseek(f, 0, SEEK_END);
    b.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(b.data(), 1, b.size(), f) != b.size()) fails++;
    fclose(f);
    return b;
}
struct MemSrc : CorSource {
    Bytes b;
    bool fail_after_set; uint32_t fail_after;
    MemSrc(const Bytes &x) : b(x), fail_after_set(false), fail_after(0) { }
    uint32_t size() { return (uint32_t)b.size(); }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if (fail_after_set && off >= fail_after) return false;
        if ((uint64_t)off + len > b.size()) return false;
        memcpy(buf, &b[off], len);
        return true;
    }
};

static RamFs *fresh()
{
    RamFs *f = new RamFs;
    f->cap["sd"] = 2000000;
    f->cap["tiny"] = 100000;
    f->dirs.insert("/SD"); f->dirs.insert("/SD/cores"); f->dirs.insert("/SD/fw"); f->dirs.insert("/Tiny"); f->dirs.insert("/Tiny/cores");
    f->dirs.insert("/Flash");
    return f;
}

static uint8_t buf[4096];

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    dir = argv[1];
    Bytes cfw = load("vendor.cfw"), want = load("expected_foreign.cor"), icon = load("icon.bin");
    MemSrc src(cfw);
    CfwInfo ci;
    CHECK(cfw_probe(src, CFW_IDCODE_XC7A50T, ci) == CFW_OK);
    char dst[96];
    // the target: <medium>/cores/<name>.cor of the medium the firmware lies on, wherever below it
    CHECK(fwcv_target("/SD/fw/gideon.cfw", dst, sizeof(dst)) == FWCT_OK && !strcmp(dst, "/SD/cores/gideon.cor"));
    CHECK(fwcv_target("/USB1/a/b/C64U_V1.1.0.UE2", dst, sizeof(dst)) == FWCT_OK && !strcmp(dst, "/USB1/cores/C64U_V1.1.0.cor"));
    CHECK(fwcv_target("/SD/gideon.cfw", dst, sizeof(dst)) == FWCT_OK && !strcmp(dst, "/SD/cores/gideon.cor"));
    CHECK(fwcv_target("/SD/cores/gideon.cfw", dst, sizeof(dst)) == FWCT_OK && !strcmp(dst, "/SD/cores/gideon.cor"));
    CHECK(fwcv_target("/SD/x.cor", dst, sizeof(dst)) == FWCT_NAME && fwcv_target("/SD/.cfw", dst, sizeof(dst)) == FWCT_NAME);
    CHECK(fwcv_target("/SD/fw/.ue2", dst, sizeof(dst)) == FWCT_NAME && fwcv_target("/x.cfw", dst, sizeof(dst)) == FWCT_NAME);   // no medium in front of it
    CHECK(fwcv_target("gideon.cfw", dst, sizeof(dst)) == FWCT_NAME);
    CHECK(fwcv_target("/Flash/fw/gideon.cfw", dst, sizeof(dst)) == FWCT_FLASH && fwcv_target("/flash/gideon.ue2", dst, sizeof(dst)) == FWCT_FLASH);
    CHECK(fwcv_target("/Flashy/gideon.cfw", dst, sizeof(dst)) == FWCT_OK && !strcmp(dst, "/Flashy/cores/gideon.cor"));
    CHECK(fwcv_target("/SD/fw/gideon.cfw", dst, 20) == FWCT_LONG && fwcv_target("/SD/fw/gideon.cfw", dst, 21) == FWCT_OK);

    // the names (Q7): the table's, else the .bit header's design and date
    FwConvPlan pl;
    { FwKnown k = { "00", "Commodore", "C64 Ultimate", "1.1.0" };
      fwcv_names(ci, &k, &pl); CHECK(!strcmp(pl.name, "Commodore C64 Ultimate") && !strcmp(pl.version, "1.1.0"));
      fwcv_names(ci, 0, &pl); CHECK(!strcmp(pl.name, "top 2026/10/25 12:00:00") && !strcmp(pl.version, "bit 2026/10/25")); }

    // the conversion: the same bytes as the python packer
    {
        RamFs *fs = fresh();
        FwConvResult r = fwcv_plan(*fs, ci, 0, true, "/SD/cores/vendor.cor", &pl);
        CHECK(r == FWCV_OK && pl.size == want.size() && !pl.exists && pl.free > pl.size);
        r = fw_convert(*fs, src, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0);
        CHECK(r == FWCV_OK);
        CHECK(fs->files.count("/SD/cores/vendor.cor") && fs->files["/SD/cores/vendor.cor"] == want);
        CHECK(fs->parts() == 0 && fs->renames == 1);
        // the host's reader takes it and the hashes hold
        MemSrc out(fs->files["/SD/cores/vendor.cor"]);
        CorInfo c;
        CHECK(cor_probe(out, c) == COR_ERR_LARGE && c.origin == COR_ORIGIN_FOREIGN && !strcmp(c.id, "fw-665f4a02") && c.size[COR_PART_FULL] == ci.bit_len);
        const char *why = "";
        CHECK(cor_large_verify(out, c, &why));
        // and as a firmware: the same bitstream and application as the source
        CfwInfo c2;
        CHECK(cfw_probe(out, CFW_IDCODE_XC7A50T, c2) == CFW_OK && c2.from_cor && !strcmp(c2.bit_sha, c.sha[COR_PART_FULL]) && c2.map == ci.map);
        FILE *f = fopen((dir + "/converted.cor").c_str(), "wb");        // for the python reader (run_fw.sh)
        fwrite(fs->files["/SD/cores/vendor.cor"].data(), 1, fs->files["/SD/cores/vendor.cor"].size(), f);
        fclose(f);
        delete fs;
    }
    // without an icon the manifest has no icon keys, and the size the plan named is the size written
    {
        RamFs *fs = fresh();
        CHECK(fwcv_plan(*fs, ci, 0, false, "/SD/cores/vendor.cor", &pl) == FWCV_OK);
        CHECK(fw_convert(*fs, src, ci, pl, 0, false, buf, sizeof(buf), 0, 0) == FWCV_OK && fs->files["/SD/cores/vendor.cor"].size() == pl.size);
        MemSrc out(fs->files["/SD/cores/vendor.cor"]);
        CorInfo c;
        CHECK(cor_probe(out, c) == COR_ERR_LARGE && !c.size[COR_PART_ICON]);
        delete fs;
    }
    // an existing target is a question, and replaced only after the new file is complete and verified
    {
        RamFs *fs = fresh();
        fs->files["/SD/cores/vendor.cor"] = Bytes(1000, 0x5A);
        CHECK(fwcv_plan(*fs, ci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_EXISTS && pl.exists);
        CHECK(fw_convert(*fs, src, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_EXISTS);
        CHECK(fs->files["/SD/cores/vendor.cor"] == Bytes(1000, 0x5A) && fs->bytes_written == 0);                 // nothing written without the answer
        CHECK(fw_convert(*fs, src, ci, pl, icon.data(), true, buf, sizeof(buf), 0, 0) == FWCV_OK && fs->files["/SD/cores/vendor.cor"] == want && fs->parts() == 0);
        // a write that fails half way: the old file is still there, no .part
        RamFs *g = fresh();
        g->files["/SD/cores/vendor.cor"] = Bytes(1000, 0x5A);
        g->fail_write_after = 50000;
        CHECK(fw_convert(*g, src, ci, pl, icon.data(), true, buf, sizeof(buf), 0, 0) == FWCV_ERR_WRITE);
        CHECK(g->files["/SD/cores/vendor.cor"] == Bytes(1000, 0x5A) && g->parts() == 0);
        // what is written does not read back as written: refused, no name
        RamFs *h = fresh();
        h->corrupt_part = true;
        CHECK(fwcv_plan(*h, ci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_OK);
        CHECK(fw_convert(*h, src, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_ERR_VERIFY);
        CHECK(!h->files.count("/SD/cores/vendor.cor") && h->parts() == 0);
        delete fs; delete g; delete h;
    }
    // no room: refused before one byte is written, free and needed named
    {
        RamFs *fs = fresh();
        FwConvResult r = fwcv_plan(*fs, ci, 0, true, "/Tiny/vendor.cor", &pl);
        CHECK(r == FWCV_ERR_NOROOM && pl.free < pl.size + 4096 && pl.size == want.size() && fs->bytes_written == 0);
        delete fs;
    }
    // the firmware cannot be read half way: no .part
    {
        RamFs *fs = fresh();
        MemSrc bad(cfw);
        bad.fail_after_set = true; bad.fail_after = ci.app_data + 100000 > cfw.size() ? ci.bit_off : ci.bit_off + 5000;
        CHECK(fwcv_plan(*fs, ci, 0, true, "/SD/cores/v.cor", &pl) == FWCV_OK);
        CHECK(fw_convert(*fs, bad, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_ERR_READ && fs->parts() == 0 && !fs->files.count("/SD/cores/v.cor"));
        delete fs;
    }
    // a cancel
    {
        RamFs *fs = fresh();
        CHECK(fwcv_plan(*fs, ci, 0, true, "/SD/cores/v.cor", &pl) == FWCV_OK);
        CHECK(fw_convert(*fs, src, ci, pl, icon.data(), false, buf, sizeof(buf), [](void *, int ph, uint64_t, uint64_t) { return ph == 1 ? 1 : 0; }, 0) == FWCV_CANCELLED);
        CHECK(fs->parts() == 0 && !fs->files.count("/SD/cores/v.cor"));
        delete fs;
    }
    // a table entry's name goes into the manifest as the table has it
    {
        RamFs *fs = fresh();
        FwKnown k = { "00", "Commodore", "C64 Ultimate", "1.1.0" };
        CHECK(fwcv_plan(*fs, ci, &k, true, "/SD/cores/c.cor", &pl) == FWCV_OK);
        CHECK(fw_convert(*fs, src, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_OK);
        MemSrc out(fs->files["/SD/cores/c.cor"]);
        CorInfo c;
        CHECK(cor_probe(out, c) == COR_ERR_LARGE && !strcmp(c.name, "Commodore C64 Ultimate") && !strcmp(c.version, "1.1.0"));
        delete fs;
    }
    // a .ue2 (no pre-header, the updater record loads at 0x03000000 as in the real files) makes the same .cor
    {
        Bytes ue2 = load("vendor.ue2");
        MemSrc usrc(ue2);
        CfwInfo uci;
        RamFs *fs = fresh();
        CHECK(cfw_probe(usrc, CFW_IDCODE_XC7A50T, uci) == CFW_OK && uci.bit_len == ci.bit_len && uci.app_len == ci.app_len);
        CHECK(fwcv_plan(*fs, uci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_OK);
        CHECK(fw_convert(*fs, usrc, uci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_OK && fs->files["/SD/cores/vendor.cor"] == want);
        delete fs;
    }
    // "<medium>/cores" is not there yet: it is made, once, and the .cor lands in it
    {
        RamFs *fs = fresh();
        fs->dirs.erase("/SD/cores");
        CHECK(fwcv_plan(*fs, ci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_OK && pl.dir_missing);
        CHECK(fw_convert(*fs, src, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_OK);
        CHECK(fs->mkdirs == 1 && fs->dirs.count("/SD/cores") && fs->files["/SD/cores/vendor.cor"] == want && fs->parts() == 0);
        CHECK(fwcv_plan(*fs, ci, 0, true, "/SD/cores/other.cor", &pl) == FWCV_OK && !pl.dir_missing);       // there now
        // the folder cannot be made: refused, nothing written
        RamFs *g = fresh();
        g->dirs.erase("/SD/cores");
        g->fail_mkdir = true;
        CHECK(fwcv_plan(*g, ci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_OK && pl.dir_missing);
        CHECK(fw_convert(*g, src, ci, pl, icon.data(), false, buf, sizeof(buf), 0, 0) == FWCV_ERR_FS && g->bytes_written == 0 && !g->files.count("/SD/cores/vendor.cor"));
        // a file where the folder should be
        RamFs *h = fresh();
        h->dirs.erase("/SD/cores");
        h->files["/SD/cores"] = Bytes(10, 1);
        CHECK(fwcv_plan(*h, ci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_ERR_FS && h->bytes_written == 0);
        // a folder where the .cor should be
        RamFs *k = fresh();
        k->dirs.insert("/SD/cores/vendor.cor");
        CHECK(fwcv_plan(*k, ci, 0, true, "/SD/cores/vendor.cor", &pl) == FWCV_ERR_FS);
        delete fs; delete g; delete h; delete k;
    }
    // the table (D3): the whole-file hash names a vendor file; any other file is nobody
    {
        MemSrc whole(cfw);
        char hex[65];
        CHECK(fw_file_sha(whole, hex, 0, 0, 0) && strlen(hex) == 64);
        sha256_t sc;
        uint8_t dg[32];
        char hex2[65];
        sha256_init(&sc);
        sha256_update(&sc, cfw.data(), (uint32_t)cfw.size());
        sha256_final(&sc, dg);
        sha256_hex(dg, hex2);
        CHECK(!strcmp(hex, hex2));                                                       // streamed in blocks = the hash of the whole
        CHECK(fw_known_lookup(hex) == 0);                                                // a synthetic image is on no list
        CHECK(fw_known_count() >= 4);
        const FwKnown *kn = fw_known_lookup("487a7d2104d6d32f61f33ca36a16439d947cd1b0eab75d5b1e78248de51bb58d");
        CHECK(kn && !strcmp(kn->vendor, "Commodore") && !strcmp(kn->version, "1.1.0"));
        CHECK(fw_known_lookup("487a7d2104d6d32f61f33ca36a16439d947cd1b0eab75d5b1e78248de51bb58e") == 0);
        for (int i = 0; known_fw[i].sha256; i++) {                                       // every row is a lower-case SHA-256
            CHECK(strlen(known_fw[i].sha256) == 64 && strspn(known_fw[i].sha256, "0123456789abcdef") == 64);
        }
        CHECK(!fw_file_sha(whole, hex, 0, 0, [](void *) { return true; }));             // an abort ends the read
        MemSrc bad(cfw);
        bad.fail_after_set = true; bad.fail_after = 20000;
        CHECK(!fw_file_sha(bad, hex, 0, 0, 0));                                          // an unreadable file is no hash
    }
    // the origin of a manifest (D1/D2): none = ours, foreign, a complete backup; a made-up one, a backup without its place, flash keys on a foreign .cor, another layout are refused
    {
        CorInfo c;
        MemSrc a(want), b(load("backup.cor"));
        CHECK(cor_probe(a, c) == COR_ERR_LARGE && c.origin == COR_ORIGIN_FOREIGN);
        CHECK(cor_probe(b, c) == COR_ERR_LARGE && c.origin == COR_ORIGIN_BACKUP && c.flash_bit == 0 && c.flash_app == 0x220000 && !strcmp(c.flash_id, "Winbond W25Q128"));
        MemSrc d(load("origin_unknown.cor")), e(load("backup_noflash.cor")), f(load("foreign_with_flash.cor")), g(load("backup_other_layout.cor"));
        CHECK(cor_probe(d, c) == COR_ERR_FORMAT);
        CHECK(cor_probe(e, c) == COR_ERR_FORMAT);
        CHECK(cor_probe(f, c) == COR_ERR_FORMAT);
        CHECK(cor_probe(g, c) == COR_ERR_FORMAT);
    }
    printf("test_fwconvert: %d failed\n", fails);
    return fails ? 1 : 0;
}
