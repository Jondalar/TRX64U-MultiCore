// T89.1 (t89-write): what the application would make of the track the core handed back (tb_a2_rwts.vhd, S3/S4), decoded with the application's own code
// (apple2_nib.cc: the bits as a shift register finds the nibbles, a2n_merge_ring). Usage: chk_rwts <dir> wr|fmt [nbits]
//   wr   saved_wr.hex:  the image's 16 sectors, physical sector 5 replaced by (i * 11 + 90) & 255
//   fmt  saved_fmt.hex: 16 sectors of zeros
// All 16 sectors have to decode (address and data checksums, track 0) and hold the expected bytes. Exit 0 = good.
#include "apple2_nib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Every field of the track that is not the data field of physical sector 5 must be unchanged bit for bit (the bits of the track as the application played them:
// nibtrack.hex for a .nib, sectrack.hex for a sector image), and what the write left behind past that data field's epilogue has to be nothing. A field is the
// nibbles from its D5 AA 96 / D5 AA AD to the nibble before the next sync gap (address: 13 nibbles incl. DE AA, data: 349 incl. DE AA EB), found on the
// ORIGINAL bits with the shift register the application uses. Returns the number of damaged fields.
static int frame_check(const char *dir, const uint8_t *saved, uint32_t nbits, bool nib)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, nib ? "nibtrack.hex" : "sectrack.hex");
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("chk_rwts: %s missing\n", path);
        return 1;
    }
    static uint8_t orig[A2N_BITS_MAX / 8 + 4096];
    memset(orig, 0, sizeof(orig));
    unsigned v;
    uint32_t n = 0;
    while (fscanf(f, "%x", &v) == 1 && n < sizeof(orig)) {
        orig[n++] = (uint8_t)v;
    }
    fclose(f);
    auto bit = [&](const uint8_t *b, uint32_t p) { return (b[p >> 3] >> (7 - (p & 7))) & 1u; };
    struct Nib { uint32_t at; uint8_t v; };
    std::vector<Nib> nl;
    unsigned rsh = 0;
    for (uint32_t i = 0; i < nbits; i++) {
        rsh = ((rsh << 1) | bit(orig, i)) & 0xFFu;
        if (rsh & 0x80u) {
            nl.push_back({ i >= 7 ? i - 7 : 0, (uint8_t)rsh });      // the bit index of the nibble's first bit (the nibbles before the wrap are ignored)
            rsh = 0;
        }
    }
    // the nibble list is linear (no wrap): fields that straddle the end are not checked, the track has 16 of each in the middle
    uint32_t w_first = 0, w_last = 0;
    int damaged = 0, nf = 0;
    for (size_t k = 0; k + 3 < nl.size(); k++) {
        bool ad = nl[k].v == 0xD5 && nl[k + 1].v == 0xAA && nl[k + 2].v == 0x96;
        bool dd = nl[k].v == 0xD5 && nl[k + 1].v == 0xAA && nl[k + 2].v == 0xAD;
        if (!ad && !dd) {
            continue;
        }
        size_t len = ad ? 13 : 349;
        if (k + len >= nl.size()) {
            continue;
        }
        uint32_t b0 = nl[k].at, b1 = nl[k + len - 1].at + 8;     // the field in bits, excluding the sync that follows it
        // is this the data field of physical sector 5? the address field before it says so (sector = odd/even 4-and-4 at nibbles 7, 8)
        bool is5 = false;
        if (dd) {
            for (size_t j = k; j > 0 && k - j < 40; j--) {
                if (nl[j].v == 0xD5 && nl[j + 1].v == 0xAA && nl[j + 2].v == 0x96) {
                    is5 = ((((nl[j + 7].v << 1) | 1) & nl[j + 8].v) == 5);
                    break;
                }
            }
        }
        if (is5) {
            w_first = b0;
            w_last = b1;
            continue;
        }
        nf++;
        uint32_t bad = 0, firstbad = 0;
        for (uint32_t p = b0; p < b1 && p < nbits; p++) {
            if (bit(orig, p) != bit(saved, p)) {
                if (!bad) {
                    firstbad = p;
                }
                bad++;
            }
        }
        if (bad) {
            damaged++;
            printf("chk_rwts frame: %s field at bit %u (nibble %zu) changed in %u bits, first at bit %u\n", ad ? "address" : "data", b0, k, bad, firstbad);
        }
    }
    // what the write left behind: the last bit that differs from the original anywhere, against the end of the data field's epilogue
    uint32_t last = 0, first = nbits, total = 0;
    for (uint32_t p = 0; p < nbits; p++) {
        if (bit(orig, p) != bit(saved, p)) {
            total++;
            last = p;
            if (first == nbits) {
                first = p;
            }
        }
    }
    printf("chk_rwts frame: %d fields checked, %d damaged; written sector 5 data field = bits %u..%u; differing bits %u in %u..%u (%d bits past the epilogue)\n",
           nf, damaged, w_first, w_last, total, first, last, total ? (int)last - (int)w_last : 0);
    return damaged;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        return 2;
    }
    bool fmt = strcmp(argv[2], "fmt") == 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", argv[1], fmt ? "saved_fmt.hex" : "saved_wr.hex");
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("chk_rwts: %s missing\n", path);
        return 1;
    }
    static uint8_t bits[A2N_BITS_MAX / 8 + 4096], nib[A2N_TRACK + 8];
    memset(bits, 0, sizeof(bits));
    unsigned v;
    uint32_t n = 0;
    while (fscanf(f, "%x", &v) == 1 && n < sizeof(bits)) {
        bits[n++] = (uint8_t)v;
    }
    fclose(f);
    const uint32_t nbits = argc > 3 ? (uint32_t)atoi(argv[3]) : 51264;
    uint32_t nn = a2n_bits_nibbles(bits, nbits, nib, A2N_TRACK);
    std::vector<uint8_t> want(4096), sec(4096, 0xEE);
    for (int fs = 0; fs < 16; fs++) {
        for (int i = 0; i < 256; i++) {
            want[fs * 256 + i] = fmt ? 0 : (uint8_t)(i * 7 + fs * 13 + 1);
        }
    }
    if (!fmt) {
        int fs = a2n_file_sector(A2_ORDER_DOS, 5);
        for (int i = 0; i < 256; i++) {
            want[fs * 256 + i] = (uint8_t)(i * 11 + 90);
        }
    }
    int got = a2n_merge_ring(nib, nn, A2_ORDER_DOS, 0, sec.data());
    int wrong = 0;
    for (int fs = 0; fs < 16; fs++) {
        if (memcmp(&sec[fs * 256], &want[fs * 256], 256) != 0) {
            wrong++;
            printf("chk_rwts %s: file sector %d differs\n", argv[2], fs);
        }
    }
    printf("chk_rwts %s: %u nibbles, %d of 16 sectors decode, %d differ from the expected bytes\n", argv[2], nn, got, wrong);
    int frame = fmt ? 0 : frame_check(argv[1], bits, nbits, argc > 3);
    return (got == 16 && wrong == 0 && frame == 0) ? 0 : 1;
}
