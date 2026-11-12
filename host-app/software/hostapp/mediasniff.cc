#include "mediasniff.h"
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const char *media_type_name(MediaType t)
{
    switch (t) {
    case MT_NONE: return "blank";
    case MT_FAT12: return "FAT12";
    case MT_FAT16: return "FAT16";
    case MT_FAT32: return "FAT32";
    case MT_EXFAT: return "exFAT";
    case MT_GPT: return "GPT";
    case MT_MULTI: return "several partitions";
    case MT_OTHER: return "other";
    default: return "unreadable";
    }
}

// One sector is read at a time into this buffer; the callers copy what they need before the next read. Only the UI task sniffs.
static uint8_t g_sec[4096];

static void label_from(char *dst, const uint8_t *src, int n)
{
    int l = 0;
    for (int i = 0; i < n; i++) {
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? (char)src[i] : '?';
    }
    l = n;
    while (l > 0 && dst[l - 1] == ' ') {
        l--;
    }
    dst[l] = 0;
    if (!strcmp(dst, "NO NAME")) {
        dst[0] = 0;
    }
}

// Is the sector a FAT boot sector (BPB sane, 0x55AA)? The type comes from the cluster count, as the specification says, not from the text.
static bool fat_type(const uint8_t *b, uint32_t ss, MediaType *t, char *label, uint32_t *cluster_bytes)
{
    if (rd16(b + 510) != 0xAA55 || (b[0] != 0xEB && b[0] != 0xE9)) {
        return false;
    }
    uint32_t bps = rd16(b + 11), spc = b[13], rsvd = rd16(b + 14), nfat = b[16], root = rd16(b + 17);
    uint32_t tot = rd16(b + 19) ? rd16(b + 19) : rd32(b + 32);
    uint32_t fatsz = rd16(b + 22) ? rd16(b + 22) : rd32(b + 36);
    if (bps < 512 || bps > 4096 || (bps & (bps - 1)) || !spc || (spc & (spc - 1)) || spc > 128 || !rsvd || nfat < 1 || nfat > 2 || !fatsz || !tot) {
        return false;
    }
    (void)ss;
    uint32_t rootsec = (root * 32 + bps - 1) / bps;
    if (rsvd + nfat * fatsz + rootsec >= tot) {
        return false;
    }
    uint32_t clusters = (tot - (rsvd + nfat * fatsz + rootsec)) / spc;
    *cluster_bytes = bps * spc;
    if (clusters < 4085) {
        *t = MT_FAT12;
    } else if (clusters < 65525) {
        *t = MT_FAT16;
    } else {
        *t = MT_FAT32;
    }
    if (*t == MT_FAT32) {
        if (b[66] == 0x29) {
            label_from(label, b + 71, 11);
        }
    } else if (b[38] == 0x29) {
        label_from(label, b + 43, 11);
    }
    return true;
}

static bool is_exfat(const uint8_t *b)
{
    return !memcmp(b + 3, "EXFAT   ", 8) && rd16(b + 510) == 0xAA55;
}

