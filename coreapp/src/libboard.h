// The board's side of the library loader (T104.1 D10-D12): the machine behind LibHost, and the one call run.cc makes at the start of a core
// application. Board code; the folder scan, the order, the loader and the refusal texts are libscan.cc / libload.cc, tested natively.
#ifndef COREAPP_LIBBOARD_H
#define COREAPP_LIBBOARD_H

#include "ctx.h"
#include "libscan.h"
#include "bundle.h"

struct LibBootReport {
    int loaded, refused;
    bool skipped;                        // no library was loaded on purpose (the Commodore key, a start that did not finish): libguard.h
    const char *title;                   // the heading of the message when screen is not empty
    char screen[LIB_ERR_MAX + 48];       // what to show: the first sentence of a refusal and how many more there are, or why the libraries were skipped; "" if none
};

// Loads every library of <root>/trx/lib (libscan.h) into the registry of this core application, once. Logs one line per file on the console.
// The core is held at this point (the start sequence has not released it).
// `bundle` (optional): what bundle_read found; its libraries load before the stick's.
void lib_board_load(CoreCtx *c, LibBootReport *rep, const BundleExtras *bundle = 0);

// The loader that holds the libraries (null before lib_board_load ran, or when the loading was skipped): the menu merge reads their records.
LibLoader *lib_board_loader(void);

// The core has started (boot_start returned): the boot-attempt counter is cleared, so the next start loads the libraries again.
void lib_board_started(void);

// How many contributions the menu merge refused (bundle_merge): the Home line says so, the log names each.
void lib_board_menu_note(int refused);

// The line for the System page: "none", "2 loaded", "1 loaded, 1 refused: cbm1541".
const char *lib_board_summary(void);

#endif
