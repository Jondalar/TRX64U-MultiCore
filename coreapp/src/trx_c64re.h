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

// What the single-frame .c64re says about the machine beyond the chip block (the caller knows it, this file does not): which of the six System Modes runs
// (0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50, 4 PAL-60/L, 5 NTSC-50/L: the manifest's model), the cartridge as it stands now (a .crt file the caller wrote, deleted
// after the dump; src_name the file it came from; eeprom the 2 KB of a GMod2) and the disk image on drive A. The pointers stay the caller's and must live
// until the dump returns. Without a call the model is "c64-pal" and the file names no medium.
struct TrxC64reSrc {
    int machine;
    const char *cart_file;
    const char *cart_name;
    const char *cart_src;
    int cart_type;
    const uint8_t *eeprom;
    const char *disk_path;
    bool drive_power;
    int drive_unit;
    const char *drive_type;
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

// Loads a .c64re written by trx_c64re_dump() into the machine (exact: its
// trx64Hw node). A file without it, or compressed (TRX64's own), is refused.
const char *trx_c64re_load(const char *path);

#else

struct TrxC64reSrc;
static inline void trx_c64re_set_source(const TrxC64reSrc *) { }
static inline const char *trx_c64re_dump(const char *, int, uint32_t *) { return "No frame ring"; }
static inline const char *trx_c64rering_dump(const char *, int, int, uint32_t *, int *) { return "No frame ring"; }
static inline const char *trx_c64re_load(const char *) { return "No frame ring"; }
typedef void (*trx_progress_fn)(int done, int total, void *ctx);
static inline const char *trx_c64rering_dump_ex(const char *, int, int, uint32_t *, int *, trx_progress_fn, void *)
{ return "No frame ring"; }

#endif

#endif
