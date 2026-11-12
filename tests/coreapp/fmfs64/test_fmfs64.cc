// T104.1 D16: the board's CoreFs (coreapp/src/fm_fs.cc, over the FileManager) on a real exFAT volume of 6 GiB (a sparse image on the Mac,
// the stack of vendor/gideon: BlockDevice, Partition, FatFS, FileSystemFAT, FileManager). A file of 5 GiB + 16 bytes is written
// through CoreFile with offsets of 64 bit, read back, listed, stat'ed. Two markers, at 1 GiB and at 5 GiB, tell an offset that wraps
// at 32 bit from a correct one. run.sh builds the same test against copies of fsif.h and fm_fs.cc that are broken on purpose.
#include "../../../vendor/gideon/software/io/usb/tests/host_test/host_test.h"
#include "blockdev_emul.h"
#include "partition.h"
#include "filesystem_fat.h"
#include "chanfat_manager.h"
#include "filemanager.h"
#include "node_directfs.h"
#include "fm_fs.h"

#include <fcntl.h>
#include <string>
#include <string.h>
#include <unistd.h>

extern "C" void outbyte(int) { }

extern "C" uint32_t get_fattime(void)
{
    return (46UL << 25) | (10 << 21) | (18 << 16);
}

namespace {

const uint64_t GiB = 1024ULL * 1024ULL * 1024ULL;
const uint64_t MARK_LOW = 1 * GiB;   // 5 GiB modulo 4 GiB
const uint64_t MARK_HIGH = 5 * GiB;
const uint64_t FILE_SIZE = MARK_HIGH + 16;

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
        image = std::string(dir ? dir : "/tmp") + "/fmfs64_test_" + std::to_string((long)getpid()) + ".img";
        int fd = open(image.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return;
        }
        bool sized = (ftruncate(fd, (off_t)(6 * GiB)) == 0); // sparse: only what is written takes space
        close(fd);
        if (!sized) {
            return;
        }
        blk = new BlockDevice_Emulated(image.c_str(), 512);
        prt = new Partition(blk, 0, 0, 0);
        uint8_t drv = ChanFATManager::getManager()->addDrive(prt);
        MKFS_PARM param;
        param.fmt = FM_EXFAT; // FM_ANY would make FAT32 of a volume this small, and a FAT32 file stops at 4 GiB - 1
        param.n_fat = 1;
        param.n_root = 0;
        param.au_size = 0;
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

bool write_at(CoreFile *f, uint64_t pos, char c)
{
    uint8_t block[16];
    memset(block, c, sizeof(block));
    return f->write(pos, block, sizeof(block));
}

bool read_at(CoreFile *f, uint64_t pos, char c)
{
    uint8_t block[16];
    if (!f->read(pos, block, sizeof(block))) {
        return false;
    }
    for (size_t i = 0; i < sizeof(block); i++) {
        if (block[i] != (uint8_t)c) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(FmFs64, CoreFileOfMoreThan4GiBKeepsSizeAndOffsets)
{
    ASSERT_TRUE(volume().ok);
    CoreFs *fs = fm_fs();
    // the file is made through the FileManager (CoreFs::open needs it to exist); the 64 bit writes then go through CoreFile
    File *mk = NULL;
    ASSERT_EQ(FR_OK, FileManager::getFileManager()->fopen("/usb0", "big.hdf", FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &mk));
    uint8_t one = 0;
    uint32_t n = 0;
    ASSERT_EQ(FR_OK, mk->write(&one, 1, &n));
    FileManager::getFileManager()->fclose(mk);

    CoreFile *f = fs->open("/usb0/big.hdf", true);
    ASSERT_TRUE(f != NULL);
    EXPECT_TRUE(f->writable());
    EXPECT_TRUE(write_at(f, MARK_LOW, 'A'));
    EXPECT_TRUE(write_at(f, MARK_HIGH, 'B'));
    EXPECT_EQ((uint64_t)FILE_SIZE, f->size());
    EXPECT_TRUE(f->sync());
    EXPECT_TRUE(read_at(f, MARK_HIGH, 'B'));
    EXPECT_TRUE(read_at(f, MARK_LOW, 'A'));
    uint8_t past[16];
    EXPECT_FALSE(f->read(FILE_SIZE - 8, past, 16)); // across the end
    fs->close(f);

    f = fs->open("/usb0/big.hdf", false);
    ASSERT_TRUE(f != NULL);
    EXPECT_EQ((uint64_t)FILE_SIZE, f->size());
    EXPECT_TRUE(read_at(f, MARK_HIGH, 'B'));
    EXPECT_TRUE(read_at(f, MARK_LOW, 'A'));
    fs->close(f);
}

TEST(FmFs64, ListStatAndLoadReportTheRealSize)
{
    ASSERT_TRUE(volume().ok);
    CoreFs *fs = fm_fs();
    FsEntry e[8];
    int n = fs->list("/usb0", e, 8);
    ASSERT_TRUE(n >= 1);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(e[i].name, "big.hdf")) {
            found = true;
            EXPECT_EQ((uint64_t)FILE_SIZE, e[i].size);
            EXPECT_EQ((uint32_t)0xFFFFFFFFu, fs_size32(e[i].size));
        }
    }
    EXPECT_TRUE(found);

    uint64_t size = 0;
    bool dir = true;
    EXPECT_TRUE(fs->stat("/usb0/big.hdf", &size, &dir));
    EXPECT_EQ((uint64_t)FILE_SIZE, size);
    EXPECT_FALSE(dir);

    char buf[64];
    EXPECT_EQ(-1, fs->load("/usb0/big.hdf", buf, sizeof(buf))); // a file of 5 GiB does not fit 64 bytes, and does not wrap into them
}
