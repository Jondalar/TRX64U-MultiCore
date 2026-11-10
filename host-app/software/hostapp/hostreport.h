// The host app's bug report (T162): the text for F8 -> "Bug report" and for GET /v1/report. Pure: the snapshot (webui.h), the clock and the log are
// handed in, so the menu and REST make the same text and a bench runs it with no board.
#ifndef HOST_HOSTREPORT_H
#define HOST_HOSTREPORT_H

#include <stdint.h>
#include "webui.h"

#define HOST_REPORT_DIR "/SD/report"
#define HOST_REPORT_MAX 24576        // a log of 16 KB plus the sections above it

// Returns the length of the text in out (always terminated, never more than cap - 1).
int host_report_build(const WebSnapshot *s, uint32_t fattime, bool time_valid, const char *log, int log_len, char *out, int cap);

#endif
