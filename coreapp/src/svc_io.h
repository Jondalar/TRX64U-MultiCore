// Base 3.3, group `io` (T239.3 section 5; boundary rows 4.5, 8.2, 11.4): the base's own hardware abstraction, CoreIo (io.h), handed to libraries. Each member is a window of
// the running core's hardware with offsets relative to that window. This header names no register of any core: the registers are the core's own, named in its API TOML (D8)
// and in the constants header of the library that uses them. It joins LibApi as `const SvcIo *io`; the group is null on a core without a register window (the diag core).
//
// Contracts of the whole group:
//   - No call takes a CoreCtx.
//   - Every access is one byte wide per register: the windows take one byte per access (the DMA window's handshake). A register of 32 bit is four calls, and
//     the caller holds sync.enter ... sync.leave around the four when the bitstream needs them back to back.
//   - Task: the register calls (rd, wr, cart_*, bus_*, drv_*, ram_memonly, video_format, stop, mode_reset) and ram_read / ram_write (what they see depends on the
//     ram_memonly mode and on the machine being stopped, which other lock holders change) belong to a task that holds the core lock (handler, hook, poll, or each step
//     of an operation the menu steps: the UI node attribute `job`, ctx.job_step). The bulk calls that depend on no mode (ddr_*, ddr_abs_*, rom_*, eeprom_*) and
//     `window` may run on a job task as well. keys_read and delay_us are any task. Work that needs the machine held for a long time (a dump) is a stepped operation:
//     the menu calls it again while it answers `more`, each call holds the lock for one step, the menu stays open (the machine frozen) until the last step.
//   - Errors: none are reported. A core or host without a window ignores a write and reads 0 (the defaults of CoreIo); ddr_abs_* ignores what lies outside its range.
//   - `io` has 25 members, the 23 of CoreIo named below, bus_rd and window (the core's own register blocks, declared in its API TOML), bus_rd being new in CoreIo (the read side of bus_wr; the default of the interface returns 0). The others of
//     io.h (palette, drv_present, drv_buffer, case LEDs, mixer, tone, hotkey registers, matrix keys, joystick lines, bling, product, run_at, now_ms, delay_ms, wall_time) are the
//     base's own and no library of a core needs them: a library that does names the one it needs and it is appended (append-only).
#ifndef COREAPP_SVC_IO_H
#define COREAPP_SVC_IO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvcIo {
    uint32_t size;
    // The register block of the core window: byte registers at the block's base + reg (CoreIo::rd, wr). The register numbers are the core's.
    uint8_t (*rd)(uint32_t reg);
    void (*wr)(uint32_t reg, uint8_t v);
    // The DDR2 window of the core: bytes 0x01000000 + off of the application's address space (CoreIo::ddr_read, ddr_write). A write is complete when the call returns (the base
    // reads the last byte back). The caller keeps off + n inside the window the core has.
    void (*ddr_read)(uint32_t off, void *p, uint32_t n);
    void (*ddr_write)(uint32_t off, const void *p, uint32_t n);
    // The ROM aperture (CoreIo::rom_read, rom_write): 32 KB, off relative to its start. What lies at which offset is the core's (its API TOML).
    void (*rom_read)(uint32_t off, void *p, uint32_t n);
    void (*rom_write)(uint32_t off, const void *p, uint32_t n);
    // The base RAM window, 64 KB per core page (CoreIo::ram_read, ram_write): addr is the machine's address. With ram_memonly(true) every byte goes to the RAM's second
    // port, I/O range included, which is a bus cycle through the machine's decoding otherwise; the caller puts it back to false. Reads and writes are byte by byte.
    void (*ram_read)(uint32_t addr, void *p, uint32_t n);
    void (*ram_write)(uint32_t addr, const void *p, uint32_t n);
    void (*ram_memonly)(bool on);
    // The built-in drive's register window (CoreIo::drv_rd, drv_wr): byte registers, off relative to the window. A core without a drive window reads 0 and ignores the write.
    uint8_t (*drv_rd)(uint32_t off);
    void (*drv_wr)(uint32_t off, uint8_t v);
    // The fixed DDR2 areas of the drive and the cartridge port, absolute addresses 0x00EA8000 .. 0x00F00000 only (CoreIo::ddr_abs_read, ddr_abs_write). A range that is not
    // wholly inside is ignored on a write and reads zeros. The base adds the fence and the read-back of ddr_write. The libraries of the TRX64-Ultimate repository do not call
    // these two: the cartridge RAM and the drive image come through c64cart.ram_* and cbm1541.image_* (c64cart_api.h, cbm1541_api.h), whose bodies use them.
    void (*ddr_abs_read)(uint32_t addr, void *p, uint32_t n);
    void (*ddr_abs_write)(uint32_t addr, const void *p, uint32_t n);
    // The cartridge-port control block (CoreIo::cart_rd, cart_wr): byte registers, reg relative to the block. A core without it reads 0 and ignores the write.
    uint8_t (*cart_rd)(uint32_t reg);
    void (*cart_wr)(uint32_t reg, uint8_t v);
    // The core's bus-side I/O block (CoreIo::bus_wr, bus_rd): every byte register the block has, off relative to its start (the whole block, not only the bridge registers). bus_rd is the read side of bus_wr and generic as the pair of an existing
    // window; its one user today is the ring (it reads back the video format and the DMA mode it wrote). It is new in CoreIo with a default that returns 0, and the board's
    // HwIo reads the byte at the address when 3.3 is built; no existing call reads the block.
    void (*bus_wr)(uint32_t off, uint8_t v);
    uint8_t (*bus_rd)(uint32_t off);
    // The EEPROM window of the cartridge port (CoreIo::eeprom_read, eeprom_write): byte 0 is the dirty flag (read: written since the last write of this byte; any write
    // clears it), the 2 KB of data are at +2048. One byte per access. A core without the window reads 0 and ignores the write. The calls of the cartridge library
    // (c64cart.eeprom_get, eeprom_put) are the way for a library that is not the cartridge's own.
    void (*eeprom_read)(uint32_t off, void *p, uint32_t n);
    void (*eeprom_write)(uint32_t off, const void *p, uint32_t n);
    // The physical keys now: m[col] bit row = down, the matrix as the menu's scan reads it, whole in one critical section; and the RESTORE level in *restore.
    // False when this host cannot say (m and *restore are then not written). Task: any.
    bool (*keys_read)(uint8_t m[8], bool *restore);
    // The machine stop (CoreIo::stop; on holds the machine) and the application's reset (CoreIo::mode_reset: on holds the application in reset, off releases it).
    void (*stop)(bool on);
    void (*mode_reset)(bool on);
    // The video format register of the host (CoreIo::video_format): one byte, the System Mode's bits for the host's mode controller. A host or core without it ignores the write.
    void (*video_format)(uint8_t v);
    // A busy wait of `us` microseconds (CoreIo::delay_us), the same call as sys.delay_us (svc_sys.h). Task: any.
    void (*delay_us)(uint32_t us);
    // A register window of the core's bitstream outside the register block of rd/wr (its video, mixer, palette or resampler block): the window `name` that the
    // core's API TOML declares (name, address, size). The base checks the window against the registers the base drives itself (it refuses a window that covers
    // one: split the block into the runs you write) when it loads the API TOML and hands out its base
    // address; the library adds the offsets its TOML names and accesses the registers through a volatile pointer, one byte per access as above. Null (and
    // *size 0) for a name the TOML does not declare or the address map does not grant (a host range). Task: any; the registers' own rules are the core's.
    volatile uint8_t *(*window)(const char *name, uint32_t *size);
} SvcIo;

#ifdef __cplusplus
}
#endif

#endif
