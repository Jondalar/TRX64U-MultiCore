// Disk II track images (T89 P8, T89.1): the 6-and-2 nibblizer and its inverse, and the conversion between nibbles and the BIT tracks that
// the core's drive (drive_ii.vhd) plays. Pure, no I/O. The drive plays up to A2N_BUF_BITS bits of a track, MSB first, from a block RAM of 8,192 bytes.
// A nibble ring of A2N_TRACK = 0x1A00 nibbles is the .nib file's track; a sector image's track is built as bits directly (a2n_sector_bits).
// Layout of the 416 bytes of one physical sector of the nibble ring (16 of them fill the ring exactly, no gap of its own):
//   0-2 D5 AA 96, 3-10 volume, track, sector, checksum (each 4-and-4: odd = (b>>1)|AA, even = b|AA), 11-13 DE AA EB, 14-19 six FF,
//   20-22 D5 AA AD, 23-365 342 data nibbles + 1 checksum nibble, 366-368 DE AA EB, 369-415 forty-seven FF.
#ifndef COREAPP_APPLE2_NIB_H
#define COREAPP_APPLE2_NIB_H

#include <stdint.h>

#define A2N_TRACK        6656u
#define A2N_SECTORS      16
#define A2N_TRACKS       35
#define A2N_SECTOR_BYTES 256u
#define A2N_TRACK_DATA   4096u          // the sector bytes of one track in a .dsk/.do/.po file
#define A2N_VOLUME       254
#define A2N_BITS_MAX     53248u         // 6,656 bytes: the largest track of a WOZ file
#define A2N_BUF_BITS     65528u         // the core's buffer: 8,191 bytes of its 8,192 (the position and the bit count are 16 bits); a .nib track with its sync zeros

enum A2Order { A2_ORDER_DOS = 0, A2_ORDER_PRODOS = 1 };

// File sector that physical sector p holds: the DOS 3.3 order of a .dsk/.do file, or the ProDOS order of a .po file.
uint8_t a2n_file_sector(A2Order order, int phys);

// 16 sectors in file order (4096 bytes) -> the track ring.
void a2n_nibblize_track(const uint8_t *sectors, A2Order order, int track, uint8_t *ring);

// The ring -> 16 sectors in file order. Every physical sector must be found with a good address checksum (track = `track`) and a
// good data checksum, else nothing is written to `sectors` and the result is -1; otherwise 16. Volume is not checked. Guest writes
// that moved the fields around the ring (a sector rewritten in place) are found by scanning the ring circularly.
int a2n_denibblize_track(const uint8_t *ring, A2Order order, int track, uint8_t *sectors);

// ---- bit tracks (T89.1 D2, D7) ----------------------------------------------------------------------------------------------
// 16 sectors in file order (4096 bytes) -> the bits of a DOS 3.3 style track: per physical sector 24 self sync bytes (FF + two zero bits),
// the address field (D5 AA 96, volume 254, track, sector, checksum in 4-and-4, DE AA EB), 6 sync bytes, the data field (D5 AA AD, 342
// nibbles + checksum, DE AA EB). 51,264 bits. `bits` is A2N_BITS_MAX / 8 bytes, cleared here; returns the bit count.
uint32_t a2n_sector_bits(const uint8_t *sectors, A2Order order, int track, uint8_t *bits);
// A nibble ring of A2N_TRACK bytes (a .nib track) -> bits, 8 per nibble; every FF of a run of three or more FF (a gap: the ring is circular) is a self sync byte
// as on a disk, FF plus two zero bits, so the bytes after a sync are found again after a write has moved the bits (a write of a 40 cycle sync byte
// over 8 bit gaps left the reader out of step until the next zero: the next address field was not found). Isolated FF (data) stay 8 bits. `bits` is
// A2N_BUF_BITS / 8 bytes, cleared here. Returns the bit count (A2N_BITS_MAX + 2 per sync byte; stops adding zeros where the buffer would overflow).
// The inverse is a2n_bits_nibbles (the zero bits are dropped).
uint32_t a2n_nib_bits(const uint8_t *nib, uint8_t *bits);
// The bit track as the Disk II's shift register sees it (a byte starts at a one bit and ends at the eighth bit; zero bits between
// bytes are skipped), one revolution, started at bit 0 after a first lap that finds the byte boundaries. Writes at most `max`
// nibbles to `out` and returns their number (a track of 10-bit sync is shorter than the ring of a .nib).
uint32_t a2n_bits_nibbles(const uint8_t *bits, uint32_t nbits, uint8_t *out, uint32_t max);
// de-nibblize a stream of `n` nibbles that is a ring (a field may straddle its end): as a2n_denibblize_track with a ring of n.
int a2n_denibblize_ring(const uint8_t *ring, uint32_t n, A2Order order, int track, uint8_t *sectors);
// The sectors of the ring that decode (good address and data checksums, this track) are written into `sectors` (4096 bytes, file order, in/out: the
// others keep what they hold). Returns how many decoded, 0..16.
int a2n_merge_ring(const uint8_t *ring, uint32_t n, A2Order order, int track, uint8_t *sectors);

#endif
