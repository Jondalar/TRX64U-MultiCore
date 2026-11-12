// update.run_record (T104.2 D9.3, S13): the way back from our application. A `.cfw` is loaded to the DDR2 address its records name and the CPU
// jumps to the start address of the last record - what the "Run Update" of Gideon's file browser does (filetype_u2p.cc), with the checks of
// his format 1 first. Gideon's own `.cfw` (the rollback to his firmware) and our updater's (`trx64_update_*.cfw`) are the same container: a
// pre-header {fingerprint 0x4115ADDE read little endian, product family[16], product name[16]} and then records {load, length, start}
// with the bytes behind each. Both name 0x03000000. Writing the flash is the updater's work, never this file's: the application stages and jumps.
#ifndef COREAPP_UPDATE_H
#define COREAPP_UPDATE_H

#include <stdint.h>
#include "fsif.h"
#include "io.h"

// The updaters' window: they load at 0x03000000 and keep .data/.bss/heap above their image and the stack below 0x03FFFFFC
// (host-app trx_time_handover.h). A record outside [UPD_WIN_LO, UPD_WIN_HI) is refused: it could overwrite the application that reads it
// (0x30000-0xE90000, the REU window, the cartridge ROM area from 0x03C00000), and nothing but an updater has a reason to be there.
#define UPD_WIN_LO        0x03000000u
#define UPD_WIN_HI        0x03C00000u
#define UPD_DDR_WINDOW    0x01000000u    // CoreIo::ddr_write(off, ...) is relative to this
#define UPD_TIME_ADDR     0x03F00000u    // the four words the updater reads its clock from (trx_time_handover.h)
#define UPD_FINGERPRINT   0x4115ADDEu    // the bytes DE AD 15 41 of the file, as a little endian word
#define UPD_RECORDS_MAX   8

// 0 and the jump done (on the board it does not return); otherwise 1 and the reason in msg. `board_ok` false: the profile cannot take an update.
int update_run_record(CoreIo *io, CoreFs *fs, bool monolith, const char *path, char *msg, int msglen);

class OpRegistry;
void update_register(OpRegistry &r);

#endif
