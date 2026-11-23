// The tables of the libraries the app is linked with (T239.3 D12, D31, base 3.3), answered by api->lib->find under their export names: "c64cart" (c64cart_api.h), "cbm1541"
// (cbm1541_api.h), "uci" (uci_api.h), "sid" (sid_api.h), "tempdisk" (tempdisk_api.h) and "cbmprg" (cbmprg_api.h), each at major 1. Which of them an app has is
// its TOML B; each library's `bind` (libtab.h) gives its table with the core's context that is given there once, so no member of a table takes one.
#ifndef COREAPP_LIBEXPORTS_H
#define COREAPP_LIBEXPORTS_H

#include "libload.h"

// Calls the `bind` of every library of the library table (libtab.h), which registers the library's tables with the loader (add_export). `c` is the context every call of a table uses; it lives as long as the application. Call it once, when the loader is made.
void libexports_register(LibLoader &ld, CoreCtx *c);

// The built-in "textgrid" (textgrid.h): the built-in monitor's screen, over mon_render, mon_cols ... and the session calls mon_open and mon_tick. `c` is the context of those calls.
// Registered by the application where the core registered the monitor (monitor_grid.cc).
const struct TextGrid *monitor_textgrid(CoreCtx *c);

#endif
