// T111 S4-S8 and the cancel (Q6): the format as the board runs it, over a sparse in-memory medium. Real code all the way: mediaglue.cc (media_format,
// media_info), fmt.cc, mbr.cc, mediasniff.cc, the FileManager with a FileDevice (the host-app copy with the T111 additions), Disk::Init,
// FileSystemFAT, hostfs.cc, and ff.c of the submodule with the board's ffconf.h. Only the block device is fake.
//   argv[1]  directory for the images the independent checker (tools/exfat_check.py) reads afterwards
//   argv[2]  "swallow": the estimate control - a device that swallows writes; S7's estimate check must go red
#include "fmtbench.h"
#include "mediaglue.h"
#include "mediasniff.h"
#include "mediaui.h"
#include "mbr.h"
#include "hostfs.h"
#include "file_device.h"
#include "filemanager.h"
#include "disk.h"


static const char *imgdir;
static FileManager *fm;

static FileDevice *mount_dev(const char *name, SparseDisk &d)
{
    FileDevice *dev = new FileDevice(&d, name, name);
    fm->add_root_entry(dev);
    dev->attach_disk(512);
    dev->probe();
    return dev;
}
static void unmount_dev(FileDevice *dev)
{
    fm->remove_root_entry(dev);
    delete dev;
}
static int drives_in_use(void)
{
    int n = 0;
    for (int i = 0; i < FF_VOLUMES; i++) {
        n += ChanFATManager::getPartition(i) != 0;
    }
    return n;
}
static bool mounted(FileDevice *dev) { return dev->get_file_info()->fs != 0; }

// "Disk::Init + f_mount" on a fresh look at the medium, independent of any FileDevice: how many partitions Disk::Init finds and whether FatFs mounts the first
struct Look { int parts; bool mounts; BYTE fs_type; uint32_t csize_bytes; DWORD free_clusters; uint8_t type; };
static Look look(SparseDisk &d)
{
    Look l = { 0, false, 0, 0, 0, 0 };
    Disk disk(&d, 512);
    l.parts = disk.Init(false);
    if (l.parts >= 1 && disk.partition_list) {
        Partition *p = disk.partition_list;
        l.type = p->get_type();
        uint8_t drv = ChanFATManager::getManager()->addDrive(p);
        char path[4] = { (char)('0' + drv), ':', 0, 0 };
        static FATFS fsobj;
        memset(&fsobj, 0, sizeof(fsobj));
        l.mounts = f_mount(&fsobj, path, 1) == FR_OK;
        if (l.mounts) {
            l.fs_type = fsobj.fs_type;
            l.csize_bytes = fsobj.csize * 512;
            DWORD fre = 0;
            FATFS *pf;
            if (f_getfree(path, &fre, &pf) == FR_OK) {
                l.free_clusters = fre;
            }
            f_mount(0, path, 0);
        }
        ChanFATManager::getManager()->removeDrive(drv);
    }
    return l;
}
static bool sector0_zero(SparseDisk &d)
{
    uint8_t b[512];
    d.get(0, b);
    for (int i = 0; i < 512; i++) {
        if (b[i]) return false;
    }
    return true;
}

// An old FAT32 volume at LBA 8192 inside a table: the thing a format has to replace, and the thing a failed one must not leave half alive.
static void old_fat32_at_8192(SparseDisk &d)
{
    uint8_t m[512] = {};
    m[446 + 4] = 0x0C;
    uint32_t st = MBR_PART_START, ln = d.nsect - MBR_PART_START;
    for (int i = 0; i < 4; i++) {
        m[446 + 8 + i] = (uint8_t)(st >> (8 * i));
        m[446 + 12 + i] = (uint8_t)(ln >> (8 * i));
    }
    m[510] = 0x55; m[511] = 0xAA;
    d.put(0, m);
    CHECK(bench_mkfs(&d, st, ln, FM_FAT32 | FM_SFD) == FR_OK);
}

