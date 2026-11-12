// T104.1 S6, on the Mac: the loader of coreapp/src/libload.cc against a model of the heap and of the address space.
//
// A RISC-V instruction cannot run here, so the model does not call the image: LibHost::call_register looks the library up by name
// and runs a host-compiled twin of its lib_register. What is real: the file reading, every check before the first instruction, the
// placement in the arena, the relocation of real bytes (the testlib.u2a that tools/u2a_pack.py made from a real riscv64 link is
// compared with the same objects linked at the address it lands on), the registration and its rollback. What is not: that the
// RISC-V code in an image runs, which needs the board or a RISC-V simulator.
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <functional>
#include "check.h"
#include "libload.h"
#include "libmenu.h"
#include "ctx.h"
#include "fakes.h"

#ifndef FIXDIR
#error FIXDIR is the directory of testlib.u2a and testlib.at1000.raw (tests/loader/run.sh makes them)
#endif

extern "C" int lib_register(const LibApi *api, LibRecord *out);      // app/lib/testlib/testlib.cc, compiled for the host: the twin

// ---------------------------------------------------------------------------------------------------------- the model of the machine

#define ARENA_SIZE 0x10000u
#define ARENA_BASE 0x1000u            // the address the CPU sees at byte 0 of the arena
#define FILL 0xA5

struct Model : LibHost {
    uint8_t *arena;
    struct Blk { uint32_t off, len; bool used; };
    std::vector<Blk> blks;
    int allocs, releases, syncs, register_calls;
    bool fail_alloc;
    std::vector<std::string> caps;
    typedef std::function<int(const Entry &, const LibApi *, LibRecord *)> Twin;
    std::map<std::string, Twin> twins;
    typedef std::function<int(const Entry &, const LibApi *, AppCtx *)> MainTwin;
    std::map<std::string, MainTwin> mains;
    uint32_t clock = 1000;
    std::vector<Entry> entries;

    Model() : allocs(0), releases(0), syncs(0), register_calls(0), fail_alloc(false)
    {
        arena = (uint8_t *)aligned_alloc(4096, ARENA_SIZE);
        reset_arena();
    }
    ~Model() { free(arena); }
    void reset_arena() { memset(arena, FILL, ARENA_SIZE); blks.clear(); }

    void *alloc(uint32_t n) override
    {
        allocs++;
        if (fail_alloc) { return 0; }
        n = (n + 15) & ~15u;
        for (size_t i = 0; i < blks.size(); i++) {
            if (!blks[i].used && blks[i].len >= n) {
                if (blks[i].len > n) {
                    Blk rest = { blks[i].off + n, blks[i].len - n, false };
                    blks[i].len = n;
                    blks.insert(blks.begin() + i + 1, rest);
                }
                blks[i].used = true;
                return arena + blks[i].off;
            }
        }
        uint32_t top = blks.empty() ? 0 : blks.back().off + blks.back().len;
        if (top + n > ARENA_SIZE) { return 0; }
        Blk b = { top, n, true };
        blks.push_back(b);
        return arena + top;
    }
    void release(void *p) override
    {
        releases++;
        uint32_t off = (uint32_t)((uint8_t *)p - arena);
        for (size_t i = 0; i < blks.size(); i++) {
            if (blks[i].off == off && blks[i].used) {
                blks[i].used = false;
                memset(arena + off, FILL, blks[i].len);
                // coalesce, and drop a free block at the top so the shape is the one before the allocation
                for (size_t k = 0; k + 1 < blks.size();) {
                    if (!blks[k].used && !blks[k + 1].used) { blks[k].len += blks[k + 1].len; blks.erase(blks.begin() + k + 1); } else { k++; }
                }
                if (!blks.empty() && !blks.back().used) { blks.pop_back(); }
                return;
            }
        }
        CHECK(!"release of a block that is not allocated");
    }
    uint32_t addr_of(const void *p) override { return ARENA_BASE + (uint32_t)((const uint8_t *)p - arena); }
    void sync_code(void *, uint32_t) override { syncs++; }
    bool has_capability(const char *c) override
    {
        for (size_t i = 0; i < caps.size(); i++) { if (caps[i] == c) { return true; } }
        return false;
    }
    const char *core_name() override { return "Test Core"; }
    int call_register(const Entry &e, const LibApi *api, LibRecord *rec) override
    {
        register_calls++;
        entries.push_back(e);
        std::map<std::string, Twin>::iterator t = twins.find(e.name);
        CHECK(t != twins.end());
        return t == twins.end() ? 99 : t->second(e, api, rec);
    }

    int call_main(const Entry &e, const LibApi *api, AppCtx *ctx) override
    {
        std::map<std::string, MainTwin>::iterator t = mains.find(e.name);
        CHECK(t != mains.end());
        return t == mains.end() ? 99 : t->second(e, api, ctx);
    }
    uint32_t now_ms() override { return clock; }
    void delay_ms(uint32_t ms) override { clock += ms; }

    uint8_t *ptr(uint32_t addr) { return arena + (addr - ARENA_BASE); }
    // the shape of the heap: what must be the same after a refused load
    std::string shape() const
    {
        std::string s;
        for (size_t i = 0; i < blks.size(); i++) {
            char b[40];
            snprintf(b, sizeof(b), "%u+%u%c ", blks[i].off, blks[i].len, blks[i].used ? 'u' : 'f');
            s += b;
        }
        return s;
    }
    uint32_t used_bytes() const
    {
        uint32_t n = 0;
        for (size_t i = 0; i < blks.size(); i++) { n += blks[i].used ? blks[i].len : 0; }
        return n;
    }
};

struct MemSource : LibSource {
    std::vector<uint8_t> d;
    explicit MemSource(const std::vector<uint8_t> &v) : d(v) { }
    uint32_t size() override { return (uint32_t)d.size(); }
    bool read(uint32_t off, void *dst, uint32_t n) override
    {
        if (off > d.size() || n > d.size() - off) { return false; }
        memcpy(dst, d.data() + off, n);
        return true;
    }
};

static std::vector<uint8_t> read_file(const char *name)
{
    std::string p = std::string(FIXDIR) + "/" + name;
    FILE *f = fopen(p.c_str(), "rb");
    std::vector<uint8_t> v;
    if (!f) { printf("FAIL cannot open %s\n", p.c_str()); g_fails++; return v; }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) { v.insert(v.end(), buf, buf + n); }
    fclose(f);
    return v;
}

// ------------------------------------------------------------------------------------------------ a writer of images, independent of the packer

