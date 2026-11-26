// T265 P2/P3: the host's own application is guarded (docs/specs/T265-host-update-app-guard.md, docs/specs/T265/p1-p3-guard-and-recovery.md).
// The boot ROM starts the application at 0x220000 only when it matches the record at 0x21F000 (flashfw.h), and otherwise the recovery program of the slot
// below it. Both leave a block in DDR2 that the host application reads once, first thing (guard_capture). This file is the application's side, pure code over
// the flash layer and the FileManager's file layer (FoFs), so tests/hostapp/test_guard.cc runs every rule against a flash model that can lose its power:
//   - the blocks the boot ROM and the recovery program leave (guard_decode),
//   - the recovery copy /Flash/trx/hostrecovery.app, its stamp, and the order application -> copy -> record (guard_finish),
//   - the provisioning of a host that was started from the flash (guard_provision) and the check before a repair (guard_repair_check).
#ifndef HOST_GUARD_H
#define HOST_GUARD_H

#include <stdint.h>
#include "cor.h"
#include "fileops.h"
#include "flashfw.h"

class Flash;

// ---- the blocks in DDR2 (0xFF60-0xFFFF, 40 words): [0..7] the recovery program's, [8..15] the boot ROM's guard block, [24..39] the result block of T15
#define GUARD_BLK_ADDR       0x0000FF60u
#define GUARD_BLK_WORDS      40
#define GUARD_W_RECOVERY     0
#define GUARD_W_GUARD        8
#define GUARD_W_RESULT       24
#define GUARD_BLOCK_MAGIC    0x31445247u       // "GRD1": the guard block the boot ROM writes at 0xFF80
#define GUARD_RECOVERY_MAGIC 0x31564352u       // "RCV1": the recovery block the recovery program writes at 0xFF60
#define GUARD_RES_VALID      0x54313552u       // the result block's magic (bootrom.c RES_VALID)
#define GUARD_ST_BOOTING     1u                // result status: booted from the flash or a request
#define GUARD_ST_GUARD       9u                // result status: the record did not match, the recovery program was started
#define GUARD_SRC_FLASH      1u                // result source: the flash

// the guard block (boot ROM, 0xFF80)
enum {
    GV_NO_RECORD = 0, GV_MATCH = 1, GV_APP_EMPTY = 2, GV_HEADER_MISMATCH = 3, GV_CRC_MISMATCH = 4
};
// the recovery block's medium
enum {
    GM_FLASH_COPY = 1, GM_FLASH_NEW = 2, GM_SD = 3, GM_USB = 4
};

enum { GS_OTHER = 0, GS_FLASH, GS_RECOVERY };       // how the application was started

struct GuardInfo {
    int start;                                       // GS_*
    uint32_t verdict, rec_len, rec_crc, flash_len, flash_crc, iface, rec_status;     // the guard block
    uint32_t medium, stage_addr, stage_len, stage_crc, copy_matches_record;            // the recovery block (GS_RECOVERY)
};

// chk of the recovery block: ~(the xor of words 0..6)
uint32_t guard_recovery_chk(const uint32_t *blk);
// What the blocks say about how this application was started. w = the 40 words from GUARD_BLK_ADDR. Anything that is not clearly a start from the flash
// with the new boot ROM, or a start by the recovery program with both blocks intact, is GS_OTHER: a JTAG load, a staged boot, an older boot ROM.
void guard_decode(const uint32_t *w, GuardInfo *g);

// The board's half (guard_board.cc): copies the 40 words once, clears the three magic words, decodes. The result is read by guard_info() afterwards.
void guard_capture(void);
const GuardInfo &guard_info(void);

// ---- the recovery copy on the flash drive
#define GUARD_DIR    "/Flash/trx"
#define GUARD_COPY   "hostrecovery.app"
#define GUARD_NEW    "hostrecovery.new"
#define GUARD_STAMP  "hostrecovery.stm"            // "len crc" of what the copy holds, written last
#define GUARD_COPY_PATH   GUARD_DIR "/" GUARD_COPY
#define GUARD_NEW_PATH    GUARD_DIR "/" GUARD_NEW
#define GUARD_STAMP_PATH  GUARD_DIR "/" GUARD_STAMP

