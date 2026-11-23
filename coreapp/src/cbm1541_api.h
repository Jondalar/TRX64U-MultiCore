// The export "cbm1541" (major 1): the built-in drive A of the CBM family, the shared drive library of T104.1 D15 (today coreapp/cores/cbm1541.cc and cbm1541.h over
// app/lib/cbm1541). A library of this repository puts the table into LibRecord::exports (base 3.2); the TRX64-Ultimate libraries find it with
// SvcLib::find("cbm1541", U2A3_VERSION(1, 0), &size). Not a member of LibApi.
//
// The table is the part of the drive that a library outside this repository needs: drive A as a slot ("Drive A", the one MountSlots slot of a core with a drive), the
// drive's state, the settings of Gideon's c1541 store that the library has a place for, the blank image, and the three calls the core's own service and descriptor make
// (service, media_flush, media_pending). It is not the C++ class CbmDrive. The rest of the class (flush of one image, swap delay, the ROM puts) stays behind the
// registry operations drive.* (drive.mount, drive.eject, drive.create, drive.set_rom, drive.mount_run, drive.set_power, ...), which a library calls with SvcOps::call.
//
// Why no CoreCtx: the functions of cbm1541.h take one; the library behind this table has the api groups and uses them. Why no CbmDrive pointer: the class does not cross the
// library boundary.
//
// Rules: `size` first; members only appended inside a major; reorder or removal makes the next major. Strings are owned by the drive library and valid until the next
// mount, eject or reset of the drive; copy them at once. Task: "lock" = a task holding the core lock (operation handler, hook, poll). Errors: int results are 0 for
// done and non-zero with the reason in msg (cut to msglen - 1; msg null or msglen 0: no reason); a refused call leaves the drive as it was.
//
// The slot: every call that changes what is in drive A (slot_mount, slot_eject, create into the drive, the operations of the drive) notifies the slot "Drive A"
// itself (ctx.mount_notify, svc_ctx.h); the caller does not.
#ifndef COREAPP_CBM1541_API_H
#define COREAPP_CBM1541_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The values of the drive library's own enums (d1541_drive.h DT_*, MM_*, DF_*), under names that cannot clash with them.
enum { CBM1541_TYPE_1541 = 0, CBM1541_TYPE_1571 = 1, CBM1541_TYPE_1581 = 2, CBM1541_TYPE_UNSET = 3 };   // the library offers no 1571 today (T103 Q3)
enum { CBM1541_MODE_RW = 0, CBM1541_MODE_RO = 1, CBM1541_MODE_UNLINKED = 2 };                           // how the image file is held
enum { CBM1541_FORMAT_D64 = 0, CBM1541_FORMAT_G64 = 1, CBM1541_FORMAT_D81 = 2 };

// Drive A as it stands. Filled by drive_info; `size` is the caller's sizeof on the way in (the library fills what that covers), so the struct grows by appending.
typedef struct Cbm1541Info {
    uint32_t size;
    int32_t type;                         // CBM1541_TYPE_*; UNSET: the drive was never given a type (it then behaves as a 1541)
    uint8_t powered;                      // 1: the drive is switched on
    uint8_t unit;                         // the IEC device number (bus id)
    uint8_t mode;                         // CBM1541_MODE_*; meaningful only with an image
    uint8_t reserved;
    const char *image_path;               // the full path of the disk image, null: no disk in the drive
    const char *image_name;               // its file name, null: no disk
    const char *last_dir;                 // the folder of the disk the drive saw last, "" when none; never null
    int32_t swap_delay_ms;                // the time the drive stays empty during a swap
} Cbm1541Info;

// The settings of Gideon's c1541 store that the library has a place for (set_twist, set_align, set_reset_with_c64, set_freeze, set_geos of the class), set at once.
typedef struct Cbm1541Options {
    uint32_t size;
    int32_t track_twist;                  // "Track Twist" in hundreds, for the next D64 mount
    uint8_t align_tracks;                 // "GCR Save Align Tracks": a G64 is written with every track starting at sector 0
    uint8_t reset_with_c64;               // "Resets when C64 resets"
    uint8_t freeze_in_menu;               // "Freezes in menu": b2 of the reset register, written with the next drive reset
    uint8_t geos_protection;              // "D64 Geos Copy Protection": 0 none, 1 gaps, 2 track 36, 3 both; for the next D64 mount
} Cbm1541Options;

typedef struct Cbm1541Api {
    uint32_t size;
    // Drive A as a mount slot. slot_spec: what is in it, null when empty; the path of the image with the mode in front when it is not read/write ("ro /usb0/x.d64",
    // "unlinked /usb0/x.d64"; a path starts with '/'). slot_mount: mounts `spec` of that form into drive A without a reset; what is in the drive is written back and let go first
    // (T180 D1), the notice of a failed write-back goes to the screen. slot_eject: empties the drive after the write-back. Task: lock.
    const char *(*slot_spec)(void);
    int (*slot_mount)(const char *spec, char *msg, int msglen);
    int (*slot_eject)(char *msg, int msglen);
    // Drive A as it stands. Result: false when `slot` is not 0 (there is one drive) or the core has no drive window (info is not written). Task: any.
    bool (*drive_info)(int slot, Cbm1541Info *info);
    // The image of drive A in DDR2 (the 64 KB the drive's hardware reads and writes; the bodies use CoreIo::ddr_abs_read and ddr_abs_write of the shared library). image_size(slot):
    // the bytes of the image of drive `slot` (65536 for 0), 0 when `slot` is not 0 or the core has no drive window. image_read copies n bytes from offset `off` into dst,
    // image_write copies n bytes from src into the image; a range that is not wholly inside is ignored on a write and reads zeros, and so does a bad slot. A write is complete when
    // the call returns. Task: any.
    uint32_t (*image_size)(int slot);
    void (*image_read)(int slot, uint32_t off, uint8_t *dst, uint32_t n);
    void (*image_write)(int slot, uint32_t off, const uint8_t *src, uint32_t n);
    // Sets the c1541 settings at once. Result: false when the core has no drive (nothing to set; the caller goes on). Task: lock.
    bool (*set_options)(const Cbm1541Options *opt);
    // A blank image file at `path`: `format` CBM1541_FORMAT_*, `name` the disk name. The file is written whole in this call; for the menu's stepped
    // write with progress use the operation drive.create. Task: lock or job.
    int (*create)(const char *path, int format, const char *name, char *msg, int msglen);
    // One pass of the drive's service; the core's own service operation calls it. True when the drive had work (the poll then wants the processor again at once). Task: poll.
    bool (*service)(void);
    // The descriptor's media_flush and media_pending of a core with a drive (T180 D4): the disk's changes are in its image file (0), or the reason (1, the disk stays in the
    // drive); and whether there is something to write. Nothing is done when the drive was never made. Task: lock.
    int (*media_flush)(char *msg, int msglen);
    bool (*media_pending)(void);
    // Resets drive A when there is one, as the C64's reset does with "Resets when C64 resets" (CbmDrive::reset): 0, or non-zero and the reason; -1 and no reason when
    // the core has no drive (none is made). New in 3.3 (advisor 2026-11-23). Task: lock.
    int (*reset)(char *msg, int msglen);
} Cbm1541Api;

#ifdef __cplusplus
}
#endif

#endif
