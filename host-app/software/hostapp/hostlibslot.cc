// The library slots of the host application (libslot.h, base 4.0, T239.3 D58): the loader of coreapp/src that runs unzip.u2a in the File Browser (host_core_flow_zip.cc) binds an
// application's needs to these slots, and the base loads no library file. The host app has no TOML B; it is linked with one library, miniz (apps/miniz, the same sources the shared
// app's TOML B lists), so unzip's need `miniz>=1.0` is met here as in a core's app.
#include "libslot.h"

const void *miniz_table(void);

const void *g_libslot_bound[1];
const LibSlot g_libslot[] = {
    { "miniz", "miniz", 1, miniz_table, 0 },
};
const unsigned g_libslot_n = 1;
