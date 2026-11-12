// Host test of delete, copy and move (host-app/software/hostapp/fileops.cc, T139 D1/D2) over a RAM disk with two volumes.
#include "fileops.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
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
    int renames;
    std::vector<H *> open;

    RamFs() : bytes_written(0), fail_write_after(-1), corrupt_part(false), renames(0) { }
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
    void *open_write(const char *path)
    {
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
    int parts()
    {
        int n = 0;
        for (std::map<std::string, Bytes>::iterator i = files.begin(); i != files.end(); ++i)
            if (i->first.size() > 5 && !i->first.compare(i->first.size() - 5, 5, ".part")) n++;
        return n;
    }
};

static Bytes pattern(size_t n, int seed)
{
    Bytes b(n);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)((i * 7 + seed + (i >> 8)) & 0xFF);
    return b;
}

static std::string sha(const Bytes &b)
{
    sha256_t c; uint8_t d[32]; char hex[65];
    sha256_init(&c); sha256_update(&c, b.empty() ? (const uint8_t *)"" : &b[0], (uint32_t)b.size()); sha256_final(&c, d); sha256_hex(d, hex);
    return hex;
}

static RamFs *fresh()
{
    RamFs *f = new RamFs;
    f->cap["sd"] = 1000000;
    f->cap["usb0"] = 1000000;
    f->dirs.insert("/SD"); f->dirs.insert("/SD/cores"); f->dirs.insert("/SD/empty"); f->dirs.insert("/Usb0"); f->dirs.insert("/Usb0/in");
    return f;
}

static uint8_t buf[4096];
static uint64_t last_done, prog_calls;
static int prog(void *, int phase, uint64_t done, uint64_t total)
{
    prog_calls++;
    if (phase == FO_PHASE_COPY) { CHECK(done >= last_done && done <= total); last_done = done; }
    return 0;
}
static int cancel_at;
static int prog_cancel(void *, int phase, uint64_t done, uint64_t)
{
    (void)phase; (void)done;
    return --cancel_at <= 0;
}

