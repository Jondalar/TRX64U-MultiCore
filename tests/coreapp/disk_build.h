// Disks built by the benches, with files of their own: a .d64 (35 tracks) or a .d81 with its sectors in one buffer, a free-sector walker and the
// directory writer. No disk image of anyone else is in the repository.
#ifndef TESTS_COREAPP_DISK_BUILD_H
#define TESTS_COREAPP_DISK_BUILD_H

#include <vector>
#include <string.h>
#include <stdio.h>
#include "d1541_gcr.h"

typedef std::vector<uint8_t> Bytes;

struct Disk {
    bool d81;
    Bytes img;
    int dir_t, dir_s, dir_n;           // where the next directory entry goes
    int next_t, next_s;                // the next free sector for file data
    int d81_dir_sector;
    Disk(bool is81, int dir81 = 3) : d81(is81), d81_dir_sector(dir81)
    {
        if (d81) {
            img.assign(819200, 0);
            Bytes tmp(819200);
            d81_blank(tmp.data(), 819200, "TESTDISK");
            img = tmp;
            // the directory chain starts where the header says: the test moves it away from the usual sector
            sec(40, 0)[1] = (uint8_t)dir81;
            sec(40, 3)[1] = 0xFF;
            dir_t = 40; dir_s = dir81;
            next_t = 1; next_s = 0;
        } else {
            img.assign(D64_BUF_SIZE, 0);
            BinImage b;
            b.format("TESTDISK,01", 35);
            memcpy(img.data(), b.bin_data, D64_35_NO_ERRORS);
            img.resize(D64_35_NO_ERRORS);
            dir_t = 18; dir_s = 1;
            next_t = 1; next_s = 0;
        }
        sec(dir_t, dir_s)[0] = 0;
        sec(dir_t, dir_s)[1] = 0xFF;
        dir_n = 0;
    }
    uint8_t *sec(int t, int s)
    {
        size_t o = d81 ? ((size_t)(t - 1) * 40 + (size_t)s) * 256 : (size_t)(d64_sectors_before(t - 1) + s) * 256;
        return &img[o];
    }
    int per_track(int t) { return d81 ? 40 : d64_sectors(t - 1); }
    void take(int *t, int *s)
    {
        if (!d81 && next_t == 18) next_t = 19;
        if (d81 && next_t == 40) next_t = 41;
        *t = next_t; *s = next_s;
        if (++next_s >= per_track(next_t)) { next_s = 0; next_t++; }
    }
    // data into a chain; returns the first sector
    void chain(const Bytes &data, int *ft, int *fs, int *blocks)
    {
        size_t pos = 0;
        int prev_t = 0, prev_s = 0;
        *blocks = 0;
        do {
            int t, s;
            take(&t, &s);
            if (prev_t) { sec(prev_t, prev_s)[0] = (uint8_t)t; sec(prev_t, prev_s)[1] = (uint8_t)s; } else { *ft = t; *fs = s; }
            size_t n = data.size() - pos > 254 ? 254 : data.size() - pos;
            memcpy(sec(t, s) + 2, data.data() + pos, n);
            pos += n;
            sec(t, s)[0] = 0;
            sec(t, s)[1] = (uint8_t)(n + 1);              // the last sector: the index of the last used byte
            prev_t = t; prev_s = s;
            (*blocks)++;
        } while (pos < data.size());
    }
    void add(const uint8_t *nm, int nmlen, int type_byte, const Bytes &data)
    {
        int ft, fs, blocks;
        chain(data, &ft, &fs, &blocks);
        if (dir_n == 8) {                                 // a second directory sector
            int t, s;
            take(&t, &s);
            sec(dir_t, dir_s)[0] = (uint8_t)t; sec(dir_t, dir_s)[1] = (uint8_t)s;
            dir_t = t; dir_s = s; dir_n = 0;
            sec(t, s)[0] = 0; sec(t, s)[1] = 0xFF;
        }
        uint8_t *e = sec(dir_t, dir_s) + dir_n * 32;
        e[2] = (uint8_t)type_byte;
        e[3] = (uint8_t)ft; e[4] = (uint8_t)fs;
        memset(e + 5, 0xA0, 16);
        memcpy(e + 5, nm, (size_t)nmlen);
        e[30] = (uint8_t)blocks; e[31] = (uint8_t)(blocks >> 8);
        dir_n++;
    }
    void add(const char *name, int type_byte, const Bytes &data) { add((const uint8_t *)name, (int)strlen(name), type_byte, data); }
};

static Bytes pat(size_t n, int seed)
{
    Bytes v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(seed + i * 5 + (i >> 7));
    return v;
}


#endif
