// T104.2 D9.3, S13: update.run_record - the way back from our application. A .cfw (Gideon's rollback or our updater) is checked, staged at
// the address its record names inside the updaters' window, read back, and only then the CPU jumps. The model keeps the window above the
// REU's 16 MB (a plain FakeIo has 16 MB) and records the jump.
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "update.h"

struct UpdIo : FakeIo {
    std::vector<uint8_t> hi;                 // 0x03000000 .. 0x04000000
    int jumps = 0;
    uint32_t jump_to = 0;
    bool corrupt = false;                    // a stage that does not hold: the first write after the first byte flips a bit
    bool have_time = false;
    uint8_t time6[6] = { 26, 10, 26, 11, 30, 5 };
    UpdIo() : hi(0x01000000, 0xEE) { }
    bool bad_write = false;                  // a write outside what the model holds: counted, not a crash, so a control sees a red check
    void ddr_write(uint32_t off, const void *p, uint32_t n)
    {
        bool in_hi = off >= 0x02000000u && (uint64_t)off - 0x02000000u + n <= hi.size();
        bool in_lo = off < 0x02000000u && (uint64_t)off + n <= ddr.size();
        if (!in_hi && !in_lo) {
            bad_write = true;
            return;
        }
        if (off >= 0x02000000u) {
            memcpy(&hi.at(off - 0x02000000u), p, n);
            if (corrupt && n > 8) hi[off - 0x02000000u + 7] ^= 1;
        } else {
            FakeIo::ddr_write(off, p, n);
        }
    }
    void ddr_read(uint32_t off, void *p, uint32_t n)
    {
        if (off >= 0x02000000u && (uint64_t)off - 0x02000000u + n > hi.size()) { memset(p, 0, n); return; }
        if (off < 0x02000000u && (uint64_t)off + n > ddr.size()) { memset(p, 0, n); return; }
        if (off >= 0x02000000u) memcpy(p, &hi.at(off - 0x02000000u), n); else FakeIo::ddr_read(off, p, n);
    }
    const char *product_family() { return "U64MK2"; }
    const char *product_name() { return "C64 Ultimate"; }
    void run_at(uint32_t a) { jumps++; jump_to = a; }
    bool wall_time(uint8_t t[6]) { if (!have_time) return false; memcpy(t, time6, 6); return true; }
    uint32_t word(uint32_t addr) { uint32_t v; memcpy(&v, &hi.at(addr - 0x03000000u), 4); return v; }
};

static void put32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }

static std::vector<uint8_t> image(const char *family, const char *name, std::vector<std::vector<uint8_t>> *payloads,
                                  const std::vector<uint32_t> &load, const std::vector<uint32_t> &start, uint32_t fp = 0x4115ADDE)
{
    std::vector<uint8_t> v;
    put32(v, fp);
    char f[16] = {}, n[16] = {};
    strncpy(f, family, 16); strncpy(n, name, 16);
    v.insert(v.end(), f, f + 16); v.insert(v.end(), n, n + 16);
    for (size_t i = 0; i < payloads->size(); i++) {
        put32(v, load[i]); put32(v, (uint32_t)(*payloads)[i].size()); put32(v, start[i]);
        v.insert(v.end(), (*payloads)[i].begin(), (*payloads)[i].end());
    }
    return v;
}

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(i * 31 + seed + (i >> 8));
    return b;
}

static int run(UpdIo &io, FakeFs &fs, const char *path, std::string *why, bool mono = true)
{
    char msg[240] = "";
    int r = update_run_record(&io, &fs, mono, path, msg, sizeof(msg));
    *why = msg;
    return r;
}

