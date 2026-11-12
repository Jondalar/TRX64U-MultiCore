// Host test of the FAT12/16 chain walker (T115 S3): host-app/software/hostapp/fatwalk.cc over fake volumes. The main
// volume has the geometry measured on the C64U's flash drive on 2026-10-21 (4096-byte sectors, one per cluster, 1 reserved,
// 1 FAT of 2 sectors, 512 root entries) and the PoC's file: 0x218 clusters from cluster 12 = flash 0x411000.
#include "fatwalk.h"
#include <stdio.h>
#include <string.h>
#include <vector>

typedef std::vector<uint8_t> Buf;

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct Vol {
    Buf img;
    uint32_t fat_off, bits;
    uint32_t reads;
};

static bool rd(void *ctx, uint32_t off, void *buf, uint32_t n)
{
    Vol *v = (Vol *)ctx;
    v->reads++;
    if ((uint64_t)off + n > v->img.size()) return false;
    memcpy(buf, v->img.data() + off, n);
    return true;
}

static void le16(Buf &b, size_t at, uint32_t v) { b[at] = v; b[at + 1] = v >> 8; }

static Vol make(uint32_t bps, uint32_t spc, uint32_t rsv, uint32_t nfat, uint32_t nroot, uint32_t spf, uint32_t total, uint32_t bits)
{
    Vol v;
    v.img.assign((size_t)total * bps, 0);
    v.fat_off = rsv * bps;
    v.bits = bits;
    v.reads = 0;
    Buf &b = v.img;
    le16(b, 11, bps); b[13] = spc; le16(b, 14, rsv); b[16] = nfat; le16(b, 17, nroot);
    if (total < 0x10000) le16(b, 19, total); else { b[32] = total; b[33] = total >> 8; b[34] = total >> 16; b[35] = total >> 24; }
    b[21] = 0xF8; le16(b, 22, spf);
    b[510] = 0x55; b[511] = 0xAA;
    return v;
}

static void setfat(Vol &v, uint32_t c, uint32_t val)
{
    if (v.bits == 12) {
        size_t p = v.fat_off + c + c / 2;
        uint32_t w = v.img[p] | (v.img[p + 1] << 8);
        w = (c & 1) ? ((w & 0x000F) | (val << 4)) : ((w & 0xF000) | (val & 0xFFF));
        v.img[p] = w; v.img[p + 1] = w >> 8;
    } else {
        le16(v.img, v.fat_off + 2 * c, val);
    }
}

// a run first ... first + n - 1, ended with the end-of-chain mark
static void chain(Vol &v, uint32_t first, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++) setfat(v, first + k, k + 1 < n ? first + k + 1 : (v.bits == 12 ? 0xFFF : 0xFFFF));
}

static Vol drive(void)       // the flash drive: 0xBE8000 bytes = 3048 sectors
{
    return make(4096, 1, 1, 1, 512, 2, 3048, 12);
}

