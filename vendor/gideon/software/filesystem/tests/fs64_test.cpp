// T104.1 D16: file sizes and offsets are 64 bit. A real exFAT volume of 6 GiB (a sparse image file on the Mac) goes through the
// firmware's own stack - BlockDevice, Partition, FatFS, FileSystemFAT, FileManager, the VFS the FTP server uses - and a file of
// more than 4 GiB is written, read back, listed and reported. Two markers in the file, one at 1 GiB and one at 5 GiB, tell a
// seek that wraps at 32 bit (5 GiB reads as 1 GiB) from a correct one. run.sh builds the same test against copies of the product
// sources that are broken on purpose (32 bit size, 32 bit seek, ...), and every one of them must fail.
#include "../../io/usb/tests/host_test/host_test.h"
#include "blockdev_emul.h"
#include "partition.h"
#include "filesystem_fat.h"
#include "chanfat_manager.h"
#include "filemanager.h"
#include "node_directfs.h"
#include "vfs.h"
#include <stdio.h>
#include <stdarg.h>
// the FTP lines are formatted with the firmware's printf (small_printf.cc, linked under the name fw_*), as on the board
extern "C" int fw_sprintf(char *, const char *, ...);
extern "C" int fw_snprintf(char *, size_t, const char *, ...);
#define sprintf fw_sprintf
#define snprintf fw_snprintf
#include "ftp_format.h"
#undef sprintf
#undef snprintf
#include "size_str.h"
#include "json.h"

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
const uint64_t MARK_LOW = 1 * GiB; // 5 GiB modulo 4 GiB
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
        image = std::string(dir ? dir : "/tmp") + "/fs64_test_" + std::to_string((long)getpid()) + ".img";
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
        node = new Node_DirectFS(fs, "Disk", AM_DIR);
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

bool write_at(File *f, uint64_t pos, char c)
{
    uint8_t block[16];
    memset(block, c, sizeof(block));
    uint32_t n = 0;
    return (f->seek(pos) == FR_OK) && (f->write(block, sizeof(block), &n) == FR_OK) && (n == sizeof(block));
}

bool read_at(File *f, uint64_t pos, char c)
{
    uint8_t block[16];
    uint32_t n = 0;
    if ((f->seek(pos) != FR_OK) || (f->read(block, sizeof(block), &n) != FR_OK) || (n != sizeof(block))) {
        return false;
    }
    for (size_t i = 0; i < sizeof(block); i++) {
        if (block[i] != (uint8_t)c) {
            return false;
        }
    }
    return true;
}

void make_big_file()
{
    ASSERT_TRUE(volume().ok);
    FileManager *fm = FileManager::getFileManager();
    File *f = NULL;
    ASSERT_EQ(FR_OK, fm->fopen("/Disk", "big.hdf", FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &f));
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(write_at(f, MARK_LOW, 'A'));
    ASSERT_TRUE(write_at(f, MARK_HIGH, 'B'));
    EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)f->get_size());
    fm->fclose(f);
}

} // namespace

TEST(Fs64, FileOfMoreThan4GiBKeepsSizeAndOffsets)
{
    make_big_file();
    FileManager *fm = FileManager::getFileManager();
    File *f = NULL;
    ASSERT_EQ(FR_OK, fm->fopen("/Disk", "big.hdf", FA_READ, &f));
    EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)f->get_size());
    EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)f->get_size64());
    EXPECT_EQ((uint32_t)0xFFFFFFFFu, f->get_size32());
    EXPECT_TRUE(read_at(f, MARK_HIGH, 'B'));
    EXPECT_TRUE(read_at(f, MARK_LOW, 'A'));
    EXPECT_EQ(FR_OK, f->seek64(MARK_HIGH));
    uint8_t b = 0;
    uint32_t n = 0;
    EXPECT_EQ(FR_OK, f->read(&b, 1, &n));
    EXPECT_EQ((int)'B', (int)b);
    fm->fclose(f);
}

