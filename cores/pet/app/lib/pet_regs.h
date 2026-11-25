// The Commodore PET core's register block as the application uses it (T134). Source: cores/pet/rtl/trx/pet_regs.v (the register process) and
// pet_win.v for the ROM aperture.
//
// The ROM aperture is 16 KB (offset = dma_addr[13:0]); ROM_SEL names what is behind it: 0 the ROM space $8000-$BFFF, 1 the ROM space $C000-$FFFF
// (a byte sits at its machine address minus the space's base), 2 the character ROM (4 KB: the first 2 KB graphics + lower case, the upper 2 KB the SuperPET's Waterloo set, CRTC R12 bit 5 picks it), 3 / 8 the Waterloo ROM of the SuperPET.
#ifndef COREAPP_PET_REGS_H
#define COREAPP_PET_REGS_H

#define PET_ID0         0x000          // "PET1"
#define PET_CTRL        0x0A0          // b0 reset hold, b1 halt; the application writes none (the host's C64_MODE and C64_STOP do both)
#define PET_MODEL       0x0A1          // 0 2001, 1 3032, 2 4032, 3 8032, 4 8096, 5 SuperPET; taken at the core's next reset
#define PET_CPU_X       0x0A2          // 0..7 = CPU speed x1..x8, live
#define PET_DIAG        0x0A3          // b0 the DIAG jumper (PIA1 PA7 low)
#define PET_STATUS      0x0A4          // b0 in reset, b1 held (Freeze), b2 80 column picture
#define PET_ST_HELD     0x02
#define PET_HZ          0x0A5          // b0 0 = 60 Hz picture, 1 = 50 Hz
#define PET_PHOSPHOR    0x0A6          // 0 green, 1 amber, 2 white
#define PET_RAMSZ       0x0A7          // CPU RAM below $8000: 0 8 KiB, 1 16 KiB, 2 32 KiB; taken at the next reset; the CRTC models always have 32 KiB
#define PET_KEYMODE     0x0AB          // b0 keyboard mapping: 0 symbolic (the C64 key legend counts), 1 positional (the PET key at the same place); live
#define PET_SP_RAM      0x0AE          // b0 the SuperPET's 64 KiB bank RAM (96 KiB in all); taken at the next reset
#define PET_SP_CPU      0x0AA          // SuperPET CPU switch: 0 6502, 1 6809, 2 PROG (6502 at reset, $EFF8 b0 = 0 hands over); taken at the next reset
#define PET_ROM_SEL     0x0B0          // b3:0 what is behind the 16 KB aperture
#define PET_FB_LATEST   0x0C0          // b7 valid, b1:0 the buffer of the newest complete frame
#define PET_FRAME_SEQ   0x0C1
#define PET_FB_BASE     0x0C4          // 32 bit, DDR2 byte address of buffer 0 (reset value 0x01D00000)
#define PET_FB_STRIDE   0x0C8          // 32 bit (reset value 0x00100000)

// what is behind ROM_SEL
#define PET_SPACE_8000  0              // ROM space $8000-$BFFF
#define PET_SPACE_C000  1              // ROM space $C000-$FFFF
#define PET_SPACE_CHAR  2              // the 2 KB character ROM
#define PET_SPACE_WL_A  3              // the SuperPET's Waterloo ROM $A000-$DFFF (16 KB)
#define PET_SPACE_WL_E  8              // the Waterloo ROM $E000-$FFFF (8 KB, offsets 0-$1FFF)
#define PET_ROM_APERTURE 16384u
#define PET_CHAR_BYTES  2048u
#define PET_CHAR_MAX    4096u

// the picture: four buffers of 640 x 400 RGB565 at the core's reset values of FB_BASE / FB_STRIDE
#define PET_DRV_CTL     0x0D0          // b0 device 8 on, b1 drive type 1 = 4040 (D64), 0 = 8250 (D80 / D82)
#define PET_DRV_RO      0x0D1          // b0 / b1 drive 0 / 1 write protected
#define PET_DRV_MNT     0x0D2          // write: b0 / b1 toggle the mount strobe of drive 0 / 1 (size and read-only are taken at the strobe)
#define PET_DRV_CLR     0x0D3          // write: b0 / b1 toggle the clear-dirty strobe
#define PET_DRV_ST      0x0D4          // b1:0 dirty (the image in DDR2 was written), b3:2 drive activity
#define PET_DRV_SIZE0   0x0D8          // 32 bit each: SIZE0 0x0D8, SIZE1 0x0DC
#define PET_DRV_BASE0   0x0E0          // 32 bit each: BASE0 0x0E0, BASE1 0x0E4 (DDR2 byte addresses; reset 0x01100000 / 0x01220000)
#define PET_DRV_IMG0    0x01100000u
#define PET_DRV_IMG1    0x01220000u
#define PET_DRV_IMG_MAX 0x00120000u    // the room of a slot: a D82 (1,066,496 bytes) fits
// cassette 1: the pulse engine of the core (cores/pet/rtl/trx/pet_tape.v), the stream in the DDR2 window
#define PET_TAPE_CTRL   0x140          // b0 play (a rising edge starts the tape; the PET's motor gates it)
#define PET_TAPE_BASE   0x144          // 32 bit DDR2 byte address of the stream (reset value 0x01800000)
#define PET_TAPE_POS    0x148          // write = the word position a seek loads, read = the position now (25 bit)
#define PET_TAPE_SEEK   0x14C          // any write loads TAPE_POS
#define PET_TAPE_STATUS 0x150          // b0 playing, b1 end of tape, b2 stop marker, b3 DDR2 read stuck
#define PET_TAPE_PIN    0x152          // b0 motor pin driven, b1 write pin driven, b3 SENSE pulled while the tape plays, b4 READ / SENSE pins are inputs
#define PET_TAPE_LINES  0x153          // b0 motor on, b1 SENSE low, b2 READ line, b3 WRITE line
#define PET_PAD1        0x160          // the USB pads' button words (T156 service): b0 up, b1 down, b2 left, b3 right, b4 fire; low byte first
#define PET_PAD2        0x162
#define PET_PADKEY0     0x164          // the C64 key each of the five buttons presses (padkeys.h): up, down, left, right, fire
#define PET_W_TAPE      0x800000u      // the stream's place in the app's DDR2 window (offset from 0x01000000)
#define PET_FB_ADDR     0x01D00000u
#define PET_FB_STRIDE_BYTES 0x00100000u


#endif
