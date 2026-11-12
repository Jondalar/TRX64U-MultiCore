// Bench of the fast seek of the core application (T104 5.16): the REAL FatFs (vendor/gideon chan_fat ff.c, built with coreapp/ffconf/ffconf.h,
// i.e. the application's own configuration plus FF_USE_FASTSEEK) over a RAM disk of 512 byte sectors and one sector per cluster, and the
// policy of coreapp/src/fastseek.cc as the thing under test. The judge is the number of FAT sectors the disk driver is asked for per
// seek + read of one block, which is what a ZX Next / Amiga block request costs on the board besides the data sector itself.
//   S1 a 16 MiB image in 256 fragments: the first random access builds the map (the first table is too small, the second has exactly the
//      size FatFs asked for), 300 random blocks read back byte for byte, and no seek costs more than FAT_BOUND FAT sectors
//   S2 the same image without the map: the cost grows with the offset (the far blocks cost thousands) - the number the map removes
//   S3 an image in more fragments than FASTSEEK_MAX_ITEMS allows (one cluster each, 32768): no map, FIL::cltbl cleared, the blocks still read correctly
//   S4 policy: a sequential read never builds the map, a small file never, one access builds it once; a write that reaches beyond the end
//      drops it and the extension succeeds; the same write through the map is what FatFs refuses (FR_DISK_FULL), which is why it is dropped
//   S6 the same on an exFAT volume
//   S5 a write inside the file through the map lands where the plain seek puts it
// argv[1] = 0: the map is never built (control: the bound of S1 has to go red).
#include "fastseek.h"
#include "diskio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#if !FF_USE_FASTSEEK
#error the coreapp ffconf.h does not switch FF_USE_FASTSEEK on
#endif

static const uint32_t SS = 512, NSECT = 262144;                  // 128 MiB, FAT32 with one sector per cluster
static std::vector<uint8_t> disk;
static uint64_t fat_reads, all_reads;
static uint32_t data_start;                                      // first sector of the data area: below it a read is FAT (or reserved) area

extern "C" {
DSTATUS disk_initialize(uint8_t) { return 0; }
DSTATUS disk_status(uint8_t) { return 0; }
DRESULT disk_read(uint8_t, uint8_t *b, uint32_t s, uint32_t n)
{
    all_reads += n;
    if (data_start && s < data_start) fat_reads += n;
    memcpy(b, &disk[(size_t)s * SS], (size_t)n * SS);
    return RES_OK;
}
DRESULT disk_write(uint8_t, const uint8_t *b, uint32_t s, uint32_t n) { memcpy(&disk[(size_t)s * SS], b, (size_t)n * SS); return RES_OK; }
DRESULT disk_ioctl(uint8_t, uint8_t cmd, void *b)
{
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(uint32_t *)b = NSECT; return RES_OK;
    case GET_SECTOR_SIZE: *(uint32_t *)b = SS; return RES_OK;      // a DWORD in this FatFs (ssize)
    case GET_BLOCK_SIZE: *(uint32_t *)b = 1; return RES_OK;
    }
    fprintf(stderr, "disk_ioctl: command %d not handled\n", cmd);
    return RES_PARERR;
}
DWORD get_fattime(void) { return (46UL << 25) | (10UL << 21) | (24UL << 16); }
}

static FATFS fs;
static BYTE work[4096];

static void format(BYTE fmt = FM_FAT32, uint32_t au = SS)
{
    disk.assign((size_t)NSECT * SS, 0);
    MKFS_PARM p = { (BYTE)(fmt | FM_SFD), 1, 0, 0, au };
    FRESULT mk = f_mkfs("0:", &p, work, sizeof(work));
    if (mk != FR_OK) { printf("FAIL f_mkfs error %d\n", (int)mk); exit(1); }
    memset(&fs, 0, sizeof(fs));
    FRESULT mt = f_mount(&fs, "0:", 1);
    if (mt != FR_OK) { printf("FAIL f_mount error %d\n", (int)mt); exit(1); }
    data_start = (uint32_t)fs.database;
}

static uint8_t pat(uint32_t tag, uint64_t i) { return (uint8_t)(i * 31 + (i >> 9) * 7 + tag); }

