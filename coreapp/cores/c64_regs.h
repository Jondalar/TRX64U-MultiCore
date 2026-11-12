// The registers and windows the C64 descriptor uses (T104.2 D4). Two bitstreams carry the same block: the TRX64 Ultimate monolith
// (designs/c64_basic) and the MultiCore `trx64` core. Byte offsets from TRX_IO_BASE (0x101A0000); a page is 0x1000 bytes.
#ifndef COREAPP_C64_REGS_H
#define COREAPP_C64_REGS_H

// ROM aperture at U64_ROMS_BASE, 32 KB, as ultimate_sys.vhd decodes it (address bits 14:12 of the offset: 111 colour RAM, 1xx CHAR,
// 01x KERNAL, 00x BASIC). The comment in that file names the arrays in another order; the decoder is what counts.
#define C64_ROM_BASIC    0x0000u
#define C64_ROM_KERNAL   0x2000u
#define C64_ROM_CHAR     0x4000u
#define C64_BASIC_BYTES  8192u
#define C64_KERNAL_BYTES 8192u
#define C64_CHAR_BYTES   4096u

// page 0
#define C64_CAPS         0x004          // b1 state bus, b2 engine, b3 debug page (trx_io.v)
#define C64_CAPS2        0x005          // b2: the CPU write path (T21 5.1.1)
#define C64_CAPS_STATEBUS 0x02
#define C64_CAPS_DEBUG   0x08
#define C64_CAPS2_CPUWRITE 0x04
#define C64_GATE_CTRL    0x100          // RW b0 FRZ: C64_STOP pauses the whole machine; R b8 halted, b9 gate shut
#define C64_GATE_FRZ     0x01
#define C64_GATE_STATUS  0x101          // the byte with b8 (halted) at bit 0: the PAUSED bit Gideon polls (he reads the word; so does freeze_status)
#define C64_GATE_HALTED  0x01
#define C64_DBG_OWNER    0x118          // W b0: the debugger owns run control, C64_STOP stops nothing

// page 1: the state bus (c64_core.v has the word map); 32 bit registers, little endian, byte 3 commits a write, byte 0 latches a read
#define C64_SB_ADDR      0x1000         // {chip 11:7, word 6:0}
#define C64_SB_DATA      0x1004         // reading or writing moves SB_ADDR on by one word
#define C64_SB_STATUS    0x1008         // b0 the gate is shut, b1 a write was refused (W: clears b1)
#define C64_SB_LOAD      0x100C         // b1: the staged CPU words load at the next opening edge; R: still armed

// page 3: the debug block (trx_dbg.v)
#define C64_DBG_CMD      0x3000
#define C64_DBG_STATUS   0x3008
#define C64_DBG_IPC_SET  0x3018
#define C64_CMD_PAUSE_IB 0x02
#define C64_CMD_RUN      0x03
#define C64_ST_SHUT      (1u << 0)
#define C64_ST_AT_IB     (1u << 2)

// the cartridge start (c64_cart.cc): the C64 core's control registers (io->cart_wr, offsets from C64_CARTREGS_BASE) and the bus bridge's
// (io->bus_wr, offsets from C64_IO_BASE), then the cartridge block of the TRX IO block (page 4, io->wr at TRX_IO_BASE + reg)
#define C64_CARTREG_TYPE     0x05
#define C64_CARTREG_REU      0x08
#define C64_CARTREG_SAMPLER  0x0E
#define C64_BUSREG_INTERNAL  0x2B
#define C64_BUSREG_EXTERNAL  0x2C
#define C64_CAPS_CART        0x10       // CAPS b4: the cartridge block (page 4) exists
#define C64_IRQ_CAUSE        0x014      // W1C: b4 flash operation pending, b5 EEPROM written
#define C64_IRQ_FLASH        0x10       // IRQ_CAUSE b4 (and IRQ_EN): a flash operation is pending until the ACK
#define C64_IRQ_EEPROM       0x20       // IRQ_CAUSE b5: the GMod2's EEPROM was written
#define C64_CART_FL_ENABLE   0x4000     // the flash responder: 0 off
#define C64_CART_FL_MAGIC1   0x4004     // 16 bit, little endian: the part's two unlock addresses and the address mask
#define C64_CART_FL_MAGIC2   0x4008
#define C64_CART_FL_MASK     0x400C
#define C64_CART_FL_ID       0x4010     // manufacturer, device, the device ID's address
#define C64_CART_EVT         0x4020     // 4 bytes: [31:30] 1 program, 2 sector erase, 3 chip erase, [29] chip, [28:8] chip address, [7:0] data; a read of byte 0 latches 1-3
#define C64_CART_EVT_HI      0x4024     // b1:0 chip address bits 22:21 (the EasyFlash 3's 8 MiB; 0 on the types this application starts)
#define C64_CART_XPORT_CAPS  0x4038     // b0 the 8 MiB image window, b1 EasyFlash 3 banking; older bitstreams read 0
#define C64_XCAPS_8MB        0x01
#define C64_XCAPS_EF3        0x02
#define C64_CART_WR_COUNT    0x402C     // 16 bit, +1 per flash operation and per EEPROM write; a read of byte 0 latches byte 1
#define C64_CART_CMD         0x4028     // b1 drop the cartridge's cached lines, b0 ACK a pending flash operation
#define C64_CART_ROM_SEL     0x4030     // b1:0 where the image is read from: 0 Gideon's __cart_rom_start, 1 the expansion-port block 0x01000000
#define C64_CART_DESC        0x4040     // the mapping descriptor, 43 bytes
#define C64_CART_CMD_ACK     0x01
#define C64_CART_CMD_INVAL   0x02

// the machine's zero page and keyboard buffer (the C64's, which the CbmPrgParams name)
#define C64_TXTTAB       0x2B

#endif
