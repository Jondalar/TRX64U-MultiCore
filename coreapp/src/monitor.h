// The machine code monitor of F4 (T104.2 UI): trxmon's own engine and screen model (coreapp/src/trxmon: mon.c, mach.c, ring.c, trace.c, rpc.c, ui.c, ...), linked into the
// application. This file is the glue the firmware's app loader gave trxmon: the table of the debug block and the frame ring (trx table of app_api.h), the platform
// calls (time, files), and the 80 x 40 screen the menu draws without its frame (screens.cc). The verbs, their texts, the keys of the line editor and the rows are
// trxmon's; nothing of them is written here. Host-owned.
#ifndef COREAPP_MONITOR_H
#define COREAPP_MONITOR_H

#include "ctx.h"
#include "ops.h"
#include <stdint.h>

// The monitor tab is shown: the session starts (once, until mon_release()).
void mon_open(CoreCtx *c);
// A key typed while the monitor has the focus (every key is the monitor's). True when the monitor took it.
bool mon_key(CoreCtx *c, int k);
// Does RUN/STOP belong to the monitor now? While the machine runs it pauses it (trxmon's ui.c); when it stands, RUN/STOP closes the menu.
bool mon_wants_stop(void);
// `x` ran (resume and leave): read once.
bool mon_leave_requested(void);
// The line `diag` was typed: the menu goes to Diagnostics (read once; the monitor session ends).
bool mon_diag_requested(void);
// Called every pass of the main loop, menu open or not: the RPC server on TCP 4312 (monsrv.h) and what its clients ask. The session is claimed only when a client sends a command.
void mon_serve(CoreCtx *c);
// Called each pass of the menu loop: the stops that came, the run-tos that finished, the live log. True when the screen changed.
bool mon_tick(CoreCtx *c);
// The menu closes (any way): the debugger's hold is given back, breakpoints and observers are disarmed (trxmon's release()), the next open starts a new session.
void mon_release(void);
// The window: mon_render() builds the rows of the screen as ui.c has them now; the accessors read them.
void mon_render(void);
int mon_cols(void);
int mon_rows(void);
const char *mon_line(int r);
int mon_attr(int r);                    // UA_* of trxmon's ui.h
int mon_blank_rows(void);
void mon_cursor(int *x, int *y);
uint32_t mon_sig(void);                 // a hash of what mon_render() built

// A command run as if typed.
void mon_run(CoreCtx *c, const char *line);

void monitor_register(OpRegistry &r);

#endif
