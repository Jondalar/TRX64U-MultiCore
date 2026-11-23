// The table of the temp RAM disk library (tempdisk.cc): its table "tempdisk" (T239.3 D12, base 3.3), answered by api->lib->find under its export name. This file is part of the library: libtab.cc (from the TOML B of the app) calls tempdisk_bind once, when the loader is made.
// The bodies call the code of the library with the core's context that is given here once, so no member of a table takes one.
#include "libexports.h"
#include "libexp_util.h"
#include "tempdisk_api.h"
#include "tempdisk.h"
#include "board.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---- tempdisk (tempdisk_api.h) -------------------------------------------------------------------------------------------------------------------------

static void temp_set(bool auto_cleanup, bool subfolders) { tempdisk_set(auto_cleanup, subfolders); }

static const TempDiskApi g_temp = { sizeof(TempDiskApi), temp_set };

void tempdisk_bind(CoreCtx *c, LibLoader &ld)
{
    (void)c;
    ld.add_export("tempdisk", 1, &g_temp);
}

// The temp RAM disk exists on the monolith only (T228 R21: as in TRX64-Ultimate's firmware, FW ramdisk.cc:16-44), not on the MultiCore host; the library names this in its entry `init`, so the application does not.
void tempdisk_lib_init(const BoardProfile &b)
{
    if (b.kind == BOARD_MONOLITH) {
        tempdisk_init();
    }
}
