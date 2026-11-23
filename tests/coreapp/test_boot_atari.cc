// T93 M2: the Atari start sequence against a model of the core's register block - ROMs through the aperture, settings into the
// registers while held, the picture's buffers, release last - and the resets. The descriptor under test is the real one
// (coreapp/cores/atari800.cc).
#include <string>
#include <setjmp.h>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "keyboard.h"
#include "cores.h"
#include "atari800_regs.h"

static jmp_buf g_gone;                               // the host's way back does not return: the model leaves boot_start the same way

struct Ui : BootUi {
    std::vector<std::string> msgs, titles;
    std::vector<uint32_t> msg_t;
    int ticks = 0;
    int clears = 0;
    FakeIo *io = 0;
    FakeFs *fs = 0;
    int insert_at = -1;
    bool released_early = false;
    int leave_at_ms = -1;                            // RUN/STOP is pressed once, at this time
    int asks = 0, returns = 0;
    bool return_works = false;                       // a way back that works does not come back: the model throws
    bool leave_asked() { asks++; if (leave_at_ms >= 0 && (int)io->t >= leave_at_ms) { leave_at_ms = -1; return true; } return false; }
    bool return_to_host(char *msg, int n) { returns++; if (return_works) longjmp(g_gone, 1); snprintf(msg, n, "no ICAP"); return false; }
    void message(const char *t, const char *m, bool) { titles.push_back(t); msgs.push_back(m); msg_t.push_back(io->t); }
    void clear() { clears++; }
    void tick()
    {
        ticks++;
        if (io->reset_releases > 0 && insert_at >= 0 && ticks < insert_at) {
            released_early = true;                   // S2: never released while a required ROM is missing
        }
        if (ticks == insert_at) {
            std::vector<uint8_t> os(16384), bas(8192);
            for (int i = 0; i < 16384; i++) os[i] = (uint8_t)(i * 7 + 3);
            for (int i = 0; i < 8192; i++) bas[i] = (uint8_t)(i * 13 + 1);
            fs->put("/USB0/roms/atari800/ATARIXL.ROM", os);
            fs->put("/USB0/roms/atari800/BASIC.ROM", bas);
        }
    }
};

struct AtariIo : FakeIo {
    AtariIo() { rom_sel_reg = A8_ROM_SEL; }
};

static int idx_of(const AtariIo &io, uint32_t reg, int v, int from = 0)
{
    for (size_t i = from; i < io.log.size(); i++) if (io.log[i].reg == reg && io.log[i].v == v) return (int)i;
    return -1;
}

static void setup(CoreCtx &c, AtariIo &io, FakeFs &fs, PxRec &px, Settings *&st, Video *&v, const char *cfg = "/sd/cores/atari800.cfg")
{
    memset(&c, 0, sizeof(c));
    c.desc = &atari800_desc;
    c.io = &io;
    c.fs = &fs;
    st = new Settings(atari800_desc.settings, atari800_desc.nsettings, atari800_desc.store, &fs, cfg, 0);
    st->load();
    v = new Video(atari800_desc.video, &px, &io);
    c.settings = st;
    c.video = v;
}

