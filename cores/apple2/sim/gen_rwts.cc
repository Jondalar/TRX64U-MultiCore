// T89.1 (t89-write): the track the application serves for tb_a2_rwts.vhd and the expected contents. Usage: gen_rwts <outdir>
//   sectrack.hex  the bits of track 0 of a sector image (apple2_nib.cc: 51,264 bits, 10-bit sync), one byte a line, MSB first
//   nibtrack.hex  the bits the application plays for a .nib image of the same sectors (a2n_nib_bits), nibbits.txt their number
//   sectexp.hex   the 16 x 256 bytes of the PHYSICAL sectors of that track (what a RWTS read of sector p must return), one byte a line
// File sector f, byte i holds (i * 7 + f * 13 + 1) & 255; the bench writes (i * 11 + 90) & 255 into physical sector 5.
#include "apple2_nib.h"
#include <stdio.h>
#include <string.h>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2) {
        return 2;
    }
    std::vector<uint8_t> sec(4096);
    for (int f = 0; f < 16; f++) {
        for (int i = 0; i < 256; i++) {
            sec[f * 256 + i] = (uint8_t)(i * 7 + f * 13 + 1);
        }
    }
    static uint8_t bits[A2N_BITS_MAX / 8];
    uint32_t nbits = a2n_sector_bits(sec.data(), A2_ORDER_DOS, 0, bits);
    char path[512];
    snprintf(path, sizeof(path), "%s/sectrack.hex", argv[1]);
    FILE *f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    for (uint32_t i = 0; i < (nbits + 7) / 8; i++) {
        fprintf(f, "%02x\n", bits[i]);
    }
    fclose(f);
    snprintf(path, sizeof(path), "%s/sectexp.hex", argv[1]);
    f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    for (int p = 0; p < 16; p++) {
        int fs = a2n_file_sector(A2_ORDER_DOS, p);
        for (int i = 0; i < 256; i++) {
            fprintf(f, "%02x\n", sec[fs * 256 + i]);
        }
    }
    fclose(f);
    // the same sectors as a .nib image holds them (the ring of 6,656 nibbles, 8-bit gaps) and the bits the application plays for it
    static uint8_t ring[A2N_TRACK], nbitsbuf[A2N_BITS_MAX / 8 + 4096];
    a2n_nibblize_track(sec.data(), A2_ORDER_DOS, 0, ring);
    uint32_t nn = a2n_nib_bits(ring, nbitsbuf);
    snprintf(path, sizeof(path), "%s/nibtrack.hex", argv[1]);
    f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    for (uint32_t i = 0; i < (nn + 7) / 8; i++) {
        fprintf(f, "%02x\n", nbitsbuf[i]);
    }
    fclose(f);
    snprintf(path, sizeof(path), "%s/nibbits.txt", argv[1]);
    f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    fprintf(f, "%u\n", nn);
    fclose(f);
    printf("gen_rwts: sector track %u bits, .nib track %u bits\n", nbits, nn);
    return 0;
}
