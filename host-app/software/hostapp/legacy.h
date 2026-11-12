// Starting a full firmware from a .cfw (T115 D2-D4): the host checks the image, keeps the bitstream of the FPGA it runs on in ONE
// scratch file of the flash drive, stages the application in DDR2 under the right address map, and hands over with an
// ICAP IPROG whose WBSTAR is the file's flash address. Nothing returns: a power cycle loads the host again (D6).
// The parsing (cfw.h) and the cluster walk (fatwalk.h) are pure and benched; this file is the board half.
#ifndef HOST_LEGACY_H
#define HOST_LEGACY_H

#include <stdint.h>
#include "coreboot.h"          // core_progress_t, the STEP_* numbers
#include "cfw.h"

#define LEGACY_DIR   "/Flash/legacy"
#define LEGACY_FILE  "fullcore.bit"
#define LEGACY_STAMP "fullcore.stm"        // T135 D10: what the scratch file holds (a line of text), written last, after the bitstream

struct LegacyPlan {
    CfwInfo info;
    uint32_t flash_addr;       // where the scratch file starts in the SPI flash
    uint32_t stage_len;        // the staged application, rounded up to 128 bytes
};

// Reads and checks the .cfw, stages the application in DDR2, brings the scratch file in the flash drive up to date (written
// only when it differs from the bitstream) and checks it again on every start: one run of clusters, bytes equal. The
// steps, as for core_progress_t: STEP_VERIFY the image, STEP_DECOUPLE the flash write, STEP_ICAP the flash file check.
// On failure *why says what is wrong and nothing has been handed over.
// path is a .cfw or a large .cor (T115 section 7; cfw_probe reads both, the start is the same). verify: for a large .cor, the SHA-256 of the
// bitstream and of the application is computed while they are read and compared with the manifest (a start of an entry that was added
// earlier passes false: the full check ran when it was added); a .cfw has no hashes.
// direct (T135 D10, a direct start): nothing is verified (verify is ignored), and when the stamp beside the scratch file names this very bitstream and the
// file has the recorded length, the bitstream is not read, not compared and not written: the only check is that the file is complete. Without a
// matching stamp the bitstream is brought into the file as above and the stamp written after it.
bool legacy_stage(const char *path, LegacyPlan *p, const char **why, core_progress_t cb, void *ctx, bool verify = false, bool direct = false);

// The stamp text a bitstream gets (pure; tests/hostapp): "cor2|<core id>|<bitstream length>|<its SHA-256 from the manifest>" for a large .cor,
// "cfw2|<file size>|<mtime>|<bitstream offset>|<length>" for a .cfw (no hashes in a .cfw). file_mtime is the directory entry's.
int legacy_stamp_text(const CfwInfo &ci, uint32_t file_mtime, char *out, int max);

// Hands over: interrupts and network off, boot magic, trampoline, copy of the application to 0x30000, IPROG. Does not return.
void legacy_jump(const LegacyPlan *p) __attribute__((noreturn));

#endif
