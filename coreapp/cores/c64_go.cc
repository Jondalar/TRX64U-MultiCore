#include "c64_go.h"
#include "c64_regs.h"
#include <stdio.h>
#include <string.h>

// ---- 32 bit registers: little endian, byte 3 commits a write, byte 0 latches a read (trx_sys.v) ----------------------------------
static void wr32(CoreIo *io, uint32_t reg, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        io->wr(reg + i, (uint8_t)(v >> (8 * i)));
    }
}

static uint32_t rd32(CoreIo *io, uint32_t reg)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)io->rd(reg + i) << (8 * i);
    }
    return v;
}

// words of a chip from the state bus; each read moves SB_ADDR on
static void sb_read(CoreIo *io, int chip, int word, uint32_t *dst, int n)
{
    wr32(io, C64_SB_ADDR, ((uint32_t)chip << 7) | (uint32_t)word);
    for (int i = 0; i < n; i++) {
        dst[i] = rd32(io, C64_SB_DATA);
    }
}

bool c64_go_available(CoreIo *io)
{
    return (io->rd(C64_CAPS) & C64_CAPS_STATEBUS) && (io->rd(C64_CAPS) & C64_CAPS_DEBUG) && (io->rd(C64_CAPS2) & C64_CAPS2_CPUWRITE);
}

// The CPU's flip-flops as chip 1 w8-w14 carry them (cpu6510.v sb_cur; hw_trx.c in the monitor): 7:0 A, 15:8 X, 23:16 Y, 31:24 S, 39:32 P,
// 55:40 PC, 87:80 IR, 102+4:0 state, +12 copy_d2p, +15 flags_update, +16 trigger_upd, 137:130 din_r.
#define B_A      0
#define B_X      8
#define B_Y      16
#define B_S      24
#define B_P      32
#define B_PC     40
#define B_IR     80
#define B_CTRL   102
#define B_DIN    130

static void put_bits(uint32_t *w, int lo, int n, uint32_t v)
{
    for (int i = 0; i < n; i++) {
        int b = lo + i;
        w[b >> 5] = (w[b >> 5] & ~(1u << (b & 31))) | (((v >> i) & 1u) << (b & 31));
    }
}

// What the CPU sees at addr, by the PLA ($01 as read): RAM, a ROM through the aperture, or nothing it can read without a bus cycle (I/O).
static int peek_cpu(CoreIo *io, uint8_t port, uint16_t a)
{
    bool loram = port & 1, hiram = port & 2, charen = port & 4;
    uint8_t b = 0;
    if (a >= 0xA000 && a < 0xC000 && loram && hiram) {
        io->rom_read(C64_ROM_BASIC + (a - 0xA000), &b, 1);
    } else if (a >= 0xD000 && a < 0xE000 && (loram || hiram)) {
        if (charen) {
            return -1;                                    // the chips: a read is a bus cycle with side effects
        }
        io->rom_read(C64_ROM_CHAR + (a - 0xD000), &b, 1);
    } else if (a >= 0xE000 && hiram) {
        io->rom_read(C64_ROM_KERNAL + (a - 0xE000), &b, 1);
    } else {
        io->ram_read(a, &b, 1);
    }
    return b;
}

static void give_back(CoreIo *io)
{
    wr32(io, C64_DBG_CMD, C64_CMD_RUN);
    wr32(io, C64_DBG_OWNER, 0);
}

#define STOP_WAIT_MS 500
#define LOAD_WAIT_MS 1000

