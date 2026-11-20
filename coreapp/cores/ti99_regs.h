// The TI-99/4A core's register block as the application uses it. Source: cores/ti99/rtl/trx/ti99_regs.v (same common registers as the VC-20 core's).
#ifndef COREAPP_TI99_REGS_H
#define COREAPP_TI99_REGS_H

#define TI99_ID0        0x000          // "TI99"
#define TI99_CTRL       0x0A0          // b0 reset hold, b1 halt; the application writes none
#define TI99_CFG        0x0A1          // b0 turbo CPU, b1 swap joysticks, b2 32K RAM card (reset value 0x04), b3 disk controller, b4 1K scratchpad
#define TI99_WBADD      0x0A2          // added to bits 15:8 of the wishbone address (GROM n = n * 0x20)
#define TI99_WBHI       0x0A3          // upper byte of the 24-bit wishbone address behind the 64 KB ROM aperture
#define TI99_STATUS     0x0A4          // b0 the machine is in reset, b1 held (Freeze)
#define TI99_ST_HELD    0x02
#define TI99_FB_LATEST  0x0C0          // b7 valid, b1:0 the buffer of the newest complete frame
#define TI99_FRAME_SEQ  0x0C1
#define TI99_FB_BASE    0x0C4          // 32 bit, DDR2 byte address of buffer 0 (reset value 0x01D00000)
#define TI99_FB_STRIDE  0x0C8          // 32 bit (reset value 0x00100000)

// the picture: four buffers of 280 x 240 RGB565 (ti99_pixw) at the core's reset values of FB_BASE / FB_STRIDE
#define TI99_FB_ADDR    0x01D00000u
#define TI99_FB_STRIDE_BYTES 0x00100000u

// the ROM aperture is the window onto Mega99's wishbone map: TI99_WBHI selects the upper address byte, TI99_WBADD adds to bits 15:8, the offset is the lower bits
#define TI99_HI_CONSOLE 0x01u          // console ROM: high bytes at +0x0000, low bytes at +0x1000 (4096 each)
#define TI99_HI_GROM    0x10u          // GROM n: TI99_WBADD = n * 0x20, data at offset 0 (6144 bytes each)
#define TI99_ROM_HB     0x0000u
#define TI99_ROM_LB     0x1000u
#define TI99_ROM_BYTES  4096u
#define TI99_GROM_BYTES 6144u

#endif
