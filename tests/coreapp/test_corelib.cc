// T239.3 base 3.3, a core whose code is a library (core_hooks.h, docs/dev/boundary-trx64u-handover.md "The core's code in its library"). The record names the library (section 1, hooks);
// the base takes the table "corehooks" of THAT library, at exact major 1, from the real loader, and calls the members where boot.cc and engines.cc call the descriptor's. The library is a
// model: native code behind a real .u2a file (the file lists the export; no RISC-V instruction runs), so the loader, the services of api (io, settings, ctx), the account of the library
// and the base's own paths (boot_start, boot_cold_reset, boot_warm_reset, boot_restart, mounts_restore, mounts_pass) are the real ones.
//   (1) the record: tools/descrec.py writes `hooks`, descrec.cc reads it with the same limits, descrec_core accepts the [mount] section and leaves the control hooks to the library;
//   (2) the first reset (boot_start, after the libraries are loaded): init sees api->io and the settings group filled; the hooks run in the order of the descriptor's; a hook runs on the
//       library's account; (3) the mount slots: the spec buffer is the base's, 0 is empty, -1 is refused, a restore sets CTX_MEDIA_RESTORING, a self change is notified and stored,
//       media_source answers, eject empties; (4) a missing library, a library without the export, the export at another major.
//
//   test_corelib FIXTURE_DIR
#include <string>
#include <strings.h>
#include <vector>
#include <stdio.h>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "libload.h"
#include "libapi.h"
#include "descrec.h"
#include "core_hooks.h"
#include "hookhost.h"
#include "hooklib.h"
#include "mediamounts.h"
#include "svc_ctx.h"
#include "svc_settings.h"
#include "ops.h"
#include "exec.h"

static std::string g_dir;

// ---- the model library: what its hooks saw, in order
static std::vector<std::string> g_log;
static const LibApi *g_api;
static LibLoader *g_ld;
static int g_handle = -1;
static bool g_init_io, g_init_settings, g_init_persist;
static uint32_t g_init_state;
static int g_blocks_in_init = -1;
static std::string g_slot[2];                       // what the engine holds in D1 and Cart
static bool g_pending, g_flush_fail, g_refuse_next;
static uint32_t g_state_at_mount;
static const char *const g_names[2] = { "D1", "Cart" };

static int h_init(char *msg, int n)
{
    g_log.push_back("init");
    g_init_io = g_api->io != 0 && g_api->io->rd != 0;
    g_init_settings = g_api->settings != 0 && g_api->settings->find != 0;
    g_init_persist = g_init_settings && g_api->settings->find(PERSIST_KEY) >= 0;
    g_init_state = g_api->ctx ? g_api->ctx->state() : 0;
    void *p = g_api->mem->malloc(24);                  // goes to the library's account while the hook runs (LibLoader::Scope); the model keeps it
    (void)p;
    g_blocks_in_init = g_ld->account_blocks(g_handle);
    (void)msg; (void)n;
    return 0;
}
static int h_release(char *, int) { g_log.push_back("release"); return 0; }
static int h_warm(char *, int) { g_log.push_back("warm_reset"); return 0; }
static int h_cold(char *msg, int n)
{
    g_log.push_back("cold_start");
    snprintf(msg, n, "ignored when it is done");
    return 0;
}
static int h_flush(char *msg, int n)
{
    g_log.push_back("media_flush");
    if (g_flush_fail) {
        snprintf(msg, n, "the card refuses the write-back");
        return 1;
    }
    g_pending = false;
    return 0;
}
static bool h_pending(void) { g_log.push_back("media_pending"); return g_pending; }
static int h_spec(int slot, char *buf, int n)
{
    if (slot < 0 || slot > 1) {
        return -1;
    }
    const std::string &s = g_slot[slot];
    if ((int)s.size() > n - 1) {
        return -1;                                     // does not fit
    }
    memcpy(buf, s.c_str(), s.size() + 1);
    return (int)s.size();
}
static int h_mount(int slot, const char *spec, char *msg, int n)
{
    g_log.push_back(std::string("slot_mount ") + g_names[slot] + " " + spec);
    g_state_at_mount = g_api->ctx->state();
    if (g_refuse_next) {
        g_refuse_next = false;
        snprintf(msg, n, "no such medium");
        return 1;
    }
    g_slot[slot] = spec;
    return 0;
}
static int h_eject(int slot, char *msg, int n)
{
    g_log.push_back(std::string("slot_eject ") + g_names[slot]);
    (void)msg; (void)n;
    g_slot[slot].clear();
    return 0;
}
static CoreHooks g_table;
static uint32_t g_major = 1;
static void make_table(bool slots = true)
{
    memset(&g_table, 0, sizeof(g_table));
    g_table.size = sizeof(g_table);
    g_table.init = h_init;
    g_table.release = h_release;
    g_table.warm_reset = h_warm;
    g_table.cold_start = h_cold;
    g_table.media_flush = h_flush;
    g_table.media_pending = h_pending;
    if (slots) {
        g_table.slot_spec = h_spec;
        g_table.slot_mount = h_mount;
        g_table.slot_eject = h_eject;
    }
}

