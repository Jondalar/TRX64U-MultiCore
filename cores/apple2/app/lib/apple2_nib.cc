#include "apple2_nib.h"
#include <string.h>

static const uint8_t enc[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF };

// physical sector -> file sector
static const uint8_t skew_dos[16]    = { 0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4, 0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF };
static const uint8_t skew_prodos[16] = { 0x0, 0x8, 0x1, 0x9, 0x2, 0xA, 0x3, 0xB, 0x4, 0xC, 0x5, 0xD, 0x6, 0xE, 0x7, 0xF };

#define SECT_RING 416u

uint8_t a2n_file_sector(A2Order order, int phys)
{
    return (order == A2_ORDER_PRODOS ? skew_prodos : skew_dos)[phys & 15];
}

static uint8_t rev2(uint8_t b) { return (uint8_t)(((b & 1) << 1) | ((b >> 1) & 1)); }

static void put44(uint8_t *p, uint8_t b)
{
    p[0] = (uint8_t)((b >> 1) | 0xAA);
    p[1] = (uint8_t)(b | 0xAA);
}

static void nibblize_data(const uint8_t *d, uint8_t *out)
{
    uint8_t s[342];
    for (int r = 0; r < 86; r++) {              // the 86 low-bit bytes come first, then the 256 top-six-bit bytes
        uint8_t a = rev2(d[r] & 3) | (uint8_t)(rev2(d[r + 86] & 3) << 2);
        if (r + 172 < 256) {
            a |= (uint8_t)(rev2(d[r + 172] & 3) << 4);
        }
        s[r] = a;
    }
    for (int i = 0; i < 256; i++) {
        s[86 + i] = d[i] >> 2;
    }
    uint8_t prev = 0;
    for (int k = 0; k < 342; k++) {
        out[k] = enc[s[k] ^ prev];
        prev = s[k];
    }
    out[342] = enc[prev];
}

void a2n_nibblize_track(const uint8_t *sectors, A2Order order, int track, uint8_t *ring)
{
    for (int p = 0; p < A2N_SECTORS; p++) {
        uint8_t *s = ring + p * SECT_RING;
        s[0] = 0xD5; s[1] = 0xAA; s[2] = 0x96;
        put44(s + 3, A2N_VOLUME);
        put44(s + 5, (uint8_t)track);
        put44(s + 7, (uint8_t)p);
        put44(s + 9, (uint8_t)(A2N_VOLUME ^ track ^ p));
        s[11] = 0xDE; s[12] = 0xAA; s[13] = 0xEB;
        memset(s + 14, 0xFF, 6);
        s[20] = 0xD5; s[21] = 0xAA; s[22] = 0xAD;
        nibblize_data(sectors + a2n_file_sector(order, p) * A2N_SECTOR_BYTES, s + 23);
        s[366] = 0xDE; s[367] = 0xAA; s[368] = 0xEB;
        memset(s + 369, 0xFF, SECT_RING - 369);
    }
}

static int dec_tab[256];
static bool dec_ready;
static void init_dec()
{
    if (dec_ready) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        dec_tab[i] = -1;
    }
    for (int i = 0; i < 64; i++) {
        dec_tab[enc[i]] = i;
    }
    dec_ready = true;
}

#define R(i) ring[(i) % ringlen]

static int get44(const uint8_t *ring, uint32_t ringlen, uint32_t i)
{
    return ((R(i) << 1) | 1) & R(i + 1);
}

static bool decode_data(const uint8_t *ring, uint32_t ringlen, uint32_t at, uint8_t *d)
{
    uint8_t s[342];
    uint8_t prev = 0;
    for (int k = 0; k < 342; k++) {
        int v = dec_tab[R(at + k)];
        if (v < 0) {
            return false;
        }
        prev ^= (uint8_t)v;
        s[k] = prev;
    }
    int c = dec_tab[R(at + 342)];
    if (c < 0 || c != prev) {
        return false;
    }
    for (int i = 0; i < 256; i++) {
        int r = i % 86, g = i / 86;             // group 0: bits 1:0 of aux r, 1: bits 3:2, 2: bits 5:4
        uint8_t a = (uint8_t)(s[r] >> (2 * g));
        d[i] = (uint8_t)((s[86 + i] << 2) | rev2(a & 3));
    }
    return true;
}

// The sectors of `ring` (n nibbles, a ring) that decode with good address and data checksums, for `track`, into `out` (4096 bytes, file order; the
// others stay as they are). Returns the mask of the physical sectors found.
static unsigned merge_ring(const uint8_t *ring, uint32_t ringlen, A2Order order, int track, uint8_t *out)
{
    init_dec();
    unsigned seen = 0;
    if (ringlen < 64) {
        return 0;
    }
    for (uint32_t i = 0; i < ringlen; i++) {
        if (R(i) != 0xD5 || R(i + 1) != 0xAA || R(i + 2) != 0x96) {
            continue;
        }
        int vol = get44(ring, ringlen, i + 3), trk = get44(ring, ringlen, i + 5), sec = get44(ring, ringlen, i + 7), chk = get44(ring, ringlen, i + 9);
        if ((vol ^ trk ^ sec) != chk || trk != track || sec > 15) {
            continue;
        }
        for (uint32_t j = i + 11; j < i + 11 + 48; j++) {      // the data prologue follows within a few bytes
            if (R(j) == 0xD5 && R(j + 1) == 0xAA && R(j + 2) == 0xAD) {
                uint8_t d[256];
                if (decode_data(ring, ringlen, j + 3, d)) {
                    memcpy(out + a2n_file_sector(order, sec) * A2N_SECTOR_BYTES, d, 256);
                    seen |= 1u << sec;
                }
                break;
            }
        }
    }
    return seen;
}

