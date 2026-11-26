// T104.1 D9, D12, Q5: what a .cor carries beyond the partial and the application. Bundles are made by the real packer (tests/coreapp/gen_bundle.py):
// the description (record 6) is found, hashed, parsed and merged into the menu; record 7 (base 4.0, version 2) holds the menu records of the linked libraries by name:
// lib_link_cor hands each to the library of that name as it registers (a fake link step stands for libtab_link), a record no library takes is refused, and a library whose menu
// the packer merged into the description is not added a second time; a bundle without either record is read as it always was; a bundle that is another core's, for another
// host, or damaged, is told by name and never stops the start.
//
//   test_bundle DIR       (DIR holds new.cor old.cor other.cor large.cor, made by gen_bundle.py)
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "cores.h"
#include "ops_glue.h"
#include "fakes.h"
#include "test_core.h"
#include "bundle.h"
#include "instfiles.h"
#include "ops_glue.h"
#include "menutree.h"

const CoreDesc *const core_table[] = { &testcore::desc, 0 };

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

static std::string slurp_text(const std::string &p) { Bytes b = slurp(p); return std::string(b.begin(), b.end()); }

// a library's registration, as the register function of a linked library makes it
static int op_nop(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
static int g_alpha_calls;
static int op_alpha(CoreCtx *, const OpArgs &, OpOut &) { g_alpha_calls++; return OPE_OK; }
static const ArgSpec bus_arg[] = { ARG_INT("value", 8, 11) };
static const ArgSpec shape_arg[] = { ARG_INT("value", 0, 2) };
static const LibOpDecl alpha_ops[] = { { "alpha.go", 0, 0, op_alpha } };
static const LibNodeDecl alpha_nodes[] = { { "F5/tools", LN_OP, "Alpha go", "alpha.go", 0, 3 } };
static int g_gamma_calls;
static int op_gamma(CoreCtx *, const OpArgs &, OpOut &) { g_gamma_calls++; return OPE_OK; }
static const ArgSpec path_arg[] = { ARG_PATH("path") };
static const LibOpDecl gamma_ops[] = { { "gamma.go", 0, 0, op_gamma }, { "gamma.play", path_arg, 1, op_nop } };
static const LibOpDecl beta_ops[] = { { "beta.go", 0, 0, op_nop } };
static const LibNodeDecl beta_nodes[] = { { "F5/tools", LN_OP, "Beta go", "beta.go", 0, 1 } };

static std::vector<std::string> g_registered;
static int reg_alpha(const LibApi *, LibRecord *rec)
{
    g_registered.push_back("alpha");
    rec->ops = alpha_ops; rec->nops = 1;
    rec->nodes = alpha_nodes; rec->nnodes = 1;
    return 0;
}
static int reg_gamma(const LibApi *, LibRecord *rec)
{
    g_registered.push_back("gamma");
    rec->ops = gamma_ops; rec->nops = 2;                       // no nodes of its own: its menu is the record of the bundle (menu_gamma.toml)
    return 0;
}
static int reg_beta(const LibApi *, LibRecord *rec)
{
    g_registered.push_back("beta");
    rec->ops = beta_ops; rec->nops = 1;
    rec->nodes = beta_nodes; rec->nnodes = 1;
    return 0;
}

// The fake link step (libtab_link on the board): the libraries of the test's TOML B in this order, each asked for its menu record, linked, and reported.
struct LinkItem {
    const char *name;
    int (*reg)(const LibApi *, LibRecord *);
    const char *menu_file;                                      // a menu record the step hands over itself, bypassing record 7 (null: whatever record 7 gives)
};
static std::vector<LinkItem> g_items;
static std::string g_dir;
static std::string slurp_s(const std::string &p);
static void fake_link_all(LibLoader &ld, bool (*menu)(void *, const char *, DescRecord **, uint8_t **), void (*done)(void *, const char *, int, const char *), void *user)
{
    for (size_t i = 0; i < g_items.size(); i++) {
        DescRecord *rec = 0;
        uint8_t *bytes = 0;
        bool have = menu(user, g_items[i].name, &rec, &bytes);
        if (g_items[i].menu_file && !have) {
            std::string b = slurp_s(g_dir + "/" + g_items[i].menu_file);
            bytes = new uint8_t[b.size()];
            memcpy(bytes, b.data(), b.size());
            rec = new DescRecord;
            char why[200];
            CHECK_EQ(descrec_parse(bytes, (uint32_t)b.size(), rec, why, sizeof(why)), 0);
        }
        char err[LIB_ERR_MAX] = "";
        int rc = ld.link(g_items[i].name, g_items[i].reg, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE, rec, bytes, err, sizeof(err));
        done(user, g_items[i].name, rc, err);
    }
}
static std::string slurp_s(const std::string &p) { Bytes b = slurp(p); return std::string(b.begin(), b.end()); }
static void link_set(const char *what)
{
    g_items.clear();
    g_registered.clear();
    if (!strcmp(what, "ab")) {                                  // beta needs alpha: the TOML B's link order puts alpha first
        g_items.push_back(LinkItem{ "alpha", reg_alpha, 0 });
        g_items.push_back(LinkItem{ "beta", reg_beta, 0 });
    } else if (!strcmp(what, "gamma")) {
        g_items.push_back(LinkItem{ "gamma", reg_gamma, 0 });
    } else if (!strcmp(what, "gamma_menu")) {                   // gamma linked with its menu record although the bundle's description merged it
        g_items.push_back(LinkItem{ "gamma", reg_gamma, "menu_gamma.bin" });
    }
}

struct Host : DescLibHost {
    std::vector<void *> live;
    explicit Host(const CoreDesc *d) : DescLibHost(d) { }
    ~Host() { for (size_t i = 0; i < live.size(); i++) free(live[i]); }
    void *alloc(uint32_t n) override { void *p = malloc(n); live.push_back(p); return p; }
    void release(void *p) override
    {
        for (size_t i = 0; i < live.size(); i++) { if (live[i] == p) { live.erase(live.begin() + i); free(p); return; } }
    }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }     // nothing is loaded from a file
};

