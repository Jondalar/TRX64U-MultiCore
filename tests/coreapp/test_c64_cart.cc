// T104.2: "Run Cartridge" for the .crt of the Library (the library c64cart), on the C64 model. A CRT is parsed, its CHIP packets land where the bitstream's cartridge
// emulation reads the image (ROM_SEL = 1, DDR2 window offset 0), CART_TYPE and the mapping descriptor are what Gideon's set_cartridge writes, the
// REU is off, the cartridge RAM is cleared, and the machine is reset into the cartridge (type set while it is held). A file this application cannot
// start is refused with the type named, before the machine is touched. The library is the real one; the machine is C64Model, and the descriptor is a stub that does
// what a C64 core's reset does to the cartridge port: hold the machine and end the SID player's cartridge (T239.3 P1-8: the trx64u descriptor is TRX64-Ultimate's).
#include <string>
#include "check.h"
#include "c64_model.h"
#include "c64_cart.h"
#include "c64_cart_desc.h"

const CoreDesc *const core_table[] = { 0 };

static const char *ev_last(const std::vector<std::string> &e) { return e.empty() ? "(none)" : e.back().c_str(); }

// the stub machine: init is the reset's hold, release lets it go; the player's cartridge ends with the reset as the C64 core's init does
static int stub_init(CoreCtx *c, char *, int)
{
    c->io->mode_reset(true);
    c64_cart_player_end(c);
    return 0;
}
static int stub_release(CoreCtx *c, char *, int)
{
    c->io->mode_reset(false);
    return 0;
}
static CoreDesc make_desc()
{
    CoreDesc d;
    memset(&d, 0, sizeof(d));
    d.init = stub_init;
    d.release = stub_release;
    d.reset_ms = 1;
    return d;
}

struct C64Rig {
    C64Model io;
    FakeFs fs;
    CoreDesc desc = make_desc();
    CoreCtx c;
    explicit C64Rig(bool = true)
    {
        memset(&c, 0, sizeof(c));
        c.desc = &desc;
        c.io = &io;
        c.fs = &fs;
    }
    void menu_op_begin() { io.stop(true); }
    void cold_reset()                                       // what machine.cold_reset does to the machine: the hold, the release
    {
        char m[80];
        desc.init(&c, m, sizeof(m));
        desc.release(&c, m, sizeof(m));
    }
};

typedef std::vector<uint8_t> Bytes;

