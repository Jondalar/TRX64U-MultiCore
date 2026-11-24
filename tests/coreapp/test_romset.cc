// T104 5.1, 5.2 `rom_set`, 5.7: the Kickstart as a ROM set - scanned out of a directory, chosen by the setting or by Auto (which reads the version
// out of the image, ADR-0056), placed in the window (a 256 KB ROM twice), reloaded when the choice changes, with the machine's RAM cleared on a
// cold start. The record is the Amiga's and the A1200's (coreapp/cores/amiga.cc); the code under test is romset.cc, kickstart.cc and boot.cc.
#include <string>
#include "check.h"
#include "amiga_model.h"
#include "boot.h"
#include "engines.h"
#include "ops_glue.h"

static std::vector<uint8_t> kick(uint32_t size, uint16_t ver, uint16_t rev, uint8_t fill)
{
    std::vector<uint8_t> v(size, fill);
    uint16_t magic = size == 262144 ? 0x1111 : 0x1114;
    v[0] = (uint8_t)(magic >> 8); v[1] = (uint8_t)magic;
    v[12] = (uint8_t)(ver >> 8); v[13] = (uint8_t)ver;
    v[14] = (uint8_t)(rev >> 8); v[15] = (uint8_t)rev;
    return v;
}

static const char *DIR = "/usb0/roms/amiga";

static void stock(FakeFs &fs)
{
    fs.put(std::string(DIR) + "/kick13.rom", kick(262144, 34, 5, 0x13));
    fs.put(std::string(DIR) + "/AmigaVision.rom", kick(524288, 40, 68, 0x31));
    fs.put(std::string(DIR) + "/MegaAGS-Kickstart.rom", kick(524288, 40, 68, 0x32));
    fs.put(std::string(DIR) + "/notes.rom", std::vector<uint8_t>(1000, 1));      // neither size: not a candidate
}

struct Ui : BootUi {
    std::vector<std::string> msgs;
    std::vector<uint32_t> msg_t;
    int ticks = 0, insert_at = -1;
    AmigaIo *io = 0;
    FakeFs *fs = 0;
    bool released_early = false;
    void message(const char *, const char *m, bool) { msgs.push_back(m); msg_t.push_back(io->t); }
    void clear() { }
    void tick()
    {
        ticks++;
        if (io->reset_releases > 0 && insert_at >= 0 && ticks < insert_at) released_early = true;
        if (ticks == insert_at) stock(*fs);
    }
};

// a medium that lists in the reverse of the sorted order: the scan has to sort, a directory is in the file system's order
struct RevFs : FakeFs {
    int list(const char *dir, FsEntry *out, int max)
    {
        int n = FakeFs::list(dir, out, max);
        for (int i = 0; i < n / 2; i++) { FsEntry t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t; }
        return n;
    }
};

struct Machine {
    RevFs fs;
    AmigaRig *r;
    Video *v;
    Machine(const CoreDesc *d = &AM_REC) { r = new AmigaRig(&fs, d); v = new Video(d->video, &r->px, &r->io); r->c.video = v; }
    bool window_is(uint32_t off, const std::vector<uint8_t> &img) { return !memcmp(&r->io.ddr[off], img.data(), img.size()); }
};

