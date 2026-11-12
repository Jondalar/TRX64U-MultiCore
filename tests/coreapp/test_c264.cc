// T90 on the generic application: the Commodore 264 descriptor (coreapp/cores/c264.cc) against a model of the register block and the 16 KB ROM
// aperture of cores/c264/rtl/trx/c264_regs.v / c264_win.v. KERNAL, BASIC and the optional function ROMs through ROM_SEL, model / RAM size / joystick
// swap written only while the machine is held, cartridge images (16 and 32 KB, the Plus/4 model), a .prg run with the 16 KB wrap refusal and
// loaded without a reset, the picture on the pixel layer (384 x 288, 3 x 3, four buffers), Freeze's PAUSED bit. The descriptor is the real one.
#include <algorithm>
#include "cbm_rig.h"

// The register file as the bench reads cores/c264/rtl/trx/c264_regs.v and c264_win.v - literal numbers on purpose (see test_vc20.cc)
#define T264_MODEL      0x0A1
#define T264_RAM_SIZE   0x0A2
#define T264_JOY_SWAP   0x0A3
#define T264_STATUS     0x0A4
#define T264_ST_HELD    0x02
#define T264_ROM_SEL    0x0B0
#define T264_CART       0x0B1
#define T264_FB_LATEST  0x0C0
#define T264_FRAME_SEQ  0x0C1
#define T264_FB_BASE    0x0C4
#define T264_FB_STRIDE  0x0C8
#define T264_ARR_KERNAL   0
#define T264_ARR_BASIC    1
#define T264_ARR_FUNC_LO  2
#define T264_ARR_FUNC_HI  3
#define T264_ARR_CART_LO  4
#define T264_ARR_CART_HI  5
#define T264_ROM_BYTES    16384u
#define T264_FB_ADDR      0x01D00000u
#define T264_FB_STRIDE_BYTES 0x00100000u
#define T264_KBD_BUF    0x0527
#define T264_KBD_COUNT  0xEF

const CoreDesc *const core_table[] = { &c264_desc, 0 };

struct C264Io : CbmIo {
    Bytes arr[6];
    uint8_t model_latched = 0, ram_latched = 0;
    bool deaf = false, sel_stuck = false;    // the aperture takes no writes / ROM_SEL does not hold what was written
    C264Io() : CbmIo(T264_STATUS, T264_KBD_COUNT)
    {
        for (auto &a : arr) a.assign(T264_ROM_BYTES, 0);
        taken_in_reset = { T264_MODEL, T264_RAM_SIZE, T264_JOY_SWAP };
    }
    uint8_t rd(uint32_t reg) { return (sel_stuck && reg == T264_ROM_SEL) ? 0xFF : CbmIo::rd(reg); }
    void latch() { model_latched = regs[T264_MODEL]; ram_latched = regs[T264_RAM_SIZE]; }
    uint16_t boot_txttab() { return 0x1001; }
    // the 16 KB aperture: the array ROM_SEL names, offsets 0 .. 0x3FFF (c264_win.v w_hit: nothing behind a selector above 5)
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        events.push_back("rom_write sel=" + std::to_string(regs[T264_ROM_SEL]));
        if (off + n > T264_ROM_BYTES) overrun = true;
        if (!deaf && regs[T264_ROM_SEL] <= 5 && off + n <= T264_ROM_BYTES) memcpy(&arr[regs[T264_ROM_SEL]][off], p, n);
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        memset(p, 0, n);
        if (regs[T264_ROM_SEL] <= 5 && off + n <= T264_ROM_BYTES) memcpy(p, &arr[regs[T264_ROM_SEL]][off], n);
    }
};

static Bytes pattern(int role, size_t n)
{
    Bytes v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + role * 61 + (i >> 8));
    return v;
}

