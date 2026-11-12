// T89 §5 (2026-10-19): the Apple //e descriptor against a model of the register block - the three ROMs (the Disk II ROM only while a disk is
// mounted), the display settings, CAPS LOCK from the C64U's SHIFT LOCK, the resets and the mount that boots. The descriptor under test is
// the real one (coreapp/cores/apple2.cc).
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "apple2_regs.h"
#include "apple2_disk.h"
#include "ops_glue.h"

struct Ui : BootUi {
    std::vector<std::string> msgs;
    int ticks = 0, insert_at = -1;
    FakeIo *io = 0; FakeFs *fs = 0;
    void message(const char *, const char *m, bool) { msgs.push_back(m); }
    void clear() { }
    void tick() { if (++ticks == insert_at) fs->put("/usb0/roms/apple2/apple2e.rom", sysrom()); }
    static std::vector<uint8_t> sysrom(bool good = true)
    {
        std::vector<uint8_t> v(16384);
        for (int i = 0; i < 16384; i++) v[i] = (uint8_t)(i * 5 + 1);
        v[0x3FFC] = good ? 0x62 : 0x00; v[0x3FFD] = 0xFA;
        return v;
    }
};

// WIN_SEL 8 is the slot 7 card's ROM, which the generic fake (selects 0..3) does not have
struct AppleIo : FakeIo {
    std::vector<uint8_t> hrom;
    int hrom_writes = 0;
    AppleIo() { rom_sel_reg = A2_WIN_SEL; hrom.assign(256, 0); regs[A2_CAPS2] = A2_CAPS2_BITS; }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        if ((regs[A2_WIN_SEL] & 15) == A2_SEL_HDDROM) { memcpy(&hrom.at(off), p, n); hrom_writes++; return; }
        FakeIo::rom_write(off, p, n);
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        if ((regs[A2_WIN_SEL] & 15) == A2_SEL_HDDROM) { memcpy(p, &hrom.at(off), n); return; }
        FakeIo::rom_read(off, p, n);
    }
};

static void put_roms(FakeFs &fs, bool disk = true, bool good = true)
{
    fs.put("/usb0/roms/apple2/apple2e.rom", Ui::sysrom(good));
    std::vector<uint8_t> chr(8192), dsk(256);
    for (int i = 0; i < 8192; i++) chr[i] = (uint8_t)(i * 3 + 7);
    for (int i = 0; i < 256; i++) dsk[i] = (uint8_t)(i + 1);
    fs.put("/usb0/roms/apple2/apple2e-video.rom", chr);
    if (disk) fs.put("/usb0/roms/apple2/diskii.rom", dsk);
}

static void setup(CoreCtx &c, AppleIo &io, FakeFs &fs, PxRec &px, Settings *&st, Video *&v)
{
    memset(&c, 0, sizeof(c));
    c.desc = &apple2_desc; c.io = &io; c.fs = &fs;
    st = new Settings(apple2_desc.settings, apple2_desc.nsettings, apple2_desc.store, &fs, "/sd/cores/apple2.cfg", 0);
    st->load();
    v = new Video(apple2_desc.video, &px, &io);
    c.settings = st; c.video = v;
}

static bool all_zero(const std::vector<uint8_t> &v, int n) { for (int i = 0; i < n; i++) if (v[i]) return false; return true; }

