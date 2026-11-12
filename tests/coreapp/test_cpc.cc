// T92 D5-D12: the Amstrad CPC descriptor against a model of the register block and the windows of cores/cpc/trx/cpc_regs.v / cpc_win.v / cpc_fdc.v:
// the ROM sets of the three models, the frame buffers, the DSK drives with their write-back (dirty chunks taken from the core, written to the file
// when the writes have stopped), the tape. The descriptor under test is the real one (coreapp/cores/cpc.cc).
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "cpc_regs.h"
#include "cpc_disk.h"
#include "engines.h"
#include "ops_glue.h"

// The core as the application sees it: three 16 KB ROM banks behind the aperture (the bank in WIN_PAGE b2:1), the running model in STATUS, the
// FDC's dirty bits (write-1-to-clear) for two drives.
struct CpcIo : FakeIo {
    std::vector<uint8_t> bank[3];
    uint8_t dirty[2][8];
    std::vector<uint8_t> ctrl_writes;
    CpcIo() { for (auto &b : bank) b.assign(CPC_ROM_BYTES, 0); memset(dirty, 0, sizeof(dirty)); }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        if (off + n > 16384) overrun = true;                         // the aperture is 16 KB wide
        int b = (regs[CPC_WIN_PAGE] >> 1) & 3;
        events.push_back("rom_write " + std::to_string(b));
        memcpy(&bank[b % 3].at(off), p, n);
    }
    void rom_read(uint32_t off, void *p, uint32_t n) { int b = (regs[CPC_WIN_PAGE] >> 1) & 3; memcpy(p, &bank[b % 3].at(off), n); }
    void on_wr(uint32_t reg, uint8_t v)
    {
        if (reg >= CPC_D0_DIRTY && reg < CPC_D0_DIRTY + 8) dirty[0][reg - CPC_D0_DIRTY] &= (uint8_t)~v;
        if (reg >= CPC_D1_DIRTY && reg < CPC_D1_DIRTY + 8) dirty[1][reg - CPC_D1_DIRTY] &= (uint8_t)~v;
        if (reg == CPC_DISK_CTRL) ctrl_writes.push_back(v);
    }
    uint8_t rd(uint32_t reg)
    {
        if (reg == CPC_STATUS) return (uint8_t)((regs[CPC_MODEL] & 3) << 2);
        if (reg >= CPC_D0_DIRTY && reg < CPC_D0_DIRTY + 8) return dirty[0][reg - CPC_D0_DIRTY];
        if (reg >= CPC_D1_DIRTY && reg < CPC_D1_DIRTY + 8) return dirty[1][reg - CPC_D1_DIRTY];
        return FakeIo::rd(reg);
    }
    // the FDC wrote one byte of drive d's image
    void fdc_write(int d, uint32_t off, uint8_t v) { ddr.at(CPC_W_DISK(d) + off) = v; dirty[d][(off >> 14) >> 3] |= (uint8_t)(1 << ((off >> 14) & 7)); }
};

struct Ui : BootUi {
    std::vector<std::string> msgs;
    int ticks = 0, insert_at = -1;
    FakeFs *fs = 0;
    void message(const char *, const char *m, bool) { msgs.push_back(m); }
    void clear() { }
    void tick() { if (++ticks == insert_at) fs->put("/usb0/roms/cpc/amsdos.rom", rom(6)); }
    static std::vector<uint8_t> rom(int n) { std::vector<uint8_t> v(CPC_ROM_BYTES); for (int i = 0; i < (int)CPC_ROM_BYTES; i++) v[i] = (uint8_t)(i * (n + 3) + n); return v; }
};

static const char *const names[7] = { "os464.rom", "basic464.rom", "os664.rom", "basic664.rom", "os6128.rom", "basic6128.rom", "amsdos.rom" };
static void put_roms(FakeFs &fs, unsigned mask)
{
    for (int i = 0; i < 7; i++) if (mask & (1u << i)) fs.put(std::string("/usb0/roms/cpc/") + names[i], Ui::rom(i));
}
static const unsigned ALL = 0x7F, S464 = 3, S664 = 4 | 8 | 64, S6128 = 16 | 32 | 64;

