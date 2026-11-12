// Memory through a lens for the firmware (REST machine:readmem lens=...), on the
// TRX64 Ultimate bitstream. The logic is trx_lens.c, the same file trxmon is built
// with (TRX64-Ultimate docs/specs/T21-monitor.md §3.4.1); this is the firmware's
// hardware under it.
//
// Only the U64-II/C64U firmware (u64ii ultimate) has trx_lens_fw.cc; every other
// build and the recovery app get the stubs at the end.
#ifndef TRX_LENS_FW_H
#define TRX_LENS_FW_H

#include <stdint.h>

#if (U64 == 2) && !defined(RECOVERYAPP)

// The bitstream has the state bus the lenses work from (TRX block, CAPS b1).
bool trx_lens_present(void);

// A debugger app (trxmon) serves: it owns the state bus window (SB_ADDR) and run
// control, so a lens that needs either must wait for it.
bool trx_lens_busy(void);

// n words of a chip from the state bus (chip 2 VIC, 3 CIA 1, 4 CIA 2, from word `word`), the machine paused and
// the gate shut: no side effect, no bus cycle. False without the state bus or while an app serves (it owns
// SB_ADDR); dst is not touched then.
bool trx_chip_words(int chip, int word, uint32_t *dst, int n);

// "cpu", "ram", "rom", "io", "cart" or "drive8"; false for any other name.
bool trx_lens_valid(const char *lens);

// Lenses that read the state bus and make the machine hold still for it:
// cpu, io and cart.
bool trx_lens_needs_pause(const char *lens);

// len bytes from addr through the lens (wrapping at $FFFF), the caller has paused the
// machine for the lenses that need it. Bytes a lens cannot give are 0; returns how
// many there were. sidefx: I/O reads are bus cycles.
int trx_lens_read(const char *lens, uint16_t addr, uint8_t *buffer, uint32_t len, bool sidefx);

#else

static inline bool trx_chip_words(int, int, uint32_t *, int) { return false; }
static inline bool trx_lens_present(void) { return false; }
static inline bool trx_lens_busy(void) { return false; }
static inline bool trx_lens_valid(const char *) { return false; }
static inline bool trx_lens_needs_pause(const char *) { return false; }
static inline int trx_lens_read(const char *, uint16_t, uint8_t *, uint32_t, bool) { return 0; }

#endif

#endif
