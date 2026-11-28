// T104 5.1, 5.2 `rom_set`, 5.7: the Kickstart as a ROM set - scanned out of a directory, chosen by the setting or by Auto (which reads the version
// out of the image, ADR-0056), placed in the window (a 256 KB ROM twice), reloaded when the choice changes, with the machine's RAM cleared on a
// cold start. The record is the Amiga's and the A1200's (cores/amiga/app/lib/amiga.cc, cores/amiga1200/app/lib/amiga1200.cc); the code under test is romset.cc, kickstart.cc and boot.cc.
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
