// T83 on the generic application: the VIC-20 descriptor (coreapp/cores/vc20.cc) against a model of the register block and the ROM aperture of
// cores/vc20/rtl/trx/vc20_regs.vhd / vc20_glue.vhd. The three ROMs at the C64's aperture offsets, the memory expansion as two register settings
// written only while the machine is held, Reset and Reload ROMs, a .prg run (reset, READY, relink to the start of BASIC the machine states,
// RUN or SYS typed) and loaded without a reset, Freeze's PAUSED bit. The descriptor and its operations are the real ones.
#include <algorithm>
#include "cbm_rig.h"

// The register file as the bench reads cores/vc20/rtl/trx/vc20_regs.vhd - literal numbers on purpose, so that a wrong constant in the product's
// own header is not shared with the model
#define T20_RAM_EXT   0x0A1
#define T20_RAM_RO    0x0A2
#define T20_STATUS    0x0A4
#define T20_ST_HELD   0x02
#define T20_FB_LATEST 0x0C0
#define T20_FRAME_SEQ 0x0C1
#define T20_FB_BASE   0x0C4
#define T20_FB_STRIDE 0x0C8
#define T20_FB_ADDR   0x01D00000u
#define T20_FB_STRIDE_BYTES 0x00100000u
#define T20_KBD_BUF   0x0277
#define T20_KBD_COUNT 0xC6

const CoreDesc *const core_table[] = { &vc20_desc, 0 };

struct Vc20Io;
static uint32_t reg32(Vc20Io &io, uint32_t r);

struct Vc20Io : CbmIo {
    Bytes basic, kernal, chr;
    uint8_t ext_latched = 0;
    bool deaf = false;                       // the aperture takes no writes (a core that is not there, or a window that is shut)
    Vc20Io() : CbmIo(T20_STATUS, T20_KBD_COUNT), basic(8192, 0), kernal(8192, 0), chr(4096, 0)
    {
        taken_in_reset = { T20_RAM_EXT, T20_RAM_RO };
    }
    void latch() { ext_latched = regs[T20_RAM_EXT]; }
    // BASIC's start pointer follows what is expanded: the 3K alone moves it to $0401, an 8K block to $1201, nothing leaves $1001
    uint16_t boot_txttab() { return (ext_latched & 0x0E) ? 0x1201 : (ext_latched & 0x01) ? 0x0401 : 0x1001; }
    // the aperture as the glue decodes it: +0 BASIC, +0x2000 KERNAL, +0x4000 CHAR (the 4 KB mirrored above)
    uint8_t *cell(uint32_t off)
    {
        off &= 0x7FFF;
        if (off < 0x2000) return &basic[off];
        if (off < 0x4000) return &kernal[off - 0x2000];
        if (off < 0x5000) return &chr[off - 0x4000];
        return 0;
    }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        events.push_back("rom_write");
        for (uint32_t i = 0; i < n && !deaf; i++) if (uint8_t *x = cell(off + i)) *x = ((const uint8_t *)p)[i];
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++) { uint8_t *x = cell(off + i); ((uint8_t *)p)[i] = x ? *x : 0; }
    }
};

static Bytes pattern(int role, size_t n)
{
    Bytes v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + role * 61 + (i >> 8));
    return v;
}

static uint32_t reg32(Vc20Io &io, uint32_t r) { return io.regs[r] | (io.regs[r + 1] << 8) | (io.regs[r + 2] << 16) | ((uint32_t)io.regs[r + 3] << 24); }

