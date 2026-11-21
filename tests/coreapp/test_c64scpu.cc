// T182 step 8 on the generic application: the C64SCPU descriptor (coreapp/cores/c64scpu.cc) against a model of the core's register page E (byte offsets
// 0xE000 + off from the core window; spec T182 section 3 "ROM and register contract") and of the DDR2 window.
//   S2  no SuperCPU ROM = no start: the core stays held, the screen names the file and the folder, the 65816's gate (ROM_VALID) stays 0; a ROM of 100
//       bytes and one with a reset vector below $8000 are refused with their own message; a good one (64, 128, 256 KiB) starts the core within 1 s.
//   S7  the speed and JiffyDOS settings reach SCPU_SW; the JiffyDOS ROM goes into drive A only with the setting On and the file supplied (the drive's
//       ROM bytes read back equal the file), else the stock ROM; On without the file says so.
// The descriptor is the real one. Offsets are literal numbers on purpose: this is the bench's own reading of the contract. No ROM byte of any machine
// is in this file or in the repository: every image is a pattern made here.
#include <algorithm>
#include <functional>
#include "cbm_rig.h"
#include "drive_model.h"
#include "d1541_fs.h"
#include "regset.h"
#include "cbm1541.h"
#include "cbm_run.h"
#include "d1541_drive.h"
#include <stdio.h>

#define T_PAGE        0xE000u
#define T_JOY         0x024
#define T_STATUS      0x030
#define T_ROM_CTRL    0x040
#define T_ROM_BASE    0x050
#define T_SROM_BASE   0x054
#define T_SROM_LOG2   0x058
#define T_ROM_VALID   0x05C
#define T_SW          0x060
#define T_CHR_ADDR    0x090
#define T_CHR_DATA    0x092
#define T_COLD        0x080
#define T_SROM_WIN    0x02080000u
#define T_ROM_WIN     0x00C00000u
#define OWN           "/SD/cores/c64scpu/roms/"

const CoreDesc *const core_table[] = { &c64scpu_desc, 0 };

struct ScpuIo : FakeIo {
    DriveModel dm;
    uint8_t e[0x100];
    int flushes = 0;
    uint32_t rom_base = 0x01C00000u, srom_base = 0x03080000u;
    uint8_t chr[4096];
    uint32_t chr_addr = 0;
    std::vector<uint8_t> valid_writes;                      // every write of ROM_VALID, in order
    bool released_unvalid = false;                          // the machine was released while ROM_VALID was 0: the 65816 would have run without a ROM
    int cold_polls = 0;
    bool ev_none(const char *n) { for (auto &x : events) if (x == n) return false; return true; }
    struct Dw { uint32_t off, n; };
    std::vector<Dw> dw;
    ScpuIo()
    {
        ddr.assign(0x02200000u, 0);
        memset(e, 0, sizeof(e));
        memset(chr, 0, sizeof(chr));
        e[T_SW] = 0x01;
    }
    uint8_t rd(uint32_t reg)
    {
        if (reg < T_PAGE) return FakeIo::rd(reg);
        uint32_t off = reg - T_PAGE;
        if (off >= 0x50 && off <= 0x53) return (uint8_t)(rom_base >> (8 * (off - 0x50)));
        if (off >= 0x54 && off <= 0x57) return (uint8_t)(srom_base >> (8 * (off - 0x54)));
        if (off == T_COLD) return cold_polls-- > 0 ? 1 : 0;
        if (off == 0x30) return (uint8_t)((stopped ? 0x08 : 0x00) | (e[T_ROM_VALID] & 1));
        if (off == T_CHR_ADDR) return (uint8_t)chr_addr;
        if (off == T_CHR_ADDR + 1) return (uint8_t)(chr_addr >> 8);
        if (off == T_CHR_DATA) { uint8_t b = chr[chr_addr & 4095]; chr_addr = (chr_addr + 1) & 4095; return b; }
        return off < sizeof(e) ? e[off] : 0;
    }
    void wr(uint32_t reg, uint8_t v)
    {
        if (reg < T_PAGE) { FakeIo::wr(reg, v); return; }
        log.push_back({ reg, v, t });
        uint32_t off = reg - T_PAGE;
        if (off == T_ROM_CTRL) { if (v & 1) { flushes++; events.push_back("flush"); } return; }
        if ((off >= 0x50 && off <= 0x57)) return;                 // read only
        if (off == T_ROM_VALID) { valid_writes.push_back(v & 1); events.push_back(v & 1 ? "valid1" : "valid0"); }
        if (off == T_CHR_ADDR) { chr_addr = (chr_addr & 0xF00) | v; return; }
        if (off == T_CHR_ADDR + 1) { chr_addr = (chr_addr & 0xFF) | ((uint32_t)(v & 0xF) << 8); return; }
        if (off == T_CHR_DATA) { chr[chr_addr & 4095] = v; chr_addr = (chr_addr + 1) & 4095; return; }
        if (off == T_COLD) { if (v & 1) { cold_polls = 2; e[T_SW] = 0x01; events.push_back("cold"); } return; }
        if (off < sizeof(e)) e[off] = v;
    }
    void ddr_write(uint32_t off, const void *p, uint32_t n)
    {
        if (off < 0x02200000u - n) {
            dw.push_back({ off, n });
            events.push_back("ddr_write");
        }
        FakeIo::ddr_write(off, p, n);
    }
    void mode_reset(bool on)
    {
        FakeIo::mode_reset(on);
        if (!on && !(e[T_ROM_VALID] & 1)) released_unvalid = true;
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
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + role * 61 + (i >> 8) + (i >> 13));
    return v;
}

