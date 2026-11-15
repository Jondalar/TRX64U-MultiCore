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
    p->max_size = 0x1A + 2 + (0x10000 - 0x1C01);
}

#endif
