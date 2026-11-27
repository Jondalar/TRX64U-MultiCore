// The SNES core's register block as the application uses it (T93 D15, T142). The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/snes/app/gateware.toml);
// the RTL is cores/snes/trx/snes_regs.v, and snes_mem.v / snes_pixw.v for the DDR2 layout.
#ifndef COREAPP_SNES_REGS_H
#define COREAPP_SNES_REGS_H

#include "drvtab.h"
// The register offset the core's register interface (CoreIo rd/wr) takes is the address in the driver table minus the address of the block "ident", at the start of the core's window (0x101A0000).
#define SNES_OFS(addr)   ((addr) - DRV_ident_ADDR)

#define SNES_CTRL         SNES_OFS(DRV_sn_ctrl_ctrl_ADDR)   // b0 reset hold, b1 halt (Freeze)
#define SNES_CTRL_HOLD   0x01
#define SNES_SYSTEM       SNES_OFS(DRV_sn_ctrl_system_ADDR)   // b0 PAL, b1 BLEND off, b2 S-DSP frequency; set while the reset is held
#define SNES_SYS_PAL     0x01
#define SNES_SYS_NOBLEND 0x02
#define SNES_SYS_DSPFREQ 0x04
#define SNES_ROM_TYPE     SNES_OFS(DRV_sn_ctrl_rom_type_ADDR)   // the mapper byte (snes_rom.h); set while the reset is held
#define SNES_STATUS       SNES_OFS(DRV_sn_status_status_ADDR)   // b0 in reset, b1 held (Freeze)
#define SNES_ST_HELD     0x02
#define SNES_FRAMES       SNES_OFS(DRV_sn_frames_frames_ADDR)   // 16 bit, S-PPU frames
#define SNES_MISS_ROM     SNES_OFS(DRV_sn_stats_miss_rom_ADDR)   // 32 bit counters: line fetches of the ROM, WRAM and battery RAM caches, clocks the machine was held
#define SNES_MISS_WRAM    SNES_OFS(DRV_sn_stats_miss_wram_ADDR)   
#define SNES_MISS_BS      SNES_OFS(DRV_sn_stats_miss_bs_ADDR)   
#define SNES_STALL        SNES_OFS(DRV_sn_stats_stall_ADDR)   
#define SNES_FB_LATEST    SNES_OFS(DRV_sn_fb_latest_ADDR)   
#define SNES_FRAME_SEQ    SNES_OFS(DRV_sn_fb_seq_ADDR)   
#define SNES_FB_BASE      SNES_OFS(DRV_sn_fb_base_ADDR)   
#define SNES_FB_STRIDE    SNES_OFS(DRV_sn_fb_stride_ADDR)   
#define SNES_CART_BASE    SNES_OFS(DRV_sn_cart_cart_base_ADDR)   // 32 bit: DDR2 byte address of the image (ROM at +0, WRAM at +0x800000, battery RAM at +0x820000), default 0x01000000
#define SNES_ROM_MASK     SNES_OFS(DRV_sn_cart_rom_mask_ADDR)   // 24 bit
#define SNES_RAM_MASK     SNES_OFS(DRV_sn_cart_ram_mask_ADDR)   // 24 bit
#define SNES_RTC          SNES_OFS(DRV_sn_rtc_time_ADDR)   // seconds, minutes, hours, day, month, year, weekday (BCD), 0xE7 unused
#define SNES_RTC_TGL      SNES_OFS(DRV_sn_rtc_tgl_ADDR)   // b0: toggled to make the S-RTC take the time
#define SNES_WRITES       SNES_OFS(DRV_sn_wr_writes_ADDR)   // 32 bit
#define SNES_PEAK_DSP     SNES_OFS(DRV_sn_peak_peak_dsp_ADDR)   // 16 bit: largest |sample| of the S-DSP's pairs (0..32768) since the last write here; a write clears this and SNES_PEAK_OUT
#define SNES_PEAK_OUT     SNES_OFS(DRV_sn_peak_peak_out_ADDR)   // 16 bit: the same for what is handed to the host's audio path
#define SNES_HANDS        SNES_OFS(DRV_sn_peak_hands_ADDR)   // 16 bit, free running: pairs handed over to the host (one per host audio frame, 48 kHz)
#define SNES_PAD1         SNES_OFS(DRV_sn_pad_pad1_ADDR)   // 16 bit, low byte first, the high byte commits: the USB pad of player 1 (T156), bit n is serial bit n (B Y Select Start Up Down Left Right A X L R)
#define SNES_PAD2         SNES_OFS(DRV_sn_pad_pad2_ADDR)   
#define SNES_IPL          SNES_OFS(DRV_sn_ipl_ipl_ADDR)   // 64 bytes: the S-SMP's boot ROM, written while the reset is held
#define SNES_DSPFW_REWIND  SNES_OFS(DRV_sn_dspfw_rewind_ADDR)   // T218: any write rewinds the DSP-n firmware stream
#define SNES_DSPFW        SNES_OFS(DRV_sn_dspfw_data_ADDR)   // T218: the next byte of the 8192 byte firmware (SNES_DSP_FW_BYTES), written while the reset is held

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