struct Log : LibLog {
    std::vector<std::string> lines;
    void line(const char *t) override { lines.push_back(t); }
    bool has(const std::string &part) const
    {
        for (size_t i = 0; i < lines.size(); i++) { if (lines[i].find(part) != std::string::npos) return true; }
        return false;
    }
};

static void ctx_for(CoreCtx *c, FakeFs *fs, FakeIo *io, const char *cor, uint32_t host_key = 0x820eded2)
{
    memset(c, 0, sizeof(*c));
    c->desc = &testcore::desc;
    c->fs = fs;
    c->io = io;
    c->host_key = host_key;
    snprintf(c->cor_path, sizeof(c->cor_path), "%s", cor);
}

static void free_extras(BundleExtras *x) { delete x->desc; delete[] x->desc_bytes; }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);                                 // a mutant may crash: what it said before has to be out
    CHECK(argc == 2);
    std::string dir = argv[1];
    g_dir = dir;
    FakeFs fs;
    FakeIo io;
    fs.put("/usb0/cores/new.cor", slurp(dir + "/new.cor"));
    fs.put("/usb0/cores/old.cor", slurp(dir + "/old.cor"));
    fs.put("/usb0/cores/other.cor", slurp(dir + "/other.cor"));
    fs.put("/usb0/cores/large.cor", slurp(dir + "/large.cor"));
    fs.put("/usb0/cores/merged.cor", slurp(dir + "/merged.cor"));

    // ---- a bundle with a description and two libraries
    {
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/usb0/cores/new.cor");
        BundleExtras x;
        bundle_read(&c, &x);
        CHECK(x.probed);
        CHECK_STR(x.why, "");
        CHECK(x.desc != 0);
        CHECK(x.ci.size[COR_PART_LIBS] > 0);
        CHECK(x.ci.size[COR_PART_DESC] > 0);
        if (x.desc) {
            CHECK(x.desc->has_route);
            CHECK_EQ(x.desc->rec.nnodes, 4);
            CHECK_STR(x.desc->settings[0].key, "Drive Bus ID");
        }
        // the libraries, linked in the order of the TOML B (alpha first: beta needs it); record 7 carries alpha's menu record only
        Host host(&testcore::desc);
        OpRegistry *reg = ops_of(&c);
        reg->contributor("zz");
        reg->add_op("test.set_bus", bus_arg, 1, op_nop);               // what the description's two settings apply
        reg->add_op("test.set_shape", shape_arg, 1, op_nop);
        LibLoader ld(&host, reg);
        Log log;
        LibScanResult r;
        link_set("ab");
        lib_link_cor(&ld, &fs, c.cor_path, x.ci.off[COR_PART_LIBS], x.ci.size[COR_PART_LIBS], fake_link_all, &log, &r, 0, 0);
        CHECK_EQ(r.loaded, 2);
        CHECK_EQ(r.refused, 0);
        CHECK_EQ(g_registered.size(), 2u);
        if (g_registered.size() == 2) {
            CHECK_STR(g_registered[0].c_str(), "alpha");
            CHECK_STR(g_registered[1].c_str(), "beta");
        }
        CHECK(log.has("[lib] alpha (linked) : ok") && log.has("[lib] beta (linked) : ok"));
        const LibInfo *ai = ld.info(ld.handle_of("alpha")), *bi = ld.info(ld.handle_of("beta"));
        CHECK(ai && ai->menu != 0 && bi && bi->menu == 0);          // alpha got its record from record 7, beta has none
        CHECK(reg->find_op("alpha.go") != 0 && reg->find_op("beta.go") != 0);
        // ... and the menu merged from the description and the two libraries
        CoreDesc merged;
        static MenuMerge mm;
        struct Say : LibLog { Log *l; void line(const char *t) override { l->line(t); } } say;
        say.l = &log;
        c.desc = &testcore::desc;
        int refused = bundle_merge(&c, &testcore::desc, &x, &ld, &say, &merged, &mm);
        for (int i = 0; i < mm.nerr; i++) printf("refused: %s\n", mm.err[i]);
        CHECK_EQ(refused, 0);
        c.desc = &merged;
        c.merge = &mm;
        MenuTree t;
        menu_build(&merged, &t, true, false, &mm);
        static char buf[16384];
        menu_dump(&merged, &t, buf, sizeof(buf));
        std::string dump = buf;
        CHECK(dump.find("tab 3 Tools") != std::string::npos);
        CHECK(dump.find("tab 4 Drives") != std::string::npos);
        // Tools: the description's nodes, then the libraries' by order key: Beta go (1) before Alpha go (3), the description's own rank first
        size_t run = dump.find("node Run self test"), slot = dump.find("node Slot"), bg = dump.find("node Beta go"), ag = dump.find("node Alpha go");
        CHECK(run != std::string::npos && slot != std::string::npos && bg != std::string::npos && ag != std::string::npos);
        CHECK(run < slot && slot < bg && bg < ag);
        // the library's own TOML record is merged beside its C tables: "Alpha menu" has the key 2 (between Beta go 1 and Alpha go 3)
        size_t am = dump.find("node Alpha menu");
        CHECK(am != std::string::npos && bg < am && am < ag);
        CHECK_EQ(mm.npolls, 2);                                    // the description's poll and alpha's
        bool alpha_poll = false;
        for (int i = 0; i < mm.npolls; i++) alpha_poll |= !strcmp(mm.polls[i].owner, "alpha") && mm.polls[i].period_ms == 100;
        CHECK(alpha_poll);
        CHECK(dump.find("setting Drive Bus ID = 8 | 9 | 10 | 11 (default 9") != std::string::npos);
        // the node works: its operation is the library's
        g_alpha_calls = 0;
        OpResult res;
        CHECK_EQ(ops_call(&c, "alpha.go", 0, 0, &res), OPE_OK);
        CHECK_EQ(g_alpha_calls, 1);
        free_extras(&x);
    }

    // ---- a menu record of the bundle that no linked library takes is a refusal (cor_pack packs a record only for a library of the TOML B it was given: another app's bundle)
    {
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/usb0/cores/new.cor");
        BundleExtras x;
        bundle_read(&c, &x);
        Host host(&testcore::desc);
        LibLoader ld(&host, ops_of(&c));
        Log log;
        LibScanResult r1;
        g_items.clear();
        g_registered.clear();
        g_items.push_back(LinkItem{ "beta", reg_beta, 0 });          // alpha is not linked here
        lib_link_cor(&ld, &fs, c.cor_path, x.ci.off[COR_PART_LIBS], x.ci.size[COR_PART_LIBS], fake_link_all, &log, &r1, 0, 0);
        CHECK_EQ(r1.loaded, 1);
        CHECK_EQ(r1.refused, 1);
        CHECK(log.has("carries a menu record for \"alpha\", which this app does not link with `register`"));
        CHECK_EQ(ld.count(), 1);
        CHECK_STR(r1.summary, "1 loaded, 1 refused: alpha");
        free_extras(&x);
    }

    // ---- an old bundle: neither record, the menu it always gave
    {
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/usb0/cores/old.cor");
        BundleExtras x;
        bundle_read(&c, &x);
        CHECK(x.probed);
        CHECK(x.desc == 0);
        CHECK_STR(x.why, "");
        CHECK_EQ(x.ci.size[COR_PART_DESC], 0u);
        CHECK_EQ(x.ci.size[COR_PART_LIBS], 0u);
        Host host(&testcore::desc);
        LibLoader ld(&host, ops_of(&c));
        Log log;
        LibScanResult r;
        link_set("none");
        lib_link_cor(&ld, &fs, c.cor_path, x.ci.off[COR_PART_LIBS], x.ci.size[COR_PART_LIBS], fake_link_all, &log, &r, 0, 0);
        CHECK_EQ(r.loaded, 0); CHECK_EQ(r.refused, 0); CHECK(log.lines.empty());
        CoreDesc merged;
        static MenuMerge mm;
        CHECK_EQ(bundle_merge(&c, &testcore::desc, &x, 0, 0, &merged, &mm), 0);
        MenuTree a, b;
        menu_build(&testcore::desc, &a);
        menu_build(&merged, &b, true, false, &mm);
        static char da[8192], db[8192];
        menu_dump(&testcore::desc, &a, da, sizeof(da));
        menu_dump(&merged, &b, db, sizeof(db));
        CHECK(std::string(da) == db);
        free_extras(&x);
    }

    // ---- not this core's, not this host's, not a .cor, a large core
    {
        CoreCtx c;
        BundleExtras x;
        ctx_for(&c, &fs, &io, "/usb0/cores/other.cor");
        bundle_read(&c, &x);
        CHECK(!x.probed && x.desc == 0);
        CHECK(strstr(x.why, "another core's bundle") != 0);
        ctx_for(&c, &fs, &io, "/usb0/cores/new.cor", 0x11111111);
        bundle_read(&c, &x);
        CHECK(!x.probed && x.desc == 0);
        CHECK(strstr(x.why, "another host") != 0);
        ctx_for(&c, &fs, &io, "");
        bundle_read(&c, &x);
        CHECK(!x.probed && x.desc == 0 && x.why[0] == 0);
        ctx_for(&c, &fs, &io, "/usb0/cores/gone.cor");
        bundle_read(&c, &x);
        CHECK(!x.probed && strstr(x.why, "cannot be opened") != 0);
        ctx_for(&c, &fs, &io, "/usb0/cores/large.cor");
        bundle_read(&c, &x);
        CHECK(x.probed && x.desc != 0);                            // a large core carries the same records
        CHECK_STR(x.why, "");
        CHECK(x.ci.size[COR_PART_LIBS] > 0);
        free_extras(&x);
    }

    // ---- a damaged description: told, the core goes on with the menu it has
    {
        Bytes whole = slurp(dir + "/new.cor");
        CoreCtx c;
        BundleExtras x;
        ctx_for(&c, &fs, &io, "/usb0/cores/new.cor");
        bundle_read(&c, &x);
        uint32_t off = x.ci.off[COR_PART_DESC], lo = x.ci.off[COR_PART_LIBS];
        free_extras(&x);
        // a flipped byte: the hash says so
        Bytes bad = whole;
        bad[off + 40] ^= 0x55;
        fs.put("/usb0/cores/bad.cor", bad);
        ctx_for(&c, &fs, &io, "/usb0/cores/bad.cor");
        bundle_read(&c, &x);
        CHECK(x.probed && x.desc == 0);
        CHECK(strstr(x.why, "description record") != 0 && strstr(x.why, "SHA-256") != 0);
        // (a record whose hash is right and whose content is not is test_descrec's: the manifest's hash cannot follow a rewritten byte)
        // a damaged library record: no menu record of it is read, it is told, and the libraries still register without one. Its hash is the host's to check (cor_full_check), the structure is ours.
        Bytes bad2 = whole;
        bad2[lo + 8] = 2;                                          // two entries claimed, one there: the second runs past the end
        fs.put("/usb0/cores/bad2.cor", bad2);
        ctx_for(&c, &fs, &io, "/usb0/cores/bad2.cor");
        BundleExtras y;
        bundle_read(&c, &y);
        Host host(&testcore::desc);
        LibLoader ld(&host, ops_of(&c));
        Log log;
        LibScanResult r;
        link_set("ab");
        lib_link_cor(&ld, &fs, c.cor_path, y.ci.off[COR_PART_LIBS], y.ci.size[COR_PART_LIBS], fake_link_all, &log, &r, 0, 0);
        CHECK_EQ(r.loaded, 2);
        CHECK_EQ(r.refused, 1);
        CHECK(log.has("damaged"));
        const LibInfo *di = ld.info(ld.handle_of("alpha"));
        CHECK(di && di->menu == 0);                                // the record could not be read: alpha registered without its menu
        // record 7 as a version 1 packer wrote it (library files): refused with the base 4.0 sentence, nothing read from it
        Bytes v1 = slurp(dir + "/libs_v1.bin");
        fs.put("/usb0/cores/v1.bin", v1);
        char why[200];
        LibCorMenus cm;
        CHECK_EQ(lib_cor_menus(&fs, "/usb0/cores/v1.bin", 0, (uint32_t)v1.size(), &cm, why, sizeof(why)), -1);
        CHECK(strstr(why, "base 4.0 loads no library file") != 0);
        free_extras(&y);
    }

    // ---- T104.2 D10: a library whose menu the packer merged into the description is not added a second time at run time
    {
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/usb0/cores/merged.cor");
        BundleExtras x;
        bundle_read(&c, &x);
        CHECK(x.probed && x.desc != 0);
        CHECK_STR(x.why, "");
        CHECK(x.desc && x.desc->merged && !strcmp(x.desc->merged, "gamma"));
        CHECK(x.desc && descrec_merged(*x.desc, "gamma") && !descrec_merged(*x.desc, "gamm") && !descrec_merged(*x.desc, "alpha") && !descrec_merged(*x.desc, ""));
        CHECK(x.desc && x.desc->rec.nnodes == 5 && x.desc->rec.nfile_actions == 2);        // the description's four nodes and one file action, and gamma's
        Host host(&testcore::desc);
        OpRegistry *reg = ops_of(&c);
        reg->contributor("zz");
        reg->add_op("test.set_bus", bus_arg, 1, op_nop);
        reg->add_op("test.set_shape", shape_arg, 1, op_nop);
        LibLoader ld(&host, reg);
        Log log;
        LibScanResult r;
        CHECK_EQ(x.ci.size[COR_PART_LIBS], 0u);                    // the packer merged the only menu record: no record 7
        link_set("gamma_menu");                                    // gamma is linked with its menu record all the same (as a bundle of a packer that did not merge would give it)
        lib_link_cor(&ld, &fs, c.cor_path, x.ci.off[COR_PART_LIBS], x.ci.size[COR_PART_LIBS], fake_link_all, &log, &r, 0, 0);
        CHECK_EQ(r.loaded, 1);
        CHECK(ld.info(ld.handle_of("gamma")) && ld.info(ld.handle_of("gamma"))->menu != 0);
        CoreDesc merged;
        static MenuMerge mm;
        struct Say : LibLog { Log *l; void line(const char *t) override { l->line(t); } } say;
        say.l = &log;
        CHECK_EQ(bundle_merge(&c, &testcore::desc, &x, &ld, &say, &merged, &mm), 0);
        MenuTree t;
        menu_build(&merged, &t, true, false, &mm);
        static char buf[16384];
        menu_dump(&merged, &t, buf, sizeof(buf));
        std::string dump = buf;
        size_t g1 = dump.find("node Gamma menu");
        CHECK(g1 != std::string::npos && dump.find("node Gamma menu", g1 + 1) == std::string::npos);      // once, not twice
        CHECK(dump.find("filetype .gam: Play gamma") != std::string::npos);
        // the same bundle read with its record's list taken away shows the node twice: the linked library's own menu record adds it (the control of the skip)
        x.desc->merged = 0;
        CoreDesc merged2;
        static MenuMerge mm2;
        bundle_merge(&c, &testcore::desc, &x, &ld, &say, &merged2, &mm2);
        MenuTree t2;
        menu_build(&merged2, &t2, true, false, &mm2);
        static char buf2[16384];
        menu_dump(&merged2, &t2, buf2, sizeof(buf2));
        std::string dump2 = buf2;
        size_t h1 = dump2.find("node Gamma menu");
        CHECK(h1 != std::string::npos && dump2.find("node Gamma menu", h1 + 1) != std::string::npos);
        free_extras(&x);
    }

    // ---- T256 T-2 (D8): the elements of a core's UI TOML: the tool writes section 25, the board's reader parses it, bundle_merge puts it over the base's table
    {
        Bytes own = slurp(dir + "/desc_elements.bin");
        CoreDesc d = testcore::desc;
        d.menu_rec = own.data();
        d.menu_rec_n = (uint32_t)own.size();
        CoreCtx c;
        ctx_for(&c, &fs, &io, "");
        c.desc = &d;
        BundleExtras x;
        bundle_read(&c, &x);
        CHECK(x.desc != 0 && x.desc->has_elem);
        CHECK_EQ(x.desc->elem.n, 2);
        CoreDesc merged;
        static MenuMerge mm;
        memset(&mm, 0, sizeof(mm));
        CHECK_EQ(bundle_merge(&c, &d, &x, 0, 0, &merged, &mm), 0);
        CHECK(mm.has_elem);
        const UiElements &base = *menu_elem_base();
        CHECK_EQ(mm.elem_store.n, base.n + 1);                               // the base's elements (the still, the PAUSED tag) and the core's new one
        const UiElementRow *bar = 0, *paused = 0, *still = 0;
        for (int i = 0; i < mm.elem_store.n; i++) {
            const UiElementRow &e = mm.elem_store.rows[i];
            if (!strcmp(e.name, "bar")) bar = &e;
            if (!strcmp(e.name, "paused")) paused = &e;
            if (e.still) still = &e;
        }
        CHECK(bar != 0 && paused != 0 && still != 0);
        if (bar && paused && still) {
            CHECK_EQ(bar->anchor, OVA_BOTTOM_MID);
            CHECK(bar->dx == 0 && bar->dy == -20 && bar->w == 400 && bar->h == 40 && !bar->still);
            CHECK_EQ(paused->anchor, OVA_TOP_LEFT);                          // the core placed the base's element again
            CHECK(paused->dx == 10 && paused->dy == 12 && paused->w == 126 && paused->h == 30);
            CHECK_STR(still->name, "still");                                 // the base's still is kept
        }
        free_extras(&x);
    }

    // ---- T104.2 D10: the record the application carries serves a start without a .cor, and the .cor's own record wins over it
    {
        Bytes own = slurp(dir + "/desc_own.bin");
        CoreDesc d = testcore::desc;
        d.menu_rec = own.data();
        d.menu_rec_n = (uint32_t)own.size();
        CoreCtx c;
        ctx_for(&c, &fs, &io, "");
        c.desc = &d;
        BundleExtras x;
        bundle_read(&c, &x);
        CHECK(!x.probed && x.desc != 0 && x.desc->has_own);                  // no .cor: the built-in record
        CoreDesc merged;
        static MenuMerge mm;
        Log log;
        struct Say : LibLog { Log *l; void line(const char *t) override { l->line(t); } } say;
        say.l = &log;
        CHECK_EQ(bundle_merge(&c, &d, &x, 0, &say, &merged, &mm), 0);
        CHECK_EQ(merged.ntasks, 17);                                         // the test core's own tables are replaced by the record's (golden/desc_own.toml: 17 tasks)
        CHECK_EQ(merged.nfiletypes, 20);                                     // all of the record's file types (CONTRIB_FILETYPES_MAX 40; it was cut at 16)
        CHECK_STR(merged.tasks[0].label, "Reset");
        CHECK_STR(merged.media[1].name, "Cartridge port");
        free_extras(&x);
        // a .cor that has its own record: that one, the built-in one is not looked at
        ctx_for(&c, &fs, &io, "/usb0/cores/new.cor");
        c.desc = &d;
        BundleExtras y;
        bundle_read(&c, &y);
        CHECK(y.probed && y.desc != 0 && !y.desc->has_own && y.desc->rec.nnodes == 4);
        free_extras(&y);
        // a .cor without a record: the built-in one
        ctx_for(&c, &fs, &io, "/usb0/cores/old.cor");
        c.desc = &d;
        BundleExtras z;
        bundle_read(&c, &z);
        CHECK(z.probed && z.desc != 0 && z.desc->has_own);
        free_extras(&z);
        // without a record in the application: nothing, as before
        ctx_for(&c, &fs, &io, "");
        BundleExtras w;
        bundle_read(&c, &w);
        CHECK(!w.probed && w.desc == 0);
    }

    // ---- T175, record 8: the files a core installs on the card (the fixture core instfiles carries files, T260 D6: the diag core carries none)
    {
        fs.put("/usb0/cores/instfiles.cor", slurp(dir + "/instfiles.cor"));
        fs.put("/usb0/cores/nofiles.cor", slurp(dir + "/nofiles.cor"));
        CoreDesc dd = testcore::desc;
        dd.cor_name = "Install Files";
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/usb0/cores/instfiles.cor");
        c.desc = &dd;
        BundleExtras x;
        bundle_read(&c, &x);
        CHECK(x.probed && x.ci.size[COR_PART_FILES] > 0);                       // the host's reader sees record 8
        free_extras(&x);
        // an unedited text of an earlier version is replaced, an edited one is not, a file the person made stays, the rest is written
        fs.put_text("/usb0/cores/instfiles/tests/cd32.toml", slurp_text("tests/coreapp/instfiles/old/cd32.v1.toml"));
        fs.put_text("/usb0/cores/instfiles/tests/stick2.toml", slurp_text("tests/coreapp/instfiles/old/stick2.v1.toml") + "# mine\n");
        fs.put_text("/usb0/cores/instfiles/tests/pins.toml", "# mine\n");
        fs.put_text("/usb0/cores/instfiles/tests/mytest.toml", "# mine too\n");
        CHECK_EQ(core_files_install(&c), 3);                      // 5 files: cd32 replaced + paddle and joystick missing written; stick2 and pins edited, kept
        CHECK_STR(fs.text("/usb0/cores/instfiles/tests/cd32.toml").c_str(), slurp_text("tests/coreapp/instfiles/sd/tests/cd32.toml").c_str());
        CHECK_STR(fs.text("/usb0/cores/instfiles/tests/pins.toml").c_str(), "# mine\n");
        CHECK(fs.text("/usb0/cores/instfiles/tests/stick2.toml").find("# mine") != std::string::npos);
        CHECK_STR(fs.text("/usb0/cores/instfiles/tests/mytest.toml").c_str(), "# mine too\n");
        CHECK_STR(fs.text("/usb0/cores/instfiles/tests/paddle.toml").c_str(), slurp_text("tests/coreapp/instfiles/sd/tests/paddle.toml").c_str());
        CHECK_EQ(core_files_install(&c), 0);                      // the second start writes nothing
        // a card that cannot be written says so
        fs.remove("/usb0/cores/instfiles/tests/paddle.toml");
        fs.read_only = true;
        CHECK_EQ(core_files_install(&c), -1);
        fs.read_only = false;
        // a bundle without the record, and another core's bundle, install nothing
        ctx_for(&c, &fs, &io, "/usb0/cores/nofiles.cor");
        c.desc = &dd;
        CHECK_EQ(core_files_install(&c), 0);
        ctx_for(&c, &fs, &io, "/usb0/cores/instfiles.cor");            // the Test Machine's descriptor with the fixture core's bundle
        size_t before = fs.writes;
        CHECK_EQ(core_files_install(&c), 0);
        CHECK_EQ((size_t)fs.writes, before);
        ctx_for(&c, &fs, &io, "/usb0/cores/new.cor");             // the layer-2 core of the other tests: no record 8
        CHECK_EQ(core_files_install(&c), 0);
        ctx_for(&c, &fs, &io, "");                                // no bundle
        CHECK_EQ(core_files_install(&c), 0);
        // a damaged record is refused and writes nothing: one flipped byte inside record 8 breaks its SHA-256
        std::vector<uint8_t> bad = slurp(dir + "/instfiles.cor");
        fs.put("/usb0/cores/bad.cor", bad);
        {
            CoreCtx cb;
            ctx_for(&cb, &fs, &io, "/usb0/cores/bad.cor");
            cb.desc = &dd;
            BundleExtras xb;
            bundle_read(&cb, &xb);
            bad[xb.ci.off[COR_PART_FILES] + xb.ci.size[COR_PART_FILES] - 8] ^= 1;
            free_extras(&xb);
            fs.put("/usb0/cores/bad.cor", bad);
            fs.remove("/usb0/cores/bad/tests/paddle.toml");
            CHECK_EQ(core_files_install(&cb), -1);
            CHECK(!fs.has("/usb0/cores/bad/tests/paddle.toml"));
        }
    }

    DONE("test_bundle");
}
