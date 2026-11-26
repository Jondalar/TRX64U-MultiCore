// The Amstrad CPC core's register block as the application uses it (T93 D15). The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/cpc/app/gateware.toml and
// the blocks of the base that sdk.toml names); the offset the core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at
// the start of the core's window. Source of the layout: cores/cpc/trx/cpc_regs.v and cpc_win.v. The ROM area is written through the host's 16 KB ROM aperture one bank at a time (WIN_PAGE
// b2:1, see cores/zx/app/lib/zx_regs.h for why).
#ifndef COREAPP_CPC_REGS_H
#define COREAPP_CPC_REGS_H

#include "drvtab.h"
#define CPC_OFS(addr)   ((addr) - DRV_ident_ADDR)

#define CPC_ID0         CPC_OFS(DRV_ident_id_ADDR)                    // "CPC_"
#define CPC_CTRL        CPC_OFS(DRV_machine_ctrl_ADDR)                // b0 reset hold, b1 halt (Freeze)
#define CPC_CTRL_HOLD   DRV_machine_ctrl_reset_hold_MASK
#define CPC_MODEL       CPC_OFS(DRV_machine_model_ADDR)               // 0 = 464 (64 KB, no FDC), 1 = 664 (64 KB), 2 = 6128 (128 KB); taken in reset
#define CPC_JOY         CPC_OFS(DRV_machine_joy_ADDR)                 // b0 swaps the host's two joystick ports
#define CPC_CRTC        CPC_OFS(DRV_machine_crtc_ADDR)                // b0: 0 HD6845S / UM6845, 1 UM6845R
#define CPC_STATUS      CPC_OFS(DRV_machine_status_ADDR)              // b0 in reset, b1 held (Freeze), b3:2 the model running
#define CPC_ST_RESET    DRV_machine_status_in_reset_MASK
#define CPC_ST_HELD     DRV_machine_status_held_MASK
#define CPC_FRAMES      CPC_OFS(DRV_machine_frames_ADDR)              // 16 bit
#define CPC_WIN_PAGE    CPC_OFS(DRV_win_win_page_ADDR)                // b0: the 64 KB half of the RAM window; b2:1: the 16 KB bank the ROM aperture shows
#define CPC_ROM_SEL(n)  ((n) << 1)     // 0 lower ROM (OS), 1 upper ROM 0 (BASIC), 2 upper ROM 7 (AMSDOS)
#define CPC_FB_LATEST   CPC_OFS(DRV_video_fb_latest_ADDR)
#define CPC_FRAME_SEQ   CPC_OFS(DRV_video_frame_seq_ADDR)
#define CPC_FB_BASE     CPC_OFS(DRV_video_fb_base_ADDR)
#define CPC_FB_STRIDE   CPC_OFS(DRV_video_fb_stride_ADDR)
#define CPC_TAPE_CTRL   CPC_OFS(DRV_tape_ctrl_ADDR)
#define CPC_TAPE_BASE   CPC_OFS(DRV_tape_base_ADDR)
#define CPC_TAPE_POS    CPC_OFS(DRV_tape_pos_ADDR)
#define CPC_TAPE_SEEK   CPC_OFS(DRV_tape_seek_ADDR)
#define CPC_TAPE_STATUS CPC_OFS(DRV_tape_status_ADDR)
#define CPC_D0_BASE     CPC_OFS(DRV_disk_d0_base_ADDR)                // 32 bit DDR2 byte address and size in bytes of drive A's image
#define CPC_D0_SIZE     CPC_OFS(DRV_disk_d0_size_ADDR)
#define CPC_D1_BASE     CPC_OFS(DRV_disk_d1_base_ADDR)
#define CPC_D1_SIZE     CPC_OFS(DRV_disk_d1_size_ADDR)
#define CPC_DISK_CTRL   CPC_OFS(DRV_disk_ctrl_ADDR)                   // W b0/b1 mount A/B (after base, size and write protect), b2/b3 eject A/B, b4/b5 write protect A/B (taken at mount)
#define CPC_DISK_MOUNT(n) (1 << (n))
#define CPC_DISK_EJECT(n) (4 << (n))
#define CPC_DISK_WP(n)  (0x10 << (n))
#define CPC_DISK_STATUS CPC_OFS(DRV_disk_status_ADDR)
#define CPC_D0_DIRTY    CPC_OFS(DRV_dirty0_map_ADDR)                  // 8 bytes: one bit per 16 KB of the image the FDC wrote; a write of a byte clears the bits set in it
#define CPC_D1_DIRTY    CPC_OFS(DRV_dirty1_map_ADDR)
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
