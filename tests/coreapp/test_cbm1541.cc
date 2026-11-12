// T104.1 D15 (cbm1541): the drive library behind the service layer, on the VIC-20 descriptor (a core with the capability drive_a) against a
// model of drive A (drive_model.h): the register sequences of a mount and an eject, the track memory the head reads, the write-back of what
// the head wrote, save and create, the drive ROM and the settings, Mount & Run through the machine's typed start, and the 1581 on a gateware
// that has it. The descriptor, the operations and the library are the real ones.
#include <algorithm>
#include <functional>
#include "cbm_rig.h"
#include "boardprintf.h"                 // the drive library's formats are checked against the board's printf as the test runs them (T122 §5)
#include "drive_model.h"
#include "d1541_drive.h"
#include "cbm1541.h"
#include "engines.h"
#include "cbm1541_desc.h"
#include "d1541_fs.h"
#include "disk_build.h"
#include "menuctl.h"
#include "keyboard.h"
#include "partial.h"
#include "vc20_regs.h"
#include "cor_build.h"

#define T20_RAM_EXT   0x0A1
#define T20_RAM_RO    0x0A2
#define T20_STATUS    0x0A4
#define T20_ST_HELD   0x02
#define T20_KBD_BUF   0x0277
#define T20_KBD_COUNT 0xC6

const CoreDesc *const core_table[] = { &vc20_desc, 0 };

struct DrvIo : CbmIo {
    DriveModel dm;
    Bytes basic, kernal, chr;
    std::string typed;                   // what the editor took from the keyboard buffer
    int overwrote = 0;                   // keys put into the buffer while it still held keys
    uint32_t last_take = 0;              // the editor looks into the buffer every 70 ms, not at once
    DrvIo() : CbmIo(T20_STATUS, T20_KBD_COUNT), basic(8192, 0), kernal(8192, 0), chr(4096, 0) { taken_in_reset = { T20_RAM_EXT, T20_RAM_RO }; }
    uint16_t boot_txttab() { return 0x1001; }
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
        for (uint32_t i = 0; i < n; i++) if (uint8_t *x = cell(off + i)) *x = ((const uint8_t *)p)[i];
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++) { uint8_t *x = cell(off + i); ((uint8_t *)p)[i] = x ? *x : 0; }
    }
    void ram_write(uint32_t a, const void *p, uint32_t n)
    {
        if (a == T20_KBD_BUF && ram[T20_KBD_COUNT] != 0) overwrote++;
        CbmIo::ram_write(a, p, n);
    }
    void advance(uint32_t u)
    {
        CbmIo::advance(u);
        if (ready && !stopped && ram[T20_KBD_COUNT] && t - last_take >= 70) {
            typed.append((const char *)&ram[T20_KBD_BUF], ram[T20_KBD_COUNT]);
            ram[T20_KBD_COUNT] = 0;
            last_take = t;
        }
    }
    void drv_wr(uint32_t off, uint8_t v)
    {
        dm.wr(off, v);
        if (off == 4) events.push_back(v ? "disk_in" : "disk_out");
    }
    bool drv_present() { return dm.present; }
    uint8_t drv_rd(uint32_t off) { return dm.rd(off); }
    void ddr_abs_write(uint32_t addr, const void *p, uint32_t n) { dm.abs_write(addr, p, n); }
    void ddr_abs_read(uint32_t addr, void *p, uint32_t n) { dm.abs_read(addr, p, n); }
    uint8_t *drv_buffer(uint32_t bytes, uint32_t *phys) { return dm.buffer(bytes, phys); }
};

static Bytes pattern(int role, size_t n)
{
    Bytes v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + role * 61 + (i >> 8));
    return v;
}

