// The diag core's engine v2, the app's side (T260 P1-P3, S4 reader): the pin table read from the bitstream's ROM, the step words of v2, the capture ring read back. The register map is the gateware's
// (docs/specs/T260 §5 "Register map v2", cores/diag/rtl/diag_regs.v): the offsets below are numbers until gateware.toml (TOML A) names them (open: the TOML A of v2 and its rtl_check line).
// Pure: the registers are a CoreIo, so tests/coreapp/test_dtv2.cc runs it all against a model of the register block.
#ifndef DIAG_DTV2_H
#define DIAG_DTV2_H

#include <stdint.h>
#include "io.h"

// ---- registers (core window page 0, byte offsets)
#define DV2_PIN_COUNT   0x080         // R: the number of pins of the bank
#define DV2_GUARD       0x081         // R: b0 GAME or EXROM is low now
#define DV2_PIN_IDX     0x0A0         // RW: the pin whose ROM entry DV2_PIN_ROM shows
#define DV2_PIN_ROM     0x0C0         // R: 32 bytes
#define DV2_CAP_STATUS  0x120         // R: b0 sampling, b1 all lines written, b2 overrun, b3 reader busy, b4 reader data valid
#define DV2_CAP_COUNT   0x124         // R: samples taken since CAP RESET (32 bit)
#define DV2_CAP_LOST    0x128         // R: samples lost to overrun (32 bit)
#define DV2_CAP_LINES   0x12C         // R: lines written to the ring (32 bit)
#define DV2_CAP_RADDR   0x130         // RW: byte offset into the ring (32 bit, bits 3:0 read 0)
#define DV2_CAP_RCTRL   0x134         // W: b0 read the 16 bytes at CAP_RADDR into CAP_RDATA
#define DV2_CAP_RDATA   0x140         // R: 16 bytes
#define DV2_CAP_SEL     0x100         // RW: the pin index of capture lane j (7 bit), 0x7F = unused
#define DV2_CAP_ST_SAMPLING 0x01
#define DV2_CAP_ST_WRITTEN  0x02
#define DV2_CAP_ST_OVERRUN  0x04
#define DV2_CAP_ST_VALID    0x10
#define DV2_CAP_LANES   32

// ---- the pin table (the ROM: name, direction, class, flags, group, idle)
#define DV2_PINS_MAX    128
struct DtPin {
    char name[16];
    uint8_t dir, cls, flags, group, idle;      // dir 0 in, 1 out, 2 inout; class 0 none .. 5 pzh; flags b0 guard b1 inv b2 buf b3 pwr b4 gen b5 host; idle 0, 1, 2
};
struct DtPins {
    int n;
    DtPin p[DV2_PINS_MAX];
};
// Reads the whole table from the core; 0, else -1 (the count is 0 or more than DV2_PINS_MAX).
int dtv2_pins_read(CoreIo *io, DtPins *out);
// The index of a pin by its name (as the table spells it), -1 when the bitstream has none.
int dtv2_pin_find(const DtPins &t, const char *name);

// ---- the step words of v2 (one word unless said)
#define DV2_OP_END       0
#define DV2_OP_DRIVE     1
#define DV2_OP_WAIT      2
#define DV2_OP_SAMPLE    3
#define DV2_OP_EDGES     4
#define DV2_OP_TTE       5
#define DV2_OP_LOOP      7
#define DV2_OP_WAIT_USER 8
#define DV2_OP_WRITE_REG 9
#define DV2_OP_CAPTURE   10
#define DV2_OP_CAPWAIT   11
enum { DV2_REL = 0, DV2_LOW = 1, DV2_HIGH = 2, DV2_HIZ = 3 };           // DRIVE modes
enum { DV2_ANY = 0, DV2_RISING = 1, DV2_FALLING = 2 };                  // EDGES / TIME_TO_EDGE edges
static inline uint32_t dv2_end(void) { return 0; }
static inline uint32_t dv2_drive(int mode, int pin) { return (1u << 28) | ((uint32_t)(mode & 3) << 26) | (uint32_t)(pin & 0x7F); }
static inline uint32_t dv2_wait(uint32_t ticks) { return (2u << 28) | (ticks & 0xFFFFFF); }
static inline uint32_t dv2_sample(void) { return 3u << 28; }
static inline uint32_t dv2_edges(int edge, int pin) { return (4u << 28) | ((uint32_t)(edge & 3) << 26) | (uint32_t)(pin & 0x7F); }       // then a word: the ticks
static inline uint32_t dv2_tte(int edge, int pin) { return (5u << 28) | ((uint32_t)(edge & 3) << 26) | (uint32_t)(pin & 0x7F); }         // then a word: the timeout ticks
static inline uint32_t dv2_loop(uint32_t count, uint32_t words) { return (7u << 28) | ((count & 0xFFFF) << 8) | (words & 0xFF); }
static inline uint32_t dv2_wait_user(uint32_t prompt) { return (8u << 28) | (prompt & 0xFFFFFF); }
static inline uint32_t dv2_write_reg(uint32_t ofs, uint32_t data) { return (9u << 28) | ((ofs & 0xFFF) << 16) | (data & 0xFF); }
static inline uint32_t dv2_capture(uint32_t every_ticks) { return (10u << 28) | (every_ticks & 0xFFFFFF); }                                // then a word: the samples
static inline uint32_t dv2_capwait(void) { return 11u << 28; }

// ---- the capture ring
// The lines (16 bytes, four samples) the ring holds for `samples` samples: a following capture starts on the next line.
static inline uint32_t dtv2_cap_lines(uint32_t samples) { return (samples + 3) / 4; }
// Reads `n` samples that start at ring line `first_line` into out[]: the register protocol of CAP_RADDR / CAP_RCTRL / CAP_RDATA, 16 bytes at a time. false when the reader does not answer.
bool dtv2_cap_read(CoreIo *io, uint32_t first_line, uint32_t n, uint32_t *out);
// A sample is the 32 bit word of one 16 byte line at byte 4 * k (little endian).
static inline uint32_t dtv2_cap_decode(const uint8_t line[16], int k)
{
    return (uint32_t)line[4 * k] | ((uint32_t)line[4 * k + 1] << 8) | ((uint32_t)line[4 * k + 2] << 16) | ((uint32_t)line[4 * k + 3] << 24);
}

#endif
