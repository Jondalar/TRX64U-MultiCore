// T104.2 S6, S7, S9: the C64 descriptor's start sequence on a model of the C64 core - the three ROMs at the aperture's offsets, nothing
// written while a required one is missing, the core held until all are in, the cold reset's two C64_MODE writes a reset_ms apart,
// the freeze through the gate bit. The descriptor is the real one (coreapp/cores/c64.cc).
#include <string>
#include "check.h"
#include "c64_rig.h"
#include "freeze.h"
#include "romfind.h"
#include "desc_check.h"

const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &c64_desc, 0 };

static int count_prefix(const std::vector<std::string> &ev, const char *p)
{
    int n = 0;
    for (auto &e : ev) n += e.compare(0, strlen(p), p) == 0;
    return n;
}
static int first_of(const std::vector<std::string> &ev, const char *p)
{
    for (size_t i = 0; i < ev.size(); i++) if (ev[i].compare(0, strlen(p), p) == 0) return (int)i;
    return -1;
}
static int last_of(const std::vector<std::string> &ev, const char *p)
{
    for (size_t i = ev.size(); i-- > 0;) if (ev[i].compare(0, strlen(p), p) == 0) return (int)i;
    return -1;
}

// withholds a ROM, or ships it with the wrong size, and gives the right file after a while; watches that the machine is not released before
struct Ui : BootUi {
    C64Rig *r;
    int ticks = 0, fix_at = 20;
    bool released_early = false, wrote_early = false;
    std::vector<std::string> msgs;
    void message(const char *, const char *m, bool) { msgs.push_back(m); }
    void clear() { }
    void tick()
    {
        ticks++;
        if (ticks > 300) { printf("FAIL boot_start was still waiting after %d passes\n", ticks); exit(1); }
        if (ticks < fix_at) {
            if (r->io.reset_releases > 0) released_early = true;
            if (first_of(r->io.events, "rom_write") >= 0) wrote_early = true;
        }
        if (ticks == fix_at) {
            r->fs.put("/flash/roms/kernal.bin", r->kernal);
            r->fs.put("/flash/roms/chars.bin", r->chars);
        }
    }
};

struct FzOps : FreezeOps {
    C64Model *m;
    const FreezeDesc *d;
    std::vector<std::string> ev, logs;
    int menu_t = -1;
    explicit FzOps(C64Model *mm, const FreezeDesc *dd) : m(mm), d(dd) { }
    void stop(bool on) { m->stop(on); ev.push_back(on ? "stop" : "run"); }
    uint8_t status() { return freeze_status(m, *d); }
    void layer_to_menu() { ev.push_back("menu"); menu_t = (int)m->t; }
    void layer_to_core() { ev.push_back("core"); }
    bool wait_keys_idle(uint32_t) { ev.push_back("keys"); return true; }
    uint32_t now_ms() { return m->t; }
    void delay_ms(uint32_t ms) { m->delay_ms(ms); }
    void log(const char *s) { logs.push_back(s); }
};

