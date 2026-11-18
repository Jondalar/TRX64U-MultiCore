// PROVENANCE: copied unchanged from TRX64-Ultimate software/ultimate-app/software/io/c64/trx_c64re.h, T104.2 UI.
// .c64re and .c64rering files from the TRX64 Ultimate frame ring
// (TRX64-Ultimate docs/specs/T20-delta-ring.md §9, §10; the container and node
// shapes are TRX64's: crates/trx64-core/src/native_snapshot.rs,
// c64re_snapshot.rs, ring_dump.rs). Written with stored deflate blocks (valid
// gzip, no compressor on the RISC-V). Every file carries our own exact state in
// a "trx64Hw" node, which TRX64 ignores and trx_c64re_load() uses.
#ifndef TRX_C64RE_H
#define TRX_C64RE_H

#include <stdint.h>

#if (U64 == 2) && !defined(RECOVERYAPP)

// What the single-frame .c64re (format 2, as TRX64-Ultimate's trx_c64re.cc writes it) says of the machine beyond the chip block (the caller knows it, this
// file does not): which of the six System Modes runs (0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50, 4 PAL-60/L, 5 NTSC-50/L), the cartridge as it stands now (its
// image is read through rom_read, `rom_bytes` long, never copied whole; src_name the file it came from; eeprom the 2 KB of a GMod2), the disk image on
// drive A and whether drive A is powered (its CPU and VIA state are read through the drive debug page when it is), the REU setting. The pointers stay the
// caller's and must live until the dump or load returns. Without a call the model is "c64-pal" and the file names no medium.
struct TrxC64reSrc {
    int machine;
    uint32_t rom_bytes;
    void (*rom_read)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);
    void *rom_ctx;
    const char *cart_name;
    const char *cart_src;
    int cart_type;
    const uint8_t *eeprom;
    const char *disk_path;
    bool drive_power;
    int drive_unit;
    const char *drive_type;
    int reu;
    int reu_size;
};
void trx_c64re_set_source(const TrxC64reSrc *src);

// Writes timeline entry idx as a .c64re. NULL, or what went wrong.
const char *trx_c64re_dump(const char *path, int idx, uint32_t *bytes);

// Writes the newest "seconds" of the ring, one entry in every "stride"
// frames (oldest first, the newest entry is the cursor), as a .c64rering.
const char *trx_c64rering_dump(const char *path, int seconds, int stride, uint32_t *bytes, int *entries);

// The same, calling progress(done, total, ctx) once the memories are read
// (done 0) and after every entry written.
typedef void (*trx_progress_fn)(int done, int total, void *ctx);
const char *trx_c64rering_dump_ex(const char *path, int seconds, int stride, uint32_t *bytes, int *entries,
                                  trx_progress_fn progress, void *ctx);

// Loads a .c64re into the machine: format 2 (the chip block, RAM, colour RAM and drive A's RAM, CPU and VIAs) or format 1 written by this board (its
// trx64Hw node). A file of another writer or compressed (TRX64's own) is refused. What the file holds and this load does not put back (the cartridge's
// RAM, image and EEPROM) is named by trx_c64re_missed() until the next load; it is empty when nothing was left out.
const char *trx_c64re_load(const char *path);
const char *trx_c64re_missed(void);

#else

struct TrxC64reSrc;
static inline void trx_c64re_set_source(const TrxC64reSrc *) { }
static inline const char *trx_c64re_dump(const char *, int, uint32_t *) { return "No frame ring"; }
static inline const char *trx_c64rering_dump(const char *, int, int, uint32_t *, int *) { return "No frame ring"; }
static inline const char *trx_c64re_load(const char *) { return "No frame ring"; }
static inline const char *trx_c64re_missed(void) { return ""; }
typedef void (*trx_progress_fn)(int done, int total, void *ctx);
static inline const char *trx_c64rering_dump_ex(const char *, int, int, uint32_t *, int *, trx_progress_fn, void *)
{ return "No frame ring"; }

#endif

#endif
