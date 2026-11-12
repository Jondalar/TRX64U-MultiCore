/*
 * hw_trx.c — hw.h on the TRX64 Ultimate bitstream (RISC-V, U64-II/C64U).
 *
 * The IO block is byte-wide; its 32-bit registers commit on byte 3 and latch
 * on a read of byte 0 (trx_sys.v). Page n is at regs + n * 0x1000.
 */
#include <string.h>

#include "hw.h"
#include "u2p_app.h"

/* The firmware's C64 windows on this board (software/system/iomap.h, u64.h
 * with U2P_IO_BASE 0x10100000) */
#define C64_MEMORY_BASE  0x10050000u /* DMA window: RAM, or a bus cycle with C64_STOP */
#define C64_DMA_MEMONLY  0x10180003u /* 1: the window is RAM only, $D000-$DFFF included */
#define U64_ROMS_BASE    0x10188000u /* BASIC +0x0000, KERNAL +0x2000 */
#define U64_CHARROM_BASE 0x1018C000u

#define IO8(a) (*(volatile uint8_t *)(uintptr_t)(a))

static const u2p_trx_api_t *s_trx;
static volatile uint8_t *s_regs;

static void wr32(uint32_t off, uint32_t v)
{
    s_regs[off + 0] = (uint8_t)v;
    s_regs[off + 1] = (uint8_t)(v >> 8);
    s_regs[off + 2] = (uint8_t)(v >> 16);
    s_regs[off + 3] = (uint8_t)(v >> 24);
}

static uint32_t rd32(uint32_t off)
{
    uint32_t v = s_regs[off + 0];
    v |= (uint32_t)s_regs[off + 1] << 8;
    v |= (uint32_t)s_regs[off + 2] << 16;
    v |= (uint32_t)s_regs[off + 3] << 24;
    return v;
}

#define P0(o) (0x0000u + (o))
#define P1(o) (0x1000u + (o))
#define P3(o) (0x3000u + (o))

int hw_init(const void *trx_api)
{
    s_trx = (const u2p_trx_api_t *)trx_api;
    if (!s_trx || !s_trx->regs || !(s_trx->caps & 0x08)) {
        return 0;
    }
    s_regs = s_trx->regs;
    return 1;
}

uint32_t hw_caps(void) { return s_trx ? s_trx->caps : 0; }

uint32_t hw_status(void) { return rd32(P3(0x008)); }

void hw_cmd(uint8_t cmd, uint16_t arg)
{
    if ((cmd == CMD_STEP_INSTR) || (cmd == CMD_STEP_CYCLES) || (cmd == CMD_DRV_STEP)) {
        wr32(P3(0x004), arg);
    }
    wr32(P3(0x000), cmd);
}

void hw_owner(int on) { wr32(P0(0x118), on ? 1 : 0); }

uint64_t hw_cycles(void)
{
    uint32_t lo = rd32(P0(0x120)); /* latches bits 47:32 */
    uint32_t hi = rd32(P0(0x124)) & 0xFFFF;
    return ((uint64_t)hi << 32) | lo;
}

uint32_t hw_instrs(void) { return rd32(P0(0x128)); }

void hw_cyc_cmp(uint32_t at) { wr32(P0(0x130), at); }

void hw_cyc_cmp_off(void) { wr32(P0(0x134), 0); }

void hw_stop_record(hw_stop_t *r)
{
    uint32_t s0 = rd32(P3(0x010));
    uint32_t s1 = rd32(P3(0x014));
    r->reason = s0 & 15;
    r->unit = (s0 >> 4) & 1;
    r->flags = (s0 >> 5) & 7;
    r->data = (uint8_t)(s0 >> 8);
    r->pos = (uint16_t)(s0 >> 16);
    r->ipc = (uint16_t)s1;
    r->addr = (uint16_t)(s1 >> 16);
}

void hw_sb_block(int chip, int word, uint32_t *dst, int n)
{
    wr32(P1(0x000), ((uint32_t)chip << 7) | (uint32_t)word);
    for (int i = 0; i < n; i++) {
        dst[i] = rd32(P1(0x004)); /* each read moves SB_ADDR on */
    }
}

uint32_t hw_sb(int chip, int word)
{
    uint32_t v;
    hw_sb_block(chip, word, &v, 1);
    return v;
}

