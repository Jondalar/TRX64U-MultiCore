// Host test of the scratch file's reservation (T115 D2, flash file): the REAL FatFS (vendor/gideon chan_fat ff.c, f_expand switched on as
// host-app/software/hostapp/ffconf.h does) over a RAM disk, and the chain walker of the host app (fatwalk.cc) as the judge.
// Board, 2026-10-24: a 2.2 MB file written with plain f_write was not one run of clusters even on an almost empty flash drive.
//   S1 a file written without a reservation over a drive with scattered free clusters is NOT one run (this is the defect)
//   S2 the same drive, the same file, reserved with f_expand(fp, size, 1) first: ONE run, bytes read back identical
//   S3 no free run of that length: f_expand says FR_DENIED, nothing is allocated, the text names the size to tidy up for
// argv[1] = 0: the reservation is skipped in S2 (the negative control: S2's checks must go red).
#include "fatwalk.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const uint32_t SS = 512, NSECT = 8192;                   // 4 MiB
static std::vector<uint8_t> disk;

extern "C" {
DSTATUS disk_initialize(uint8_t) { return 0; }
DSTATUS disk_status(uint8_t) { return 0; }
DRESULT disk_read(uint8_t, uint8_t *b, uint32_t s, uint32_t n) { memcpy(b, &disk[(size_t)s * SS], (size_t)n * SS); return RES_OK; }
DRESULT disk_write(uint8_t, const uint8_t *b, uint32_t s, uint32_t n) { memcpy(&disk[(size_t)s * SS], b, (size_t)n * SS); return RES_OK; }
DRESULT disk_ioctl(uint8_t, uint8_t cmd, void *b)
{
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(uint32_t *)b = NSECT; return RES_OK;
    case GET_SECTOR_SIZE: *(uint16_t *)b = SS; return RES_OK;
    case GET_BLOCK_SIZE: *(uint32_t *)b = 8; return RES_OK;
    }
    fprintf(stderr, "ioctl %d\n", cmd); return RES_PARERR;
}
DWORD get_fattime(void) { return (46UL << 25) | (10UL << 21) | (24UL << 16); }
}

static bool rd(void *, uint32_t off, void *buf, uint32_t n)
{
    if ((uint64_t)off + n > disk.size()) return false;
    memcpy(buf, &disk[off], n);
    return true;
}

static const uint32_t CL = 4096;
static const uint32_t BIG = 100 * CL + 123;                      // the "bitstream": 101 clusters

// a fresh 4 MiB FAT12 drive with 1 cluster = 8 sectors; every other cluster of the first 2*pairs clusters holds a small file
static FATFS fs;
// a start of the host: the allocation hint (last allocated cluster) is gone, the first write starts at the bottom of the drive
static void remount()
{
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    memset(&fs, 0, sizeof(fs));
    CHECK(f_mount(&fs, "0:", 1) == FR_OK);
}

