// T128 S9: the folder disk over the board's CoreFs (coreapp/src/fm_fs.cc) on the real stack of vendor/gideon (BlockDevice, Partition, FatFS with
// fast seek, FileSystemFAT, FileManager), on an exFAT volume of 512 MiB with 4 KiB clusters. Two things the host benches with the fake file system
// cannot reach:
//   (a) the CoreFs calls the folder disk added: free_bytes, mkdir, rename, the date and attributes of a listing, an empty file made with save
//   (b) the handle policy with many files: 200 files of 1 MiB, each in 64 fragments, 3,000 random sector reads of the disk. At most FH_HANDLES
//       files are open, and so at most that many link maps exist (fastseek_maps_peak), an evicted file's map is freed, and the sectors the block
//       device is asked for per request are counted - the data (the read-ahead of 4 KiB is 8 sectors) apart from the directory and the FAT.
//       A sequential read of one file is served from the read-ahead.
// the C++ library first: the firmware headers define max() and min() as macros
#include "folderhd_reader.h"
#include <fcntl.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "../../../vendor/gideon/software/io/usb/tests/host_test/host_test.h"
#include "blockdev_emul.h"
#include "partition.h"
#include "filesystem_fat.h"
#include "chanfat_manager.h"
#include "filemanager.h"
#include "node_directfs.h"
#include "fm_fs.h"
#include "fastseek_api.h"
// the fast seek library's table goes to the CoreFs as the app's bind entry does (T239.3 P3-1)
static struct SetFastSeek { SetFastSeek() { fm_fs_set_fastseek(fastseek_table()); } } g_set_fastseek;
#include "fastseek.h"
#include "folderhd.h"

extern "C" void outbyte(int) { }

extern "C" uint32_t get_fattime(void)
{
    return (46UL << 25) | (10 << 21) | (24 << 16);
}

#if !FF_USE_FASTSEEK
#error this bench is built with FF_USE_FASTSEEK 1
#endif

namespace {

const uint64_t MiB = 1024ULL * 1024ULL;
static uint64_t sectors_read, read_calls;

class CountingDevice : public BlockDevice_Emulated {
public:
    CountingDevice(const char *name) : BlockDevice_Emulated(name, 512) { }
    virtual DRESULT read(uint8_t *b, uint32_t s, int n)
    {
        sectors_read += n;
        read_calls++;
        return BlockDevice_Emulated::read(b, s, n);
    }
};

struct Volume {
    std::string image;
    BlockDevice_Emulated *blk;
    Partition *prt;
    FileSystemFAT *fs;
    Node_DirectFS *node;
    bool ok;

    Volume() : blk(NULL), prt(NULL), fs(NULL), node(NULL), ok(false)
    {
        const char *dir = getenv("TMPDIR");
        image = std::string(dir ? dir : "/tmp") + "/fmfs_folder_test_" + std::to_string((long)getpid()) + ".img";
        int fd = open(image.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return;
        }
        bool sized = (ftruncate(fd, (off_t)(512 * MiB)) == 0);
        close(fd);
        if (!sized) {
            return;
        }
        blk = new CountingDevice(image.c_str());
        prt = new Partition(blk, 0, 0, 0);
        uint8_t drv = ChanFATManager::getManager()->addDrive(prt);
        MKFS_PARM param;
        param.fmt = FM_EXFAT;
        param.n_fat = 1;
        param.n_root = 0;
        param.au_size = 4096;
        param.align = 0;
        uint8_t work[4096];
        char name[3] = { (char)('0' + drv), ':', 0 };
        FRESULT fres = f_mkfs(name, &param, work, sizeof(work));
        ChanFATManager::getManager()->removeDrive(drv);
        if (fres != FR_OK) {
            return;
        }
        fs = new FileSystemFAT(prt);
        if (!fs->init()) {
            return;
        }
        node = new Node_DirectFS(fs, "usb0", AM_DIR);
        FileManager::getFileManager()->add_root_entry(node);
        ok = true;
    }
    ~Volume()
    {
        if (node) {
            FileManager::getFileManager()->remove_root_entry(node);
            delete node;
        }
        delete fs;
        delete prt;
        delete blk;
        unlink(image.c_str());
    }
};

Volume &volume()
{
    static Volume v;
    return v;
}

uint8_t pat(uint32_t tag, uint64_t i) { return (uint8_t)(i * 31 + (i >> 9) * 7 + tag * 13); }

struct TickIo : CoreIo {
    uint32_t t = 0;
    uint8_t rd(uint32_t) { return 0; }
    void wr(uint32_t, uint8_t) { }
    void ddr_write(uint32_t, const void *, uint32_t) { }
    void ddr_read(uint32_t, void *, uint32_t) { }
    void rom_write(uint32_t, const void *, uint32_t) { }
    void rom_read(uint32_t, void *, uint32_t) { }
    void ram_write(uint32_t, const void *, uint32_t) { }
    void ram_read(uint32_t, void *, uint32_t) { }
    void stop(bool) { }
    void mode_reset(bool) { }
    uint32_t now_ms() { return t++; }
    void delay_ms(uint32_t) { }
    void delay_us(uint32_t) { }
};

// the disk as a block source for the reader of the benches
struct DiskSource : bench::BlockSource {
    CoreFile *f;
    explicit DiskSource(CoreFile *file) : f(file) { }
    bool read(uint32_t lba, uint8_t *out) { return f->read((uint64_t)lba * 512, out, 512); }
    bool write(uint32_t lba, const uint8_t *in) { return f->write((uint64_t)lba * 512, in, 512); }
};

uint32_t lcg = 12345;
uint32_t rnd() { lcg = lcg * 1664525u + 1013904223u; return lcg >> 8; }

} // namespace

