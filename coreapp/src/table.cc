// The core table (T93 D1). One line per core; a core appears here when its descriptor is built.
#include "desc.h"
#include "cores.h"

#ifdef CORE_ONLY
// T228 R25 (Alex 2026-11-18): an image of one core. The build links one for each core that has an image of its own (the core app Makefile's
// target `own`, ci/trxapps.sh); --gc-sections then drops every other core's code and data, and that image may have its own heap.
const CoreDesc *const core_table[] = { &CORE_ONLY, 0 };
#else
const CoreDesc *const core_table[] = {
    &atari800_desc,
    &nes_desc,
    &apple2_desc,
    &zx_desc,
    &cpc_desc,
    &vc20_desc,
    &c264_desc,
    &pet_desc,
    &c128_desc,
    &c64scpu_desc,
    &zxnext_desc,
    &snes_desc,
    &neogeo_desc,
#ifndef CORE_OWN_IMAGE_C64
    &c64_desc,                    // T228 R25: the TRX64U C64 has an image of its own (coreapp_trx64u); the shared RISC-V image leaves it out
#endif
    &amiga_desc,
    &amiga1200_desc,
    &ddt_mandel_desc,
    &ddt_novic_desc,
    &portdiag_desc,
    0
};
#endif
