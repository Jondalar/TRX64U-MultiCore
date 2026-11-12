// The Amiga side of the folder disk benches, without the model of the core (T128 section 2): an independent reader and writer of FFS volumes
// that shares no code with coreapp/src/amiga_ffs.cc or folderhd.cc. It reads a disk the way the Amiga's FFS does (RDB, partition, boot block,
// root, hash chains, headers, extension blocks, bitmap), and writes the way FFS does (allocates from the bitmap, writes data blocks into free
// blocks, then header, chain, bitmap) - through a BlockSource: the sector mailbox of the Amiga model (folderhd_bench.h) or a CoreFile
// (tests/coreapp/fmfs64/test_fmfs_folder.cc). Format knowledge: the Amiga Developer docs / ADF format description, checked against amitools
// (spec section 5.2: tests/coreapp/folderhd_amitools.py).
#ifndef TESTS_COREAPP_FOLDERHD_READER_H
#define TESTS_COREAPP_FOLDERHD_READER_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "check.h"

namespace bench {

struct BlockSource {
    virtual ~BlockSource() { }
    virtual bool read(uint32_t lba, uint8_t *out) = 0;
    virtual bool write(uint32_t lba, const uint8_t *in) = 0;
};

static inline uint32_t be(const uint8_t *b, int l)
{
    return ((uint32_t)b[l * 4] << 24) | ((uint32_t)b[l * 4 + 1] << 16) | ((uint32_t)b[l * 4 + 2] << 8) | b[l * 4 + 3];
}
static inline void put(uint8_t *b, int l, uint32_t v)
{
    b[l * 4] = (uint8_t)(v >> 24); b[l * 4 + 1] = (uint8_t)(v >> 16); b[l * 4 + 2] = (uint8_t)(v >> 8); b[l * 4 + 3] = (uint8_t)v;
}
static inline uint32_t sum_all(const uint8_t *b)
{
    uint32_t s = 0;
    for (int i = 0; i < 128; i++) s += be(b, i);
    return s;
}
// the international upper case: a-z, and the Latin-1 letters 0xE0..0xFE without the division sign
static inline int up(int c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    return c;
}
static inline int hashname(const std::string &n)
{
    uint32_t h = (uint32_t)n.size();
    for (unsigned char c : n) h = (h * 13 + (unsigned)up(c)) & 0x7FF;
    return (int)(h % 72);
}
static inline std::string fold(const std::string &n)
{
    std::string r;
    for (unsigned char c : n) r += (char)up(c);
    return r;
}

struct Ent {
    std::string path;                      // "dir/sub/file"
    bool dir = false;
    uint32_t blk = 0, size = 0, prot = 0, days = 0, mins = 0, ticks = 0, parent = 0;
    std::string comment;
    std::vector<uint32_t> data;
    std::vector<uint32_t> exts;
};

class Reader {
public:
    BlockSource &d;
    std::vector<std::string> errs;
    uint32_t cyl = 0, secs = 0, heads = 0, lo = 0, hi = 0, reserved = 0, dostype = 0;
    uint32_t nblocks = 0, base = 0, root = 0;
    std::vector<uint8_t> owner;            // 0 unclaimed, 1 boot, 2 root, 3 bitmap, 4 header, 5 data, 6 extension, 7 bitmap extension
    std::vector<Ent> ents;
    std::vector<uint32_t> bm_blocks;
    uint64_t reads = 0;

    explicit Reader(BlockSource &dev) : d(dev) { }
    void err(const std::string &s) { if (errs.size() < 40) errs.push_back(s); }
    bool rb(uint32_t blk, uint8_t *o)
    {
        reads++;
        if (blk >= nblocks) return false;
        return d.read(base + blk, o);
    }

