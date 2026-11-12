// T91 D10 / S11: the application's snapshot reader against the reference reader of tools/zx_snap.py. run.sh makes the corpus (SNA 48K and 128K,
// Z80 v1/v2/v3, plain and packed, from random states) into a temporary directory with `zx_snap.py corpus`; for every file the Python reader's
// result is next to it (<name>.regs.hex: the register window writes, <name>.ram.hex: the banks the file holds, <name>.meta). Nothing of it is
// committed.
#include <stdlib.h>
#include <vector>
#include <string>
#include <fstream>
#include "check.h"
#include "zx_snap.h"

typedef std::vector<uint8_t> Bytes;

class VecFile : public CoreFile {
public:
    Bytes d;
    bool fail = false;
    uint64_t size() { return d.size(); }
    bool writable() { return false; }
    bool read(uint64_t off, void *buf, uint32_t len)
    {
        if (fail || off > d.size() || len > d.size() - off) return false;
        memcpy(buf, d.data() + off, len);
        return true;
    }
    bool write(uint64_t, const void *, uint32_t) { return false; }
    bool sync() { return true; }
};

static Bytes slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string dir;

static void one(const std::string &name)
{
    std::string base = name.substr(0, name.rfind('.'));
    bool z80 = name.size() > 4 && name.compare(name.size() - 4, 4, ".z80") == 0;
    VecFile f;
    f.d = slurp(dir + "/" + name);
    char msg[200] = "";
    ZxSnap s;
    int rc = zx_snap_parse(&f, z80, &s, msg, sizeof(msg));
    if (rc) printf("  %s: refused: %s\n", name.c_str(), msg);
    CHECK_EQ(rc, 0);
    if (rc) return;
    // the expected register writes
    std::ifstream regs(dir + "/" + base + ".regs.hex");
    unsigned off, val;
    int n = 0, bad = 0;
    while (regs >> std::hex >> off >> val) {
        n++;
        unsigned got = 0xFFFF;
        if (off >= 0x100 && off < 0x11B) got = s.dir[off - 0x100];
        else if (off == 0x11B) got = s.p7ffd;
        else if (off == 0x11C) got = s.border;
        else if (off >= 0x120 && off < 0x130) got = s.ay[off - 0x120];
        else if (off == 0x130) got = s.ay_sel;
        else if (off == 0x0A1) got = s.model == 128 ? 1 : 0;
        if (got != val) { bad++; printf("  %s: register %03x is %02x, the reference says %02x\n", name.c_str(), off, got, val); }
    }
    CHECK_EQ(n, 27 + 1 + 1 + 16 + 1 + 1);
    CHECK_EQ(bad, 0);
    // the banks
    std::ifstream meta(dir + "/" + base + ".meta");
    std::string l1, l2;
    std::getline(meta, l1);
    std::getline(meta, l2);
    std::vector<int> banks;
    for (size_t p = l2.find('=') + 1; p < l2.size();) {
        banks.push_back(atoi(l2.c_str() + p));
        size_t c = l2.find(',', p);
        if (c == std::string::npos) break;
        p = c + 1;
    }
    CHECK_EQ(s.model, atoi(l1.c_str() + 6));
    uint8_t mask = 0;
    for (int b : banks) mask |= 1 << b;
    CHECK_EQ(s.banks, mask);
    std::ifstream ram(dir + "/" + base + ".ram.hex");
    int wrong = 0;
    for (int b : banks) {
        const uint8_t *g = zx_snap_bank(b);
        for (unsigned k = 0; k < ZX_BANK_BYTES; k++) {
            unsigned v = 0;
            ram >> std::hex >> v;
            if (g[k] != v && wrong++ < 3) printf("  %s: bank %d byte %u is %02x, the reference says %02x\n", name.c_str(), b, k, g[k], v);
        }
    }
    CHECK_EQ(wrong, 0);
    for (int b = 0; b < 8; b++) {                               // a bank the file does not hold is zeros
        if (mask & (1 << b)) continue;
        const uint8_t *g = zx_snap_bank(b);
        bool zero = true;
        for (unsigned k = 0; k < ZX_BANK_BYTES; k++) zero = zero && g[k] == 0;
        CHECK(zero);
    }
}

