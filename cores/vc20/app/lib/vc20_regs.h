// The VIC-20 core's registers as the application uses them (T83 on the generic application) and the ROM aperture of the host.
//
// The host's ROM aperture (U64_ROMS_BASE) is the C64's: BASIC at +0x0000, KERNAL at +0x2000, CHAR at +0x4000 (ultimate_sys.vhd r_rom). The
// VIC-20's window answers those three as the arrays at $C000, $E000 and $8000 (vc20_win: dma_addr 0x4000 -> "110" & a[12:0], 0x2000 ->
// "111" & a[12:0], 0x0000 -> $8000 + a[11:0]), so the offsets are the C64's.
#ifndef COREAPP_VC20_REGS_H
#define COREAPP_VC20_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/vc20/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define VC20_OFS(addr)  ((addr) - DRV_ident_ADDR)

#define VC20_CTRL       VC20_OFS(DRV_machine_ctrl_ADDR)
#define VC20_RAM_EXT    VC20_OFS(DRV_machine_ram_ext_ADDR)        // b0 3K at $0400, b1 8K at $2000, b2 8K at $4000, b3 8K at $6000, b4 8K at $A000; taken from the register at once
#define VC20_RAM_RO     VC20_OFS(DRV_machine_ram_ro_ADDR)         // the same bits: the block is read only (b4: the $A000 block as a ROM)
#define VC20_TAPE_PIN   VC20_OFS(DRV_tape_tape_pin_ADDR)
#define VC20_STATUS     VC20_OFS(DRV_machine_status_ADDR)
#define VC20_ST_HELD    DRV_machine_status_held_MASK
#define VC20_FB_LATEST  VC20_OFS(DRV_video_fb_latest_ADDR)
#define VC20_FRAME_SEQ  VC20_OFS(DRV_video_frame_seq_ADDR)
#define VC20_FB_BASE    VC20_OFS(DRV_video_fb_base_ADDR)
#define VC20_FB_STRIDE  VC20_OFS(DRV_video_fb_stride_ADDR)

// the picture (T119): four buffers of 376 x 283 RGB565 at the reset values of FB_BASE / FB_STRIDE, the states "reset" of those two properties
#define VC20_FB_ADDR    DRV_video_fb_base_reset
#define VC20_FB_STRIDE_BYTES DRV_video_fb_stride_reset

// the ROM aperture
#define VC20_ROM_BASIC  0x0000u
#define VC20_ROM_KERNAL 0x2000u
#define VC20_ROM_CHAR   0x4000u
#define VC20_BASIC_BYTES  8192u
#define VC20_KERNAL_BYTES 8192u
#define VC20_CHAR_BYTES   4096u

// the machine's memory the PRG loader needs
#define VC20_TXTTAB     0x2B           // start of BASIC, set by the KERNAL once it has sized the RAM; a program whose load address is $1001, $0401 or $1201 is BASIC
#define VC20_KBD_BUF    0x0277
#define VC20_KBD_COUNT  0xC6

#endif
