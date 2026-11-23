// T239.3 base 3.3: the tables the base answers api->lib->find with for the shared libraries that stay in it (libexports.cc): c64cart, cbm1541, uci, sid, tempdisk, cbmprg. The application's code
// behind the members is a recording model (exports_stub.cc); what is proven is the table: found by name, size first, every member there, and each member calling the code the header
// names with the core's context.
#include <string>
#include <vector>
#include <stddef.h>
#include "check.h"
#include "libload.h"
#include "libexports.h"
#include "c64cart_api.h"
#include "cbm1541_api.h"
#include "uci_api.h"
#include "sid_api.h"
#include "tempdisk_api.h"
#include "cbmprg_api.h"
#include "cbm_prg.h"
#include "sidfile.h"
#include "ctx.h"
#include "desc.h"
#include "board.h"
#include "fakes.h"
#include "service.h"
#include "exports_stub.h"
#define private public                 // the test reads what the setters of the drive library wrote
#include "d1541_drive.h"
#undef private

#include "drvtab.h"                   // generated from coreapp/gateware/shared.toml, its registers redirected to g_drv_fake (tests/drv_fake.h)
uint8_t g_drv_fake[0x200000];
#define g_slot_regs (g_drv_fake + (DRV_cmd_if_ADDR & 0x1FFFFF))      // the command interface's SLOT_BASE (+0) and SLOT_ENABLE (+1), written by uci_slot.cc

struct Host : LibHost {
    void *alloc(uint32_t n) override { return malloc(n); }
    void release(void *p) override { free(p); }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "Test Core"; }
    int call_register(const Entry &e, const LibApi *, LibRecord *) override
    {
        if (on_register) {
            on_register(e);
        }
        return 99;
    }
    void (*on_register)(const Entry &) = 0;
};

// the machine side of the DDR2 windows and of the EEPROM, recorded
class WinIo : public FakeIo {
public:
    std::vector<uint8_t> win;          // absolute 0x00EA8000 .. 0x00F00000
    uint8_t ee[4096];
    bool drv = true;                   // the core has the drive window (CoreIo::drv_present)
    bool drv_present() override { return drv; }
    WinIo() : win(0x00F00000u - 0x00EA8000u, 0) { memset(ee, 0, sizeof(ee)); }
    void ddr_abs_write(uint32_t a, const void *p, uint32_t n) override { memcpy(&win.at(a - 0x00EA8000u), p, n); }
    void ddr_abs_read(uint32_t a, void *p, uint32_t n) override { memcpy(p, &win.at(a - 0x00EA8000u), n); }
    void eeprom_write(uint32_t off, const void *p, uint32_t n) override { memcpy(ee + off, p, n); }
    void eeprom_read(uint32_t off, void *p, uint32_t n) override { memcpy(p, ee + off, n); }
};

// the major of the table, as U2A3_VERSION(major, 0): the base matches it exactly
static const void *find_major(const char *name, uint32_t major, uint32_t *size) { return LibLoader::find_export(name, U2A3_VERSION(major, 0), size); }

// how many of the pointer-sized members of a table of `size` bytes, from offset `first` on, are null
static int null_members(const void *t, uint32_t size, size_t first)
{
    int n = 0;
    for (size_t o = first; o + sizeof(void *) <= size; o += sizeof(void *)) {
        n += *(void *const *)((const uint8_t *)t + o) == 0;
    }
    return n;
}

static void base_list()
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    static const uint32_t ta[3] = { 12, 0, 0 }, tb[3] = { 12, 1, 1 };
    uint32_t sz = 0;
    CHECK(find_major("x", 1, &sz) == 0);
    CHECK(ld.add_export("x", 1, ta));
    CHECK(find_major("x", 1, &sz) == ta && sz == 12);
    CHECK(find_major("x", 2, &sz) == 0 && find_major("y", 1, &sz) == 0);
    CHECK(ld.add_export("x", 1, tb));                                      // the same name and major: replaced
    CHECK(find_major("x", 1, &sz) == tb);
    CHECK(ld.add_export("x", 3, ta));                                      // a higher major beside it: each major answers for itself, none for a major in between
    CHECK(find_major("x", 1, &sz) == tb && find_major("x", 2, &sz) == 0 && find_major("x", 3, &sz) == ta && find_major("x", 4, &sz) == 0);
    CHECK(ld.add_export("x", 1, 0));                                       // a null table takes the entry away
    CHECK(find_major("x", 1, &sz) == 0 && find_major("x", 3, &sz) == ta);
    CHECK(ld.add_export("x", 9, 0));                                       // nothing there: nothing to do
    static const char *const names[] = { "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "n10", "n11", "n12", "n13", "n14", "n15" };
    int added = 0;
    for (int i = 0; i < LIB_BASE_EXPORTS_MAX; i++) {
        added += ld.add_export(names[i], 1, ta);
    }
    CHECK_EQ(added, LIB_BASE_EXPORTS_MAX - 1);                              // "x" holds one place; the list is full after that and says so
    CHECK(!ld.add_export("one_more", 1, ta));
}

