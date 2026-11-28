// T116 S5/S9/S10: the ZX Spectrum Next descriptor (cores/zxnext/app/lib/zxnext.cc) against a model of the register block of cores/zxnext/trx/zxn_regs.v:
// the SD image the core starts with (the default file, the notice without one, the image chosen in the menu), a block served to the card model and
// written back into the file, the clock the application gives the machine, the special keys. The descriptor under test is the real one.
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "zxnext_regs.h"
#include "ops_glue.h"
#include "engines.h"

extern const CoreDesc zxnext_desc;              // cores.h lists the shared app's cores; the ZX Spectrum Next is its own app (cores/zxnext/app/lib)

struct ZxnIo : FakeIo {
    bool have_time = false;
    uint8_t tm[6] = { 26, 10, 22, 14, 5, 9 };          // 2026-10-22 14:05:09 (a Thursday)
    bool wall_time(uint8_t t[6]) { if (have_time) memcpy(t, tm, 6); return have_time; }
    uint8_t last_done = 0xEE;                              // what the last DONE write carried
    void on_wr(uint32_t reg, uint8_t v) { if (reg == ZXN_MBOX_DONE) { regs[ZXN_MBOX_STATUS] &= (uint8_t)~1; last_done = v; } }
    void post(bool write, uint32_t lba, uint8_t tag = 0)
    {
        regs[ZXN_MBOX_STATUS] = (uint8_t)(1 | (write ? 2 : 0) | (tag & 0xF0));
        for (int i = 0; i < 4; i++) regs[ZXN_MBOX_LBA + i] = (uint8_t)(lba >> (8 * i));
    }
    bool pending() const { return regs[ZXN_MBOX_STATUS] & 1; }
};

struct ZxnRig {
    ZxnIo io;
    FakeFs *fs;
    CoreCtx c;
    Settings *st;
    PxRec px;
    explicit ZxnRig(FakeFs *f) : fs(f)
    {
        memset(&c, 0, sizeof(c));
        c.desc = &zxnext_desc;
        c.io = &io;
        c.fs = fs;
        st = new Settings(zxnext_desc.settings, zxnext_desc.nsettings, zxnext_desc.store, fs, "", 0);
        st->load();
        c.settings = st;
    }
};

static std::vector<uint8_t> image(size_t sectors, uint8_t seed)
{
    std::vector<uint8_t> v(sectors * 512);
    for (size_t i = 0; i < v.size(); i++) v[i] = (uint8_t)(seed + i * 7 + (i >> 9));
    return v;
}

static int call(ZxnRig &r, const char *op)
{
    OpResult res;
    return ops_call(&r.c, op, 0, 0, &res);
}
static int call_key(ZxnRig &r, int key)
{
    OpResult res;
    OpIn in = in_int("key", key);
    return ops_call(&r.c, "zxnext.fkey", &in, 1, &res);
}
static uint32_t rd32(ZxnIo &io, uint32_t reg) { return io.regs[reg] | io.regs[reg + 1] << 8 | io.regs[reg + 2] << 16 | (uint32_t)io.regs[reg + 3] << 24; }


// ---- T116 D11: stand-ins for the firmware and the card it sits on (no image of the user's is ever in the repository) ----------------------------------
// The board table of TBBLUE.FW as the two real firmwares carry it (read from Alex's images, 2026-11-02): per board <id> "ZX Next Issue <id+2>" 0, then
// the core file name and the version fields, 37 bytes an entry, the table three times in the file. `top` is the newest issue in the table (4 = the
// firmware that stops at Issue 5's id, 5 = the one that takes it). `at` places the first copy (the others follow 9000 and 21000 bytes on); `only`
// writes the first copy only. Between the copies: filler and, when `bait`, the text "ZX Next Issue 5" with a byte in front that is not its id.
static std::vector<uint8_t> fw_standin(int top, size_t size, size_t at, bool only, bool bait)
{
    std::vector<uint8_t> v(size);
    for (size_t i = 0; i < v.size(); i++) v[i] = (uint8_t)(0x13 + i * 5 + (i >> 8));
    if (bait) {
        static const char t[] = "\x20ZX Next Issue 5";
        for (size_t o = 3000; o + sizeof(t) < size; o += 4099) memcpy(&v[o], t, sizeof(t));
    }
    static const char *const file[] = { "core.bit", "core.bit", "core2.bit", "core3.bit", "core4.bit", "core5.bit", "core6.bit", "core7.bit" };
    for (int copy = 0; copy < (only ? 1 : 3); copy++) {
        size_t o = at + (size_t)copy * 9000 + (copy == 2 ? 3000 : 0);
        if (o + 37 * (size_t)(top - 1) > v.size()) break;                 // a copy that does not fit in the file is not written (it was written past the vector's end, 20000 bytes, third copy at 26000)
        for (int id = 0; id <= top - 2; id++, o += 37) {
            uint8_t *e = &v[o];
            memset(e, 0, 37);
            e[0] = (uint8_t)id;
            snprintf((char *)e + 1, 16, "ZX Next Issue %d", id + 2);
            strcpy((char *)e + 17, file[id]);
            e[34] = 0x20; e[35] = 0x10;
        }
    }
    return v;
}

