// A model of the C64 core behind CoreIo, as far as the application can see it (T104.2): the RAM window with the rule that decides whether a
// byte is a bus cycle through the PLA or goes to the RAM's second port (ultimate_sys.vhd, the r_dma branch: $D000-$DFFF is a bus cycle
// unless MEMONLY, a write of $0000/$0001 is one while C64_STOP is set, every other byte is one only with C64_STOP set and MEMONLY off), the
// ROM aperture with its mirrors, the machine's reset and the READY it reaches after the KERNAL has run, the gate bit of C64_STOP, the state
// bus with the CPU's flip-flops (c64_core.v chip 1 w0-w14) and the debug block's CMD/STATUS (trx_dbg.v). It is the bench's reading of
// those files, not the files: what it cannot show is what the gateware does.
#ifndef TESTS_COREAPP_C64_MODEL_H
#define TESTS_COREAPP_C64_MODEL_H

#include <map>
#include "fakes.h"
#include "c64_regs.h"

struct C64Model : FakeIo {
    // ---- configuration
    uint32_t boot_ms = 700;               // reset released -> the KERNAL has run and BASIC says READY
    int halt_after_ms = 3;                // C64_STOP -> the gate bit (b8 of GATE_CTRL); -1 never
    int pause_after_ms = 4;               // PAUSE_IB -> the machine stands on an instruction boundary; -1 never
    int run_after_ms = 6;                 // RUN -> the gate is open again (frame alignment) and a staged CPU load has happened
    bool cpu_write = true;                // CAPS2 b2
    bool refuse_stage = false;            // the state bus refuses the staged words (the gate is not shut)

    // ---- the memory
    std::vector<uint8_t> ap;              // the 32 KB ROM aperture
    bool memonly = false;
    uint8_t ddr = 0x2F, port = 0x37;      // the 6510 port, $00 / $01
    int bus_cycles = 0;                   // bytes that went through the PLA as bus cycles
    int ram_port_bytes = 0;
    int writes_while_stopped = 0;         // bytes written while C64_STOP was set
    int writes_while_debug_stop = 0;      // ... while the debugger held the machine
    std::vector<std::pair<uint16_t, uint8_t> > chip_writes;     // bus cycles that reached a chip in $D000-$DFFF

    // ---- the machine
    bool booted = false;
    uint64_t ran_us = 0;                  // microseconds the machine has run since the reset was released (C64_STOP stops the clock)
    bool released = false;
    uint16_t exec_pc = 0xE5CD;            // the address of the next instruction the CPU runs
    uint8_t cpu_a = 0x11, cpu_x = 0x22, cpu_y = 0x33, cpu_s = 0xF5, cpu_p = 0x2D;   // P with I and D set: it stopped inside an interrupt handler
    uint32_t ff[7];                       // chip 1 w8-w14, the CPU's flip-flops
    int cpu_loads = 0;
    std::vector<uint32_t> assert_t, release_t;   // when (ms) C64_MODE held and released the machine

    // ---- registers behind the pages
    uint32_t gate_ctrl = 0;
    uint32_t owner = 0;
    uint32_t sb_addr = 0, sb_latch = 0, stage[7], stage_acc = 0;
    bool sb_refused = false, load_armed = false;
    uint32_t ipc_set = 0;
    bool stop_set_at_valid = false;
    uint64_t stop_us = 0;
    bool pause_req = false, run_req = false;
    uint64_t pause_us = 0, run_us = 0;
    bool shut = false, at_ib = false;
    uint32_t last_ipc_set = 0;
    std::vector<uint32_t> cmds;
    std::map<uint32_t, uint32_t> acc;     // the bytes of a 32 bit register collected until byte 3 commits it

