// The logo of a .cfw row (T115 D7): 32 x 32 RGB565, 2048 bytes. Built in by the design name in the .bit header, or a
// sidecar file beside the .cfw. Pure code: the sidecar comes through a CorSource.
#ifndef HOST_CFWICON_H
#define HOST_CFWICON_H

#include <stdint.h>
#include "cor.h"

#define CFW_ICON_BYTES 2048

enum { CFW_ICON_KIND_GIDEON = 0, CFW_ICON_KIND_TRX64 = 1, CFW_ICON_KIND_GENERIC = 2 };

// Which built-in logo a firmware gets, by the name cfw_probe made ("<design> <date> <time>"; the date and the time change
// with every build, so only the design, the word before the first space, counts).
int cfw_icon_kind(const char *name);

// The built-in logo of a kind.
const uint8_t *cfw_icon_builtin(int kind);

// The sidecar's path: the .cfw path with its extension (any case) replaced by .icon. False when it is no .cfw path or does not fit.
bool cfw_icon_sidecar_path(const char *cfw_path, char *out, int n);

// Fills dst (CFW_ICON_BYTES) with the logo of the firmware: the sidecar when there is one (side != 0) and it is exactly
// CFW_ICON_BYTES long and reads, else the built-in one by name. Returns true when it was the sidecar.
bool cfw_icon_load(const char *name, CorSource *side, void *dst);

#endif
