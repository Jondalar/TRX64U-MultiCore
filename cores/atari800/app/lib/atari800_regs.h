// The Atari 800 XL core's registers as the application uses them (T93 D15: constants in one header, a register change is one reviewed diff) and its DDR2 window.
// The gateware's block is cores/atari800/trx/a8_regs.vhd (the registers) and a8_win.vhd (the window); the ROM aperture offsets were checked against host/ultimate_sys.vhd (r_rom) and the bench's load_rom().
#ifndef COREAPP_ATARI800_REGS_H
#define COREAPP_ATARI800_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/atari800/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define A8_OFS(addr)    ((addr) - DRV_ident_ADDR)

#define A8_ID0          A8_OFS(DRV_ident_id_ADDR)                  // "A8XL"
#define A8_CTRL         A8_OFS(DRV_machine_ctrl_ADDR)
#define A8_CTRL_HOLD    DRV_machine_ctrl_reset_hold_MASK
#define A8_CTRL_KEEP    DRV_machine_ctrl_keep_ram_MASK
#define A8_RAM_SEL      A8_OFS(DRV_machine_ram_sel_ADDR)          // b2:0, taken at the core's next reset
#define A8_FLAGS        A8_OFS(DRV_machine_flags_ADDR)
#define A8_FLAG_SWAP    DRV_machine_flags_swap_joy_MASK
#define A8_FLAG_PAL     DRV_machine_flags_pal_MASK
#define A8_CART1        A8_OFS(DRV_machine_cart1_ADDR)            // cartridge type, 0 = none; the image is in the DDR2 window at A8_W_CART
#define A8_CART2        A8_OFS(DRV_machine_cart2_ADDR)
#define A8_THROTTLE     A8_OFS(DRV_machine_throttle_ADDR)         // b5:0, 1 = the machine's speed
#define A8_ROM_SEL      A8_OFS(DRV_machine_rom_sel_ADDR)          // the array behind the ROM aperture: 0 XL OS (16 KB), 1 BASIC (8 KB), 2 PBI/XEX ROM
#define A8_STATUS       A8_OFS(DRV_machine_status_ADDR)
#define A8_ST_HELD      DRV_machine_status_held_MASK
#define A8_FRAMES       A8_OFS(DRV_machine_frames_ADDR)           // 16 bit
#define A8_FB_LATEST    A8_OFS(DRV_video_fb_latest_ADDR)
#define A8_FRAME_SEQ    A8_OFS(DRV_video_frame_seq_ADDR)
#define A8_FB_BASE      A8_OFS(DRV_video_fb_base_ADDR)
#define A8_FB_STRIDE    A8_OFS(DRV_video_fb_stride_ADDR)
#define A8_SIO_ADDR     A8_OFS(DRV_sio_addr_ADDR)                 // b4:0 register of the SIO handler
#define A8_SIO_WDATA    A8_OFS(DRV_sio_wdata_ADDR)
#define A8_SIO_GO       A8_OFS(DRV_sio_go_ADDR)                   // b0 read SIO_ADDR, b1 write SIO_WDATA into it
#define A8_SIO_STATUS   A8_OFS(DRV_sio_status_ADDR)               // b0 an operation is under way (it needs machine ticks)
#define A8_SIO_RDATA    A8_OFS(DRV_sio_rdata_ADDR)                // 15 bit, low byte first

// the DDR2 window (offsets from 0x01000000): extra RAM above 64 KB, the cartridge image, the four frame buffers
#define A8_W_RAM        0x000000u
#define A8_W_CART       0x400000u
#define A8_W_FB         0xD00000u
#define A8_FB_BYTES     (384u * 288u * 2u)

// the ROM aperture: offsets into the 16 KB the host maps at U64_ROMS_BASE (contiguous; BASIC and the loader ROM at 0)
#define A8_OS_BYTES     16384u
#define A8_BASIC_BYTES  8192u

// the SIO handler's registers (sio_handler.vhdl)
#define SIO_TX          0
#define SIO_TX_STATUS   1              // {full, empty, count[7:0]}: bit 9 full, bit 8 empty
#define SIO_RX          2              // pops a word {command count[6:0], data[7:0]}: only when RX_STATUS says not empty
#define SIO_RX_STATUS   3              // bit 9 full, bit 8 empty, count in 7:0
#define SIO_DIVISOR     4
#define SIO_ERRORS      5

#endif
