// The bug report of the generic core application (T162): one text that says which software ran, how the core was set up, what its named registers
// held (D3) and what the application had just printed. The text is made from the context (the descriptor, the settings, the registry's state
// values) and the log the caller hands in; it reads no device but the descriptor's listed registers. The privacy rules (P4) are bugreport.h's.
#ifndef COREAPP_COREREPORT_H
#define COREAPP_COREREPORT_H

#include <stdint.h>
#include "ctx.h"

#define CORE_REPORT_MAX 28672               // the text: the sections (about 9 KB with the TRX64U's 216 settings, T228 R21) and the log (D2: at most 16 KB)
#define CORE_REPORT_DIR "/sd/report"

// Builds the text into out[0..cap). Returns its length. `log` is the application's log, in order (the last lines are taken).
int core_report_build(CoreCtx *c, uint32_t fattime, bool time_valid, const char *log, int log_len, char *out, int cap);

// R8: how the report reads the screen's EDID (buf, max, &len -> false when none); the board wires video_read_edid. Null: no EDID is said.
bool (*&core_report_edid_reader())(uint8_t *, int, int *);

// P3 + D5: writes <dir>/<YYYYMMDD-HHMMSS>.txt (clock not set: nodate-<n>.txt), dir = CORE_REPORT_DIR with an SD card, else /usb<n>/report on the first USB
// medium that takes the folder (made; a name that exists gets -1, -2, ...). True with the file name (without the folder)
// in `name`, false with the reason in `why`.
// The folder the last core_report_save used.
const char *core_report_dir(void);
bool core_report_save(CoreCtx *c, const char *text, int len, uint32_t fattime, bool time_valid, char *name, int namelen, char *why, int whylen);

#endif
