// T152 D4/D6/D7: the flash protection set before a LARGE core is handed the machine. Pure code over an abstract SPI port, so
// tests/hostapp/test_flashlock.cc runs it against a model of the W25Q128JV; flashlock_board.cc is the port on the board.
//
// What is set (D6): a VOLATILE write of status register 1 (write enable 0x50, never 0x06) with SRP = 0, SEC = 0, TB = 1, BP2:0 = 101, which on the
// W25Q128JV protects exactly 0x000000-0x3FFFFF (the bitstream slot and the application slot, D7); then a volatile write of status register 2 with
// SRL = 1 (power-supply lock-down: the status registers cannot be written again until the power is removed). Both registers are read back; a
// start that does not read back as set is refused.
//
// SRP = 1 together with SRL = 1 is the ONE-TIME PROGRAM LOCK of this chip and cannot be undone. Three things keep it impossible:
//   1. every status register 1 write of flashlock.cc goes through fl_write_sr1(), which clears bit 7 in the byte it sends;
//   2. SRL is written only after status register 1 was read back with SRP = 0;
//   3. only the chip with JEDEC ID EF 40 18 is touched at all; any other chip gets no status register access (not even a read).
#ifndef HOST_FLASHLOCK_H
#define HOST_FLASHLOCK_H

#include <stdint.h>

#define FL_JEDEC_W25Q128 0xEF4018u      // manufacturer, memory type, capacity as the command 0x9F returns them

// Status register 1 (command 0x05 / 0x01): bit 7 SRP, 6 SEC, 5 TB, 4:2 BP2:0, 1 WEL, 0 BUSY. Status register 2 (0x35 / 0x31): bit 6 CMP, bit 0 SRL.
#define FL_SR1_SRP   0x80u
#define FL_SR1_SEC   0x40u
#define FL_SR1_TB    0x20u
#define FL_SR1_BP    0x1Cu
#define FL_SR1_BP101 0x14u              // BP2 = 1, BP1 = 0, BP0 = 1
#define FL_SR1_WANT  (FL_SR1_TB | FL_SR1_BP101)
#define FL_SR2_CMP   0x40u
#define FL_SR2_SRL   0x01u

// One chip-select frame at a time: begin(), bytes out with send() and bytes in with recv(), end(). The controller of the board clocks a byte in only on a
// read of its data register, so a frame that only writes (a status register write) must never call recv(): the extra byte would become data. wait_idle() polls
// BUSY (false after the time out in ms).
struct FlashSpi {
    virtual ~FlashSpi() { }
    virtual void begin() = 0;
    virtual void send(uint8_t out) = 0;
    virtual uint8_t recv() = 0;
    virtual void end() = 0;
    virtual bool wait_idle(int ms) = 0;
};

enum FlashLockResult {
    FLR_LOCKED = 0,        // protection set and read back
    FLR_OFF,               // the setting is off: nothing touched (S7)
    FLR_OTHER_CHIP,        // not a W25Q128JV: no status register was read or written, the core starts unprotected
    FLR_FAILED             // the chip did not read back as set (or a guard refused): the core must not start
};

struct FlashLockInfo {
    uint32_t jedec;        // what the chip answered (0 when it was not asked)
    uint8_t sr1, sr2;      // read back after the write (0 when not read)
    const char *msg;       // static text for the page and the log
};

uint32_t flashlock_jedec(FlashSpi &spi);
uint8_t  flashlock_read_sr1(FlashSpi &spi);
uint8_t  flashlock_read_sr2(FlashSpi &spi);

// The protection for the start of a large core. enabled = the F8 setting.
FlashLockResult flashlock_apply(FlashSpi &spi, bool enabled, FlashLockInfo *info);

// Read only (S6): the chip's ID and, for a W25Q128JV only, both status registers. false for any other chip: nothing but the ID was read.
bool flashlock_status(FlashSpi &spi, FlashLockInfo *info);

// Is this the state flashlock_apply() promises? (SRP clear, SEC clear, TB set, BP = 101, CMP clear, SRL set)
bool flashlock_is_set(uint8_t sr1, uint8_t sr2);

// ---- the board side (flashlock_board.cc): the setting and the real chip. The host tests and the render bench link their own.
// T152 D9 (Alex, 2026-10-31): the feature toggle. 1 (the board build since T164 S15): the F8 row is shown and the lock is tried before a large
// core. The host's flash gate (host/flash_gate.vhd, g_lock) lets exactly this file's lock pattern through (0x50, then 0x01 with SR1 0x34, then
// 0x50 and 0x31 with SR2 0x03, once each) and every other status-register write out as 0x9F. 0 hides the row and tries no lock (a host without
// the lock class, e.g. a flashed older one).
#ifndef HOSTAPP_FLASH_LOCK
#define HOSTAPP_FLASH_LOCK 1
#endif
bool flashprot_enabled(void);                          // F8 "Protect Flash from updates" (default off, D9)
void flashprot_set(bool on);
// Runs flashlock_apply on the board's flash with the setting, logs the result. true = the start may go on. *msg: the text for a refusal.
bool flashprot_before_handover(const char **msg);
void flashprot_register(void);                         // makes the settings store (called once at start, after hostcfg_install_board)
// The chip and its status registers right now, as one line for the F8 page (S6: the host menu keeps the flash open). Reads only.
void flashprot_status_text(char *out, int n);

#endif
