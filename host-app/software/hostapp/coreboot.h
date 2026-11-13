// Starting a core from a .cor bundle (T84 D6): verify and stage everything first, then cut the old core
// off, stream the partial bitstream through the ICAP (layer 1, T79.1 section 7), and softboot the bundle's
// application: copy it to its load address and jump. The application releases the decouple and resets
// its core. Same sequence as apps/coreload (t79-coreload) followed by apps/softboot's copy_and_jump.
#ifndef HOST_COREBOOT_H
#define HOST_COREBOOT_H

#include <stdint.h>
#include "cor.h"
#include "bootcfg.h"

#define STAGE_APP_ADDR   0x03000000u   // the application image waits here (4 MiB)
#define STAGE_APP_MAX    0x00400000u
#define STAGE_PART_ADDR  0x03400000u   // the partial bitstream waits here (up to 0x03C00000)
#define STAGE_PART_MAX   0x00800000u
#define TRAMPOLINE_ADDR  0x03F00000u   // the copy routine runs from here, outside every target
#define APP_LOAD_MIN     0x00030000u   // an application may land from here ...
#define APP_LOAD_LIMIT   0x00E80000u   // ... up to here (below the drive/cart areas of linker.x)

// *why is this when the user aborted; the core is then untouched, or (after the ICAP) loaded but not started.
#define CORE_ABORTED "aborted"
#define CORE_LOADED_NOT_STARTED "aborted: the core is loaded but not started (it stays cut off; power cycle or pick again)"

enum { STEP_VERIFY = 0, STEP_DECOUPLE, STEP_ICAP, STEP_HANDOVER, STEP_JUMP, STEPS };

// Progress for the screen: step, percent of that step, message (NULL = none).
// A non-zero return asks to abort (RUN/STOP, Esc); it is honoured while that is still safe.
typedef int (*core_progress_t)(int step, int percent, const char *msg, void *ctx);

// Reads and checks the bundle (header, contract, SHA-256 of every part) into the staging areas.
// Touches no hardware. On failure *why says what is wrong.
// trusted (the boot.cfg entry, taken at the last full check): when the file still has that size, mtime, header digest and partial hash
// (startcheck_trusts), the parts are only read, not hashed again. NULL = hash everything.
// skip_checks (T135 D10, a direct start): no SHA-256 of any part, whatever `trusted` says; the header and the contract / host key check of
// cor_probe stay (a bundle for another host would hang in the ICAP), and the parts are read into the staging areas as always.
bool core_stage(const char *path, CorInfo *ci, const char **why, core_progress_t cb, void *ctx,
                const BootEntry *trusted = 0, bool skip_checks = false);

// Milliseconds (ITU ms timer, unwrapped) for the timing logs.
uint32_t core_cycles(void);
#define CORE_CYC_PER_MS 1u        // core_cycles() counts milliseconds

// True when the layer-1 ICAP loader answers its ID byte (0x7A).
bool core_icap_present(void);

// Decouples the core and streams the staged partial through the ICAP; checks the word count. On
// failure the core stays decoupled (a half-loaded core must not touch the host) and *why says what.
bool core_swap(const CorInfo *ci, const char **why, core_progress_t cb, void *ctx);

// Hands the machine over and starts the staged application. Does not return.
void core_jump(const CorInfo *ci) __attribute__((noreturn));

// T198 D7: the config the next core_jump hands over ("" or null = a plain start). One-shot: set before the start, the jump reads it.
void core_set_start_config(const char *name);

#endif