int main()
{
    // --- the sequence, with the stick inserted after 12.5 s
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs; ui.insert_at = 25;
        BootResult r = boot_start(&c, &ui);
        CHECK_EQ(r, BOOT_RUNNING);
        CHECK(!ui.released_early);
        CHECK_EQ(ui.msgs.size(), 1);                                    // told once, not every pass
        CHECK(ui.msgs.size() && ui.msgs[0].find("ATARIXL.ROM") != std::string::npos);
        CHECK(ui.msg_t.size() && ui.msg_t[0] >= 9500 && ui.msg_t[0] <= 10500);   // about 10 s (S2)
        CHECK(ui.clears >= 1);
        CHECK(io.t >= 12000 && io.t <= 14000);
        CHECK(c.started);
        // order: the machine is held before a single ROM byte goes in, and released last
        CHECK(io.events.size() >= 4);
        CHECK_STR(io.events.front().c_str(), "mode_reset");
        CHECK_STR(io.events.back().c_str(), "mode_unreset");
        int first_rom = -1, last_rom = -1;
        for (size_t i = 0; i < io.events.size(); i++) if (io.events[i].compare(0, 9, "rom_write") == 0) { if (first_rom < 0) first_rom = (int)i; last_rom = (int)i; }
        CHECK(first_rom > 0 && last_rom < (int)io.events.size() - 1);
        CHECK_EQ(io.reset_releases, 1);
        CHECK_EQ(io.log.front().reg, A8_CTRL);
        CHECK_EQ(io.log.front().v, A8_CTRL_HOLD);
        CHECK_EQ(io.log.back().reg, A8_CTRL);
        CHECK_EQ(io.log.back().v, 0);
        // the ROMs are where the core reads them: OS in array 0, BASIC in array 1
        bool os_ok = true, bas_ok = true;
        for (int i = 0; i < 16384; i++) os_ok &= io.rom[0][i] == (uint8_t)(i * 7 + 3);
        for (int i = 0; i < 8192; i++) bas_ok &= io.rom[1][i] == (uint8_t)(i * 13 + 1);
        CHECK(os_ok);
        CHECK(bas_ok);
        // settings are in the registers (defaults: 64 KB, PAL, normal ports, 1x), before the release
        CHECK_EQ(io.regs[A8_RAM_SEL], 0);
        CHECK_EQ(io.regs[A8_FLAGS], A8_FLAG_PAL);
        CHECK_EQ(io.regs[A8_THROTTLE], 1);
        int rel = (int)io.log.size() - 1;
        CHECK(idx_of(io, A8_RAM_SEL, 0) > 0 && idx_of(io, A8_RAM_SEL, 0) < rel);
        CHECK(idx_of(io, A8_FLAGS, A8_FLAG_PAL) > 0 && idx_of(io, A8_FLAGS, A8_FLAG_PAL) < rel);
        // the picture's buffers are told to the core and cleared; the layer is not touched (the run loop gives it)
        uint32_t base = io.regs[A8_FB_BASE] | io.regs[A8_FB_BASE + 1] << 8 | io.regs[A8_FB_BASE + 2] << 16 | (uint32_t)io.regs[A8_FB_BASE + 3] << 24;
        CHECK_EQ(base, 0x01D00000u);
        CHECK_EQ(io.regs[A8_FB_STRIDE + 2], 0x10);
        CHECK(px.w.empty());
    }
    // --- settings from the .cfg reach the registers: RAM 576 KB Rambo, PAL -> NTSC, ports swapped, 4x
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put_text("/sd/cores/atari800.cfg", "[Atari 800 XL Settings]\nRAM size=576 KB Rambo\nVideo standard=NTSC\nJoystick ports=Swapped\nCPU speed=4x\n\n");
        fs.put_size("/flash/roms/atari800/atarixl.rom", 16384);
        fs.put_size("/flash/roms/atari800/basic.rom", 8192);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(ui.msgs.size(), 0);                                    // the flash had them: nothing to say, no wait
        CHECK(io.t < 200);
        CHECK_EQ(io.regs[A8_RAM_SEL], 5);
        CHECK_EQ(io.regs[A8_FLAGS], A8_FLAG_SWAP);                      // NTSC = PAL bit clear, swapped
        CHECK_EQ(io.regs[A8_THROTTLE], 4);
    }
    // --- the stick is pulled between the listing and the read: told, retried, then released
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
        fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
        fs.fail_loads = 1;
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(ui.msgs.size(), 1);
        CHECK(ui.msgs.size() && ui.msgs[0].find("cannot read") != std::string::npos);
        CHECK_EQ(io.reset_releases, 1);
    }
    // --- reload and the resets, after the core has been running
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384, 9);
        fs.put_size("/usb0/roms/atari800/basic.rom", 8192, 8);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        boot_start(&c, &ui);
        char msg[300];
        // warm reset keeps the RAM: CTRL = hold + keep, then 0
        size_t n0 = io.log.size();
        CHECK_EQ(boot_warm_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.log[n0].reg, A8_CTRL);
        CHECK_EQ(io.log[n0].v, A8_CTRL_HOLD | A8_CTRL_KEEP);
        CHECK_EQ(io.log.back().v, 0);
        CHECK_STR(io.events.back().c_str(), "mode_unreset");
        // cold start clears the RAM: CTRL = hold only
        n0 = io.log.size();
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.log[n0].v, A8_CTRL_HOLD);
        CHECK_EQ(io.log.back().v, 0);
        // a reset-time setting changed in the menu goes in at the next reset, not before
        int ram = st->find("RAM size");
        st->set(ram, 3);
        CHECK_EQ(io.regs[A8_RAM_SEL], 0);
        boot_cold_reset(&c, msg, sizeof(msg));
        CHECK_EQ(io.regs[A8_RAM_SEL], 3);
        // reload with the ROMs gone: refused, the machine is NOT touched (no hold, no reset)
        fs.files.clear();
        size_t ev = io.events.size();
        CHECK_EQ(boot_reload(&c, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "ATARIXL.ROM") != 0);
        CHECK_EQ(io.events.size(), ev);
        // reload with a new OS file: held, the new bytes in, released
        fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384, 0x77);
        fs.put_size("/usb0/roms/atari800/basic.rom", 8192, 0x66);
        CHECK_EQ(boot_reload(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.rom[0][100], 0x77);
        CHECK_EQ(io.rom[1][100], 0x66);
        CHECK_STR(io.events.back().c_str(), "mode_unreset");
    }
    // --- T243 S1: a ROM missing, RUN/STOP on the wait screen: the host's way back is taken, the core was never released
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs; ui.leave_at_ms = 12000; ui.return_works = true;
        bool left = false;
        if (setjmp(g_gone)) {
            left = true;
        } else {
            boot_start(&c, &ui);
        }
        CHECK(left);
        CHECK_EQ(ui.returns, 1);
        CHECK_EQ(io.reset_releases, 0);
        CHECK(!c.started);
        CHECK(ui.msgs.size() && ui.msgs[0].find("RUN/STOP") != std::string::npos);
    }
    // --- T243 S3: the way back fails (told, the wait goes on), then the ROMs arrive: the core starts as before
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs; ui.insert_at = 40; ui.leave_at_ms = 12000;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(ui.returns, 1);
        CHECK_EQ(ui.msgs.size(), 2);
        CHECK(ui.msgs.size() > 1 && ui.msgs[1].find("did not work: no ICAP") != std::string::npos);
        CHECK(!ui.released_early);
        CHECK_EQ(io.reset_releases, 1);
        CHECK(c.started);
    }
    // --- T243 S2: a board without the ICAP loader: no promise on the screen, RUN/STOP is not even asked
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        setup(c, io, fs, px, st, v);
        BoardProfile bp;
        memset(&bp, 0, sizeof(bp));
        bp.can_return_host = false;
        c.board = &bp;
        Ui ui; ui.io = &io; ui.fs = &fs; ui.insert_at = 25; ui.leave_at_ms = 12000; ui.return_works = true;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(ui.asks, 0);
        CHECK_EQ(ui.returns, 0);
        CHECK(ui.msgs.size() && ui.msgs[0].find("RUN/STOP") == std::string::npos);
    }
    // --- P1-4: release_core (boot.cc) lets the core go on every path that ends a start, unless run.cc found the bitstream not to be this core's: then no path releases it
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        setup(c, io, fs, px, st, v);
        char msg[200] = "";
        CHECK_EQ(boot_finish(&c, msg, sizeof(msg)), 0);                  // control: an accepted identity is released and the core is started
        CHECK_EQ(io.reset_releases, 1);
        CHECK(c.started);
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.reset_releases, 2);
    }
    {
        AtariIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        setup(c, io, fs, px, st, v);
        c.identity_refused = true;
        snprintf(c.identity_why, sizeof(c.identity_why), "the bitstream is not the Atari core");
        char msg[200] = "";
        CHECK_EQ(boot_finish(&c, msg, sizeof(msg)), 1);                  // the start's own release
        CHECK_STR(msg, "the bitstream is not the Atari core");
        CHECK_EQ(io.reset_releases, 0);
        CHECK(!c.started);
        msg[0] = 0;
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 1);              // a reset asked later: held again, not released
        CHECK_STR(msg, "the bitstream is not the Atari core");
        CHECK_EQ(io.reset_releases, 0);
        CHECK(!c.started);
    }
    // --- T243 S4: the USB keyboard's ESC asks as the matrix's RUN/STOP does; no other key does
    CHECK(boot_leave_key(KEY_BREAK));
    CHECK(boot_leave_key(KEY_ESCAPE));
    CHECK(!boot_leave_key(KEY_RETURN));
    CHECK(!boot_leave_key(KEY_SPACE));
    DONE("test_boot_atari");
}