int main()
{
    // --- start: held until the system ROM is there; a file with another reset vector is refused (S4), then the right one is taken
    {
        AppleIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, true, false);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs; ui.insert_at = 6;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK(ui.msgs.size() >= 1);
        CHECK_EQ(io.reset_releases, 1);
        CHECK_EQ(io.log.front().reg, A2_CTRL);
        CHECK_EQ(io.log.front().v, A2_CTRL_HOLD);
        CHECK_EQ(io.log.back().reg, A2_CTRL);
        CHECK_EQ(io.log.back().v, 0);
        CHECK_STR(io.events.back().c_str(), "mode_unreset");
        // the arrays: system 16 KB in 0, character ROM in 1
        auto good = Ui::sysrom();
        CHECK(memcmp(io.rom[0].data(), good.data(), 16384) == 0);
        CHECK_EQ(io.rom[1][100], (uint8_t)(100 * 3 + 7));
        // no disk is mounted: the slot-6 ROM is not in the core (zeros), so the monitor reaches BASIC (the boot bench's case)
        CHECK(all_zero(io.rom[2], 256));
        // defaults: colour, original palette, no scanlines, first set, normal ports; CAPS LOCK follows the (unlocked) SHIFT LOCK, its SHIFT off
        CHECK_EQ(io.regs[A2_CFG], 0);
        CHECK_EQ(io.regs[A2_CAPS], 0);
        CHECK_EQ(io.bling & 1, 1);
        uint32_t base = io.regs[A2_FB_BASE] | io.regs[A2_FB_BASE + 1] << 8 | io.regs[A2_FB_BASE + 2] << 16 | (uint32_t)io.regs[A2_FB_BASE + 3] << 24;
        CHECK_EQ(base, 0x01D00000u);
    }
    // --- monitor items share one register
    {
        AppleIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs);
        fs.put_text("/sd/cores/apple2.cfg", "[Apple IIe Settings]\nDisplay=Amber\nPalette=AppleWin\nScanlines=On\nCharacter ROM set=Second 4 KB\nJoystick ports=Swapped\n\n");
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
        CHECK_EQ(io.regs[A2_CFG], 3 | 2 << 2 | 1 << 4 | 1 << 5 | 1 << 6);
        st->set(st->find("Display"), 1); ops_set(&c, "apple2.set_cfg", 1);   // live: no reset needed
        CHECK_EQ(io.regs[A2_CFG] & 3, 1);
    }
    // --- CAPS LOCK follows the SHIFT LOCK when told to; the board's lock_off is set whatever the mode
    {
        AppleIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs);
        fs.put_text("/sd/cores/apple2.cfg", "[Apple IIe Settings]\nCAPS LOCK=Follow SHIFT LOCK\n\n");
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        boot_start(&c, &ui);
        CHECK_EQ(io.regs[A2_CAPS], 0);                              // the lock is up
        CHECK_EQ(io.bling & 1, 1);
        io.bling |= 2;                                              // SHIFT LOCK pressed
        OpResult r;
        CHECK_EQ(ops_call(&c, "apple2.service", 0, 0, &r), 0);
        CHECK_EQ(io.regs[A2_CAPS], 1);
        io.bling = 0;                                               // the keyboard code of the host wrote lock_off = 0
        ops_call(&c, "apple2.service", 0, 0, &r);
        CHECK_EQ(io.bling & 1, 1);
        CHECK_EQ(io.regs[A2_CAPS], 0);
        st->set(st->find("CAPS LOCK"), 0); ops_set(&c, "apple2.set_caps", 0);   // On, whatever the lock says
        CHECK_EQ(io.regs[A2_CAPS], 1);
        st->set(st->find("CAPS LOCK"), 1); ops_set(&c, "apple2.set_caps", 1);   // Off
        CHECK_EQ(io.regs[A2_CAPS], 0);
    }
    // --- resets: Ctrl-Reset keeps the RAM and does not use the host's reset; a mount of D1: boots with the card ROM in
    {
        AppleIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        boot_start(&c, &ui);
        char msg[300];
        size_t n0 = io.log.size(); int asserts = io.reset_asserts;
        CHECK_EQ(boot_warm_reset(&c, msg, sizeof(msg)), 0);
        CHECK_EQ(io.log[n0].reg, A2_CTRL);
        CHECK_EQ(io.log[n0].v, A2_CTRL_HOLD);
        CHECK_EQ(io.log.back().v, 0);
        CHECK_EQ(io.reset_asserts, asserts);
        // a disk image: 35 tracks of 16 sectors
        fs.put("/usb0/g.dsk", std::vector<uint8_t>(143360, 0));
        const FileAction &a1 = apple2_desc.filetypes[0].actions[0];
        OpResult r;
        asserts = io.reset_asserts;
        CHECK_EQ(ops_run_bound(&c, a1.op, a1.bind, a1.nbind, "/usb0/g.dsk", &r), 0);
        CHECK_EQ(io.reset_asserts, asserts + 1);                    // D1: ends in a cold start
        CHECK_EQ(io.rom[2][0], 1);                                  // the card ROM is in the core now
        CHECK_EQ(io.rom[2][255], 0);
        CHECK_STR(ops_state(&c, "apple2.d1.image"), "g.dsk");
        CHECK_STR(a1.label, "Mount D1: and cold start");
        // "Mount D1:" swaps the disk of a running machine: no reset (T108 D6, Alex 2026-10-20)
        const FileAction &a1s = apple2_desc.filetypes[0].actions[1];
        CHECK_STR(a1s.label, "Mount D1:");
        fs.put("/usb0/h.dsk", std::vector<uint8_t>(143360, 0));
        asserts = io.reset_asserts;
        size_t n1 = io.log.size();
        CHECK_EQ(ops_run_bound(&c, a1s.op, a1s.bind, a1s.nbind, "/usb0/h.dsk", &r), 0);
        CHECK_EQ(io.reset_asserts, asserts);
        for (size_t i = n1; i < io.log.size(); i++) CHECK(!(io.log[i].reg == A2_CTRL && (io.log[i].v & A2_CTRL_HOLD)));
        CHECK_STR(ops_state(&c, "apple2.d1.image"), "h.dsk");
        // D2: is a mount and nothing else
        const FileAction &a2 = apple2_desc.filetypes[0].actions[2];
        asserts = io.reset_asserts;
        CHECK_EQ(ops_run_bound(&c, a2.op, a2.bind, a2.nbind, "/usb0/g.dsk", &r), 0);
        CHECK_EQ(io.reset_asserts, asserts);
        CHECK_STR(ops_state(&c, "apple2.d2.image"), "g.dsk");
    }
    // --- the hard disk in slot 7 (T89.1): the file types, the card's ROM follows the units, the cold start, the ejects
    {
        AppleIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        boot_start(&c, &ui);
        OpResult r;
        const FileType *hdv = 0, *po = 0, *mg = 0, *woz = 0;
        for (int i = 0; i < apple2_desc.nfiletypes; i++) {
            const FileType &ft = apple2_desc.filetypes[i];
            if (!strcmp(ft.ext, ".hdv")) hdv = &ft;
            if (!strcmp(ft.ext, ".po")) po = &ft;
            if (!strcmp(ft.ext, ".2mg")) mg = &ft;
            if (!strcmp(ft.ext, ".woz")) woz = &ft;
        }
        CHECK(hdv && po && mg && woz);
        CHECK_STR(woz->actions[0].label, "Mount D1: and cold start");
        CHECK_STR(hdv->actions[0].label, "Mount S7 unit 1 and cold start");
        CHECK_STR(hdv->actions[2].label, "Mount S7 unit 2");
        // a .po is a floppy at 143,360 bytes (the floppy actions carry that size) and a volume at any size
        CHECK_EQ(po->nactions, 6);
        CHECK_EQ(po->actions[0].nsize_in, 1);
        CHECK_EQ(po->actions[0].size_in[0], 143360u);
        CHECK(po->actions[3].size_in == 0);
        CHECK_EQ(mg->actions[0].nsize_in, 2);
        std::vector<uint8_t> vol(512 * 300, 0x11);
        vol[0] = 0x42;
        fs.put("/usb0/p.hdv", vol);
        CHECK(all_zero(std::vector<uint8_t>(io.hrom), 256));
        int asserts = io.reset_asserts;
        CHECK_EQ(ops_run_bound(&c, hdv->actions[0].op, hdv->actions[0].bind, hdv->actions[0].nbind, "/usb0/p.hdv", &r), 0);
        CHECK_EQ(io.reset_asserts, asserts + 1);                    // unit 1 and a cold start: the Autostart ROM boots slot 7
        CHECK_EQ(io.hrom[0], 0xA9);                                 // the card ROM is in the core
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x01);
        CHECK_STR(ops_state(&c, "apple2.s71.image"), "p.hdv");
        asserts = io.reset_asserts;
        CHECK_EQ(ops_run_bound(&c, hdv->actions[2].op, hdv->actions[2].bind, hdv->actions[2].nbind, "/usb0/p.hdv", &r), 0);
        CHECK_EQ(io.reset_asserts, asserts);                        // unit 2 is a mount and nothing else
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x03);
        CHECK_STR(ops_state(&c, "apple2.s72.image"), "p.hdv");
        // the ejects are tasks of the menu
        int ejects = 0;
        for (int i = 0; i < apple2_desc.ntasks; i++) {
            const Task &t = apple2_desc.tasks[i];
            if (strstr(t.label, "Eject S7")) {
                ejects++;
                CHECK_EQ(ops_run_bound(&c, t.op, t.bind, t.nbind, 0, &r), 0);
            }
        }
        CHECK_EQ(ejects, 2);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x00);
        CHECK(all_zero(std::vector<uint8_t>(io.hrom), 256));        // no unit: no card signature, the Autostart ROM goes on to slot 6
        CHECK(ops_state(&c, "apple2.s71.image") == 0);
        // the slot 7 service: a block read through the descriptor's service operation
        CHECK_EQ(ops_run_bound(&c, hdv->actions[1].op, hdv->actions[1].bind, hdv->actions[1].nbind, "/usb0/p.hdv", &r), 0);
    }
    // --- a mount without the card ROM changes nothing
    {
        AppleIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
        put_roms(fs, false);
        setup(c, io, fs, px, st, v);
        Ui ui; ui.io = &io; ui.fs = &fs;
        boot_start(&c, &ui);
        fs.put("/usb0/g.dsk", std::vector<uint8_t>(143360, 0));
        const FileAction &a1 = apple2_desc.filetypes[0].actions[0];
        OpResult r; size_t ev = io.events.size();
        CHECK(ops_run_bound(&c, a1.op, a1.bind, a1.nbind, "/usb0/g.dsk", &r) != 0);
        CHECK(strstr(r.msg, "diskii.rom") != 0);
        CHECK_EQ(io.events.size(), ev);
        CHECK(ops_state(&c, "apple2.d1.image") == 0);
    }
    DONE("test_apple2");
}
