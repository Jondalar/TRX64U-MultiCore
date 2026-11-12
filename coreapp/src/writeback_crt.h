// The writer of a .crt medium (T154 P1/P2): the file as the user has it is rebuilt from the machine's image. Every CHIP packet that holds image
// data (type 0 ROM or 2 flash, load $8000 / $A000 / $E000, up to 16 KB) gets its bytes from the image in DDR2; every other packet, the header and
// whatever follows the last packet stay byte for byte. A block of image that changed and that no packet of the file holds (a flash cartridge
// whose file left banks out and the C64 programmed one) gets a packet of its own behind the last one, unless it is erased ($FF throughout).
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
};

int wb_crt_write(CoreCtx *c, void *user, CoreFile *old, CoreFile *out, const WbDirty &dirty, char *msg, int msglen);

#endif
