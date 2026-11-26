// The board side that the core application's base (menuctl.cc, menuflows.cc, filemgr.cc, ops_glue.cc, the folder disk library) reaches and this native build has not: the monitor
// session menuctl.cc drives, the diagnostics text, the network and diagnostics registrars. Each registers nothing and answers nothing here (they need the board's headers: U64
// registers, USB HID, FileManager, the network stack, FreeRTOS). t64.cc is linked for real. The stubs of trx64u's own code went with its descriptor (T239.3 P1-8); the sources
// of that code stay in the application until P2-6.
#include <string>
#include <vector>
#include <stdlib.h>
#include "ops.h"
#include "ctx.h"
#include "libfiles.h"
#include "diag.h"
#include "netops.h"
#include "sectors.h"
#include "folderhd_api.h"

// The folder disk library gives the sector engine its table by its bind entry (libtab.cc, which the native builds leave out with the loader's start): the native builds link the library's sources, so the table goes in at the start.
static struct SetFolderHd { SetFolderHd() { sec_set_folderhd(folderhd_table()); } } g_set_folderhd;

__attribute__((weak)) void libfiles_register(OpRegistry &) { }     // tests/coreapp/test_menuctl.cc links the real one
void diag_register(OpRegistry &) { }
void netops_register(OpRegistry &) { }

const char *diag_text(int, int *n)
{
    if (n) {
        *n = 0;
    }
    return "";
}
void diag_help_areas(const char *, const char *) { }

