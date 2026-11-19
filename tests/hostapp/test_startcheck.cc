// T101 S10: what the boot menu reads per entry at start. The files come from tests/hostapp/run.sh (cor_pack.py bundles); the file
// system is a stub that counts every stat, open, list and every byte read, so "the start reads no part" is a number.
#include "startcheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef std::vector<uint8_t> Bytes;

static Bytes slurp(const char *p)
{
    Bytes b;
    FILE *f = fopen(p, "rb");
    if (!f) { printf("cannot open %s\n", p); exit(2); }
    uint8_t t[65536];
    size_t n;
    while ((n = fread(t, 1, sizeof(t), f)) > 0) b.insert(b.end(), t, t + n);
    fclose(f);
    return b;
}

struct Counts { int stats, opens, lists; uint32_t bytes; };
static Counts cnt;

class MemSource : public CorSource {
    const Bytes &b;
public:
    explicit MemSource(const Bytes &x) : b(x) { }
    uint32_t size() { return (uint32_t)b.size(); }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if (off > b.size() || len > b.size() - off) return false;
        memcpy(buf, b.data() + off, len);
        cnt.bytes += len;
        return true;
    }
};

struct File { Bytes data; uint32_t mtime; bool present; };
static File files[4];       // 0 good, 1 modified, 2 other contract, 3 other host
static const char *names[4] = { "/Usb0/good.cor", "/Usb0/mod.cor", "/Usb0/c2.cor", "/Usb0/h2.cor" };

class StubFs : public StartFs {
    int find(const char *p) { for (int i = 0; i < 4; i++) if (!strcmp(p, names[i]) && files[i].present) return i; return -1; }
public:
    bool stat(const char *p, FileStat *st)
    {
        cnt.stats++;
        int i = find(p);
        if (i < 0) return false;
        st->size = (uint32_t)files[i].data.size();
        st->mtime = files[i].mtime;
        return true;
    }
    CorSource *open(const char *p)
    {
        cnt.opens++;
        int i = find(p);
        return i < 0 ? 0 : new MemSource(files[i].data);
    }
    int list(const char *) { cnt.lists++; return 0; }
};

static void entry_for(BootEntry &e, StubFs &fs, int i)
{
    memset(&e, 0, sizeof(e));
    snprintf(e.path, sizeof(e.path), "%s", names[i]);
    snprintf(e.name, sizeof(e.name), "core %d", i);
    CHECK(startcheck_migrate(e, fs, false));        // the full check once, as adding does
}