/* CAPS2 (trx_io.v, page 0 byte 0x005): b2 the CPU write path (b1 is T15's BUILD) */
int hw_has_cpu_write(void) { return (s_regs[0x005] & 0x04) != 0; }

void hw_cpu(hw_cpu_t *c)
{
    uint32_t w[6];
    hw_sb_block(1, 0, w, 6);
    c->a = (uint8_t)w[0];
    c->x = (uint8_t)(w[0] >> 8);
    c->y = (uint8_t)(w[0] >> 16);
    c->s = (uint8_t)(w[0] >> 24);
    c->pc = (uint16_t)w[1];
    c->p = (uint8_t)(w[1] >> 16);
    c->sync = (w[1] >> 24) & 1;
    c->ddr = (uint8_t)w[2];
    c->latch = (uint8_t)(w[2] >> 8);
    c->port = (uint8_t)(w[2] >> 16);
    c->ipc = (uint16_t)w[3];
    c->op = (uint8_t)(w[3] >> 16);
    c->at_ib = (w[3] >> 24) & 1;
    c->settled = 0;
    if (c->at_ib && hw_has_cpu_write() && (hw_status() & ST_SHUT)) {
        /* chip 1 w4/w5: what the pending update leaves */
        c->a = (uint8_t)w[4];
        c->x = (uint8_t)(w[4] >> 8);
        c->y = (uint8_t)(w[4] >> 16);
        c->s = (uint8_t)(w[4] >> 24);
        c->p = (uint8_t)w[5];
        c->settled = 1;
    }
}

/* The CPU's flip-flops as chip 1 w8-w14 carry them (cpu6510.v sb_cur):
 * 7:0 A, 15:8 X, 23:16 Y, 31:24 S, 39:32 P, 55:40 PC, 87:80 IR (6502N
 * proc_registers); 102 + 4:0 state, + 12 copy_d2p, + 15 flags_update, + 16
 * trigger_upd, + 18 intg (proc_control); 137:130 din_r (the byte the last
 * read took, cpu6510.v) */
#define B_A      0
#define B_X      8
#define B_Y      16
#define B_S      24
#define B_P      32
#define B_PC     40
#define B_IR     80
#define B_CTRL   102
#define B_DIN    130

static uint32_t get_bits(const uint32_t *w, int lo, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        int b = lo + i;
        v |= ((w[b >> 5] >> (b & 31)) & 1u) << i;
    }
    return v;
}

static void put_bits(uint32_t *w, int lo, int n, uint32_t v)
{
    for (int i = 0; i < n; i++) {
        int b = lo + i;
        w[b >> 5] = (w[b >> 5] & ~(1u << (b & 31))) | (((v >> i) & 1u) << (b & 31));
    }
}

void hw_cpu_fetch(int *in_fetch, int *intg, uint16_t *pc, uint8_t *din)
{
    uint32_t w[7];
    hw_sb_block(1, 8, w, 7);
    *in_fetch = get_bits(w, B_CTRL, 5) == 0; /* t_state'pos(fetch) */
    *intg = (int)get_bits(w, B_CTRL + 18, 1);
    *pc = (uint16_t)get_bits(w, B_PC, 16);
    *din = (uint8_t)get_bits(w, B_DIN, 8);
}

/* At an IB the update to come takes the opcode (IR, the data latch, PC + 1)
 * and writes back the previous instruction (reg_update, copy_d2p,
 * flags_update, and CLC..SED through IR). The staged words carry the
 * registers as they are to be after it, and IR = NOP with those three off,
 * so the update leaves them alone. */