static int parse(const Bytes &d, bool z80, char *msg, ZxSnap *s)
{
    VecFile f;
    f.d = d;
    return zx_snap_parse(&f, z80, s, msg, 200);
}

int main()
{
    const char *e = getenv("ZX_CORPUS");
    if (!e) { printf("FAIL: ZX_CORPUS is not set - run this through tests/coreapp/run.sh\n"); return 1; }
    dir = e;
    std::ifstream idx(dir + "/INDEX");
    std::string name;
    int files = 0;
    while (std::getline(idx, name)) {
        if (name.empty()) continue;
        one(name);
        files++;
    }
    CHECK(files >= 20);

    // refusals: each with its reason, and the module left clean
    char msg[200];
    ZxSnap s;
    Bytes sna48 = slurp(dir + "/sna48.sna");
    CHECK(parse(Bytes(), false, msg, &s) != 0 && strstr(msg, "neither a 48K nor a 128K"));
    CHECK(parse(Bytes(100, 0), false, msg, &s) != 0 && strstr(msg, "neither"));
    Bytes b = sna48;
    b[23] = 0x00; b[24] = 0x20;                                    // SP in the ROM: the pushed PC is not in RAM
    CHECK(parse(b, false, msg, &s) != 0 && strstr(msg, "stack is not in RAM"));
    b = sna48; b[23] = 0xFF; b[24] = 0xFF;                         // SP = FFFF: the PC would wrap
    CHECK(parse(b, false, msg, &s) != 0 && strstr(msg, "stack is not in RAM"));
    CHECK_EQ(s.banks, 0);
    CHECK_EQ(zx_snap_bank(5)[0], 0);                                // a refused file leaves no half-loaded RAM
    Bytes sna128 = slurp(dir + "/sna128_13.sna");
    b = sna128; b[27 + 49152 + 2] = 0x15;                           // the port says bank 5: one more bank is due, the file is short
    CHECK(parse(b, false, msg, &s) != 0 && strstr(msg, "shorter than its paging"));
    Bytes z3 = slurp(dir + "/z80v3p_128_13.z80");
    b = z3; b[34] = 7;                                              // hardware 7 of version 3: the Spectrum +3
    CHECK(parse(b, true, msg, &s) != 0 && strstr(msg, "not a 48K or 128K"));
    b = z3; b[30] = 24;                                             // an extended header of 24 bytes
    CHECK(parse(b, true, msg, &s) != 0 && strstr(msg, "extended header"));
    b = z3; b.resize(b.size() - 3000);                              // cut in the last page
    CHECK(parse(b, true, msg, &s) != 0);
    b = slurp(dir + "/z80v3r_48.z80"); b.resize(b.size() - (3 + 16384));  // a 48K file without its last page
    CHECK(parse(b, true, msg, &s) != 0 && strstr(msg, "no page"));
    Bytes z1 = slurp(dir + "/z80v1p_48.z80");
    b = z1; b[b.size() - 1] = 1;                                    // the end marker is gone
    CHECK(parse(b, true, msg, &s) != 0 && strstr(msg, "end marker"));
    Bytes z1r = slurp(dir + "/z80v1r_48.z80");
    b = z1r; b.resize(b.size() - 100);
    CHECK(parse(b, true, msg, &s) != 0 && strstr(msg, "shorter than 48 KB"));
    CHECK(parse(Bytes(10, 1), true, msg, &s) != 0 && strstr(msg, "too short"));
    Bytes z2p = slurp(dir + "/z80v2p_48.z80");
    b = z2p;                                                         // damage a packed page: a run of ED ED 00 00 (zero-length run) never fills it
    for (size_t k = 55; k + 6 < b.size() && k < 55 + 12; k++) b[k] = 0xED;
    CHECK(parse(b, true, msg, &s) != 0);
    VecFile gone;
    gone.d = sna128; gone.fail = true;
    CHECK(zx_snap_parse(&gone, false, &s, msg, sizeof(msg)) != 0 && strstr(msg, "cannot read"));

    bool z;
    CHECK(zx_snap_kind("/usb0/a.Z80", &z) && z);
    CHECK(zx_snap_kind("/usb0/a.sna", &z) && !z);
    CHECK(!zx_snap_kind("/usb0/a.tap", &z));
    DONE("test_zx_snap");
}
