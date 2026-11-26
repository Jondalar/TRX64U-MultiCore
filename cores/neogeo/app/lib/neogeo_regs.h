// The Neo Geo core's register block as the application uses it (T143). Source: cores/neogeo/trx/neo_regs.v (the register process, not its header comment).
#ifndef COREAPP_NEOGEO_REGS_H
#define COREAPP_NEOGEO_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/neogeo/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define NEO_OFS(addr)  ((addr) - DRV_ident_ADDR)

#define NEO_ID0           NEO_OFS(DRV_ident_id_ADDR)          // "NEO_"
#define NEO_CTRL          NEO_OFS(DRV_neo_machine_ctrl_ADDR)          // b0 reset hold (power-on 1), b1 halt (Freeze)
#define NEO_HOLD          DRV_neo_machine_ctrl_reset_hold_MASK
#define NEO_SYSTEM        NEO_OFS(DRV_neo_machine_system_ADDR)          // b0 1 = MVS, 0 = AES; b1 PAL video; b2 memory card unplugged; set while the reset is held
#define NEO_SYS_MVS       DRV_neo_machine_system_mvs_MASK
#define NEO_SYS_PAL       DRV_neo_machine_system_pal_MASK
#define NEO_SYS_NOCARD    DRV_neo_machine_system_nocard_MASK
#define NEO_DIP           NEO_OFS(DRV_neo_machine_dip_ADDR)          // b0 settings, b1 freeplay, b2 freeze
#define NEO_PAD           NEO_OFS(DRV_neo_machine_joy_ADDR)          // b0 swaps the host's two joystick ports
#define NEO_PAD1          NEO_OFS(DRV_neo_pad_pad1_ADDR)          // 16 bit, low byte first, the high byte commits: the USB pad of player 1 (T156), the machine's joystick word (b0 right, 1 left, 2 down, 3 up, 4 A, 5 B, 6 C, 7 D, 8 Start, 9 Select, 10 Coin)
#define NEO_PAD2          NEO_OFS(DRV_neo_pad_pad2_ADDR)
#define NEO_STATUS        NEO_OFS(DRV_neo_machine_status_ADDR)          // b0 in reset, b1 held (Freeze), b2 machine running, b3 backup RAM / memory card changed, b4 mover busy, b5 PLL locked
#define NEO_ST_HELD       DRV_neo_machine_status_held_MASK
#define NEO_ST_RUNNING    DRV_neo_machine_status_running_MASK
#define NEO_ST_DIRTY      DRV_neo_machine_status_dirty_MASK
#define NEO_ST_MOVING     DRV_neo_machine_status_moving_MASK
#define NEO_SAVE_ACK      NEO_OFS(DRV_neo_machine_save_ack_ADDR)          // any write clears NEO_ST_DIRTY
#define NEO_CFG           NEO_OFS(DRV_neo_machine_cfg_ADDR)          // 32 bit: the romset's chip word (neogeo.sv cfg)
#define NEO_STALL_CLK     NEO_OFS(DRV_neo_stats_stall_clk_ADDR)          // 32 bit counters: clocks the machine was held, late sprite words, late fix words, 68k reads asked
#define NEO_C_LATE        NEO_OFS(DRV_neo_stats_c_late_ADDR)
#define NEO_S_LATE        NEO_OFS(DRV_neo_stats_s_late_ADDR)
#define NEO_P_READS       NEO_OFS(DRV_neo_stats_p_reads_ADDR)
#define NEO_FB_LATEST     NEO_OFS(DRV_neo_video_fb_latest_ADDR)
#define NEO_FRAME_SEQ     NEO_OFS(DRV_neo_video_frame_seq_ADDR)
#define NEO_FB_BASE       NEO_OFS(DRV_neo_video_fb_base_ADDR)
#define NEO_FB_STRIDE     NEO_OFS(DRV_neo_video_fb_stride_ADDR)
#define NEO_ROM_BASE      NEO_OFS(DRV_neo_mem_rom_base_ADDR)          // 32 bit DDR2 byte addresses
#define NEO_V1_BASE       NEO_OFS(DRV_neo_mem_v1_base_ADDR)
#define NEO_V2_BASE       NEO_OFS(DRV_neo_mem_v2_base_ADDR)
#define NEO_M1_BASE       NEO_OFS(DRV_neo_mem_m1_base_ADDR)
#define NEO_IMG_BASE      NEO_OFS(DRV_neo_mem_img_base_ADDR)
#define NEO_MV_CMD        NEO_OFS(DRV_neo_mover_mv_cmd_ADDR)          // b1:0 image (0 LO ROM, 2 memory card, 3 XRAM), b2 save, b3 copy
#define NEO_MV_GO         NEO_OFS(DRV_neo_mover_mv_go_ADDR)
#define NEO_MV_DONE       NEO_OFS(DRV_neo_mover_mv_done_ADDR)          // counts finished moves
#define NEO_MV_LO        0x00
#define NEO_MV_MEMCARD   0x02
#define NEO_MV_XRAM      0x03
#define NEO_MV_SAVE      0x04
#define NEO_MV_COPY      0x08
#define NEO_RTC           NEO_OFS(DRV_neo_rtc_rtc_ADDR)          // 7 bytes BCD: seconds, minutes, hours (24 h), weekday 1-7, day, month, year 00-99
#define NEO_P2_MASK       NEO_OFS(DRV_neo_masks_p2_mask_ADDR)          // 32 bit address masks (2^n - 1) of the loaded ROMs
#define NEO_C_MASK        NEO_OFS(DRV_neo_masks_c_mask_ADDR)
#define NEO_V1_MASK       NEO_OFS(DRV_neo_masks_v1_mask_ADDR)
#define NEO_V2_MASK       NEO_OFS(DRV_neo_masks_v2_mask_ADDR)
#define NEO_M_MASK        NEO_OFS(DRV_neo_masks_m_mask_ADDR)
#define NEO_C_START       NEO_OFS(DRV_neo_masks_c_start_ADDR)          // 8 bit: the first MB of the sprite ROM above the P ROMs
#define NEO_C_DL          NEO_OFS(DRV_neo_masks_c_dl_ADDR)          // 8 bit: machine clocks a sprite word may be late before the machine is held (reset 8)
#define NEO_S_DL          NEO_OFS(DRV_neo_masks_s_dl_ADDR)          // the same for a fix word
#define NEO_C_CMDS        NEO_OFS(DRV_neo_perf_c_cmds_ADDR)          // 32 bit counters: c_* commands, e_* commands, ADPCM-A misses, Z80 program misses
#define NEO_E_CMDS        NEO_OFS(DRV_neo_perf_e_cmds_ADDR)
#define NEO_VA_MISS       NEO_OFS(DRV_neo_perf_va_miss_ADDR)
#define NEO_MZ_MISS       NEO_OFS(DRV_neo_perf_mz_miss_ADDR)
#define NEO_PF            NEO_OFS(DRV_neo_pf_pf_ADDR)          // 11 32 bit counters of the line caches: sprites hit, pend, miss, hint; fix hit, pend, miss, hint; 68k program hit, pend, miss
#define NEO_C_LAT_MAX     NEO_OFS(DRV_neo_perf_c_lat_max_ADDR)          // 8 bit: longest c_* read in ui clocks; machine clocks the longest sprite / fix word waited; the age of the last sprite word
#define NEO_C_MAX         NEO_OFS(DRV_neo_perf_c_max_ADDR)
#define NEO_S_MAX         NEO_OFS(DRV_neo_perf_s_max_ADDR)
#define NEO_PCK_AGE       NEO_OFS(DRV_neo_perf_pck_age_ADDR)
#define NEO_CP_SRC        NEO_OFS(DRV_neo_copy_cp_src_ADDR)          // 32 bit
#define NEO_CP_DST        NEO_OFS(DRV_neo_copy_cp_dst_ADDR)
#define NEO_CP_LEN        NEO_OFS(DRV_neo_copy_cp_len_ADDR)

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