struct Host : LibHost {
    void *alloc(uint32_t n) override { return malloc(n); }
    void release(void *p) override { free(p); }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "Model Core"; }
    int call_register(const Entry &e, const LibApi *api, LibRecord *rec) override
    {
        g_api = api;
        static LibExportDecl l1[1], l2[1];                                   // the record points into these for as long as the library is loaded
        if (!strcmp(e.name, "cmodel")) {
            l1[0] = LibExportDecl{ "corehooks", g_major, &g_table, (uint32_t)sizeof(g_table) };
            rec->export_list = l1;
            rec->nexport_list = 1;
        } else if (!strcmp(e.name, "cmajor2")) {
            l2[0] = LibExportDecl{ "corehooks", 2u, &g_table, (uint32_t)sizeof(g_table) };
            rec->export_list = l2;
            rec->nexport_list = 1;
        }
        return 0;
    }
};

struct FileSource : LibSource {
    std::vector<uint8_t> d;
    explicit FileSource(const std::string &name)
    {
        std::string p = g_dir + "/" + name;
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) {
            printf("FAIL cannot open %s\n", p.c_str());
            g_fails++;
            return;
        }
        uint8_t b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0) {
            d.insert(d.end(), b, b + n);
        }
        fclose(f);
    }
    uint32_t size() override { return (uint32_t)d.size(); }
    bool read(uint32_t off, void *dst, uint32_t n) override
    {
        if (off > d.size() || n > d.size() - off) {
            return false;
        }
        memcpy(dst, d.data() + off, n);
        return true;
    }
};

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &name)
{
    FileSource s(name);
    return s.d;
}
static int parse(const Bytes &b, DescRecord *d, char *why, int n)
{
    uint8_t *exact = new uint8_t[b.size()];                // the record is read in place and kept
    memcpy(exact, b.data(), b.size());
    return descrec_parse(exact, (uint32_t)b.size(), d, why, n);
}

struct Ui : BootUi {
    int n = 0;
    void message(const char *, const char *, bool) { }
    void clear() { }
    void tick() { if (++n > 50) { printf("FAIL boot_start was still waiting after %d passes\n", n); exit(1); } }
};

static std::string joined()
{
    std::string s;
    for (size_t i = 0; i < g_log.size(); i++) {
        s += (i ? "," : "") + g_log[i];
    }
    return s;
}

// a run: the context, the loader with the model library, the descriptor made of the record and bound to the library
// An OsPort that only counts the lock: the core lock's owner and the calls media_source makes (it reads an engine's slot under the lock, and never takes it twice on one task)
struct CountOs : OsPort {
    int locks = 0, unlocks = 0;
    void *mutex_new() { return this; }
    void lock(void *) { locks++; }
    void unlock(void *) { unlocks++; }
    void *sem_new() { return this; }
    void give(void *) { }
    bool take(void *, uint32_t) { return true; }
    bool task_new(const char *, void (*)(void *), void *, int, uint32_t) { return false; }
    void *self() { return this; }
    uint32_t now_ms() { return 0; }
    void sleep_ms(uint32_t) { }
};

