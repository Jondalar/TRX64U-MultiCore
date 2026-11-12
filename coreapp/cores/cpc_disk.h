// The CPC's two disk drives (T92 D10): a .dsk (standard or extended) is copied as it is into a slot of the core's DDR2 window, the core's FDC
// (u765 with the sector adapter cpc_fdc.v) reads and writes it there, and the application writes what the FDC changed back into the file.
// The core keeps one dirty bit per 16 KB chunk of an image (D0_DIRTY / D1_DIRTY, write-1-to-clear); the poll takes the bits (clearing them in
// the core first, so a write that lands during the copy sets its bit again), and a drive's chunks go to its file when the writes have stopped
// for a second or the drive has been dirty for five. Eject, mount over a mounted drive and reset flush first; a flush that fails keeps the
// chunks, tells the screen once and is tried again every second. Pure: CoreIo and CoreFs only.
#ifndef COREAPP_CPC_DISK_H
#define COREAPP_CPC_DISK_H

#include "ctx.h"

#define CPCD_IDLE_MS 1000
#define CPCD_MAX_DIRTY_MS 5000

struct CpcDisk {
    CoreFile *file;
    bool mounted, writable, err_told;
    bool unsynced;                // T180 D4: written to the file, the sync that makes it durable failed (or has not run)
    uint32_t size;
    uint64_t pending;             // chunks taken from the core and not yet in the file
    uint32_t first_ms, last_ms, try_ms;
    char name[56];
    char path[FS_PATH_MAX];               // T180 D2: where the image came from, for the stored mounts (cpc.cc mount_slots)
};

struct CpcDisks {
    CpcDisk d[2];
    bool wp_setting;              // the setting "Disks write protected": taken at the mount
    uint8_t wp_bits;              // b0/b1: the protect bits last given to the core (DISK_CTRL b4/b5)
};

// 0, or a reason in msg; nothing of the machine or of the old image is touched when the new file is refused.
int cpcd_mount(CoreCtx *c, CpcDisks *s, int drive, const char *path, char *msg, int msglen);
// Writes the drive back, then ejects. A flush that fails refuses the eject (the image would be lost), with the reason.
int cpcd_eject(CoreCtx *c, CpcDisks *s, int drive, char *msg, int msglen);
// One pass: takes the dirty bits of both drives, flushes by the policy (force: now).
void cpcd_poll(CoreCtx *c, CpcDisks *s, bool force);
// Everything in the files, for a reset or a start over (T180 D4: CoreDesc::media_flush). True when nothing is left to write; false and the first reason in msg
// otherwise, the disks stay mounted with their chunks.
bool cpcd_flush_all(CoreCtx *c, CpcDisks *s, char *msg = 0, int msglen = 0);
bool cpcd_pending(CoreCtx *c, const CpcDisks *s);     // something is on its way to a file: for the "Saving media..." display
const char *cpcd_name(const CpcDisks *s, int drive);

#endif
