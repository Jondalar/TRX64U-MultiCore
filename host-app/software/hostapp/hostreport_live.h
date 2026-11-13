// The board side of the host app's bug report (T162): the log ring (every byte the host app prints), the clock, the file on the SD card.
// hostreport.cc makes the text from facts; this file reads them where they live. Only the firmware links it.
#ifndef HOST_HOSTREPORT_LIVE_H
#define HOST_HOSTREPORT_LIVE_H

#include <stdint.h>
#include "webui.h"

// Makes the report text in the one static buffer. Null while another report (the other task's) is being made; release after use.
const char *hostreport_make(const WebSnapshot *s, int *len);
void hostreport_release(void);

// F8 -> "Bug report": makes the report and writes <dir>/<YYYYMMDD-HHMMSS>.txt (clock not set: nodate-<n>.txt), the folder is made: /SD/report with an
// SD card, else /Usb<n>/report on the first USB medium that takes it (T162 D5). True with the file name in `name` (without the folder), false with the reason in `why`.
// The folder the last hostreport_write used.
const char *hostreport_dir(void);
bool hostreport_write(const WebSnapshot *s, char *name, int namelen, char *why, int whylen);

// The clock the report is stamped with (the same one the file names use); *valid false: the clock was never set.
uint32_t hostreport_fattime(bool *valid);

// T203: writes the start log's block (startfile.h) to STARTFILE_DIR on the SD card; false when nothing could be written (no SD card, read-only).
bool hostreport_startlog_store(const char *app, const char *built);

// What the board calls itself (getProductString); hostlog.cc, which only the firmware links, as a bench brings its own.
const char *hostreport_board(void);

// The last `max` bytes of the log, in order; returns the length (hostlog.cc, the ring; a bench brings its own).
int hostlog_tail(char *out, int max);

#endif
