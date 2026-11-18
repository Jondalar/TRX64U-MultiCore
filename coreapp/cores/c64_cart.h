// "Run cartridge" for the C64 (T104.2, the .crt of the Library): the CRT file is parsed and its CHIP packets are placed where the TRX64 Ultimate
// bitstream's cartridge emulation reads the image, the cartridge type and its mapping descriptor are set the way Gideon's C64::set_cartridge does,
// and the machine is cold reset into the cartridge. A cartridge stays until it is removed: every later reset starts it again, as on a C64.
//
// The flash responder is answered (T154): a program or erase from the C64 is applied to the image and reported to the write-back service
// (writeback.h), which updates the .crt as "Save Changed Cartridge" says; the GMod2's EEPROM travels the same way. Not done, and told by name when a
// file needs it: the UCI at $DE1C, EasyFlash 3, and every type for which the bitstream has no mapping descriptor (Final Cartridge, Epyx, Zaxxon,
// Super Games, Super Snapshot, ...).
#ifndef COREAPP_C64_CART_H
#define COREAPP_C64_CART_H

#include "ctx.h"

// Reads the .crt, places it and starts it. 0, or a reason in msg; the machine is not touched when the file is no cartridge this application can
// start (header, chip packets, type and the window are checked first).
int c64_cart_run(CoreCtx *c, const char *path, char *msg, int msglen);

// The MOD player's cartridge (FW start_modplayer): started with the REU at 16 MB, the sampler and the UCI. The module is in the REU already. 0 or a reason.
int c64_cart_run_modplayer(CoreCtx *c, char *msg, int msglen);

// The cartridge in the port: its file name and CRT type, empty / 0 when there is none. A cartridge started from the Library stays until it is removed.
bool c64_cart_active(void);
const char *c64_cart_name(void);
const char *c64_cart_path(void);                  // the full path of the file it came from, for the Library's [C] mark
int c64_cart_crt_type(void);

// Writes the cartridge in the port out as a .crt (header and chip layout of its file, the data from DDR2 as it is now). 0 or a reason.
int c64_cart_save(CoreCtx *c, const char *path, char *msg, int msglen);

// The image as it stands in DDR2 now (whole 16 KB banks up to the highest the file has; 0 without a cartridge) and a piece of it.
uint32_t c64_cart_image_bytes(void);
void c64_cart_image_read(CoreCtx *c, uint32_t off, uint8_t *dst, uint32_t n);
void c64_cart_image_write(CoreCtx *c, uint32_t off, const uint8_t *src, uint32_t n);
// The command interface's EasyFlash sector erase (CONTROL 0x20): 8 banks from `bank`, the ROMH half when `high`, to $FF. False without a cartridge.
bool c64_cart_ef_clear(CoreCtx *c, uint8_t bank, bool high);
// A loaded state's cartridge: the .crt at path inserted, or the port emptied for NULL, with no reset. 0, or a reason in msg.
int c64_cart_for_state(CoreCtx *c, const char *path, char *msg, int msglen);

// Takes the cartridge away (type 0, no descriptor) and cold resets. 0 or a reason.
int c64_cart_remove(CoreCtx *c, char *msg, int msglen);

#endif
