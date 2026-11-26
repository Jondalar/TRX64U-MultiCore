// The ZX Spectrum Next core's register block as the application uses it (T116). Source: cores/zxnext/trx/zxn_regs.v (the register process, not its header comment). The SD card, the mouse and the sector service
// sit where the Amiga core puts them (amiga_regs.h), so the base's sector-server and mouse libraries drive them unchanged.
#ifndef COREAPP_ZXNEXT_REGS_H
#define COREAPP_ZXNEXT_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/zxnext/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define ZXN_OFS(addr)  ((addr) - DRV_ident_ADDR)

#define ZXN_ID0          ZXN_OFS(DRV_ident_id_ADDR)                       // "ZXN_"
#define ZXN_FEAT         ZXN_OFS(DRV_zxn_features_feat_ADDR)                    // b0 the SD size register (ZXN_SD_MOUNT + 4) is in 512 byte sectors (always set)
#define ZXN_CTRL         ZXN_OFS(DRV_zxn_machine_ctrl_ADDR)               // b0 reset hold, b1 halt (Freeze)
#define ZXN_HOLD         DRV_zxn_machine_ctrl_reset_hold_MASK
#define ZXN_JOY          ZXN_OFS(DRV_zxn_machine_joy_ADDR)                // b0 swaps the host's two joystick ports
#define ZXN_LOCK         ZXN_OFS(DRV_zxn_machine_lock_ADDR)               // b0 frame lock off (default 0: the machine runs slightly fast so that its frame is the host's)
#define ZXN_BOARD_ID     ZXN_OFS(DRV_zxn_machine_board_id_ADDR)           // b3:0 the id NextReg 0x0F reports (reset 3 = issue 5; 0 = issue 2, 2 = issue 4); only the report
#define ZXN_STATUS       ZXN_OFS(DRV_zxn_machine_status_ADDR)             // b0 in reset, b1 held (Freeze), b2 an SD image is mounted, b3 the card model is busy
#define ZXN_ST_HELD      DRV_zxn_machine_status_held_MASK
#define ZXN_MACHINE      ZXN_OFS(DRV_zxn_machine_machine_ADDR)            // b2:0 machine timing (000 48K, 010 128K, 011 +3, 100 Pentagon), b3 60 Hz, b5:4 CPU speed
#define ZXN_FRAMES       ZXN_OFS(DRV_zxn_machine_frames_ADDR)             // 16 bit
#define ZXN_FB_LATEST    ZXN_OFS(DRV_zxn_video_fb_latest_ADDR)
#define ZXN_FRAME_SEQ    ZXN_OFS(DRV_zxn_video_frame_seq_ADDR)
#define ZXN_FB_BASE      ZXN_OFS(DRV_zxn_video_fb_base_ADDR)
#define ZXN_FB_STRIDE    ZXN_OFS(DRV_zxn_video_fb_stride_ADDR)
#define ZXN_RAM_BASE     ZXN_OFS(DRV_zxn_mem_ram_base_ADDR)               // 32 bit DDR2 byte address of the machine's 2 MB (default 0x01000000)
#define ZXN_STAGE_BASE   ZXN_OFS(DRV_zxn_mem_stage_base_ADDR)             // 32 bit DDR2 byte address of the SD staging sector (default 0x01200000)
#define ZXN_RTC          ZXN_OFS(DRV_zxn_rtc_rtc_ADDR)                    // 7 bytes BCD: seconds, minutes, hours (24 h), weekday 1-7, day, month, year 00-99
#define ZXN_FKEYS        ZXN_OFS(DRV_zxn_keys_fkeys_ADDR)                 // 16 bit: the Next's special keys F1-F10 (bit n = F(n+1))
#define ZXN_BUTTONS      ZXN_OFS(DRV_zxn_keys_buttons_ADDR)               // b0 the Multiface NMI button
#define ZXN_SD_MOUNT     ZXN_OFS(DRV_zxn_sd_sd_mount_ADDR)                // b0 a card is present; the size in sectors at +4
#define ZXN_MBOX_STATUS  ZXN_OFS(DRV_zxn_mbox_mbox_status_ADDR)           // b0 pending, b1 write, b7:4 request tag (DONE carries it back in b7:4)
#define ZXN_MBOX_LBA     ZXN_OFS(DRV_zxn_mbox_mbox_lba_ADDR)
#define ZXN_MBOX_DONE    ZXN_OFS(DRV_zxn_mbox_mbox_done_ADDR)
#define ZXN_SD_LASTCMD   ZXN_OFS(DRV_zxn_sd_stats_sd_lastcmd_ADDR)        // the last command the card model was given: b6:0 the number, b7 it was an application command (ACMD)
#define ZXN_MOUSE_DX     ZXN_OFS(DRV_zxn_mouse_mouse_dx_ADDR)
#define ZXN_MOUSE_DY     ZXN_OFS(DRV_zxn_mouse_mouse_dy_ADDR)             // this write sends the pair
#define ZXN_MOUSE_BTN    ZXN_OFS(DRV_zxn_mouse_mouse_btn_ADDR)

#define ZXN_FKEY_F3      2              // 50 / 60 Hz
#define ZXN_FKEY_F8      7              // CPU speed

// the DDR2 window (offsets from 0x01000000): the machine's 2 MB at +0 (the core's RAM_BASE reset value), the SD staging sector, the frame buffers
// (four buffers 1 MiB apart, 640 x 512 RGB565)
#define ZXN_W_RAM        0x000000u
#define ZXN_W_STAGE      0x200000u
#define ZXN_W_FB         0xD00000u

#endif
