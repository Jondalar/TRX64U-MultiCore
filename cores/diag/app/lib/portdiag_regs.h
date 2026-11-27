// The register block of the diag core (T175, T201, T259) as the application uses it; the step words are the probe engine's (cores/diag/rtl/probe.v).
#ifndef COREAPP_PORTDIAG_REGS_H
#define COREAPP_PORTDIAG_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A cores/diag/app/gateware.toml); the offset CoreIo rd/wr takes is the address minus the address of the
// block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define PD_OFS(addr)    ((addr) - DRV_ident_ADDR)

#define PD_ID           PD_OFS(DRV_ident_id_ADDR)   // "DIAG", byte 0 first
#define PD_CAPS         PD_OFS(DRV_ident_dcaps_ADDR)   // DCAPS: b0 joystick pins open drain, b1 PADDLE_DIS / PADDLE_ADJ driven by the engine, b2 A/V encoder, b3 pin bank v2, b4 capture
#define PD_CAP_PINS     0x01
#define PD_CAP_PADDLE   0x02
#define PD_CAP_AV       0x04           // T201: the A/V test encoder (the registers below)
#define PD_CAP_BANK     0x08           // T260: the pin table ROM and DRIVE by index
#define PD_CAP_CAPTURE  0x10           // T260: the capture ring
#define PD_TICKS        PD_OFS(DRV_ident_ticks_us_x256_ADDR)          // 00C-00F, 32 bit little endian: clock ticks per microsecond x 256
#define PD_CTRL         PD_OFS(DRV_engine_ctrl_ADDR)   // W: b0 RUN (edge), b1 ABORT, b2 GO, b3 CLEAR FIFO, b4 CAP RESET
#define PD_CTRL_RUN     0x01
#define PD_CTRL_ABORT   0x02
#define PD_CTRL_GO      0x04
#define PD_CTRL_CLEAR   0x08
#define PD_CTRL_CAPRST  0x10
#define PD_STATUS       PD_OFS(DRV_engine_status_ADDR)   // R: b0 running, b1 waiting for GO, b2 done, b3 FIFO overflow, b4 step error, b5 a step was refused, b6 capture sampling, b7 guard trip
#define PD_ST_RUNNING   0x01
#define PD_ST_WAITGO    0x02
#define PD_ST_DONE      0x04
#define PD_ST_OVERFLOW  0x08
#define PD_ST_STEPERR   0x10
#define PD_ST_REFUSED   0x20
#define PD_ST_CAPTURING 0x40
#define PD_ST_GUARD     0x80
#define PD_PC           PD_OFS(DRV_engine_pc_ADDR)   // 016: the engine's step index (8 bit)
#define PD_REF_PC       PD_OFS(DRV_engine_ref_pc_ADDR)   // 017: the step index of the first refused step since RUN (a two word op: its second word)
#define PD_REF_CNT      PD_OFS(DRV_engine_ref_cnt_ADDR)   // 018: the refused steps since RUN, saturating at 255
#define PD_STEP         PD_OFS(DRV_engine_step_data_ADDR)   // 020-023: one step word, byte 0 first; the write of 023 stores it and advances the address
#define PD_STEP_ADDR    PD_OFS(DRV_engine_step_addr_ADDR)   // the step write address; the app writes 0 before it loads
#define PD_FIFO         PD_OFS(DRV_engine_fifo_data_ADDR)   // 030-033: the word at the head, byte 0 first; the read of 033 pops it
#define PD_FIFO_LEVEL   PD_OFS(DRV_engine_fifo_level_ADDR)   // 034-035, 16 bit little endian
#define PD_SNAP         PD_OFS(DRV_live_snap_ADDR)   // 040-04F: the snapshot of every pin of the bank, 128 bit, byte k = pins 8k+7..8k (bit = the pin's index in the table)
#define PD_SNAP_BYTES   16
#define PD_DRVEN        PD_OFS(DRV_drven_drven_ADDR)   // 070-07F: a bit per pin index, the engine drives or overrides this pin now
#define PD_PIN_COUNT    PD_OFS(DRV_pinbank_count_ADDR)   // 080: the pins of the bank
#define PD_GUARD        PD_OFS(DRV_pinbank_guard_ADDR)   // 081: b0 GAME or EXROM is low now
#define PD_PIN_IDX      PD_OFS(DRV_pinsel_idx_ADDR)   // 0A0: the pin whose table entry PD_PIN_ROM shows
#define PD_PIN_ROM      PD_OFS(DRV_pinrom_rom_ADDR)   // 0C0-0DF: 32 bytes of that entry (0-15 name, 16 dir, 17 class, 18 flags, 19 group, 20 idle)
#define PD_CAP_SEL      PD_OFS(DRV_capsel_sel_ADDR)   // 100-11F: the pin index of capture lane j, 0x7F = unused
#define PD_CAP_STATUS   PD_OFS(DRV_capst_status_ADDR)   // 120: b0 sampling, b1 all lines written, b2 overrun, b3 reader busy, b4 reader data valid
#define PD_CAP_ST_SAMPLING 0x01
#define PD_CAP_ST_WRITTEN  0x02
#define PD_CAP_ST_OVERRUN  0x04
#define PD_CAP_ST_VALID    0x10
#define PD_CAP_COUNT    PD_OFS(DRV_capcnt_count_ADDR)   // 124-127: samples taken since CAP RESET
#define PD_CAP_LOST     PD_OFS(DRV_capcnt_lost_ADDR)   // 128-12B: samples lost to overrun since CAP RESET
#define PD_CAP_LINES    PD_OFS(DRV_capcnt_lines_ADDR)   // 12C-12E: 16 byte lines of the ring used since CAP RESET (20 bit)
#define PD_CAP_RADDR    PD_OFS(DRV_capraddr_raddr_ADDR)   // 130-132: the ring byte offset the reader takes (bits 3:0 read 0)
#define PD_CAP_RCTRL    PD_OFS(DRV_capctl_rctrl_ADDR)   // 134 W: b0 read the 16 bytes at CAP_RADDR into CAP_RDATA
#define PD_CAP_RDATA    PD_OFS(DRV_caprd_rdata_ADDR)   // 140-14F: the 16 bytes of the last read line

// T201: the A/V test encoder (gateware cores/diag/rtl/diag_regs.v 050-05F), all read/write and live. Reset values are the C128's PAL values.
#define PD_AV_STD       PD_OFS(DRV_av_std_ADDR)   // 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50 (the sys_clk must be that System Mode's: the application sets both)
#define PD_AV_PERM      PD_OFS(DRV_av_perm_ADDR)   // 0..5: which of Y, C, CVBS goes to the groups A, B, C (5 = the C128's: CVBS on A, C on B, Y on C)
#define PD_AV_REV       PD_OFS(DRV_av_rev_ADDR)   // bit n: the bit order of group n (A, B, C) reversed
#define PD_AV_VINV      PD_OFS(DRV_av_vinv_ADDR)   // b0: the V sign inverted; b1: the marker stands still (freeze, set by the test walk while a question is open)
#define PD_AV_TRIM      PD_OFS(DRV_av_trim_ADDR)   // signed: subcarrier trim in steps of 8 x f_clk / 2^26 (about 3.9 Hz)
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

// The step words are dtv2.h's (dv2_*): the layout of the gateware's register map v2, docs/specs/T260 section 5.

#endif
