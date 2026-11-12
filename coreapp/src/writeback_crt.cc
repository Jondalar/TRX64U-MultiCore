#include "writeback_crt.h"
#include <stdio.h>
#include <string.h>

#define CRT_HDRSIZE 0x10
#define CHP_LEN     0x04
#define CHP_TYPE    0x08
#define CHP_BANK    0x0A
#define CHP_LOAD    0x0C
#define CHP_SIZE    0x0E
#define BANK        0x4000u
#define BLOCK       0x2000u
#define EEPROM_LOAD 0xDE00
#define EEPROM_BYTES 0x800u

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xFFFF); }

namespace {

struct Pkt {
    uint32_t pos;                         // the packet's header in the file
    uint32_t len;                         // header and data
    uint16_t type, bank, load, size;
    bool image;                           // its data is the cartridge image
    uint32_t img_off;                     // ... at this offset in the image
};

// The packet at `pos`; false when there is none (the end, or padding).
bool packet_at(CoreFile *f, uint64_t fsize, uint32_t pos, const WbCrt *m, Pkt *p)
{
    uint8_t h[16];
    if (pos + 16 > fsize || !f->read(pos, h, 16) || memcmp(h, "CHIP", 4)) {
        return false;
    }
    p->pos = pos;
    p->len = be32(h + CHP_LEN);
    p->type = be16(h + CHP_TYPE);
    p->bank = be16(h + CHP_BANK);
    p->load = be16(h + CHP_LOAD);
    p->size = be16(h + CHP_SIZE);
    p->img_off = (uint32_t)p->bank * BANK + (p->load & 0x2000);
    uint32_t span = p->size < BLOCK ? BLOCK : p->size;
    p->image = p->size && (p->type == 0 || p->type == 2) && (p->load == 0x8000 || p->load == 0xA000 || p->load == 0xE000) && p->size <= BANK &&
               p->img_off + span <= m->img_bytes;
    return p->len >= 16u + p->size && pos + 16ull + p->size <= fsize;
}

// out <- bytes [from, from + n) of the old file
bool copy(CoreFile *old, CoreFile *out, uint64_t from, uint64_t to, uint32_t n, uint8_t *buf)
{
    while (n) {
        uint32_t k = n < BANK ? n : BANK;
        if (!old->read(from, buf, k) || !out->write(to, buf, k)) {
            return false;
        }
        from += k;
        to += k;
        n -= k;
    }
    return true;
}

}  // namespace

