// The core table (T93 D1). One line per core; a core appears here when its descriptor is built.
#include "desc.h"
#include "cores.h"

#ifdef CORE_ONLY
// An image of one core (the SDK's app build, app/lib/appbuild.mk, names it by [app] desc): --gc-sections drops every other core's code and data.
const CoreDesc *const core_table[] = { &CORE_ONLY, 0 };
#elif defined(CORE_NONE)
// T239.3 S17: an image of a core that is described by its bundle's record (D17): no descriptor is compiled in, start_identify makes it from the record
const CoreDesc *const core_table[] = { 0 };
#else
const CoreDesc *const core_table[] = {
    &apple2_desc,
    &zx_desc,
    &cpc_desc,
    &pet_desc,
    &c128_desc,
    &c64scpu_desc,
    &snes_desc,
    &ddt_novic_desc,
    &portdiag_desc,
    0
};
#endif