static void tables(CoreCtx *cx)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    CHECK(find_major("c64cart", 1, 0) == 0 && find_major("sid", 1, 0) == 0);           // nothing registered yet
    libexports_register(ld, cx);
    uint32_t sz = 0;
    const C64CartApi *cart = (const C64CartApi *)find_major("c64cart", 1, &sz);
    CHECK(cart && sz == sizeof(C64CartApi) && cart->size == sizeof(C64CartApi));
    CHECK_EQ(null_members(cart, sz, offsetof(C64CartApi, active)), 0);
    const Cbm1541Api *drv = (const Cbm1541Api *)find_major("cbm1541", 1, &sz);
    CHECK(drv && sz == sizeof(Cbm1541Api) && drv->size == sizeof(Cbm1541Api));
    CHECK_EQ(null_members(drv, sz, offsetof(Cbm1541Api, slot_spec)), 0);
    const UciApi *uci = (const UciApi *)find_major("uci", 1, &sz);
    CHECK(uci && sz == sizeof(UciApi) && uci->size == sizeof(UciApi));
    CHECK_EQ(null_members(uci, sz, offsetof(UciApi, start)), 0);
    const SidApi *sid = (const SidApi *)find_major("sid", 1, &sz);
    CHECK(sid && sz == sizeof(SidApi) && sid->size == sizeof(SidApi));
    CHECK_EQ(null_members(sid, sz, offsetof(SidApi, parse)), 0);
    const TempDiskApi *tmp = (const TempDiskApi *)find_major("tempdisk", 1, &sz);
    CHECK(tmp && sz == sizeof(TempDiskApi) && tmp->size == sizeof(TempDiskApi));
    CHECK_EQ(null_members(tmp, sz, offsetof(TempDiskApi, set)), 0);
    const CbmPrgApi *prg = (const CbmPrgApi *)find_major("cbmprg", 1, &sz);
    CHECK(prg && sz == sizeof(CbmPrgApi) && prg->size == sizeof(CbmPrgApi));
    CHECK_EQ(null_members(prg, sz, offsetof(CbmPrgApi, place)), 0);
    CHECK(find_major("c64cart", 2, &sz) == 0 && find_major("uci", 2, &sz) == 0 && find_major("cbmprg", 2, &sz) == 0);
    CHECK(find_major("textgrid", 1, &sz) == 0);                                  // the monitor's grid is registered where the core has the monitor (libboard.cc)
}

static void cart_table(CoreCtx *cx, WinIo &io, CoreDesc &desc)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    libexports_register(ld, cx);
    const C64CartApi *c = (const C64CartApi *)find_major("c64cart", 1, 0);
    g_stub = ExportsStub();
    g_stub.cart_active = true; g_stub.cart_name = "game.crt"; g_stub.cart_path = "/usb0/game.crt"; g_stub.crt_type = 32; g_stub.image_bytes = 0x100000;
    CHECK(c->active() && c->crt_type() == 32 && c->image_bytes() == 0x100000);
    CHECK_STR(c->name(), "game.crt");
    CHECK_STR(c->path(), "/usb0/game.crt");
    uint8_t buf[4] = { 0xA5, 1, 2, 3 };
    c->image_read(0x4000, buf, 4);
    c->image_write(0x8000, buf, 4);
    CHECK_STR(g_stub.log.c_str(), "image_read(16384,4);image_write(32768,4,5a);");
    CHECK(g_stub.ctx == cx && buf[0] == 0x5A);                             // the call got the core's context, and the model's bytes came back
    // a state's cartridge, a start and a removal, with and without room for the reason
    g_stub = ExportsStub();
    g_stub.rc = 3;
    char msg[8];
    CHECK_EQ(c->for_state("/a.crt", msg, sizeof(msg)), 3);
    CHECK_STR(msg, "state");
    CHECK_EQ(c->for_state(0, 0, 0), 3);
    CHECK(g_stub.msg_null);                                                // the code behind got a buffer of its own
    CHECK_EQ(c->run("/b.crt", msg, sizeof(msg)), 3);
    CHECK_STR(msg, "a long ");                                            // cut to msglen - 1
    CHECK_EQ(c->run("/b.crt", 0, 0), 3);
    CHECK_EQ(c->remove(msg, sizeof(msg)), 3);
    CHECK_EQ(c->save("/c.crt", 0, 0), 3);
    uint8_t img[1] = { 0x77 };
    CHECK_EQ(c->run_image("sid", img, 0, 0), 3);
    c->player_end();
    CHECK_STR(g_stub.log.c_str(), "for_state(/a.crt);for_state(null);run(/b.crt);run(/b.crt);remove;save(/c.crt);run_image(sid,77);player_end;");
    g_stub.ef_ok = false;
    CHECK(!c->ef_clear(8, true) && !c->player_active());
    g_stub.player = true;
    CHECK(c->player_active());
    // the RAM window: only a core with the cartridge port of the C64 family has it
    CHECK_EQ(c->ram_size(), 0);
    uint8_t one = 9, z[4] = { 1, 1, 1, 1 };
    c->ram_write(0, &one, 1);
    c->ram_read(0, z, 4);
    CHECK(io.win[0x00EF0000u - 0x00EA8000u] == 0 && z[0] == 0 && z[3] == 0);   // no port: a write is ignored, a read gives zeros
    static const char *const caps[] = { "cartridge" };
    desc.caps = caps;
    desc.ncaps = 1;
    // a core that lists the capability and is no C64 (the Atari, the NES ...): no RAM window, no EEPROM window; nothing is read or written
    uint8_t ee_in[2048], ee_out[2048];
    memset(ee_in, 0x33, sizeof(ee_in));
    memset(ee_out, 0x44, sizeof(ee_out));
    io.ee[0] = 0xAA;
    io.ee[2048] = 0xBB;
    CHECK_EQ(c->ram_size(), 0);
    c->ram_write(0, &one, 1);
    c->ram_read(0, z, 4);
    c->eeprom_put(ee_in);
    c->eeprom_get(ee_out);
    CHECK(io.win[0x00EF0000u - 0x00EA8000u] == 0 && z[0] == 0 && z[3] == 0);
    CHECK(io.ee[0] == 0xAA && io.ee[2048] == 0xBB && ee_out[0] == 0 && ee_out[2047] == 0);
    g_stub.c64core = true;
    CHECK_EQ(c->ram_size(), 65536);
    uint8_t w[3] = { 7, 8, 9 }, r[3] = { 5, 5, 5 };
    c->ram_write(0xFFFD, w, 3);
    CHECK(io.win[0x00F00000u - 0x00EA8000u - 1] == 9 && io.win[0x00EF0000u - 0x00EA8000u + 0xFFFD] == 7);   // the last three bytes of the window
    c->ram_read(0xFFFD, r, 3);
    CHECK(r[0] == 7 && r[1] == 8 && r[2] == 9);
    uint8_t before = io.win[0x00EF0000u - 0x00EA8000u + 0xFFFE];
    w[0] = 0x11;
    c->ram_write(0xFFFE, w, 3);                                            // one byte too far: ignored
    c->ram_write(0x10000, w, 1);
    c->ram_write(0xFFFFFFFFu, w, 3);                                       // an offset that wraps: ignored
    CHECK_EQ(io.win[0x00EF0000u - 0x00EA8000u + 0xFFFE], before);
    c->ram_read(0xFFFE, r, 3);                                             // not wholly inside: zeros
    CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0);
    // what the cartridge says about itself
    g_stub = ExportsStub();
    g_stub.c64core = true;
    CHECK(c->require() == 0 && c->prohibit() == 0 && !c->writable());
    g_stub.wb_active = true;
    CHECK(!c->writable());                                                 // no cartridge in the port: false whatever the write-back service holds
    g_stub.cart_active = true;
    CHECK(c->require() == 0 && c->prohibit() == 0);                        // "not known by the base": 0 also with a cartridge in the port
    CHECK(c->writable());
    g_stub.wb_active = false;
    CHECK(!c->writable());                                                 // a cartridge without flash or EEPROM
    // the EEPROM: the data at +2048, byte 0 is the dirty flag
    uint8_t put[2048], got[2048];
    for (int i = 0; i < 2048; i++) { put[i] = (uint8_t)(i * 7 + 1); }
    io.ee[0] = 0;
    c->eeprom_put(put);
    CHECK(!memcmp(io.ee + 2048, put, 2048) && io.ee[0] == 1);              // written, dirty flag cleared (1: clean)
    io.ee[0] = 0;
    c->eeprom_get(got);
    CHECK(!memcmp(got, put, 2048) && io.ee[0] == 0);                       // a dump leaves the flag alone
    desc.caps = 0;
    desc.ncaps = 0;
}