// The volume label entry (type 0x83) in the first sectors of the root directory. The VBR is in g_sec on entry.
static bool exfat_info(MediaRead rd, void *ctx, uint32_t vol, uint32_t ss, char *label, uint32_t *cluster_bytes)
{
    uint32_t heap = rd32(g_sec + 88), root = rd32(g_sec + 96);
    uint32_t sshift = g_sec[108], cshift = g_sec[109];
    if (sshift < 9 || sshift > 12 || cshift > 25 || root < 2) {
        return false;
    }
    *cluster_bytes = (1u << sshift) << cshift;
    uint32_t first = vol + heap + ((root - 2) << cshift);
    uint32_t n = 1u << cshift;
    if (n > 8) {
        n = 8;
    }
    for (uint32_t s = 0; s < n; s++) {
        if (!rd(ctx, first + s, g_sec)) {
            return true;                      // the type is known; only the label is missing
        }
        for (uint32_t o = 0; o + 32 <= ss; o += 32) {
            uint8_t t = g_sec[o];
            if (t == 0x00) {
                return true;
            }
            if (t == 0x83) {
                int cnt = g_sec[o + 1] > 11 ? 11 : g_sec[o + 1];
                for (int i = 0; i < cnt; i++) {
                    uint32_t ch = rd16(g_sec + o + 2 + 2 * i);
                    label[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
                }
                label[cnt] = 0;
                return true;
            }
        }
    }
    return true;
}

// The volume at `lba` (sector 0 of the medium for a super floppy, a partition start otherwise).
static void volume_at(MediaRead rd, void *ctx, uint32_t lba, uint32_t ss, MediaSniff *o)
{
    if (!rd(ctx, lba, g_sec)) {
        o->type = MT_ERROR;
        return;
    }
    MediaType t;
    uint32_t cb = 0;
    if (is_exfat(g_sec)) {
        o->type = MT_EXFAT;
        exfat_info(rd, ctx, lba, ss, o->label, &cb);
        o->cluster_bytes = cb;
    } else if (fat_type(g_sec, ss, &t, o->label, &cb)) {
        o->type = t;
        o->cluster_bytes = cb;
    } else {
        o->type = MT_OTHER;
    }
}

void media_sniff(MediaRead rd, void *ctx, uint32_t sectors, uint32_t sector_size, MediaSniff *o)
{
    memset(o, 0, sizeof(*o));
    o->sectors = sectors;
    o->type = MT_NONE;
    if (sector_size < 512 || sector_size > 4096 || !rd(ctx, 0, g_sec)) {
        o->type = MT_ERROR;
        return;
    }
    if (rd16(g_sec + 510) != 0xAA55) {
        return;                                   // blank (or a volume with no signature: not one the host mounts)
    }
    // sector 0 is a volume itself (no partition table): "EXFAT   " or a FAT boot sector
    {
        MediaType t;
        char lb[16] = "";
        uint32_t cb = 0;
        bool ex = is_exfat(g_sec);
        if (ex || fat_type(g_sec, sector_size, &t, lb, &cb)) {
            volume_at(rd, ctx, 0, sector_size, o);
            o->parts = 1;
            o->mbr = false;
            o->part_start = 0;
            o->part_len = sectors;
            return;
        }
    }
    uint8_t ent[64];
    memcpy(ent, g_sec + 446, 64);
    int used = 0, first = -1;
    bool gpt = false;
    for (int i = 0; i < 4; i++) {
        if (ent[i * 16 + 4]) {
            used++;
            if (ent[i * 16 + 4] == 0xEE) {
                gpt = true;
            } else if (first < 0) {
                first = i;
            }
        }
    }
    o->mbr = true;
    if (gpt) {
        o->type = MT_GPT;
        if (rd(ctx, 1, g_sec) && !memcmp(g_sec, "EFI PART", 8)) {
            uint32_t arr = rd32(g_sec + 0x48), cnt = rd32(g_sec + 0x50), esz = rd32(g_sec + 0x54);
            if (esz >= 128 && esz <= sector_size && !(esz & (esz - 1)) && cnt) {
                uint32_t per = sector_size / esz, seen = 0;
                for (uint32_t s = 0; s < 4 && seen < cnt; s++) {
                    if (!rd(ctx, arr + s, g_sec)) {
                        break;
                    }
                    for (uint32_t k = 0; k < per && seen < cnt; k++, seen++) {
                        const uint8_t *e = g_sec + k * esz;
                        bool nz = false;
                        for (int j = 0; j < 16; j++) {
                            nz = nz || e[j];
                        }
                        o->parts += nz;
                    }
                }
            }
        }
        return;
    }
    o->parts = used;
    if (used == 0) {
        return;                                   // 0x55AA and an empty table: blank
    }
    if (used > 1) {
        o->type = MT_MULTI;
        return;
    }
    uint32_t start = rd32(ent + first * 16 + 8), len = rd32(ent + first * 16 + 12);
    o->part_start = start;
    o->part_len = len;
    volume_at(rd, ctx, start, sector_size, o);
}
