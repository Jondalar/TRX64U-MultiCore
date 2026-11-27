// T148 S1-S4, S7: the core folder - made at the first start, the settings moved into it, ROMs read from it alone and copied there (verified)
// from the old places, one path in the message, a path for saves. The file system is the fake.
#include <string>
#include "check.h"
#include "fakes.h"
#ifdef TEST_APP_AMIGA                    // T239.5 D13: the Amiga 500's case (the Kickstart set) runs as its app (run.sh amigaapp, unit amiga_corefolder); the shared unit corefolder keeps S1-S4
#include "amiga_model.h"
#else
#include "ctx.h"
#include "cores.h"
#include "video.h"
#endif
#include "corefolder.h"
#include "romfind.h"
#include "romset.h"
#include "engines.h"
#include "start.h"
#include "handover.h"

const CoreDesc *const core_table[] = { 0 };           // start.cc looks cores up here; this test chooses none

static const RomSpec specs[2] = {
    { "XL OS", { "DUMMYXL.ROM", 0 },               { 16384, 0 }, true,  0, 0 },
    { "BASIC", { "BASIC.ROM", "DUMMYBAS.ROM", 0 }, { 8192, 0 },  true,  0, 0 },
};
static const char *COR = "/SD/cores/dummy800.cor";
static const char *OWN = "/SD/cores/dummy800/roms";

// a medium whose copy comes back damaged: the stick lost a bit between the write and the read
struct BadFs : FakeFs {
    bool bad = false;
    bool save(const char *path, const void *buf, uint32_t len)
    {
        if (!bad) return FakeFs::save(path, buf, len);
        std::vector<uint8_t> v((const uint8_t *)buf, (const uint8_t *)buf + len);
        v[len / 2] ^= 0x10;
        return FakeFs::save(path, v.data(), len) ;
    }
};

// The first byte of a file, -1 when it is missing or empty: a defect that copies nothing has to give a red check, not a crash.
static int first_byte(FakeFs &fs, const char *path)
{
    auto it = fs.files.find(path);
    return it == fs.files.end() || it->second.empty() ? -1 : it->second[0];
}

static RomStatus start_scan(CoreFs *fs, const char *cor, RomResult *res, char *sum, int sl, const char *first = 0)
{
    char own[96], old[96];
    bool have = rom_own_dir(cor, own, sizeof(own));
    rom_import(fs, "dummy800", specs, 2, have ? own : 0, first, corefolder_old_roms(cor, old, sizeof(old)) ? old : 0);
    return rom_scan(fs, "dummy800", specs, 2, res, sum, sl, first, have ? own : 0);
}

