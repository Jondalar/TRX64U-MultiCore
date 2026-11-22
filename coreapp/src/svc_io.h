// Base 3.3, group `io` (T239.3 section 5; boundary rows 4.5, 8.2, 11.4): the windows of the running core, the calls of CoreIo (io.h). A library reaches the
// registers of its own bitstream through these calls, with the register numbers and offsets of its core's API TOML (D8); nothing here knows a register. It joins
// LibApi as `const SvcIo *io`; the group is null on a core without a register window (the diag core).
//
// Contracts of the whole group:
//   - No call takes a CoreCtx.
//   - Every access is one byte wide per register: the windows take one byte per access (the DMA window's handshake). A register of 32 bit is four calls, and
//     the caller holds sync.enter ... sync.leave around the four when the bitstream needs them back to back (the TRX registers, trx_io.cc).
//   - Task: the register calls (rd, wr, cart_*, bus_*, ram_memonly, stop, mode_reset) belong to a task that holds the core lock (handler, hook, poll). The
//     bulk calls (ddr_*, ram_read, ram_write, eeprom_*) touch no shared state and a job task may call them as well. keys_read is any task.
//   - Errors: none are reported. A bitstream without a window ignores a write and reads 0; ddr_abs_* ignores what lies outside its range.
//   - `io` has no CoreIo members beyond these 19. The others of io.h (palette, video_format, drive window, case LEDs, mixer, hotkeys, matrix keys, joystick,
//     bling) are the base's own and no library of a core needs them: a library that does names the one it needs and it is appended (append-only).
#ifndef COREAPP_SVC_IO_H
#define COREAPP_SVC_IO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvcIo {
    uint32_t size;
    // The register block of the core window, byte registers at IOBASE + 0x1A0000 + reg (page 0 of the TRX block). Read and write.
    uint8_t (*rd)(uint32_t reg);
    void (*wr)(uint32_t reg, uint8_t v);
    // The cartridge-port control registers, C64_CARTREGS_BASE (IOBASE + 0x40000) + reg: +0 MODE, +1 STOP, +5 CARTRIDGE_TYPE, +8 REU_ENABLE, +9 REU_SIZE,
    // +0xE SAMPLER_ENABLE on the C64 bitstream. A core without them reads 0 and ignores the write. cart_rd is appended to CoreIo for this group.
    uint8_t (*cart_rd)(uint32_t reg);
    void (*cart_wr)(uint32_t reg, uint8_t v);
    // The C64 bus bridge's registers, C64_IO_BASE (U2P_IO_BASE + 0x80000) + off: +1 VIDEOFORMAT, +3 DMA_MEMONLY, +0x2B BUS_INTERNAL, +0x2C BUS_EXTERNAL. bus_rd is
    // new in CoreIo (HwIo reads the byte at the address; the default of the interface returns 0) because the ring reads VIDEOFORMAT and DMA_MEMONLY back.
    uint8_t (*bus_rd)(uint32_t off);
    void (*bus_wr)(uint32_t off, uint8_t v);
    // The DDR2 window of the core: bytes 0x01000000 + off of the application's address space (the REU's memory from offset 0, the cartridge image). A write
    // is complete when the call returns (the base reads the last byte back). The caller keeps off + n inside the window the core has.
    void (*ddr_read)(uint32_t off, void *p, uint32_t n);
    void (*ddr_write)(uint32_t off, const void *p, uint32_t n);
    // The fixed DDR2 areas of the drive and the cartridge port, absolute addresses 0x00EA8000 .. 0x00F00000 only (0x00EE0000 the 64 KB image of drive A, 0x00EF0000 the
    // 64 KB RAM of the cartridge). A range that is not wholly inside is ignored on a write and reads zeros. The base adds the fence and the read-back of ddr_write.
    // Replaces the pointers c64_cart_ram() and drive_a_image() of the ring's pending list.
    void (*ddr_abs_read)(uint32_t addr, void *p, uint32_t n);
    void (*ddr_abs_write)(uint32_t addr, const void *p, uint32_t n);
    // The base RAM window at C64_MEMORY_BASE, 64 KB per core page: addr is the machine's address. With ram_memonly(true) every byte goes to the RAM's second port,
    // $D000-$DFFF included, which is a bus cycle through the PLA otherwise (C64_DMA_MEMONLY); the caller puts it back to false. Reads and writes are byte by byte.
    void (*ram_read)(uint32_t addr, void *p, uint32_t n);
    void (*ram_write)(uint32_t addr, const void *p, uint32_t n);
    void (*ram_memonly)(bool on);
    // The ROM aperture at U64_ROMS_BASE: 32 KB, BASIC at +0 (8 KB), KERNAL at +0x2000 (8 KB), CHAR at +0x4000 (4 KB); off is relative to it.
    void (*rom_write)(uint32_t off, const void *p, uint32_t n);
    // The EEPROM window of the cartridge port (the GMod2's 93C86): byte 0 is the dirty flag (read: written since the last write of this byte; any write clears
    // it), the 2 KB of data are at +2048. One byte per access. A core without the window reads 0 and ignores the write. The calls of the cartridge library
    // (c64cart.eeprom_get, eeprom_put) are the way for a library that is not the cartridge's own.
    void (*eeprom_read)(uint32_t off, void *p, uint32_t n);
    void (*eeprom_write)(uint32_t off, const void *p, uint32_t n);
    // The physical keys now: m[col] bit row = down, the matrix as the menu's scan reads it, whole in one critical section; and the RESTORE level in *restore.
    // False when this host cannot say (m and *restore are then not written). Task: any.
    bool (*keys_read)(uint8_t m[8], bool *restore);
    // The machine stop (C64_STOP; on holds the machine) and the application's reset (C64_MODE reset on, unreset off).
    void (*stop)(bool on);
    void (*mode_reset)(bool on);
} SvcIo;

#ifdef __cplusplus
}
#endif

#endif
