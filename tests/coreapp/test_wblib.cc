// T239.3 D51: the write-back service for libraries, through api->wb (svc_wb.h). The service (writeback.cc), the loader and its group (libload.cc, libsvc.cc) are the real
// ones; the library is this test's C code that calls only what LibApi gives it, the medium a fake image, the change source the test itself (what a library's hardware would raise).
// Scenarios: S1 register, report a change, ask for the write: the file holds the image, the old file is NAME.bak, no NAME.tmp is left, the writer got the old file and the dirty map;
// S2 the setting decides (Ask: SVCWB_ASK, Off: SVCWB_OFF, the file unchanged, the change stays pending); S3 the medium of the base's driver is not taken (SVCWB_E_TAKEN, nothing
// changed, a library's marks and write_now do nothing to it); S4 the operation writeback.flush writes the registered medium (the way of an application and of JSON-RPC); S5 a medium with
// may_create gives the writer a null old file and creates the file; S6 a failing writer keeps the change pending and the old file; S7 the image goes (drop_owner, what libload.cc's discard
// calls): Auto writes first, then the medium is gone; S8 bad arguments change nothing. S1 is the one the negative control breaks (run.sh: wb_lib_mark_dropped: the report of the change never
// reaches the service, so the file must stay as it was and S1 must fail).
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#define private public                          // the test reads the LibApi the loader hands libraries
#include "libload.h"
#undef private
#include "fakes.h"
#include "check.h"
#include "writeback.h"
#include "ops.h"
#include "ops_glue.h"

const CoreDesc *const core_table[] = { 0 };

struct Host : LibHost {
    void *alloc(uint32_t n) override { return malloc(n); }
    void release(void *p) override { free(p); }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "Model Core"; }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }
};
struct Port : WbPort {
    uint32_t t = 0;
    uint32_t ticks_ms() { return t; }
    void enter() { }
    void leave() { }
    void irq_attach(uint8_t, uint8_t (*)(uint8_t)) { }
    void irq_enable(uint8_t, bool) { }
};

// ---- the library: C code that sees LibApi only ---------------------------------------------------------------------------------------------
static const LibApi *g_api;
static uint8_t g_img[64];
static int g_calls, g_old_seen, g_old_size, g_block0, g_fail, g_holds_on, g_holds_off;
static int lib_write(void *user, SvcWbFile *old, SvcWbFile *out, const SvcWbDirty *d, char *msg, int msglen)
{
    (void)user;
    g_calls++;
    g_old_seen = old != 0;
    g_old_size = old ? (int)g_api->wb->file_size(old) : -1;
    g_block0 = d->block_shift == 13 && d->nblocks == 1024 && (d->blocks[0] & 1);
    if (g_fail) {
        snprintf(msg, msglen, "the image cannot be read");
        return 1;
    }
    if (old && g_old_size >= 1) {
        uint8_t b;
        g_api->wb->file_read(old, 0, &b, 1);                         // the old file is readable through the facade
    }
    if (!g_api->wb->file_write(out, 0, g_img, sizeof(g_img))) {
        snprintf(msg, msglen, "write failed");
        return 1;
    }
    return 0;
}
static void lib_hold(void *, bool on) { (on ? g_holds_on : g_holds_off)++; }
static void lib_change(int at, uint8_t v)                           // the game writes one byte of the medium; the library reports it
{
    g_img[at] = v;
    g_api->wb->mark((uint32_t)at, 1);
}
static SvcWbMedium medium(const char *path, bool create = false)
{
    SvcWbMedium m;
    memset(&m, 0, sizeof(m));
    m.size = sizeof(m);
    m.path = path;
    m.name = "game.crt";
    m.write = lib_write;
    m.may_create = create;
    m.hold = lib_hold;
    return m;
}

