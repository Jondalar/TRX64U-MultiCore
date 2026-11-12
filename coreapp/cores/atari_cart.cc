#include "atari_cart.h"
#include <stdio.h>
#include <string.h>

namespace {
struct Type { uint8_t car; uint16_t kb; uint8_t mode; };
// car = CAR type, kb = data size in KB (2 and 4 KB types are listed in KB x 1 below as 2 and 4), mode = cart_logic.vhd constant
const Type types[] = {
    {  1,    8, 0x01 }, {  2,   16, 0x21 }, {  3,   16, 0x04 }, {  8,   64, 0x0D }, {  9,   64, 0x0B }, { 10,   64, 0x0A },
    { 11,   64, 0x08 }, { 12,   32, 0x30 }, { 13,   64, 0x31 }, { 14,  128, 0x32 }, { 17,  128, 0x0C }, { 21,    8, 0x14 },
    { 22,   32, 0x0E }, { 23,  256, 0x33 }, { 24,  512, 0x34 }, { 25, 1024, 0x35 }, { 26,   16, 0x28 }, { 27,   32, 0x29 },
    { 28,   64, 0x2A }, { 29,  128, 0x2B }, { 30,  256, 0x2C }, { 31,  512, 0x2D }, { 32, 1024, 0x2E }, { 33,   32, 0x38 },
    { 34,   64, 0x39 }, { 35,  128, 0x3A }, { 36,  256, 0x3B }, { 37,  512, 0x3C }, { 38, 1024, 0x3D }, { 39,    8, 0x40 },
    { 40,   16, 0x23 }, { 41,  128, 0x02 }, { 42, 1024, 0x03 }, { 43,  128, 0x09 }, { 44,    8, 0x05 }, { 45,   16, 0x06 },
    { 46,    4, 0x12 }, { 47,   32, 0x41 }, { 48,   64, 0x43 }, { 49,  128, 0x44 }, { 50,   64, 0x45 }, { 51,  128, 0x46 },
    { 52,   32, 0x47 }, { 54,  128, 0x24 }, { 55,  256, 0x25 }, { 56,  512, 0x26 }, { 57,    2, 0x16 }, { 58,    4, 0x17 },
    { 59,    4, 0x15 }, { 60,   32, 0x13 }, { 63, 4096, 0x20 }, { 64, 2048, 0x2F }, { 67,   64, 0x36 },
};
}

uint32_t cart_checksum(const uint8_t *d, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) {
        s += d[i];
    }
    return s;
}

CartInfo cart_probe(const uint8_t *h, uint32_t file_size)
{
    CartInfo c;
    memset(&c, 0, sizeof(c));
    if (file_size >= 16 && !memcmp(h, "CART", 4)) {
        int t = (h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
        c.data_off = 16;
        c.size = file_size - 16;
        c.car_type = t;
        for (const Type &e : types) {
            if (e.car == t) {
                if (c.size != (uint32_t)e.kb * 1024) {
                    snprintf(c.err, sizeof(c.err), "CAR type %d is %u KB but the file holds %u bytes", t, e.kb, (unsigned)c.size);
                    return c;
                }
                c.mode = e.mode;
                c.ok = true;
                return c;
            }
        }
        snprintf(c.err, sizeof(c.err), "CAR type %d is not one this core has", t);
        return c;
    }
    c.size = file_size;
    switch (file_size) {
    case 2048:  c.mode = 0x16; break;
    case 4096:  c.mode = 0x17; break;
    case 8192:  c.mode = 0x01; break;
    case 16384: c.mode = 0x21; break;
    default:
        snprintf(c.err, sizeof(c.err), "a raw image of %u bytes has no known type; use a .car file", (unsigned)file_size);
        return c;
    }
    c.ok = true;
    return c;
}
