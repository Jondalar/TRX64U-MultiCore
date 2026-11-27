// The register block of the DDT partial core ddt_novic "DDTN" as the application uses it (T168). The registers are the block ddt_regs of cores/ddt_mandel/trx/ddt_regs.v (shared with
// ddt_mandel; ddt_mandel is a large core since T255 and has no register block in an application) and the status wires of cores/ddt_novic/layer2_ddt_novic.v (status = {.., halted_q, hold | cpu_rst}).
#ifndef COREAPP_DDT_REGS_H
#define COREAPP_DDT_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/ddt_novic/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define DDT_OFS(addr)  ((addr) - DRV_ident_ADDR)

#define DDT_CTRL        DDT_OFS(DRV_machine_ctrl_ADDR)
#define DDT_CTRL_HOLD   DRV_machine_ctrl_reset_hold_MASK        // b0 reset hold (b1 halt: Freeze)
#define DDT_STATUS      DDT_OFS(DRV_machine_status_ADDR)
#define DDT_ST_HELD     DRV_machine_status_held_MASK            // b1 held (Freeze); b0 in reset
#define DDT_FB_LATEST   DDT_OFS(DRV_video_fb_latest_ADDR)
#define DDT_FRAME_SEQ   DDT_OFS(DRV_video_frame_seq_ADDR)
#define DDT_FB_BASE     DDT_OFS(DRV_video_fb_base_ADDR)
#define DDT_FB_STRIDE   DDT_OFS(DRV_video_fb_stride_ADDR)

// the picture: four buffers at the reset values of FB_BASE / FB_STRIDE, the states "reset" of those two properties
#define DDT_FB_ADDR          DRV_video_fb_base_reset            // the host's REU block
#define DDT_FB_STRIDE_BYTES  DRV_video_fb_stride_reset

#endif
