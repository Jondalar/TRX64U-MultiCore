// The TRX64 Ultimate bitstream's IO block (TRX64-Ultimate designs/c64_basic/trx_io.v,
// docs/specs/ARCH-lean-state-access.md §10, §15.3): detection, the 32-bit register
// rule and the one interrupt line all its users share.
//
// Only the U64-II/C64U application builds this (trx_cart.cc, trx_video.cc). On
// Gideon's bitstreams the block reads 0: trx_io_caps() is 0 and nothing here
// touches the hardware.
#ifndef TRX_IO_H
#define TRX_IO_H

#include <stdint.h>
#include "iomap.h"

#define TRX_IO_BASE        (IOBASE + 0x1A0000)
#define TRX_ID             (TRX_IO_BASE + 0x000)
#define TRX_CAPS           (TRX_IO_BASE + 0x004)
#define TRX_CAPS2          (TRX_IO_BASE + 0x005)
#define TRX_CAPS3          (TRX_IO_BASE + 0x006)
#define TRX_IRQ_EN         (TRX_IO_BASE + 0x010)
#define TRX_IRQ_CAUSE      (TRX_IO_BASE + 0x014)

// CAPS
#define TRX_CAPS_GATE      0x01 // page 0 from 0x100: gate and capture control
#define TRX_CAPS_STATEBUS  0x02
#define TRX_CAPS_CART      0x10
#define TRX_CAPS_FREEZE    0x20 // page 5: freeze session responder
#define TRX_CAPS_VIDEO     0x40 // page 6: pixel layer and still capture
#define TRX_CAPS_PX565     0x80 // the pixel layer is RGB565 (PX_CTRL format 1, PX_KEY)
// CAPS2 (older bitstreams read 0)
#define TRX_CAPS2_F4       0x01 // the drain writes stream 2's F4 only after an F3 marker
#define TRX_CAPS2_APPLY    0x20 // b5: the engine applies stream 0 records itself (AP_START/AP_COUNT/AP_CMD)
#define TRX_CAPS2_TURBO    0x40 // b6: the turbo engine (T29): TURBO_STAT, TURBO_W1/W2
#define TRX_CAPS2_SESSION  0x08 // b3: freeze session commit list, SH_CTRL PROTO/SIDEFX, TX_CTRL b0 = show (gone on a slim core)
#define TRX_CAPS2_TRACE    0x80 // b7: the instruction trace former (LOG_CTRL b4/b9, LOG_CNT[3], T21 §4.11.1)

// CAPS3 (bitstreams without the byte read 0, Gideon's too; TRX64-Ultimate docs/specs/T25.5-resid8-menus.md)
#define TRX_CAPS3_RESID8   0x01 // b0: the SID is the 8-SID reDIP (reSID8) with the filter control registers below

// reSID8 filter control, page 0 0x040-0x04B (engine 1 / 2). Reset values (Auto, 245, -780) are the original reDIP.
#define TRX_RS8_FCSEL(u)   (TRX_IO_BASE + 0x040 + (u))        // b1:0 0 the chip model's own filter, 1 own 6581 curve, 2 own 8580, 3 reserved (acts as 2)
#define TRX_RS8_AUTO(u)    (TRX_IO_BASE + 0x042 + (u))        // b0 resonance Auto, b1 digis Auto (follow the chip model)
#define TRX_RS8_BASE(u)    (TRX_IO_BASE + 0x044 + 4 * (u))    // 9 bit, little endian: 6581 curve base
#define TRX_RS8_OFFSET(u)  (TRX_IO_BASE + 0x046 + 4 * (u))    // 11 bit, little endian, signed: 6581 curve offset
#define TRX_RS8_BYPASS(u)  (TRX_IO_BASE + 0x04C + (u))        // b0 1: the engine's filter is bypassed (reSID enable_filter(false))
#define TRX_RS8_SRC_MODEL  0
#define TRX_RS8_SRC_6581   1
#define TRX_RS8_SRC_8580   2

// T29 turbo engine, page 0 from 0x140 (TRX64-Ultimate docs/specs/T29-turbo.md §12.7)
#define TRX_TURBO_STAT     (TRX_IO_BASE + 0x140) // b30 engine on, b28 ran since cleared, b22:16 CPU cycles per PHI2
#define TRX_TURBO_CTRL     (TRX_IO_BASE + 0x144) // W b0 clears "ran"
#define TRX_TURBO_W1       (TRX_IO_BASE + 0x148) // {$D031 b31:24, $D030 bit 0 b11, enable word b10:8, PREFER b7:0}
#define TRX_TURBO_W2       (TRX_IO_BASE + 0x14C) // reset hold left in PHI2 cycles
#define TRX_TURBO_ON       (1u << 30)
#define TRX_TURBO_RAN      (1u << 28)