    bool open()
    {
        uint8_t b[512];
        if (!d.read(0, b)) { err("no RDB sector"); return false; }
        if (memcmp(b, "RDSK", 4)) { err("no RDSK"); return false; }
        {                                                    // the RDB sums over its SummedLongs (64)
            uint32_t s = 0;
            for (uint32_t i = 0; i < be(b, 1); i++) s += be(b, (int)i);
            if (s) { err("RDB checksum"); return false; }
        }
        cyl = be(b, 16); secs = be(b, 17); heads = be(b, 18);
        uint32_t part = be(b, 7);
        if (!d.read(part, b)) { err("no PART sector"); return false; }
        if (memcmp(b, "PART", 4)) { err("no PART"); return false; }
        {
            uint32_t s = 0;
            for (uint32_t i = 0; i < be(b, 1); i++) s += be(b, (int)i);
            if (s) { err("PART checksum"); return false; }
        }
        if (be(b, 32) < 16) { err("DosEnvec too short"); return false; }
        uint32_t blocksz = be(b, 33), sph = be(b, 35 + 1), surf = be(b, 35), bpt = be(b, 37);
        if (blocksz != 128 || sph != 1 || surf != heads || bpt != secs) { err("DosEnvec geometry"); return false; }
        reserved = be(b, 38);
        lo = be(b, 41);
        hi = be(b, 42);
        dostype = be(b, 48);
        if (dostype != 0x444F5303) err("DosType is not DOS\\3");
        base = lo * heads * secs;
        nblocks = (hi - lo + 1) * heads * secs;
        root = nblocks / 2;
        owner.assign(nblocks, 0);
        if (cyl != hi + 1) err("RDB cylinders and the partition do not agree");
        return true;
    }

    void claim(uint32_t blk, uint8_t what, const std::string &who)
    {
        if (blk >= nblocks) { err("block out of the partition: " + who); return; }
        if (owner[blk]) { err("block claimed twice: " + std::to_string(blk) + " " + who); return; }
        owner[blk] = what;
    }

    // the checks of a header block that all kinds have
    bool hdr_ok(const uint8_t *b, uint32_t blk, const std::string &who)
    {
        if (sum_all(b) != 0) { err("checksum of " + who); return false; }
        if (be(b, 0) != 2) { err("type of " + who); return false; }
        if (be(b, 1) != blk) { err("own key of " + who); return false; }
        return true;
    }
    static std::string bstr(const uint8_t *b, int off, int max)
    {
        int n = b[off];
        if (n > max) n = max;
        return std::string((const char *)b + off + 1, (size_t)n);
    }

    void walk_dir(uint32_t dirblk, const std::string &prefix)
    {
        uint8_t db[512];
        if (!rb(dirblk, db)) { err("dir block unreadable"); return; }
        std::set<std::string> names;
        for (int slot = 0; slot < 72; slot++) {
            uint32_t blk = be(db, 6 + slot);
            int guard = 0;
            while (blk && guard++ < 100000) {
                uint8_t b[512];
                if (!rb(blk, b)) { err("entry unreadable"); break; }
                std::string nm = bstr(b, 432, 30);
                std::string who = prefix + nm;
                if (!hdr_ok(b, blk, who)) break;
                if (hashname(nm) != slot) err("hash slot of " + who);
                if (be(b, 125) != dirblk) err("parent of " + who);
                if (!names.insert(fold(nm)).second) err("duplicate name " + who);
                if (nm.empty() || nm.find(':') != std::string::npos || nm.find('/') != std::string::npos) err("illegal name " + who);
                claim(blk, 4, who);
                Ent e;
                e.path = who;
                e.blk = blk;
                e.parent = dirblk;
                e.prot = be(b, 80);
                e.days = be(b, 105); e.mins = be(b, 106); e.ticks = be(b, 107);
                e.comment = bstr(b, 328, 79);
                uint32_t sec = be(b, 127);
                if (sec == 2) {
                    e.dir = true;
                    ents.push_back(e);
                    walk_dir(blk, who + "/");
                } else if (sec == 0xFFFFFFFDu) {
                    e.size = be(b, 81);
                    uint32_t nd = (e.size + 511) / 512;
                    uint32_t hs = be(b, 2);
                    if (hs > 72) err("high_seq of " + who);
                    for (uint32_t i = 0; i < hs && i < 72; i++) e.data.push_back(be(b, 77 - (int)i));
                    if (e.data.size() && be(b, 4) != e.data[0]) err("first_data of " + who);
                    uint32_t ext = be(b, 126);
                    int eg = 0;
                    while (ext && eg++ < 1000000) {
                        uint8_t x[512];
                        if (!rb(ext, x)) { err("extension unreadable " + who); break; }
                        if (sum_all(x) != 0 || be(x, 0) != 16 || be(x, 1) != ext || be(x, 125) != blk || be(x, 127) != 0xFFFFFFFDu) { err("extension block of " + who); break; }
                        claim(ext, 6, who);
                        e.exts.push_back(ext);
                        uint32_t n = be(x, 2);
                        if (n > 72) { err("extension high_seq of " + who); break; }
                        for (uint32_t i = 0; i < n; i++) e.data.push_back(be(x, 77 - (int)i));
                        ext = be(x, 126);
                    }
                    if (e.data.size() != nd) err("data block count of " + who + " " + std::to_string(e.data.size()) + " vs " + std::to_string(nd));
                    for (uint32_t blkn : e.data) claim(blkn, 5, who);
                    ents.push_back(e);
                } else {
                    err("secondary type of " + who);
                }
                blk = be(b, 124);
            }
        }
    }

