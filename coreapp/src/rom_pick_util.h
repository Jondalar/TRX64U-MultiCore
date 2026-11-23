// Helpers of the shared drive-ROM pick (coreapp/cores/drive_rom_pick.h), kept here so the pick itself stays small (T192 D24).
#ifndef COREAPP_ROM_PICK_UTIL_H
#define COREAPP_ROM_PICK_UTIL_H

#include "sha256.h"
#include <stdint.h>
#include <string.h>

// The hex SHA-256 of a ROM, for the console line of what a drive holds.
static inline void rom_sha_hex(const uint8_t *d, uint32_t n, char *out)
{
    sha256_t h;
    uint8_t dig[32];
    sha256_init(&h);
    sha256_update(&h, d, n);
    sha256_final(&h, dig);
    sha256_hex(dig, out);
}

// T258 D6: the 8 KiB JiffyDOS file of the old 1541 is the upper half of the ROM (the chip 901229); the lower half (the chip 325302-01) is the first 8 KiB of
// the stock 1541 ROM. The drive maps a 32 KiB ROM as the CPU's window $8000-$FFFF, so the 1541 part is the last 16 KiB of the stock file (the whole file when it
// has 16 KiB). Builds the whole 16 KiB ROM into `out` (it may not overlap `half`) and returns its size, or 0 when there is no 16 KiB of stock ROM to take it from.
#define ROM_HALF 8192u
static inline uint32_t rom_half_over(uint8_t *out, const uint8_t *stock, uint32_t n_stock, const uint8_t *half)
{
    if (n_stock < 2 * ROM_HALF) {
        return 0;
    }
    memcpy(out, stock + (n_stock - 2 * ROM_HALF), ROM_HALF);
    memcpy(out + ROM_HALF, half, ROM_HALF);
    return 2 * ROM_HALF;
}

#endif