static void s3(void)
{
    // ---- the measured geometry
    Vol v = drive();
    FatGeo g;
    CHECK(fat_geometry(rd, &v, &g) == FAT_OK);
    CHECK(g.bps == 4096 && g.spc == 1 && g.csize == 4096 && g.fat_off == 0x1000 && g.data_off == 0x7000 && g.fatbits == 12);
    CHECK(g.nclust == 3048 - 7);

    // ---- the PoC's file: contiguous 0x218 clusters from 12, size 2,192,121 bytes (0x2172F9, the last cluster part full)
    chain(v, 12, 0x218);
    uint32_t off = 0;
    CHECK(fat_contiguous(&g, rd, &v, 12, 0x2172F9, &off) == FAT_OK);
    CHECK(off == 0x11000);
    CHECK(0x400000u + off == 0x411000u);                                  // the flash address the PoC measured
    // the spec's formula: 0x400000 + (data start sector + (first - 2) * sectors per cluster) * 4096
    CHECK(0x400000u + (7u + (12 - 2) * 1u) * 4096u == 0x411000u);
    // the file starts anywhere else: the offset follows the first cluster
    {
        Vol w = drive();
        chain(w, 700, 0x218);
        FatGeo gw;
        CHECK(fat_geometry(rd, &w, &gw) == FAT_OK && fat_contiguous(&gw, rd, &w, 700, 0x2172F9, &off) == FAT_OK && off == 0x7000 + 698u * 4096);
    }
    // exact multiple of the cluster size, and one byte more (needs one more cluster)
    {
        Vol w = drive();
        chain(w, 20, 4);
        FatGeo gw;
        fat_geometry(rd, &w, &gw);
        CHECK(fat_contiguous(&gw, rd, &w, 20, 4 * 4096, &off) == FAT_OK);
        CHECK(fat_contiguous(&gw, rd, &w, 20, 4 * 4096 + 1, &off) == FAT_ERR_SIZE);     // the chain is one cluster short
        CHECK(fat_contiguous(&gw, rd, &w, 20, 3 * 4096 + 1, &off) == FAT_OK);
        CHECK(fat_contiguous(&gw, rd, &w, 20, 3 * 4096, &off) == FAT_ERR_SIZE);         // the chain is one cluster long
    }

    // ---- fragmented: a gap in the middle of the run
    {
        Vol w = drive();
        chain(w, 12, 0x218);
        setfat(w, 100, 900);                                               // 100 -> 900 ... the rest is elsewhere
        chain(w, 900, 0x218 - 89);
        for (uint32_t c = 101; c <= 111 + 0x218 - 89; c++) if (c != 900 && (c < 900 || c >= 900 + 0x218 - 89)) setfat(w, c, 0);
        FatGeo gw;
        fat_geometry(rd, &w, &gw);
        CHECK(fat_contiguous(&gw, rd, &w, 12, 0x2172F9, &off) == FAT_ERR_FRAGMENTED);
        CHECK(!strcmp(fat_result_str(FAT_ERR_FRAGMENTED), "flash drive fragmented"));
    }
    // fragmented at the very first link, and backwards
    {
        Vol w = drive();
        chain(w, 12, 5);
        setfat(w, 12, 40);
        FatGeo gw;
        fat_geometry(rd, &w, &gw);
        CHECK(fat_contiguous(&gw, rd, &w, 12, 5 * 4096, &off) == FAT_ERR_FRAGMENTED);
        setfat(w, 12, 11);
        CHECK(fat_contiguous(&gw, rd, &w, 12, 5 * 4096, &off) == FAT_ERR_FRAGMENTED);
    }
    // ---- the chain does not match the size
    {
        Vol w = drive();
        chain(w, 12, 0x100);
        FatGeo gw;
        fat_geometry(rd, &w, &gw);
        CHECK(fat_contiguous(&gw, rd, &w, 12, 0x2172F9, &off) == FAT_ERR_SIZE);         // ends at 0x100 clusters, the file needs 0x218
        CHECK(fat_contiguous(&gw, rd, &w, 12, 0, &off) == FAT_ERR_SIZE);                // a file without clusters
        CHECK(fat_contiguous(&gw, rd, &w, 12, 0x100 * 4096, &off) == FAT_OK);
    }
    // ---- clusters out of range, free, bad
    {
        Vol w = drive();
        chain(w, 12, 4);
        FatGeo gw;
        fat_geometry(rd, &w, &gw);
        CHECK(fat_contiguous(&gw, rd, &w, 0, 4096, &off) == FAT_ERR_CLUSTER);
        CHECK(fat_contiguous(&gw, rd, &w, 1, 4096, &off) == FAT_ERR_CLUSTER);
        CHECK(fat_contiguous(&gw, rd, &w, gw.nclust + 2, 4096, &off) == FAT_ERR_CLUSTER);
        CHECK(fat_contiguous(&gw, rd, &w, gw.nclust + 1, 2 * 4096, &off) == FAT_ERR_CLUSTER);   // runs off the end of the volume
        setfat(w, 14, 0);                                                  // a free cluster inside the chain
        CHECK(fat_contiguous(&gw, rd, &w, 12, 4 * 4096, &off) == FAT_ERR_CLUSTER);
        setfat(w, 14, 0xFF7);                                              // a bad cluster
        CHECK(fat_contiguous(&gw, rd, &w, 12, 4 * 4096, &off) == FAT_ERR_CLUSTER);
    }
    // ---- FAT16 (more than 4084 clusters), 512-byte sectors, 4 per cluster
    {
        Vol w = make(512, 4, 1, 2, 512, 40, 20000, 16);
        FatGeo gw;
        CHECK(fat_geometry(rd, &w, &gw) == FAT_OK && gw.fatbits == 16 && gw.csize == 2048);
        chain(w, 300, 50);
        CHECK(fat_contiguous(&gw, rd, &w, 300, 50 * 2048 - 5, &off) == FAT_OK && off == gw.data_off + 298u * 2048);
        setfat(w, 320, 700);
        CHECK(fat_contiguous(&gw, rd, &w, 300, 50 * 2048, &off) == FAT_ERR_FRAGMENTED);
        CHECK(gw.data_off == (1 + 80 + 32) * 512u);
    }
    // ---- not a FAT12/16 volume
    {
        Vol w = drive();
        w.img[510] = 0;                                                    // no signature
        FatGeo gw;
        CHECK(fat_geometry(rd, &w, &gw) == FAT_ERR_GEOMETRY);
        Vol f32 = make(512, 1, 32, 2, 0, 0, 70000, 16);                    // FAT32: spf (16-bit) is 0
        CHECK(fat_geometry(rd, &f32, &gw) == FAT_ERR_GEOMETRY);
        Vol big = make(512, 1, 1, 1, 512, 250, 70000, 16);                 // a FAT16-looking header with 65525+ clusters
        CHECK(fat_geometry(rd, &big, &gw) == FAT_ERR_GEOMETRY);
        Vol odd = drive();
        le16(odd.img, 11, 3000);                                           // bytes per sector not a power of two
        CHECK(fat_geometry(rd, &odd, &gw) == FAT_ERR_GEOMETRY);
        Vol none;
        none.reads = 0;
        CHECK(fat_geometry(rd, &none, &gw) == FAT_ERR_IO);
    }
    // ---- a read error in the middle of the walk
    {
        Vol w = drive();
        chain(w, 12, 0x218);
        FatGeo gw;
        fat_geometry(rd, &w, &gw);
        w.img.resize(0x1000 + 600);                                        // the FAT is cut short
        CHECK(fat_contiguous(&gw, rd, &w, 12, 0x2172F9, &off) == FAT_ERR_IO);
    }
}

int main()
{
    s3();
    printf("test_fatwalk: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