int main(int argc, char **argv)
{
    if (argc < 4) { printf("usage: good.cor c2.cor h2.cor\n"); return 2; }
    files[0] = { slurp(argv[1]), 0x58C12345u, true };
    files[1] = files[0]; files[1].present = true;
    files[2] = { slurp(argv[2]), 0x58C12346u, true };
    files[3] = { slurp(argv[3]), 0x58C12347u, true };
    StubFs fs;
    cor_host_key = 0x820eded2;
    uint32_t fsize = (uint32_t)files[0].data.size();

    // ADD: the full check reads every part once (this is the cost that moved out of the start)
    BootEntry e;
    memset(&cnt, 0, sizeof(cnt));
    entry_for(e, fs, 0);
    CHECK(cnt.bytes >= fsize - 4096);                          // essentially the whole file
    CHECK(e.size == fsize && e.mtime == 0x58C12345u && e.hdr != 0 && strlen(e.hash) == 64);
    uint32_t add_bytes = cnt.bytes;

    // START, nothing changed: stat + header only
    CorInfo ci;
    FileStat st;
    memset(&cnt, 0, sizeof(cnt));
    CHECK(startcheck_entry(e, fs, ci, st) == EC_OK);
    CHECK(cnt.stats == 1 && cnt.opens == 1 && cnt.lists == 0);
    CHECK(cnt.bytes > 0 && cnt.bytes <= 20 + COR_MANIFEST_MAX + 8 * 4);       // header, manifest, record headers - no part
    CHECK(cnt.bytes * 100 < add_bytes);                                        // under 1 % of what adding reads
    printf("bytes read: add %u, start %u (file %u)\n", (unsigned)add_bytes, (unsigned)cnt.bytes, (unsigned)fsize);

    // START over a full menu (BOOT_MAX entries), still no listing and no part
    {
        memset(&cnt, 0, sizeof(cnt));
        for (int i = 0; i < BOOT_MAX; i++) CHECK(startcheck_entry(e, fs, ci, st) == EC_OK);
        CHECK(cnt.lists == 0 && cnt.stats == BOOT_MAX && cnt.bytes <= (uint32_t)BOOT_MAX * (20 + COR_MANIFEST_MAX + 32));
    }

    // (a) a changed directory entry marks it: other mtime, other size
    files[0].mtime = 0x58C12999u;
    CHECK(startcheck_entry(e, fs, ci, st) == EC_CHANGED);
    files[0].mtime = 0x58C12345u;
    files[0].data.push_back(0);
    CHECK(startcheck_entry(e, fs, ci, st) == EC_CHANGED || startcheck_entry(e, fs, ci, st) == EC_BAD);   // a longer file is not the package any more
    files[0].data.pop_back();
    CHECK(startcheck_entry(e, fs, ci, st) == EC_OK);

    // (b) the header changed, size and mtime the same: the digest catches it
    {
        files[1].data = files[0].data;
        files[1].mtime = files[0].mtime;
        Bytes &d = files[1].data;
        const char *k = "\"version\"";
        size_t at = 0;
        for (size_t i = 20; i + 9 < d.size(); i++) if (!memcmp(&d[i], k, 9)) { at = i; break; }
        CHECK(at != 0);
        size_t v = at + 9;
        while (d[v] != '"') v++;                   // the opening quote of the value
        d[v + 1] = d[v + 1] == '9' ? '8' : '9';    // one character of the version
        BootEntry e1 = e;
        snprintf(e1.path, sizeof(e1.path), "%s", names[1]);
        CHECK(startcheck_entry(e1, fs, ci, st) == EC_CHANGED);
        CHECK(!startcheck_trusts(e1, ci, st));
    }

    // The start no longer checks anything before the menu (T101 S10); the start itself does. A file changed since adding (other mtime) is
    // not trusted, so core_stage reads its parts with verify on, and a damaged part is refused with COR_ERR_HASH; a file that is gone
    // cannot be opened. (core_stage runs `verify = !(trusted && startcheck_trusts(...))`.)
    {
        files[1].data = files[0].data;
        files[1].mtime = files[0].mtime + 7;
        BootEntry e1 = e;
        snprintf(e1.path, sizeof(e1.path), "%s", names[1]);
        CorInfo c1;
        FileStat s1;
        CHECK(startcheck_entry(e1, fs, c1, s1) == EC_CHANGED && !startcheck_trusts(e1, c1, s1));
        files[1].data[c1.off[COR_PART_APP] + 5] ^= 1;
        MemSource m1(files[1].data);
        std::vector<uint8_t> dst(c1.size[COR_PART_APP]);
        CHECK(cor_read_part(m1, c1, COR_PART_APP, dst.data(), 0, 0, true) == COR_ERR_HASH);          // refused at its start
        CHECK(cor_read_part(m1, c1, COR_PART_APP, dst.data(), 0, 0, false) == COR_OK);               // the trusted path would not have noticed
        files[1].present = false;
        CHECK(fs.open(names[1]) == 0);                                                              // missing: nothing to open
        files[1].present = true;
    }

    // a missing file, an entry from an older boot.cfg, the other contract and host
    {
        BootEntry m = e;
        snprintf(m.path, sizeof(m.path), "/Usb0/nothing.cor");
        memset(&cnt, 0, sizeof(cnt));
        CHECK(startcheck_entry(m, fs, ci, st) == EC_MISSING && cnt.opens == 0 && cnt.bytes == 0);
        BootEntry old = e;
        old.mtime = 0; old.hdr = 0;
        CHECK(startcheck_entry(old, fs, ci, st) == EC_UNCHECKED);
        memset(&cnt, 0, sizeof(cnt));
        CHECK(startcheck_migrate(old, fs, false) && cnt.bytes >= fsize - 4096);   // the full check, once
        CHECK(old.mtime == e.mtime && old.hdr == e.hdr && old.size == e.size && !strcmp(old.hash, e.hash));
        memset(&cnt, 0, sizeof(cnt));
        CHECK(startcheck_entry(old, fs, ci, st) == EC_OK && cnt.bytes <= 20 + COR_MANIFEST_MAX + 32);   // and never again
        BootEntry stale = e;                                     // an older entry whose stored hash is not the file's: not accepted silently
        stale.mtime = 0; stale.hdr = 0;
        stale.hash[0] = stale.hash[0] == 'a' ? 'b' : 'a';
        CHECK(!startcheck_migrate(stale, fs, false) && stale.mtime == 0);
        CHECK(startcheck_entry(stale, fs, ci, st) == EC_UNCHECKED);
        BootEntry swapped = e;                                   // an older entry whose file was replaced by one of another size
        swapped.mtime = 0; swapped.hdr = 0; swapped.size = e.size + 48;
        memset(&cnt, 0, sizeof(cnt));
        CHECK(!startcheck_migrate(swapped, fs, false) && swapped.mtime == 0 && cnt.opens == 0 && cnt.bytes == 0);   // no full read
        BootEntry c2 = e;
        snprintf(c2.path, sizeof(c2.path), "%s", names[2]);
        CHECK(startcheck_entry(c2, fs, ci, st) == EC_CONTRACT);
        BootEntry h2 = e;
        snprintf(h2.path, sizeof(h2.path), "%s", names[3]);
        CHECK(startcheck_entry(h2, fs, ci, st) == EC_HOST);
        BootEntry nohash = e;
        nohash.hash[0] = 0;
        startcheck_entry(nohash, fs, ci, st);
        CHECK(!startcheck_trusts(nohash, ci, st));               // no stored hash: nothing to trust
    }

    // a firmware image: only the directory entry, no open
    {
        BootEntry f;
        memset(&f, 0, sizeof(f));
        snprintf(f.path, sizeof(f.path), "%s", names[0]);
        memset(&cnt, 0, sizeof(cnt));
        CHECK(startcheck_firmware(f, fs, st) == EC_UNCHECKED);
        CHECK(startcheck_migrate(f, fs, true) && f.size == fsize && f.mtime == 0x58C12345u && f.hdr == 0 && cnt.bytes == 0);
        CHECK(startcheck_firmware(f, fs, st) == EC_OK && cnt.opens == 0 && cnt.bytes == 0 && cnt.lists == 0);
        files[0].mtime++;
        CHECK(startcheck_firmware(f, fs, st) == EC_CHANGED);
        files[0].mtime--;
    }
    printf(fails ? "test_startcheck: %d FAILED\n" : "test_startcheck: ok\n", fails);
    return fails != 0;
}
