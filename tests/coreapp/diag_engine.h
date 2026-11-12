// A model of the diag core's probe engine behind CoreIo (T175): the register map of section 3 and the step-word fields of cores/portdiag_regs.h; the pins
// of the ports and the comparators are set by the test. Used by test_diagtest.cc and the render bench.
#ifndef TESTS_COREAPP_DIAG_ENGINE_H
#define TESTS_COREAPP_DIAG_ENGINE_H

#include <deque>
#include <functional>
#include "fakes.h"
#include "portdiag_regs.h"

// ---- the engine --------------------------------------------------------------------------------------------------------------------------------
struct Engine : FakeIo {
    uint32_t steps[256];
    int wptr = 0, pc = 0;
    bool waitgo = false, done = false, overflow = false;
    std::deque<uint32_t> fifo;
    uint32_t tpus256 = 25600;                          // 100 ticks per microsecond
    uint64_t tick = 0;
    uint32_t driven = 0, drivable = 0x3FF, forced_low = 0;
    uint32_t cmp_now = 0;
    uint32_t charge[4] = { 0, 0, 0, 0 };               // ticks from the start of a time_to_flip until the bit sets; 0: never
    int abort_writes = 0;
    bool lag_on = false, lagging = false;              // the real engine takes a RUN a few clocks after the write: until the caller waits, STATUS and the FIFO level are the old ones
    uint8_t stale_status = 0;
    bool started = false;                              // RUN seen and the engine has not ended: the RTL's `running`
    int run_count = 0;
    uint32_t padctl = 0;                               // the last PADCTL argument (b0 DIS level, b1 DIS enable, b2 ADJ level, b3 ADJ enable)
    int rises = 0;                                     // rising edges on pin 6 of port 2 since the last PADCTL: what a pad's shift register would have seen
    std::function<uint32_t(const Engine &)> cmp_fn;    // when set, a SAMPLE reads the comparators from this instead of cmp_now (a model of a pad)
    std::function<void(int)> on_prompt;
    std::vector<uint32_t> prompts_seen;

    Engine() { memset(steps, 0, sizeof(steps)); }
    uint32_t pins() const { return 0x3FF & ~((driven & drivable) | forced_low); }
    void push(uint32_t w) { if (fifo.size() >= 512) { overflow = true; } else { fifo.push_back(w); } }
    void exec(uint32_t w)
    {
        uint32_t op = w >> 28, sel = (w >> 24) & 15, arg = w & 0xFFFFFF;
        switch (op) {
        case PD_OP_DRIVE: {
            uint32_t m = arg & 0x3FF, lv = (arg >> 10) & 0x3FF, was = driven;
            driven = (driven & ~(m & lv)) | (m & ~lv);
            if ((was & 0x200) && !(driven & 0x200)) rises++;
            break;
        }
        case PD_OP_PADCTL: padctl = arg & 15; rises = 0; break;
        case PD_OP_MASK: break;
        case PD_OP_WAIT: tick += arg; break;
        case PD_OP_SAMPLE: push(((uint32_t)(tick & 0xFFFF) << 16) | (pins() << 6) | ((cmp_fn ? cmp_fn(*this) : cmp_now) << 2)); break;
        case PD_OP_EDGES: push(0); tick += arg; break;
        case PD_OP_TIME_TO_FLIP: {
            uint32_t best = 0;
            for (int b = 0; b < 4; b++) if ((sel >> b & 1) && charge[b] && (!best || charge[b] < best)) best = charge[b];
            bool hit = best && best <= arg;
            uint32_t v = hit ? best : arg;
            tick += v;
            push((hit ? 0u : 0x80000000u) | v);
            break;
        }
        default: break;
        }
    }
    uint8_t status() const { return (uint8_t)((started && !done ? 1 : 0) | (waitgo ? 2 : 0) | (done ? 4 : 0) | (overflow ? 8 : 0)); }
    void settle() { lagging = false; }                 // time passed: the engine has taken the RUN
    void run()
    {
        while (pc < wptr) {
            uint32_t w = steps[pc];
            uint32_t op = w >> 28;
            if (op == PD_OP_END) { done = true; return; }
            if (op == PD_OP_WAIT_USER) {
                waitgo = true;
                prompts_seen.push_back(w & 0xFFFFFF);
                return;
            }
            if (op == PD_OP_LOOP) {
                int k = w & 0xFF, c = (w >> 8) & 0xFFFF;
                for (int r = 0; r < c; r++) for (int j = 1; j <= k; j++) exec(steps[pc + j]);
                pc += k + 1;
                continue;
            }
            exec(w);
            pc++;
        }
        done = true;
    }
    uint8_t rd(uint32_t reg) override
    {
        reg &= 0xFFF;
        if (reg < 4) return "DIAG"[reg];
        if (reg == PD_CAPS) return 3;
        if (reg >= PD_TICKS && reg < PD_TICKS + 4) return (uint8_t)(tpus256 >> (8 * (reg - PD_TICKS)));
        if (reg == PD_STATUS) return lagging ? stale_status : status();
        if (reg == PD_PC) return (uint8_t)pc;
        if (reg == PD_PC + 1) return (uint8_t)(pc >> 8);
        if (reg >= PD_FIFO && reg < PD_FIFO + 4) {
            uint32_t w = fifo.empty() ? 0 : fifo.front();
            uint8_t b = (uint8_t)(w >> (8 * (reg - PD_FIFO)));
            if (reg == PD_FIFO + 3 && !fifo.empty()) fifo.pop_front();
            return b;
        }
        if (reg == PD_FIFO_LEVEL) return lagging ? 0 : (uint8_t)fifo.size();
        if (reg == PD_FIFO_LEVEL + 1) return lagging ? 0 : (uint8_t)(fifo.size() >> 8);
        if (reg == PD_LIVE) return (uint8_t)(pins() | (cmp_now << 10));
        if (reg == PD_LIVE + 1) return (uint8_t)((pins() | (cmp_now << 10)) >> 8);
        return 0;
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
        } else if (reg == PD_CTRL) {
            if (v & PD_CTRL_ABORT) {                       // the RTL: an engine that runs (or waits) stops with DONE set; an idle one is not touched; every pin is released
                abort_writes++;
                if (started && !done) { done = true; }
                waitgo = false;
                driven = 0;
            }
            if (v & PD_CTRL_CLEAR) { fifo.clear(); overflow = false; }
            if (v & PD_CTRL_RUN) {
                run_count++;
                if (lag_on) { stale_status = status(); lagging = true; }
                pc = 0; done = false; waitgo = false; started = true;
                run();
            }
            if (v & PD_CTRL_GO) { if (waitgo) { waitgo = false; pc++; run(); } }
        }
    }
};


#endif