struct Img {
    uint8_t kind, residence;
    std::string name, cap;
    uint32_t version;
    std::vector<std::pair<std::string, uint32_t> > needs;
    std::vector<uint8_t> payload;
    uint32_t bss, align, reg, main;
    std::vector<uint32_t> relocs;
    uint16_t abi_min, abi_max, fmt;
    uint32_t flags = 0, reserved = 0;
    Img(const char *n, const char *marker) : kind(U2A3_KIND_LIB), residence(U2A3_RES_RESIDENT), name(n), version(U2A3_VERSION(1, 0)),
          payload(64, 0), bss(32), align(16), reg(0), main(U2A3_NONE), abi_min(3), abi_max(3), fmt(3)
    {
        // offset 0: code that is never run; 4: a marker; 16: a word that holds the offset of the marker (relocated: its address);
        // 20: a word that holds the offset of that word
        strcpy((char *)&payload[4], marker);
        uint32_t w16 = 4, w20 = 16;
        memcpy(&payload[16], &w16, 4);
        memcpy(&payload[20], &w20, 4);
        relocs.push_back(16);
        relocs.push_back(20);
    }
};

static void put32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; i++) { v.push_back((uint8_t)(x >> (8 * i))); } }
static void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void putname(std::vector<uint8_t> &v, const std::string &s) { for (int i = 0; i < 16; i++) { v.push_back(i < (int)s.size() ? s[i] : 0); } }

static std::vector<uint8_t> build(const Img &m)
{
    uint32_t needs_off = U2A3_HEADER_SIZE;
    uint32_t pay_off = needs_off + (uint32_t)m.needs.size() * U2A3_NEED_SIZE;
    uint32_t rel_off = pay_off + (uint32_t)m.payload.size();
    std::vector<uint8_t> v;
    put32(v, U2A3_MAGIC); put16(v, U2A3_HEADER_SIZE); put16(v, m.fmt); put16(v, m.abi_min); put16(v, m.abi_max);
    v.push_back(m.kind); v.push_back(m.residence); v.push_back(U2A3_ARCH_RISCV32); v.push_back((uint8_t)m.needs.size());
    put32(v, m.flags); putname(v, m.name); put32(v, m.version); putname(v, m.cap);
    put32(v, pay_off); put32(v, (uint32_t)m.payload.size()); put32(v, m.bss); put32(v, m.align); put32(v, m.reg); put32(v, m.main);
    put32(v, rel_off); put32(v, (uint32_t)m.relocs.size()); put32(v, needs_off); put32(v, 0); put32(v, 0); put32(v, m.reserved);
    CHECK_EQ(v.size(), U2A3_HEADER_SIZE);
    for (size_t i = 0; i < m.needs.size(); i++) { putname(v, m.needs[i].first); put32(v, m.needs[i].second); }
    v.insert(v.end(), m.payload.begin(), m.payload.end());
    for (size_t i = 0; i < m.relocs.size(); i++) { put32(v, m.relocs[i]); }
    return v;
}

// ---------------------------------------------------------------------------------------------------------------- the twins

static Model *g_m;
static uint32_t g_base[4];

static uint32_t rd32(Model &m, uint32_t addr) { uint32_t w; memcpy(&w, m.ptr(addr), 4); return w; }

// "name@address:marker": the marker is reached through the relocated word at offset 16 of the image, so the reply proves where the
// image is and that the relocation made a working pointer out of it
template <int N>
static int h_where(CoreCtx *, const OpArgs &, OpOut &o)
{
    uint32_t base = g_base[N];
    uint32_t p = rd32(*g_m, base + 16);
    snprintf(o.msg, o.msglen, "%s@%x:%s", N == 0 ? "alpha" : N == 1 ? "beta" : "gamma", base, (const char *)g_m->ptr(p));
    return OPE_OK;
}

template <int N>
static int twin_where(const LibHost::Entry &e, const LibApi *, LibRecord *out)
{
    static LibOpDecl ops[1];
    static const char *names[] = { "alpha.where", "beta.where", "gamma.where" };
    g_base[N] = e.image_addr;
    ops[0].name = names[N]; ops[0].args = 0; ops[0].nargs = 0; ops[0].fn = h_where<N>;
    static LibOpDecl keep[3];
    keep[N] = ops[0];
    out->ops = &keep[N];
    out->nops = 1;
    return 0;
}

static int h_nop(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }

// ---------------------------------------------------------------------------------------------------------------------- helpers

struct World {
    Model m;
    OpRegistry reg;
    LibLoader *ld;
    World(uint16_t base_minor = 0)
    {
        g_m = &m;
        ld = new LibLoader(&m, &reg, base_minor);
        m.twins["testlib"] = [](const LibHost::Entry &, const LibApi *api, LibRecord *rec) { return lib_register(api, rec); };
        m.twins["alpha"] = twin_where<0>;
        m.twins["beta"] = twin_where<1>;
        m.twins["gamma"] = twin_where<2>;
    }
    ~World() { delete ld; }
    int load(const std::vector<uint8_t> &f, int *h, char *err)
    {
        MemSource s(f);
        return ld->load(&s, h, err, LIB_ERR_MAX);
    }
};

// A load that must be refused: the code, the words in the sentence, and nothing left behind.
static void refused(const char *what, World &w, const std::vector<uint8_t> &f, int rc, const char *n1, const char *n2 = 0, const char *n3 = 0)
{
    std::string heap = w.m.shape();
    int ops = w.reg.nops(), st = w.reg.nstates(), ev = w.reg.nevents(), count = w.ld->count();
    uint32_t resident = w.ld->resident_bytes();
    char err[LIB_ERR_MAX] = "";
    int h = 0;
    int got = w.load(f, &h, err);
    bool ok = got == rc && h == -1;
    ok = ok && (!n1 || strstr(err, n1)) && (!n2 || strstr(err, n2)) && (!n3 || strstr(err, n3));
    ok = ok && heap == w.m.shape() && ops == w.reg.nops() && st == w.reg.nstates() && ev == w.reg.nevents() && count == w.ld->count();
    ok = ok && resident == w.ld->resident_bytes();
    g_checks++;
    if (!ok) {
        printf("FAIL %s: rc %d (want %d), handle %d, text \"%s\"; heap %s -> %s; ops %d -> %d\n", what, got, rc, h, err, heap.c_str(),
               w.m.shape().c_str(), ops, w.reg.nops());
        g_fails++;
    }
}

static std::string call(World &w, const char *op, const char *arg = 0)
{
    OpResult r;
    OpIn in = in_str("text", arg ? arg : "");
    int rc = w.reg.call(0, op, arg ? &in : 0, arg ? 1 : 0, &r);
    return rc == OPE_OK ? std::string(r.msg) : "ERR " + std::string(r.msg);
}

// the bytes a correct loader leaves: the payload, every listed word plus the base, zeros for bss
static std::vector<uint8_t> expected_image(const Img &m, uint32_t base)
{
    std::vector<uint8_t> e = m.payload;
    for (size_t i = 0; i < m.relocs.size(); i++) {
        uint32_t w;
        memcpy(&w, &e[m.relocs[i]], 4);
        w += base;
        memcpy(&e[m.relocs[i]], &w, 4);
    }
    e.resize(m.payload.size() + m.bss, 0);
    return e;
}

