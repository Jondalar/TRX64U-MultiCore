// The model of the Amiga core for the folder disk benches (T128 section 2): the sector mailbox as a BlockSource, and NanoMig's own look at sector 0.
// The independent reader and writer of FFS volumes are in folderhd_reader.h.
#ifndef TESTS_COREAPP_FOLDERHD_BENCH_H
#define TESTS_COREAPP_FOLDERHD_BENCH_H

#include "folderhd_reader.h"
#include "amiga_model.h"
#include "sparsefs.h"
#include "engines.h"
#include "ops_glue.h"
#include "folderhd.h"

namespace bench {

static const uint32_t STAGE = AM_W_STAGE;

// one request through the mailbox of the model, answered by the sector service
struct Dev : BlockSource {
    AmigaRig &r;
    int unit;
    Dev(AmigaRig &rig, int u) : r(rig), unit(u) { }
    bool read(uint32_t lba, uint8_t *out)
    {
        r.io.post(unit, false, lba);
        if (sec_service(&r.c, 16) != 1) return false;
        memcpy(out, &r.io.ddr[STAGE], 512);
        return true;
    }
    bool write(uint32_t lba, const uint8_t *in)
    {
        memcpy(&r.io.ddr[STAGE], in, 512);
        r.io.post(unit, true, lba);
        return sec_service(&r.c, 16) == 1;
    }
};

// ---- NanoMig's own look at sector 0 (cores/amiga/rtl/src/nanomig.v:741-765, 696-697) ----
struct Nano {
    bool present = false;
    uint32_t cyl = 0, sec = 0, heads = 0;
};
static inline Nano nano_parse(const uint8_t *s0)
{
    Nano n;
    if (s0[0] != 'R' || s0[1] != 'D' || s0[2] != 'S' || s0[3] != 'K') return n;           // lines 744-747: the drive goes to NONE
    n.cyl = ((uint32_t)s0[16 * 4 + 2] << 8) | s0[16 * 4 + 3];                               // 750-751: the low 16 bits of long word 16, 17, 18
    n.sec = ((uint32_t)s0[17 * 4 + 2] << 8) | s0[17 * 4 + 3];
    n.heads = ((uint32_t)s0[18 * 4 + 2] << 8) | s0[18 * 4 + 3];
    n.present = true;
    return n;
}
static inline uint32_t nano_lba(const Nano &n, uint32_t cyl, uint32_t head, uint32_t sector1)
{
    return (cyl * n.heads + head) * n.sec + sector1 - 1;                                   // 696-697
}

}   // namespace bench

#endif