static void setup(CoreCtx &c, CpcIo &io, FakeFs &fs, PxRec &px, Settings *&st, Video *&v)
{
    memset(&c, 0, sizeof(c));
    c.desc = &cpc_desc; c.io = &io; c.fs = &fs;
    st = new Settings(cpc_desc.settings, cpc_desc.nsettings, cpc_desc.store, &fs, "/sd/cores/cpc.cfg", 0);
    st->load();
    v = new Video(cpc_desc.video, &px, &io);
    c.settings = st; c.video = v;
}

static std::vector<uint8_t> dsk(size_t n, bool extended = false)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 31 + 7);
    const char *magic = extended ? "EXTENDED CPC DSK File\r\nDisk-Info\r\n" : "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    memcpy(v.data(), magic, strlen(magic));
    return v;
}

static const FileAction &act(const char *ext, int k)
{
    for (int i = 0; i < cpc_desc.nfiletypes; i++) if (!strcmp(cpc_desc.filetypes[i].ext, ext)) return cpc_desc.filetypes[i].actions[k];
    static FileAction none = { 0, 0, 0, 0, false, 0, 0 };
    return none;
}

static int run(CoreCtx &c, const FileAction &a, const char *path, OpResult &r) { return ops_run_bound(&c, a.op, a.bind, a.nbind, path, &r); }