struct MHost : MenuHost {
    void info(int w, char *out, int n) { snprintf(out, n, "v%d", w); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no ICAP"); return false; }
};

struct Rig {
    DrvIo io;
    FakeFs fs;
    DiskFs dfs;                          // a disk image is a folder, as on the board (run.cc)
    MHost mhost;
    MenuCtl *mc = 0;
    PxRec px;
    CoreCtx c;
    Settings *st;
    Video *vid;
    CbmMenu menu;
    Rig(bool multi = false) : dfs(&fs)
    {
        io.dm.multi = multi;
        memset(&c, 0, sizeof(c));
        c.desc = &vc20_desc; c.io = &io; c.fs = &dfs;
        st = new Settings(vc20_desc.settings, vc20_desc.nsettings, vc20_desc.store, &fs, "/sd/cores/vc20.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(vc20_desc.video, &px, &io);
        c.video = vid;
        menu = { &c, &io, &vc20_desc, "vc20.service", "vc20.pending" };
    }
    ~Rig() { delete mc; delete vid; delete st; delete (CbmDrive *)c.drive; }
    MenuCtl *ctl() { if (!mc) mc = new MenuCtl(&c, &mhost); return mc; }
    void roms(bool drive_rom = true, size_t drive_size = 16384)
    {
        fs.put("/usb0/roms/vc20/basic.bin", pattern(1, 8192));
        fs.put("/usb0/roms/vc20/kernal.bin", pattern(2, 8192));
        fs.put("/usb0/roms/vc20/char.bin", pattern(3, 4096));
        if (drive_rom) fs.put("/usb0/roms/vc20/1541.rom", pattern(5, drive_size));
    }
    bool boot()
    {
        BoundedUi ui;
        BootResult r = boot_start(&c, &ui);
        io.delay_ms(io.boot_ms + io.editor_ms + 50);
        return r == BOOT_RUNNING && io.ready;
    }
    int op(const char *name, std::vector<OpIn> in, OpResult *r = 0)
    {
        OpResult tmp;
        return ops_call(&c, name, in.data(), (int)in.size(), r ? r : &tmp);
    }
    int mount(const char *path, const char *mode = "rw", OpResult *r = 0)
    {
        return op("drive.mount", { in_str("slot", "iec:drive_a"), in_str("path", path), in_str("mode", mode) }, r);
    }
    int eject(OpResult *r = 0) { return op("drive.eject", { in_str("slot", "iec:drive_a") }, r); }
    void poll() { OpResult x; ops_call(&c, "drive.poll", 0, 0, &x); }
    const char *image() { return ops_state(&c, "drive.drive_a.image"); }
    const char *mode() { return ops_state(&c, "drive.drive_a.mode"); }
    int pos(const char *what, int from = 0)
    {
        for (size_t i = (size_t)from; i < io.dm.log.size(); i++) if (io.dm.log[i] == what) return (int)i;
        return -1;
    }
    int lastpos(const char *what)
    {
        for (int i = (int)io.dm.log.size() - 1; i >= 0; i--) if (io.dm.log[(size_t)i] == what) return i;
        return -1;
    }
};

// A disk with a pattern on every sector outside the directory.
static Bytes make_d64(const char *name, int tracks = 35)
{
    BinImage b;
    b.format(name, tracks);
    for (int t = 1; t <= b.num_tracks; t++) {
        if (t == 18) continue;
        for (int s = 0; s < d64_sectors(t - 1); s++) {
            uint8_t *p = b.sector(t, s);
            for (int i = 0; i < 256; i++) p[i] = (uint8_t)(t * 31 + s * 7 + i * 3);
        }
    }
    Bytes v(b.bin_data, b.bin_data + b.file_bytes());
    return v;
}

static Bytes make_g64(const Bytes &d64)
{
    FakeFs fs;
    fs.put("/x.d64", d64);
    CoreFile *f = fs.open("/x.d64", false);
    BinImage b;
    b.load(f);
    fs.close(f);
    Bytes mem(GCR_MAXSIZE);
    GcrImage g(mem.data(), 0);
    g.from_bin(&b, 2700);
    Bytes out(GCR_MAXSIZE + 8192);
    uint32_t n = g.build_g64(out.data(), (uint32_t)out.size(), false);
    out.resize(n);
    return out;
}

// the track of a d64 as the hardware would find it in memory
static Bytes gcr_track_of(const Bytes &d64, int track_1b)
{
    FakeFs fs;
    fs.put("/x.d64", d64);
    CoreFile *f = fs.open("/x.d64", false);
    BinImage b;
    b.load(f);
    fs.close(f);
    Bytes mem(GCR_MAXSIZE);
    GcrImage g(mem.data(), 0);
    g.from_bin(&b, 2700);
    const GcrTrack &t = g.tracks[2 * (track_1b - 1)];
    return Bytes(t.track_address, t.track_address + t.track_length);
}

static bool track_decodes_to(const Bytes &gcr, int track_1b, const Bytes &d64)
{
    Bytes out(21 * 256);
    uint8_t st[64];
    int want = d64_sectors(track_1b - 1);
    Bytes g = gcr;
    int secs = GcrImage::decode_track(g.data(), track_1b, (int)g.size(), want, out.data(), st, 64);
    return secs == want && !memcmp(out.data(), d64.data() + d64_sectors_before(track_1b - 1) * 256, (size_t)want * 256);
}

static uint16_t crc_ccitt_bits(const uint8_t *p, int n)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

int main()
{
    OpResult res;
    const Bytes d64 = make_d64("PLAIN,01");
    const Bytes g64 = make_g64(d64);

    // ---- the registry: a core with the capability registers the library, the descriptor's entries all resolve ----
    {
        Rig r;
        const char *err = ops_error(&r.c);
        CHECK(err == 0 || !err[0]);
        for (const char *n : { "drive.mount", "drive.eject", "drive.save", "drive.create", "drive.reset", "drive.set_rom", "drive.mount_run",
                               "drive.put_rom", "drive.put_rom1581", "drive.set_power", "drive.set_bus_id", "drive.set_ram", "drive.set_swap_delay",
                               "drive.poll" }) {
            CHECK(ops_of(&r.c)->find_op(n) != 0);
            if (!ops_of(&r.c)->find_op(n)) printf("  missing %s\n", n);
        }
        CHECK(ops_of(&r.c)->find_state("drive.drive_a.image") != 0);
        CHECK(ops_of(&r.c)->find_state("drive.drive_a.mode") != 0);
        CHECK_STR(ops_of(&r.c)->find_op("drive.mount")->owner, "cbm1541");
        // the arguments are checked before the library sees them
        CHECK_EQ(r.op("drive.mount", { in_str("slot", "iec:drive_a"), in_str("path", "/a.d64"), in_str("mode", "write") }, &res), OPE_ARG);
        CHECK_EQ(r.op("drive.mount", { in_str("slot", "iec:drive_a"), in_str("path", "") , in_str("mode", "rw") }, &res), OPE_ARG);
        CHECK_EQ(r.io.dm.log.size(), 0u);                                 // nothing reached the drive
    }

    // ---- the drive ROM, the settings and the start ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        const Bytes want = pattern(5, 16384);
        std::vector<uint8_t> rom = r.io.dm.rom();
        CHECK(!memcmp(rom.data(), want.data(), 16384));
        CHECK(!memcmp(rom.data() + 16384, want.data(), 16384));            // a 16 KB ROM fills both halves
        CHECK_EQ(r.io.dm.reg[0], 1);                                       // power
        CHECK_EQ(r.io.dm.reg[2], 0);                                       // device 8
        CHECK_EQ(r.io.dm.ram_byte(0x77), 0x28);
        CHECK_EQ(r.io.dm.ram_byte(0x78), 0x48);
        CHECK_EQ(r.io.dm.reg[5], 0);
        CHECK_EQ(r.io.dm.reg[1], 6);                                       // the drive resets with the machine and stops with the menu
        CHECK(r.io.dm.abs_rejected == 0);
        CHECK(((CbmDrive *)r.c.drive)->attached());
        CHECK(!((CbmDrive *)r.c.drive)->multi_mode());
        // the track memory is a buffer the hardware can reach: every half track points somewhere
        CHECK(r.io.dm.host(r.io.dm.track_phys(0)) != 0);

        CHECK_EQ(r.op("drive.set_bus_id", { in_int("value", 2) }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[2], 2);
        CHECK_EQ(r.io.dm.ram_byte(0x77), 0x2A);
        CHECK_EQ(r.io.dm.ram_byte(0x78), 0x4A);
        CHECK_EQ(r.op("drive.set_ram", { in_int("value", 1) }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[5], 0x80);
        CHECK_EQ(r.op("drive.set_ram", { in_int("value", 0) }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[5], 0);
        CHECK_EQ(r.op("drive.set_bus_id", { in_int("value", 4) }, &res), OPE_ARG);   // 8 .. 11 only
        CHECK_EQ(r.op("drive.set_power", { in_int("value", 0) }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[0], 0);
        r.io.dm.log.clear();
        CHECK_EQ(r.op("drive.set_power", { in_int("value", 1) }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[0], 1);
        CHECK(r.pos("reset=1") >= 0 && r.lastpos("reset=6") > r.pos("reset=1"));    // a power-on is a reset
        // reset keeps the device number and cycles the power
        r.io.dm.log.clear();
        CHECK_EQ(r.op("drive.reset", { in_str("slot", "iec:drive_a") }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[2], 2);
        CHECK(r.pos("reset=1") >= 0);
        CHECK_EQ(r.op("drive.reset", { in_str("slot", "iec:drive_b") }, &res), OPE_FAILED);     // there is no drive B
        CHECK(strstr(res.msg, "iec:drive_a") != 0);

        // a ROM file picked in the browser: the drive takes it at once, 32 KB as they are
        r.fs.put("/usb0/dos.bin", pattern(7, 32768));
        CHECK_EQ(r.op("drive.set_rom", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/dos.bin") }, &res), OPE_OK);
        CHECK(r.io.dm.rom() == pattern(7, 32768));
        // a blob of the wrong size is refused with the reason and the ROM stays
        Bytes bad = pattern(8, 1000);
        CHECK_EQ(r.op("drive.put_rom", { in_blob("data", bad.data(), (uint32_t)bad.size()) }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "16 or 32 KB") != 0);
        CHECK(r.io.dm.rom() == pattern(7, 32768));
        r.fs.put("/usb0/odd.bin", pattern(9, 20000));
        CHECK_EQ(r.op("drive.set_rom", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/odd.bin") }, &res), OPE_FAILED);
        CHECK_EQ(r.op("drive.set_rom", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/none.bin") }, &res), OPE_FAILED);
        CHECK(r.io.dm.rom() == pattern(7, 32768));
    }

    // no drive ROM: the machine runs, the drive stays off and says why; a ROM that arrives later switches it on
    {
        Rig r;
        r.roms(false);
        CHECK(r.boot());
        CHECK_EQ(r.io.dm.reg[0], 0);
        CHECK_EQ(r.op("drive.set_power", { in_int("value", 1) }, &res), OPE_OK);
        CHECK(strstr(res.msg, "no 1541 ROM") != 0);
        CHECK_EQ(r.io.dm.reg[0], 0);
        Bytes rom = pattern(5, 32768);
        CHECK_EQ(r.op("drive.put_rom", { in_blob("data", rom.data(), (uint32_t)rom.size()) }), OPE_OK);
        CHECK_EQ(r.io.dm.reg[0], 1);
        CHECK(r.io.dm.rom() == rom);
        // a disk can be mounted while the drive is off, and Mount & Run refuses to start into a drive that is off
        Rig q;
        q.roms(false);
        CHECK(q.boot());
        q.fs.put("/usb0/a.d64", d64);
        CHECK_EQ(q.mount("/usb0/a.d64"), OPE_OK);
        CHECK_STR(q.image(), "a.d64");
        q.io.events.clear();
        CHECK_EQ(q.op("drive.mount_run", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d64") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "off") != 0);
        CHECK(std::find(q.io.events.begin(), q.io.events.end(), "mode_reset") == q.io.events.end());     // the machine was not reset for nothing
    }

    // a core without the drive window: every operation says so and nothing is written
    {
        Rig r;
        r.roms();
        r.io.dm.present = false;
        r.fs.put("/usb0/a.d64", d64);
        CHECK_EQ(r.mount("/usb0/a.d64", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "no built-in drive") != 0);
        CHECK_EQ(r.eject(&res), OPE_FAILED);
        CHECK_EQ(r.io.dm.log.size(), 0u);
        CHECK_EQ(r.io.dm.abs_writes, 0);
        CHECK(r.image() == 0);
        CHECK_STR(r.mode(), "empty");
    }

    // ---- mount a .d64 ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/disks/plain.d64", d64);
        CHECK(r.image() == 0);
        r.io.dm.log.clear();
        CHECK_EQ(r.mount("/usb0/disks/plain.d64"), OPE_OK);
        CHECK_STR(r.image(), "plain.d64");
        CHECK_STR(r.mode(), "rw");
        // the sequence: the drive is not stopped by the menu while the disk changes, the sensor is covered, the tracks are given, the sensor is
        // free again (a writable disk), the disk is in and the change is announced; then the stop with the menu is back
        int rs0 = r.pos("reset=0"), dark = r.pos("sensor=0"), light = r.lastpos("sensor=1"), in = r.pos("inserted=1"), chg = r.pos("diskchange=1");
        int rs6 = r.lastpos("reset=6");
        CHECK(rs0 >= 0 && dark > rs0 && light > dark && in > light && chg >= in);
        CHECK(rs6 > chg);
        CHECK_EQ(r.io.dm.reg[4], 1);
        CHECK_EQ(r.io.dm.reg[3], 1);
        CHECK_EQ(r.io.dm.reg[12], 1);
        CHECK(r.io.dm.reg[1] == 6);
        // every whole track points at memory that decodes to the sectors of the file; the half tracks at the empty track
        int good = 0, empty = 0;
        for (int t = 1; t <= 35; t++) {
            Bytes tr = r.io.dm.read_track(2 * (t - 1));
            good += track_decodes_to(tr, t, d64);
            CHECK_EQ(tr.size(), (size_t)(t <= 17 ? 7692 : t <= 24 ? 7142 : t <= 30 ? 6666 : 6250));
        }
        CHECK_EQ(good, 35);
        uint32_t dummy = r.io.dm.track_phys(1);
        for (int i = 1; i < 84; i += 2) empty += r.io.dm.track_phys(i) == dummy;
        CHECK_EQ(empty, 42);
        CHECK_EQ(r.io.dm.track_len(1), 7692u);
        CHECK(r.io.dm.read_track(1) == Bytes(7692, 0));
        // the bit time follows the track's length: one revolution (5,000,000 of the 100 MHz clock) over the bytes
        CHECK_EQ(r.io.dm.track_bit_time(0), 5000000u / 7692u);
        CHECK_EQ(r.io.dm.track_bit_time(2 * 34), 5000000u / 6250u);
        // a track the file does not have is a blank one that is there (tracks 36 .. 40) and points at its own memory
        CHECK(r.io.dm.track_phys(70) != dummy);

        // eject: the disk leaves, the tracks are the empty one again
        r.io.dm.log.clear();
        CHECK_EQ(r.eject(), OPE_OK);
        CHECK(r.image() == 0);
        CHECK_STR(r.mode(), "empty");
        CHECK_EQ(r.io.dm.reg[4], 0);
        CHECK(r.pos("inserted=0") < r.pos("sensor=0") && r.pos("sensor=0") < r.lastpos("sensor=1"));
        CHECK_EQ(r.io.dm.reg[12], 1);
        for (int i = 0; i < 84; i += 2) CHECK_EQ(r.io.dm.track_phys(i), dummy);
        CHECK_EQ(r.eject(), OPE_OK);                                       // nothing to eject is not an error

        // the swap delay is the time between "out" and "in": 100 ms by default, 500 ms when set so
        CHECK_EQ(r.op("drive.set_swap_delay", { in_int("value", 4) }), OPE_OK);
        CHECK_EQ(r.mount("/usb0/disks/plain.d64"), OPE_OK);
        uint32_t t0 = r.io.now_ms();
        CHECK_EQ(r.eject(), OPE_OK);
        CHECK(r.io.now_ms() - t0 >= 500);
        CHECK_EQ(r.op("drive.set_swap_delay", { in_int("value", 10) }, &res), OPE_ARG);

        // not a disk image, missing, a .d71, too small: the drive is left as it was and the reason is told
        r.io.dm.log.clear();
        r.fs.put("/usb0/x.txt", Bytes(100, 1));
        CHECK_EQ(r.mount("/usb0/x.txt", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "no disk image") != 0);
        CHECK_EQ(r.mount("/usb0/missing.d64", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "Cannot open") != 0);
        r.fs.put("/usb0/big.d64", Bytes(349696, 0));
        CHECK_EQ(r.mount("/usb0/big.d64", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "1571") != 0);
        r.fs.put("/usb0/small.d64", Bytes(5000, 0));
        CHECK_EQ(r.mount("/usb0/small.d64", "rw", &res), OPE_FAILED);
        CHECK(r.image() == 0);
        CHECK_EQ(r.io.dm.reg[4], 0);
        // a disk that was in when a new one fails to mount is out (the old tracks are gone from under the head)
        CHECK_EQ(r.mount("/usb0/disks/plain.d64"), OPE_OK);
        CHECK_EQ(r.mount("/usb0/small.d64", "rw", &res), OPE_FAILED);
        CHECK(r.image() == 0);
        CHECK_EQ(r.io.dm.reg[4], 0);
        CHECK_EQ(r.io.dm.reg[1], 6);                                       // and the stop with the menu is back

        // a 40 track disk and a disk with error bytes
        Bytes d40 = make_d64("FORTY", 40);
        r.fs.put("/usb0/forty.d64", d40);
        CHECK_EQ(r.mount("/usb0/forty.d64"), OPE_OK);
        CHECK(track_decodes_to(r.io.dm.read_track(2 * 39), 40, d40));

        // a .d81 on a 1541 gateware
        r.fs.put("/usb0/x.d81", Bytes(819200, 0));
        CHECK_EQ(r.mount("/usb0/x.d81", "ro", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "1541 only") != 0);
    }

    // read only, unlinked, and a medium that cannot be written
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/a.d64", d64);
        CHECK_EQ(r.mount("/usb0/a.d64", "ro"), OPE_OK);
        CHECK_STR(r.mode(), "ro");
        CHECK_EQ(r.io.dm.reg[3], 0);                                       // the write protect sensor stays covered
        CHECK_EQ(r.io.dm.reg[4], 1);
        CHECK_EQ(r.mount("/usb0/a.d64", "unlinked"), OPE_OK);
        CHECK_STR(r.mode(), "unlinked");
        CHECK_EQ(r.io.dm.reg[3], 1);
        r.fs.read_only = true;
        CHECK_EQ(r.mount("/usb0/a.d64", "rw"), OPE_OK);
        CHECK_STR(r.mode(), "ro");                                         // a read only medium: the disk is write protected
        CHECK_EQ(r.io.dm.reg[3], 0);
    }

    // ---- write-back of what the head wrote ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/w.d64", d64);
        CHECK_EQ(r.mount("/usb0/w.d64"), OPE_OK);
        // the DOS changes sector 4 of track 5: the new track is what a disk with that sector changed looks like
        Bytes d2 = d64;
        size_t at = (size_t)(d64_sectors_before(4) + 4) * 256;
        for (int i = 0; i < 256; i++) d2[at + (size_t)i] = (uint8_t)(0xA0 ^ i);
        Bytes t5 = gcr_track_of(d2, 5);
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        int w0 = r.fs.writes;
        r.poll();
        CHECK(r.fs.writes > w0);
        CHECK(r.fs.files["/usb0/w.d64"] == d2);                            // only that sector changed
        CHECK(!r.io.dm.dirty[4]);
        CHECK(!r.io.dm.any_dirty);
        CHECK(r.fs.syncs > 0);
        CHECK(!((CbmDrive *)r.c.drive)->unsaved());
        // nothing dirty: a poll does nothing
        w0 = r.fs.writes;
        r.poll();
        CHECK_EQ(r.fs.writes, w0);

        // the head is still writing this track: the flag stays, the track is written when the head is done
        Bytes d3 = d2;
        size_t at2 = (size_t)(d64_sectors_before(4) + 6) * 256;
        for (int i = 0; i < 256; i++) d3[at2 + (size_t)i] = (uint8_t)(0x33 + i);
        Bytes t5b = gcr_track_of(d3, 5);
        r.io.dm.head_write(8, 0, t5b.data(), (uint32_t)t5b.size());
        r.io.dm.reg[8] = 8;                                                // half track 8 = track 5
        r.io.dm.reg[9] = 0x04;                                             // writing
        r.poll();
        CHECK(r.fs.files["/usb0/w.d64"] == d2);
        CHECK(r.io.dm.dirty[4]);
        r.io.dm.reg[9] = 0;
        r.poll();
        CHECK(r.fs.files["/usb0/w.d64"] == d3);
        CHECK(!r.io.dm.dirty[4]);
        // writing busy on another track does not hold this one
        Bytes d4 = d3;
        size_t at3 = (size_t)(d64_sectors_before(9) + 1) * 256;
        for (int i = 0; i < 256; i++) d4[at3 + (size_t)i] = (uint8_t)(0x77 ^ i);
        Bytes t10 = gcr_track_of(d4, 10);
        r.io.dm.head_write(18, 0, t10.data(), (uint32_t)t10.size());
        r.io.dm.reg[8] = 40;
        r.io.dm.reg[9] = 0x04;
        r.poll();
        CHECK(r.fs.files["/usb0/w.d64"] == d4);
        r.io.dm.reg[9] = 0;

        // eject writes first: the head's last write is in the file before the disk leaves
        Bytes d5 = d4;
        size_t at4 = (size_t)(d64_sectors_before(2) + 0) * 256;
        for (int i = 0; i < 256; i++) d5[at4 + (size_t)i] = (uint8_t)(0x11 + 5 * i);
        Bytes t3 = gcr_track_of(d5, 3);
        r.io.dm.head_write(4, 0, t3.data(), (uint32_t)t3.size());
        CHECK_EQ(r.eject(&res), OPE_OK);
        CHECK(r.fs.files["/usb0/w.d64"] == d5);

        // read only: the hardware cannot write, and if it did the file stays as it is
        CHECK_EQ(r.mount("/usb0/w.d64", "ro"), OPE_OK);
        Bytes t7 = gcr_track_of(d2, 7);
        r.io.dm.head_write(12, 0, t7.data(), (uint32_t)t7.size());
        Bytes before = r.fs.files["/usb0/w.d64"];
        r.poll();
        CHECK(r.fs.files["/usb0/w.d64"] == before);
        CHECK(((CbmDrive *)r.c.drive)->unsaved());
    }

    // a write that cannot reach the file is told once, the change stays in the drive, and ejecting the disk asks twice
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/w.d64", d64);
        CHECK_EQ(r.mount("/usb0/w.d64"), OPE_OK);
        Bytes d2 = d64;
        size_t at = (size_t)(d64_sectors_before(4) + 4) * 256;
        d2[at] ^= 0xFF;
        Bytes t5 = gcr_track_of(d2, 5);
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        r.fs.read_only = true;                                             // the stick is write protected after the mount
        r.poll();
        CHECK(r.fs.files["/usb0/w.d64"] == d64);
        CHECK(((CbmDrive *)r.c.drive)->unsaved());
        CHECK(r.c.notice_pending);
        CHECK(strstr(r.c.notice, "failed") != 0);
        r.c.notice_pending = false;
        // the second failure is not told again
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        r.poll();
        CHECK(!r.c.notice_pending);
        // eject and mount refuse, once; the second call throws the change away
        CHECK_EQ(r.eject(&res), OPE_FAILED);
        CHECK(strstr(res.msg, "changes") != 0);
        CHECK_STR(r.image(), "w.d64");
        CHECK_EQ(r.eject(&res), OPE_OK);
        CHECK(r.image() == 0);
        // saving the disk makes it safe to leave
        r.fs.read_only = false;
        CHECK_EQ(r.mount("/usb0/w.d64"), OPE_OK);
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        r.fs.read_only = true;
        r.poll();
        CHECK(((CbmDrive *)r.c.drive)->unsaved());
        r.fs.read_only = false;
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/safe.d64"), in_str("format", "d64") }, &res), OPE_OK);
        CHECK(!((CbmDrive *)r.c.drive)->unsaved());
        CHECK(r.fs.files["/usb0/safe.d64"] == d2);
        CHECK_EQ(r.eject(&res), OPE_OK);
        // an unlinked disk is changed in the drive only; ejecting it asks as well
        CHECK_EQ(r.mount("/usb0/w.d64", "unlinked"), OPE_OK);
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        r.poll();
        CHECK(r.fs.files["/usb0/w.d64"] == d64);
        CHECK_EQ(r.eject(&res), OPE_FAILED);
        CHECK_EQ(r.mount("/usb0/w.d64", "rw", &res), OPE_OK);              // the second call (a mount) throws it away as well
    }

    // ---- T180 D4 / D1 (S7): what the head wrote is in the image's file, completely, before the disk is let go - at a reset, a reload, a restart (media_writeback
    // through the descriptor's media_flush), an eject and a replacement - also when the poll has not taken the write yet; a file that refuses it keeps the disk in the
    // drive and stops the action; the track is written when the medium is back; an unlinked disk has no file and is no reason to stop
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        char msg[300];
        OpResult res;
        Bytes d2 = d64;
        d2[(size_t)(d64_sectors_before(4) + 4) * 256] ^= 0xFF;
        Bytes t5 = gcr_track_of(d2, 5);
        CHECK(vc20_desc.media_flush != 0 && vc20_desc.media_pending != 0);
        // the head wrote, the poll did not run: the common path settles it
        r.fs.put("/usb0/w.d64", d64);
        CHECK_EQ(r.mount("/usb0/w.d64"), OPE_OK);
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        CHECK(r.fs.files["/usb0/w.d64"] == d64);
        CHECK(vc20_desc.media_pending(&r.c));
        CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)), 0);
        CHECK(r.fs.files["/usb0/w.d64"] == d2);
        CHECK(!vc20_desc.media_pending(&r.c));
        CHECK_STR(r.image(), "w.d64");
        // replace (D1): the old disk is in its file before the new one comes in
        r.fs.put("/usb0/x.d64", d64);
        Bytes d3 = d2;
        d3[(size_t)(d64_sectors_before(2) + 2) * 256] ^= 0x5A;
        Bytes t3 = gcr_track_of(d3, 3);
        r.io.dm.head_write(4, 0, t3.data(), (uint32_t)t3.size());
        CHECK_EQ(r.mount("/usb0/x.d64"), OPE_OK);
        CHECK(r.fs.files["/usb0/w.d64"] == d3);
        CHECK_STR(r.image(), "x.d64");
        CHECK_EQ(r.eject(&res), OPE_OK);
        // the medium refuses the track: the disk stays, every exit stops, nothing is held
        CHECK_EQ(r.mount("/usb0/w.d64"), OPE_OK);
        r.fs.read_only = true;
        Bytes t5b = gcr_track_of(d3, 5);
        d3[(size_t)(d64_sectors_before(4) + 4) * 256] ^= 0x33;
        t5b = gcr_track_of(d3, 5);
        r.io.dm.head_write(8, 0, t5b.data(), (uint32_t)t5b.size());
        CHECK(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "stays in the drive") != 0);
        CHECK(vc20_desc.media_pending(&r.c));
        size_t log = r.io.events.size();
        CHECK(boot_cold_reset(&r.c, msg, sizeof(msg)) != 0);
        CHECK(boot_warm_reset(&r.c, msg, sizeof(msg)) != 0);
        CHECK(boot_reload(&r.c, msg, sizeof(msg)) != 0);
        CHECK(boot_restart(&r.c, msg, sizeof(msg)) != 0);
        CHECK_EQ(r.io.events.size(), log);                                 // the machine was never held
        CHECK_STR(r.image(), "w.d64");
        CHECK_EQ(r.mount("/usb0/x.d64"), OPE_FAILED);                      // D1: the replacement asks first (the disk holds a change that is in no file)
        CHECK_STR(r.image(), "w.d64");
        r.fs.read_only = false;                                            // the medium is back: the track is written again from the drive's memory
        CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)), 0);
        CHECK(r.fs.files["/usb0/w.d64"] == d3);
        CHECK(!vc20_desc.media_pending(&r.c));
        CHECK(!((CbmDrive *)r.c.drive)->unsaved());
        CHECK_EQ(r.eject(&res), OPE_OK);
        // the same without the common path: the eject itself writes the track again when the medium is back, and does not ask
        CHECK_EQ(r.mount("/usb0/w.d64"), OPE_OK);
        r.fs.read_only = true;
        Bytes d4 = d3;
        d4[(size_t)(d64_sectors_before(1) + 1) * 256] ^= 0x44;
        Bytes t2 = gcr_track_of(d4, 2);
        r.io.dm.head_write(2, 0, t2.data(), (uint32_t)t2.size());
        r.poll();                                                          // the file refused it
        CHECK(((CbmDrive *)r.c.drive)->unsaved());
        r.fs.read_only = false;
        CHECK_EQ(r.eject(&res), OPE_OK);
        CHECK(r.fs.files["/usb0/w.d64"] == d4);
        d3 = d4;
        // an unlinked disk has no file: the changes are the person's choice, they do not stop a reset
        CHECK_EQ(r.mount("/usb0/w.d64", "unlinked"), OPE_OK);
        r.io.dm.head_write(8, 0, t5.data(), (uint32_t)t5.size());
        CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)), 0);
        CHECK(!vc20_desc.media_pending(&r.c));
        CHECK(r.fs.files["/usb0/w.d64"] == d3);
        CHECK_EQ(r.eject(&res), OPE_FAILED);                               // the eject still asks, as before
        CHECK_EQ(r.eject(&res), OPE_OK);
        // nothing in the drive, nothing pending
        CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)), 0);
    }

    // ---- a .g64 ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/g.g64", g64);
        CHECK_EQ(r.mount("/usb0/g.g64"), OPE_OK);
        CHECK_STR(r.image(), "g.g64");
        CHECK_STR(r.mode(), "rw");
        int good = 0;
        for (int t = 1; t <= 35; t++) good += track_decodes_to(r.io.dm.read_track(2 * (t - 1)), t, d64);
        CHECK_EQ(good, 35);
        // the head formats / writes track 3: the track goes back to its place in the file, the length field too
        Bytes d2 = d64;
        d2[(size_t)d64_sectors_before(2) * 256 + 5] ^= 0x5A;
        Bytes t3 = gcr_track_of(d2, 3);
        r.io.dm.head_write(4, 0, t3.data(), (uint32_t)t3.size());
        r.poll();
        auto le = [&](const Bytes &f, uint32_t o) { return (uint32_t)f[o] | ((uint32_t)f[o + 1] << 8) | ((uint32_t)f[o + 2] << 16) | ((uint32_t)f[o + 3] << 24); };
        const Bytes &f = r.fs.files["/usb0/g.g64"];
        uint32_t off = le(f, 12 + 4 * 4);
        CHECK(f.size() == g64.size());
        // the track in the file is the one the head wrote, turned to start at sector 0 (the "align" the firmware does)
        Bytes in_file(f.begin() + off + 2, f.begin() + off + 2 + t3.size());
        CHECK(track_decodes_to(in_file, 3, d2));
        CHECK(in_file[0] == 0xFF && in_file[5] == 0x52);
        CHECK(!memcmp(f.data(), g64.data(), 12 + 4 * 4));                  // the tables are untouched
        // the other tracks are as they were
        CHECK(Bytes(f.begin() + le(f, 12), f.begin() + le(f, 12) + 100) == Bytes(g64.begin() + le(g64, 12), g64.begin() + le(g64, 12) + 100));
        CHECK(!((CbmDrive *)r.c.drive)->unsaved());

        // a track the file does not have (track 36) has no place in it: the change stays in the drive and is told
        Bytes junk(7000, 0x55);
        r.io.dm.head_write(70, 0, junk.data(), (uint32_t)junk.size());
        r.poll();
        CHECK(((CbmDrive *)r.c.drive)->unsaved());
        CHECK(r.c.notice_pending);

        // save as a .d64: the decoded disk
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/out"), in_str("format", "d64") }, &res), OPE_OK);
        CHECK(r.fs.files["/usb0/out.d64"] == d2);
        CHECK(!((CbmDrive *)r.c.drive)->unsaved());
        // save as a .g64: loads again and decodes to the same disk
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/copy.g64"), in_str("format", "g64") }, &res), OPE_OK);
        {
            CoreFile *cf = r.fs.open("/usb0/copy.g64", false);
            Bytes m(GCR_MAXSIZE);
            GcrImage gi(m.data(), 0);
            char why[100];
            CHECK(gi.load(cf, why, sizeof(why)));
            r.fs.close(cf);
            BinImage bi;
            CHECK_EQ(gi.to_bin(&bi), 0);
            CHECK(!memcmp(bi.bin_data, d2.data(), d2.size()));
        }
        // no place given: next to the disk, with a name of its own, and the next one does not overwrite it
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", ""), in_str("format", "d64") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/g_saved.d64"));
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", ""), in_str("format", "d64") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/g_saved2.d64"));
        // a medium that cannot be written, and no disk
        r.fs.read_only = true;
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/no.d64"), in_str("format", "d64") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "Cannot write") != 0);
        r.fs.read_only = false;
        CHECK_EQ(r.eject(&res), OPE_OK);
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/no.d64"), in_str("format", "d64") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "no disk") != 0);
        CHECK(!r.fs.has("/usb0/no.d64"));
    }

    // a .g64 that is no disk: told, the drive empty
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        Bytes bad = g64;
        bad[0] = 'Z';
        r.fs.put("/usb0/bad.g64", bad);
        CHECK_EQ(r.mount("/usb0/bad.g64", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "signature") != 0);
        CHECK(r.image() == 0);
        Bytes ds = g64;
        memcpy(ds.data(), "GCR-1571", 8);
        r.fs.put("/usb0/ds.g64", ds);
        CHECK_EQ(r.mount("/usb0/ds.g64", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "single sided") != 0);
    }

    // ---- a new file is never written over an old one ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/games/x.d64", d64);
        CHECK_EQ(r.mount("/usb0/games/x.d64"), OPE_OK);
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/copy"), in_str("format", "d64") }, &res), OPE_OK);
        Bytes first = r.fs.files["/usb0/games/copy.d64"];
        r.fs.files["/usb0/games/copy.d64"][5] ^= 0xFF;                      // a file of its own now
        Bytes mine = r.fs.files["/usb0/games/copy.d64"];
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/copy.d64"), in_str("format", "d64") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "exists") != 0);
        CHECK(r.fs.files["/usb0/games/copy.d64"] == mine);
        // a bare name goes into the folder of the last disk, with its extension; again, it is refused; the other format is another file
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "My Disk"), in_str("format", "d64") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/games/My Disk.d64"));
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "My Disk"), in_str("format", "d64") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "exists") != 0);
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "My Disk.g64"), in_str("format", "g64") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/games/My Disk.g64"));
        CHECK_EQ(r.op("drive.create", { in_str("path", "Fresh"), in_str("format", "d64"), in_str("name", "") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/games/Fresh.d64"));
        CHECK(!memcmp(&r.fs.files["/usb0/games/fresh.d64"][357 * 256 + 144], "FRESH", 5));    // the volume name is the file name
        CHECK_EQ(r.op("drive.create", { in_str("path", "Fresh"), in_str("format", "d64"), in_str("name", "") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "exists") != 0);
        // the name suggested for the prompt is a free one, and follows what exists
        CHECK_STR(ops_state(&r.c, "drive.suggest.d64"), "blank");
        CHECK_STR(ops_state(&r.c, "drive.suggest.save_d64"), "x_saved");
        CHECK_EQ(r.op("drive.create", { in_str("path", "blank"), in_str("format", "d64"), in_str("name", "") }, &res), OPE_OK);
        CHECK_STR(ops_state(&r.c, "drive.suggest.d64"), "blank2");
        CHECK_STR(ops_state(&r.c, "drive.suggest.g64"), "blank");
    }

    // ---- create ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb0/new"), in_str("format", "d64"), in_str("name", "HELLO,42") }, &res), OPE_OK);
        const Bytes &nd = r.fs.files["/usb0/new.d64"];
        CHECK_EQ(nd.size(), (size_t)174848);
        CHECK(!memcmp(&nd[357 * 256 + 144], "HELLO", 5));
        CHECK(nd[357 * 256 + 162] == '4' && nd[357 * 256 + 163] == '2');
        // it mounts and the drive finds an empty directory track
        CHECK_EQ(r.mount("/usb0/new.d64"), OPE_OK);
        CHECK(track_decodes_to(r.io.dm.read_track(2 * 17), 18, nd));
        CHECK_EQ(r.eject(), OPE_OK);
        // an existing file is not overwritten
        r.fs.put("/usb0/there.d64", Bytes(3, 7));
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb0/there.d64"), in_str("format", "d64"), in_str("name", "") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "exists") != 0);
        CHECK_EQ(r.fs.files["/usb0/there.d64"].size(), 3u);
        // the volume name defaults to the file's name
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb0/mydisk.d64"), in_str("format", "d64"), in_str("name", "") }, &res), OPE_OK);
        CHECK(!memcmp(&r.fs.files["/usb0/mydisk.d64"][357 * 256 + 144], "MYDISK", 6));
        // a .g64: a disk with the same sectors
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb0/g"), in_str("format", "g64"), in_str("name", "G") }, &res), OPE_OK);
        {
            CoreFile *cf = r.fs.open("/usb0/g.g64", false);
            Bytes m(GCR_MAXSIZE);
            GcrImage gi(m.data(), 0);
            char why[100];
            CHECK(gi.load(cf, why, sizeof(why)));
            r.fs.close(cf);
            BinImage bi;
            CHECK_EQ(gi.to_bin(&bi), 0);
            CHECK(!memcmp(bi.bin_data, nd.data(), 357 * 256));             // the tracks before the directory are the same blank sectors
            CHECK_EQ(bi.bin_data[357 * 256], 18);
            CHECK(!memcmp(&bi.bin_data[357 * 256 + 144], "G", 1));
        }
        // a .d81 can be made on any core: a file
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb0/big"), in_str("format", "d81"), in_str("name", "BIG") }, &res), OPE_OK);
        CHECK_EQ(r.fs.files["/usb0/big.d81"].size(), 819200u);
        // T122: what a 1581 reads of it - header and BAM on track 40, an empty directory, 3160 blocks free
        {
            const Bytes &d = r.fs.files["/usb0/big.d81"];
            auto sec = [&](int t, int s) { return &d[(size_t)((t - 1) * 40 + s) * 256]; };
            const uint8_t *h = sec(40, 0);
            CHECK(h[0] == 40 && h[1] == 3 && h[2] == 'D');                       // directory starts at 40/3, DOS version D
            CHECK(!memcmp(h + 4, "BIG", 3));
            for (int i = 3; i < 16; i++) CHECK_EQ(h[4 + i], 0xA0);              // the name padded with shifted spaces
            CHECK(h[20] == 0xA0 && h[21] == 0xA0 && h[24] == 0xA0);
            CHECK(h[25] == '3' && h[26] == 'D');                                 // DOS type 3D
            const uint8_t *b1 = sec(40, 1), *b2 = sec(40, 2);
            CHECK(b1[0] == 40 && b1[1] == 2 && b1[2] == 'D' && b1[3] == 0xBB);   // BAM 1 links BAM 2
            CHECK(b2[0] == 0 && b2[1] == 0xFF && b2[2] == 'D' && b2[3] == 0xBB); // BAM 2 is the last
            CHECK(b1[4] == h[22] && b1[5] == h[23] && b2[4] == h[22] && b2[5] == h[23]);   // the disk ID in all three
            int total = 0;
            for (int t = 1; t <= 80; t++) {
                const uint8_t *e = (t <= 40 ? b1 : b2) + 0x10 + 6 * ((t - 1) % 40);
                int bits = 0;
                for (int k = 1; k <= 5; k++) for (int v = e[k]; v; v >>= 1) bits += v & 1;
                CHECK_EQ(e[0], bits);                                            // the counter is the number of free bits
                if (t == 40) {
                    CHECK_EQ(e[0], 36);                                          // header, both BAM sectors and the first directory sector are in use
                    CHECK_EQ(e[1], 0xF0);
                } else {
                    CHECK_EQ(e[0], 40);
                }
                if (t != 40) total += e[0];                                      // the 1581 does not count its directory track
            }
            CHECK_EQ(total, 3160);                                               // "3160 BLOCKS FREE": track 40 is the directory track
            const uint8_t *dir = sec(40, 3);
            CHECK(dir[0] == 0 && dir[1] == 0xFF);                                // the only directory sector, no next one
            for (int i = 2; i < 256; i++) CHECK_EQ(dir[i], 0);                   // no entry in use
        }
        // T122: the 1581 task of the descriptor pieces: the prompt, the free name for it, the file in the folder the menu is on
        {
            static const Task t81[] = { CBM1581_TASKS };
            CHECK_STR(t81[0].label, "Create blank D81");
            CHECK_STR(t81[0].ask, "Name of the new D81");
            CHECK_STR(ops_state(&r.c, t81[0].ask_state), "blank");
            r.c.media_dir[0] = 0;
            snprintf(r.c.media_dir, sizeof(r.c.media_dir), "%s", "/usb2/work");
            r.fs.put("/usb2/work/keep.txt", Bytes(1, 0));
            CHECK_EQ(ops_run_bound(&r.c, t81[0].op, t81[0].bind, t81[0].nbind, 0, &res, 0, "Data One"), OPE_OK);
            CHECK(r.fs.has("/usb2/work/Data One.d81"));                            // the menu's folder, not the folder of the last disk
            const Bytes d1 = r.fs.has("/usb2/work/Data One.d81") ? r.fs.files["/usb2/work/data one.d81"] : Bytes();   // [] on a missing key would insert one
            CHECK_EQ(d1.size(), 819200u);
            if (d1.size() == 819200u) {
                CHECK(!memcmp(&d1[39 * 40 * 256 + 4], "DATA ONE", 8));
            }
            CHECK_EQ(ops_run_bound(&r.c, t81[0].op, t81[0].bind, t81[0].nbind, 0, &res, 0, "Data One"), OPE_FAILED);
            CHECK(strstr(res.msg, "exists") != 0);
            CHECK_STR(ops_state(&r.c, t81[0].ask_state), "blank");
            r.fs.put("/usb2/work/blank.d81", Bytes(1, 0));
            CHECK_STR(ops_state(&r.c, t81[0].ask_state), "blank2");                // the free name is looked for among the .d81 files
            r.c.media_dir[0] = 0;
        }
        // no place given: the first medium that takes it, a free name each time; into a folder: the folder
        CHECK_EQ(r.op("drive.create", { in_str("path", ""), in_str("format", "d64"), in_str("name", "") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/blank.d64"));
        CHECK_EQ(r.op("drive.create", { in_str("path", ""), in_str("format", "d64"), in_str("name", "") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/blank2.d64"));
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb1/"), in_str("format", "g64"), in_str("name", "") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb1/blank.g64"));
        // after a mount the folder of the last disk is the place
        r.fs.put("/usb0/games/x.d64", d64);
        CHECK_EQ(r.mount("/usb0/games/x.d64"), OPE_OK);
        CHECK_EQ(r.op("drive.create", { in_str("path", ""), in_str("format", "d64"), in_str("name", "") }, &res), OPE_OK);
        CHECK(r.fs.has("/usb0/games/blank.d64"));
        // a medium that cannot be written
        r.fs.read_only = true;
        CHECK_EQ(r.op("drive.create", { in_str("path", "/usb0/ro.d64"), in_str("format", "d64"), in_str("name", "") }, &res), OPE_FAILED);
        CHECK(!r.fs.has("/usb0/ro.d64"));
        CHECK_EQ(r.op("drive.create", { in_str("path", ""), in_str("format", "d64"), in_str("name", "") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "No medium") != 0);
    }

    // ---- Run Disk (the whole disk: mount, reset, LOAD"*",8,1 and RUN) ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put("/usb0/games/run.d64", d64);
        r.io.events.clear();
        r.io.typed.clear();
        // the file action of the descriptor: ".d64" action 3
        const FileAction &a = vc20_desc.filetypes[2].actions[3];
        CHECK_STR(vc20_desc.filetypes[2].ext, ".d64");
        CHECK_STR(a.label, "Run Disk");
        CHECK_EQ(r.menu.action(".d64", 3, "/usb0/games/run.d64", &res), OPE_OK);
        CHECK(a.closes_menu);
        r.menu.closed();
        CHECK_STR(r.image(), "run.d64");
        CHECK(r.io.typed == "LOAD\"*\",8,1\rRUN\r");                       // typed in pieces the keyboard buffer holds
        CHECK_EQ(r.io.overwrote, 0);                                       // a piece goes in only when the machine took the one before
        CHECK(!r.io.typed_early);                                          // no key before the editor is up
        // the disk is in before the machine is let go
        auto idx = [&](const char *e) { for (size_t i = 0; i < r.io.events.size(); i++) if (r.io.events[i] == e) return (int)i; return -1; };
        CHECK(idx("disk_in") >= 0 && idx("disk_in") < idx("mode_unreset"));
        CHECK(idx("mode_reset") > idx("disk_in"));
        CHECK(r.io.dm.reg[4] == 1);
        // another device number is the number typed
        CHECK_EQ(r.op("drive.set_bus_id", { in_int("value", 1) }), OPE_OK);
        r.io.typed.clear();
        CHECK_EQ(r.menu.action(".d64", 3, "/usb0/games/run.d64", &res), OPE_OK);
        r.menu.closed();
        CHECK(r.io.typed == "LOAD\"*\",9,1\rRUN\r");
        CHECK_EQ(r.io.overwrote, 0);
        // a disk that does not mount does not reset the machine
        r.fs.put("/usb0/games/bad.d64", Bytes(500, 0));
        r.io.events.clear();
        r.io.typed.clear();
        CHECK_EQ(r.menu.action(".d64", 3, "/usb0/games/bad.d64", &res), OPE_FAILED);
        CHECK(std::find(r.io.events.begin(), r.io.events.end(), "mode_reset") == r.io.events.end());
        r.menu.closed();
        CHECK(r.io.typed.empty());
        // the three mount actions of the menu: read-write, read only, unlinked
        CHECK_EQ(r.menu.action(".d64", 1, "/usb0/games/run.d64", &res), OPE_OK);
        CHECK_STR(r.mode(), "ro");
        CHECK_EQ(r.menu.action(".d64", 2, "/usb0/games/run.d64", &res), OPE_OK);
        CHECK_STR(r.mode(), "unlinked");
        CHECK_EQ(r.menu.action(".d64", 0, "/usb0/games/run.d64", &res), OPE_OK);
        CHECK_STR(r.mode(), "rw");
        // and the .g64 ones
        r.fs.put("/usb0/games/run.g64", g64);
        CHECK_STR(vc20_desc.filetypes[3].ext, ".g64");
        CHECK_EQ(r.menu.action(".g64", 0, "/usb0/games/run.g64", &res), OPE_OK);
        CHECK_STR(r.image(), "run.g64");
        // a ROM file in the browser: the drive takes it
        r.fs.put("/usb0/games/dos.rom", pattern(11, 32768));
        CHECK_STR(vc20_desc.filetypes[4].ext, ".rom");
        CHECK_EQ(r.menu.action(".rom", 0, "/usb0/games/dos.rom", &res), OPE_OK);
        CHECK(r.io.dm.rom() == pattern(11, 32768));
        // the media slot's eject is the library's
        CHECK_STR(vc20_desc.media[1].eject_op, "drive.eject");
        CHECK_STR(vc20_desc.media[1].state, "drive.drive_a.image");
        CHECK_EQ(ops_run_bound(&r.c, vc20_desc.media[1].eject_op, vc20_desc.media[1].eject_bind, vc20_desc.media[1].neject_bind, 0, &res), OPE_OK);
        CHECK(r.image() == 0);
    }

    // ---- a disk image is a folder in the browser; Mount & Run and Real Run of a PRG inside it; the name prompt of Create and Save ----
    {
        Rig r;
        r.roms();
        CHECK(r.boot());
        Disk dk(false);
        dk.add("HELLO", 0x82, basic_prg(0x1001));
        dk.add("THREE SECTORS", 0x82, pat(600, 3));
        dk.add("SEQFILE", 0x81, pat(30, 1));
        const uint8_t q[] = { 'A', '"', 'B', '|', 'C' };
        dk.add(q, 5, 0x82, basic_prg(0x1001));
        r.fs.put("/usb0/games/t.d64", dk.img);
        MenuCtl *m = r.ctl();
        auto left = [&](int i) { char l[80], rt[80], sb[80]; bool act; m->row(i, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb)); return std::string(l); };
        auto pick = [&](const char *name) {
            for (int i = 0; i < m->rows(); i++) {
                if (left(i) == name) {
                    while (m->selected() > i) m->key(KEY_UP);
                    while (m->selected() < i) m->key(KEY_DOWN);
                    return true;
                }
            }
            printf("FAIL no row %s (in %s, the list: ", name, m->title());
            for (int i = 0; i < m->rows(); i++) printf("[%s]", left(i).c_str());
            printf(")\n");
            g_fails++;
            return false;
        };
        m->open();
        m->key(KEY_F3);                                                    // the Library opens on the media level (T104.2), the stick is a row
        CHECK_STR(m->title(), "/");
        CHECK(pick("usb0/"));
        m->key(KEY_RETURN);
        CHECK_STR(m->title(), "/usb0");
        CHECK(pick("games/"));
        m->key(KEY_RETURN);
        CHECK_STR(m->title(), "/usb0/games");
        CHECK(pick("t.d64"));
        m->key(KEY_RETURN);                                                // the chooser of the disk's actions
        CHECK(m->chooser());
        CHECK_EQ(m->rows(), 13);                                           // the disk's five actions, then the sheet of every file (View ... Delete)
        CHECK_STR(left(5).c_str(), "View");
        CHECK_STR(left(12).c_str(), "Delete");
        CHECK_STR(left(3).c_str(), "Run Disk");
        CHECK_STR(left(4).c_str(), "Browse Disk");
        CHECK(pick("Browse Disk"));
        m->key(KEY_RETURN);
        CHECK(!m->chooser());
        CHECK_STR(m->title(), "/usb0/games/t.d64");                        // the image is the folder
        CHECK_EQ(m->rows(), 4);                                            // "..", and the three PRG (the SEQ file is not offered)
        CHECK_STR(left(0).c_str(), "../");
        CHECK(left(1) == "HELLO.prg" && left(2) == "THREE SECTORS.prg" && left(3) == "A_B_C.prg");
        // the files inside offer the image's actions, not the stick's
        CHECK(pick("HELLO.prg"));
        m->key(KEY_RETURN);
        CHECK(m->chooser());
        CHECK_EQ(m->rows(), 2);
        CHECK_STR(left(0).c_str(), "Mount & Run");
        CHECK_STR(left(1).c_str(), "Real Run");
        m->key(KEY_ESCAPE);
        CHECK(!m->chooser());
        // back out of the image, and in again
        m->key(KEY_LEFT);
        CHECK_STR(m->title(), "/usb0/games");
        CHECK(pick("t.d64"));
        m->key(KEY_RETURN);
        CHECK(pick("Browse Disk"));
        m->key(KEY_RETURN);
        CHECK_STR(m->title(), "/usb0/games/t.d64");
        CHECK(pick("../"));
        m->key(KEY_RETURN);
        CHECK_STR(m->title(), "/usb0/games");
        // a disk that cannot be read is told and the browser stays where it was
        r.fs.put("/usb0/games/bad.d64", Bytes(500, 0));
        m->relist();
        CHECK(pick("bad.d64"));
        m->key(KEY_RETURN);
        CHECK(pick("Browse Disk"));
        m->key(KEY_RETURN);
        CHECK_STR(m->title(), "/usb0/games");
        CHECK(m->status_bad());

        // Mount & Run: the disk is mounted, the machine reset, the program read out of the image and run as any PRG is
        CHECK(pick("t.d64"));
        m->key(KEY_RETURN);
        CHECK(pick("Browse Disk"));
        m->key(KEY_RETURN);
        CHECK(pick("HELLO.prg"));
        m->key(KEY_RETURN);
        CHECK(pick("Mount & Run"));
        r.io.events.clear();
        r.io.typed.clear();
        r.io.stop(true);
        CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_CLOSE);
        r.menu.closed();
        r.io.delay_ms(200);                                                // the editor takes the keys
        CHECK_STR(r.image(), "t.d64");
        auto idx = [&](const char *e) { for (size_t i = 0; i < r.io.events.size(); i++) if (r.io.events[i] == e) return (int)i; return -1; };
        CHECK(idx("disk_in") >= 0 && idx("disk_in") < idx("mode_reset"));
        CHECK(r.io.typed == "RUN\r");                                      // the PRG library started it: BASIC program at $1001
        CHECK_EQ(r.io.ram[0x1001] | (r.io.ram[0x1002] << 8), 0x1007);     // relinked, from the bytes the image holds
        CHECK_STR(ops_state(&r.c, "vc20.program"), "HELLO.prg");

        // Real Run: the machine is reset and LOADs it by its name from the drive
        r.io.stop(false);
        CHECK_STR(m->title(), "/usb0/games/t.d64");                        // the menu stays where it was in the image
        CHECK(pick("THREE SECTORS.prg"));
        m->key(KEY_RETURN);
        CHECK(pick("Real Run"));
        r.io.events.clear();
        r.io.typed.clear();
        r.io.stop(true);
        CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_CLOSE);
        r.menu.closed();
        CHECK(r.io.typed == "LOAD\"THREE SECTORS\",8,1\rRUN\r");
        CHECK_EQ(r.io.overwrote, 0);
        // a name with a quote or the separator in it is typed with the wildcard in their place
        OpResult rr;
        CHECK_EQ(r.op("drive.mount_run_file", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/t.d64/A_B_C.prg"), in_str("how", "load") }, &rr), OPE_OK);
        r.io.typed.clear();
        r.io.stop(false);
        r.menu.closed();
        CHECK(r.io.typed == "LOAD\"A?B?C\",8,1\rRUN\r");
        // refusals: not a file of an image, not a PRG, no such file, the drive off
        CHECK_EQ(r.op("drive.mount_run_file", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/readme.txt"), in_str("how", "dma") }, &rr), OPE_FAILED);
        CHECK(strstr(rr.msg, "no file of a disk image") != 0);
        CHECK_EQ(r.op("drive.mount_run_file", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/t.d64/NOPE.prg"), in_str("how", "dma") }, &rr), OPE_FAILED);
        CHECK_EQ(r.op("drive.mount_run_file", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/t.d64/SEQFILE.seq"), in_str("how", "dma") }, &rr), OPE_FAILED);
        CHECK(strstr(rr.msg, "PRG") != 0);
        r.io.events.clear();
        CHECK_EQ(r.op("drive.set_power", { in_int("value", 0) }), OPE_OK);
        CHECK_EQ(r.op("drive.mount_run_file", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/games/t.d64/HELLO.prg"), in_str("how", "dma") }, &rr), OPE_FAILED);
        CHECK(strstr(rr.msg, "off") != 0);
        CHECK(std::find(r.io.events.begin(), r.io.events.end(), "mode_reset") == r.io.events.end());
        CHECK_EQ(r.op("drive.set_power", { in_int("value", 1) }), OPE_OK);
        // a Run PRG on a file inside the image (the stick's own action) works as well, because the image is a folder for every reader
        CHECK_EQ(r.op("vc20.load_prg", { in_str("path", "/usb0/games/t.d64/HELLO.prg") }, &rr), OPE_OK);

        // ---- the name prompt of Create and Save ----
        r.io.stop(false);
        m->key(KEY_F7);
        // Create opens the folder browser first (T122 §5); the browser of the library is inside t.d64, which is no folder, so it starts on the
        // first stick and the test walks to games/. The write runs in steps, one tick of the run loop each.
        auto create = [&](const char *label) {
            CHECK(pick(label));
            CHECK(!m->entering());
            m->key(KEY_RETURN);
            CHECK(m->picking());
            if (strcmp(m->title(), "/usb0/games") != 0) {
                CHECK_STR(m->title(), "/usb0");
                CHECK(pick("games/"));
                m->key(KEY_RETURN);
            }
            CHECK_STR(m->title(), "/usb0/games");
            m->key(KEY_RETURN);                                              // "Create here"
        };
        auto finish = [&]() { for (int i = 0; m->busy() && i < 5000; i++) m->tick(); };
        create("Create blank D64");
        CHECK(m->entering());
        CHECK_EQ(m->rows(), 1);
        {
            char l[80], rt[80], sb[100];
            bool act;
            m->row(0, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
            CHECK_STR(l, "Name of the new D64");
            CHECK_STR(rt, "blank_");                                         // the free name is the start, the cursor behind it
        }
        for (const char *p = "5"; *p; p++) m->key(*p);                       // "blank5"
        m->key('/');                                                         // not a character of a name
        m->key('*');
        for (int i = 0; i < 5; i++) m->key(KEY_BACK);                        // "b"
        for (const char *p = "ig disk"; *p; p++) m->key(*p);                 // "big disk"
        m->key(' ');
        CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(!m->entering());
        finish();
        CHECK(r.fs.has("/usb0/games/big disk.d64"));                          // the trailing space is gone
        CHECK(!m->status_bad());
        CHECK(strstr(m->status(), "Created") != 0);
        // the same name again is refused and nothing changes
        Bytes was = r.fs.has("/usb0/games/big disk.d64") ? r.fs.files["/usb0/games/big disk.d64"] : Bytes();
        create("Create blank D64");
        for (int i = 0; i < 12; i++) m->key(KEY_BACK);
        for (const char *p = "big disk"; *p; p++) m->key(*p);
        m->key(KEY_RETURN);
        finish();
        CHECK(m->status_bad());
        CHECK(strstr(m->status(), "exists") != 0);
        CHECK(!r.fs.has("/usb0/games/big disk.d64") || r.fs.files["/usb0/games/big disk.d64"] == was);
        // ESC leaves it untouched; an empty name is a free one
        create("Create blank G64");
        for (const char *p = "zzz"; *p; p++) m->key(*p);
        m->key(KEY_ESCAPE);
        CHECK(!m->entering());
        CHECK(!r.fs.has("/usb0/games/blankzzz.g64"));
        create("Create blank G64");
        for (int i = 0; i < 12; i++) m->key(KEY_BACK);
        m->key(KEY_RETURN);
        finish();
        CHECK(r.fs.has("/usb0/games/blank.g64"));
        // Save: the start is the disk's own name with _saved
        CHECK(pick("Save Disk as D64"));
        m->key(KEY_RETURN);
        {
            char l[80], rt[80], sb[100];
            bool act;
            m->row(0, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
            CHECK_STR(l, "Save as D64, name");
            CHECK_STR(rt, "t_saved_");
        }
        m->key(KEY_RETURN);
        CHECK(r.fs.has("/usb0/games/t_saved.d64"));
        // (the browser is inside t.d64 for all of the above: no folder there, so the folder of the last disk took every new image)
        // a function key leaves the prompt
        CHECK(pick("Save Disk as G64"));
        m->key(KEY_RETURN);
        CHECK(m->entering());
        m->key(KEY_F5);
        CHECK(!m->entering());
        // the length of a name is limited
        m->key(KEY_F7);
        create("Create blank D64");
        for (int i = 0; i < 12; i++) m->key(KEY_BACK);
        for (int i = 0; i < 40; i++) m->key('a');
        m->key(KEY_RETURN);
        finish();
        CHECK(r.fs.has("/usb0/games/aaaaaaaaaaaaaaaaaaaaaaaa.d64"));        // 24 characters
    }

    // ---- the 1581, on a gateware that can be one ----
    {
        Rig r(true);
        r.roms();
        CHECK(r.boot());
        Bytes rom81 = pattern(6, 32768);
        CHECK_EQ(r.op("drive.put_rom1581", { in_blob("data", rom81.data(), (uint32_t)rom81.size()) }), OPE_OK);
        CbmDrive *d = (CbmDrive *)r.c.drive;
        CHECK(d->multi_mode());
        CHECK_EQ(d->type(), (int)DT_1541);
        // the d81 is 80 tracks of two sides of ten 512 byte sectors; every sector holds its own address
        Bytes d81(819200);
        for (int t = 0; t < 80; t++) for (int s = 0; s < 2; s++) for (int k = 0; k < 10; k++) {
            size_t o = ((size_t)t * 2 + (size_t)s) * 5120 + (size_t)k * 512;
            for (int i = 0; i < 512; i++) d81[o + (size_t)i] = (uint8_t)(t * 3 + s * 50 + k * 5 + i);
        }
        r.fs.put("/usb0/a.d81", d81);
        r.io.dm.log.clear();
        CHECK_EQ(r.mount("/usb0/a.d81"), OPE_OK);
        CHECK_EQ(d->type(), (int)DT_1581);
        CHECK_STR(r.image(), "a.d81");
        CHECK_STR(r.mode(), "rw");
        CHECK_EQ(r.io.dm.type_latched, 2);                                 // the drive type register says 1581
        CHECK(r.io.dm.rom() == pattern(6, 32768));
        CHECK_EQ(r.io.dm.reg[4], 1);
        CHECK_EQ(r.io.dm.reg[3], 1);
        CHECK_EQ(r.io.dm.reg[1], 6);
        // a read of sector 3 on track 5, side 1: the controller is told the position, the library answers with a DMA
        auto cmd_read = [&](int track, int side, int sector) {
            r.io.dm.reg[8] = (uint8_t)track; r.io.dm.reg[6] = (uint8_t)side;
            r.io.dm.wd[1] = (uint8_t)track; r.io.dm.wd[2] = (uint8_t)sector;
            r.io.dm.wd_command(0x80);
            r.poll();
        };
        cmd_read(5, 1, 3);
        CHECK_EQ(r.io.dm.wd[7], 1);                                        // DMA to the 1581
        CHECK_EQ(r.io.dm.wd_dma_len(), 512u);
        uint8_t *src = r.io.dm.host(r.io.dm.wd_dma_addr());
        CHECK(src != 0);
        size_t off = ((size_t)5 * 2 + 1) * 5120 + 2 * 512;
        CHECK(src && !memcmp(src, &d81[off], 512));
        CHECK(r.io.dm.fifo.empty());                                       // the command was taken
        // a sector that is not on the track: record not found, busy cleared
        r.io.dm.wd_status = WD_STATUS_BUSY;
        cmd_read(5, 0, 11);
        CHECK(r.io.dm.wd_status & WD_STATUS_RNF);
        CHECK(!(r.io.dm.wd_status & WD_STATUS_BUSY));
        // a write: the library prepares the DMA, the data arrives, the completion puts it in the file
        r.io.dm.reg[8] = 7; r.io.dm.reg[6] = 0; r.io.dm.wd[1] = 7; r.io.dm.wd[2] = 4;
        r.io.dm.wd_command(0xA0);
        r.poll();
        CHECK_EQ(r.io.dm.wd[7], 2);
        uint8_t *dst = r.io.dm.host(r.io.dm.wd_dma_addr());
        CHECK(dst != 0);
        if (dst) for (int i = 0; i < 512; i++) dst[i] = (uint8_t)(0xC3 ^ i);
        r.io.dm.wd_command(0xA0, true);
        r.poll();
        CHECK_EQ(r.io.dm.wd[7], 0);
        Bytes want = d81;
        for (int i = 0; i < 512; i++) want[(size_t)7 * 2 * 5120 + 3 * 512 + (size_t)i] = (uint8_t)(0xC3 ^ i);
        CHECK(r.fs.files["/usb0/a.d81"] == want);
        CHECK(r.fs.syncs > 0);
        {   // T180 D4: a sector the file did not take (the sync is refused) cannot be written again: every exit stops, the disk stays, and only the eject asks twice
            char wmsg[300];
            CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, wmsg, sizeof(wmsg)), 0);
            r.fs.sync_fails = true;
            r.io.dm.reg[8] = 9; r.io.dm.reg[6] = 0; r.io.dm.wd[1] = 9; r.io.dm.wd[2] = 2;
            r.io.dm.wd_command(0xA0);
            r.poll();
            uint8_t *dst2 = r.io.dm.host(r.io.dm.wd_dma_addr());
            if (dst2) for (int i = 0; i < 512; i++) dst2[i] = (uint8_t)(0x3C ^ i);
            r.io.dm.wd_command(0xA0, true);
            r.poll();
            CHECK(((CbmDrive *)r.c.drive)->unsaved());
            CHECK(media_writeback(&r.c, WB_WHY_RESET, wmsg, sizeof(wmsg)) != 0);
            CHECK(strstr(wmsg, "1581") != 0 && strstr(wmsg, "stays in the drive") != 0);
            CHECK(boot_cold_reset(&r.c, wmsg, sizeof(wmsg)) != 0);
            CHECK_STR(r.image(), "a.d81");
            r.fs.sync_fails = false;
            CHECK_EQ(r.eject(&res), OPE_FAILED);                           // the disk holds a change that is in no file: asked once
            CHECK_EQ(r.eject(&res), OPE_OK);                               // and thrown away on the second call
            CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, wmsg, sizeof(wmsg)), 0);
            CHECK_EQ(r.mount("/usb0/a.d81"), OPE_OK);                      // the disk as it is in the file: the next tests go on from there
        }
        // seek, restore, step
        r.io.dm.reg[8] = 12;
        r.io.dm.wd[3] = 20;
        r.io.dm.wd_command(0x10);
        r.poll();
        CHECK_EQ(r.io.dm.wd[1], 20);
        CHECK_EQ(r.io.dm.wd[14], 20);
        r.io.dm.wd_command(0x00);
        r.poll();
        CHECK_EQ(r.io.dm.wd[1], 0);
        CHECK_EQ(r.io.dm.wd[14], 0);
        // read address: the next sector header and its CRC (the controller's own: A1 A1 A1 FE and the four bytes)
        r.io.dm.reg[8] = 9; r.io.dm.reg[6] = 1;
        r.io.dm.wd_command(0xC0);
        r.poll();
        CHECK_EQ(r.io.dm.wd_dma_len(), 6u);
        uint8_t *ad = r.io.dm.host(r.io.dm.wd_dma_addr());
        CHECK(ad != 0);
        if (ad) {
            uint8_t raw[8] = { 0xA1, 0xA1, 0xA1, 0xFE, ad[0], ad[1], ad[2], ad[3] };
            CHECK_EQ(ad[0], 9);
            CHECK_EQ(ad[1], 1);
            CHECK_EQ(ad[3], 2);
            CHECK_EQ((ad[4] << 8) | ad[5], crc_ccitt_bits(raw, 8));
        }
        // a track format (the DOS's NEW): sectors in the order 1 3 5 7 9 2 4 6 8 10, each filled with its own number
        {
            uint8_t raw[WD_BUFFER_BYTES];
            memset(raw, 0x4E, sizeof(raw));
            int order[10] = { 1, 3, 5, 7, 9, 2, 4, 6, 8, 10 };
            int p = 0;
            for (int k = 0; k < 10; k++) {
                raw[p] = 0xFE; raw[p + 1] = 3; raw[p + 2] = 0; raw[p + 3] = (uint8_t)order[k]; raw[p + 4] = 2; raw[p + 5] = 0xF7;
                p += 16;
                raw[p] = 0xFB;
                memset(&raw[p + 1], order[k], 512);
                raw[p + 1 + 512] = 0xF7;
                p += 512 + 16;
            }
            r.io.dm.reg[8] = 3; r.io.dm.reg[6] = 0;
            r.io.dm.wd[1] = 3;
            r.io.dm.wd_command(0xF0);
            r.poll();
            CHECK_EQ(r.io.dm.wd[7], 2);
            CHECK_EQ(r.io.dm.wd_dma_len(), (uint32_t)WD_RAW_TRACK_BYTES);
            uint8_t *tb = r.io.dm.host(r.io.dm.wd_dma_addr());
            CHECK(tb != 0);
            if (tb) memcpy(tb, raw, sizeof(raw));
            r.io.dm.wd_command(0xF0, true);
            r.poll();
            CHECK_EQ(r.io.dm.wd[7], 0);
            const Bytes &f = r.fs.files["/usb0/a.d81"];
            size_t base = (size_t)3 * 2 * 5120;
            bool laid = true;
            for (int k = 0; k < 10; k++) for (int i = 0; i < 512; i++) laid = laid && f[base + (size_t)k * 512 + (size_t)i] == (uint8_t)order[k];
            CHECK(laid);                                                   // the data of the track in the order it was written
            // and sector 5 is now found where the second place of the order holds the data of 3 ... : sector 3 is second
            cmd_read(3, 0, 3);
            uint8_t *s3 = r.io.dm.host(r.io.dm.wd_dma_addr());
            CHECK(s3 && s3[0] == 3 && s3[511] == 3);
            CHECK(!memcmp(&f[base + 512], s3, 512));
        }
        // the 1541 again: the type register, the ROM and the d64's tracks
        r.fs.put("/usb0/p.d64", d64);
        CHECK_EQ(r.mount("/usb0/p.d64"), OPE_OK);
        CHECK_EQ(d->type(), (int)DT_1541);
        CHECK_EQ(r.io.dm.type_latched, 0);
        {
            std::vector<uint8_t> rm = r.io.dm.rom();
            CHECK(!memcmp(rm.data(), pattern(5, 16384).data(), 16384) && !memcmp(rm.data() + 16384, pattern(5, 16384).data(), 16384));
        }
        CHECK(track_decodes_to(r.io.dm.read_track(0), 1, d64));
        // read only: the sector write cannot reach the file, which stays as it is, and the library says so
        CHECK_EQ(r.mount("/usb0/a.d81", "ro"), OPE_OK);
        CHECK_EQ(r.io.dm.reg[3], 0);
        Bytes snap = r.fs.files["/usb0/a.d81"];
        r.io.dm.reg[8] = 2; r.io.dm.reg[6] = 0; r.io.dm.wd[1] = 2; r.io.dm.wd[2] = 1;
        r.io.dm.wd_command(0xA0);
        r.poll();
        uint8_t *dd = r.io.dm.host(r.io.dm.wd_dma_addr());
        if (dd) memset(dd, 0xEE, 512);
        r.io.dm.wd_command(0xA0, true);
        r.poll();
        CHECK(r.fs.files["/usb0/a.d81"] == snap);
        CHECK(r.c.notice_pending);
        CHECK(strstr(r.c.notice, "1581") != 0);
        // the failed write left the disk marked as changed: leaving it asks twice
        CHECK_EQ(r.eject(&res), OPE_FAILED);
        CHECK_EQ(r.eject(&res), OPE_OK);
        // not a .d81, unlinked, saving a d81
        r.fs.put("/usb0/odd.d81", Bytes(12345, 0));
        CHECK_EQ(r.mount("/usb0/odd.d81", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "not a .d81") != 0);
        CHECK_EQ(r.mount("/usb0/a.d81", "unlinked", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "unlinked") != 0);
        CHECK_EQ(r.mount("/usb0/a.d81", "rw"), OPE_OK);
        CHECK_EQ(r.op("drive.save", { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/s"), in_str("format", "d64") }, &res), OPE_FAILED);
        CHECK(strstr(res.msg, ".d81") != 0);
        // the 1581 ROM missing: the mount says which file to put where
        Rig q(true);
        q.roms();
        CHECK(q.boot());
        q.fs.put("/usb0/a.d81", d81);
        CHECK_EQ(q.mount("/usb0/a.d81", "rw", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "1581") != 0);
        CHECK(q.image() == 0);
    }

    // the drive ROM is a shared device ROM (Alex, 2026-10-23: "It already lies elsewhere :-) from the C64"): found in <root>/roms when the
    // core's folder has none, the core's folder wins, and a machine ROM never comes from there
    {
        Rig r;                                                             // only the shared folder of a stick has the drive ROM
        r.roms(false);
        r.fs.put("/usb1/roms/1541-ii.bin", pattern(8, 32768));
        CHECK(r.boot());
        CHECK(r.io.dm.rom() == pattern(8, 32768));
        CHECK_EQ(r.io.dm.reg[0], 1);
        Rig p;                                                             // the part number name of the stick
        p.roms(false);
        p.fs.put("/usb1/roms/dos1541-325302-01+901229-05.bin", pattern(9, 16384));
        CHECK(p.boot());
        CHECK_EQ(p.io.dm.reg[0], 1);
        Rig w;                                                             // both: the core's folder wins, even with a later name
        w.roms(false);
        w.fs.put("/usb0/roms/vc20/dos1541.rom", pattern(5, 16384));
        w.fs.put("/usb0/roms/1541.bin", pattern(8, 16384));
        CHECK(w.boot());
        const Bytes core_copy = pattern(5, 16384);
        std::vector<uint8_t> got = w.io.dm.rom();
        CHECK(!memcmp(got.data(), core_copy.data(), 16384));
        // the machine ROMs: a C64 KERNAL (its name and the generic one) in <root>/roms is not the VIC-20's
        FakeFs f;
        f.put("/usb1/roms/vc20/basic.bin", pattern(1, 8192));
        f.put("/usb1/roms/vc20/char.bin", pattern(3, 4096));
        f.put("/usb1/roms/kernal-901227-03.bin", pattern(2, 8192));
        f.put("/usb1/roms/kernal.bin", pattern(2, 8192));
        RomResult rr[8];
        char summary[300];
        CHECK_EQ(rom_scan(&f, vc20_desc.folder, vc20_desc.roms, vc20_desc.nroms, rr, summary, sizeof(summary), 0), ROM_MISSING);
        CHECK(strstr(summary, "KERNAL") != 0);
        RomResult dr[8];
        f.put("/usb1/roms/vc20/kernal.bin", pattern(2, 8192));
        CHECK_EQ(rom_scan(&f, vc20_desc.folder, vc20_desc.roms, vc20_desc.nroms, dr, summary, sizeof(summary), 0), ROM_FOUND);
        CHECK_EQ(dr[3].status, ROM_ABSENT);
        f.put("/usb1/roms/1541.bin", pattern(5, 16384));
        rom_scan(&f, vc20_desc.folder, vc20_desc.roms, vc20_desc.nroms, dr, summary, sizeof(summary), 0);
        CHECK_EQ(dr[3].status, ROM_FOUND);
        CHECK_STR(dr[3].hit.dir, "/usb1/roms");
    }

    // F8 Restart core: the start path again - a drive ROM that appeared after the start is put, the media stay
    {
        Rig r;
        r.roms(false);
        CHECK(r.boot());
        CHECK_EQ(r.io.dm.reg[0], 0);                                       // no drive ROM at the start: the drive is off
        r.fs.put("/usb0/a.d64", d64);
        CHECK_EQ(r.mount("/usb0/a.d64"), OPE_OK);
        CHECK_STR(r.image(), "a.d64");
        r.fs.put("/usb1/roms/1541.bin", pattern(5, 16384));                // the stick is put in, the ROM is in the shared folder
        r.io.ram[0x0400] = 0x55;                                           // the machine has been used: a cold boot leaves nothing of it
        r.io.ram[0xFFFF] = 0x66;
        r.io.events.clear();
        MenuCtl *m = r.ctl();
        m->key(KEY_F8);
        for (int i = 0; i < m->rows(); i++) {
            char l[80], rt[80]; bool act;
            m->row(i, l, sizeof(l), rt, sizeof(rt), &act);
            if (!strcmp(l, "Restart core")) {
                while (m->selected() < i) m->key(KEY_DOWN);
                break;
            }
        }
        CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_CLOSE);
        CHECK(!m->status_bad());
        std::vector<uint8_t> got = r.io.dm.rom();
        CHECK(!memcmp(got.data(), pattern(5, 16384).data(), 16384));
        CHECK_EQ(r.io.dm.reg[0], 1);                                       // the drive is on with its ROM
        CHECK_STR(r.image(), "a.d64");                                     // and the disk in it was not touched
        CHECK_EQ(r.io.ram[0x0400], 0);                                     // the RAM cleared through the window, all 64 KB
        CHECK_EQ(r.io.ram[0xFFFF], 0);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
    }

    // the commands of the 1581 are answered whenever the poll runs, many at once
    {
        Rig r(true);
        r.roms();
        CHECK(r.boot());
        Bytes rom81 = pattern(6, 32768);
        CHECK_EQ(r.op("drive.put_rom1581", { in_blob("data", rom81.data(), (uint32_t)rom81.size()) }), OPE_OK);
        Bytes d81(819200, 0);
        r.fs.put("/usb0/a.d81", d81);
        CHECK_EQ(r.mount("/usb0/a.d81"), OPE_OK);
        for (int i = 0; i < 5; i++) r.io.dm.wd_command(0x00);
        r.poll();
        CHECK(r.io.dm.fifo.empty());
    }


    // ---- T104 5.14: F8 "Reload core" on a DFX core: the partial bitstream loaded again by the application ----
    {
        const char *kHostKey = "0badf00d";
        Bytes partial(40000);
        for (size_t i = 0; i < partial.size(); i++) partial[i] = (uint8_t)(i * 13 + (i >> 9));
        Bytes cor = make_cor(vc20_desc.cor_name, kHostKey, partial);
        Bytes d64 = make_d64("RELOAD");

        // the host's loader and decoupler as the application sees them; a loaded partial puts every register of the core back to its configuration value
        struct FakePort : PartialPort {
            DrvIo *io = 0;
            Bytes got;
            bool is_present = true, take = true, cut = false;
            int ons = 0, offs = 0, begins = 0, ends = 0;
            uint32_t drop_words = 0;
            std::function<void()> on_write;
            bool present() { return is_present; }
            bool decouple(bool on)
            {
                io->events.push_back(on ? "decouple_on" : "decouple_off");
                if (!on) { cut = false; offs++; return true; }
                if (!take) return false;
                cut = true; ons++;
                return true;
            }
            void begin() { io->events.push_back("icap_begin"); got.clear(); begins++; }
            void write(const uint8_t *p, uint32_t n)
            {
                if (got.empty()) io->events.push_back("icap_write");
                got.insert(got.end(), p, p + n);
                if (on_write) on_write();
            }
            void end()
            {
                io->events.push_back("icap_end");
                ends++;
                memset(io->regs, 0, sizeof(io->regs));
                memset(io->dm.reg, 0, sizeof(io->dm.reg));
                std::fill(io->dm.param.begin(), io->dm.param.end(), 0);
            }
            uint32_t words() { return (uint32_t)(got.size() / 4) - drop_words; }
        };
        auto setup = [&](Rig &r, FakePort &p, const Bytes &file, const char *path) {
            p.io = &r.io;
            if (!file.empty()) r.fs.put("/usb0/cores/vc20.cor", file);
            snprintf(r.c.cor_path, sizeof(r.c.cor_path), "%s", path);
            r.c.host_key = 0x0badf00d;
            r.c.partial = &p;
        };
        auto row_of = [&](MenuCtl *m, const char *label) {
            m->key(KEY_F8);
            for (int i = 0; i < m->rows(); i++) {
                char l[80], rt[80]; bool act;
                m->row(i, l, sizeof(l), rt, sizeof(rt), &act);
                if (!strcmp(l, label)) { while (m->selected() < i) m->key(KEY_DOWN); return true; }
            }
            return false;
        };
        auto idx = [&](const std::vector<std::string> &e, const char *what, int from = 0) {
            for (size_t i = (size_t)from; i < e.size(); i++) if (e[i] == what) return (int)i;
            return -1;
        };

        // the whole sequence, in order, and what the new core has to be told again
        {
            Rig r;
            FakePort p;
            r.roms();
            CHECK(r.boot());
            r.fs.put("/usb0/a.d64", d64);
            CHECK_EQ(r.mount("/usb0/a.d64"), OPE_OK);
            setup(r, p, cor, "/usb0/cores/vc20.cor");
            r.io.ram[0x0400] = 0x55;
            r.io.events.clear();
            MenuCtl *m = r.ctl();
            CHECK(row_of(m, "Reload core"));
            CHECK(!row_of(m, "Restart core"));
            CHECK(row_of(m, "Reload core"));
            CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_CLOSE);
            CHECK(!m->status_bad());
            CHECK(p.got == partial);                                         // the file's bytes, in file order, all of them
            CHECK_EQ(p.begins, 1);
            CHECK_EQ(p.ons, 1);
            CHECK_EQ(p.offs, 1);
            CHECK(!p.cut);
            const std::vector<std::string> &e = r.io.events;
            int hold1 = idx(e, "mode_reset");
            int on = idx(e, "decouple_on"), wr = idx(e, "icap_write"), en = idx(e, "icap_end"), off = idx(e, "decouple_off");
            int hold2 = idx(e, "mode_reset", off);
            int rom = idx(e, "rom_write", off);
            CHECK(hold1 >= 0 && hold1 < on);                                 // the machine is held before the core is cut off
            CHECK(on < wr && wr < en && en < off);
            CHECK(off < hold2);                                              // and held again: the new core has its power-up state
            CHECK(hold2 < rom);                                              // the ROMs go into the new core
            CHECK_STR(ev_last(e), "mode_unreset");
            CHECK_EQ(idx(e, "mode_unreset", hold1), (int)e.size() - 1);      // released once, at the very end
            CHECK_EQ(r.io.regs[VC20_FB_BASE + 3], VC20_FB_ADDR >> 24);       // the picture's buffers told to the core again
            CHECK_EQ(r.io.regs[VC20_FB_BASE + 2], (VC20_FB_ADDR >> 16) & 255);
            CHECK_EQ(r.io.dm.reg[0], 1);                                     // the drive: power, ...
            CHECK_EQ(r.io.dm.reg[4], 1);                                     // ... the disk in it ...
            CHECK_STR(r.image(), "a.d64");                                   // ... and the image is the same one
            CHECK_EQ(r.io.ram[0x0400], 0);                                   // the machine's RAM cleared as a cold start does
            CHECK(!memcmp(r.io.dm.rom().data(), pattern(5, 16384).data(), 16384));
        }

        // nothing may be touched when the bundle is not good: the machine keeps running, the menu says why
        auto refused = [&](const char *what, const Bytes &file, const char *path, const char *key, const char *expect) {
            Rig r;
            FakePort p;
            r.roms();
            CHECK(r.boot());
            setup(r, p, file, path);
            if (key) r.c.host_key = (uint32_t)strtoul(key, 0, 16);
            r.io.events.clear();
            MenuCtl *m = r.ctl();
            CHECK(row_of(m, "Reload core"));
            CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_REDRAW);
            CHECK(m->status_bad());
            if (!strstr(m->status(), expect)) { printf("FAIL %s: status \"%s\" lacks \"%s\"\n", what, m->status(), expect); g_fails++; }
            CHECK(r.io.events.empty());                                      // not held, not cut off: nothing happened
            CHECK_EQ(p.ons + p.begins, 0);
        };
        refused("missing", Bytes(), "/usb0/cores/other.cor", 0, "Cannot open");
        { Bytes bad = cor; bad[bad.size() - 64 - 8 - 100] ^= 1; refused("damaged", bad, "/usb0/cores/vc20.cor", 0, "SHA-256"); }
        refused("host", cor, "/usb0/cores/vc20.cor", "11111111", "another host");
        refused("name", make_cor("Other", kHostKey, partial), "/usb0/cores/vc20.cor", 0, "not this core");
        refused("junk", Bytes(100, 7), "/usb0/cores/vc20.cor", 0, "not a valid core bundle");

        // the core is cut off and the load goes wrong: the core stays cut off and the menu tells what to do
        {
            Rig r; FakePort p;
            r.roms(); CHECK(r.boot());
            setup(r, p, cor, "/usb0/cores/vc20.cor");
            p.on_write = [&]() { r.fs.read_budget = 0; };                    // the stick is pulled after the first chunk
            MenuCtl *m = r.ctl();
            CHECK(row_of(m, "Reload core"));
            CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_REDRAW);
            CHECK(m->status_bad());
            CHECK(p.cut);
            CHECK_EQ(p.offs, 0);
            CHECK_EQ(p.ends, 1);
        }
        {
            Rig r; FakePort p;
            r.roms(); CHECK(r.boot());
            setup(r, p, cor, "/usb0/cores/vc20.cor");
            p.drop_words = 1;                                                // the loader took one word less than it was given
            MenuCtl *m = r.ctl();
            CHECK(row_of(m, "Reload core"));
            CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_REDRAW);
            CHECK(m->status_bad());
            CHECK(p.cut);
            CHECK_EQ(p.offs, 0);
        }
        {
            Rig r; FakePort p;
            r.roms(); CHECK(r.boot());
            setup(r, p, cor, "/usb0/cores/vc20.cor");
            p.take = false;                                                  // the decouple does not take effect
            MenuCtl *m = r.ctl();
            CHECK(row_of(m, "Reload core"));
            CHECK_EQ(m->key(KEY_RETURN), MenuCtl::R_REDRAW);
            CHECK(m->status_bad());
            CHECK_EQ(p.begins, 0);
            CHECK(!p.cut);
        }

        // the entry is "Reload core" only where there is a partial to load: a .cfw, a start without a hand-over and a host without a loader keep "Restart core"
        {
            const char *paths[] = { "/usb0/cores/c128.cfw", "", "/usb0/cores/vc20.cor" };
            for (int k = 0; k < 3; k++) {
                Rig r; FakePort p;
                r.roms(); CHECK(r.boot());
                setup(r, p, cor, paths[k]);
                if (k == 2) p.is_present = false;
                MenuCtl *m = r.ctl();
                CHECK(row_of(m, "Restart core"));
                CHECK(!row_of(m, "Reload core"));
            }
        }

        // a flat build with the descriptor hook: "Restart core" calls it first, a refusal stops the restart with the machine running
        {
            static int calls = 0, fail = 0;
            static std::vector<std::string> *ev = 0;
            struct H { static int hook(CoreCtx *, char *msg, int n) { calls++; ev->push_back("cold_start"); if (fail) { snprintf(msg, n, "the C128 says no"); return 1; } return 0; } };
            CoreDesc flat = vc20_desc;
            flat.cold_start = H::hook;
            for (int f = 0; f < 2; f++) {
                Rig r;
                r.c.desc = &flat;
                r.roms(); CHECK(r.boot());
                ev = &r.io.events;
                calls = 0; fail = f;
                r.io.events.clear();
                MenuCtl *m = r.ctl();
                CHECK(row_of(m, "Restart core"));
                int rc = m->key(KEY_RETURN);
                CHECK_EQ(calls, 1);
                CHECK_STR(ev_first(r.io.events), "cold_start");              // before the hold
                if (!f) {
                    CHECK_EQ(rc, MenuCtl::R_CLOSE);
                    CHECK_STR(ev_last(r.io.events), "mode_unreset");
                } else {
                    CHECK_EQ(rc, MenuCtl::R_REDRAW);
                    CHECK(m->status_bad());
                    CHECK_EQ((int)r.io.events.size(), 1);
                }
            }
        }

        // the NES rule: a core that waits for its program is not touched, on a DFX core either
        {
            CoreDesc waits = vc20_desc;
            waits.needs_program = "Load a program";
            Rig r; FakePort p;
            r.roms(); CHECK(r.boot());
            setup(r, p, cor, "/usb0/cores/vc20.cor");
            r.c.desc = &waits;
            r.c.program_loaded = false;
            r.io.events.clear();
            char msg[200] = "";
            CHECK_EQ(boot_restart(&r.c, msg, sizeof(msg)), 1);
            CHECK(strstr(msg, "first") != 0);
            CHECK(r.io.events.empty());
            CHECK_EQ(p.ons, 0);
        }
    }

    // ---- T180 #38: disk images with a FAT long name (80 and 255 characters): listed whole, the image is a folder in the DiskFs, mounted by the whole path; the drive keeps
    // the whole name and path (Swap Disk and the Library's [A] mark compare them), and the status line shows the cut form
    {
        std::string n80, n255;
        for (int i = 0; i < 76; i++) n80 += (char)('a' + i % 26);
        n80 += ".d64";
        for (int i = 0; i < 251; i++) n255 += (char)('a' + (i * 7) % 26);
        n255 += ".d64";
        Rig r;
        r.roms();
        CHECK(r.boot());
        r.fs.put(("/usb0/disks/" + n80).c_str(), d64);
        r.fs.put(("/usb0/disks/" + n255).c_str(), d64);
        FsEntry e[4];
        int n = r.dfs.list("/usb0/disks", e, 4);
        CHECK_EQ(n, 2);
        if (n == 2) {
            CHECK(std::string(e[0].name) == n80 || std::string(e[1].name) == n80);
            CHECK(std::string(e[0].name) == n255 || std::string(e[1].name) == n255);
        }
        const std::string full255 = "/usb0/disks/" + n255;
        CHECK_EQ(r.dfs.list(full255.c_str(), e, 4) >= 0, true);              // the image of a long path is a folder: its files list, no error
        CHECK_EQ(r.mount(("/usb0/disks/" + n80).c_str()), OPE_OK);
        CHECK_STR(r.image(), n80.c_str());
        CHECK_EQ(r.mount(full255.c_str()), OPE_OK);
        CHECK_STR(r.image(), n255.c_str());                                  // the drive's name is the whole one ...
        CHECK_STR(ops_state(&r.c, "drive.drive_a.path"), full255.c_str());  // ... and so is the path the Library's [A] mark is compared with
        CbmDrive *dr = (CbmDrive *)r.c.drive;
        CHECK(dr && dr->image_path() && full255 == dr->image_path());       // the path the mount was made from (the stored mounts' spec)
    }

    DONE("cbm1541");
}
