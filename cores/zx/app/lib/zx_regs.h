// The ZX Spectrum core's register block as the application uses it (T93 D15). The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/zx/app/gateware.toml and
// the blocks of the base that sdk.toml names); the offset the core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at
// the start of the core's window. Source of the layout: cores/zx/trx/zx_regs.v and zx_win.v; the snapshot sequence is the bench's (cores/zx/sim/tb_zx.v snap_load) and tools/zx_snap.py's
// load_script. The ROM aperture and the RAM window are the DMA window of zx_win.v: rom = 1 is the 32 KB ROM area, rom = 0 the 128 KB RAM
// (WIN_PAGE b0 selects the 64 KB half, bank n is at (n & 3) * 16 KB of its half). The host's ROM aperture is 16 KB wide and its address bits
// above 13 are the C64 ROM block's own layout (ultimate_sys.vhd, r_rom), so the ROM area is written 16 KB at a time with the bank in
// WIN_PAGE b2:1 (ROM_SEL) and the aperture offsets 0 .. 0x3FFF.
#ifndef COREAPP_ZX_REGS_H
#define COREAPP_ZX_REGS_H

#include "drvtab.h"
#define ZX_OFS(addr)    ((addr) - DRV_ident_ADDR)

#define ZX_ID0          ZX_OFS(DRV_ident_id_ADDR)                     // "ZXSP"
#define ZX_CTRL         ZX_OFS(DRV_machine_ctrl_ADDR)                 // b0 reset hold, b1 halt (Freeze), b2 the tape's ear signal on the speaker
#define ZX_CTRL_HOLD    DRV_machine_ctrl_reset_hold_MASK
#define ZX_CTRL_EAR     DRV_machine_ctrl_ear_MASK
#define ZX_MODEL        ZX_OFS(DRV_machine_model_ADDR)                // b0: 0 = 48K, 1 = 128K, taken in reset
#define ZX_JOY1         ZX_OFS(DRV_machine_joy1_ADDR)                 // what the host's port 1 acts as
#define ZX_JOY2         ZX_OFS(DRV_machine_joy2_ADDR)                 // ... and port 2: 0 none, 1 Kempston, 2 Sinclair 1 (keys 6-0), 3 Sinclair 2 (keys 1-5), 4 Cursor (5 6 7 8 0)
#define ZX_STATUS       ZX_OFS(DRV_machine_status_ADDR)               // b0 in reset, b1 held (Freeze), b2 snapshot load busy, b3 128K running
#define ZX_ST_RESET     DRV_machine_status_in_reset_MASK
#define ZX_ST_HELD      DRV_machine_status_held_MASK
#define ZX_ST_SNAPBUSY  DRV_machine_status_snap_busy_MASK
#define ZX_ST_M128      DRV_machine_status_m128_MASK
#define ZX_SNAP_GO      ZX_OFS(DRV_machine_snap_go_ADDR)              // any write starts the snapshot load while CTRL.b0 holds the reset
#define ZX_FRAMES       ZX_OFS(DRV_machine_frames_ADDR)               // 16 bit
#define ZX_FB_LATEST    ZX_OFS(DRV_video_fb_latest_ADDR)              // b7 valid, b1:0 the buffer of the newest complete frame (T119)
#define ZX_FRAME_SEQ    ZX_OFS(DRV_video_frame_seq_ADDR)              // changes with every complete frame
#define ZX_FB_BASE      ZX_OFS(DRV_video_fb_base_ADDR)                // 32 bit, DDR2 byte address of buffer 0
#define ZX_FB_STRIDE    ZX_OFS(DRV_video_fb_stride_ADDR)              // 32 bit
#define ZX_WIN_PAGE     ZX_OFS(DRV_win_win_page_ADDR)                 // b0: the 64 KB half of the RAM window; b2:1: the 16 KB bank the ROM aperture shows
#define ZX_ROM_SEL(n)   ((n) << 1)
#define ZX_SNAP_DIR     ZX_OFS(DRV_snap_dir_ADDR)                     // 27 bytes: T80pa's register image
#define ZX_SNAP_7FFD    ZX_OFS(DRV_snap_p7ffd_ADDR)
#define ZX_SNAP_BORDER  ZX_OFS(DRV_snap_border_ADDR)
#define ZX_SNAP_AY      ZX_OFS(DRV_snap_ay_ADDR)                      // 16 bytes
#define ZX_SNAP_AYSEL   ZX_OFS(DRV_snap_ay_sel_ADDR)
#define ZX_TAPE_CTRL    ZX_OFS(DRV_tape_ctrl_ADDR)                    // b0 play: a rising edge starts the tape, a falling edge stops it
#define ZX_TAPE_BASE    ZX_OFS(DRV_tape_base_ADDR)                    // 32 bit DDR2 byte address of the pulse stream
#define ZX_TAPE_POS     ZX_OFS(DRV_tape_pos_ADDR)                     // 32 bit: write = the word position a seek loads, read = the position now (25 bit)
#define ZX_TAPE_SEEK    ZX_OFS(DRV_tape_seek_ADDR)                    // any write loads TAPE_POS
#define ZX_TAPE_STATUS  ZX_OFS(DRV_tape_status_ADDR)                  // b0 playing, b1 end of tape, b2 stop marker, b3 DDR2 read stuck
#define ZX_TS_PLAYING   DRV_tape_status_playing_MASK
#define ZX_TS_END       DRV_tape_status_end_MASK
#define ZX_TS_STOP      DRV_tape_status_stop_MASK
#define ZX_TS_STUCK     DRV_tape_status_stuck_MASK
#define ZX_TAPE_UNDERRUN ZX_OFS(DRV_tape_underrun_ADDR)

// sizes
#define ZX_ROM48_BYTES  16384u
#define ZX_ROM128_BYTES 32768u

// the DDR2 window (offsets from 0x01000000): the tape's pulse stream, 4 MB (TAPE_MAX_WORDS) from the core's reset value of TAPE_BASE
#define ZX_W_TAPE       0x800000u

// the picture (T119): four buffers of 344 x 280 RGB565 at the reset values of FB_BASE / FB_STRIDE (the states "reset" of those two properties)
#define ZX_FB_ADDR      DRV_video_fb_base_reset
#define ZX_FB_STRIDE_BYTES DRV_video_fb_stride_reset

#endif
