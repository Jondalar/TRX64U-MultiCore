// T217: what the SuperCPU core's program start is made of, in one place: the core's descriptor (c64scpu.cc) and the real-ROM bench's writer
// (tools/cbm_prg_sim.cc, cores/c64scpu/sim/run_prgrun.sh) both read it, so the bench places a program with the numbers the board uses.
// The machine is the C64's (the SuperCPU changes the CPU, not the KERNAL): BASIC at $0801, TXTTAB $2B. The 65816's bank 0 is its own SRAM in the DDR2
// (SCPU_SRAM_WIN_OFF), not the C64 RAM the RAM window reaches, so the program and the pointers go there (mem_ddr).
#ifndef COREAPP_C64SCPU_PRG_H
#define COREAPP_C64SCPU_PRG_H

#include "cbm_prg.h"
#include "c64scpu_map.h"

#define SCPU_KEY_HOLD_MS 90          // two scans at 50 Hz are 40 ms; 90 ms is four, and a held key repeats only after ~400 ms
#define SCPU_KEY_GAP_MS  90

static inline void scpu_prg_params(CbmPrgParams *p)
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
    p->mem_ddr = SCPU_SRAM_WIN_OFF;
}

#endif