int main()
{
    CHECK(!desc_check(&c64_desc));
    CHECK_STR(c64_desc.cor_name, "TRX64U");
    CHECK(!memcmp(c64_desc.id, "TRX2", 4));
    CHECK_EQ(c64_desc.freeze.reg_status, 0x101);
    CHECK_EQ(c64_desc.freeze.mask, 0x01);
    CHECK_EQ(c64_desc.video.w, 0);

    // ---- S6: BASIC at +0, KERNAL at +0x2000, CHAR at +0x4000, from Gideon's layout in /flash/roms
    {
        C64Rig r;
        r.put_roms();
        BoundedUi ui;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        CHECK(!memcmp(&r.io.ap[0x0000], r.basic.data(), 8192));
        CHECK(!memcmp(&r.io.ap[0x2000], r.kernal.data(), 8192));
        CHECK(!memcmp(&r.io.ap[0x4000], r.chars.data(), 4096));
        std::vector<uint8_t> back(4096);
        r.io.rom_read(0x5000, back.data(), 4096);                 // the aperture mirrors CHAR at +0x5000: the model knows it, so a write at
        CHECK(back == r.chars);                                    // the wrong offset could not hide behind the mirror
        CHECK(r.io.events.size() >= 6);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        CHECK_EQ(count_prefix(r.io.events, "rom_write"), 3);
        CHECK_STR(ev_at(r.io.events, 1), "rom_write 0 8192");     // descriptor order: BASIC, KERNAL, CHAR
        CHECK_STR(ev_at(r.io.events, 2), "rom_write 8192 8192");
        CHECK_STR(ev_at(r.io.events, 3), "rom_write 16384 4096");
        // the freeze mode (a reset-time setting) goes in while the core is held, the release is last
        CHECK_EQ(count_prefix(r.io.events, "gate_ctrl"), 1);
        CHECK(last_of(r.io.events, "rom_write") < first_of(r.io.events, "gate_ctrl"));
        CHECK(first_of(r.io.events, "gate_ctrl") < last_of(r.io.events, "mode_unreset"));
        CHECK_EQ(r.io.gate_ctrl, 1);                               // "Whole machine" is the default: C64_STOP pauses everything
        // the C64 draws its own picture: no write reaches the ID bytes or any register of the video service (S5 on the real descriptor)
        for (auto &w : r.io.log) CHECK(w.reg >= 0x100);
        CHECK(r.px.w.empty());
        CHECK(r.c.started);
    }
    // a KERNAL of 8191 bytes is a wrong size, CHAR is missing: nothing is written, nothing is released, the screen says why. Then the files
    // come and the machine starts (S7: the core is never released with a ROM missing)
    {
        C64Rig r;
        r.fs.put("/flash/roms/basic.bin", r.basic);
        r.fs.put("/flash/roms/kernal.bin", std::vector<uint8_t>(8191, 0x77));
        Ui ui;
        ui.r = &r;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        CHECK(!ui.released_early);
        CHECK(!ui.wrote_early);
        CHECK(ui.msgs.size() >= 1);
        CHECK(ui.msgs.size() && ui.msgs[0].find("KERNAL") != std::string::npos && ui.msgs[0].find("8191") != std::string::npos);
        CHECK(r.io.t >= 9500);
        CHECK(!memcmp(&r.io.ap[0x2000], r.kernal.data(), 8192));
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
    }
    // CHAR alone missing for the same time
    {
        C64Rig r;
        r.fs.put("/flash/roms/basic.bin", r.basic);
        r.fs.put("/flash/roms/kernal.bin", r.kernal);
        Ui ui;
        ui.r = &r;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        CHECK(!ui.released_early && !ui.wrote_early);
        CHECK(ui.msgs.size() && ui.msgs[0].find("CHAR") != std::string::npos);
        CHECK(!memcmp(&r.io.ap[0x4000], r.chars.data(), 4096));
    }
    // the operations refuse bytes of the wrong size
    {
        C64Rig r;
        std::vector<uint8_t> small(100, 1);
        OpResult res;
        OpIn in = in_blob("data", small.data(), (uint32_t)small.size());
        CHECK_EQ(ops_call(&r.c, "c64.put_kernal", &in, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "KERNAL") != 0 && strstr(res.msg, "100") != 0);
        CHECK(r.io.events.empty());
    }

    // ---- D8: where the files are looked for
    {
        C64Rig mono(true), multi(false);
        mono.put_roms("/flash/roms");
        multi.put_roms("/flash/roms");                               // the same files; only the monolith looks in this directory itself
        RomResult res[3];
        char sum[200];
        CHECK_EQ(rom_scan(mono.c.fs, "c64", c64_desc.roms, 3, res, sum, sizeof(sum), mono.board.rom_dir), ROM_FOUND);
        CHECK_STR(res[0].hit.dir, "/flash/roms");
        CHECK_STR(res[1].hit.name, "kernal.bin");
        CHECK_EQ(rom_scan(multi.c.fs, "c64", c64_desc.roms, 3, res, sum, sizeof(sum), multi.board.rom_dir), ROM_MISSING);
        CHECK(strstr(sum, "BASIC") != 0);
        // a stick with roms/c64 works on both, with the names of the ROM set of the line
        FakeFs fs;
        fs.put("/usb1/roms/c64/basic.901226-01", mono.basic);
        fs.put("/usb1/roms/c64/KERNAL.901227-03", mono.kernal);
        fs.put("/usb1/roms/c64/characters.901225-01", mono.chars);
        CHECK_EQ(rom_scan(&fs, "c64", c64_desc.roms, 3, res, sum, sizeof(sum), mono.board.rom_dir), ROM_FOUND);
        CHECK_STR(res[0].hit.dir, "/usb1/roms/c64");
        CHECK_EQ(rom_scan(&fs, "c64", c64_desc.roms, 3, res, sum, sizeof(sum), 0), ROM_FOUND);
        // both present: the flash directory wins (it is searched first)
        fs.put("/flash/roms/basic.bin", mono.basic);
        fs.put("/flash/roms/kernal.bin", mono.kernal);
        fs.put("/flash/roms/chars.bin", mono.chars);
        CHECK_EQ(rom_scan(&fs, "c64", c64_desc.roms, 3, res, sum, sizeof(sum), mono.board.rom_dir), ROM_FOUND);
        CHECK_STR(res[2].hit.dir, "/flash/roms");
        // the screen text names the places it looked in
        FakeFs none;
        CHECK_EQ(rom_scan(&none, "c64", c64_desc.roms, 3, res, sum, sizeof(sum), mono.board.rom_dir), ROM_MISSING);
        CHECK(strstr(sum, "/flash/roms") != 0);
    }

    // ---- S7: the cold reset holds the machine, waits reset_ms and lets it go; the settings of the reset go in between
    {
        C64Rig r;
        r.put_roms();
        CHECK(r.boot());
        r.io.events.clear();
        r.io.assert_t.clear();
        r.io.release_t.clear();
        OpResult res;
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK_EQ((int)r.io.assert_t.size(), 1);
        CHECK_EQ((int)r.io.release_t.size(), 1);
        uint32_t held = (r.io.release_t.empty() || r.io.assert_t.empty()) ? 0 : r.io.release_t[0] - r.io.assert_t[0];
        CHECK(held >= c64_desc.reset_ms);
        CHECK(held < c64_desc.reset_ms + 20);
        CHECK_EQ(c64_desc.reset_ms, 50);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        CHECK_EQ(count_prefix(r.io.events, "rom_write"), 0);       // a reset does not touch the ROMs
        CHECK(!r.io.booted);                                       // the KERNAL starts over ...
        r.io.delay_ms(r.io.boot_ms + 10);
        CHECK(r.io.booted);                                        // ... and reaches READY
        // reload: a ROM that went missing leaves the machine running and untouched; the files back, it is held, reloaded, released
        r.fs.remove("/flash/roms/chars.bin");
        r.io.events.clear();
        CHECK_EQ(ops_call(&r.c, "machine.reload_roms", 0, 0, &res), OPE_FAILED);
        CHECK(r.io.events.empty());
        r.fs.put("/flash/roms/chars.bin", r.chars);
        CHECK_EQ(ops_call(&r.c, "machine.reload_roms", 0, 0, &res), OPE_OK);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_EQ(count_prefix(r.io.events, "rom_write"), 3);
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        // the setting: CPU only takes the gate bit away, at the next reset
        CHECK_EQ(r.io.gate_ctrl, 1);
        CHECK_EQ(ops_set(&r.c, "c64.set_freeze_mode", 1), OPE_OK);
        CHECK_EQ(r.io.gate_ctrl, 0);
        CHECK_EQ(ops_set(&r.c, "c64.set_freeze_mode", 0), OPE_OK);
        CHECK_EQ(r.io.gate_ctrl, 1);
        CHECK_EQ(ops_set(&r.c, "c64.set_freeze_mode", 2), OPE_ARG);
    }

    // ---- S9: C64_STOP, the gate bit awaited, the layer to the menu; leaving: the keys, then the machine runs
    {
        C64Rig r;
        r.put_roms();
        CHECK(r.boot());
        FzOps o(&r.io, &c64_desc.freeze);
        Freeze f(c64_desc.freeze, &o);
        uint32_t t0 = r.io.t;
        CHECK(f.enter());
        CHECK(r.io.stopped);
        CHECK(o.menu_t >= (int)(t0 + r.io.halt_after_ms));         // the menu gets the layer only once the gate bit is up
        CHECK(o.logs.empty());
        f.leave();
        CHECK(!r.io.stopped);
        CHECK_EQ((int)o.ev.size(), 5);
        CHECK(o.ev == std::vector<std::string>({ "stop", "menu", "core", "keys", "run" }));
    }
    {   // the status polled by its byte alone sees the stale latch of the word: it never rises (why the descriptor reads the word)
        C64Rig r;
        r.put_roms();
        CHECK(r.boot());
        r.io.stop(true);
        r.io.delay_ms(20);
        CHECK(r.io.halted());
        CHECK_EQ(r.io.rd(c64_desc.freeze.reg_status) & c64_desc.freeze.mask, 0);             // the byte alone: stale
        CHECK_EQ(freeze_status(&r.io, c64_desc.freeze) & c64_desc.freeze.mask, 1);           // the word's way: the bit
        r.io.stop(false);
    }
    {   // a core that never shows the gate bit: the menu opens after 200 ms and the log says so
        C64Rig r;
        r.put_roms();
        CHECK(r.boot());
        r.io.halt_after_ms = -1;
        FzOps o(&r.io, &c64_desc.freeze);
        Freeze f(c64_desc.freeze, &o);
        uint32_t t0 = r.io.t;
        CHECK(!f.enter());
        CHECK(o.menu_t - (int)t0 >= 200 && o.menu_t - (int)t0 <= 205);
        CHECK_EQ((int)o.logs.size(), 1);
        CHECK(o.logs.size() && o.logs[0].find("no PAUSED after 200 ms") != std::string::npos);
    }
    DONE("test_boot_c64");
}