static bool image_is(World &w, const LibInfo *i, const std::vector<uint8_t> &want)
{
    return want.size() == i->image_size && memcmp(w.m.ptr(i->image_addr), want.data(), want.size()) == 0;
}

// ------------------------------------------------------------------------------------------------------------------------ cases

// S6: two libraries and an application resident at once
static void coexist()
{
    World w;
    w.m.caps.push_back("drive_a");
    std::vector<uint8_t> tl = read_file("testlib.u2a");
    std::vector<uint8_t> raw = read_file("testlib.at1000.raw");
    CHECK(tl.size() > U2A3_HEADER_SIZE && raw.size() > 0);
    char err[LIB_ERR_MAX];
    int h_tl = -1, h_a = -1, h_b = -1;

    // the real riscv32 image, first in the arena: it lands at ARENA_BASE, the address the second link was made for
    CHECK_EQ(w.load(tl, &h_tl, err), LIB_OK);
    const LibInfo *itl = w.ld->info(h_tl);
    CHECK(itl != 0);
    if (!itl) { return; }
    CHECK_EQ(itl->image_addr, 0x1000);
    CHECK_STR(itl->name, "testlib");
    CHECK_EQ(w.m.syncs, 1);
    CHECK(raw.size() == itl->image_size && memcmp(w.m.ptr(itl->image_addr), raw.data(), raw.size()) == 0);   // the loader's relocation == the linker's
    CHECK_EQ(w.reg.find_op("test.echo") != 0, true);
    CHECK_STR(w.reg.find_op("test.echo")->owner, "testlib");
    CHECK(w.reg.find_state("test.version") != 0);
    CHECK_EQ((int)w.m.entries[0].entry_addr, 0x1000);                       // lib_register is at offset 0 of the image

    // alpha: a library with an alignment of 64, a dependency on the first one and on the base
    Img a("alpha", "ALPHA-MARK");
    a.align = 64;
    a.needs.push_back(std::make_pair(std::string("testlib"), U2A3_VERSION(1, 0)));
    a.needs.push_back(std::make_pair(std::string("base"), U2A3_VERSION(3, 0)));
    a.cap = "drive_a";
    CHECK_EQ(w.load(build(a), &h_a, err), LIB_OK);
    // beta: a resident application that registers (trxmon's shape), with app_main
    Img b("beta", "BETA-MARK");
    b.kind = U2A3_KIND_APP;
    b.main = 8;
    b.align = 256;
    CHECK_EQ(w.load(build(b), &h_b, err), LIB_OK);

    const LibInfo *ia = w.ld->info(h_a), *ib = w.ld->info(h_b);
    CHECK(ia && ib);
    if (!ia || !ib) { return; }
    const LibInfo *all[3] = { itl, ia, ib };
    for (int i = 0; i < 3; i++) {
        for (int k = i + 1; k < 3; k++) {
            bool apart = all[i]->image_addr + all[i]->image_size <= all[k]->image_addr || all[k]->image_addr + all[k]->image_size <= all[i]->image_addr;
            CHECK(apart);
        }
    }
    CHECK_EQ(ia->image_addr % 64, 0);
    CHECK_EQ(ib->image_addr % 256, 0);
    CHECK(ia->image_addr != itl->image_addr && ib->image_addr != ia->image_addr);
    CHECK_EQ(ib->main_addr, ib->image_addr + 8);
    CHECK_EQ(ia->main_addr, 0);
    CHECK(image_is(w, ia, expected_image(a, ia->image_addr)));
    CHECK(image_is(w, ib, expected_image(b, ib->image_addr)));
    CHECK_EQ(w.ld->resident_bytes(), itl->image_size + ia->image_size + ib->image_size);

    // each is called through its registered operation and answers from where it was put
    char want[80];
    std::string r1 = call(w, "test.echo", "hello");
    CHECK_STR(r1.c_str(), "hello");
    snprintf(want, sizeof(want), "alpha@%x:ALPHA-MARK", ia->image_addr);
    std::string r2 = call(w, "alpha.where");
    CHECK_STR(r2.c_str(), want);
    snprintf(want, sizeof(want), "beta@%x:BETA-MARK", ib->image_addr);
    std::string r3 = call(w, "beta.where");
    CHECK_STR(r3.c_str(), want);
    CHECK_STR(w.reg.find_op("beta.where")->owner, "beta");

    // none overwrote another: the three images are what they were, and nothing else in the arena was written
    CHECK(raw.size() == itl->image_size && memcmp(w.m.ptr(itl->image_addr), raw.data(), raw.size()) == 0);
    CHECK(image_is(w, ia, expected_image(a, ia->image_addr)));
    CHECK(image_is(w, ib, expected_image(b, ib->image_addr)));
    int stray = 0;
    for (uint32_t i = 0; i < ARENA_SIZE; i++) {
        uint32_t addr = ARENA_BASE + i;
        bool inside = false;
        for (int k = 0; k < 3; k++) { inside |= addr >= all[k]->image_addr && addr < all[k]->image_addr + all[k]->image_size; }
        stray += !inside && w.m.arena[i] != FILL;
    }
    CHECK_EQ(stray, 0);

    // a transient app: placed, main known, nothing registered, freed when it returns; a resident image is not
    Img t("tapp", "T");
    t.kind = U2A3_KIND_APP;
    t.residence = U2A3_RES_TRANSIENT;
    t.reg = U2A3_NONE;
    t.main = 4;
    std::string heap = w.m.shape();
    uint32_t resident = w.ld->resident_bytes();
    int ops = w.reg.nops(), h_t = -1, calls = w.m.register_calls;
    CHECK_EQ(w.load(build(t), &h_t, err), LIB_OK);
    CHECK_EQ(w.m.register_calls, calls);
    CHECK_EQ(w.reg.nops(), ops);
    CHECK(w.ld->info(h_t) && w.ld->info(h_t)->main_addr != 0);
    CHECK_EQ(w.ld->resident_bytes(), resident);
    CHECK(heap != w.m.shape());
    CHECK_EQ(w.ld->finish(h_t, err, sizeof(err)), LIB_OK);
    CHECK(heap == w.m.shape());
    CHECK(w.ld->info(h_t) == 0);
    CHECK_EQ(w.ld->finish(h_t, err, sizeof(err)), LIB_STATE);                 // twice
    heap = w.m.shape();
    CHECK_EQ(w.ld->finish(h_b, err, sizeof(err)), LIB_STATE);                 // a resident application stays
    CHECK(strstr(err, "stays"));
    CHECK_EQ(w.ld->finish(h_tl, err, sizeof(err)), LIB_STATE);                // and so does a library
    CHECK(heap == w.m.shape() && w.ld->info(h_b) != 0 && w.ld->info(h_tl) != 0);

    // loading the same name again is refused, and the first copy is untouched
    refused("duplicate", w, tl, LIB_DUPLICATE, "testlib", "loaded already");
}

