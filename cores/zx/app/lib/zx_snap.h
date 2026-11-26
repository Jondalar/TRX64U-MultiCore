// ZX Spectrum snapshots (T91 D10, S11): .sna (48K, 128K) and .z80 (version 1 with RLE, 2, 3; 48K and 128K), read into the form the core's
// registers take. The file layouts are the formats' authors' (hardware facts); tools/zx_snap.py is the reference of this file and the
// bench's loader (its sna_read / z80_read and the register image of load_script), tests/coreapp/test_zx_snap.cc compares the two.
// Pure: CoreFile only.
#ifndef COREAPP_ZX_SNAP_H
#define COREAPP_ZX_SNAP_H

#include "fsif.h"

#define ZX_BANK_BYTES 16384u

struct ZxSnap {
    int model;                  // 48 or 128
    uint8_t dir[27];            // T80pa's register image (DIR, 212 bits), byte 0 = DIR[7:0]; byte 26 = IM | IFF1 << 2 | IFF2 << 3
    uint8_t p7ffd;              // the paging port (0 for a 48K)
    uint8_t border;             // 0..7
    uint8_t ay[16];             // the AY's registers (zeros for a 48K)
    uint8_t ay_sel;             // the selected AY register
    // the RAM banks the file holds: bit n = bank n (a 48K machine: 5, 2, 0)
    uint8_t banks;
};

// Reads the snapshot into *s and the banks into the module's own storage (zx_snap_bank). 0, or a reason in msg: a file that is not a 48K
// or 128K Spectrum, a damaged file, a stack outside RAM (a 48K SNA keeps its PC on the stack).
int zx_snap_parse(CoreFile *f, bool z80, ZxSnap *s, char *msg, int msglen);

// 16 KB of bank n (0..7) of the last parse; the banks the file does not hold are zeros.
const uint8_t *zx_snap_bank(int n);

// ".z80" -> true, ".sna" -> false in *z80; any other extension -> false returned.
bool zx_snap_kind(const char *path, bool *z80);

#endif