#ifdef TEST_APP_A1200
// the A1200's app (a12app): the A1200 reads the same files as the A500 and takes 3.1: its Auto, its start with a 512 KB ROM placed once
int main()
{
    char sum[200];
    {
        Machine a12;
        stock(a12.fs);
        CHECK_EQ(romset_scan(&a12.r->c, sum, sizeof(sum)), ROM_FOUND);
        CHECK_EQ(romset_choice(&a12.r->c), 0);
        CHECK_STR(amiga1200_desc.settings[0].values[0], "Auto (3.1 first)");
    }
    {
        Machine b;
        stock(b.fs);
        Ui ub; ub.io = &b.r->io; ub.fs = &b.fs;
        CHECK_EQ(boot_start(&b.r->c, &ub), BOOT_RUNNING);
        CHECK(b.window_is(AM_W_ROM, kick(524288, 40, 68, 0x31)));
        CHECK_STR(b.r->c.eng->rs.loaded, "AmigaVision.rom");
    }
    DONE("a12_romset");
}
#else
int main()
{
    // ---- the format: the version is in the image, not in the name
    {
        std::vector<uint8_t> a = kick(262144, 34, 5, 0), b = kick(524288, 40, 68, 0);
        uint16_t ver = 0, rev = 0;
        CHECK(ks_header_version(a.data(), 16, &ver, &rev) && ver == 34 && rev == 5);
        CHECK(ks_header_version(b.data(), 16, &ver, &rev) && ver == 40 && rev == 68);
        CHECK(!ks_header_version(b.data(), 15, &ver, &rev));                       // too short
        std::vector<uint8_t> junk(16, 0x55);
        CHECK(!ks_header_version(junk.data(), 16, &ver, &rev));
        std::vector<uint8_t> nomagic = kick(524288, 40, 68, 0);                     // a plausible version word, but not a Kickstart's first word
        nomagic[0] = 0xAB; nomagic[1] = 0xCD;
        CHECK(!ks_header_version(nomagic.data(), 16, &ver, &rev));
        std::vector<uint8_t> odd = kick(524288, 99, 0, 0);                          // a magic, but not a version
        CHECK(!ks_header_version(odd.data(), 16, &ver, &rev));
        KsCandidate l[4] = { { 524288, 40, 68 }, { 262144, 34, 5 }, { 524288, 0, 0 }, { 524288, 40, 70 } };
        CHECK_EQ(ks_pick(l, 4, KS_VER_13), 1);                                      // exactly the wanted version
        CHECK_EQ(ks_pick(l, 4, KS_VER_31), 0);                                      // the first of 3.1
        KsCandidate p[2] = { { 262144, 33, 1 }, { 262144, 34, 5 } };               // 1.2 before 1.3: the exact version beats the family's first
        CHECK_EQ(ks_pick(p, 2, KS_VER_13), 1);
        KsCandidate q[2] = { { 524288, 39, 1 }, { 524288, 40, 68 } };
        CHECK_EQ(ks_pick(q, 2, KS_VER_31), 1);
        KsCandidate m[3] = { { 524288, 0, 0 }, { 524288, 37, 1 }, { 262144, 33, 1 } };
        CHECK_EQ(ks_pick(m, 3, KS_VER_13), 2);                                      // no exact 1.3: the same family (1.x/2.x)
        CHECK_EQ(ks_pick(m, 3, KS_VER_31), 1);                                      // the family wanted has none: any real Kickstart, not the blob before it
        KsCandidate n[2] = { { 524288, 0, 0 }, { 524288, 33, 0 } };
        CHECK_EQ(ks_pick(n, 2, KS_VER_31), 1);                                      // ... a real Kickstart beats a blob
        KsCandidate o[2] = { { 524288, 0, 0 }, { 524288, 0, 0 } };
        CHECK_EQ(ks_pick(o, 2, KS_VER_31), 0);
        CHECK_EQ(ks_pick(l, 0, KS_VER_31), 0);
    }

    // ---- the scan: the members, their order, the labels of the setting, and what Auto takes for each machine
    {
        Machine m;
        stock(m.fs);
        char sum[200];
        CHECK_EQ(romset_scan(&m.r->c, sum, sizeof(sum)), ROM_FOUND);
        RomSetState &rs = m.r->c.eng->rs;
        CHECK_EQ(rs.count, 3);
        CHECK_STR(rs.m[0].name, "AmigaVision.rom");                                 // sorted by name, case-blind
        CHECK_STR(rs.m[1].name, "kick13.rom");
        CHECK_STR(rs.m[2].name, "MegaAGS-Kickstart.rom");
        CHECK_STR(rs.dir, DIR);
        const Setting &ks = amiga_desc.settings[0];
        CHECK_STR(ks.values[0], "Auto (1.3 first)");
        CHECK_STR(ks.values[1], "AmigaVision.rom");
        CHECK_STR(ks.values[3], "MegaAGS-Kickstart.rom");
        CHECK_STR(ks.values[4], "-");
        CHECK_EQ(romset_choice(&m.r->c), 1);                                        // the A500: 1.3, though the names say nothing
        CHECK(rs.m[1].c.ver == 34 && rs.m[1].c.rev == 5);
        m.r->st->set(0, 3);                                                         // the setting names a file
        CHECK_EQ(romset_choice(&m.r->c), 2);
        m.r->st->set(0, 8);                                                         // a place the list does not fill: Auto
        CHECK_EQ(romset_choice(&m.r->c), 1);
    }

    // ---- refusals: no file at all; files, but none of an allowed size (the first directory with a .rom decides, as the reference's does)
    {
        Machine m;
        char sum[240];
        CHECK_EQ(romset_scan(&m.r->c, sum, sizeof(sum)), ROM_MISSING);
        CHECK(strstr(sum, "Kickstart not found") && strstr(sum, "roms/amiga"));
        m.fs.put("/flash/roms/amiga/odd.rom", std::vector<uint8_t>(1000, 1));
        stock(m.fs);                                                                // good files on a stick, a bad one in the flash
        CHECK_EQ(romset_scan(&m.r->c, sum, sizeof(sum)), ROM_WRONG_SIZE);
        CHECK(strstr(sum, "odd.rom") && strstr(sum, "/flash/roms/amiga") && strstr(sum, "256K or 512K"));
        Machine k;                                                                  // more than eight: eight
        for (int i = 0; i < 10; i++) k.fs.put(std::string(DIR) + "/k" + std::to_string(i) + ".rom", kick(262144, 34, 5, (uint8_t)i));
        CHECK_EQ(romset_scan(&k.r->c, sum, sizeof(sum)), ROM_FOUND);
        CHECK_EQ(k.r->c.eng->rs.count, 8);
    }

    // ---- the start: held, the Kickstart placed (256 KB twice), the RAM clean, settings in, released last
    {
        Machine m;
        stock(m.fs);
        std::vector<uint8_t> k13 = kick(262144, 34, 5, 0x13);
        memset(&m.r->io.ddr[0], 0xEE, AM_W_ROM);                                    // what the last run left in chip, slow and fast RAM
        Ui ui; ui.io = &m.r->io; ui.fs = &m.fs;
        BootResult br = boot_start(&m.r->c, &ui);
        CHECK_EQ(br, BOOT_RUNNING);
        CHECK(m.window_is(AM_W_ROM, k13));
        CHECK(m.window_is(AM_W_ROM + 0x40000, k13));                                // twice
        bool clean = true;
        for (uint32_t i = 0; i < AM_W_ROM; i += 4093) clean &= m.r->io.ddr[i] == 0;
        CHECK(clean);
        CHECK(m.r->c.started);
        CHECK_STR(m.r->io.events.front().c_str(), "mode_reset");
        CHECK_STR(m.r->io.events.back().c_str(), "mode_unreset");
        // order of the register writes: HOLD first, the configuration, HOLD off last
        int hold = -1, mem = -1, ide = -1, rel = -1;
        for (size_t i = 0; i < m.r->io.log.size(); i++) {
            auto &w = m.r->io.log[i];
            if (w.reg == AM_CTRL && w.v == 1 && hold < 0) hold = (int)i;
            if (w.reg == AM_MEM_CFG) mem = (int)i;
            if (w.reg == AM_IDE_CFG) ide = (int)i;
            if (w.reg == AM_CTRL && w.v == 0) rel = (int)i;
            CHECK(w.reg >= 8);                                                      // the frame buffer has no FB_BASE register: nothing written over the ID block
        }
        CHECK(hold >= 0 && mem > hold && ide > hold && rel > mem && rel > ide);
        CHECK_EQ(m.r->io.regs[AM_IDE_CFG], 0x21);
        CHECK_EQ(m.r->io.regs[AM_CTRL], 0);
        CHECK_STR(m.r->c.eng->rs.loaded, "kick13.rom");
    }

    // ---- the wait: never released without a Kickstart, told after 10 s; a file of the wrong size is told at once and looked at again after 5 s
    {
        Machine m;
        Ui ui; ui.io = &m.r->io; ui.fs = &m.fs; ui.insert_at = 25;
        CHECK_EQ(boot_start(&m.r->c, &ui), BOOT_RUNNING);
        CHECK(!ui.released_early);
        CHECK_EQ(ui.msgs.size(), 1);
        CHECK(ui.msg_t.size() && ui.msg_t[0] >= 9500 && ui.msg_t[0] <= 10500);
        Machine w;
        w.fs.put(std::string(DIR) + "/odd.rom", std::vector<uint8_t>(1000, 1));
        Ui uw; uw.io = &w.r->io; uw.fs = &w.fs; uw.insert_at = 2;                   // a good file appears after the second pass
        CHECK_EQ(boot_start(&w.r->c, &uw), BOOT_RUNNING);
        CHECK(uw.msgs.size() == 1 && strstr(uw.msgs[0].c_str(), "odd.rom"));
        CHECK(uw.msg_t.size() && uw.msg_t[0] == 0);                                                   // at once
        CHECK(w.r->io.t >= 10000 && w.r->io.t < 11000);                             // two passes of 5 s
    }

    // ---- resets: a plain reset keeps the Kickstart and the RAM; a changed choice reloads and cleans; "Reload" always does
    {
        Machine m;
        stock(m.fs);
        Ui ui; ui.io = &m.r->io; ui.fs = &m.fs;
        boot_start(&m.r->c, &ui);
        std::vector<uint8_t> k13 = kick(262144, 34, 5, 0x13), mega = kick(524288, 40, 68, 0x32);
        m.r->io.ddr[AM_W_ROM + 100] = 0x99;                                         // a mark in the window ROM
        m.r->io.ddr[0x1000] = 0x77;                                                 // and one in chip RAM
        char msg[200];
        CHECK_EQ(boot_cold_reset(&m.r->c, msg, sizeof(msg)), 0);
        CHECK_EQ(m.r->io.ddr[AM_W_ROM + 100], 0x99);                                // not reloaded
        CHECK_EQ(m.r->io.ddr[0x1000], 0x77);
        m.r->st->set(0, 3);                                                         // another file is chosen
        CHECK_EQ(boot_cold_reset(&m.r->c, msg, sizeof(msg)), 0);                    // "Reset Amiga" reloads it
        CHECK(m.window_is(AM_W_ROM, mega));
        CHECK_EQ(m.r->io.ddr[0x1000], 0);                                           // a cold start: no leftovers
        CHECK_STR(m.r->c.eng->rs.loaded, "MegaAGS-Kickstart.rom");
        m.r->io.ddr[AM_W_ROM + 100] = 0x99;
        CHECK_EQ(boot_reload(&m.r->c, msg, sizeof(msg)), 0);                        // "Reload Kickstart, Reset": always
        CHECK(m.window_is(AM_W_ROM, mega));
        // the stick is gone: the reload refuses, the machine is not touched
        m.fs.files.clear();
        size_t events = m.r->io.events.size();
        CHECK(boot_reload(&m.r->c, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "not found") != 0);
        CHECK_EQ(m.r->io.events.size(), events);
        // ... but a plain reset with the list it has still works from the file it knows, and says so when that is gone
        m.r->st->set(0, 1);
        CHECK(boot_cold_reset(&m.r->c, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "cannot read") != 0);
    }

    // ---- the .cfg names a file the list did not hold before the scan: it is read again after it
    {
        for (int i = 1; i <= ROMSET_MAX; i++) strcpy(amiga_desc.romset->labels->text[i], "-");      // the labels are the descriptor's: a new run has none
        FakeFs fs;
        stock(fs);
        fs.put_text("/sd/cores/amiga.cfg", "[Amiga Settings]\nKickstart file (at reload)=MegaAGS-Kickstart.rom\nChip RAM (at next reset)=1M\n");
        AmigaRig r(&fs);
        r.st = new Settings(amiga_desc.settings, amiga_desc.nsettings, amiga_desc.store, &fs, "/sd/cores/amiga.cfg", 0);
        r.st->load();
        r.c.settings = r.st;
        CHECK_EQ(r.st->value(0), 0);                                                // before the scan the label is unknown
        CHECK_EQ(r.st->value(1), 1);
        char sum[200];
        romset_scan(&r.c, sum, sizeof(sum));
        CHECK_EQ(r.st->value(0), 3);
        CHECK_EQ(romset_choice(&r.c), 2);
    }

    // ---- the core's memory does not take the ROM: read back, refused, and the start waits instead of running without it
    {
        struct Bad : AmigaIo {
            void ddr_write(uint32_t off, const void *p, uint32_t n) { if (off >= AM_W_ROM && off < AM_W_ROM + 0x80000) return; AmigaIo::ddr_write(off, p, n); }
        };
        FakeFs fs;
        stock(fs);
        Bad io;
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = &amiga_desc; c.io = &io; c.fs = &fs;
        c.settings = new Settings(amiga_desc.settings, amiga_desc.nsettings, amiga_desc.store, &fs, "", 0);
        engines_of(&c);
        char sum[200], msg[200];
        romset_scan(&c, sum, sizeof(sum));
        RomHit hit;
        CHECK(romset_hit(&c, &hit));
        std::vector<uint8_t> img = kick(262144, 34, 5, 0x13);
        CHECK(romset_put(&c, img.data(), img.size(), msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "read-back") != 0);
        CHECK_STR(c.eng->rs.loaded, "");
    }
    DONE("test_romset");
}
#endif