// A card: a FAT volume (optionally behind an MBR) with the firmware in the root directory next to bait that must not be taken (a deleted entry, a long
// name entry, another file) and, when `frag`, in clusters that are not one after the other, with bait clusters between them. Only the part of the
// volume that is used is in the vector (the volume's size in its boot sector is the claimed one).
static void put16(std::vector<uint8_t> &v, size_t o, uint32_t x) { v[o] = (uint8_t)x; v[o + 1] = (uint8_t)(x >> 8); }
static void put32(std::vector<uint8_t> &v, size_t o, uint32_t x) { put16(v, o, x & 0xFFFF); put16(v, o + 2, x >> 16); }
static std::vector<uint8_t> card(const std::vector<uint8_t> &fw, bool fat32, bool mbr, bool frag)
{
    const uint32_t spc = fat32 ? 8 : 4, cs = spc * 512, nclus = fat32 ? 70000 : 20000, rsv = fat32 ? 32 : 4, rootent = fat32 ? 0 : 512;
    const uint32_t fsz = fat32 ? (nclus + 2) * 4 / 512 + 1 : (nclus + 2) * 2 / 512 + 1, rootsec = rootent * 32 / 512;
    const uint32_t first = rsv + 2 * fsz + rootsec, base = mbr ? 2048 : 0;
    const uint32_t nfile = (uint32_t)((fw.size() + cs - 1) / cs), step = frag ? 2 : 1, c0 = fat32 ? 3 : 2;       // FAT32: cluster 2 is the root directory
    const uint32_t last = c0 + (nfile + 1) * step + 2;
    std::vector<uint8_t> v((size_t)(base + first + (size_t)(last - 1) * spc) * 512);
    const size_t B = (size_t)base * 512;
    if (mbr) {
        v[446 + 4] = fat32 ? 0x0C : 0x06;
        put32(v, 446 + 8, base);
        put32(v, 446 + 12, first + nclus * spc);
        v[510] = 0x55; v[511] = 0xAA;
    }
    v[B + 0] = 0xEB; v[B + 2] = 0x90;
    put16(v, B + 11, 512);
    v[B + 13] = (uint8_t)spc;
    put16(v, B + 14, rsv);
    v[B + 16] = 2;
    put16(v, B + 17, rootent);
    if (fat32) {
        put32(v, B + 32, first + nclus * spc);
        put32(v, B + 36, fsz);
        put32(v, B + 44, 2);
    } else {
        put32(v, B + 32, first + nclus * spc);                          // more than 65535 sectors: the 32 bit field
        put16(v, B + 22, fsz);
    }
    v[B + 510] = 0x55; v[B + 511] = 0xAA;
    const size_t fat = B + (size_t)rsv * 512, data = B + (size_t)first * 512, root = B + (size_t)(rsv + 2 * fsz) * 512;
    auto setfat = [&](uint32_t c, uint32_t n) { if (fat32) put32(v, fat + c * 4, n); else put16(v, fat + c * 2, n); };
    const uint32_t eoc = fat32 ? 0x0FFFFFFF : 0xFFFF;
    if (fat32) setfat(2, eoc);
    // the file's clusters c0, c0 + step, ...; the clusters between hold an entry of the table that says Issue 5 (id 3) and are in no chain
    std::vector<uint32_t> ch;
    for (uint32_t i = 0; i < nfile; i++) ch.push_back(c0 + i * step);
    for (uint32_t i = 0; i < nfile; i++) {
        setfat(ch[i], i + 1 < nfile ? ch[i + 1] : eoc);
        size_t n = fw.size() - (size_t)i * cs < cs ? fw.size() - (size_t)i * cs : cs;
        memcpy(&v[data + (size_t)(ch[i] - 2) * cs], &fw[(size_t)i * cs], n);
    }
    if (frag) {
        for (uint32_t i = 0; i < nfile + 1; i++) {
            size_t d = data + (size_t)(c0 + 1 + i * step - 2) * cs;
            v[d] = 3;
            memcpy(&v[d + 1], "ZX Next Issue 5", 16);
        }
    }
    const size_t dir = fat32 ? data : root;
    auto ent = [&](int n, const char *name, uint8_t attr, uint32_t cl, uint32_t size) {
        size_t e = dir + 32 * (size_t)n;
        memcpy(&v[e], name, 11);
        v[e + 11] = attr;
        put16(v, e + 20, cl >> 16);
        put16(v, e + 26, cl & 0xFFFF);
        put32(v, e + 28, size);
    };
    ent(0, "\xE5" "BBLUE  FW ", 0x20, c0 + 1, 40000);                        // a deleted entry of the name, pointing into the bait
    ent(1, "TBBLUE  FW ", 0x10, c0 + 1, 0);                                  // a directory of the name
    ent(2, "TBBLUE  FW ", 0x0F, c0 + 1, 40000);                              // a long name entry (attribute 0x0F) whose bytes happen to read as the name
    ent(3, "TBBLUE  FW ", 0x08, c0 + 1, 40000);                              // the volume label
    ent(4, "TBBLUE  TBU", 0x20, c0 + 1, 100);                                // another file
    ent(5, "TBBLUE  FW ", 0x20, ch[0], (uint32_t)fw.size());
    return v;
}