// Two files grown alternately, `chunk` bytes each time: both end up in size/chunk fragments. Returns with a.bin and b.bin written.
static void interleave(uint32_t size, uint32_t chunk)
{
    FIL a, b;
    CHECK(f_open(&a, "0:/a.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    CHECK(f_open(&b, "0:/b.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    std::vector<uint8_t> da(chunk), db(chunk);
    for (uint32_t done = 0; done < size; done += chunk) {
        for (uint32_t i = 0; i < chunk; i++) { da[i] = pat(1, done + i); db[i] = pat(2, done + i); }
        UINT bw;
        CHECK(f_write(&a, da.data(), chunk, &bw) == FR_OK && bw == chunk);
        CHECK(f_write(&b, db.data(), chunk, &bw) == FR_OK && bw == chunk);
    }
    CHECK(f_close(&a) == FR_OK);
    CHECK(f_close(&b) == FR_OK);
}

static uint32_t lcg = 12345;
static uint32_t rnd() { lcg = lcg * 1664525u + 1013904223u; return lcg >> 8; }

struct Stat { uint64_t total, max; uint32_t n; bool bytes_ok; };

// n random block reads, each with the policy's access() in front, as the CoreFile does; FAT sectors per read are collected
static Stat blocks(FIL *f, FastSeek *fsk, uint32_t size, int n, bool use_map, uint32_t tag, bool far_only)
{
    Stat st = { 0, 0, 0, true };
    uint8_t buf[512];
    for (int k = 0; k < n; k++) {
        uint64_t off = (uint64_t)(rnd() % (size / 512)) * 512;
        if (far_only && off < size / 2) off += size / 2;
        if (use_map) fsk->access(f, off, 512, false, "test.img");
        uint64_t before = fat_reads;
        UINT br = 0;
        bool ok = f_lseek(f, off) == FR_OK && f_read(f, buf, 512, &br) == FR_OK && br == 512;
        CHECK(ok);
        uint64_t cost = fat_reads - before;
        st.total += cost; if (cost > st.max) st.max = cost; st.n++;
        for (int i = 0; i < 512 && ok; i++) if (buf[i] != pat(tag, off + i)) { st.bytes_ok = false; break; }
    }
    return st;
}

int main(int argc, char **argv)
{
    bool map_on = !(argc > 1 && !strcmp(argv[1], "0"));
    const uint32_t SIZE = 16u << 20, FAT_BOUND = 2;

    // S1: 256 fragments of 64 KiB. The first table (FASTSEEK_START_ITEMS = 62 fragments) is too small: the second try has the needed size.
    format();
    interleave(SIZE, 64 * 1024);
    FIL f;
    CHECK(f_open(&f, "0:/a.bin", FA_READ | FA_WRITE) == FR_OK);
    CHECK(f.obj.objsize == SIZE);
    FastSeek fsk;
    CHECK(f.cltbl == 0);
    if (map_on) fsk.access(&f, 4096, 512, false, "test.img");       // an access that does not continue the previous one (offset 0)
    CHECK(fsk.active() == map_on);
    if (map_on) {
        CHECK(f.cltbl != 0 && f.cltbl[0] == 2 + 2 * 256);          // 256 fragments: table = header + 2 per fragment + terminator
        CHECK(f.cltbl != 0 && f.cltbl[0] > FASTSEEK_START_ITEMS);  // so the table was grown once
    }
    lcg = 12345;
    Stat on = blocks(&f, &fsk, SIZE, 300, map_on, 1, false);
    printf("S1 map %s: %u random blocks of a 16 MiB image in 256 fragments: FAT sectors per seek+read: avg %.2f, max %u\n",
           map_on ? "on " : "off", on.n, (double)on.total / on.n, (unsigned)on.max);
    CHECK(on.bytes_ok);
    CHECK(on.max <= FAT_BOUND);

    // S2: the same file, no map: far blocks walk the chain from the start of the file
    if (map_on) {
        FIL g;
        CHECK(f_open(&g, "0:/a.bin", FA_READ) == FR_OK);
        lcg = 12345;
        Stat off = blocks(&g, NULL, SIZE, 300, false, 1, false);
        Stat far = blocks(&g, NULL, SIZE, 100, false, 1, true);
        printf("S2 map off: same blocks: FAT sectors per seek+read: avg %.2f, max %u; blocks in the second half: avg %.2f, max %u\n",
               (double)off.total / off.n, (unsigned)off.max, (double)far.total / far.n, (unsigned)far.max);
        CHECK(off.bytes_ok && far.bytes_ok);
        CHECK(off.max > 50 * FAT_BOUND);                           // the defect, reproduced; the map must beat it by a wide margin
        CHECK(far.total / far.n > 100);
        f_close(&g);
    }

    // S5: a write inside the file through the map lands where a plain seek puts it, and the other file is untouched
    if (map_on) {
        uint8_t blk[512], rd[512];
        memset(blk, 0xA5, sizeof(blk));
        uint64_t off = 11u * 1024 * 1024 + 512 * 3;
        fsk.access(&f, off, 512, true, "test.img");
        CHECK(fsk.active());                                       // inside the file: the map stays
        UINT bw = 0;
        CHECK(f_lseek(&f, off) == FR_OK && f_write(&f, blk, 512, &bw) == FR_OK && bw == 512);
        CHECK(f_sync(&f) == FR_OK);
        FIL h;
        CHECK(f_open(&h, "0:/a.bin", FA_READ) == FR_OK);           // a second open file object without a map: the plain walk
        UINT br = 0;
        CHECK(f_lseek(&h, off) == FR_OK && f_read(&h, rd, 512, &br) == FR_OK && br == 512 && !memcmp(rd, blk, 512));
        f_close(&h);
        CHECK(f_open(&h, "0:/b.bin", FA_READ) == FR_OK);
        CHECK(f_lseek(&h, off) == FR_OK && f_read(&h, rd, 512, &br) == FR_OK && br == 512);
        bool same = true;
        for (int i = 0; i < 512; i++) same = same && rd[i] == pat(2, off + i);
        CHECK(same);
        f_close(&h);

        // S4: a write that reaches beyond the end. Through the map FatFs cannot allocate: this is the danger the policy removes.
        uint64_t end = f.obj.objsize;
        uint8_t ext[512];
        memset(ext, 0x5A, sizeof(ext));
        CHECK(f_lseek(&f, end) == FR_OK);
        FRESULT wr = f_write(&f, ext, 512, &bw);
        CHECK(wr != FR_OK || bw != 512);                          // refused with the map in place
        CHECK(f.obj.objsize == end);
        f_close(&f);
        CHECK(f_open(&f, "0:/a.bin", FA_READ | FA_WRITE) == FR_OK);
        FastSeek fs2;
        fs2.access(&f, 0, 512, false, "test.img");                 // sequential from 0: nothing built
        CHECK(!fs2.active() && f.cltbl == 0);
        fs2.access(&f, 8u << 20, 512, false, "test.img");          // a seek: built
        CHECK(fs2.active() && f.cltbl != 0);
        fs2.access(&f, end, 512, true, "test.img");                // reaches beyond the end: dropped before the write
        CHECK(!fs2.active() && f.cltbl == 0);
        CHECK(f_lseek(&f, end) == FR_OK && f_write(&f, ext, 512, &bw) == FR_OK && bw == 512);
        CHECK(f.obj.objsize == end + 512);
        fs2.access(&f, 4u << 20, 512, false, "test.img");          // and not built again for this open file
        CHECK(!fs2.active() && f.cltbl == 0);
        CHECK(f_close(&f) == FR_OK);
        CHECK(f_open(&f, "0:/a.bin", FA_READ) == FR_OK);
        CHECK(f.obj.objsize == end + 512 && f_lseek(&f, end) == FR_OK && f_read(&f, rd, 512, &br) == FR_OK && !memcmp(rd, ext, 512));
        f_close(&f);
    } else {
        f_close(&f);
    }

    // S4 (policy, small and sequential): a file below FASTSEEK_MIN_SIZE never gets a map, a sequential big read never
    {
        FIL s;
        CHECK(f_open(&s, "0:/b.bin", FA_READ) == FR_OK);
        FastSeek seq;
        for (uint64_t o = 0; o < (2u << 20); o += 32768) seq.access(&s, o, 32768, false, "test.img");
        CHECK(!seq.active() && s.cltbl == 0);
        f_close(&s);
        CHECK(f_open(&s, "0:/small.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        UINT bw;
        uint8_t z[4096] = { 1 };
        for (int i = 0; i < 8; i++) CHECK(f_write(&s, z, sizeof(z), &bw) == FR_OK);
        FastSeek sm;
        sm.access(&s, 8192, 512, false, "small.bin");
        CHECK(!sm.active() && s.cltbl == 0);
        f_close(&s);
    }

    // S3: one cluster per fragment in a 16 MiB file: 32768 fragments, more than FASTSEEK_MAX_ITEMS holds (8191)
    if (map_on) {
        format();
        interleave(SIZE, 512);
        CHECK(f_open(&f, "0:/a.bin", FA_READ) == FR_OK);
        FastSeek big;
        big.access(&f, 1u << 20, 512, false, "test.img");
        CHECK(!big.active() && f.cltbl == 0);                      // refused, the pointer cleared (FatFs leaves it set on failure)
        lcg = 777;
        Stat slow = blocks(&f, NULL, SIZE, 20, false, 1, false);
        CHECK(slow.bytes_ok);
        printf("S3 refused map (too many fragments), normal seek still correct: avg %.0f FAT sectors per seek\n", (double)slow.total / slow.n);
        f_close(&f);
    }

    // S6: the same on an exFAT volume (4 KiB clusters; the USB sticks with images of 4 GiB and more are exFAT): fragments, map, bound
    if (map_on) {
        format(FM_EXFAT, 4096);
        interleave(SIZE, 64 * 1024);
        CHECK(f_open(&f, "0:/a.bin", FA_READ) == FR_OK);
        CHECK(f.obj.objsize == SIZE);
        FastSeek ex;
        ex.access(&f, 1u << 20, 512, false, "exfat.img");
        CHECK(ex.active());
        lcg = 4242;
        Stat xs = blocks(&f, &ex, SIZE, 300, true, 1, false);
        printf("S6 exFAT, map on: 300 random blocks, FAT sectors per seek+read: avg %.2f, max %u\n", (double)xs.total / xs.n, (unsigned)xs.max);
        CHECK(xs.bytes_ok && xs.max <= FAT_BOUND);
        f_close(&f);
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