// S6: what is refused before the first instruction
static void refusals()
{
    World w(1);                                            // a base of 3.1
    w.m.caps.push_back("drive_a");
    char err[LIB_ERR_MAX];
    int h = 0;
    Img ok("alpha", "A");

    // ---- the relocation table
    Img r1 = ok;
    r1.relocs.push_back((uint32_t)r1.payload.size());                       // one word past the payload (bss is not payload)
    refused("reloc just outside", w, build(r1), LIB_BAD_FILE, "alpha", "outside the image", "0x40");
    Img r2 = ok;
    r2.relocs.push_back((uint32_t)r2.payload.size() - 2);                   // straddles the end and is not aligned
    refused("reloc straddles", w, build(r2), LIB_BAD_FILE, "outside the image");
    Img r3 = ok;
    r3.relocs[1] = 0x8000;
    refused("reloc far outside", w, build(r3), LIB_BAD_FILE, "outside the image");
    Img r4 = ok;
    r4.relocs[1] = 22;
    refused("reloc unaligned", w, build(r4), LIB_BAD_FILE, "word aligned");
    Img r5 = ok;
    r5.relocs[1] = 16;                                                      // the same word twice would be added to twice
    refused("reloc repeated", w, build(r5), LIB_BAD_FILE, "ascending");
    Img r6 = ok;
    std::swap(r6.relocs[0], r6.relocs[1]);
    refused("reloc descending", w, build(r6), LIB_BAD_FILE, "ascending");
    CHECK_EQ(w.m.register_calls, 0);                                        // not one instruction of any of them was entered

    // ---- the entry points, checked before anything is copied
    int allocs = w.m.allocs;
    Img e1 = ok;
    e1.reg = (uint32_t)e1.payload.size();
    refused("entry just outside", w, build(e1), LIB_BAD_FILE, "alpha", "lib_register", "outside the image");
    Img e2 = ok;
    e2.reg = 0x40000;
    refused("entry far outside", w, build(e2), LIB_BAD_FILE, "outside the image");
    Img e3 = ok;
    e3.reg = 3;
    refused("entry odd", w, build(e3), LIB_BAD_FILE, "4-aligned");
    Img e3b = ok;
    e3b.reg = 2;                                                            // 2-aligned: legal with the C extension, a trap on this CPU
    refused("entry 2-aligned", w, build(e3b), LIB_BAD_FILE, "alpha", "lib_register", "4-aligned");
    Img e3c = ok;
    e3c.kind = U2A3_KIND_APP;
    e3c.main = 6;
    refused("main 2-aligned", w, build(e3c), LIB_BAD_FILE, "app_main", "4-aligned");
    // header flags and the reserved word are zero in format 3: anything else is a feature this base does not have
    Img f1 = ok;
    f1.flags = 4;                                                   // bit 0 is the menu record (libmenu.h)
    refused("flags set", w, build(f1), LIB_BAD_FILE, "alpha", "flags");
    Img f2 = ok;
    f2.reserved = 0x80000000u;
    refused("reserved set", w, build(f2), LIB_BAD_FILE, "alpha", "reserved");
    Img e4 = ok;
    e4.kind = U2A3_KIND_APP;
    e4.main = 0xFFFFFF00;
    refused("main outside", w, build(e4), LIB_BAD_FILE, "app_main");
    Img e5 = ok;
    e5.reg = U2A3_NONE;
    refused("library without lib_register", w, build(e5), LIB_BAD_FILE, "lib_register");
    Img e6 = ok;
    e6.residence = U2A3_RES_TRANSIENT;
    refused("transient library", w, build(e6), LIB_BAD_FILE, "resident");
    CHECK_EQ(w.m.allocs, allocs);                                           // refused without spending memory
    CHECK_EQ(w.m.register_calls, 0);

    // ---- D11: versions, by name and both numbers
    Img v1 = ok;
    v1.needs.push_back(std::make_pair(std::string("base"), U2A3_VERSION(3, 2)));
    refused("needs base 3.2 on 3.1", w, build(v1), LIB_NEEDS, "alpha", "base", "3.2");
    {
        char e[LIB_ERR_MAX];
        MemSource s(build(v1));
        w.ld->load(&s, &h, e, sizeof(e));
        CHECK(strstr(e, "3.2") && strstr(e, "3.1"));                       // both numbers
    }
    Img v2 = ok;
    v2.needs.push_back(std::make_pair(std::string("base"), U2A3_VERSION(3, 1)));
    CHECK_EQ(w.load(build(v2), &h, err), LIB_OK);                           // 3.1 on 3.1 is enough
    Img v3 = ok;
    v3.name = "gamma";
    v3.needs.push_back(std::make_pair(std::string("cbm1541"), U2A3_VERSION(1, 0)));
    refused("needs a library that is not loaded", w, build(v3), LIB_NEEDS, "gamma", "cbm1541", "not loaded");
    Img dep("cbm1541", "D");
    dep.version = U2A3_VERSION(1, 0);
    w.m.twins["cbm1541"] = [](const LibHost::Entry &, const LibApi *, LibRecord *) { return 0; };
    CHECK_EQ(w.load(build(dep), &h, err), LIB_OK);
    Img v4 = v3;
    v4.needs[0].second = U2A3_VERSION(2, 0);
    refused("needs a newer library", w, build(v4), LIB_NEEDS, "gamma", "cbm1541", "2.0");
    {
        char e[LIB_ERR_MAX];
        MemSource s(build(v4));
        w.ld->load(&s, &h, e, sizeof(e));
        CHECK(strstr(e, "2.0") && strstr(e, "1.0"));
    }
    Img a1 = ok;
    a1.name = "gamma";
    a1.abi_min = 4;
    a1.abi_max = 4;
    refused("built for ABI 4", w, build(a1), LIB_ABI, "gamma", "ABI 4", "ABI 3");
    Img a2 = a1;
    a2.abi_min = 2;
    a2.abi_max = 2;
    refused("built for ABI 2", w, build(a2), LIB_ABI, "ABI 2");

    // ---- Q10: a capability the core lacks
    Img c1 = ok;
    c1.name = "gamma";
    c1.cap = "drive_b";
    allocs = w.m.allocs;
    refused("capability the core lacks", w, build(c1), LIB_CAPABILITY, "gamma", "drive_b", "Test Core");
    CHECK_EQ(w.m.allocs, allocs);                                           // no memory is spent on it
    Img c2 = c1;
    c2.cap = "drive_a";
    c2.name = "gamma";
    CHECK_EQ(w.load(build(c2), &h, err), LIB_OK);                           // the same file with a capability the core has

    // ---- the file itself
    std::vector<uint8_t> good = build(Img("delta", "D"));
    std::vector<uint8_t> f;
    f = good; f[0] ^= 1;
    refused("bad magic", w, f, LIB_BAD_FILE, "magic");
    f = good; f[6] = 2;
    refused("format 2", w, f, LIB_BAD_FILE, "Old file format 2");
    f = good; f.resize(f.size() - 4);
    refused("truncated relocation table", w, f, LIB_BAD_FILE, "relocation table lies outside the file");
    f = good; f.resize(U2A3_HEADER_SIZE + 10);
    refused("truncated payload", w, f, LIB_BAD_FILE, "image lies outside the file");
    f.resize(20);
    refused("too short", w, f, LIB_BAD_FILE, "too short");
    f = good; f[20] = 'A';
    refused("name with a capital", w, f, LIB_BAD_FILE, "no valid name");
    f = good; f[68] = 24;
    refused("alignment 24", w, f, LIB_BAD_FILE, "alignment");
    Img big("delta", "D");
    big.bss = U2A3_IMAGE_MAX;
    refused("image above the limit", w, build(big), LIB_BAD_FILE, "larger than");
}