TEST(FmFsFolder, TheCoreFsCallsOfTheFolderDisk)
{
    ASSERT_TRUE(volume().ok);
    CoreFs *fs = fm_fs();
    uint64_t free0 = 0, free1 = 0;
    ASSERT_TRUE(fs->free_bytes("/usb0", &free0));
    EXPECT_TRUE(free0 > 400 * MiB && free0 < 512 * MiB);
    // mkdir: new, again (a folder that is there is success), over a file (not)
    EXPECT_TRUE(fs->mkdir("/usb0/d1"));
    EXPECT_TRUE(fs->mkdir("/usb0/d1"));
    uint64_t sz;
    bool isdir;
    EXPECT_TRUE(fs->stat("/usb0/d1", &sz, &isdir) && isdir);
    EXPECT_TRUE(fs->save("/usb0/d1/a.txt", "hello", 5));
    EXPECT_TRUE(!fs->mkdir("/usb0/d1/a.txt"));
    // rename: a file, a folder with a file in it, onto a name that is taken (refused)
    EXPECT_TRUE(fs->rename("/usb0/d1/a.txt", "/usb0/d1/b.txt"));
    EXPECT_TRUE(!fs->stat("/usb0/d1/a.txt", &sz, &isdir));
    EXPECT_TRUE(fs->stat("/usb0/d1/b.txt", &sz, &isdir) && sz == 5 && !isdir);
    EXPECT_TRUE(fs->save("/usb0/d1/c.txt", "x", 1));
    EXPECT_TRUE(!fs->rename("/usb0/d1/c.txt", "/usb0/d1/b.txt"));
    EXPECT_TRUE(fs->rename("/usb0/d1", "/usb0/d2"));
    EXPECT_TRUE(!fs->stat("/usb0/d1", &sz, &isdir) && fs->stat("/usb0/d2/b.txt", &sz, &isdir));
    // the listing carries the date (the FAT time of get_fattime above: 2026-10-24 00:00) and the attributes
    FsEntry e[8];
    int n = fs->list("/usb0/d2", e, 8);
    ASSERT_EQ(2, n);
    for (int i = 0; i < n; i++) {
        EXPECT_EQ((uint16_t)((46 << 9) | (10 << 5) | 24), e[i].fdate);
        EXPECT_EQ((uint16_t)0, e[i].ftime);
        EXPECT_EQ((uint8_t)0, e[i].attr);
    }
    // an empty file made with save (the scratch file of the folder disk), then opened for writing
    EXPECT_TRUE(fs->mkdir("/usb0/trx64"));
    EXPECT_TRUE(fs->save("/usb0/trx64/folderhd0.scratch", "", 0));
    EXPECT_TRUE(fs->stat("/usb0/trx64/folderhd0.scratch", &sz, &isdir) && sz == 0);
    CoreFile *f = fs->open("/usb0/trx64/folderhd0.scratch", true);
    ASSERT_TRUE(f != NULL);
    EXPECT_TRUE(f->writable());
    uint8_t blk[512];
    memset(blk, 7, sizeof(blk));
    EXPECT_TRUE(f->write(0, blk, 512) && f->write(512, blk, 512) && f->size() == 1024);
    fs->close(f);
    // the free space shrinks with a megabyte written
    std::vector<uint8_t> mb(1 * MiB, 1);
    EXPECT_TRUE(fs->save("/usb0/trx64/mb.bin", mb.data(), (uint32_t)mb.size()));
    ASSERT_TRUE(fs->free_bytes("/usb0", &free1));
    EXPECT_TRUE(free0 - free1 >= 1 * MiB && free0 - free1 < 2 * MiB);
    // remove: a file, an empty folder, a folder that is not empty (refused)
    EXPECT_TRUE(fs->remove("/usb0/trx64/mb.bin"));
    EXPECT_TRUE(!fs->remove("/usb0/d2"));
    EXPECT_TRUE(fs->remove("/usb0/d2/b.txt") && fs->remove("/usb0/d2/c.txt") && fs->remove("/usb0/d2"));
    EXPECT_TRUE(!fs->stat("/usb0/d2", &sz, &isdir));
    // the same refusals as the write guard of the flash
    EXPECT_TRUE(!fs->mkdir("/flash/x") && !fs->rename("/flash/a", "/usb0/a"));
}