static void put16(Bytes &b, uint16_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
static void put32(Bytes &b, uint32_t v) { put16(b, (uint16_t)(v >> 16)); put16(b, (uint16_t)v); }

static Bytes crt_header(uint16_t type, uint8_t exrom, uint8_t game)
{
    Bytes b;
    const char *sig = "C64 CARTRIDGE   ";
    b.insert(b.end(), sig, sig + 16);
    put32(b, 0x40);
    put16(b, 0x0100);
    put16(b, type);
    b.push_back(exrom); b.push_back(game);
    b.resize(0x40, 0);
    return b;
}
// one CHIP packet: `size` bytes of `fill`+index so that a byte in the wrong place is a wrong byte
static void chip(Bytes &b, uint16_t bank, uint16_t load, uint16_t size, uint8_t fill)
{
    const char *sig = "CHIP";
    b.insert(b.end(), sig, sig + 4);
    put32(b, 16u + size);
    put16(b, 0);                                               // ROM
    put16(b, bank);
    put16(b, load);
    put16(b, size);
    for (int i = 0; i < size; i++) b.push_back((uint8_t)(fill + i));
}

// the DDR2 window as the cartridge emulation reads it (C64Model's own `ddr` is the 6510's data direction register)
static uint8_t img(C64Model &io, size_t off) { return io.FakeIo::ddr.at(off); }

static int run(C64Rig &r, const char *path, OpResult *res)
{
    r.menu_op_begin();
    return c64_cart_run(&r.c, path, res->msg, sizeof(res->msg)) ? OPE_FAILED : OPE_OK;
}

static C64Rig *booted()
{
    C64Rig *r = new C64Rig(true);
    r->io.events.clear();
    r->io.log.clear();
    return r;
}

static const uint8_t *desc_of(uint8_t t)
{
    for (unsigned i = 0; i < sizeof(c64_cart_descs) / sizeof(c64_cart_descs[0]); i++) if (c64_cart_descs[i].cart_type == t) return c64_cart_descs[i].desc;
    return 0;
}
static bool desc_written(C64Model &io, uint8_t t)
{
    const uint8_t *d = desc_of(t);
    if (!d) return false;
    for (int i = 0; i < C64_CART_DESC_BYTES; i++) if (io.p4_get(C64_CART_DESC + i) != d[i]) return false;
    return true;
}
static int index_of(const std::vector<std::string> &e, const std::string &s)
{
    for (size_t i = 0; i < e.size(); i++) if (e[i] == s) return (int)i;
    return -1;
}

int main()
{
    OpResult res;

    // ---- an 8 KB cartridge (EXROM 0, GAME 1): CART_TYPE 0x41, the descriptor, the image at ROM_SEL 1, a cold reset into it
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(0, 0, 1);
        chip(f, 0, 0x8000, 0x2000, 0x10);
        r.fs.put("/usb0/eight.crt", f);
        r.io.cartreg[C64_CARTREG_REU] = 1;                         // the REU was on
        CHECK_EQ(run(r, "/usb0/eight.crt", &res), OPE_OK);
        C64Model &io = r.io;
        CHECK_EQ(io.cartreg[C64_CARTREG_TYPE], 0x41);
        CHECK(desc_written(io, 0x41));
        CHECK_EQ(io.p4_get(C64_CART_FL_ENABLE), 0);                // the flash responder stays off
        CHECK_EQ(io.p4_get(C64_CART_ROM_SEL), 1);
        CHECK_EQ(img(io, 0), 0x10);
        CHECK_EQ(img(io, 0x1FFF), (uint8_t)(0x10 + 0x1FFF));
        CHECK_EQ(img(io, 0x2000), 0xFF);                            // erased where the file has nothing
        CHECK_EQ(io.cartreg[C64_CARTREG_REU], 0);
        CHECK_EQ(io.busreg[C64_BUSREG_INTERNAL], 15);
        CHECK_EQ(io.busreg[C64_BUSREG_EXTERNAL], 0);
        CHECK(io.cart_ram[0] == 0 && io.cart_ram[0xFFFF] == 0);    // the cartridge's RAM was 0xAA
        // the type is set with the machine held, and the last thing that happens is the release
        int held = index_of(io.events, "mode_reset"), ty = index_of(io.events, "cart_type 65"), rel = index_of(io.events, "mode_unreset");
        CHECK(held >= 0 && ty > held && rel > ty);
        CHECK_STR(ev_last(io.events), "mode_unreset");
        delete &r;
    }
    // ---- the lines pick the type: 16 KB, Ultimax; an Ultimax cartridge's ROMH chip lands at +0x2000 of its bank
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(0, 0, 0);
        chip(f, 0, 0x8000, 0x4000, 0x20);
        r.fs.put("/usb0/sixteen.crt", f);
        CHECK_EQ(run(r, "/usb0/sixteen.crt", &res), OPE_OK);
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0x01);
        CHECK_EQ(img(r.io, 0x3FFF), (uint8_t)(0x20 + 0x3FFF));
        Bytes u = crt_header(0, 1, 0);
        chip(u, 0, 0xE000, 0x2000, 0x30);
        r.fs.put("/usb0/umax.crt", u);
        CHECK_EQ(run(r, "/usb0/umax.crt", &res), OPE_OK);
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0xA1);
        CHECK_EQ(img(r.io, 0x2000), 0x30);                          // $E000 -> bank 0, +0x2000
        CHECK_EQ(img(r.io, 0), 0xFF);
        delete &r;
    }
    // ---- Ocean: 8 KB banks -> 0x08; with a chip at $A000 16 KB banks -> 0x69; the banks repeat up to the window
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(5, 0, 0);
        for (int b = 0; b < 4; b++) chip(f, (uint16_t)b, 0x8000, 0x2000, (uint8_t)(0x40 + b * 16));
        r.fs.put("/usb0/ocean.crt", f);
        CHECK_EQ(run(r, "/usb0/ocean.crt", &res), OPE_OK);
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0x08);
        CHECK(desc_written(r.io, 0x08));
        CHECK_EQ(img(r.io, 3 * 0x4000), 0x70);
        CHECK_EQ(img(r.io, 4 * 0x4000), 0x40);                      // bank 4 is bank 0 again
        CHECK_EQ(img(r.io, 0x400000 - 0x4000), 0x70);               // ... to the end of the 4 MiB window
        Bytes g = crt_header(5, 0, 0);
        chip(g, 0, 0x8000, 0x2000, 0x50);
        chip(g, 0, 0xA000, 0x2000, 0x60);
        r.fs.put("/usb0/ocean16.crt", g);
        CHECK_EQ(run(r, "/usb0/ocean16.crt", &res), OPE_OK);
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0x69);
        CHECK_EQ(img(r.io, 0x2000), 0x60);
        delete &r;
    }
    // ---- EasyFlash: ROML and ROMH of every bank, the chip's 64 banks present whatever the file holds
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(32, 1, 0);
        chip(f, 0, 0x8000, 0x2000, 0x01);
        chip(f, 0, 0xA000, 0x2000, 0x02);
        r.fs.put("/usb0/ef.crt", f);
        CHECK_EQ(run(r, "/usb0/ef.crt", &res), OPE_OK);
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0x11);
        CHECK(desc_written(r.io, 0x11));
        CHECK_EQ(img(r.io, 0), 0x01);
        CHECK_EQ(img(r.io, 0x2000), 0x02);
        CHECK_EQ(img(r.io, 1 * 0x4000), 0xFF);                      // bank 1: erased flash, not a copy of bank 0
        CHECK_EQ(img(r.io, 63 * 0x4000), 0xFF);
        CHECK_EQ(img(r.io, 64 * 0x4000), 0x01);                     // above the chip it repeats
        delete &r;
    }
    // ---- a chip below 8 KB repeats up to 8 KB
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(0, 0, 1);
        chip(f, 0, 0x8000, 0x1000, 0x77);
        r.fs.put("/usb0/four.crt", f);
        CHECK_EQ(run(r, "/usb0/four.crt", &res), OPE_OK);
        CHECK_EQ(img(r.io, 0), 0x77);
        CHECK_EQ(img(r.io, 0x1000), 0x77);
        CHECK_EQ(img(r.io, 0x1FFF), (uint8_t)(0x77 + 0xFFF));
        delete &r;
    }
    // ---- refused before the machine is touched: the type named, nothing reset, nothing written
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(3, 0, 0);
        chip(f, 0, 0x8000, 0x2000, 0);
        r.fs.put("/usb0/fc3.crt", f);
        CHECK_EQ(run(r, "/usb0/fc3.crt", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "type 3") && strstr(res.msg, "Final Cartridge III"));
        Bytes u = crt_header(200, 0, 0);
        chip(u, 0, 0x8000, 0x2000, 0);
        r.fs.put("/usb0/odd.crt", u);
        CHECK_EQ(run(r, "/usb0/odd.crt", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "type 200"));
        r.fs.put("/usb0/junk.crt", Bytes(100, 0x41));
        CHECK_EQ(run(r, "/usb0/junk.crt", &res), OPE_FAILED);
        Bytes cut = crt_header(0, 0, 1);
        chip(cut, 0, 0x8000, 0x2000, 0);
        cut.resize(cut.size() - 100);
        r.fs.put("/usb0/cut.crt", cut);
        CHECK_EQ(run(r, "/usb0/cut.crt", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "cut off"));
        Bytes none = crt_header(0, 0, 1);
        r.fs.put("/usb0/empty.crt", none);
        CHECK_EQ(run(r, "/usb0/empty.crt", &res), OPE_FAILED);
        CHECK(r.io.events.empty());
        CHECK(r.io.p4.empty());
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0);
        delete &r;
    }
    // ---- Remove Cartridge: type 0, the descriptor zero, a reset; and a Reset after Run Cartridge keeps the cartridge
    {
        C64Rig &r = *booted();
        Bytes f = crt_header(0, 0, 1);
        chip(f, 0, 0x8000, 0x2000, 0x10);
        r.fs.put("/usb0/eight.crt", f);
        CHECK_EQ(run(r, "/usb0/eight.crt", &res), OPE_OK);
        r.menu_op_begin();
        r.cold_reset();
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0x41);
        r.io.events.clear();
        r.menu_op_begin();
        CHECK_EQ(c64_cart_remove(&r.c, res.msg, sizeof(res.msg)), 0);
        CHECK_EQ(r.io.cartreg[C64_CARTREG_TYPE], 0);
        bool zero = true;
        for (int i = 0; i < C64_CART_DESC_BYTES; i++) zero = zero && r.io.p4_get(C64_CART_DESC + i) == 0;
        CHECK(zero);
        CHECK(index_of(r.io.events, "cart_type 0") < index_of(r.io.events, "mode_unreset"));
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        delete &r;
    }
    DONE("test_c64_cart");
}