int main()
{
    // ---- delete (S1, S2)
    {
        RamFs *f = fresh();
        f->files["/SD/cores/old.cor"] = pattern(1000, 1);
        f->dirs.insert("/SD/full"); f->files["/SD/full/x"] = pattern(10, 2);
        CHECK(fo_delete(*f, "/SD/cores/old.cor") == FO_OK && !f->files.count("/SD/cores/old.cor"));
        CHECK(fo_delete(*f, "/SD/cores/old.cor") == FO_ERR_NOSRC);
        CHECK(fo_delete(*f, "/SD/full") == FO_ERR_DIR_NOTEMPTY && f->dirs.count("/SD/full") && f->files.count("/SD/full/x"));
        CHECK(fo_delete(*f, "/SD/empty") == FO_OK && !f->dirs.count("/SD/empty"));
        f->files["/SD/cores/locked.cor"] = pattern(10, 3); f->fail_remove = "/SD/cores/locked.cor";
        CHECK(fo_delete(*f, "/SD/cores/locked.cor") == FO_ERR_FS && f->files.count("/SD/cores/locked.cor"));
        delete f;
        BootCfg c;
        bootcfg_init(&c);
        bootcfg_add(&c, "Amiga", "/SD/cores/Amiga.cor", "aa");
        bootcfg_add(&c, "C64", "/SD/cores/c64.cor", "bb");
        bootcfg_add(&c, "Amiga 2", "/SD/cores/amiga.cor", "cc");
        int idx[4];
        CHECK(fo_boot_refs(c, "/SD/cores/amiga.cor", idx, 4) == 2 && idx[0] == 0 && idx[1] == 2);   // the card does not tell cases apart
        CHECK(fo_boot_refs(c, "/SD/cores/old.cor", idx, 4) == 0);
        CHECK(fo_boot_refs(c, "/SD/cores/amiga.cor", idx, 1) == 2 && idx[0] == 0);                    // the total, filled up to max
    }
    // ---- copy (S3)
    {
        RamFs *f = fresh();
        Bytes src = pattern(100000, 5);
        f->files["/SD/cores/a.bin"] = src;
        FoPlan p;
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/Usb0/in", false, &p) == FO_OK);
        CHECK(!strcmp(p.dst, "/Usb0/in/a.bin") && p.size == 100000 && !p.exists && !p.rename_only);
        last_done = 0; prog_calls = 0;
        CHECK(fo_copy(*f, p, false, buf, sizeof(buf), prog, 0) == FO_OK);
        CHECK(f->files.count("/Usb0/in/a.bin") && sha(f->files["/Usb0/in/a.bin"]) == sha(src));          // byte-identical
        CHECK(f->files.count("/SD/cores/a.bin") && sha(f->files["/SD/cores/a.bin"]) == sha(src));        // the source stays
        CHECK(f->parts() == 0 && last_done == 100000 && prog_calls > 25);
        CHECK(f->bytes_written == 100000);
        // a source that is an empty file
        f->files["/SD/cores/zero"] = Bytes();
        CHECK(fo_plan(*f, "/SD/cores/zero", "/Usb0/in", false, &p) == FO_OK && fo_copy(*f, p, false, buf, sizeof(buf), 0, 0) == FO_OK);
        CHECK(f->files.count("/Usb0/in/zero") && f->files["/Usb0/in/zero"].empty());
        delete f;
    }
    // ---- no room: refused before one byte is written, with the numbers
    {
        RamFs *f = fresh();
        f->files["/SD/cores/a.bin"] = pattern(100000, 5);
        f->cap["usb0"] = 70000;
        f->files["/Usb0/other"] = pattern(20000, 9);
        FoPlan p;
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/Usb0/in", false, &p) == FO_ERR_NOROOM);
        CHECK(p.free == 50000 && p.size == 100000);
        CHECK(f->bytes_written == 0 && !f->files.count("/Usb0/in/a.bin") && f->parts() == 0);
        f->files["/SD/cores/fits.bin"] = pattern(50000, 6);                                              // exactly the free room fits
        CHECK(fo_plan(*f, "/SD/cores/fits.bin", "/Usb0/in", false, &p) == FO_OK && fo_copy(*f, p, false, buf, sizeof(buf), 0, 0) == FO_OK);
        // the card fills up under the copy: the half copy is gone
        RamFs *g = fresh();
        g->files["/SD/cores/a.bin"] = pattern(100000, 5);
        CHECK(fo_plan(*g, "/SD/cores/a.bin", "/Usb0/in", false, &p) == FO_OK);
        g->fail_write_after = 30000;
        CHECK(fo_copy(*g, p, false, buf, sizeof(buf), 0, 0) == FO_ERR_WRITE && g->parts() == 0 && !g->files.count("/Usb0/in/a.bin"));
        delete f; delete g;
    }
    // ---- a file of that name exists: asked, replaced only on a yes, never half
    {
        RamFs *f = fresh();
        Bytes a = pattern(30000, 1), b = pattern(40000, 2);
        f->files["/SD/cores/a.bin"] = b;
        f->files["/Usb0/in/a.bin"] = a;
        FoPlan p;
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/Usb0/in", false, &p) == FO_EXISTS && p.exists);
        CHECK(fo_copy(*f, p, false, buf, sizeof(buf), 0, 0) == FO_EXISTS);
        CHECK(sha(f->files["/Usb0/in/a.bin"]) == sha(a) && f->bytes_written == 0);                       // no answer, no change
        f->fail_write_after = 10000;                                                                     // a replace that dies halfway
        CHECK(fo_copy(*f, p, true, buf, sizeof(buf), 0, 0) == FO_ERR_WRITE);
        CHECK(sha(f->files["/Usb0/in/a.bin"]) == sha(a) && f->parts() == 0);                             // the old file is whole
        f->fail_write_after = -1;
        CHECK(fo_copy(*f, p, true, buf, sizeof(buf), 0, 0) == FO_OK);
        CHECK(sha(f->files["/Usb0/in/a.bin"]) == sha(b) && f->parts() == 0);
        // a folder of that name is never replaced
        f->dirs.insert("/Usb0/in/dir.bin"); f->files["/SD/cores/dir.bin"] = a;
        CHECK(fo_plan(*f, "/SD/cores/dir.bin", "/Usb0/in", false, &p) == FO_ERR_NOTFILE);
        // not enough room for the new data beside the old one
        f->cap["usb0"] = f->used("usb0") + 100;
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/Usb0/in", false, &p) == FO_ERR_NOROOM && sha(f->files["/Usb0/in/a.bin"]) == sha(b));
        delete f;
    }
    // ---- what was written is read back
    {
        RamFs *f = fresh();
        f->files["/SD/cores/a.bin"] = pattern(20000, 4);
        FoPlan p;
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/Usb0/in", false, &p) == FO_OK);
        f->corrupt_part = true;
        CHECK(fo_copy(*f, p, false, buf, sizeof(buf), 0, 0) == FO_ERR_VERIFY);
        CHECK(!f->files.count("/Usb0/in/a.bin") && f->parts() == 0 && f->files.count("/SD/cores/a.bin"));
        delete f;
    }
    // ---- move
    {
        RamFs *f = fresh();
        Bytes src = pattern(60000, 8);
        f->files["/SD/cores/m.bin"] = src;
        FoPlan p;
        CHECK(fo_plan(*f, "/SD/cores/m.bin", "/SD/empty", true, &p) == FO_OK && p.rename_only);       // same volume: a rename
        f->cap["sd"] = f->used("sd");                                                                   // ... which needs no room at all
        CHECK(fo_move(*f, p, false, buf, sizeof(buf), 0, 0) == FO_OK);
        CHECK(f->renames == 1 && f->bytes_written == 0 && !f->files.count("/SD/cores/m.bin") && sha(f->files["/SD/empty/m.bin"]) == sha(src));
        // another volume: copy, verify, delete
        f->files["/SD/cores/n.bin"] = src;
        CHECK(fo_plan(*f, "/SD/cores/n.bin", "/Usb0/in", true, &p) == FO_OK && !p.rename_only);
        CHECK(fo_move(*f, p, false, buf, sizeof(buf), 0, 0) == FO_OK);
        CHECK(!f->files.count("/SD/cores/n.bin") && sha(f->files["/Usb0/in/n.bin"]) == sha(src) && f->parts() == 0);
        // the copy does not verify: the original stays
        f->files["/SD/cores/c.bin"] = src;
        CHECK(fo_plan(*f, "/SD/cores/c.bin", "/Usb0/in", true, &p) == FO_OK);
        f->corrupt_part = true;
        CHECK(fo_move(*f, p, false, buf, sizeof(buf), 0, 0) == FO_ERR_VERIFY);
        CHECK(f->files.count("/SD/cores/c.bin") && !f->files.count("/Usb0/in/c.bin") && f->parts() == 0);
        f->corrupt_part = false;
        // the original cannot be removed: both exist, and it says so
        f->fail_remove = "/SD/cores/c.bin";
        CHECK(fo_move(*f, p, false, buf, sizeof(buf), 0, 0) == FO_ERR_DELETE);
        CHECK(f->files.count("/SD/cores/c.bin") && sha(f->files["/Usb0/in/c.bin"]) == sha(src));
        f->fail_remove = "";
        // no room on the other volume: refused, nothing written
        f->files["/SD/cores/big.bin"] = pattern(500000, 3);
        f->cap["usb0"] = f->used("usb0") + 1000;
        uint64_t w0 = f->bytes_written;
        CHECK(fo_plan(*f, "/SD/cores/big.bin", "/Usb0/in", true, &p) == FO_ERR_NOROOM && f->bytes_written == w0 && f->files.count("/SD/cores/big.bin"));
        // a name in the way on the same volume: asked, then the bytes are copied (no rename over a file), and the source goes
        f->cap["sd"] = 5000000;
        f->files["/SD/empty/q.bin"] = pattern(10, 1); f->files["/SD/cores/q.bin"] = src;
        CHECK(fo_plan(*f, "/SD/cores/q.bin", "/SD/empty", true, &p) == FO_EXISTS && !p.rename_only);
        CHECK(fo_move(*f, p, false, buf, sizeof(buf), 0, 0) == FO_EXISTS && f->files.count("/SD/cores/q.bin"));
        CHECK(fo_move(*f, p, true, buf, sizeof(buf), 0, 0) == FO_OK && !f->files.count("/SD/cores/q.bin") && sha(f->files["/SD/empty/q.bin"]) == sha(src));
        delete f;
    }
    // ---- refusals of the plan, and the cancel
    {
        RamFs *f = fresh();
        f->files["/SD/cores/a.bin"] = pattern(100, 1);
        FoPlan p;
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/SD/cores", false, &p) == FO_ERR_SAME);
        CHECK(fo_plan(*f, "/SD/cores/gone", "/Usb0/in", false, &p) == FO_ERR_NOSRC);
        CHECK(fo_plan(*f, "/SD/cores", "/Usb0/in", false, &p) == FO_ERR_NOTFILE);
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/Usb0/nodir", false, &p) == FO_ERR_NOTDIR);
        CHECK(fo_plan(*f, "/SD/cores/a.bin", "/SD/cores/a.bin", false, &p) == FO_ERR_NOTDIR);
        f->files["/SD/cores/big"] = pattern(100000, 2);
        CHECK(fo_plan(*f, "/SD/cores/big", "/Usb0/in", false, &p) == FO_OK);
        cancel_at = 5;
        CHECK(fo_copy(*f, p, false, buf, sizeof(buf), prog_cancel, 0) == FO_CANCELLED);
        CHECK(!f->files.count("/Usb0/in/big") && f->parts() == 0);
        CHECK(fo_plan(*f, "/SD/cores/big", "/Usb0/in", true, &p) == FO_OK);
        cancel_at = 5;
        CHECK(fo_move(*f, p, false, buf, sizeof(buf), prog_cancel, 0) == FO_CANCELLED && f->files.count("/SD/cores/big"));
        delete f;
    }
    printf(fails ? "FAILED %d\n" : "test_fileops: all ok\n", fails);
    return fails != 0;
}