// IRQ_EN / IRQ_CAUSE (write 1 to clear)
#define TRX_IRQ_STOP       0x01
#define TRX_IRQ_CAPTURE    0x08
#define TRX_IRQ_FLASH      0x10
#define TRX_IRQ_EEPROM     0x20
#define TRX_IRQ_VSYNC      0x40
#define TRX_IRQ_FLIP       0x80

// CAPS of the block, 0 without it ("TRX2" not at ID). Read once, then cached.
uint8_t trx_io_caps(void);

// The slim core (TRX64-Ultimate docs/specs/SLIM-state-path.md): TX_CTRL b31:24 reads 2. It has no freeze
// session: SH_CTRL, SH_STATUS and the counters of page 5 read 0, the app's accesses to a stopped machine are
// bus cycles, the C64 text layer takes its VIC view from TX_VIEW (page 5, +028), the menu reads the keyboard
// and the joysticks over KEYB_COL / KEYB_ROW / KEYB_JOY. False on every other bitstream.
bool trx_io_slim(void);

// CAPS3 of the block, 0 without it or without the byte. Read once, then cached.
uint8_t trx_io_caps3(void);

// The SID of this bitstream is the 8-SID reDIP (reSID8): the app names it so and uses its filter control.
// False on Gideon's bitstreams (UltiSID) and on ours without that engine.
bool trx_io_resid8(void);

// Programs the filter control of engine u (0 or 1): source (TRX_RS8_SRC_*), Auto bits, 6581 curve base and offset.
void trx_io_resid8_filter(int u, uint8_t src, uint8_t auto_bits, int base, int offset);

// Filter Off for engine u: all voices straight to the mixer, FC / RES / routing / filter mode bits without effect, volume applies.
void trx_io_resid8_bypass(int u, bool bypass);

// The 32-bit registers of pages 0 (from 0x100), 1, 5 and 6 over the byte bus:
// writing byte 3 commits bytes 0-2, reading byte 0 takes bytes 1-3. The four byte
// accesses of one register are one critical section: the block keeps the bytes of
// a word and its latch in a buffer of its own, and a task switch between two byte
// accesses of one master let another master's accesses in (T16 §13.11: the lens
// read $00, the policy of the write gate was the third master that tore it).
// trx_io_lock()/unlock() nest; a sequence of several registers that shares state
// in the block (SB_ADDR, then SB_DATA reads) takes the lock around all of them.
uint32_t trx_io_read32(uint32_t addr);
void trx_io_write32(uint32_t addr, uint32_t v);
void trx_io_lock(void);
void trx_io_unlock(void);

// The "trx64" object of the ident service (UDP 64) and GET /v1/info, so a
// client can tell this core from a stock one (T21-monitor.md §7.4): core (the
// ID register), caps (CAPS), board (C64U or U64-II) and, while an app serves
// the TRX64 runtime contract (trxmon), rpc (its TCP port). NULL without the
// IO block. There is no register with the bitstream's build yet, so the
// spec's "build" field is left out.
class JSON_Object;
JSON_Object *trx_io_ident(void);

// The firmware is about to stop the C64 for itself (C64::stop, hard_stop:
// the menu's freeze, a DMA load, a reset). If a debugger app holds run control
// (trx_dbg.v OWNER), C64_STOP would not stop the machine: OWNER is cleared and
// the debugger's hold dropped, so C64_STOP rules again. The app sees OWNER gone
// and takes run control back when it next needs it.
#if (U64 == 2) && !defined(RECOVERYAPP)
void trx_dbg_yield(void);
#else
static inline void trx_dbg_yield(void) { }
#endif

// Interrupt (ITU high interrupt 2). A handler is called from the ISR with the active
// causes it registered for (IRQ_CAUSE & IRQ_EN & causes), clears them itself
// and returns non-zero when a task switch is due.
typedef uint8_t (*trx_irq_handler_t)(uint8_t active);

// Registers h for these cause bits (up to four handlers) and takes the line.
void trx_irq_attach(uint8_t causes, trx_irq_handler_t h);

// Sets or clears these bits of IRQ_EN, leaving the others as they are.
void trx_irq_enable(uint8_t causes, bool on);

#endif
