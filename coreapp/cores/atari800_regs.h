// The Atari 800 XL core's register block as the application uses it (T93 D15: constants in one header, a register change is
// one reviewed diff). Source: cores/atari800/trx/a8_regs.vhd and a8_win.vhd - the code, not its header comment - read on
// 2026-10-15; the ROM aperture offsets were checked against host/ultimate_sys.vhd (r_rom) and the bench's load_rom().
#ifndef COREAPP_ATARI800_REGS_H
#define COREAPP_ATARI800_REGS_H

#define A8_ID0          0x000          // "A8XL"
#define A8_CTRL         0x0A0          // b0 reset hold, b1 halt, b2 keep RAM through the reset, b3 flush the cache
#define A8_CTRL_HOLD    0x01
#define A8_CTRL_KEEP    0x04
#define A8_RAM_SEL      0x0A1          // b2:0, taken at the core's next reset
#define A8_FLAGS        0x0A2          // b0 swap joystick ports, b1 PAL (taken at the next reset), b2 XEX loader mode
#define A8_FLAG_SWAP    0x01
#define A8_FLAG_PAL     0x02
#define A8_CART1        0x0A3          // cartridge type, 0 = none; the image is in the DDR2 window at A8_W_CART
#define A8_CART2        0x0A4
#define A8_THROTTLE     0x0A5          // b5:0, 1 = the machine's speed
#define A8_ROM_SEL      0x0A6          // the array behind the ROM aperture: 0 XL OS (16 KB), 1 BASIC (8 KB), 2 PBI/XEX ROM
#define A8_STATUS       0x0A8          // b0 in reset, b1 held (Freeze), b2 PAL, b3 a memory access is under way
#define A8_ST_HELD      0x02
#define A8_FRAMES       0x0AC          // 16 bit
#define A8_FB_LATEST    0x0C0
#define A8_FRAME_SEQ    0x0C1
#define A8_FB_BASE      0x0C4
#define A8_FB_STRIDE    0x0C8
#define A8_SIO_ADDR     0x0D0          // b4:0 register of the SIO handler
#define A8_SIO_WDATA    0x0D1
#define A8_SIO_GO       0x0D2          // b0 read SIO_ADDR, b1 write SIO_WDATA into it
#define A8_SIO_STATUS   0x0D3          // b0 an operation is under way (it needs machine ticks)
#define A8_SIO_RDATA    0x0D4          // 15 bit, low byte first

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
