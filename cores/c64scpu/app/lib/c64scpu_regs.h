// The C64SCPU core's register page as the application uses it (T182 step 8, spec section 3 "ROM and register contract"). The registers are named by the driver table of the app (drvtab.h, made
// from the TOML A: cores/c64scpu/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the core's register interface (RegBind, CoreIo rd/wr) takes is the address in the
// table minus the address of the block "ident", which sits at the start of the core's window (0x101A0000). The core's own page is E (blocks page_id .. ram_cnt); the rest of the window is the
// C64U's (ident, irq). Reads of absent offsets give 0.
#ifndef COREAPP_C64SCPU_REGS_H
#define COREAPP_C64SCPU_REGS_H

#include "drvtab.h"
#define SCPU_OFS(addr)   ((addr) - DRV_ident_ADDR)

#define SCPU_ID0         SCPU_OFS(DRV_ident_id_ADDR)                  // page 0: "SCPU" (the host's ID block, bytes S C P U)
#define SCPU_CAPS        SCPU_OFS(DRV_page_id_caps_ADDR)              // 0x01: this page, layout version 1 (step 1); 0x02: with the SuperCPU registers below (steps 2-4)
#define SCPU_JOY         SCPU_OFS(DRV_machine_joy_swap_ADDR)          // b0: the host's joystick ports 1 and 2 are swapped before the core (T157); live; back to 0 by COLD
#define SCPU_STATUS      SCPU_OFS(DRV_status_status_ADDR)             // b3 held (Freeze), b4 PAL; b0 SuperCPU ROM valid, b1 20 MHz in effect, b2 emulation mode (E flag)
#define SCPU_ST_HELD     DRV_status_status_held_MASK
#define SCPU_ROM_CTRL    SCPU_OFS(DRV_rom_ctrl_ADDR)                  // W b0: flush the line caches: the C64 image's, the SuperCPU ROM's and the 65816's 32 KiB cache of SRAM and
                                                      // SuperRAM (c64_core_c64scpu.v scpu_flush); R b0: the C64 image cache's walker is running (the 65816's cache
                                                      // clears in 2048 clocks, not shown)
#define SCPU_ROM_BASE    SCPU_OFS(DRV_rom_base_ADDR)                  // 32 bit, little endian: the DDR2 byte address of the C64 ROM image (BASIC, KERNAL), 0x01C00000
#define SCPU_CHR_ADDR    SCPU_OFS(DRV_chr_addr_ADDR)                  // RW 12 bit (090 low, 091 high b3:0): the character ROM store's byte address
#define SCPU_CHR_DATA    SCPU_OFS(DRV_chr_data_ADDR)                  // W: store the byte at CHR_ADDR, then add 1; R: the byte at CHR_ADDR (2 clocks after a CHR_ADDR write), then add 1
#define SCPU_COLD        SCPU_OFS(DRV_cold_cold_ADDR)                 // W b0 = 1: cold start; R b0: it is running
#define SCPU_COLD_MAX_MS 200

// ---- this step: the SuperCPU ROM and the two switches ------------------------------------------------------------------------------
// SuperCPU ROM: the file as it is (65,536, 131,072 or 262,144 bytes), written through the DDR2 window at SCPU_ROM_BASE. The gateware shows the image
// repeated to fill 512 KiB: byte k of the CPU's ROM view is file byte (k & (len - 1)), len = 1 << SCPU_ROM_LOG2; the reset vector is file offset 0xFFFC.
#define SCPU_SROM_BASE   SCPU_OFS(DRV_rom_srom_base_ADDR)             // R 32 bit, little endian: the DDR2 byte address of the SuperCPU ROM, 0x03080000 (a window of 512 KiB)
#define SCPU_SROM_LOG2   SCPU_OFS(DRV_rom_srom_log2_ADDR)             // RW b4:0: log2 of the image length (16, 17 or 18); written before SCPU_ROM_VALID; not touched by COLD
#define SCPU_ROM_VALID   SCPU_OFS(DRV_rom_rom_valid_ADDR)             // RW b0: 1 = the image in the DDR2 is complete and checked. Reset 0; NOT touched by COLD (the image stays in the DDR2).
                                                      // With 0 the 65816 is held in reset: no bus cycle, no PHI2 access of its own. The application writes 0 first, the
                                                      // image, the cache flush, and 1 last.
#define SCPU_SW          SCPU_OFS(DRV_rom_sw_ADDR)                    // RW: b0 the speed switch (1 = 20 MHz, 0 = 1 MHz: $D0B5 bit 6 follows it), b1 the JiffyDOS switch (1 = JiffyDOS:
                                                      // $D0B5 bit 7 follows it). Live. Reset (and COLD) 0x01. The application writes it with every start.
#define SCPU_SW_FAST     0x01
#define SCPU_SW_JIFFY    0x02
#define SCPU_SW_RESET    0x01

#include "c64scpu_map.h"

#endif