    // opens the disk, walks everything, checks the bitmap against what was claimed
    bool check_all()
    {
        if (!open()) return false;
        uint8_t bt[1024];
        if (!d.read(base, bt) || !d.read(base + 1, bt + 512)) { err("no boot block"); return false; }
        if (be(bt, 0) != 0x444F5303) err("boot block DosType");
        {
            uint32_t s = 0;                                        // the boot checksum: carry wrapped sum of 256 longs, inverted
            for (int i = 0; i < 256; i++) { uint32_t v = be(bt, i); s += v; if (s < v) s++; }
            if (s != 0xFFFFFFFFu) err("boot checksum");
        }
        if (be(bt, 2) != root) err("boot block root pointer");
        claim(0, 1, "boot");
        claim(1, 1, "boot");
        uint8_t rb_[512];
        if (!rb(root, rb_)) { err("no root"); return false; }
        if (sum_all(rb_) != 0 || be(rb_, 0) != 2 || be(rb_, 127) != 1 || be(rb_, 3) != 72) { err("root block"); return false; }
        if (be(rb_, 78) != 0xFFFFFFFFu) err("bitmap flag not valid");
        claim(root, 2, "root");
        // bitmap
        std::vector<uint32_t> pages;
        for (int i = 0; i < 25; i++) if (be(rb_, 79 + i)) pages.push_back(be(rb_, 79 + i));
        uint32_t ext = be(rb_, 104);
        int eg = 0;
        while (ext && eg++ < 100000) {
            uint8_t x[512];
            if (!rb(ext, x)) { err("bitmap extension unreadable"); break; }
            claim(ext, 7, "bitmap ext");
            for (int i = 0; i < 127; i++) if (be(x, i)) pages.push_back(be(x, i));
            ext = be(x, 127);
        }
        uint32_t want = nblocks > 2 ? (nblocks - 2 + 4063) / 4064 : 0;
        if (pages.size() != want) err("bitmap blocks: " + std::to_string(pages.size()) + " vs " + std::to_string(want));
        std::vector<uint8_t> freebit(nblocks, 0);
        for (size_t k = 0; k < pages.size(); k++) {
            uint8_t b[512];
            if (!rb(pages[k], b)) { err("bitmap block unreadable"); continue; }
            if (sum_all(b) != 0) err("bitmap checksum");
            claim(pages[k], 3, "bitmap");
            for (uint32_t i = 0; i < 127 * 32; i++) {
                uint32_t blk = 2 + (uint32_t)k * 4064 + i;
                if (blk >= nblocks) break;
                freebit[blk] = (be(b, 1 + (int)(i / 32)) >> (i % 32)) & 1;
            }
        }
        walk_dir(root, "");
        uint64_t bad = 0, firstbad = 0;
        for (uint32_t b = 2; b < nblocks; b++) {
            bool used = owner[b] != 0;
            if (used == (freebit[b] != 0)) { if (!bad) firstbad = b; bad++; }
        }
        if (bad) err("bitmap disagrees with the claimed blocks in " + std::to_string(bad) + " blocks, first " + std::to_string(firstbad));
        return errs.empty();
    }

