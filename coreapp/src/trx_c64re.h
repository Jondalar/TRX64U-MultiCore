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
    // for a load: put a piece of the cartridge image into DDR2; make the cartridge in the port the one the state names (the .crt at that path) WITHOUT a
    // reset, or an empty port for NULL (0: done); put the GMod2's 2 KB EEPROM back. All get rom_ctx.
    void (*rom_write)(void *ctx, uint32_t off, const uint8_t *src, uint32_t n);
    int (*cart_for_state)(void *ctx, const char *crt_path);
    void (*eeprom_put)(void *ctx, const uint8_t *data);
    // drive A's mount mode for a dump (0 read/write, 1 read only, 2 unlinked). For a load: the System Mode the file is of, put in for this run only (0: done), and
    // the image drive A had, mounted without a reset ("ro <path>" / "unlinked <path>" / "<path>", 0: done). Both get rom_ctx.
    int disk_mode;
    int (*machine_set)(void *ctx, int machine);
    int (*drive_mount)(void *ctx, const char *spec);
};
void trx_c64re_set_source(const TrxC64reSrc *src);

// Writes timeline entry idx as a .c64re. NULL, or what went wrong.
const char *trx_c64re_dump(const char *path, int idx, uint32_t *bytes);
// The same, calling progress(done, total, ctx) once before the first section (done 0) and after every piece written.
typedef void (*trx_progress_fn)(int done, int total, void *ctx);
const char *trx_c64re_dump_ex(const char *path, int idx, uint32_t *bytes, trx_progress_fn progress, void *ctx);

// The machine as it stands, with no frame ring: RAM, VIC registers, colour RAM and the media in the machine, format 1 (FW apps/c64re "Save .C64RE Dump").
const char *trx_c64re_dump_machine(const char *path, uint32_t *bytes);

// Writes the newest "seconds" of the ring, one entry in every "stride"
// frames (oldest first, the newest entry is the cursor), as a .c64rering.
const char *trx_c64rering_dump(const char *path, int seconds, int stride, uint32_t *bytes, int *entries);

// The same, calling progress(done, total, ctx) once the memories are read
// (done 0) and after every entry written.
const char *trx_c64rering_dump_ex(const char *path, int seconds, int stride, uint32_t *bytes, int *entries,
                                  trx_progress_fn progress, void *ctx);

// Loads a .c64re into the machine: format 2 (the chip block, RAM, colour RAM and drive A's RAM, CPU and VIAs) or format 1 written by this board (its
// trx64Hw node). A file of another writer or compressed (TRX64's own) is refused. What the file holds and this load does not put back (the cartridge's
// RAM, image and EEPROM) is named by trx_c64re_missed() until the next load; it is empty when nothing was left out.
const char *trx_c64re_load(const char *path);
const char *trx_c64re_missed(void);

// The .trxring (FW io/c64/trx_trxring.h): the whole frame ring in a file. is_one: the file starts with the magic; check: the header only, NULL or what is
// wrong; load: the ring and the machine back (trx_c64re_load sends such a file here, so one path loads whichever kind it is).
bool trx_trxring_is_one(const char *path);
const char *trx_trxring_check(const char *path);
const char *trx_trxring_load(const char *path);

#else

struct TrxC64reSrc;
typedef void (*trx_progress_fn)(int done, int total, void *ctx);
static inline void trx_c64re_set_source(const TrxC64reSrc *) { }
static inline const char *trx_c64re_dump(const char *, int, uint32_t *) { return "No frame ring"; }
static inline const char *trx_c64rering_dump(const char *, int, int, uint32_t *, int *) { return "No frame ring"; }
static inline const char *trx_c64re_load(const char *) { return "No frame ring"; }
static inline const char *trx_c64re_missed(void) { return ""; }
static inline const char *trx_c64re_dump_machine(const char *, uint32_t *) { return "No frame ring"; }
static inline const char *trx_c64re_dump_ex(const char *, int, uint32_t *, trx_progress_fn, void *) { return "No frame ring"; }
static inline const char *trx_c64rering_dump_ex(const char *, int, int, uint32_t *, int *, trx_progress_fn, void *)
{ return "No frame ring"; }

#endif

#endif
