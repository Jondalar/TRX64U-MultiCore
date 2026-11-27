#include "apple2_woz.h"
#include <stdio.h>
#include <string.h>

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

int a2w_open(CoreFile *f, A2Woz *w, char *msg, int msglen)
{
    memset(w, 0, sizeof(*w));
    w->f = f;
    uint8_t h[12];
    if (f->size() < 256 || !f->read(0, h, 12)) {
        snprintf(msg, msglen, "not a WOZ file (too short)");
        return 1;
    }
    if (memcmp(h, "WOZ", 3) != 0 || (h[3] != '1' && h[3] != '2') || h[4] != 0xFF || h[5] != 0x0A || h[6] != 0x0D || h[7] != 0x0A) {
        snprintf(msg, msglen, "not a WOZ 1 or 2 file");
        return 1;
    }
    w->version = (uint8_t)(h[3] - '0');
    bool have_info = false, have_tmap = false, have_trks = false;
    uint64_t fsize = f->size();
    uint64_t pos = 12;
    while (pos + 8 <= fsize) {
        uint8_t ch[8];
        if (!f->read(pos, ch, 8)) {
            snprintf(msg, msglen, "WOZ: cannot read the chunk list");
            return 1;
        }
        uint32_t size = le32(ch + 4);
        uint64_t data = pos + 8;
        if (!memcmp(ch, "INFO", 4) && size >= 3) {
            uint8_t info[64];
            uint32_t n = size < sizeof(info) ? size : (uint32_t)sizeof(info);
            if (!f->read(data, info, n)) {
                snprintf(msg, msglen, "WOZ: cannot read INFO");
                return 1;
            }
            if (info[0] >= 3) {
                snprintf(msg, msglen, "this WOZ image has FLUX tracks (WOZ 3); only bit-level WOZ 1 and 2 images are supported");
                return 1;
            }
            if (info[1] != 1) {
                snprintf(msg, msglen, "this WOZ image is a %s disk; only 5.25\" disks are supported", info[1] == 2 ? "3.5\"" : "foreign");
                return 1;
            }
            w->write_protect = info[2] != 0;
            have_info = true;
        } else if (!memcmp(ch, "TMAP", 4) && size >= 160) {
            if (!f->read(data, w->tmap, 160)) {
                snprintf(msg, msglen, "WOZ: cannot read TMAP");
                return 1;
            }
            have_tmap = true;
        } else if (!memcmp(ch, "TRKS", 4)) {
            w->trks_off = (uint32_t)data;
            w->trks_size = size;
            have_trks = true;
        } else if (!memcmp(ch, "FLUX", 4)) {
            snprintf(msg, msglen, "this WOZ image has FLUX tracks (WOZ 3); only bit-level WOZ 1 and 2 images are supported");
            return 1;
        }
        pos = data + size;
    }
    if (!have_info || !have_tmap || !have_trks) {
        snprintf(msg, msglen, "WOZ: INFO, TMAP or TRKS is missing");
        return 1;
    }
    return 0;
}

bool a2w_track(A2Woz *w, int q, uint8_t *bits, uint32_t *nbits, bool *noise)
{
    memset(bits, 0, A2N_BITS_MAX / 8);
    *nbits = 1000;
    *noise = true;
    if (q < 0 || q >= 160 || w->tmap[q] == 0xFF) {
        return true;
    }
    unsigned t = w->tmap[q];
    if (w->version == 2) {
        if ((t + 1) * 8u > 1280u) {
            return true;
        }
        uint8_t d[8];
        if (!w->f->read(w->trks_off + t * 8u, d, 8)) {
            return false;
        }
        uint32_t block = le16(d), count = le16(d + 2), nb = le32(d + 4);
        if (nb == 0 || block == 0 || count == 0) {
            return true;
        }
        if (nb > A2N_BITS_MAX) {
            nb = A2N_BITS_MAX;
        }
        uint32_t bytes = (nb + 7) / 8;
        if (bytes > count * 512u) {
            bytes = count * 512u;
            nb = bytes * 8;
        }
        if (!w->f->read((uint64_t)block * 512u, bits, bytes)) {
            return false;
        }
        *nbits = nb;
        *noise = false;
        return true;
    }
    // WOZ 1: 6,656 bytes a track: 6,646 of bits, bytes used (2), bit count (2), splice point (2), splice nibble, splice bits, 2 reserved
    uint8_t tr[6656];
    if ((t + 1) * 6656u > w->trks_size || !w->f->read(w->trks_off + t * 6656u, tr, 6656)) {
        return (t + 1) * 6656u > w->trks_size;
    }
    uint32_t nb = le16(tr + 6648);
    if (nb == 0) {
        return true;
    }
    if (nb > 6646u * 8u) {
        nb = 6646u * 8u;
    }
    memcpy(bits, tr, (nb + 7) / 8);
    *nbits = nb;
    *noise = false;
    return true;
}

bool a2w_save(A2Woz *w, int q, const uint8_t *bits, uint32_t nbits)
{
    if (q < 0 || q >= 160 || w->tmap[q] == 0xFF || !w->f->writable()) {
        return false;
    }
    unsigned t = w->tmap[q];
    uint32_t bytes = (nbits + 7) / 8;
    uint64_t at;
    if (w->version == 2) {
        uint8_t d[8];
        if (!w->f->read(w->trks_off + t * 8u, d, 8)) {
            return false;
        }
        uint32_t block = le16(d), count = le16(d + 2);
        if (block == 0 || bytes > count * 512u) {
            return false;
        }
        at = (uint64_t)block * 512u;
    } else {
        if (bytes > 6646u) {
            return false;
        }
        at = w->trks_off + t * 6656u;
    }
    if (!w->crc_cleared) {
        static const uint8_t zero[4] = { 0, 0, 0, 0 };
        if (!w->f->write(8, zero, 4)) {
            return false;
        }
        w->crc_cleared = true;
    }
    return w->f->write(at, bits, bytes);
}
