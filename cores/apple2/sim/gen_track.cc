// T89.1 S5: a sector image's track as the application makes it (coreapp/cores/apple2_nib.cc), for tb_a2_woz.vhd. Writes sectrack.hex (the bits, one byte a line, MSB first)
// and sectnib.hex (the nibbles a Disk II shift register finds in it, one a line, from bit 0). The bench plays the first through the drive and compares the bytes
// a 6502 program reads with the second. Usage: gen_track <outdir>
#include "apple2_nib.h"
#include <stdio.h>
#include <string.h>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2) {
        return 2;
    }
    const int track = 5;
    std::vector<uint8_t> sec(4096);
    for (int s = 0; s < 16; s++) {
        for (int i = 0; i < 256; i++) {
            sec[s * 256 + i] = (uint8_t)(i * 7 + s * 13 + track * 3 + 1);
        }
    }
    static uint8_t bits[A2N_BITS_MAX / 8], nib[A2N_TRACK];
    uint32_t nbits = a2n_sector_bits(sec.data(), A2_ORDER_DOS, track, bits);
    uint32_t n = a2n_bits_nibbles(bits, nbits, nib, A2N_TRACK);
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
    snprintf(path, sizeof(path), "%s/sectnib.hex", argv[1]);
    f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        fprintf(f, "%02x\n", nib[i]);
    }
    fclose(f);
    printf("gen_track: %u bits, %u nibbles\n", nbits, n);
    return 0;
}
