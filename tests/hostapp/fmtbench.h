// Shared scaffolding of the T111 benches: a block device that keeps only the pages that were written (a 33 GiB medium costs a few MB),
// answers the way the board's devices do (GET_BLOCK_SIZE: RES_PARERR, like UsbScsi), and can fail the Nth write; and a binder that lets the
// REAL f_mkfs of FatFs write onto a Partition of it. The code under test is the product's: mbr.cc, mediasniff.cc, fmt.cc, the real
// disk.cc / partition.cc / diskio.cc / FileManager / FileDevice / FileSystemFAT, and ff.c with the board's ffconf.h (tests/hostapp/run.sh).
#ifndef TEST_FMTBENCH_H
#define TEST_FMTBENCH_H

#include "blockdev.h"
#include "partition.h"
#include "chanfat_manager.h"
#include "ff.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <unordered_map>
#include <vector>
#include <string>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

uint32_t bench_free_heap = 8000000;      // the board: configTOTAL_HEAP_SIZE 0x7A0000, a part of it in use (what stubfs/task.h answers)

extern "C" uint32_t get_fattime(void) { return (46UL << 25) | (10UL << 21) | (26UL << 16); }

class SparseDisk : public BlockDevice
{
public:
    std::unordered_map<uint64_t, std::vector<uint8_t> > pages;       // 4 KiB pages by index
    uint32_t nsect;                  // what GET_SECTOR_COUNT answers
    uint32_t ssize;                  // what GET_SECTOR_SIZE answers (the storage stays 512-byte based)
    uint8_t fill;                    // what a sector never written reads as ("old data")
    uint64_t writes, wsectors, reads;
    int64_t fail_nth;                // the Nth write call (1-based) fails with RES_ERROR and writes nothing; -1 = never
    int64_t gone_from;               // from the Nth write call on the medium is gone: state no media, RES_NOTRDY
    bool protect, capfail, swallow;  // write protected, GET_SECTOR_COUNT fails, writes are accepted and thrown away (and not counted)
    bool nodisk;
    bool log_writes;
    std::vector<uint32_t> write_lba; // first sector of every write call, in order (log_writes)

