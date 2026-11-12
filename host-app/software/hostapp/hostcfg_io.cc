// T163 D5: the board's file access for hostcfg.cc - /Flash/trx/host.cfg through the FileManager.
#include "hostcfg.h"
#include "filemanager.h"
#include <string.h>

static int board_read(char *buf, int max)
{
    File *f = NULL;
    FRESULT r = FileManager::getFileManager()->fopen(HOSTCFG_DIR, HOSTCFG_FILE, FA_READ, &f);
    if (r == FR_NO_PATH) {                          // no folder yet (first start) or no volume: only a folder we can make says the volume is there
        FRESULT m = FileManager::getFileManager()->create_dir(HOSTCFG_DIR);
        r = (m == FR_OK || m == FR_EXIST) ? FR_NO_FILE : m;
    }
    if (r == FR_NO_FILE) {
        return -1;                                  // not there: the first start
    }
    if (r != FR_OK || !f) {
        return -2;                                  // there may be a file, the disk says otherwise: do not take over, do not write
    }
    uint32_t got = 0;
    r = f->read(buf, (uint32_t)max, &got);
    FileManager::getFileManager()->fclose(f);
    return (r == FR_OK) ? (int)got : -2;
}

static bool board_write(const char *buf, int len)
{
    FileManager *fm = FileManager::getFileManager();
    fm->create_dir(HOSTCFG_DIR);                    // exists or not: the open tells
    File *f = NULL;
    if (fm->fopen(HOSTCFG_DIR, HOSTCFG_FILE, FA_CREATE_ALWAYS | FA_WRITE, &f) != FR_OK || !f) {
        return false;
    }
    uint32_t wr = 0;
    FRESULT r = f->write(buf, (uint32_t)len, &wr);
    fm->fclose(f);
    return r == FR_OK && (int)wr == len;
}

static const HostCfgIo board_io = { board_read, board_write };

void hostcfg_install_board(void)
{
    hostcfg_install(&board_io);
}