int main()
{
#ifndef TEST_APP_AMIGA
    char b[260];
    RomResult res[2];
    char sum[300];

    // ---- the names (D1, D5)
    CHECK(corefolder_dir("/SD/cores/apple2.cor", b, sizeof(b)));
    CHECK_STR(b, "/SD/cores/apple2");
    CHECK(corefolder_sub("/SD/cores/apple2.cor", "roms", b, sizeof(b)));
    CHECK_STR(b, "/SD/cores/apple2/roms");
    CHECK(corefolder_cfg("/SD/cores/apple2.cor", b, sizeof(b)));
    CHECK_STR(b, "/SD/cores/apple2/apple2.cfg");
    CHECK(corefolder_old_cfg("/SD/cores/apple2.cor", b, sizeof(b)));
    CHECK_STR(b, "/SD/cores/apple2.cfg");
    CHECK(corefolder_old_roms("/SD/cores/apple2.cor", b, sizeof(b)));
    CHECK_STR(b, "/SD/cores/roms");
    CHECK(corefolder_dir("/USB1/APPLE2.COR", b, sizeof(b)));                  // a .cor at a medium's top has its folder there
    CHECK_STR(b, "/USB1/APPLE2");
    CHECK(!corefolder_dir("/x.cor", b, sizeof(b)));                           // no medium
    CHECK(!corefolder_dir("", b, sizeof(b)));
    CHECK(!corefolder_dir("cores/x.cor", b, sizeof(b)));
    CHECK(!corefolder_dir("/SD/cores/x.bin", b, sizeof(b)));
    CHECK(!corefolder_dir("/SD/cores/.cor", b, sizeof(b)));
    CHECK(!corefolder_dir("/SD/cores/apple2.cor", b, 12));                    // does not fit: refused, not cut
    CHECK_STR(b, "");

    // ---- S7: the saves path
    CHECK(corefolder_save_path("/SD/cores/trx64.cor", "snap1.c64re", b, sizeof(b)));
    CHECK_STR(b, "/SD/cores/trx64/saves/snap1.c64re");
    CHECK(!corefolder_save_path("/SD/cores/trx64.cor", "../x", b, sizeof(b)));
    CHECK(!corefolder_save_path("/SD/cores/trx64.cor", "", b, sizeof(b)));
    CHECK(!corefolder_save_path("/SD/cores/trx64.cor", 0, b, sizeof(b)));
    CHECK(!corefolder_save_path("", "a.sav", b, sizeof(b)));

    // ---- S1: the first start makes the folder, roms/ and saves/; the settings path is in it
    {
        FakeFs fs;
        fs.put_size("/SD/cores/apple2.cor", 10);
        CHECK(corefolder_ensure(&fs, "/SD/cores/apple2.cor"));
        uint64_t sz; bool dir;
        CHECK(fs.stat("/SD/cores/apple2", &sz, &dir) && dir);
        CHECK(fs.stat("/SD/cores/apple2/roms", &sz, &dir) && dir);
        CHECK(fs.stat("/SD/cores/apple2/saves", &sz, &dir) && dir);
        CHECK(corefolder_ensure(&fs, "/SD/cores/apple2.cor"));                 // again: nothing breaks
        FakeFs ro;
        ro.read_only = true;
        CHECK(!corefolder_ensure(&ro, "/SD/cores/apple2.cor"));                // a write protected card: told, not crashed
        CHECK(!corefolder_ensure(&fs, ""));
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        Handover h;
        handover_fill(&h, "/SD/cores/apple2.cor", "Apple IIe", "1.0.0", 1);
        start_paths(&c, &h);
        CHECK_STR(c.cfg_path, "/SD/cores/apple2/apple2.cfg");
        // the old .cfg beside the .cor moves once, and is gone from there
        fs.put_text("/SD/cores/apple2.cfg", "[Apple]\nx=1\n");
        CHECK(corefolder_move_cfg(&fs, "/SD/cores/apple2.cor"));
        CHECK_STR(fs.text("/SD/cores/apple2/apple2.cfg").c_str(), "[Apple]\nx=1\n");
        CHECK(!fs.has("/SD/cores/apple2.cfg"));
        CHECK(!corefolder_move_cfg(&fs, "/SD/cores/apple2.cor"));              // nothing left to move
        // a new .cfg that exists is never overwritten by an old one that turns up again
        fs.put_text("/SD/cores/apple2.cfg", "[Apple]\nx=OLD\n");
        CHECK(!corefolder_move_cfg(&fs, "/SD/cores/apple2.cor"));
        CHECK_STR(fs.text("/SD/cores/apple2/apple2.cfg").c_str(), "[Apple]\nx=1\n");
        CHECK(fs.has("/SD/cores/apple2.cfg"));
        // a card that fails the write: the old file stays
        BadFs bf;
        bf.put_text("/SD/cores/apple2.cfg", "[Apple]\nx=1\n");
        bf.bad = true;
        CHECK(!corefolder_move_cfg(&bf, "/SD/cores/apple2.cor"));
        CHECK(bf.has("/SD/cores/apple2.cfg") && !bf.has("/SD/cores/apple2/apple2.cfg"));
    }

    // ---- S2: a ROM only in an old place is copied once, byte-identical, the original stays; then nothing outside the folder is read
    {
        FakeFs fs;
        std::vector<uint8_t> xl(16384), ba(8192);
        for (size_t i = 0; i < xl.size(); i++) xl[i] = (uint8_t)(i * 7 + 1);
        for (size_t i = 0; i < ba.size(); i++) ba[i] = (uint8_t)(i * 13 + 5);
        fs.put("/sd/roms/dummy800/DummyXL.rom", xl);
        fs.put("/USB1/roms/dummy800/dummybas.rom", ba);
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_FOUND);
        CHECK_STR(res[0].hit.dir, OWN);
        CHECK_STR(res[1].hit.dir, OWN);
        CHECK(fs.files["/sd/cores/dummy800/roms/dummyxl.rom"] == xl);
        CHECK(fs.files["/sd/cores/dummy800/roms/dummybas.rom"] == ba);
        CHECK(fs.files["/sd/roms/dummy800/dummyxl.rom"] == xl);                // the original is untouched
        CHECK(fs.files["/usb1/roms/dummy800/dummybas.rom"] == ba);
        int w = fs.writes;
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_FOUND);       // next start: nothing is copied
        CHECK_EQ(fs.writes, w);
        // ... and the old place is not read any more: take it away and the core still runs; take the copy away and it does not
        fs.remove("/sd/roms/dummy800/DummyXL.rom");
        fs.remove("/USB1/roms/dummy800/dummybas.rom");
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_FOUND);
        FakeFs only_old;
        only_old.put("/sd/roms/dummy800/dummyxl.rom", xl);
        only_old.put("/sd/roms/dummy800/basic.rom", ba);
        CHECK_EQ(rom_scan(&only_old, "dummy800", specs, 2, res, sum, sizeof(sum), 0, OWN), ROM_MISSING);     // never used in place
        CHECK(only_old.writes == 0);
    }
    // the order of the old places is the old order: the flash, then the sticks, then the card; the board's folder first of all
    {
        FakeFs fs;
        fs.put_size("/sd/roms/dummy800/dummyxl.rom", 16384, 3);
        fs.put_size("/usb1/roms/dummy800/dummyxl.rom", 16384, 2);
        fs.put_size("/flash/roms/dummy800/dummyxl.rom", 16384, 1);
        fs.put_size("/board/basic.rom", 8192, 9);
        fs.put_size("/sd/roms/dummy800/basic.rom", 8192, 4);
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum), "/board"), ROM_FOUND);
        CHECK_EQ(first_byte(fs, "/sd/cores/dummy800/roms/dummyxl.rom"), 1);
        CHECK_EQ(first_byte(fs, "/sd/cores/dummy800/roms/basic.rom"), 9);
    }
    // the roms folder beside the cores is a source only for a file the spec NAMES with an allowed size (the 2026-10-28 case)
    {
        FakeFs fs;
        fs.put_size("/SD/cores/roms/apple2e-video.rom", 8192, 1);              // another machine's file, of a size this core knows
        fs.put_size("/SD/cores/roms/dummyxl.rom", 100, 2);                     // the right name, a wrong size
        fs.put_size("/SD/cores/roms/basic.rom", 8192, 3);                      // the right name and size
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_MISSING);
        CHECK(fs.has("/SD/cores/dummy800/roms/basic.rom"));
        CHECK(!fs.has("/SD/cores/dummy800/roms/dummyxl.rom"));
        CHECK(!fs.has("/SD/cores/dummy800/roms/apple2e-video.rom"));
    }
    // a name that lies in the folder is never overwritten, not even one of the wrong size; the message says what is wrong there
    {
        FakeFs fs;
        fs.put_size("/SD/cores/dummy800/roms/dummyxl.rom", 100, 5);
        fs.put_size("/sd/roms/dummy800/dummyxl.rom", 16384, 6);
        fs.put_size("/sd/roms/dummy800/basic.rom", 8192, 6);
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_WRONG_SIZE);
        CHECK_EQ(fs.files["/sd/cores/dummy800/roms/dummyxl.rom"].size(), 100);
        CHECK(strstr(sum, "dummyxl.rom") && strstr(sum, "/SD/cores/dummy800/roms") && !strstr(sum, "/sd/roms"));
    }
    // a copy that does not verify is removed and not used (the card lost a bit)
    {
        BadFs fs;
        fs.put_size("/sd/roms/dummy800/dummyxl.rom", 16384, 6);
        fs.put_size("/sd/roms/dummy800/basic.rom", 8192, 6);
        fs.bad = true;
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_MISSING);
        CHECK(!fs.has("/SD/cores/dummy800/roms/dummyxl.rom") && !fs.has("/SD/cores/dummy800/roms/basic.rom"));
        fs.bad = false;
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_FOUND);       // the next pass copies it right
    }
    // a content check applies before the copy: a file the core refuses is not copied
    {
        static const RomSpec chk[1] = { { "Checked", { "A.ROM", 0 }, { 4, 0 }, true, [](const uint8_t *d, uint32_t) { return d[0] == 0x42; }, 0 } };
        FakeFs fs;
        fs.put("/usb0/roms/x/a.rom", std::vector<uint8_t>{1, 2, 3, 4});
        rom_import(&fs, "x", chk, 1, "/SD/cores/x/roms", 0, 0);
        CHECK(!fs.has("/SD/cores/x/roms/a.rom"));
        fs.put("/sd/roms/x/a.rom", std::vector<uint8_t>{0x42, 2, 3, 4});
        rom_import(&fs, "x", chk, 1, "/SD/cores/x/roms", 0, 0);
        CHECK(fs.has("/SD/cores/x/roms/a.rom"));
    }

    // ---- S3: a device ROM (shared) is copied per core from <root>/roms, a machine ROM is not
    {
        static const RomSpec drv[2] = {
            { "Drive",  { "d1541.rom", 0 }, { 4, 0 }, true, 0, 0, 0, 1 },
            { "KERNAL", { "kernal.rom", 0 }, { 4, 0 }, true, 0, 0, 0, 0 },
        };
        FakeFs fs;
        fs.put_size("/usb0/roms/d1541.rom", 4, 1);
        fs.put_size("/usb0/roms/kernal.rom", 4, 2);                             // the C64's KERNAL in the shared folder is not this core's
        char own[96];
        CHECK(rom_own_dir("/SD/cores/dummy264.cor", own, sizeof(own)));
        CHECK_EQ(rom_import(&fs, "dummy264", drv, 2, own, 0, 0), 1);
        CHECK(fs.has("/SD/cores/dummy264/roms/d1541.rom") && !fs.has("/SD/cores/dummy264/roms/kernal.rom"));
        RomResult r2[2];
        CHECK_EQ(rom_scan(&fs, "dummy264", drv, 2, r2, sum, sizeof(sum), 0, own), ROM_MISSING);
        CHECK_STR(r2[0].hit.dir, own);
        FakeFs f3;                                                              // the scan never reads the shared folder, only the import does
        f3.put_size("/usb0/roms/d1541.rom", 4, 1);
        CHECK_EQ(rom_scan(&f3, "dummy264", drv, 1, r2, sum, sizeof(sum), 0, own), ROM_MISSING);
        // a second core gets its own copy
        CHECK(rom_own_dir("/SD/cores/dummy128.cor", own, sizeof(own)));
        CHECK_EQ(rom_import(&fs, "dummy128", drv, 1, own, 0, 0), 1);
        CHECK(fs.has("/SD/cores/dummy128/roms/d1541.rom"));
        CHECK_EQ(rom_scan(&fs, "dummy264", drv, 1, r2, sum, sizeof(sum), 0, own), ROM_FOUND);
        CHECK_STR(r2[0].hit.dir, "/SD/cores/dummy128/roms");
    }

    // ---- S4: a missing ROM names one place, and only that
    {
        FakeFs fs;
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_MISSING);
        CHECK_STR(sum, "XL OS: DUMMYXL.ROM is missing in /SD/cores/dummy800/roms/");
        CHECK(!strstr(sum, "/flash") && !strstr(sum, "/usb") && !strstr(sum, "/sd/roms"));
        fs.put_size("/SD/cores/dummy800/roms/dummyxl.rom", 16384);
        CHECK_EQ(start_scan(&fs, COR, res, sum, sizeof(sum)), ROM_MISSING);
        CHECK_STR(sum, "BASIC: BASIC.ROM or DUMMYBAS.ROM is missing in /SD/cores/dummy800/roms/");
        // no .cor path (a developer start): the old search and the old message remain
        FakeFs fd;
        CHECK_EQ(start_scan(&fd, "", res, sum, sizeof(sum)), ROM_MISSING);
        CHECK(strstr(sum, "roms/dummy800") != 0);
    }

