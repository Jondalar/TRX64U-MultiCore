// The host app's bug report (T162): the text for F8 -> "Bug report" and for GET /v1/report. Pure: the snapshot (webui.h), the clock and the log are
// handed in, so the menu and REST make the same text and a bench runs it with no board.
#ifndef HOST_HOSTREPORT_H
#define HOST_HOSTREPORT_H

#include <stdint.h>
#include "webui.h"

#define HOST_REPORT_DIR "/SD/report"
#define HOST_REPORT_MAX 32768        // a log of 16 KB, the start log (up to HOST_STARTLOG_MAX) and the sections above them
#define HOST_STARTLOG_MAX 8192
#define HOST_REPORT_OUT_MAX (HOST_REPORT_MAX + 1024)   // the web server's one output buffer: the text and the HTTP headers (under 400 bytes) in one piece

// Returns the length of the text in out (always terminated, never more than cap - 1).
int host_report_build(const WebSnapshot *s, uint32_t fattime, bool time_valid, const char *log, int log_len, char *out, int cap,
                      const char *startlog = 0);   // T203: startlog.txt as read from the SD card (null/empty: the section says so)

#endif
