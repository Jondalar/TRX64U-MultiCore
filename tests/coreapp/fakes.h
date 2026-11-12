// Fakes for the native tests (T93): a file system in memory with switches for the media failures the spec names, and a model of a
// core's register block and windows behind CoreIo. C++11, STL allowed (this is host code).
#ifndef TESTS_COREAPP_FAKES_H
#define TESTS_COREAPP_FAKES_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "fsif.h"
#include "io.h"
#include "video.h"

static inline std::string lower(std::string s)
{
    for (auto &c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

class FakeFs : public CoreFs {
public:
    std::map<std::string, std::vector<uint8_t>> files;     // keyed by the lower-cased path
    std::map<std::string, std::string> names;              // lower-cased path -> path as given
    bool read_only = false;                                // writes and removes fail (write-protected card)
    bool gone = false;                                     // the medium was pulled: everything fails
    std::map<std::string, MediumInfo> media;               // medium_info: what the media level says of a medium (name as listed); absent: the board cannot tell
    int writes = 0, lists = 0;
    int write_attempts = 0;                                // CoreFile::write() calls, successful or not
    int read_budget = -1;                                  // file reads that still succeed (-1: unlimited): a stick pulled mid-copy
    int syncs = 0;                                         // CoreFile::sync() calls over all files
    bool sync_fails = false;
    int open_files = 0;                                    // files open() gave and close() has not taken back
    int fail_loads = 0;                                    // the next N loads fail (the stick is pulled between list and read)
    bool extend_writes = true;                             // CoreFile::write at the end of a file makes it longer (FAT does; the chunked writer needs it)
    int write_fail_at = -1;                                // CoreFile::write calls that still succeed (-1: unlimited)
    int chunks_ok = 0;
    std::vector<uint32_t> write_sizes;                     // the length of every successful CoreFile::write
    std::vector<uint32_t> save_sizes;                      // the length of every CoreFs::save
    // T128: directories that hold nothing (the map above only has files), per entry the FAT date/time and attributes, the free space,
    // and a budget of host mutations after which the medium is full / gone / read only (S7 cuts at every step).
    std::set<std::string> dirs;                            // lower-cased paths without a trailing slash
    struct Meta { uint16_t fdate = 0, ftime = 0; uint8_t attr = 0; };
    std::map<std::string, Meta> meta;                      // lower-cased path
    uint64_t free_space = 1ull << 40;
    int mut_budget = -1;                                   // host mutations (save, remove, mkdir, rename, file write) that still succeed; -1 unlimited
    int budget_mode = 0;                                   // when it runs out: 0 full (mutations fail, reads work), 1 gone (everything fails)
    int mutations = 0;                                     // mutations that succeeded
    uint32_t write_ms = 0;                                 // virtual time a file write costs per 32 KiB (the bench's FakeIo clock)
    void (*tick)(void *arg, uint32_t us) = 0;               // called with the virtual microseconds a file write took
    void *tick_arg = 0;
    bool mut_ok()
    {
        if (gone || read_only) return false;
        if (mut_budget == 0) { if (budget_mode == 1) gone = true; return false; }
        if (mut_budget > 0) mut_budget--;
        mutations++;
        return true;
    }

    void put(const std::string &path, const std::vector<uint8_t> &data) { files[lower(path)] = data; names[lower(path)] = path; }
    void put_text(const std::string &path, const std::string &text) { put(path, std::vector<uint8_t>(text.begin(), text.end())); }
    void put_size(const std::string &path, size_t n, uint8_t fill = 0xA5) { put(path, std::vector<uint8_t>(n, fill)); }
    bool has(const std::string &path) const { return files.count(lower(path)) != 0; }
    std::string text(const std::string &path) const
    {
        auto it = files.find(lower(path));
        return it == files.end() ? std::string() : std::string(it->second.begin(), it->second.end());
    }

    bool medium_info(const char *name, MediumInfo *out) override
    {
        auto it = media.find(name);
        if (it == media.end()) return false;
        *out = it->second;
        return true;
    }
    int list(const char *dir, FsEntry *out, int max)
    {
        lists++;
        if (gone) {
            return -1;
        }
        std::string d = lower(dir);
        while (d.size() > 1 && d.back() == '/') {
            d.pop_back();
        }
        std::string prefix = d == "/" ? "/" : d + "/";
        std::map<std::string, FsEntry> found;
        bool any_dir = d == "/";
        for (auto &kv : files) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            any_dir = true;
            auto nm = names.find(kv.first);              // a file made through a handle (File::write) has no spelling of its own
            std::string rest = (nm != names.end() ? nm->second : kv.first).substr(prefix.size());
            size_t slash = rest.find('/');
            FsEntry e;
            memset(&e, 0, sizeof(e));
            if (slash == std::string::npos) {
                strncpy(e.name, rest.c_str(), sizeof(e.name) - 1);
                e.size = kv.second.size();
            } else {
                strncpy(e.name, rest.substr(0, slash).c_str(), sizeof(e.name) - 1);
                e.dir = true;
            }
            found[lower(e.name)] = e;
        }
        for (auto &dp : dirs) {                              // empty directories
            if (dp == d || dp.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            any_dir = true;
            std::string rest = dp.substr(prefix.size());
            size_t slash = rest.find('/');
            FsEntry e;
            memset(&e, 0, sizeof(e));
            strncpy(e.name, (slash == std::string::npos ? rest : rest.substr(0, slash)).c_str(), sizeof(e.name) - 1);
            e.dir = true;
            if (!found.count(lower(e.name))) {
                found[lower(e.name)] = e;
            }
        }
        if (!any_dir && dirs.count(d)) {
            any_dir = true;
        }
        if (!any_dir) {
            return -1;
        }
        for (auto &kv : found) {                             // date and attributes of the entry
            auto m = meta.find(prefix + kv.first);
            if (m != meta.end()) {
                kv.second.fdate = m->second.fdate;
                kv.second.ftime = m->second.ftime;
                kv.second.attr = m->second.attr;
            }
        }
        int n = 0;
        for (int pass = 0; pass < 2; pass++) {
            for (auto &kv : found) {
                if ((pass == 0) == kv.second.dir && n < max) {
                    out[n++] = kv.second;
                }
            }
        }
        return n;
    }
    bool stat(const char *path, uint64_t *size, bool *dir)
    {
        if (gone) return false;
        auto it = files.find(lower(path));
        if (it != files.end()) {
            if (size) *size = it->second.size();
            if (dir) *dir = false;
            return true;
        }
        if (is_dir(path)) {
            if (size) *size = 0;
            if (dir) *dir = true;
            return true;
        }
        return false;
    }
    bool is_dir(const char *path) const
    {
        std::string d = lower(path);
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        if (d == "/" || dirs.count(d)) return true;
        std::string prefix = d + "/";
        for (auto &kv : files) if (kv.first.compare(0, prefix.size(), prefix) == 0) return true;
        for (auto &dp : dirs) if (dp.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    }
    bool free_bytes(const char *path, uint64_t *out)
    {
        (void)path;
        if (gone) return false;
        *out = free_space;
        return true;
    }
    bool mkdir(const char *path)
    {
        if (gone) return false;
        if (is_dir(path)) return true;
        if (files.count(lower(path))) return false;
        if (!mut_ok()) return false;
        std::string d = lower(path);
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        dirs.insert(d);
        return true;
    }
    bool rename(const char *from, const char *to)
    {
        if (gone) return false;
        std::string f = lower(from), t = lower(to);
        if (files.count(t) || is_dir(to)) return false;
        auto it = files.find(f);
        if (it != files.end()) {
            if (!mut_ok()) return false;
            files[t] = it->second;
            names[t] = to;
            files.erase(it);
            names.erase(f);
            auto m = meta.find(f);
            if (m != meta.end()) { meta[t] = m->second; meta.erase(m); }
            return true;
        }
        if (!is_dir(from)) return false;
        if (!mut_ok()) return false;
        std::string fp = f + "/", tp = t + "/";
        std::vector<std::string> ks;
        for (auto &kv : files) if (kv.first.compare(0, fp.size(), fp) == 0) ks.push_back(kv.first);
        for (auto &k : ks) {
            std::string nk = tp + k.substr(fp.size());
            files[nk] = files[k];
            names[nk] = std::string(to) + "/" + names[k].substr(fp.size());
            files.erase(k);
            names.erase(k);
        }
        std::vector<std::string> ds;
        for (auto &dp : dirs) if (dp == f || dp.compare(0, fp.size(), fp) == 0) ds.push_back(dp);
        for (auto &dp : ds) { dirs.erase(dp); dirs.insert(t + dp.substr(f.size())); }
        auto m = meta.find(f);
        if (m != meta.end()) { meta[t] = m->second; meta.erase(m); }
        return true;
    }
    int load(const char *path, void *buf, uint32_t max)
    {
        if (gone) return -1;
        if (fail_loads > 0) { fail_loads--; return -1; }
        auto it = files.find(lower(path));
        if (it == files.end() || it->second.size() > max) return -1;
        memcpy(buf, it->second.data(), it->second.size());
        return (int)it->second.size();
    }
    std::string deny_prefix;                               // a save under this folder (lower case, no trailing slash) fails: one folder that cannot be written
    bool save(const char *path, const void *buf, uint32_t len)
    {
        if (gone || read_only) return false;
        if (!deny_prefix.empty() && lower(path).compare(0, deny_prefix.size(), lower(deny_prefix)) == 0) return false;
        if (!mut_ok()) return false;
        writes++;
        save_sizes.push_back(len);
        put(path, std::vector<uint8_t>((const uint8_t *)buf, (const uint8_t *)buf + len));
        return true;
    }
    bool remove(const char *path)
    {
        if (gone || read_only) return false;
        std::string k = lower(path);
        if (!files.count(k)) {
            while (k.size() > 1 && k.back() == '/') k.pop_back();
            if (!dirs.count(k)) return false;                  // an empty directory goes; one with entries does not
            std::string prefix = k + "/";
            for (auto &kv : files) if (kv.first.compare(0, prefix.size(), prefix) == 0) return false;
            for (auto &dp : dirs) if (dp.compare(0, prefix.size(), prefix) == 0) return false;
            if (!mut_ok()) return false;
            writes++;
            dirs.erase(k);
            return true;
        }
        if (!mut_ok()) return false;
        writes++;
        meta.erase(k);
        return files.erase(k) != 0;
    }
    CoreFile *open(const char *path, bool rw);
    void close(CoreFile *f) { open_files--; delete f; }
};

class FakeFile : public CoreFile {
    FakeFs *fs_;
    std::string key_;
    bool rw_;
public:
    int syncs = 0;
    FakeFile(FakeFs *fs, const std::string &key, bool rw) : fs_(fs), key_(key), rw_(rw) { }
    uint64_t size() { return fs_->files[key_].size(); }
    bool writable() { return rw_; }
    bool read(uint64_t off, void *buf, uint32_t len)
    {
        auto &v = fs_->files[key_];
        if (fs_->gone || off > v.size() || len > v.size() - off) return false;
        if (fs_->read_budget == 0) return false;
        if (fs_->read_budget > 0) fs_->read_budget--;
        memcpy(buf, v.data() + off, len);
        return true;
    }
    bool write(uint64_t off, const void *buf, uint32_t len)
    {
        auto &v = fs_->files[key_];
        fs_->write_attempts++;
        if (!rw_ || fs_->gone || fs_->read_only || off > v.size()) return false;
        if (!fs_->mut_ok()) return false;
        if (fs_->tick && fs_->write_ms) fs_->tick(fs_->tick_arg, (uint32_t)((uint64_t)fs_->write_ms * 1000 * len / 32768));
        if (fs_->write_fail_at >= 0 && fs_->chunks_ok >= fs_->write_fail_at) return false;   // the stick is pulled in the middle of a file
        if (len > v.size() - off) {                    // appending at the end extends the file, as FAT does; a gap is refused
            if (!fs_->extend_writes) return false;
            v.resize((size_t)off + len);
        }
        fs_->chunks_ok++;
        fs_->writes++;
        fs_->write_sizes.push_back(len);
        memcpy(v.data() + off, buf, len);
        return true;
    }
    bool sync() { fs_->syncs++; return !fs_->gone && !fs_->sync_fails; }
};

inline CoreFile *FakeFs::open(const char *path, bool rw)
{
    std::string k = lower(path);
    if (gone || !files.count(k)) return 0;
    open_files++;
    return new FakeFile(this, k, rw && !read_only);
}

// A core's windows and register file, as plain memory. Cores' models derive from it and add behaviour in wr().
class FakeIo : public CoreIo {
public:
    uint8_t regs[0x1000];
    std::vector<uint8_t> ddr, ram;
    std::vector<uint8_t> rom[4];           // the arrays behind the ROM aperture, indexed by regs[rom_sel_reg]
    int rom_sel_reg = -1;
    uint32_t t = 0;                        // virtual ms
    uint64_t us = 0;                       // virtual microseconds
    int stop_calls = 0, reset_asserts = 0, reset_releases = 0;
    bool stopped = false, in_reset = true; // the host holds the core from power-up
    struct Wr { uint32_t reg; uint8_t v; uint32_t t; };
    std::vector<Wr> log;                   // every register write, in order
    std::vector<std::string> events;       // reset / stop / rom writes, in order, for sequence checks

    FakeIo() : ddr(16u << 20, 0), ram(65536, 0)
    {
        memset(regs, 0, sizeof(regs));
        for (auto &r : rom) r.assign(16384, 0);
    }
    virtual void on_wr(uint32_t, uint8_t) { }
    uint8_t pal[16][3] = {};               // the scaler path's palette as the application wrote it
    int pal_writes = 0;
    int vfmt = -1;                         // C64_VIDEOFORMAT as the application wrote it, -1: never
    void video_format(uint8_t v) { vfmt = v; }
    void palette(uint8_t n, uint8_t r, uint8_t g, uint8_t b) { pal[n & 15][0] = r; pal[n & 15][1] = g; pal[n & 15][2] = b; pal_writes++; }
    std::vector<Wr> led_log;            // T159: every write of the case LED selector (reg = 0, v = the byte), with the virtual time
    void case_led_select(uint8_t v) { led_log.push_back({0, v, t}); }
    std::vector<Wr> vol_log;            // T160: every write of the host's master volume (reg = 0, v = the byte), with the virtual time
    void master_volume(uint8_t v) { vol_log.push_back({0, v, t}); }
    uint8_t hk_regs_now[11] = {};       // T181: the host's hotkey mask registers as the application wrote them last (hotkey_regs), and how many times
    int hk_writes = 0;
    void hotkey_regs(const uint8_t r[11]) { memcpy(hk_regs_now, r, 11); hk_writes++; }
    uint8_t kb[8] = {};                 // T181: the physical keys the bench holds down (bit = row of the column), and the RESTORE level
    bool kb_restore = false;
    bool keys_read(uint8_t m[8], bool *r) { memcpy(m, kb, 8); *r = kb_restore; return true; }
    void key(int idx, bool down) { if (idx == 64) kb_restore = down; else if (down) kb[idx / 8] |= (uint8_t)(1u << (idx % 8)); else kb[idx / 8] &= (uint8_t)~(1u << (idx % 8)); }
    uint8_t bling = 0;                  // BLING_RX_FLAGS: b1 SHIFT LOCK down (a test sets it), b0 lock_off (the core's app writes it)
    uint8_t bling_flags() { return bling; }
    void bling_lock_off(bool on) { bling = (uint8_t)((bling & ~1) | (on ? 1 : 0)); }
    uint8_t rd(uint32_t reg) { return regs[reg & 0xFFF]; }
    void wr(uint32_t reg, uint8_t v) { log.push_back({reg, v, t}); regs[reg & 0xFFF] = v; on_wr(reg, v); }
    virtual void ddr_write(uint32_t off, const void *p, uint32_t n) { memcpy(&ddr.at(off), p, n); }
    virtual void ddr_read(uint32_t off, void *p, uint32_t n) { memcpy(p, &ddr.at(off), n); }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        int sel = rom_sel_reg >= 0 ? regs[rom_sel_reg] & 3 : 0;
        events.push_back("rom_write sel=" + std::to_string(sel));
        memcpy(&rom[sel].at(off), p, n);
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        int sel = rom_sel_reg >= 0 ? regs[rom_sel_reg] & 3 : 0;
        memcpy(p, &rom[sel].at(off), n);
    }
    // An access past $FFFF is a defect of the caller, not of the fake: it is clipped and noted, so a test sees it as a
    // red CHECK on `overrun` (or on what the caller returned) instead of a heap error from memcpy past the vector.
    bool overrun = false;
    uint32_t clip(uint32_t a, uint32_t n) { if (a >= ram.size()) { overrun = true; return 0; } if (n > ram.size() - a) { overrun = true; return (uint32_t)ram.size() - a; } return n; }
    void ram_write(uint32_t a, const void *p, uint32_t n) { n = clip(a, n); if (n) memcpy(&ram[a], p, n); }
    void ram_read(uint32_t a, void *p, uint32_t n) { uint32_t m = clip(a, n); memset(p, 0, n); if (m) memcpy(p, &ram[a], m); }
    void stop(bool on) { stopped = on; stop_calls++; events.push_back(on ? "stop" : "run"); }
    void mode_reset(bool on)
    {
        in_reset = on;
        if (on) reset_asserts++; else reset_releases++;
        events.push_back(on ? "mode_reset" : "mode_unreset");
    }
    virtual void advance(uint32_t u) { us += u; t = (uint32_t)(us / 1000); }
    uint32_t now_ms() { return t; }
    void delay_ms(uint32_t ms) { advance(ms * 1000); }
    void delay_us(uint32_t u) { advance(u); }
};

// Records the pixel layer's register writes.
class PxRec : public PxPort {
public:
    struct W { uint32_t reg, v; };
    std::vector<W> w;
    struct Pl { int x, y, w, h; };
    std::vector<Pl> place;                              // T107 5.9: every place the video service gave the menu
    void menu_place(int x, int y, int w_, int h) { place.push_back({x, y, w_, h}); }
    std::vector<std::pair<int, int>> canvas;            // T164 D10a: every canvas size the video service told the menu
    void (*on_canvas)(int, int) = nullptr;              // the bench with a view: the real ui_set_canvas
    void menu_canvas(int w_, int h) { canvas.push_back({w_, h}); if (on_canvas) on_canvas(w_, h); }
    void w32(uint32_t reg, uint32_t v) { w.push_back({reg, v}); }
    int count(uint32_t reg) const { int n = 0; for (auto &x : w) n += x.reg == reg; return n; }
    uint32_t last(uint32_t reg) const { uint32_t v = 0xDEADBEEF; for (auto &x : w) if (x.reg == reg) v = x.v; return v; }
};

#endif
