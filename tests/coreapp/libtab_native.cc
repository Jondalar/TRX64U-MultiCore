// The library table of the native builds (tests/coreapp/run.sh, tools/menusim, the render bench): on the board libtab_table.cc is generated from the TOML B of the app (tools/libtab.py);
// here the export slots (libslot.h, base 4.0) name the table functions of the shared app's libraries. A library whose source a unit does not link has a null table: the generic code
// treats it as absent. Every definition here is weak: the library's own table function (mouse_exports.cc, pad_exports.cc, ...) wins where a unit links it, and a unit that needs other
// slots (a register library of its own, LibLoader::link) defines g_libslot, g_libslot_n, g_libslot_bound and the link order itself.
#include "libtab.h"

__attribute__((weak)) const void *c64cart_table(void) { return 0; }
__attribute__((weak)) const void *cbm1541_table(void) { return 0; }
__attribute__((weak)) const void *sid_table(void) { return 0; }
__attribute__((weak)) const void *tempdisk_table(void) { return 0; }
__attribute__((weak)) const void *cbmprg_table(void) { return 0; }
__attribute__((weak)) const void *mouse_table(void) { return 0; }
__attribute__((weak)) const void *pad_table(void) { return 0; }
__attribute__((weak)) const void *padkeys_table(void) { return 0; }

__attribute__((weak)) extern const LibEntry g_libtab[] = {
    { "mouse", 0, 0, 0, 0, 0, 0, 0, 0, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE },
    { "pad", 0, 0, 0, 0, 0, 0, 0, 0, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE },
    { "padkeys", 0, 0, 0, 0, 0, 0, 0, 0, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE },
};
__attribute__((weak)) extern const unsigned g_libtab_n = 3;
__attribute__((weak)) extern const unsigned g_libtab_link_order[] = { 0 };
__attribute__((weak)) extern const unsigned g_libtab_link_n = 0;

__attribute__((weak)) const void *g_libslot_bound[8];
__attribute__((weak)) extern const LibSlot g_libslot[] = {
    { "c64cart", "c64cart", 1, c64cart_table, 0 },
    { "cbm1541", "cbm1541", 1, cbm1541_table, 0 },
    { "sid", "sid", 1, sid_table, 0 },
    { "tempdisk", "tempdisk", 1, tempdisk_table, 0 },
    { "cbmprg", "cbmprg", 1, cbmprg_table, 0 },
    { "mouse", "mouse", 1, mouse_table, 0 },
    { "pad", "pad", 1, pad_table, 0 },
    { "padkeys", "padkeys", 1, padkeys_table, 0 },
};
__attribute__((weak)) extern const unsigned g_libslot_n = 8;
