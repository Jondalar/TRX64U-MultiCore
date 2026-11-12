#include "espinspect.h"
#include "sha256.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

#define SCAN_BLK   32768u
#define SCAN_OV    48u              // bytes read beyond the block so a header that starts at its end is whole
#define HASH_BLK   16384u
#define IDENT      "ESP32 WiFi Bridge V"
#define IDENT_LEN  19u
#define DESC_MAGIC 0xABCD5432u
#define PTABLE_LEN 0xC00u           // the partition table image of the vendor updater: 3072 bytes

static uint8_t scan_buf[SCAN_BLK + SCAN_OV];
static uint8_t hash_buf[HASH_BLK + 64];

const char *esp_result_text(EspResult r)
{
    switch (r) {
    case ESP_FOUND: return "carries an ESP32 image set";
    case ESP_NONE: return "has no ESP32 part";
    case ESP_INCOMPLETE: return "has an ESP32 application but no bootloader and partition table before it";
    case ESP_DAMAGED: return "has an ESP32 image set whose application is damaged";
    case ESP_IO: return "cannot be read";
    case ESP_ABORTED: return "cancelled";
    }
    return "?";
}

struct Img {
    uint32_t total;          // image bytes up to and including the checksum byte (16-aligned)
    bool hash;               // a SHA-256 follows
    bool app;                // the first segment starts with the application descriptor
    char project[24];
};

// An ESP32-S3 image at `off`? Walks the segment headers; reads no segment data.
static bool image_at(CorSource &src, uint32_t off, Img *im)
{
    uint8_t h[32];
    uint32_t size = src.size();
    if (off > size || size - off < 32 || !src.read(off, h, 32)) {
        return false;
    }
    if (h[0] != 0xE9 || h[1] < 1 || h[1] > 16 || h[2] > 4 || h[12] != 0x09 || h[13] != 0x00) {
        return false;
    }
    memset(im, 0, sizeof(*im));
    im->hash = h[23] == 1;
    uint32_t pos = 24;
    for (unsigned i = 0; i < h[1]; i++) {
        uint8_t sh[8];
        if (off + pos + 8 > size || !src.read(off + pos, sh, 8)) {
            return false;
        }
        uint32_t len = le32(sh + 4);
        if (len > 0x1000000u || (uint64_t)off + pos + 8 + len > size) {
            return false;
        }
        pos += 8 + len;
    }
    im->total = (pos + 1 + 15) & ~15u;
    if ((uint64_t)off + im->total + (im->hash ? 32 : 0) > size) {
        return false;
    }
    uint32_t seg0 = le32(h + 28);
    if (seg0 >= 112) {
        uint8_t d[80];
        if (src.read(off + 32, d, 80) && le32(d) == DESC_MAGIC) {
            im->app = true;
            memcpy(im->project, d + 48, 23);
            im->project[23] = 0;
        }
    }
    return true;
}

static bool has_ident_at(const uint8_t *b, uint32_t n, uint32_t i, char *version)
{
    if (i + IDENT_LEN > n || memcmp(b + i, IDENT, IDENT_LEN)) {
        return false;
    }
    unsigned k = 0;
    version[k++] = 'V';
    for (uint32_t j = i + IDENT_LEN; j < n && k < 15 && ((b[j] >= '0' && b[j] <= '9') || b[j] == '.'); j++) {
        version[k++] = (char)b[j];
    }
    version[k] = 0;
    return k > 1;
}