int hw_cpu_write(const hw_regs_t *r, int pc, uint8_t op)
{
    uint32_t w[7];
    hw_sb_block(1, 8, w, 7);
    put_bits(w, B_A, 8, r->a);
    put_bits(w, B_X, 8, r->x);
    put_bits(w, B_Y, 8, r->y);
    put_bits(w, B_S, 8, r->s);
    put_bits(w, B_P, 8, r->p);
    put_bits(w, B_IR, 8, 0xEA);
    put_bits(w, B_CTRL + 12, 1, 0);
    put_bits(w, B_CTRL + 15, 1, 0);
    put_bits(w, B_CTRL + 16, 1, 0);
    if (pc >= 0) {
        put_bits(w, B_PC, 16, (uint32_t)pc); /* the fetch's address; the update adds 1 */
        put_bits(w, B_DIN, 8, op);           /* what the fetch read */
    }
    wr32(P1(0x008), 0); /* SB_STATUS: clear "refused" */
    wr32(P1(0x000), (1u << 7) | 8u);
    for (int k = 0; k < 7; k++) {
        wr32(P1(0x004), w[k]);
    }
    if (rd32(P1(0x008)) & 2) {
        return -1;
    }
    wr32(P1(0x00C), 0x02); /* SB_LOAD: the CPU, at the next opening edge */
    if (pc >= 0) {
        hw_ipc_set((uint16_t)pc, op, 1);
    }
    return 0;
}

int hw_cpu_write_pending(void) { return (rd32(P1(0x00C)) & 0x02) != 0; }

void hw_ipc_set(uint16_t ipc, uint8_t op, int at_ib)
{
    wr32(P3(0x018), (at_ib ? (1u << 24) : 0) | ((uint32_t)op << 16) | ipc);
}

void hw_bmp_word(int idx, uint32_t bits)
{
    wr32(P3(0x040), (uint32_t)idx);
    wr32(P3(0x044), bits);
}

void hw_bmp_enable(int on) { wr32(P3(0x048), on ? 1 : 0); }

void hw_unit(int n, uint32_t ctrl, uint16_t lo, uint16_t hi, uint32_t data, uint32_t reg, uint16_t pass)
{
    uint32_t b = P3(0x080 + 0x20 * (uint32_t)n);
    wr32(b + 0x00, 0);
    wr32(b + 0x04, ((uint32_t)hi << 16) | lo);
    wr32(b + 0x08, data);
    wr32(b + 0x0C, reg);
    wr32(b + 0x10, pass);
    wr32(b + 0x14, 0); /* clears HITS */
    wr32(b + 0x00, ctrl);
}

void hw_unit_off(int n) { wr32(P3(0x080 + 0x20 * (uint32_t)n), 0); }

uint32_t hw_hits(void) { return rd32(P3(0x0B4)); }

uint32_t hw_drv_stat(void) { return rd32(P3(0x0C4)); }

#define DRIVE_IMAGE 0x0EE0000u /* drive_unit.vhd g_ram_base of drive A */

int hw_drive_peek(uint16_t a)
{
    if (a < 0x1800) {
        return IO8(DRIVE_IMAGE + (a & 0x07FFu)); /* 2 K RAM, mirrored */
    }
    if (a < 0x2000) {
        return -1;
    }
    return IO8(DRIVE_IMAGE + a);
}

uint32_t hw_drv_cycles(void) { return rd32(P3(0x0C8)); }

void hw_drv_cmp(uint16_t a, int on) { wr32(P3(0x0C0), (on ? (1u << 16) : 0) | a); }

/* RAM only, no bus cycle: C64_DMA_MEMONLY is set for the access and put back
 * as the firmware had it (it may run its own DMA while trxmon serves). */
static uint8_t memonly_on(void)
{
    uint8_t old = IO8(C64_DMA_MEMONLY);
    if (!old) {
        IO8(C64_DMA_MEMONLY) = 1;
    }
    return old;
}

static void memonly_back(uint8_t old)
{
    if (!old) {
        IO8(C64_DMA_MEMONLY) = old;
    }
}

void hw_ram_read(uint16_t a, uint8_t *d, int n)
{
    uint8_t old = memonly_on();
    for (int i = 0; i < n; i++) {
        d[i] = IO8(C64_MEMORY_BASE + (uint16_t)(a + i));
    }
    memonly_back(old);
}

void hw_ram_write(uint16_t a, const uint8_t *s, int n)
{
    uint8_t old = memonly_on();
    for (int i = 0; i < n; i++) {
        IO8(C64_MEMORY_BASE + (uint16_t)(a + i)) = s[i];
    }
    memonly_back(old);
}

uint8_t hw_rom(uint16_t off)
{
    if (off < 0x1000) {
        return IO8(U64_CHARROM_BASE + off);
    }
    if ((off >= 0x2000) && (off < 0x4000)) {
        return IO8(U64_ROMS_BASE + off); /* KERNAL at +0x2000 */
    }
    if ((off >= 0x4000) && (off < 0x6000)) {
        return IO8(U64_ROMS_BASE + (off - 0x4000u)); /* BASIC at +0x0000 */
    }
    return 0xFF;
}

