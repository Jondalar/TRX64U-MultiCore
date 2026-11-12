// The frame ring, app side. See trx_ring.h.
//
// Hardware (TRX64-Ultimate designs/c64_basic: trx_engine.v page 2, trx_sys.v
// pages 0/1, c64_core.v gate and state bus; 32-bit registers, trx_io.h rule):
//   0x101A0100 GATE_CTRL   b0 the app's C64_STOP pauses the machine (else the
//                          CPU only); R b8 halted
//   0x101A0104 CAP_CTRL    b7:0 a capture every n frames
//   0x101A0114 STOP_POS    b5:0 cycle, b8 arm: stop on line 0 one edge before
//                          the phi2 fall that ends that cycle; W b9 release
//                          the hold; R b16 holding there
//   0x101A1000 SB_ADDR, 1004 SB_DATA (W: staged word, only with the gate
//                          shut), 1008 SB_STATUS (b1 a write was refused),
//                          100C SB_LOAD (b1 CPU, b3 CIA 1, b4 CIA 2)
//   0x101A2000 LOG_CTRL    b0 on, b1 writes from outside, b2 cartridge RAM,
//                          b3 colour RAM, b8 restart (counts to 0)
//   0x101A2004 LOG_STAT    b1 a block is being copied, b2 mover busy, b3
//                          records wait in the FIFO
//   0x101A200C DROPPED, 2028+16s LOG_CNT[s] (R, records since the restart, mod 2^28).
//   The areas are fixed in the engine (area_base/area_size below, SLIM D4): LOG_BASE/LOG_MASK are gone.
//   0x101A2080 ENG_CMD     W 1 COPY ENG_LEN bytes ENG_SRC -> ENG_DST; R b0 busy
//   0x101A2094 AP_START, 2098 AP_COUNT, 209C AP_CMD: APPLY (CAPS2 b5), see apply_run()
//   0x101A2084 ENG_SRC, 2088 ENG_DST, 208C ENG_LEN (16-byte units)
//   0x101A4028 CMD (page 4, a byte) b1: the cartridge drops its cached lines
//   0x101A5000 SH_CTRL     b1:0 freeze sessions: 0 off, 1 auto, 2 every pause (cores before the slim
//                          one; on it the register reads 0 and ignores writes)
//
// Record formats (trx_engine.v header): stream 0 {new, old, addr16} 4 bytes;
// stream 1 8 bytes [27:0] address, [31:28] space (1 colour RAM, 2 cartridge
// RAM), [39:32] old, [47:40] new; stream 2 F1-F4 per frame boundary, then
// B1, the 58 words two per record, B2 when a chip block follows (F1 b1).
//
// The app's CPU cannot address the upper DDR2: every read of the areas is an
// engine COPY into a bounce buffer below 64 MB (the rvlite core has no data
// cache, so the copy is visible to it as soon as the mover is done).
//
// Not undone (no hardware for it): the SID (not on the state bus), the VIC's
// internals and its write-only registers ($D012 compare, $D011 bit 7: the
// current ones stay; its IRQ and collision latches only by clearing during
// the run, see clear_vic_latches), the REU, the banking registers of the
// cartridge types replay_cart() leaves out, drive A, and cartridge flash or
// EEPROM writes (no tap: the engine's LOG_PUT is gone, SLIM D6).

#include "trx_ring.h"
#include "trx_ea.h"
#include "trx_ring_reu.h"
#include "trx_io.h"
#include "c64.h"
#include "u64.h"
#include "itu.h"
#include "init_function.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPS_ENGINE    0x04
#define CAPS_DBG       0x08

#define GATE_CTRL      (TRX_IO_BASE + 0x0100)
#define GATE_HALTED    0x100
#define CAP_CTRL       (TRX_IO_BASE + 0x0104)
#define STOP_POS       (TRX_IO_BASE + 0x0114)
#define POS_ARM        0x100
#define POS_RELEASE    0x200
#define POS_HOLDING    0x10000
#define SB_ADDR        (TRX_IO_BASE + 0x1000)
#define SB_DATA        (TRX_IO_BASE + 0x1004)
#define SB_STATUS      (TRX_IO_BASE + 0x1008)
#define SB_LOAD        (TRX_IO_BASE + 0x100C)
#define SB_REFUSED     0x02
#define SB_LOAD_ALL    0x1A
#define SH_CTRL        (TRX_IO_BASE + 0x5000)
#define CART_CMD       (TRX_IO_BASE + 0x4028)
#define CART_INVAL     0x02

#define P2             (TRX_IO_BASE + 0x2000)
#define LOG_CTRL       (P2 + 0x000)
#define LOG_STAT       (P2 + 0x004)
#define DROPPED        (P2 + 0x00C)
#define LOG_CNT(s)     (P2 + 0x028 + 16 * (s))
#define ENG_CMD        (P2 + 0x080)
#define ENG_SRC        (P2 + 0x084)
#define ENG_DST        (P2 + 0x088)
#define ENG_LEN        (P2 + 0x08C)
#define AP_START       (P2 + 0x094)     // APPLY (T20 §17.3): stream 0 records through the second port
#define AP_COUNT       (P2 + 0x098)
#define AP_CMD         (P2 + 0x09C)     // W b0 1 undo (downwards, old bytes) / 0 redo; R b0 busy

#define LOG_ON_ALL     0x00F   // on, outside writes, cartridge RAM, colour RAM
#define LOG_RESTART    0x100
#define LOG_TRACE      0x010   // b4: the trace former (T21 §4.11.1)
#define LOG_FLUSH      0x200   // b9 (W): drop the pending instruction, the machine's flow is cut
#define TRACE_CNT      (P2 + 0x058)   // records written (R), wraps at TRACE_WRAP
#define STAT_COPYING   0x02
#define STAT_FIFO      0x08

#define M28            0x0FFFFFFFu
#define OPEN           0xFFFFFFFFu

// The areas (the engine's fixed layout, T20 §5.1 / ARCH §15.7, SLIM D4)
static const uint32_t area_base[3] = { 0x7000000, 0x6000000, 0x5C00000 };
static const uint32_t area_size[3] = { 0x1000000, 0x0800000, 0x0400000 };
static const uint32_t rec_size[3]  = { 4, 8, 8 };

#define NREC_BLOCK     ((TRX_BLOCK_WORDS + 1) / 2)
#define TL_CAP         4096    // timeline entries (81 s at one per frame)
#define BOUNCE         0x10000
#define SAFETY         0x1000  // records kept clear of the area's wrap

// the block's words in table order (the capture table's reset value):
// glue 1, CPU 3, VIC 12, CIA 1 5, CIA 2 5, cartridge 1, CPU hidden 7,
// CIA 1 hidden 12, CIA 2 hidden 12
#define W_CPU          1
#define W_VIC          4
#define W_CIA1         16
#define W_CIA2         21
#define W_CPUH         27
#define W_CIA1H        34
#define W_CIA2H        46

static SemaphoreHandle_t mutex;
static bool present;
static bool log_on;
static uint8_t *bounce;             // 16-byte aligned
static TrxFrame *tl;                // circular, TL_CAP
static int tl_first, tl_n;
static uint32_t next_seq = 1;
static uint32_t p2;                 // next stream 2 record to parse
static uint64_t cyc_acc;
static uint32_t cyc_last;
static bool cyc_have;
static uint32_t resyncs;
static uint32_t spurious_f4;       // F4s the drain wrote after block records (see parse)
static bool f4_after_blocks;        // an older engine: it writes them (CAPS2 b0 = 0)
static bool apply_hw;               // the engine applies stream 0 records itself (CAPS2 b5)
static uint32_t cnt_now[2];         // the last LOG_CNT 0/1 read
static volatile int pending = -1;   // the freeze menu's choice (timeline index)
static volatile bool scrubbing;     // a Timewarp scrub holds the machine (§16.7 below)
static TrxReuGate reu_gate;         // A08: the REU is on, the ring is off
static bool trace_on = true;       // the trace former runs with the log (trace_ctl)
static uint32_t log_rd;             // the observer log reader (trx_ring_log_read): the next stream 1 record to look at
static bool log_have;               // log_rd is valid

// LOG_CTRL as the log runs: every tap, the trace, and with flush the machine's flow cut (a
// restore, land, scrub or load: the pending instruction belongs to the time that was undone)
static inline uint32_t logctl(bool flush)
{
    return LOG_ON_ALL | (trace_on ? LOG_TRACE : 0) | (flush ? LOG_FLUSH : 0);
}

// While a scrub is open every step logs one frame run into the areas: a target
// keeps this much more headroom, so the entry the machine stands on stays
// readable for the next step (a frame writes at most ~10 K records).
#define SCRUB_MARGIN   0x8000

static inline uint32_t r32(uint32_t a) { return trx_io_read32(a); }
static inline void w32(uint32_t a, uint32_t v) { trx_io_write32(a, v); }

// A08: the REU's enable, as the machine has it now. The ring does not record the REU's contents, so
// while it is on there is no ring (trx_ring_reu_sync) and no way into it (E_REU).
static inline bool reu_on(void) { return C64_REU_ENABLE != 0; }

static inline TrxFrame &ent(int i) { return tl[(tl_first + i) % TL_CAP]; }

static void *alloc_aligned(size_t size)
{
    uint8_t *p = (uint8_t *)malloc(size + 15);
    if (!p) {
        return NULL;
    }
    return (void *)(((uint32_t)p + 15) & ~15u);
}