// SHA-256 of the image against the 32 bytes behind it; while it is read, the application's IDENT_STRING is looked for (version != NULL).
static EspResult digest_of(CorSource &src, uint32_t off, const Img &im, char *version, EspProgress cb, void *ctx, bool (*abort)(void *), uint32_t scanned,
                           uint32_t total, bool *ok)
{
    sha256_t sh;
    sha256_init(&sh);
    uint32_t done = 0, carry = 0;
    bool found = false;
    while (done < im.total) {
        if (abort && abort(ctx)) {
            return ESP_ABORTED;
        }
        uint32_t c = im.total - done > HASH_BLK ? HASH_BLK : im.total - done;
        if (!src.read(off + done, hash_buf + 32, c)) {
            return ESP_IO;
        }
        sha256_update(&sh, hash_buf + 32, c);
        if (version && !found) {
            uint32_t n = carry + c;
            bool last = done + c == im.total;
            for (uint32_t i = 0; i + IDENT_LEN < n && !found; i++) {
                if (last || i + IDENT_LEN + 8 <= n) {                          // the digits of the version are whole (or the image ends)
                    found = has_ident_at(hash_buf + 32 - carry, n, i, version);
                }
            }
        }
        carry = c >= 32 ? 32 : c;
        memmove(hash_buf + 32 - carry, hash_buf + 32 + c - carry, carry);
        done += c;
        if (cb) {
            cb(ctx, scanned + done < total ? scanned + done : total, total);
        }
    }
    uint8_t d[32], want[32];
    sha256_final(&sh, d);
    *ok = !im.hash || (src.read(off + im.total, want, 32) && !memcmp(d, want, 32));
    return ESP_FOUND;
}

// Partition table at `off`: entries 0xAA 0x50, one of them the factory application at 0x10000.
static bool ptable_at(CorSource &src, uint32_t off)
{
    uint8_t t[128];
    uint32_t size = src.size();
    if (off > size || size - off < sizeof(t) || !src.read(off, t, sizeof(t))) {
        return false;
    }
    for (unsigned i = 0; i < 4; i++) {
        const uint8_t *e = t + 32 * i;
        if (e[0] != 0xAA || e[1] != 0x50) {
            return false;
        }
        if (e[2] == 0 && e[3] == 0 && le32(e + 4) == 0x10000u) {
            return true;
        }
    }
    return false;
}

EspResult esp_inspect(CorSource &src, EspInfo *out, EspProgress cb, void *ctx, bool (*abort)(void *))
{
    memset(out, 0, sizeof(*out));
    uint32_t size = src.size(), pos = 0;
    uint32_t total = size;
    while (pos < size && !out->app) {
        if (abort && abort(ctx)) {
            return ESP_ABORTED;
        }
        uint32_t n = size - pos > SCAN_BLK ? SCAN_BLK : size - pos, avail = size - pos > SCAN_BLK + SCAN_OV ? SCAN_BLK + SCAN_OV : size - pos;
        if (!src.read(pos, scan_buf, avail)) {
            return ESP_IO;
        }
        for (uint32_t i = 0; i < n && !out->app; i++) {
            uint8_t b = scan_buf[i];
            if (b == 0xE9 && i + 14 <= avail && scan_buf[i + 12] == 0x09 && scan_buf[i + 13] == 0x00) {
                Img im;
                if (!image_at(src, pos + i, &im)) {
                    continue;
                }
                bool ok = false;
                if (!im.app) {
                    if (!out->boot) {
                        EspResult r = digest_of(src, pos + i, im, 0, 0, ctx, abort, pos + i, total, &ok);
                        if (r == ESP_ABORTED || r == ESP_IO) {
                            return r;
                        }
                        if (ok) {
                            out->boot = true;
                            out->boot_off = pos + i;
                            out->boot_len = im.total + (im.hash ? 32 : 0);
                        }
                    }
                    continue;
                }
                char ver[16] = "";
                EspResult r = digest_of(src, pos + i, im, ver, cb, ctx, abort, pos + i, total, &ok);
                if (r == ESP_ABORTED || r == ESP_IO) {
                    return r;
                }
                out->app = true;
                out->app_off = pos + i;
                out->app_len = im.total + (im.hash ? 32 : 0);
                out->digest_ok = ok;
                memcpy(out->version, ver, sizeof(out->version));
                memcpy(out->project, im.project, sizeof(out->project));
            } else if (b == 0xAA && out->boot && !out->ptable && i + 1 < avail && scan_buf[i + 1] == 0x50 && ptable_at(src, pos + i)) {
                out->ptable = true;
                out->ptable_off = pos + i;
                out->ptable_len = size - (pos + i) < PTABLE_LEN ? size - (pos + i) : PTABLE_LEN;
            }
        }
        pos += n;
        if (cb) {
            cb(ctx, pos, total);
        }
    }
    if (!out->app) {
        return ESP_NONE;
    }
    if (!out->digest_ok) {
        return ESP_DAMAGED;
    }
    if (!out->boot || !out->ptable) {
        return ESP_INCOMPLETE;
    }
    return ESP_FOUND;
}
