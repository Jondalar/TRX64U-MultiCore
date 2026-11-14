// Writable cartridges on the TRX64 Ultimate bitstream (TRX64-Ultimate
// docs/specs/T16.2-app-cartridge-writeback.md, ARCH-lean-state-access.md §7.4).
//
// The bitstream's cartridge model answers the flash command sequences itself and
// stops at a program or erase until the firmware has applied it to the image in
// DDR2 (the expansion-port block 0x01000000 with the TRX bitstream's ROM_SEL, Gideon's
// 0x03C00000 without it; 16 KiB per bank). The interrupt handler here does that and
// keeps the map of changed 8 KiB image blocks; the auto-save code in
// c64_subsys.cc writes the file back from it.
//
// On Gideon's bitstreams the register block reads 0 (no ID), trx_cart_present()
// is false and nothing here touches the hardware.
//
// No includes beyond stdint.h: c64_crt.cc includes this, and is also built on
// the host by its test.
#ifndef TRX_CART_H
#define TRX_CART_H

#include <stdint.h>

// CART_TYPE values only the TRX bitstream maps (by their descriptor, trx_cart_desc.h).
#define CART_TYPE_C64MEGACART  0x68 // Gideon's bitstream: Ocean 8K
#define CART_TYPE_DINAMIC      0x88
#define CART_TYPE_MAGICDESK16  0xA8
#define CART_TYPE_EASYFLASH_XL 0x31 // TRX64's 4 MB EasyFlash
#define CART_TYPE_EASYFLASH3   0x12 // TRX64's EasyFlash 3 (T51): 8 MB, CRT type 90

// T60 (TRX64-Ultimate docs/specs/T60-reu-16mb.md §7): the 16 MB expansion-port block of DDR2 at
// TRX_XPORT_BLOCK (= REU_MEMORY_BASE) holds the REU's memory or the CRT image, never both. The app's
// own small cartridges (Run PRG's boot cart, SID, MUS and MOD players) have a slot of their own,
// TRX_BUILTIN_SLOT, so that starting them never overwrites REU data.
#define TRX_XPORT_BLOCK   0x01000000u
#define TRX_XPORT_ROM_MAX 0x00400000u // the window of every type but EasyFlash 3 (22-bit image offset): the largest CRT, EasyFlashXl
#define TRX_XPORT_ROM_MAX_EF3 0x00800000u // EasyFlash 3, CRT type 90 (T51): 512 banks of 16 KiB; needs XPORT_CAPS
#define TRX_BUILTIN_SLOT  0x00F00000u

// Only the U64-II/C64U firmware (u64ii ultimate) has trx_cart.cc; every other
// build, the recovery app and the host test get the stubs at the end.
#if (U64 == 2) && !defined(RECOVERYAPP)

// ID "TRX2" with the cartridge page. Read once, then cached.
bool trx_cart_present(void);

// The bitstream reads the image from where trx_io ROM_SEL says. True when it has ROM_SEL (probed once:
// a bitstream without it reads 0 after a write of 1); false: Gideon's layout, the image at __cart_rom_start.
bool trx_xport_shared(void);

// ROM_SEL for the cartridge about to start (the C64 is held in reset): a built-in one reads from
// TRX_BUILTIN_SLOT, a CRT image from TRX_XPORT_BLOCK. The cached lines of the previous image are dropped.
void trx_cart_select_rom(bool builtin);

// The bitstream has the 8 MiB image window and the EasyFlash 3 banking (descriptor R2, 9-bit bank; page 4 XPORT_CAPS, T51); older ones read 0.
bool trx_ef3_present(void);

// The write counter (+1 per flash operation and per EEPROM write): the seqlock
// for copying the EEPROM. 0 without the TRX block.
uint16_t trx_cart_wr_count(void);

// From C64::set_cartridge, after CART_TYPE is written: writes the type's mapping
// descriptor (none: the bitstream keeps the cartridge off; it takes the descriptor
// at the next reset or force) and its flash configuration (or disables the responder),
// drops a pending operation of the previous cartridge and clears the map.
void trx_cart_configure(uint8_t cart_type);

// A type with flash or an EEPROM the C64 can change is configured.
bool trx_cart_writable(void);

// Changes the file does not have yet (map not empty, or the EEPROM written).
bool trx_cart_pending(void);

// Pending and due: quiet for 2 s, pending for 30 s, or a flush requested.
bool trx_cart_due(void);

// Write back at the next look of the auto-save task, without the quiet time.
void trx_cart_request_flush(void);

// Hold flash operations off (the C64 sees the chip busy) while the image is copied.
void trx_cart_hold(bool on);

// Write-back bracket: begin moves the pending map to "in flight" (call it with
// the image still, inside the hold); end clears it, or puts it back on failure.
void trx_cart_begin_writeback(void);
void trx_cart_end_writeback(bool ok);

// The pending changes need no write (the user declined, or the file cannot be written).
void trx_cart_forget(void);

// The expansion port's write gate (SLOT_CTRL b1 ROM_WR_EN, T16 §6.1, T57 §4.1). The bitstream has the
// slot block (SLOT_STATUS b31); Gideon's and older ones read 0 there.
bool trx_slot_present(void);
// SLOT_STATUS b16: the expansion port is armed (a physical cartridge is being served).
bool trx_slot_armed(void);

// The gate is always open: writes reach the expansion port as on a real C64 (no write protection,
// Alex 2026-10-04). Called at every C64 reset and settings change; the masks are no longer used.
void trx_slot_apply_policy(int internal, int external);

// The same from the masks the registers hold now (after an app: it puts them back itself).
void trx_slot_apply_policy_now(void);

#else

static inline bool trx_cart_present(void) { return false; }
static inline bool trx_xport_shared(void) { return false; }
#ifdef TRX_HOST_EF3 // host test: the bitstream with the EasyFlash 3 block, or not
extern bool host_ef3_present;
static inline bool trx_ef3_present(void) { return host_ef3_present; }
#else
static inline bool trx_ef3_present(void) { return false; }
#endif
static inline void trx_cart_select_rom(bool) { }
static inline uint16_t trx_cart_wr_count(void) { return 0; }
static inline void trx_cart_configure(uint8_t) { }
static inline bool trx_cart_writable(void) { return false; }
static inline bool trx_cart_pending(void) { return false; }
static inline bool trx_cart_due(void) { return false; }
static inline void trx_cart_request_flush(void) { }
static inline void trx_cart_hold(bool) { }
static inline void trx_cart_begin_writeback(void) { }
static inline void trx_cart_end_writeback(bool) { }
static inline void trx_cart_forget(void) { }
static inline bool trx_slot_present(void) { return false; }
static inline bool trx_slot_armed(void) { return false; }
static inline void trx_slot_apply_policy(int, int) { }
static inline void trx_slot_apply_policy_now(void) { }

#endif

#endif
