// Atari cartridge images (T93 D6, T88 Q5): raw 2/4/8/16 KB images and .car files, mapped to the core's CART1 mode. The CAR type
// numbers are those of atari800's cartridge_info.h (fetched 2026-10-15), the modes those of gen/a8core/cart_logic.vhd; a type
// is listed only where the core's mode has the same name and size, so a mapping is a name correspondence, not a bench result.
#ifndef COREAPP_ATARI_CART_H
#define COREAPP_ATARI_CART_H

#include <stdint.h>

struct CartInfo {
    bool ok;
    uint8_t mode;          // the value for CART1
    uint32_t size;         // bytes of cartridge data
    uint32_t data_off;     // where the data starts in the file (16 for a .car, 0 for a raw image)
    int car_type;          // 0 for a raw image
    char err[160];
};

// head = the first 16 bytes of the file (zero padded when it is shorter), file_size = its size.
CartInfo cart_probe(const uint8_t *head, uint32_t file_size);

// The sum of a .car's data bytes, as the header stores it (big endian, 4 bytes at offset 8).
uint32_t cart_checksum(const uint8_t *data, uint32_t n);

#endif
