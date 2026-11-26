// Base 4.0 (T239.3 D58): libraries linked into the app through its TOML B with `register`, through LibLoader::link and libtab_link. The table is made by the real generator
// (tools/libtab.py table tests/loader/libs_register.toml); the register functions are native, as testlib.cc's lib_register is. One libtab_link runs the five libraries; each scenario
// reads what that run left. A cycle of `needs` is refused where the table is made (tools/libtab.py link_order, LinkOrderTests in tools/test_libtab.py), not here.
//   (a) the operations and the state value of a library are registered, callable and readable
//   (b) an export a library delivers in export_list lands in g_libslot_bound, and a library registered after it reaches it with lib_slot while it registers
//   (c) the menu record handed to the loader ends up in LibInfo::menu
//   (d) heap_keep over the room of the host: LIB_NO_ROOM, lib_register never runs; an allocation over heap_peak while it registers: LIB_BUDGET; nothing stays registered
//   (e) the library that needs another is registered after it, whatever their order in the file
//   (g) an export the TOML B declares and lib_register does not return refuses the library, naming both
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "check.h"
#include "libload.h"
#include "libtab.h"
#include "libslots.h"
#include "libmenu.h"
#include "descrec.h"
#include "ctx.h"

#ifndef FIXDIR
#error FIXDIR is the directory of menu_lib.bin (tests/loader/run.sh makes it)
#endif

void ops_ask_list(CoreCtx *, const char *, int, const char *, const char *) { }   // libsvc.cc raises the menu's choice list; no menu here

struct Host : LibHost {
    uint32_t room_v = 0xFFFFFFFFu;
    void *alloc(uint32_t n) override { return malloc(n); }
    void release(void *p) override { free(p); }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "Test Core"; }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 99; }
    uint32_t room() override { return room_v; }
};

struct MemSource : LibSource {
    std::vector<uint8_t> d;
    uint32_t size() override { return (uint32_t)d.size(); }
    bool read(uint32_t off, void *dst, uint32_t n) override
    {
        if (off > d.size() || n > d.size() - off) { return false; }
        memcpy(dst, d.data() + off, n);
        return true;
    }
};

static bool read_file(const char *name, std::vector<uint8_t> *out)
{
    std::string p = std::string(FIXDIR) + "/" + name;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) { return false; }
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) { out->insert(out->end(), buf, buf + n); }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------- the libraries

struct Tab { uint32_t size; uint32_t member; };
static Tab g_ring_tab = { sizeof(Tab), 1 };
static Tab g_two_tab = { sizeof(Tab), 2 };
static std::string g_order;                          // the register functions that ran, in order
static const void *g_seen_ring;                      // what regtwo's lib_slot(LIBSLOT_ring) gave while it registered
static uint32_t g_seen_size;
static bool g_big_called;
static std::vector<void *> g_peaky_blocks;

static int one_op(CoreCtx *, const OpArgs &, OpOut &o) { snprintf(o.msg, o.msglen, "one"); return OPE_OK; }
static const char *one_state(CoreCtx *) { return "state of one"; }
static const LibOpDecl g_one_ops[] = { { "reg.one", 0, 0, one_op } };
static const LibStateDecl g_one_states[] = { { "reg.state", one_state } };
static LibExportDecl g_one_exp[] = { { "ring", 1, &g_ring_tab, sizeof(Tab) } };
static LibExportDecl g_two_exp[] = { { "tworing", 1, &g_two_tab, sizeof(Tab) } };
static const LibOpDecl g_peaky_ops[] = { { "peaky.op", 0, 0, one_op } };

