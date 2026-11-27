// The writer of a .crt medium (T154 P1/P2): the file as the user has it is rebuilt from the machine's image. Every CHIP packet that holds image
// data (type 0 ROM or 2 flash, load $8000 / $A000 / $E000, up to 16 KB) gets the image's bytes; every other packet, the header and whatever follows
// the last packet stay byte for byte. A block of image that changed and that no packet of the file holds (a flash cartridge whose file left banks
// out and the C64 programmed one) gets a packet of its own behind the last one, unless it is erased ($FF throughout).
// The image's bytes (T154 D6): wb_crt_snap copies the changed 8 KiB blocks under the hold, the hold ends, and the writer takes a changed block from
// that copy and an unchanged one from the old file, which holds it as it is in DDR2. Without the copy the writer reads DDR2 under the hold: more than
// WB_CRT_SNAP_MAX changed, no memory, and the write after the image changed beyond the map (a restored state, wb_unmarked; a declined change, wb_forget),
// when the old file no longer holds the unchanged blocks.
// The EEPROM of a GMod2 is a packet at load $DE00; when it changed its bytes come from the machine, and a file without one gets it.
#ifndef COREAPP_WRITEBACK_CRT_H
#define COREAPP_WRITEBACK_CRT_H

#include "writeback.h"

struct WbCrt {
    CoreIo *io;
    uint32_t img_base;                    // the image's place in the DDR2 window
    uint32_t img_bytes;                   // how much of the window is the image (blocks above it are not this medium's)
    // The EEPROM's 2 KB, a consistent copy; 0 or an error. Null: the medium has none.
    int (*eeprom_read)(void *user, uint8_t *buf, uint32_t n);
    void *eeprom_user;
    // T154 D6: the changed blocks as they were at the hold (wb_crt_snap)
    bool snapped;
    uint8_t *copy;
    uint16_t slot[WB_BLOCKS_MAX];         // block -> its 8 KiB in copy, WB_CRT_NO_SLOT when it did not change
};
#define WB_CRT_NO_SLOT 0xFFFFu
#define WB_CRT_SNAP_MAX (1u << 20)        // at most 1 MiB of changed blocks is copied; more (a chip erase of an EasyFlash 3) is written under the hold

int wb_crt_write(CoreCtx *c, void *user, CoreFile *old, CoreFile *out, const WbDirty &dirty, char *msg, int msglen);
int wb_crt_snap(void *user, const WbDirty &dirty);
void wb_crt_unsnap(void *user);
void wb_crt_medium(WbMedium *m);          // a .crt medium's writer, snap and unsnap

#endif