/* The lens unit (freeze_resp.v, trx_sys.v page 5; CAPS2 b4): LK_REQ 0x020 {REAL, class,
 * address}, LK_RES 0x024 {busy, refused, real, ext, hit, done, data} */
#define P5(o) (0x5000u + (o))

int hw_has_lk(void) { return (s_regs[0x005] & 0x10) != 0; }

uint32_t hw_lk(uint16_t a, int cls, int real)
{
    wr32(P5(0x020), (uint32_t)a | ((uint32_t)(cls & 3) << 16) | (real ? (1u << 18) : 0));
    for (int n = 0; n < 100000; n++) {
        uint32_t r = rd32(P5(0x024));
        if (!(r & 0x80000000u)) {
            return r;
        }
    }
    return 0;
}

/* A bus cycle: the window's D page reaches the C64 side only while the machine is held, by the
 * firmware's C64_STOP or by a debugger stop (ultimate_sys, dbg_stop), and only a machine whose
 * gate is shut may take it. */
int hw_bus_held(void)
{
    uint32_t st = hw_status();
    return (st & ST_SHUT) && (st & (ST_FW_STOP | ST_DBG_STOP));
}

uint8_t hw_bus_read(uint16_t a)
{
    if (!hw_bus_held()) {
        return 0xFF;
    }
    uint8_t old = IO8(C64_DMA_MEMONLY);
    IO8(C64_DMA_MEMONLY) = 0;
    uint8_t v = IO8(C64_MEMORY_BASE + a);
    IO8(C64_DMA_MEMONLY) = old;
    return v;
}

int hw_bus_read_held(uint16_t a) { return hw_bus_held() ? hw_bus_read(a) : -1; }

/* SH_STATUS (page 5 0x004) b0 is a session; CAPS2 b3 says the bitstream has one. The window then
 * answers from the shadows, so the read is no bus cycle. */
int hw_session_read(uint16_t a)
{
    if (!(s_regs[0x005] & 0x08) || !(rd32(P5(0x004)) & 1) || !hw_bus_held()) {
        return -1;
    }
    return hw_bus_read(a);
}

void hw_bus_write(uint16_t a, uint8_t v)
{
    if (!hw_bus_held()) {
        return;
    }
    uint8_t old = IO8(C64_DMA_MEMONLY);
    IO8(C64_DMA_MEMONLY) = 0;
    IO8(C64_MEMORY_BASE + a) = v;
    IO8(C64_DMA_MEMONLY) = old;
}

int hw_acquire(void) { return s_trx->acquire(0); }

int hw_detach(void (*fn)(void *), void *arg)
{
    if (!U2P_HAS(s_trx, detach)) {
        return U2P_ERR_UNSUPPORTED;
    }
    return s_trx->detach(fn, arg);
}

int hw_stop_requested(void)
{
    return U2P_HAS(s_trx, stop_requested) && s_trx->stop_requested();
}

int hw_ui_request(void (*fn)(void *), void *arg)
{
    if (!U2P_HAS(s_trx, ui_request)) {
        return U2P_ERR_UNSUPPORTED;
    }
    return s_trx->ui_request(fn, arg);
}

void hw_release(void) { s_trx->release(); }

void hw_announce(uint16_t port)
{
    if (U2P_HAS(s_trx, announce)) {
        s_trx->announce(port);
    }
}

/* ---- the frame ring (trx->ring) -------------------------------------------- */

static const u2p_ring_api_t *ring(void)
{
    if (!s_trx || !U2P_HAS(s_trx, ring)) {
        return 0;
    }
    return s_trx->ring;
}

int hw_ring_present(void) { return ring() != 0; }

int hw_ring_status(hw_ring_status_t *st)
{
    memset(st, 0, sizeof(*st));
    if (!ring()) {
        return -1;
    }
    u2p_ring_status_t r;
    int e = ring()->status(&r);
    st->present = r.present;
    st->on = r.on;
    st->landed = r.landed;
    st->reu_off = r.reu_off;
    st->frames = r.frames;
    st->targets = r.targets;
    st->head_seq = r.head_seq;
    st->oldest_seq = r.oldest_seq;
    st->landed_seq = r.landed_seq;
    return e;
}