int c64_go(CoreIo *io, uint16_t pc, c64_go_place_t place, void *user, char *msg, int msglen)
{
    if (!c64_go_available(io)) {
        snprintf(msg, msglen, "this bitstream cannot set the CPU's address (no CPU write path)");
        return 1;
    }
    wr32(io, C64_DBG_OWNER, 1);                                // the debugger owns run control; C64_STOP is not ours now
    wr32(io, C64_DBG_CMD, C64_CMD_PAUSE_IB);
    uint32_t t0 = io->now_ms();
    uint32_t st;
    while (((st = rd32(io, C64_DBG_STATUS)) & (C64_ST_SHUT | C64_ST_AT_IB)) != (C64_ST_SHUT | C64_ST_AT_IB)) {
        if (io->now_ms() - t0 > STOP_WAIT_MS) {
            give_back(io);
            snprintf(msg, msglen, "the CPU did not stop on an instruction boundary (status %08X)", (unsigned)st);
            return 2;
        }
        io->delay_ms(1);
    }

    if (place) {
        int rc = place(user, msg, msglen);
        if (rc) {
            give_back(io);
            return rc;
        }
    }

    // the registers as the pending update leaves them (w4/w5) and the port ($01 as read, w2): both only meaningful at an IB with the gate shut
    uint32_t w0[6];
    sb_read(io, 1, 0, w0, 6);
    uint8_t port = (uint8_t)(w0[2] >> 16);
    int op = peek_cpu(io, port, pc);
    if (op < 0) {
        give_back(io);
        snprintf(msg, msglen, "$%04X is in the I/O area: the opcode there cannot be read without a bus cycle", pc);
        return 3;
    }
    uint8_t a = (uint8_t)w0[4], x = (uint8_t)(w0[4] >> 8), y = (uint8_t)(w0[4] >> 16), s = (uint8_t)(w0[4] >> 24);
    uint8_t p = (uint8_t)w0[5];
    p &= (uint8_t)~0x0C;                                       // a program is started with interrupts allowed and binary arithmetic

    uint32_t w[7];
    sb_read(io, 1, 8, w, 7);
    put_bits(w, B_A, 8, a);
    put_bits(w, B_X, 8, x);
    put_bits(w, B_Y, 8, y);
    put_bits(w, B_S, 8, s);
    put_bits(w, B_P, 8, p);
    put_bits(w, B_IR, 8, 0xEA);                                // the pending write-back of the previous instruction is taken out:
    put_bits(w, B_CTRL + 12, 1, 0);                            // IR = NOP, copy_d2p, flags_update, trigger_upd off
    put_bits(w, B_CTRL + 15, 1, 0);
    put_bits(w, B_CTRL + 16, 1, 0);
    put_bits(w, B_PC, 16, pc);                                 // the fetch's address; the update adds 1
    put_bits(w, B_DIN, 8, (uint32_t)op);                       // what the fetch read
    wr32(io, C64_SB_STATUS, 0);                                // clear "refused"
    wr32(io, C64_SB_ADDR, (1u << 7) | 8u);
    for (int k = 0; k < 7; k++) {
        wr32(io, C64_SB_DATA, w[k]);
    }
    if (rd32(io, C64_SB_STATUS) & 2) {
        give_back(io);
        snprintf(msg, msglen, "the state bus refused the staged CPU words (the gate is not shut)");
        return 4;
    }
    wr32(io, C64_SB_LOAD, 0x02);                               // the CPU, at the next opening edge
    wr32(io, C64_DBG_IPC_SET, (1u << 24) | ((uint32_t)op << 16) | pc);     // what the debug block reports as the PC

    wr32(io, C64_DBG_CMD, C64_CMD_RUN);
    t0 = io->now_ms();
    while ((rd32(io, C64_SB_LOAD) & 0x02) || (rd32(io, C64_DBG_STATUS) & C64_ST_SHUT)) {
        if (io->now_ms() - t0 > LOAD_WAIT_MS) {
            wr32(io, C64_DBG_OWNER, 0);
            snprintf(msg, msglen, "the machine did not take the new address in time (the load is still armed)");
            return 5;
        }
        io->delay_ms(1);
    }
    wr32(io, C64_DBG_OWNER, 0);                                  // C64_STOP is the menu's again
    return 0;
}
