// The board's side of the library loader (T104.1 D10-D12): the machine behind LibHost, and the one call run.cc makes at the start of a core
// application. Board code; the registration of the linked libraries, the loader and the refusal texts are libscan.cc / libload.cc / libtab.cc, tested natively.
#ifndef COREAPP_LIBBOARD_H
#define COREAPP_LIBBOARD_H

#include "ctx.h"
#include "libscan.h"
#include "bundle.h"

struct LibBootReport {
    int loaded, refused;
    bool skipped;                        // no library registered on purpose (the Commodore key, a start that did not finish): libguard.h
    const char *title;                   // the heading of the message when screen is not empty
    char screen[LIB_ERR_MAX + 48];       // what to show: the first sentence of a refusal and how many more there are, or why the libraries were skipped; "" if none
};

// Makes the loader and registers the libraries linked with `register` (libscan.h lib_link_cor; base 4.0 loads no library file, T239.3 D58), once. Logs one line per
// library on the console. The core is held at this point (the start sequence has not released it).
// `bundle` (optional): what bundle_read found; its record 7 holds the menu records of those libraries.
void lib_board_load(CoreCtx *c, LibBootReport *rep, const BundleExtras *bundle = 0);

// T230 P-2: a start without a hand-over page has no medium to wait for; this waits (lib_wait_for_stick: only when a stick is known to be coming,
// at most LIB_MOUNT_WAIT_MS) so that a start file on a stick can be found. Logs the decision.
void lib_board_wait_stick(CoreCtx *c);

// The loader that holds the libraries (null before lib_board_load ran): the menu merge reads their records.
LibLoader *lib_board_loader(void);

// The core has started (boot_start returned): the boot-attempt counter is cleared, so the next start loads the libraries again.
void lib_board_started(void);

// How many contributions the menu merge refused (bundle_merge): the Home line says so, the log names each.
void lib_board_menu_note(int refused);

// The line for the System page: "none", "2 loaded", "1 loaded, 1 refused: cbm1541".
const char *lib_board_summary(void);

#endif
