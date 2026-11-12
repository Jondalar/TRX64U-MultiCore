#include "t64.h"
#include <string.h>

// Layout (the C64S container): 64 byte header with "C64" at the start, used entries at +0x24; 32 byte entries from +0x40: type (1 normal tape file, 3 memory snapshot),
// 1541 type, start, end (little endian), two reserved bytes, offset in the file at +8, the name at +16 padded with blanks.
static bool header(CoreFile *f, uint64_t size, int *used)
{
    uint8_t h[0x40];
    if (size < 0x40 || !f->read(0, h, 0x40) || memcmp(h, "C64", 3)) {
        return false;
    }
    *used = h[0x24] | (h[0x25] << 8);
    return true;
}

int t64_list(CoreFs *fs, const char *path, T64Entry *out, int max)
{
    CoreFile *f = fs->open(path, false);
    if (!f) {
        return -1;
    }
    int used = 0, n = 0;
    uint64_t size = f->size();
    if (!header(f, size, &used)) {
        fs->close(f);
        return -1;
    }
    for (int i = 0; i < used && i < 256 && n < max; i++) {
        uint8_t e[32];
        if (!f->read(0x40 + 32u * i, e, 32)) {
            break;
        }
        if (e[0] != 1) {
            continue;
        }
        T64Entry &t = out[n++];
        t.start = (uint16_t)(e[2] | (e[3] << 8));
        t.end = (uint16_t)(e[4] | (e[5] << 8));
        t.offset = (uint32_t)e[8] | ((uint32_t)e[9] << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        int l = 16;
        while (l > 0 && (e[16 + l - 1] == ' ' || e[16 + l - 1] == 0)) {
            l--;
        }
        for (int k = 0; k < l && k < 19; k++) {
            uint8_t ch = e[16 + k];
            t.name[k] = ch >= 0x20 && ch < 0x7F ? (char)ch : '?';
        }
        t.name[l < 19 ? l : 19] = 0;
    }
    fs->close(f);
    return n;
}

int t64_load(CoreFs *fs, const char *path, int index, uint8_t *out, uint32_t max)
{
    T64Entry list[64];
    int n = t64_list(fs, path, list, 64);
    if (n < 0 || index < 0 || index >= n) {
        return -1;
    }
    const T64Entry &t = list[index];
    uint32_t len = t.end > t.start ? (uint32_t)(t.end - t.start) : 0;        // some files carry a wrong end address: then the rest of the file is the program
    CoreFile *f = fs->open(path, false);
    if (!f) {
        return -1;
    }
    uint64_t fsz = f->size();
    if (t.offset >= fsz) {
        fs->close(f);
        return -1;
    }
    if (len == 0 || t.offset + len > fsz) {
        len = (uint32_t)(fsz - t.offset);
    }
    if (len + 2 > max) {
        fs->close(f);
        return -1;
    }
    out[0] = (uint8_t)t.start;
    out[1] = (uint8_t)(t.start >> 8);
    bool ok = f->read(t.offset, out + 2, len);
    fs->close(f);
    return ok ? (int)(len + 2) : -1;
}
