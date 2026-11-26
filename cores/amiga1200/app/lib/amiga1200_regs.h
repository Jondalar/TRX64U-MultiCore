// The Amiga 1200 core's registers as the application uses them, and its DDR2 window (T213.3 P1; the gateware's block is amiga_regs.v, cores/amiga1200/rtl).
#ifndef AMIGA1200_REGS_H
#define AMIGA1200_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/amiga1200/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window (byte registers, page 0;
// the pixel layer is page 6).
#include "drvtab.h"
#define AM_OFS(addr)    ((addr) - DRV_ident_ADDR)

#define AM_FEAT         AM_OFS(DRV_ident_features_ADDR)
#define AM_FEAT_RAWKEY  DRV_ident_features_rawkey_MASK
#define AM_FEAT_PAD2    DRV_ident_features_pad2_MASK
#define AM_FEAT_FBWIN   DRV_ident_features_fbwin_MASK
#define AM_CTRL         AM_OFS(DRV_machine_ctrl_ADDR)
#define AM_HOLD         DRV_machine_ctrl_hold_MASK
#define AM_VIDEO_OFF    DRV_machine_ctrl_video_off_MASK
#define AM_MEM_CFG      AM_OFS(DRV_machine_mem_cfg_ADDR)        // [1:0] chip 0/1/2/3 = 512K/1M/1.5M/2M, [3:2] slow 0/512K/1M/1.5M
#define AM_FAST_CFG     AM_OFS(DRV_machine_fast_cfg_ADDR)       // 0 none, 1 2 MB, 2 4 MB, 3 4 MB + the 1.5 MB the slow RAM gives up (5.5 MB), 4 one Zorro III board of 32 MB
#define AM_CPU_CFG      AM_OFS(DRV_machine_cpu_cfg_ADDR)
#define AM_CHIPSET_CFG  AM_OFS(DRV_machine_chipset_cfg_ADDR)
#define AM_FLOPPY_CFG   AM_OFS(DRV_machine_floppy_cfg_ADDR)     // [1] write protect all, [3:2] number of drives - 1
#define AM_IDE_CFG      AM_OFS(DRV_machine_ide_cfg_ADDR)        // 0x21 = IDE on
#define AM_JOY_CFG      AM_OFS(DRV_machine_joy_cfg_ADDR)        // b0 swaps the host's joystick ports 1 and 2 before the Amiga's two ports; written at once, no commit
#define AM_RTC_EN       AM_OFS(DRV_rtc_rtc_en_ADDR)
#define AM_RTC_VAL      AM_OFS(DRV_rtc_rtc_val_ADDR)
#define AM_RTC_IDX      AM_OFS(DRV_rtc_rtc_idx_ADDR)
#define AM_RTC_STAT     AM_OFS(DRV_rtc_rtc_stat_ADDR)
#define AM_STATUS       AM_OFS(DRV_machine_status_ADDR)
#define AM_ST_PAUSED    DRV_machine_status_paused_MASK
#define AM_FRAMES       AM_OFS(DRV_machine_frames_ADDR)         // 16 bit little endian, the core's frame counter (wraps)
#define PX_NFRAMES      AM_OFS(DRV_pixel_nframes_ADDR)          // 16 bit little endian, the pixel layer's count of HDMI output frames (wraps)
#define AM_STAT_SEL     AM_OFS(DRV_stat_stat_sel_ADDR)
#define AM_STAT_VAL     AM_OFS(DRV_stat_stat_val_ADDR)          // 32 bit, little endian
#define AM_FB_LATEST    AM_OFS(DRV_video_fb_latest_ADDR)        // b7 valid, b1:0 buffer
#define AM_FRAME_SEQ    AM_OFS(DRV_video_frame_seq_ADDR)
#define AM_FLOPPY_MOUNT AM_OFS(DRV_floppy_mount_ADDR)           // DRV_floppy_mount_STRIDE bytes per drive
#define AM_FLOPPY_STRIDE DRV_floppy_mount_STRIDE
#define AM_HDF_MOUNT    AM_OFS(DRV_hdf_mount_ADDR)              // DRV_hdf_mount_STRIDE bytes per unit; the 32 bit size register is at +4 (hdf.size)
#define AM_HDF_STRIDE   DRV_hdf_mount_STRIDE
#define AM_MBOX_STATUS  AM_OFS(DRV_mbox_status_ADDR)
#define AM_MBOX_LBA     AM_OFS(DRV_mbox_lba_ADDR)
#define AM_MBOX_DONE    AM_OFS(DRV_mbox_done_ADDR)
#define AM_MOUSE_DX     AM_OFS(DRV_mouse_dx_ADDR)
#define AM_MOUSE_DY     AM_OFS(DRV_mouse_dy_ADDR)               // this write sends the pair
#define AM_MOUSE_BTN    AM_OFS(DRV_mouse_btn_ADDR)
#define AM_RAWKEY       AM_OFS(DRV_rawkey_rawkey_ADDR)          // W = one Amiga key event {release, raw code}; R b0 = room for 8 more events
#define AM_PAD2_WORD    AM_OFS(DRV_pad2_word_ADDR)              // W 16 bit pair, low byte first, the high byte (b2:0) commits: bit 0 up, 1 down, 2 left, 3 right, 4 fire/red, 5 blue (2nd button), 6 green, 7 yellow, 8 play, 9 reverse, 10 forward
#define AM_PAD2_MODE    AM_OFS(DRV_pad2_mode_ADDR)              // W b1:0 0 game port 2 as before (the host's joystick port), 1 one-button joystick, 2 two-button joystick, 3 CD32 pad; with 1..3 port 2 is the USB pad only
#define AM_DLOG_STATUS  AM_OFS(DRV_dlog_status_ADDR)            // b0 not empty, b1 overflow (write 0x02 clears)
#define AM_DLOG_TRACK   AM_OFS(DRV_dlog_track_ADDR)
#define AM_DLOG_DRIVE   AM_OFS(DRV_dlog_drive_ADDR)
#define AM_DLOG_POP     AM_OFS(DRV_dlog_pop_ADDR)

