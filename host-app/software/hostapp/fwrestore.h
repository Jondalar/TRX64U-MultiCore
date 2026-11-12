// Restore of the original firmware (T137 P2/P3, D2-D4): which file may be written back as the machine's firmware, and the check of every byte of it
// BEFORE the first erase. Pure code over CorSource and the flash layer (flashfw.h); the pages are hostui.cc's. docs/specs/T137 is the description,
// the code is the check.
//   recognised  a backup .cor (origin "backup", this board's flash chip, the fixed layout cor.cc checks) or a .cfw/.ue2 whose whole-file SHA-256 is in
//               known_fw.h (fw_known_lookup); nothing else: a converted (foreign) .cor, a .cor of ours, any other .cfw, a MultiCore update package.
#ifndef HOST_FWRESTORE_H
#define HOST_FWRESTORE_H

#include <stdint.h>
#include "cor.h"
#include "cfw.h"
#include "fwknown.h"
#include "flashfw.h"

enum FwKind {
    FWK_NONE = 0,         // not a firmware at all
    FWK_FIRMWARE,         // a .cfw/.ue2: official only by the table
    FWK_BACKUP_COR,       // large .cor, origin backup (D2)
    FWK_FOREIGN_COR,      // large .cor, origin foreign (D1): never restored (Q5)
    FWK_OUR_COR           // any other large .cor
};

typedef enum {
    FW_OK = 0,
    FW_ERR_IO,
    FW_ERR_PROBE,         // cfw_probe refused it (the CfwInfo result is in FwPkg::probe)
    FW_ERR_NOT_FIRMWARE   // a layer-2 .cor, or not a package at all
} fw_result_t;

struct FwPkg {
    FwKind kind;
    cfw_result_t probe;               // what cfw_probe said
    CfwInfo ci;                       // bitstream and application places in the file
    uint32_t file_size;
    uint32_t bit_flash_len;           // bytes written at FW_BIT_ADDR
    uint32_t app_flash_len;           // bytes written at FW_APP_ADDR: record (12) + data padded to 4
    int origin;                       // a .cor: COR_ORIGIN_*
    char flash_id[40];                // a backup .cor: the chip it was read from
    char id[33];
    char name[48];
    char version[24];
};

// Reads and classifies the file (cheap: the structure and a .cor's manifest; no whole-file hash). *p is always filled as far as it got.
fw_result_t fw_probe(CorSource &src, uint32_t running_idcode, FwPkg *p);
const char *fw_kind_name(FwKind k);

// May this file be restored? `known` is what fw_known_lookup said about the whole file (NULL for none), `chip` the board's flash type string.
// A backup is for this board's chip only; a .cfw/.ue2 only when it is in the table; a foreign .cor and our own .cor never.
bool fw_can_restore(const FwPkg &p, const FwKnown *known, const char *chip);

struct FwCheck {
    bool ok;
    const char *why;                  // the failing check in words (static text)
    char bit_sha[65], app_sha[65];    // SHA-256 of the two parts as they go into the flash (the application without its record and pad)
    uint32_t bit_axss;                // the stamp found in the staged bitstream (0 = none)
    uint32_t bit_len, app_len;        // flash bytes
};

// The check before the first erase. Refuses what fw_can_restore refuses (so a caller cannot forget the gate), then reads both parts into bit_dst
// (at least FW_BIT_SLOT bytes of room) and app_dst, compares a .cor's parts with the SHA-256 in its manifest, and seals the image. For a .cfw/.ue2
// the whole-file SHA-256 (the table) is the integrity proof of every byte; the application record and the sizes are checked for both. Nothing is
// written anywhere; on failure *img stays unsealed and o->why names the first check that failed. `abort` (may be NULL) is polled per chunk.
bool fw_restore_stage(CorSource &src, const FwPkg &p, const FwKnown *known, const char *chip, uint8_t *bit_dst, uint8_t *app_dst, FwStageProgress cb, void *ctx,
                      bool (*abort)(void *), FwCheck *o, FwImage *img);

#endif