// S6 last line and S7: a library that fails leaves nothing; no room is an answer, not a crash
static void failures()
{
    World w;
    char err[LIB_ERR_MAX];
    int h = -1;
    std::vector<uint8_t> tl = read_file("testlib.u2a");
    CHECK_EQ(w.load(tl, &h, err), LIB_OK);
    uint32_t used = w.m.used_bytes();
    std::string heap = w.m.shape();
    int ops = w.reg.nops();

    // lib_register itself fails
    w.m.twins["alpha"] = [](const LibHost::Entry &, const LibApi *, LibRecord *) { return 7; };
    int calls = w.m.register_calls;
    refused("lib_register returns 7", w, build(Img("alpha", "A")), LIB_REGISTER, "alpha", "lib_register failed (7)");
    CHECK_EQ(w.m.register_calls, calls + 1);                                // it did run, and nothing of it stayed

    // lib_register registers one name fine and takes a name that is owned: the first is rolled back too
    static const LibOpDecl two[2] = { { "late.fine", 0, 0, h_nop }, { "test.echo", 0, 0, h_nop } };
    static const LibStateDecl st[1] = { { "late.state", [](CoreCtx *) -> const char * { return "x"; } } };
    w.m.twins["beta"] = [](const LibHost::Entry &, const LibApi *, LibRecord *r) { r->ops = two; r->nops = 2; r->states = st; r->nstates = 1; return 0; };
    refused("a name that is taken", w, build(Img("beta", "B")), LIB_REGISTER, "beta", "test.echo", "testlib");
    CHECK(w.reg.find_op("late.fine") == 0);
    CHECK(w.reg.find_state("late.state") == 0);
    CHECK_STR(w.reg.error(), "");                                           // the refusal is in the load's sentence, not left in the registry
    CHECK_STR(w.reg.find_op("test.echo")->owner, "testlib");                // the first keeps it
    std::string r4 = call(w, "test.echo", "still");
    CHECK_STR(r4.c_str(), "still");

    // a record of another layout, a count without a list
    w.m.twins["gamma"] = [](const LibHost::Entry &, const LibApi *, LibRecord *r) { r->struct_size = 4; return 0; };
    refused("other record layout", w, build(Img("gamma", "G")), LIB_REGISTER, "gamma", "record layout");
    w.m.twins["gamma"] = [](const LibHost::Entry &, const LibApi *, LibRecord *r) { r->nops = 3; return 0; };
    refused("a count without a list", w, build(Img("gamma", "G")), LIB_REGISTER, "gamma", "no list");
    CHECK(heap == w.m.shape() && used == w.m.used_bytes() && ops == w.reg.nops());

    // the name is free after a failure: the same name loads once it works
    w.m.twins["alpha"] = twin_where<0>;
    CHECK_EQ(w.load(build(Img("alpha", "A")), &h, err), LIB_OK);

    // S7: no room
    w.m.fail_alloc = true;
    std::string heap2 = w.m.shape();
    refused("no room", w, build(Img("gamma", "G")), LIB_NO_ROOM, "No room", "gamma");
    w.m.fail_alloc = false;
    Img huge("gamma", "G");
    huge.bss = ARENA_SIZE;
    refused("heap too small", w, build(huge), LIB_NO_ROOM, "No room", "gamma");
    w.m.twins["gamma"] = twin_where<2>;
    CHECK_EQ(w.load(build(Img("gamma", "G")), &h, err), LIB_OK);            // and the machine is fine afterwards

    // every slot taken
    for (int i = w.ld->count(); i < LIB_MAX; i++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "fill%d", i);
        w.m.twins[nm] = [](const LibHost::Entry &, const LibApi *, LibRecord *) { return 0; };
        CHECK_EQ(w.load(build(Img(nm, "F")), &h, err), LIB_OK);
    }
    refused("no slot", w, build(Img("extra", "E")), LIB_NO_ROOM, "No room", "extra");
}

static std::vector<std::string> g_sink;
static void sink(const char *t) { g_sink.push_back(t); }

// Everything a record declares is served now (the menu merge for nodes, file actions, settings and tabs; the poll task for the poll): the loader has
// nothing to say about a record, whatever it declares, and counts nothing as unserved.
static void unserved()
{
    World w;
    g_sink.clear();
    w.ld->set_log(sink);
    char err[LIB_ERR_MAX];
    int h = -1;
    CHECK_EQ(w.load(read_file("testlib.u2a"), &h, err), LIB_OK);
    w.m.twins["alpha"] = [](const LibHost::Entry &, const LibApi *, LibRecord *rec) {
        static const LibNodeDecl nodes[2] = { };
        static const LibFileActionDecl fas[1] = { };
        static const LibSettingDecl st[3] = { };
        static const LibTabDecl tabs[1] = { };
        rec->nnodes = 2; rec->nodes = nodes;
        rec->nfile_actions = 1; rec->file_actions = fas;
        rec->nsettings = 3; rec->settings = st;
        rec->ntabs = 1; rec->tabs = tabs;
        rec->poll.op = "alpha.poll"; rec->poll.period_ms = 20;
        return 0;
    };
    CHECK_EQ(w.load(build(Img("alpha", "A")), &h, err), LIB_OK);
    CHECK_EQ((int)g_sink.size(), 0);
    CHECK_EQ(w.ld->unserved_count(), 0);
    const LibInfo *i = w.ld->info(h);
    CHECK(i && i->record.nnodes == 2 && i->record.ntabs == 1 && !strcmp(i->record.poll.op, "alpha.poll"));      // and the record is kept whole for the merge
}


// A library's own menu record (T104.1 D9, libmenu.h): header flag bit 0 and the offset in `reserved`; read and checked before anything is spent, kept by the
// slot, freed with it. Refused by name: an offset outside the file, a record that is damaged, one that gives a route.
static std::vector<uint8_t> with_menu(Img m, const std::vector<uint8_t> &rec, uint32_t offset_delta = 0)
{
    m.flags = U2A3_FLAG_MENU;
    std::vector<uint8_t> f = build(m);
    while (f.size() & 3) f.push_back(0);
    uint32_t off = (uint32_t)f.size() + offset_delta;
    memcpy(&f[100], &off, 4);                                       // the `reserved` word of the header
    f.insert(f.end(), rec.begin(), rec.end());
    return f;
}