static void fresh(int pairs)
{
    disk.assign((size_t)NSECT * SS, 0);
    MKFS_PARM p = { FM_ANY | FM_SFD, 1, 0, 0, CL };
    static BYTE work[4096];
    FRESULT mk = f_mkfs("0:", &p, work, sizeof(work));
    if (mk != FR_OK) printf("mkfs error %d\n", (int)mk);
    CHECK(mk == FR_OK);
    memset(&fs, 0, sizeof(fs));
    CHECK(f_mount(&fs, "0:", 1) == FR_OK);
    for (int i = 0; i < 2 * pairs; i++) {                       // small files in a row, then every second one deleted: holes of one cluster
        char nm[32];
        snprintf(nm, sizeof(nm), "0:/s%03d.bin", i);
        FIL f;
        CHECK(f_open(&f, nm, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        UINT bw;
        CHECK(f_write(&f, nm, 8, &bw) == FR_OK && bw == 8);
        CHECK(f_close(&f) == FR_OK);
    }
    for (int i = 0; i < 2 * pairs; i += 2) {
        char nm[32];
        snprintf(nm, sizeof(nm), "0:/s%03d.bin", i);
        CHECK(f_unlink(nm) == FR_OK);
    }
    remount();
}

// the file of `size` bytes whose pattern is i*7+3; reserve: f_expand first. Returns FRESULT of the reservation (FR_OK without one).
static FRESULT put(const char *nm, uint32_t size, bool reserve)
{
    FIL f;
    FRESULT r = FR_OK;
    CHECK(f_open(&f, nm, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    if (reserve) {
        r = f_expand(&f, size, 1);
        if (r != FR_OK) {
            f_close(&f);
            f_unlink(nm);
            return r;
        }
    }
    std::vector<uint8_t> d(size);
    for (uint32_t i = 0; i < size; i++) d[i] = (uint8_t)(i * 7 + 3);
    for (uint32_t done = 0; done < size;) {
        uint32_t n = size - done > 65536 ? 65536 : size - done;
        UINT bw = 0;
        CHECK(f_write(&f, &d[done], n, &bw) == FR_OK && bw == n);
        done += n;
    }
    CHECK(f_close(&f) == FR_OK);
    return r;
}

static fat_result_t judge(const char *nm, uint32_t size, uint32_t *vol_off, uint32_t *first_out)
{
    FatGeo g;
    CHECK(fat_geometry(rd, NULL, &g) == FAT_OK);
    FIL f;
    CHECK(f_open(&f, nm, FA_READ) == FR_OK);
    uint32_t first = f.obj.sclust;
    CHECK(f.obj.objsize == size);
    f_close(&f);
    if (first_out) *first_out = first;
    return fat_contiguous(&g, rd, NULL, first, size, vol_off);
}

int main(int argc, char **argv)
{
    bool reserve = !(argc > 1 && !strcmp(argv[1], "0"));

    // S1: scattered free clusters in front, a long free run behind. Without a reservation FatFS follows its hint through the holes.
    fresh(20);
    CHECK(put("0:/big.bit", BIG, false) == FR_OK);
    uint32_t off = 0, first = 0;
    fat_result_t plain = judge("0:/big.bit", BIG, &off, &first);
    CHECK(plain == FAT_ERR_FRAGMENTED);                          // the defect, reproduced; if FatFS ever lays it out in one run this goes red

    // S2: the same drive, the same file, with the reservation (the control runs this block without it)
    fresh(20);
    CHECK(put("0:/big.bit", BIG, reserve) == FR_OK);
    fat_result_t res = judge("0:/big.bit", BIG, &off, &first);
    CHECK(res == FAT_OK);
    if (res == FAT_OK) {
        FatGeo g;
        fat_geometry(rd, NULL, &g);
        CHECK(off == g.data_off + (first - 2) * g.csize);
        bool same = true;
        for (uint32_t i = 0; i < BIG; i++) same = same && disk[off + i] == (uint8_t)(i * 7 + 3);   // the bytes are where the walker says
        CHECK(same);
    }
    FILINFO fi;
    CHECK(f_stat("0:/s001.bin", &fi) == FR_OK && fi.fsize == 8);       // the files that were there are untouched
    CHECK(f_stat("0:/s039.bin", &fi) == FR_OK && fi.fsize == 8);

    // S3: free space in plenty, but never 101 clusters in a row: the survivors sit at every second cluster of the whole drive
    {
        fresh(0);
        FatGeo g;
        CHECK(fat_geometry(rd, NULL, &g) == FAT_OK);
        for (uint32_t i = 0; i < g.nclust / 3; i++) {           // three clusters each (the root directory has 512 entries), odd ones deleted
            char nm[32];
            snprintf(nm, sizeof(nm), "0:/f%04u", (unsigned)i);
            FIL f;
            if (f_open(&f, nm, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) break;
            UINT bw;
            static BYTE three[3 * CL];
            f_write(&f, three, sizeof(three), &bw);
            f_close(&f);
        }
        for (uint32_t i = 1; i < g.nclust / 3; i += 2) {
            char nm[32];
            snprintf(nm, sizeof(nm), "0:/f%04u", (unsigned)i);
            f_unlink(nm);
        }
        remount();
        DWORD freec = 0;
        FATFS *pf;
        CHECK(f_getfree("0:", &freec, &pf) == FR_OK && freec > 101);     // the sum would do, the biggest hole does not
        FIL f;
        CHECK(f_open(&f, "0:/big.bit", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        CHECK(f_expand(&f, BIG, 1) == FR_DENIED);
        f_close(&f);
        f_unlink("0:/big.bit");
        DWORD freec2 = 0;
        CHECK(f_getfree("0:", &freec2, &pf) == FR_OK && freec2 == freec);   // a refused reservation allocated nothing
        char txt[200];
        fat_no_run_text(txt, sizeof(txt), &g, BIG);
        printf("%s\n", txt);
        CHECK(strstr(txt, "404 KiB (101 clusters)") != NULL);   // 101 * 4096 = 413696 = 404 KiB
        CHECK(strstr(txt, "Tidy the flash drive") != NULL);
        CHECK(fat_run_bytes(&g, 1) == CL && fat_run_bytes(&g, CL) == CL && fat_run_bytes(&g, CL + 1) == 2 * CL);
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
