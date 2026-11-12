// A .cfw/.ue2 turned into a large .cor of origin "foreign" (T137 D9, S1). Pure code over CorSource (the firmware file) and FoFs (where the .cor
// goes: "<medium>/cores/<name>.cor" of the medium the firmware lies on, T148 D1), benched against tools/cor_pack.py pack_large byte for byte.
// The .cor is written to "<name>.cor.part", read back and compared by SHA-256, and only then gets its name; an existing file of that name is
// replaced only after that, and only when the caller said so. Nothing here writes the flash.
#ifndef HOST_FWCONVERT_H
#define HOST_FWCONVERT_H

#include <stdint.h>
#include "cor.h"
#include "cfw.h"
#include "fileops.h"
#include "fwknown.h"

enum FwConvResult {
    FWCV_OK = 0,
    FWCV_EXISTS,        // the target holds a file of that name: ask, then repeat with replace
    FWCV_ERR_NOROOM,    // plan.free and plan.size say how much
    FWCV_ERR_READ,      // the firmware file could not be read
    FWCV_ERR_WRITE,
    FWCV_ERR_VERIFY,    // what was written does not read back as written
    FWCV_ERR_FS,
    FWCV_CANCELLED
};

const char *fwcv_text(FwConvResult r);

// phase: 0 reads and hashes the firmware, 1 writes, 2 reads back. Non-zero cancels (nothing is left behind but a removed .part).
typedef int (*FwConvProgress)(void *ctx, int phase, uint64_t done, uint64_t total);

struct FwConvPlan {
    char dst[FO_PATH_MAX];
    uint64_t size;      // the .cor's length
    uint64_t free;      // free on the target volume
    bool exists;
    bool dir_missing;   // "<medium>/cores" is not there yet: fw_convert makes it
    char name[48];      // what goes into the manifest
    char version[24];
    char id[24];
};

enum FwCvTarget {
    FWCT_OK = 0,
    FWCT_NAME,          // not a .cfw/.ue2 path, or a file that is nothing but the extension, or no medium in front of it
    FWCT_FLASH,         // the firmware lies on the internal drive: a 3 MiB file does not go there
    FWCT_LONG           // the path does not fit
};

// The .cor's path for a firmware path: "/<medium>/cores/<name>.cor", <name> being the firmware's file name without .cfw/.ue2 (any case).
FwCvTarget fwcv_target(const char *fw_path, char *out, int n);

// What the .cor will say about itself: the table's name and version when `known`, else the .bit header's design and date ("bit 2026/09/10"; Q7).
void fwcv_names(const CfwInfo &ci, const FwKnown *known, FwConvPlan *p);

// Checks everything before a byte is written: the length, the room, an existing file. FWCV_EXISTS (plan filled) when the target exists and there is room.
FwConvResult fwcv_plan(FoFs &fs, const CfwInfo &ci, const FwKnown *known, bool has_icon, const char *dst, FwConvPlan *plan);

// Writes it. icon: 2048 bytes or NULL. buf: a block buffer (at least 4 KiB).
FwConvResult fw_convert(FoFs &fs, CorSource &src, const CfwInfo &ci, const FwConvPlan &plan, const uint8_t *icon, bool replace, uint8_t *buf, uint32_t buflen,
                        FwConvProgress cb, void *ctx);

#endif
