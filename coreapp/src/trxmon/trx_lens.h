/*
 * trx_lens.h - memory through a lens on the TRX64 Ultimate bitstream
 * (TRX64-Ultimate docs/specs/T21-monitor.md §3.4, §3.4.1).
 *
 * One piece of code for every reader: the firmware's REST machine:readmem
 * (trx_lens_fw.cc) and trxmon's mach_peek, m, d and session/read_memory
 * (apps/trxmon/src/mach.c) compile this file and hand it the few hardware
 * operations as a table. Plain C, no library beyond stdint.
 *
 * Lenses: "cpu" the PLA as the machine has it now, "ram", "rom", "io",
 * "cart" ROML/ROMH as the cartridge answers, "drive8".
 */
#ifndef TRX_LENS_H
#define TRX_LENS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LK_RES (trx_sys page 5, freeze_resp.v): the answer of the lens unit */
#define LK_DATA(r)    ((uint8_t)(r))
#define LK_DONE       0x00000100u
#define LK_HIT        0x00000200u /* the cartridge or the REU drives this read */
#define LK_EXT        0x00000400u /* the real port serves it: take a REAL cycle */
#define LK_REAL       0x00000800u /* the data came from a bus cycle */
#define LK_REFUSED    0x00001000u /* REAL, and the machine is not paused */
#define LK_BUSY       0x80000000u

typedef struct
{
    /* state-bus words (chip, word) */
    void (*sb_block)(int chip, int word, uint32_t *dst, int n);
    /* RAM without a bus cycle */
    void (*ram_read)(uint16_t a, uint8_t *d, int n);
    /* the ROM block: char 0x0000, KERNAL 0x2000, BASIC 0x4000 */
    uint8_t (*rom)(uint16_t off);
    /* drive A as its CPU reads it, -1 for the VIAs */
    int (*drive_peek)(uint16_t a);
    /* The lens unit: a look (real 0) or one bus cycle with the select forced
     * (real 1) at address a, class 0 ROML, 1 ROMH, 2 IO1/I-O, 3 IO2/I-O;
     * returns LK_RES. NULL on a bitstream without the unit (CAPS2 b4). */
    uint32_t (*lk)(uint16_t a, int cls, int real);
    /* Old bitstreams: a bus cycle through the window while the menu's
     * C64_STOP holds the machine; -1 when it is not held. May be NULL. */
    int (*bus_read)(uint16_t a);
    /* The colour RAM through the shadow session's window (T12 §14.8: a debugger stop is a freeze
     * session, nothing reads it without a bus cycle otherwise): the byte, or -1 when no session
     * answers. May be NULL. */
    int (*session_read)(uint16_t a);
} lens_hw_t;

/* What the PLA decides, and the banking words the lenses need, taken once
 * per request */
typedef struct
{
    uint8_t vic[48];
    uint8_t cia1[16], cia2[16];
    uint8_t pa_out, ddra;
    uint8_t ddr, latch, port;
    uint8_t game, exrom, cart; /* the cartridge's lines (1 = high) and present */
    uint8_t cart_reg0, cart_reg1;
} lens_state_t;

enum { R_RAM, R_BASIC, R_KERNAL, R_CHAR, R_IO, R_ROML, R_ROMH, R_OPEN };

/* Takes the state-bus words (VIC, CIA 1/2, $01, GAME/EXROM, cartridge registers) */
void lens_snapshot(const lens_hw_t *hw);
const lens_state_t *lens_state(void);

/* The region of an address as the CPU reads it now (R_*) */
int lens_pla(uint16_t a);
int lens_valid(const char *lens);

/* One byte through a lens; -1 when the lens cannot reach it without a bus
 * cycle it may not make (SID, colour RAM, the real port while the machine
 * runs, IO1/IO2 of a real cartridge without sidefx). sidefx: I/O reads are
 * bus cycles (real side effects) and need the machine paused. */
int lens_peek(const lens_hw_t *hw, const char *lens, uint16_t a, int sidefx);

/* len bytes through a lens, wrapping at $FFFF like the 6510, unreadable
 * bytes as 0; returns how many were unreadable. Takes its own snapshot. */
int lens_read(const lens_hw_t *hw, const char *lens, uint16_t a, uint8_t *dst, uint32_t len, int sidefx);

#ifdef __cplusplus
}
#endif

#endif
