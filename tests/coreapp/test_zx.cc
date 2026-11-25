// T91 D5-D12: the ZX Spectrum descriptor against a model of the register block and the windows of cores/zx/trx/zx_regs.v / zx_win.v: the ROM
// sets and the model, the palette, the snapshot sequence (compared with the reference reader's registers and banks), the tape. The descriptor
// under test is the real one (cores/zx/app/lib/zx.cc). The snapshot corpus is made by run.sh (tools/zx_snap.py corpus).
#include <stdlib.h>
#include <fstream>
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
extern const CoreDesc zx_desc;                  // cores.h lists the shared app's cores; the ZX Spectrum is its own app (cores/zx/app/lib, run.sh zxapp)
#include "zx_regs.h"
#include "ops_glue.h"

// The core as the application sees it: 128 KB of RAM behind the window (the half in WIN_PAGE), a 32 KB ROM area, the snapshot FSM's busy
// bit, the tape position registers.
struct ZxIo : FakeIo {
    std::vector<uint8_t> mem, rom32;
    int busy_reads = 0, busy_left = 0;
    bool never_done = false;
    int go_count = 0;
    uint8_t at_go[0x200];
    uint8_t ctrl_at_go = 0;
    size_t log_at_go = 0;
    ZxIo() : mem(131072, 0), rom32(32768, 0) { memset(at_go, 0, sizeof(at_go)); }
    uint32_t base() { return (regs[ZX_WIN_PAGE] & 1) * 65536u; }
    bool memonly = false;
    int plain_writes = 0;                                            // RAM window writes made without C64_DMA_MEMONLY
    void ram_memonly(bool on) { memonly = on; }
    void ram_write(uint32_t a, const void *p, uint32_t n) { if (!memonly) plain_writes++; memcpy(&mem.at(base() + a), p, n); }
    void ram_read(uint32_t a, void *p, uint32_t n) { memcpy(p, &mem.at(base() + a), n); }
    // the aperture is 16 KB wide, the bank is WIN_PAGE b2:1
    uint32_t rbase(uint32_t off, uint32_t n) { if (off + n > 16384) overrun = true; return ((regs[ZX_WIN_PAGE] >> 1) & 1) * 16384u + off; }
    void rom_write(uint32_t off, const void *p, uint32_t n) { events.push_back("rom_write"); memcpy(&rom32.at(rbase(off, n)), p, n); }
    void rom_read(uint32_t off, void *p, uint32_t n) { memcpy(p, &rom32.at(rbase(off, n)), n); }
    void on_wr(uint32_t reg, uint8_t)
    {
        if (reg == ZX_SNAP_GO) {
            go_count++;
            memcpy(at_go, regs, sizeof(at_go));
            ctrl_at_go = regs[ZX_CTRL];
            log_at_go = log.size();
            busy_left = never_done ? 1000000 : busy_reads;
        }
    }
    uint8_t rd(uint32_t reg)
    {
        if (reg == ZX_STATUS) {
            uint8_t s = (regs[ZX_MODEL] & 1) << 3;
            if (busy_left > 0) { busy_left--; s |= ZX_ST_SNAPBUSY; }
            return s;
        }
        return FakeIo::rd(reg);
    }
};

struct Ui : BootUi {
    std::vector<std::string> msgs;
    int ticks = 0, insert_at = -1;
    FakeFs *fs = 0;
    void message(const char *, const char *m, bool) { msgs.push_back(m); }
    void clear() { }
    void tick() { if (++ticks == insert_at) fs->put("/usb0/roms/zx/48.rom", rom48()); }
    static std::vector<uint8_t> rom48() { std::vector<uint8_t> v(16384); for (int i = 0; i < 16384; i++) v[i] = (uint8_t)(i * 7 + 3); return v; }
    static std::vector<uint8_t> rom128() { std::vector<uint8_t> v(32768); for (int i = 0; i < 32768; i++) v[i] = (uint8_t)(i * 5 + 1 + (i >> 14) * 7); return v; }
};

static void setup(CoreCtx &c, ZxIo &io, FakeFs &fs, PxRec &px, Settings *&st, Video *&v)
{
    memset(&c, 0, sizeof(c));
    c.desc = &zx_desc; c.io = &io; c.fs = &fs;
    st = new Settings(zx_desc.settings, zx_desc.nsettings, zx_desc.store, &fs, "/sd/cores/zx.cfg", 0);
    st->load();
    v = new Video(zx_desc.video, &px, &io);
    c.settings = st; c.video = v;
}

