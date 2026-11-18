// The Temp RAM disk (T228 R21): TRX64-Ultimate mounts the DDR2 window __ram_disk_start..__ram_disk_limit (linker.x: 0x02000000..0x03000000, 16 MB) as a FAT volume
// "Temp" / "RAM Disk" in the file manager's root (FW filesystem/ramdisk.cc:16-44), and the file manager's temp folder follows "Temp Auto Cleanup" and "Temp Subfolders"
// (FW userinterface/userinterface.cc:175-176, :363-364). Here the monolith board profile does the same, after the board is known; the MultiCore host does not: a core
// writes its own DDR2 there (video.cc, T93 5.7).
#include "tempdisk.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"
#include "blockdev_ram.h"
#include <stdio.h>
#include <stdint.h>

extern uint8_t __ram_disk_start;
extern uint8_t __ram_disk_limit;

void tempdisk_init(void)
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    const int sz = 512;
    const int size = (int)(intptr_t)&__ram_disk_limit - (int)(intptr_t)&__ram_disk_start;
    BlockDevice *blk = new BlockDevice_Ram(&__ram_disk_start, sz, size / sz);
    {
        Partition *prt = new Partition(blk, 0, 0, 0);
        FileSystem *fs = new FileSystemFAT(prt);
        fs->format("RamDisk");
        delete fs;
        delete prt;
    }
    FileDevice *node = new FileDevice(blk, "Temp", "RAM Disk");
    node->attach_disk(512);
    node->probe();
    FileManager::getFileManager()->add_root_entry(node);
    printf("[core] Temp RAM disk: %d KB at 0x%08x\n", size / 1024, (unsigned)(intptr_t)&__ram_disk_start);
}

void tempdisk_set(bool auto_cleanup, bool subfolders)
{
    FileManager *fm = FileManager::getFileManager();
    fm->set_temp_auto_cleanup_enabled(auto_cleanup);
    fm->set_temp_use_cache_subfolder_enabled(subfolders);
}
