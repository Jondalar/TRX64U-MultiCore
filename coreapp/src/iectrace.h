// The bus trace of a core's serial (IEC) port (T170 D2) in the bug report (T162): the core records every edge of ATN / CLK / DATA / SRQ with its time and
// the fast serial direction in a ring; this reads it out. Generic: any core that has the register page answers; the page says so itself (its first four
// bytes are "IECT"), nothing in the application names a core. The page is at 0xF000 of the core window (CoreIo::rd/wr take the window's byte offset).
#ifndef COREAPP_IECTRACE_H
#define COREAPP_IECTRACE_H

#include <stdint.h>
#include "io.h"

#define IECTRACE_PAGE 0xF000u
#define IECTRACE_SHOW 96                // the newest events the report lists

struct BrOut;

// Does the core have the page (its ID bytes read "IECT", layout version 1)? Reads five bytes, changes nothing.
bool iectrace_present(CoreIo *io);

// Writes the section: the totals and the counters of one snapshot, then the newest `max_events` events, oldest first. The core is not stopped.
void iectrace_report(CoreIo *io, BrOut *o, int max_events);

#endif
