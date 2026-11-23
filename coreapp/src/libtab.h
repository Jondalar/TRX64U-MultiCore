// The library table (T239.3 D31): the libraries the app is linked with, as the TOML B of its core names them. tools/libtab.py writes libtab_table.cc from the TOML B at build time;
// libtab.cc is the generic code that walks it. The generic application calls these functions and no function of a library: what a library does at the start, it does by
// the entries it names in the TOML B, and an app linked without a library has no code of it and no entry.
#ifndef COREAPP_LIBTAB_H
#define COREAPP_LIBTAB_H

#include "ctx.h"
#include "ops.h"
#include "board.h"
#include "libload.h"

struct LibEntry {
    const char *name;
    void (*bind)(CoreCtx *c, LibLoader &ld);            // gives the library its context and registers its tables (add_export); once, when the loader is made. Null: none
    void (*register_ops)(OpRegistry &r);                // the operation groups of the library, for a core described by its record (ops_glue.h ops_record_groups). Null: none
    const char *capability;                             // register_ops runs only for a core whose description names this capability (desc_has_capability). Null: every core
    void (*init)(const BoardProfile &b);                // once, when the board is known (coreapp.cc), before the core runs. Null: none
    void (*start)(CoreCtx *c);                          // once, after the network settings are pushed (netsvc.cc). Null: none
    bool (*freeze_request)(void);                       // true once when the library asks for the menu (the command interface's FREEZE); read by the service loop. Null: none
};

extern const LibEntry g_libtab[];                       // libtab_table.cc, generated
extern const unsigned g_libtab_n;

void libtab_bind(CoreCtx *c, LibLoader &ld);
void libtab_register_ops(OpRegistry &r, const CoreDesc *d);
void libtab_init(const BoardProfile &b);
void libtab_start(CoreCtx *c);
bool libtab_freeze_request(void);

#endif