    SparseDisk(uint32_t sectors, uint8_t f = 0xA5) : nsect(sectors), ssize(512), fill(f), writes(0), wsectors(0), reads(0), fail_nth(-1),
        gone_from(-1), protect(false), capfail(false), swallow(false), nodisk(false), log_writes(false)
    {
        set_state(e_device_ready);
    }
    SparseDisk(const SparseDisk &o) : BlockDevice(), pages(o.pages), nsect(o.nsect), ssize(o.ssize), fill(o.fill), writes(0), wsectors(0), reads(0),
        fail_nth(-1), gone_from(-1), protect(false), capfail(false), swallow(false), nodisk(false), log_writes(false)
    {
        set_state(e_device_ready);
    }
    std::vector<uint8_t> &page(uint64_t pi)
    {
        std::vector<uint8_t> &p = pages[pi];
        if (p.empty()) {
            p.assign(4096, fill);
        }
        return p;
    }
    DSTATUS init(void) { return status(); }
    DSTATUS status(void) { return (protect ? STA_PROTECT : 0) | (nodisk ? STA_NODISK : 0); }
    DRESULT read(uint8_t *b, uint32_t s, int n)
    {
        if (get_state() != e_device_ready) {
            return RES_NOTRDY;
        }
        reads++;
        for (int i = 0; i < n; i++) {
            uint64_t lba = (uint64_t)s + i, pi = lba / 8;
            std::unordered_map<uint64_t, std::vector<uint8_t> >::iterator it = pages.find(pi);
            if (it == pages.end()) {
                memset(b + i * 512, fill, 512);
            } else {
                memcpy(b + i * 512, &it->second[(lba % 8) * 512], 512);
            }
        }
        return RES_OK;
    }
    DRESULT write(const uint8_t *b, uint32_t s, int n)
    {
        if (get_state() != e_device_ready) {
            return RES_NOTRDY;
        }
        if (swallow) {
            return RES_OK;
        }
        writes++;
        if (gone_from >= 0 && (int64_t)writes >= gone_from) {
            set_state(e_device_no_media);
            return RES_NOTRDY;
        }
        if (fail_nth >= 0 && (int64_t)writes == fail_nth) {
            return RES_ERROR;
        }
        if (log_writes) {
            write_lba.push_back(s);
        }
        wsectors += n;
        for (int i = 0; i < n; i++) {
            uint64_t lba = (uint64_t)s + i;
            memcpy(&page(lba / 8)[(lba % 8) * 512], b + i * 512, 512);
        }
        return RES_OK;
    }
    DRESULT ioctl(uint8_t c, void *d)
    {
        switch (c) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT:
            if (capfail) {
                return RES_ERROR;
            }
            *(uint32_t *)d = nsect;
            return RES_OK;
        case GET_SECTOR_SIZE: *(uint32_t *)d = ssize; return RES_OK;
        }
        return RES_PARERR;           // GET_BLOCK_SIZE and the rest: as UsbScsi::ioctl answers
    }
    // poke sectors without counting a write (preparing an image)
    void put(uint32_t lba, const void *src, int n = 1)
    {
        const uint8_t *p = (const uint8_t *)src;
        for (int i = 0; i < n; i++) {
            memcpy(&page((uint64_t)(lba + i) / 8)[((lba + i) % 8) * 512], p + i * 512, 512);
        }
    }
    void get(uint32_t lba, void *dst, int n = 1) { read((uint8_t *)dst, lba, n); }
    // a sparse file of the medium's size; the first `prefill` bytes are written out in full (written pages and the old-data fill), later pages only where written
    bool save(const char *path, uint64_t prefill)
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return false;
        }
        bool ok = ftruncate(fd, (off_t)nsect * 512) == 0;
        std::vector<uint8_t> fp(4096, fill);
        for (uint64_t pi = 0; pi * 4096 < prefill && pi * 4096 < (uint64_t)nsect * 512; pi++) {
            std::unordered_map<uint64_t, std::vector<uint8_t> >::iterator it = pages.find(pi);
            const std::vector<uint8_t> &p = it == pages.end() ? fp : it->second;
            ok = ok && pwrite(fd, p.data(), 4096, (off_t)(pi * 4096)) == 4096;
        }
        for (std::unordered_map<uint64_t, std::vector<uint8_t> >::iterator it = pages.begin(); it != pages.end(); ++it) {
            if (it->first * 4096 >= prefill) {
                ok = ok && pwrite(fd, it->second.data(), 4096, (off_t)(it->first * 4096)) == 4096;
            }
        }
        close(fd);
        return ok;
    }
};

// The real f_mkfs on a window [start, start+len) of the device: what the format does through its shim, here without one.
// fmt: FM_EXFAT, FM_FAT32, FM_FAT ... (| FM_SFD for no table of FatFs's own). Returns the FRESULT.
static FRESULT bench_mkfs(BlockDevice *dev, uint32_t start, uint32_t len, BYTE fmt, UINT align = 0, DWORD au = 0)
{
    static uint8_t work[65536];
    Partition p(dev, start, len, 0x07);
    uint8_t drv = ChanFATManager::getManager()->addDrive(&p);
    char path[4] = { (char)('0' + drv), ':', 0, 0 };
    MKFS_PARM parm;
    memset(&parm, 0, sizeof(parm));
    parm.fmt = fmt;
    parm.n_fat = 1;
    parm.align = align;
    parm.au_size = au;
    FRESULT fr = f_mkfs(path, &parm, work, sizeof(work));
    ChanFATManager::getManager()->removeDrive(drv);
    return fr;
}

static const uint32_t MiB_S = 2048;            // sectors per MiB
static const uint32_t GiB_S = 2048 * 1024;     // sectors per GiB

#endif
