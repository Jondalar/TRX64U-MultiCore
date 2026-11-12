// Diagnostics (diag.cc). Host-owned.
#ifndef COREAPP_DIAG_H
#define COREAPP_DIAG_H

#include "ctx.h"
#include "ops.h"

void diag_register(OpRegistry &r);
// The text or bytes the viewer shows: 0 the debug log, 1 the screen's EDID, 2 the instruction trace, 3 system information. *n is their length.
const char *diag_text(int kind, int *n);


// T162: the bug report text for GET /v1/report into `body` (cap at least CORE_REPORT_MAX); its length, -1 when busy or too small. `stamp` gets the file name.
int diag_report_rest(CoreCtx *c, char *body, int cap, char *stamp, int stamplen);

#endif
