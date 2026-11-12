// The register block of the two DDT cores (ddt_mandel "DDTM", ddt_novic "DDTN") as the application uses it (T168). Source: cores/ddt_mandel/trx/ddt_regs.v
// (the code, read 2026-10-31) and the status wires in the two layer2 files (status = {.., halted_q, hold | cpu_rst}); the frame buffers are the register's reset values.
#ifndef COREAPP_DDT_REGS_H
#define COREAPP_DDT_REGS_H

#define DDT_CTRL        0x0A0          // b0 reset hold, b1 halt (Freeze)
#define DDT_CTRL_HOLD   0x01
#define DDT_STATUS      0x0A4          // b0 in reset, b1 held (Freeze); ddt_mandel b2: a render is on its way
#define DDT_ST_HELD     0x02
#define DDT_FB_LATEST   0x0C0
#define DDT_FRAME_SEQ   0x0C1
#define DDT_FB_BASE     0x0C4
#define DDT_FB_STRIDE   0x0C8

#define DDT_FB_ADDR          0x01D00000u    // the reset value of FB_BASE: the host's REU block
#define DDT_FB_STRIDE_BYTES  0x00100000u    // the reset value of FB_STRIDE

#endif
