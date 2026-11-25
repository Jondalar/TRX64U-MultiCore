// The register block of the diag core (T175, T201, T259) as the application uses it; the step words are the probe engine's (cores/diag/rtl/probe.v).
#ifndef COREAPP_PORTDIAG_REGS_H
#define COREAPP_PORTDIAG_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A cores/diag/app/gateware.toml); the offset CoreIo rd/wr takes is the address minus the address of the
// block "ident", which sits at the start of the core's window. PD_TICKS (00C-00F) is a module parameter of the RTL that rtl_check does not read, so it is a number here.
#include "drvtab.h"
#define PD_OFS(addr)    ((addr) - DRV_ident_ADDR)

#define PD_ID           PD_OFS(DRV_ident_id_ADDR)   // "DIAG", byte 0 first
#define PD_CAPS         PD_OFS(DRV_ident_dcaps_ADDR)   // DCAPS: b0 the pins are drivable by the top, b1 PADDLE_DIS / PADDLE_ADJ
#define PD_CAP_PINS     0x01
#define PD_CAP_PADCTL   0x02
#define PD_CAP_AV       0x04           // T201: the A/V test encoder (the registers below)
#define PD_TICKS        0x00C          // 00C-00F, 32 bit little endian: clock ticks per microsecond x 256
#define PD_CTRL         PD_OFS(DRV_engine_ctrl_ADDR)   // W: b0 RUN (edge), b1 ABORT, b2 GO, b3 CLEAR FIFO
#define PD_CTRL_RUN     0x01
#define PD_CTRL_ABORT   0x02
#define PD_CTRL_GO      0x04
#define PD_CTRL_CLEAR   0x08
#define PD_STATUS       PD_OFS(DRV_engine_status_ADDR)   // R: b0 running, b1 waiting for GO, b2 done, b3 FIFO overflow, b4 step RAM error
#define PD_ST_RUNNING   0x01
#define PD_ST_WAITGO    0x02
#define PD_ST_DONE      0x04
#define PD_ST_OVERFLOW  0x08
#define PD_ST_STEPERR   0x10
#define PD_PC           PD_OFS(DRV_engine_pc_ADDR)   // 016: the engine's step index (8 bit; 017 reads 0)
#define PD_STEP         PD_OFS(DRV_engine_step_data_ADDR)   // 020-023: one step word, byte 0 first; the write of 023 stores it and advances the address
#define PD_STEP_ADDR    PD_OFS(DRV_engine_step_addr_ADDR)   // the step write address; the app writes 0 before it loads
#define PD_FIFO         PD_OFS(DRV_engine_fifo_data_ADDR)   // 030-033: the word at the head, byte 0 first; the read of 033 pops it
#define PD_FIFO_LEVEL   PD_OFS(DRV_engine_fifo_level_ADDR)   // 034-035, 16 bit little endian
#define PD_LIVE         PD_OFS(DRV_live_live_ADDR)   // 040-041, 16 bit little endian: {PADDLE_CMP[3:0], KBJOY_2[4:0], KBJOY_1[4:0]}, two flip-flop synchronised

