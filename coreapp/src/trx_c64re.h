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

static inline const char *trx_c64re_dump(const char *, int, uint32_t *) { return "No frame ring"; }
static inline const char *trx_c64rering_dump(const char *, int, int, uint32_t *, int *) { return "No frame ring"; }
static inline const char *trx_c64re_load(const char *) { return "No frame ring"; }
typedef void (*trx_progress_fn)(int done, int total, void *ctx);
static inline const char *trx_c64rering_dump_ex(const char *, int, int, uint32_t *, int *, trx_progress_fn, void *)
{ return "No frame ring"; }

#endif

#endif
