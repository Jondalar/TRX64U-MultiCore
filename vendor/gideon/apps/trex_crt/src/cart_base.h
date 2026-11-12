/*
 * cart_base.h
 *
 *  Shared base for the cartridge dumper, writer and inspector: bus
 *  management, bank switching, endian helpers, bank scanning, cart detection
 *  primitives and the bank map. Port of the firmware CartBase
 *  (software/io/c64/cart_base.h).
 */

#ifndef CART_BASE_H
#define CART_BASE_H

#include "ui_shim.h"

// Bank status flags for visual display (used per-chip: status_lo / status_hi)
#define BANK_UNKNOWN    0   // not yet scanned
#define BANK_EMPTY      1   // all $FF (erased)
#define BANK_FILLED     2   // has data
#define BANK_ERASING    3   // erase in progress
#define BANK_WRITING    4   // programming / dumping in progress
#define BANK_VERIFIED   5   // verified OK / dumped OK
#define BANK_ERROR      6   // verify failed / dump error

#define MAX_CART_BANKS  256

// C64 bus access through the app ABI, named like the firmware macros. The wrappers pass every access on
// unchanged and note that the app touched the cartridge's I/O or ROM space (a bank register is write-only, so
// the cartridge has to be reset after the tool, T57 5.5)
uint8_t cart_peek(uint16_t addr);
void cart_poke(uint16_t addr, uint8_t val);
int cart_read(uint16_t addr, void *dst, int len);
#define C64_PEEK(x)           cart_peek(x)
#define C64_POKE(x, y)        cart_poke(x, y)
#define C64_READ(x, dst, len) cart_read(x, dst, len)

#define MODE_NORMAL 0

// EasyFlash 3: 8 slots of 64 EasyFlash banks. Slot 0 is the system area (KERNALs, menu, EasyProg, freezers,
// EF-Directory), slots 1-7 hold EasyFlash CRTs. $DE01 selects the slot and reads back in EasyFlash mode.
#define EF3_SLOT_REG     0xDE01
#define EF3_NUM_SLOTS    8
#define EF3_DIR_BANK     0x10 // EF-Directory: slot 0, bank $10, ROML
#define EF3_DIR_SLOTS    16   // slot names in the directory
#define EF3_DIR_KERNALS  8    // KERNAL names in the directory
#define EF3_NAME_LEN     16   // bytes per name, PETSCII, 0-padded
#define EF3_DIR_SIGN_LEN 16   // "EF-Directory V1:"
#define EF3_DIR_SIZE     (EF3_DIR_SIGN_LEN + (EF3_DIR_SLOTS + EF3_DIR_KERNALS) * EF3_NAME_LEN + 2) // + checksum word

// ---- the two cores (T57) ----------------------------------------------------------------------------------
// Gideon's bitstream: api->trx is NULL, the app does what it always did. Our bitstream (TRX2): the port's slot
// block at 0x101A7000 is there, the app asks for the registers it needs and leaves every other access as it was.
enum { CORE_GIDEON = 0, CORE_TRX = 1 };
extern int g_core;
extern bool g_cart_touched; // the app accessed cartridge I/O or ROM space during the session

#define SLOT_CTRL_OFF   0x7000u // b1 ROM_WR_EN
#define SLOT_STATUS_OFF 0x7004u
#define SLOT_ST_PINS    0x00000003u // b1 EXROM, b0 GAME (1 = high)
#define SLOT_ST_EXT_ROM 0x00000400u // b10: the port serves ROML/ROMH
#define SLOT_ST_EXT_IO  0x00000300u // b9:8 IO2, IO1 to the port
#define SLOT_ST_INT_ROM 0x00004000u // b14: the internal cartridge serves ROM
#define SLOT_ST_ARMED   0x00010000u
#define SLOT_ST_FAULT   0x00020000u
#define SLOT_ST_WR_EN   0x00040000u // b18
#define SLOT_ST_BLOCK   0x80000000u // b31: our slot block is there

// Decides the core once, at the start (api->trx and the "TRX2" ID); the slot block is checked by the session
void core_detect(void);
static inline bool is_trx(void) { return g_core == CORE_TRX; }

uint32_t slot_status(void);

// ROM_WR_EN around a write (T57 5.4). open: sets the bit and waits until SLOT_STATUS shows it; false if it never
// does. close: puts the bit back to the value found at the start of the session, idempotent, checks it by reading
// back. Both do nothing on Gideon's core.
bool slot_wr_open(void);
void slot_wr_close(void);

// The start of every tool (T57 5.1). Gideon's core: the bus set-up as before (the dumper also takes the app out of
// Ultimax, which it always did). Our core: checks the port, then the same set-up, the Normal mode and the masks.
// False: the tool cannot go on, cart_session_status() has the status line and the dialog has been shown.
bool cart_session_open(bool gideon_normal);
const char *cart_session_status(void);
// The end (T57 5.5): the write lock, the bus registers, and on our core a C64 reset when the cartridge was touched
void cart_session_close(void);

// A dialog in the UI
void cart_notify(const char *msg);
// "No cartridge on the pins" for our core, nothing on Gideon's
void cart_notify_no_cartridge(void);

typedef struct
{
    // Detected GAME/EXROM pin levels
    uint8_t det_game; // 0=active(low), 1=inactive(high)
    uint8_t det_exrom;

    // EasyFlash 3, set by cart_detect_ef3() and cart_ef3_pick_slot()
    bool is_ef3;
    uint8_t ef3_start_slot;          // $DE01 found at start, restored on exit
    uint8_t ef3_slot;                // slot picked by the user (1-7), 0 = none yet
    bool ef3_whole;                  // the dumper's "all slots" entry was picked (type 90, 8 MB, slot 0 included)
    char ef3_name[EF3_NAME_LEN + 1]; // its name from the EF-Directory, empty if none
} cart_base_t;