    const Ent *find(const std::string &path) const
    {
        for (auto &e : ents) if (e.path == path) return &e;
        return 0;
    }
    // block i of the content of a file as the reader gets it; the size bytes of the file in all
    std::vector<uint8_t> content(const Ent &e)
    {
        std::vector<uint8_t> out;
        out.reserve(e.size);
        for (uint32_t i = 0; i < e.data.size(); i++) {
            uint8_t b[512];
            rb(e.data[i], b);
            uint32_t n = e.size - i * 512 < 512 ? e.size - i * 512 : 512;
            out.insert(out.end(), b, b + n);
        }
        return out;
    }
    void block_of(const Ent &e, uint32_t i, uint8_t *o) { rb(e.data[i], o); }
};


// ---------------------------------------------------------------------------------------------------------------------------------------
// What FFS does when it writes: allocates from the bitmap, writes data blocks straight to the disk, keeps headers, directories and bitmap in its
// cache and writes them at the end - bitmap first, root last, and the root's bitmap flag is 0 for as long as the bitmap is not on the disk.
// ---------------------------------------------------------------------------------------------------------------------------------------
// FFS allocates on from the last block (next fit). A first-fit writer takes the lowest free block, so a block it has just freed is the next one it writes data into.
static inline bool &first_fit_flag() { static bool f = false; return f; }

class Driver {
public:
    BlockSource &d;
    uint32_t base = 0, nblocks = 0, root = 0;
    std::map<uint32_t, std::vector<uint8_t>> cache;
    std::set<uint32_t> dirty;
    uint32_t days = 16000, mins = 600, ticks = 0;
    bool invalid_written = false;
    uint32_t hint = 2;
    uint64_t data_writes = 0;
    std::vector<uint32_t> pages;

    explicit Driver(BlockSource &dev) : d(dev)
    {
        Reader r(d);
        r.open();
        base = r.base; nblocks = r.nblocks; root = r.root;
        uint8_t *rb = blk(root);
        for (int i = 0; i < 25; i++) if (be(rb, 79 + i)) pages.push_back(be(rb, 79 + i));
        uint32_t ext = be(rb, 104);
        while (ext) {
            uint8_t *x = blk(ext);
            for (int i = 0; i < 127; i++) if (be(x, i)) pages.push_back(be(x, i));
            ext = be(x, 127);
        }
    }
    uint8_t *blk(uint32_t b)
    {
        auto it = cache.find(b);
        if (it == cache.end()) {
            std::vector<uint8_t> v(512);
            d.read(base + b, v.data());
            it = cache.emplace(b, v).first;
        }
        return it->second.data();
    }
    void invalidate()                               // the bitmap is going to change: the root says so first
    {
        if (invalid_written) return;
        invalid_written = true;
        uint8_t *rb = blk(root);
        uint8_t tmp[512];
        memcpy(tmp, rb, 512);
        put(tmp, 78, 0);
        put(tmp, 5, 0); put(tmp, 5, 0u - sum_all(tmp));
        d.write(base + root, tmp);
    }
    void touch(uint32_t b) { invalidate(); dirty.insert(b); }
    static void seal(uint8_t *b) { put(b, 5, 0); put(b, 5, 0u - sum_all(b)); }
    static void seal_bm(uint8_t *b) { put(b, 0, 0); put(b, 0, 0u - sum_all(b)); }

    bool is_free(uint32_t b)
    {
        uint32_t k = (b - 2) / 4064, i = (b - 2) % 4064;
        return (be(blk(pages[k]), 1 + (int)(i / 32)) >> (i % 32)) & 1;
    }
    void set_free(uint32_t b, bool f)
    {
        uint32_t k = (b - 2) / 4064, i = (b - 2) % 4064;
        uint8_t *p = blk(pages[k]);
        uint32_t v = be(p, 1 + (int)(i / 32));
        v = f ? (v | (1u << (i % 32))) : (v & ~(1u << (i % 32)));
        put(p, 1 + (int)(i / 32), v);
        touch(pages[k]);
    }
    uint32_t alloc()
    {
        for (uint32_t n = 0; n < nblocks; n++) {
            uint32_t b = hint + n;
            if (b >= nblocks) b = 2 + (b - nblocks);
            if (b >= 2 && is_free(b)) {
                set_free(b, false);
                hint = b + 1;
                return b;
            }
        }
        return 0;
    }
    void release(uint32_t b) { set_free(b, true); if (first_fit_flag()) hint = 2; }                                // FFS allocates on from the last block (next fit): a freed block is not reused at once