struct Run {
    FakeFs fs;
    FakeIo io;
    Host host;
    OpRegistry reg;
    LibLoader ld;
    CoreCtx c;
    DescRecord *rec;
    CoreDesc rd, d;
    Setting items[SETTINGS_MAX];
    Settings *st = 0;
    int bind_rc = 0;
    char bind_why[200];
    const char *bind_err = 0;
    Run(const char *cfg, const char *recfile = "desc.rec", const char *libfile = "cmodel.u2a") : ld(&host, &reg, U2A_BASE_MINOR)
    {
        char why[240];
        rec = new DescRecord;
        CHECK_EQ(parse(slurp(recfile), rec, why, sizeof(why)), 0);
        CHECK_EQ(descrec_core(rec, 0, &rd, why, sizeof(why)), 0);
        const char *pe = persist_extend(&rd, &d, items);                  // "Persist medium mounts", as the application adds it for a core that has slots
        CHECK(pe == 0);
        d.reset_ms = 1;
        memset(&c, 0, sizeof(c));
        c.desc = &d;
        c.io = &io;
        c.fs = &fs;
        if (cfg) {
            fs.put_text("/sd/cores/modelcore.cfg", cfg);
        }
        st = new Settings(d.settings, d.nsettings, d.store, &fs, "/sd/cores/modelcore.cfg", 0);
        st->load();
        c.settings = st;
        snprintf(c.cfg_path, sizeof(c.cfg_path), "/sd/cores/modelcore.cfg");
        c.started = false;
        g_ld = &ld;
        ld.add_services();
        ld.set_ctx(&c);
        char err[LIB_ERR_MAX];
        FileSource src(libfile);
        CHECK_EQ(ld.load(&src, &g_handle, err, LIB_ERR_MAX), LIB_OK);
        g_log.clear();
    }
    int attach()
    {
        bind_rc = hooks_attach_lib(&ld, d.hooks_lib, bind_why, sizeof(bind_why));
        bind_err = hooks_bind(&d);
        return bind_rc;
    }
};

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    g_dir = argv[1];
    char why[240];

    // ---- (1) the record
    {
        DescRecord *r = new DescRecord;
        CHECK_EQ(parse(slurp("desc.rec"), r, why, sizeof(why)), 0);
        CHECK(r->has_core && r->sec && r->sec->ident.hooks);
        CHECK_STR(r->sec->ident.hooks, "cmodel");
        CoreDesc d;
        CHECK_EQ(descrec_core(r, 0, &d, why, sizeof(why)), 0);           // no compiled-in code at all: the [mount] list and the control hooks come with the library
        CHECK_STR(d.hooks_lib, "cmodel");
        CHECK(d.init == 0 && d.release == 0 && d.warm_reset == 0 && d.cold_start == 0 && d.media_flush == 0 && d.media_pending == 0);
        CHECK(d.mount_slots != 0 && d.mount_slots->n == 2);
        CHECK_STR(d.mount_slots->names[0], "D1");
        CHECK_STR(d.mount_slots->names[1], "Cart");
        CHECK(d.mount_slots->spec == 0 && d.mount_slots->mount == 0 && d.mount_slots->eject == 0);   // put in by hooks_bind, once the library is there

        DescRecord *n = new DescRecord;                                   // the same record without the key: a [mount] list needs compiled-in code, as before
        CHECK_EQ(parse(slurp("desc_nohooks.rec"), n, why, sizeof(why)), 0);
        CHECK(n->sec->ident.hooks == 0);
        CoreDesc nd;
        CHECK_EQ(descrec_core(n, 0, &nd, why, sizeof(why)), -1);
        CHECK(strstr(why, "mount slots") != 0);
        CHECK(nd.hooks_lib == 0);

        DescRecord *l = new DescRecord;                                   // 15 characters are the longest name
        CHECK_EQ(parse(slurp("desc_long.rec"), l, why, sizeof(why)), 0);
        CHECK_STR(l->sec->ident.hooks, "0123456789abcde");
        const char *bad[2] = { "desc_upper.rec", "desc_empty.rec" };       // a name of capitals, an empty one
        for (int i = 0; i < 2; i++) {
            DescRecord *b = new DescRecord;
            CHECK(parse(slurp(bad[i]), b, why, sizeof(why)) != 0);
            CHECK(strstr(why, "hooks library") != 0);
        }
    }

    // ---- (2) the first reset at power-on: the libraries are loaded, then boot_start. init sees api->io and the settings
    {
        make_table();
        Run r("[Model Core Settings]\nPersist medium mounts=Yes\nMount D1=/usb0/a.d64\n");
        CHECK_EQ(r.attach(), 0);
        CHECK(r.bind_err == 0);
        CHECK(hooks_present());
        CHECK(r.d.init != 0 && r.d.release != 0 && r.d.warm_reset != 0 && r.d.cold_start != 0 && r.d.media_flush != 0 && r.d.media_pending != 0);
        CHECK(r.d.mount_slots != 0 && r.d.mount_slots->spec && r.d.mount_slots->mount && r.d.mount_slots->eject);
        CHECK_EQ(mounts_slots(&r.c), 2);
        char nm[16];
        mounts_slot_name(&r.c, 1, nm, sizeof(nm));
        CHECK_STR(nm, "Cart");
        Ui ui;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        CHECK(g_init_io && g_init_settings && g_init_persist);              // the groups are there when the first init runs
        CHECK_EQ(g_init_state & CTX_MEDIA_RESTORING, 0u);
        CHECK_EQ(g_blocks_in_init, 1);                                      // the block of the hook is on the library's account ...
        CHECK_EQ(r.ld.account_blocks(-1), 0);                               // ... and on nobody's
        // the persisted mount is restored between init and release, and the library is told that the base restores
        CHECK_STR(joined().c_str(), "init,slot_mount D1 /usb0/a.d64,release");
        CHECK((g_state_at_mount & CTX_MEDIA_RESTORING) != 0);
        CHECK_STR(g_slot[0].c_str(), "/usb0/a.d64");
        CHECK(r.c.started);
        CHECK(!r.c.media_restoring);

        // ---- (3) the slots: media_source by name, the spec buffer is the base's, a self change is notified and stored, a refusal keeps the slot, eject empties
        char buf[64], msg[200];
        const SvcCtx *cx = g_api->ctx;
        CHECK(cx->media_source("D1", buf, sizeof(buf)));
        CHECK_STR(buf, "/usb0/a.d64");
        CHECK(!cx->media_source("Cart", buf, sizeof(buf)));                  // length 0: empty
        CHECK(!cx->media_source("Nope", buf, sizeof(buf)));
        {                                                                    // the engine's slot_spec is Task: lock: read under the core lock, taken by media_source unless this task holds it
            CountOs os;
            CoreLock lk(&os);
            r.c.lock = &lk;
            CHECK(cx->media_source("D1", buf, sizeof(buf)) && !strcmp(buf, "/usb0/a.d64"));
            CHECK_EQ(os.locks, 1);
            CHECK_EQ(os.unlocks, 1);
            CHECK(!lk.mine());
            lk.lock();                                                       // a handler or a hook that holds it already: no second take (the lock is not recursive)
            CHECK(lk.mine());
            CHECK(cx->media_source("D1", buf, sizeof(buf)) && !strcmp(buf, "/usb0/a.d64"));
            CHECK_EQ(os.locks, 2);
            lk.unlock();
            CHECK_EQ(os.unlocks, 2);
            CHECK(!cx->media_source("Cart", buf, sizeof(buf)));              // an empty slot gives the lock back too
            CHECK_EQ(os.locks, 3);
            CHECK_EQ(os.unlocks, 3);
            r.c.lock = 0;
        }
        // T262 P2: ctx.ask_list is the base's list of choices for a library: only the request is stored, the menu shows the list when the operation has returned
        CHECK(cx->size >= offsetof(SvcCtx, ask_list) + sizeof(cx->ask_list) && cx->ask_list);
        r.c.want_prompt.kind = 0;
        cx->ask_list("lib.pick", 4, "Pick a year", "lib.years");              // outside a call of the menu (a job, a REST call): nothing is asked
        CHECK_EQ(r.c.want_prompt.kind, 0);
        r.c.menu_op = 1;                                                     // inside an operation the menu called
        cx->ask_list("lib.pick", 4, "Pick a year", "lib.years");
        CHECK_EQ(r.c.want_prompt.kind, 3);
        CHECK_EQ(r.c.want_prompt.phase, 4);
        CHECK_STR(r.c.want_prompt.op, "lib.pick");
        CHECK_STR(r.c.want_prompt.prompt, "Pick a year");
        CHECK_STR(r.c.want_prompt.src, "lib.years");
        r.c.want_prompt.kind = 0;
        cx->ask_list("", 1, "x", "y");                                       // no operation to call again: nothing is asked
        cx->ask_list("lib.pick", 1, "x", "");                                // no rows: nothing is asked
        CHECK_EQ(r.c.want_prompt.kind, 0);
        r.c.menu_op = 0;
        g_slot[1] = "ro /usb0/g.crt";                                        // the engine changes by its own doing
        cx->mount_notify("Cart");
        CHECK_EQ(r.c.mounts_dirty[1], 1);
        mounts_pass(&r.c);
        char val[Settings::MOUNT_VAL_LEN];
        CHECK(r.st->mount_get("Mount Cart", val, sizeof(val)));
        CHECK_STR(val, "ro /usb0/g.crt");
        CHECK(cx->media_source("Cart", buf, sizeof(buf)));
        CHECK_STR(buf, "ro /usb0/g.crt");
        const char *s0 = r.d.mount_slots->spec(&r.c, 0), *s1 = r.d.mount_slots->spec(&r.c, 1);
        CHECK(s0 == s1);                                                     // one buffer, the base's, valid until the next call
        CHECK_STR(s1, "ro /usb0/g.crt");
        g_slot[0] = std::string(250, 'x');                                   // does not fit the base's buffer: -1, the slot reads as empty
        CHECK(r.d.mount_slots->spec(&r.c, 0) == 0);
        CHECK(mounts_slot_spec(&r.c, 0) == 0);
        g_slot[0] = "/usb0/a.d64";
        g_log.clear();
        g_refuse_next = true;
        CHECK_EQ(r.d.mount_slots->mount(&r.c, 0, "/usb0/b.d64", msg, sizeof(msg)), 1);
        CHECK_STR(msg, "no such medium");
        CHECK_STR(g_slot[0].c_str(), "/usb0/a.d64");                         // the slot that cannot take the medium keeps what it held
        CHECK_EQ(r.d.mount_slots->mount(&r.c, 0, "/usb0/b.d64", msg, sizeof(msg)), 0);
        CHECK_STR(g_slot[0].c_str(), "/usb0/b.d64");
        CHECK_EQ(g_state_at_mount & CTX_MEDIA_RESTORING, 0u);                // an ordinary mount is not a restore
        CHECK_EQ(r.d.mount_slots->eject(&r.c, 0, msg, sizeof(msg)), 0);
        CHECK(r.d.mount_slots->spec(&r.c, 0) == 0);
        CHECK(!cx->media_source("D1", buf, sizeof(buf)));
        CHECK_STR(joined().c_str(), "slot_mount D1 /usb0/b.d64,slot_mount D1 /usb0/b.d64,slot_eject D1");

        // ---- (2b) the resets: the write-back first (media_pending, media_flush), then the hook of the reset
        char rm[200];
        g_pending = true;
        g_log.clear();
        CHECK_EQ(boot_warm_reset(&r.c, rm, sizeof(rm)), 0);
        CHECK_STR(joined().c_str(), "media_pending,media_flush,warm_reset");
        CHECK(!g_pending);
        g_log.clear();
        g_pending = true;
        g_flush_fail = true;                                                 // a write-back that fails stops the reset and says why: no warm_reset
        CHECK(boot_warm_reset(&r.c, rm, sizeof(rm)) != 0);
        CHECK_STR(rm, "the card refuses the write-back");
        CHECK_STR(joined().c_str(), "media_pending,media_flush");
        g_flush_fail = false;
        g_pending = false;
        g_log.clear();
        CHECK_EQ(boot_cold_reset(&r.c, rm, sizeof(rm)), 0);
        CHECK_STR(joined().c_str(), "media_pending,media_flush,init,release");
        g_log.clear();
        CHECK_EQ(boot_restart(&r.c, rm, sizeof(rm)), 0);
        CHECK_STR(joined().c_str(), "media_pending,media_flush,cold_start,init,release");
        hooks_detach();
    }

    // ---- (2c) a table without warm_reset, cold_start and the media members: those descriptor members are null, a warm reset is a cold one, the control hooks are still the library's
    {
        make_table();
        g_table.warm_reset = 0;
        g_table.cold_start = 0;
        g_table.media_flush = 0;
        g_table.media_pending = 0;
        g_table.release = 0;
        Run r(0);
        CHECK_EQ(r.attach(), 0);
        CHECK(r.d.warm_reset == 0 && r.d.cold_start == 0 && r.d.media_flush == 0 && r.d.media_pending == 0);
        CHECK(r.d.init != 0 && r.d.release != 0);                            // boot.cc calls both without a test
        char rm[200];
        g_log.clear();
        CHECK_EQ(boot_warm_reset(&r.c, rm, sizeof(rm)), 0);
        CHECK_STR(joined().c_str(), "init");                                 // the cold reset: hold, release (a null member does nothing)
        hooks_detach();
    }

    // ---- (4) what is not there: the library is not loaded, it has no export, the export is of another major, the table has no slot members
    {
        make_table();
        Run r(0);
        char w[200];
        CHECK_EQ(hooks_attach_lib(&r.ld, "other", w, sizeof(w)), -1);
        CHECK(strstr(w, "\"other\"") && strstr(w, "not loaded"));
        CHECK(!hooks_present());
        const char *err = hooks_bind(&r.d);                                  // no table: the control hooks are bound to do nothing, the slots are gone, the answer says why
        CHECK(err != 0 && strstr(err, "no table of hooks"));
        CHECK(r.d.init != 0 && r.d.release != 0 && r.d.mount_slots == 0);
        char err2[LIB_ERR_MAX];
        int h2 = -1, h3 = -1;
        { FileSource src("cnohooks.u2a"); CHECK_EQ(r.ld.load(&src, &h2, err2, LIB_ERR_MAX), LIB_OK); }
        CHECK_EQ(hooks_attach_lib(&r.ld, "cnohooks", w, sizeof(w)), -1);
        CHECK(strstr(w, "does not export corehooks at major 1"));
        { FileSource src("cmajor2.u2a"); CHECK_EQ(r.ld.load(&src, &h3, err2, LIB_ERR_MAX), LIB_OK); }
        CHECK_EQ(hooks_attach_lib(&r.ld, "cmajor2", w, sizeof(w)), -1);      // the export is there, at major 2: not ours
        CHECK(strstr(w, "does not export corehooks at major 1"));
        CHECK(!hooks_present());
    }
    {
        make_table(false);                                                   // a table with the control members but no slot_spec, slot_mount, slot_eject
        Run r(0);
        CHECK_EQ(r.attach(), 0);
        CHECK(r.bind_err != 0 && strstr(r.bind_err, "slot_spec"));
        CHECK(r.d.mount_slots == 0);                                         // "a row that does nothing is a defect": no slot rows
        CHECK(r.d.init != 0);
        hooks_detach();
    }
    {
        make_table(false);                                                   // the slots go, so does "Persist medium mounts" that the record's [mount] names brought (it does nothing without a slot)
        Run r(0);
        auto has_persist_in = [](const Setting *v, int n) { for (int i = 0; i < n; i++) { if (!strcasecmp(v[i].key, PERSIST_KEY)) return true; } return false; };
        auto has_persist = [&](const CoreDesc &x) { return has_persist_in(x.settings, x.nsettings); };
        const int before = r.d.nsettings;
        const Setting *own = r.d.settings;
        CHECK(has_persist(r.d));                                             // persist_extend put it there, from the record's [mount] names
        CHECK_EQ(r.attach(), 0);
        CHECK(r.d.mount_slots == 0 && !mounts_have_slots(&r.d));
        CHECK(!has_persist(r.d) && r.d.nsettings == before - 1);
        CHECK(own != r.d.settings && has_persist_in(own, before));            // the array persist_extend filled is not written, the descriptor got a copy
        hooks_detach();
    }
    {
        make_table();                                                        // a descriptor with no hooks_lib is not touched
        CoreDesc plain;
        memset(&plain, 0, sizeof(plain));
        CHECK(hooks_bind(&plain) == 0 && plain.init == 0 && plain.mount_slots == 0);
    }
    if (!g_fails) {
        printf("test_corelib: %d checks\n", g_checks);
    }
    return g_fails ? 1 : 0;
}
