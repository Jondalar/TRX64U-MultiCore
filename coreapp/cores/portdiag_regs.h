// The register block of the diag core (T175 section 3 D3) as the application uses it: the offsets and step words of the gateware's spec section 5 (branch
// t175-diag-gw), which moved some registers of the section 3 table. A difference in either place is reported to the coordinator, never patched here.
#ifndef COREAPP_PORTDIAG_REGS_H
#define COREAPP_PORTDIAG_REGS_H

#define PD_ID           0x000          // "DIAG", byte 0 first
#define PD_CAPS         0x006          // DCAPS (gateware spec 5): b0 the pins are drivable by the top, b1 PADDLE_DIS / PADDLE_ADJ
#define PD_CAP_PINS     0x01
#define PD_CAP_PADCTL   0x02
#define PD_CAP_AV       0x04           // T201: the A/V test encoder (the registers below)
#define PD_TICKS        0x00C          // 00C-00F, 32 bit little endian: clock ticks per microsecond x 256
#define PD_CTRL         0x012          // W: b0 RUN (edge), b1 ABORT, b2 GO, b3 CLEAR FIFO
#define PD_CTRL_RUN     0x01
#define PD_CTRL_ABORT   0x02
#define PD_CTRL_GO      0x04
#define PD_CTRL_CLEAR   0x08
#define PD_STATUS       0x011          // R: b0 running, b1 waiting for GO, b2 done, b3 FIFO overflow, b4 step RAM error
#define PD_ST_RUNNING   0x01
#define PD_ST_WAITGO    0x02
#define PD_ST_DONE      0x04
#define PD_ST_OVERFLOW  0x08
#define PD_ST_STEPERR   0x10
#define PD_PC           0x016          // 016-017, 16 bit little endian
#define PD_STEP         0x020          // 020-023: one step word, byte 0 first; the write of 023 stores it and advances the address
#define PD_STEP_ADDR    0x024          // the step write address; the app writes 0 before it loads
#define PD_FIFO         0x030          // 030-033: the word at the head, byte 0 first; the read of 033 pops it
#define PD_FIFO_LEVEL   0x034          // 034-035, 16 bit little endian
#define PD_LIVE         0x040          // 040-041, 16 bit little endian: {PADDLE_CMP[3:0], KBJOY_2[4:0], KBJOY_1[4:0]}, two flip-flop synchronised

// T201: the A/V test encoder (gateware cores/diag/rtl/diag_regs.v 050-05F), all read/write and live. Reset values are the C128's PAL values.
#define PD_AV_STD       0x050          // 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50 (the sys_clk must be that System Mode's: the application sets both)
#define PD_AV_PERM      0x051          // 0..5: which of Y, C, CVBS goes to the groups A, B, C (5 = the C128's: CVBS on A, C on B, Y on C)
#define PD_AV_REV       0x052          // bit n: the bit order of group n (A, B, C) reversed
#define PD_AV_VINV      0x053          // b0: the V sign inverted; b1: the marker stands still (freeze, set by the test walk while a question is open)
#define PD_AV_TRIM      0x054          // signed: subcarrier trim in steps of 64 x f_clk / 2^26 (about 31 Hz)
#define PD_AV_BSTART    0x055          // burst window start, samples after the sync's trailing edge
#define PD_AV_BEND      0x056          // 056-057, 10 bit little endian: the window's end
#define PD_AV_BAMP      0x058          // burst amplitude, 32 = the C128's
#define PD_AV_GAIN      0x059          // chroma gain x 16
#define PD_AV_SYNC      0x05A          // sync tip level (8 bit code)
#define PD_AV_BLANK     0x05B          // blanking level
#define PD_AV_WHITE     0x05C          // white level, x 16 of the luma (11 = the C128's)
#define PD_AV_SYNCB     0x05D          // b0: equalizing + serrated vsync (0: broad pulses at the line rate); b1: 262 lines per frame at the 60 Hz standards (0: 263)
#define PD_AV_VLEN      0x05E          // vsync length (each block's with equalizing) in half lines: 6 = 3 lines (reset), 5 = 2.5, 12 = 6
#define PD_AV_VPOS      0x05F          // signed: the vsync's start in lines against the default (broad pulses from line 244 of the picture at 60 Hz, 280 at 50 Hz)

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