struct Rig {
    C264Io io;
    FakeFs fs;
    PxRec px;
    CoreCtx c;
    Settings *st;
    Video *vid;
    CbmMenu menu;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &c264_desc; c.io = &io; c.fs = &fs;
        st = new Settings(c264_desc.settings, c264_desc.nsettings, c264_desc.store, &fs, "/sd/cores/c264.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(c264_desc.video, &px, &io);
        c.video = vid;
        menu = { &c, &io, &c264_desc, "c264.service", "c264.pending" };
    }
    void roms(bool func = false)
    {
        fs.put("/usb0/roms/c16/kernal.bin", pattern(1, T264_ROM_BYTES));
        fs.put("/usb0/roms/c16/basic.bin", pattern(2, T264_ROM_BYTES));
        if (func) {
            fs.put("/usb0/roms/c16/3plus1lo.bin", pattern(3, T264_ROM_BYTES));
            fs.put("/usb0/roms/c16/3plus1hi.bin", pattern(4, T264_ROM_BYTES));
        }
    }
    bool boot()
    {
        BoundedUi ui;
        BootResult r = boot_start(&c, &ui);
        io.delay_ms(io.boot_ms + io.editor_ms + 50);
        return r == BOOT_RUNNING && io.ready;
    }
    void set(const char *key, int v) { st->set(st->find(key), v); }
    void quiet() { io.events.clear(); io.log.clear(); io.bad_writes = 0; io.typed_early = false; }
    std::string keys() { return std::string((const char *)&io.ram[T264_KBD_BUF], io.ram[T264_KBD_COUNT]); }
    uint16_t w(uint32_t a) { return (uint16_t)(io.ram[a] | (io.ram[a + 1] << 8)); }
};

static uint32_t reg32(C264Io &io, uint32_t r) { return io.regs[r] | (io.regs[r + 1] << 8) | (io.regs[r + 2] << 16) | ((uint32_t)io.regs[r + 3] << 24); }

