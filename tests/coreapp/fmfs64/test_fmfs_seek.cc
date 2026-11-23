// T104 5.16: the board's CoreFs (coreapp/src/fm_fs.cc) with the fast seek, over the real stack of vendor/gideon (BlockDevice, Partition, FatFS,
// FileSystemFAT, FileManager) on an exFAT volume of 128 MiB with 4 KiB clusters. FatFS is built with FF_USE_FASTSEEK 1 here (run.sh derives the
// configuration from chan_fat/pc). This is the bench for the part tests/coreapp/test_fastseek.cc cannot reach: that fm_fs.cc recognises a FileFAT
// (vtable test, no RTTI), hands its FIL to the policy and frees the table with the file.
// The judge is the number of sectors the block device is asked for per 512 byte random read through CoreFile::read, counted in the device.
//   T1 two files grown alternately (256 fragments each): 200 random blocks read back byte for byte, and each costs at most 4 sectors
//      (the data sector, a few for the directory/bitmap cache); without the map a far block walks the exFAT chain through the FAT
//   T2 a write that reaches beyond the end of the image, after the map was built, succeeds and the new size is on the medium
// Controls (run.sh): the vtable test broken (no FileFAT recognised: T1's bound goes red), the drop on growth removed (T2 goes red).
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

#include <fcntl.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <vector>

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
const uint32_t SIZE = 16u * (uint32_t)MiB, CHUNK = 64 * 1024, BOUND = 4;

static uint64_t sectors_read;

class CountingDevice : public BlockDevice_Emulated {
public:
    CountingDevice(const char *name) : BlockDevice_Emulated(name, 512) { }
    virtual DRESULT read(uint8_t *b, uint32_t s, int n)
    {
        sectors_read += n;
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
        image = std::string(dir ? dir : "/tmp") + "/fmfs_seek_test_" + std::to_string((long)getpid()) + ".img";
        int fd = open(image.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return;
        }
        bool sized = (ftruncate(fd, (off_t)(128 * MiB)) == 0);
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

uint8_t pat(uint32_t tag, uint64_t i) { return (uint8_t)(i * 31 + (i >> 9) * 7 + tag); }

bool interleave()
{
    FileManager *fm = FileManager::getFileManager();
    File *a = NULL, *b = NULL;
    if (fm->fopen("/usb0", "a.img", FA_CREATE_ALWAYS | FA_WRITE, &a) != FR_OK || fm->fopen("/usb0", "b.img", FA_CREATE_ALWAYS | FA_WRITE, &b) != FR_OK) {
        return false;
    }
    std::vector<uint8_t> da(CHUNK), db(CHUNK);
    bool ok = true;
    for (uint32_t done = 0; ok && done < SIZE; done += CHUNK) {
        for (uint32_t i = 0; i < CHUNK; i++) { da[i] = pat(1, done + i); db[i] = pat(2, done + i); }
        uint32_t n = 0;
        ok = a->write(da.data(), CHUNK, &n) == FR_OK && n == CHUNK && b->write(db.data(), CHUNK, &n) == FR_OK && n == CHUNK;
    }
    fm->fclose(a);
    fm->fclose(b);
    return ok;
}

uint32_t lcg = 99;
uint32_t rnd() { lcg = lcg * 1664525u + 1013904223u; return lcg >> 8; }

} // namespace

TEST(FmFsSeek, RandomBlocksOfAFragmentedImageCostNoChainWalk)
{
    ASSERT_TRUE(volume().ok);
    ASSERT_TRUE(interleave());
    CoreFile *f = fm_fs()->open("/usb0/a.img", true);
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ((uint64_t)SIZE, f->size());
    uint8_t buf[512];
    ASSERT_TRUE(f->read(4096, buf, sizeof(buf)));            // not a continuation of the previous access (offset 0): builds the map
    uint64_t worst = 0, total = 0;
    bool bytes = true;
    for (int k = 0; k < 200; k++) {
        uint64_t off = (uint64_t)(rnd() % (SIZE / 512)) * 512;
        uint64_t before = sectors_read;
        ASSERT_TRUE(f->read(off, buf, sizeof(buf)));
        uint64_t cost = sectors_read - before;
        total += cost;
        if (cost > worst) worst = cost;
        for (int i = 0; i < 512 && bytes; i++) bytes = buf[i] == pat(1, off + i);
    }
    printf("FmFsSeek: 200 random blocks of a 16 MiB image in 256 fragments: sectors read per block avg %.2f, max %u\n", total / 200.0, (unsigned)worst);
    EXPECT_TRUE(bytes);
    EXPECT_TRUE(worst <= BOUND);
    fm_fs()->close(f);
}

TEST(FmFsSeek, AWriteBeyondTheEndStillExtendsTheImage)
{
    ASSERT_TRUE(volume().ok);
    CoreFile *f = fm_fs()->open("/usb0/a.img", true);
    ASSERT_TRUE(f != NULL);
    uint8_t buf[512], ext[512];
    memset(ext, 0x5A, sizeof(ext));
    ASSERT_TRUE(f->read(8 * MiB, buf, sizeof(buf)));         // a seek: the map is built
    EXPECT_TRUE(f->write(SIZE, ext, sizeof(ext)));           // beyond the end: the map is dropped, FatFS allocates
    EXPECT_EQ((uint64_t)SIZE + 512, f->size());
    EXPECT_TRUE(f->sync());
    EXPECT_TRUE(f->read(SIZE, buf, sizeof(buf)));
    EXPECT_TRUE(!memcmp(buf, ext, sizeof(buf)));
    EXPECT_TRUE(f->read(3 * MiB, buf, sizeof(buf)));         // and the file is readable at its other places
    bool same = true;
    for (int i = 0; i < 512; i++) same = same && buf[i] == pat(1, 3 * MiB + i);
    EXPECT_TRUE(same);
    fm_fs()->close(f);
    f = fm_fs()->open("/usb0/a.img", false);
    ASSERT_TRUE(f != NULL);
    EXPECT_EQ((uint64_t)SIZE + 512, f->size());
    fm_fs()->close(f);
}
