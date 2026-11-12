// The Amstrad CPC core's register block as the application uses it (T93 D15). Source: cores/cpc/trx/cpc_regs.v and cpc_win.v - the code, not
// its header comment - read on 2026-10-22. The ROM area is written through the host's 16 KB ROM aperture one bank at a time (WIN_PAGE
// b2:1, see zx_regs.h for why).
#ifndef COREAPP_CPC_REGS_H
#define COREAPP_CPC_REGS_H

#define CPC_ID0         0x000          // "CPC_"
#define CPC_CTRL        0x0A0          // b0 reset hold, b1 halt (Freeze)
#define CPC_CTRL_HOLD   0x01
#define CPC_MODEL       0x0A1          // 0 = 464 (64 KB, no FDC), 1 = 664 (64 KB), 2 = 6128 (128 KB); taken in reset
#define CPC_JOY         0x0A2          // b0 swaps the host's two joystick ports
#define CPC_CRTC        0x0A3          // b0: 0 HD6845S / UM6845, 1 UM6845R
#define CPC_STATUS      0x0A4          // b0 in reset, b1 held (Freeze), b3:2 the model running
#define CPC_ST_RESET    0x01
#define CPC_ST_HELD     0x02
#define CPC_FRAMES      0x0A8          // 16 bit
#define CPC_WIN_PAGE    0x0D4          // b0: the 64 KB half of the RAM window; b2:1: the 16 KB bank the ROM aperture shows
#define CPC_ROM_SEL(n)  ((n) << 1)     // 0 lower ROM (OS), 1 upper ROM 0 (BASIC), 2 upper ROM 7 (AMSDOS)
#define CPC_FB_LATEST   0x0C0
#define CPC_FRAME_SEQ   0x0C1
#define CPC_FB_BASE     0x0C4
#define CPC_FB_STRIDE   0x0C8
#define CPC_TAPE_CTRL   0x140
#define CPC_TAPE_BASE   0x144
#define CPC_TAPE_POS    0x148
#define CPC_TAPE_SEEK   0x14C
#define CPC_TAPE_STATUS 0x150
#define CPC_D0_BASE     0x180          // 32 bit DDR2 byte address and size in bytes of drive A's image
#define CPC_D0_SIZE     0x184
#define CPC_D1_BASE     0x188
#define CPC_D1_SIZE     0x18C
#define CPC_DISK_CTRL   0x190          // W b0/b1 mount A/B (after base, size and write protect), b2/b3 eject A/B, b4/b5 write protect A/B (taken at mount)
#define CPC_DISK_MOUNT(n) (1 << (n))
#define CPC_DISK_EJECT(n) (4 << (n))
#define CPC_DISK_WP(n)  (0x10 << (n))
#define CPC_DISK_STATUS 0x191
#define CPC_D0_DIRTY    0x1A0          // 8 bytes: one bit per 16 KB of the image the FDC wrote; a write of a byte clears the bits set in it
#define CPC_D1_DIRTY    0x1B0
#define CPC_DIRTY_CHUNK 16384u

// sizes
#define CPC_ROM_BYTES   16384u
#define CPC_DSK_MAX     (880u * 1024u)       // the 3 inch drive (T92 D10); a larger extended DSK is refused

// the DDR2 window (offsets from 0x01000000): the two disk images, 1 MB each, the tape's pulse stream (the core resets TAPE_BASE to it), the three
// frame buffers (the core resets FB_BASE to it)
#define CPC_W_DISK(n)   ((n) * 0x100000u)
#define CPC_W_TAPE      0x800000u
#define CPC_W_FB        0xD00000u

#endif
