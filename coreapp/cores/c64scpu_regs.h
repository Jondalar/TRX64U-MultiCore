// The C64SCPU core's register page as the application uses it (T182 step 8, spec section 3 "ROM and register contract"). The core window is the
// c64u core's (pages 0, 4, 7, C, D, as there) plus one page of its own, E: cores/c64scpu/c64scpu_regs.v. CoreIo::rd/wr take a byte offset from the
// core window, so the page is 0xE000 + off (as c128_regs.h). Reads of absent offsets give 0.
// The offsets up to 0x0A5 are the register page as step 1 built it (the kept C128 offsets); 0x054, 0x058, 0x05C and 0x060 are the registers of this
// step: the gateware side (steps 2-4) implements them as written here.
#ifndef COREAPP_C64SCPU_REGS_H
#define COREAPP_C64SCPU_REGS_H

#define SCPU_PAGE        0xE000u
#define SCPU_REG(off)    (SCPU_PAGE + (off))

#define SCPU_ID0         0x000                        // page 0: "SCPU" (the host's ID block, bytes S C P U)
#define SCPU_CAPS        SCPU_REG(0x004)              // 0x01: this page, layout version 1 (step 1); 0x02: with the SuperCPU registers below (steps 2-4)
#define SCPU_JOY         SCPU_REG(0x024)              // b0: the host's joystick ports 1 and 2 are swapped before the core (T157); live; back to 0 by COLD
#define SCPU_STATUS      SCPU_REG(0x030)              // b3 held (Freeze), b4 PAL; b0 SuperCPU ROM valid, b1 20 MHz in effect, b2 emulation mode (E flag)
#define SCPU_ST_HELD     0x08
#define SCPU_ROM_CTRL    SCPU_REG(0x040)              // W b0: flush the ROM line caches (the C64 image's and the SuperCPU ROM's); R b0: a flush is running
#define SCPU_ROM_BASE    SCPU_REG(0x050)              // 32 bit, little endian: the DDR2 byte address of the C64 ROM image (BASIC, KERNAL), 0x01C00000
#define SCPU_CHR_ADDR    SCPU_REG(0x090)              // RW 12 bit (090 low, 091 high b3:0): the character ROM store's byte address
#define SCPU_CHR_DATA    SCPU_REG(0x092)              // W: store the byte at CHR_ADDR, then add 1; R: the byte at CHR_ADDR (2 clocks after a CHR_ADDR write), then add 1
#define SCPU_COLD        SCPU_REG(0x080)              // W b0 = 1: cold start; R b0: it is running
#define SCPU_COLD_MAX_MS 200

// ---- this step: the SuperCPU ROM and the two switches ------------------------------------------------------------------------------
// SuperCPU ROM: the file as it is (65,536, 131,072 or 262,144 bytes), written through the DDR2 window at SCPU_ROM_BASE. The gateware shows the image
// repeated to fill 512 KiB: byte k of the CPU's ROM view is file byte (k & (len - 1)), len = 1 << SCPU_ROM_LOG2; the reset vector is file offset 0xFFFC.
#define SCPU_SROM_BASE   SCPU_REG(0x054)              // R 32 bit, little endian: the DDR2 byte address of the SuperCPU ROM, 0x03080000 (a window of 512 KiB)
#define SCPU_SROM_LOG2   SCPU_REG(0x058)              // RW b4:0: log2 of the image length (16, 17 or 18); written before SCPU_ROM_VALID; not touched by COLD
#define SCPU_ROM_VALID   SCPU_REG(0x05C)              // RW b0: 1 = the image in the DDR2 is complete and checked. Reset 0; NOT touched by COLD (the image stays in the DDR2).
                                                      // With 0 the 65816 is held in reset: no bus cycle, no PHI2 access of its own. The application writes 0 first, the
                                                      // image, the cache flush, and 1 last.
#define SCPU_SW          SCPU_REG(0x060)              // RW: b0 the speed switch (1 = 20 MHz, 0 = 1 MHz: $D0B5 bit 6 follows it), b1 the JiffyDOS switch (1 = JiffyDOS:
                                                      // $D0B5 bit 7 follows it). Live. Reset (and COLD) 0x01. The application writes it with every start.
#define SCPU_SW_FAST     0x01
#define SCPU_SW_JIFFY    0x02
#define SCPU_SW_RESET    0x01

#define SCPU_SROM_DDR_ADDR 0x03080000u                // = SCPU_SROM_BASE
#define SCPU_SROM_WIN_OFF  0x02080000u                // = SCPU_SROM_DDR_ADDR - 0x01000000, the offset inside the application's DDR2 window
#define SCPU_SRAM_WIN_OFF  0x02000000u                // T217: the 65816's SRAM bank 0 ($0000-$FFFF) in the application's DDR2 window = 0x03000000 - 0x01000000 (scpu_mem.v sram(): p = 0x1000000 + addr,
                                                      // scpu_cache.v: p is the byte address minus 0x02000000). The CPU's page 0 and the BASIC pointers live here, not in the C64 RAM window.
#define SCPU_SUPERRAM_WIN_OFF 0x01000000u             // T250 D2: SuperRAM byte 0 in the application's DDR2 window = 0x02000000 - 0x01000000 (c64scpu_ddrx.v
                                                      // SC_BASE_W; scpu_mem.v: banks $02-$F5 at p = the 24-bit address, $F6/$F7 at p = the low 17 bits)
#define SCPU_SROM_MAX      262144u

// ---- the C64 ROMs (step 1's path, unchanged) -----------------------------------------------------------------------------------------
#define SCPU_ROM_DDR_ADDR  0x01C00000u                // = SCPU_ROM_BASE: BASIC at +0x0000 (8 KiB), KERNAL at +0x2000 (8 KiB)
#define SCPU_ROM_WIN_OFF   0x00C00000u
#define SCPU_BASIC_OFF     0x0000u
#define SCPU_KERNAL_OFF    0x2000u
#define SCPU_BASIC_BYTES   8192u
#define SCPU_KERNAL_BYTES  8192u
#define SCPU_CHR_BYTES     4096u                      // the character ROM, through SCPU_CHR_ADDR / SCPU_CHR_DATA

#endif
