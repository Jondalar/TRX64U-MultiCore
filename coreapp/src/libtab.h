// The library table (T239.3 D31): the libraries the app is linked with, as the TOML B of its core names them, and their exports (libslot.h). tools/libtab.py writes libtab_table.cc from the TOML B at build time;
// libtab.cc is the generic code that walks it. The generic application calls these functions and no function of a library: what a library does at the start, it does by
// the entries it names in the TOML B, and an app linked without a library has no code of it and no entry.
#ifndef COREAPP_LIBTAB_H
#define COREAPP_LIBTAB_H

#include "ctx.h"
#include "ops.h"
#include "board.h"
#include "libload.h"
#include "libslot.h"
#include <string.h>

struct CoreDesc;
struct DescRecord;

struct LibEntry {
    const char *name;
    void (*bind)(CoreCtx *c, LibLoader &ld);            // gives the library its context; once, when the loader is made. Null: none
    void (*register_ops)(OpRegistry &r);                // the operation groups of the library, for a core described by its record (ops_glue.h ops_record_groups). Null: none
    const char *const *capability;                      // register_ops runs only for a core whose description names one of these capabilities (desc_has_capability), a null-terminated list. Null: every core
    void (*init)(const BoardProfile &b);                // once, when the board is known (coreapp.cc), before the core runs. Null: none
    void (*start)(CoreCtx *c);                          // once, after the network settings are pushed (netsvc.cc). Null: none
    bool (*freeze_request)(void);                       // true once when the library asks for the menu (the command interface's FREEZE); read by the service loop. Null: none
    void (*core)(CoreCtx *c, const CoreDesc *d, const DescRecord *rec);   // the core's description is known (rec: the bundle's record, null before it is read; run.cc calls this again once it is): the library takes what it needs from it. Null: none
    // Base 4.0: a library from outside this repository (TOML B `register`): the file interface of lib_register, called at start through LibLoader::link (libtab_link), which takes
    // its record with the checks of a loaded file. Null: none
    int (*reg)(const LibApi *api, LibRecord *out);
    uint32_t heap_keep, heap_peak;                      // TOML B heap_keep / heap_peak (U2A3_BUDGET_NONE: not declared); held against a library with `reg` as a file's budget was
};

extern const LibEntry g_libtab[];                       // libtab_table.cc, generated
// Base 4.0: the libraries with `register` in the order libtab_link calls them: each after the libraries its `needs` name (a need may point to a library later in the
// TOML B; ties keep the order of the file; libtab.py refuses a cycle), as indexes into g_libtab. A library reaches through lib_slot only the exports of the libraries it
// needs, which are registered by then.
extern const unsigned g_libtab_link_order[];
extern const unsigned g_libtab_link_n;
extern const unsigned g_libtab_n;

void libtab_bind(CoreCtx *c, LibLoader &ld);
void libtab_register_ops(OpRegistry &r, const CoreDesc *d);
void libtab_init(const BoardProfile &b);
void libtab_start(CoreCtx *c);
bool libtab_freeze_request(void);
void libtab_core(CoreCtx *c, const CoreDesc *d, const DescRecord *rec);

// Base 4.0 (libslot.h): the export `name` at exactly `major` of the libraries the app is linked with, and its size (the table's first word); null and 0 when the app has no
// such export or its table function answers null on this core. What a library's `table` entry gave in base 3.3. The generic code treats null as "no such function".
static inline const void *libtab_export(const char *name, uint32_t major, uint32_t *size = 0)
{
    for (unsigned i = 0; i < g_libslot_n; i++) {
        if (g_libslot[i].major == major && !strcmp(g_libslot[i].name, name)) {
            return lib_slot(i, size);
        }
    }
    if (size) {
        *size = 0;
    }
    return 0;
}

// Base 4.0: the libraries with `register` (a library from outside this repository), in g_libtab_link_order, through LibLoader::link, each with the menu record the
// bundle carries for it (menu(name, &rec, &bytes): the caller's record for that library, or false; ownership passes to the loader). Each refusal is logged by `say` with
// the library's name, and the count of loaded and refused libraries is added to *loaded / *refused. Call it once, after libtab_bind, where the base loaded library files.
void libtab_link(LibLoader &ld, bool (*menu)(void *user, const char *name, DescRecord **rec, uint8_t **bytes), void *user, void (*say)(const char *line), int *loaded,
                 int *refused, char *first_refusal, int first_len);

#endif