// Bus management (the ABI saves and restores the bus registers, C64_DMA_MEMONLY and the CPU port).
// cart_restore_bus only acts after cart_setup_bus.
void cart_setup_bus(void);
void cart_restore_bus(void);

// False when the boot skipped the cartridge init, after telling the user how to fix it
bool cart_check_dma_access(void);

// Bank switching (type-aware for multi-type dumper, 0 = plain $DE00)
void cart_switch_bank(int bank, int crt_hw_type);

// Cart presence detection: reads GAME/EXROM, returns false if no cart
bool cart_detect_present(cart_base_t *cb);

// EasyFlash detection — $DF00 RAM test (definitive: only EF has RAM at $DF00)
bool cart_detect_easyflash(void);

// GMod2 detection: serial EEPROM test, then the high-bank duplicate test.
// Must be called AFTER cart_detect_easyflash().
bool cart_detect_gmod2(void);

// GMod2 EEPROM read — M93C86 serial protocol via $DE00
// Reads all 2KB (1024 words) into buf. Returns true if EEPROM responds.
bool cart_read_gmod2_eeprom(uint8_t *buf);

#define GMOD2_EEPROM_SIZE  2048 // bytes, word n = buf[2n] << 8 | buf[2n + 1]
#define GMOD2_EEPROM_WORDS 1024

// One GMod2 EEPROM word (READ, 10-bit address)
uint16_t cart_gmod2_eeprom_read_word(int address);
// EWEN (true) or EWDS (false). The EEPROM powers up write-disabled.
void cart_gmod2_eeprom_write_enable(bool enable);
// WRITE of one word, then waits for DO to show ready. Needs EWEN. False on a timeout.
bool cart_gmod2_eeprom_write_word(int address, uint16_t data);

// EasyFlash 3 detection: $DE01 must read back two different slot numbers. Must be called AFTER
// cart_detect_easyflash() succeeded. Restores $DE01 on an EF3; selects bank 0 otherwise (an EF1 takes the
// $DE01 writes as bank numbers).
bool cart_detect_ef3(cart_base_t *cb);

// Selects the picked slot, slot 0 for "all slots", or the start slot while none is picked (no-op if not EF3)
void cart_ef3_select_slot(const cart_base_t *cb);

// Selects the slot found at start again (no-op if not EF3). Call before cart_restore_bus().
void cart_ef3_restore_slot(const cart_base_t *cb);

// Reads the EF-Directory (EF3_DIR_SIZE bytes) in Ultimax mode, then selects the slot again and bank 0.
// Returns true if the signature matches.
bool cart_ef3_read_dir(const cart_base_t *cb, uint8_t *dir);

// True if the EF-Directory signature stands in bank $10 of the active slot, which identifies an EasyFlash 3
// showing its system area. Writes nothing but the $DE00 bank select and leaves bank 0 selected.
bool cart_ef3_dir_visible(void);

// ASCII name of a slot in a directory read by cart_ef3_read_dir(); name holds EF3_NAME_LEN + 1 bytes
void cart_ef3_slot_name(const uint8_t *dir, int slot, char *name);

// What cart_ef3_pick_slot() offers after the seven slots, and what it returned.
#define EF3_PICK_SLOTS     0 // slots 1-7 only
#define EF3_PICK_WHOLE     1 // plus "Full EF3 (all slots, type 90)": sets ef3_whole, ef3_slot = 0 (dumper and writer)
#define EF3_PICK_ERASE     2 // plus (behind it) "ERASE WHOLE CART...": the writer's F7; returns EF3_PICKED_ERASE, changes nothing else
                             // `extra` is a mask of these two
#define EF3_PICKED_NONE    0 // cancelled, nothing changed
#define EF3_PICKED_SLOT    1
#define EF3_PICKED_WHOLE   2
#define EF3_PICKED_ERASE   3

// Lets the user pick one of slots 1-7 by its directory name ("Slot n" without a valid directory), sets
// ef3_slot and ef3_name and selects the slot (EF3_PICKED_SLOT). `extra` adds one entry behind the slots (EF3_PICK_*).
int cart_ef3_pick_slot(cart_base_t *cb, int extra);

// PETSCII (upper/lower case set) to ASCII, stops at a 0 byte; dst holds len + 1 bytes
void cart_petscii_to_ascii(char *dst, const uint8_t *src, int len);
// ASCII to PETSCII as cc65 and ACME store strings; pads with 0 like strncpy
void cart_ascii_to_petscii(uint8_t *dst, const char *src, int len);

// Bank count via wrap-around (returns first power-of-2 where bank N == bank 0)
int cart_count_banks(void);

// Bank scanning — marks each bank as EMPTY or FILLED
// status_lo: ROML ($8000), always filled.  status_hi: ROMH, only if has_romh.
void cart_scan_all_banks(uint8_t *status_lo, uint8_t *status_hi, int max_banks, bool has_romh, uint16_t romh_addr);

// Endian helpers (big-endian CRT format)
void cart_put_word(uint8_t *p, uint16_t v);
void cart_put_dword(uint8_t *p, uint32_t v);
uint16_t cart_get_word(const uint8_t *p);
uint32_t cart_get_dword(const uint8_t *p);

// Colour of a bank cell by its status; the map itself is drawn by cart_screen.c
int cart_bank_color(uint8_t status);

#endif