// T201: the A/V test encoder (gateware cores/diag/rtl/diag_regs.v 050-05F), all read/write and live. Reset values are the C128's PAL values.
#define PD_AV_STD       PD_OFS(DRV_av_std_ADDR)   // 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50 (the sys_clk must be that System Mode's: the application sets both)
#define PD_AV_PERM      PD_OFS(DRV_av_perm_ADDR)   // 0..5: which of Y, C, CVBS goes to the groups A, B, C (5 = the C128's: CVBS on A, C on B, Y on C)
#define PD_AV_REV       PD_OFS(DRV_av_rev_ADDR)   // bit n: the bit order of group n (A, B, C) reversed
#define PD_AV_VINV      PD_OFS(DRV_av_vinv_ADDR)   // b0: the V sign inverted; b1: the marker stands still (freeze, set by the test walk while a question is open)
#define PD_AV_TRIM      PD_OFS(DRV_av_trim_ADDR)   // signed: subcarrier trim in steps of 64 x f_clk / 2^26 (about 31 Hz)
#define PD_AV_BSTART    PD_OFS(DRV_av_bstart_ADDR)   // burst window start, samples after the sync's trailing edge
#define PD_AV_BEND      PD_OFS(DRV_av_bend_ADDR)   // 056-057, 10 bit little endian: the window's end
#define PD_AV_BAMP      PD_OFS(DRV_av_bamp_ADDR)   // burst amplitude, 32 = the C128's
#define PD_AV_GAIN      PD_OFS(DRV_av_gain_ADDR)   // chroma gain x 16
#define PD_AV_SYNC      PD_OFS(DRV_av_sync_ADDR)   // sync tip level (8 bit code)
#define PD_AV_BLANK     PD_OFS(DRV_av_blank_ADDR)   // blanking level
#define PD_AV_WHITE     PD_OFS(DRV_av_white_ADDR)   // white level, x 16 of the luma (11 = the C128's)
#define PD_AV_SYNCB     PD_OFS(DRV_av_syncb_ADDR)   // b0: equalizing + serrated vsync (0: broad pulses at the line rate); b1: 262 lines per frame at the 60 Hz standards (0: 263)
#define PD_AV_VLEN      PD_OFS(DRV_av_vlen_ADDR)   // vsync length (each block's with equalizing) in half lines: 6 = 3 lines (reset), 5 = 2.5, 12 = 6
#define PD_AV_VPOS      PD_OFS(DRV_av_vpos_ADDR)   // signed: the vsync's start in lines against the default (broad pulses from line 244 of the picture at 60 Hz, 280 at 50 Hz)

// T259: the channel map, the channel probe, the RGB mode and the two pins (cores/diag/rtl/diag_regs.v 060-065, one byte each, b1:0, read/write, live, reset 0 = the T201 behaviour)
#define PD_AV_MAP       PD_OFS(DRV_av_map_ADDR)   // 060 0 scan groups, 1 names (a word per AVID_R, AVID_G, AVID_B)   061 mode: 0 test picture, 1 channel probe, 2 RGB   062 the probe's channel 0..2
//                                        063 the RGB picture: 0 palette, 1 / 2 / 3 the R / G / B word as a ramp   064 SYNCn: 0 high, 1 low, 2 csync, 3 inverted csync   065 FB: 0 low, 1 high, 2 window, 3 inverted window

#define PD_STEPS_MAX    256
#define PD_FIFO_MAX     512

// The live vector and the pin numbers in it. The port pins are the joystick inputs of the top (up, down, left, right, fire = pins 1, 2, 3, 4, 6; low =
// pressed or driven), bit n of the 10 bit pin mask is bit n of the live vector.
#define PD_PINS         10
#define PD_CMP_SHIFT    10
#define PD_CMPS         4
#define PD_LIVE_BITS    14

// The step word, [31:28] op, [27:24] sel, [23:0] arg, as the gateware's spec section 5 fixes it:
//   DRIVE         arg[9:0] the pins, arg[19:10] the level per pin (0 drive low, 1 release); put a WAIT of 1 between a DRIVE and the SAMPLE that is to see it
//   WAIT          arg the clocks
//   SAMPLE        pushes {t[15:0], pins[9:0], cmp[3:0], twrap, lost}
//   MASK          arg[13:0] the pins EDGES counts (reset to 0x3FFF at RUN)
//   EDGES         arg the clocks; pushes the count of the masked pins' edges
//   TIME_TO_FLIP  sel the CMP bits to watch, arg the timeout in clocks; pushes {timeout, 7'b0, clocks[23:0]}
//   PADCTL        arg[0] DIS level, [1] DIS enabled, [2] ADJ level, [3] ADJ enabled
//   LOOP          arg[7:0] K (the following steps), arg[23:8] C (repeats)
//   WAIT_USER     arg the prompt number; the engine halts until GO and PC then points at this step
enum { PD_OP_END = 0, PD_OP_DRIVE = 1, PD_OP_WAIT = 2, PD_OP_SAMPLE = 3, PD_OP_EDGES = 4, PD_OP_TIME_TO_FLIP = 5, PD_OP_PADCTL = 6, PD_OP_LOOP = 7, PD_OP_WAIT_USER = 8, PD_OP_MASK = 9 };
#define PD_STEP_WORD(op, sel, arg) ((uint32_t)(((uint32_t)(op) << 28) | (((uint32_t)(sel) & 0xF) << 24) | ((uint32_t)(arg) & 0xFFFFFF)))
#define PD_ARG_MAX      0xFFFFFFu

#endif
