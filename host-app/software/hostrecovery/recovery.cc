// T265 P2: the recovery program of the host (docs/specs/T265/p1-p3-guard-and-recovery.md 2). The boot ROM starts it from the slot below the host application when the
// application in the flash is not the one the guard's record names. It finds a copy of the host application, stages it at 0x03000000, checks it, copies it to 0x30000 and
// starts it; the host application then sees that it was started by the recovery (guard.h) and repairs the flash. It writes nothing: the flash drive is attached read-only
// and never formatted (never init_flash_disk(): that formats on a failed probe), the media are only read, and no flash write path is called.
//
// Order (D6, Q4): /Flash/trx/hostrecovery.app, /Flash/trx/hostrecovery.new (the cut between the old copy's removal and the rename), the SD card's root, the root of every USB
// volume. While nothing is found it keeps looking (a stick put in later works) and says so on the console; a JTAG load still works (the boot ROM takes it at the next reset).
// It needs no clock synthesizer and no HDMI: the CPU and USB clocks come from the Ethernet PHY clock; the USB hub is configured over I2C (custom_hardware_init below).
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "u64.h"
#include "iomap.h"
#include "usb_base.h"
#include "usb_nano.h"
#include "hw_i2c_drv.h"
#include "i2c_drv.h"
#include "filemanager.h"
#include "file_device.h"
#include "file_info.h"
#include "indexed_list.h"
#include "path.h"
#include "blockdev_flash.h"
#include "flash.h"
#include "guardimg.h"

void initialize_usb_hub();                         // usb_hwinit.cc: the hub over I2C (C++ linkage, as u64ii_init.cc declares it)

// the recovery block the host application reads (guard.h GUARD_BLK_ADDR; the numbers are repeated in guardimg.h and checked against guard.h by guard.cc)
#define REC_BLK        ((volatile uint32_t *)GUARDIMG_BLK_ADDR)
#define STAGE_ADDR     0x03000000u
#define STAGE_MAX      0x001E0000u                    // the application slot: the staging area has room for a good deal more, the boot ROM takes no more
#define SEARCH_MS      500

enum { M_FLASH_COPY = 1, M_FLASH_NEW, M_SD, M_USB };

extern "C" void custom_hardware_init()
{
    // no audio codec, no HDMI, no keyboard expander: only the I2C master and the USB hub behind it
    i2c = new Hw_I2C_Driver((volatile t_hw_i2c *)U64II_HW_I2C_BASE);
    initialize_usb_hub();
}