struct Rig {
    Vc20Io io;
    FakeFs fs;
    PxRec px;
    CoreCtx c;
    Settings *st;
    Video *vid;
    CbmMenu menu;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &vc20_desc; c.io = &io; c.fs = &fs;
        st = new Settings(vc20_desc.settings, vc20_desc.nsettings, vc20_desc.store, &fs, "/sd/cores/vc20.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(vc20_desc.video, &px, &io);
        c.video = vid;
        menu = { &c, &io, &vc20_desc, "vc20.service", "vc20.pending" };
    }
    void roms(const char *dir = "/usb0/roms/vc20", const char *b = "basic.bin", const char *k = "kernal.bin", const char *ch = "char.bin")
    {
        fs.put(std::string(dir) + "/" + b, pattern(1, 8192));
        fs.put(std::string(dir) + "/" + k, pattern(2, 8192));
        fs.put(std::string(dir) + "/" + ch, pattern(3, 4096));
    }
    bool boot()
    {
        BoundedUi ui;
        BootResult r = boot_start(&c, &ui);
        io.delay_ms(io.boot_ms + io.editor_ms + 50);
        return r == BOOT_RUNNING && io.ready;
    }
    void expansion(int e) { st->set(st->find("Memory Expansion (at next reset)"), e); }
    void quiet() { io.events.clear(); io.log.clear(); io.bad_writes = 0; io.typed_early = false; }
    std::string keys() { return std::string((const char *)&io.ram[T20_KBD_BUF], io.ram[T20_KBD_COUNT]); }
    uint16_t w(uint32_t a) { return (uint16_t)(io.ram[a] | (io.ram[a + 1] << 8)); }
};

