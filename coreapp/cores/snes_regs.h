// The SNES core's register block as the application uses it (T93 D15, T142). Source: cores/snes/trx/snes_regs.v (the code) and snes_mem.v / snes_pixw.v for the
// DDR2 layout.
#ifndef COREAPP_SNES_REGS_H
#define COREAPP_SNES_REGS_H

#define SNES_CTRL        0x0A0          // b0 reset hold, b1 halt (Freeze)
#define SNES_CTRL_HOLD   0x01
#define SNES_SYSTEM      0x0A1          // b0 PAL, b1 BLEND off, b2 S-DSP frequency; set while the reset is held
#define SNES_SYS_PAL     0x01
#define SNES_SYS_NOBLEND 0x02
#define SNES_SYS_DSPFREQ 0x04
#define SNES_ROM_TYPE    0x0A2          // the mapper byte (snes_rom.h); set while the reset is held
#define SNES_STATUS      0x0A4          // b0 in reset, b1 held (Freeze)
#define SNES_ST_HELD     0x02
#define SNES_FRAMES      0x0A8          // 16 bit, S-PPU frames
#define SNES_MISS_ROM    0x0B0          // 32 bit counters: line fetches of the ROM, WRAM and battery RAM caches, clocks the machine was held
#define SNES_MISS_WRAM   0x0B4
#define SNES_MISS_BS     0x0B8
#define SNES_STALL       0x0BC
#define SNES_FB_LATEST   0x0C0
#define SNES_FRAME_SEQ   0x0C1
#define SNES_FB_BASE     0x0C4
#define SNES_FB_STRIDE   0x0C8
#define SNES_CART_BASE   0x0D0          // 32 bit: DDR2 byte address of the image (ROM at +0, WRAM at +0x800000, battery RAM at +0x820000), default 0x01000000
#define SNES_ROM_MASK    0x0D8          // 24 bit
#define SNES_RAM_MASK    0x0DC          // 24 bit
#define SNES_RTC         0x0E0          // seconds, minutes, hours, day, month, year, weekday (BCD), 0xE7 unused
#define SNES_RTC_TGL     0x0E8          // b0: toggled to make the S-RTC take the time
#define SNES_WRITES      0x0F0          // 32 bit
#define SNES_PEAK_DSP    0x0F8          // 16 bit: largest |sample| of the S-DSP's pairs (0..32768) since the last write here; a write clears this and SNES_PEAK_OUT
#define SNES_PEAK_OUT    0x0FA          // 16 bit: the same for what is handed to the host's audio path
#define SNES_HANDS       0x0FC          // 16 bit, free running: pairs handed over to the host (one per host audio frame, 48 kHz)
#define SNES_PAD1        0x0F4          // 16 bit, low byte first, the high byte commits: the USB pad of player 1 (T156), bit n is serial bit n (B Y Select Start Up Down Left Right A X L R)
#define SNES_PAD2        0x0F6
#define SNES_IPL         0x100          // 64 bytes: the S-SMP's boot ROM, written while the reset is held

// DDR2 window offsets (from 0x01000000)
#define SNES_W_ROM       0x000000u
#define SNES_W_WRAM      0x800000u
#define SNES_W_BSRAM     0x820000u
#define SNES_W_FB        0x900000u
#define SNES_ROM_MAX     0x800000u      // 8 MiB
#define SNES_WRAM_BYTES  0x20000u
#define SNES_BSRAM_MAX   0x40000u       // 256 KiB
#define SNES_FB_STRIDE_V 0x80000u
#define SNES_IPL_BYTES   64

#endif