// the flash drive, read-only: the attach of init_flash_disk without its format (update_trx.cc mount_flash_disk is the same, with a probe)
class FlashRo : public BlockDevice_Flash {
public:
    explicit FlashRo(Flash *f) : BlockDevice_Flash(f) { }
    DRESULT write(const uint8_t *, uint32_t, int) { return RES_WRPRT; }
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

// Reads path into the stage; true when it is a host application (guardimg.h). *total: the file's length.
static bool stage_file(const char *path, uint32_t *total)
{
    FileManager *fm = FileManager::getFileManager();
    File *f = NULL;
    if (fm->fopen(path, FA_READ, &f) != FR_OK || !f) {
        return false;
    }
    uint32_t size = f->get_size32();
    bool ok = size >= 12 && size <= STAGE_MAX + 12;
    uint8_t *dst = (uint8_t *)STAGE_ADDR;
    uint32_t done = 0;
    while (ok && done < size) {
        uint32_t n = size - done > 32768 ? 32768 : size - done, got = 0;
        ok = f->read(dst + done, n, &got) == FR_OK && got == n;
        done += n;
    }
    fm->fclose(f);
    if (ok && guardimg_ok(dst, size, total)) {
        return true;
    }
    return false;
}

// the guard's record as the flash holds it: does it name this CRC? (information for the application: a copy that differs from the record is taken all the same)
static bool record_matches(uint32_t len, uint32_t crc)
{
    Flash *fl = get_flash();
    uint8_t pg[256] __attribute__((aligned(4)));
    if (!fl || !fl->read_page((int)(GUARDIMG_RECORD_ADDR / 256), pg)) {
        return false;
    }
    uint32_t m = guardimg_le32(pg), l = guardimg_le32(pg + 4), c = guardimg_le32(pg + 8), k = guardimg_le32(pg + 12);
    return m == GUARDIMG_RECORD_MAGIC && k == ~(m ^ l ^ c) && l == len && c == crc;
}

static void start_stage(int medium, uint32_t total)
{
    uint8_t *stage = (uint8_t *)STAGE_ADDR;
    uint32_t dest = guardimg_le32(stage), len = guardimg_le32(stage + 4), run = guardimg_le32(stage + 8);
    uint32_t crc = crc32_update(0, stage, total);
    bool same = record_matches(total, crc);
    printf("recovery: starting the application from %s: %u bytes, crc %08x, record %s\n", medium == M_FLASH_COPY ? "the flash drive" : medium == M_FLASH_NEW ? "the flash drive (new)" : medium == M_SD ? "the SD card" : "a USB stick",
           (unsigned)total, (unsigned)crc, same ? "equal" : "differs");
    const uint32_t w[7] = { GUARDIMG_RECOVERY_MAGIC, (uint32_t)medium, STAGE_ADDR, total, crc, same ? 1u : 0u, 0u };
    for (int i = 1; i < 7; i++) {
        REC_BLK[i] = w[i];
    }
    REC_BLK[7] = ~(w[0] ^ w[1] ^ w[2] ^ w[3] ^ w[4] ^ w[5] ^ w[6]);           // guard_recovery_chk
    REC_BLK[0] = w[0];                                                         // the magic last: a block is valid only whole
    // nothing runs behind the application's back: the USB engine stops, no interrupt reaches it
    NANO_START = 0;
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    portDISABLE_INTERRUPTS();
    memcpy((void *)dest, stage + 12, len);
    __asm__ volatile ("fence" ::: "memory");
    (void)*(volatile uint32_t *)(dest + ((len + 3) & ~3u) - 4);                 // posted stores are done
    ((void (*)(void))run)();
    for (;;) {
    }
}

static bool try_flash_file(const char *path, int medium)
{
    uint32_t total;
    if (stage_file(path, &total)) {
        start_stage(medium, total);
        return true;
    }
    return false;
}

static void search(void)
{
    FileManager *fm = FileManager::getFileManager();
    uint32_t total;
    if (try_flash_file("/Flash/trx/hostrecovery.app", M_FLASH_COPY) || try_flash_file("/Flash/trx/hostrecovery.new", M_FLASH_NEW)) {
        return;
    }
    if (stage_file("/SD/hostrecovery.app", &total)) {
        start_stage(M_SD, total);
        return;
    }
    // the root of every USB volume: the root of the file manager lists the volumes ("Usb0", "Usb1", ...)
    Path p;
    p.cd("/");
    IndexedList<FileInfo *> infos(16, NULL);
    if (fm->get_directory(&p, infos, NULL) == FR_OK) {
        for (int i = 0; i < infos.get_elements(); i++) {
            const char *n = infos[i]->lfname;
            if (n && !strncasecmp(n, "Usb", 3)) {
                char path[64];
                snprintf(path, sizeof(path), "/%s/hostrecovery.app", n);
                if (stage_file(path, &total)) {
                    for (int k = 0; k < infos.get_elements(); k++) {
                        delete infos[k];
                    }
                    start_stage(M_USB, total);
                    return;
                }
            }
        }
        for (int i = 0; i < infos.get_elements(); i++) {
            delete infos[i];
        }
    }
}

extern "C" void ultimate_main(void *)
{
    puts("\n*** TRX64 host recovery program ***");
    puts("The application in the flash is not the host's own. Looking for hostrecovery.app: the flash drive, the SD card, the USB sticks.");
    Flash *fl = get_flash();
    if (fl) {
        BlockDevice *blk = new FlashRo(fl);
        FileDevice *node = new FileDevice(blk, "Flash", "Flash Disk");
        node->attach_disk(fl->get_sector_size(0));
        if (node->probe() > 0) {                          // a drive that does not read is left alone: never formatted here
            FileManager::getFileManager()->add_root_entry(node);
        } else {
            puts("recovery: the flash drive does not read; not formatted here");
        }
    }
    usb2.initHardware();                                  // the USB task enumerates in the background; the SD card has its own poll task (sdcard_manager.cc)
    uint32_t waited = 0;
    for (;;) {
        search();
        vTaskDelay(pdMS_TO_TICKS(SEARCH_MS));
        waited += SEARCH_MS;
        if (waited % 5000 == 0) {
            puts("recovery: no hostrecovery.app found. Copy it to the root of a USB stick or the SD card (it is in the release), or load the host over JTAG.");
        }
    }
}