// the heap of the process, counted: a menu record is two blocks (the bytes and the decoded table) that must go back with the slot
static long g_live;
void *operator new(size_t n) { g_live++; return malloc(n ? n : 1); }
void *operator new[](size_t n) { g_live++; return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { if (p) { g_live--; free(p); } }
void operator delete[](void *p) noexcept { if (p) { g_live--; free(p); } }
void operator delete(void *p, size_t) noexcept { if (p) { g_live--; free(p); } }
void operator delete[](void *p, size_t) noexcept { if (p) { g_live--; free(p); } }

static void menu_record()
{
    std::vector<uint8_t> rec = read_file("menu_lib.bin");
    CHECK(rec.size() > DESCREC_HEADER);
    {
        World w;
        char err[LIB_ERR_MAX] = "";
        int h = -1;
        CHECK_EQ(w.load(with_menu(Img("alpha", "A"), rec), &h, err), LIB_OK);
        const LibInfo *i = w.ld->info(h);
        CHECK(i && i->menu != 0);
        if (i && i->menu) {
            CHECK_EQ(i->menu->rec.nnodes, 1); CHECK_STR(i->menu->nodes[0].label, "Pulse");
            CHECK_EQ(i->menu->rec.ntabs, 1); CHECK_STR(i->menu->rec.poll.op, "test.poll"); CHECK_EQ(i->menu->rec.poll.period_ms, 20);
            CHECK_EQ(i->menu->rec.nsettings, 1); CHECK_EQ(i->menu->rec.nfile_actions, 1);
        }
        // a file without the flag has none, and its `reserved` word still has to be 0
        CHECK_EQ(w.load(build(Img("beta", "B")), &h, err), LIB_OK);
        CHECK(w.ld->info(h)->menu == 0);
    }
    World w;
    refused("menu offset outside", w, with_menu(Img("alpha", "A"), rec, 100000), LIB_BAD_FILE, "alpha", "menu record lies outside the file");
    refused("menu offset odd", w, with_menu(Img("alpha", "A"), rec, 1), LIB_BAD_FILE, "alpha", "outside the file");
    std::vector<uint8_t> bad = rec;
    bad[0] ^= 1;
    refused("menu damaged", w, with_menu(Img("alpha", "A"), bad), LIB_BAD_FILE, "alpha", "magic");
    std::vector<uint8_t> shortrec(rec.begin(), rec.begin() + rec.size() / 2);
    refused("menu cut off", w, with_menu(Img("alpha", "A"), shortrec), LIB_BAD_FILE, "alpha", "length");
    std::vector<uint8_t> route = read_file("menu_route.bin");
    refused("menu with a route", w, with_menu(Img("alpha", "A"), route), LIB_BAD_FILE, "alpha", "gives a route");
    std::vector<uint8_t> hides = read_file("menu_hide.bin");
    refused("menu that hides", w, with_menu(Img("alpha", "A"), hides), LIB_BAD_FILE, "alpha", "hides and renames");
    Img f2("alpha", "A");
    f2.flags = 2;
    refused("an unknown flag", w, build(f2), LIB_BAD_FILE, "alpha", "header flags");
    Img r1("alpha", "A");
    r1.reserved = 16;                                               // a reserved word without the flag
    refused("reserved without the flag", w, build(r1), LIB_BAD_FILE, "alpha", "reserved bits");
    // a transient application's record goes with it
    {
        World w2;
        char err[LIB_ERR_MAX] = "";
        int h = -1;
        Img a("app1", "A");
        a.kind = U2A3_KIND_APP;
        a.residence = U2A3_RES_TRANSIENT;
        a.reg = U2A3_NONE;
        a.main = 8;
        std::vector<uint8_t> file = with_menu(a, rec), plain = build(a);
        // the test's model keeps a block of its own per load; what the menu record adds is measured against the same load without it
        CHECK_EQ(w2.load(plain, &h, err), LIB_OK);                      // once to warm the model up (it keeps a block of its own the first time)
        CHECK_EQ(w2.ld->finish(h, err, LIB_ERR_MAX), LIB_OK);
        long l0 = g_live;
        CHECK_EQ(w2.load(plain, &h, err), LIB_OK);
        long plain_held = g_live - l0;
        CHECK_EQ(w2.ld->finish(h, err, LIB_ERR_MAX), LIB_OK);
        long plain_after = g_live - l0;
        long l1 = g_live;
        CHECK_EQ(w2.load(file, &h, err), LIB_OK);
        CHECK(w2.ld->info(h)->menu != 0);
        CHECK_EQ(g_live - l1 - plain_held, 2);                         // the bytes and the table
        CHECK_EQ(w2.ld->finish(h, err, LIB_ERR_MAX), LIB_OK);
        CHECK(w2.ld->info(h) == 0);
        CHECK_EQ(g_live - l1, plain_after);                            // and they went with the application
        // a refusal after the record was read leaves nothing of it either
        Img needy("app2", "A");
        needy.kind = U2A3_KIND_APP; needy.residence = U2A3_RES_TRANSIENT; needy.reg = U2A3_NONE; needy.main = 8;
        needy.needs.push_back(std::make_pair(std::string("nothing"), U2A3_VERSION(1, 0)));
        char e2[LIB_ERR_MAX];
        std::vector<uint8_t> nf = with_menu(needy, rec), np = build(needy);
        long r0 = g_live;
        CHECK(w2.load(np, &h, e2) == LIB_NEEDS);
        long r_plain = g_live - r0;
        long r1 = g_live;
        CHECK(w2.load(nf, &h, e2) == LIB_NEEDS);
        CHECK_EQ(g_live - r1, r_plain);
    }
}

// ------------------------------------------------------------------------------------------------ applications run (T104.3 S1)

static int h_upper(CoreCtx *, const OpArgs &a, OpOut &o)
{
    int i = 0;
    for (const char *p = a.s(0); *p && i < o.msglen - 1; p++) { o.msg[i++] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p; }
    o.msg[i] = 0;
    return OPE_OK;
}
static const ArgSpec upper_args[] = { ARG_STRING("text", 32) };

// The application twin: takes three blocks and gives one back (two "leak"), grows one, calls an operation by name, reads the clock and
// the capability, sleeps, leaves a reply and returns 7.
static int app_twin(const LibHost::Entry &, const LibApi *api, AppCtx *ctx)
{
    CHECK(api->sys && api->sys->size >= sizeof(SvcSys) && api->mem && api->ops && api->fs && !api->dlg);
    char *a = (char *)api->mem->malloc(100);
    char *b = (char *)api->mem->malloc(40);
    char *c = (char *)api->mem->malloc(3000);
    CHECK(a && b && c);
    CHECK(((uintptr_t)a & 15) == 0 && ((uintptr_t)b & 15) == 0);
    strcpy(b, "kept");
    b = (char *)api->mem->realloc(b, 200);
    CHECK(b && !strcmp(b, "kept"));
    api->mem->free(a);
    OpIn in = in_str("text", ctx->file);
    OpResult r;
    int rc = api->ops->call("test.upper", &in, 1, &r);
    CHECK_EQ(rc, OPE_OK);
    uint32_t t0 = api->sys->ms();
    api->sys->sleep_ms(25);
    CHECK_EQ(api->sys->ms() - t0, 25u);
    CHECK(api->sys->has_capability("drive_a") && !api->sys->has_capability("nope"));
    snprintf(ctx->reply, ctx->reply_len, "%s:%s", ctx->action, r.msg);
    return 7;
}

static void apps()
{
    World w;
    w.m.caps.push_back("drive_a");
    w.reg.contributor("bench");
    CHECK_EQ(w.reg.add_op("test.upper", upper_args, 1, h_upper), OPE_OK);
    CoreCtx cx;
    memset(&cx, 0, sizeof(cx));                                              // the handler never looks at it
    w.ld->set_ctx(&cx);
    w.m.mains["tapp"] = app_twin;
    w.m.mains["rapp"] = app_twin;
    char err[LIB_ERR_MAX] = "", reply[64];

    std::string heap0 = w.m.shape();
    for (int round = 0; round < 2; round++) {                                // S1: a second run works the same
        Img t("tapp", "T");
        t.kind = U2A3_KIND_APP;
        t.residence = U2A3_RES_TRANSIENT;
        t.reg = U2A3_NONE;
        t.main = 4;
        int h = -1, arc = -1;
        CHECK_EQ(w.load(build(t), &h, err), LIB_OK);
        CHECK_EQ(w.ld->count(), 1);
        CHECK_EQ(w.ld->run(h, "write", "game.crt", reply, sizeof(reply), &arc, err, sizeof(err)), LIB_OK);
        CHECK_EQ(arc, 7);
        CHECK_STR(reply, "write:GAME.CRT");
        CHECK_EQ(w.ld->count(), 0);                                          // a transient app is gone ...
        CHECK(w.ld->info(h) == 0);
        CHECK_EQ(w.ld->account_blocks(h), 0);                                // ... with the two blocks it never gave back
        CHECK(heap0 == w.m.shape());
        CHECK_EQ(w.ld->account_blocks(-1), 0);
    }

    // a resident app keeps its image and its blocks
    Img r("rapp", "R");
    r.kind = U2A3_KIND_APP;
    r.residence = U2A3_RES_RESIDENT;
    r.reg = U2A3_NONE;
    r.main = 4;
    int hr = -1, arc = 0;
    CHECK_EQ(w.load(build(r), &hr, err), LIB_OK);
    CHECK_EQ(w.ld->run(hr, "", "", 0, 0, &arc, err, sizeof(err)), LIB_OK);
    CHECK_EQ(arc, 7);
    CHECK(w.ld->info(hr) != 0);
    CHECK_EQ(w.ld->account_blocks(hr), 2);
    CHECK_EQ(w.ld->account_bytes(hr), 3200u);

    // an application with screens (T104.3 D0): it registers what its screens bind to while it is open, runs, stays loaded (keep), and what it registered
    // goes with it when it is released - the other contributors' names stay
    static const LibOpDecl scr_ops[1] = { { "scr.go", 0, 0, h_nop } };
    static const LibStateDecl scr_st[1] = { { "scr.value", [](CoreCtx *) -> const char * { return "open"; } } };
    w.m.twins["sapp"] = [](const LibHost::Entry &, const LibApi *, LibRecord *rec) { rec->ops = scr_ops; rec->nops = 1; rec->states = scr_st; rec->nstates = 1; return 0; };
    w.m.mains["sapp"] = app_twin;
    Img sc("sapp", "S");
    sc.kind = U2A3_KIND_APP;
    sc.residence = U2A3_RES_TRANSIENT;
    sc.reg = 0;
    sc.main = 4;
    int hs = -1;
    CHECK_EQ(w.load(build(sc), &hs, err), LIB_OK);
    CHECK(w.reg.find_op("scr.go") != 0 && w.reg.find_state("scr.value") != 0);
    CHECK_STR(w.reg.find_op("scr.go")->owner, "sapp");
    CHECK_EQ(w.ld->run(hs, "x", "s.crt", reply, sizeof(reply), &arc, err, sizeof(err), true), LIB_OK);
    CHECK(w.ld->info(hs) != 0);                                              // kept: its screens are up
    CHECK(w.reg.find_op("scr.go") != 0);
    CHECK(w.reg.find_op("test.upper") != 0);
    CHECK_EQ(w.ld->release(hs, err, sizeof(err)), LIB_OK);
    CHECK(w.ld->info(hs) == 0);
    CHECK(w.reg.find_op("scr.go") == 0 && w.reg.find_state("scr.value") == 0);
    CHECK(w.reg.find_op("test.upper") != 0);                                  // everybody else's stay
    CHECK_EQ(w.ld->account_blocks(hs), 0);

    // run() of a library, of nothing, and on a host that cannot run apps
    int hl = -1;
    CHECK_EQ(w.load(read_file("testlib.u2a"), &hl, err), LIB_OK);
    CHECK_EQ(w.ld->run(hl, 0, 0, reply, sizeof(reply), &arc, err, sizeof(err)), LIB_NOT_APP);
    CHECK(strstr(err, "not an app"));
    CHECK_EQ(w.ld->run(11, 0, 0, reply, sizeof(reply), &arc, err, sizeof(err)), LIB_STATE);

    // a host that keeps LibHost's call_main cannot run apps: the run is refused and the app stays loaded
    struct Plain : LibHost {
        Model &m;
        explicit Plain(Model &x) : m(x) { }
        void *alloc(uint32_t n) override { return m.alloc(n); }
        void release(void *p) override { m.release(p); }
        uint32_t addr_of(const void *p) override { return m.addr_of(p); }
        void sync_code(void *, uint32_t) override { }
        bool has_capability(const char *) override { return false; }
        const char *core_name() override { return "plain"; }
        int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }
    };
    Model pm;
    Plain plain(pm);
    OpRegistry reg2;
    LibLoader ld2(&plain, &reg2);
    Img t("tapp", "T");
    t.kind = U2A3_KIND_APP;
    t.residence = U2A3_RES_TRANSIENT;
    t.reg = U2A3_NONE;
    t.main = 4;
    MemSource s2(build(t));
    int h2 = -1;
    CHECK_EQ(ld2.load(&s2, &h2, err, sizeof(err)), LIB_OK);
    CHECK_EQ(ld2.run(h2, 0, 0, reply, sizeof(reply), &arc, err, sizeof(err)), LIB_NOT_APP);
    CHECK(strstr(err, "cannot run apps"));
    CHECK(ld2.info(h2) != 0);
}

