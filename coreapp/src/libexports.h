// The libraries the app is linked with (T239.3 D12, D31): their exports are declared in the TOML B (`exports`) and reached through libslot.h (base 4.0); "c64cart"
// (c64cart_api.h), "cbm1541" (cbm1541_api.h), "sid" (sid_api.h), "tempdisk" (tempdisk_api.h) and "cbmprg" (cbmprg_api.h), each at major 1, in the shared app. Which of them an
// app has is its TOML B; a library's `bind` (libtab.h) gives it the core's context once, so no member of a table takes one.
#ifndef COREAPP_LIBEXPORTS_H
#define COREAPP_LIBEXPORTS_H

#include "libload.h"

// Calls the `bind` of every library of the library table (libtab.h), which gives the library its context. `c` is the context every call of a table uses; it lives as long as the
// application. Call it once, when the loader is made.
void libexports_register(LibLoader &ld, CoreCtx *c);

#endif
