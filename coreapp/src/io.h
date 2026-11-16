// What a core application can reach of its core (T93 D5): the register block at the core window, the DDR2 window, the
// ROM aperture and the base RAM window, the machine stop and the app's reset. Board code is hw.cc; tests/coreapp/ has a
// model of each core behind the same interface, so the descriptors' hooks run natively.
#ifndef COREAPP_IO_H
#define COREAPP_IO_H

#include <stdint.h>
#include <string.h>

class CoreIo {
public:
    virtual ~CoreIo() { }
    // The register block, byte registers at IOBASE + 0x1A0000 + reg (page 0; page 6 is the pixel layer, see video.h).
    virtual uint8_t rd(uint32_t reg) = 0;
    virtual void wr(uint32_t reg, uint8_t v) = 0;
    // The DDR2 window: bytes 0x01000000.. of the application's address space; off is relative to the window.
    virtual void ddr_write(uint32_t off, const void *p, uint32_t n) = 0;
    virtual void ddr_read(uint32_t off, void *p, uint32_t n) = 0;
    // The ROM aperture at U64_ROMS_BASE: 32 KB, as ultimate_sys.vhd decodes 0x188000-0x18FFFF - BASIC at +0 (8 KB), KERNAL at +0x2000
// (8 KB), CHAR at +0x4000 (4 KB, mirrored at +0x5000 and +0x6000), the colour RAM at +0x7000 (1 KB, mirrored up to +0x7FFF). On a core
// with several arrays (the Atari) the first 16 KB show the one the core's ROM_SEL picks.
    virtual void rom_write(uint32_t off, const void *p, uint32_t n) = 0;
    virtual void rom_read(uint32_t off, void *p, uint32_t n) = 0;
    // The base RAM window at C64_MEMORY_BASE (64 KB per core page).
    virtual void ram_write(uint32_t addr, const void *p, uint32_t n) = 0;
    virtual void ram_read(uint32_t addr, void *p, uint32_t n) = 0;
    // C64_DMA_MEMONLY (C64_IO_BASE + 3), T104.2 D6: on, every byte of the RAM window goes to the RAM's second port - $D000-$DFFF
    // included, which is a bus cycle through the PLA otherwise, machine running or stopped (ultimate_sys.vhd, G06). Cores without
    // the register ignore it.
    virtual void ram_memonly(bool) { }
    // The keyboard board's flags byte (BLING_RX_FLAGS, host/bling_io.v +2): b1 SHIFT LOCK is down (the C64U's lock state), b0 lock_off (write:
    // the matrix gets no SHIFT from the lock). Cores whose machine has its own CAPS LOCK (the Apple //e) take the state and set lock_off.
    // A host without the register reads 0 and ignores the write.
    virtual uint8_t bling_flags() { return 0; }
    virtual void bling_lock_off(bool) { }
    // The host's 16 entry palette of the scaler path (C64_PALETTE, T59): entry n's red, green and blue, one byte each. A core whose
    // picture is the 4-bit colour index of that path (the ZX Spectrum, T91 D6) has the application write its colours; the others never call it.
    virtual void palette(uint8_t, uint8_t, uint8_t, uint8_t) { }
    // C64_VIDEOFORMAT (C64_IO_BASE + 1, T140 D24): the System Mode's bits for the host's mode controller (host/mode_ctl.v): b0 NTSC colour encoding,
    // b1 60 Hz, b3 NTSC colour clock, b5 65 cycles. A core whose clock and VIC follow the System Mode (the C128) has the application write the four
    // modes' values; a host or core without the register ignores the write.
    virtual void video_format(uint8_t) { }
    // ---- T104.2 D9.1, the built-in drive A (`cbm1541`): DRIVE_A_BASE (0x10020000), 16 KB, the same address on the monolith and on every
    // MultiCore core that carries drive_l2. Byte registers (0 .. 14), the dirty flags at 0x800, the track parameter RAM at 0x1000 (write
    // only: two little endian 32 bit words per half track, side 1 at +0x400) and the WD177x block of a 1581 at 0x1800. `drv_present()` is
    // false on a core without the window (the defaults below), so nothing in the library touches a drive that is not there.
    virtual bool drv_present() { return false; }
    virtual uint8_t drv_rd(uint32_t off) { (void)off; return 0; }
    virtual void drv_wr(uint32_t off, uint8_t v) { (void)off; (void)v; }
    // One DDR2 word of the parameter RAM or the WD177x DMA address, little endian, byte by byte (the bus is 8 bit).
    void drv_wr32(uint32_t off, uint32_t v)
    {
        for (int i = 0; i < 4; i++) {
            drv_wr(off + (uint32_t)i, (uint8_t)(v >> (8 * i)));
        }
    }
    uint32_t drv_rd32(uint32_t off)
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            v |= (uint32_t)drv_rd(off + (uint32_t)i) << (8 * i);
        }
        return v;
    }
    // The drive's fixed DDR2 areas, absolute addresses (linker.x: __drive_a_sound 0x00EC0000, __drive_a_area 0x00EE0000 - the drive's
    // 64 KB image: RAM below, the ROM at +0x8000). They lie outside the application and the DDR2 window above, so they have a call of their
    // own; the board restricts it to 0x00EA8000 .. 0x00F00000 and ignores the rest.
    virtual void ddr_abs_write(uint32_t addr, const void *p, uint32_t n) { (void)addr; (void)p; (void)n; }
    virtual void ddr_abs_read(uint32_t addr, void *p, uint32_t n) { (void)addr; memset(p, 0, n); }
    // A buffer of the application's heap that the drive hardware reads and writes by its DDR2 address (the GCR track images, the 1581's sector
    // buffer): the pointer for the CPU, and in *phys the address the hardware is given. Gideon's `new uint8_t[]` is that on the board (CPU
    // address = DDR2 address); the model of a test returns host memory and a made-up address. Null when out of memory or no drive.
    // The memory stays the library's until the application ends.
    virtual uint8_t *drv_buffer(uint32_t bytes, uint32_t *phys) { (void)bytes; (void)phys; return 0; }
    // T104.2 D9.3, update.run_record: what the board is called in a `.cfw`'s pre-header (getProductFamily "U64MK2", getProductString), and the jump into
    // the staged updater. `run_at` does not return on the board: the video layer is yielded, interrupts are off, the CPU runs at addr. A model
    // records the address and returns.
    virtual const char *product_family() { return ""; }
    virtual const char *product_name() { return ""; }
    virtual void run_at(uint32_t addr) { (void)addr; }
    // T104.2 D9, the cartridge start of the C64: the control registers at C64_CARTREGS_BASE (+0 MODE, +5 CART_TYPE, +8 REU_ENABLE, +0xE SAMPLER_ENABLE)
    // and the bus bridge's at C64_IO_BASE (+0x2B BUS_INTERNAL, +0x2C BUS_EXTERNAL), both byte registers, `reg` the offset in its block. A core
    // without them ignores the write and reads 0 (the defaults), so only the C64 descriptor ever calls them.
    virtual uint8_t cart_rd(uint32_t reg) { (void)reg; return 0; }
    virtual void cart_wr(uint32_t reg, uint8_t v) { (void)reg; (void)v; }
    virtual void bus_wr(uint32_t off, uint8_t v) { (void)off; (void)v; }
    // T154, the GMod2's EEPROM window (EEPROM_BASE, Gideon's 0x1004C000): byte 0 is the dirty flag (read: the EEPROM was written since the last write
    // of this byte, any write clears it), the 2 KB of data are at +2048. One byte per access. A core without the window reads 0 and ignores the write.
    virtual void eeprom_read(uint32_t off, void *p, uint32_t n) { (void)off; memset(p, 0, n); }
    virtual void eeprom_write(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; }
    // T159: U64_CASELED_SELECT, the sources of the two case LEDs (low nibble LED 1, high nibble LED 2; host/case_leds.v). A host without it ignores the write.
    virtual void case_led_select(uint8_t) { }
    // T160 D1 / T164 WP11: the host's master volume, a byte at U64_AUDIO_MIXER + 0x50 (ultimate_sys.vhd, trx_gain.v in host/top.v): 128 unity, 0 silent,
    // 255 +6 dB. A host without it ignores the write.
    virtual void master_volume(uint8_t) { }
    // T189: the host's tone preset, a byte at U64_AUDIO_MIXER + 0x51 (ultimate_sys.vhd, trx_tone.v in host/top.v): 0 Flat, 1 Bass boost, 2 Treble boost. A host
    // without it ignores the write.
    virtual void master_tone(uint8_t) { }
    // T181: the host's hotkey mask registers (U64_AUDIO_MIXER + 0x60 ...): MOD_A, MOD_B, the 8 mask bytes, the RESTORE bit (hotkeys.h hk_regs). A host without them
    // ignores the writes.
    virtual void hotkey_regs(const uint8_t r[11]) { (void)r; }
    // T206: the application's own keys in the core's key matrix (MATRIX_KEYB, U2P_IO_BASE + 0x300; host/ultimate_sys.vhd page 001, host/app_input.v ORs them with
    // the physical keyboard): m[col] bit row = down, the same col * 8 + row index as hotkeys.h. All zero releases every key the application holds. A host
    // without the register ignores the write.
    virtual void matrix_keys(const uint8_t m[8]) { (void)m; }
    // T181: the physical keys now, whatever the core is shown: m[col] bit row = down, and the RESTORE level. False: this host cannot say.
    virtual bool keys_read(uint8_t m[8], bool *restore) { (void)m; (void)restore; return false; }
    // The host's machine stop (C64_STOP) and the app's reset (C64_MODE reset / unreset).
    virtual void stop(bool on) = 0;
    virtual void mode_reset(bool on) = 0;
    virtual uint32_t now_ms() = 0;
    virtual void delay_ms(uint32_t ms) = 0;
    virtual void delay_us(uint32_t us) = 0;      // busy wait, a few microseconds to a few milliseconds (SIO timing)
    // The wall clock of the board (T116 Q10: the ZX Spectrum Next's DS1307 answers from it): t = year 0-99, month 1-12, day 1-31, hour, minute,
    // second. False when the board has no clock; the caller then makes one up. The default is "no clock", so only the board and the tests that
    // want a time override it.
    virtual bool wall_time(uint8_t t[6]) { (void)t; return false; }
};

#endif