struct Rig {
    FakeFs fs;
    Port port;
    Host host;
    OpRegistry reg;
    LibLoader ld;
    CoreCtx c;
    Rig() : ld(&host, &reg, U2A_BASE_MINOR)
    {
        memset(&c, 0, sizeof(c));
        c.fs = &fs;
        ld.add_services();
        ld.set_ctx(&c);
        ld.set_wb(wb_lib_svc());
        g_api = &ld.api_;
        wb_attach(&c, &port);                                       // the service points at the previous rig's port until it is attached again
        wb_unregister();
        wb_set_mode(WB_AUTO);
        wb_register_ops(reg);
        memset(g_img, 0x11, sizeof(g_img));
        g_calls = g_old_seen = g_old_size = g_block0 = g_fail = g_holds_on = g_holds_off = 0;
    }
    ~Rig() { wb_unregister(); }
    std::vector<uint8_t> file(const char *p) { auto it = fs.files.find(lower(p)); return it == fs.files.end() ? std::vector<uint8_t>() : it->second; }
};

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    const char *P = "/usb0/games/game.crt";
    // S1: register, report, write now (Auto): the safe write
    {
        Rig r;
        CHECK(g_api->wb != 0 && LIB_API_COVERS(g_api, wb) && g_api->wb->size == sizeof(SvcWb));
        CHECK_EQ(g_api->abi_minor, 4);
        r.fs.put_size(P, 64, 0xEE);
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);
        CHECK(wb_active());
        CHECK_EQ(g_api->wb->pending(), 0);
        lib_change(5, 0x77);
        CHECK_EQ(g_api->wb->pending(), 1);
        CHECK(r.file(P)[5] == 0xEE);                                // not written yet
        char msg[100];
        CHECK_EQ(g_api->wb->write_now(msg, sizeof(msg)), SVCWB_OK);
        CHECK_EQ(g_calls, 1);
        CHECK(g_old_seen && g_old_size == 64 && g_block0);
        CHECK_EQ(g_holds_on, 1);
        CHECK_EQ(g_holds_off, 1);
        std::vector<uint8_t> f = r.file(P);
        CHECK_EQ(f.size(), 64);
        CHECK(f.size() == 64 && f[5] == 0x77 && f[0] == 0x11);
        std::vector<uint8_t> bak = r.file("/usb0/games/game.crt.bak");
        CHECK(bak.size() == 64 && bak[5] == 0xEE);                  // the file as the user had it
        CHECK(!r.fs.has("/usb0/games/game.crt.tmp"));
        CHECK_EQ(g_api->wb->pending(), 0);
        CHECK_EQ(g_api->wb->write_now(msg, sizeof(msg)), SVCWB_OK); // nothing pending: no second write
        CHECK_EQ(g_calls, 1);
        lib_change(6, 0x66);                                        // a second change: NAME.bak stays the first original
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_OK);
        f = r.file(P);
        bak = r.file("/usb0/games/game.crt.bak");
        CHECK(f.size() == 64 && f[6] == 0x66 && bak.size() == 64 && bak[5] == 0xEE && bak[6] == 0xEE);
        CHECK_EQ(g_api->wb->unregister_medium(), SVCWB_OK);
        CHECK(!wb_active());
        CHECK_EQ(g_api->wb->unregister_medium(), SVCWB_E_NOTMINE);
    }
    // S2: the setting decides ("user beats API")
    {
        Rig r;
        r.fs.put_size(P, 64, 0xEE);
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);
        wb_set_mode(WB_ASK);
        lib_change(1, 0x22);
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_ASK);
        CHECK_EQ(g_calls, 0);
        CHECK(r.file(P)[1] == 0xEE && g_api->wb->pending() == 1);
        wb_set_mode(WB_OFF);
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_OFF);
        CHECK(r.file(P)[1] == 0xEE && g_api->wb->pending() == 1 && g_calls == 0);
        wb_set_mode(WB_AUTO);
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_OK);              // the same change goes out once the person allows it
        CHECK(r.file(P)[1] == 0x22);
    }
    // S3: the base's own driver holds the medium: a library gets a refusal and changes nothing
    {
        Rig r;
        r.fs.put_size("/usb0/saves/x.sav", 16, 0xCC);
        r.fs.put_size(P, 64, 0xEE);
        static uint8_t ram[16];
        WbRaw raw;
        raw.read = [](void *, uint32_t, void *b, uint32_t n) { memset(b, 0x42, n); return 0; };
        raw.user = ram;
        raw.size = 16;
        WbMedium w;
        memset(&w, 0, sizeof(w));
        snprintf(w.path, sizeof(w.path), "/usb0/saves/x.sav");
        snprintf(w.name, sizeof(w.name), "x.sav");
        w.write = wb_raw_write;
        w.user = &raw;
        wb_register(w);
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_E_TAKEN);
        CHECK(wb_active() && !strcmp(wb_path(), "/usb0/saves/x.sav") && wb_user() == &raw);
        g_api->wb->mark(0, 4);                                       // not the library's medium: the report is not taken
        g_api->wb->mark_aux();
        CHECK(!wb_pending());
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_E_NOTMINE);
        CHECK_EQ(g_api->wb->pending(), 0);
        CHECK_EQ(g_api->wb->unregister_medium(), SVCWB_E_NOTMINE);
        CHECK(wb_active() && g_calls == 0);
        wb_mark(0, 4);                                               // the driver's own report still counts
        CHECK(wb_pending());
    }
    // S4: an application or the API asks over the operation: only the registered medium is written, as the setting says
    {
        Rig r;
        r.fs.put_size(P, 64, 0xEE);
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);
        lib_change(9, 0x99);
        OpResult res;
        wb_set_mode(WB_OFF);
        CHECK_EQ(g_api->ops->call("writeback.flush", 0, 0, &res), OPE_OK);
        CHECK(r.file(P)[9] == 0xEE && g_calls == 0 && std::string(res.msg).find("Off") != std::string::npos);
        wb_set_mode(WB_AUTO);
        CHECK_EQ(ops_call(&r.c, "writeback.flush", 0, 0, &res), OPE_OK);
        CHECK(r.file(P)[9] == 0x99 && g_calls == 1 && !wb_pending());
        CHECK_EQ(ops_call(&r.c, "writeback.flush", 0, 0, &res), OPE_OK);   // nothing pending: nothing happens
        CHECK_EQ(g_calls, 1);
    }
    // S5: may_create: the file is not there, the writer gets a null old file, the file is made
    {
        Rig r;
        SvcWbMedium m = medium("/usb0/saves/new.sav", true);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);
        lib_change(2, 0x33);
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_OK);
        CHECK(!g_old_seen && g_old_size == -1);
        std::vector<uint8_t> f = r.file("/usb0/saves/new.sav");
        CHECK(f.size() == 64 && f[2] == 0x33);
        CHECK(!r.fs.has("/usb0/saves/new.sav.bak"));                 // nothing to keep
        // without may_create the missing file is an error, not a created file
        SvcWbMedium n = medium("/usb0/saves/gone.sav", false);
        CHECK_EQ(g_api->wb->register_medium(&n), SVCWB_OK);          // replaces the first (written already)
        lib_change(3, 0x44);
        char msg[100];
        CHECK_EQ(g_api->wb->write_now(msg, sizeof(msg)), SVCWB_E_FAILED);
        CHECK(!r.fs.has("/usb0/saves/gone.sav") && g_api->wb->pending() == 1 && msg[0]);
    }
    // S6: a writer that fails: the change stays pending, the old file is untouched, no NAME.tmp is left; the next try writes
    {
        Rig r;
        r.fs.put_size(P, 64, 0xEE);
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);
        lib_change(4, 0x55);
        g_fail = 1;
        char msg[100];
        CHECK_EQ(g_api->wb->write_now(msg, sizeof(msg)), SVCWB_E_FAILED);
        CHECK(std::string(msg).find("cannot be read") != std::string::npos);
        CHECK(r.file(P)[4] == 0xEE && !r.fs.has("/usb0/games/game.crt.tmp") && !r.fs.has("/usb0/games/game.crt.bak"));
        CHECK_EQ(g_api->wb->pending(), 1);
        g_fail = 0;
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_OK);
        CHECK(r.file(P)[4] == 0x55);
        // a write protected card is told, not tried
        lib_change(7, 0x77);
        r.fs.read_only = true;
        CHECK_EQ(g_api->wb->write_now(msg, sizeof(msg)), SVCWB_E_FAILED);
        CHECK(g_api->wb->pending() == 1 && r.file(P)[7] == 0x11);
        r.fs.read_only = false;
        CHECK_EQ(g_api->wb->write_now(0, 0), SVCWB_OK);
        CHECK(r.file(P)[7] == 0x77);
    }
    // S7: the image is released: what is pending is written (Auto), then the medium is gone; under Ask it is dropped with the image
    {
        Rig r;
        r.fs.put_size(P, 64, 0xEE);
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);
        lib_change(8, 0x88);
        wb_lib_svc()->drop_owner(r.ld.owner());                       // the owner the calls above had (no image is loaded: the loader's own account)
        CHECK(r.file(P)[8] == 0x88 && !wb_active());
        CHECK_EQ(g_api->wb->register_medium(&m), SVCWB_OK);          // the medium is free again
        lib_change(10, 0xAA);
        wb_set_mode(WB_ASK);
        wb_lib_svc()->drop_owner(r.ld.owner());
        CHECK(!wb_active());
        CHECK(r.file(P)[10] != 0xAA);                                // Ask: not written behind the person's back
    }
    // S8: bad arguments change nothing
    {
        Rig r;
        SvcWbMedium m = medium(P);
        CHECK_EQ(g_api->wb->register_medium(0), SVCWB_E_ARG);
        SvcWbMedium z = m;
        z.path = "";
        CHECK_EQ(g_api->wb->register_medium(&z), SVCWB_E_ARG);
        z = m;
        z.write = 0;
        CHECK_EQ(g_api->wb->register_medium(&z), SVCWB_E_ARG);
        z = m;
        z.size = 8;
        CHECK_EQ(g_api->wb->register_medium(&z), SVCWB_E_ARG);
        std::string longp(FS_PATH_MAX + 4, 'a');
        z = m;
        z.path = longp.c_str();
        CHECK_EQ(g_api->wb->register_medium(&z), SVCWB_E_ARG);
        CHECK(!wb_active());
        CHECK(std::string(g_api->wb->error(SVCWB_E_TAKEN)).size() > 0 && std::string(g_api->wb->error(-99)) == "unknown code");
    }
    DONE("wblib");
}
