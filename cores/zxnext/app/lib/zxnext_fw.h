// T116 D11: which board ids the Next firmware on the SD image accepts. The image is a whole FAT card (MBR, then FAT16 or FAT32; or a bare FAT volume);
// its root directory holds TBBLUE.FW, the firmware the boot ROM starts. The firmware carries a table of the boards it knows, one entry per board:
//     <id byte> "ZX Next Issue <n>" 0 <file name> ... (37 bytes each, in three copies in the file)
// with id = n - 2, and the board test at its start (NextReg 0x0F read, "Unknown board id: 0x%02x") passes ids below the table's size: SUB 3 / RET C in
// the firmware that knows Issue 2 to 4, SUB 4 / RET C in the one that knows Issue 5 (the three test sites of each file were read). The newest id of the
// table is therefore the highest id the firmware takes. Pure: CoreFile only, read by offset.
#ifndef COREAPP_ZXNEXT_FW_H
#define COREAPP_ZXNEXT_FW_H

#include <stdint.h>
#include <string.h>
#include "fsif.h"

enum { ZXFW_OK = 0, ZXFW_NO_FILE = 1, ZXFW_NO_TABLE = 2 };

struct ZxFat {
    CoreFile *f;
    uint64_t vol;                         // byte offset of the FAT volume
    uint32_t spc;                         // sectors per cluster
    uint64_t fat;                         // byte offset of the first FAT
    uint64_t root16;                      // FAT16: byte offset of the root directory
    uint32_t root16_sectors;
    uint32_t root_cluster;                // FAT32
    uint64_t data;                        // byte offset of cluster 2
    bool fat32;
    uint32_t nclus;
};

static inline uint32_t zxfw_le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static inline bool zxfw_bpb(const uint8_t *b)
{
    return b[510] == 0x55 && b[511] == 0xAA && zxfw_le(b + 11, 2) == 512 && b[13] && !(b[13] & (b[13] - 1)) && zxfw_le(b + 14, 2) && b[16];
}

static inline bool zxfw_open(CoreFile *f, ZxFat *x)
{
    uint8_t b[512];
    memset(x, 0, sizeof(*x));
    x->f = f;
    if (!f->read(0, b, 512)) {
        return false;
    }
    uint64_t vol = 0;
    if (!zxfw_bpb(b)) {                   // not a bare volume: the first FAT partition of the MBR
        if (b[510] != 0x55 || b[511] != 0xAA) {
            return false;
        }
        vol = 0;
        for (int p = 0; p < 4 && !vol; p++) {
            const uint8_t *e = b + 446 + 16 * p;
            uint8_t t = e[4];
            if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E) {
                vol = (uint64_t)zxfw_le(e + 8, 4) * 512;
            }
        }
        if (!vol || !f->read(vol, b, 512) || !zxfw_bpb(b)) {
            return false;
        }
    }
    x->vol = vol;
    x->spc = b[13];
    uint32_t rsv = zxfw_le(b + 14, 2), nfat = b[16], rootent = zxfw_le(b + 17, 2);
    uint32_t tot = zxfw_le(b + 19, 2) ? zxfw_le(b + 19, 2) : zxfw_le(b + 32, 4);
    uint32_t fsz = zxfw_le(b + 22, 2) ? zxfw_le(b + 22, 2) : zxfw_le(b + 36, 4);
    x->root16_sectors = (rootent * 32 + 511) / 512;
    uint32_t first = rsv + nfat * fsz + x->root16_sectors;      // the first data sector
    if (!fsz || tot <= first) {
        return false;
    }
    x->nclus = (tot - first) / x->spc;
    if (x->nclus < 4085) {
        return false;                     // FAT12: no card of the Next
    }
    x->fat32 = x->nclus >= 65525;
    x->fat = vol + (uint64_t)rsv * 512;
    x->root16 = vol + (uint64_t)(rsv + nfat * fsz) * 512;
    x->root_cluster = zxfw_le(b + 44, 4);
    x->data = vol + (uint64_t)first * 512;
    return !x->fat32 || x->root_cluster >= 2;
}

static inline uint64_t zxfw_cluster_off(const ZxFat &x, uint32_t c)
{
    return x.data + (uint64_t)(c - 2) * x.spc * 512;
}