int wb_crt_write(CoreCtx *, void *user, CoreFile *old, CoreFile *out, const WbDirty &dirty, char *msg, int msglen)
{
    const WbCrt *m = (const WbCrt *)user;
    static uint8_t buf[BANK];
    uint64_t fsize = old->size();
    uint8_t h[0x40];
    if (fsize < 0x40 || !old->read(0, h, 0x40) || memcmp(h, "C64 CARTRIDGE   ", 16)) {
        snprintf(msg, msglen, "the file is no longer a CRT file");
        return 1;
    }
    uint32_t hdr = be32(h + CRT_HDRSIZE);
    if (hdr < 0x40) {
        hdr = 0x40;
    }

    // pass 1: which blocks of the image a packet holds, where the packets end, whether there is an EEPROM packet, the packet type of the file
    static uint32_t covered[WB_BLOCKS_MAX / 32];
    memset(covered, 0, sizeof(covered));
    uint32_t end = hdr, npk = 0;
    uint16_t ctype = 0;
    bool have_ee = false, have_img = false;
    Pkt p;
    while (packet_at(old, fsize, end, m, &p)) {
        npk++;
        if (p.image) {
            uint32_t span = p.size < BLOCK ? BLOCK : p.size;
            for (uint32_t b = p.img_off >> WB_BLOCK_SHIFT; b <= (p.img_off + span - 1) >> WB_BLOCK_SHIFT && b < WB_BLOCKS_MAX; b++) {
                covered[b >> 5] |= 1u << (b & 31);
            }
            if (!have_img) {
                ctype = p.type;
                have_img = true;
            }
        } else if (p.load == EEPROM_LOAD) {
            have_ee = true;
        }
        end += p.len;
    }
    if (!npk) {
        snprintf(msg, msglen, "the file has no CHIP packet where the header says");
        return 1;
    }

    // the EEPROM: a consistent copy before anything is written
    static uint8_t ee[EEPROM_BYTES];
    bool ee_new = dirty.aux && m->eeprom_read;
    if (ee_new && m->eeprom_read(m->eeprom_user, ee, sizeof(ee))) {
        snprintf(msg, msglen, "the EEPROM changed while it was copied");
        return 1;
    }

    // pass 2: header, the packets, the new ones, the rest
    uint64_t pos = 0;
    if (!copy(old, out, 0, 0, hdr, buf)) {
        goto fail;
    }
    pos = hdr;
    for (uint32_t at = hdr; packet_at(old, fsize, at, m, &p); at += p.len) {
        uint8_t ph[16];
        if (!old->read(at, ph, 16) || !out->write(pos, ph, 16)) {
            goto fail;
        }
        pos += 16;
        if (p.image) {
            m->io->ddr_read(m->img_base + p.img_off, buf, p.size);
            if (!out->write(pos, buf, p.size)) {
                goto fail;
            }
        } else if (ee_new && p.load == EEPROM_LOAD && p.size <= EEPROM_BYTES) {
            if (!out->write(pos, ee, p.size)) {
                goto fail;
            }
        } else if (!copy(old, out, at + 16ull, pos, p.size, buf)) {
            goto fail;
        }
        pos += p.size;
        if (p.len > 16u + p.size) {                       // whatever the packet announces beyond its data stays
            uint32_t extra = p.len - 16u - p.size;
            if (!copy(old, out, at + 16ull + p.size, pos, extra, buf)) {
                goto fail;
            }
            pos += extra;
        }
    }
    // blocks the file does not hold: erased ones are what the file implies already
    for (uint32_t b = 0; b < WB_BLOCKS_MAX; b++) {
        if (!dirty.test(b) || ((covered[b >> 5] >> (b & 31)) & 1) || (b << WB_BLOCK_SHIFT) + BLOCK > m->img_bytes) {
            continue;
        }
        uint32_t off = b << WB_BLOCK_SHIFT;
        m->io->ddr_read(m->img_base + off, buf, BLOCK);
        bool blank = true;
        for (uint32_t i = 0; i < BLOCK && blank; i++) {
            blank = buf[i] == 0xFF;
        }
        if (blank) {
            continue;
        }
        uint32_t bank = off / BANK;
        uint32_t load = (off & 0x2000) ? (bank == 0 ? 0xE000 : 0xA000) : 0x8000;
        uint8_t ph[16];
        memcpy(ph, "CHIP", 4);
        put32(ph + CHP_LEN, 16u + BLOCK);
        put16(ph + CHP_TYPE, ctype);
        put16(ph + CHP_BANK, bank);
        put16(ph + CHP_LOAD, load);
        put16(ph + CHP_SIZE, BLOCK);
        if (!out->write(pos, ph, 16) || !out->write(pos + 16, buf, BLOCK)) {
            goto fail;
        }
        pos += 16 + BLOCK;
    }
    if (ee_new && !have_ee) {
        uint8_t ph[16];
        memcpy(ph, "CHIP", 4);
        put32(ph + CHP_LEN, 16u + EEPROM_BYTES);
        put16(ph + CHP_TYPE, 0);
        put16(ph + CHP_BANK, 0);
        put16(ph + CHP_LOAD, EEPROM_LOAD);
        put16(ph + CHP_SIZE, EEPROM_BYTES);
        if (!out->write(pos, ph, 16) || !out->write(pos + 16, ee, EEPROM_BYTES)) {
            goto fail;
        }
        pos += 16 + EEPROM_BYTES;
    }
    if (end < fsize && !copy(old, out, end, pos, (uint32_t)(fsize - end), buf)) {
        goto fail;
    }
    return 0;
fail:
    snprintf(msg, msglen, "reading the old file or writing the new one failed (medium full or gone)");
    return 1;
}