#else
    // ---- the ROM set (Kickstart): the core folder alone, copied from the old places by extension and size, never from the shared folder
    {
        AmigaRig *r;
        FakeFs fs;
        r = new AmigaRig(&fs, &amiga_desc);
        Video v(amiga_desc.video, &r->px, &r->io);
        r->c.video = &v;
        strcpy(r->c.cor_path, "/SD/cores/amiga.cor");
        std::vector<uint8_t> k(262144, 0x13);
        k[0] = 0x11; k[1] = 0x11; k[12] = 0; k[13] = 34; k[14] = 0; k[15] = 5;
        fs.put("/usb0/roms/amiga/kick13.rom", k);
        fs.put("/SD/cores/roms/apple2e-video.rom", std::vector<uint8_t>(262144, 7));    // the shared folder of the 2026-10-28 case
        char s[240];
        CHECK_EQ(romset_scan(&r->c, s, sizeof(s)), ROM_FOUND);
        CHECK_STR(r->c.eng->rs.dir, "/SD/cores/amiga/roms");
        CHECK_EQ(r->c.eng->rs.count, 1);
        CHECK_STR(r->c.eng->rs.m[0].name, "kick13.rom");
        CHECK(fs.files["/sd/cores/amiga/roms/kick13.rom"] == k);
        CHECK(!fs.has("/SD/cores/amiga/roms/apple2e-video.rom"));
        // only the shared folder: never taken
        FakeFs f2;
        AmigaRig *r2 = new AmigaRig(&f2, &amiga_desc);
        Video v2(amiga_desc.video, &r2->px, &r2->io);
        r2->c.video = &v2;
        strcpy(r2->c.cor_path, "/SD/cores/amiga.cor");
        f2.put("/SD/cores/roms/kick13.rom", k);
        f2.put("/SD/cores/roms/apple2e-video.rom", std::vector<uint8_t>(262144, 7));
        CHECK_EQ(romset_scan(&r2->c, s, sizeof(s)), ROM_MISSING);
        CHECK(strstr(s, "/SD/cores/amiga/roms/") != 0 && !strstr(s, "/usb"));
    }
#endif
    DONE("test_corefolder");
}
