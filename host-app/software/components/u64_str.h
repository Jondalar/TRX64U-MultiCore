#ifndef U64_STR_H
#define U64_STR_H

#include <stdint.h>

// The firmware's printf (small_printf.cc) has no 64 bit conversion: %llu prints garbage. A 64 bit number (file size, offset;
// exFAT files can exceed 4 GiB) goes through this and is printed with %s. buf takes 21 bytes (20 digits and the NUL).
static inline char *u64_dec(uint64_t v, char *buf)
{
    char tmp[21];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    int i = 0;
    while (n) {
        buf[i++] = tmp[--n];
    }
    buf[i] = 0;
    return buf;
}


// Right aligned in width columns (at most 20), the way %11d does it; the firmware's %s pads on the right.
static inline char *u64_dec_pad(uint64_t v, char *buf, int width)
{
    char dig[21];
    u64_dec(v, dig);
    int n = 0;
    while (dig[n]) {
        n++;
    }
    int pad = (width > n && width <= 20) ? width - n : 0;
    for (int i = 0; i < pad; i++) {
        buf[i] = ' ';
    }
    for (int i = 0; i <= n; i++) {
        buf[pad + i] = dig[i];
    }
    return buf;
}

#endif