TEST(Fs64, DirectoryEntryAndFstatReportTheRealSize)
{
    make_big_file();
    FileManager *fm = FileManager::getFileManager();
    FileInfo info(64);
    ASSERT_EQ(FR_OK, fm->fstat("/Disk", "big.hdf", info));
    EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)info.size);
    EXPECT_EQ((uint32_t)0xFFFFFFFFu, info.size32());

    FileInfo copy(info);
    EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)copy.size);
}

TEST(Fs64, FtpSizeListAndMlsdReportTheRealSize)
{
    make_big_file();
    vfs_t *v = vfs_openfs();
    vfs_stat_t st;
    ASSERT_EQ(0, vfs_stat(v, "/Disk/big.hdf", &st));
    EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)st.st_size);

    char line[300];
    ftp_fmt_size_reply(line, st.st_size);
    EXPECT_TRUE(std::string(line) == "213 5368709136");

    ftp_fmt_list_line(line, &st, "Oct", true);
    EXPECT_TRUE(strstr(line, "ftp   5368709136 Oct") != NULL);
    ftp_fmt_list_line(line, &st, "Oct", false);
    EXPECT_TRUE(strstr(line, "ftp   5368709136 Oct") != NULL);
    ftp_fmt_mlsd_line(line, &st);
    EXPECT_TRUE(strstr(line, "type=file;size=5368709136;") != NULL);
    ftp_fmt_mlst_file(line, sizeof(line), &st, "file");
    EXPECT_TRUE(strstr(line, "type=file;size=5368709136;") != NULL);

    vfs_dir_t *dir = vfs_opendir(v, "/Disk", e_vfs_files_only);
    ASSERT_TRUE(dir != NULL);
    bool found = false;
    for (vfs_dirent_t *e = vfs_readdir(dir); e; e = vfs_readdir(dir)) {
        vfs_stat_t es;
        vfs_stat_dirent(e, &es);
        if (!strcmp(es.name, "big.hdf")) {
            found = true;
            EXPECT_EQ((uint64_t)FILE_SIZE, (uint64_t)es.st_size);
        }
    }
    EXPECT_TRUE(found);
    vfs_closedir(dir);
    vfs_closefs(v);
}

TEST(Fs64, RestAndUserInterfaceShowTheRealSize)
{
    make_big_file();
    FileInfo info(64);
    ASSERT_EQ(FR_OK, FileManager::getFileManager()->fstat("/Disk", "big.hdf", info));
    JSON_Object *o = JSON::Obj();
    o->add_u64("size", info.size);
    EXPECT_TRUE(strstr(o->render(), "5368709136") != NULL);
    delete o;

    char text[16];
    size_to_string_bytes(info.size, text);
    EXPECT_TRUE(std::string(text) == "5120M");
    size_to_string_bytes(20 * GiB, text);
    EXPECT_TRUE(std::string(text) == "  20G");
    char big[24];
    EXPECT_TRUE(std::string(u64_dec(0xFFFFFFFFFFFFFFFFULL, big)) == "18446744073709551615");
}

TEST(Fs64, FilesUnder4GiBBehaveAsBefore)
{
    ASSERT_TRUE(volume().ok);
    FileManager *fm = FileManager::getFileManager();
    File *f = NULL;
    ASSERT_EQ(FR_OK, fm->fopen("/Disk", "small.bin", FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &f));
    uint8_t data[1234];
    memset(data, 0x5A, sizeof(data));
    uint32_t n = 0;
    ASSERT_EQ(FR_OK, f->write(data, sizeof(data), &n));
    EXPECT_EQ((uint64_t)1234, (uint64_t)f->get_size());
    EXPECT_EQ((uint32_t)1234, f->get_size32());
    EXPECT_EQ(FR_OK, f->seek(10));
    fm->fclose(f);
    FileInfo info(64);
    ASSERT_EQ(FR_OK, fm->fstat("/Disk", "small.bin", info));
    EXPECT_EQ((uint64_t)1234, (uint64_t)info.size);
    EXPECT_EQ((uint32_t)1234, info.size32());
}