int main()
{
    OpResult res;
    char msg[300];

    // ---- the start: KERNAL and BASIC through ROM_SEL 0 and 1, the function ROMs not touched when the files are not there; the settings written in
    // the hold; the picture's buffers announced to the core and the layer set up for 384 x 288 at 3 x 3
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        CHECK(r.io.arr[T264_ARR_KERNAL] == pattern(1, T264_ROM_BYTES));
        CHECK(r.io.arr[T264_ARR_BASIC] == pattern(2, T264_ROM_BYTES));
        CHECK(r.io.arr[T264_ARR_FUNC_LO] == Bytes(T264_ROM_BYTES, 0));
        CHECK(r.io.arr[T264_ARR_FUNC_HI] == Bytes(T264_ROM_BYTES, 0));
        CHECK(!r.io.overrun);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        CHECK_EQ(r.io.reset_releases, 1);
        int last_rom = -1, rel = -1;
        for (size_t i = 0; i < r.io.events.size(); i++) {
            if (r.io.events[i].compare(0, 9, "rom_write") == 0) last_rom = (int)i;
            if (r.io.events[i] == "mode_unreset") rel = (int)i;
        }
        CHECK(last_rom >= 0 && last_rom < rel);
        bool model_written = false;
        for (auto &w : r.io.log) model_written = model_written || w.reg == T264_MODEL;
        CHECK(model_written);
        CHECK_EQ(r.io.bad_writes, 0);
        CHECK_EQ(r.io.model_latched, 1);                                        // the defaults (Alex, 2026-10-23): Plus/4 and 64 KB
        CHECK_EQ(r.io.ram_latched, 1);
        CHECK_EQ(reg32(r.io, T264_FB_BASE), T264_FB_ADDR);
        CHECK_EQ(reg32(r.io, T264_FB_STRIDE), T264_FB_STRIDE_BYTES);
        r.vid->show();
        CHECK_EQ(r.px.last(PX_SIZE), (288u << 16) | 384u);
        CHECK_EQ(r.px.last(PX_STRIDE), 768u);
        CHECK_EQ(r.px.last(PX_POS), (108u << 16) | 384u);                       // 1152 x 864 centred on 1920 x 1080
        CHECK_EQ(r.px.last(PX_CTRL), Video::ctrl_value(3, true));
        CHECK_EQ(r.px.last(PX_FRONT), T264_FB_ADDR);
        // a frame in buffer 2 is flipped to; the same sequence number is not a new frame
        r.io.regs[T264_FB_LATEST] = 0x82;
        r.io.regs[T264_FRAME_SEQ] = 7;
        CHECK(r.vid->poll());
        CHECK_EQ(r.px.last(PX_BACK), T264_FB_ADDR + 2 * T264_FB_STRIDE_BYTES);
        CHECK(!r.vid->poll());
        // a ROM the core does not take, a ROM_SEL that does not hold, a blob of the wrong size: refused by the operation, with the reason
        {
            Bytes k = pattern(1, T264_ROM_BYTES);
            OpIn in = in_blob("data", k.data(), (uint32_t)k.size());
            Bytes other = pattern(8, T264_ROM_BYTES);
            OpIn in_other = in_blob("data", other.data(), (uint32_t)other.size());
            r.io.deaf = true;
            CHECK_EQ(ops_call(&r.c, "c264.put_kernal", &in_other, 1, &res), OPE_FAILED);
            CHECK(strstr(res.msg, "read-back") != 0 && strstr(res.msg, "KERNAL") != 0);
            r.io.deaf = false;
            r.io.sel_stuck = true;
            CHECK_EQ(ops_call(&r.c, "c264.put_basic", &in_other, 1, &res), OPE_FAILED);
            CHECK(strstr(res.msg, "ROM_SEL") != 0);
            r.io.sel_stuck = false;
            OpIn small = in_blob("data", k.data(), 8192);
            CHECK_EQ(ops_call(&r.c, "c264.put_kernal", &small, 1, &res), OPE_FAILED);
            CHECK(strstr(res.msg, "8192") != 0);
            CHECK_EQ(ops_call(&r.c, "c264.put_kernal", &in, 1, &res), OPE_OK);
        }
        // Freeze: C64_STOP raises the descriptor's PAUSED bit in time
        CbmFreezeOps ops(&r.io, &c264_desc);
        Freeze f(c264_desc.freeze, &ops);
        CHECK(f.enter());
        CHECK(ops.logs.empty());
        f.leave();
        CHECK(!r.io.stopped);
    }
    // ---- with the function ROMs next to them they go to ROM_SEL 2 and 3 (the Plus/4's 3-plus-1)
    {
        Rig r;
        r.roms(true);
        CHECK(r.boot());
        CHECK(r.io.arr[T264_ARR_FUNC_LO] == pattern(3, T264_ROM_BYTES));
        CHECK(r.io.arr[T264_ARR_FUNC_HI] == pattern(4, T264_ROM_BYTES));
        CHECK(r.io.arr[T264_ARR_KERNAL] == pattern(1, T264_ROM_BYTES));
        CHECK(r.io.arr[T264_ARR_BASIC] == pattern(2, T264_ROM_BYTES));
    }
    // ---- the Commodore part names, and a KERNAL of the wrong size is told by name
    {
        Rig r;
        r.fs.put("/usb0/roms/c16/kernal-318004-05.bin", pattern(1, T264_ROM_BYTES));
        r.fs.put("/usb0/roms/c16/318006-01.bin", pattern(2, T264_ROM_BYTES));
        CHECK(r.boot());
        CHECK(r.io.arr[T264_ARR_KERNAL] == pattern(1, T264_ROM_BYTES));
        Rig q;
        q.roms();
        q.fs.put("/usb0/roms/c16/kernal.bin", Bytes(8192, 1));
        RomResult rr[4];
        char summary[300];
        CHECK_EQ(rom_scan(&q.fs, c264_desc.folder, c264_desc.roms, c264_desc.nroms, rr, summary, sizeof(summary), 0), ROM_WRONG_SIZE);
        CHECK(strstr(summary, "KERNAL") != 0 && strstr(summary, "16384") != 0);
    }

    // ---- the settings are registers of the core, taken with a reset and written only in the hold
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        CHECK_EQ(r.io.regs[T264_MODEL], 1);                         // the start wrote the defaults: Plus/4, 64 KB
        CHECK_EQ(r.io.regs[T264_RAM_SIZE], 1);
        r.set("Model (at next reset)", 0);
        r.set("RAM (at next reset)", 0);
        r.set("Joystick ports (at next reset)", 1);
        CHECK_EQ(r.io.regs[T264_MODEL], 1);                         // choosing writes nothing
        CHECK_EQ(r.io.regs[T264_JOY_SWAP], 0);
        // the Reset task as the menu runs it (its operation and bindings from the descriptor): hold, the reset-time settings, release
        {
            const Task *reset = 0;
            for (int i = 0; i < c264_desc.ntasks; i++) if (!strcmp(c264_desc.tasks[i].label, "Reset")) reset = &c264_desc.tasks[i];
            CHECK(reset != 0);
            if (reset) {
                r.quiet();
                CHECK_EQ(ops_run_bound(&r.c, reset->op, reset->bind, reset->nbind, 0, &res), OPE_OK);
                CHECK_STR(ev_first(r.io.events), "mode_reset");
                CHECK_STR(ev_last(r.io.events), "mode_unreset");
                CHECK_EQ(r.io.regs[T264_MODEL], 0);
                CHECK_EQ(r.io.regs[T264_RAM_SIZE], 0);
                CHECK_EQ(r.io.regs[T264_JOY_SWAP], 1);
                CHECK_EQ(r.io.model_latched, 0);                    // what the core took at the release
                CHECK_EQ(r.io.ram_latched, 0);
                CHECK_EQ(r.io.bad_writes, 0);
            }
        }
        r.set("Model (at next reset)", 1);
        r.set("RAM (at next reset)", 1);
        r.set("Joystick ports (at next reset)", 0);
        CHECK_EQ(r.io.regs[T264_MODEL], 0);                         // still the last reset's
        CHECK_EQ(boot_cold_reset(&r.c, msg, sizeof(msg)), 0);
        CHECK(r.io.regs[T264_MODEL] == 1 && r.io.regs[T264_RAM_SIZE] == 1 && r.io.regs[T264_JOY_SWAP] == 0);
        // Reset leaves the ROMs, Reload ROMs puts them again
        r.quiet();
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK(std::find_if(r.io.events.begin(), r.io.events.end(), [](const std::string &e) { return e.compare(0, 9, "rom_write") == 0; }) == r.io.events.end());
        r.fs.put("/usb0/roms/c16/basic.bin", pattern(9, T264_ROM_BYTES));
        CHECK_EQ(ops_call(&r.c, "machine.reload_roms", 0, 0, &res), OPE_OK);
        CHECK(r.io.arr[T264_ARR_BASIC] == pattern(9, T264_ROM_BYTES));
        CHECK(r.io.arr[T264_ARR_KERNAL] == pattern(1, T264_ROM_BYTES));
    }

    // ---- Run PRG with the 64 KB setting: relinked at $1001, RUN typed into the 264's buffer ($0527, count $EF) after the editor is up
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.set("RAM (at next reset)", 1);
        r.quiet();
        r.fs.put("/usb0/games/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/games/two.prg", &res), OPE_OK);
        CHECK(ops_state(&r.c, "c264.pending") != 0);
        r.menu.closed();
        CHECK(!ops_state(&r.c, "c264.pending"));
        CHECK_EQ(r.io.regs[T264_RAM_SIZE], 1);                      // the reset took the setting
        CHECK_EQ(r.w(0x1001), 0x1007);
        CHECK_EQ(r.w(0x1007), 0x100D);
        CHECK_EQ(r.io.ram[0x1009], 20);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.w(p), 0x100F);
        CHECK_EQ(r.io.ram[T264_KBD_COUNT], 4);
        CHECK(r.keys() == "RUN\r");
        CHECK(!r.io.typed_early);
        CHECK(!r.c.notice_pending);
        const char *prog = ops_state(&r.c, "c264.program");
        CHECK(prog && !strcmp(prog, "two.prg"));
        // machine language: SYS; .P00 header
        r.fs.put("/usb0/ml.prg", prg(0x1C00, { 0x60, 0x60 }));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/ml.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.io.ram[0x1C00] == 0x60);
        CHECK(r.keys() == "SYS7168\r");
        Bytes p00(0x1A, 0);
        Bytes f = basic_prg(0x1001);
        p00.insert(p00.end(), f.begin(), f.end());
        r.fs.put("/usb0/x.P00", p00);
        CHECK_EQ(r.menu.action(".p00", 0, "/usb0/x.P00", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.keys() == "RUN\r");
    }
    // ---- with 16 KB a program that would run past $3FFF is refused before anything is touched; with 64 KB the same file runs
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.set("RAM (at next reset)", 0);                            // 16 KB: not the default any more
        r.quiet();
        Bytes big(0x4000, 0x55);
        big[0] = 0x01; big[1] = 0x10;                               // $1001 + 16382 bytes: past $3FFF
        big[2] = 0; big[3] = 0;                                      // a first line with link 0: a BASIC program of no lines
        r.fs.put("/usb0/big.prg", big);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/big.prg", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "64 KB") != 0);
        CHECK_EQ(r.io.reset_asserts, 1);                            // no reset beyond the start's
        CHECK(!ops_state(&r.c, "c264.pending"));
        CHECK_EQ(r.menu.action(".prg", 1, "/usb0/big.prg", &res), OPE_FAILED);      // Load refuses the same way
        CHECK(strstr(res.msg, "64 KB") != 0);
        r.set("RAM (at next reset)", 1);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/big.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK(!r.c.notice_pending);
        // a file that fits in 16 KB runs with 16 KB
        r.set("RAM (at next reset)", 0);
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.keys() == "RUN\r");
        // no program at all
        r.fs.put("/usb0/tiny.prg", Bytes{ 0x01, 0x10 });
        int rs = r.io.reset_asserts;
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/tiny.prg", &res), OPE_FAILED);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/missing.prg", &res), OPE_FAILED);
        CHECK_EQ(r.io.reset_asserts, rs);
    }
    // ---- a machine that never reaches READY
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
        CHECK(!ops_state(&r.c, "c264.pending"));
        CHECK(r.c.notice_pending && strstr(r.c.notice, "READY") != 0);
        CHECK(r.io.t - t0 < 7000);
        CHECK_EQ(r.io.ram[T264_KBD_COUNT], 0);
    }
    // ---- a reset while the program waits ends the wait; a second program replaces the first
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        r.fs.put("/usb0/ml.prg", prg(0x1C00, { 0x60 }));
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        CHECK(ops_state(&r.c, "c264.pending") != 0);
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK(ops_state(&r.c, "c264.pending") == 0);
        r.menu.closed();
        CHECK(r.keys() == "");
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/two.prg", &res), OPE_OK);
        CHECK_EQ(r.menu.action(".prg", 0, "/usb0/ml.prg", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.keys() == "SYS7168\r");
        CHECK_EQ(r.io.ram[0x1009], 0);
    }
    // ---- Load PRG: no reset, no typing
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        int rs = r.io.reset_asserts;
        r.fs.put("/usb0/two.prg", basic_prg(0x1001));
        CHECK_EQ(r.menu.action(".prg", 1, "/usb0/two.prg", &res), OPE_OK);
        CHECK_EQ(r.io.reset_asserts, rs);
        CHECK(!ops_state(&r.c, "c264.pending"));
        CHECK_EQ(r.w(0x1001), 0x1007);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.w(p), 0x100F);
        CHECK(r.keys() == "");
    }

    // ---- cartridge images
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.set("Model (at next reset)", 0);                          // not the default any more: the load has to change it
        r.quiet();
        Bytes lo = pattern(5, T264_ROM_BYTES), hi = pattern(6, T264_ROM_BYTES);
        r.fs.put("/usb0/carts/prog16.bin", lo);
        CHECK_EQ(r.menu.action(".bin", 0, "/usb0/carts/prog16.bin", &res), OPE_OK);
        CHECK(r.io.arr[T264_ARR_CART_LO] == lo);
        CHECK(r.io.arr[T264_ARR_CART_HI] == Bytes(T264_ROM_BYTES, 0));
        CHECK_EQ(r.io.regs[T264_CART], 0x01);
        CHECK_EQ(r.st->value(r.st->find("Model (at next reset)")), 1);      // the bank register is live only in the Plus/4 model
        CHECK_EQ(r.io.regs[T264_MODEL], 1);                          // ... and the reset took it
        CHECK_EQ(r.io.model_latched, 1);
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        CHECK_EQ(r.io.bad_writes, 0);
        CHECK_STR(ops_state(&r.c, "c264.cartridge"), "prog16.bin");
        // the cartridge stays across a plain reset
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK_EQ(r.io.regs[T264_CART], 0x01);
        // 32 KB: both halves
        Bytes both = lo;
        both.insert(both.end(), hi.begin(), hi.end());
        r.fs.put("/usb0/carts/prog32.bin", both);
        CHECK_EQ(r.menu.action(".bin", 0, "/usb0/carts/prog32.bin", &res), OPE_OK);
        CHECK(r.io.arr[T264_ARR_CART_LO] == lo && r.io.arr[T264_ARR_CART_HI] == hi);
        CHECK_EQ(r.io.regs[T264_CART], 0x03);
        CHECK_STR(ops_state(&r.c, "c264.cartridge"), "prog32.bin");
        // an image of another size is refused, the machine is not touched
        r.quiet();
        int rs = r.io.reset_asserts;
        r.fs.put("/usb0/carts/odd.bin", Bytes(20000, 1));
        CHECK_EQ(r.menu.action(".bin", 0, "/usb0/carts/odd.bin", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "16384 or 32768") != 0);
        CHECK_EQ(r.io.reset_asserts, rs);
        CHECK_EQ(r.io.regs[T264_CART], 0x03);
        CHECK_STR(ops_state(&r.c, "c264.cartridge"), "prog32.bin");
        // eject takes it out and resets
        CHECK_EQ(ops_call(&r.c, "c264.cart_eject", 0, 0, &res), OPE_OK);
        CHECK_EQ(r.io.regs[T264_CART], 0);
        CHECK(ops_state(&r.c, "c264.cartridge") == 0);
        CHECK_EQ(r.io.reset_asserts, rs + 1);
    }

    // ---- the shared folder (Alex, 2026-10-23): the drive ROM is found in <root>/roms, a C64 KERNAL there is not the C16's
    {
        FakeFs f;
        f.put("/usb1/roms/c16/basic.bin", pattern(2, T264_ROM_BYTES));
        f.put("/usb1/roms/kernal.bin", pattern(1, T264_ROM_BYTES));
        f.put("/usb1/roms/kernal-901227-03.bin", pattern(1, T264_ROM_BYTES));
        RomResult rr[8];
        CHECK_EQ(rom_scan(&f, c264_desc.folder, c264_desc.roms, c264_desc.nroms, rr, msg, sizeof(msg), 0), ROM_MISSING);
        CHECK_EQ(rr[0].status, ROM_MISSING);
        CHECK(strstr(msg, "KERNAL") != 0);
        f.put("/usb1/roms/1541.bin", pattern(5, 16384));
        CHECK_EQ(rom_scan(&f, c264_desc.folder, c264_desc.roms, c264_desc.nroms, rr, msg, sizeof(msg), 0), ROM_MISSING);   // still no KERNAL
        CHECK_EQ(rr[4].status, ROM_FOUND);
        CHECK_STR(rr[4].hit.dir, "/usb1/roms");
        f.put("/usb1/roms/c16/kernal.bin", pattern(1, T264_ROM_BYTES));
        CHECK_EQ(rom_scan(&f, c264_desc.folder, c264_desc.roms, c264_desc.nroms, rr, msg, sizeof(msg), 0), ROM_FOUND);
        CHECK_STR(rr[0].hit.dir, "/usb1/roms/c16");
    }

    // ---- the descriptor's table
    {
        CHECK_STR(c264_desc.folder, "c16");
        CHECK_STR(c264_desc.cor_name, "Commodore 264 (C16, Plus/4)");
        CHECK_EQ(c264_desc.nroms, 5);                                   // KERNAL, BASIC, two function ROMs and the 1541's (cbm1541, optional)
        CHECK(!c264_desc.roms[4].required);
        CHECK_EQ(c264_desc.nsettings, 8);                               // three of the core, the four of the drive and the LED Load Indicator (T159)
        CHECK(c264_desc.tasks[0].closes_menu && c264_desc.tasks[1].closes_menu && c264_desc.tasks[2].closes_menu);
        CHECK(desc_has_capability(&c264_desc, "drive_a") && desc_has_capability(&c264_desc, "iec"));
        CHECK(!desc_has_capability(&c264_desc, "tape") && !desc_has_capability(&c264_desc, "sid_sockets"));
        CHECK_EQ(c264_desc.freeze.reg_status, T264_STATUS);
        CHECK_EQ(c264_desc.freeze.mask, T264_ST_HELD);
        CHECK(!c264_desc.roms[0].check && c264_desc.roms[0].required && c264_desc.roms[1].required);
        CHECK(!c264_desc.roms[2].required && !c264_desc.roms[3].required);        // the 3-plus-1 is optional
        CHECK_EQ(c264_desc.video.w, 384);
        CHECK_EQ(c264_desc.video.h, 288);
        CHECK_EQ(c264_desc.video.scale, 3);
        CHECK_EQ(c264_desc.video.nbuf, 4);
    }
    // T104.1 D15, Mount & Run's half of the machine: the typed script goes in one piece (at most ten keys) at a time, each only after the machine
    // took the one before, and not before READY; a piece that does not fit the buffer is refused before the machine is reset
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.quiet();
        OpIn in = in_str("text", "AB|CD\r|EF\r");
        r.menu.begin();
        CHECK_EQ(ops_call(&r.c, "c264.type_script", &in, 1, &res), OPE_OK);
        CHECK(std::find(r.io.events.begin(), r.io.events.end(), "mode_reset") != r.io.events.end());
        CHECK(ops_state(&r.c, "c264.pending") && !strcmp(ops_state(&r.c, "c264.pending"), "type"));
        r.io.stop(false);
        std::string typed;
        for (int i = 0; i < 400 && ops_state(&r.c, "c264.pending"); i++) {
            r.io.delay_ms(50);
            ops_call(&r.c, "c264.service", 0, 0, &res);
            if (r.io.ram[T264_KBD_COUNT] && r.io.ready) {
                std::string k = r.keys();
                if (typed.empty() || i % 2) {
                    typed += k;
                    r.io.ram[T264_KBD_COUNT] = 0;
                }
            }
        }
        CHECK(typed == "ABCD\rEF\r");
        CHECK(!r.io.typed_early);
        r.quiet();
        OpIn bad = in_str("text", "THIS IS TOO LONG|X");
        CHECK_EQ(ops_call(&r.c, "c264.type_script", &bad, 1, &res), OPE_FAILED);
        CHECK(r.io.events.empty());
    }
    DONE("test_c264");
}
