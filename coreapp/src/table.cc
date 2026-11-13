// The core table (T93 D1). One line per core; a core appears here when its descriptor is built.
#include "desc.h"
#include "cores.h"

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
    &c64_desc,
    &amiga_desc,
    &amiga1200_desc,
    &ddt_mandel_desc,
    &ddt_novic_desc,
    &portdiag_desc,
    0
};