static void drive_table(CoreCtx *cx, WinIo &io)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    libexports_register(ld, cx);
    const Cbm1541Api *d = (const Cbm1541Api *)find_major("cbm1541", 1, 0);
    g_stub = ExportsStub();
    g_stub.rc = 2;
    g_stub.slot_spec = "ro /usb0/x.d64";
    char msg[16];
    CHECK_STR(d->slot_spec(), "ro /usb0/x.d64");
    CHECK_EQ(d->slot_mount("/usb0/y.d64", msg, sizeof(msg)), 2);
    CHECK_STR(msg, "mount");
    CHECK_EQ(d->slot_eject(0, 0), 2);
    CHECK_EQ(d->media_flush(0, 0), 2);
    g_stub.pending = true;
    g_stub.service_work = true;
    CHECK(d->media_pending() && d->service());
    CHECK(g_stub.ctx == cx);
    g_stub.slot_spec = "";
    CHECK(d->slot_spec() == 0);                                            // an empty slot: null
    // no drive window in the core: nothing is answered, nothing is written
    g_stub.drive = 0;
    io.drv = false;
    cx->drive = 0;
    Cbm1541Info info;
    memset(&info, 0xEE, sizeof(info));
    info.size = sizeof(info);
    uint8_t blob[4] = { 1, 2, 3, 4 };
    CHECK(!d->drive_info(0, &info) && info.type == (int32_t)0xEEEEEEEE);
    CHECK_EQ(d->image_size(0), 0);
    d->image_write(0, 0, blob, 4);
    d->image_read(0, 0, blob, 4);
    CHECK(blob[0] == 0 && blob[3] == 0);
    Cbm1541Options opt = { sizeof(opt), 15, 1, 0, 1, 3 };
    CHECK(!d->set_options(&opt));
    g_stub.log.clear();
    CHECK_EQ(d->reset(0, 0), -1);                                          // reset: no drive made, -1 and nothing made or called
    CHECK(g_stub.log.empty());
    char m2[40];
    CHECK_EQ(d->create("/x.d64", CBM1541_FORMAT_D64, "disk", m2, sizeof(m2)), 1);
    CHECK(strstr(m2, "No drive") != 0 && g_stub.log.find("create") == std::string::npos);
    // the members of class any never make the drive: the core has the window and cbm1541_drive would make one, but until a lock member did, drive_info says no and no call reaches it
    CbmDrive drive(0, 0);
    drive.attached_ = true;
    g_stub.drive = &drive;
    io.drv = true;
    g_stub.log.clear();
    memset(&info, 0xEE, sizeof(info));
    info.size = sizeof(info);
    CHECK(!d->drive_info(0, &info) && info.type == (int32_t)0xEEEEEEEE);
    CHECK_EQ(d->image_size(0), 65536);                                     // the window answers, the drive is not needed
    uint8_t any4[4] = { 1, 2, 3, 4 };
    d->image_write(0, 0, any4, 4);
    d->image_read(0, 0, any4, 4);
    CHECK(g_stub.log.empty());
    CHECK(io.win[0x00EE0000u - 0x00EA8000u + 3] == 4 && any4[3] == 4);
    memset(&io.win[0x00EE0000u - 0x00EA8000u], 0, 4);
    cx->drive = &drive;                                                    // made, but not attached to the hardware yet: still no
    drive.attached_ = false;
    CHECK(!d->drive_info(0, &info));
    CHECK(g_stub.log.empty());
    drive.attached_ = true;
    // reset: the drive that was made is reset, the reason comes back, and the call makes no drive (the log has no "drive;")
    g_stub.log.clear();
    g_stub.rc = 3;
    char rm[16];
    CHECK_EQ(d->reset(rm, sizeof(rm)), 3);
    CHECK_STR(rm, "reset");
    CHECK_STR(g_stub.log.c_str(), "drive_reset;");
    g_stub.rc = 2;
    g_stub.log.clear();
    // drive A
    CHECK(d->drive_info(0, &info));
    CHECK(info.size == sizeof(info) && info.type == CBM1541_TYPE_1581 && info.powered == 1 && info.unit == 9 && info.mode == CBM1541_MODE_RO && info.swap_delay_ms == 700);
    CHECK_STR(info.image_path, "/usb0/games/x.d81");
    CHECK_STR(info.image_name, "x.d81");
    CHECK_STR(info.last_dir, "/usb0/games");
    CHECK(!d->drive_info(1, &info));                                        // there is one drive
    // a caller that knows fewer members gets those and keeps its own size; the rest is left alone
    struct { uint32_t size; int32_t type; uint8_t powered, unit, mode, reserved; uint8_t tail[40]; } small;
    memset(&small, 0xEE, sizeof(small));
    small.size = offsetof(Cbm1541Info, reserved) + 1;
    CHECK(d->drive_info(0, (Cbm1541Info *)&small));
    CHECK(small.size == offsetof(Cbm1541Info, reserved) + 1 && small.type == CBM1541_TYPE_1581 && small.unit == 9 && small.tail[0] == 0xEE);
    // the image of drive A in DDR2
    CHECK_EQ(d->image_size(0), 65536);
    CHECK_EQ(d->image_size(1), 0);
    uint8_t w4[4] = { 1, 2, 3, 4 }, r4[4] = { 0, 0, 0, 0 };
    d->image_write(0, 0xFFFC, w4, 4);
    CHECK(io.win[0x00EE0000u - 0x00EA8000u + 0xFFFF] == 4);
    d->image_read(0, 0xFFFC, r4, 4);
    CHECK(!memcmp(r4, w4, 4));
    d->image_write(0, 0xFFFD, w4, 4);                                      // not wholly inside: ignored
    d->image_write(1, 0, w4, 4);                                           // no such slot: ignored
    CHECK(io.win[0x00EE0000u - 0x00EA8000u] == 0);
    memset(r4, 9, 4);
    d->image_read(0, 0xFFFD, r4, 4);
    CHECK(!r4[0] && !r4[3]);
    // the settings of the c1541 store
    g_stub.log.clear();
    CHECK(d->set_options(&opt));
    CHECK(drive.twist_ == 1500 && drive.align_ && drive.freeze_ && drive.geos_ == 3);
    CHECK_STR(g_stub.log.c_str(), "drive;reset_with_c64(0);");
    // a blank image
    g_stub.rc = 0;
    CHECK_EQ(d->create("/x.d81", CBM1541_FORMAT_D81, "blank", 0, 0), 0);
    CHECK(g_stub.log.find("create(/x.d81,2,blank)") != std::string::npos);
    cx->drive = 0;
}

