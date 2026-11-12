#include "fatwalk.h"
#include <string.h>
#include <stdio.h>

const char *fat_result_str(fat_result_t r)
{
    switch (r) {
    case FAT_OK: return "ok";
    case FAT_ERR_IO: return "cannot read the flash drive";
    case FAT_ERR_GEOMETRY: return "the flash drive is not FAT12/16";
    case FAT_ERR_FRAGMENTED: return "flash drive fragmented";
    case FAT_ERR_SIZE: return "the file's cluster chain does not match its size";
    case FAT_ERR_CLUSTER: return "bad cluster in the flash file";
    }
    return "?";
}

static uint32_t le16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static bool pow2(uint32_t v) { return v && !(v & (v - 1)); }

fat_result_t fat_geometry(fat_read_t rd, void *ctx, FatGeo *g)
{
    uint8_t s[512];
    if (!rd(ctx, 0, s, sizeof(s))) {
        return FAT_ERR_IO;
    }
    uint32_t bps = le16(s + 11), spc = s[13], rsv = le16(s + 14), nfat = s[16], nroot = le16(s + 17);
    uint32_t tot = le16(s + 19), spf = le16(s + 22);
    if (!tot) {
        tot = le32(s + 32);
    }
    if (le16(s + 510) != 0xAA55 || bps < 512 || bps > 4096 || !pow2(bps) || !pow2(spc) || !rsv || !nfat || !spf || !tot) {
        return FAT_ERR_GEOMETRY;                    // spf = 0 is FAT32 (its FAT size is in another field)
    }
    uint32_t root = (nroot * 32 + bps - 1) / bps;
    uint32_t meta = rsv + nfat * spf + root;
    if (meta >= tot) {
        return FAT_ERR_GEOMETRY;
    }
    uint32_t nclust = (tot - meta) / spc;
    if (nclust >= 65525) {
        return FAT_ERR_GEOMETRY;                    // FAT32 by the cluster count
    }
    g->bps = bps;
    g->spc = spc;
    g->csize = bps * spc;
    g->fat_off = rsv * bps;
    g->data_off = meta * bps;
    g->nclust = nclust;
    g->fatbits = nclust < 4085 ? 12 : 16;
    return FAT_OK;
}

// One FAT entry; the FAT is read in windows of 512 bytes (the file's chain runs through them in order).
struct FatWin {
    uint8_t b[514];                  // two bytes past the window: an entry may straddle it
    uint32_t at;
    bool valid;
};

static bool fat_entry(const FatGeo *g, fat_read_t rd, void *ctx, FatWin *w, uint32_t c, uint32_t *next)
{
    uint32_t pos = g->fatbits == 12 ? c + c / 2 : c * 2;
    uint32_t blk = pos & ~511u;
    if (!w->valid || w->at != blk) {
        w->valid = false;
        if (!rd(ctx, g->fat_off + blk, w->b, sizeof(w->b))) {
            return false;
        }
        w->at = blk;
        w->valid = true;
    }
    uint32_t v = le16(w->b + (pos - blk));
    *next = g->fatbits == 12 ? ((c & 1) ? v >> 4 : v & 0xFFF) : v;
    return true;
}

fat_result_t fat_contiguous(const FatGeo *g, fat_read_t rd, void *ctx, uint32_t first, uint32_t size, uint32_t *vol_off)
{
    if (!size) {
        return FAT_ERR_SIZE;
    }
    uint32_t n = (size + g->csize - 1) / g->csize;
    if (first < 2 || first - 2 >= g->nclust || n > g->nclust - (first - 2)) {
        return FAT_ERR_CLUSTER;
    }
    uint32_t eoc = g->fatbits == 12 ? 0xFF8 : 0xFFF8, bad = g->fatbits == 12 ? 0xFF7 : 0xFFF7;
    FatWin w;
    w.valid = false;
    w.at = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t c = first + k, next;
        if (!fat_entry(g, rd, ctx, &w, c, &next)) {
            return FAT_ERR_IO;
        }
        if (next == bad) {
            return FAT_ERR_CLUSTER;
        }
        if (k + 1 < n) {
            if (next >= eoc) {
                return FAT_ERR_SIZE;                // the chain ends before the file does
            }
            if (next != c + 1) {
                return next < 2 ? FAT_ERR_CLUSTER : FAT_ERR_FRAGMENTED;
            }
        } else if (next < eoc) {
            return FAT_ERR_SIZE;                    // the chain goes on past the file's last cluster
        }
    }
    *vol_off = g->data_off + (first - 2) * g->csize;
    return FAT_OK;
}

uint32_t fat_run_bytes(const FatGeo *g, uint32_t size)
{
    return (size + g->csize - 1) / g->csize * g->csize;
}

void fat_no_run_text(char *out, unsigned max, const FatGeo *g, uint32_t size)
{
    uint32_t need = fat_run_bytes(g, size);
    snprintf(out, max, "flash drive: no free run of %u KiB (%u clusters) in one piece. Tidy the flash drive (delete files you do not need) and start again",
             (unsigned)((need + 1023) / 1024), (unsigned)(need / g->csize));
}