int main()
{
    // --- all ROMs: the 6128 runs; OS, BASIC and AMSDOS in the three banks; the frame buffers are told to the core; the CRTC type is the default
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, ALL);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.regs[CPC_MODEL], 2);
        CHECK(io.bank[0] == Ui::rom(4) && io.bank[1] == Ui::rom(5) && io.bank[2] == Ui::rom(6));
        CHECK_EQ(io.regs[CPC_CRTC], 1);
        CHECK_EQ(io.regs[CPC_JOY], 0);
        CHECK_EQ(io.regs[CPC_WIN_PAGE], 0);                           // the ROM bank selection is put back
        CHECK(!io.overrun);
        CHECK_EQ(io.log.front().reg, CPC_CTRL);
        CHECK_EQ(io.log.front().v, CPC_CTRL_HOLD);
        CHECK_EQ(io.log.back().reg, CPC_CTRL);
        CHECK_EQ(io.log.back().v, 0);
        CHECK_EQ(io.reset_releases, 1);
        uint32_t base = io.regs[CPC_FB_BASE] | io.regs[CPC_FB_BASE + 1] << 8 | io.regs[CPC_FB_BASE + 2] << 16 | (uint32_t)io.regs[CPC_FB_BASE + 3] << 24;
        uint32_t stride = io.regs[CPC_FB_STRIDE] | io.regs[CPC_FB_STRIDE + 1] << 8 | io.regs[CPC_FB_STRIDE + 2] << 16 | (uint32_t)io.regs[CPC_FB_STRIDE + 3] << 24;
        CHECK_EQ(base, 0x01D00000u);
        CHECK_EQ(stride, 0x100000u);
        CHECK(cpc_desc.video.w == 720 && cpc_desc.video.h == 540 && cpc_desc.video.nbuf == 3);
        // the models are settings taken in reset: the 464 has two ROMs and no AMSDOS write, the 664 shares the 6128's AMSDOS
        char msg[300];
        st->set(st->find("Model"), 0);
        io.events.clear();
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[CPC_MODEL], 0);
        CHECK(io.bank[0] == Ui::rom(0) && io.bank[1] == Ui::rom(1));
        CHECK(io.bank[2] == Ui::rom(6));                              // untouched: a 464 has no disk ROM and reads none
        for (auto &e : io.events) CHECK(e != "rom_write 2");
        st->set(st->find("Model"), 1);
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[CPC_MODEL], 1);
        CHECK(io.bank[0] == Ui::rom(2) && io.bank[1] == Ui::rom(3) && io.bank[2] == Ui::rom(6));
        // joystick ports and the CRTC type
        st->set(st->find("Joystick ports"), 1); ops_set(&c, "cpc.set_joy", 1);
        CHECK_EQ(io.regs[CPC_JOY], 1);
        st->set(st->find("CRTC type"), 0);
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[CPC_CRTC], 0);
    }
    // --- the ROM sets: only the 464's, only the 664's (its AMSDOS is the 6128's), the 6128's without AMSDOS (told by name)
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, S464);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.regs[CPC_MODEL], 0);                              // the 6128 is the default, the 464 is what exists
        CHECK(c.notice_pending && strstr(c.notice, "CPC 464") && strstr(c.notice, "os6128.rom"));
    }
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, S664);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.regs[CPC_MODEL], 1);
        CHECK(io.bank[2] == Ui::rom(6));
    }
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, S6128 & ~64u);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs; ui.insert_at = 25;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);                  // held until the AMSDOS arrives
        CHECK(ui.msgs.size() >= 1);
        if (ui.msgs.size() >= 1) CHECK(strstr(ui.msgs[0].c_str(), "AMSDOS (amsdos.rom)") && strstr(ui.msgs[0].c_str(), "One complete set"));
        CHECK_EQ(io.regs[CPC_MODEL], 2);
    }
    // --- disks: the image into the window, the registers, the mount; "and reset" is a cold start, the others are swaps
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, ALL);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        std::vector<uint8_t> img = dsk(40000);
        fs.put("/usb0/g.dsk", img);
        OpResult r;
        CHECK_STR(act(".dsk", 0).label, "Mount A: and reset");
        int asserts = io.reset_asserts;
        CHECK_EQ(run(c, act(".dsk", 0), "/usb0/g.dsk", r), 0);
        CHECK_EQ(io.reset_asserts, asserts + 1);
        CHECK(memcmp(&io.ddr[CPC_W_DISK(0)], img.data(), img.size()) == 0);
        auto r32 = [&](uint32_t reg) { return (uint32_t)(io.regs[reg] | io.regs[reg + 1] << 8 | io.regs[reg + 2] << 16 | (uint32_t)io.regs[reg + 3] << 24); };
        CHECK_EQ(r32(CPC_D0_BASE), 0x01000000u);
        CHECK_EQ(r32(CPC_D0_SIZE), 40000u);
        CHECK(io.ctrl_writes.size() == 1 && io.ctrl_writes[0] == CPC_DISK_MOUNT(0));
        CHECK_STR(ops_state(&c, "cpc.a.image"), "g.dsk");
        // B: is a swap, no reset
        fs.put("/usb0/h.dsk", dsk(30000, true));
        asserts = io.reset_asserts;
        CHECK_STR(act(".dsk", 2).label, "Mount B:");
        CHECK_EQ(run(c, act(".dsk", 2), "/usb0/h.dsk", r), 0);
        CHECK_EQ(io.reset_asserts, asserts);
        CHECK_EQ(r32(CPC_D1_BASE), 0x01100000u);
        CHECK_EQ(r32(CPC_D1_SIZE), 30000u);
        CHECK_EQ(io.ctrl_writes.back(), CPC_DISK_MOUNT(1));
        CHECK_STR(ops_state(&c, "cpc.b.image"), "h.dsk");
        // refusals leave the drives as they are
        size_t n = io.log.size();
        fs.put("/usb0/x.dsk", std::vector<uint8_t>(40000, 0));
        CHECK(run(c, act(".dsk", 1), "/usb0/x.dsk", r) != 0 && strstr(r.msg, "not a DSK"));
        fs.put("/usb0/tiny.dsk", std::vector<uint8_t>(100, 0));
        CHECK(run(c, act(".dsk", 1), "/usb0/tiny.dsk", r) != 0 && strstr(r.msg, "not a DSK"));
        fs.put("/usb0/big.dsk", dsk(CPC_DSK_MAX + 1, true));
        CHECK(run(c, act(".dsk", 1), "/usb0/big.dsk", r) != 0 && strstr(r.msg, "880 KB"));
        fs.put("/usb0/ok880.dsk", dsk(CPC_DSK_MAX, true));
        CHECK(run(c, act(".dsk", 1), "/usb0/nothere.dsk", r) != 0 && strstr(r.msg, "cannot open"));
        CHECK_EQ(io.log.size(), n);
        CHECK_STR(ops_state(&c, "cpc.a.image"), "g.dsk");
        // the largest image the drive takes
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/ok880.dsk", r), 0);
        CHECK_EQ(r32(CPC_D0_SIZE), CPC_DSK_MAX);
        CHECK_STR(ops_state(&c, "cpc.a.image"), "ok880.dsk");
        // eject
        const Task *ea = 0;
        for (int i = 0; i < cpc_desc.ntasks; i++) if (!strcmp(cpc_desc.tasks[i].label, "Eject A:")) ea = &cpc_desc.tasks[i];
        CHECK(ea != 0);
        CHECK_EQ(ops_run_bound(&c, ea->op, ea->bind, ea->nbind, 0, &r), 0);
        CHECK(ops_state(&c, "cpc.a.image") == 0);
        CHECK_EQ(io.ctrl_writes.back(), CPC_DISK_EJECT(0));
        CHECK_EQ(r32(CPC_D0_SIZE), 0u);
    }
    // --- write-back: the FDC's chunks are taken at once and written when the writes have stopped; the last chunk is short
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, ALL);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        std::vector<uint8_t> img = dsk(40000);
        fs.put("/usb0/g.dsk", img);
        OpResult r;
        io.dirty[0][3] = 0x05;                                       // marks of an earlier image are not this one's
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/g.dsk", r), 0);
        CHECK_EQ(io.dirty[0][3], 0);
        io.fdc_write(0, 16384 + 10, 0xEE);                          // chunk 1
        io.fdc_write(0, 39990, 0xDD);                               // chunk 2, which ends at 40000 (7232 bytes)
        CHECK_EQ(ops_call(&c, "cpc.service", 0, 0, &r), 0);
        CHECK(io.dirty[0][0] == 0);                                  // taken from the core
        CHECK_EQ(fs.writes, 0);                                      // ... and not written yet
        io.advance(600 * 1000);
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK_EQ(fs.writes, 0);
        io.advance(500 * 1000);                                      // 1.1 s after the last mark
        ops_call(&c, "cpc.service", 0, 0, &r);
        const std::vector<uint8_t> &f = fs.files["/usb0/g.dsk"];
        CHECK_EQ(f.size(), 40000u);
        CHECK_EQ(f[16384 + 10], 0xEE);
        CHECK_EQ(f[39990], 0xDD);
        CHECK(memcmp(f.data(), img.data(), 16384) == 0);             // chunk 0 was never marked: untouched
        CHECK_EQ(fs.writes, 2);                                      // two chunks, nothing else
        CHECK(fs.syncs >= 1);
        // a write that lands while the chunk is being taken is not lost: the bit comes again
        io.fdc_write(0, 5, 0x42);
        ops_call(&c, "cpc.service", 0, 0, &r);
        io.fdc_write(0, 6, 0x43);
        ops_call(&c, "cpc.service", 0, 0, &r);
        io.advance(1100 * 1000);
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK(fs.files["/usb0/g.dsk"][5] == 0x42 && fs.files["/usb0/g.dsk"][6] == 0x43);
        // constant activity: written after five seconds at the latest
        int w0 = fs.writes;
        for (int i = 0; i < 14; i++) {
            io.fdc_write(0, 100 + i, (uint8_t)(0x50 + i));
            ops_call(&c, "cpc.service", 0, 0, &r);
            CHECK(i < 13 || fs.writes > w0);                         // the chunk never rested for a second, and went at five
            io.advance(400 * 1000);
            if (i < 13) CHECK_EQ(fs.writes, w0);
        }
        CHECK(fs.files["/usb0/g.dsk"][100] == 0x50);                 // the first write is in the file by now
        io.advance(1100 * 1000);
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK(fs.files["/usb0/g.dsk"][113] == 0x5D);
    }
    // --- a medium that fails: told once, kept in memory, tried again; eject and mount refuse while it is unwritten; a reset flushes
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, ALL);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        fs.put("/usb0/g.dsk", dsk(40000));
        fs.put("/usb0/h.dsk", dsk(20000));
        OpResult r;
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/g.dsk", r), 0);
        io.fdc_write(0, 300, 0x99);
        ops_call(&c, "cpc.service", 0, 0, &r);
        fs.read_only = true;                                         // the card is write protected after the mount
        io.advance(1100 * 1000);
        c.notice_pending = false;
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK(c.notice_pending && strstr(c.notice, "A: could not be written"));
        c.notice_pending = false;
        io.advance(300 * 1000);
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK(!c.notice_pending);                                    // told once
        const Task *ea = 0;
        for (int i = 0; i < cpc_desc.ntasks; i++) if (!strcmp(cpc_desc.tasks[i].label, "Eject A:")) ea = &cpc_desc.tasks[i];
        CHECK(ops_run_bound(&c, ea->op, ea->bind, ea->nbind, 0, &r) != 0);
        CHECK(strstr(r.msg, "not ejected") != 0);
        CHECK_STR(ops_state(&c, "cpc.a.image"), "g.dsk");
        CHECK(run(c, act(".dsk", 1), "/usb0/h.dsk", r) != 0 && strstr(r.msg, "not mounted"));
        CHECK_STR(ops_state(&c, "cpc.a.image"), "g.dsk");
        fs.read_only = false;                                        // the card is back: the data was kept
        io.advance(1100 * 1000);
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK_EQ(fs.files["/usb0/g.dsk"][300], 0x99);
        // a reset writes what is pending: the hook flushes before the machine starts over
        io.fdc_write(0, 400, 0x5A);
        char msg[300];
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(fs.files["/usb0/g.dsk"][400], 0x5A);
        // eject with a marked chunk: flushed first
        io.fdc_write(0, 500, 0x66);
        CHECK_EQ(ops_run_bound(&c, ea->op, ea->bind, ea->nbind, 0, &r), 0);
        CHECK_EQ(fs.files["/usb0/g.dsk"][500], 0x66);
        // a read-only card: the drive is write protected and nothing is written
        fs.read_only = true;
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/h.dsk", r), 0);
        CHECK_EQ(io.ctrl_writes.back(), CPC_DISK_MOUNT(0) | CPC_DISK_WP(0));
        int wa = fs.write_attempts;
        io.fdc_write(0, 10, 1);
        io.advance(2000 * 1000);
        ops_call(&c, "cpc.service", 0, 0, &r);
        CHECK_EQ(fs.write_attempts, wa);
        // the setting "Disks write protected" is taken at the mount
        fs.read_only = false;
        st->set(st->find("Disks write protected"), 1); ops_set(&c, "cpc.set_wp", 1);
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/h.dsk", r), 0);
        CHECK_EQ(io.ctrl_writes.back(), CPC_DISK_MOUNT(0) | CPC_DISK_WP(0));
        st->set(st->find("Disks write protected"), 0); ops_set(&c, "cpc.set_wp", 0);
        CHECK_EQ(run(c, act(".dsk", 2), "/usb0/g.dsk", r), 0);
        CHECK_EQ(io.ctrl_writes.back(), CPC_DISK_MOUNT(1) | CPC_DISK_WP(0));   // drive A: keeps its protect bit, B: has none
    }
    // --- T180 D4 / D1 (S7): what the FDC wrote is in the file before the disk is let go - also when the poll has not taken the marks yet; a sync the medium refuses
    // is not forgotten with the cleared marks; a medium that refuses keeps the disk, stops the action (the machine is not held) and says why
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, ALL);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        fs.put("/usb0/g.dsk", dsk(40000));
        fs.put("/usb0/h.dsk", dsk(20000));
        OpResult r;
        char msg[300];
        const Task *ea = 0;
        for (int i = 0; i < cpc_desc.ntasks; i++) if (!strcmp(cpc_desc.tasks[i].label, "Eject A:")) ea = &cpc_desc.tasks[i];
        // a replacement while the FDC's marks are still in the core (the poll has not run): they are taken and written, then the new disk comes in
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/g.dsk", r), 0);
        io.fdc_write(0, 700, 0x31);
        CHECK(cpc_desc.media_pending(&c));
        CHECK_EQ(run(c, act(".dsk", 1), "/usb0/h.dsk", r), 0);
        CHECK_EQ(fs.files["/usb0/g.dsk"][700], 0x31);
        CHECK_STR(ops_state(&c, "cpc.a.image"), "h.dsk");
        CHECK(!cpc_desc.media_pending(&c));
        // a sync the medium refuses: the chunk reached the file, the marks are clear, and still the disk is not let go
        io.fdc_write(0, 800, 0x32);
        fs.sync_fails = true;
        CHECK(media_writeback(&c, WB_WHY_RESET, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "A:") != 0 && strstr(msg, "stays mounted") != 0);
        CHECK(media_writeback(&c, WB_WHY_RESET, msg, sizeof(msg)) != 0);   // asked again: still owed (the first answer cleared the marks)
        CHECK(cpc_desc.media_pending(&c));
        size_t ev = io.events.size();
        CHECK(boot_cold_reset(&c, msg, sizeof(msg)) != 0);
        CHECK(boot_warm_reset(&c, msg, sizeof(msg)) != 0);
        CHECK(boot_reload(&c, msg, sizeof(msg)) != 0);
        CHECK(boot_restart(&c, msg, sizeof(msg)) != 0);
        CHECK_EQ(io.events.size(), ev);                              // the machine was never held
        CHECK(ops_run_bound(&c, ea->op, ea->bind, ea->nbind, 0, &r) != 0);
        CHECK_STR(ops_state(&c, "cpc.a.image"), "h.dsk");
        CHECK(run(c, act(".dsk", 1), "/usb0/g.dsk", r) != 0);
        CHECK_STR(ops_state(&c, "cpc.a.image"), "h.dsk");
        fs.sync_fails = false;                                       // the medium is back: the same actions go through
        CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
        CHECK(!cpc_desc.media_pending(&c));
        CHECK_EQ(fs.files["/usb0/h.dsk"][800], 0x32);
        CHECK_EQ(ops_run_bound(&c, ea->op, ea->bind, ea->nbind, 0, &r), 0);
    }
    // --- no disk drive on a 464, none without AMSDOS
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, S464);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        fs.put("/usb0/g.dsk", dsk(40000));
        OpResult r;
        CHECK(run(c, act(".dsk", 1), "/usb0/g.dsk", r) != 0 && strstr(r.msg, "464 has no disk drive"));
    }
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, S6128 & ~64u);
        fs.put("/usb0/roms/cpc/amsdos.rom", Ui::rom(6));
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        c.state = 0;                                                 // a restart of the application without the file: the AMSDOS is unknown
        fs.files.erase("/usb0/roms/cpc/amsdos.rom");
        fs.put("/usb0/g.dsk", dsk(40000));
        OpResult r;
        io.regs[CPC_MODEL] = 2;
        CHECK(run(c, act(".dsk", 1), "/usb0/g.dsk", r) != 0 && strstr(r.msg, "amsdos.rom"));
    }
    // --- the tape (a CDT is a TZX): the same deck as the Spectrum's, at the CPC's registers
    {
        CpcIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, ALL);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.fs = &fs;
        boot_start(&c, &ui);
        std::vector<uint8_t> cdt = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20, 0x12, 100, 0, 5, 0, 0x20, 0xE8, 0x03, 0x12, 200, 0, 2, 0 };
        fs.put("/usb0/t.cdt", cdt);
        OpResult r;
        CHECK_STR(act(".cdt", 0).label, "Insert tape");
        CHECK_EQ(run(c, act(".cdt", 1), "/usb0/t.cdt", r), 0);        // insert and play
        CHECK_STR(ops_state(&c, "cpc.tape.image"), "t.cdt");
        auto word = [&](unsigned i) { return (unsigned)(io.ddr[CPC_W_TAPE + 2 * i] | io.ddr[CPC_W_TAPE + 2 * i + 1] << 8); };
        CHECK_EQ(word(0), 100);
        CHECK_EQ(word(4), 100);
        CHECK_EQ(word(5), 0xFFFF);                                    // 1000 ms of silence: a gap
        CHECK_EQ(word(6) | word(7) << 16, 1000u * 3500u);
        CHECK_EQ(word(8), 200);
        uint32_t base = io.regs[CPC_TAPE_BASE] | io.regs[CPC_TAPE_BASE + 1] << 8 | io.regs[CPC_TAPE_BASE + 2] << 16 | (uint32_t)io.regs[CPC_TAPE_BASE + 3] << 24;
        CHECK_EQ(base, 0x01800000u);
        CHECK_EQ(io.regs[CPC_TAPE_CTRL], 1);
        io.regs[CPC_TAPE_STATUS] = 2;                                 // end of the tape
        CHECK_EQ(ops_call(&c, "cpc.service", 0, 0, &r), 0);
        CHECK_EQ(io.regs[CPC_TAPE_CTRL], 0);
        CHECK_EQ(ops_call(&c, "cpc.tape_next", 0, 0, &r), 0);
        CHECK_EQ(ops_call(&c, "cpc.tape_rewind", 0, 0, &r), 0);
        CHECK_EQ(ops_call(&c, "cpc.tape_eject", 0, 0, &r), 0);
        CHECK(ops_state(&c, "cpc.tape.image") == 0);
    }
    DONE("test_cpc");
}