namespace {
struct Hk {
    static int reu(uint32_t *s, uint32_t *o, char *img, int, char *busy, int) { *s = 16; *o = 4; strcpy(img, "reu.img"); strcpy(busy, ""); return 0; }
    static bool drive_info(int idx, uint8_t *t, uint8_t *a, bool *p) { *t = 2; *a = (uint8_t)(8 + idx); *p = true; return idx == 0; }
    static int drive_do(int idx, int what, const char *path, char *msg, int) { strcpy(msg, path); return idx * 10 + what; }
    static int sid_map(uint8_t *d) { d[0] = 0x42; return 1; }
    static bool ef_clear(uint8_t bank, bool high) { return bank == 8 && high; }
    static void ramdrive(int n, int *type, uint32_t *size, uint32_t *base) { *type = 1; *size = 174848u * (n + 1); *base = 0x1000; }
    static void palette(int what, uint8_t idx, uint8_t (*rgb)[3]) { rgb[0][0] = (uint8_t)(what * 16 + idx); }
};
}

static void uci_table(CoreCtx *cx, CoreDesc &desc, BoardProfile &board)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    libexports_register(ld, cx);
    const UciApi *u = (const UciApi *)find_major("uci", 1, 0);
    g_stub = ExportsStub();
    // the callbacks of a library, adapted to the application's UciHost: the context in front, a member past the caller's size is null
    static UciHooks hooks = { sizeof(UciHooks), Hk::reu, Hk::drive_info, Hk::drive_do, Hk::sid_map, "/temp/mem.bin", Hk::ef_clear, Hk::ramdrive, Hk::palette };
    u->start(0);
    CHECK_EQ(g_stub.uci_starts, 0);                                        // no hooks: nothing started
    u->start(&hooks);
    CHECK_EQ(g_stub.uci_starts, 1);
    CHECK(g_stub.ctx == cx);
    const UciHost &uh = g_stub.host;
    uint32_t s = 0, o = 0;
    char img[16], busy[16];
    CHECK_EQ(uh.reu(cx, &s, &o, img, sizeof(img), busy, sizeof(busy)), 0);
    CHECK(s == 16 && o == 4 && !strcmp(img, "reu.img"));
    uint8_t t = 0, a = 0;
    bool p = false;
    CHECK(uh.drive_info(cx, 0, &t, &a, &p) && t == 2 && a == 8 && p);
    CHECK(!uh.drive_info(cx, 1, &t, &a, &p));
    char m[16];
    CHECK_EQ(uh.drive_do(cx, 1, 3, "/x.d64", m, sizeof(m)), 13);
    CHECK_STR(m, "/x.d64");
    uint8_t map[16];
    CHECK(uh.sid_map(cx, map) == 1 && map[0] == 0x42);
    CHECK_STR(uh.memory_file, "/temp/mem.bin");
    CHECK(uh.ef_clear(cx, 8, true) && !uh.ef_clear(cx, 0, true));
    int rt = 0;
    uint32_t rs = 0, rb = 0;
    uh.ramdrive(cx, 1, &rt, &rs, &rb);
    CHECK(rt == 1 && rs == 174848u * 2 && rb == 0x1000);
    uint8_t rgb[16][3] = { { 0 } };
    uh.palette(cx, 2, 5, rgb);
    CHECK_EQ(rgb[0][0], 37);
    // the interface starts once: a second start with other hooks does nothing
    u->start(&hooks);
    CHECK_EQ(g_stub.uci_starts, 1);
    CHECK_EQ(g_stub.host.sid_map(cx, map), 1);
    // a library built against a shorter table: what it does not have answers "the machine has none"
    libexports_register(ld, cx);
    static UciHooks few = { offsetof(UciHooks, sid_map), Hk::reu, Hk::drive_info, Hk::drive_do, 0, 0, 0, 0, 0 };
    u->start(&few);
    CHECK_EQ(g_stub.uci_starts, 2);
    const UciHost &us = g_stub.host;
    CHECK(us.sid_map(cx, map) == 0 && !us.ef_clear(cx, 8, true));
    CHECK(us.memory_file == 0);                                            // the library's default: uci.cc names its own
    rt = 7; rs = 7; rb = 7;
    us.ramdrive(cx, 0, &rt, &rs, &rb);
    CHECK(rt == 0 && rs == 0 && rb == 0);
    CHECK(us.drive_info(cx, 0, &t, &a, &p));                               // the members it has still work
    libexports_register(ld, cx);
    static UciHooks none = { sizeof(uint32_t) };
    u->start(&none);
    CHECK_EQ(g_stub.host.reu(cx, &s, &o, img, sizeof(img), busy, sizeof(busy)), -2);
    CHECK(!g_stub.host.drive_info(cx, 0, &t, &a, &p));
    CHECK_EQ(g_stub.host.drive_do(cx, 0, 0, "x", m, sizeof(m)), 1);
    uint8_t pal0[16][3] = { { 1 } };
    g_stub.host.palette(cx, 0, 0, pal0);                                   // nothing to call: nothing happens
    CHECK_EQ(pal0[0][0], 1);

    // the slot of the interface: ignored unless the core has "uci" and the board did not strike it
    memset(g_slot_regs, 0xEE, 2);
    desc.caps = 0;
    desc.ncaps = 0;
    CHECK_EQ(u->slot_enable(), 0);
    u->set_slot_enable(1);
    u->set_slot_base(0x47);
    CHECK(g_slot_regs[0] == 0xEE && g_slot_regs[1] == 0xEE);               // the core has no UCI: no register is touched
    static const char *const caps[] = { "iec", "uci" };
    desc.caps = caps;
    desc.ncaps = 2;
    cx->board = &board;
    board.probes = 0;
    u->set_slot_enable(1);
    CHECK(g_slot_regs[1] == 0xEE && u->slot_enable() == 0);                // the board's probe struck it
    board.probes = BC_UCI;
    u->set_slot_enable(1);
    u->set_slot_base(0x47);
    CHECK(g_slot_regs[0] == 0x47 && g_slot_regs[1] == 1 && u->slot_enable() == 1);
    u->set_slot_enable(0);
    CHECK(g_slot_regs[1] == 0 && u->slot_enable() == 0);
    cx->board = 0;                                                          // no board profile: the core's own list decides (the old function's case)
    u->set_slot_enable(1);
    CHECK(g_slot_regs[1] == 1);
    desc.caps = 0;
    desc.ncaps = 0;
}

