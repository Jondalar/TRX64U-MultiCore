// The NES core's register block as the application uses it (T93 D15). Source: cores/nes/trx/nes_regs.v (the code, read 2026-10-15)
// and nes_mem.v / nes_pixw.v for the DDR2 layout.
#ifndef COREAPP_NES_REGS_H
#define COREAPP_NES_REGS_H

#define NES_CTRL        0x0A0          // b0 reset hold, b1 halt (Freeze)
#define NES_CTRL_HOLD   0x01
#define NES_SYSTEM      0x0A1          // b1:0 0 NTSC, 1 PAL, 2 Dendy; set while the reset is held
#define NES_JOY         0x0A2          // b0 swaps the host's joystick ports 1 and 2 before the two controller ports (T157); live
#define NES_STATUS      0x0A4          // b0 in reset, b1 held (Freeze)
#define NES_ST_HELD     0x02
#define NES_FB_LATEST   0x0C0
#define NES_FRAME_SEQ   0x0C1
#define NES_FB_BASE     0x0C4
#define NES_FB_STRIDE   0x0C8
#define NES_CART_BASE   0x0D0          // 32 bit: DDR2 byte address of the image (PRG at +0, CHR at +0x200000), default 0x01000000
#define NES_WIN_PAGE    0x0D4
#define NES_PAD1        0x0F8          // 16 bit, low byte first: the USB pad of player 1 (T156), bit 0 A, 1 B, 2 Select, 3 Start, 4 Up, 5 Down, 6 Left, 7 Right
#define NES_PAD2        0x0FA
#define NES_INES        0x0E0          // 16 bytes: the iNES header; written while the reset is held

// DDR2 window offsets (from 0x01000000)
#define NES_W_PRG       0x000000u
#define NES_W_CHR       0x200000u
#define NES_W_FB        0xD00000u
#define NES_PRG_MAX     0x200000u
#define NES_CHR_MAX     0x200000u

#endif