TEST(FmFsFolder, ManyFilesFewHandles)
{
    ASSERT_TRUE(volume().ok);
    FileManager *fm = FileManager::getFileManager();
    const int FILES = 200, GROUP = 20, CHUNK = 16 * 1024, ROUNDS = 64;
    ASSERT_TRUE(fm_fs()->mkdir("/usb0/hd"));
    // 64 fragments per file: the files of a group grow alternately, a chunk (4 clusters) at a time
    for (int g = 0; g < FILES / GROUP; g++) {
        File *fh[GROUP];
        for (int i = 0; i < GROUP; i++) {
            char nm[32];
            snprintf(nm, sizeof(nm), "f%03d.bin", g * GROUP + i);
            ASSERT_EQ(FR_OK, fm->fopen("/usb0/hd", nm, FA_CREATE_ALWAYS | FA_WRITE, &fh[i]));
        }
        std::vector<uint8_t> buf(CHUNK);
        for (int r = 0; r < ROUNDS; r++) {
            for (int i = 0; i < GROUP; i++) {
                for (int b = 0; b < CHUNK; b++) buf[b] = pat((uint32_t)(g * GROUP + i), (uint64_t)r * CHUNK + b);
                uint32_t wr = 0;
                ASSERT_EQ(FR_OK, fh[i]->write(buf.data(), CHUNK, &wr));
                ASSERT_EQ((uint32_t)CHUNK, wr);
            }
        }
        for (int i = 0; i < GROUP; i++) fm->fclose(fh[i]);
    }
    TickIo io;
    char msg[240];
    FolderDisk::Cfg cfg = { fm_fs(), &io, 0, "/usb0/hd", 1ull << 30, 65535 };
    FolderDisk *d = FolderDisk::mount(cfg, msg, sizeof(msg));
    if (!d) printf("mount: %s\n", msg);
    ASSERT_TRUE(d != NULL);
    EXPECT_EQ((uint32_t)FILES, d->stats().nodes);
    DiskSource src(d);
    bench::Reader rd(src);
    bool ok = rd.check_all();
    for (auto &er : rd.errs) printf("  reader: %s\n", er.c_str());
    ASSERT_TRUE(ok);
    ASSERT_EQ((size_t)FILES, rd.ents.size());
    std::vector<int> index(rd.ents.size());
    for (size_t i = 0; i < rd.ents.size(); i++) index[i] = atoi(rd.ents[i].path.c_str() + 1);
    const int BLOCKS = 1024 * 1024 / 512;
    // (b) 3,000 random sectors over all the files
    uint64_t sec0 = sectors_read, calls0 = read_calls;
    uint32_t ra0 = d->stats().ra_reads;
    bool bytes = true;
    uint64_t worst = 0;
    for (int k = 0; k < 3000; k++) {
        size_t fi = rnd() % rd.ents.size();
        uint32_t j = rnd() % BLOCKS;
        uint8_t blk[512];
        uint64_t before = sectors_read;
        ASSERT_TRUE(d->read((uint64_t)(rd.base + rd.ents[fi].data[j]) * 512, blk, 512));
        uint64_t cost = sectors_read - before;
        if (cost > worst) worst = cost;
        for (int i = 0; i < 512 && bytes; i++) bytes = blk[i] == pat((uint32_t)index[fi], (uint64_t)j * 512 + i);
    }
    EXPECT_TRUE(bytes);
    uint64_t sectors = sectors_read - sec0, calls = read_calls - calls0;
    uint32_t fills = d->stats().ra_reads - ra0;
    double data = (3000 - fills) * 1.0 + fills * 8.0, other = (double)sectors - data;      // a random request reads its own sector; a read-ahead fill is 8
    printf("FmFsFolder: 3000 random sectors over %d files of 1 MiB in 64 fragments, %d handles: block device sectors per request %.2f"
           " (data %.2f, directory and FAT %.2f), max %u; device read calls %.2f per request; link maps: peak %u, now %u; files open: peak %u\n",
           FILES, FH_HANDLES, sectors / 3000.0, data / 3000.0, other / 3000.0, (unsigned)worst, calls / 3000.0, (unsigned)fastseek_maps_peak(),
           (unsigned)fastseek_maps_live(), (unsigned)d->stats().handle_peak);
    EXPECT_TRUE(fastseek_maps_peak() <= (uint32_t)FH_HANDLES);                // at most 8 maps at any time
    EXPECT_TRUE(fastseek_maps_live() <= (uint32_t)FH_HANDLES);
    EXPECT_TRUE(d->stats().handle_peak <= (uint32_t)FH_HANDLES);
    EXPECT_TRUE(d->stats().handle_opens > 2000);                              // the policy was exercised: nearly every request opened a file
    EXPECT_TRUE(other / 3000.0 < 160.0);                                         // the look-up of the path in a directory of 200 files (measured: 120)
    // (b2) the same, over as many files as there are handles: no file is opened again, the level of T104 5.16 (avg 1.00, max <= 4 per block)
    {
        uint64_t s0 = sectors_read, worst2 = 0;
        uint32_t opens0 = d->stats().handle_opens;
        const int WARM = 200, N = 3000;
        for (int k = 0; k < WARM + N; k++) {
            size_t fi = (size_t)(rnd() % (uint32_t)FH_HANDLES);
            uint32_t j = rnd() % BLOCKS;
            uint8_t blk[512];
            if (k == WARM) { s0 = sectors_read; worst2 = 0; opens0 = d->stats().handle_opens; }
            uint64_t before = sectors_read;
            ASSERT_TRUE(d->read((uint64_t)(rd.base + rd.ents[fi].data[j]) * 512, blk, 512));
            uint64_t cost = sectors_read - before;
            if (k >= WARM && cost > worst2) worst2 = cost;
            bool ok2 = true;
            for (int i = 0; i < 512 && ok2; i++) ok2 = blk[i] == pat((uint32_t)index[fi], (uint64_t)j * 512 + i);
            EXPECT_TRUE(ok2);
        }
        double avg = (double)(sectors_read - s0) / N;
        printf("FmFsFolder: %d random sectors over %d files (all open): block device sectors per request avg %.2f, max %u; files opened again: %u\n", N, FH_HANDLES, avg,
               (unsigned)worst2, (unsigned)(d->stats().handle_opens - opens0));
        EXPECT_TRUE(avg <= 1.1);
        EXPECT_TRUE(worst2 <= 4);
        EXPECT_EQ((uint32_t)0, d->stats().handle_opens - opens0);
    }
    // (c) a sequential read of one file is served from the read-ahead: one fill of 4 KiB for eight sectors
    {
        const bench::Ent &e = rd.ents[17];
        uint32_t hits0 = d->stats().ra_hits, fills0 = d->stats().ra_reads;
        uint64_t s0 = sectors_read;
        bool seq = true;
        for (int j = 0; j < BLOCKS; j++) {
            uint8_t blk[512];
            if (!d->read((uint64_t)(rd.base + e.data[j]) * 512, blk, 512)) { seq = false; break; }
            for (int i = 0; i < 512 && seq; i++) seq = blk[i] == pat((uint32_t)index[17], (uint64_t)j * 512 + i);
        }
        EXPECT_TRUE(seq);
        uint32_t hits = d->stats().ra_hits - hits0, fl = d->stats().ra_reads - fills0;
        double per = (double)(sectors_read - s0) / BLOCKS;
        printf("FmFsFolder: a sequential read of one file: %u read-ahead fills, %u hits for %d requests, block device sectors per request %.2f\n", fl, hits, BLOCKS, per);
        EXPECT_TRUE(fl <= (uint32_t)BLOCKS / 8 + 2);
        EXPECT_TRUE(hits >= (uint32_t)BLOCKS - (uint32_t)BLOCKS / 8 - 2);
        EXPECT_TRUE(per <= 1.2);
    }
    // (d) an evicted file's table is freed with it: nothing is left when the disk is gone
    delete d;
    EXPECT_EQ((uint32_t)0, fastseek_maps_live());
}