int hw_ring_frame(int index, hw_ring_frame_t *f)
{
    u2p_ring_frame_t r;
    if (!ring() || ring()->frame(index, &r) < 0) {
        return -1;
    }
    f->seq = r.seq;
    f->c24 = r.c24;
    f->cycles = r.cycles;
    f->line = r.line;
    f->x = r.x;
    f->flags = r.flags;
    f->target = r.target;
    return 0;
}

int hw_ring_find(uint32_t seq) { return ring() ? ring()->find(seq) : -1; }

const char *hw_ring_land(int index)
{
    if (!ring()) {
        return "no frame ring (the bitstream has no engine, or the firmware no ring table)";
    }
    int e = ring()->land(index);
    return e ? ring()->error(e) : 0;
}

void hw_ring_commit(void)
{
    if (ring()) {
        ring()->commit();
    }
}

/* ---- the video mode and the instruction trace ------------------------------ */

void hw_video(hw_video_t *v)
{
    u2p_video_t u;
    if (s_trx && U2P_HAS(s_trx, video) && (s_trx->video(&u) == 0)) {
        v->lines = u.lines;
        v->cycles_per_line = u.cycles_per_line;
        v->frame_cycles = u.frame_cycles;
        v->cpu_hz = u.cpu_hz;
        v->fps_x100 = u.fps_x100;
        v->hz60 = u.hz60;
        return;
    }
    v->lines = 312;
    v->cycles_per_line = 63;
    v->frame_cycles = 19656;
    v->cpu_hz = 985248;
    v->fps_x100 = 5012;
    v->hz60 = 0;
}

static const u2p_ring_api_t *tring(void)
{
    const u2p_ring_api_t *r = ring();
    return (r && U2P_HAS(r, trace_whowrote)) ? r : 0;
}

int hw_trace_present(void)
{
    u2p_trace_status_t t;
    return tring() && (tring()->trace_status(&t) == 0) && t.present;
}

int hw_trace_status(hw_trace_status_t *st)
{
    u2p_trace_status_t t;
    memset(st, 0, sizeof(*st));
    if (!tring() || (tring()->trace_status(&t) < 0)) {
        return -1;
    }
    st->present = t.present;
    st->on = t.on;
    st->gap = t.gap;
    st->records = t.records;
    st->capacity = t.capacity;
    st->lost = t.lost;
    st->oldest_cycle = t.oldest_cycle;
    st->newest_cycle = t.newest_cycle;
    return 0;
}

int hw_trace_ctl(int on) { return tring() ? tring()->trace_ctl(on) : -1; }

int hw_trace_read(uint32_t first, uint32_t n, hw_trace_rec_t *out)
{
    return tring() ? tring()->trace_read(first, n, (u2p_trace_rec_t *)out) : 0;
}

int hw_trace_whowrote(uint16_t addr, int limit, hw_trace_hit_t *hits, int *exhausted)
{
    *exhausted = 1;
    return tring() ? tring()->trace_whowrote(addr, limit, (u2p_trace_hit_t *)hits, exhausted) : 0;
}

int hw_log_present(void)
{
    const u2p_ring_api_t *r = ring();
    return r && U2P_HAS(r, log_read);
}

int hw_log_read(hw_logrec_t *out, int max, uint32_t *lost)
{
    const u2p_ring_api_t *r = ring();
    uint64_t raw[16];
    *lost = 0;
    if (!r || !U2P_HAS(r, log_read)) {
        return 0;
    }
    if (max <= 0) {
        return r->log_read(raw, 0, lost);
    }
    if (max > 16) {
        max = 16;
    }
    int n = r->log_read(raw, max, lost);
    for (int i = 0; i < n; i++) {
        uint64_t v = raw[i];
        out[i].addr = (uint16_t)v;
        out[i].data = (uint8_t)(v >> 16);
        out[i].unit = (uint8_t)((v >> 24) & 1);
        out[i].write = (uint8_t)((v >> 25) & 1);
        out[i].ib = (uint8_t)((v >> 26) & 1);
        out[i].ipc = (uint16_t)(v >> 32);
        out[i].cyc15 = (uint16_t)((v >> 48) & 0x7FFF);
    }
    return n;
}
