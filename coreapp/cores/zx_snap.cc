#include "zx_snap.h"
#include <stdio.h>
#include <string.h>

namespace {

uint8_t store[8][ZX_BANK_BYTES];
uint8_t lin48[3 * ZX_BANK_BYTES];             // a version 1 .z80 is one run of 48 KB

struct Rd {
    CoreFile *f;
    uint64_t size;
    uint8_t cache[1024];
    uint64_t coff;
    uint32_t cn;
    bool fail;

    bool at(uint64_t o, uint8_t *v)
    {
        if (o >= size) {
            return false;
        }
        if (cn == 0 || o < coff || o >= coff + cn) {
            uint32_t n = size - o < sizeof(cache) ? (uint32_t)(size - o) : (uint32_t)sizeof(cache);
            if (!f->read(o, cache, n)) {
                fail = true;
                cn = 0;
                return false;
            }
            coff = o;
            cn = n;
        }
        *v = cache[o - coff];
        return true;
    }
    uint32_t u16(uint64_t o)
    {
        uint8_t a = 0, b = 0;
        at(o, &a);
        at(o + 1, &b);
        return a | (b << 8);
    }
    bool block(uint64_t o, uint8_t *dst, uint32_t n)
    {
        if (o > size || n > size - o) {
            return false;
        }
        if (!f->read(o, dst, n)) {
            fail = true;
            return false;
        }
        return true;
    }
};

// The Z80 format's run-length coding: ED ED count byte is a run, everything else is itself. A run is only taken when all four bytes are
// still inside the source (the reference's rule). False when the source ends before `size` bytes are out, or a run overshoots.
bool unpack(Rd &r, uint64_t src, uint32_t src_len, uint8_t *dst, uint32_t size)
{
    uint32_t out = 0, i = 0;
    while (out < size) {
        uint8_t b, b1, cnt, val;
        if (i >= src_len || !r.at(src + i, &b)) {
            return false;
        }
        if (i + 4 <= src_len && b == 0xED && r.at(src + i + 1, &b1) && b1 == 0xED && r.at(src + i + 2, &cnt) && r.at(src + i + 3, &val)) {
            if (cnt > size - out) {
                return false;
            }
            memset(dst + out, val, cnt);
            out += cnt;
            i += 4;
        } else {
            dst[out++] = b;
            i++;
        }
    }
    return true;
}

void pack_dir(ZxSnap *s, const uint8_t a, const uint8_t f, const uint8_t a2, const uint8_t f2, const uint8_t i, const uint8_t r, uint32_t sp, uint32_t pc,
              uint32_t bc, uint32_t de, uint32_t hl, uint32_t ix, uint32_t bc2, uint32_t de2, uint32_t hl2, uint32_t iy, uint32_t im, bool iff1,
              bool iff2)
{
    uint8_t *d = s->dir;
    d[0] = a; d[1] = f; d[2] = a2; d[3] = f2; d[4] = i; d[5] = r;
    const uint32_t w[10] = { sp, pc, bc, de, hl, ix, bc2, de2, hl2, iy };
    for (int k = 0; k < 10; k++) {
        d[6 + 2 * k] = (uint8_t)w[k];
        d[7 + 2 * k] = (uint8_t)(w[k] >> 8);
    }
    d[26] = (uint8_t)((im & 3) | (iff1 ? 4 : 0) | (iff2 ? 8 : 0));
}

int refuse(char *msg, int msglen, const char *text)
{
    snprintf(msg, msglen, "%s", text);
    return 1;
}

int parse_sna(Rd &r, ZxSnap *s, char *msg, int msglen)
{
    uint8_t h[27];
    if (r.size != 49179 && r.size != 131103 && r.size != 147487) {
        snprintf(msg, msglen, "an SNA of %u bytes is neither a 48K nor a 128K snapshot", (unsigned)(r.size > 0xFFFFFFFFu ? 0xFFFFFFFFu : r.size));
        return 1;
    }
    if (!r.block(0, h, 27)) {
        return refuse(msg, msglen, "cannot read the snapshot (the medium is gone?)");
    }
    auto w = [&](int o) { return (uint32_t)(h[o] | (h[o + 1] << 8)); };
    bool iff2 = (h[19] >> 2) & 1;
    s->border = h[26] & 7;
    s->model = r.size == 49179 ? 48 : 128;
    s->banks = 0;
    if (s->model == 48) {
        if (!r.block(27, store[5], ZX_BANK_BYTES) || !r.block(27 + ZX_BANK_BYTES, store[2], ZX_BANK_BYTES) ||
            !r.block(27 + 2 * ZX_BANK_BYTES, store[0], ZX_BANK_BYTES)) {
            return refuse(msg, msglen, "cannot read the snapshot (the medium is gone?)");
        }
        s->banks = (1 << 5) | (1 << 2) | 1;
        uint32_t sp = w(23);
        if (sp < 0x4000 || sp == 0xFFFF) {
            return refuse(msg, msglen, "the 48K snapshot's stack is not in RAM, so its PC cannot be read");
        }
        auto ram = [&](uint32_t a) { return store[(a >> 14) == 1 ? 5 : (a >> 14) == 2 ? 2 : 0][a & 0x3FFF]; };
        uint32_t pc = ram(sp) | (ram(sp + 1) << 8);
        pack_dir(s, h[22], h[21], h[8], h[7], h[0], h[20], (sp + 2) & 0xFFFF, pc, w(13), w(11), w(9), w(17), w(5), w(3), w(1), w(15), h[25],
                 iff2, iff2);
        s->p7ffd = 0;
        memset(s->ay, 0, sizeof(s->ay));
        s->ay_sel = 0;
        return 0;
    }
    uint8_t tail[4];
    if (!r.block(27 + 3 * ZX_BANK_BYTES, tail, 4)) {
        return refuse(msg, msglen, "cannot read the snapshot (the medium is gone?)");
    }
    uint32_t pc = tail[0] | (tail[1] << 8);
    uint8_t p7 = tail[2];
    uint32_t cur = p7 & 7;
    uint32_t rest[8], nrest = 0;
    for (uint32_t b = 0; b < 8; b++) {
        if (b != 5 && b != 2 && b != cur) {
            rest[nrest++] = b;
        }
    }
    if (r.size < 27 + 3 * ZX_BANK_BYTES + 4 + (uint64_t)nrest * ZX_BANK_BYTES) {
        return refuse(msg, msglen, "the 128K snapshot is shorter than its paging port says");
    }
    if (!r.block(27, store[5], ZX_BANK_BYTES) || !r.block(27 + ZX_BANK_BYTES, store[2], ZX_BANK_BYTES) ||
        !r.block(27 + 2 * ZX_BANK_BYTES, store[cur], ZX_BANK_BYTES)) {
        return refuse(msg, msglen, "cannot read the snapshot (the medium is gone?)");
    }
    uint64_t o = 27 + 3 * ZX_BANK_BYTES + 4;
    for (uint32_t k = 0; k < nrest; k++, o += ZX_BANK_BYTES) {
        if (!r.block(o, store[rest[k]], ZX_BANK_BYTES)) {
            return refuse(msg, msglen, "cannot read the snapshot (the medium is gone?)");
        }
    }
    s->banks = 0xFF;
    pack_dir(s, h[22], h[21], h[8], h[7], h[0], h[20], w(23), pc, w(13), w(11), w(9), w(17), w(5), w(3), w(1), w(15), h[25], iff2, iff2);
    s->p7ffd = p7;
    memset(s->ay, 0, sizeof(s->ay));                       // the SNA has no AY registers
    s->ay_sel = 0;
    return 0;
}

int parse_z80(Rd &r, ZxSnap *s, char *msg, int msglen)
{
    uint8_t h[30 + 2 + 55];
    if (r.size < 30) {
        return refuse(msg, msglen, "the .z80 file is too short");
    }
    uint32_t hn = r.size < sizeof(h) ? (uint32_t)r.size : (uint32_t)sizeof(h);
    if (!r.block(0, h, hn)) {
        return refuse(msg, msglen, "cannot read the snapshot (the medium is gone?)");
    }
    auto w = [&](int o) { return (uint32_t)(h[o] | (h[o + 1] << 8)); };
    uint32_t flags = h[12] == 255 ? 1 : h[12];
    uint32_t a = h[0], f = h[1], bc = w(2), hl = w(4), pc = w(6), sp = w(8), i = h[10], rr = (h[11] & 0x7F) | ((flags & 1) << 7);
    s->border = (flags >> 1) & 7;
    uint32_t de = w(13), bc2 = w(15), de2 = w(17), hl2 = w(19), a2 = h[21], f2 = h[22], iy = w(23), ix = w(25);
    bool iff1 = h[27] != 0, iff2 = h[28] != 0;
    uint32_t im = h[29] & 3;
    memset(s->ay, 0, sizeof(s->ay));
    s->ay_sel = 0;
    s->p7ffd = 0;
    s->banks = 0;
    if (pc != 0) {                                         // version 1: a 48K machine, one block
        uint64_t body = 30;
        if (flags & 0x20) {
            uint8_t end[4];
            if (r.size < body + 4 || !r.block(r.size - 4, end, 4) || end[0] != 0x00 || end[1] != 0xED || end[2] != 0xED || end[3] != 0x00) {
                return refuse(msg, msglen, "the version 1 .z80 has no end marker");
            }
            if (!unpack(r, body, (uint32_t)(r.size - body - 4), lin48, sizeof(lin48))) {
                return refuse(msg, msglen, "the .z80 file's compression is damaged");
            }
        } else if (!r.block(body, lin48, sizeof(lin48))) {
            return refuse(msg, msglen, r.fail ? "cannot read the snapshot (the medium is gone?)" : "the version 1 .z80 is shorter than 48 KB");
        }
        memcpy(store[5], lin48, ZX_BANK_BYTES);
        memcpy(store[2], lin48 + ZX_BANK_BYTES, ZX_BANK_BYTES);
        memcpy(store[0], lin48 + 2 * ZX_BANK_BYTES, ZX_BANK_BYTES);
        s->banks = (1 << 5) | (1 << 2) | 1;
        s->model = 48;
        pack_dir(s, a, f, a2, f2, i, rr, sp, pc, bc, de, hl, ix, bc2, de2, hl2, iy, im, iff1, iff2);
        return 0;
    }
    if (hn < 32) {
        return refuse(msg, msglen, "the .z80 file is too short");
    }
    uint32_t elen = w(30);
    if (elen != 23 && elen != 54 && elen != 55) {
        snprintf(msg, msglen, "the .z80 file has an unknown extended header (%u bytes)", (unsigned)elen);
        return 1;
    }
    if (hn < 32 + elen) {
        return refuse(msg, msglen, "the .z80 file is too short");
    }
    bool v2 = elen == 23;
    uint32_t hw = h[34];
    int model = 0;
    if (v2) {
        model = hw == 0 || hw == 1 ? 48 : hw == 3 || hw == 4 ? 128 : 0;
    } else {
        model = hw == 0 || hw == 1 ? 48 : hw == 4 || hw == 5 ? 128 : 0;
    }
    if (!model) {
        snprintf(msg, msglen, "the .z80 file is for hardware %u (version %d), not a 48K or 128K Spectrum", (unsigned)hw, v2 ? 2 : 3);
        return 1;
    }
    pc = w(32);
    if (model == 128) {
        s->p7ffd = h[35];
        memcpy(s->ay, h + 39, 16);
        s->ay_sel = h[38] & 15;
    }
    uint16_t seen = 0;                                     // bit n = page n
    uint64_t o = 32 + elen;
    while (o + 3 <= r.size) {
        uint32_t ln = r.u16(o), pg = 0;
        uint8_t pgb = 0;
        r.at(o + 2, &pgb);
        pg = pgb;
        o += 3;
        int bank = model == 48 ? (pg == 8 ? 5 : pg == 4 ? 2 : pg == 5 ? 0 : -1) : (pg >= 3 && pg <= 10 ? (int)pg - 3 : -1);
        uint8_t *dst = bank >= 0 ? store[bank] : lin48;    // a page this machine does not have is read and dropped
        if (ln == 0xFFFF) {
            if (!r.block(o, dst, ZX_BANK_BYTES)) {
                return refuse(msg, msglen, r.fail ? "cannot read the snapshot (the medium is gone?)" : "the .z80 file ends inside a page");
            }
            o += ZX_BANK_BYTES;
        } else {
            if (o + ln > r.size) {
                return refuse(msg, msglen, "the .z80 file ends inside a page");
            }
            if (!unpack(r, o, ln, dst, ZX_BANK_BYTES)) {
                return refuse(msg, msglen, r.fail ? "cannot read the snapshot (the medium is gone?)" : "the .z80 file's compression is damaged");
            }
            o += ln;
        }
        if (bank >= 0) {
            seen |= (uint16_t)(1u << pg);
        }
    }
    if (model == 48) {
        static const int need48[3] = { 8, 4, 5 };
        for (int k = 0; k < 3; k++) {
            if (!(seen & (1u << need48[k]))) {
                snprintf(msg, msglen, "the 48K .z80 file has no page %d", need48[k]);
                return 1;
            }
        }
        s->banks = (1 << 5) | (1 << 2) | 1;
    } else {
        for (int pg = 3; pg <= 10; pg++) {
            if (!(seen & (1u << pg))) {
                snprintf(msg, msglen, "the 128K .z80 file has no page %d", pg);
                return 1;
            }
        }
        s->banks = 0xFF;
    }
    s->model = model;
    pack_dir(s, a, f, a2, f2, i, rr, sp, pc, bc, de, hl, ix, bc2, de2, hl2, iy, im, iff1, iff2);
    return 0;
}

}  // namespace

int zx_snap_parse(CoreFile *f, bool z80, ZxSnap *s, char *msg, int msglen)
{
    Rd r;
    memset(&r, 0, sizeof(r));
    r.f = f;
    r.size = f->size();
    memset(s, 0, sizeof(*s));
    memset(store, 0, sizeof(store));
    int rc = z80 ? parse_z80(r, s, msg, msglen) : parse_sna(r, s, msg, msglen);
    if (rc) {
        memset(store, 0, sizeof(store));
        memset(s, 0, sizeof(*s));
    }
    return rc;
}

const uint8_t *zx_snap_bank(int n)
{
    return store[n & 7];
}

bool zx_snap_kind(const char *path, bool *z80)
{
    const char *dot = strrchr(path, '.');
    if (dot && !strcasecmp(dot, ".z80")) {
        *z80 = true;
        return true;
    }
    if (dot && !strcasecmp(dot, ".sna")) {
        *z80 = false;
        return true;
    }
    return false;
}
