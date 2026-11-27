// T239.3 P2-6, base API 4.0 (D58): the libraries a core application links with `register` are registered at its start, each with the menu record the bundle carries for it by name
// (record 7 version 2 of the .cor, tools/cor_pack.py pack_libs). Pure: the file system is a map of byte vectors, the heap a model, the step that registers the libraries (libtab_link on the
// board) is a fake of this file that calls the callbacks of lib_link_cor and LibLoader::link with two test register libraries. The menu records are the real ones (gen_bundle.py).
//   (1) lib_cor_menus: a good record of two entries; version 1 (library files) refused with the base 4.0 sentence; damaged records (count, an entry past the end, a duplicate name, a
//       name with no NUL, a wrong magic, a record outside the file) refused;
//   (2) lib_link_cor: each library gets the menu record of its own name, whatever the order of the record; one log line per library ("(linked) : ok" / "refused"); before_first runs once, before
//       the first registration; a library that fails to register is refused with the loader's sentence; a menu record no library takes is a refusal; a damaged record or a record that cannot be
//       read is a refusal and the libraries still register without a menu record; no record at all is no refusal; the summary line of each outcome.
// The Atari's and the NES's units (a8_libscan, nes_libscan) run the same cases against their descriptors' hosts and check that the descriptors are well formed. What loaded library files
// (folders, dependency order, capability sentences, file limits) is gone with the files: the order of the libraries is the TOML B's (tools/libtab.py link_order, test_libtab.py), their capability
// is the app's (register_ops with `capability`, libtab.cc).
//
//   test_libscan BUNDLE_DIR      (menu_alpha.bin, menu_gamma.bin made by gen_bundle.py)
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "desc_check.h"
#include "libscan.h"
#include "descrec.h"

extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
const CoreDesc *const core_table[] = { &nes_desc, 0 };

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}
static void put32(Bytes &v, uint32_t x) { for (int i = 0; i < 4; i++) { v.push_back((uint8_t)(x >> (8 * i))); } }
static void putname(Bytes &v, const std::string &s) { for (int i = 0; i < 16; i++) { v.push_back(i < (int)s.size() ? s[i] : 0); } }

// record 7 version 2: "TRXL", 2, count, per entry name[16], length, bytes, zeros to 4
struct Entry {
    std::string name;
    Bytes menu;
};
static Bytes record7(const std::vector<Entry> &es, uint32_t version = 2, int32_t count = -1)
{
    Bytes v;
    v.push_back('T'); v.push_back('R'); v.push_back('X'); v.push_back('L');
    put32(v, version);
    put32(v, count < 0 ? (uint32_t)es.size() : (uint32_t)count);
    for (size_t i = 0; i < es.size(); i++) {
        putname(v, es[i].name);
        put32(v, (uint32_t)es[i].menu.size());
        v.insert(v.end(), es[i].menu.begin(), es[i].menu.end());
        while (v.size() & 3) { v.push_back(0); }
    }
    return v;
}

static const char *COR = "/usb0/cores/test.cor";
static const uint32_t PREFIX = 36;                                  // the record does not start at the file's start
static Bytes in_cor(const Bytes &rec)
{
    Bytes f(PREFIX, 0xEE);
    f.insert(f.end(), rec.begin(), rec.end());
    return f;
}

// ---------------------------------------------------------------------------------------------------------------- the machine

struct Host : DescLibHost {
    std::vector<void *> live;
    int allocs;
    explicit Host(const CoreDesc *d) : DescLibHost(d), allocs(0) { }
    ~Host() { for (size_t i = 0; i < live.size(); i++) { free(live[i]); } }
    void *alloc(uint32_t n) override { allocs++; void *p = malloc(n); live.push_back(p); return p; }
    void release(void *p) override
    {
        for (size_t i = 0; i < live.size(); i++) { if (live[i] == p) { live.erase(live.begin() + i); free(p); return; } }
        CHECK(!"release of a block that is not allocated");
    }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }   // nothing is loaded from a file
};

struct Log : LibLog {
    std::vector<std::string> lines;
    void line(const char *t) override { lines.push_back(t); }
    int find(const std::string &part) const
    {
        for (size_t i = 0; i < lines.size(); i++) { if (lines[i].find(part) != std::string::npos) { return (int)i; } }
        return -1;
    }
    int count(const std::string &part) const
    {
        int n = 0;
        for (size_t i = 0; i < lines.size(); i++) { n += lines[i].find(part) != std::string::npos; }
        return n;
    }
};

