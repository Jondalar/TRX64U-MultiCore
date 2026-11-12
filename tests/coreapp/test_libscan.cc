// T104.1 D12, Q10: the libraries of a folder are loaded at the start of a core application - in dependency order, a cycle refused, a
// missing folder no error, one log line per file, a library whose capability the core lacks refused with a sentence that names the library,
// the capability and the core. The file system is a map of byte vectors, the heap a model, the core descriptors are the real ones (the
// capability list goes through desc_has_capability on atari800_desc / nes_desc, the table through desc_check). What is not here: that
// RISC-V code runs; the host's twin of lib_register is a function of this file (the same split tests/loader/ makes).
#include <string>
#include <vector>
#include <functional>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "desc_check.h"
#include "libscan.h"
#include "board.h"

const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &c64_desc, 0 };

// ---------------------------------------------------------------------------------------------------------------- files

struct Img {
    uint8_t kind, residence;
    std::string name, cap;
    uint32_t version;
    std::vector<std::pair<std::string, uint32_t> > needs;
    uint32_t reg, main;
    Img(const char *n) : kind(U2A3_KIND_LIB), residence(U2A3_RES_RESIDENT), name(n), version(U2A3_VERSION(1, 0)), reg(0), main(U2A3_NONE) { }
    Img &need(const char *n, uint32_t v = U2A3_VERSION(1, 0)) { needs.push_back(std::make_pair(std::string(n), v)); return *this; }
    Img &capability(const char *c) { cap = c; return *this; }
    Img &ver(uint32_t major, uint32_t minor) { version = U2A3_VERSION(major, minor); return *this; }
    Img &app() { kind = U2A3_KIND_APP; residence = U2A3_RES_TRANSIENT; reg = U2A3_NONE; main = 0; return *this; }
};
typedef std::vector<uint8_t> Bytes;
static void put32(Bytes &v, uint32_t x) { for (int i = 0; i < 4; i++) { v.push_back((uint8_t)(x >> (8 * i))); } }
static void put16(Bytes &v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void putname(Bytes &v, const std::string &s) { for (int i = 0; i < 16; i++) { v.push_back(i < (int)s.size() ? s[i] : 0); } }

static Bytes build(const Img &m)
{
    uint32_t needs_off = U2A3_HEADER_SIZE, pay_off = needs_off + (uint32_t)m.needs.size() * U2A3_NEED_SIZE, psize = 64;
    Bytes v;
    put32(v, U2A3_MAGIC); put16(v, U2A3_HEADER_SIZE); put16(v, 3); put16(v, 3); put16(v, 3);
    v.push_back(m.kind); v.push_back(m.residence); v.push_back(U2A3_ARCH_RISCV32); v.push_back((uint8_t)m.needs.size());
    put32(v, 0); putname(v, m.name); put32(v, m.version); putname(v, m.cap);
    put32(v, pay_off); put32(v, psize); put32(v, 16); put32(v, 16); put32(v, m.reg); put32(v, m.main);
    put32(v, pay_off + psize); put32(v, 0); put32(v, needs_off); put32(v, 0); put32(v, 0); put32(v, 0);
    for (size_t i = 0; i < m.needs.size(); i++) { putname(v, m.needs[i].first); put32(v, m.needs[i].second); }
    v.resize(v.size() + psize, 0);
    return v;
}

// ---------------------------------------------------------------------------------------------------------------- the machine

static void test_log_sink(const char *);
static std::vector<std::string> g_lib_logs;
static void test_log_sink(const char *t) { g_lib_logs.push_back(t); }

struct Host : DescLibHost {
    std::vector<void *> live;
    int allocs, releases;
    std::vector<std::string> registered;
    std::vector<std::string> declares;       // libraries whose record carries two nodes and a poll
    explicit Host(const CoreDesc *d) : DescLibHost(d), allocs(0), releases(0) { }
    ~Host() { for (size_t i = 0; i < live.size(); i++) { free(live[i]); } }
    void *alloc(uint32_t n) override { allocs++; void *p = malloc(n); live.push_back(p); return p; }
    void release(void *p) override
    {
        releases++;
        for (size_t i = 0; i < live.size(); i++) { if (live[i] == p) { live.erase(live.begin() + i); free(p); return; } }
        CHECK(!"release of a block that is not allocated");
    }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    int call_register(const Entry &e, const LibApi *api, LibRecord *rec) override
    {
        registered.push_back(e.name);
        for (size_t i = 0; i < declares.size(); i++) {
            if (declares[i] == e.name) {
                static const LibNodeDecl nodes[2] = { };
                rec->nnodes = 2; rec->nodes = nodes;
                rec->poll.op = "x.poll";
            }
        }
        if (api->log) { api->log((std::string("hello from ") + e.name).c_str()); }
        return 0;
    }
};

struct Log : LibLog {
    std::vector<std::string> lines;
    void line(const char *t) override { lines.push_back(t); }
    int find(const std::string &part, int from = 0) const
    {
        for (size_t i = from; i < lines.size(); i++) { if (lines[i].find(part) != std::string::npos) { return (int)i; } }
        return -1;
    }
    int count(const std::string &part) const
    {
        int n = 0;
        for (size_t i = 0; i < lines.size(); i++) { n += lines[i].find(part) != std::string::npos; }
        return n;
    }
};

struct World {
    FakeFs fs;
    Host host;
    OpRegistry reg;
    LibLoader ld;
    Log log;
    LibScanResult r;
    World(const CoreDesc *d = &atari800_desc) : host(d), ld(&host, &reg) { }
    void add(const char *path, const Img &i) { fs.put(path, build(i)); }
    void run() { lib_load_folder(&ld, &fs, lib_roots, LIB_FOLDER, &log, &r); }
    std::string dump() const { std::string s; for (size_t i = 0; i < log.lines.size(); i++) { s += log.lines[i] + "\n"; } return s; }
};

#define L1 "/usb1/trx/lib/"
static int index_of(const std::vector<std::string> &v, const char *s)
{
    for (size_t i = 0; i < v.size(); i++) { if (v[i] == s) { return (int)i; } }
    return -1;
}

int main()
{
    // the real descriptors are well formed and name their capabilities (desc_check is the one of the product)
    for (const CoreDesc *const *p = core_table; *p; p++) { const char *why = desc_check(*p); if (why) printf("%s: %s\n", (*p)->cor_name, why); CHECK(!why); }
    CHECK(!desc_check_table(core_table));

    // ---- a missing folder is not an error
    {
        World w;
        w.fs.put_text("/usb1/roms/atari800/readme.txt", "x");        // a stick with other things on it
        w.run();
        CHECK_EQ(w.r.roots_found, 0); CHECK_EQ(w.r.loaded, 0); CHECK_EQ(w.r.refused, 0); CHECK_EQ(w.ld.count(), 0);
        CHECK_EQ(w.host.allocs, 0);
        CHECK(w.log.find("no library folder") >= 0);
        CHECK(w.log.find("refused") < 0);
        CHECK_STR(w.r.summary, "none");
        World g;                                                       // the stick is gone altogether
        g.fs.gone = true;
        g.run();
        CHECK_EQ(g.r.loaded + g.r.refused, 0);
        CHECK(g.log.find("no library folder") >= 0);
    }
    // ---- a folder with no .u2a in it; other files and folders are left alone, the extension's case does not matter
    {
        World w;
        w.fs.put_text(L1 "readme.txt", "hello");
        w.fs.put_text(L1 "sub/inner.u2a", "a folder's content is not looked at");
        w.run();
        CHECK_EQ(w.r.roots_found, 1); CHECK_EQ(w.r.files, 0); CHECK(w.log.find("holds no .u2a") >= 0); CHECK_STR(w.r.summary, "none");
        World w2;
        w2.add(L1 "Upper.U2A", Img("upper"));
        w2.run();
        CHECK_EQ(w2.r.loaded, 1); CHECK(w2.ld.info(0) && !strcmp(w2.ld.info(0)->name, "upper"));
    }
    // ---- dependency order: a needs b, b needs c; their file names sort the other way round
    {
        World w;
        w.add(L1 "a.u2a", Img("alpha").need("beta"));
        w.add(L1 "b.u2a", Img("beta").need("gamma").need("base"));
        w.add(L1 "c.u2a", Img("gamma"));
        w.add(L1 "d.u2a", Img("delta"));                              // independent: keeps its place among the ready ones
        w.add(L1 "e.u2a", Img("eps"));
        w.run();
        CHECK_EQ(w.r.loaded, 5); CHECK_EQ(w.r.refused, 0);
        std::vector<std::string> want; want.push_back("gamma"); want.push_back("beta"); want.push_back("alpha"); want.push_back("delta"); want.push_back("eps");
        CHECK(w.host.registered == want);
        int g = w.log.find("[lib] gamma v1.0 at 0x"), b = w.log.find("[lib] beta v1.0 at 0x"), a = w.log.find("[lib] alpha v1.0 at 0x");
        CHECK(g >= 0 && b > g && a > b);
        CHECK_EQ(w.log.count(" : ok"), 5);
        // the address in the line is the one the loader put the image at
        char want_line[80];
        bool addr_ok = false;
        for (int h = 0; h < LIB_MAX; h++) {
            const LibInfo *i = w.ld.info(h);
            if (i && !strcmp(i->name, "gamma")) {
                snprintf(want_line, sizeof(want_line), "[lib] gamma v1.0 at 0x%08x : ok", (unsigned)i->image_addr);
                addr_ok = w.log.find(want_line) >= 0;
            }
        }
        CHECK(addr_ok);
        CHECK_STR(w.r.summary, "5 loaded");
    }
    // ---- a version too old is the loader's sentence, with both numbers; the dependent of a refused library follows
    {
        World w;
        w.add(L1 "a.u2a", Img("alpha").need("beta", U2A3_VERSION(2, 0)));
        w.add(L1 "b.u2a", Img("beta").ver(1, 3));
        w.add(L1 "c.u2a", Img("gamma").need("alpha"));
        w.run();
        CHECK_EQ(w.r.loaded, 1); CHECK_EQ(w.r.refused, 2);
        CHECK(w.log.find("[lib] alpha v1.0 : refused: \"alpha\" needs \"beta\" 2.0, 1.3 is loaded.") >= 0);
        CHECK(w.log.find("[lib] gamma v1.0 : refused: \"gamma\" needs \"alpha\" 1.0, which is not loaded.") >= 0);
    }
    // ---- a cycle: refused, named, nothing allocated for it; what lies behind it is refused by the loader; the rest loads
    {
        World w;
        w.add(L1 "a.u2a", Img("pp").need("qq"));
        w.add(L1 "b.u2a", Img("qq").need("pp"));
        w.add(L1 "c.u2a", Img("rr").need("pp"));                      // behind the cycle
        w.add(L1 "d.u2a", Img("ss"));
        w.add(L1 "e.u2a", Img("zz").need("zz"));                      // needs itself
        w.run();
        CHECK_EQ(w.r.loaded, 1); CHECK_EQ(w.r.refused, 4);
        CHECK(w.log.find("[lib] ss v1.0 at 0x") >= 0);
        CHECK(w.log.find("[lib] pp v1.0 : refused: Library \"pp\" is part of a dependency cycle (pp, qq) and is not loaded.") >= 0);
        CHECK(w.log.find("[lib] qq v1.0 : refused: Library \"qq\" is part of a dependency cycle (pp, qq) and is not loaded.") >= 0);
        CHECK(w.log.find("[lib] zz v1.0 : refused: Library \"zz\" is part of a dependency cycle (zz) and is not loaded.") >= 0);
        CHECK(w.log.find("[lib] rr v1.0 : refused: \"rr\" needs \"pp\" 1.0, which is not loaded.") >= 0);
        CHECK_EQ(w.host.allocs, 1);                                   // only ss ever asked for memory
        std::vector<std::string> want(1, "ss");
        CHECK(w.host.registered == want);
        CHECK_EQ(w.ld.count(), 1);
        CHECK_STR(w.r.summary, "1 loaded, 4 refused: pp");
        CHECK_EQ(w.r.nkept, 4);
    }
    // ---- Q10 on the real descriptors: the Atari has "sio", it has no "drive_a"
    {
        World w;
        w.add(L1 "cbm1541.u2a", Img("cbm1541").capability("drive_a"));
        w.add(L1 "sioext.u2a", Img("sioext").capability("sio"));
        w.add(L1 "user.u2a", Img("user").need("cbm1541"));            // its dependency is refused
        w.run();
        CHECK_EQ(w.r.loaded, 1); CHECK_EQ(w.r.refused, 2);
        const char *want = "[lib] cbm1541 v1.0 : refused: Library \"cbm1541\" needs the capability \"drive_a\", which the core \"Atari 800 XL\" does not have.";
        CHECK(w.log.find(want) >= 0);
        CHECK(w.log.find("[lib] sioext v1.0 at 0x") >= 0);
        CHECK(w.log.find("[lib] user v1.0 : refused: \"user\" needs \"cbm1541\" 1.0, which is not loaded.") >= 0);
        // the screen gets the sentence as the loader wrote it, with library, capability and core in it
        CHECK(w.r.nkept == 2 && strstr(w.r.kept[0], "cbm1541") && strstr(w.r.kept[0], "drive_a") && strstr(w.r.kept[0], "Atari 800 XL"));
        CHECK_EQ(w.host.allocs, 1);                                   // nothing was allocated for the refused one
        CHECK_STR(w.r.summary, "1 loaded, 2 refused: cbm1541");
        // the same files on the NES: it has neither
        World n(&nes_desc);
        n.add(L1 "sioext.u2a", Img("sioext").capability("sio"));
        n.add(L1 "carts.u2a", Img("carts").capability("cartridge"));
        n.run();
        CHECK_EQ(n.r.loaded, 1);
        CHECK(n.log.find("needs the capability \"sio\", which the core \"Nintendo Entertainment System\" does not have.") >= 0);
        CHECK(n.log.find("[lib] carts v1.0 at 0x") >= 0);
    }
    // ---- T104.2 D3 on the C64 descriptor: what the board strikes, a library cannot have. The monolith has the T37 sockets, the MultiCore
    // trx64 core has not; neither has a drive B. The core's effective list is what the loader is given (run.cc merges it).
    {
        struct P : BoardProbe {
            uint8_t icap; P(uint8_t i) : icap(i) { }
            uint8_t icap_id() { return icap; }
            uint32_t fpga_caps() { return BOARD_FPGA_DRIVE_1541_1 | BOARD_FPGA_COMMAND_INTF; }
            uint8_t trx_caps() { return BOARD_TRX_CAPS_GATE | BOARD_TRX_CAPS_STATEBUS | BOARD_TRX_CAPS_ENGINE | BOARD_TRX_CAPS_CART; }
            uint8_t iec_version() { return BOARD_IEC_VERSION; }
            bool handover_valid() { return true; }
        };
        for (int mc = 0; mc < 2; mc++) {
            P p(mc ? 0x7A : 0);
            BoardProfile b = board_detect(&p);
            const char *eff[16];
            CoreDesc d = c64_desc;
            d.ncaps = board_effective_caps(b, c64_desc.caps, c64_desc.ncaps, eff, 16, 0, 0);
            d.caps = eff;
            World w(&d);
            w.add(L1 "cbm1541.u2a", Img("cbm1541").capability("drive_a"));
            w.add(L1 "socks.u2a", Img("socks").capability("sid_sockets"));
            w.add(L1 "driveb.u2a", Img("driveb").capability("drive_b"));
            w.run();
            CHECK(w.log.find("[lib] cbm1541 v1.0 at 0x") >= 0);                 // a drive A is on both
            CHECK(w.log.find("needs the capability \"drive_b\", which the core \"TRX64U\" does not have.") >= 0);
            if (mc) {
                CHECK_EQ(w.r.loaded, 1); CHECK_EQ(w.r.refused, 2);
                CHECK(w.log.find("Library \"socks\" needs the capability \"sid_sockets\", which the core \"TRX64U\" does not have.") >= 0);
            } else {
                CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 1);
                CHECK(w.log.find("[lib] socks v1.0 at 0x") >= 0);
            }
        }
    }
    // ---- an application is not loaded from here; a file that is not a library is refused by name and the others still load
    {
        World w;
        w.add(L1 "app.u2a", Img("someapp").app());
        w.fs.put_text(L1 "junk.u2a", "this is not a u2a file at all, but it is long enough to hold a header, no it is not, it is shorter than 104 bytes?");
        w.fs.put_size(L1 "zeros.u2a", 300, 0);
        w.add(L1 "z.u2a", Img("zlib"));
        w.run();
        CHECK_EQ(w.r.loaded, 1); CHECK_EQ(w.r.refused, 3);
        CHECK(w.log.find("[lib] someapp v1.0 : refused: \"someapp\" is an application, not a library") >= 0);
        CHECK(w.log.find("[lib] junk.u2a : refused: Not a library or app file") >= 0);
        CHECK(w.log.find("[lib] zeros.u2a : refused: Not a library or app file") >= 0);
        CHECK(w.log.find("[lib] zlib v1.0 at 0x") >= 0);
        CHECK_EQ(w.host.allocs, 1);
        bool named = false;
        for (int i = 0; i < w.r.nkept; i++) { named |= strstr(w.r.kept[i], "junk.u2a: Not a library") != 0; }
        CHECK(named);                                                 // a refusal about a file names the file on the screen
    }
    // ---- two sticks: the same library twice is the loader's duplicate; both folders are looked at
    {
        World w;
        w.add("/usb0/trx/lib/same.u2a", Img("same"));
        w.add("/usb1/trx/lib/same.u2a", Img("same"));
        w.add("/usb1/trx/lib/other.u2a", Img("other"));
        w.run();
        CHECK_EQ(w.r.roots_found, 2); CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 1);
        CHECK(w.log.find("\"same\" is loaded already") >= 0);
    }
    // ---- more files than the scan takes: the rest is named, never silently dropped; the loader's own limit shows as refusals
    {
        World w;
        for (int i = 0; i < 30; i++) {
            char path[64], name[24];
            snprintf(path, sizeof(path), L1 "l%02d.u2a", i);
            snprintf(name, sizeof(name), "lib%02d", i);
            w.add(path, Img(name));
        }
        w.run();
        CHECK_EQ(w.r.files, 30); CHECK_EQ(w.r.ignored, 6);
        CHECK_EQ(w.r.loaded, LIB_MAX); CHECK_EQ(w.r.loaded + w.r.refused, LIB_SCAN_MAX);
        CHECK(w.log.find("6 more file(s)") >= 0);
        CHECK_EQ(w.host.allocs - w.host.releases, LIB_MAX);
    }
    // ---- a record with nodes and a poll: the menu merge and the poll task serve them, so the Home line has nothing to add
    {
        World w;
        w.host.declares.push_back("alpha");
        w.add(L1 "a.u2a", Img("alpha"));
        w.add(L1 "b.u2a", Img("beta"));
        w.run();
        CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.unserved, 0);
        CHECK_STR(w.r.summary, "2 loaded");
        World q;
        q.add(L1 "a.u2a", Img("alpha"));
        q.run();
        CHECK_EQ(q.r.unserved, 0); CHECK_STR(q.r.summary, "1 loaded");
    }
    // ---- what the library logs reaches the sink the board sets
    {
        World w;
        g_lib_logs.clear();
        w.ld.set_log(test_log_sink);
        w.add(L1 "a.u2a", Img("alpha"));
        w.run();
        CHECK_EQ(g_lib_logs.size(), 1);
        if (!g_lib_logs.empty()) { CHECK_STR(g_lib_logs[0].c_str(), "hello from alpha"); }
    }
    // ---- the order itself, on made-up metadata (no files)
    {
        LibMeta m[4];
        memset(m, 0, sizeof(m));
        for (int i = 0; i < 4; i++) { m[i].valid = true; m[i].kind = U2A3_KIND_LIB; }
        strcpy(m[0].name, "a"); strcpy(m[1].name, "b"); strcpy(m[2].name, "c"); strcpy(m[3].name, "d");
        m[0].nneeds = 1; strcpy(m[0].need[0], "b");
        m[1].nneeds = 1; strcpy(m[1].need[0], "c");
        m[2].nneeds = 1; strcpy(m[2].need[0], "base");
        m[3].nneeds = 1; strcpy(m[3].need[0], "nobody");                // a name no file has: the loader's to refuse, not a cycle
        int order[4], cyc[4];
        int n = lib_dep_order(m, 4, order, cyc);
        CHECK_EQ(n, 4);
        CHECK(order[0] == 2 && order[1] == 1 && order[2] == 0 && order[3] == 3);
        for (int i = 0; i < 4; i++) { CHECK_EQ(cyc[i], -1); }
        // two cycles, one group each
        strcpy(m[2].need[0], "a");                                     // a -> b -> c -> a
        strcpy(m[3].need[0], "d");                                     // d -> d
        n = lib_dep_order(m, 4, order, cyc);
        CHECK_EQ(n, 0);
        CHECK(cyc[0] == 0 && cyc[1] == 0 && cyc[2] == 0 && cyc[3] == 3);
    }
    DONE("test_libscan");
}
