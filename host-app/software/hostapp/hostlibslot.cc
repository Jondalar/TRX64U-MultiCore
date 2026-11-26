// The library slots of the host application (libslot.h, base 4.0, T239.3 D58): the loader of coreapp/src that runs unzip.u2a in the File Browser (host_core_flow_zip.cc) binds an
// application's needs to these slots, and the base loads no library file. The host app links no library (T239.3 D60: Unzip carries its own miniz), so the table is empty; the one-element
// arrays only keep the declaration legal.
// This hand-made table stays until T239.6 builds the host app from the SDK; then the generator writes it from the host app's TOML B.
#include "libslot.h"

const void *g_libslot_bound[1];
const LibSlot g_libslot[1] = {};
const unsigned g_libslot_n = 0;
