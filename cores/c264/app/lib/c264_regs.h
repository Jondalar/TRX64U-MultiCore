// The Commodore 264 core's register block as the application uses it (T90 on the generic application). Source: cores/c264/rtl/trx/c264_regs.v
// (the register process, not its header comment) and c264_win.v for the ROM aperture, read on 2026-10-22.
//
// The host's ROM aperture (U64_ROMS_BASE) is 32 KB with the C64's layout; the core's window takes its BASIC half (+0x0000) and KERNAL half
// (+0x2000) as ONE contiguous 16 KB aperture, offset = dma_addr[13:0], and ROM_SEL names the array behind it (c264_win.v: w_ai = a[13:0],
// w_sel = rom_sel, hit only for the two halves and rom_sel <= 5). The host's CHAR and colour RAM apertures are not there.
#ifndef COREAPP_C264_REGS_H
#define COREAPP_C264_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/c264/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define C264_OFS(addr)  ((addr) - DRV_ident_ADDR)

#define C264_ID0        C264_OFS(DRV_ident_id_ADDR)                       // "C264"
#define C264_CTRL       C264_OFS(DRV_c264_machine_ctrl_ADDR)              // b0 reset hold, b1 halt; the application writes none (the host's C64_MODE and C64_STOP do both)
#define C264_MODEL      C264_OFS(DRV_c264_machine_model_ADDR)             // b0: 0 C16 / C116, 1 Plus/4; taken at the core's next reset
#define C264_RAM_SIZE   C264_OFS(DRV_c264_machine_ram_size_ADDR)          // b0: 0 = 16 KB mirrored through the 64 KB space, 1 = 64 KB; taken at the next reset
#define C264_JOY_SWAP   C264_OFS(DRV_c264_machine_joy_swap_ADDR)          // b0 swaps the host's two joystick ports
#define C264_STATUS     C264_OFS(DRV_c264_machine_status_ADDR)            // b0 in reset, b1 held (Freeze), b2 the TED's PAL flag
#define C264_ST_HELD    DRV_c264_machine_status_held_MASK
#define C264_ROM_SEL    C264_OFS(DRV_c264_media_rom_sel_ADDR)             // b2:0 the array behind the 16 KB aperture
#define C264_TAPE_PIN   C264_OFS(DRV_c264_media_tape_pin_ADDR)            // b0 MOTOR pin driven, b1 WRITE pin driven, b4 READ / SENSE pins are the machine's inputs (reset 0: every pin released)
#define C264_CART       C264_OFS(DRV_c264_media_cart_ADDR)                // b0 a cartridge low image is loaded, b1 a cartridge high image is loaded
#define C264_FB_LATEST  C264_OFS(DRV_c264_video_fb_latest_ADDR)           // b7 valid, b1:0 the buffer of the newest complete frame
#define C264_FRAME_SEQ  C264_OFS(DRV_c264_video_frame_seq_ADDR)
#define C264_FB_BASE    C264_OFS(DRV_c264_video_fb_base_ADDR)             // 32 bit, DDR2 byte address of buffer 0
#define C264_FB_STRIDE  C264_OFS(DRV_c264_video_fb_stride_ADDR)           // 32 bit

// the arrays behind ROM_SEL
#define C264_ARR_KERNAL   0
#define C264_ARR_BASIC    1
#define C264_ARR_FUNC_LO  2
#define C264_ARR_FUNC_HI  3
#define C264_ARR_CART_LO  4
#define C264_ARR_CART_HI  5
#define C264_ROM_BYTES    16384u

// the picture: four buffers of 384 x 288 RGB565 at the reset values of FB_BASE / FB_STRIDE, the states "reset" of those two properties
#define C264_FB_ADDR    DRV_c264_video_fb_base_reset
#define C264_FB_STRIDE_BYTES DRV_c264_video_fb_stride_reset

// the machine's memory the PRG loader needs
#define C264_TXTTAB     0x2B
#define C264_KBD_BUF    0x0527
#define C264_KBD_COUNT  0xEF

#endif
