// The exports of the libraries an app is linked with, at slots fixed when the app is linked (base 4.0, T239.3 D31, P2-6). Nothing finds a table by name at run time:
// api->lib->find is gone (D57).
//
// The TOML B of a core's app names its libraries; each [[library]] lists the tables it exports:
//
//     [[library]]
//     name = "c64cart"
//     sources = [...]
//     exports = [{ name = "c64cart", major = 1, table = "c64cart_table" }]
//
// `table` names a function of the library, `const void *fn(void)`, that returns the table, or null when the running core has none (the table's first member is its
// size, uint32_t, as every exported table has it; members are only appended inside a major). An export name is 1 to 15 characters of [a-z0-9_] and stands once in a
// TOML B; the major starts at 1. This key replaces the `add_export` calls of a library's `bind` and the `table` entry of base 3.3 (libtab.h): an export is declared
// once, in the TOML B, and the build knows it. tools/libtab.py writes, from the TOML B and in the order of the file:
//   - libtab_table.cc: g_libslot[] and g_libslot_n, one LibSlot per export;
//   - libslots.h: LIBSLOT_<name> (the slot of the export), LIBSLOT_<name>_MAJOR and LIBSLOT_COUNT, for the sources the app compiles.
//
// Who reaches a slot, and how:
//   - A library linked into the app: lib_slot(LIBSLOT_<name>, &size), with libslots.h. Its [[library]] lists the library that exports the table in `needs` (the build
//     refuses a need the file does not list). A slot the TOML B does not export is no macro, so a source that names it does not compile: the absent library stops the
//     build, not the run. A library that uses an export only when the app has it writes `#ifdef LIBSLOT_<name>`; a slot whose table function answers null is null here.
//   - A file (a sub-app, or a library .u2a while the loader takes those): its needs list is its own slot list, api->lib->need(n, &size) (svctab.h SvcLib). The loader
//     binds each entry once, when it loads the file, to the slot of that name at exactly that major; an entry no slot (and no file loaded before) answers refuses the
//     load. cor_pack can make the same check when it packs a file into a .cor, against the TOML B the .cor's app is linked from.
//   - The generic application: through the same table by the export's name (libtab.h), never through a library's header.
// Rules of every exported table: `size` first; within a major, members are only appended; removing or reordering one makes the next major. A caller checks the size
// before it uses a member, and a member is there when its pointer is not null.
#ifndef COREAPP_LIBSLOT_H
#define COREAPP_LIBSLOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LibSlot {
    const char *lib;                      // the [[library]] of the TOML B that exports it (a core's `hooks` names the library whose export "corehooks" the base binds)
    const char *name;                     // the export's name
    uint32_t major;                       // the major of its layout, from 1
    const void *(*table)(void);           // the library's function that returns the table; null answer: none on this core
} LibSlot;

extern const LibSlot g_libslot[];         // libtab_table.cc, generated from the TOML B; one entry per export, in the order of the file
extern const unsigned g_libslot_n;

// The table in slot `i` and its size (its first word): null and 0 for a slot past the table and for a table function that answers null. Task: any; the table is the
// library's, and its members say their own task.
static inline const void *lib_slot(unsigned i, uint32_t *size)
{
    const void *t = (i < g_libslot_n && g_libslot[i].table) ? g_libslot[i].table() : 0;
    if (size) {
        *size = t ? *(const uint32_t *)t : 0;
    }
    return t;
}

#ifdef __cplusplus
}
#endif

#endif
