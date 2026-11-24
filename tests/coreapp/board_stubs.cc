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
#include "monitor.h"
#include "diag.h"
#include "netops.h"
#include "uci_slot.h"
#include "sectors.h"
#include "folderhd_api.h"

// The folder disk library gives the sector engine its table by its bind entry (libtab.cc, which the native builds leave out with the loader's start): the native builds link the library's sources, so the table goes in at the start.
static struct SetFolderHd { SetFolderHd() { sec_set_folderhd(folderhd_table()); } } g_set_folderhd;

__attribute__((weak)) void libfiles_register(OpRegistry &) { }     // tests/coreapp/test_menuctl.cc links the real one
void diag_register(OpRegistry &) { }
void netops_register(OpRegistry &) { }

// the monitor session (monitor.cc) that menuctl.cc opens on the F4 tab: no debug block here
void mon_open(CoreCtx *) { }
bool mon_key(CoreCtx *, int) { return true; }
bool mon_wants_stop(void) { return false; }
bool mon_leave_requested(void) { return false; }
bool mon_diag_requested(void) { return false; }
void mon_release(void) { }
const char *diag_text(int, int *n)
{
    if (n) {
        *n = 0;
    }
    return "";
}
void diag_help_areas(const char *, const char *) { }
bool mon_tick(CoreCtx *) { return false; }
void mon_run(CoreCtx *, const char *) { }
void mon_render(void) { }
int mon_cols(void) { return 80; }
int mon_rows(void) { return 0; }
const char *mon_line(int) { return ""; }
int mon_attr(int) { return 0; }
int mon_blank_rows(void) { return 0; }
void mon_cursor(int *x, int *y) { *x = 0; *y = 0; }
uint32_t mon_sig(void) { return 0; }

