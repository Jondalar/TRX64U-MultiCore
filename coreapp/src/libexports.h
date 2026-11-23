// The tables of the shared libraries that stay in the base (T239.3 D12, base 3.3), answered by api->lib->find under their export names: "c64cart" (c64cart_api.h), "cbm1541"
// (cbm1541_api.h), "uci" (uci_api.h), "sid" (sid_api.h) and "tempdisk" (tempdisk_api.h), each at major 1. The bodies call the code of the application (c64_cart.cc, cbm1541.cc, uci.cc,
// sidfile.cc, tempdisk.cc) with the core's context that is given here once, so no member of a table takes one.
#ifndef COREAPP_LIBEXPORTS_H
#define COREAPP_LIBEXPORTS_H

#include "libload.h"

// Registers the five tables with the loader (add_export). `c` is the context every call of a table uses; it lives as long as the application. Call it once, when the loader is made.
void libexports_register(LibLoader &ld, CoreCtx *c);

// The built-in "textgrid" (textgrid.h): the built-in monitor's screen, over mon_render, mon_cols ... and the session calls mon_open and mon_tick. `c` is the context of those calls.
// Registered by the application where the core registered the monitor (monitor_grid.cc).
const struct TextGrid *monitor_textgrid(CoreCtx *c);

#endif
