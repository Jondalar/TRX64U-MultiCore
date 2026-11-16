// T146: see menusnap.h.
#include "menusnap.h"
#include <string.h>

const char *ms_result_text(MsResult r)
{
    switch (r) {
    case MS_OK: return "ok";
    case MS_TOO_SHORT: return "file shorter than the header";
    case MS_MAGIC: return "not a menu picture";
    case MS_FORMAT: return "other format";
    case MS_SIZE: return "other size";
    case MS_KEY: return "taken from another boot.cfg, mode or build";
    case MS_LEN: return "length does not fit the file";
    case MS_CRC: return "data CRC differs";
    case MS_DATA: return "RLE data does not make the picture";
    }
    return "?";
}

uint32_t ms_crc32(uint32_t crc, const void *data, uint32_t n)
{
    static const uint32_t t[16] = { 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
                                    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

uint32_t ms_key(const char *cfg_text, int cfg_len, int vid_sys, int vid_hdmi, const char *git_id)
{
    uint32_t c = ms_crc32(0, cfg_text, cfg_len > 0 ? (uint32_t)cfg_len : 0);
    int32_t mode[2] = { vid_sys, vid_hdmi };
    c = ms_crc32(c, mode, sizeof(mode));
    c = ms_crc32(c, git_id, (uint32_t)strlen(git_id));
    return c;
}

// RLE over 16-bit pixels. Control byte:
//   0lllllll             literal: l+1 (1..128) pixels follow, little endian
//   1rrrrrrr  (r < 127)  run: r+3 (3..129) copies of the pixel that follows
//   11111111 lo hi       long run: (hi:lo)+130 (130..65665) copies of the pixel that follows
#define RUN_MIN 3
#define RUN_MAX (65535 + 130)
#define LIT_MAX 128

uint32_t ms_max_encoded(uint32_t npx)
{
    return MS_HDR_BYTES + npx * 2 + (npx + LIT_MAX - 1) / LIT_MAX;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static uint32_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }

void ms_enc_begin(MsEnc *e, const uint16_t *px, int w, int h, uint32_t key, uint8_t *out, uint32_t max_out)
{
    uint32_t cap = max_out < MS_LIMIT ? max_out : MS_LIMIT;
    e->px = px;
    e->w = w;
    e->h = h;
    e->key = key;
    e->out = out;
    e->n = (uint32_t)w * (uint32_t)h;
    e->i = 0;
    e->failed = cap < MS_HDR_BYTES;
    e->o = out + MS_HDR_BYTES;
    e->end = out + cap;
}

// Encodes the runs that start in the next max_px pixels (a run or a literal may reach past them); true when all pixels are done or the output is full.
bool ms_enc_step(MsEnc *e, uint32_t max_px)
{
    if (e->failed) {
        return true;
    }
    const uint16_t *px = e->px;
    uint32_t n = e->n;
    uint32_t i = e->i;
    uint32_t stop = (n - i > max_px) ? i + max_px : n;
    uint8_t *o = e->o;
    uint8_t *end = e->end;
    while (i < stop) {
        uint32_t r = 1;
        while (i + r < n && px[i + r] == px[i] && r < RUN_MAX) {
            r++;
        }
        if (r >= RUN_MIN) {
            if (r < 130) {
                if (end - o < 3) { e->failed = true; return true; }
                *o++ = 0x80 | (r - RUN_MIN);
            } else {
                if (end - o < 5) { e->failed = true; return true; }
                *o++ = 0xFF;
                *o++ = (r - 130) & 0xFF;
                *o++ = (r - 130) >> 8;
            }
            *o++ = px[i] & 0xFF;
            *o++ = px[i] >> 8;
            i += r;
        } else {
            uint32_t j = i + 1;
            while (j < n && j - i < LIT_MAX && !(j + 2 < n && px[j] == px[j + 1] && px[j] == px[j + 2])) {
                j++;
            }
            uint32_t c = j - i;
            if ((uint32_t)(end - o) < 1 + c * 2) { e->failed = true; return true; }
            *o++ = c - 1;
            for (uint32_t k = i; k < j; k++) {
                *o++ = px[k] & 0xFF;
                *o++ = px[k] >> 8;
            }
            i = j;
        }
    }
    e->i = i;
    e->o = o;
    return i >= n;
}

int ms_enc_finish(MsEnc *e)
{
    if (e->failed) {
        return -1;
    }
    uint8_t *out = e->out;
    uint32_t dlen = (uint32_t)(e->o - (out + MS_HDR_BYTES));
    memcpy(out, "MSNP", 4);
    put16(out + 4, 1);
    put16(out + 6, e->w);
    put16(out + 8, e->h);
    put16(out + 10, 0);
    put32(out + 12, e->key);
    put32(out + 16, dlen);
    put32(out + 20, ms_crc32(0, out + MS_HDR_BYTES, dlen));
    return (int)(MS_HDR_BYTES + dlen);
}

int ms_encode(const uint16_t *px, int w, int h, uint32_t key, uint8_t *out, uint32_t max_out)
{
    MsEnc e;
    ms_enc_begin(&e, px, w, h, key, out, max_out);
    while (!ms_enc_step(&e, 0xFFFFFFFFu)) {
    }
    return ms_enc_finish(&e);
}

bool ms_header_info(const uint8_t *file, uint32_t len, uint32_t *key, uint32_t *data_crc)
{
    if (len < MS_HDR_BYTES || memcmp(file, "MSNP", 4)) {
        return false;
    }
    *key = get32(file + 12);
    *data_crc = get32(file + 20);
    return true;
}

MsResult ms_load(const uint8_t *file, uint32_t len, int w, int h, uint32_t key, uint16_t *px)
{
    if (len < MS_HDR_BYTES) return MS_TOO_SHORT;
    if (memcmp(file, "MSNP", 4)) return MS_MAGIC;
    if (get16(file + 4) != 1) return MS_FORMAT;
    if ((int)get16(file + 6) != w || (int)get16(file + 8) != h) return MS_SIZE;
    if (get32(file + 12) != key) return MS_KEY;
    uint32_t dlen = get32(file + 16);
    if (dlen != len - MS_HDR_BYTES) return MS_LEN;
    const uint8_t *p = file + MS_HDR_BYTES;
    if (ms_crc32(0, p, dlen) != get32(file + 20)) return MS_CRC;
    uint32_t n = (uint32_t)w * (uint32_t)h, o = 0;
    const uint8_t *e = p + dlen;
    while (p < e) {
        uint8_t c = *p++;
        if (!(c & 0x80)) {
            uint32_t cnt = (c & 0x7F) + 1;
            if ((uint32_t)(e - p) < cnt * 2 || o + cnt > n) return MS_DATA;
            for (uint32_t k = 0; k < cnt; k++, p += 2) {
                px[o++] = p[0] | (p[1] << 8);
            }
        } else {
            uint32_t cnt;
            if (c == 0xFF) {
                if (e - p < 2) return MS_DATA;
                cnt = (p[0] | (p[1] << 8)) + 130;
                p += 2;
            } else {
                cnt = (c & 0x7F) + RUN_MIN;
            }
            if (e - p < 2 || o + cnt > n) return MS_DATA;
            uint16_t v = p[0] | (p[1] << 8);
            p += 2;
            while (cnt--) px[o++] = v;
        }
    }
    return o == n ? MS_OK : MS_DATA;
}

void ms_settle_init(MsSettle *s) { memset(s, 0, sizeof(*s)); }

bool ms_settle(MsSettle *s, uint32_t now, uint32_t sig, bool eligible, uint32_t quiet_ms)
{
    if (!s->primed || sig != s->sig) {
        s->primed = true;
        s->sig = sig;
        s->since = now;
        return false;
    }
    if (!eligible || (s->done && s->done_sig == sig)) {
        return false;
    }
    return now - s->since >= quiet_ms;
}

void ms_settle_saved(MsSettle *s)
{
    s->done = true;
    s->done_sig = s->sig;
}

void ms_settle_touch(MsSettle *s, uint32_t now) { s->since = now; }

void ms_keyq_init(MsKeyQ *q) { memset(q, 0, sizeof(*q)); }

bool ms_keyq_push(MsKeyQ *q, int key)
{
    if (q->n >= MS_KEYQ) {
        q->dropped++;
        return false;
    }
    q->k[q->n++] = key;
    return true;
}

int ms_keyq_pop(MsKeyQ *q)
{
    if (!q->n) {
        return -1;
    }
    int k = q->k[0];
    memmove(q->k, q->k + 1, sizeof(int) * --q->n);
    return k;
}