// The table of UciHooks lies in the image of the library that gave it: when that image is let go (here: a load that fails after the library called start) the adapters stop calling
// through it, and a library loaded later may give its own table. uci_start itself runs once.
namespace {
struct FileSource : LibSource {
    std::vector<uint8_t> d;
    explicit FileSource(const char *name)
    {
        std::string p = std::string(FIXDIR) + "/" + name;
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
struct Reg {
    static const UciApi *u;
    static LibLoader *ld;
    static const UciHooks *tab;
    static bool in_image;               // the table is copied into the image the call came from, and the loader is told no image is running (the hook of a base task)
    static void call(const LibHost::Entry &e)
    {
        const UciHooks *t = tab;
        if (in_image) {
            memcpy(e.image, tab, sizeof(UciHooks));
            t = (const UciHooks *)e.image;
            ld->set_current(-1);
        }
        u->start(t);
    }
};
const UciApi *Reg::u;
LibLoader *Reg::ld;
const UciHooks *Reg::tab;
bool Reg::in_image;
struct Hk2 {
    static int sid_map(uint8_t *d) { d[0] = 0x43; return 1; }
};
}

static void uci_hooks_lifetime(CoreCtx *cx)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    h.on_register = Reg::call;
    libexports_register(ld, cx);
    Reg::u = (const UciApi *)find_major("uci", 1, 0);
    Reg::ld = &ld;
    static UciHooks a = { sizeof(UciHooks), 0, 0, 0, Hk::sid_map, 0, 0, 0, 0 };
    static UciHooks b = { sizeof(UciHooks), 0, 0, 0, Hk2::sid_map, 0, 0, 0, 0 };
    static UciHooks mem = { sizeof(UciHooks), 0, 0, 0, Hk::sid_map, "/temp/gone.bin", 0, 0, 0 };
    uint8_t map[16] = { 0 };
    char err[LIB_ERR_MAX];
    int hd = -1;
    // a table of the base's own side (no image runs): a library that is let go does not take it away
    g_stub = ExportsStub();
    Reg::tab = &a;
    Reg::in_image = false;
    Reg::u->start(&a);
    CHECK_EQ(g_stub.uci_starts, 1);
    Reg::tab = 0;
    { FileSource s("testlib.u2a"); CHECK_EQ(ld.load(&s, &hd, err, LIB_ERR_MAX), LIB_REGISTER); }
    Reg::u->start(&b);
    CHECK(g_stub.host.sid_map(cx, map) == 1 && map[0] == 0x42);
    // the call came from the library's own code (the loader says which image runs): its table goes with it, and a later one is taken; uci_start does not run again
    libexports_register(ld, cx);
    g_stub = ExportsStub();
    Reg::tab = &mem;
    { FileSource s("testlib.u2a"); CHECK_EQ(ld.load(&s, &hd, err, LIB_ERR_MAX), LIB_REGISTER); }
    CHECK_EQ(g_stub.uci_starts, 1);
    CHECK_STR(g_stub.host_live->memory_file, "/temp/gone.bin");          // a copy: the library's own string goes with its image
    CHECK(g_stub.host_live->memory_file != (const char *)mem.memory_file);
    Reg::tab = 0;
    Reg::u->start(&b);
    CHECK_EQ(g_stub.uci_starts, 1);
    CHECK(g_stub.host.sid_map(cx, map) == 1 && map[0] == 0x43);
    CHECK(g_stub.host_live->memory_file == 0);                             // the new table names none
    Reg::u->start(&a);                                                     // a table in use is not replaced
    CHECK(g_stub.host.sid_map(cx, map) == 1 && map[0] == 0x43);
    // the table lies inside the image and no image is running: found by its address
    libexports_register(ld, cx);
    g_stub = ExportsStub();
    Reg::tab = &a;
    Reg::in_image = true;
    { FileSource s("testlib.u2a"); CHECK_EQ(ld.load(&s, &hd, err, LIB_ERR_MAX), LIB_REGISTER); }
    Reg::in_image = false;
    CHECK_EQ(g_stub.uci_starts, 1);
    Reg::u->start(&b);
    CHECK(g_stub.host.sid_map(cx, map) == 1 && map[0] == 0x43);
    h.on_register = 0;
}

static void sid_temp_tables(CoreCtx *cx)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    libexports_register(ld, cx);
    const SidApi *a = (const SidApi *)find_major("sid", 1, 0);
    // the numbers of tests/coreapp/test_sidfile.cc, through the table
    uint8_t hd[SID_HDR_READ];
    memset(hd, 0, sizeof(hd));
    memcpy(hd, "PSID", 4);
    hd[5] = 2; hd[7] = 0x7C; hd[8] = 0x10; hd[0x0A] = 0x10; hd[0x0C] = 0x10; hd[0x0D] = 3; hd[0x0F] = 3; hd[0x11] = 2;
    memcpy(hd + 0x16, "Title", 5);
    SidTune t, direct;
    CHECK(a->parse(hd, sizeof(hd), "x.sid", false, &t) && sid_parse(hd, sizeof(hd), "x.sid", false, &direct));
    CHECK(t.songs == 3 && t.version == 2 && !memcmp(&t, &direct, sizeof(t)));
    char txt[256], txt2[256];
    int n = a->info_text(&t, txt, sizeof(txt));
    CHECK(n > 0 && n == sid_info_text(direct, txt2, sizeof(txt2)) && !memcmp(txt, txt2, n) && strstr(txt, "Number of songs: 3"));
    const char *why = "unset";
    CHECK_EQ(a->plan(&t, 0x7C + 0x100, 0, 0, &why), 0);
    CHECK(t.start == 0x1000 && t.end == 0x1000 + 0x100 - 2 && t.song == 2 && t.header_location == 0x0340);
    CHECK(a->plan(&t, 0x7C + 0x100, 0, 9, &why) != 0 && why && strcmp(why, "unset"));
    CHECK(a->plan(&t, 0x7C + 0x100, 0, 9, 0) != 0);                         // `why` may be null
    SidReq rq[3];
    CHECK(a->requests(&t, rq) >= 1 && rq[0].base == 0x40);
    CHECK(a->parse(0, 0, "My_Tune.mus", true, &t) && !strcmp((const char *)t.hdr + 0x16, "My Tune"));
    struct R { static uint8_t rd(void *, uint32_t) { return 0; } };
    uint32_t first = 0, first2 = 0;
    CHECK_EQ(a->mus_stereo(&t, 0x400, R::rd, 0, &first), sid_mus_stereo(t, 0x400, R::rd, 0, &first2));
    CHECK_EQ(first, first2);
    SidTune tm = t, td = t;
    a->mus_header(&tm, true);
    sid_mus_header(&td, true);
    CHECK(!memcmp(&tm, &td, sizeof(tm)));
    // tempdisk
    const TempDiskApi *d = (const TempDiskApi *)find_major("tempdisk", 1, 0);
    d->set(true, false);
    CHECK(g_stub.temp_auto == 1 && g_stub.temp_sub == 0);
    d->set(false, true);
    CHECK(g_stub.temp_auto == 0 && g_stub.temp_sub == 1);
}


