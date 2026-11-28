// The C64SCPU core's DDR2 map as the application uses it (T182 step 8): the places of the ROM images and of the 65816's RAM in the application's DDR2 window. Numbers only, no register: the
// real-ROM bench's PRG writer (tools/cbm_prg_sim.cc, via c64scpu_prg.h) takes them without the driver table; the registers are c64scpu_regs.h.
#ifndef COREAPP_C64SCPU_MAP_H
#define COREAPP_C64SCPU_MAP_H

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
