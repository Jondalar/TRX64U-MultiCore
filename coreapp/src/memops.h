// The memory of a core as two operations of the service layer (T104.1, Alex 2026-10-26: every core registers its own `machine.read_mem` and
// `machine.write_mem`, with its address width and its memory). A transport that wants bytes - the REST alias `machine:readmem`, the TRX64
// contract's `session/read_memory` - calls these and nothing else; what a core can reach is a MemMap in its own file, declared from the windows its
// own code already uses (the base RAM window, the DDR2 window), never from a guess.
//
//   machine.read_mem(address, length)    the bytes come back with the result (at most OP_DATA_MAX), nothing is written
//   machine.write_mem(address, data)     data = hex text, two digits a byte, at most 127 bytes
//
// A read or a write lies inside one region; it does not wrap and does not cross a region's end (OPE_ARG, the sentence names the region).
// A core whose memory the application cannot reach registers the pair too: both answer OPE_UNSUPPORTED and say why.
#ifndef COREAPP_MEMOPS_H
#define COREAPP_MEMOPS_H

#include "ctx.h"

enum MemKind {
    MK_RAM = 0,        // the base RAM window (CoreIo::ram_read / ram_write), 64 KB
    MK_RAMPAGE,        // the same window with a page register: bit n of (offset >> 16) goes to register `reg`, and the register is put back to 0
    MK_DDR             // the DDR2 window, offset `base` + offset
};

struct MemRegion {
    uint32_t first;    // the first address of the region in the core's address space
    uint32_t size;
    uint8_t kind;
    uint32_t base;     // MK_DDR: the window offset of `first`
    uint16_t reg;      // MK_RAMPAGE: the page register
    const char *name;  // "RAM", "aux RAM": for the sentences
};

struct MemMap {
    const MemRegion *regions; int nregions;     // none: the memory is not reachable
    bool ram_only;     // C64 family: the window shows I/O through the bus unless C64_DMA_MEMONLY is on; reads and writes want the RAM underneath
    const char *none;  // nregions == 0: why
};

int mem_read(const MemMap &m, CoreCtx *c, const OpArgs &a, OpOut &o);
int mem_write(const MemMap &m, CoreCtx *c, const OpArgs &a, OpOut &o);

// The pair as operations of a core's contributor; `last` is the highest address of the core's address space (its width), `MAP` a static MemMap.
template <const MemMap *M> int mem_op_read(CoreCtx *c, const OpArgs &a, OpOut &o) { return mem_read(*M, c, a, o); }
template <const MemMap *M> int mem_op_write(CoreCtx *c, const OpArgs &a, OpOut &o) { return mem_write(*M, c, a, o); }
#define MEM_OPS(R, MAP, LAST) do { \
        static const ArgSpec rd_[] = { ARG_INT("address", 0, (LAST)), ARG_INT("length", 1, OP_DATA_MAX) }; \
        static const ArgSpec wr_[] = { ARG_INT("address", 0, (LAST)), ARG_STRING("data", 254) }; \
        (R).add_op("machine.read_mem", rd_, 2, mem_op_read<&MAP>); \
        (R).add_op("machine.write_mem", wr_, 2, mem_op_write<&MAP>); \
    } while (0)

#endif