// The file layer the copy lives on (hostfs_fo() on the board, a RAM disk in the test). Set once; the flows below use it.
void guard_install_fs(FoFs *fs);

// A CorSource over memory (the staged application) and over the application as the flash holds it.
struct GuardMem : CorSource {
    const uint8_t *p;
    uint32_t n;
    GuardMem(const uint8_t *p_, uint32_t n_) : p(p_), n(n_) { }
    uint32_t size() { return n; }
    bool read(uint32_t off, void *buf, uint32_t len);
};
struct GuardFlashApp : CorSource {
    Flash *fl;
    uint32_t n;
    GuardFlashApp(Flash *f, uint32_t len) : fl(f), n(len) { }
    uint32_t size() { return n; }
    bool read(uint32_t off, void *buf, uint32_t len);
};

// The copy holds exactly this image: the stamp names (len, crc) and the file has that length.
bool guard_copy_current(uint32_t len, uint32_t crc);
// Writes the copy from src[0, len): the room is checked first, the data goes to GUARD_NEW and is read back and compared by CRC-32, then the old copy goes and
// the new one gets its name, then the stamp. A cut at any point leaves a copy the recovery program takes (the old one, or GUARD_NEW). false: *why says what
// happened and *room tells whether the room was what failed; the old copy is not touched until the new data is complete and verified.
bool guard_copy_write(CorSource &src, uint32_t len, uint32_t crc, const char **why, bool *room);
// Removes the copy, GUARD_NEW and the stamp: a copy that is older than the record must not outlive it (the recovery would start an older host application).
void guard_copy_drop(void);

// The write order of P3, after the application is in the flash: the copy, then the record. When the copy cannot be written the old one is dropped before the
// record is. Both failures are reported and none stops the other step: a copy that differs from the record is taken by the recovery program, and a record that
// is missing means no guard, never a wrong one.
struct GuardOut {
    bool copy_ok, record_ok, copy_room;
    const char *copy_why, *record_why;
    uint32_t free_have, free_need;
};
void guard_finish(Flash *fl, CorSource &app, uint32_t len, uint32_t crc, bool copy_is_there, GuardOut *o);

// The step of the .txu writer after the application is in the flash (host_core_flow.cc): the copy, then the record, from the staged image. false (nothing done)
// when no file layer was installed: a build without the board's glue (the benches of the other flows) writes no copy and no record.
bool guard_active(void);
bool guard_after_update(Flash *fl, const uint8_t *app, uint32_t len, GuardOut *o);

// ---- a host application started from the flash (the boot ROM's verdict: no record, or a match)
struct GuardEnv {
    Flash *fl;
    const uint8_t *rec_img;         // the recovery program this application carries (the image with its trailer), null when it carries none
    uint32_t rec_len;
    void (*log)(const char *line);
};
struct GuardProv {
    bool app_ok;                    // the flash application is the one running (its CRC is the guard's)
    bool rec_checked, rec_written, rec_failed;
    bool copy_written, copy_failed, copy_room;
    bool record_written, record_failed;
    const char *why;                // the first thing that failed, for the log and the note
    uint32_t free_have, free_need;
};
// Brings the recovery program, the copy and the record up to date, in that order, writing only what differs. false: something failed (GuardProv says what).
bool guard_provision(const GuardEnv &env, const GuardInfo &gi, GuardProv *o);

// ---- a host application started by the recovery program
// The check before anything is written: the host-app interface the boot ROM's bitstream provides is the one this application needs, the staged image is intact
// (CRC-32 of stage_len bytes equals the recovery block's) and one the flash layer seals. *img is sealed on true; *why names the refusal otherwise.
bool guard_repair_check(const GuardInfo &gi, uint32_t iface_needed, const uint8_t *stage, FwImage *img, const char **why);
// After the application is in the flash (or already was): the copy unless the stage came from it, then the record.
void guard_repair_finish(Flash *fl, const GuardInfo &gi, const FwImage &img, GuardOut *o);

#endif