    // ---- the cartridge start (c64_cart.cc): the control registers, the bus bridge, the cartridge block of the TRX IO block (page 4) and the
    // cartridge RAM at 0x00EF0000. ROM_SEL reads back what is written to it when the bitstream has it (has_rom_sel).
    bool has_rom_sel = true;
    uint8_t cartreg[16] = {};
    uint8_t busreg[0x40] = {};
    std::map<uint32_t, uint8_t> p4;
    std::vector<uint8_t> cart_ram = std::vector<uint8_t>(0x10000, 0xAA);
    uint8_t cart_rd(uint32_t reg) { return cartreg[reg & 15]; }
    void cart_wr(uint32_t reg, uint8_t v)
    {
        cartreg[reg & 15] = v;
        if (reg == C64_CARTREG_TYPE) events.push_back("cart_type " + std::to_string(v));
    }
    void bus_wr(uint32_t off, uint8_t v) { busreg[off & 0x3F] = v; }
    void ddr_abs_write(uint32_t addr, const void *p, uint32_t n)
    {
        if (addr >= 0x00EF0000u && addr + n <= 0x00F00000u) memcpy(&cart_ram[addr - 0x00EF0000u], p, n);
    }
    // T154: the GMod2's EEPROM window (EEPROM_BASE): byte 0 the dirty flag, the 2 KB at +2048. `ee_tear` copies are torn by a write of the C64 (WR_COUNT moves).
    std::vector<uint8_t> ee = std::vector<uint8_t>(4096, 0xFF);
    int ee_tear = 0;
    void eeprom_read(uint32_t off, void *p, uint32_t n)
    {
        if (off >= 2048 && ee_tear > 0) { ee_tear--; p4[C64_CART_WR_COUNT] = (uint8_t)(p4_get(C64_CART_WR_COUNT) + 1); }
        memcpy(p, &ee.at(off), n);
    }
    void eeprom_write(uint32_t off, const void *p, uint32_t n)
    {
        if (off == 0) { ee[0] = 0; return; }
        memcpy(&ee.at(off), p, n);
    }
    uint8_t p4_get(uint32_t reg) const { auto it = p4.find(reg); return it == p4.end() ? 0 : it->second; }

    C64Model()
    {
        ap.assign(0x8000, 0);
        memset(ff, 0, sizeof(ff));
        memset(stage, 0, sizeof(stage));
        regs[C64_CAPS] = C64_CAPS_STATEBUS | C64_CAPS_DEBUG | 0x01 | 0x02 | 0x04 | 0x10;
        regs[C64_CAPS2] = C64_CAPS2_CPUWRITE;
        // the CPU's image: PC = the idle loop's opcode, IR/din something else
        setbits(ff, B_PC, 16, 0xE5CD);
        setbits(ff, B_IR, 8, 0xA5);
        setbits(ff, B_DIN, 8, 0xA5);
        setbits(ff, B_CTRL + 12, 1, 1);
        setbits(ff, B_CTRL + 15, 1, 1);
        setbits(ff, B_CTRL + 16, 1, 1);
    }

