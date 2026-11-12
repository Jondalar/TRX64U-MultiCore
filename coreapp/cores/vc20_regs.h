// The VIC-20 core's register block as the application uses it (T83 on the generic application). Source: cores/vc20/rtl/trx/vc20_regs.vhd
// (the register process, not its header comment) and vc20_glue.vhd for the ROM block, read on 2026-10-22.
//
// The host's ROM aperture (U64_ROMS_BASE) is the C64's: BASIC at +0x0000, KERNAL at +0x2000, CHAR at +0x4000 (ultimate_sys.vhd r_rom). The
// VIC-20's window answers those three as the arrays at $C000, $E000 and $8000 (vc20_win: dma_addr 0x4000 -> "110" & a[12:0], 0x2000 ->
// "111" & a[12:0], 0x0000 -> $8000 + a[11:0]), so the offsets are the C64's.
#ifndef COREAPP_VC20_REGS_H
#define COREAPP_VC20_REGS_H

#define VC20_ID0        0x000          // "VC20"
#define VC20_CTRL       0x0A0          // b0 reset hold, b1 halt (b2, the old identity palette, does nothing since T119); the application writes none
#define VC20_RAM_EXT    0x0A1          // b0 3K at $0400, b1 8K at $2000, b2 8K at $4000, b3 8K at $6000, b4 8K at $A000; taken from the register at once
#define VC20_RAM_RO     0x0A2          // the same bits: the block is read only (b4: the $A000 block as a ROM)
#define VC20_STATUS     0x0A4          // b0 the machine is in reset, b1 held (Freeze)
#define VC20_ST_HELD    0x02
#define VC20_FB_LATEST  0x0C0          // b7 valid, b1:0 the buffer of the newest complete frame
#define VC20_FRAME_SEQ  0x0C1
#define VC20_FB_BASE    0x0C4          // 32 bit, DDR2 byte address of buffer 0 (reset value 0x01D00000)
#define VC20_FB_STRIDE  0x0C8          // 32 bit (reset value 0x00100000)

// the picture (T119): four buffers of 376 x 283 RGB565 (vc20_pixw: 3 VIC-20 pixels -> 5, lines 1:1) at the core's reset values of FB_BASE / FB_STRIDE
#define VC20_FB_ADDR    0x01D00000u
#define VC20_FB_STRIDE_BYTES 0x00100000u

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