// The VC-20's numbers (the machine cbm_prg.cc was read from), as the caller's CbmPrgMachine
static CbmPrgMachine vc20_machine(void)
{
    CbmPrgMachine m;
    memset(&m, 0, sizeof(m));
    m.size = sizeof(m);
    m.basic_starts[0] = 0x1001; m.basic_starts[1] = 0x0401; m.basic_starts[2] = 0x1201;
    m.nbasic_starts = 3;
    m.txttab = 0x2B; m.txttab_min = 0x0400; m.txttab_max = 0x2000;
    m.vartab = 0x2D; m.arytab = 0x2F; m.strend = 0x31;
    m.kbd_buffer = 0x0277; m.kbd_count = 0xC6;
    m.ready_ms = 2500; m.p00_header = 0x1A;
    m.min_size = 3; m.max_size = 0x10020;
    return m;
}

// "10 PRINT" twice as a .prg at `load`
static std::vector<uint8_t> basic_prg(uint16_t load)
{
    std::vector<uint8_t> v;
    uint16_t l2 = (uint16_t)(load + 6), l3 = (uint16_t)(l2 + 6);
    const uint8_t b[] = { (uint8_t)load, (uint8_t)(load >> 8), (uint8_t)l2, (uint8_t)(l2 >> 8), 10, 0, 0x99, 0, (uint8_t)l3, (uint8_t)(l3 >> 8), 20, 0, 0x99, 0, 0, 0 };
    v.assign(b, b + sizeof(b));
    return v;
}