// The cluster after c; 0 at the end of the chain or on a bad entry.
static inline uint32_t zxfw_next(const ZxFat &x, uint32_t c)
{
    uint8_t e[4];
    uint32_t n;
    if (x.fat32) {
        if (!x.f->read(x.fat + (uint64_t)c * 4, e, 4)) {
            return 0;
        }
        n = zxfw_le(e, 4) & 0x0FFFFFFF;
        return n >= 2 && n < x.nclus + 2 ? n : 0;
    }
    if (!x.f->read(x.fat + (uint64_t)c * 2, e, 2)) {
        return 0;
    }
    n = zxfw_le(e, 2);
    return n >= 2 && n < x.nclus + 2 ? n : 0;
}

// Finds the plain file with the 8.3 name `name` (11 characters, blank padded) in the root directory.
static inline bool zxfw_find(const ZxFat &x, const char *name, uint32_t *cluster, uint32_t *size)
{
    uint32_t c = x.root_cluster;
    uint64_t off = x.fat32 ? zxfw_cluster_off(x, c) : x.root16;
    uint32_t left = x.fat32 ? x.spc * 16 : x.root16_sectors * 16;      // entries in this piece
    for (int guard = 0; guard < 64 * 1024; ) {
        for (; left; left--, off += 32, guard++) {
            uint8_t e[32];
            if (!x.f->read(off, e, 32) || e[0] == 0) {
                return false;
            }
            if (e[0] == 0xE5 || (e[11] & 0x18) || memcmp(e, name, 11)) {       // deleted; a directory, the label or a long name entry (0x0F)
                continue;
            }
            *cluster = (x.fat32 ? (zxfw_le(e + 20, 2) << 16) : 0) | zxfw_le(e + 26, 2);
            *size = zxfw_le(e + 28, 4);
            return *cluster >= 2;
        }
        if (!x.fat32 || !(c = zxfw_next(x, c))) {
            return false;
        }
        off = zxfw_cluster_off(x, c);
        left = x.spc * 16;
    }
    return false;
}

// Reads TBBLUE.FW of the image and returns ZXFW_OK with the highest board id of its table in *maxid, ZXFW_NO_FILE when the image has no such file
// (or is no FAT card), ZXFW_NO_TABLE when the file has no table in the form above.
static inline int zxfw_board_max(CoreFile *img, int *maxid)
{
    static const char tag[] = "ZX Next Issue ";
    enum { TAG = 14, ENTRY = 1 + TAG + 1 + 1, CHUNK = 2048, CARRY = ENTRY - 1, FILE_MAX = 2 * 1024 * 1024 };
    ZxFat x;
    uint32_t c, size;
    if (!img || !zxfw_open(img, &x) || !zxfw_find(x, "TBBLUE  FW ", &c, &size)) {
        return ZXFW_NO_FILE;
    }
    if (size > FILE_MAX) {
        size = FILE_MAX;
    }
    static uint8_t buf[CARRY + CHUNK];
    int have = 0, best = -1;
    uint32_t done = 0;
    uint32_t cbytes = x.spc * 512;
    for (uint32_t n = 0; c && done < size; c = zxfw_next(x, c), n++) {
        if (n > x.nclus) {
            break;                        // a loop in the chain
        }
        for (uint32_t o = 0; o < cbytes && done < size; o += CHUNK) {
            uint32_t len = cbytes - o < CHUNK ? cbytes - o : CHUNK;
            if (len > size - done) {
                len = size - done;
            }
            if (!img->read(zxfw_cluster_off(x, c) + o, buf + have, len)) {
                return best < 0 ? ZXFW_NO_TABLE : ZXFW_OK;
            }
            done += len;
            int total = have + (int)len;
            for (int i = 0; i + ENTRY <= total; i++) {
                if (!memcmp(buf + i + 1, tag, TAG) && buf[i + 1 + TAG] >= '2' && buf[i + 1 + TAG] <= '9' && buf[i + 2 + TAG] == 0 &&
                    buf[i] == buf[i + 1 + TAG] - '2' && (int)buf[i] > best) {
                    best = buf[i];
                }
            }
            have = total < CARRY ? total : CARRY;
            memmove(buf, buf + total - have, have);
        }
    }
    if (best < 0) {
        return ZXFW_NO_TABLE;
    }
    *maxid = best;
    return ZXFW_OK;
}

#endif