// ------------------------------------------------------------------------------------------------ api->fs (T104.3 D2)

// The files twin: writes and reads a file through a handle, makes a directory, moves a file into it, lists it, leaves one file open;
// then everything under /flash that must be refused, and the one path that must not be.
static int app_files(const LibHost::Entry &, const LibApi *api, AppCtx *ctx)
{
    const SvcFs *fs = api->fs;
    CHECK(fs && fs->size >= sizeof(SvcFs));
    SvcFile *f = fs->open("/USB0/dump.bin", true);
    CHECK(f && fs->writable(f));
    CHECK_EQ((int)fs->fsize(f), 16);
    CHECK(fs->write(f, 4, "TRX!", 4));
    char b[5] = "";
    CHECK(fs->read(f, 4, b, 4));
    CHECK_STR(b, "TRX!");
    CHECK(!fs->read(f, 14, b, 4));                                       // all or nothing past the end
    CHECK(fs->sync(f));
    fs->close(f);
    CHECK(fs->save("/USB0/new.txt", "hello", 5));
    CHECK(fs->mkdir("/USB0/saves"));
    CHECK(fs->mkdir("/USB0/saves"));                                     // exists already: a directory is success (fsif.h)
    CHECK(fs->rename("/USB0/new.txt", "/USB0/saves/new.txt"));
    SvcFsEntry e[8];
    int n = fs->list("/USB0", e, 8);
    CHECK_EQ(n, 2);
    if (n == 2) { CHECK(e[0].dir && !strcmp(e[0].name, "saves") && !e[1].dir && !strcmp(e[1].name, "dump.bin")); }
    {   // T180 #38: the core lists a FAT long name whole; an application built against SvcFsEntry (56 bytes of name) gets the name cut to 55 characters and the other fields as they were
        CHECK_EQ(sizeof(SvcFsEntry), 72u);                                    // the layout of the app ABI did not move
        std::string longname(100, 'q');
        longname += ".adf";
        CHECK(fs->mkdir("/USB0/long"));
        CHECK(fs->save(("/USB0/long/" + longname).c_str(), "0123456789", 10));
        CHECK(fs->save("/USB0/long/b.txt", "xy", 2));
        SvcFsEntry le[4];
        memset(le, 0x5A, sizeof(le));
        int ln = fs->list("/USB0/long", le, 4);
        CHECK_EQ(ln, 2);
        if (ln == 2) {
            CHECK(!strcmp(le[0].name, "b.txt") && le[0].size == 2);
            CHECK_EQ(strlen(le[1].name), 55u);
            CHECK(!strncmp(le[1].name, longname.c_str(), 55) && !le[1].dir && le[1].size == 10);
        }
        CHECK_EQ(le[2].name[0], 0x5A);                                        // nothing past the count is written
        CHECK_EQ(fs->list("/USB0/long", le, 1), 1);                           // the max is the application's own
    }
    uint64_t sz = 0;
    bool dir = true;
    CHECK(fs->stat("/USB0/saves/new.txt", &sz, &dir) && sz == 5 && !dir);
    char t[8] = "";
    CHECK_EQ(fs->load("/USB0/saves/new.txt", t, sizeof(t) - 1), 5);
    // the flash: refused here, whatever the CoreFs below would do
    CHECK(!fs->save("/flash/evil.bin", "x", 1));
    CHECK(!fs->save("/FLASH/trx64u/other.cfg", "x", 1));
    CHECK(!fs->remove("/flash/roms/kernal.bin"));
    CHECK(!fs->mkdir("/Flash/new"));
    CHECK(!fs->rename("/USB0/dump.bin", "/flash/dump.bin"));
    CHECK(!fs->rename("/flash/roms/kernal.bin", "/USB0/k.bin"));
    SvcFile *k = fs->open("/flash/roms/kernal.bin", true);
    CHECK(k && !fs->writable(k) && !fs->write(k, 0, "x", 1));
    fs->close(k);
    CHECK(fs->save(FS_FLASH_CFG_PATH, "a=1", 3));                       // the exception of fsif.h
    CHECK(fs->open("/USB0/missing.bin", false) == 0);
    SvcFile *left = fs->open("/USB0/dump.bin", false);                  // left open: closed when the app returns
    CHECK(left != 0);
    snprintf(ctx->reply, ctx->reply_len, "ok");
    return 0;
}

