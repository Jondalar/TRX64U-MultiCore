// The board side of the TRX64U application, as far as the core application's base (menuctl.cc, c64_cart.cc, menuflows.cc) reaches it. What the base's sources call of
// trx64u's code (u64cfg, ringops, monitor, diag, netops, trx64u_ops) and the monitor session menuctl.cc drives need the board's headers (U64 registers, USB HID,
// FileManager, the network stack, FreeRTOS), which this native build does not have: each registers nothing and answers nothing here. t64.cc is linked for real.
// The stubs of trx64u's own contributor and its export list went with its descriptor (T239.3 P1-8); the sources of that code stay until P2-6.
#include <string>
#include <vector>
#include <stdlib.h>
#include "ops.h"
#include "ctx.h"
#include "u64cfg.h"
#include "libfiles.h"
#include "ringops.h"
#include "monitor.h"
#include "diag.h"
#include "netops.h"
#include "trx64u_ops.h"
#include "uci_slot.h"
#include "sectors.h"
#include "folderhd_api.h"

// The folder disk library gives the sector engine its table by its bind entry (libtab.cc, which the native builds leave out with the loader's start): the native builds link the library's sources, so the table goes in at the start.
static struct SetFolderHd { SetFolderHd() { sec_set_folderhd(folderhd_table()); } } g_set_folderhd;

void trx64u_kernal_put(CoreCtx *, const uint8_t *, uint32_t) { }
void trx64u_load_assoc_cfg(CoreCtx *, const char *, bool) { }
void trx64u_basic_put(CoreCtx *, const uint8_t *, uint32_t) { }           // T228 R21 item 7: the SID player's BASIC copy
void trx64u_sid_service(CoreCtx *) { }
void trx64u_sid_cancel(void) { }
void u64cfg_register(OpRegistry &) { }
void u64cfg_power_on_keys(CoreCtx *) { }
__attribute__((weak)) void libfiles_register(OpRegistry &) { }     // tests/coreapp/test_menuctl.cc links the real one
void ringops_register(OpRegistry &) { }
static int stub_mon_key(CoreCtx *c, const OpArgs &a, OpOut &o) { mon_key(c, a.i(0)); o.msg[0] = 0; return OPE_OK; }
static int stub_mon_tick(CoreCtx *c, const OpArgs &, OpOut &o) { o.msg[0] = 0; o.more = mon_tick(c); return OPE_OK; }
static int stub_mon_open(CoreCtx *c, const OpArgs &, OpOut &o) { mon_open(c); o.msg[0] = 0; return OPE_OK; }
static const ArgSpec stub_mon_key_arg[] = { ARG_INT("key", 0, 0x1FFF) };
static const char *stub_mon_wants_stop(CoreCtx *) { return mon_wants_stop() ? "yes" : 0; }
void monitor_register(OpRegistry &r)
{
    r.contributor("monitor");
    r.add_op("monitor.key", stub_mon_key_arg, 1, stub_mon_key);
    r.add_op("monitor.open", 0, 0, stub_mon_open);
    r.add_op("monitor.tick", 0, 0, stub_mon_tick);
    r.add_state("monitor.wants_stop", stub_mon_wants_stop);
}
void diag_register(OpRegistry &) { }
void netops_register(OpRegistry &) { }

// the monitor session (monitor.cc) that menuctl.cc opens on the F4 tab: no debug block here
int stub_mon_open_n;                                        // monitor_grid.cc over the stubs (tests/coreapp/test_cut.cc): what the monitor was asked
void mon_open(CoreCtx *) { stub_mon_open_n++; }
int stub_mon_key_n, stub_mon_key_last;                      // the monitor keys the controller sent (tests/coreapp/test_cut.cc)
bool mon_key(CoreCtx *, int k) { stub_mon_key_n++; stub_mon_key_last = k; return true; }
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
int stub_mon_tick_n, stub_mon_render_n;
bool stub_mon_tick_answer;
bool mon_tick(CoreCtx *) { stub_mon_tick_n++; bool a = stub_mon_tick_answer; stub_mon_tick_answer = false; return a; }
void mon_run(CoreCtx *, const char *) { }
void mon_render(void) { stub_mon_render_n++; }
int mon_cols(void) { return 80; }
int mon_rows(void) { return 0; }
const char *mon_line(int) { return ""; }
int mon_attr(int) { return 0; }
int mon_blank_rows(void) { return 0; }
void mon_cursor(int *x, int *y) { *x = 0; *y = 0; }
uint32_t stub_mon_sig;
uint32_t mon_sig(void) { return stub_mon_sig; }