// the decoded display window and the picture's crop window
#define AM_DIW_H        AM_OFS(DRV_diw_diw_h_ADDR)              // 32 bit: HDIWSTRT [8:0], HDIWSTOP [24:16] (lores pixels)
#define AM_DIW_V        AM_OFS(DRV_diw_diw_v_ADDR)              // 32 bit: VDIWSTRT [10:0], VDIWSTOP [26:16] (lines)
#define AM_FB_SKIP_X    AM_OFS(DRV_crop_skip_x_ADDR)            // 9 bit (2 bytes): hires pixels after hblank
#define AM_FB_SKIP_Y    AM_OFS(DRV_crop_skip_y_ADDR)            // 8 bit: lines after vblank
#define AM_FB_COLS      AM_OFS(DRV_crop_cols_ADDR)              // 7 bit: words of 8 hires pixels, <= 90
#define AM_FB_LINES     AM_OFS(DRV_crop_lines_ADDR)             // 9 bit (2 bytes): <= 270

#define AM_CHS_ECS      DRV_machine_chipset_cfg_ecs_MASK
#define AM_CHS_AGA      DRV_machine_chipset_cfg_aga_MASK

// the DDR2 window: offsets from 0x01000000 (memory, no registers: the gateware's parameters, not in the TOML A)
#define AM_W_ROM        0x780000u            // Kickstart, 512 KB (a 256 KB ROM also at +0x7C0000)
#define AM_W_ADF        0x900000u            // + n * AM_ADF_STRIDE
#define AM_ADF_STRIDE   0xE0000u
#define AM_W_STAGE      0xC80000u            // HDF staging sector
#define AM_W_FB         0xD00000u            // three buffers, 1 MiB apart (FB_BUFS = 3, gateware parameters)
#define AM_FB32_ADDR    0x04000000u          // the A1200: three 32-bit buffers at this DDR2 byte address, 2 MiB apart
#define AM_FB32_STRIDE  0x200000u
#define AM_ADF_SIZE     901120u
#define AM_ADF_TRACKS   160
#define AM_ADF_TRACK_BYTES 5632u             // 11 sectors * 512

#endif
