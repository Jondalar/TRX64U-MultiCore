// The ZX Spectrum core's register block as the application uses it (T93 D15). Source: cores/zx/trx/zx_regs.v and zx_win.v - the code, not
// its header comment - read on 2026-10-22; the snapshot sequence is the bench's (cores/zx/sim/tb_zx.v snap_load) and tools/zx_snap.py's
// load_script. The ROM aperture and the RAM window are the DMA window of zx_win.v: rom = 1 is the 32 KB ROM area, rom = 0 the 128 KB RAM
// (WIN_PAGE b0 selects the 64 KB half, bank n is at (n & 3) * 16 KB of its half). The host's ROM aperture is 16 KB wide and its address bits
// above 13 are the C64 ROM block's own layout (ultimate_sys.vhd, r_rom), so the ROM area is written 16 KB at a time with the bank in
// WIN_PAGE b2:1 (ROM_SEL) and the aperture offsets 0 .. 0x3FFF.
#ifndef COREAPP_ZX_REGS_H
#define COREAPP_ZX_REGS_H

#define ZX_ID0          0x000          // "ZXSP"
#define ZX_CTRL         0x0A0          // b0 reset hold, b1 halt (Freeze), b2 the tape's ear signal on the speaker
#define ZX_CTRL_HOLD    0x01
#define ZX_CTRL_EAR     0x04
#define ZX_MODEL        0x0A1          // b0: 0 = 48K, 1 = 128K, taken in reset
#define ZX_JOY1         0x0A2          // what the host's port 1 acts as
#define ZX_JOY2         0x0A3          // ... and port 2: 0 none, 1 Kempston, 2 Sinclair 1 (keys 6-0), 3 Sinclair 2 (keys 1-5), 4 Cursor (5 6 7 8 0)
#define ZX_STATUS       0x0A4          // b0 in reset, b1 held (Freeze), b2 snapshot load busy, b3 128K running
#define ZX_ST_RESET     0x01
#define ZX_ST_HELD      0x02
#define ZX_ST_SNAPBUSY  0x04
#define ZX_SNAP_GO      0x0A5          // any write starts the snapshot load while CTRL.b0 holds the reset
#define ZX_FRAMES       0x0A8          // 16 bit
#define ZX_FB_LATEST    0x0C0          // b7 valid, b1:0 the buffer of the newest complete frame (T119)
#define ZX_FRAME_SEQ    0x0C1          // changes with every complete frame
#define ZX_FB_BASE      0x0C4          // 32 bit, DDR2 byte address of buffer 0 (reset value 0x01D00000)
#define ZX_FB_STRIDE    0x0C8          // 32 bit (reset value 0x00100000)
#define ZX_WIN_PAGE     0x0D4          // b0: the 64 KB half of the RAM window; b2:1: the 16 KB bank the ROM aperture shows
#define ZX_ROM_SEL(n)   ((n) << 1)
#define ZX_SNAP_DIR     0x100          // 27 bytes: T80pa's register image
#define ZX_SNAP_7FFD    0x11B
#define ZX_SNAP_BORDER  0x11C
#define ZX_SNAP_AY      0x120          // 16 bytes
#define ZX_SNAP_AYSEL   0x130
#define ZX_TAPE_CTRL    0x140          // b0 play: a rising edge starts the tape, a falling edge stops it
#define ZX_TAPE_BASE    0x144          // 32 bit DDR2 byte address of the pulse stream
#define ZX_TAPE_POS     0x148          // 32 bit: write = the word position a seek loads, read = the position now (25 bit)
#define ZX_TAPE_SEEK    0x14C          // any write loads TAPE_POS
#define ZX_TAPE_STATUS  0x150          // b0 playing, b1 end of tape, b2 stop marker, b3 DDR2 read stuck
#define ZX_TS_PLAYING   0x01
#define ZX_TS_END       0x02
#define ZX_TS_STOP      0x04
#define ZX_TS_STUCK     0x08
#define ZX_TAPE_UNDERRUN 0x151

// sizes
#define ZX_ROM48_BYTES  16384u
#define ZX_ROM128_BYTES 32768u

// the DDR2 window (offsets from 0x01000000): the tape's pulse stream, 4 MB (TAPE_MAX_WORDS) from the core's reset value of TAPE_BASE
#define ZX_W_TAPE       0x800000u

// the picture (T119): four buffers of 344 x 280 RGB565 at the core's reset values of FB_BASE / FB_STRIDE (zx_regs.v)
#define ZX_FB_ADDR      0x01D00000u
#define ZX_FB_STRIDE_BYTES 0x00100000u

#endif
