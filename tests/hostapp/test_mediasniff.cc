// T111 S1: the list tells what is on each medium. media_sniff() reads disk images made by the REAL f_mkfs of FatFs (FAT12, FAT16, FAT32, exFAT)
// and by hand (blank, GPT, two partitions, NTFS), and must name type, label, partitions and sizes. A FAT32 volume is never exFAT.
//   run.sh builds this against mediasniff.cc as it is and against mutants (the exFAT test removed, FAT types from the text...); each must go red.
#include "fmtbench.h"
#include "mediasniff.h"
#include "mbr.h"

static bool rd(void *ctx, uint32_t lba, uint8_t *buf) { return ((BlockDevice *)ctx)->read(buf, lba, 1) == RES_OK; }

static MediaSniff sniff(SparseDisk &d)
{
    MediaSniff s;
    media_sniff(rd, &d, d.nsect, 512, &s);
    return s;
}

static void mbr_with(SparseDisk &d, int idx, uint8_t type, uint32_t start, uint32_t len)
{
    uint8_t m[512];
    d.get(0, m);
    if (m[510] != 0x55) {
        memset(m, 0, 512);
        m[510] = 0x55;
        m[511] = 0xAA;
    }
    uint8_t *e = m + 446 + idx * 16;
    memset(e, 0, 16);
    e[4] = type;
    for (int i = 0; i < 4; i++) {
        e[8 + i] = (uint8_t)(start >> (8 * i));
        e[12 + i] = (uint8_t)(len >> (8 * i));
    }
    d.put(0, m);
}

int main()
{
    // FAT32 without a partition table, with MBR (FatFs's own, at sector 63), FAT16, FAT12
    {
        SparseDisk d(600 * MiB_S, 0x00);
        CHECK(bench_mkfs(&d, 0, d.nsect, FM_FAT32 | FM_SFD) == FR_OK);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_FAT32 && !s.mbr && s.parts == 1 && s.part_start == 0);
        CHECK(s.label[0] == 0);                                      // FatFs writes "NO NAME"
        CHECK(s.cluster_bytes >= 512);
        // the label: eleven characters of the boot sector, trailing blanks cut
        uint8_t b[512];
        d.get(0, b);
        memcpy(b + 71, "MEDIA      ", 11);
        d.put(0, b);
        s = sniff(d);
        CHECK(s.type == MT_FAT32 && !strcmp(s.label, "MEDIA"));
    }
    {
        SparseDisk d(600 * MiB_S, 0x00);
        CHECK(bench_mkfs(&d, 0, d.nsect, FM_FAT32) == FR_OK);       // FatFs writes its own table, the volume at sector 63
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_FAT32 && s.mbr && s.parts == 1 && s.part_start == 63 && s.part_len == d.nsect - 63);
    }
    {
        SparseDisk d(100 * MiB_S, 0x00);
        CHECK(bench_mkfs(&d, 0, d.nsect, FM_FAT) == FR_OK);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_FAT16 && s.mbr && s.parts == 1);
    }
    {
        SparseDisk d(4 * MiB_S, 0x00);
        CHECK(bench_mkfs(&d, 0, d.nsect, FM_FAT | FM_SFD) == FR_OK);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_FAT12 && !s.mbr && s.parts == 1);
    }
    // exFAT in the table the format writes, and the label entry of its root directory
    {
        SparseDisk d(512 * MiB_S, 0x00);
        uint8_t m[512];
        mbr_build(m, d.nsect, 0x12345678);
        d.put(0, m);
        CHECK(bench_mkfs(&d, MBR_PART_START, d.nsect - MBR_PART_START, FM_EXFAT | FM_SFD, MBR_PART_START) == FR_OK);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_EXFAT && s.mbr && s.parts == 1 && s.part_start == MBR_PART_START && s.part_len == d.nsect - MBR_PART_START);
        CHECK(s.cluster_bytes == 32768 && s.label[0] == 0);          // 512 MiB: FatFs picks 32 KiB
        uint8_t vbr[512];
        d.get(MBR_PART_START, vbr);
        uint32_t heap = vbr[88] | (vbr[89] << 8) | (vbr[90] << 16) | ((uint32_t)vbr[91] << 24);
        uint32_t root = vbr[96] | (vbr[97] << 8) | (vbr[98] << 16) | ((uint32_t)vbr[99] << 24);
        uint32_t rs = MBR_PART_START + heap + (root - 2) * (1u << vbr[109]);
        uint8_t r[512];
        d.get(rs, r);
        CHECK(r[0] == 0x83);                                         // f_mkfs: the label entry is there, empty
        r[1] = 5;
        const char *l = "TRX64";
        for (int i = 0; i < 5; i++) {
            r[2 + 2 * i] = l[i];
            r[3 + 2 * i] = 0;
        }
        d.put(rs, r);
        s = sniff(d);
        CHECK(s.type == MT_EXFAT && !strcmp(s.label, "TRX64"));
    }
    // exFAT with no table at all: the host cannot mount it (Disk::Init finds a volume only by "FAT" at 54 or 82), but the list can say so
    {
        SparseDisk d(256 * MiB_S, 0x00);
        CHECK(bench_mkfs(&d, 0, d.nsect, FM_EXFAT | FM_SFD) == FR_OK);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_EXFAT && !s.mbr && s.parts == 1);
    }
    // blank: nothing, and 0x55AA with an empty table
    {
        SparseDisk d(64 * MiB_S, 0x00);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_NONE && s.parts == 0);
        uint8_t m[512] = {};
        m[510] = 0x55; m[511] = 0xAA;
        d.put(0, m);
        s = sniff(d);
        CHECK(s.type == MT_NONE && s.parts == 0);
        SparseDisk junk(64 * MiB_S, 0xA5);                           // old data, no signature
        s = sniff(junk);
        CHECK(s.type == MT_NONE);
    }
    // GPT behind a protective MBR; two used entries
    {
        SparseDisk d(64 * MiB_S, 0x00);
        mbr_with(d, 0, 0xEE, 1, d.nsect - 1);
        uint8_t h[512] = {};
        memcpy(h, "EFI PART", 8);
        h[0x48] = 2;                                                // the array at LBA 2
        h[0x50] = 128;                                              // 128 entries
        h[0x54] = 128;                                              // of 128 bytes
        d.put(1, h);
        uint8_t a[512] = {};
        memset(a, 0x11, 16);
        memset(a + 128, 0x22, 16);
        d.put(2, a);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_GPT && s.parts == 2);
    }
    // two partitions
    {
        SparseDisk d(64 * MiB_S, 0x00);
        mbr_with(d, 0, 0x0C, 2048, 10000);
        mbr_with(d, 1, 0x07, 20000, 30000);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_MULTI && s.parts == 2);
    }
    // a partition with a file system the host does not know
    {
        SparseDisk d(64 * MiB_S, 0x00);
        mbr_with(d, 0, 0x07, 2048, 100000);
        uint8_t v[512] = {};
        v[0] = 0xEB; v[1] = 0x52; v[2] = 0x90;
        memcpy(v + 3, "NTFS    ", 8);
        v[510] = 0x55; v[511] = 0xAA;
        d.put(2048, v);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_OTHER && s.parts == 1);
    }
    // a medium that does not answer
    {
        SparseDisk d(64 * MiB_S, 0x00);
        d.set_state(e_device_no_media);
        MediaSniff s = sniff(d);
        CHECK(s.type == MT_ERROR);
    }
    printf("test_mediasniff: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
