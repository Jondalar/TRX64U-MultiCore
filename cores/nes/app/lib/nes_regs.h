// The NES core's registers as the application uses them (T93 D15: constants in one header, a register change is one reviewed diff) and its DDR2 window.
// The gateware's block is cores/nes/trx/nes_regs.v (the registers), nes_mem.v and nes_pixw.v (the DDR2 layout).
#ifndef COREAPP_NES_REGS_H
#define COREAPP_NES_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/nes/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define NES_OFS(addr)   ((addr) - DRV_ident_ADDR)

#define NES_CTRL        NES_OFS(DRV_nes_machine_ctrl_ADDR)             // b0 reset hold, b1 halt (Freeze)
#define NES_CTRL_HOLD   DRV_nes_machine_ctrl_reset_hold_MASK
#define NES_SYSTEM      NES_OFS(DRV_nes_machine_system_ADDR)           // b1:0 0 NTSC, 1 PAL, 2 Dendy; set while the reset is held
#define NES_JOY         NES_OFS(DRV_nes_machine_joy_ADDR)              // b0 swaps the host's joystick ports 1 and 2 before the two controller ports (T157); live
#define NES_STATUS      NES_OFS(DRV_nes_machine_status_ADDR)           // b0 in reset, b1 held (Freeze)
#define NES_ST_HELD     DRV_nes_machine_status_held_MASK
#define NES_FB_LATEST   NES_OFS(DRV_nes_video_fb_latest_ADDR)
#define NES_FRAME_SEQ   NES_OFS(DRV_nes_video_frame_seq_ADDR)
#define NES_FB_BASE     NES_OFS(DRV_nes_video_fb_base_ADDR)
#define NES_FB_STRIDE   NES_OFS(DRV_nes_video_fb_stride_ADDR)
#define NES_CART_BASE   NES_OFS(DRV_nes_cart_cart_base_ADDR)           // 32 bit: DDR2 byte address of the image (PRG at +0, CHR at +0x200000), default 0x01000000
#define NES_WIN_PAGE    NES_OFS(DRV_nes_cart_win_page_ADDR)
#define NES_PAD1        NES_OFS(DRV_nes_pad_pad1_ADDR)                 // the USB pad of player 1 (T156), bit 0 A, 1 B, 2 Select, 3 Start, 4 Up, 5 Down, 6 Left, 7 Right
#define NES_PAD2        NES_OFS(DRV_nes_pad_pad2_ADDR)
#define NES_INES        NES_OFS(DRV_nes_ines_hdr_ADDR)             // 16 bytes: the iNES header; written while the reset is held

// DDR2 window offsets (from 0x01000000)
#define NES_W_PRG       0x000000u
#define NES_W_CHR       0x200000u
#define NES_W_FB        0xD00000u
#define NES_PRG_MAX     0x200000u
#define NES_CHR_MAX     0x200000u

#endif
