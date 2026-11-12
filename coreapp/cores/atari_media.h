// The Atari descriptor's media (T93 D6): D1:-D4: through the SIO bridge, the cartridge in the DDR2 window, a .xex as a boot disk.
#ifndef COREAPP_ATARI_MEDIA_H
#define COREAPP_ATARI_MEDIA_H

#include "ctx.h"
#include "atari_sio.h"

struct AtariState {
    AtariDisk *disk[SIO_DRIVES];
    SioLink link;
    SioEngine sio;
    uint8_t cart_mode;                    // 0: none
    char cart_name[56];
    char disk_spec[SIO_DRIVES][FS_PATH_MAX + 8];      // T180 D2: what is in each drive in the words of the stored mounts: the path, or "xex <path>" for a boot disk made of a .xex
    char cart_path[FS_PATH_MAX];                  // ... and the cartridge's path
    explicit AtariState(CoreIo *io) : link(io), sio(&link, disk), cart_mode(0)
    {
        for (int i = 0; i < SIO_DRIVES; i++) { disk[i] = 0; disk_spec[i][0] = 0; }
        cart_name[0] = 0;
        cart_path[0] = 0;
    }
};

AtariState *a8_state(CoreCtx *c);          // created on first use

int a8_mount(CoreCtx *c, const char *path, int drive, bool boot, char *msg, int msglen);   // boot: cold start after the mount
int a8_run_xex(CoreCtx *c, const char *path, int arg, char *msg, int msglen);        // boot disk in D1:, cold start
int a8_insert_cart(CoreCtx *c, const char *path, int arg, char *msg, int msglen);    // image to the window, cold start
int a8_remove_cart(CoreCtx *c, char *msg, int msglen);
int a8_eject(CoreCtx *c, int drive, char *msg, int msglen);
int a8_media_flush(CoreCtx *c, char *msg, int msglen);   // CoreDesc::media_flush (T180 D4): every dirty drive synced; 0, or 1 and the reason
bool a8_media_pending(CoreCtx *c);
// T180 D2, D5: the slots D1..D4 and Cartridge (mount_slots in atari800.cc): what each holds, and a mount/eject that does not reset the machine
// unless the caller says so
int a8_slot_mount(CoreCtx *c, int slot, const char *spec, char *msg, int msglen);
int a8_slot_eject(CoreCtx *c, int slot, char *msg, int msglen);
const char *a8_slot_spec(CoreCtx *c, int slot);
bool a8_service(CoreCtx *c);               // SIO + write-back, called with the core running; true: busy
void a8_write_cart_reg(CoreCtx *c);        // CART1 from the state; called while the core is held

#endif