static void prg_table(CoreCtx *cx, WinIo &io)
{
    Host h;
    OpRegistry reg;
    LibLoader ld(&h, &reg, 3);
    libexports_register(ld, cx);
    const CbmPrgApi *a = (const CbmPrgApi *)find_major("cbmprg", 1, 0);
    CHECK(a);
    CbmPrgMachine m = vc20_machine();
    char msg[160];
    // a BASIC program saved for $1001 on a machine that says BASIC starts at $1201: relinked and placed there, the three pointers follow
    io.ram_write(m.txttab, "\x01\x12", 2);
    std::vector<uint8_t> f = basic_prg(0x1001);
    CbmPrgPlaced r;
    CHECK_EQ(a->place(f.data(), (uint32_t)f.size(), &m, false, &r, msg, sizeof(msg)), 0);
    CHECK(r.basic && r.load == 0x1201 && r.end == 0x1201 + 14 && r.start == 0);
    uint8_t out[14];
    a->mem_read(&m, 0x1201, out, sizeof(out));
    CHECK_EQ(out[0] | (out[1] << 8), 0x1207);                              // the first link moved by 0x200
    CHECK_EQ(out[6] | (out[7] << 8), 0x120D);
    CHECK(out[12] == 0 && out[13] == 0);
    uint8_t ptr[2];
    io.ram_read(m.vartab, ptr, 2);
    CHECK_EQ(ptr[0] | (ptr[1] << 8), 0x1201 + 14);
    // the same bytes through the model's own call (cbm_prg.cc): the table is that code with the core's io
    uint8_t direct[14];
    io.ram_read(0x1201, direct, sizeof(direct));
    CHECK(!memcmp(direct, out, sizeof(out)));
    // typed start: RUN for BASIC, SYS<start> for a machine-language file
    a->type_start(&m, &r);
    uint8_t kb[5];
    io.ram_read(m.kbd_buffer, kb, 4);
    uint8_t cnt;
    io.ram_read(m.kbd_count, &cnt, 1);
    CHECK(!memcmp(kb, "RUN\r", 4) && cnt == 4);
    uint8_t ml[] = { 0x00, 0x20, 0xA9, 0x01, 0x60 };                         // $2000: LDA #1, RTS
    CHECK_EQ(a->place(ml, sizeof(ml), &m, false, &r, msg, sizeof(msg)), 0);
    CHECK(!r.basic && r.load == 0x2000 && r.end == 0x2003 && r.start == 0x2000);
    a->type_start(&m, &r);
    io.ram_read(m.kbd_buffer, kb, 5);
    CHECK(!memcmp(kb, "SYS8192\r", 5));
    // a refusal: the window of BASIC's start; the reason comes back, also with no buffer given
    io.ram_write(m.txttab, "\x00\x40", 2);
    CHECK_EQ(a->place(f.data(), (uint32_t)f.size(), &m, false, &r, msg, sizeof(msg)), 1);
    CHECK(strstr(msg, "cannot be right") != 0);
    CHECK_EQ(a->place(f.data(), (uint32_t)f.size(), &m, false, &r, 0, 0), 1);
    CHECK_EQ(a->place(f.data(), 2, &m, false, &r, msg, sizeof(msg)), 1);          // two bytes: below min_size 3 plus nothing
    CHECK(strstr(msg, "not a program") != 0);
    // a machine checked by its size: one that stops before max_size is no machine, a shorter one than ours reads its missing members as 0
    CbmPrgMachine bad = m;
    bad.size = offsetof(CbmPrgMachine, max_size);
    io.ram_write(m.txttab, "\x01\x12", 2);
    CHECK_EQ(a->place(f.data(), (uint32_t)f.size(), &bad, false, &r, msg, sizeof(msg)), 1);
    CHECK(strstr(msg, "No machine") != 0);
    CHECK_EQ(a->place(f.data(), (uint32_t)f.size(), 0, false, &r, msg, sizeof(msg)), 1);
    uint8_t before[2] = { 0x55, 0x55 }, zeros[2] = { 0x55, 0x55 };
    io.ram_write(0x3000, before, 2);
    a->mem_write(&bad, 0x3000, "\x01\x02", 2);                               // no machine: nothing written
    io.ram_read(0x3000, zeros, 2);
    CHECK(zeros[0] == 0x55 && zeros[1] == 0x55);
    a->mem_read(&bad, 0x3000, zeros, 2);                                   // ... and zeros read
    CHECK(zeros[0] == 0 && zeros[1] == 0);
    CbmPrgMachine old = m;                                                 // a caller built before mem_ddr existed: its size ends at max_size, mem_ddr is 0
    old.size = offsetof(CbmPrgMachine, max_size) + sizeof(old.max_size);
    old.mem_ddr = 0x1000;                                                  // past its size: not read
    a->mem_write(&old, 0x3000, "\x07\x08", 2);
    io.ram_read(0x3000, zeros, 2);
    CHECK(zeros[0] == 7 && zeros[1] == 8);
    uint8_t d0[2];
    io.ddr_read(0x1000 + 0x3000, d0, 2);
    CHECK(d0[0] == 0 && d0[1] == 0);
    // mem_ddr: the DDR2 window as well, and the read comes from there
    CbmPrgMachine scpu = m;
    scpu.mem_ddr = 0x20000;
    a->mem_write(&scpu, 0x3002, "\x09\x0A", 2);
    io.ddr_read(0x20000 + 0x3002, d0, 2);
    CHECK(d0[0] == 9 && d0[1] == 0x0A);
    io.ram_read(0x3002, zeros, 2);
    CHECK(zeros[0] == 9 && zeros[1] == 0x0A);
    uint8_t viaddr[2] = { 0, 0 };
    a->mem_read(&scpu, 0x3002, viaddr, 2);
    CHECK(viaddr[0] == 9 && viaddr[1] == 0x0A);
    // the script: checked, then typed piece by piece when the buffer is empty
    CHECK_EQ(a->script_check("LOAD\"X\",8|RUN", msg, sizeof(msg)), 0);
    CHECK_EQ(a->script_check("12345678901|RUN", msg, sizeof(msg)), 1);
    CHECK(strstr(msg, "11 keys") != 0);
    CHECK_EQ(a->script_check("12345678901", 0, 0), 1);                     // no buffer given: the same answer
    uint8_t zero = 0;
    io.ram_write(m.kbd_count, &zero, 1);
    int pos = 0;
    CHECK_EQ(a->script_step(&m, "AB|CD", &pos), 0);
    io.ram_read(m.kbd_buffer, kb, 2);
    io.ram_read(m.kbd_count, &cnt, 1);
    CHECK(kb[0] == 'A' && kb[1] == 'B' && cnt == 2 && pos == 3);
    CHECK_EQ(a->script_step(&m, "AB|CD", &pos), 0);                        // the machine has not taken the first piece
    io.ram_write(m.kbd_count, &zero, 1);
    CHECK_EQ(a->script_step(&m, "AB|CD", &pos), 0);
    io.ram_read(m.kbd_buffer, kb, 2);
    CHECK(kb[0] == 'C' && kb[1] == 'D' && pos == 5);
    io.ram_write(m.kbd_count, &zero, 1);
    CHECK_EQ(a->script_step(&m, "AB|CD", &pos), 1);                        // all in and taken
    // relink on a buffer and the key of a character
    std::vector<uint8_t> g = basic_prg(0x1001);
    CHECK_EQ(a->relink(g.data() + 2, (uint32_t)g.size() - 2, 0x1001, 0x0801), 0x0801 + 14);
    CHECK_EQ(g[2] | (g[3] << 8), 0x0807);
    uint8_t idx = 0;
    bool shift = true;
    CHECK(a->key_for('A', &idx, &shift) && idx == 10 && !shift);
    CHECK(a->key_for('!', &idx, &shift) && shift);
    CHECK(!a->key_for(0x01, &idx, &shift));
}

