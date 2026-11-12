// The `cbm1541` library's contributor (T104.1 D15, built): the operations, states and the poll that put the drive library
// (app/lib/cbm1541/drive.h) behind the service layer, and the descriptor pieces a core with the capability `drive_a` splices into its own
// tables. The library is compiled into the application as a built-in contributor like the PRG library; the loader (libload.h) is not wired
// to the board yet, so a core's register_ops calls cbm1541_register() the way the C64's does for its own operations.
//
// Names (D15): operations drive.mount, drive.eject, drive.save, drive.create, drive.reset, drive.set_rom and the setting applies
// drive.set_power, drive.set_bus_id, drive.set_ram, drive.set_swap_delay; drive.put_rom / drive.put_rom1581 take the ROM manager's bytes;
// drive.mount_run is "Mount & Run". States drive.drive_a.image and drive.drive_a.mode (a slot id has no ':' in a name).
#ifndef COREAPP_CBM1541_H
#define COREAPP_CBM1541_H

#include "ctx.h"

class CbmDrive;

// The drive of this core application, made on first use. Null when the core has no drive window (the reason is in msg).
CbmDrive *cbm1541_drive(CoreCtx *c, char *msg = 0, int msglen = 0);

// Registers the library's operations and states under the contributor "cbm1541". Call it last in a core's register_ops.
void cbm1541_register(OpRegistry &r);

// T180 D4: the descriptor's media_flush / media_pending of a core with a drive: the disk's changes are in its image file (0), or the reason (1, the disk
// stays in the drive). Nothing when no drive was ever made.
int cbm1541_media_flush(CoreCtx *c, char *msg, int msglen);
bool cbm1541_media_pending(CoreCtx *c);

// T180 D2, D5: the one slot of a core with a drive and nothing else to mount (C128, VC-20): "Drive A". A core with more slots (the C64's cartridge) builds its
// own MountSlots and calls these for slot 0.
extern const MountSlots cbm1541_mount_slots;
const char *cbm1541_slot_spec(CoreCtx *c);
int cbm1541_slot_mount(CoreCtx *c, const char *spec, char *msg, int msglen);
int cbm1541_slot_eject(CoreCtx *c, char *msg, int msglen);

// One pass of the drive's service; the core's own service operation calls it. Returns true when the drive had work (the poll asks for the
// processor again at once).
bool cbm1541_service(CoreCtx *c);

// The text Mount & Run types: pieces of at most ten keys. `bus_id` is the drive's device number.
void cbm1541_run_script(int bus_id, char *out, int outlen);
// The text that LOADs a file of a disk by its name: `pet` is the name as the disk holds it.
void cbm1541_load_script(int bus_id, const uint8_t *pet, int len, char *out, int outlen);

#endif