// The Auto choice for a card: the id the register holds after the settings are applied while the core is held (0xEE: the register was not written).
struct AutoRes { int id; bool notice; std::string text; };
static AutoRes auto_run(const std::vector<uint8_t> *img, int setting_value = 0)
{
    FakeFs fs;
    if (img) fs.put("/usb1/roms/zxnext/next.img", *img);
    ZxnRig r(&fs);
    r.io.regs[ZXN_FEAT] = 0x01;
    char msg[200];
    zxnext_desc.init(&r.c, msg, sizeof(msg));
    r.c.notice_pending = false;
    r.c.notice[0] = 0;
    int bi = -1;
    for (int i = 0; i < zxnext_desc.nsettings; i++) if (!strcmp(zxnext_desc.settings[i].key, "Board issue")) bi = i;
    r.st->set(bi, setting_value);
    r.io.regs[ZXN_BOARD_ID] = 0xEE;
    boot_apply_settings(&r.c, false);
    return { r.io.regs[ZXN_BOARD_ID], r.c.notice_pending, r.c.notice };
}
static AutoRes auto_run(const std::vector<uint8_t> &img, int setting_value = 0) { return auto_run(&img, setting_value); }
static bool read_file(const char *path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    static uint8_t b[65536];
    size_t n;
    out.clear();
    while ((n = fread(b, 1, sizeof(b), f)) > 0) out.insert(out.end(), b, b + n);
    fclose(f);
    return true;
}