static void files()
{
    World w;
    FakeFs disk;
    disk.put_size("/USB0/dump.bin", 16);
    disk.put_size("/flash/roms/kernal.bin", 8192);
    disk.put_size("/flash/roms/basic.bin", 8192);
    CoreCtx cx;
    memset(&cx, 0, sizeof(cx));
    cx.fs = &disk;
    w.ld->set_ctx(&cx);
    w.m.mains["fapp"] = app_files;
    char err[LIB_ERR_MAX] = "", reply[16] = "";
    std::string heap0 = w.m.shape();
    Img t("fapp", "F");
    t.kind = U2A3_KIND_APP;
    t.residence = U2A3_RES_TRANSIENT;
    t.reg = U2A3_NONE;
    t.main = 4;
    int h = -1, arc = -1;
    CHECK_EQ(w.load(build(t), &h, err), LIB_OK);
    CHECK_EQ(w.ld->run(h, "", "", reply, sizeof(reply), &arc, err, sizeof(err)), LIB_OK);
    CHECK_EQ(arc, 0);
    CHECK_STR(reply, "ok");
    CHECK_EQ(disk.open_files, 0);                                        // the file it left open is closed ...
    CHECK_EQ(w.ld->account_files(h), 0);
    CHECK(heap0 == w.m.shape());                                         // ... and its handle given back
    CHECK(!disk.has("/flash/evil.bin") && !disk.has("/flash/dump.bin") && disk.has("/flash/roms/kernal.bin"));
    CHECK(disk.text("/flash/roms/kernal.bin") == std::string(8192, (char)0xA5));
    CHECK(disk.text(FS_FLASH_CFG_PATH) == "a=1");
    CHECK(disk.text("/USB0/dump.bin").substr(4, 4) == "TRX!");

    // without a core context there are no files
    World w2;
    w2.m.mains["napp"] = [](const LibHost::Entry &, const LibApi *api, AppCtx *) -> int {
        CHECK(api->fs->open("/USB0/dump.bin", false) == 0);
        SvcFsEntry e[2];
        CHECK_EQ(api->fs->list("/", e, 2), -1);
        CHECK(!api->fs->save("/USB0/x", "x", 1));
        return 3;
    };
    Img n("napp", "N");
    n.kind = U2A3_KIND_APP;
    n.residence = U2A3_RES_TRANSIENT;
    n.reg = U2A3_NONE;
    n.main = 4;
    int hn = -1;
    CHECK_EQ(w2.load(build(n), &hn, err), LIB_OK);
    CHECK_EQ(w2.ld->run(hn, "", "", 0, 0, &arc, err, sizeof(err)), LIB_OK);
    CHECK_EQ(arc, 3);
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);                                  // a mutant may crash: what it said before has to be out
    menu_record();
    unserved();
    coexist();
    refusals();
    failures();
    apps();
    files();
    DONE("test_libload");
}
