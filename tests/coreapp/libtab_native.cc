// The library table of the native builds (tests/coreapp/run.sh, tools/menusim, the render bench): on the board libtab_table.cc is generated from the TOML B of the app (tools/libtab.py); here the
// libraries that link natively give their call tables (libtab.h `table` entry). A library whose source a unit does not link has a null table: the generic code treats it as absent.
#include "libtab.h"

// The definitions here are weak: the library's own (mouse_exports.cc, pad_exports.cc, padkeys_exports.cc) wins where a unit links it.
__attribute__((weak)) const void *mouse_table(void) { return 0; }
__attribute__((weak)) const void *pad_table(void) { return 0; }
__attribute__((weak)) const void *padkeys_table(void) { return 0; }

const LibEntry g_libtab[] = {
    { "mouse", 0, 0, 0, 0, 0, 0, mouse_table, 0 },
    { "pad", 0, 0, 0, 0, 0, 0, pad_table, 0 },
    { "padkeys", 0, 0, 0, 0, 0, 0, padkeys_table, 0 },
};
const unsigned g_libtab_n = 3;
