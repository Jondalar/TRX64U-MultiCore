// Directory listing of the board's CoreFs (coreapp/src/fm_fs.cc) on an exFAT volume whose directories span several clusters and sectors,
// with long names and spaces. Built with the core application's FatFs options (FF_USE_FASTSEEK 1, LFN buffer on the stack).
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
#include <set>
#include <string>
#include <string.h>
#include <unistd.h>

extern "C" void outbyte(int) { }
extern "C" uint32_t get_fattime(void) { return (46UL << 25) | (10 << 21) | (18 << 16); }

namespace {
struct Volume {
    std::string image;
    BlockDevice_Emulated *blk; Partition *prt; FileSystemFAT *fs; Node_DirectFS *node; bool ok;
    Volume() : blk(NULL), prt(NULL), fs(NULL), node(NULL), ok(false)
    {
        const char *dir = getenv("TMPDIR");
        image = std::string(dir ? dir : "/tmp") + "/fmfs_list_" + std::to_string((long)getpid()) + ".img";
        int fd = open(image.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) return;
        bool sized = (ftruncate(fd, (off_t)(64 * 1024 * 1024)) == 0);
        close(fd);
        if (!sized) return;
        blk = new BlockDevice_Emulated(image.c_str(), 512);
        prt = new Partition(blk, 0, 0, 0);
        uint8_t drv = ChanFATManager::getManager()->addDrive(prt);
        MKFS_PARM param; param.fmt = FM_EXFAT; param.n_fat = 1; param.n_root = 0; param.au_size = 4096; param.align = 0;
        uint8_t work[4096];
        char name[3] = { (char)('0' + drv), ':', 0 };
        FRESULT fres = f_mkfs(name, &param, work, sizeof(work));
        ChanFATManager::getManager()->removeDrive(drv);
        if (fres != FR_OK) return;
        fs = new FileSystemFAT(prt);
        if (!fs->init()) return;
        node = new Node_DirectFS(fs, "sd", AM_DIR);
        FileManager::getFileManager()->add_root_entry(node);
        ok = true;
    }
    ~Volume()
    {
        if (node) { FileManager::getFileManager()->remove_root_entry(node); delete node; }
        delete fs; delete prt; delete blk;
        unlink(image.c_str());
    }
};
Volume &volume() { static Volume v; return v; }

std::string name_of(int i)
{
    char b[64];
    snprintf(b, sizeof(b), "%s%d", (i % 3) == 0 ? "Long Name With Spaces " : (i % 3) == 1 ? "Entry_" : "x ", i);
    return b;
}
}

TEST(FmFsList, ExFatDirectoryOfManyEntriesListsEveryOne)
{
    ASSERT_TRUE(volume().ok);
    FileManager *fm = FileManager::getFileManager();
    const int N = 300;
    std::set<std::string> want;
    for (int i = 0; i < N; i++) {
        std::string n = name_of(i);
        want.insert(n);
        if (i % 5 == 0) {
            ASSERT_EQ(FR_OK, fm->create_dir(("/sd/" + n).c_str()));
        } else {
            File *f = NULL;
            ASSERT_EQ(FR_OK, fm->fopen("/sd", n.c_str(), FA_CREATE_ALWAYS | FA_WRITE, &f));
            fm->fclose(f);
        }
    }
    FsEntry *e = new FsEntry[400];
    int n = fm_fs()->list("/sd", e, 400);
    EXPECT_EQ(N, n);
    std::set<std::string> got;
    for (int i = 0; i < n; i++) got.insert(e[i].name);
    EXPECT_TRUE(got == want);
    delete[] e;
}

// T180 #38: a FAT long name through the board's CoreFs: listed whole by list(), cut at 55 and marked by list_short(), found by stat(), renamed, loaded and removed.
// The FileManager reads a directory entry into a FileInfo of INFO_SIZE (128) bytes (strncpy, so a name of 128 or more has no end byte there), and it comes back cut from
// the listing and from every look-up by name (vendor/gideon/software/filemanager/filemanager.h): 127 is the longest name this CoreFs can work with on the board, and the test
// says so for 255.
TEST(FmFsList, LongNamesAreListedWholeAndWorkWithTheirWholePath)
{
    ASSERT_TRUE(volume().ok);
    FileManager *fm = FileManager::getFileManager();
    std::string n80, n127, r127, n255;
    for (int i = 0; i < 76; i++) n80 += (char)('a' + i % 26);
    n80 += ".adf";
    for (int i = 0; i < 123; i++) n127 += (char)('a' + (i * 7) % 26);
    r127 = n127 + ".xfd";
    n127 += ".adf";
    for (int i = 0; i < 251; i++) n255 += (char)('a' + (i * 11) % 26);
    n255 += ".adf";
    ASSERT_EQ(80u, n80.size());
    ASSERT_EQ(127u, n127.size());
    ASSERT_EQ(127u, r127.size());
    ASSERT_EQ(255u, n255.size());
    ASSERT_EQ(FR_OK, fm->create_dir("/sd/longs"));
    for (const std::string &n : { n80, n127, n255 }) {
        File *f = NULL;
        ASSERT_EQ(FR_OK, fm->fopen("/sd/longs", n.c_str(), FA_CREATE_ALWAYS | FA_WRITE, &f));
        uint32_t w = 0;
        f->write("0123456789", 10, &w);
        fm->fclose(f);
    }
    FsEntry e[8];
    int n = fm_fs()->list("/sd/longs", e, 8);
    ASSERT_EQ(3, n);
    std::set<std::string> got;
    for (int i = 0; i < n; i++) got.insert(e[i].name);
    EXPECT_TRUE(got.count(n80) == 1);
    EXPECT_TRUE(got.count(n127) == 1);                               // every character of the longest name the FileManager lists
    EXPECT_TRUE(got.count(n255.substr(0, 128)) == 1);                // the FileManager's cut (INFO_SIZE 128, no end byte), not ours: FsEntry holds all 255 and is not read past the 128
    FsEntryShort s[8];
    ASSERT_EQ(3, fm_fs()->list_short("/sd/longs", s, 8));
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(55u, strlen(s[i].name));
        EXPECT_TRUE(s[i].cut);
        EXPECT_EQ(10u, (unsigned)s[i].size);
    }
    uint64_t size = 0;
    bool dir = true;
    EXPECT_TRUE(fm_fs()->stat(("/sd/longs/" + n127).c_str(), &size, &dir) && size == 10 && !dir);
    EXPECT_TRUE(fm_fs()->rename(("/sd/longs/" + n127).c_str(), ("/sd/longs/" + r127).c_str()));
    EXPECT_TRUE(!fm_fs()->stat(("/sd/longs/" + n127).c_str(), &size, &dir));
    EXPECT_TRUE(fm_fs()->stat(("/sd/longs/" + r127).c_str(), &size, &dir) && size == 10);
    uint8_t buf[16];
    EXPECT_EQ(10, fm_fs()->load(("/sd/longs/" + r127).c_str(), buf, sizeof(buf)));
    EXPECT_TRUE(fm_fs()->remove(("/sd/longs/" + r127).c_str()));
    EXPECT_TRUE(fm_fs()->remove(("/sd/longs/" + n80).c_str()));
    EXPECT_EQ(1, fm_fs()->list("/sd/longs", e, 8));                  // the 255 character file stays: the FileManager cannot name it
}