int main()
{
    // --- no image: the core still starts (its boot ROM shows its own error), and the application says what to do
    {
        FakeFs fs;
        ZxnRig r(&fs);
        char msg[200];
        CHECK_EQ(zxnext_desc.init(&r.c, msg, sizeof(msg)), 0);
        CHECK(r.c.notice_pending && strstr(r.c.notice, "next.img") && strstr(r.c.notice, "roms/zxnext"));
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 0);
        CHECK_EQ(r.io.log.front().reg, ZXN_CTRL);
        CHECK_EQ(r.io.log.front().v, ZXN_HOLD);
        CHECK_EQ(rd32(r.io, ZXN_RAM_BASE), 0x01000000u + ZXN_W_RAM);
        CHECK_EQ(rd32(r.io, ZXN_STAGE_BASE), 0x01000000u + ZXN_W_STAGE);
    }
    // --- the default image: roms/zxnext/next.img on the first root that has it, mounted with its size in sectors
    {
        FakeFs fs;
        fs.put("/usb1/roms/zxnext/next.img", image(4096, 3));
        ZxnRig r(&fs);
        r.io.regs[ZXN_FEAT] = 0x01;
        char msg[200];
        CHECK_EQ(zxnext_desc.init(&r.c, msg, sizeof(msg)), 0);
        CHECK(!r.c.notice_pending);
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 1);
        CHECK_EQ(rd32(r.io, ZXN_SD_MOUNT + 4), 4096u);
        CHECK_STR(sec_name(&r.c, 0), "next.img");

        // the card asks for block 5: the file's bytes arrive in the staging sector; the Next writes block 9: they go into the file
        std::vector<uint8_t> want = image(4096, 3);
        r.io.post(false, 5, 0xA0);
        CHECK_EQ(sec_service(&r.c, 16), 1);
        CHECK(!r.io.pending());
        CHECK_EQ(r.io.last_done, 0xA0);                            // the answer names the request it read (board B: a stale answer must not answer the request that replaced it)
        CHECK(!memcmp(&r.io.ddr[ZXN_W_STAGE], &want[5 * 512], 512));
        for (int i = 0; i < 512; i++) r.io.ddr[ZXN_W_STAGE + i] = (uint8_t)(0x40 + i);
        r.io.post(true, 9, 0x30);
        CHECK_EQ(sec_service(&r.c, 16), 1);
        CHECK_EQ(r.io.last_done, 0x30);
        const std::vector<uint8_t> &f = fs.files["/usb1/roms/zxnext/next.img"];
        CHECK_EQ(f[9 * 512], 0x40);
        CHECK_EQ(f[9 * 512 + 511], (uint8_t)(0x40 + 511));
        CHECK(!memcmp(&f[8 * 512], &want[8 * 512], 512) && !memcmp(&f[10 * 512], &want[10 * 512], 512));    // its neighbours are as they were
        // the service call is one op: the mailbox, the clock
        zxnext_desc.release(&r.c, msg, sizeof(msg));
        CHECK_EQ(r.io.regs[ZXN_CTRL], 0);
    }
    // --- T148: with a .cor path the image of the core's own folder is the first place; the old roots stay as the place of an image the person keeps
    {
        FakeFs fs;
        fs.put("/usb1/roms/zxnext/next.img", image(2048, 5));
        fs.put("/SD/cores/zxnext/roms/next.img", image(4096, 3));
        ZxnRig r(&fs);
        strcpy(r.c.cor_path, "/SD/cores/zxnext.cor");
        r.io.regs[ZXN_FEAT] = 0x01;
        char msg[200];
        CHECK_EQ(zxnext_desc.init(&r.c, msg, sizeof(msg)), 0);
        CHECK_EQ(rd32(r.io, ZXN_SD_MOUNT + 4), 4096u);                 // the own folder's, not the stick's
        FakeFs f2;
        ZxnRig r2(&f2);
        strcpy(r2.c.cor_path, "/SD/cores/zxnext.cor");
        CHECK_EQ(zxnext_desc.init(&r2.c, msg, sizeof(msg)), 0);
        CHECK(r2.c.notice_pending && strstr(r2.c.notice, "/SD/cores/zxnext/roms/") && !strstr(r2.c.notice, "roms/zxnext on"));
    }
    // --- the stick comes up after the start (board, 2026-10-23: "Error initializing SD card!"): nothing at init, the notice once, then the service
    //     looks again once a second, mounts the image and starts the machine over with it; a choice of the user is never overridden
    {
        FakeFs fs;
        ZxnRig r(&fs);
        r.io.regs[ZXN_FEAT] = 0x01;
        char msg[200];
        CHECK_EQ(zxnext_desc.init(&r.c, msg, sizeof(msg)), 0);
        zxnext_desc.release(&r.c, msg, sizeof(msg));
        r.c.started = true;
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 0);
        r.c.notice_pending = false;
        r.io.delay_ms(1100);
        call(r, "zxnext.service");                                 // a look, nothing there: no card, no reset, no second notice
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 0);
        CHECK(!r.c.notice_pending);
        fs.put("/usb1/roms/zxnext/next.img", image(4096, 3));      // the stick is mounted now
        size_t before = r.io.log.size();
        call(r, "zxnext.service");                                 // less than a second since the last look: it waits
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 0);
        r.io.delay_ms(1100);
        call(r, "zxnext.service");
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 1);
        CHECK_EQ(rd32(r.io, ZXN_SD_MOUNT + 4), 4096u);
        CHECK_STR(sec_name(&r.c, 0), "next.img");
        int hold = -1, rel = -1;                                   // the machine started over: held with the card announced, then released
        for (size_t i = before; i < r.io.log.size(); i++) {
            if (r.io.log[i].reg == ZXN_CTRL && r.io.log[i].v == ZXN_HOLD && hold < 0) hold = (int)i;
            if (r.io.log[i].reg == ZXN_CTRL && r.io.log[i].v == 0) rel = (int)i;
        }
        CHECK(hold >= 0 && rel > hold);
        // settled: no further look, no further reset
        size_t after = r.io.log.size();
        r.io.delay_ms(1100);
        call(r, "zxnext.service");
        for (size_t i = after; i < r.io.log.size(); i++) CHECK(r.io.log[i].reg != ZXN_CTRL);
        // an ejected image stays ejected over a Reset and over the service
        call(r, "machine.cold_reset");
        OpResult res;
        OpIn in = in_int("unit", 0);
        ops_call(&r.c, "sectors.eject", &in, 1, &res);
        call(r, "machine.cold_reset");
        r.io.delay_ms(1100);
        call(r, "zxnext.service");
        CHECK_EQ(r.io.regs[ZXN_SD_MOUNT], 0);
    }
    {
        FakeFs fs;
        ZxnRig r(&fs);
        r.io.regs[ZXN_FEAT] = 0x01;
        char msg[200];
        zxnext_desc.init(&r.c, msg, sizeof(msg));
        r.c.started = true;
        fs.put("/usb0/other.img", image(2048, 9));
        OpIn in[2] = { in_str("path", "/usb0/other.img"), in_int("unit", 0) };
        OpResult res;
        CHECK_EQ(ops_call(&r.c, "sectors.mount", in, 2, &res), 0);   // the user chose an image (Mount, no reset) ...
        fs.put("/usb1/roms/zxnext/next.img", image(4096, 3));
        size_t before = r.io.log.size();
        r.io.delay_ms(1100);
        call(r, "zxnext.service");                                 // ... and the default does not replace it
        CHECK_STR(sec_name(&r.c, 0), "other.img");
        for (size_t i = before; i < r.io.log.size(); i++) CHECK(r.io.log[i].reg != ZXN_CTRL);
    }
    // --- the clock: the application's time in BCD, the weekday (1 = Sunday); without a clock a stamp that moves on
    {
        FakeFs fs;
        ZxnRig r(&fs);
        r.io.have_time = true;
        call(r, "zxnext.service");
        static const uint8_t want[7] = { 0x09, 0x05, 0x14, 5, 0x22, 0x10, 0x26 };
        for (int i = 0; i < 7; i++) CHECK_EQ(r.io.regs[ZXN_RTC + i], want[i]);
        r.io.have_time = false;
        r.io.delay_ms(1500);
        call(r, "zxnext.service");
        CHECK_EQ(r.io.regs[ZXN_RTC + 6], 0x26);
        CHECK_EQ(r.io.regs[ZXN_RTC + 0], 0x01);        // 1 s of uptime
    }
    // --- the special keys: F3 held for 60 ms, then let go; the Multiface button the same
    {
        FakeFs fs;
        ZxnRig r(&fs);
        call_key(r, ZXN_FKEY_F8);
        int down = -1, up = -1;
        for (size_t i = 0; i < r.io.log.size(); i++) {
            if (r.io.log[i].reg == ZXN_FKEYS && r.io.log[i].v == (1 << (ZXN_FKEY_F8 & 7))) down = (int)i;
            if (r.io.log[i].reg == ZXN_FKEYS && r.io.log[i].v == 0) up = (int)i;
        }
        CHECK(down >= 0 && up > down);
        CHECK(r.io.log[up].t - r.io.log[down].t >= 60);
        call(r, "zxnext.nmi_m1");
        CHECK_EQ(r.io.regs[ZXN_BUTTONS], 0);
    }
    // --- T116 D10, D11: the board issue the machine reports (NextReg 0x0F) is the BOARD_ID register, written with the settings while the core is held:
    // Auto (the default) takes it from the firmware on the image, "Issue 5" is id 3, "Issue 4" is id 2, "Issue 2" is id 0; a live setting write does not touch it
    {
        FakeFs fs;
        ZxnRig r(&fs);
        int bi = -1;
        for (int i = 0; i < zxnext_desc.nsettings; i++) if (!strcmp(zxnext_desc.settings[i].key, "Board issue")) bi = i;
        CHECK(bi >= 0);
        CHECK_EQ(zxnext_desc.settings[bi].when, WHEN_RESET);
        CHECK_STR(zxnext_desc.settings[bi].values[zxnext_desc.settings[bi].def], "Auto");     // a fresh .cfg: Auto
        CHECK_STR(zxnext_desc.settings[bi].values[1], "Issue 2");
        CHECK_STR(zxnext_desc.settings[bi].values[2], "Issue 4");
        CHECK_STR(zxnext_desc.settings[bi].values[3], "Issue 5");
        CHECK_EQ(r.st->value(bi), zxnext_desc.settings[bi].def);
        r.io.regs[ZXN_BOARD_ID] = 0xEE;
        boot_apply_settings(&r.c, false);                  // Auto, no image: issue 5
        CHECK_EQ(r.io.regs[ZXN_BOARD_ID], 3);
        Settings s2(zxnext_desc.settings, zxnext_desc.nsettings, zxnext_desc.store, &fs, "/usb1/zxnext.cfg", 0);
        r.c.settings = &s2;
        CHECK(s2.set(bi, 2));
        boot_apply_settings(&r.c, false);
        CHECK_EQ(r.io.regs[ZXN_BOARD_ID], 2);
        {   // stored: a new start reads the choice back
            Settings s3(zxnext_desc.settings, zxnext_desc.nsettings, zxnext_desc.store, &fs, "/usb1/zxnext.cfg", 0);
            s3.load();
            CHECK_STR(zxnext_desc.settings[bi].values[s3.value(bi)], "Issue 4");
        }
        // a .cfg of the version before Auto (its values are labels): the choice it names is the one that is used
        std::string cfg = fs.text("/usb1/zxnext.cfg");
        CHECK(cfg.find("Issue 4") != std::string::npos);
        static const char *const old_labels[3] = { "Issue 2", "Issue 4", "Issue 5" };
        static const int want[3] = { 0, 2, 3 };
        for (int k = 0; k < 3; k++) {
            std::string t = cfg;
            t.replace(t.find("Issue 4"), 7, old_labels[k]);
            fs.put_text("/usb1/old.cfg", t);
            Settings s4(zxnext_desc.settings, zxnext_desc.nsettings, zxnext_desc.store, &fs, "/usb1/old.cfg", 0);
            s4.load();
            r.c.settings = &s4;
            r.io.regs[ZXN_BOARD_ID] = 0xEE;
            boot_apply_settings(&r.c, true);
            CHECK_EQ(r.io.regs[ZXN_BOARD_ID], want[k]);
        }
        r.c.settings = &s2;
        CHECK(s2.set(bi, 1));
        r.io.regs[ZXN_BOARD_ID] = 0xEE;
        boot_apply_settings(&r.c, true);
        CHECK_EQ(r.io.regs[ZXN_BOARD_ID], 0);
    }
    // --- T116 D11: Auto reads the firmware's board table from the mounted image. Stand-ins that reproduce what the two real firmwares carry (ids 0..3,
    // "ZX Next Issue 2".."5": takes Issue 5; ids 0..2: stops at "Unknown board id 0x03") on FAT32 and FAT16 cards, behind an MBR or bare, in one run of
    // clusters or scattered.
    {
        std::vector<uint8_t> fnew = fw_standin(5, 40000, 6000, false, true), fold = fw_standin(4, 40000, 6000, false, true);
        struct { bool fat32, mbr, frag; } geo[] = { { true, true, false }, { true, true, true }, { false, true, false }, { false, false, true }, { true, false, true } };
        for (auto &g : geo) {
            AutoRes n = auto_run(card(fnew, g.fat32, g.mbr, g.frag));
            CHECK_EQ(n.id, 3);
            CHECK(!n.notice);
            AutoRes o = auto_run(card(fold, g.fat32, g.mbr, g.frag));
            CHECK_EQ(o.id, 2);                              // the old firmware: Issue 4's id; "ZX Next Issue 5" in its text (a byte in front that is not 3) does not count
            CHECK(!o.notice);
        }
        // a table that straddles a read boundary of the scan: the one copy of the table whose Issue 5 entry has its id byte 8 bytes before a 2048 boundary,
        // and the one that crosses a cluster boundary on a scattered card
        for (size_t at : { (size_t)(2048 * 3 - 37 * 3 - 8), (size_t)(4096 * 5 - 37 * 3 - 8) }) {
            std::vector<uint8_t> one = fw_standin(5, 30000, at, true, false);
            CHECK_EQ(auto_run(card(one, true, true, at > 8192)).id, 3);
            CHECK_EQ(auto_run(card(one, false, true, true)).id, 3);
        }
        CHECK_EQ(auto_run(card(fw_standin(3, 20000, 5000, false, false), true, true, false)).id, 1);      // a firmware that knows Issue 3 as its newest
        CHECK_EQ(auto_run(card(fw_standin(2, 20000, 5000, false, false), true, true, false)).id, 0);
        CHECK_EQ(auto_run(card(fw_standin(6, 20000, 5000, false, false), true, true, false)).id, 3);      // a later firmware (Issue 6): the machine has issue 5's structure
        // an image Auto cannot place: Issue 5 and a notice that says so
        AutoRes x = auto_run(image(4096, 9));
        CHECK_EQ(x.id, 3);
        CHECK(x.notice && strstr(x.text.c_str(), "Board issue Auto") && strstr(x.text.c_str(), "no TBBLUE.FW") && strstr(x.text.c_str(), "Issue 5"));
        std::vector<uint8_t> junk(30000);
        for (size_t i = 0; i < junk.size(); i++) junk[i] = (uint8_t)(i * 31 + (i >> 7));
        x = auto_run(card(junk, true, true, false));
        CHECK_EQ(x.id, 3);
        CHECK(x.notice && strstr(x.text.c_str(), "no board table in TBBLUE.FW"));
        x = auto_run(nullptr);                              // no image at all: the notice about that was given at the start, none about the issue
        CHECK_EQ(x.id, 3);
        CHECK(!x.notice);
        // a fixed choice wins over the firmware, either way (1 = Issue 2, 2 = Issue 4, 3 = Issue 5)
        CHECK_EQ(auto_run(card(fnew, true, true, false), 1).id, 0);
        CHECK_EQ(auto_run(card(fnew, true, true, false), 2).id, 2);
        CHECK_EQ(auto_run(card(fold, true, true, false), 3).id, 3);        // Issue 5 on the old firmware: as chosen, not corrected
        CHECK_EQ(auto_run(card(fold, true, true, false), 0).id, 2);
        // local only (never in the repository): the real firmware files and, when given, the first part of the real cards, from the environment
        const char *rn = getenv("ZXN_REAL_FW_NEW"), *ro = getenv("ZXN_REAL_FW_OLD");
        std::vector<uint8_t> f;
        if (rn && read_file(rn, f)) { printf("real TBBLUE.FW (new): %zu bytes\n", f.size()); CHECK_EQ(auto_run(card(f, true, true, false)).id, 3); CHECK_EQ(auto_run(card(f, true, true, true)).id, 3); }
        if (ro && read_file(ro, f)) { printf("real TBBLUE.FW (old): %zu bytes\n", f.size()); CHECK_EQ(auto_run(card(f, true, true, false)).id, 2); CHECK_EQ(auto_run(card(f, true, true, true)).id, 2); }
        rn = getenv("ZXN_REAL_IMG_NEW"); ro = getenv("ZXN_REAL_IMG_OLD");
        if (rn && read_file(rn, f)) { printf("real card (new), first %zu bytes\n", f.size()); AutoRes q = auto_run(f); CHECK_EQ(q.id, 3); CHECK(!q.notice); }
        if (ro && read_file(ro, f)) { printf("real card (old), first %zu bytes\n", f.size()); AutoRes q = auto_run(f); CHECK_EQ(q.id, 2); CHECK(!q.notice); }
    }
    DONE("test_zxnext");
}
