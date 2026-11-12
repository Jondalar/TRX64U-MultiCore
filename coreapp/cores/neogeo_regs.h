// The Neo Geo core's register block as the application uses it (T143). Source: cores/neogeo/trx/neo_regs.v - the code, not its header
// comment - read on 2026-10-28.
#ifndef COREAPP_NEOGEO_REGS_H
#define COREAPP_NEOGEO_REGS_H

#define NEO_ID0          0x000          // "NEO_"
#define NEO_CTRL         0x0A0          // b0 reset hold (power-on 1), b1 halt (Freeze)
#define NEO_HOLD         0x01
#define NEO_SYSTEM       0x0A1          // b0 1 = MVS, 0 = AES; b1 PAL video; b2 memory card unplugged; set while the reset is held
#define NEO_SYS_MVS      0x01
#define NEO_SYS_PAL      0x02
#define NEO_SYS_NOCARD   0x04
#define NEO_DIP          0x0A2          // b0 settings, b1 freeplay, b2 freeze
#define NEO_PAD          0x0A3          // b0 swaps the host's two joystick ports
#define NEO_PAD1         0x0F0          // 16 bit, low byte first, the high byte commits: the USB pad of player 1 (T156), the machine's joystick word (b0 right, 1 left, 2 down, 3 up, 4 A, 5 B, 6 C, 7 D, 8 Start, 9 Select, 10 Coin)
#define NEO_PAD2         0x0F2
#define NEO_STATUS       0x0A4          // b0 in reset, b1 held (Freeze), b2 machine running, b3 backup RAM / memory card changed, b4 mover busy, b5 PLL locked
#define NEO_ST_HELD      0x02
#define NEO_ST_RUNNING   0x04
#define NEO_ST_DIRTY     0x08
#define NEO_ST_MOVING    0x10
#define NEO_SAVE_ACK     0x0A5          // any write clears NEO_ST_DIRTY
#define NEO_CFG          0x0A8          // 32 bit: the romset's chip word (neogeo.sv cfg)
#define NEO_STALL_CLK    0x0B0          // 32 bit counters: clocks the machine was held, late sprite words, late fix words, 68k reads asked
#define NEO_C_LATE       0x0B4
#define NEO_S_LATE       0x0B8
#define NEO_P_READS      0x0BC
#define NEO_FB_LATEST    0x0C0
#define NEO_FRAME_SEQ    0x0C1
#define NEO_FB_BASE      0x0C4
#define NEO_FB_STRIDE    0x0C8
#define NEO_SEEN_COLS    0x0CC
#define NEO_SEEN_LINES   0x0CE
#define NEO_ROM_BASE     0x0D0          // 32 bit DDR2 byte addresses
#define NEO_V1_BASE      0x0D4
#define NEO_V2_BASE      0x0D8
#define NEO_M1_BASE      0x0DC
#define NEO_IMG_BASE     0x0E0
#define NEO_MV_CMD       0x0E4          // b1:0 image (0 LO ROM, 2 memory card, 3 XRAM), b2 save, b3 copy
#define NEO_MV_GO        0x0E5
#define NEO_MV_DONE      0x0E6          // counts finished moves
#define NEO_MV_LO        0x00
#define NEO_MV_MEMCARD   0x02
#define NEO_MV_XRAM      0x03
#define NEO_MV_SAVE      0x04
#define NEO_MV_COPY      0x08
#define NEO_RTC          0x0E8          // 7 bytes BCD: seconds, minutes, hours (24 h), weekday 1-7, day, month, year 00-99
#define NEO_P2_MASK      0x100          // 32 bit address masks (2^n - 1) of the loaded ROMs
#define NEO_C_MASK       0x104
#define NEO_V1_MASK      0x108
#define NEO_V2_MASK      0x10C
#define NEO_M_MASK       0x110
#define NEO_C_START      0x114          // 8 bit: the first MB of the sprite ROM above the P ROMs
#define NEO_C_DL         0x118          // 8 bit: machine clocks a sprite word may be late before the machine is held (reset 8)
#define NEO_S_DL         0x119          // the same for a fix word
#define NEO_C_CMDS       0x120          // 32 bit counters: c_* commands, e_* commands, ADPCM-A misses, Z80 program misses
#define NEO_E_CMDS       0x124
#define NEO_VA_MISS      0x128
#define NEO_MZ_MISS      0x12C
#define NEO_PF           0x150          // 11 32 bit counters of the line caches: sprites hit, pend, miss, hint; fix hit, pend, miss, hint; 68k program hit, pend, miss
#define NEO_C_LAT_MAX    0x130          // 8 bit: longest c_* read in ui clocks; machine clocks the longest sprite / fix word waited; the age of the last sprite word
#define NEO_C_MAX        0x131
#define NEO_S_MAX        0x132
#define NEO_PCK_AGE      0x133
#define NEO_CP_SRC       0x140          // 32 bit
#define NEO_CP_DST       0x144
#define NEO_CP_LEN       0x148

// the cfg word (neogeo.sv)
#define NEO_CFG_MS5P     (1u << 17)
#define NEO_CFG_XRAM     (1u << 18)
#define NEO_CFG_ADPCMA_EXT (1u << 19)
#define NEO_CFG_PCM      (1u << 23)

// the DDR2 window (offsets from 0x01000000)
#define NEO_W_IMG        0x000000u      // the RAM images: LO ROM +0 (64 KB), backup RAM +0x10000 (64 KB), memory card +0x20000 (2 KB), XRAM +0x22000 (8 KB)
#define NEO_W_BACKUP     (NEO_W_IMG + 0x10000u)
#define NEO_W_MEMCARD    (NEO_W_IMG + 0x20000u)
#define NEO_W_XRAM       (NEO_W_IMG + 0x22000u)
#define NEO_W_BIOS       0x040000u      // the three BIOS files, 128 KB each: the MVS one (sp-s2.sp1), the AES one (neo-epo.sp1), the Universe BIOS
#define NEO_W_M1         0x0A0000u      // the Z80 program (up to 512 KB)
#define NEO_W_FB         0x200000u      // the four frame buffers 256 KB apart
#define NEO_FB_STRIDE_B  0x040000u
#define NEO_W_ROM        0x300000u      // the machine's ROM space (rom_base) and the V ROMs after it

#endif