struct Prog {
    int calls;
    uint32_t last_written, max_gap, total;
    int last_pct, min_pct_step;
    bool monotone;
    int cancel_at_call, cancel_step;
    int steps_seen[FST_STEPS];
    Prog() : calls(0), last_written(0), max_gap(0), total(0), last_pct(-1), min_pct_step(0), monotone(true), cancel_at_call(-1), cancel_step(-1) { memset(steps_seen, 0, sizeof(steps_seen)); }
};
static int prog_cb(void *ctx, int step, uint32_t written, uint32_t total)
{
    Prog *p = (Prog *)ctx;
    p->calls++;
    p->total = total;
    if (written - p->last_written > p->max_gap) p->max_gap = written - p->last_written;
    p->last_written = written;
    int pct = ui_percent(step, written, total, false);
    if (pct < p->last_pct) p->monotone = false;
    p->last_pct = pct;
    if (step >= 0 && step < FST_STEPS) p->steps_seen[step]++;
    if (p->cancel_at_call >= 0 && p->calls == p->cancel_at_call) return 1;
    if (p->cancel_step >= 0 && step == p->cancel_step) return 1;
    return 0;
}

static std::string imgpath(const char *n) { return std::string(imgdir) + "/" + n; }

int main(int argc, char **argv)
{
    imgdir = argc > 1 ? argv[1] : "/tmp";
    bool swallow = argc > 2 && !strcmp(argv[2], "swallow");
    fm = FileManager::getFileManager();
    char why[200];

    // ---- S7 control: the estimate is compared with what the device counted --------------------------------------------------------------
    if (swallow) {
        SparseDisk d(8 * GiB_S);
        d.swallow = true;
        FileDevice *dev = mount_dev("Usb0", d);
        Prog pr;
        media_format("Usb0", prog_cb, &pr, why, sizeof(why));
        CHECK(pr.total > 0 && pr.total / 20 * 19 <= (uint32_t)(d.wsectors * 512) + 1 && (uint32_t)(d.wsectors * 512) <= pr.total / 20 * 21);   // est_bytes within 5 % of the bytes the device counted
        unmount_dev(dev);
        printf("test_exfatfmt swallow: %d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }

    // ---- S4 + S7: three sizes, old data everywhere, the 2 GiB one with a FAT32 volume on it -----------------------------------------------
    struct Size { const char *name; uint32_t sectors; uint32_t cluster; const char *img; bool old_fat; } sizes[] = {
        { "64 MiB", 64 * MiB_S, 4096, "exfat_64M.img", false },
        { "2 GiB", 2 * GiB_S, 32768, "exfat_2G.img", true },
        { "33 GiB", 33 * GiB_S, 131072, "exfat_33G.img", false },
    };
    SparseDisk *big = 0;
    FileDevice *bigdev = 0;
    for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        Size &sz = sizes[si];
        printf("== %s\n", sz.name);
        SparseDisk *dp = new SparseDisk(sz.sectors, 0xA5);
        SparseDisk &d = *dp;
        if (sz.old_fat) {
            old_fat32_at_8192(d);
        }
        FileDevice *dev = mount_dev("Usb0", d);
        if (sz.old_fat) {
            CHECK(mounted(dev));                                  // the old volume is mounted by the host when the format starts
        }
        int drives0 = drives_in_use();
        MediaInfo mi;
        CHECK(media_info("Usb0", &mi) && mi.found && mi.formattable && mi.ready && mi.refuse == 0);
        CHECK(mi.sectors == sz.sectors);
        Prog pr;
        d.writes = 0; d.wsectors = 0;
        FmtResult r = media_format("Usb0", prog_cb, &pr, why, sizeof(why));
        printf("   result %d '%s', %llu writes, %llu bytes, %d progress calls, max gap %u\n", (int)r, why, (unsigned long long)d.writes,
               (unsigned long long)d.wsectors * 512, pr.calls, (unsigned)pr.max_gap);
        CHECK(r == FMT_OK && why[0] == 0);
        // sector 0: one MBR entry
        uint8_t m[512];
        d.get(0, m);
        const uint8_t *e = m + 446;
        uint32_t st = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24), ln = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
        CHECK(e[0] == 0 && e[4] == 0x07 && st == 8192 && ln == sz.sectors - 8192 && m[510] == 0x55 && m[511] == 0xAA);
        bool rest0 = true;
        for (int i = 16; i < 64; i++) rest0 = rest0 && e[i] == 0;
        for (int i = 0; i < 440; i++) rest0 = rest0 && m[i] == 0;
        CHECK(rest0);                                             // entries 2-4 and the boot code are zero
        // Disk::Init and f_mount from scratch
        Look l = look(d);
        CHECK(l.parts == 1 && l.type == 0x07 && l.mounts && l.fs_type == FS_EXFAT);
        CHECK(l.csize_bytes == sz.cluster);
        FmtGeom g;
        fmt_geometry(sz.sectors, &g);
        CHECK(g.cluster_bytes == sz.cluster && l.free_clusters == g.clusters - g.sys_clusters);
        // the FileDevice the host keeps: mounted, same numbers
        CHECK(mounted(dev));
        uint32_t fc = 0, cs = 0;
        CHECK(mounted(dev) && dev->get_file_info()->fs->get_free(&fc, &cs) == FR_OK && cs == sz.cluster && fc == g.clusters - g.sys_clusters);
        CHECK(drives_in_use() == drives0 + (sz.old_fat ? 0 : 1));   // no drive slot leaked: the old volume's slot was freed (one slot is the new volume's)
        // S7: progress
        CHECK(pr.calls >= (si == 2 ? 5 : 4) && pr.monotone);
        CHECK(pr.max_gap <= 1024 * 1024);                         // at least every 1 MiB of written data
        CHECK(pr.steps_seen[FST_RELEASE] && pr.steps_seen[FST_CLEAR] + pr.steps_seen[FST_WRITE] && pr.steps_seen[FST_SYNC] && pr.steps_seen[FST_VERIFY]);
        uint64_t counted = d.wsectors * 512;
        CHECK(pr.total > 0 && counted >= pr.total / 20 * 19 && counted <= pr.total / 20 * 21);   // the estimate within 5 % of the bytes the device counted
        CHECK(pr.total == counted);                               // and in fact exact
        CHECK(ui_percent(FST_VERIFY, pr.total, pr.total, false) == 99 && ui_percent(FST_VERIFY, pr.total, pr.total, true) == 100);
        // the image for the independent checker, before anything else is written to it
        CHECK(d.save(imgpath(sz.img).c_str(), 64u * 1024 * 1024));
        uint64_t freeb = 0;
        CHECK(media_free_bytes("Usb0", &freeb) && freeb == (uint64_t)(g.clusters - g.sys_clusters) * sz.cluster);
        // the mounted volume works: a file, a folder listing
        CHECK(hostfs_save("/Usb0", "hello.txt", "hello", 5));
        HostDirEntry de[8];
        int n = hostfs_list("/Usb0", de, 8, false);
        for (int i = 0; i < n; i++) printf("   listing: '%s' dir %d size %u size64 %llu\n", de[i].name, (int)de[i].dir, (unsigned)de[i].size, (unsigned long long)de[i].size64);
        CHECK(n == 1 && !strcmp(de[0].name, "hello.txt") && de[0].size == 5 && de[0].size64 == 5);
        if (si == 2) {
            big = dp;
            bigdev = dev;
        } else {
            unmount_dev(dev);
            delete dp;
        }
    }

    // ---- S5: a file of 9,008,283,648 bytes on the 33 GiB exFAT volume; a FAT32 volume of the same size as the control ----------------------
    {
        const uint64_t SIZE = 9008283648ULL;
        printf("== S5\n");
        uint8_t mark[4096];
        for (int i = 0; i < 4096; i++) mark[i] = (uint8_t)(i * 7 + 3);
        struct Vol { const char *dev, *path; } vols[2] = { { "Usb0", "/Usb0/MegaAGS.hdf" }, { "Usb1", "/Usb1/MegaAGS.hdf" } };
        SparseDisk fat(33 * GiB_S, 0xA5);
        CHECK(bench_mkfs(&fat, 0, fat.nsect, FM_FAT32 | FM_SFD) == FR_OK);
        FileDevice *fdev = mount_dev("Usb1", fat);
        CHECK(mounted(fdev));
        bool size_ok[2], mark_ok[2];
        for (int v = 0; v < 2; v++) {
            File *f = 0;
            size_ok[v] = mark_ok[v] = false;
            CHECK(fm->fopen(vols[v].path, FA_WRITE | FA_CREATE_ALWAYS, &f) == FR_OK && f);
            if (!f) continue;
            FRESULT sk = f->seek(SIZE - 4096);
            uint32_t bw = 0;
            FRESULT wr = f->write(mark, 4096, &bw);
            printf("   %s: seek %d, write %d (%u bytes)\n", vols[v].dev, (int)sk, (int)wr, (unsigned)bw);
            fm->fclose(f);
            f = 0;
            if (fm->fopen(vols[v].path, FA_READ, &f) == FR_OK && f) {
                size_ok[v] = f->get_size64() == SIZE;
                uint8_t back[4096] = {};
                uint32_t got = 0;
                if (f->seek(SIZE - 4096) == FR_OK && f->read(back, 4096, &got) == FR_OK && got == 4096) {
                    mark_ok[v] = !memcmp(back, mark, 4096);
                }
                printf("   %s: size %llu\n", vols[v].dev, (unsigned long long)f->get_size64());
                fm->fclose(f);
            }
        }
        CHECK(size_ok[0] && mark_ok[0]);                           // exFAT: 9,008,283,648 and the marker intact
        CHECK(!size_ok[1]);                                        // FAT32 control: the size check is RED here (a FAT32 file stops at 4 GiB - 1), so it can fail
        uint32_t hsz = 0, hmt = 0;
        CHECK(hostfs_stat(vols[0].path, &hsz, &hmt) && hsz == 0xFFFFFFFFu);        // saturated, not the wrapped 418,349,056
        HostDirEntry de[8];
        int n = hostfs_list("/Usb0", de, 8, false);
        bool found = false;
        for (int i = 0; i < n; i++) {
            printf("   listing: '%s' size %u size64 %llu\n", de[i].name, (unsigned)de[i].size, (unsigned long long)de[i].size64);
            if (!strcmp(de[i].name, "MegaAGS.hdf")) {
                found = true;
                CHECK(de[i].size == 0xFFFFFFFFu && de[i].size64 == SIZE);
            }
        }
        CHECK(found);
        char t[24];
        ui_size_text(t, sizeof(t), SIZE);
        CHECK(!strcmp(t, "8.3 GiB"));                              // 9,008,283,648 / 2^30 = 8.39; the browser shows 8.3 (cut, not rounded)
        unmount_dev(fdev);
    }
    unmount_dev(bigdev);
    delete big;

    // ---- S6: a failure at any write leaves no half-valid volume --------------------------------------------------------------------------
    {
        printf("== S6\n");
        SparseDisk base(2 * GiB_S, 0xA5);
        old_fat32_at_8192(base);
        {   // the medium before: one partition, FAT32, mounts
            Look l0 = look(base);
            CHECK(l0.parts == 1 && l0.mounts && l0.fs_type == FS_FAT32);
        }
        // a run without a failure counts the writes
        int W;
        {
            SparseDisk d(base);
            d.log_writes = true;
            FileDevice *dev = mount_dev("Usb0", d);
            Prog pr;
            FmtResult r = media_format("Usb0", prog_cb, &pr, why, sizeof(why));
            CHECK(r == FMT_OK);
            W = (int)d.writes;
            printf("   a format is %d writes; the first at %u, the last at %u\n", W, (unsigned)d.write_lba.front(), (unsigned)d.write_lba.back());
            CHECK(W > 20 && d.write_lba.front() == 0 && d.write_lba.back() == 0);     // the zero first, the table last
            unmount_dev(dev);
        }
        int old_ok = 0, blank = 0, bad = 0;
        for (int N = 1; N <= W; N++) {
            SparseDisk d(base);
            d.fail_nth = N;
            FileDevice *dev = mount_dev("Usb0", d);
            CHECK(mounted(dev));
            FmtResult r = media_format("Usb0", prog_cb, new Prog, why, sizeof(why));
            Look l = look(d);
            bool z = sector0_zero(d);
            if (r != FMT_FAILED || !why[0]) bad++;
            if (N == 1) {
                // the zeroing of sector 0 itself failed: nothing was written, the old volume is as it was and the host has it again
                if (!(l.parts == 1 && l.mounts && l.fs_type == FS_FAT32 && !z && mounted(dev))) bad++; else old_ok++;
            } else {
                // from the second write on the medium has no volume: no table, nothing for Disk::Init to find, f_mount fails, the host mounts nothing
                if (!(z && !l.mounts && l.fs_type != FS_FAT32 && l.fs_type != FS_EXFAT && !mounted(dev))) bad++; else blank++;
            }
            unmount_dev(dev);
        }
        printf("   sweep over %d failing writes: %d old volume intact, %d blank, %d wrong\n", W, old_ok, blank, bad);
        CHECK(bad == 0 && old_ok == 1 && blank == W - 1);
        // the medium is gone from the Nth write on
        for (int N = 2; N <= 6; N += 2) {
            SparseDisk d(base);
            d.gone_from = N;
            FileDevice *dev = mount_dev("Usb0", d);
            FmtResult r = media_format("Usb0", prog_cb, new Prog, why, sizeof(why));
            CHECK(r == FMT_FAILED && why[0]);
            d.set_state(e_device_ready);
            d.gone_from = -1;
            CHECK(sector0_zero(d) || N == 1);
            Look l = look(d);
            CHECK(!l.mounts);
            unmount_dev(dev);
        }
        // a failed read of the capacity, a card that was pulled before: refused, nothing written, the old volume still mounts
        {
            SparseDisk d(base);
            FileDevice *dev = mount_dev("Usb0", d);
            d.capfail = true;
            d.writes = 0;
            FmtResult r = media_format("Usb0", prog_cb, new Prog, why, sizeof(why));
            CHECK(r == FMT_REFUSED && d.writes == 0 && strstr(why, "size"));
            d.capfail = false;
            Look l = look(d);
            CHECK(l.mounts && l.fs_type == FS_FAT32);
            unmount_dev(dev);
        }
        // a medium that takes the writes and loses them is not reported as formatted: the read-back catches it
        {
            SparseDisk d(base);
            FileDevice *dev = mount_dev("Usb0", d);
            d.swallow = true;
            FmtResult r = media_format("Usb0", prog_cb, new Prog, why, sizeof(why));
            printf("   a medium that loses the writes: result %d '%s'\n", (int)r, why);
            CHECK(r == FMT_FAILED && why[0]);
            unmount_dev(dev);
        }
        // three formats in a row on one device: the drive slots do not pile up
        {
            SparseDisk d(base);
            FileDevice *dev = mount_dev("Usb0", d);
            int u0 = drives_in_use();
            for (int i = 0; i < 3; i++) {
                CHECK(media_format("Usb0", prog_cb, new Prog, why, sizeof(why)) == FMT_OK);
            }
            CHECK(drives_in_use() == u0 && mounted(dev));
            unmount_dev(dev);
        }
    }

    // ---- Q6: a cancel while writing leaves a blank medium; the table is the point after which there is no cancel ------------------------------
    {
        printf("== cancel\n");
        SparseDisk base(2 * GiB_S, 0xA5);
        old_fat32_at_8192(base);
        struct C { int at_call; int at_step; bool blank; bool honoured; const char *what; } cs[] = {
            { 1, -1, false, true, "at the very first report, before anything is written" },
            { -1, FST_WRITE, true, true, "after sector 0 is cleared" },
            { 3, -1, true, true, "in the middle of the file system" },
            { -1, FST_SYNC, false, false, "after the table: too late to cancel, the format completes" },
            { -1, FST_VERIFY, false, false, "while verifying: the same" },
        };
        for (unsigned i = 0; i < sizeof(cs) / sizeof(cs[0]); i++) {
            SparseDisk d(base);
            FileDevice *dev = mount_dev("Usb0", d);
            Prog pr;
            pr.cancel_at_call = cs[i].at_call;
            pr.cancel_step = cs[i].at_step;
            FmtResult r = media_format("Usb0", prog_cb, &pr, why, sizeof(why));
            Look l = look(d);
            printf("   cancel %s: result %d '%s'\n", cs[i].what, (int)r, why);
            if (cs[i].honoured) {
                CHECK(r == FMT_CANCELLED && strstr(why, "cancel"));
                if (cs[i].blank) {
                    CHECK(sector0_zero(d) && !l.mounts && !mounted(dev));
                } else {
                    CHECK(l.mounts && l.fs_type == FS_FAT32 && mounted(dev));       // before the first write: the old volume is as it was
                }
            } else {
                CHECK(r == FMT_OK && l.mounts && l.fs_type == FS_EXFAT && mounted(dev));
            }
            unmount_dev(dev);
        }
    }

    // ---- S8: limits are refused before any write ------------------------------------------------------------------------------------------
    {
        printf("== S8\n");
        struct L { const char *what; const char *word; } lim[8];
        (void)lim;
        auto refused = [&](const char *what, const char *word, SparseDisk &d, uint32_t heap = 8000000) {
            bench_free_heap = heap;
            FileDevice *dev = mount_dev("Usb0", d);
            d.writes = 0; d.wsectors = 0;
            FmtResult r = media_format("Usb0", prog_cb, new Prog, why, sizeof(why));
            printf("   %s: result %d '%s', writes %llu\n", what, (int)r, why, (unsigned long long)d.writes);
            CHECK(r == FMT_REFUSED && d.writes == 0 && d.wsectors == 0 && strstr(why, word));
            MediaInfo mi;
            media_info("Usb0", &mi);
            CHECK(!mi.ready || mi.refuse != 0);
            unmount_dev(dev);
            bench_free_heap = 8000000;
        };
        { SparseDisk d(0, 0x00); refused("0 sectors", "no medium", d); }
        { SparseDisk d(1 * MiB_S, 0x00); refused("1 MiB", "smaller than 64 MiB", d); }
        { SparseDisk d(64 * MiB_S - 1, 0x00); refused("64 MiB - 1 sector", "smaller than 64 MiB", d); }
        { SparseDisk d(1 * GiB_S, 0x00); d.protect = true; refused("write protected", "write protected", d); }
        { SparseDisk d(1 * GiB_S, 0x00); d.set_state(e_device_not_ready); refused("not ready", "no medium", d); }
        { SparseDisk d(1 * GiB_S, 0x00); d.nodisk = true; refused("no disk", "no medium", d); }
        { SparseDisk d(0xFFFFFFFFu, 0x00); refused("2 TiB (the largest a 32 bit sector count can say)", "2 TiB", d); }
        { SparseDisk d(0xFFFFFFFFu - 8191, 0x00); refused("just over the limit", "2 TiB", d); }
        { SparseDisk d(1 * GiB_S, 0x00); d.ssize = 4096; refused("4096-byte sectors", "512", d); }
        { SparseDisk d(1 * GiB_S, 0x00); refused("too little heap", "memory", d, 8192); }
        // the limits that are inside: exactly 64 MiB and 2 TiB - 8192 sectors are accepted by the check
        {
            SparseDisk d(64 * MiB_S, 0x00);
            FmtGeom g;
            CHECK(fmt_check(&d, fmt_work_size(8000000), &g) == 0 && g.cluster_bytes == 4096);
            SparseDisk d2(0xFFFFFFFFu - MBR_PART_START, 0x00);
            CHECK(fmt_check(&d2, fmt_work_size(8000000), &g) == 0 && g.cluster_bytes == 131072);
        }
        // the internal flash drive is never formatted, whatever it is
        {
            SparseDisk d(16 * MiB_S, 0x00);
            FileDevice *dev = mount_dev("Flash", d);
            d.writes = 0;
            MediaInfo mi;
            CHECK(media_info("Flash", &mi) && mi.found && !mi.formattable);
            CHECK(media_format("Flash", prog_cb, new Prog, why, sizeof(why)) == FMT_REFUSED && d.writes == 0);
            unmount_dev(dev);
        }
        // the work buffer
        CHECK(fmt_work_size(8000000) == 65536 && fmt_work_size(100000) == 24576 && fmt_work_size(16384) == 4096 && fmt_work_size(16383) == 0 && fmt_work_size(0) == 0);
    }

    printf("test_exfatfmt: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
