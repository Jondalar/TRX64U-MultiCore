// T217: the C128 core's program start (128 mode), in one place: c128.cc and the real-ROM bench's writer (tools/cbm_prg_sim.cc, cores/c128/sim/run_prgrun.sh)
// read it. BASIC 7 text starts at $1C01, TXTTAB is the pointer pair $2D/$2E (VARTAB $2F, ARYTAB $31, STREND $33), the keyboard buffer KEYD is $034A (ten
// keys) with its count NDX at $D0. The RAM window reaches RAM block 0, which the 8502 reads (no second memory as on the SuperCPU).
#ifndef COREAPP_C128_PRG_H
#define COREAPP_C128_PRG_H

#include "cbm_prg.h"

static inline void c128_prg_params(CbmPrgParams *p)
{
    memset(p, 0, sizeof(*p));
    p->basic_starts[0] = 0x1C01;
    p->nbasic_starts = 1;
    p->txttab = 0x2D; p->txttab_min = 0x1C01; p->txttab_max = 0xBFFF;
    p->vartab = 0x2F; p->arytab = 0x31; p->strend = 0x33;
    p->kbd_buffer = 0x034A; p->kbd_count = 0xD0;
    p->p00_header = 0x1A;
    p->min_size = 3;
    p->max_size = 0x1A + 2 + (0x10000 - 0x0801);        // a C64 file ($0801, T217 D4) is the larger window; load + length past $FFFF is refused by the job
}

// T217 D4: the same machine in C64 mode (GO64): BASIC at $0801, TXTTAB $2B/$2C, VARTAB $2D, ARYTAB $2F, STREND $31, keyboard buffer $0277 with its count $C6 -
// the C64's own numbers (coreapp/cores/c64.cc run_params). RAM block 0 is what both modes use and what the RAM window reaches.
static inline void c128_prg_params_c64(CbmPrgParams *p)
{
    memset(p, 0, sizeof(*p));
    p->basic_starts[0] = 0x0801;
    p->nbasic_starts = 1;
    p->txttab = 0x2B; p->txttab_min = 0x0801; p->txttab_max = 0x9FFF;
    p->vartab = 0x2D; p->arytab = 0x2F; p->strend = 0x31;
    p->kbd_buffer = 0x0277; p->kbd_count = 0xC6;
    p->p00_header = 0x1A;
    p->min_size = 3;
    p->max_size = 0x1A + 2 + (0x10000 - 0x0801);
}

// T217 D4: what the application types to reach C64 mode ('|' separates the pieces: the second goes in when the machine asks) and the 40-column screen codes of that
// question (GO64 prints it with PRIMM; the bench waits for the same codes, cores/c128/sim/run_prgrun.sh go64)
#define C128_SWITCH_TEXT "GO64\r|Y\r"
static const uint8_t c128_go64_ask[13] = { 1, 18, 5, 32, 25, 15, 21, 32, 19, 21, 18, 5, 63 };      // ARE YOU SURE?

#endif