    // names and chains
    static std::string nameof(const uint8_t *h) { return Reader::bstr(h, 432, 30); }
    uint32_t lookup(uint32_t dir, const std::string &name)
    {
        uint32_t b = be(blk(dir), 6 + hashname(name));
        while (b) {
            if (fold(nameof(blk(b))) == fold(name)) return b;
            b = be(blk(b), 124);
        }
        return 0;
    }
    uint32_t resolve(const std::string &path)
    {
        uint32_t cur = root;
        size_t i = 0;
        while (i < path.size()) {
            size_t j = path.find('/', i);
            if (j == std::string::npos) j = path.size();
            cur = lookup(cur, path.substr(i, j - i));
            if (!cur) return 0;
            i = j + 1;
        }
        return cur;
    }
    void link(uint32_t dir, uint32_t h)
    {
        uint8_t *hb = blk(h), *db = blk(dir);
        int s = hashname(nameof(hb));
        put(hb, 124, be(db, 6 + s));
        put(hb, 125, dir);
        put(db, 6 + s, h);
        seal(hb); seal(db);
        touch(h); touch(dir);
    }
    void unlink(uint32_t dir, uint32_t h)
    {
        uint8_t *db = blk(dir);
        int s = hashname(nameof(blk(h)));
        uint32_t b = be(db, 6 + s);
        if (b == h) {
            put(db, 6 + s, be(blk(h), 124));
            seal(db); touch(dir);
            return;
        }
        while (b) {
            uint8_t *pb = blk(b);
            if (be(pb, 124) == h) {
                put(pb, 124, be(blk(h), 124));
                seal(pb); touch(b);
                return;
            }
            b = be(pb, 124);
        }
    }
    void setname(uint8_t *hb, const std::string &name)
    {
        memset(hb + 432, 0, 32);
        hb[432] = (uint8_t)name.size();
        memcpy(hb + 433, name.data(), name.size());
    }

    // the pointer lists
    std::vector<uint32_t> ptrs_of(uint32_t h, std::vector<uint32_t> *exts = 0)
    {
        std::vector<uint32_t> p;
        uint8_t *hb = blk(h);
        for (uint32_t i = 0; i < be(hb, 2); i++) p.push_back(be(hb, 77 - (int)i));
        uint32_t e = be(hb, 126);
        while (e) {
            if (exts) exts->push_back(e);
            uint8_t *x = blk(e);
            for (uint32_t i = 0; i < be(x, 2); i++) p.push_back(be(x, 77 - (int)i));
            e = be(x, 126);
        }
        return p;
    }
    // writes header and extension blocks for a file with these data blocks
    void rebuild(uint32_t h, const std::vector<uint32_t> &p, uint32_t size)
    {
        std::vector<uint32_t> old_exts;
        ptrs_of(h, &old_exts);
        uint32_t need = p.size() > 72 ? (uint32_t)((p.size() - 72 + 71) / 72) : 0;
        std::vector<uint32_t> exts;
        for (uint32_t i = 0; i < need; i++) exts.push_back(i < old_exts.size() ? old_exts[i] : alloc());
        for (size_t i = need; i < old_exts.size(); i++) release(old_exts[i]);
        uint8_t *hb = blk(h);
        for (int i = 0; i < 72; i++) put(hb, 6 + i, 0);
        size_t n0 = p.size() < 72 ? p.size() : 72;
        put(hb, 2, (uint32_t)n0);
        put(hb, 4, p.empty() ? 0 : p[0]);
        for (size_t i = 0; i < n0; i++) put(hb, 77 - (int)i, p[i]);
        put(hb, 81, size);
        put(hb, 126, exts.empty() ? 0 : exts[0]);
        seal(hb); touch(h);
        for (uint32_t e = 0; e < need; e++) {
            uint8_t *x = blk(exts[e]);
            memset(x, 0, 512);
            put(x, 0, 16); put(x, 1, exts[e]);
            size_t from = 72 + (size_t)e * 72, cnt = p.size() - from < 72 ? p.size() - from : 72;
            put(x, 2, (uint32_t)cnt);
            for (size_t i = 0; i < cnt; i++) put(x, 77 - (int)i, p[from + i]);
            put(x, 125, h);
            put(x, 126, e + 1 < need ? exts[e + 1] : 0);
            put(x, 127, 0xFFFFFFFDu);
            seal(x); touch(exts[e]);
        }
    }
    void write_data(uint32_t b, const uint8_t *data, size_t n)
    {
        uint8_t blkbuf[512];
        memset(blkbuf, 0, 512);
        memcpy(blkbuf, data, n);
        d.write(base + b, blkbuf);
        data_writes++;
    }

