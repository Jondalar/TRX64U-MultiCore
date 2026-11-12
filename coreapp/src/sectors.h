// The sector-server media kind (T104 5.1, 5.2 `media_slot: sectors`): the core has a hard disk it does not hold in memory. It posts one
// request at a time into a mailbox (unit, read or write, LBA) and the base answers with 512 bytes through a staging sector in the DDR2
// window. Nothing of the file is parsed or loaded: an HDF of any size is served by offset, so offsets and sizes are 64 bit (an exFAT
// image can be larger than 4 GiB, T104.1 D16). The Amiga's HD0/HD1 are two of these.
//
// The size is announced to the core in a register that holds sectors when the core's FEATURES bit says so and bytes otherwise: the
// encoding is read from the core at mount, never written in the record (the Amiga core of run 35 counts bytes, the later ones sectors).
// An image the register cannot hold in the core's encoding is refused with the reason. Pure: CoreIo and CoreFs only.
#ifndef COREAPP_SECTORS_H
#define COREAPP_SECTORS_H

#include "ctx.h"

#define SEC_UNITS_MAX 2
#define SEC_BYTES 512

struct SectorMediaSpec {
    const char *prefix;                   // "HD"
    uint8_t nunits;                       // 1..SEC_UNITS_MAX
    uint16_t mount_reg, mount_step;       // unit u: mount_reg + u * mount_step (b0 present); the size register is 4 bytes above it
    uint16_t feat_reg;                    // FEATURES
    uint8_t feat_sectors;                 // its bit: the size register is in sectors
    uint16_t mbox_status;                 // b0 pending, b1 write, b2 unit
    uint16_t mbox_lba;                    // 4 bytes, little endian
    uint16_t mbox_done;                   // any write answers the request
    uint32_t stage_off;                   // the staging sector: DDR2 window offset
    uint16_t idle_sync_ms;                // 1000: a written file is synced after this long without a write
    uint8_t done_tag_mask;                // the bits of the status that identify the request (a counter the core bumps with every request): the DONE write carries them back,
                                          // so that the answer to a request that was given up meanwhile does not answer the one that replaced it. 0: DONE is any write
};

struct SecUnit {
    CoreFile *file;
    bool mounted, writable, unsynced;
    bool folder;                          // file is a FolderDisk (folderhd.h, T128): not closed through CoreFs, folded back at idle
    uint64_t size;                        // bytes
    uint32_t last_write_ms;
    char name[64];
    char spec[200];                       // what the .cfg and a media set say about it: the image's path, or "folder <path>, <GB> GB" (T180 D2, D5)
};

struct SecState {
    SecUnit unit[SEC_UNITS_MAX];
};

int sec_mount(CoreCtx *c, int u, const char *path, char *msg, int msglen);
// T128: a folder as a hard disk. `spec` is what the menu hands over: "<folder>/<size in GB>" (the size is the last path component, empty: the default).
int sec_mount_folder(CoreCtx *c, int u, const char *spec, char *msg, int msglen);
// The size the menu suggests for the folder the person chose (c->media_dir): 1 GB, less when the medium or the core cannot back that, as text ("" none).
const char *sec_folder_offer(CoreCtx *c);
// Writes everything pending for the unit back first (a folder disk: the full fold-back), then tells the core and closes it. A write-back that fails keeps
// the unit mounted: 5 and the reason in msg (T180 D4).
int sec_eject(CoreCtx *c, int u, char *msg, int msglen);
// Writes back every unit (a folder disk completely); true when all are on their medium, else false with the first unit's reason in msg.
bool sec_sync_all(CoreCtx *c, char *msg = 0, int msglen = 0);
bool sec_pending(CoreCtx *c);
void sec_reannounce(CoreCtx *c);
// Answers requests until the mailbox is empty or `max` were served; returns how many. The poll asks to be called again at once when it
// served any (sector after sector is how a disk is read).
int sec_service(CoreCtx *c, int max);
// The idle sync of the policy.
void sec_idle(CoreCtx *c);
const char *sec_name(CoreCtx *c, int u);

template <int N> const char *sec_state(CoreCtx *c) { return sec_name(c, N); }

#endif