// A SuperCPU ROM of our own: a pattern whose 16 bit vector at file offset 0xFFFC is `vec` (a correct one points into $8000-$FFFF).
static Bytes srom(size_t n, uint16_t vec = 0xE123, int role = 11)
{
    Bytes v = pattern(role, n);
    if (n > 0xFFFD) { v[0xFFFC] = (uint8_t)vec; v[0xFFFD] = (uint8_t)(vec >> 8); }
    return v;
}

// The UI of the start: counts the passes of the wait loop, keeps what the screen was told, and runs `at_tick` once at the pass named (the person puts a
// file on the card while the screen waits).
struct WaitUi : BootUi {
    int n = 0, at = -1;
    std::vector<std::string> msgs;
    std::function<void(int)> hook;
    void message(const char *, const char *m, bool) { msgs.push_back(m); }
    void clear() { }
    void tick()
    {
        n++;
        if (hook) hook(n);
        if (n > 300) {
            printf("FAIL boot_start was still waiting for a ROM after %d passes\n", n);
            exit(1);
        }
    }
};

struct Rig {
    ScpuIo io;
    FakeFs fs;
    DiskFs dfs;
    PxRec px;
    CoreCtx c;
    Settings *st;
    Video *vid;
    Rig() : dfs(&fs)
    {
        memset(&c, 0, sizeof(c));
        c.desc = &c64scpu_desc; c.io = &io; c.fs = &dfs;
        strcpy(c.cor_path, "/SD/cores/c64scpu.cor");                   // the core's own folder: /SD/cores/c64scpu/roms (T148)
        st = new Settings(c64scpu_desc.settings, c64scpu_desc.nsettings, c64scpu_desc.store, &fs, "/sd/cores/c64scpu.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(c64scpu_desc.video, &px, &io);
        c.video = vid;
    }
    ~Rig() { delete vid; delete st; delete (CbmDrive *)c.drive; }
    void c64roms()
    {
        fs.put(OWN "kernal-901227-03.bin", pattern(2, 8192));
        fs.put(OWN "basic-901226-01.bin", pattern(1, 8192));
        fs.put(OWN "chargen-901225-01.bin", pattern(3, 4096));
    }
    void all(size_t sn = 65536)
    {
        c64roms();
        fs.put(OWN "scpu64.rom", srom(sn));
    }
    int boot(WaitUi *ui = 0)
    {
        WaitUi local;
        BootResult r = boot_start(&c, ui ? ui : &local);
        return r == BOOT_RUNNING;
    }
    void set(const char *key, int v) { st->set(st->find(key), v); }
    OpResult apply(const char *key)
    {
        OpResult res;
        memset(&res, 0, sizeof(res));
        int i = st->find(key);
        int v = st->value(i);
        OpIn in = in_int("value", v);
        ops_call(&c, c64scpu_desc.settings[i].apply, &in, 1, &res);
        return res;
    }
    Bytes window(uint32_t off, uint32_t n) { return Bytes(io.ddr.begin() + off, io.ddr.begin() + off + n); }
    int ev(const char *name, int from = 0)
    {
        for (size_t i = (size_t)from; i < io.events.size(); i++) if (io.events[i] == name) return (int)i;
        return -1;
    }
};

int main()
{
    OpResult res;

    // ---- the descriptor: identity, the files Alex put on the card (S2 names), the sizes, the required flags, the settings
    {
        CHECK(!memcmp(c64scpu_desc.id, "SCPU", 4));
        CHECK_STR(c64scpu_desc.cor_name, "C64 with SuperCPU");
        CHECK_STR(c64scpu_desc.folder, "c64scpu");
        CHECK(desc_has_capability(&c64scpu_desc, "drive_a"));
        CHECK_EQ(c64scpu_desc.nroms, 8);
        CHECK_STR(c64scpu_desc.roms[0].role, "SuperCPU ROM");
        CHECK(c64scpu_desc.roms[0].required);
        CHECK(c64scpu_desc.roms[0].check != 0);
        CHECK_STR(c64scpu_desc.roms[0].names[0], "scpu64.rom");
        CHECK_EQ(c64scpu_desc.roms[0].sizes[1], 65536u);
        CHECK(!c64scpu_desc.roms[4].required && !c64scpu_desc.roms[5].required);
        int sp = -1, jf = -1;
        for (int i = 0; i < c64scpu_desc.nsettings; i++) {
            if (!strcmp(c64scpu_desc.settings[i].key, "SuperCPU speed")) sp = i;
            if (!strcmp(c64scpu_desc.settings[i].key, "JiffyDOS")) jf = i;
        }
        CHECK(sp >= 0 && jf >= 0);
        if (sp >= 0) { CHECK_STR(c64scpu_desc.settings[sp].values[0], "20 MHz"); CHECK_STR(c64scpu_desc.settings[sp].values[1], "1 MHz"); CHECK_EQ(c64scpu_desc.settings[sp].def, 0); }
        if (jf >= 0) { CHECK_STR(c64scpu_desc.settings[jf].values[0], "Off"); CHECK_STR(c64scpu_desc.settings[jf].values[1], "On"); CHECK_EQ(c64scpu_desc.settings[jf].def, 0); }
        for (int i = 0; i < c64scpu_desc.nsettings; i++) CHECK(strcmp(c64scpu_desc.settings[i].key, "Datasette port") != 0);   // T249 S2: no datasette
    }

    // ---- the start with the five files of the card (their names as Alex put them): everything lands where the contract says, in the order
    // "ROM_VALID 0, image, flush, ROM_VALID 1, release"
    {
        Rig r;
        r.all(65536);
        r.fs.put(OWN "dos1541-325302-01+901229-05.bin", pattern(5, 32768));
        CHECK(r.boot());
        CHECK(r.window(T_SROM_WIN, 65536) == srom(65536));
        CHECK_EQ(r.window(T_SROM_WIN - 1, 1)[0], 0);
        CHECK_EQ(r.window(T_SROM_WIN + 65536, 1)[0], 0);                 // not one byte around it
        CHECK_EQ(r.io.e[T_SROM_LOG2], 16);
        CHECK(r.window(T_ROM_WIN, 8192) == pattern(1, 8192));            // BASIC
        CHECK(r.window(T_ROM_WIN + 0x2000, 8192) == pattern(2, 8192));   // KERNAL
        CHECK(memcmp(r.io.chr, pattern(3, 4096).data(), 4096) == 0);
        CHECK_EQ(r.io.dw.size(), 3u);
        CHECK_EQ(r.io.flushes, 3);                                       // one after each of the three images in the DDR2
        int v0 = r.ev("valid0"), w = r.ev("ddr_write", v0 < 0 ? 0 : v0), f = r.ev("flush", w < 0 ? 0 : w), v1 = r.ev("valid1"), rel = r.ev("mode_unreset");
        CHECK(v0 >= 0 && w > v0 && f > w && v1 > f && rel > v1);
        CHECK(!r.io.released_unvalid);
        CHECK_EQ(r.io.valid_writes.size(), 2u);
        CHECK_EQ(r.io.e[T_SW], 0x01);                                    // the defaults: 20 MHz, JiffyDOS off
        CHECK(r.io.dm.rom() == pattern(5, 32768));                       // the stock 1541 ROM in drive A
    }

    // ---- S2: no SuperCPU ROM. The other four files are there. The core stays held through every pass of the wait, nothing reaches the DDR2 or
    // the gate, the screen names the file and the folder, and the plain C64 KERNAL never runs (no release). With the file put on the card in pass 30
    // the same start goes on, within a second of it, and not before.
    {
        Rig r;
        r.c64roms();
        WaitUi ui;
        uint32_t t_put = 0;
        bool held_before = true;
        ui.hook = [&](int n) {
            if (n < 30) {
                held_before = held_before && r.io.in_reset && !r.io.released_unvalid && r.io.valid_writes.empty() && r.io.dw.empty()
                              && r.io.ev_none("mode_unreset") && !(r.io.e[T_ROM_VALID] & 1);
            }
            if (n == 30) {
                r.fs.put(OWN "scpu64.rom", srom(131072));
                t_put = r.io.t;
            }
        };
        CHECK(r.boot(&ui));
        CHECK(held_before);
        CHECK(ui.n >= 30);                                               // it did wait (a mutant that does not require the file never ticks)
        CHECK(!ui.msgs.empty());
        if (!ui.msgs.empty()) {
            const std::string &m = ui.msgs[0];
            CHECK(m.find("SuperCPU ROM") != std::string::npos);
            CHECK(m.find("scpu64.rom") != std::string::npos);
            CHECK(m.find("is missing in /SD/cores/c64scpu/roms/") != std::string::npos);
        }
        CHECK(r.io.t - t_put <= 1000);                                   // started within a second of the file
        CHECK(r.io.t >= t_put);
        CHECK(r.window(T_SROM_WIN, 131072) == srom(131072));
        CHECK_EQ(r.io.e[T_SROM_LOG2], 17);
        CHECK(!r.io.released_unvalid);
        CHECK(r.io.e[T_ROM_VALID] & 1);
        CHECK(!r.io.in_reset);
    }
    // ---- S2, refused files: 100 bytes (size), 131072 bytes with a reset vector below $8000 (content), an all-0xFF image; each one keeps the core held and
    // is told by its own message; the same file with a good vector is accepted (the negative of the control)
    {
        struct Case { const char *what; Bytes img; const char *says; } cases[] = {
            { "short", srom(100), "100 bytes" },
            { "vector", srom(131072, 0x7FFF), "not a SuperCPU ROM image" },
            { "blank", Bytes(65536, 0xFF), "not a SuperCPU ROM image" },
        };
        for (auto &k : cases) {
            Rig r;
            r.c64roms();
            r.fs.put(OWN "scpu64.rom", k.img);
            WaitUi ui;
            bool held = true;
            ui.hook = [&](int n) {
                held = held && r.io.in_reset && !r.io.released_unvalid && r.io.valid_writes.empty() && r.io.ev_none("mode_unreset");
                if (n == 12) r.fs.put(OWN "scpu64.rom", srom(131072));
            };
            CHECK(r.boot(&ui));
            CHECK(held);
            CHECK(ui.n >= 12);
            CHECK(!ui.msgs.empty());
            if (!ui.msgs.empty()) CHECK(ui.msgs[0].find(k.says) != std::string::npos);
            CHECK(ui.msgs.empty() || ui.msgs[0].find("is missing") == std::string::npos);        // a refused file is not "missing"
            CHECK(r.window(T_SROM_WIN, 131072) == srom(131072));
        }
        // the three sizes are accepted as they are, with the log2 the gateware masks by
        for (size_t n : { (size_t)65536, (size_t)131072, (size_t)262144 }) {
            Rig r;
            r.all(n);
            CHECK(r.boot());
            CHECK_EQ(r.io.e[T_SROM_LOG2], n == 65536 ? 16 : n == 131072 ? 17 : 18);
            CHECK(r.window(T_SROM_WIN, (uint32_t)n) == srom(n));
        }
        // other names of the list
        Rig q;
        q.c64roms();
        q.fs.put(OWN "SCPU.ROM", srom(65536));
        CHECK(q.boot());
    }
    // ---- S2, the second lock: the descriptor's own release does not let the machine go while the core says the image is not valid, and does when it is
    {
        Rig r;
        r.all();
        CHECK(r.boot());
        r.io.e[T_ROM_VALID] = 0;                                         // the core lost the image
        char msg[200];
        r.io.events.clear();
        CHECK_EQ(c64scpu_desc.release(&r.c, msg, sizeof(msg)), 1);
        CHECK(r.ev("mode_unreset") < 0);
        CHECK(strstr(msg, "SuperCPU ROM") != 0);
        r.io.e[T_ROM_VALID] = 1;
        CHECK_EQ(c64scpu_desc.release(&r.c, msg, sizeof(msg)), 0);
        CHECK(r.ev("mode_unreset") >= 0);
    }
    // ---- T249 S2: no datasette (Alex 2026-11-21: no SuperCPU software comes on tape): the op of the old setting is not registered
    {
        Rig r;
        r.all();
        CHECK(r.boot());
        OpResult rr;
        memset(&rr, 0, sizeof(rr));
        OpIn in = in_int("value", 1);
        CHECK_EQ(ops_call(&r.c, "c64scpu.set_dset", &in, 1, &rr), OPE_NOT_FOUND);
    }
    // ---- the image the DDR2 does not keep, a core of another version, a flush that never ends: the gate stays 0 and the put says why
    {
        Rig r;
        r.all();
        CHECK(r.boot());
        Bytes img = srom(65536, 0xE000, 12);
        OpIn in = in_blob("data", img.data(), (uint32_t)img.size());
        r.io.srom_base = 0x03000000u;
        CHECK_EQ(ops_call(&r.c, "c64scpu.put_srom", &in, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "03000000") != 0);
        r.io.srom_base = 0x03080000u;
        r.io.valid_writes.clear();
        CHECK_EQ(ops_call(&r.c, "c64scpu.put_srom", &in, 1, &res), OPE_OK);
        CHECK(r.window(T_SROM_WIN, 65536) == img);
        CHECK(r.io.valid_writes.size() == 2 && r.io.valid_writes[0] == 0 && r.io.valid_writes[1] == 1);
        Bytes bad = srom(65536, 0x1000, 13);
        in = in_blob("data", bad.data(), (uint32_t)bad.size());
        CHECK_EQ(ops_call(&r.c, "c64scpu.put_srom", &in, 1, &res), OPE_FAILED);
        CHECK(r.window(T_SROM_WIN, 65536) == img);                      // a refused image touches nothing
    }

    // ---- S7: the two switches reach SCPU_SW (b0 speed, b1 JiffyDOS), live and at the start
    {
        Rig r;
        r.all();
        CHECK(r.boot());
        CHECK_EQ(r.io.e[T_SW], 0x01);
        r.set("SuperCPU speed", 1);                                      // 1 MHz
        r.apply("SuperCPU speed");
        CHECK_EQ(r.io.e[T_SW], 0x00);
        r.set("JiffyDOS", 1);
        r.apply("JiffyDOS");
        CHECK_EQ(r.io.e[T_SW], 0x02);
        r.set("SuperCPU speed", 0);                                      // 20 MHz again
        r.apply("SuperCPU speed");
        CHECK_EQ(r.io.e[T_SW], 0x03);
        r.set("JiffyDOS", 0);
        r.apply("JiffyDOS");
        CHECK_EQ(r.io.e[T_SW], 0x01);
        // the stored choice is applied at the next start, and the core's own reset value is not what stays after a cold start
        Rig q;
        q.all();
        q.set("SuperCPU speed", 1);
        q.set("JiffyDOS", 1);
        CHECK(q.boot());
        CHECK_EQ(q.io.e[T_SW], 0x02);
        q.io.e[T_SW] = 0x01;                                             // COLD put the core's reset value back ...
        CHECK_EQ(ops_call(&q.c, "machine.restart", 0, 0, &res), OPE_OK);
        CHECK_EQ(q.io.e[T_SW], 0x02);                                    // ... the restart writes the settings again
        CHECK(q.ev("cold") >= 0);
    }
    // ---- S7, the drive: the stock ROM with JiffyDOS Off, the JiffyDOS ROM with On (the drive's bytes read back equal the file), the stock ROM again with
    // Off; On without the file: one line, the stock ROM stays, the switch bit follows; file present and setting Off: stock
    {
        Bytes stock = pattern(5, 32768), jiffy = pattern(7, 32768);
        Rig r;
        r.all();
        r.fs.put(OWN "1541.rom", stock);
        r.fs.put(OWN "jiffydos-1541.bin", jiffy);
        CHECK(r.boot());
        CHECK(r.io.dm.rom() == stock);                                   // file present, setting Off -> stock (the negative)
        r.set("JiffyDOS", 1);
        res = r.apply("JiffyDOS");
        CHECK(r.io.dm.rom() == jiffy);
        CHECK_EQ(r.io.e[T_SW] & 2, 2);
        CHECK(res.msg[0] == 0);
        r.set("JiffyDOS", 0);
        r.apply("JiffyDOS");
        CHECK(r.io.dm.rom() == stock);
        CHECK_EQ(r.io.e[T_SW] & 2, 0);
        // the start with On: the drive holds the JiffyDOS ROM from the first cycle, whatever the order the files came in
        Rig q;
        q.all();
        q.fs.put(OWN "1541.rom", stock);
        q.fs.put(OWN "jiffydos-1541.bin", jiffy);
        q.set("JiffyDOS", 1);
        CHECK(q.boot());
        CHECK(q.io.dm.rom() == jiffy);
        CHECK_EQ(q.io.e[T_SW], 0x03);
        // On without the JiffyDOS file: the line, the stock ROM, the bit
        Rig n;
        n.all();
        n.fs.put(OWN "1541.rom", stock);
        CHECK(n.boot());
        n.set("JiffyDOS", 1);
        res = n.apply("JiffyDOS");
        CHECK_STR(res.msg, "JiffyDOS 1541 ROM not supplied");
        CHECK(n.io.dm.rom() == stock);
        CHECK_EQ(n.io.e[T_SW] & 2, 2);
        // a 16 KiB JiffyDOS ROM is taken too (the drive library places it); a wrong size is refused by the scan
        Rig m;
        m.all();
        m.fs.put(OWN "1541.rom", stock);
        m.fs.put(OWN "jiffydos-1541.bin", pattern(9, 16384));
        m.set("JiffyDOS", 1);
        CHECK(m.boot());
        {
            Bytes j16 = pattern(9, 16384), want(j16);
            want.insert(want.end(), j16.begin(), j16.end());
            CHECK(m.io.dm.rom() == want);                                // a 16 KiB ROM is mirrored into the drive's 32 KiB
        }
    }
    // ---- T182 D17: drive A offers the 1581: the .d81 type, the blank D81 task, the 1581 ROM (own file, 32 KiB only) and the JiffyDOS 1581 ROM
    {
        bool has81 = false, rom81 = false, jf81 = false, blank = false;
        for (int i = 0; i < c64scpu_desc.nfiletypes; i++) has81 = has81 || !strcmp(c64scpu_desc.filetypes[i].ext, ".d81");
        for (int i = 0; i < c64scpu_desc.nroms; i++) {
            if (!strcmp(c64scpu_desc.roms[i].role, "1581 drive")) {
                rom81 = true;
                CHECK_STR(c64scpu_desc.roms[i].names[0], "1581.rom");
                CHECK_EQ(c64scpu_desc.roms[i].sizes[0], 32768u);
                CHECK(!c64scpu_desc.roms[i].required);
            }
            jf81 = jf81 || !strcmp(c64scpu_desc.roms[i].role, "JiffyDOS 1581 ROM");
        }
        for (int i = 0; i < c64scpu_desc.ntasks; i++) blank = blank || (c64scpu_desc.tasks[i].label && !strcmp(c64scpu_desc.tasks[i].label, "Create blank D81"));
        CHECK(has81 && rom81 && jf81 && blank);
        Bytes s81 = pattern(11, 32768);
        Rig r;
        r.all();
        r.fs.put(OWN "1581.rom", s81);
        r.io.dm.multi = true;
        r.fs.put("/usb0/a.d81", Bytes(819200, 0));
        r.fs.put(OWN "1541.rom", pattern(5, 32768));
        CHECK(r.boot());
        CHECK(r.io.dm.rom() == pattern(5, 32768));                       // no disk yet: the 1541
        OpIn m1[3] = { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d81"), in_str("mode", "rw") };
        CHECK_EQ(ops_call(&r.c, "drive.mount", m1, 3, &res), OPE_OK);
        CHECK(r.io.dm.rom() == s81);                                     // the .d81 mount switches the drive to the 1581 and its ROM
        Rig q;
        q.all();
        q.fs.put(OWN "1581.rom", pattern(11, 16384));                    // a 16 KiB file is no 1581 ROM
        CHECK(q.boot());
        CHECK(q.io.dm.rom() != pattern(11, 16384));
    }
    // ---- the joystick swap and the System Mode reach their registers (the same registers and format bytes as the C128's: host/mode_ctl.v is one file)
    {
        Rig r;
        r.all();
        CHECK(r.boot());
        CHECK_EQ(r.io.e[T_JOY], 0);
        r.set("Joystick ports", 1);
        r.apply("Joystick ports");
        CHECK_EQ(r.io.e[T_JOY], 1);
        static const uint8_t fmt[4] = { 0x00, 0x2B, 0x2A, 0x01 };
        for (int m : { 1, 2, 3, 0 }) {
            r.set("System Mode", m);
            r.apply("System Mode");
            CHECK_EQ(r.io.vfmt, fmt[m]);
        }
    }
    // ---- T206 S1: Run Disk. The disk is mounted, the machine reset, and LOAD"*",8,1 / RUN pressed on the key matrix (not poked into the keyboard buffer:
    // the 65816 reads bank 0 from the SuperCPU's SRAM, scpu_mem.v mir_tab) once the screen shows the prompt. A model of the KERNAL's side: it decodes the
    // matrix writes as the scan would (a key counts when it is down at the 50 Hz scan, with L SHIFT) and answers a RETURN with a LOAD that ends 3 s later.
    {
        Rig r;
        r.all();
        r.fs.put(OWN "1541.rom", pattern(5, 16384));
        r.fs.put("/usb0/games/run.d64", Bytes(174848, 0));
        r.fs.put("/usb0/games/run.d81", Bytes(819200, 0));
        r.io.dm.multi = true;
        r.fs.put(OWN "1581.rom", pattern(11, 32768));
        CHECK(r.boot());
        CHECK(c64scpu_desc.type_op && !strcmp(c64scpu_desc.type_op, "c64scpu.type_script"));
        for (const char *ext : { ".d64", ".g64", ".d81" }) {
            for (int i = 0; i < c64scpu_desc.nfiletypes; i++) {
                if (strcmp(c64scpu_desc.filetypes[i].ext, ext)) continue;
                bool run = false;
                for (int k = 0; k < c64scpu_desc.filetypes[i].nactions; k++) run = run || !strcmp(c64scpu_desc.filetypes[i].actions[k].label, "Run Disk");
                CHECK(run);
            }
        }
        // the inverse of cbm_key_for over the characters the script uses
        auto decode = [](const uint8_t m[8], char *ch, bool *only_shift) {
            int down[3], n = 0;
            for (int idx = 0; idx < 64; idx++) if (m[idx / 8] & (1u << (idx % 8))) down[n < 3 ? n++ : 2] = idx;
            bool shift = false;
            int key = -1;
            for (int k = 0; k < n; k++) { if (down[k] == 15) shift = true; else key = down[k]; }
            *only_shift = n == 1 && shift;
            *ch = 0;
            if (key < 0) return;
            for (int c = 1; c < 256; c++) {
                CbmKey ck;
                if (cbm_key_for((uint8_t)c, &ck) && ck.idx == key && ck.shift == shift) { *ch = (char)c; return; }
            }
        };
        auto put_row = [&](Rig &rr, int row, const char *text) {            // screen codes: A-Z 1-26, '.' 46, others as ASCII, rest blank
            for (int i = 0; i < 40; i++) rr.io.ram[0x0400 + row * 40 + i] = 32;
            for (int i = 0; text[i]; i++) rr.io.ram[0x0400 + row * 40 + i] = text[i] >= 'A' && text[i] <= 'Z' ? (uint8_t)(text[i] - 'A' + 1) : (uint8_t)text[i];
        };
        for (const char *img : { "/usb0/games/run.d64", "/usb0/games/run.d81" }) {
            r.io.events.clear();
            r.io.mx_log.clear();
            for (int i = 0; i < 1000; i++) r.io.ram[0x0400 + i] = 32;
            for (int i = 0; i < 0x100; i++) r.io.ram[i] = 0;                  // page 0 of the C64 RAM stays zero: BASIC's pointers and $C6 are not what READY is read from
            put_row(r, 3, "**** COMMODORE 64 BASIC V2 ****");                 // a boot screen without the prompt yet: nothing may be pressed
            OpIn in[2] = { in_str("slot", "iec:drive_a"), in_str("path", img) };
            OpResult res;
            memset(&res, 0, sizeof(res));
            CHECK_EQ(ops_call(&r.c, "drive.mount_run", in, 2, &res), OPE_OK);
            CHECK(r.ev("mode_reset") >= 0);
            std::string typed;
            uint32_t load_done = 0, t0 = r.io.t;
            size_t seen = 0;
            for (int i = 0; i < 700; i++) {
                if (i == 40) put_row(r, 5, "READY.");                         // 2 s: the prompt appears
                if (i < 40) CHECK_EQ(r.io.mx_log.size(), 0u);                  // nothing before READY
                r.io.delay_ms(50);
                ops_call(&r.c, "c64scpu.service", 0, 0, &res);
                for (; seen < r.io.mx_log.size(); seen++) {
                    char ch; bool only_shift;
                    decode(r.io.mx_log[seen].m, &ch, &only_shift);
                    if (ch) typed += ch;
                    if (ch == '\r') {                                           // RETURN down: the KERNAL takes the line, LOAD runs and the prompt returns 3 s after the key is up
                        put_row(r, 6, "SEARCHING FOR *");
                        load_done = 0xFFFFFFFFu;
                    }
                }
                if (load_done == 0xFFFFFFFFu && seen == r.io.mx_log.size() && (r.io.mx_log.back().m[0] | r.io.mx_log.back().m[1] | r.io.mx_log.back().m[2] | r.io.mx_log.back().m[3]) == 0 &&
                    r.io.mx_log.back().m[4] == 0) load_done = r.io.mx_log.back().t + 3000;
                if (load_done && load_done != 0xFFFFFFFFu && r.io.t >= load_done) { put_row(r, 8, "READY."); load_done = 0; }
            }
            (void)t0;
            CHECK(typed == "LOAD\"*\",8,1\rRUN\r");
            CHECK_EQ(r.io.ram[0xC6], 0);                                       // the keyboard buffer was never poked
            CHECK_EQ(r.io.ram[0x0277], 0);
            CHECK_EQ(r.io.mx_log.back().t > 0, true);
            for (int k = 0; k < 8; k++) CHECK_EQ(r.io.mx_log.back().m[k], 0);  // every key let go at the end
            // timing: a key (with its L SHIFT) is down >= 90 ms, up >= 90 ms before the next press, L SHIFT leads its key by >= 40 ms
            for (size_t k = 0; k < r.io.mx_log.size(); k++) {
                char ch; bool only_shift;
                decode(r.io.mx_log[k].m, &ch, &only_shift);
                bool zero = true;
                for (int b = 0; b < 8; b++) zero = zero && r.io.mx_log[k].m[b] == 0;
                if (zero || k + 1 >= r.io.mx_log.size()) continue;
                uint32_t held = r.io.mx_log[k + 1].t - r.io.mx_log[k].t;
                if (only_shift) CHECK(held >= 40);
                else CHECK(held >= 90);
                if (!only_shift && k + 2 < r.io.mx_log.size() && ch != 0) {
                    bool z1 = true;
                    for (int b = 0; b < 8; b++) z1 = z1 && r.io.mx_log[k + 1].m[b] == 0;
                    if (z1) CHECK(r.io.mx_log[k + 2].t - r.io.mx_log[k + 1].t >= 90);
                }
            }
            // the quote is SHIFT + 2: the log holds L SHIFT alone first, then L SHIFT with the 2 (col 7, row 3 = index 59)
            bool lead = false, quote = false;
            for (size_t k = 0; k + 1 < r.io.mx_log.size(); k++) {
                const uint8_t *a = r.io.mx_log[k].m, *b = r.io.mx_log[k + 1].m;
                bool a_shift_only = a[1] == 0x80 && !a[0] && !a[2] && !a[3] && !a[4] && !a[5] && !a[6] && !a[7];
                if (a_shift_only && b[1] == 0x80 && b[7] == 0x08) { lead = true; quote = true; }
            }
            CHECK(lead && quote);
        }
        // ---- the other cores keep the buffer: the method is chosen by CbmRunMachine::screen (the SuperCPU sets it, the C64U-family descriptors do not)
        {
            CbmRunMachine m;
            memset(&m, 0, sizeof(m));
            CHECK_EQ(m.screen, 0);
        }
        // ---- the characters of a name: letters, digits, the shifted symbols, SHIFT + letter; a graphic character has no key
        {
            CbmKey k;
            CHECK(cbm_key_for('"', &k) && k.shift && k.idx == 59);
            CHECK(cbm_key_for(',', &k) && !k.shift && k.idx == 47);
            CHECK(cbm_key_for('*', &k) && !k.shift && k.idx == 49);
            CHECK(cbm_key_for('A', &k) && !k.shift && k.idx == 10);
            CHECK(cbm_key_for(0xC1, &k) && k.shift && k.idx == 10);
            CHECK(cbm_key_for('?', &k) && k.shift && k.idx == 55);
            CHECK(cbm_key_for('\r', &k) && !k.shift && k.idx == 1);
            CHECK(!cbm_key_for(0x00, &k));
            CHECK(!cbm_key_for(0xA0, &k));
        }
    }
    // ---- T250 S2: machine.read_mem / machine.write_mem. Each region of T250 D2 reaches its window offset; a write into the 65816's RAM
    // is followed by one cache flush, a write into the C64 RAM by none; the ROM banks $F8-$FF are not mapped
    {
        Rig r;
        for (uint32_t i = 0; i < 4; i++) {
            r.io.ddr[0x0200C000u + i] = (uint8_t)(0x11 + i);              // SRAM bank 0, $00C000
            r.io.ddr[0x0201F000u + i] = (uint8_t)(0x21 + i);              // SRAM bank 1, $01F000
            r.io.ddr[0x01020000u + i] = (uint8_t)(0x31 + i);              // SuperRAM, $020000
            r.io.ddr[0x01000010u + i] = (uint8_t)(0x41 + i);              // SuperRAM byte $10, seen at $F60010
            r.io.ram[0x0400u + i] = (uint8_t)(0x51 + i);                  // C64 RAM $0400, seen at $1000400
        }
        struct Rd { int32_t addr; uint8_t first; } rds[] = { { 0x00C000, 0x11 }, { 0x01F000, 0x21 }, { 0x020000, 0x31 }, { 0xF60010, 0x41 }, { 0x1000400, 0x51 } };
        for (auto &x : rds) {
            OpIn in[2] = { in_int("address", x.addr), in_int("length", 4) };
            OpResult rr;
            CHECK_EQ(ops_call(&r.c, "machine.read_mem", in, 2, &rr), OPE_OK);
            CHECK_EQ(rr.ndata, 4);
            CHECK_EQ(rr.data[0], x.first);
            CHECK_EQ(rr.data[3], (uint8_t)(x.first + 3));
        }
        int f0 = r.io.flushes;
        OpResult rw;
        OpIn w1[2] = { in_int("address", 0x01F100), in_str("data", "A1B2C3D4") };
        CHECK_EQ(ops_call(&r.c, "machine.write_mem", w1, 2, &rw), OPE_OK);
        CHECK_EQ(r.io.ddr[0x0201F100u], 0xA1);
        CHECK_EQ(r.io.ddr[0x0201F103u], 0xD4);
        CHECK_EQ(r.io.flushes, f0 + 1);
        OpIn w2[2] = { in_int("address", 0x100C000), in_str("data", "5A5B") };
        CHECK_EQ(ops_call(&r.c, "machine.write_mem", w2, 2, &rw), OPE_OK);
        CHECK_EQ(r.io.ram[0xC000u], 0x5A);
        CHECK_EQ(r.io.ram[0xC001u], 0x5B);
        CHECK_EQ(r.io.flushes, f0 + 1);
        OpIn x1[2] = { in_int("address", 0xF80000), in_int("length", 1) };
        CHECK_EQ(ops_call(&r.c, "machine.read_mem", x1, 2, &rw), OPE_ARG);
    }
    DONE("test_c64scpu");
}