int main()
{
    const Bytes want_b = pattern(1, 8192), want_k = pattern(2, 8192), want_c = pattern(3, 4096);
    OpResult res;

    // ---- the start: ROMs at the C64's aperture offsets, put with the machine held; the expansion registers written in that hold; released last
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        CHECK(r.io.basic == want_b);
        CHECK(r.io.kernal == want_k);
        CHECK(r.io.chr == want_c);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        CHECK_EQ(r.io.reset_releases, 1);
        int last_rom = -1, rel = -1;
        for (size_t i = 0; i < r.io.events.size(); i++) {
            if (r.io.events[i] == "rom_write") last_rom = (int)i;
            if (r.io.events[i] == "mode_unreset") rel = (int)i;
        }
        CHECK(last_rom >= 0 && last_rom < rel);                  // every ROM byte went in before the machine was released
        bool ext_written = false;
        for (auto &w : r.io.log) ext_written = ext_written || w.reg == T20_RAM_EXT;
        CHECK(ext_written);                                      // the start writes the expansion once, in the hold
        CHECK_EQ(r.io.bad_writes, 0);
        CHECK_EQ(r.io.regs[T20_RAM_EXT], 0);
        CHECK_EQ(r.io.regs[T20_RAM_RO], 0);
        // the picture (T119): the buffers announced to the core, the layer set up for 376 x 283 at 3 x 3 as vc20_regs.vhd resets them
        CHECK_EQ(vc20_desc.video.w, 376);
        CHECK_EQ(vc20_desc.video.h, 283);
        CHECK_EQ(vc20_desc.video.scale, 3);
        CHECK_EQ(vc20_desc.video.nbuf, 4);
        CHECK(!r.vid->own_picture());
        CHECK_EQ(reg32(r.io, T20_FB_BASE), T20_FB_ADDR);
        CHECK_EQ(reg32(r.io, T20_FB_STRIDE), T20_FB_STRIDE_BYTES);
        r.vid->show();
        CHECK_EQ(r.px.last(PX_SIZE), (283u << 16) | 376u);
        CHECK_EQ(r.px.last(PX_STRIDE), 752u);
        CHECK_EQ(r.px.last(PX_POS), (115u << 16) | 396u);                       // 1128 x 849 centred on 1920 x 1080
        CHECK_EQ(r.px.last(PX_CTRL), Video::ctrl_value(3, true));
        CHECK_EQ(r.px.last(PX_FRONT), T20_FB_ADDR);
        r.io.regs[T20_FB_LATEST] = 0x82;
        r.io.regs[T20_FRAME_SEQ] = 7;
        CHECK(r.vid->poll());
        CHECK_EQ(r.px.last(PX_BACK), T20_FB_ADDR + 2 * T20_FB_STRIDE_BYTES);
        // a ROM the core does not take (read-back differs) and a blob of the wrong size are refused by the operation, with the reason
        {
            OpIn in = in_blob("data", want_c.data(), (uint32_t)want_c.size());
            Bytes other = pattern(8, 4096);                          // not what the aperture holds, so that a write that did not happen shows
            OpIn in_other = in_blob("data", other.data(), (uint32_t)other.size());
            r.io.deaf = true;
            CHECK_EQ(ops_call(&r.c, "vc20.put_char", &in_other, 1, &res), OPE_FAILED);
            CHECK(strstr(res.msg, "read-back") != 0 && strstr(res.msg, "CHAR") != 0);
            r.io.deaf = false;
            OpIn small = in_blob("data", want_c.data(), 4000);
            CHECK_EQ(ops_call(&r.c, "vc20.put_char", &small, 1, &res), OPE_FAILED);
            CHECK(strstr(res.msg, "4000") != 0);
            CHECK_EQ(ops_call(&r.c, "vc20.put_char", &in, 1, &res), OPE_OK);
        }
        // Freeze: C64_STOP raises the descriptor's PAUSED bit in time
        CbmFreezeOps ops(&r.io, &vc20_desc);
        Freeze f(vc20_desc.freeze, &ops);
        CHECK(f.enter());
        CHECK(ops.logs.empty());
        f.leave();
        CHECK(!r.io.stopped);
    }

    // ---- the names the old application tried: the fourth KERNAL name, the sixth CHAR name, a Commodore part name for BASIC
    {
        Rig r;
        r.fs.put("/usb0/roms/vc20/basic-901486-01.bin", pattern(1, 8192));
        r.fs.put("/usb0/roms/vc20/kernal.901486-06.bin", pattern(2, 8192));
        r.fs.put("/usb1/roms/vc20/chargen-901460-02.bin", pattern(3, 4096));      // another root: the roots are searched one by one
        CHECK(r.boot());
        CHECK(r.io.basic == want_b && r.io.kernal == want_k && r.io.chr == want_c);
    }
    // ---- a CHAR file of the wrong size is told at once, by name; nothing starts
    {
        Rig r;
        r.roms();
        r.fs.put("/usb0/roms/vc20/char.bin", Bytes(8192, 1));
        RomResult rr[3];
        char summary[300];
        CHECK_EQ(rom_scan(&r.fs, vc20_desc.folder, vc20_desc.roms, vc20_desc.nroms, rr, summary, sizeof(summary), 0), ROM_WRONG_SIZE);
        CHECK(strstr(summary, "CHAR") != 0 && strstr(summary, "4096") != 0);
    }

    // ---- the settings: every expansion reaches RAM_EXT with its bits, written while the machine is held, taken with a reset
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        static const uint8_t bits[] = { 0x00, 0x01, 0x02, 0x06, 0x0E, 0x0F, 0x1F };
        char msg[200];
        for (int e = 0; e < 7; e++) {
            r.quiet();
            r.expansion(e);
            CHECK_EQ(r.io.regs[T20_RAM_EXT], e ? bits[e - 1] : bits[e]);        // choosing alone writes nothing: it is the next reset's
            r.io.regs[T20_RAM_EXT] = 0xEE;                                        // so that a missing write is seen
            CHECK_EQ(boot_cold_reset(&r.c, msg, sizeof(msg)), 0);
            CHECK_EQ(r.io.regs[T20_RAM_EXT], bits[e]);
            CHECK_EQ(r.io.bad_writes, 0);
            CHECK_STR(ev_first(r.io.events), "mode_reset");
            CHECK_STR(ev_last(r.io.events), "mode_unreset");
        }
        // block 5 as a ROM is the read-only bit of the $A000 block; as RAM it is off
        r.st->set(r.st->find("Block 5 ($A000), when expanded"), 1);
        CHECK_EQ(boot_cold_reset(&r.c, msg, sizeof(msg)), 0);
        CHECK_EQ(r.io.regs[T20_RAM_RO], 0x10);
        r.st->set(r.st->find("Block 5 ($A000), when expanded"), 0);
        CHECK_EQ(boot_cold_reset(&r.c, msg, sizeof(msg)), 0);
        CHECK_EQ(r.io.regs[T20_RAM_RO], 0x00);
        // a reset does not touch the ROMs (Reset never writes them), Reload ROMs does
        r.quiet();
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK(std::find(r.io.events.begin(), r.io.events.end(), "rom_write") == r.io.events.end());
        r.fs.put("/usb0/roms/vc20/kernal.bin", pattern(9, 8192));
        CHECK_EQ(ops_call(&r.c, "machine.reload_roms", 0, 0, &res), OPE_OK);
        CHECK(r.io.kernal == pattern(9, 8192));
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
    }

    // ---- Run PRG: unexpanded, a BASIC program at $1001 is relinked in place, the pointers follow, RUN is typed after the editor is up
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.fs.put("/usb0/games/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/games/two.prg", &res), OPE_OK);
        CHECK(ops_state(&r.c, "vc20.pending") != 0);
        CHECK_EQ(r.io.ram[T20_KBD_COUNT], 0);                    // the operation itself put nothing in and typed nothing
        r.menu.closed();
        CHECK(!ops_state(&r.c, "vc20.pending"));
        CHECK_STR(ev_first(r.io.events), "stop");                  // (the menu's own stop), then the reset
        CHECK(r.io.released);
        CHECK_EQ(r.w(0x1001), 0x1007);
        CHECK_EQ(r.w(0x1007), 0x100D);
        CHECK_EQ(r.io.ram[0x1009], 20);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.w(p), 0x100F);
        CHECK_EQ(r.io.ram[T20_KBD_COUNT], 4);
        CHECK(r.keys() == "RUN\r");
        CHECK(!r.io.typed_early);
        CHECK(!r.c.notice_pending);
        const char *prog = ops_state(&r.c, "vc20.program");
        CHECK(prog && !strcmp(prog, "two.prg"));
    }
    // ... with 8K at $2000 the machine says $1201: the same file is moved there and its links with it
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.expansion(2);
        r.quiet();
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK_EQ(r.io.regs[T20_RAM_EXT], 0x02);
        CHECK_EQ(r.w(0x2B), 0x1201);
        CHECK_EQ(r.w(0x1201), 0x1207);                             // the links moved by $200
        CHECK_EQ(r.w(0x1207), 0x120D);
        CHECK_EQ(r.io.ram[0x1209], 20);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.w(p), 0x120F);
        CHECK(r.keys() == "RUN\r");
        CHECK(!r.io.typed_early);
    }
    // ... with the 3K alone $0401
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.expansion(1);
        r.quiet();
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK_EQ(r.w(0x2B), 0x0401);
        CHECK_EQ(r.w(0x0401), 0x0407);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.w(p), 0x040F);
        CHECK(r.keys() == "RUN\r");
    }
    // a machine-language file goes where it says and SYS starts it there; a .P00 carries 0x1A bytes before the load address
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.fs.put("/usb0/ml.prg", prg(0x1C00, { 0x60, 0x60 }));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/ml.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.io.ram[0x1C00] == 0x60 && r.io.ram[0x1C01] == 0x60);
        CHECK(r.keys() == "SYS7168\r");
        Bytes p00(0x1A, 0);
        Bytes f = basic_prg(0x1001);
        p00.insert(p00.end(), f.begin(), f.end());
        r.fs.put("/usb0/x.P00", p00);
        CHECK_EQ(r.menu.action(".p00", 0, "/usb0/x.P00", &res), OPE_OK);
        r.menu.closed();
        CHECK_EQ(r.io.ram[0x1009], 20);
        CHECK(r.keys() == "RUN\r");
    }
    // a file that is no program is refused before the machine is touched: no reset, nothing typed
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.fs.put("/usb0/tiny.prg", Bytes{ 0x01, 0x10 });
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/tiny.prg", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "not a program") != 0);
        r.fs.put("/usb0/huge.prg", Bytes(0x1001D, 1));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/huge.prg", &res), OPE_FAILED);
        Bytes wrap(0xF000, 3);
        wrap[0] = 0x01; wrap[1] = 0x20;                             // $2001 + 61438 bytes runs past $FFFF
        r.fs.put("/usb0/wrap.prg", wrap);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/wrap.prg", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "past $FFFF") != 0);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/missing.prg", &res), OPE_FAILED);
        CHECK_EQ(r.io.reset_asserts, 1);                           // the start's, and no other
        CHECK(!ops_state(&r.c, "vc20.pending"));
        CHECK_EQ(r.io.ram[T20_KBD_COUNT], 0);
    }
    // the machine is held while the menu runs the operation: it does not reach READY there, the program waits for the menu to close
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        for (int i = 0; i < 80; i++) r.io.delay_ms(50);            // 4 s of the menu: the machine is stopped
        CHECK(!r.io.booted);
        CHECK(ops_state(&r.c, "vc20.pending") != 0);
        r.menu.closed();
        CHECK(r.io.booted);
        CHECK_EQ(r.w(0x1001), 0x1007);
    }
    // a machine that never reaches READY: nothing is placed, nothing typed, and the reason waits on the screen of the next menu
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.io.boot_ms = 600000;
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        uint32_t t0 = r.io.t;
        r.menu.closed();
        CHECK(!ops_state(&r.c, "vc20.pending"));
        CHECK(r.c.notice_pending && strstr(r.c.notice, "READY") != 0);
        CHECK(r.io.t - t0 < 7000);
        CHECK_EQ(r.io.ram[T20_KBD_COUNT], 0);
    }
    // a reset while the program waits ends the wait; a second program replaces the first
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        r.fs.put("/usb0/ml.prg", prg(0x1C00, { 0x60 }));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        CHECK(ops_state(&r.c, "vc20.pending") != 0);
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK(ops_state(&r.c, "vc20.pending") == 0);
        r.menu.closed();
        CHECK(r.keys() == "");                                     // it never came
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/ml.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.keys() == "SYS7168\r");
        CHECK_EQ(r.io.ram[0x1009], 0);                             // the first one was replaced, not run as well
    }

    // ---- Load PRG: the bytes go in as the machine is - no reset, no hold, nothing typed; BASIC is relinked to the start the machine states
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.expansion(2);
        CHECK_EQ(boot_cold_reset(&r.c, res.msg, sizeof(res.msg)), 0);     // the machine now runs with 8K: BASIC at $1201
        r.io.delay_ms(r.io.boot_ms + r.io.editor_ms + 50);
        r.quiet();
        int rs = r.io.reset_asserts;
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 1, "/usb0/two.prg", &res), OPE_OK);
        CHECK_EQ(r.io.reset_asserts, rs);
        CHECK(!ops_state(&r.c, "vc20.pending"));
        CHECK_EQ(r.w(0x1201), 0x1207);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.w(p), 0x120F);
        CHECK_EQ(r.io.ram[T20_KBD_COUNT], 0);
        CHECK(r.keys() == "");
        // before BASIC has set its pointer the library refuses: the machine says $0000, which cannot be right
        r.io.ram[0x2B] = 0; r.io.ram[0x2C] = 0;
        CHECK_EQ(r.menu.action(".prg", 1, "/usb0/two.prg", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "cannot be right") != 0);
    }

    // ---- the descriptor's table: what the old application offered and nothing it did not
    {
        CHECK_STR(vc20_desc.folder, "vc20");
        CHECK_STR(vc20_desc.cor_name, "VIC-20");
        CHECK_EQ(vc20_desc.nroms, 4);                                   // BASIC, KERNAL, CHAR and the 1541's (cbm1541, optional)
        CHECK(!vc20_desc.roms[3].required);
        CHECK_EQ(vc20_desc.nsettings, 7);                               // two of the core, the four of the drive and the LED Load Indicator (T159)
        CHECK_EQ(vc20_desc.ntasks, 7);                                  // Reset, Reload, and the drive's Reset / Create x2 / Save x2
        CHECK_STR(vc20_desc.tasks[0].label, "Reset");
        CHECK_STR(vc20_desc.tasks[1].label, "Reload ROMs, reset");
        CHECK(vc20_desc.tasks[0].closes_menu && vc20_desc.tasks[1].closes_menu);   // a reset from the menu closes it
        CHECK(desc_has_capability(&vc20_desc, "drive_a") && desc_has_capability(&vc20_desc, "iec"));
        CHECK(!desc_has_capability(&vc20_desc, "tape") && !desc_has_capability(&vc20_desc, "cartridge") && !desc_has_capability(&vc20_desc, "sid_sockets"));
        CHECK_EQ(vc20_desc.freeze.reg_status, T20_STATUS);
        CHECK_EQ(vc20_desc.freeze.mask, T20_ST_HELD);
    }
    DONE("test_vc20");
}
