// A part read from the medium and checked before anything is built on it (SD reads are occasionally wrong: board 2026-10-28, one flipped
// bit in a bitstream copied to the scratch file). Pure: no hardware, benched natively (tests/hostapp/test_srcverify.cc).
#ifndef HOST_SRCVERIFY_H
#define HOST_SRCVERIFY_H

#include "cor.h"

enum sv_result_t { SV_OK, SV_READ, SV_STOPPED, SV_MISMATCH };

// Called with every chunk as it was read (buf is writable: the caller may pad it); non-zero stops the read.
// On a repeated read it is called again from done == 0.
typedef int (*sv_sink_t)(void *ctx, uint32_t done, uint8_t *buf, uint32_t n);

struct SvJob {
    uint32_t off, len;          // the part inside the source
    uint8_t *dst;               // the part is read here, contiguous (NULL: chunk by chunk through scratch, the sink takes it)
    uint8_t *scratch;           // chunk buffer, scratch_len bytes
    uint32_t scratch_len;
    const char *expect_sha;     // 64 lower-case hex digits: the part is hashed and must match. NULL: see twice
    bool twice;                 // with expect_sha NULL: the part is read a second time and both reads must be the same (no reference hash exists)
    sv_sink_t sink;             // may be NULL
    void *ctx;
    int tries;                  // reads of the whole part before SV_MISMATCH (3)
    int first_try;              // the number the first read here carries in the log (2 when the caller already read and failed once)
    const char *label;          // "bitstream" / "application", for the console
    const char *path;
};

sv_result_t sv_read(CorSource &src, const SvJob &j);

#endif
