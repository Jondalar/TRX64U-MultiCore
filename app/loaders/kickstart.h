// The Amiga Kickstart image format (T104 5.7, ADR-0056): which Kickstart a machine's Auto choice takes. A format belongs to no core
// (D2): the A500 and the A1200 both read it, and a future Amiga would.
//
// The file name is not a usable key: of the three 512 KB images Alex keeps, kick13.rom is the only one whose name carries a version, and the
// other two are Kickstart 3.1. The version is in the image: a Kickstart starts with 1111 (a 256 KB image) or 1114 (512 KB), and the
// big-endian word at offset 0x0C is the major version, the one at 0x0E the revision. Measured 2026-10-16: kick13.rom 1111 / 0022 0005 =
// 34.5 = Kickstart 1.3, the other two 1114 / 0028 0044 = 40.68 = Kickstart 3.1.
//
// Alex, 2026-10-16: the Amiga 500 and its kin load 1.3, the AGA machines load 3.1.
// Pure: no hardware and no file system, so the rule is a native bench.
#ifndef LOADERS_KICKSTART_H
#define LOADERS_KICKSTART_H

#include <stdint.h>

#define KS_VER_13    34       // Kickstart 1.3  = 34.5
#define KS_VER_31    40       // Kickstart 3.1  = 40.68
#define KS_VER_AGA   39       // 3.0 and up: the AGA family starts here
#define KS_HEAD      16       // the bytes ks_header_version() needs

// One candidate of the ROM directory. `ver` is 0 when the file carries no Kickstart header.
struct KsCandidate {
    uint32_t size;
    uint16_t ver;
    uint16_t rev;
};

// The first 16 bytes of a file. 1 = it is a Kickstart image and *ver / *rev are written, 0 = it is not.
int ks_header_version(const uint8_t *hdr, int len, uint16_t *ver, uint16_t *rev);

// The index Auto takes out of n candidates for a machine that wants major `want` (KS_VER_13 or KS_VER_31): the first with exactly that
// version, else the first of the same family (1.x/2.x up to 34, or 39 and up), else the first that is a Kickstart at all, else the first in
// the list. Never fails: a list of n >= 1 always has an answer.
int ks_pick(const KsCandidate *list, int n, uint16_t want);

#endif
