#include "iectrace.h"
#include "bugreport.h"
#include <stdio.h>

// the page (cores/c128/c128_iectrace.v is the text of record)
#define P_ID        0x000
#define P_VERSION   0x004
#define P_DEPTH     0x005
#define P_TICK_HZ   0x008
#define P_CTRL      0x010
#define P_TOTAL     0x014
#define P_LIVE      0x018
#define P_CNT_ATN   0x020
#define P_CNT_CLK   0x024
#define P_CNT_DATA  0x028
#define P_CNT_SRQ   0x02C
#define P_IDX       0x030
#define P_DATA      0x034
#define SNAP_POLLS  2000                // reads of CTRL while the core takes the snapshot (a few core clocks per read)

static uint8_t rb(CoreIo *io, uint32_t off)
{
    return io->rd(IECTRACE_PAGE + off);
}

static uint32_t r32(CoreIo *io, uint32_t off)
{
    return (uint32_t)rb(io, off) | ((uint32_t)rb(io, off + 1) << 8) | ((uint32_t)rb(io, off + 2) << 16) | ((uint32_t)rb(io, off + 3) << 24);
}

bool iectrace_present(CoreIo *io)
{
    return rb(io, P_ID) == 'I' && rb(io, P_ID + 1) == 'E' && rb(io, P_ID + 2) == 'C' && rb(io, P_ID + 3) == 'T' && rb(io, P_VERSION) == 1;
}

// four line levels {SRQ, DATA, CLK, ATN} (bit 3 .. 0), 1 = high: the letter when high, a dot when the line is pulled low
static void lines(char *out, unsigned v)
{
    out[0] = (v & 1u) ? 'A' : '.';
    out[1] = (v & 2u) ? 'C' : '.';
    out[2] = (v & 4u) ? 'D' : '.';
    out[3] = (v & 8u) ? 'S' : '.';
    out[4] = 0;
}

void iectrace_report(CoreIo *io, BrOut *o, int max_events)
{
    uint32_t hz = r32(io, P_TICK_HZ);
    unsigned lg = rb(io, P_DEPTH);
    if (lg < 1 || lg > 10) {                                    // the hardware keeps a 10-bit slot index (RD_IDX b9:0): a larger depth cannot be read
        br_put(o, "The trace page is invalid (ring depth 2^%u, expected 2^1 .. 2^10).\n", lg);
        return;
    }
    unsigned depth = 1u << lg;
    io->wr(IECTRACE_PAGE + P_CTRL, 0x02);                       // a snapshot: the counters stand still while they are read
    int polls = 0;
    while ((rb(io, P_CTRL) & 1) && polls < SNAP_POLLS) {
        polls++;
    }
    if (rb(io, P_CTRL) & 1) {
        br_put(o, "The snapshot did not finish; the values below may be torn.\n");
    }
    uint32_t total = r32(io, P_TOTAL);
    unsigned live = (unsigned)rb(io, P_LIVE) | ((unsigned)(rb(io, P_LIVE + 1) & 3) << 8);
    char bus[5], port[5];
    lines(bus, live & 15u);
    lines(port, (live >> 4) & 15u);
    br_put(o, "Layout %u, ring of %u events, clock %u Hz\n", (unsigned)rb(io, P_VERSION), depth, (unsigned)hz);
    br_put(o, "Events since the start: %u\n", (unsigned)total);
    br_put(o, "Edges the core heard: ATN %u, CLK %u, DATA %u, SRQ %u\n", (unsigned)r32(io, P_CNT_ATN), (unsigned)r32(io, P_CNT_CLK),
           (unsigned)r32(io, P_CNT_DATA), (unsigned)r32(io, P_CNT_SRQ));
    // mode = the core's c128_mode bit: 1 = C128 mode, 0 = C64 mode (the names stay out of the strings: generic code, T192 D24)
    br_put(o, "Now: bus %s, board port %s, fast serial %s, mode %u\n", bus, port, (live & 0x100u) ? "out" : "in", (live >> 9) & 1u);
    br_put(o, "Lines: letter = high, dot = pulled low (A ATN, C CLK, D DATA, S SRQ). +time = since the event before; \">\" = longer than shown. fs = fast serial direction; mode 1 = machine mode, 0 = compatibility mode.\n");
    br_put(o, "While SRQ moves (a fast serial burst) only its first edge and the state after it are listed; the counts above hold every edge.\n");
    uint32_t have = total < depth ? total : depth;
    uint32_t show = have < (uint32_t)max_events ? have : (uint32_t)max_events;
    if (!show) {
        br_put(o, "No event recorded.\n");
        return;
    }
    br_put(o, "Newest %u of %u events, oldest first:\n", (unsigned)show, (unsigned)have);
    for (uint32_t k = show; k > 0; k--) {
        uint32_t n = total - k;                                  // the event's number; its slot is the number modulo the ring
        uint32_t slot = n & (depth - 1);
        io->wr(IECTRACE_PAGE + P_IDX, (uint8_t)(slot & 0xFF));
        io->wr(IECTRACE_PAGE + P_IDX + 1, (uint8_t)(slot >> 8));
        uint32_t e = r32(io, P_DATA);
        uint32_t dt = e >> 16;
        char b[5], p[5];
        lines(b, e & 15u);
        lines(p, (e >> 4) & 15u);
        char tm[20];
        uint32_t khz = hz / 1000u;                                      // 32 bit only: tenths of a microsecond = ticks * 10000 / kHz
        uint32_t t10 = khz ? (dt * 10000u) / khz : 0;
        snprintf(tm, sizeof(tm), "%s%u.%u", dt == 0xFFFF ? ">" : "", (unsigned)(t10 / 10), (unsigned)(t10 % 10));
        br_put(o, "#%u +%sus bus %s port %s fs %s mode %u\n", (unsigned)n, tm, b, p, (e & 0x100u) ? "out" : "in", (unsigned)((e >> 9) & 1u));
    }
}
