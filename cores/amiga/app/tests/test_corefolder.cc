// T148 S1-S4, S7: the core folder - made at the first start, the settings moved into it, ROMs read from it alone and copied there (verified)
// from the old places, one path in the message, a path for saves. The file system is the fake.
#include <string>
#include "check.h"
#include "fakes.h"
#include "amiga_model.h"
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
    DONE("test_corefolder");
}