// the test register libraries and the fake link step
static std::vector<std::string> g_registered;
static int g_fail_rc;                                               // what the register function of "bb" returns
static int reg_aa(const LibApi *, LibRecord *) { g_registered.push_back("aa"); return 0; }
static int reg_bb(const LibApi *, LibRecord *) { g_registered.push_back("bb"); return g_fail_rc; }
struct Item {
    const char *name;
    int (*reg)(const LibApi *, LibRecord *);
};
static std::vector<Item> g_items;
static std::vector<int> g_menu_given;                               // per item: did the menu callback hand it a record
static void fake_link_all(LibLoader &ld, bool (*menu)(void *, const char *, DescRecord **, uint8_t **), void (*done)(void *, const char *, int, const char *), void *user)
{
    g_menu_given.clear();
    for (size_t i = 0; i < g_items.size(); i++) {
        DescRecord *rec = 0;
        uint8_t *bytes = 0;
        g_menu_given.push_back(menu(user, g_items[i].name, &rec, &bytes) ? 1 : 0);
        char err[LIB_ERR_MAX] = "";
        int rc = ld.link(g_items[i].name, g_items[i].reg, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE, rec, bytes, err, sizeof(err));
        done(user, g_items[i].name, rc, err);
    }
}

static int g_before, g_before_at;
static void before_first(void *ctx)
{
    g_before++;
    g_before_at = (int)g_registered.size();
    CHECK(ctx == (void *)&g_before);
}