static std::vector<uint8_t> slurp(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void u16(std::vector<uint8_t> &b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }

int main()
{
    const char *corp = getenv("ZX_CORPUS");
    if (!corp) { printf("FAIL: ZX_CORPUS is not set - run this through tests/coreapp/run.sh\n"); return 1; }
    std::string dir = corp;

    // --- both ROMs: the 48K runs, the machine is held by CTRL and the host's reset, the picture's 16 colours are written
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put("/usb0/roms/zx/48.rom", Ui::rom48());
        fs.put("/usb0/roms/zx/128.rom", Ui::rom128());
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.reset_releases, 1);
        CHECK_EQ(io.log.front().reg, ZX_CTRL);
        CHECK_EQ(io.log.front().v, ZX_CTRL_HOLD);
        CHECK_EQ(io.log.back().reg, ZX_CTRL);
        CHECK_EQ(io.log.back().v, 0);
        CHECK_STR(io.events.back().c_str(), "mode_unreset");
        CHECK_EQ(io.regs[ZX_MODEL], 0);
        auto r48 = Ui::rom48();
        CHECK(memcmp(io.rom32.data(), r48.data(), 16384) == 0);
        CHECK_EQ(io.regs[ZX_JOY1], 0);                                // none on port 1, Kempston on port 2
        CHECK_EQ(io.regs[ZX_JOY2], 1);
        // the picture (T119): RGB565 from the core into the pixel layer, no palette to write; 344 x 280 at 3 x 3 centred
        CHECK_EQ(io.pal_writes, 0);
        CHECK_EQ(zx_desc.video.w, 344u);
        CHECK_EQ(zx_desc.video.h, 280u);
        CHECK_EQ(zx_desc.video.scale, 3);
        CHECK_EQ(zx_desc.video.x0, 444);                             // (1920 - 3 x 344) / 2
        CHECK_EQ(zx_desc.video.y0, 120);                             // (1080 - 3 x 280) / 2
        CHECK_EQ(zx_desc.video.nbuf, 4);
        CHECK_EQ(zx_desc.freeze.reg_status, ZX_STATUS);
        // the model is a setting taken in reset: 128K, then a reset puts the 32 KB ROM in and the model register
        st->set(st->find("Model"), 1);
        char msg[300];
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[ZX_MODEL], 1);
        auto r128 = Ui::rom128();
        CHECK(memcmp(io.rom32.data(), r128.data(), 32768) == 0);
        // back to 48K: the 16 KB are rewritten, and the 128K ROM's second half is not what the machine runs
        st->set(st->find("Model"), 0);
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[ZX_MODEL], 0);
        CHECK(memcmp(io.rom32.data(), r48.data(), 16384) == 0);
        // joysticks and the tape sound are live; the sound keeps the reset hold bit as it is
        st->set(st->find("Joystick port 1"), 4); ops_set(&c, "zx.set_joy1", 4);
        st->set(st->find("Joystick port 2"), 3); ops_set(&c, "zx.set_joy2", 3);
        CHECK_EQ(io.regs[ZX_JOY1], 4);
        CHECK_EQ(io.regs[ZX_JOY2], 3);
        ops_set(&c, "zx.set_ear", 1);
        CHECK_EQ(io.regs[ZX_CTRL], ZX_CTRL_EAR);
        // a reset holds with the sound bit kept
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        bool held_with_ear = false;
        for (auto &w : io.log) held_with_ear |= w.reg == ZX_CTRL && w.v == (ZX_CTRL_HOLD | ZX_CTRL_EAR);
        CHECK(held_with_ear);
    }
    // --- only the 48K ROM: a 128K choice is not run, the 48K is, and the screen says so
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put("/usb0/roms/zx/48.rom", Ui::rom48());
        fs.put_text("/sd/cores/zx.cfg", "[ZX Spectrum Settings]\nModel=128K\n\n");
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.regs[ZX_MODEL], 0);
        CHECK(c.notice_pending && strstr(c.notice, "128.rom"));
    }
    // --- only the 128K ROM: that is a complete set, the 48K default runs as a 128K
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put("/usb0/roms/zx/128.rom", Ui::rom128());
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.regs[ZX_MODEL], 1);
        CHECK(c.notice_pending && strstr(c.notice, "48.rom"));
        auto r128 = Ui::rom128();
        CHECK(memcmp(io.rom32.data(), r128.data(), 32768) == 0);
    }
    // --- no ROM: held, the screen names the file, the machine starts when it comes; a ROM of the wrong size is told
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put_size("/usb0/roms/zx/48.rom", 1000);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs; ui.insert_at = 4;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK(ui.msgs.size() >= 1);
        if (ui.msgs.size() >= 1) {
            CHECK(strstr(ui.msgs[0].c_str(), "48K ROM"));
            CHECK(strstr(ui.msgs[0].c_str(), "1000 bytes"));
            CHECK(strstr(ui.msgs[0].c_str(), "One complete set"));
        }
        CHECK_EQ(io.reset_releases, 1);
    }
    // --- snapshots: the register image, the banks and the order of the sequence
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put("/usb0/roms/zx/48.rom", Ui::rom48());
        fs.put("/usb0/roms/zx/128.rom", Ui::rom128());
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        const FileAction &sa = zx_desc.filetypes[0].actions[0];
        CHECK_STR(sa.label, "Run snapshot");
        const char *names[] = { "sna128_13.sna", "z80v3p_128_13.z80", "sna48.sna", "z80v1p_48.z80", "z80v2r_48.z80", "sna128_15.sna" };
        for (const char *name : names) {
            std::string n = name, base = n.substr(0, n.rfind('.'));
            std::string path = "/usb0/snap/" + n;
            fs.put(path, slurp(dir + "/" + n));
            io.busy_reads = 5; io.go_count = 0;
            size_t evs = io.events.size();
            OpResult r;
            int rc = ops_run_bound(&c, sa.op, sa.bind, sa.nbind, path.c_str(), &r);
            if (rc) printf("  %s: %s\n", name, r.msg);
            CHECK_EQ(rc, 0);
            CHECK_EQ(io.go_count, 1);
            // the host's reset line is not touched, the machine is held by CTRL alone, and the load ends with the hold gone
            for (size_t i = evs; i < io.events.size(); i++) CHECK(io.events[i] != "mode_reset" && io.events[i] != "mode_unreset");
            CHECK_EQ(io.ctrl_at_go & ZX_CTRL_HOLD, ZX_CTRL_HOLD);
            CHECK_EQ(io.regs[ZX_CTRL] & ZX_CTRL_HOLD, 0);
            CHECK_EQ(io.regs[ZX_WIN_PAGE], 0);
            CHECK(!io.overrun);
            CHECK_EQ(io.plain_writes, 0);                                  // $D000-$DFFF of bank 3 is RAM, not a bus cycle of the host
            CHECK(!io.memonly);
            // the registers at the moment of SNAP_GO are the reference's
            std::ifstream regs(dir + "/" + base + ".regs.hex");
            unsigned off, val;
            int bad = 0, cnt = 0;
            while (regs >> std::hex >> off >> val) {
                cnt++;
                if (io.at_go[off] != val) { bad++; printf("  %s: register %03x was %02x at SNAP_GO, the reference says %02x\n", name, off, io.at_go[off], val); }
            }
            CHECK_EQ(cnt, 47);
            CHECK_EQ(bad, 0);
            // the RAM through the window: the banks the file holds
            std::ifstream meta(dir + "/" + base + ".meta");
            std::string l1, l2;
            std::getline(meta, l1); std::getline(meta, l2);
            std::vector<int> banks;
            for (size_t p = l2.find('=') + 1; p < l2.size();) { banks.push_back(atoi(l2.c_str() + p)); size_t cm = l2.find(',', p); if (cm == std::string::npos) break; p = cm + 1; }
            std::ifstream ram(dir + "/" + base + ".ram.hex");
            int wrong = 0;
            for (int b : banks)
                for (unsigned k = 0; k < 16384; k++) {
                    unsigned vv = 0; ram >> std::hex >> vv;
                    if (io.mem[b * 16384 + k] != vv) wrong++;
                }
            CHECK_EQ(wrong, 0);
            // the model's ROM is in
            auto want = io.regs[ZX_MODEL] ? Ui::rom128() : Ui::rom48();
            CHECK(memcmp(io.rom32.data(), want.data(), want.size()) == 0);
        }
        CHECK_STR(zx_desc.filetypes[1].ext, ".z80");
    }
    // --- a snapshot that cannot run leaves the machine alone: a 128K file without the 128K ROM, a damaged file, a load that never ends
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put("/usb0/roms/zx/48.rom", Ui::rom48());
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        const FileAction &sa = zx_desc.filetypes[0].actions[0];
        fs.put("/usb0/s128.sna", slurp(dir + "/sna128_13.sna"));
        fs.put("/usb0/bad.sna", std::vector<uint8_t>(1234, 0));
        fs.put("/usb0/s48.sna", slurp(dir + "/sna48.sna"));
        OpResult r;
        size_t n = io.log.size();
        CHECK(ops_run_bound(&c, sa.op, sa.bind, sa.nbind, "/usb0/s128.sna", &r) != 0);
        CHECK(strstr(r.msg, "128.rom") != 0);
        CHECK(ops_run_bound(&c, sa.op, sa.bind, sa.nbind, "/usb0/bad.sna", &r) != 0);
        CHECK(strstr(r.msg, "neither a 48K nor a 128K") != 0);
        CHECK(ops_run_bound(&c, sa.op, sa.bind, sa.nbind, "/usb0/nothere.sna", &r) != 0);
        CHECK(strstr(r.msg, "cannot open") != 0);
        CHECK_EQ(io.log.size(), n);                                   // not one register written
        CHECK_EQ(io.go_count, 0);
        io.never_done = true;
        CHECK(ops_run_bound(&c, sa.op, sa.bind, sa.nbind, "/usb0/s48.sna", &r) != 0);
        CHECK(strstr(r.msg, "did not finish") != 0);
        CHECK_EQ(io.regs[ZX_CTRL] & ZX_CTRL_HOLD, ZX_CTRL_HOLD);       // still held: the person sees it and resets
    }
    // --- tapes: the stream in the DDR2, the engine's registers, Play as an edge, the blocks, the stop marker, the eject
    {
        ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        fs.put("/usb0/roms/zx/48.rom", Ui::rom48());
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        std::vector<uint8_t> tap;
        std::vector<uint8_t> h = { 0x00, 1, 2, 3 }, d = { 0xFF, 9, 8 };
        u16(tap, h.size()); tap.insert(tap.end(), h.begin(), h.end());
        u16(tap, d.size()); tap.insert(tap.end(), d.begin(), d.end());
        fs.put("/usb0/g.tap", tap);
        const FileType *ft = 0;
        for (int i = 0; i < zx_desc.nfiletypes; i++) if (!strcmp(zx_desc.filetypes[i].ext, ".tap")) ft = &zx_desc.filetypes[i];
        CHECK(ft != 0);
        CHECK_STR(ft->actions[0].label, "Insert tape");
        CHECK_STR(ft->actions[1].label, "Insert tape and play");
        OpResult r;
        CHECK_EQ(ops_run_bound(&c, ft->actions[0].op, ft->actions[0].bind, ft->actions[0].nbind, "/usb0/g.tap", &r), 0);
        CHECK_STR(ops_state(&c, "zx.tape.image"), "g.tap");
        auto word = [&](unsigned i) { return (unsigned)(io.ddr[ZX_W_TAPE + 2 * i] | io.ddr[ZX_W_TAPE + 2 * i + 1] << 8); };
        CHECK_EQ(word(0), 2168);
        CHECK_EQ(word(8062), 2168);
        CHECK_EQ(word(8063), 667);
        CHECK_EQ(word(8064), 735);
        uint32_t base = io.regs[ZX_TAPE_BASE] | io.regs[ZX_TAPE_BASE + 1] << 8 | io.regs[ZX_TAPE_BASE + 2] << 16 | (uint32_t)io.regs[ZX_TAPE_BASE + 3] << 24;
        CHECK_EQ(base, 0x01800000u);
        CHECK_EQ(io.regs[ZX_TAPE_CTRL], 0);
        // Play is a rising edge
        size_t n = io.log.size();
        CHECK_EQ(ops_call(&c, "zx.tape_play", 0, 0, &r), 0);
        CHECK_EQ(io.log.size() - n, 2);
        CHECK(io.log[n].reg == ZX_TAPE_CTRL && io.log[n].v == 0 && io.log[n + 1].reg == ZX_TAPE_CTRL && io.log[n + 1].v == 1);
        // at the end of a block the engine stops itself (stop marker or end): the service takes the control bit back
        io.regs[ZX_TAPE_STATUS] = ZX_TS_STOP;
        CHECK_EQ(ops_call(&c, "zx.service", 0, 0, &r), 0);
        CHECK_EQ(io.regs[ZX_TAPE_CTRL], 0);
        io.regs[ZX_TAPE_STATUS] = 0;
        // blocks: rewind, next block, previous block (the start of the current one first)
        auto pos_reg = [&]() { return (unsigned)(io.regs[ZX_TAPE_POS] | io.regs[ZX_TAPE_POS + 1] << 8 | io.regs[ZX_TAPE_POS + 2] << 16 | io.regs[ZX_TAPE_POS + 3] << 24); };
        CHECK_EQ(ops_call(&c, "zx.tape_next", 0, 0, &r), 0);
        unsigned second = pos_reg();
        CHECK(second > 8000);
        CHECK_EQ(ops_call(&c, "zx.tape_next", 0, 0, &r), -7);          // the last block
        CHECK(strstr(r.msg, "last block") != 0);
        io.regs[ZX_TAPE_POS] = (uint8_t)(second + 10); io.regs[ZX_TAPE_POS + 1] = (uint8_t)((second + 10) >> 8);   // the position moved on inside block 2
        CHECK_EQ(ops_call(&c, "zx.tape_prev", 0, 0, &r), 0);
        CHECK_EQ(pos_reg(), second);                                    // first the start of this block ...
        CHECK_EQ(ops_call(&c, "zx.tape_prev", 0, 0, &r), 0);
        CHECK_EQ(pos_reg(), 0u);                                        // ... then the one before
        CHECK_EQ(ops_call(&c, "zx.tape_next", 0, 0, &r), 0);
        CHECK_EQ(ops_call(&c, "zx.tape_rewind", 0, 0, &r), 0);
        CHECK_EQ(pos_reg(), 0u);
        CHECK(io.regs[ZX_TAPE_SEEK] == 1);
        // eject
        CHECK_EQ(ops_call(&c, "zx.tape_eject", 0, 0, &r), 0);
        CHECK(ops_state(&c, "zx.tape.image") == 0);
        CHECK_EQ(ops_call(&c, "zx.tape_play", 0, 0, &r), -7);
        CHECK(strstr(r.msg, "no tape") != 0);
        // "insert and play" starts the tape; a TZX block 2A stops a 48K and not a 128K
        CHECK_EQ(ops_run_bound(&c, ft->actions[1].op, ft->actions[1].bind, ft->actions[1].nbind, "/usb0/g.tap", &r), 0);
        CHECK_EQ(io.regs[ZX_TAPE_CTRL], 1);
        std::vector<uint8_t> z = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20, 0x12, 100, 0, 1, 0, 0x2A, 0, 0, 0, 0, 0x12, 100, 0, 1, 0 };
        fs.put("/usb0/s.tzx", z);
        io.regs[ZX_MODEL] = 0;
        CHECK_EQ(ops_call(&c, "zx.tape_insert", 0, 0, &r) != 0, 1);      // arguments are checked
        OpIn in[2] = { in_str("path", "/usb0/s.tzx"), in_int("play", 0) };
        CHECK_EQ(ops_call(&c, "zx.tape_insert", in, 2, &r), 0);
        CHECK_EQ(word(1), 0xFFFE);                                      // 48K: the stop marker
        io.regs[ZX_MODEL] = 1;
        CHECK_EQ(ops_call(&c, "zx.tape_insert", in, 2, &r), 0);
        CHECK_EQ(word(1), 100);                                         // 128K: the block is ignored
        // refusals leave no tape behind: a wrong extension, a tape the engine cannot play
        fs.put("/usb0/x.txt", std::vector<uint8_t>(10, 0));
        OpIn bad1[2] = { in_str("path", "/usb0/x.txt"), in_int("play", 0) };
        CHECK(ops_call(&c, "zx.tape_insert", bad1, 2, &r) != 0 && strstr(r.msg, "is not a tape"));
        std::vector<uint8_t> turbo = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20, 0x12, 5, 0, 3, 0 };
        fs.put("/usb0/t.tzx", turbo);
        OpIn bad2[2] = { in_str("path", "/usb0/t.tzx"), in_int("play", 0) };
        CHECK(ops_call(&c, "zx.tape_insert", bad2, 2, &r) != 0 && strstr(r.msg, "shorter than 16"));
        CHECK(ops_state(&c, "zx.tape.image") == 0);
    }
    DONE("test_zx");
}
