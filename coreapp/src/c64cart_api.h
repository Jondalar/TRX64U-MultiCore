// The export "c64cart" (major 1): the cartridge port of the C64 family, the shared cartridge library of T239.3 D12 (today coreapp/cores/c64_cart.cc and c64_cart.h). A
// library of this repository puts the table into LibRecord::exports (base 3.2); the TRX64-Ultimate libraries (ring, base) and any C64-family core (C64SCPU, C128) find it
// with SvcLib::find("c64cart", U2A3_VERSION(1, 0), &size). It is not a member of LibApi.
//
// Why no CoreCtx: the functions of c64_cart.h take one; the library behind this table has the api groups (io, ctx, settings, ops, fs) and uses them, so no call here
// takes it. Run and remove cold reset the machine through the operation "machine.cold_reset"; for_state and run_image do not reset it.
//
// The slot: every call that changes what is in the port (run, remove, for_state, run_image and the rest) notifies the slot "Cartridge" itself (ctx.mount_notify,
// svc_ctx.h); the caller does not.
//
// Rules: `size` first; members only appended inside a major; reorder or removal makes the next major. A library tests `size` before it calls a member added later.
// Strings are owned by the cartridge library and valid until the cartridge in the port changes; copy them at once. Task classes: "lock" = a task holding the core lock
// (an operation handler, a hook, a poll); "any" = any task. Errors: a result of int is 0 for done and non-zero with the reason in msg (cut to msglen - 1); a call
// with msg null or msglen 0 gives no reason. The machine is not touched when a call refuses.
//
// Play MOD (c64_cart_run_modplayer) is not in the table: T239.3 D5.
#ifndef COREAPP_C64CART_API_H
#define COREAPP_C64CART_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct C64CartApi {
    uint32_t size;
    // ---- what is in the port (any task) --------------------------------------------------------------------------------------------------------------------
    // True while a cartridge is in the port. One started from the Library stays until it is removed, so every later reset starts it again, as on a C64.
    bool (*active)(void);
    // Its file name (short form for the screen) and the full path of its file ("" when the cartridge has no file of its own); "" without a cartridge. Never null.
    const char *(*name)(void);
    const char *(*path)(void);
    // The CRT type of the file, as its header says it; 0 without a cartridge. The hardware type the bitstream was given (C64_CARTRIDGE_TYPE) is
    // read from the register, not from here.
    int (*crt_type)(void);
    // ---- the image in DDR2 (the cartridge's ROM/flash as it is now) --------------------------------------------------------------------------------------------
    // Whole 16 KB banks up to the highest the file has; 0 without a cartridge.
    uint32_t (*image_bytes)(void);
    // `n` bytes from offset `off` of the image into dst (image_read), or from src into the image (image_write). The caller keeps off + n <= image_bytes() on a read.
    // A write is ignored without a cartridge and past the window the cartridge was inserted with. Task: any.
    void (*image_read)(uint32_t off, uint8_t *dst, uint32_t n);
    void (*image_write)(uint32_t off, const uint8_t *src, uint32_t n);
    // A loaded state's cartridge (.c64re): the .crt at `path` inserted, or the port emptied for path null, with no reset; the state goes in next. Task: lock.
    int (*for_state)(const char *path, char *msg, int msglen);
    // ---- the cartridge's RAM (the 64 KB of the port's RAM window in DDR2; the bodies use CoreIo::ddr_abs_read and ddr_abs_write of the shared library) ----------------------
    // ram_size: the bytes of the RAM the core's cartridge port has (65536), 0 on a core without the port. ram_read copies n bytes from offset `off` into dst, ram_write copies
    // n bytes from src into the RAM. A range that is not wholly inside the RAM is ignored on a write and reads zeros. A write is complete when the call returns. Task: any.
    uint32_t (*ram_size)(void);
    void (*ram_read)(uint32_t off, uint8_t *dst, uint32_t n);
    void (*ram_write)(uint32_t off, const uint8_t *src, uint32_t n);
    // ---- what the cartridge in the port says about itself (any task) ---------------------------------------------------------------------------------------------
    // The cartridge definition's require and prohibit flags as the cartridge in the port has them (the firmware's C64::get_cart_definition()); a .c64re's cartridge entry
    // carries them. Both are 0 without a cartridge. writable: the cartridge in the port has flash or an EEPROM that the C64 can change (the firmware's trx_cart_writable()); a
    // state file carries the image of such a cartridge with it (cartFlash). False without a cartridge.
    uint16_t (*require)(void);
    uint16_t (*prohibit)(void);
    bool (*writable)(void);
    // ---- the GMod2's EEPROM (the 93C86 behind the cartridge port; byte 0 of its window is the dirty flag, the 2048 data bytes at +2048) -------------------------------
    // eeprom_get copies the 2048 bytes as they are into out and leaves the dirty flag alone (a dump of a state). eeprom_put writes the 2048 bytes and clears the dirty
    // flag, so only what the C64 writes from here on is a change (a load of a state). A core without the window reads 0 and ignores the write. Task: any.
    void (*eeprom_get)(uint8_t out[2048]);
    void (*eeprom_put)(const uint8_t data[2048]);
    // ---- start and stop (lock) ------------------------------------------------------------------------------------------------------------------------------
    // Reads the .crt, places it and starts it: the machine is held, the image placed where the bitstream's cartridge emulation reads it, the type and the mapping
    // descriptor set, the REU, the sampler and the UCI at $DF1C switched on when the file needs them, and the machine cold reset into the cartridge. The machine is
    // not touched when the file is no cartridge this application can start (header, chip packets, type and window are checked first).
    int (*run)(const char *path, char *msg, int msglen);
    // Takes the cartridge away (type 0, no descriptor) and cold resets.
    int (*remove)(char *msg, int msglen);
    // Writes the cartridge in the port out as a .crt: the header and chip layout of its file, the data from DDR2 as it is now. Task: lock.
    int (*save)(const char *path, char *msg, int msglen);
    // The command interface's EasyFlash sector erase (CONTROL 0x20): the 8 banks of 8 KB from `bank` (a multiple of 8), the ROMH half when `high`, become $FF; the change
    // goes to the write-back service. A bank outside the image is left alone. False without a cartridge. Task: any.
    bool (*ef_clear)(uint8_t bank, bool high);
    // The SID player's cartridge: a 16 KB normal cartridge from the RAM image `img`, the machine held in reset and not released; the caller writes the RAM the player
    // expects, then resets. The cartridge in the port before is put back by the next reset (player_end, called from the reset's hold). `name` is what the screen shows.
    int (*run_image)(const char *name, const uint8_t *img, char *msg, int msglen);
    void (*player_end)(void);
    bool (*player_active)(void);
} C64CartApi;

#ifdef __cplusplus
}
#endif

#endif