static bool lock(void)
{
    if (!present) {
        return false;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    return true;
}

static void unlock(void)
{
    xSemaphoreGive(mutex);
}

// ---- the mover -------------------------------------------------------------------
static void eng_copy(uint32_t src, void *dst, uint32_t len)
{
    w32(ENG_SRC, src);
    w32(ENG_DST, (uint32_t)dst);
    w32(ENG_LEN, len);
    w32(ENG_CMD, 1);
    uint16_t t0 = getMsTimer();
    while (r32(ENG_CMD) & 1) {
        if (uint16_t(getMsTimer() - t0) > 200) {
            // the mover never answered: the data is not there, but the CPU must not wait forever
            printf("[RING] COPY did not finish: %u bytes from 0x%07x\n", (unsigned)len, (unsigned)src);
            break;
        }
    }
    // the copy went through the memory controller behind the CPU's back; a
    // fence keeps the loads after the poll
    __asm__ volatile ("fence" ::: "memory");
}

// Records [from, from + n) of stream s, contiguous in the bounce buffer
// (n * size <= BOUNCE - 32): one COPY, two where the range wraps the area
// (the area's end is 16-byte aligned, so the second part lands aligned
// right behind the first). Returns where record "from" is.
static const uint8_t *fetch(int s, uint32_t from, uint32_t n)
{
    uint32_t size = area_size[s];
    uint32_t off = (from * rec_size[s]) & (size - 1);
    uint32_t a0 = off & ~15u;
    uint32_t pre = off - a0;
    uint32_t len = (pre + n * rec_size[s] + 15) & ~15u;
    if (a0 + len <= size) {
        eng_copy(area_base[s] + a0, bounce, len);
    } else {
        uint32_t l1 = size - a0;
        eng_copy(area_base[s] + a0, bounce, l1);
        eng_copy(area_base[s], bounce + l1, len - l1);
    }
    return bounce + pre;
}

static uint64_t rec64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static uint32_t rec32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// ---- the timeline ----------------------------------------------------------------
static bool reachable(const TrxFrame &f, uint32_t margin = 0)
{
    uint32_t r0 = area_size[0] / rec_size[0] - SAFETY - margin;
    uint32_t r1 = area_size[1] / rec_size[1] - SAFETY - margin;
    return ((cnt_now[0] - f.s0) & M28) <= r0 && ((cnt_now[1] - f.s1) & M28) <= r1;
}

static void trim(void)
{
    while (tl_n && (tl_n >= TL_CAP || !reachable(ent(0)))) {
        tl_first = (tl_first + 1) % TL_CAP;
        tl_n--;
    }
}

static void tl_clear(void)
{
    tl_first = 0;
    tl_n = 0;
}

// ---- trace epochs (T21 §4.11.2) ---------------------------------------------------
// The trace's records carry the engine's phi2 count (24 bits). A restore, land,
// scrub or load cuts the machine's flow and rebases the timeline's cycle count, so
// each cut starts an epoch: the first physical record, the timeline count of the
// entry the machine starts from (tc_start), and an anchor {hw_ref, tc_ref} that
// turns a record's count into a timeline count: tc = tc_ref + sext24(c24 - hw_ref).
// The anchor of the open epoch follows the frame markers (parse_locked), so a
// record is never further than a few frames from it.
#define TR_AREA     0x6800000u
#define TRACE_CAP      349525u                  // records in the area (3 words each)
#define TRACE_WRAP     (TRACE_CAP * 768u)       // LOG_CNT[3] wraps here
#define TRACE_SAFETY   64                       // records kept clear of the wrap
#define TRACE_CHUNK    512                      // records per read
#define MAX_EP         48

struct Epoch {
    uint32_t phys0;         // first physical record (count when the epoch began)
    uint64_t tc_start;      // timeline count of the cut: records after it belong to this epoch
    uint32_t hw_ref;
    int64_t  tc_ref;
    bool     anch;
    bool     keep_known;    // keep_end is set (the epoch is closed)
    bool     gap;           // the cut left a hole in the history
    uint32_t keep_end;      // closed: the first physical record that is no history any more
};
static Epoch ep[MAX_EP];
static int ep_n;
static uint8_t *tbuf;       // TRACE_CHUNK records, and stream 0 records for them

static inline uint32_t trace_cnt(void)
{
    return r32(TRACE_CNT) & M28;
}

static void ep_reset(void)
{
    memset(ep, 0, sizeof(ep));
    ep_n = 1;
    ep[0].tc_start = 0;
}

// the anchor of the open epoch: the newest marker
static void ep_anchor(void)
{
    if (ep_n && cyc_have) {
        ep[ep_n - 1].hw_ref = cyc_last;
        ep[ep_n - 1].tc_ref = (int64_t)cyc_acc;
        ep[ep_n - 1].anch = true;
    }
}

// The machine starts over from the timeline entry at tc_start, the engine's count being
// hw there. Called with the machine stopped, before the log is put on again with a flush.
static void ep_push(uint64_t tc_start, uint32_t hw)
{
    if (!(trx_io_caps() & CAPS_DBG) || !ep_n) {
        return;
    }
    Epoch &c = ep[ep_n - 1];
    if (c.tc_start == tc_start && c.anch && c.hw_ref == (hw & 0xFFFFFF) && ep_n > 1) {
        return;             // the commit of a land: the same cut
    }
    if (ep_n == MAX_EP) {
        memmove(&ep[0], &ep[1], sizeof(Epoch) * (MAX_EP - 1));
        ep_n--;
    }
    Epoch &e = ep[ep_n++];
    memset(&e, 0, sizeof(e));
    e.phys0 = trace_cnt();
    e.tc_start = tc_start;
    e.hw_ref = hw & 0xFFFFFF;
    e.tc_ref = (int64_t)tc_start;
    e.anch = true;
    ep[ep_n - 2].keep_known = false;
}

// stream 2: every whole frame since the last call
static void parse_locked(void)
{
    uint32_t cnt = r32(LOG_CNT(2)) & M28;
    cnt_now[0] = r32(LOG_CNT(0)) & M28;
    cnt_now[1] = r32(LOG_CNT(1)) & M28;
    if (scrubbing) {
        // a scrub or a debugger land holds the machine: what runs from there
        // (a frame run, the debugger's steps) makes markers that are no
        // timeline entries; the final rewind or the commit skips them
        return;
    }
    const uint32_t chunk_recs = (BOUNCE - 32) / 8;
    for (;;) {
        uint32_t avail = (cnt - p2) & M28;
        if (avail > area_size[2] / 8) {
            // the parser fell a whole area behind: what it missed is gone
            printf("[RING] stream 2 lapped (%u records behind), timeline dropped\n", (unsigned)avail);
            tl_clear();
            p2 = cnt;
            break;
        }
        if (avail < 4) {
            break;
        }
        uint32_t n = avail < chunk_recs ? avail : chunk_recs;
        const uint8_t *buf = fetch(2, p2, n);
        uint32_t i = 0;
        for (;;) {
            if (n - i < 4) {
                break;
            }
            uint64_t m0 = rec64(buf + 8 * i), m1 = rec64(buf + 8 * (i + 1));
            uint64_t m2 = rec64(buf + 8 * (i + 2)), m3 = rec64(buf + 8 * (i + 3));
            if (m0 >> 56 != 0xF1 || m1 >> 56 != 0xF2 || m2 >> 56 != 0xF3 || m3 >> 56 != 0xF4) {
                i++;            // not at a marker: step until one comes
                resyncs++;
                continue;
            }
            uint8_t flags = uint8_t(m0 >> 48);
            // the block: B1, the words two per record, B2. An older engine
            // (CAPS2 b0 = 0) takes any stream 2 record whose top byte is F3 for
            // a frame's F3 marker and writes an F4 after it: a block record with
            // F3 in its top byte (an odd word whose high byte is $F3, e.g. the
            // 6510's S) is followed by such an F4, which is skipped here. A fixed
            // engine writes none, and the next record is a block record that
            // may have F4 on top itself: no skip there.
            uint32_t j = i + 4;
            uint32_t wv[TRX_BLOCK_WORDS];
            bool whole = false;
            uint8_t bfl = 0;
            bool incomplete = false;
            if (flags & 2) {
                if (j >= n) {
                    incomplete = true;
                } else if (rec64(buf + 8 * j) >> 56 == 0xB1) {
                    j++;
                    int got = 0;
                    while (got < TRX_BLOCK_WORDS) {
                        if (j >= n) {
                            incomplete = true;
                            break;
                        }
                        uint64_t r = rec64(buf + 8 * j);
                        j++;
                        wv[got++] = uint32_t(r);
                        if (got < TRX_BLOCK_WORDS) {
                            wv[got++] = uint32_t(r >> 32);
                        }
                        if (f4_after_blocks && r >> 56 == 0xF3) {
                            if (j >= n) {
                                incomplete = true;
                                break;
                            }
                            if (rec64(buf + 8 * j) >> 56 == 0xF4) {
                                j++;
                                spurious_f4++;
                            }
                        }
                    }
                    if (!incomplete) {
                        if (j >= n) {
                            incomplete = true;
                        } else {
                            uint64_t bt = rec64(buf + 8 * j);
                            if (bt >> 56 == 0xB2) {
                                j++;
                                bfl = uint8_t(bt >> 48);
                                uint16_t words = uint16_t(bt >> 32);
                                uint16_t g0 = uint16_t(bt), g1 = uint16_t(bt >> 16);
                                whole = (flags & 1) && !(bfl & 1) && words == TRX_BLOCK_WORDS && g0 == g1;
                            }
                        }
                    }
                }
            }
            if (incomplete) {
                break;          // the block is still on its way (or past this chunk)
            }
            uint32_t need = j - i;
            if (tl_n >= TL_CAP) {
                tl_first = (tl_first + 1) % TL_CAP;   // full: the oldest goes
                tl_n--;
            }
            TrxFrame &nf = ent(tl_n);
            nf.seq = next_seq++;
            nf.marker = uint32_t(m0);
            nf.flags = flags;
            uint32_t c24 = uint32_t(m1) & 0xFFFFFF;
            if (cyc_have) {
                cyc_acc += (c24 - cyc_last) & 0xFFFFFF;
            }
            cyc_last = c24;
            cyc_have = true;
            nf.cycles = cyc_acc;
            nf.c24 = c24;
            nf.line = uint16_t(m2 >> 40) & 0x1FF;
            nf.x = uint16_t(m2 >> 24) & 0x3FF;
            nf.s0 = uint32_t(m3) & M28;
            nf.s1 = uint32_t(m3 >> 28) & M28;
            nf.e0 = nf.e1 = OPEN;
            nf.block = whole;
            nf.bflags = bfl;
            if (whole) {
                memcpy(nf.w, wv, sizeof(wv));
            }
            if (tl_n) {
                TrxFrame &pf = ent(tl_n - 1);
                if (pf.e0 == OPEN) {
                    pf.e0 = nf.s0;
                    pf.e1 = nf.s1;
                }
            }
            tl_n++;
            i += need;
        }
        if (!i) {
            break;
        }
        p2 = (p2 + i) & M28;
        if (i < n && n == avail) {
            break;              // the rest is a frame not yet complete
        }
    }
    trim();
    ep_anchor();
}

// The machine starts over from entry e's snap: e's F2 count becomes the one
// of that load edge, so the next marker's cycle step is one frame and the
// count stays the debug block's CYCLES (T21 relies on both). Without the
// debug page (CYCLES reads 0) nothing changes.
#define DBG_CYCLES     (TRX_IO_BASE + 0x0120)
#define DBG_OWNER      (TRX_IO_BASE + 0x0118)
#define DBG_CYC_CMP    (TRX_IO_BASE + 0x0130)
#define DBG_CMD        (TRX_IO_BASE + 0x3000)
#define DBG_STATUS     (TRX_IO_BASE + 0x3008)
#define DBG_ST_NEW     0x08
#define DBG_ST_OWNER   0x100000
#define DBG_RUN        0x03

static void rebase_cycles(TrxFrame &e, uint32_t c24)
{
    if (!(trx_io_caps() & CAPS_DBG)) {
        return;
    }
    e.c24 = c24 & 0xFFFFFF;
    ep_push(e.cycles, e.c24);
    cyc_acc = e.cycles;
    cyc_last = e.c24;
    cyc_have = true;
}

// While the debugger owns run control (T21: OWNER) the app's C64_STOP does not
// reach the gate, and its hold keeps the machine from the run to a snap
// position. A restore therefore first lets C64_STOP hold the machine, then
// takes OWNER and the debugger's hold away. Returns whether the debugger had
// run control; a restore or load leaves it with C64_STOP (the debugger sees
// that, as when the firmware takes the machine for its menu), a land gives it
// back.
static bool dbg_handover(void)
{
    if (!(trx_io_caps() & CAPS_DBG) || !(r32(DBG_STATUS) & DBG_ST_OWNER)) {
        return false;
    }
    w32(GATE_CTRL, 1);
    C64_STOP = 1;
    w32(DBG_OWNER, 0);
    w32(DBG_CMD, DBG_RUN);
    return true;
}

// ---- machine control -------------------------------------------------------------
static bool wait_reg(uint32_t addr, uint32_t mask, uint32_t want, int ms)
{
    uint16_t t0 = getMsTimer();
    while ((r32(addr) & mask) != want) {
        if (uint16_t(getMsTimer() - t0) > ms) {
            return false;
        }
    }
    return true;
}

static void drain(void)
{
    // the drain empties the FIFO by itself; the sequencer's copy is ~8 us
    wait_reg(LOG_STAT, STAT_FIFO | STAT_COPYING, 0, 20);
}

struct Saved {
    uint32_t gate;
    uint32_t sh;
    bool was_halted;
};

// Stops the machine on line 0 (STOP_POS) if it runs, so the VIC's
// write-only registers keep the values a frame start has; then the stop is
// held by the app's C64_STOP as a machine pause, sessions off.
static void stop_machine(Saved *sv, bool at_line0)
{
    sv->gate = r32(GATE_CTRL);
    sv->sh = r32(SH_CTRL) & 3;
    sv->was_halted = (sv->gate & GATE_HALTED) != 0;
    if (!sv->was_halted && at_line0) {
        w32(STOP_POS, POS_ARM | 12);
        if (!wait_reg(STOP_POS, POS_HOLDING, POS_HOLDING, 60)) {
            printf("[RING] no stop on line 0, pausing anywhere\n");
            w32(STOP_POS, 0);
        }
    }
    w32(SH_CTRL, 0);                // ends a freeze session, no new one
    w32(GATE_CTRL, 1);              // the app's stop = machine pause
    C64_STOP = 1;
    wait_reg(GATE_CTRL, GATE_HALTED, GATE_HALTED, 100);
}

// ---- undo ------------------------------------------------------------------------
// What an undo needs is, for every address written between entry lo's start
// and entry hi's start (hi = tl_n: now), the old byte of its earliest write
// there. collect() walks those records newest first and stores each one's old
// byte, so the earliest write's is what stays, and marks the address; the
// caller then writes each marked byte once. The rvlite core reads DDR2
// without a cache (a load costs about a microsecond) while its stores are
// posted, so the walk is one load per record and stores only; the marks are
// bytes, scanned a word at a time afterwards.
#define C64_MEM(a)  (*(volatile uint8_t *)(C64_MEMORY_BASE + (a)))

struct Coll {
    uint32_t n0, n1, other;
};

static uint8_t *ram_val;            // C64 RAM, 64 KB
static uint8_t *ram_tch;            // a byte per address, 64 KB (word aligned)
static uint8_t *cart_val;           // cartridge RAM, 64 KB
static uint8_t *cart_tch;
static uint8_t col_val[1024];       // colour RAM
static uint32_t col_tch_w[256];
static uint8_t *const col_tch = (uint8_t *)col_tch_w;

static void collect_range(int s, uint32_t from, uint32_t to, Coll *c)
{
    const uint32_t rs = rec_size[s];
    const uint32_t chunk = (BOUNCE - 32) / rs;
    uint8_t *const rv = ram_val, *const rt = ram_tch;
    uint8_t *const kv = cart_val, *const kt = cart_tch;
    while (((to - from) & M28) != 0) {
        uint32_t n = (to - from) & M28;
        if (n > chunk) {
            n = chunk;
        }
        uint32_t start = (to - n) & M28;
        const uint32_t *wp = (const uint32_t *)fetch(s, start, n);
        if (s == 0) {
            for (int k = int(n) - 1; k >= 0; k--) {
                uint32_t r = wp[k];
                uint32_t a = r & 0xFFFF;
                rv[a] = uint8_t(r >> 16);
                rt[a] = 1;
            }
            c->n0 += n;
        } else {
            for (int k = int(n) - 1; k >= 0; k--) {
                uint32_t lo = wp[2 * k];
                uint32_t a = lo & 0xFFFF;
                switch (lo >> 28) {
                case 1:
                    col_val[a & 0x3FF] = uint8_t(wp[2 * k + 1]) & 0x0F;
                    col_tch[a & 0x3FF] = 1;
                    break;
                case 2:
                    kv[a] = uint8_t(wp[2 * k + 1]);
                    kt[a] = 1;
                    break;
                default:
                    c->other++;
                    break;
                }
            }
            c->n1 += n;
        }
        to = start;
    }
}

// entries [lo, hi): their stream 0 and/or stream 1 records, newest first
static void collect(int lo, int hi, bool s0, bool s1, Coll *c)
{
    if (s0) {
        memset(ram_tch, 0, 65536);
    }
    if (s1) {
        memset(col_tch, 0, 1024);
        memset(cart_tch, 0, 65536);
    }
    for (int j = hi - 1; j >= lo; j--) {
        TrxFrame &f = ent(j);
        if (s0) {
            collect_range(0, f.s0, f.e0 == OPEN ? cnt_now[0] : f.e0, c);
        }
        if (s1) {
            collect_range(1, f.s1, f.e1 == OPEN ? cnt_now[1] : f.e1, c);
        }
    }
}

// calls put(a, v) for every marked address (marks: bytes, len a multiple of 4)
template <typename F> static uint32_t each(const uint8_t *tch, int len, const uint8_t *val, F put)
{
    uint32_t n = 0;
    const uint32_t *w = (const uint32_t *)tch;
    for (int i = 0; i < len / 4; i++) {
        uint32_t m = w[i];
        if (!m) {
            continue;
        }
        for (int b = 0; b < 4; b++) {
            if (m & (0xFFu << (8 * b))) {
                int a = 4 * i + b;
                put(a, val[a]);
                n++;
            }
        }
    }
    return n;
}

// ---- restore ---------------------------------------------------------------------
static uint32_t bits(const uint32_t *w, int lo, int n)
{
    uint32_t v = 0;
    for (int b = 0; b < n; b++) {
        v |= ((w[(lo + b) / 32] >> ((lo + b) % 32)) & 1u) << b;
    }
    return v;
}

// VIC registers from the block (read images) as bus writes; $D012-$D014,
// $D019, $D01E, $D01F are not written (their read image is not the
// register), $D011 only when bits 6-0 differ from the machine's now (bit 7
// reads the raster line, so writing it would clear the compare's bit 8).
static void replay_vic(const uint32_t *w, uint32_t *count)
{
    w32(SB_ADDR, (2 << 7) | 4);
    uint32_t now11 = r32(SB_DATA);   // chip 2 word 4 = $D010-$D013
    auto vreg = [&](int r) { return uint8_t(w[W_VIC + r / 4] >> (8 * (r & 3))); };
    for (int r = 0; r <= 0x2E; r++) {
        if (r == 0x12 || r == 0x13 || r == 0x14 || r == 0x19 || r == 0x1E || r == 0x1F) {
            continue;
        }
        if (r == 0x11 && ((vreg(r) ^ uint8_t(now11 >> 8)) & 0x7F) == 0) {
            continue;
        }
        C64_MEM(0xD000 + r) = vreg(r);
        (*count)++;
    }
}

// The CIA registers the subset load leaves out (ARCH §15.9): port and timer
// latches, ICR mask, TOD alarm, from the hidden words (bit positions as
// tb_ring.cpp restoretest uses them).
static void replay_cia(const uint32_t *w, uint32_t *count)
{
    for (int c = 0; c < 2; c++) {
        const uint32_t *h = w + (c ? W_CIA2H : W_CIA1H);
        uint16_t io = c ? 0xDD00 : 0xDC00;
        C64_MEM(io + 2) = bits(h, 16, 8);         // DDRA
        C64_MEM(io + 3) = bits(h, 8, 8);          // DDRB
        C64_MEM(io + 0) = bits(h, 32, 8);         // PRA
        C64_MEM(io + 1) = bits(h, 24, 8);         // PRB
        uint32_t ta = bits(h, 43, 16), tb = bits(h, 83, 16);
        C64_MEM(io + 4) = ta & 0xFF;
        C64_MEM(io + 5) = ta >> 8;
        C64_MEM(io + 6) = tb & 0xFF;
        C64_MEM(io + 7) = tb >> 8;
        C64_MEM(io + 0xD) = 0x7F;
        C64_MEM(io + 0xD) = 0x80 | bits(h, 311, 5);
        C64_MEM(io + 0xF) = 0x80;                 // CRB: alarm writes (CRB itself is loaded)
        C64_MEM(io + 0xB) = bits(h, 123, 8);
        C64_MEM(io + 0xA) = bits(h, 131, 8);
        C64_MEM(io + 0x9) = bits(h, 139, 8);
        C64_MEM(io + 0x8) = bits(h, 147, 8);
        *count += 17;
    }
}

// The cartridge's banking registers (chip 7 w0 = {reg_1, reg_0} of icepi's
// model, cart_slot.v) as bus writes to IO1, for the types whose registers are
// plain writes there (c64_cartridge.v): EasyFlash $DE00/$DE02, MegaByter
// $DE00/$DE02, Ocean / Magic Desk / Magic Desk 16 $DE00. Not replayed: GMod2
// ($DE00 also drives the EEPROM's lines), C64MegaCart ($DF00 is cartridge
// RAM too), Dinamic and C64GS (banked by the address of a read or write),
// Action Replay, Simons' BASIC. Returns false for a cartridge it left as it
// was.
#define W_GLUE         0
#define W_CART         26
#define GLUE_CART      0x08

static bool replay_cart(const uint32_t *w, uint32_t *count)
{
    if (!(w[W_GLUE] & GLUE_CART)) {
        return true;
    }
    uint8_t ct = C64_CARTRIDGE_TYPE;
    uint8_t r0 = uint8_t(w[W_CART]), r1 = uint8_t(w[W_CART] >> 8);
    uint8_t var = ct >> 5;
    switch (ct & 0x1F) {
    case 0x11:              // EasyFlash
    case 0x0F:              // MegaByter
        C64_MEM(0xDE00) = r0;
        C64_MEM(0xDE02) = r1;
        *count += 2;
        return true;
    case 0x08:
        if (var == 0 || var == 1 || var == 5) {     // Ocean, Magic Desk, Magic Desk 16
            C64_MEM(0xDE00) = r0;
            (*count)++;
            return true;
        }
        return false;
    case 0x09:              // Ocean 16K
        C64_MEM(0xDE00) = r0;
        (*count)++;
        return true;
    case 0x01:              // Normal: no registers
        return true;
    default:
        return false;
    }
}

static bool stage_and_load(const uint32_t *w)
{
    w32(SB_STATUS, 0);
    w32(SB_ADDR, (1 << 7) | 8);
    for (int k = 0; k < 7; k++) {
        w32(SB_DATA, w[W_CPUH + k]);
    }
    w32(SB_ADDR, (3 << 7) | 8);
    for (int k = 0; k < 12; k++) {
        w32(SB_DATA, w[W_CIA1H + k]);
    }
    w32(SB_ADDR, (4 << 7) | 8);
    for (int k = 0; k < 12; k++) {
        w32(SB_DATA, w[W_CIA2H + k]);
    }
    if (r32(SB_STATUS) & SB_REFUSED) {
        return false;
    }
    w32(SB_LOAD, SB_LOAD_ALL);
    return true;
}

enum {
    E_NONE = 0,
    E_ABSENT = -1,
    E_INDEX = -2,
    E_NOBLOCK = -3,
    E_GONE = -4,
    E_NOPOS = -5,
    E_STAGE = -6,
    E_MEM = -7,
    E_APPLY = -8,
    E_REU = -9,
};

const char *trx_ring_error(int code)
{
    switch (code) {
    case E_NONE:    return "ok";
    case E_ABSENT:  return "no frame ring in this bitstream";
    case E_INDEX:   return "no such frame";
    case E_NOBLOCK: return "the frame has no whole chip block";
    case E_GONE:    return "the log no longer reaches the frame";
    case E_NOPOS:   return "the machine did not reach the frame's snap position";
    case E_STAGE:   return "the state bus refused the staged words";
    case E_MEM:     return "out of memory";
    case E_APPLY:   return "the engine did not finish applying the log";
    case E_REU:     return TRX_REU_OFF_TEXT;
    default:        return "error";
    }
}

// ---- APPLY (T20 §17.3) -----------------------------------------------------------
// With the engine's APPLY (apply_hw) stream 0 is not walked by the CPU: the
// ranges to undo or redo are planned as runs (contiguous, one direction) and
// the engine applies them through the RAM's second port when the memory is
// put in (apply_plan()). Consecutive timeline entries are contiguous in the
// stream; a cut leaves a gap, so a plan is a few runs. Without APPLY
// plan_range() walks the records at once (walk_range, marks for each_pg).
struct Run {
    uint32_t a, b;          // records [a, b) of stream 0 (28-bit counts)
    bool redo;              // oldest first, new bytes; else newest first, old bytes
};
#define MAX_RUNS       256
static Run runs[MAX_RUNS];
static int n_runs;
static bool plan_full;      // more runs than MAX_RUNS: the plan is not whole
static int apply_err;       // the last apply_plan()'s result (E_NONE or E_APPLY / E_MEM)

// the engine's job: records of stream 0 from index "start", "count" of them,
// downwards (undo) or upwards (redo); false when it does not finish in time
static bool apply_run(uint32_t start, uint32_t count, bool undo)
{
    w32(AP_START, start & M28);
    w32(AP_COUNT, count);
    w32(AP_CMD, undo ? 1 : 0);
    uint32_t ms = 50 + count / 10000;       // the engine does 20 records per us and more
    uint16_t t0 = getMsTimer();
    while (r32(AP_CMD) & 1) {
        if (uint16_t(getMsTimer() - t0) > ms) {
            printf("[RING] APPLY did not finish: %u records from %u\n", (unsigned)count, (unsigned)start);
            return false;
        }
    }
    return true;
}

static void walk_range(int s, uint32_t from, uint32_t to, bool redo, Coll *c);

static void plan_range(uint32_t a, uint32_t b, bool redo, Coll *c)
{
    uint32_t n = (b - a) & M28;
    if (!n) {
        return;
    }
    if (!apply_hw) {
        walk_range(0, a, b, redo, c);
        return;
    }
    c->n0 += n;
    if (n_runs) {
        Run &r = runs[n_runs - 1];
        if (r.redo == redo && (redo ? r.b == a : r.a == b)) {
            if (redo) {
                r.b = b;
            } else {
                r.a = a;
            }
            return;
        }
    }
    if (n_runs == MAX_RUNS) {
        plan_full = true;
        return;
    }
    runs[n_runs++] = Run{ a, b, redo };
}

// the planned runs, in the order they were planned; the plan is empty after
static int apply_plan(void)
{
    int err = plan_full ? E_MEM : E_NONE;
    for (int i = 0; !err && i < n_runs; i++) {
        const Run &r = runs[i];
        uint32_t n = (r.b - r.a) & M28;
        if (!apply_run(r.redo ? r.a : (r.b - 1) & M28, n, !r.redo)) {
            err = E_APPLY;
        }
    }
    n_runs = 0;
    plan_full = false;
    return err;
}

// Phases 3-8 of the restore with the machine stopped by stop_machine(): the
// register replay and colour RAM as bus cycles (Ultimax, so the I/O is seen
// whatever $01 holds), the run to the snap position with the CPU held, the
// RAM through the second port, the staged words, release. mem() puts the
// memories in: undo for a ring entry, a full copy for a loaded state.
typedef void (*bus_mem_fn)(void *ctx);   // colour RAM, while the bus is ours
typedef void (*ram_fn)(void *ctx);       // C64 RAM, at the stop position

// While the machine runs to the snap position (CPU held), the VIC passes
// every raster line, so its compare sets the raster IRQ latch and sprites set
// the collision latches, which the block's frame did not have: an extra IRQ
// or collision after the restore. The latches are not on the state bus, so
// they are cleared by DMA cycles during the run ($D019 written, $D01E/$D01F
// read: the CPU is held anyway, the VIC goes on in step) for the bits the
// block's frame had clear, until line 300. No DMA after that: one that came
// once the machine stood on the position would open the clock again. A
// compare on lines 300-311 or 0 still leaves its latch (put_back reports
// it). Only when $01 shows the I/O (no Ultimax while the VIC runs).
static void clear_vic_latches(const uint32_t *w)
{
    uint8_t port = uint8_t(w[W_CPU + 2] >> 16);     // $01 as read
    if (!(port & 4) || !(port & 3)) {
        return;
    }
    uint8_t clr = ~uint8_t(w[W_VIC + 6] >> 8) & 0x0F;
    bool c1e = uint8_t(w[W_VIC + 7] >> 16) == 0, c1f = uint8_t(w[W_VIC + 7] >> 24) == 0;
    uint16_t t0 = getMsTimer();
    while (!(r32(STOP_POS) & POS_HOLDING) && uint16_t(getMsTimer() - t0) < 60) {
        int raster = C64_MEM(0xD012) | ((C64_MEM(0xD011) & 0x80) << 1);
        if (raster >= 300) {
            break;
        }
        if (clr) {
            C64_MEM(0xD019) = clr;
        }
        if (c1e) {
            (void)C64_MEM(0xD01E);
        }
        if (c1f) {
            (void)C64_MEM(0xD01F);
        }
    }
}

static int put_back(const uint32_t *w, uint16_t x, const Saved &sv, bus_mem_fn busmem, ram_fn ram,
                    void *ctx, uint32_t *bus_writes, uint16_t *pos_ms = NULL, bool *latch_ok = NULL)
{
    uint8_t memonly = C64_DMA_MEMONLY;
    C64_DMA_MEMONLY = 0;
    C64_MODE = C64_MODE_ULTIMAX;
    if (!replay_cart(w, bus_writes)) {
        printf("[RING] cartridge type %02x: banking registers not replayed\n", C64_CARTRIDGE_TYPE);
    }
    if (busmem) {
        busmem(ctx);
    }
    replay_vic(w, bus_writes);
    replay_cia(w, bus_writes);
    C64_MODE = MODE_NORMAL;

    // hold the CPU (CPU-only pause), let the machine run to the snap position
    w32(GATE_CTRL, 0);
    w32(STOP_POS, POS_ARM | POS_RELEASE | ((x >> 3) & 0x3F));
    int err = E_NONE;
    clear_vic_latches(w);
    if (!wait_reg(STOP_POS, POS_HOLDING, POS_HOLDING, 100)) {
        err = E_NOPOS;
    }
    if (latch_ok) {
        // the VIC's latches as they are now against the block's ($D019 in
        // chip 2 w6 byte 1, $D01E/$D01F w7 bytes 2/3)
        w32(SB_ADDR, (2 << 7) | 6);
        uint32_t v6 = r32(SB_DATA), v7 = r32(SB_DATA);
        *latch_ok = ((v6 >> 8) & 0x0F) == ((w[W_VIC + 6] >> 8) & 0x0F) && (v7 >> 16) == (w[W_VIC + 7] >> 16);
    }
    if (pos_ms) {
        *pos_ms = getMsTimer();
    }

    // C64 RAM through the second port (the CPU is held at a read: no more
    // writes of its own). The stop position holds the whole gate now, so the app's pause
    // is let go first: with C64_STOP set, ultimate_sys makes a MEMONLY write of $0000/$0001
    // a bus cycle (T16 Q8), which opens the gate for one edge and moves the machine off the
    // snap position the block is loaded on. The CPU walk's undo of the KERNAL IRQ's STA $01
    // did that (T20: $01F2 differed from the engine's APPLY, which only writes RAM).
    if (!err) {
        C64_STOP = 0;
    }
    C64_DMA_MEMONLY = 1;
    if (ram) {
        ram(ctx);
    }
    C64_DMA_MEMONLY = memonly;

    if (!err && !stage_and_load(w)) {
        err = E_STAGE;
    }
    if (err) {
        w32(SB_LOAD, 0);
    }

    // release: the hold of the stop position keeps the machine until b9
    C64_STOP = 0;
    w32(GATE_CTRL, sv.gate & 1);
    w32(SH_CTRL, sv.sh);
    return err;
}

struct RestoreCtx {
    int idx;
    TrxRestoreStats *st;
};

// colour RAM as bus cycles (stopped, Ultimax); cartridge RAM in DDR2
// (cart_slot.v: 0xEF0000 + address), then the cartridge drops its cached lines
static void restore_bus(void *ctx)
{
    RestoreCtx *c = (RestoreCtx *)ctx;
    Coll cl = { 0, 0, 0 };
    collect(c->idx, tl_n, false, true, &cl);
    c->st->rec1 = cl.n1;
    c->st->other = cl.other;
    c->st->bus_writes += each(col_tch, 1024, col_val, [](int a, uint8_t v) { C64_MEM(0xD800 + a) = v; });
    c->st->cart_bytes = each(cart_tch, 65536, cart_val,
                             [](int a, uint8_t v) { *(volatile uint8_t *)(0x0EF0000 + a) = v; });
    if (c->st->cart_bytes) {
        __asm__ volatile ("fence" ::: "memory");
        ioWrite8(CART_CMD, CART_INVAL);
    }
}

// C64 RAM through the second port
static void restore_ram(void *ctx)
{
    RestoreCtx *c = (RestoreCtx *)ctx;
    Coll cl = { 0, 0, 0 };
    if (apply_hw) {
        // the entries' records newest first, by the engine
        n_runs = 0;
        plan_full = false;
        for (int j = tl_n - 1; j >= c->idx; j--) {
            TrxFrame &f = ent(j);
            plan_range(f.s0, f.e0 == OPEN ? cnt_now[0] : f.e0, false, &cl);
        }
        apply_err = apply_plan();
        c->st->rec0 = cl.n0;
        c->st->ram_writes = cl.n0;      // records applied (no coalescing)
        return;
    }
    collect(c->idx, tl_n, true, false, &cl);
    c->st->rec0 = cl.n0;
    c->st->ram_writes = each(ram_tch, 65536, ram_val, [](int a, uint8_t v) { C64_MEM(a) = v; });
}

static int restore_locked(int idx, TrxRestoreStats *st)
{
    uint16_t t0 = getMsTimer();
    memset(st, 0, sizeof(*st));
    if (idx < 0 || idx >= tl_n) {
        return E_INDEX;
    }
    uint32_t seq = ent(idx).seq;
    if (dbg_handover()) {
        wait_reg(GATE_CTRL, GATE_HALTED, GATE_HALTED, 100);
    }
    Saved sv;
    stop_machine(&sv, true);
    // One bus cycle with the log still on: the first DMA holds the CPU at its
    // next read, and the writes it still makes before that (a push sequence,
    // a read-modify-write) are logged and undone with the rest.
    C64_DMA_MEMONLY = 0;
    (void)C64_MEM(0x0002);
    drain();
    parse_locked();                 // up to the stop (trim() may drop from the front)
    idx = -1;
    for (int i = tl_n - 1; i >= 0; i--) {
        if (ent(i).seq == seq) {
            idx = i;
            break;
        }
    }
    int err = E_NONE;
    if (idx < 0) {
        err = E_GONE;
    } else if (!ent(idx).block) {
        err = E_NOBLOCK;
    } else if (!reachable(ent(idx))) {
        err = E_GONE;
    }
    if (err) {
        C64_STOP = 0;
        w32(GATE_CTRL, sv.gate & 1);
        w32(SH_CTRL, sv.sh);
        if (r32(STOP_POS) & POS_HOLDING) {
            w32(STOP_POS, POS_RELEASE);
        }
        return err;
    }
    w32(LOG_CTRL, 0);
    w32(CAP_CTRL, 0);
    log_on = false;

    TrxFrame f = ent(idx);
    st->frames = tl_n - 1 - idx;
    st->ms_stop = uint16_t(getMsTimer() - t0);
    RestoreCtx rc;
    rc.idx = idx;
    rc.st = st;
    uint16_t tpos = t0;
    bool latch_ok = true;
    apply_err = E_NONE;
    err = put_back(f.w, f.x, sv, restore_bus, restore_ram, &rc, &st->bus_writes, &tpos, &latch_ok);
    if (!err) {
        err = apply_err;
    }
    st->vic_latches = latch_ok;
    st->ms_pos = uint16_t(tpos - t0);

    // the frames after it are cut; the re-run of the entry's frame logs from
    // the counts of now on
    tl_n = idx + 1;
    TrxFrame &e = ent(idx);
    e.s0 = cnt_now[0];
    e.s1 = cnt_now[1];
    e.e0 = e.e1 = OPEN;
    rebase_cycles(e, r32(DBG_CYCLES) + 1);   // + the load edge itself
    w32(CAP_CTRL, 1);
    w32(LOG_CTRL, logctl(true));
    log_on = true;
    w32(STOP_POS, POS_RELEASE);     // go on: the load is the first edge
    st->ms = uint16_t(getMsTimer() - t0);
    st->next_seq = next_seq;
    printf("[RING] restore to entry %d (seq %u): %u frames, %u + %u records, %u RAM / %u bus writes, "
           "%u ms (stopped at %u, at the position %u), %s\n",
           idx, (unsigned)f.seq, (unsigned)st->frames, (unsigned)st->rec0, (unsigned)st->rec1,
           (unsigned)st->ram_writes, (unsigned)st->bus_writes, (unsigned)st->ms, (unsigned)st->ms_stop,
           (unsigned)st->ms_pos, trx_ring_error(err));
    return err;
}

// ---- scrub (TRX64-Ultimate docs/specs/T20-delta-ring.md §16.7) ------------------
// Timewarp moves the paused machine from the entry it shows (D) to another (T)
// and lets it draw that frame: the writes since the last step undone, D -> T
// by the logged values (back: old bytes, newest first; forward: new bytes,
// oldest first; the program never re-runs to go forward), T's block put in as
// a restore puts it, one frame run with the log on, a stop on line 0 of the
// next frame. That frame's writes stay in the log until the next step (or the
// final rewind) takes them back, so between steps the machine is "D plus one
// frame" and the still capture holds the picture the VIC drew from D.
//
// The walks mark pages, so a step costs its records, not 64 KB scans.
#define STILL_STATUS   (TRX_IO_BASE + 0x6068)
#define SCRUB_STOP_CYC 4        // line 0 cycle 4: before every snap (x >= 96)

static struct {
    Saved sv;               // the freeze's gate and sessions, for the final release
    uint8_t mode, memonly;  // the menu's DMA mode, put back after every step
    bool d_now;             // D is the freeze point (the closed head's end)
    uint32_t d_seq;         // else D's entry
    uint32_t mark[2];       // stream 0/1 counts since which the log holds writes not undone
    uint32_t land_c24;      // the debug block's CYCLES after a land's load edge (T21)
} scr;
static uint8_t ram_pg[256], cart_pg[256];

// records [from, to) of stream s: redo (oldest first, new bytes) or undo
// (newest first, old bytes); the last store per address wins either way
static void walk_range(int s, uint32_t from, uint32_t to, bool redo, Coll *c)
{
    const uint32_t rs = rec_size[s];
    const uint32_t chunk = (BOUNCE - 32) / rs;
    uint8_t *const rv = ram_val, *const rt = ram_tch;
    uint8_t *const kv = cart_val, *const kt = cart_tch;
    uint32_t left = (to - from) & M28;
    while (left) {
        uint32_t n = left < chunk ? left : chunk;
        uint32_t start = redo ? from : ((to - n) & M28);
        const uint32_t *wp = (const uint32_t *)fetch(s, start, n);
        int k = redo ? 0 : int(n) - 1;
        const int step = redo ? 1 : -1;
        if (s == 0) {
            const int sh = redo ? 24 : 16;
            for (uint32_t i = 0; i < n; i++, k += step) {
                uint32_t r = wp[k];
                uint32_t a = r & 0xFFFF;
                rv[a] = uint8_t(r >> sh);
                rt[a] = 1;
                ram_pg[a >> 8] = 1;
            }
            c->n0 += n;
        } else {
            const int sh = redo ? 8 : 0;
            for (uint32_t i = 0; i < n; i++, k += step) {
                uint32_t lo = wp[2 * k];
                uint8_t v = uint8_t(wp[2 * k + 1] >> sh);
                uint32_t a = lo & 0xFFFF;
                switch (lo >> 28) {
                case 1:
                    col_val[a & 0x3FF] = v & 0x0F;
                    col_tch[a & 0x3FF] = 1;
                    break;
                case 2:
                    kv[a] = v;
                    kt[a] = 1;
                    cart_pg[a >> 8] = 1;
                    break;
                default:
                    c->other++;
                    break;
                }
            }
            c->n1 += n;
        }
        left -= n;
        if (redo) {
            from = (from + n) & M28;
        } else {
            to = (to - n) & M28;
        }
    }
}

// entries [lo, hi): undo newest first, or redo oldest first
static void walk_entries(int lo, int hi, bool redo, Coll *c)
{
    for (int i = 0; i < hi - lo; i++) {
        TrxFrame &f = ent(redo ? lo + i : hi - 1 - i);
        plan_range(f.s0, f.e0 == OPEN ? cnt_now[0] : f.e0, redo, c);
        walk_range(1, f.s1, f.e1 == OPEN ? cnt_now[1] : f.e1, redo, c);
    }
}

// the marked bytes of a 64 KB space, page by page; the marks are cleared
template <typename F> static uint32_t each_pg(uint8_t *pg, uint8_t *tch, const uint8_t *val, F put)
{
    uint32_t n = 0;
    for (int p = 0; p < 256; p++) {
        if (!pg[p]) {
            continue;
        }
        pg[p] = 0;
        uint32_t *w = (uint32_t *)(tch + 256 * p);
        for (int i = 0; i < 64; i++) {
            uint32_t m = w[i];
            if (!m) {
                continue;
            }
            w[i] = 0;
            for (int b = 0; b < 4; b++) {
                if (m & (0xFFu << (8 * b))) {
                    int a = 256 * p + 4 * i + b;
                    put(a, val[a]);
                    n++;
                }
            }
        }
    }
    return n;
}

struct ScrubCtx {
    TrxRestoreStats *st;
    const uint32_t *w;      // the block being put in
};

// colour RAM as bus cycles, cartridge RAM in DDR2 (as restore_bus)
static void scrub_bus(void *ctx)
{
    TrxRestoreStats *st = ((ScrubCtx *)ctx)->st;
    for (int a = 0; a < 1024; a++) {
        if (col_tch[a]) {
            C64_MEM(0xD800 + a) = col_val[a];
            st->bus_writes++;
        }
    }
    memset(col_tch, 0, 1024);
    // The frame run may have left IRQ or collision latches the block's frame
    // had clear, and the run to the snap position is only a few cycles from
    // the scrub's stop (too short for clear_vic_latches): cleared here.
    const uint32_t *w = ((ScrubCtx *)ctx)->w;
    uint8_t clr = ~uint8_t(w[W_VIC + 6] >> 8) & 0x0F;
    if (clr) {
        C64_MEM(0xD019) = clr;
    }
    if (uint8_t(w[W_VIC + 7] >> 16) == 0) {
        (void)C64_MEM(0xD01E);
    }
    if (uint8_t(w[W_VIC + 7] >> 24) == 0) {
        (void)C64_MEM(0xD01F);
    }
    st->bus_writes += 3;
    st->cart_bytes = each_pg(cart_pg, cart_tch, cart_val,
                             [](int a, uint8_t v) { *(volatile uint8_t *)(0x0EF0000 + a) = v; });
    if (st->cart_bytes) {
        __asm__ volatile ("fence" ::: "memory");
        ioWrite8(CART_CMD, CART_INVAL);
    }
}

// C64 RAM through the second port
static void scrub_ram(void *ctx)
{
    TrxRestoreStats *st = ((ScrubCtx *)ctx)->st;
    if (apply_hw) {
        apply_err = apply_plan();       // the plan scrub_walk_locked made
        st->ram_writes = st->rec0;
        return;
    }
    st->ram_writes = each_pg(ram_pg, ram_tch, ram_val, [](int a, uint8_t v) { C64_MEM(a) = v; });
}

// The CIA 1 port directions the keyboard scan needs (C64::init_io sets them
// in the freeze session; without one the scan drives the real CIA). Every
// step's and the final load replays the block's. The slim core's menu scans
// the hardware matrix (KEYB_COL / KEYB_ROW) and the CIA stays the program's.
static void scrub_keyboard_io(void)
{
    if (trx_io_slim()) {
        return;
    }
    C64_DMA_MEMONLY = 0;
    C64_MODE = C64_MODE_ULTIMAX;
    C64_MEM(0xDC02) = 0xFF;
    C64_MEM(0xDC03) = 0x00;
    C64_MODE = scr.mode;
    C64_DMA_MEMONLY = scr.memonly;
}

// The first step: the freeze session ends (the machine pause stays), the head
// is closed at the counts of now, capture off. The log stays on: what the
// menu writes from here on is undone by the next step.
static int scrub_begin_locked(void)
{
    parse_locked();
    if (!tl_n) {
        return E_INDEX;
    }
    scr.mode = C64_MODE;
    scr.memonly = C64_DMA_MEMONLY;
    stop_machine(&scr.sv, true);
    C64_DMA_MEMONLY = 0;
    (void)C64_MEM(0x0002);          // as restore_locked: the CPU's last writes logged
    drain();
    parse_locked();
    TrxFrame &h = ent(tl_n - 1);
    if (h.e0 == OPEN) {
        h.e0 = cnt_now[0];
        h.e1 = cnt_now[1];
    }
    scr.mark[0] = cnt_now[0];
    scr.mark[1] = cnt_now[1];
    w32(CAP_CTRL, 0);
    log_on = false;                 // the ring records nothing now: the poll leaves it alone
    memset(ram_tch, 0, 65536);
    memset(cart_tch, 0, 65536);
    memset(col_tch, 0, 1024);
    memset(ram_pg, 0, sizeof(ram_pg));
    memset(cart_pg, 0, sizeof(cart_pg));
    scr.d_now = true;
    scrubbing = true;
    scrub_keyboard_io();
    return E_NONE;
}

// Log off; the writes since the mark undone and D -> T walked into the marks.
static int scrub_walk_locked(int idx, Coll *cl)
{
    drain();
    w32(LOG_CTRL, 0);
    cnt_now[0] = r32(LOG_CNT(0)) & M28;
    cnt_now[1] = r32(LOG_CNT(1)) & M28;
    n_runs = 0;
    plan_full = false;
    plan_range(scr.mark[0], cnt_now[0], false, cl);
    walk_range(1, scr.mark[1], cnt_now[1], false, cl);
    scr.mark[0] = cnt_now[0];
    scr.mark[1] = cnt_now[1];
    int d = tl_n;
    if (!scr.d_now) {
        d = -1;
        for (int i = tl_n - 1; i >= 0; i--) {
            if (ent(i).seq == scr.d_seq) {
                d = i;
                break;
            }
        }
        if (d < 0) {
            return E_GONE;          // cannot happen with SCRUB_MARGIN
        }
    }
    if (idx < d) {
        walk_entries(idx, d, false, cl);
    } else if (idx > d) {
        walk_entries(d, idx, true, cl);
    }
    return E_NONE;
}

static int scrub_show_locked(int idx, TrxRestoreStats *st)
{
    uint16_t t0 = getMsTimer();
    memset(st, 0, sizeof(*st));
    if (!scrubbing) {
        int err = scrub_begin_locked();
        if (err) {
            return err;
        }
    }
    if (idx < 0 || idx >= tl_n || !ent(idx).block || !reachable(ent(idx), SCRUB_MARGIN)) {
        return E_NOBLOCK;
    }
    Coll cl = { 0, 0, 0 };
    int err = scrub_walk_locked(idx, &cl);
    if (err) {
        return err;
    }
    st->rec0 = cl.n0;
    st->rec1 = cl.n1;
    st->other = cl.other;
    TrxFrame f = ent(idx);
    ScrubCtx sc = { st, f.w };
    Saved pv = { 1, 0, true };      // released into a running machine, no sessions
    apply_err = E_NONE;
    err = put_back(f.w, f.x, pv, scrub_bus, scrub_ram, &sc, &st->bus_writes);
    if (!err) {
        err = apply_err;
    }
    st->ms_pos = uint16_t(getMsTimer() - t0);

    // one frame with the log on, stopped on line 0 of the next one
    uint32_t still_n = r32(STILL_STATUS) >> 8;
    w32(LOG_CTRL, logctl(true));
    w32(STOP_POS, POS_ARM | POS_RELEASE | SCRUB_STOP_CYC);
    if (!wait_reg(STOP_POS, POS_HOLDING, POS_HOLDING, 60) && !err) {
        err = E_NOPOS;
    }
    w32(GATE_CTRL, 1);
    C64_STOP = 1;
    wait_reg(GATE_CTRL, GATE_HALTED, GATE_HALTED, 100);
    C64_DMA_MEMONLY = 0;
    (void)C64_MEM(0x0002);          // the CPU's last writes into the log too
    C64_MODE = scr.mode;
    C64_DMA_MEMONLY = scr.memonly;
    scrub_keyboard_io();
    drain();
    p2 = r32(LOG_CNT(2)) & M28;     // the run's markers are no timeline entries
    cnt_now[0] = r32(LOG_CNT(0)) & M28;
    cnt_now[1] = r32(LOG_CNT(1)) & M28;
    scr.d_now = false;
    scr.d_seq = f.seq;
    trim();                         // the run used one frame of the areas
    // the still of that frame: complete at line 285 (60 Hz modes: line 262), long before the stop
    uint16_t t1 = getMsTimer();
    while ((r32(STILL_STATUS) >> 8) == still_n && uint16_t(getMsTimer() - t1) < 10)
        ;
    st->frames = 1;
    st->ms = uint16_t(getMsTimer() - t0);
    printf("[RING] scrub to seq %u: %u + %u records, %u RAM / %u bus writes, %u ms (%u to the position) %s\n",
           (unsigned)f.seq, (unsigned)st->rec0, (unsigned)st->rec1, (unsigned)st->ram_writes,
           (unsigned)st->bus_writes, (unsigned)st->ms, (unsigned)st->ms_pos, trx_ring_error(err));
    return err;
}

// RETURN / RUN/STOP: the machine onto entry idx from the scrub's state, the
// timeline cut after it, log and capture on, release (restore_locked's end).
static int scrub_final_locked(int idx, TrxRestoreStats *st)
{
    uint16_t t0 = getMsTimer();
    memset(st, 0, sizeof(*st));
    int err = E_NONE;
    Coll cl = { 0, 0, 0 };
    if (idx < 0 || idx >= tl_n || !ent(idx).block) {
        err = E_NOBLOCK;
    } else {
        err = scrub_walk_locked(idx, &cl);
    }
    scrubbing = false;
    if (err) {
        // nothing to go back to: the log and capture back on, the machine
        // released as it stands
        w32(CAP_CTRL, 1);
        w32(LOG_CTRL, logctl(true));
        log_on = true;
        C64_MODE = MODE_NORMAL;
        C64_STOP = 0;
        w32(GATE_CTRL, scr.sv.gate & 1);
        w32(SH_CTRL, scr.sv.sh);
        w32(STOP_POS, POS_RELEASE);
        return err;
    }
    st->rec0 = cl.n0;
    st->rec1 = cl.n1;
    st->other = cl.other;
    TrxFrame f = ent(idx);
    ScrubCtx sc = { st, f.w };
    bool latch_ok = true;
    apply_err = E_NONE;
    err = put_back(f.w, f.x, scr.sv, scrub_bus, scrub_ram, &sc, &st->bus_writes, NULL, &latch_ok);
    if (!err) {
        err = apply_err;
    }
    st->vic_latches = latch_ok;
    tl_n = idx + 1;
    TrxFrame &e = ent(idx);
    e.s0 = cnt_now[0];
    e.s1 = cnt_now[1];
    e.e0 = e.e1 = OPEN;
    p2 = r32(LOG_CNT(2)) & M28;
    rebase_cycles(e, r32(DBG_CYCLES) + 1);
    w32(CAP_CTRL, 1);
    w32(LOG_CTRL, logctl(true));
    log_on = true;
    w32(STOP_POS, POS_RELEASE);
    st->ms = uint16_t(getMsTimer() - t0);
    st->next_seq = next_seq;
    printf("[RING] scrub rewind to seq %u: %u + %u records, %u ms, %s\n", (unsigned)f.seq,
           (unsigned)st->rec0, (unsigned)st->rec1, (unsigned)st->ms, trx_ring_error(err));
    return err;
}

// ---- the debugger's land and commit (T21 §4.10) ---------------------------------
// The scrub's walk and put-back, but the machine ends standing on the entry's
// snap edge itself: the debug block's CYC_CMP is armed for the next edge while
// the machine holds one edge before it (STOP_POS), so the release lets exactly
// the load edge through. The debugger owns run control (OWNER), which takes the
// app's C64_STOP away from the gate; for the restore it is handed back to
// C64_STOP (which already holds the machine) and taken again before the release.

static int land_locked(int idx, TrxRestoreStats *st)
{
    uint16_t t0 = getMsTimer();
    memset(st, 0, sizeof(*st));
    uint32_t gate0 = r32(GATE_CTRL) & 1;
    // The session mode of the stop this land starts from: the stop it ends in gets it back.
    // stop_machine's SH_CTRL 0 must not outlive the land; a debugger stop is a freeze session
    // only with SH_MODE != 0 (T21 as-built 2026-10-05).
    uint32_t sh0 = r32(SH_CTRL) & 3;
    // C64_STOP takes the machine over from the debugger's hold
    bool owned = dbg_handover();
    w32(GATE_CTRL, 1);
    C64_STOP = 1;
    wait_reg(GATE_CTRL, GATE_HALTED, GATE_HALTED, 100);
    if (scrubbing) {
        // a land from a land: the put-back's bus cycles must reach the chips, not a session's
        // overlay (the first land's stop is a session); the first one has stop_machine do it
        w32(SH_CTRL, 0);
    }
    int err = E_NONE;
    if (!scrubbing) {
        err = scrub_begin_locked();
        scr.sv.gate = gate0;        // what commit puts back
    }
    if (!err && (idx < 0 || idx >= tl_n || !ent(idx).block || !reachable(ent(idx), SCRUB_MARGIN))) {
        err = E_NOBLOCK;
    }
    Coll cl = { 0, 0, 0 };
    if (!err) {
        err = scrub_walk_locked(idx, &cl);
    }
    if (err) {
        // the machine stays where it is, held by the debugger again
        w32(DBG_OWNER, owned ? 1 : 0);
        w32(DBG_CMD, 0x01);         // PAUSE_CB: the gate is shut, a record at once
        C64_STOP = 0;
        if (!scrubbing) {
            w32(GATE_CTRL, gate0);
        }
        w32(SH_CTRL, sh0);          // the debugger's stop is a session again
        return err;
    }
    st->rec0 = cl.n0;
    st->rec1 = cl.n1;
    st->other = cl.other;
    TrxFrame f = ent(idx);
    ScrubCtx sc = { st, f.w };
    Saved pv = { 1, 0, true };
    apply_err = E_NONE;
    err = put_back(f.w, f.x, pv, scrub_bus, scrub_ram, &sc, &st->bus_writes);
    if (!err) {
        err = apply_err;
    }
    st->ms_pos = uint16_t(getMsTimer() - t0);
    // the machine holds one edge before the snap's phi2 fall: CYC_CMP on that edge
    uint32_t now = r32(DBG_CYCLES);
    w32(DBG_CYC_CMP, now);
    scr.land_c24 = now + 1;         // the count after the load edge, as F2 has it after a snap
    ep_push(f.cycles, scr.land_c24);
    w32(DBG_OWNER, 1);
    // no bus cycle from here on: it would open the clock before the release
    C64_MODE = scr.mode;
    C64_DMA_MEMONLY = scr.memonly;
    w32(LOG_CTRL, logctl(true));      // what the debugger runs from here is undone by the next land
    w32(STOP_POS, POS_RELEASE);
    if (!wait_reg(DBG_STATUS, DBG_ST_NEW, DBG_ST_NEW, 100) && !err) {
        err = E_NOPOS;
    }
    if (!owned) {
        w32(DBG_OWNER, 0);
    }
    // The machine stands on the load edge, held by the debugger: its session starts now, from
    // the restored chips (the responder keeps no copy from before, ARCH 7.6).
    w32(SH_CTRL, sh0);
    p2 = r32(LOG_CNT(2)) & M28;
    scr.d_now = false;
    scr.d_seq = f.seq;
    st->frames = tl_n - 1 - idx;
    st->ms = uint16_t(getMsTimer() - t0);
    printf("[RING] land on seq %u: %u + %u records, %u RAM / %u bus writes, %u ms, %s\n", (unsigned)f.seq,
           (unsigned)st->rec0, (unsigned)st->rec1, (unsigned)st->ram_writes, (unsigned)st->bus_writes,
           (unsigned)st->ms, trx_ring_error(err));
    return err;
}

static void commit_locked(void)
{
    if (!scrubbing) {
        return;
    }
    scrubbing = false;
    int d = -1;
    if (!scr.d_now) {
        for (int i = tl_n - 1; i >= 0; i--) {
            if (ent(i).seq == scr.d_seq) {
                d = i;
                break;
            }
        }
    }
    if (d >= 0) {
        tl_n = d + 1;
        TrxFrame &e = ent(d);
        e.s0 = scr.mark[0];
        e.s1 = scr.mark[1];
        e.e0 = e.e1 = OPEN;
        rebase_cycles(e, scr.land_c24);
    }
    p2 = r32(LOG_CNT(2)) & M28;
    w32(CAP_CTRL, 1);
    w32(LOG_CTRL, logctl(true));
    log_on = true;
    w32(GATE_CTRL, scr.sv.gate & 1);
    w32(SH_CTRL, scr.sv.sh);
    printf("[RING] commit: the timeline goes on from seq %u\n", d >= 0 ? (unsigned)ent(d).seq : 0u);
}

int trx_ring_land(int index, TrxRestoreStats *stats)
{
    TrxRestoreStats dummy;
    if (!stats) {
        stats = &dummy;
    }
    if (!lock()) {
        return E_ABSENT;
    }
    if (reu_on()) {
        unlock();
        return E_REU;
    }
    int err = land_locked(index, stats);
    unlock();
    return err;
}

void trx_ring_commit(void)
{
    if (!lock()) {
        return;
    }
    commit_locked();
    unlock();
}

uint32_t trx_ring_landed(void)
{
    return (scrubbing && !scr.d_now) ? scr.d_seq : 0;
}

bool trx_ring_reachable(int index)
{
    if (!lock()) {
        return false;
    }
    bool ok = index >= 0 && index < tl_n && ent(index).block && reachable(ent(index), scrubbing ? SCRUB_MARGIN : 0);
    unlock();
    return ok;
}

// ---- API -------------------------------------------------------------------------
static void start_locked(void)
{
    w32(LOG_CTRL, 0);
    tl_clear();
    ep_reset();
    p2 = 0;
    cyc_have = false;
    cyc_acc = 0;
    resyncs = 0;
    w32(CAP_CTRL, 1);
    w32(LOG_CTRL, LOG_RESTART | logctl(false));
    log_on = true;
    log_rd = 0; // the counters start again: the observer log reader goes on from the new beginning
}

// 60 Hz modes (NTSC, PAL-60): 65 cycles x 263 lines at the NTSC clock; 50 Hz
// modes: 63 x 312 at the PAL clock
static bool fmt_60hz(void)
{
    return (C64_VIDEOFORMAT & VIDEO_FMT_60_HZ) != 0;
}

bool trx_ring_apply_hw(int mode)
{
    if (mode == 0) {
        apply_hw = false;
    } else if (mode == 1) {
        apply_hw = (ioRead8(TRX_CAPS2) & TRX_CAPS2_APPLY) != 0;
    }
    return apply_hw;
}

int trx_ring_fps(void)
{
    return fmt_60hz() ? 60 : 50;
}

uint32_t trx_ring_fps_x100(void)
{
    return fmt_60hz() ? 5983 : 5012;
}

uint32_t trx_ring_frame_cycles(void)
{
    return fmt_60hz() ? 17095 : 19656;
}

bool trx_ring_start(void)
{
    if (!lock()) {
        return false;
    }
    if (reu_on()) {
        unlock();
        return false;
    }
    start_locked();
    unlock();
    return true;
}

void trx_ring_stop(void)
{
    if (!lock()) {
        return;
    }
    w32(LOG_CTRL, 0);
    log_on = false;
    unlock();
}

void trx_ring_status(TrxRingStatus *st)
{
    memset(st, 0, sizeof(*st));
    if (!lock()) {
        return;
    }
    parse_locked();
    st->present = true;
    st->on = log_on;
    st->reu_off = reu_on();
    st->frames = tl_n;
    for (int i = 0; i < tl_n; i++) {
        if (ent(i).block) {
            st->targets++;
        }
    }
    if (tl_n) {
        st->oldest_seq = ent(0).seq;
        st->head_seq = ent(tl_n - 1).seq;
    }
    for (int s = 0; s < 3; s++) {
        st->log_cnt[s] = r32(LOG_CNT(s)) & M28;
    }
    st->dropped = r32(DROPPED) & 0xFFFF;
    st->log_stat = r32(LOG_STAT);
    st->resyncs = resyncs;
    st->spurious_f4 = spurious_f4;
    unlock();
}

const char *trx_ring_reu_refusal(void)
{
    return (present && reu_on()) ? TRX_REU_OFF_TEXT : NULL;
}

// The one place the ring follows the REU (A08). Called wherever the REU enable can change
// (C64::set_emulation_flags, init_cartridge, effectuate_settings) and by every poll, which also
// catches a change made behind those. It never waits for the ring: a restore in progress holds
// the mutex, and the next poll looks again.
void trx_ring_reu_sync(void)
{
    if (!present || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return;
    }
    switch (trx_reu_gate_step(&reu_gate, reu_on(), scrubbing, log_on)) {
    case TRX_REU_STOP:
        w32(LOG_CTRL, 0);
        log_on = false;
        pending = -1;
        tl_clear();
        ep_reset();
        p2 = 0;
        cyc_have = false;
        cyc_acc = 0;
        resyncs = 0;
        printf("[RING] REU enabled: ring off, frames dropped (A08)\n");
        break;
    case TRX_REU_START:
        start_locked();
        printf("[RING] REU off: ring restarted from now (A08)\n");
        break;
    default:
        break;
    }
    unlock();
}

void trx_ring_poll(void)
{
    trx_ring_reu_sync();
    if (!lock()) {
        return;
    }
    if (log_on && (ioRead8(TRX_CAPS2) & TRX_CAPS2_TURBO)) {
        // The engine ran faster than 1 MHz since the last look: its RAM writes are not in the log
        // (T29 §6.1), the ring cannot be walked back across them. Start it over once the engine is off.
        uint32_t st = r32(TRX_TURBO_STAT);
        if ((st & TRX_TURBO_RAN) && !(st & TRX_TURBO_ON)) {
            w32(TRX_TURBO_CTRL, 1);
            start_locked();
            printf("[TRX] Timewarp ring restarted: the machine ran in turbo\n");
        }
    }
    if (log_on) {
        parse_locked();
        // the splash's READY watch (trx_ui.cc) borrows CAP_CTRL; take it back
        if ((r32(CAP_CTRL) & 0xFF) != 1) {
            w32(CAP_CTRL, 1);
        }
    }
    unlock();
}

int trx_ring_count(void)
{
    if (!lock()) {
        return 0;
    }
    int n = tl_n;
    unlock();
    return n;
}

bool trx_ring_frame(int index, TrxFrame *out)
{
    if (!lock()) {
        return false;
    }
    bool ok = index >= 0 && index < tl_n;
    if (ok) {
        *out = ent(index);
    }
    unlock();
    return ok;
}

int trx_ring_target(int frames_back)
{
    if (!lock()) {
        return -1;
    }
    int i = tl_n - 1 - frames_back;
    if (i >= tl_n) {
        i = tl_n - 1;
    }
    uint32_t margin = scrubbing ? SCRUB_MARGIN : 0;
    while (i >= 0 && !(ent(i).block && reachable(ent(i), margin))) {
        i--;
    }
    unlock();
    return i;
}

int trx_ring_find(uint32_t seq)
{
    if (!lock()) {
        return -1;
    }
    int idx = -1;
    for (int i = tl_n - 1; i >= 0; i--) {
        if (ent(i).seq == seq) {
            idx = i;
            break;
        }
    }
    unlock();
    return idx;
}

int trx_ring_restore(int index, TrxRestoreStats *stats)
{
    TrxRestoreStats dummy;
    if (!stats) {
        stats = &dummy;
    }
    if (!lock()) {
        return E_ABSENT;
    }
    if (reu_on()) {
        unlock();
        return E_REU;
    }
    int err = restore_locked(index, stats);
    unlock();
    return err;
}

int trx_ring_scrub_show(int index, TrxRestoreStats *stats)
{
    TrxRestoreStats dummy;
    if (!stats) {
        stats = &dummy;
    }
    if (!lock()) {
        return E_ABSENT;
    }
    if (reu_on()) {
        unlock();
        return E_REU;
    }
    int err = scrub_show_locked(index, stats);
    unlock();
    return err;
}

bool trx_ring_scrubbing(void)
{
    return scrubbing;
}

void trx_ring_set_pending(int index)
{
    pending = index;
}

bool trx_ring_pending(void)
{
    return pending >= 0 || scrubbing;
}

static int scrub_final_locked(int idx, TrxRestoreStats *st);

void trx_ring_unfreeze(void)
{
    int idx = pending;
    pending = -1;
    if (scrubbing && lock()) {
        // a scrub has the machine: the final rewind starts from its state;
        // without a choice (RUN/STOP, any other unfreeze) the newest frame
        TrxRestoreStats st;
        if (idx < 0) {
            idx = tl_n - 1;
            while (idx >= 0 && !(ent(idx).block && reachable(ent(idx)))) {
                idx--;
            }
        }
        int err = scrub_final_locked(idx, &st);
        unlock();
        if (err) {
            printf("[RING] rewind from the scrub failed: %s\n", trx_ring_error(err));
        }
        return;
    }
    TrxRestoreStats st;
    int err = trx_ring_restore(idx, &st);
    if (err) {
        // the machine is released either way (put_back or the early exit)
        printf("[RING] rewind from the menu failed: %s\n", trx_ring_error(err));
    }
}

// ---- the instruction trace (T21 §4.11) ---------------------------------------------
// Records are TRX64's DeltaEntry: three little-endian 64-bit words per instruction in
// stream 3 (the area at TR_AREA, TRACE_CAP records, trx_engine.v). What the app sees
// is the history of the timeline the machine is on (epochs above).
// Opcode lengths (b1/b2 hold whatever the bus returned; the bytes an opcode does not
// have read 0, as in TRX64)
static const uint8_t op_len[256] = {
    1, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    3, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    1, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    1, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,
    2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
};

static inline uint32_t tr_age(uint32_t p, uint32_t cnt) { return (cnt + TRACE_WRAP - p) % TRACE_WRAP; }
static inline uint32_t tr_add(uint32_t p, uint32_t d) { return (p + d) % TRACE_WRAP; }

// n records from physical index `from` into dst (24 bytes each, n <= TRACE_CHUNK); the
// area's end is not 16-byte aligned, so a run is copied on its own
static void trace_fetch(uint32_t from, uint32_t n, uint8_t *dst)
{
    while (n) {
        uint32_t slot = from % TRACE_CAP;
        uint32_t run = TRACE_CAP - slot;
        if (run > n) {
            run = n;
        }
        uint32_t off = slot * 24;
        uint32_t a0 = off & ~15u;
        uint32_t pre = off - a0;
        uint32_t len = (pre + run * 24 + 15) & ~15u;
        eng_copy(TR_AREA + a0, bounce, len);
        memcpy(dst, bounce + pre, run * 24);
        dst += run * 24;
        from = tr_add(from, run);
        n -= run;
    }
}

static inline uint64_t tr_tc(const Epoch &e, uint32_t c24)
{
    int32_t d = (int32_t)(((c24 - e.hw_ref) & 0xFFFFFF) << 8) >> 8;
    return (uint64_t)(e.tc_ref + d);
}

static void tr_decode(const uint8_t *p, const Epoch &e, TrxTraceRec *o)
{
    uint64_t w0 = rec64(p), w1 = rec64(p + 8), w2 = rec64(p + 16);
    o->cycle = tr_tc(e, uint32_t(w0) & 0xFFFFFF);
    o->write_start = uint32_t(w1);
    o->pc = uint16_t(w1 >> 32);
    o->write_count = uint16_t(w1 >> 48);
    o->a = uint8_t(w2);
    o->x = uint8_t(w2 >> 8);
    o->y = uint8_t(w2 >> 16);
    o->sp = uint8_t(w2 >> 24);
    o->p = uint8_t(w2 >> 32);
    o->opcode = uint8_t(w2 >> 40);
    uint8_t len = op_len[o->opcode];
    o->b1 = len >= 2 ? uint8_t(w2 >> 48) : 0;
    o->b2 = len >= 3 ? uint8_t(w2 >> 56) : 0;
}

// The first physical record of a closed epoch that is not history any more: the
// first one after the cut (the next epoch's tc_start; the instruction the cut stands
// on is kept: its fetch came before the snap, the next epoch has no record of it)
static void ep_keep(int i, uint32_t cnt)
{
    Epoch &E = ep[i];
    if (E.keep_known) {
        return;
    }
    E.keep_known = true;
    E.gap = false;
    uint32_t floor_age = TRACE_CAP - TRACE_SAFETY;
    uint32_t lo = E.phys0;
    uint32_t a0 = tr_age(lo, cnt);
    if (a0 > floor_age) {
        a0 = floor_age;
        lo = tr_add(cnt, TRACE_WRAP - floor_age);
    }
    uint32_t a1 = tr_age(ep[i + 1].phys0, cnt);
    uint32_t m = a0 > a1 ? a0 - a1 : 0;
    if (!E.anch || !m) {
        E.keep_end = lo;
        return;
    }
    uint64_t cut = ep[i + 1].tc_start;
    uint8_t tmp[24 + 16];
    uint32_t l = 0, h = m;      // the first k with tc(rec[lo + k]) > cut
    while (l < h) {
        uint32_t mid = (l + h) / 2;
        trace_fetch(tr_add(lo, mid), 1, tmp);
        uint64_t tc = tr_tc(E, uint32_t(rec64(tmp)) & 0xFFFFFF);
        if (tc > cut) {
            h = mid;
        } else {
            l = mid + 1;
        }
    }
    E.keep_end = tr_add(lo, l);
    if (l == 0) {
        E.gap = true;
    } else {
        trace_fetch(tr_add(lo, l - 1), 1, tmp);
        E.gap = tr_tc(E, uint32_t(rec64(tmp)) & 0xFFFFFF) + 8 < cut;
    }
}

struct TraceSeg {
    int e;
    uint32_t start, n;
};

// the history as physical runs, oldest first; total = records in all of them
static int trace_segs(TraceSeg *sg, uint32_t cnt, uint32_t *total, bool *gap)
{
    uint32_t floor_age = TRACE_CAP - TRACE_SAFETY;
    int ns = 0;
    *total = 0;
    *gap = false;
    for (int i = 0; i < ep_n; i++) {
        bool last = i == ep_n - 1;
        if (!last) {
            ep_keep(i, cnt);
        }
        uint32_t end = last ? cnt : ep[i].keep_end;
        uint32_t start = ep[i].phys0;
        uint32_t a0 = tr_age(start, cnt);
        if (a0 > floor_age) {
            a0 = floor_age;
            start = tr_add(cnt, TRACE_WRAP - floor_age);
        }
        uint32_t a1 = tr_age(end, cnt);
        uint32_t n = a0 > a1 ? a0 - a1 : 0;
        if (!n || !ep[i].anch) {
            continue;
        }
        if (!last && ep[i].gap) {
            *gap = true;
        }
        sg[ns].e = i;
        sg[ns].start = start;
        sg[ns].n = n;
        *total += n;
        ns++;
    }
    return ns;
}

static bool trace_ready(void)
{
    return present && (ioRead8(TRX_CAPS2) & TRX_CAPS2_TRACE) && tbuf;
}

bool trx_ring_trace_present(void)
{
    return trace_ready();
}

int trx_ring_trace_ctl(int on)
{
    if (!trace_ready() || !lock()) {
        return -1;
    }
    if (on >= 0) {
        trace_on = on != 0;
        if (log_on) {
            w32(LOG_CTRL, logctl(false));
        }
    }
    bool r = trace_on;
    unlock();
    return r;
}

bool trx_ring_trace_status(TrxTraceStatus *st)
{
    memset(st, 0, sizeof(*st));
    if (!trace_ready() || !lock()) {
        return false;
    }
    parse_locked();
    st->present = true;
    st->on = trace_on && log_on;
    st->capacity = TRACE_CAP - TRACE_SAFETY;
    st->lost = r32(DROPPED) & 0xFFFF;
    TraceSeg sg[MAX_EP];
    uint32_t cnt = trace_cnt(), total;
    bool gap;
    int ns = trace_segs(sg, cnt, &total, &gap);
    st->records = total;
    st->gap = gap;
    if (total) {
        uint8_t tmp[24 + 16];
        trace_fetch(sg[0].start, 1, tmp);
        st->oldest_cycle = tr_tc(ep[sg[0].e], uint32_t(rec64(tmp)) & 0xFFFFFF);
        trace_fetch(tr_add(sg[ns - 1].start, sg[ns - 1].n - 1), 1, tmp);
        st->newest_cycle = tr_tc(ep[sg[ns - 1].e], uint32_t(rec64(tmp)) & 0xFFFFFF);
    }
    unlock();
    return true;
}

// records first.. of the history (0 = oldest), n of them; returns how many
int trx_ring_trace_read(uint32_t first, uint32_t n, TrxTraceRec *out)
{
    if (!trace_ready() || !n || !lock()) {
        return 0;
    }
    parse_locked();
    TraceSeg sg[MAX_EP];
    uint32_t cnt = trace_cnt(), total;
    bool gap;
    int ns = trace_segs(sg, cnt, &total, &gap);
    uint32_t got = 0, base = 0;
    for (int i = 0; i < ns && got < n; i++) {
        if (first + got >= base + sg[i].n) {
            base += sg[i].n;
            continue;
        }
        uint32_t k = first + got - base;
        while (k < sg[i].n && got < n) {
            uint32_t m = sg[i].n - k;
            if (m > TRACE_CHUNK) {
                m = TRACE_CHUNK;
            }
            if (m > n - got) {
                m = n - got;
            }
            trace_fetch(tr_add(sg[i].start, k), m, tbuf);
            for (uint32_t j = 0; j < m; j++) {
                tr_decode(tbuf + 24 * j, ep[sg[i].e], &out[got + j]);
            }
            got += m;
            k += m;
        }
        base += sg[i].n;
    }
    unlock();
    return (int)got;
}

// The debug block's log action (trx_dbg.v, T55): the stream 1 records of space 8 that came since the last call, as
// the engine wrote them ([15:0] address, [23:16] data, [27:24] {0, IB, write, unit}, [31:28] 8, [47:32] IPC,
// [62:48] CYCLES low 15). max = 0 only moves the reader to the present (a log observer was armed: what was logged
// before is not wanted). *lost counts records the area overwrote before they were read. The records of the
// stream are mostly colour RAM and cartridge RAM writes, so the reader walks them all; a call costs one register
// read when nothing came.
int trx_ring_log_read(uint64_t *out, int max, uint32_t *lost)
{
    *lost = 0;
    if (!lock()) {
        return 0;
    }
    uint32_t cnt = r32(LOG_CNT(1)) & M28;
    if (!log_have || !max || ((cnt - log_rd) & M28) > (M28 >> 1)) {
        log_rd = cnt; // first look, a sync, or the counters restarted behind the reader
        log_have = true;
        unlock();
        return 0;
    }
    uint32_t cap = area_size[1] / rec_size[1] - SAFETY;
    uint32_t avail = (cnt - log_rd) & M28;
    if (avail > cap) {
        *lost = avail - cap;
        log_rd = (cnt - cap) & M28;
        avail = cap;
    }
    int got = 0;
    while (avail && got < max) {
        uint32_t n = avail > TRACE_CHUNK ? TRACE_CHUNK : avail;
        const uint8_t *p = fetch(1, log_rd, n);
        uint32_t used = 0;
        for (; used < n && got < max; used++) {
            uint64_t r = rec64(p + 8 * used);
            if (((r >> 28) & 15) == 8 && !(r >> 63)) {
                out[got++] = r;
            }
        }
        log_rd = (log_rd + used) & M28;
        avail -= used;
    }
    unlock();
    return got;
}

static inline bool s0_reach(uint32_t idx, uint32_t cnt0)
{
    return ((cnt0 - idx) & M28) <= area_size[0] / rec_size[0] - SAFETY;
}

// The last writers of addr in the history, newest first. *exhausted: the scan reached the
// start of the history (or of stream 0's area) with fewer than limit hits.
// An I/O address ($D000-$DFFF) is not in stream 0 (that is the RAM write log): its writers are the store
// instructions of the trace itself, by effective address (trx_ea.h). The record carries no old byte, so the
// hit says so (TRX_HIT_OLD_UNKNOWN), and no new byte for a read-modify-write. *unresolved counts the
// instructions that write through a zero page pointer (or an SH* store over a page crossing): their address is
// not in the record, they are not guessed.
int trx_ring_trace_whowrote(uint16_t addr, int limit, TrxTraceHit *hits, bool *exhausted, uint32_t *unresolved)
{
    *exhausted = true;
    *unresolved = 0;
    if (!trace_ready() || limit < 1 || !lock()) {
        return 0;
    }
    parse_locked();
    TraceSeg sg[MAX_EP];
    uint32_t cnt = trace_cnt(), total;
    bool gap;
    int ns = trace_segs(sg, cnt, &total, &gap);
    int found = 0;
    uint32_t lbase = total;
    if (addr >= 0xD000 && addr <= 0xDFFF) {
        for (int si = ns - 1; si >= 0 && found < limit; si--) {
            lbase -= sg[si].n;
            uint32_t remaining = sg[si].n;
            while (remaining && found < limit) {
                uint32_t m = remaining < TRACE_CHUNK ? remaining : TRACE_CHUNK;
                uint32_t k0 = remaining - m;
                trace_fetch(tr_add(sg[si].start, k0), m, tbuf);
                for (int j = (int)m - 1; j >= 0 && found < limit; j--) {
                    TrxTraceRec r;
                    tr_decode(tbuf + 24 * j, ep[sg[si].e], &r);
                    trx_store_t st;
                    int k = trx_store_ea(r.opcode, r.b1, r.b2, r.a, r.x, r.y, &st);
                    if (k == TRX_EA_UNRESOLVED) {
                        (*unresolved)++;
                        continue;
                    }
                    if (k != TRX_EA_KNOWN || st.ea != addr) {
                        continue;
                    }
                    TrxTraceHit &h = hits[found++];
                    h.cycle = r.cycle;
                    h.addr = addr;
                    h.pc = r.pc;
                    h.old_value = 0;
                    h.new_value = st.new_known ? st.new_value : 0;
                    h.flags = TRX_HIT_OLD_UNKNOWN | (st.new_known ? 0 : TRX_HIT_NEW_UNKNOWN);
                    h.index = lbase + k0 + (uint32_t)j;
                }
                remaining -= m;
            }
        }
        *exhausted = found < limit;
        unlock();
        return found;
    }
    for (int si = ns - 1; si >= 0 && found < limit; si--) {
        lbase -= sg[si].n;
        uint32_t remaining = sg[si].n;
        while (remaining && found < limit) {
            uint32_t m = remaining < TRACE_CHUNK ? remaining : TRACE_CHUNK;
            uint32_t k0 = remaining - m;
            trace_fetch(tr_add(sg[si].start, k0), m, tbuf);
            // the writes of the chunk's records: one range of stream 0 when they are close
            uint32_t ws0 = uint32_t(rec64(tbuf + 8)) & M28;
            uint32_t maxend = 0;
            bool one = true;
            for (uint32_t j = 0; j < m; j++) {
                uint64_t w1 = rec64(tbuf + 24 * j + 8);
                uint32_t d = (uint32_t(w1) - ws0) & M28;
                uint32_t e = d + uint16_t(w1 >> 48);
                if (d > 12000 || e > 12000) {
                    one = false;
                }
                if (e > maxend) {
                    maxend = e;
                }
            }
            const uint8_t *wp = NULL;
            if (one && maxend && s0_reach(ws0, cnt_now[0])) {
                wp = fetch(0, ws0, maxend);
            }
            for (int j = (int)m - 1; j >= 0 && found < limit; j--) {
                uint64_t w1 = rec64(tbuf + 24 * j + 8);
                uint32_t ws = uint32_t(w1) & M28;
                uint32_t wc = uint16_t(w1 >> 48);
                for (int w = (int)wc - 1; w >= 0 && found < limit; w--) {
                    uint32_t idx = (ws + w) & M28;
                    uint32_t v;
                    if (wp) {
                        v = rec32(wp + 4 * (((ws - ws0) & M28) + w));
                    } else {
                        if (!s0_reach(idx, cnt_now[0])) {
                            continue;
                        }
                        v = rec32(fetch(0, idx, 1));
                    }
                    if (uint16_t(v) != addr) {
                        continue;
                    }
                    TrxTraceRec r;
                    tr_decode(tbuf + 24 * j, ep[sg[si].e], &r);
                    TrxTraceHit &h = hits[found++];
                    h.cycle = r.cycle;
                    h.addr = addr;
                    h.pc = r.pc;
                    h.old_value = uint8_t(v >> 16);
                    h.new_value = uint8_t(v >> 24);
                    h.flags = 0;
                    h.index = lbase + k0 + (uint32_t)j;
                }
            }
            remaining -= m;
        }
    }
    *exhausted = found < limit;
    unlock();
    return found;
}

// ---- the poll task ---------------------------------------------------------------
#define POLL_MS        100
#define START_AFTER_MS 6000         // after the splash's READY watch (at most 4 s)

static void ring_task(void *)
{
    vTaskDelay(START_AFTER_MS / portTICK_PERIOD_MS);
    if (lock()) {
        if (reu_on()) {
            // A08: the REU is on at boot: no ring until it is off, then it starts
            reu_gate.blocked = true;
            reu_gate.resume = true;
            printf("[RING] REU enabled: no ring until it is off (A08)\n");
        } else {
            start_locked();
            printf("[RING] log on: streams at %07x/%07x/%07x%s\n", (unsigned)area_base[0], (unsigned)area_base[1],
                   (unsigned)area_base[2], f4_after_blocks ? ", older engine: F4s after block records skipped" : "");
        }
        unlock();
    }
    for (;;) {
        trx_ring_poll();
        vTaskDelay(POLL_MS / portTICK_PERIOD_MS);
    }
}

static void trx_ring_init(void *_obj, void *_param)
{
    if (!(trx_io_caps() & CAPS_ENGINE)) {
        return;
    }
    f4_after_blocks = !(ioRead8(TRX_CAPS2) & TRX_CAPS2_F4);
    apply_hw = (ioRead8(TRX_CAPS2) & TRX_CAPS2_APPLY) != 0;
    bounce = (uint8_t *)alloc_aligned(BOUNCE);
    ram_val = (uint8_t *)malloc(65536);
    tbuf = (uint8_t *)malloc(TRACE_CHUNK * 24);
    ram_tch = (uint8_t *)alloc_aligned(65536);
    cart_val = (uint8_t *)malloc(65536);
    cart_tch = (uint8_t *)alloc_aligned(65536);
    tl = (TrxFrame *)malloc(sizeof(TrxFrame) * TL_CAP);
    mutex = xSemaphoreCreateMutex();
    if (!bounce || !tl || !mutex || !ram_val || !ram_tch || !cart_val || !cart_tch) {
        printf("[RING] out of memory, no frame ring\n");
        return;
    }
    present = true;
    xTaskCreate(ring_task, "TRX Ring", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);
}

InitFunction trx_ring_init_func("TRX Ring", trx_ring_init, NULL, NULL, 91);