    uint32_t create_file(uint32_t dir, const std::string &name, const std::vector<uint8_t> &data, uint32_t prot = 0)
    {
        uint32_t h = alloc();
        uint8_t *hb = blk(h);
        memset(hb, 0, 512);
        put(hb, 0, 2); put(hb, 1, h);
        put(hb, 80, prot);
        put(hb, 105, days); put(hb, 106, mins); put(hb, 107, ticks);
        setname(hb, name);
        put(hb, 127, 0xFFFFFFFDu);
        std::vector<uint32_t> p;
        for (size_t off = 0; off < data.size(); off += 512) {
            uint32_t b = alloc();
            write_data(b, data.data() + off, data.size() - off < 512 ? data.size() - off : 512);
            p.push_back(b);
        }
        rebuild(h, p, (uint32_t)data.size());
        link(dir, h);
        return h;
    }
    void append(uint32_t h, const std::vector<uint8_t> &data)
    {
        std::vector<uint32_t> p = ptrs_of(h);
        uint32_t size = be(blk(h), 81);
        size_t done = 0;
        if (size % 512 && !p.empty()) {                         // the last block is read, filled and written again
            uint8_t b[512];
            d.read(base + p.back(), b);
            size_t room = 512 - size % 512, n = data.size() < room ? data.size() : room;
            memcpy(b + size % 512, data.data(), n);
            d.write(base + p.back(), b);
            data_writes++;
            done = n;
        }
        for (; done < data.size(); done += 512) {
            uint32_t b = alloc();
            write_data(b, data.data() + done, data.size() - done < 512 ? data.size() - done : 512);
            p.push_back(b);
        }
        rebuild(h, p, size + (uint32_t)data.size());
    }
    void truncate(uint32_t h, uint32_t newsize)
    {
        std::vector<uint32_t> p = ptrs_of(h);
        size_t keep = (newsize + 511) / 512;
        for (size_t i = keep; i < p.size(); i++) release(p[i]);
        p.resize(keep);
        rebuild(h, p, newsize);
    }
    void remove(uint32_t dir, uint32_t h)
    {
        unlink(dir, h);
        std::vector<uint32_t> exts;
        std::vector<uint32_t> p = ptrs_of(h, &exts);
        for (uint32_t b : p) release(b);
        for (uint32_t b : exts) release(b);
        release(h);
    }
    uint32_t mkdir(uint32_t dir, const std::string &name)
    {
        uint32_t h = alloc();
        uint8_t *hb = blk(h);
        memset(hb, 0, 512);
        put(hb, 0, 2); put(hb, 1, h);
        put(hb, 105, days); put(hb, 106, mins); put(hb, 107, ticks);
        setname(hb, name);
        put(hb, 127, 2);
        seal(hb); touch(h);
        link(dir, h);
        return h;
    }
    void rmdir(uint32_t dir, uint32_t h)
    {
        unlink(dir, h);
        release(h);
    }
    void rename(uint32_t dir, uint32_t h, uint32_t newdir, const std::string &newname)
    {
        unlink(dir, h);
        uint8_t *hb = blk(h);
        setname(hb, newname);
        seal(hb); touch(h);
        link(newdir, h);
    }
    void set_protect(uint32_t h, uint32_t prot) { uint8_t *hb = blk(h); put(hb, 80, prot); seal(hb); touch(h); }
    void set_comment(uint32_t h, const std::string &c)
    {
        uint8_t *hb = blk(h);
        memset(hb + 328, 0, 80);
        hb[328] = (uint8_t)c.size();
        memcpy(hb + 329, c.data(), c.size());
        seal(hb); touch(h);
    }
    // the cache goes to the disk: everything but the bitmap and the root first, the bitmap, then the root with a valid flag
    void flush()
    {
        std::vector<uint32_t> late;
        for (uint32_t b : dirty) {
            bool bm = std::find(pages.begin(), pages.end(), b) != pages.end();
            if (bm || b == root) { late.push_back(b); continue; }
            d.write(base + b, blk(b));
        }
        for (uint32_t b : late) if (b != root) { seal_bm(blk(b)); d.write(base + b, blk(b)); }
        if (invalid_written) {
            uint8_t *rb = blk(root);
            put(rb, 78, 0xFFFFFFFFu);
            seal(rb);
            d.write(base + root, rb);
            invalid_written = false;
        }
        dirty.clear();
    }
};

}   // namespace bench

#endif
