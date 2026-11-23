// Diagnostics (diag.cc). Host-owned.
#ifndef COREAPP_DIAG_H
#define COREAPP_DIAG_H

#include "ctx.h"
#include "ops.h"

void diag_register(OpRegistry &r);
// The text or bytes the viewer shows: 0 the debug log, 1 the screen's EDID, 3 system information, 5 the keys. *n is their length.
const char *diag_text(int kind, int *n);
// T228 D12, R1: the keys text (diag_text kind 5) names the Timewarp bar and the monitor only when the core's menu has those screens; each argument is how
// the screen is opened ("F1 Home > Timewarp..., CTRL + T", MenuCtl::screen_way), null: the menu has no such screen.
void diag_help_areas(const char *timewarp, const char *monitor);


// T162: the bug report text for GET /v1/report into `body` (cap at least CORE_REPORT_MAX); its length, -1 when busy or too small. `stamp` gets the file name.
int diag_report_rest(CoreCtx *c, char *body, int cap, char *stamp, int stamplen);

#endif
