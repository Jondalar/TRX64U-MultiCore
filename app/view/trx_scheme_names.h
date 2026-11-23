// The names of the colour schemes of the panel skin, in the order of k_schemes (trxview.cc), as the setting "Color Scheme" of the application stores them (coreapp/src/baseset.cc):
// Gideon's "Color Scheme" item. No LVGL here: the core application's native tests read it too.
#ifndef TRX_SCHEME_NAMES_H
#define TRX_SCHEME_NAMES_H

#define TRX_SCHEME_COUNT 7
static const char *const trx_scheme_names[TRX_SCHEME_COUNT] = { "Standard Blue", "Ultimate Black", "C128 Style", "Commodore Blue", "Commodore 1", "Commodore 2", "Commodore 3" };

#endif
