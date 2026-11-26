// A model of the diag core's probe engine v2 behind CoreIo (T260): the register map of docs/specs/T260 §5 and the step words of dtv2.h; the pin table is cores/diag/rtl/pins.toml (read from the
// repository root, as diag_examples.h reads the shipped tests), the pins of the ports and the comparators are set by the test. Used by test_diagtest.cc, test_dtv2.cc and the render bench.
#ifndef TESTS_COREAPP_DIAG_ENGINE_H
#define TESTS_COREAPP_DIAG_ENGINE_H

#include <deque>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include "fakes.h"
#include "dtv2.h"

// ---- the pin table of the bitstream: cores/diag/rtl/pins.toml, the one source the gateware's ROM is generated from (tools/diag_pins_gen.py)
struct PinEnt { std::string name; int dir, cls, flags, group, idle; };

static inline int pe_word(const std::string &s, const char *key, const char *const *vals, int nv, int def)
{
    size_t p = s.find(std::string(key) + " = \"");
    if (p == std::string::npos) { return def; }
    p += strlen(key) + 4;
    for (int i = 0; i < nv; i++) { if (!s.compare(p, strlen(vals[i]), vals[i]) && s[p + strlen(vals[i])] == '"') { return i; } }
    return def;
}
static inline bool pe_flag(const std::string &s, const char *key) { return s.find(std::string(key) + " = true") != std::string::npos; }

static inline std::vector<PinEnt> engine_load_pins(const char *path = "cores/diag/rtl/pins.toml")
{
    std::ifstream f(path);
    std::string line;
    std::vector<PinEnt> v;
    static const char *const dirs[] = { "in", "out", "inout" };
    static const char *const cls[] = { "none", "od", "pull", "push", "pz", "pzh" };
    static const char *const grp[] = { "slot", "joy", "paddle", "c2n", "iec", "ui", "led", "av", "misc" };
    while (std::getline(f, line)) {
        size_t p = line.find("{ index = ");
        if (p == std::string::npos) { continue; }
        PinEnt e;
        size_t q = line.find("name = \"", p) + 8;
        e.name = line.substr(q, line.find('"', q) - q);
        e.dir = pe_word(line, "dir", dirs, 3, 0);
        e.cls = pe_word(line, "class", cls, 6, 0);
        e.group = pe_word(line, "group", grp, 9, 8);
        e.flags = (pe_flag(line, "guard") ? 1 : 0) | (pe_flag(line, "inv") ? 2 : 0) | (pe_flag(line, "buf") ? 4 : 0) | (pe_flag(line, "pwr") ? 8 : 0) | (pe_flag(line, "gen") ? 16 : 0) | (pe_flag(line, "host") ? 32 : 0);
        size_t ip = line.find("idle = ");
        e.idle = ip == std::string::npos ? 2 : atoi(line.c_str() + ip + 7);
        v.push_back(e);
    }
    return v;
}

// ---- the engine --------------------------------------------------------------------------------------------------------------------------------
// The pins of the joystick ports are table indexes 50..54 (port 1: up, down, left, right, fire) and 55..59 (port 2); the paddle comparators 63..66; PADDLE_DIS / ADJ 67 / 68.
enum { EP_J1 = 50, EP_J2 = 55, EP_CMP = 63, EP_DIS = 67, EP_ADJ = 68, EP_GAME = 38, EP_EXROM = 39 };

struct Engine : FakeIo {
    std::vector<PinEnt> tab;
    uint32_t steps[256];
    int wptr = 0, pc = 0;
    bool waitgo = false, done = false, overflow = false;
    std::deque<uint32_t> fifo;
    uint32_t tpus256 = 25600;                          // 100 ticks per microsecond
    uint64_t tick = 0;
    uint8_t mode[128];                                 // what the engine drives per pin: DV2_REL / LOW / HIGH / HIZ
    bool ext[128];                                     // the outside of each pin, 1 = high (not held low from outside)
    uint32_t drivable = 0x3FF;                         // a port pin (bit n = pin 50 + n) the board cannot drive: a DRIVE is accepted and has no effect
    uint32_t forced_low = 0;                           // a port pin held low from outside (bit n = pin 50 + n)
    uint32_t cmp_now = 0;                              // the comparators (bit n = PADDLE_CMPn)
    uint32_t charge[4] = { 0, 0, 0, 0 };               // ticks from the start of a time_to_edge on PADDLE_CMPn until it flips; 0: never
    int abort_writes = 0;
    bool lag_on = false, lagging = false;              // the real engine takes a RUN a few clocks after the write: until the caller waits, STATUS and the FIFO level are the old ones
    uint8_t stale_status = 0;
    bool started = false;                              // RUN seen and the engine has not ended: the RTL's `running`
    int run_count = 0;
    int refused = 0, ref_pc = 0;                       // STATUS b5, REF_CNT, REF_PC
    bool guard_trip = false;
    int rises = 0;                                     // rising edges on pin 6 of port 2 since the last DRIVE of PADDLE_DIS / PADDLE_ADJ: what a pad's shift register would have seen
    std::function<uint32_t(const Engine &)> cmp_fn;    // when set, a SAMPLE reads the comparators from this instead of cmp_now (a model of a pad)
    std::function<uint32_t(int pin, int edge, uint32_t ticks)> edges_fn;   // the count an EDGES step pushes (default 0)
    std::function<void(int)> on_prompt;
    std::vector<uint32_t> prompts_seen;
    std::vector<std::pair<uint32_t, uint8_t>> reg_writes;   // WRITE_REG steps, in order: offset, data
    // capture
    std::vector<uint8_t> ring;
    std::function<uint32_t(const Engine &, uint32_t i)> cap_fn;           // sample i of a capture (default: the lanes' pins as they stand now)
    uint32_t cap_count = 0, cap_lines = 0, raddr = 0;
    bool cap_overrun = false;
    uint32_t cap_lost = 0;