static CoreDesc g_blank;
struct World {
    FakeFs fs;
    Host host;
    OpRegistry reg;
    LibLoader ld;
    Log log;
    LibScanResult r;
    World() : host(&nes_desc), ld(&host, &reg) { reset(); }
    void reset() { g_registered.clear(); g_items.clear(); g_fail_rc = 0; g_before = 0; g_before_at = -1; }
    void libs(bool aa, bool bb)
    {
        g_items.clear();
        if (aa) { g_items.push_back(Item{ "aa", reg_aa }); }
        if (bb) { g_items.push_back(Item{ "bb", reg_bb }); }
    }
    void cor(const Bytes &rec) { fs.put(COR, in_cor(rec)); }
    void run(uint32_t rec_size) { lib_link_cor(&ld, &fs, COR, PREFIX, rec_size, fake_link_all, &log, &r, before_first, &g_before); }
};

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    std::string dir = argv[1];
    // the real descriptors are well formed and name their capabilities (desc_check is the one of the product)
    for (const CoreDesc *const *p = core_table; *p; p++) { const char *why = desc_check(*p); if (why) printf("%s: %s\n", (*p)->cor_name, why); CHECK(!why); }
    CHECK(!desc_check_table(core_table));

    Bytes alpha = slurp(dir + "/menu_alpha.bin"), gamma = slurp(dir + "/menu_gamma.bin");
    CHECK(alpha.size() > 12 && gamma.size() > 12);
    std::vector<Entry> two;                                         // bb first: the menu goes by name, not by position
    two.push_back(Entry{ "bb", gamma });
    two.push_back(Entry{ "aa", alpha });

    // ---- (1) lib_cor_menus: a good record, read where it lies in the .cor
    {
        World w;
        Bytes rec = record7(two);
        w.cor(rec);
        LibCorMenus m;
        char why[200] = "";
        CHECK_EQ(lib_cor_menus(&w.fs, COR, PREFIX, (uint32_t)rec.size(), &m, why, sizeof(why)), 0);
        CHECK_EQ(m.n, 2);
        CHECK_STR(m.name[0], "bb"); CHECK_STR(m.name[1], "aa");
        CHECK_EQ(m.len[0], gamma.size()); CHECK_EQ(m.len[1], alpha.size());
        Bytes f = in_cor(rec);
        CHECK(m.n == 2 && !memcmp(&f[m.off[0]], gamma.data(), gamma.size()) && !memcmp(&f[m.off[1]], alpha.data(), alpha.size()));   // the offsets are in the .cor
        CHECK(!m.taken[0] && !m.taken[1]);
    }
    // ---- (1) version 1 (library files) and damaged records are refused, each with a sentence
    {
        World w;
        LibCorMenus m;
        char why[200];
        Bytes v1 = record7(two, 1);
        w.cor(v1);
        CHECK_EQ(lib_cor_menus(&w.fs, COR, PREFIX, (uint32_t)v1.size(), &m, why, sizeof(why)), -1);
        CHECK(strstr(why, "base 4.0 loads no library file") != 0);
        CHECK_EQ(m.n, 0);
        struct Bad { const char *what; Bytes rec; const char *sentence; } bad[8];
        int n = 0;
        bad[n].what = "count 3, two entries"; bad[n].rec = record7(two, 2, 3); bad[n].sentence = "damaged"; n++;
        bad[n].what = "count 1, two entries"; bad[n].rec = record7(two, 2, 1); bad[n].sentence = "damaged"; n++;
        bad[n].what = "count 0"; bad[n].rec = record7(std::vector<Entry>(), 2); bad[n].sentence = "damaged"; n++;
        {   // an entry past the end: the length of the last claims more than is there
            Bytes r = record7(two);
            size_t len_at = 12 + 16 + 4 + gamma.size() + ((4 - (gamma.size() & 3)) & 3) + 16;
            r[len_at + 2] = 0x10;                                   // +1 MiB
            bad[n].what = "an entry past the end"; bad[n].rec = r; bad[n].sentence = "damaged"; n++;
        }
        {   // a duplicate name
            std::vector<Entry> d;
            d.push_back(Entry{ "aa", alpha });
            d.push_back(Entry{ "aa", gamma });
            bad[n].what = "a duplicate name"; bad[n].rec = record7(d); bad[n].sentence = "damaged"; n++;
        }
        {   // a name of 16 characters with no NUL
            Bytes r = record7(two);
            for (int i = 0; i < 16; i++) { r[12 + i] = 'x'; }
            bad[n].what = "a name with no NUL"; bad[n].rec = r; bad[n].sentence = "damaged"; n++;
        }
        {   // an entry with an empty name
            Bytes r = record7(two);
            r[12] = 0;
            bad[n].what = "an empty name"; bad[n].rec = r; bad[n].sentence = "damaged"; n++;
        }
        {   // the magic
            Bytes r = record7(two);
            r[0] = 'Q';
            bad[n].what = "the magic"; bad[n].rec = r; bad[n].sentence = "cannot be read"; n++;
        }
        for (int i = 0; i < n; i++) {
            w.cor(bad[i].rec);
            why[0] = 0;
            int rc = lib_cor_menus(&w.fs, COR, PREFIX, (uint32_t)bad[i].rec.size(), &m, why, sizeof(why));
            if (rc != -1 || !strstr(why, bad[i].sentence)) { printf("FAIL lib_cor_menus %s: rc %d, \"%s\"\n", bad[i].what, rc, why); g_fails++; }
            CHECK_EQ(m.n, 0);
        }
        // a record that lies past the end of the file, and one that is too short for a header
        w.cor(record7(two));
        CHECK_EQ(lib_cor_menus(&w.fs, COR, PREFIX, 100000, &m, why, sizeof(why)), -1);
        CHECK_EQ(lib_cor_menus(&w.fs, COR, PREFIX, 8, &m, why, sizeof(why)), -1);
        CHECK_EQ(lib_cor_menus(&w.fs, "/usb0/cores/gone.cor", PREFIX, 100, &m, why, sizeof(why)), -1);
    }
    // ---- (2) each library gets the menu record of its own name; the log, the count, before_first, the summary
    {
        World w;
        Bytes rec = record7(two);
        w.cor(rec);
        w.libs(true, true);
        w.run((uint32_t)rec.size());
        CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 0); CHECK_EQ(w.r.nkept, 0);
        CHECK_STR(w.r.summary, "2 loaded");
        CHECK_EQ(w.ld.count(), 2);
        CHECK(g_menu_given.size() == 2 && g_menu_given[0] == 1 && g_menu_given[1] == 1);
        const LibInfo *a = w.ld.info(w.ld.handle_of("aa")), *b = w.ld.info(w.ld.handle_of("bb"));
        CHECK(a && b && a->menu && b->menu);
        if (a && b && a->menu && b->menu) {
            CHECK(a->menu->rec.poll.op && !strcmp(a->menu->rec.poll.op, "alpha.go"));     // aa got alpha's record (it declares a poll) ...
            CHECK(b->menu->rec.poll.op == 0 || !b->menu->rec.poll.op[0]);                  // ... bb got gamma's (it has none)
            CHECK(b->menu->rec.nnodes == 1 && b->menu->rec.nfile_actions == 1);
        }
        CHECK(a && a->kind == U2A3_KIND_LIB && a->image_size == 0);                       // linked: a slot of kind lib, no image
        CHECK_EQ(w.log.count("(linked) : ok"), 2);
        CHECK(w.log.find("[lib] aa (linked) : ok") >= 0 && w.log.find("[lib] bb (linked) : ok") >= 0);
        CHECK(w.log.find("refused") < 0);
        CHECK_EQ(g_before, 1);                                                             // once ...
        CHECK_EQ(g_before_at, 0);                                                          // ... before the first library registered
        CHECK(g_registered.size() == 2 && g_registered[0] == "aa" && g_registered[1] == "bb");
    }
    // ---- (2) only one of the two is linked and the record carries one of them: a library with no record in the bundle registers without
    {
        World w;
        std::vector<Entry> one(1, Entry{ "aa", alpha });
        Bytes rec = record7(one);
        w.cor(rec);
        w.libs(true, true);
        w.run((uint32_t)rec.size());
        CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 0);
        CHECK(g_menu_given.size() == 2 && g_menu_given[0] == 1 && g_menu_given[1] == 0);
        const LibInfo *b = w.ld.info(w.ld.handle_of("bb"));
        CHECK(b && b->menu == 0);
    }
    // ---- (2) a library that fails to register: the loader's sentence, on the log, in the screen's list and in the summary; the other still links
    {
        World w;
        Bytes rec = record7(two);
        w.cor(rec);
        w.libs(true, true);
        g_fail_rc = 5;
        w.run((uint32_t)rec.size());
        CHECK_EQ(w.r.loaded, 1); CHECK_EQ(w.r.refused, 1);
        CHECK(w.log.find("[lib] bb (linked) : refused: Library \"bb\": lib_register failed (5).") >= 0);
        CHECK(w.log.find("[lib] aa (linked) : ok") >= 0);
        CHECK(w.r.nkept == 1 && !strcmp(w.r.kept[0], "Library \"bb\": lib_register failed (5)."));
        CHECK_STR(w.r.summary, "1 loaded, 1 refused: bb");
        CHECK_EQ(w.ld.count(), 1);                                                         // nothing of bb stays
        CHECK_EQ(w.ld.handle_of("bb"), -1);
    }
    // ---- (2) a menu record no library takes is a refusal
    {
        World w;
        std::vector<Entry> es = two;
        es.push_back(Entry{ "zz", gamma });
        Bytes rec = record7(es);
        w.cor(rec);
        w.libs(true, true);
        w.run((uint32_t)rec.size());
        CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 1);
        CHECK(w.log.find("The core bundle carries a menu record for \"zz\", which this app does not link with `register`.") >= 0);
        CHECK(w.r.nkept == 1 && strstr(w.r.kept[0], "\"zz\""));
        CHECK_STR(w.r.summary, "2 loaded, 1 refused: zz");
        // two libraries of the record, only one linked
        World v;
        v.cor(rec);
        v.libs(false, true);
        v.run((uint32_t)rec.size());
        CHECK_EQ(v.r.loaded, 1); CHECK_EQ(v.r.refused, 2);
        CHECK_STR(v.r.summary, "1 loaded, 2 refused: aa");
    }
    // ---- (2) a damaged record, a version 1 record, a record that cannot be read: told, the libraries still register without a menu record
    {
        Bytes damaged = record7(two, 2, 3), v1 = record7(two, 1);
        const Bytes *recs[2] = { &damaged, &v1 };
        for (int i = 0; i < 2; i++) {
            World w;
            w.cor(*recs[i]);
            w.libs(true, true);
            w.run((uint32_t)recs[i]->size());
            CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 1);
            CHECK(w.log.find(std::string("[lib] ") + COR + ": ") >= 0);
            CHECK(w.r.nkept == 1 && strstr(w.r.kept[0], i ? "base 4.0 loads no library file" : "damaged"));
            CHECK_STR(w.r.summary, "2 loaded, 1 refused: bundle");
            CHECK(g_menu_given.size() == 2 && g_menu_given[0] == 0 && g_menu_given[1] == 0);
            CHECK(w.ld.info(w.ld.handle_of("aa")) && w.ld.info(w.ld.handle_of("aa"))->menu == 0);
        }
        World g;                                                                           // the .cor is not there at all
        g.libs(true, false);
        g.run(200);
        CHECK_EQ(g.r.loaded, 1); CHECK_EQ(g.r.refused, 1);
        CHECK(g.log.find("cannot be read") >= 0);
    }
    // ---- (2) no record: nothing refused, before_first still runs before the first registration; no library linked: it does not run
    {
        World w;
        w.libs(true, true);
        w.run(0);
        CHECK_EQ(w.r.loaded, 2); CHECK_EQ(w.r.refused, 0);
        CHECK_STR(w.r.summary, "2 loaded");
        CHECK_EQ(g_before, 1); CHECK_EQ(g_before_at, 0);
        CHECK(w.ld.info(w.ld.handle_of("bb")) && w.ld.info(w.ld.handle_of("bb"))->menu == 0);
        World n;
        n.run(0);
        CHECK_EQ(n.r.loaded + n.r.refused, 0);
        CHECK_STR(n.r.summary, "none");
        CHECK(n.log.lines.empty());
        CHECK_EQ(g_before, 0);
        World x;                                                                           // no link step at all
        lib_link_cor(&x.ld, &x.fs, COR, PREFIX, 0, 0, &x.log, &x.r, 0, 0);
        CHECK_STR(x.r.summary, "none");
    }
    DONE("test_libscan");
}