// The slot of a shared engine is told by its name (ctx.h): the index comes from the running core's [mount] list
static int g_told[4], g_told_n;
static void told(CoreCtx *, int slot) { if (g_told_n < 4) g_told[g_told_n++] = slot; }
static const char *slot_spec_none(CoreCtx *, int) { return 0; }
static void notify_by_name()
{
    static const char *const c64_order[] = { "Drive A", "Cartridge" };
    static const char *const other_order[] = { "Tape", "Cartridge", "Drive A" };
    MountSlots c64s = { 2, c64_order, slot_spec_none, 0, 0 };
    MountSlots other = { 3, other_order, slot_spec_none, 0, 0 };
    CoreDesc d;
    memset(&d, 0, sizeof(d));
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &d;
    c.mount_changed = told;
    g_told_n = 0;
    d.mount_slots = &c64s;
    mount_notify_name(&c, "Cartridge");
    mount_notify_name(&c, "Drive A");
    CHECK(g_told_n == 2 && g_told[0] == 1 && g_told[1] == 0);
    d.mount_slots = &other;                                                // another order: the same names reach the other indexes
    mount_notify_name(&c, "Cartridge");
    mount_notify_name(&c, "Drive A");
    CHECK(g_told_n == 4 && g_told[2] == 1 && g_told[3] == 2);
    g_told_n = 0;
    mount_notify_name(&c, "Disk");                                         // a name the core does not list: nothing
    mount_notify_name(&c, 0);
    d.mount_slots = 0;                                                     // a core without a list: nothing
    mount_notify_name(&c, "Cartridge");
    c.desc = 0;
    mount_notify_name(&c, "Cartridge");
    CHECK_EQ(g_told_n, 0);
    CHECK_EQ(mount_slot_index(0, "Cartridge"), -1);
    d.mount_slots = &other;
    c.desc = &d;
    CHECK_EQ(mount_slot_index(&c, "cartridge"), 1);                        // without regard to case
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    WinIo io;
    CoreDesc desc;
    memset(&desc, 0, sizeof(desc));
    BoardProfile board;
    memset(&board, 0, sizeof(board));
    CoreCtx cx;
    memset(&cx, 0, sizeof(cx));
    cx.io = &io;
    cx.desc = &desc;
    base_list();
    tables(&cx);
    cart_table(&cx, io, desc);
    drive_table(&cx, io);
    uci_table(&cx, desc, board);
    uci_hooks_lifetime(&cx);
    sid_temp_tables(&cx);
    prg_table(&cx, io);
    notify_by_name();
    DONE("test_exports");
}