    Engine()
    {
        memset(steps, 0, sizeof(steps));
        memset(mode, 0, sizeof(mode));
        for (bool &e : ext) { e = true; }
        tab = engine_load_pins();
        regs[PD_PIN_COUNT] = (uint8_t)tab.size();
        for (int j = 0; j < 32; j++) { regs[PD_CAP_SEL + j] = 0x7F; }
    }
    int cls(int pin) const { return pin < (int)tab.size() ? tab[pin].cls : 0; }
    bool ext_level(int pin) const
    {
        if (pin >= EP_J1 && pin < EP_J1 + 10) { return !((forced_low >> (pin - EP_J1)) & 1) && ext[pin]; }
        if (pin >= EP_CMP && pin < EP_CMP + 4) { return ((cmp_fn ? cmp_fn(*this) : cmp_now) >> (pin - EP_CMP)) & 1; }
        return ext[pin];
    }
    bool port_dead(int pin) const { return pin >= EP_J1 && pin < EP_J1 + 10 && !((drivable >> (pin - EP_J1)) & 1); }
    // the level the snapshot shows for a pin (the ball for an input, what the FPGA drives for an output, per class)
    bool level(int pin) const
    {
        if (pin >= (int)tab.size()) { return false; }
        int m = port_dead(pin) ? DV2_REL : mode[pin];
        switch (cls(pin)) {
        case 1: return m == DV2_LOW ? false : ext_level(pin);                     // od: wired AND with the outside
        case 2: return m == DV2_LOW;                                               // pull: the snapshot is the output, 1 = pulling the line low
        case 3: return m == DV2_LOW ? false : m == DV2_HIGH ? true : tab[pin].idle == 1;
        case 4: return m == DV2_LOW ? false : m == DV2_HIGH ? true : ext_level(pin);
        case 5: return m == DV2_LOW ? false : m == DV2_HIZ ? ext_level(pin) : true;
        }
        return ext_level(pin);
    }
    uint32_t pins() const { uint32_t v = 0; for (int i = 0; i < 10; i++) { v |= (uint32_t)level(EP_J1 + i) << i; } return v; }     // the ten port pins, bit n = pin 50 + n
    int n_driven() const { int n = 0; for (int i = 0; i < 128; i++) { n += mode[i] != DV2_REL; } return n; }
    // the last PADCTL of engine v1, as the two pins say it: b0 DIS level, b1 DIS driven, b2 ADJ level, b3 ADJ driven (what a model of a pad behind the pot levers reads)
    uint32_t padctl() const { return (mode[EP_DIS] == DV2_HIGH ? 1u : 0) | (mode[EP_DIS] != DV2_REL ? 2u : 0) | (mode[EP_ADJ] == DV2_HIGH ? 4u : 0) | (mode[EP_ADJ] != DV2_REL ? 8u : 0); }
    bool guard_now() const { return !ext[EP_GAME] || !ext[EP_EXROM]; }
    uint32_t snap_word(int k) const { uint32_t w = 0; for (int b = 0; b < 32; b++) { w |= (uint32_t)level(32 * k + b) << b; } return w; }
    void release_all() { memset(mode, 0, sizeof(mode)); }
    void push(uint32_t w) { if (fifo.size() >= 512) { overflow = true; } else { fifo.push_back(w); } }
    void refuse(int word) { if (!refused) { ref_pc = word; } if (refused < 255) { refused++; } }
    void drive(int pin, int m, int word)
    {
        int c = pin < (int)tab.size() ? cls(pin) : 0;
        bool bad = pin >= (int)tab.size() || c == 0 || (m == DV2_HIGH && (c == 1 || c == 2)) || (m == DV2_HIZ && c != 5);
        if (m != DV2_REL && (bad || ((tab[pin].flags & 1) && guard_now()))) { refuse(word); return; }
        bool was = level(pin);
        mode[pin] = (uint8_t)m;
        if (pin == EP_J2 + 4 && !was && level(pin)) { rises++; }
        if (pin == EP_DIS || pin == EP_ADJ) { rises = 0; }
    }
    // runs `n` words from index i; a two word op takes its second. Returns the index after them.
    int exec(int i, int n)
    {
        int end = i + n;
        while (i < end) {
            uint32_t w = steps[i];
            uint32_t op = w >> 28, arg = w & 0xFFFFFF;
            switch (op) {
            case DV2_OP_DRIVE: drive(w & 0x7F, (w >> 26) & 3, i); break;
            case DV2_OP_WAIT: tick += arg; break;
            case DV2_OP_SAMPLE: push((uint32_t)tick); for (int k = 0; k < 4; k++) { push(snap_word(k)); } break;
            case DV2_OP_EDGES: {
                uint32_t nt = steps[i + 1];
                int pin = w & 0x7F;
                if (pin >= (int)tab.size()) { refuse(i + 1); push(0xFFFFFFFFu); } else { push(edges_fn ? edges_fn(pin, (w >> 26) & 3, nt) : 0); }
                tick += nt;
                i++;
                break;
            }
            case DV2_OP_TTE: {
                uint32_t tmo = steps[i + 1];
                int pin = w & 0x7F;
                uint32_t best = pin >= EP_CMP && pin < EP_CMP + 4 ? charge[pin - EP_CMP] : 0;
                bool hit = best && best <= tmo;
                uint32_t v = hit ? best : tmo;
                tick += v;
                push((hit ? 0u : 0x80000000u) | v);
                i++;
                break;
            }
            case DV2_OP_WRITE_REG: reg_writes.push_back({(w >> 16) & 0xFFF, (uint8_t)w}); regs[(w >> 16) & 0xFFF] = (uint8_t)w; break;
            case DV2_OP_CAPTURE: {
                uint32_t m = steps[i + 1];
                for (uint32_t k = 0; k < m; k++) {
                    uint32_t v = 0;
                    if (cap_fn) { v = cap_fn(*this, k); } else { for (int j = 0; j < 32; j++) { if (regs[PD_CAP_SEL + j] != 0x7F) { v |= (uint32_t)level(regs[PD_CAP_SEL + j]) << j; } } }
                    size_t at = (size_t)cap_lines * 16 + 4 * k;
                    if (ring.size() < at + 4) { ring.resize(at + 4 + 12, 0); }
                    for (int b = 0; b < 4; b++) { ring[at + b] = (uint8_t)(v >> (8 * b)); }
                }
                cap_count += m;
                cap_lines += (m + 3) / 4;
                tick += (uint64_t)arg * m;
                i++;
                break;
            }
            default: break;                                  // CAPWAIT: the model's capture is complete when it starts
            }
            i++;
        }
        return i;
    }
    uint8_t status() const
    {
        return (uint8_t)((started && !done ? 1 : 0) | (waitgo ? 2 : 0) | (done ? 4 : 0) | (overflow ? 8 : 0) | (refused ? 0x20 : 0) | (guard_trip ? 0x80 : 0));
    }
    void settle() { lagging = false; }                 // time passed: the engine has taken the RUN
    void run()
    {
        while (pc < wptr) {
            uint32_t w = steps[pc];
            uint32_t op = w >> 28;
            if (op == DV2_OP_END) { release_all(); done = true; return; }
            if (op == DV2_OP_WAIT_USER) {
                waitgo = true;
                prompts_seen.push_back(w & 0xFFFFFF);
                return;
            }
            if (op == DV2_OP_LOOP) {
                int k = w & 0xFF, c = (w >> 8) & 0xFFFF;
                for (int r = 0; r < c; r++) { exec(pc + 1, k); }
                pc += k + 1;
                continue;
            }
            pc = exec(pc, 1);
        }
        release_all();
        done = true;
    }
    uint8_t rd(uint32_t reg) override
    {
        reg &= 0xFFF;
        if (reg < 4) return "DIAG"[reg];
        if (reg == PD_CAPS) return 0x1F;
        if (reg >= PD_TICKS && reg < PD_TICKS + 4) return (uint8_t)(tpus256 >> (8 * (reg - PD_TICKS)));
        if (reg == PD_STATUS) return lagging ? stale_status : status();
        if (reg == PD_PC) return (uint8_t)pc;
        if (reg == PD_REF_PC) return (uint8_t)ref_pc;
        if (reg == PD_REF_CNT) return (uint8_t)refused;
        if (reg >= PD_FIFO && reg < PD_FIFO + 4) {
            uint32_t w = fifo.empty() ? 0 : fifo.front();
            uint8_t b = (uint8_t)(w >> (8 * (reg - PD_FIFO)));
            if (reg == PD_FIFO + 3 && !fifo.empty()) fifo.pop_front();
            return b;
        }
        if (reg == PD_FIFO_LEVEL) return lagging ? 0 : (uint8_t)fifo.size();
        if (reg == PD_FIFO_LEVEL + 1) return lagging ? 0 : (uint8_t)(fifo.size() >> 8);
        if (reg >= PD_SNAP && reg < PD_SNAP + PD_SNAP_BYTES) { uint32_t k = reg - PD_SNAP; return (uint8_t)(snap_word((int)(k / 4)) >> (8 * (k % 4))); }
        if (reg >= PD_DRVEN && reg < PD_DRVEN + 16) {
            uint8_t b = 0;
            for (int j = 0; j < 8; j++) { int pin = 8 * (int)(reg - PD_DRVEN) + j; if (pin < (int)tab.size() && mode[pin] != DV2_REL) { b |= (uint8_t)(1u << j); } }
            return b;
        }
        if (reg == PD_GUARD) return guard_now() ? 1 : 0;
        if (reg >= PD_PIN_ROM && reg < PD_PIN_ROM + 32) {
            int ix = regs[PD_PIN_IDX], k = (int)(reg - PD_PIN_ROM);
            if (ix >= (int)tab.size()) return 0;
            const PinEnt &e = tab[ix];
            if (k < 16) return k < (int)e.name.size() ? (uint8_t)e.name[k] : 0;
            return k == 16 ? (uint8_t)e.dir : k == 17 ? (uint8_t)e.cls : k == 18 ? (uint8_t)e.flags : k == 19 ? (uint8_t)e.group : k == 20 ? (uint8_t)e.idle : 0;
        }
        if (reg == PD_CAP_STATUS) return (uint8_t)(cap_count || cap_lines ? PD_CAP_ST_WRITTEN : 0) | (cap_overrun ? PD_CAP_ST_OVERRUN : 0) | (regs[PD_CAP_STATUS] & PD_CAP_ST_VALID);
        if (reg >= PD_CAP_COUNT && reg < PD_CAP_COUNT + 4) return (uint8_t)(cap_count >> (8 * (reg - PD_CAP_COUNT)));
        if (reg >= PD_CAP_LOST && reg < PD_CAP_LOST + 4) return (uint8_t)(cap_lost >> (8 * (reg - PD_CAP_LOST)));
        if (reg >= PD_CAP_LINES && reg < PD_CAP_LINES + 3) return (uint8_t)(cap_lines >> (8 * (reg - PD_CAP_LINES)));
        return regs[reg];
    }
    void wr(uint32_t reg, uint8_t v) override
    {
        FakeIo::wr(reg, v);
        reg &= 0xFFF;
        static uint32_t cur;
        if (reg >= PD_STEP && reg < PD_STEP + 4) {
            if (reg == PD_STEP) cur = 0;
            cur |= (uint32_t)v << (8 * (reg - PD_STEP));
            if (reg == PD_STEP + 3) { if (wptr < 256) steps[wptr++] = cur; }
        } else if (reg == PD_STEP_ADDR) {
            wptr = 0;
        } else if (reg >= PD_CAP_RADDR && reg < PD_CAP_RADDR + 3) {
            int b = (int)(reg - PD_CAP_RADDR);
            raddr = (raddr & ~(0xFFu << (8 * b))) | ((uint32_t)v << (8 * b));
            raddr &= ~15u;
        } else if (reg == PD_CAP_RCTRL && (v & 1)) {
            for (int k = 0; k < 16; k++) { regs[PD_CAP_RDATA + k] = raddr + k < ring.size() ? ring[raddr + k] : 0; }
            raddr += 16;
            regs[PD_CAP_STATUS] |= PD_CAP_ST_VALID;
        } else if (reg == PD_CTRL) {
            if (v & PD_CTRL_ABORT) {                       // the RTL: an engine that runs (or waits) stops with DONE set; an idle one is not touched; every pin is released
                abort_writes++;
                if (started && !done) { done = true; }
                waitgo = false;
                release_all();
            }
            if (v & PD_CTRL_CLEAR) { fifo.clear(); overflow = false; }
            if (v & PD_CTRL_CAPRST) { ring.clear(); cap_count = cap_lines = cap_lost = 0; cap_overrun = false; }
            if (v & PD_CTRL_RUN) {
                run_count++;
                if (lag_on) { stale_status = status(); lagging = true; }
                refused = 0; ref_pc = 0; guard_trip = false;
                pc = 0; done = false; waitgo = false; started = true;
                run();
            }
            if (v & PD_CTRL_GO) { if (waitgo) { waitgo = false; pc++; run(); } }
        }
    }
};


#endif