int regone_register(const LibApi *, LibRecord *out)
{
    g_order += "regone;";
    out->ops = g_one_ops; out->nops = 1;
    out->states = g_one_states; out->nstates = 1;
    out->export_list = g_one_exp; out->nexport_list = 1;
    return 0;
}
int regtwo_register(const LibApi *, LibRecord *out)
{
    g_order += "regtwo;";
    g_seen_ring = lib_slot(LIBSLOT_ring, &g_seen_size);
    out->export_list = g_two_exp; out->nexport_list = 1;
    return 0;
}
int nodeliver_register(const LibApi *, LibRecord *)
{
    g_order += "nodeliver;";
    return 0;                                         // declares "lost" in the TOML B and returns no export_list
}
int bigkeep_register(const LibApi *, LibRecord *)
{
    g_big_called = true;
    return 0;
}
int peaky_register(const LibApi *api, LibRecord *out)
{
    g_order += "peaky;";
    g_peaky_blocks.push_back(api->mem->malloc(200));  // over its peak of 100
    out->ops = g_peaky_ops; out->nops = 1;
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------- one libtab_link run

struct Done { std::string name; int rc; std::string err; };
static std::vector<Done> g_done;
static std::vector<std::string> g_menu_asked;

static bool menu_cb(void *, const char *name, DescRecord **rec, uint8_t **bytes)
{
    g_menu_asked.push_back(name);
    if (strcmp(name, "regone")) {
        return false;
    }
    MemSource s;
    CHECK(read_file("menu_lib.bin", &s.d));
    char why[160] = "";
    CHECK_EQ(libmenu_read(&s, s.size(), 0, rec, bytes, why, sizeof(why), false), 0);
    return *rec != 0;
}
static void done_cb(void *, const char *name, int rc, const char *err)
{
    Done d = { name, rc, err };
    g_done.push_back(d);
}
static const Done *done_of(const char *name)
{
    for (size_t i = 0; i < g_done.size(); i++) {
        if (g_done[i].name == name) { return &g_done[i]; }
    }
    return 0;
}

static Host g_host;
static OpRegistry g_reg;
static LibLoader *g_ld;

static void link_all()
{
    g_host.room_v = 4000;                                                           // bigkeep keeps 5000
    g_ld = new LibLoader(&g_host, &g_reg);
    CHECK(libtab_export("ring", 1, 0) == 0 && libtab_export("tworing", 1, 0) == 0);   // nothing delivered before the run
    libtab_link(*g_ld, menu_cb, done_cb, 0);
}

// (e) the order: the table lists regtwo first, regone after it; regtwo needs regone
static void order()
{
    CHECK_EQ((int)g_libtab_n, 5);
    CHECK_EQ((int)g_libtab_link_n, 5);
    CHECK_STR(g_libtab[0].name, "regtwo");
    CHECK_STR(g_libtab[1].name, "regone");
    CHECK_EQ((int)g_libtab_link_order[0], 1);                                       // regone, then
    CHECK_EQ((int)g_libtab_link_order[1], 0);                                       // regtwo
    CHECK_EQ((int)g_done.size(), 5);
    CHECK(g_done.size() == 5 && g_done[0].name == "regone" && g_done[1].name == "regtwo");
    CHECK(g_order.compare(0, 14, "regone;regtwo;") == 0);                           // the functions ran in that order
    CHECK(g_menu_asked.size() == 5 && g_menu_asked[0] == "regone");
}

// (a) operations and a state value
static void ops_and_states()
{
    const Done *d = done_of("regone");
    CHECK(d && d->rc == LIB_OK);
    const OpDef *op = g_reg.find_op("reg.one");
    CHECK(op && !strcmp(op->owner, "regone"));
    OpResult r;
    CHECK_EQ(g_reg.call(0, "reg.one", 0, 0, &r), OPE_OK);
    CHECK_STR(r.msg, "one");
    CHECK(g_reg.find_state("reg.state") != 0);
    CHECK_STR(g_reg.state(0, "reg.state", 0), "state of one");
    CHECK(g_ld->handle_of("regone") >= 0 && g_ld->handle_of("regtwo") >= 0);
    CHECK_EQ(g_ld->count(), 2);                                                      // regone and regtwo; the other three were refused
}

// (b) an export in export_list is written into g_libslot_bound, and a library registered after it reaches it
static void exports_across()
{
    uint32_t sz = 0;
    CHECK(g_libslot_bound[LIBSLOT_ring] == &g_ring_tab);
    CHECK(g_libslot_bound[LIBSLOT_tworing] == &g_two_tab);
    CHECK(lib_slot(LIBSLOT_ring, &sz) == &g_ring_tab && sz == sizeof(Tab));
    CHECK(libtab_export("tworing", 1, &sz) == &g_two_tab && sz == sizeof(Tab));
    CHECK(LibLoader::find_export("ring", U2A3_VERSION(1, 0), &sz) == &g_ring_tab);
    CHECK(libtab_export("ring", 2, &sz) == 0 && sz == 0);                            // the major is exact
    CHECK(g_seen_ring == &g_ring_tab && g_seen_size == sizeof(Tab));                // regtwo saw it while it registered
    CHECK_EQ(g_ld->handle_providing("ring", U2A3_VERSION(1, 0)), g_ld->handle_of("regone"));
    CHECK_EQ(g_ld->handle_providing("tworing", U2A3_VERSION(1, 0)), g_ld->handle_of("regtwo"));
}

// (c) the menu record
static void menu_record()
{
    const LibInfo *i = g_ld->info(g_ld->handle_of("regone"));
    CHECK(i && i->menu != 0);
    if (i && i->menu) {
        CHECK_EQ(i->menu->rec.nnodes, 1);
        CHECK_STR(i->menu->nodes[0].label, "Pulse");
        CHECK_EQ(i->menu->rec.nsettings, 1);
        CHECK_STR(i->menu->rec.poll.op, "test.poll");
    }
    const LibInfo *t = g_ld->info(g_ld->handle_of("regtwo"));
    CHECK(t && t->menu == 0);
}

// (d) the budgets
static void budgets()
{
    const Done *k = done_of("bigkeep");
    CHECK(k && k->rc == LIB_NO_ROOM);
    CHECK(k && strstr(k->err.c_str(), "bigkeep") && strstr(k->err.c_str(), "keeps 5000 bytes") && strstr(k->err.c_str(), "room for 4000"));
    CHECK(!g_big_called);
    CHECK_EQ(g_ld->handle_of("bigkeep"), -1);
    const Done *p = done_of("peaky");
    CHECK(p && p->rc == LIB_BUDGET);
    CHECK(p && strstr(p->err.c_str(), "peaky") && strstr(p->err.c_str(), "200 bytes") && strstr(p->err.c_str(), "heap peak of 100"));
    CHECK_EQ((int)g_peaky_blocks.size(), 1);
    CHECK(g_peaky_blocks.size() == 1 && g_peaky_blocks[0] == 0);                    // the request was refused: a null pointer
    CHECK_EQ(g_ld->handle_of("peaky"), -1);
    CHECK(g_reg.find_op("peaky.op") == 0);                                           // nothing it brought stayed
    CHECK_EQ(g_ld->account_blocks(-1), 0);
}

// (g) a declared export the library does not deliver
static void undelivered()
{
    const Done *d = done_of("nodeliver");
    CHECK(d && d->rc == LIB_REGISTER);
    CHECK(d && strstr(d->err.c_str(), "nodeliver") && strstr(d->err.c_str(), "\"lost\" 1") && strstr(d->err.c_str(), "does not return it"));
    CHECK_EQ(g_ld->handle_of("nodeliver"), -1);
    CHECK(g_libslot_bound[LIBSLOT_lost] == 0);
    CHECK(libtab_export("lost", 1, 0) == 0);
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    link_all();
    order();
    ops_and_states();
    exports_across();
    menu_record();
    budgets();
    undelivered();
    delete g_ld;
    DONE("test_register");
}