int main()
{
    // 1. the good image, Gideon's shape: one record at 0x03000000, start at its load, a size that is not a chunk multiple
    {
        UpdIo io; FakeFs fs; std::string why;
        std::vector<std::vector<uint8_t>> p = { blob(100003, 7) };
        fs.put("/usb0/up.cfw", image("U64MK2", "", &p, { 0x03000000 }, { 0x03000000 }));
        CHECK_EQ(run(io, fs, "/usb0/up.cfw", &why), 0);
        CHECK_EQ(io.jumps, 1);
        CHECK(!io.bad_write);
        CHECK_EQ(io.jump_to, 0x03000000u);
        CHECK(memcmp(&io.hi[0], p[0].data(), p[0].size()) == 0);
        CHECK_EQ(io.hi[p[0].size()], 0xEE);                        // nothing past the record
        CHECK_EQ(io.word(0x03F00000), 0u);                         // no clock: a cleared record
        CHECK_EQ(io.word(0x03F0000C), 0u);
    }
    // 2. two records, the second names the start (his loader takes the last start); the clock is handed over with the check word
    {
        UpdIo io; FakeFs fs; std::string why;
        io.have_time = true;
        std::vector<std::vector<uint8_t>> p = { blob(5000, 1), blob(70000, 2) };
        fs.put("/USB0/two.CFW", image("U64MK2", "C64 Ultimate", &p, { 0x03000000, 0x03100000 }, { 0, 0x03100010 }));
        CHECK_EQ(run(io, fs, "/USB0/two.CFW", &why), 0);
        CHECK_EQ(io.jump_to, 0x03100010u);
        CHECK(memcmp(&io.hi[0x100000], p[1].data(), p[1].size()) == 0);
        CHECK_EQ(io.word(0x03F00000), 0x54585254u);
        CHECK_EQ(io.word(0x03F00004), (2026u << 16) | (10u << 8) | 26u);
        CHECK_EQ(io.word(0x03F00008), (11u << 16) | (30u << 8) | 5u);
        CHECK_EQ(io.word(0x03F0000C), 0x54585254u ^ io.word(0x03F00004) ^ io.word(0x03F00008) ^ 0x5A17C0DEu);
    }
    // 3. every refusal: no jump, the reason named, and nothing staged before the checks passed
    struct Bad { const char *what; std::vector<uint8_t> file; const char *path; bool mono; const char *needle; };
    std::vector<std::vector<uint8_t>> one = { blob(1000, 3) };
    std::vector<Bad> bad;
    bad.push_back({ "fingerprint", image("U64MK2", "", &one, { 0x03000000 }, { 0x03000000 }, 0x12345678), "/usb0/a.cfw", true, "Invalid Update Image" });
    bad.push_back({ "family", image("U64MK1", "", &one, { 0x03000000 }, { 0x03000000 }), "/usb0/a.cfw", true, "Wrong Product Family" });
    bad.push_back({ "product", image("", "Ultimate II+", &one, { 0x03000000 }, { 0x03000000 }), "/usb0/a.cfw", true, "Wrong Product" });
    bad.push_back({ "below the window", image("U64MK2", "", &one, { 0x00030000 }, { 0x00030000 }), "/usb0/a.cfw", true, "outside" });
    bad.push_back({ "the REU window", image("U64MK2", "", &one, { 0x01000000 }, { 0x01000000 }), "/usb0/a.cfw", true, "outside" });
    bad.push_back({ "over the top", image("U64MK2", "", &one, { 0x03BFFF00 }, { 0x03BFFF00 }), "/usb0/a.cfw", true, "outside" });
    bad.push_back({ "start outside", image("U64MK2", "", &one, { 0x03000000 }, { 0x03000400 }), "/usb0/a.cfw", true, "not inside" });
    bad.push_back({ "no start", image("U64MK2", "", &one, { 0x03000000 }, { 0 }), "/usb0/a.cfw", true, "no start" });
    bad.push_back({ "wrong extension", image("U64MK2", "", &one, { 0x03000000 }, { 0x03000000 }), "/usb0/a.ue2", true, "not a .cfw" });
    bad.push_back({ "the MultiCore host", image("U64MK2", "", &one, { 0x03000000 }, { 0x03000000 }), "/usb0/a.cfw", false, "host's menu" });
    {
        std::vector<uint8_t> cut = image("U64MK2", "", &one, { 0x03000000 }, { 0x03000000 });
        cut.resize(cut.size() - 10);
        bad.push_back({ "cut off", cut, "/usb0/a.cfw", true, "cut off" });
        std::vector<uint8_t> hdr = image("U64MK2", "", &one, { 0x03000000 }, { 0x03000000 });
        hdr.resize(36 + 7);
        bad.push_back({ "header cut", hdr, "/usb0/a.cfw", true, "Invalid" });
        std::vector<std::vector<uint8_t>> many(9, std::vector<uint8_t>(4, 1));
        std::vector<uint32_t> ld, st;
        for (int i = 0; i < 9; i++) { ld.push_back(0x03000000 + 16 * i); st.push_back(i == 8 ? 0x03000080 : 0); }
        bad.push_back({ "nine records", image("U64MK2", "", &many, ld, st), "/usb0/a.cfw", true, "more than" });
    }
    for (auto &b : bad) {
        UpdIo io; FakeFs fs; std::string why;
        fs.put(b.path, b.file);
        int r = run(io, fs, b.path, &why, b.mono);
        if (r != 1 || io.jumps != 0 || why.find(b.needle) == std::string::npos) printf("  case \"%s\": r=%d jumps=%d why=\"%s\"\n", b.what, r, io.jumps, why.c_str());
        CHECK_EQ(r, 1);
        CHECK_EQ(io.jumps, 0);
        CHECK(why.find(b.needle) != std::string::npos);
        CHECK_EQ(io.hi[0], 0xEE);                                  // the first pass wrote nothing
        CHECK(!io.bad_write);
    }
    // 4. no file; a medium that goes away in the middle; a stage that reads back different - none of them gets the jump
    {
        UpdIo io; FakeFs fs; std::string why;
        CHECK_EQ(run(io, fs, "/usb0/none.cfw", &why), 1);
        CHECK(why.find("Cannot open") != std::string::npos);
        CHECK_EQ(io.jumps, 0);
    }
    {
        UpdIo io; FakeFs fs; std::string why;
        std::vector<std::vector<uint8_t>> p = { blob(300000, 9) };
        fs.put("/usb0/up.cfw", image("U64MK2", "", &p, { 0x03000000 }, { 0x03000000 }));
        fs.read_budget = 8;                                        // the header reads and a few chunks; then the medium goes
        CHECK_EQ(run(io, fs, "/usb0/up.cfw", &why), 1);
        CHECK(why.find("Cannot read") != std::string::npos);
        CHECK_EQ(io.jumps, 0);
    }
    {
        UpdIo io; FakeFs fs; std::string why;
        io.corrupt = true;
        std::vector<std::vector<uint8_t>> p = { blob(70000, 4) };
        fs.put("/usb0/up.cfw", image("U64MK2", "", &p, { 0x03000000 }, { 0x03000000 }));
        CHECK_EQ(run(io, fs, "/usb0/up.cfw", &why), 1);
        CHECK(why.find("did not stay") != std::string::npos);
        CHECK_EQ(io.jumps, 0);
    }
    DONE("test_update");
}
