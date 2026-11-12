// The Apple //e descriptor's floppies and hard disk (T89 P8, S9; T89.1): two Disk II drives whose images (.dsk/.do DOS 3.3 order, .po ProDOS
// order, .nib, .woz, and .2mg of the first three kinds) the application serves quarter track by quarter track through the core's track window
// (a2_trk.vhd: the core raises a request, the application puts the track's BITS in or takes the dirty one out and acknowledges), and two
// ProDOS hard disk units in slot 7 (.hdv, .po, .2mg) that it serves block by block (a2_hdd.vhd). Not registered anywhere here: the
// descriptor (apple2.cc) calls these from its operations and its service.
#ifndef COREAPP_APPLE2_DISK_H
#define COREAPP_APPLE2_DISK_H

#include "ctx.h"
#include "apple2_nib.h"
#include "apple2_woz.h"

#define A2_DRIVES 2
#define A2_UNITS  2

enum A2Format { A2_FMT_NONE = 0, A2_FMT_DOS, A2_FMT_PRODOS, A2_FMT_NIB, A2_FMT_WOZ };

struct A2Drive {
    CoreFile *file;
    A2Format fmt;
    uint32_t off;                  // 64 byte header of a .2mg
    bool writable;                 // the file and the medium accept writes; the core's write protect bit is the opposite
    bool needs_sync;               // tracks were written since the last sync
    A2Woz woz;
    int loaded_id;                 // what the core's buffer holds: the track of a sector or nibble image (0..34) or the WOZ TRKS entry; -1 nothing, -2 noise
    uint32_t loaded_bits;          // its length in bits
    bool kept;                     // T180 D4: a written track the file did not take; its bits are kept (apple2_disk.cc) and tried again by the flush
    int kept_q, kept_id;
    uint32_t kept_bits;
    char name[56];
    char path[FS_PATH_MAX];                // T180 D2: where the image came from, for the stored mounts (mount_slots in apple2.cc)
};

// A hard disk unit: a file of 512 byte blocks, behind an optional 64 byte .2mg header.
struct A2Unit {
    CoreFile *file;
    uint32_t off;
    uint32_t blocks;               // at most 65,535 (the card's block number is 16 bit)
    bool writable;
    bool needs_sync;
    char name[56];
    char path[FS_PATH_MAX];                // T180 D2
};

struct A2State {
    A2Drive drive[A2_DRIVES];
    A2Unit unit[A2_UNITS];
    CoreFs *fs;                    // the file system the images were opened on (closed there)
    uint8_t diskrom[256];          // diskii.rom as the ROM manager found it (apple2.cc puts it into the core only while a disk is mounted)
    bool have_diskrom;
    bool caps_known;               // the CAPS register holds caps_last (apple2.cc)
    uint8_t caps_last;
    uint32_t diag_at;              // ms of the last release; the start diagnostics are printed twice after it (apple2.cc)
    uint8_t diag_stage;
    uint32_t last_report;          // ms of the last counter line (a2_report)
    A2State() : fs(0), have_diskrom(false), caps_known(false), caps_last(0), diag_at(0), diag_stage(2), last_report(0)
    {
        memset(drive, 0, sizeof(drive));
        memset(unit, 0, sizeof(unit));
        memset(diskrom, 0, sizeof(diskrom));
    }
};

int a2_qindex(int q);                                                           // the stepper's quarter track -> the image's (whole track N is 4N + 2 -> 4N)
A2State *a2_state(CoreCtx *c);                                                  // created on first use
const char *a2_drive_name(CoreCtx *c, int drive);                               // the image's file name, null when empty
const char *a2_unit_name(CoreCtx *c, int unit);                                 // the hard disk image's file name, null when empty

// The image, its format by extension (.woz or the magic: WOZ; .2mg: its header; .po: ProDOS order, .nib: raw, else DOS order; the size decides the
// rest: 143,360 or 232,960 bytes). Flushes what the drive held, opens, tells the core (image present, write protect, image changed). 0 or a
// reason in msg. Does not reset the machine: the caller decides (D1 at a cold start, as the Atari).
int a2_mount(CoreCtx *c, const char *path, int drive, char *msg, int msglen);
int a2_eject(CoreCtx *c, int drive, char *msg, int msglen);                   // flush, close, tell the core
void a2_write_ctl(CoreCtx *c);                                                  // DISK_CTL and the card (after a core reload)
// Takes a dirty track out of the buffer of `drive` now and syncs the file (needs the core's request path: the machine may be held,
// the track window's state machine runs on the system clock). Bounded wait. 0 or a reason.
int a2_flush(CoreCtx *c, int drive, char *msg, int msglen);
void a2_flush_all(CoreCtx *c);
// T180 D4, the descriptor's media_flush / media_pending: both drives and both slot 7 units. 0, or 1 and the first reason in msg; the media stay mounted.
int a2_media_flush(CoreCtx *c, char *msg, int msglen);
bool a2_media_pending(CoreCtx *c);
// One poll of both drives' request registers and the card's: serves what is open. true: it did work, call again at once.
bool a2_service(CoreCtx *c);

// ---- slot 7 (apple2_hdd.cc) -------------------------------------------------------------------------------------------------
// A ProDOS volume: .hdv, .po, or .2mg (ProDOS order). 0 or a reason in msg. The card's ROM goes into the core while a unit is mounted.
int a2_hdd_mount(CoreCtx *c, const char *path, int unit, char *msg, int msglen);
int a2_hdd_eject(CoreCtx *c, int unit, char *msg, int msglen);
void a2_hdd_write_ctl(CoreCtx *c);                                              // HDD_CTL, the sizes and the ROM from the state
bool a2_hdd_service(CoreCtx *c);                                                // one block, when the card wants one
bool a2_hdd_sync(CoreCtx *c, char *msg, int msglen);                            // the files out to the medium; false and the reason when one refused
// What a file is for the file browser: floppy, hard disk, neither. A .po decides by size, a .2mg by its header.
enum A2Kind { A2K_NONE = 0, A2K_FLOPPY, A2K_HDD };
A2Kind a2_kind_of(uint64_t size, const char *ext_lower, const uint8_t *head64);
bool a2_2mg_header(const uint8_t *h, uint64_t fsize, uint32_t *off, uint32_t *len, uint8_t *fmt);   // false: not a usable .2mg

#endif
