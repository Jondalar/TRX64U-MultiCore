// T229: see menurows.h.
#include "menurows.h"
#include "menusnap.h"
#include <string.h>

#define MR_FORMAT_VERSION 1
#define MR_HDR 20                      // "MROW", format u16, count u16, key u32, length u32 (of the records), CRC u32 (of the records)

const char *mr_result_text(MrResult r)
{
    switch (r) {
    case MR_OK: return "ok";
    case MR_TOO_SHORT: return "file shorter than the header";
    case MR_MAGIC: return "not a row cache";
    case MR_FORMAT: return "other format";
    case MR_KEY: return "taken from another host app build";
    case MR_LEN: return "length does not fit the file";
    case MR_CRC: return "CRC differs";
    }
    return "?";
}

static void put16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// The record's fields in a fixed order (strings in their full arrays, numbers little endian), the icon last when there is one.
#define BODY_FIXED (96 + 65 + 5 * 4 + 4 + 48 + 8 + 24 + 24 + 4 + 2 + 4 + 1)
#define REC_HDR 8                      // length u32, CRC u32

uint32_t mr_max_encoded(int n) { return MR_HDR + (uint32_t)n * (REC_HDR + BODY_FIXED + MR_ICON_BYTES); }

static uint8_t *put_rec(uint8_t *o, const MrRec &r)
{
    uint8_t *b = o + REC_HDR;
    uint8_t *p = b;
    memcpy(p, r.path, 96); p += 96;
    memcpy(p, r.hash, 65); p += 65;
    put32(p, r.size); p += 4;
    put32(p, r.mtime); p += 4;
    put32(p, r.hdr); p += 4;
    put32(p, r.dsize); p += 4;
    put32(p, r.dmtime); p += 4;
    *p++ = r.st; *p++ = r.full; *p++ = r.large; *p++ = r.origin;
    memcpy(p, r.name, 48); p += 48;
    memcpy(p, r.shortn, 8); p += 8;
    memcpy(p, r.version, 24); p += 24;
    memcpy(p, r.id, 24); p += 24;
    put32(p, r.contract); p += 4;
    put16(p, (uint16_t)r.cfgn); p += 2;
    put32(p, r.cfgstamp); p += 4;
    *p++ = r.has_icon && r.icon ? 1 : 0;
    if (r.has_icon && r.icon) {
        memcpy(p, r.icon, MR_ICON_BYTES);
        p += MR_ICON_BYTES;
    }
    put32(o, (uint32_t)(p - b));
    put32(o + 4, ms_crc32(0, b, (uint32_t)(p - b)));
    return p;
}

int mr_encode(const MrRec *r, int n, uint32_t key, uint8_t *out, uint32_t cap)
{
    if (n < 0 || n > MR_MAX || cap < mr_max_encoded(n)) {
        return -1;
    }
    uint8_t *o = out + MR_HDR;
    for (int i = 0; i < n; i++) {
        o = put_rec(o, r[i]);
    }
    uint32_t dlen = (uint32_t)(o - (out + MR_HDR));
    memcpy(out, "MROW", 4);
    put16(out + 4, MR_FORMAT_VERSION);
    put16(out + 6, (uint32_t)n);
    put32(out + 8, key);
    put32(out + 12, dlen);
    put32(out + 16, ms_crc32(0, out + MR_HDR, dlen));
    return (int)(MR_HDR + dlen);
}

bool mr_file_crc(const uint8_t *file, uint32_t len, uint32_t *crc)
{
    if (len < MR_HDR || memcmp(file, "MROW", 4)) {
        return false;
    }
    *crc = get32(file + 16);
    return true;
}

MrResult mr_decode(const uint8_t *file, uint32_t len, uint32_t key, MrRec *out, int max, int *n)
{
    *n = 0;
    if (len < MR_HDR) return MR_TOO_SHORT;
    if (memcmp(file, "MROW", 4)) return MR_MAGIC;
#ifndef T229_MUT_LOADER
    if (get16(file + 4) != MR_FORMAT_VERSION) return MR_FORMAT;
#endif
    if (get32(file + 8) != key) return MR_KEY;
    uint32_t dlen = get32(file + 12);
    if (dlen != len - MR_HDR) return MR_LEN;
    const uint8_t *p = file + MR_HDR, *e = p + dlen;
#ifndef T229_MUT_LOADER
    if (ms_crc32(0, p, dlen) != get32(file + 16)) return MR_CRC;
#endif
    uint32_t count = get16(file + 6);
    for (uint32_t k = 0; k < count && p < e; k++) {
        if (e - p < REC_HDR) return MR_LEN;
        uint32_t l = get32(p), c = get32(p + 4);
        p += REC_HDR;
        if (l > (uint32_t)(e - p)) return MR_LEN;
        const uint8_t *b = p;
        p += l;
        if (l < BODY_FIXED
#ifndef T229_MUT_LOADER
            || ms_crc32(0, b, l) != c
#endif
            || *n >= max) {
            continue;                                              // a record that is not whole is dropped alone
        }
        MrRec &r = out[*n];
        memset(&r, 0, sizeof(r));
        const uint8_t *q = b;
        memcpy(r.path, q, 96); q += 96;
        memcpy(r.hash, q, 65); q += 65;
        r.size = get32(q); q += 4;
        r.mtime = get32(q); q += 4;
        r.hdr = get32(q); q += 4;
        r.dsize = get32(q); q += 4;
        r.dmtime = get32(q); q += 4;
        r.st = *q++; r.full = *q++; r.large = *q++; r.origin = *q++;
        memcpy(r.name, q, 48); q += 48;
        memcpy(r.shortn, q, 8); q += 8;
        memcpy(r.version, q, 24); q += 24;
        memcpy(r.id, q, 24); q += 24;
        r.contract = get32(q); q += 4;
        r.cfgn = (int16_t)get16(q); q += 2;
        r.cfgstamp = get32(q); q += 4;
        r.has_icon = *q++;
        if (r.has_icon) {
            if ((uint32_t)(b + l - q) < MR_ICON_BYTES) {
                continue;
            }
            r.icon = q;
        }
        r.path[95] = r.hash[64] = r.name[47] = r.shortn[7] = r.version[23] = r.id[23] = 0;
        (*n)++;
    }
    return MR_OK;
}
