// A model of drive A as the application sees it (T104.2 D9.1): the DRIVE_A_BASE window, the drive's DDR2 areas, and the buffers the hardware
// reads by DDR2 address. The bench's reading of cores/c64u/gideon/drive_registers.vhd, floppy.vhd (dirty flags) and floppy_param_mem.vhd,
// not the gateware: byte registers, a drive type register that only a multi mode (mm_drive) gateware has, the dirty flags (read b7 = any
// track written, b0 = this track; write b7 clears "any", otherwise clears this track), the write-only parameter RAM, and the WD177x block
// with the FIFO flag in irq_ack b7.
//
// "The head" is played by the helpers below: read a track's bytes through the parameter RAM as the hardware would, write bytes into a
// track's memory and set the dirty flags.
#ifndef TESTS_COREAPP_DRIVE_MODEL_H
#define TESTS_COREAPP_DRIVE_MODEL_H

#include <vector>
#include <deque>
#include <string>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "io.h"

struct DriveModel {
    bool present = true;
    bool multi = false;                    // mm_drive: the drive type register takes a 2 (the 1581)
    uint8_t reg[16];
    uint8_t type_latched = 0;
    bool dirty[128];
    bool any_dirty = false;
    std::vector<uint8_t> param;            // 0x1000 .. 0x17FF, write only
    uint8_t wd[16];
    struct Cmd { uint16_t cmd; bool done; };
    std::deque<Cmd> fifo;
    uint8_t wd_status = 0;                 // status_set / status_clear folded
    std::vector<uint8_t> area;             // 0x00EA8000 .. 0x00F00000
    struct Buf { uint32_t phys; std::vector<uint8_t> mem; };
    std::vector<Buf *> bufs;
    uint32_t next_phys = 0x00100000;
    std::vector<std::string> log;          // register writes worth a sequence check: "sensor=0", "inserted=1" ...
    int abs_writes = 0, abs_rejected = 0;
    bool fail_alloc = false;

    DriveModel() : param(0x800, 0), area(0x58000, 0)
    {
        memset(reg, 0, sizeof(reg));
        memset(wd, 0, sizeof(wd));
        memset(dirty, 0, sizeof(dirty));
    }
    ~DriveModel() { for (Buf *b : bufs) delete b; }

    // ---- CoreIo side ----
    uint8_t rd(uint32_t off)
    {
        if (off < 16) {
            if (off == 13) return multi ? (uint8_t)(type_latched & 3) : 0;
            return reg[off];
        }
        if (off >= 0x800 && off < 0x880) {
            return (uint8_t)((any_dirty ? 0x80 : 0) | (dirty[off - 0x800] ? 1 : 0));
        }
        if (off >= 0x1800 && off < 0x1810) {
            uint32_t r = off - 0x1800;
            if (r == 6) return (uint8_t)((fifo.empty() ? 0 : 0x80) | ((!fifo.empty() && fifo.front().done) ? 1 : 0));
            if (r == 0 && !fifo.empty()) return (uint8_t)(fifo.front().cmd & 0xFF);
            return wd[r];
        }
        return 0;
    }
    void wr(uint32_t off, uint8_t v)
    {
        if (off < 16) {
            static const char *names[16] = { "power", "reset", "hw_addr", "sensor", "inserted", "rammap", "side", "manwrite", "track", "status",
                                             "r10", "r11", "diskchange", "type", "sounds", "r15" };
            char b[40];
            snprintf(b, sizeof(b), "%s=%d", names[off], v);
            log.push_back(b);
            if (off == 13) {
                if (multi) type_latched = (uint8_t)((v & 2));
                return;
            }
            reg[off] = v;
            return;
        }
        if (off >= 0x800 && off < 0x880) {
            if (v & 0x80) any_dirty = false; else dirty[off - 0x800] = false;
            return;
        }
        if (off >= 0x1000 && off < 0x1800) {
            param[off - 0x1000] = v;
            return;
        }
        if (off >= 0x1800 && off < 0x1810) {
            uint32_t r = off - 0x1800;
            if (r == 4) { wd_status &= (uint8_t)~v; return; }
            if (r == 5) { wd_status |= v; return; }
            if (r == 6) { if (!fifo.empty()) fifo.pop_front(); return; }
            wd[r] = v;
            return;
        }
    }
    void abs_write(uint32_t addr, const void *p, uint32_t n)
    {
        if (addr < 0x00EA8000u || addr >= 0x00F00000u || n > 0x00F00000u - addr) { abs_rejected++; return; }
        abs_writes++;
        memcpy(&area[addr - 0x00EA8000u], p, n);
    }
    void abs_read(uint32_t addr, void *p, uint32_t n)
    {
        if (addr < 0x00EA8000u || addr >= 0x00F00000u || n > 0x00F00000u - addr) { memset(p, 0, n); return; }
        memcpy(p, &area[addr - 0x00EA8000u], n);
    }
    uint8_t *buffer(uint32_t bytes, uint32_t *phys)
    {
        if (fail_alloc) return 0;
        Buf *b = new Buf;
        b->phys = next_phys;
        next_phys += (bytes + 0xFFFu) & ~0xFFFu;
        b->mem.assign(bytes, 0);
        bufs.push_back(b);
        *phys = b->phys;
        return b->mem.data();
    }
    // the host memory behind a DDR2 address the hardware was given
    uint8_t *host(uint32_t phys)
    {
        for (Buf *b : bufs) {
            if (phys >= b->phys && phys < b->phys + b->mem.size()) return &b->mem[phys - b->phys];
        }
        return 0;
    }