    // the same bit layout as c64_go.cc (cpu6510.v sb_cur)
    enum { B_A = 0, B_X = 8, B_Y = 16, B_S = 24, B_P = 32, B_PC = 40, B_IR = 80, B_CTRL = 102, B_DIN = 130 };
    static void setbits(uint32_t *w, int lo, int n, uint32_t v)
    {
        for (int i = 0; i < n; i++) { int b = lo + i; w[b >> 5] = (w[b >> 5] & ~(1u << (b & 31))) | (((v >> i) & 1u) << (b & 31)); }
    }
    static uint32_t getbits(const uint32_t *w, int lo, int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) { int b = lo + i; v |= ((w[b >> 5] >> (b & 31)) & 1u) << i; }
        return v;
    }

    // ---- the RAM window
    bool io_visible() const
    {
        uint8_t eff = (uint8_t)((port | ~ddr) & 7);
        return (eff & 4) && (eff & 3);
    }
    bool dma_bus(uint16_t a, bool write) const
    {
        if ((a >> 12) == 0xD) return !memonly;
        if (write && a < 2) return stopped;
        return stopped && !memonly;
    }
    void put(uint16_t a, uint8_t v)
    {
        if (stopped) writes_while_stopped++;
        if (owner && shut) writes_while_debug_stop++;
        if (!dma_bus(a, true)) {
            ram_port_bytes++;
            ram[a] = v;
            return;
        }
        bus_cycles++;
        if (a == 0) { ddr = v; ram[a] = v; return; }
        if (a == 1) { port = v; ram[a] = v; return; }
        if ((a >> 12) == 0xD && io_visible()) {
            chip_writes.push_back(std::make_pair(a, v));
            return;
        }
        ram[a] = v;
    }
    void ram_write(uint32_t a, const void *p, uint32_t n)
    {
        const uint8_t *s = (const uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) put((uint16_t)(a + i), s[i]);
    }
    void ram_read(uint32_t a, void *p, uint32_t n)
    {
        uint8_t *d = (uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t x = (uint16_t)(a + i);
            d[i] = (dma_bus(x, false) && (x >> 12) == 0xD && io_visible()) ? 0xFF : ram[x];
        }
    }
    void ram_memonly(bool on) { memonly = on; }

    // ---- the ROM aperture: char mirrored at +0x5000/+0x6000, the colour RAM at +0x7000
    static uint32_t ap_index(uint32_t off)
    {
        off &= 0x7FFF;
        if ((off & 0x7000) == 0x7000) return 0x7000 + (off & 0x3FF);
        if (off & 0x4000) return 0x4000 + (off & 0xFFF);
        return off;
    }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        events.push_back("rom_write " + std::to_string(off) + " " + std::to_string(n));
        const uint8_t *s = (const uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) ap[ap_index(off + i)] = s[i];
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        uint8_t *d = (uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) d[i] = ap[ap_index(off + i)];
    }

    // ---- reset and READY
    void mode_reset(bool on)
    {
        FakeIo::mode_reset(on);
        (on ? assert_t : release_t).push_back(t);
        if (on) {
            released = false;
            booted = false;
        } else {
            released = true;
            ran_us = 0;
            ramtas();                     // the KERNAL's first act after a reset clears the zero page, the stack and $0200-$03FF
        }
    }
    void ramtas()
    {
        for (int i = 2; i < 0x102; i++) ram[i] = 0;
        for (int i = 0x200; i < 0x400; i++) ram[i] = 0;
    }
    void boot_kernal()
    {
        booted = true;
        ramtas();
        ram[0x2B] = 0x01; ram[0x2C] = 0x08;
        ram[0x0314] = 0x31; ram[0x0315] = 0xEA;
        ram[0x0316] = 0x66; ram[0x0317] = 0xFE;
        ram[0xC6] = 0;
        exec_pc = 0xE5CD;
        setbits(ff, B_PC, 16, 0xE5CD);
    }
    // the system is alive when BASIC's pointers and the KERNAL's vectors are where the KERNAL put them: what a typed SYS runs through
    bool system_alive() const
    {
        return ram[0x2B] == 0x01 && ram[0x2C] == 0x08 && ram[0x0314] == 0x31 && ram[0x0315] == 0xEA;
    }
    void advance(uint32_t u)
    {
        FakeIo::advance(u);
        if (released && !booted && !stopped) {
            ran_us += u;
            if (ran_us >= (uint64_t)boot_ms * 1000) boot_kernal();
        }
    }

    // ---- the gate bit of C64_STOP
    void stop(bool on)                    // not in `events`: those are the reset and the ROMs (the menu's stop is not the subject of them)
    {
        stopped = on;
        stop_calls++;
        if (on) stop_us = us;
    }
    bool halted() const { return stopped && halt_after_ms >= 0 && us - stop_us >= (uint64_t)halt_after_ms * 1000; }

    // ---- the debug block, evaluated lazily against virtual time
    void settle()
    {
        if (pause_req && pause_after_ms >= 0 && us - pause_us >= (uint64_t)pause_after_ms * 1000) {
            pause_req = false;
            shut = at_ib = true;
        }
        if (run_req && us - run_us >= (uint64_t)run_after_ms * 1000) {
            run_req = false;
            shut = at_ib = false;
            if (load_armed) {
                load_armed = false;
                apply_stage();
            }
        }
    }
    void apply_stage()
    {
        memcpy(ff, stage, sizeof(ff));
        exec_pc = (uint16_t)getbits(ff, B_PC, 16);
        cpu_a = (uint8_t)getbits(ff, B_A, 8); cpu_x = (uint8_t)getbits(ff, B_X, 8); cpu_y = (uint8_t)getbits(ff, B_Y, 8);
        cpu_s = (uint8_t)getbits(ff, B_S, 8); cpu_p = (uint8_t)getbits(ff, B_P, 8);
        cpu_loads++;
    }
    uint32_t sb_word(uint32_t addr)
    {
        uint32_t chip = addr >> 7, w = addr & 0x7F;
        if (chip != 1) return 0;
        switch (w) {
        case 0: case 4: return cpu_a | (cpu_x << 8) | (cpu_y << 16) | ((uint32_t)cpu_s << 24);
        case 1: return (uint32_t)getbits(ff, B_PC, 16) | ((uint32_t)cpu_p << 16);
        case 2: return ddr | ((uint32_t)port << 8) | ((uint32_t)((port | ~ddr) & 0xFF) << 16);
        case 3: return exec_pc | ((uint32_t)(getbits(ff, B_DIN, 8)) << 16) | (at_ib ? (1u << 24) : 0);
        case 5: return cpu_p;
        default:
            if (w >= 8 && w <= 14) return ff[w - 8];
            return 0;
        }
    }

    uint32_t reg32(uint32_t reg) const
    {
        switch (reg) {
        case C64_GATE_CTRL: return (gate_ctrl & 1) | (halted() ? 0x100 : 0);
        case C64_DBG_STATUS: return (shut ? C64_ST_SHUT : 0) | (at_ib ? C64_ST_AT_IB : 0);
        case C64_SB_STATUS: return (shut ? 1 : 0) | (sb_refused ? 2 : 0);
        case C64_SB_LOAD: return load_armed ? 2 : 0;
        case C64_SB_ADDR: return sb_addr;
        default: return 0;
        }
    }

    // the 32 bit registers (trx_sys.v): a read of byte 0 latches the word, bytes 1-3 come from the latch - a byte read alone sees what the
    // last read of byte 0 left, so a status polled by its second byte never changes
    std::map<uint32_t, uint32_t> latch;
    uint8_t rd(uint32_t reg)
    {
        settle();
        if (reg >= 0x4000 && reg < 0x5000) return (reg == C64_CART_ROM_SEL && !has_rom_sel) ? 0 : p4_get(reg);
        if (reg == C64_CAPS2) return cpu_write ? regs[C64_CAPS2] : 0;
        if (reg >= 0x1000 || (reg >= 0x100 && reg < 0x140)) {
            uint32_t base = reg & ~3u;
            if ((reg & 3) == 0) {
                if (base == C64_SB_DATA) { latch[base] = sb_word(sb_addr); sb_addr++; }
                else latch[base] = reg32(base);
            }
            return (uint8_t)(latch[base] >> (8 * (reg & 3)));
        }
        return FakeIo::rd(reg);
    }
    void wr(uint32_t reg, uint8_t v)
    {
        settle();
        if (reg >= 0x4000 && reg < 0x5000) {
            if (reg == C64_CART_ROM_SEL && !has_rom_sel) return;
            p4[reg] = v;
            log.push_back({reg, v, t});
            return;
        }
        if (reg >= 0x1000 || (reg >= C64_GATE_CTRL && reg < 0x140)) {
            log.push_back({reg, v, t});
            uint32_t base = reg & ~3u;
            uint32_t &a32 = acc[base];
            a32 = (a32 & ~(0xFFu << (8 * (reg & 3)))) | ((uint32_t)v << (8 * (reg & 3)));
            if ((reg & 3) != 3) return;
            uint32_t val = a32;
            switch (base) {
            case C64_SB_ADDR: sb_addr = val & 0xFFF; break;
            case C64_SB_DATA:
                if (((sb_addr >> 7) == 1) && (sb_addr & 0x7F) >= 8 && (sb_addr & 0x7F) <= 14) {
                    if (shut && !refuse_stage) stage[(sb_addr & 0x7F) - 8] = val; else sb_refused = true;
                }
                sb_addr++;
                break;
            case C64_SB_STATUS: sb_refused = false; break;
            case C64_SB_LOAD: if (val & 2) load_armed = true; break;
            case C64_DBG_CMD:
                cmds.push_back(val);
                if (val == C64_CMD_PAUSE_IB) { pause_req = true; pause_us = us; }
                if (val == C64_CMD_RUN) { run_req = true; run_us = us; pause_req = false; }
                break;
            case C64_DBG_IPC_SET: last_ipc_set = val; break;
            case C64_GATE_CTRL: gate_ctrl = val; events.push_back("gate_ctrl " + std::to_string(val)); break;
            case C64_DBG_OWNER: owner = val & 1; break;
            default: break;
            }
            return;
        }
        FakeIo::wr(reg, v);
    }
};

#endif