int a2n_merge_ring(const uint8_t *ring, uint32_t ringlen, A2Order order, int track, uint8_t *sectors)
{
    unsigned seen = merge_ring(ring, ringlen, order, track, sectors);
    int n = 0;
    for (int p = 0; p < 16; p++) {
        n += (seen >> p) & 1;
    }
    return n;
}

int a2n_denibblize_ring(const uint8_t *ring, uint32_t ringlen, A2Order order, int track, uint8_t *sectors)
{
    static uint8_t out[A2N_TRACK_DATA];
    if (merge_ring(ring, ringlen, order, track, out) != 0xFFFFu) {
        return -1;
    }
    memcpy(sectors, out, A2N_TRACK_DATA);
    return 16;
}

int a2n_denibblize_track(const uint8_t *ring, A2Order order, int track, uint8_t *sectors)
{
    return a2n_denibblize_ring(ring, A2N_TRACK, order, track, sectors);
}

// ---- bit tracks ---------------------------------------------------------------------------------------------------------------
namespace {
struct BitWriter {
    uint8_t *b;
    uint32_t n;
    void bit(unsigned v)
    {
        if (v) {
            b[n >> 3] = (uint8_t)(b[n >> 3] | (0x80u >> (n & 7)));
        }
        n++;
    }
    void byte(uint8_t v)
    {
        for (int i = 7; i >= 0; i--) {
            bit((v >> i) & 1u);
        }
    }
    void sync()
    {
        byte(0xFF);
        bit(0);
        bit(0);
    }
};
}

uint32_t a2n_sector_bits(const uint8_t *sectors, A2Order order, int track, uint8_t *bits)
{
    memset(bits, 0, A2N_BITS_MAX / 8);
    BitWriter w = { bits, 0 };
    for (int p = 0; p < A2N_SECTORS; p++) {
        for (int i = 0; i < 24; i++) {
            w.sync();
        }
        uint8_t a[8];
        put44(a, A2N_VOLUME);
        put44(a + 2, (uint8_t)track);
        put44(a + 4, (uint8_t)p);
        put44(a + 6, (uint8_t)(A2N_VOLUME ^ track ^ p));
        w.byte(0xD5); w.byte(0xAA); w.byte(0x96);
        for (int i = 0; i < 8; i++) {
            w.byte(a[i]);
        }
        w.byte(0xDE); w.byte(0xAA); w.byte(0xEB);
        for (int i = 0; i < 6; i++) {
            w.sync();
        }
        uint8_t d[343];
        nibblize_data(sectors + a2n_file_sector(order, p) * A2N_SECTOR_BYTES, d);
        w.byte(0xD5); w.byte(0xAA); w.byte(0xAD);
        for (int i = 0; i < 343; i++) {
            w.byte(d[i]);
        }
        w.byte(0xDE); w.byte(0xAA); w.byte(0xEB);
    }
    return w.n;
}

uint32_t a2n_nib_bits(const uint8_t *nib, uint8_t *bits)
{
    memset(bits, 0, A2N_BUF_BITS / 8);
    // the length of the circular run of FF that nibble i belongs to (0 when it is not FF)
    static uint16_t run[A2N_TRACK];
    uint32_t ff = 0;
    for (uint32_t i = 0; i < A2N_TRACK; i++) {
        ff += nib[i] == 0xFF;
    }
    if (ff == A2N_TRACK) {
        for (uint32_t i = 0; i < A2N_TRACK; i++) {
            run[i] = (uint16_t)A2N_TRACK;
        }
    } else {
        uint32_t start = 0;
        while (nib[start] == 0xFF) {                           // a nibble that is not FF exists: begin right after one
            start++;
        }
        for (uint32_t k = 0; k < A2N_TRACK;) {
            uint32_t i = (start + k) % A2N_TRACK;
            if (nib[i] != 0xFF) {
                run[i] = 0;
                k++;
                continue;
            }
            uint32_t n = 0;
            while (k + n < A2N_TRACK && nib[(start + k + n) % A2N_TRACK] == 0xFF) {
                n++;
            }
            for (uint32_t j = 0; j < n; j++) {
                run[(start + k + j) % A2N_TRACK] = (uint16_t)n;
            }
            k += n;
        }
    }
    BitWriter w = { bits, 0 };
    for (uint32_t i = 0; i < A2N_TRACK; i++) {
        w.byte(nib[i]);
        if (run[i] >= 3 && w.n + 2 + (A2N_TRACK - 1 - i) * 8 <= A2N_BUF_BITS) {
            w.bit(0);
            w.bit(0);
        }
    }
    return w.n;
}

uint32_t a2n_bits_nibbles(const uint8_t *bits, uint32_t nbits, uint8_t *out, uint32_t max)
{
    if (nbits == 0) {
        return 0;
    }
    uint32_t n = 0;
    unsigned rsh = 0;
    for (uint32_t i = 0; i < 2 * nbits; i++) {
        uint32_t p = i >= nbits ? i - nbits : i;
        unsigned bit = (bits[p >> 3] >> (7 - (p & 7))) & 1u;
        rsh = ((rsh << 1) | bit) & 0xFFu;
        if (rsh & 0x80u) {
            if (i >= nbits && n < max) {
                out[n++] = (uint8_t)rsh;
            }
            rsh = 0;
        }
    }
    return n;
}