    // ---- the drive ROM and RAM as the CPU sees them ----
    uint8_t ram_byte(uint32_t off) { uint8_t b; abs_read(0x00EE0000u + off, &b, 1); return b; }
    std::vector<uint8_t> rom() { std::vector<uint8_t> v(0x8000); abs_read(0x00EE8000u, v.data(), 0x8000); return v; }

    // ---- the head ----
    uint32_t word(uint32_t off) { return (uint32_t)param[off] | ((uint32_t)param[off + 1] << 8) | ((uint32_t)param[off + 2] << 16) | ((uint32_t)param[off + 3] << 24); }
    uint32_t track_phys(int half) { return word((uint32_t)half * 8); }
    uint32_t track_len(int half) { return (word((uint32_t)half * 8 + 4) & 0xFFFF) + 1; }
    uint32_t track_bit_time(int half) { return word((uint32_t)half * 8 + 4) >> 16; }
    std::vector<uint8_t> read_track(int half)
    {
        uint8_t *p = host(track_phys(half));
        if (!p) return std::vector<uint8_t>();
        return std::vector<uint8_t>(p, p + track_len(half));
    }
    // the head wrote `n` bytes at `at` of the half track and the hardware raised the flags
    void head_write(int half, uint32_t at, const uint8_t *p, uint32_t n)
    {
        uint8_t *m = host(track_phys(half));
        if (m) memcpy(m + at, p, n);
        dirty[half / 2] = true;
        any_dirty = true;
    }
    // the drive's 1581 sends a command
    void wd_command(uint16_t cmd, bool dma_done = false)
    {
        fifo.push_back({ (uint16_t)(cmd & 0xFF), dma_done });
        if (dma_done) fifo.back().cmd = cmd;
    }
    uint32_t wd_dma_addr() { return (uint32_t)wd[8] | ((uint32_t)wd[9] << 8) | ((uint32_t)wd[10] << 16) | ((uint32_t)wd[11] << 24); }
    uint32_t wd_dma_len() { return (uint32_t)wd[12] | ((uint32_t)wd[13] << 8); }
};

// A CoreIo mixin: the class that derives from a core's model and from this forwards the drive calls.
#define DRIVE_IO_FORWARD(DM) \
    bool drv_present() { return (DM).present; } \
    uint8_t drv_rd(uint32_t off) { return (DM).rd(off); } \
    void drv_wr(uint32_t off, uint8_t v) { (DM).wr(off, v); } \
    void ddr_abs_write(uint32_t addr, const void *p, uint32_t n) { (DM).abs_write(addr, p, n); } \
    void ddr_abs_read(uint32_t addr, void *p, uint32_t n) { (DM).abs_read(addr, p, n); } \
    uint8_t *drv_buffer(uint32_t bytes, uint32_t *phys) { return (DM).buffer(bytes, phys); }

#endif
