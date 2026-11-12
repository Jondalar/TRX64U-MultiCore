// A minimal 6502 for running the XEX boot loader in the native tests: only the opcodes xexboot.asm uses (anything else stops the
// run and says so). It is test code - the loader's correctness is argued by running it, not by reading it.
#ifndef TESTS_COREAPP_MOS6502_H
#define TESTS_COREAPP_MOS6502_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Mos6502 {
    uint8_t m[65536];
    uint16_t pc = 0;
    uint8_t a = 0, x = 0, y = 0, sp = 0xFF;
    bool n = false, z = false, c = false;
    long steps = 0;
    int bad_opcode = -1;
    uint16_t bad_pc = 0;
    int jsr_hits_stop = 0;

    Mos6502() { memset(m, 0, sizeof(m)); }
    uint16_t w(uint16_t addr) const { return (uint16_t)(m[addr] | (m[(uint16_t)(addr + 1)] << 8)); }
    uint16_t abs_() { uint16_t v = w(pc); pc += 2; return v; }
    void nz(uint8_t v) { n = v & 0x80; z = v == 0; }
    void push(uint8_t v) { m[0x100 + sp--] = v; }
    uint8_t pull() { return m[0x100 + ++sp]; }
    void branch(bool take) { int8_t d = (int8_t)m[pc++]; if (take) pc = (uint16_t)(pc + d); }

    // SIOV hook: returns true when it handled the call (the caller returns to the JSR's successor)
    bool (*siov)(Mos6502 *) = 0;
    uint16_t stop_pc = 0xFFFF;             // run() returns when pc reaches this

    bool run(long max_steps)
    {
        while (steps++ < max_steps) {
            if (pc == stop_pc) return true;
            uint16_t at = pc;
            uint8_t op = m[pc++];
            uint16_t ad;
            switch (op) {
            case 0xA9: a = m[pc++]; nz(a); break;
            case 0xAD: a = m[abs_()]; nz(a); break;
            case 0xBD: a = m[(uint16_t)(abs_() + x)]; nz(a); break;
            case 0xA2: x = m[pc++]; nz(x); break;
            case 0xAE: x = m[abs_()]; nz(x); break;
            case 0x8D: m[abs_()] = a; break;
            case 0x8E: m[abs_()] = x; break;
            case 0xEE: ad = abs_(); m[ad]++; nz(m[ad]); break;
            case 0xCE: ad = abs_(); m[ad]--; nz(m[ad]); break;
            case 0xE8: x++; nz(x); break;
            case 0x98: a = y; nz(a); break;
            case 0x0D: a |= m[abs_()]; nz(a); break;
            case 0x09: a |= m[pc++]; nz(a); break;
            case 0x2D: a &= m[abs_()]; nz(a); break;
            case 0xCD: { uint8_t v = m[abs_()]; c = a >= v; nz((uint8_t)(a - v)); break; }
            case 0xC9: { uint8_t v = m[pc++]; c = a >= v; nz((uint8_t)(a - v)); break; }
            case 0xE0: { uint8_t v = m[pc++]; c = x >= v; nz((uint8_t)(x - v)); break; }
            case 0xD0: branch(!z); break;
            case 0xF0: branch(z); break;
            case 0xB0: branch(c); break;
            case 0x30: branch(n); break;
            case 0x18: c = false; break;
            case 0x38: c = true; break;
            case 0x48: push(a); break;
            case 0x68: a = pull(); nz(a); break;
            case 0x4C: pc = abs_(); break;
            case 0x6C: ad = abs_(); pc = w(ad); break;
            case 0x20:
                ad = abs_();
                if (ad == 0xE459 && siov) {
                    if (!siov(this)) return false;
                } else {
                    push((uint8_t)((pc - 1) >> 8)); push((uint8_t)(pc - 1)); pc = ad;
                }
                break;
            case 0x60: { uint8_t lo = pull(), hi = pull(); pc = (uint16_t)(((hi << 8) | lo) + 1); break; }
            default:
                bad_opcode = op; bad_pc = at; return false;
            }
        }
        return false;
    }
};

#endif
