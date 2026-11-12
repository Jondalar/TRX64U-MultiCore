/*
 * ring.c — the frame ring as the monitor's timeline (see ring.h).
 *
 * Cycles: the firmware gives each entry its ring cycle count (phi2 cycles
 * since the ring started, kept on the debug block's CYCLES across cuts) and
 * c24, CYCLES after its snap edge. The machine's position on the ring is the
 * newest entry's count plus the cycles since its snap; after a land, the
 * land's entry plus the cycles since the land. What trxmon reports
 * (mach_cycles) is that position plus a constant, so a land sets time back
 * as TRX64's restore does.
 *
 * Reverse step: an entry is a frame start, so going back n instructions is a
 * replay. With the instruction trace (T21 §4.11) the target is the record n
 * back: land on the frame at or before its cycle and STEP_CYCLES to it, then
 * check the stop against the record. Without enough records: land on the
 * frame that holds the stop, count the instructions to the stop (STEP_CYCLES
 * to its cycle, the debug block's INSTRS), land again and step forward to
 * the instruction n before it; over a frame start the frame before is
 * counted the same way. Exact as far as a land is exact
 * (CPU, CIAs and memory; the VIC's registers replayed, its internals, the
 * SID, the REU and the drive are not put back: T20 §16.6).
 */
#include "ring.h"
#include "mon.h"
#include "spans.h"
#include "trace.h"

/* the System Mode that runs now (T40): read when used, the mode can change while trxmon runs */
static uint32_t cyc_frame(void)
{
    hw_video_t v;
    hw_video(&v);
    return v.frame_cycles;
}

static uint32_t cpu_hz(void)
{
    hw_video_t v;
    hw_video(&v);
    return v.cpu_hz;
}

static const char *const KEYS =
    " F9 \xe2\x97\x80|   F10 \xe2\x97\x80\xe2\x97\x80   F11 \xe2\x8f\xb8/\xe2\x96\xb6   F12 |\xe2\x96\xb6        (`rewind` for the full picture)";

enum { MD_LIVE = 0, MD_REPLAY, MD_CUT, MD_BACK, MD_FWD };
static const char *const mode_name[] = { "LIVE", "REPLAY", "CUT", "STEP \xe2\x97\x80", "STEP \xe2\x96\xb6" };
static const char *const mode_glyph[] = { "\xe2\x96\xb6", "\xe2\x97\x80\xe2\x97\x80", "\xe2\x96\xb6",
                                          "\xe2\x97\x80|", "|\xe2\x96\xb6" };

static struct
{
    int landed;
    uint32_t land_seq;
    uint64_t land_hw; /* CYCLES right after the land */
    int mode;
    int last_cut;
    struct
    {
        char name[24];
        uint32_t seq;
    } mk[RING_MAX_MARKS];
    int nmk;
} RG;

int ring_present(void) { return hw_ring_present(); }

const char *ring_off_reason(void)
{
    hw_ring_status_t st;
    return ((hw_ring_status(&st) >= 0) && st.reu_off) ? RING_REU_OFF_TEXT : 0;
}

static int parse_u64(const char *t, uint64_t *out)
{
    uint64_t v = 0;
    int n = 0;
    for (; (t[n] >= '0') && (t[n] <= '9') && (n < 20); n++) {
        v = v * 10 + (uint64_t)(t[n] - '0');
    }
    if (!n || t[n]) {
        return 0;
    }
    *out = v;
    return 1;
}

/* ---- positions ---------------------------------------------------------- */

/* The machine's ring cycle count now; returns the entry it is in, -1 none */
static int virt_now(uint64_t *v)
{
    uint64_t hw = hw_cycles();
    hw_ring_frame_t f;
    if (RG.landed) {
        int i = hw_ring_find(RG.land_seq);
        if ((i < 0) || (hw_ring_frame(i, &f) < 0)) {
            return -1;
        }
        *v = f.cycles + (hw - RG.land_hw);
        return i;
    }
    hw_ring_status_t st;
    if ((hw_ring_status(&st) < 0) || (st.frames < 1) || (hw_ring_frame(st.frames - 1, &f) < 0)) {
        return -1;
    }
    *v = f.cycles + (((uint32_t)hw - f.c24) & 0xFFFFFFu);
    return st.frames - 1;
}

/* reported cycles - ring cycles */
static int64_t k_off(void)
{
    uint64_t v;
    if (virt_now(&v) < 0) {
        return 0;
    }
    return (int64_t)mach_cycles() - (int64_t)v;
}

int64_t ring_cycle_offset(void) { return k_off(); }

static void fill_entry(int index, const hw_ring_frame_t *f, int64_t k, ring_entry_t *e)
{
    e->index = index;
    e->seq = f->seq;
    e->cycles = (uint64_t)((int64_t)f->cycles + k);
    e->frame = (uint32_t)(f->cycles / cyc_frame());
    e->target = f->target;
    sbuf_t b;
    sb_init(&b, e->id, sizeof(e->id));
    sb_put(&b, "cp_");
    sb_dec(&b, e->frame);
    sb_putc(&b, '_');
    sb_dec(&b, e->seq);
    sb_str(&b);
}

int ring_count(void)
{
    hw_ring_status_t st;
    return (hw_ring_status(&st) < 0) ? 0 : st.frames;
}

int ring_entry(int index, ring_entry_t *e)
{
    hw_ring_frame_t f;
    if (hw_ring_frame(index, &f) < 0) {
        return -1;
    }
    fill_entry(index, &f, k_off(), e);
    return 0;
}

int ring_cursor(void)
{
    uint64_t v;
    return virt_now(&v);
}

int ring_find_id(const char *id)
{
    /* cp_<frame>_<seq>: the seq decides */
    int n = m_strlen(id);
    int us = -1;
    for (int i = n - 1; i >= 0; i--) {
        if (id[i] == '_') {
            us = i;
            break;
        }
    }
    uint32_t seq;
    if ((us < 0) || !m_parse_dec(id + us + 1, -1, &seq)) {
        return -1;
    }
    return hw_ring_find(seq);
}

/* the newest entry a land can go to with ring cycles <= v, below `below` */
static int target_at_or_before(uint64_t v, int below)
{
    for (int i = below - 1; i >= 0; i--) {
        hw_ring_frame_t f;
        if ((hw_ring_frame(i, &f) == 0) && f.target && (f.cycles <= v)) {
            return i;
        }
    }
    return -1;
}

/* ---- land and commit ---------------------------------------------------- */

const char *ring_land(int index)
{
    if (!hw_ring_present()) {
        return "no frame ring (the bitstream has no engine, or the firmware no ring table)";
    }
    if (ring_off_reason()) {
        return ring_off_reason();
    }
    if (!mach_own()) {
        return "the debug block is not available";
    }
    if (M.running) {
        mach_pause();
    }
    uint64_t v;
    int have = virt_now(&v) >= 0;
    int64_t k = have ? (int64_t)mach_cycles() - (int64_t)v : 0;
    hw_ring_frame_t f;
    if (hw_ring_frame(index, &f) < 0) {
        return "no such frame";
    }
    if (!f.target) {
        return "the log no longer reaches that frame, or it has no whole chip block";
    }
    const char *e = hw_ring_land(index);
    if (hw_status() & ST_STOP_NEW) {
        mach_read_stop();
    }
    if (e) {
        return e;
    }
    RG.landed = 1;
    RG.land_seq = f.seq;
    RG.land_hw = hw_cycles();
    if (have) {
        M.cyc_off = (int64_t)f.cycles + k - (int64_t)RG.land_hw;
    }
    /* the land edge is a snap: the end of an opcode fetch (an IB unless an
     * interrupt drops it); the debug block's IPC follows */
    int in_fetch, intg;
    uint16_t pc;
    uint8_t din;
    hw_cpu_fetch(&in_fetch, &intg, &pc, &din);
    int ib = in_fetch && !intg;
    hw_ipc_set(pc, din, ib);
    M.stop.reason = "goto";
    M.stop.pc = pc;
    M.stop.at_ib = ib;
    M.stop.cycles = mach_cycles();
    M.stop.instrs = hw_instrs();
    M.stop.bp_num = 0;
    M.stop.obs = -1;
    M.have_stop = 1;
    M.running = 0;
    M.frame++;
    M.disasm_cursor_set = 0;
    mach_snapshot();
    return 0;
}

void ring_before_run(void)
{
    if (!RG.landed) {
        return;
    }
    hw_ring_status_t st;
    int i = hw_ring_find(RG.land_seq);
    RG.last_cut = ((i >= 0) && (hw_ring_status(&st) == 0)) ? st.frames - 1 - i : 0;
    hw_ring_commit();
    RG.landed = 0;
    RG.mode = RG.last_cut ? MD_CUT : MD_LIVE;
}

/* ---- reverse step ------------------------------------------------------- */

static int step_n(uint64_t n, int cycles)
{
    while (n) {
        uint32_t c = (n > 0xFFFF) ? 0xFFFF : (uint32_t)n;
        if (mach_step((int)c, cycles) < 0) {
            return -1;
        }
        n -= c;
    }
    return 0;
}

/* Lands on k and counts the instructions (IB edges) to ring cycle `to`; the
 * machine stands there afterwards. ib0: the land itself is an IB. */
static const char *count_to(int k, uint64_t to, uint32_t *n, int *ib0)
{
    hw_ring_frame_t f;
    if (hw_ring_frame(k, &f) < 0) {
        return "the frame left the ring";
    }
    const char *e = ring_land(k);
    if (e) {
        return e;
    }
    *ib0 = M.stop.at_ib;
    uint32_t i0 = hw_instrs();
    if ((to > f.cycles) && (step_n(to - f.cycles, 1) < 0)) {
        return "the replay did not stop";
    }
    *n = hw_instrs() - i0;
    return 0;
}

const char *ring_rstep(int n, ring_rstep_t *r)
{
    if (!hw_ring_present()) {
        return "no frame ring (the bitstream has no engine, or the firmware no ring table)";
    }
    if (ring_off_reason()) {
        return ring_off_reason();
    }
    if (!mach_own()) {
        return "the debug block is not available";
    }
    if (M.running) {
        mach_pause();
    }
    if (n < 1) {
        n = 1;
    }
    hw_cpu_t p;
    mach_cpu(&p);
    if (!p.at_ib) {
        return "mid-instruction: step to the boundary first (z)";
    }
    uint64_t vp;
    int cur = virt_now(&vp);
    if (cur < 0) {
        return "the ring holds no frame";
    }
    {
        /* the trace: the record n back is the instruction to land on */
        hw_trace_rec_t tr;
        if (trace_rec_back(vp, n, &tr) == 0) {
            int kt = target_at_or_before(tr.cycle, cur + 1);
            if (kt >= 0) {
                hw_ring_frame_t ft;
                hw_ring_frame(kt, &ft);
                const char *e = ring_land(kt);
                if (e) {
                    return e;
                }
                if ((tr.cycle > ft.cycles) && (step_n(tr.cycle - ft.cycles, 1) < 0)) {
                    return "the replay did not stop";
                }
                hw_cpu_t c;
                mach_cpu(&c);
                if (!c.at_ib || (mach_pc(&c) != tr.pc) ||
                    (c.settled && ((c.a != tr.a) || (c.x != tr.x) || (c.y != tr.y) || (c.s != tr.sp) || (c.p != tr.p)))) {
                    return "the replay from the frame start did not come back to the recorded instruction (the frame is "
                           "not exact here: VIC internals, SID, REU and drive are not put back); the machine stands "
                           "where the replay ended";
                }
                r->steps = n;
                r->pc = tr.pc;
                r->a = c.a;
                r->x = c.x;
                r->y = c.y;
                r->s = c.s;
                r->p = c.p;
                r->cycle = mach_cycles();
                r->frame_from = kt + 1;
                M.stop.reason = "reverse";
                M.stop.pc = r->pc;
                M.stop.cycles = r->cycle;
                RG.mode = MD_BACK;
                return 0;
            }
        }
    }
    int k = target_at_or_before(vp, cur + 1);
    if (k < 0) {
        return "the ring holds no frame this stop can be replayed from";
    }
    /* where the stop is in k's frame */
    uint32_t np;
    int ib0;
    const char *e = count_to(k, vp, &np, &ib0);
    if (e) {
        return e;
    }
    hw_cpu_t q;
    mach_cpu(&q);
    if (!q.at_ib || (q.ipc != p.ipc) || (q.a != p.a) || (q.x != p.x) || (q.y != p.y) || (q.s != p.s) ||
        (q.p != p.p)) {
        return "the replay from the frame start did not come back to the same stop (the frame is not exact "
               "here: VIC internals, SID, REU and drive are not put back); the machine stands where the replay ended";
    }
    hw_ring_frame_t fk;
    hw_ring_frame(k, &fk);
    int64_t t = (int64_t)np - n;
    while (t < (ib0 ? 0 : 1)) {
        int j = target_at_or_before(fk.cycles ? fk.cycles - 1 : 0, k);
        if ((j < 0) || (fk.cycles == 0)) {
            /* back to the stop */
            ring_land(k);
            step_n(np, 0);
            return "the ring does not reach that far back";
        }
        uint32_t nj;
        int ibj;
        e = count_to(j, fk.cycles, &nj, &ibj);
        if (e) {
            return e;
        }
        t += (int64_t)nj - (ib0 ? 0 : 1);
        k = j;
        ib0 = ibj;
        hw_ring_frame(k, &fk);
    }
    e = ring_land(k);
    if (e) {
        return e;
    }
    if ((t > 0) && (step_n((uint64_t)t, 0) < 0)) {
        return "the replay did not stop";
    }
    hw_cpu_t c;
    mach_cpu(&c);
    r->steps = n;
    r->pc = mach_pc(&c);
    r->a = c.a;
    r->x = c.x;
    r->y = c.y;
    r->s = c.s;
    r->p = c.p;
    r->cycle = mach_cycles();
    r->frame_from = k + 1;
    M.stop.reason = "reverse";
    M.stop.pc = r->pc;
    M.stop.cycles = r->cycle;
    RG.mode = MD_BACK;
    return 0;
}

/* ---- transport ---------------------------------------------------------- */

static void sb_fix(sbuf_t *b, uint64_t num, uint64_t den, int digits)
{
    uint64_t scale = 1;
    for (int i = 0; i < digits; i++) {
        scale *= 10;
    }
    uint64_t v = (num * scale + den / 2) / den;
    sb_dec(b, v / scale);
    if (digits) {
        sb_putc(b, '.');
        uint64_t f = v % scale;
        for (uint64_t d = scale / 10; d; d /= 10) {
            sb_putc(b, (char)('0' + (f / d) % 10));
        }
    }
}

typedef struct
{
    int index, total;
    hw_ring_frame_t f, head;
} pos_t;

static int locate(pos_t *p)
{
    uint64_t v;
    p->index = virt_now(&v);
    p->total = ring_count();
    if ((p->index < 0) || (p->total < 1)) {
        return 0;
    }
    hw_ring_frame(p->index, &p->f);
    hw_ring_frame(p->total - 1, &p->head);
    return 1;
}

static void line_of(sbuf_t *out, const pos_t *p, int ok)
{
    char name[16];
    sbuf_t nb;
    sb_init(&nb, name, sizeof(name));
    sb_put(&nb, mode_name[RG.mode]);
    /* {:<7}: the mode padded to 7 columns (a glyph counts as one) */
    int cols = 0;
    for (const char *s = mode_name[RG.mode]; *s; s++) {
        cols += ((*s & 0xC0) != 0x80);
    }
    sb_put(out, sb_str(&nb));
    sb_spaces(out, cols < 7 ? 7 - cols : 0);
    sb_putc(out, ' ');
    sb_put(out, mode_glyph[RG.mode]);
    if (!ok) {
        sb_put(out, "  (no anchors yet)");
        return;
    }
    sb_put(out, "  frame ");
    sb_dec(out, (uint64_t)p->index + 1);
    sb_putc(out, '/');
    sb_dec(out, (uint64_t)p->total);
    sb_put(out, "   ");
    if (p->index + 1 == p->total) {
        sb_put(out, "now");
    } else {
        sb_putc(out, '-');
        sb_fix(out, p->head.cycles - p->f.cycles, cpu_hz(), 1);
        sb_putc(out, 's');
    }
    if ((RG.mode == MD_CUT) && RG.last_cut) {
        sb_put(out, "  (-");
        sb_dec(out, (uint64_t)RG.last_cut);
        sb_put(out, " anchors)");
    }
    const char *mk = ring_mark_of(p->f.seq);
    if (mk) {
        sb_put(out, "   \xc2\xb7   ");
        sb_put(out, mk);
        sb_put(out, " +0");
    }
}

void ring_status_line(sbuf_t *out)
{
    pos_t p;
    int ok = locate(&p);
    line_of(out, &p, ok);
}

static void range_line(sbuf_t *out)
{
    hw_ring_status_t st;
    int n = (hw_ring_status(&st) == 0) ? st.frames : 0;
    if (n < 1) {
        sb_put(out, "RINGBUFFER SIZE: empty (fills while the machine runs)");
        return;
    }
    int64_t k = k_off();
    hw_ring_frame_t first, last;
    hw_ring_frame(0, &first);
    hw_ring_frame(n - 1, &last);
    uint64_t live = mach_cycles();
    sb_put(out, "RINGBUFFER SIZE ");
    sb_dec(out, (uint64_t)n);
    sb_put(out, " (starts at Cycle ");
    sb_dec(out, (uint64_t)((int64_t)first.cycles + k));
    sb_put(out, " Frame 1 // ends at Cycle ");
    sb_dec(out, (uint64_t)((int64_t)last.cycles + k));
    sb_put(out, " Frame ");
    sb_dec(out, (uint64_t)n);
    sb_put(out, ")\n           span ");
    uint64_t span = last.cycles - first.cycles;
    sb_dec(out, span);
    sb_put(out, " cyc = ");
    sb_fix(out, span, cpu_hz(), 2);
    sb_put(out, "s   \xc2\xb7   cursor at ");
    pos_t p;
    if (RG.landed && locate(&p)) {
        sb_put(out, "Cycle ");
        sb_dec(out, (uint64_t)((int64_t)p.f.cycles + k));
        sb_put(out, " Frame ");
        sb_dec(out, (uint64_t)p.index + 1);
    } else {
        sb_put(out, "Cycle ");
        sb_dec(out, live);
        sb_put(out, " (live head)");
    }
    sb_put(out, "   \xc2\xb7   machine now Cycle ");
    sb_dec(out, live);
    sb_put(out, "\n           reaches ");
    uint64_t first_r = (uint64_t)((int64_t)first.cycles + k);
    sb_fix(out, live > first_r ? live - first_r : 0, cpu_hz(), 2);
    sb_put(out, "s back from now   \xc2\xb7   ");
    /* continuity: entries one frame apart (a frame is cyc_frame() cycles) */
    int gaps = 0;
    uint64_t worst = 0, worst_at = 0, prev = first.cycles;
    for (int i = 1; i < n; i++) {
        hw_ring_frame_t f;
        if (hw_ring_frame(i, &f) < 0) {
            continue;
        }
        uint64_t d = f.cycles - prev;
        if (d > cyc_frame() + 8) {
            gaps++;
            if (d > worst) {
                worst = d;
                worst_at = prev;
            }
        }
        prev = f.cycles;
    }
    if (!gaps) {
        sb_put(out, "continuous (every ");
        sb_dec(out, cyc_frame());
        sb_put(out, " cyc)");
    } else {
        sb_dec(out, (uint64_t)gaps);
        sb_put(out, " GAP(S) \xe2\x80\x94 worst ");
        sb_dec(out, worst);
        sb_put(out, " cyc (");
        sb_fix(out, worst, cpu_hz(), 2);
        sb_put(out, "s) after Cycle ");
        sb_dec(out, (uint64_t)((int64_t)worst_at + k));
        sb_put(out, ", expected ");
        sb_dec(out, cyc_frame());
    }
}

void ring_status_json(jw_t *w, int hit_end)
{
    pos_t p;
    int ok = locate(&p);
    char t[512];
    sbuf_t b;
    jw_obj(w);
    jw_kstr(w, "mode", mode_name[RG.mode]);
    jw_knull(w, "playing");
    jw_kstr(w, "direction", "none");
    jw_ku64(w, "speed", 1);
    ring_entry_t e;
    int at_head = !ok || (p.index + 1 == p.total);
    if (ok) {
        fill_entry(p.index, &p.f, k_off(), &e);
    }
    jw_key(w, "cursor");
    if (ok && !at_head) {
        jw_str(w, e.id);
    } else {
        jw_null(w);
    }
    if (ok) {
        jw_ku64(w, "frameIndex", (uint64_t)p.index + 1);
        jw_ku64(w, "frameTotal", (uint64_t)p.total);
        jw_kstr(w, "anchorId", e.id);
        jw_ku64(w, "frame", e.frame);
        jw_ku64(w, "cycles", e.cycles);
    } else {
        jw_knull(w, "frameIndex");
        jw_knull(w, "frameTotal");
        jw_knull(w, "anchorId");
        jw_knull(w, "frame");
        jw_knull(w, "cycles");
    }
    sb_init(&b, t, sizeof(t));
    sb_fix(&b, at_head ? 0 : p.head.cycles - p.f.cycles, cpu_hz(), 3);
    jw_key(w, "secondsBehind");
    jw_raw(w, b.p, b.len);
    jw_kbool(w, "atHead", at_head);
    jw_ku64(w, "lastCut", (uint64_t)RG.last_cut);
    sb_init(&b, t, sizeof(t));
    line_of(&b, &p, ok);
    jw_key(w, "line");
    jw_strn(w, b.p, b.len);
    jw_kstr(w, "keys", KEYS);
    sb_init(&b, t, sizeof(t));
    range_line(&b);
    jw_key(w, "range");
    jw_strn(w, b.p, b.len);
    jw_kbool(w, "rawFrame", 0);
    jw_kstr(w, "shownFrame", "first");
    jw_knull(w, "nearestMark");
    if (hit_end) {
        jw_kbool(w, "hitEnd", 1);
    }
    jw_end_obj(w);
}

/* the nearest land target from index i in direction dir (+1/-1), i itself first */
static int target_near(int i, int dir, int total)
{
    for (; (i >= 0) && (i < total); i += dir) {
        hw_ring_frame_t f;
        if ((hw_ring_frame(i, &f) == 0) && f.target) {
            return i;
        }
    }
    return -1;
}

const char *ring_frame_move(int delta, int *at_end)
{
    *at_end = 0;
    if (ring_off_reason()) {
        return ring_off_reason();
    }
    pos_t p;
    if (!locate(&p)) {
        return "transport: no anchors yet \xe2\x80\x94 the ring fills while the machine RUNS";
    }
    int at_head = !RG.landed || (p.index + 1 == p.total);
    if ((delta > 0) && at_head) {
        /* forward from the head runs: one frame of emulation per step */
        if (!mach_own()) {
            return "the debug block is not available";
        }
        if (M.running) {
            mach_pause();
        }
        if (step_n((uint64_t)delta * cyc_frame(), 1) < 0) {
            return "the machine did not stop";
        }
        hw_cpu_t c;
        mach_cpu(&c);
        if (!c.at_ib) {
            mach_step(1, 0);
        }
        RG.mode = MD_FWD;
        return 0;
    }
    int want = p.index + delta;
    if (want < 0) {
        want = 0;
        *at_end = 1;
    }
    if (want >= p.total) {
        want = p.total - 1;
        *at_end = 1;
    }
    int to = target_near(want, delta < 0 ? 1 : -1, p.total);
    if ((to < 0) || ((delta < 0) && (to >= p.index) && RG.landed)) {
        to = target_near(want, delta < 0 ? -1 : 1, p.total);
    }
    if (to < 0) {
        return "transport: no frame there a land can go to";
    }
    const char *e = ring_land(to);
    if (e) {
        return e;
    }
    RG.mode = (delta < 0) ? MD_BACK : MD_FWD;
    return 0;
}

const char *ring_goto(const char *t)
{
    if (ring_off_reason()) {
        return ring_off_reason();
    }
    hw_ring_status_t st;
    if ((hw_ring_status(&st) < 0) || (st.frames < 1)) {
        return "transport: the checkpoint ring is empty";
    }
    for (int i = 0; i < RG.nmk; i++) {
        if (m_streq(RG.mk[i].name, t)) {
            int idx = hw_ring_find(RG.mk[i].seq);
            if (idx < 0) {
                return "goto: that mark's frame has left the ring";
            }
            const char *e = ring_land(idx);
            if (!e) {
                RG.mode = (idx + 1 == st.frames) ? MD_LIVE : MD_REPLAY;
            }
            return e;
        }
    }
    int idx;
    if ((t[0] == 'c') || (t[0] == '@')) {
        uint64_t want;
        if (!parse_u64(t + 1, &want)) {
            return "goto: not a cycle (use c<number> or @<number>)";
        }
        int64_t k = k_off();
        int64_t v = (int64_t)want - k;
        idx = target_at_or_before(v < 0 ? 0 : (uint64_t)v, st.frames);
        if ((v < 0) || (idx < 0)) {
            return "goto: that cycle is older than the ring";
        }
    } else {
        uint32_t n;
        if (!m_parse_dec(t, -1, &n)) {
            return "goto: neither a frame number nor c<cycle>";
        }
        if ((n == 0) || ((int)n > st.frames)) {
            return "goto: the frame is outside the ring";
        }
        idx = target_near((int)n - 1, -1, st.frames);
        if (idx < 0) {
            return "goto: no frame there a land can go to";
        }
    }
    const char *e = ring_land(idx);
    if (!e) {
        RG.mode = (idx + 1 == st.frames) ? MD_LIVE : MD_REPLAY;
    }
    return e;
}

/* ---- marks -------------------------------------------------------------- */

const char *ring_mark_of(uint32_t seq)
{
    for (int i = 0; i < RG.nmk; i++) {
        if (RG.mk[i].seq == seq) {
            return RG.mk[i].name;
        }
    }
    return 0;
}

const char *ring_mark_set(const char *name, ring_entry_t *e)
{
    if (ring_off_reason()) {
        return ring_off_reason();
    }
    int i = ring_cursor();
    if (i < 0) {
        return "no anchors yet \xe2\x80\x94 the ring fills while the machine runs";
    }
    int slot = -1;
    for (int k = 0; k < RG.nmk; k++) {
        if (m_streq(RG.mk[k].name, name)) {
            slot = k;
        }
    }
    if (slot < 0) {
        if (RG.nmk >= RING_MAX_MARKS) {
            return "all 32 marks are used (`unmark` one)";
        }
        slot = RG.nmk++;
    }
    int n = m_strlen(name);
    if (n > (int)sizeof(RG.mk[slot].name) - 1) {
        n = (int)sizeof(RG.mk[slot].name) - 1;
    }
    memcpy(RG.mk[slot].name, name, (size_t)n);
    RG.mk[slot].name[n] = 0;
    ring_entry(i, e);
    RG.mk[slot].seq = e->seq;
    return 0;
}

const char *ring_mark_drop(const char *name, uint64_t *was)
{
    for (int k = 0; k < RG.nmk; k++) {
        if (m_streq(RG.mk[k].name, name)) {
            ring_entry_t e;
            int idx = hw_ring_find(RG.mk[k].seq);
            *was = ((idx >= 0) && (ring_entry(idx, &e) == 0)) ? e.cycles : 0;
            for (int j = k; j < RG.nmk - 1; j++) {
                RG.mk[j] = RG.mk[j + 1];
            }
            RG.nmk--;
            return 0;
        }
    }
    return "no mark by that name";
}

int ring_marks(void) { return RG.nmk; }
const char *ring_mark_name(int i) { return RG.mk[i].name; }

int ring_mark_entry(int i, ring_entry_t *e)
{
    int idx = hw_ring_find(RG.mk[i].seq);
    return (idx < 0) ? -1 : ring_entry(idx, e);
}

/* ---- monitor verbs ------------------------------------------------------ */

static int fail(sbuf_t *out, const char *verb, const char *why)
{
    sb_put(out, verb);
    sb_put(out, ": ");
    sb_put(out, why);
    return MON_ERR;
}

static void addr4(sbuf_t *out, uint16_t a)
{
    char t[8];
    sbuf_t s;
    sb_init(&s, t, sizeof(t));
    sb_putc(&s, '$');
    sb_hex(&s, a, 4);
    span_mark(out, t, 5, a, ROLE_PC, 0, 1);
}

/* the frame start at or before now (dump, capture) */
int ring_verb(const char *op, int argc, char **argv, sbuf_t *out)
{
    static const char *const ours[] = { "rstep", "reverse", "rewind", "frame", "goto", "mark", "marks", "unmark",
                                        0 };
    int known = 0;
    for (int i = 0; ours[i]; i++) {
        known |= m_streq(op, ours[i]);
    }
    if (!known) {
        return -1;
    }
    if (!hw_ring_present()) {
        return fail(out, op, "needs the T20 frame ring (the bitstream has no engine, or the firmware no ring table)");
    }
    if (ring_off_reason()) {
        return fail(out, op, ring_off_reason());
    }
    if (m_streq(op, "rstep") || m_streq(op, "reverse")) {
        uint32_t n = 1;
        if ((argc > 1) && !m_parse_dec(argv[1], -1, &n)) {
            return fail(out, op, "usage: rstep [n]");
        }
        ring_rstep_t r;
        const char *e = ring_rstep((int)n, &r);
        if (e) {
            return fail(out, "reverse-step", e);
        }
        sb_put(out, "reverse-step: undid ");
        sb_dec(out, (uint64_t)r.steps);
        sb_put(out, " instruction(s) \xe2\x86\x92 landed @ ");
        addr4(out, r.pc);
        sb_put(out, "  A=$");
        sb_hex(out, r.a, 2);
        sb_put(out, " X=$");
        sb_hex(out, r.x, 2);
        sb_put(out, " Y=$");
        sb_hex(out, r.y, 2);
        sb_put(out, " SP=$");
        sb_hex(out, r.s, 2);
        sb_put(out, " P=$");
        sb_hex(out, r.p, 2);
        sb_put(out, "  cyc=");
        sb_dec(out, r.cycle);
        sb_put(out, "\n  (memory writes rolled back with the frame's log; this host does not list them)"
                    "\n  NOTE: replayed from the start of Frame ");
        sb_dec(out, (uint64_t)r.frame_from);
        sb_put(out, " \xe2\x80\x94 CPU, RAM and CIAs exact, the VIC's registers replayed (its internals, the SID, "
                    "REU and drive are not put back). `g` from here cuts the frames after it.");
        return MON_OK;
    }
    if (m_streq(op, "rewind")) {
        ring_status_line(out);
        sb_putc(out, '\n');
        sb_put(out, KEYS);
        sb_putc(out, '\n');
        range_line(out);
        sb_put(out, "\n  cap 4096 anchors   cadence every 1 frame(s)   window = what the log areas hold"
                    "\n  frame -N|+N \xc2\xb7 goto <frame|c<cycle>|mark> \xc2\xb7 rstep [n]"
                    " (play back|fwd: not available on this host)");
        return MON_OK;
    }
    if (m_streq(op, "frame")) {
        if (argc < 2) {
            ring_status_line(out);
            return MON_OK;
        }
        const char *t = argv[1];
        int neg = t[0] == '-';
        uint32_t n;
        if (!m_parse_dec(t + ((t[0] == '-') || (t[0] == '+')), -1, &n)) {
            return fail(out, "frame", "usage: frame -N | frame +N");
        }
        int end;
        const char *e = ring_frame_move(neg ? -(int)n : (int)n, &end);
        if (e) {
            return fail(out, "frame", e);
        }
        ring_status_line(out);
        if (end) {
            sb_put(out, "   (END OF BUFFER)\n");
            range_line(out);
        }
        return MON_OK;
    }
    if (m_streq(op, "goto")) {
        if (argc < 2) {
            return fail(out, "goto", "usage: goto <frame> | goto c<cycle>");
        }
        const char *e = ring_goto(argv[1]);
        if (e) {
            sb_put(out, e);
            return MON_ERR;
        }
        ring_status_line(out);
        return MON_OK;
    }
    if (m_streq(op, "mark")) {
        if (argc < 2) {
            return fail(out, "mark", "usage: mark <name>  (names the anchor you are standing on)");
        }
        ring_entry_t e;
        const char *er = ring_mark_set(argv[1], &e);
        if (er) {
            return fail(out, "mark", er);
        }
        sb_put(out, "MARK ");
        sb_put(out, argv[1]);
        sb_put(out, " @ Cycle ");
        sb_dec(out, e.cycles);
        sb_put(out, " Frame ");
        sb_dec(out, (uint64_t)e.index + 1);
        sb_put(out, "   \xc2\xb7   ");
        sb_dec(out, (uint64_t)RG.nmk);
        sb_put(out, " of 32 marks (not pinned on this host: a mark goes when its frame leaves the ring)");
        return MON_OK;
    }
    if (m_streq(op, "marks")) {
        if (!RG.nmk) {
            sb_put(out, "no marks (0 of 32) \xe2\x80\x94 `mark <name>` names the anchor you are on");
            return MON_OK;
        }
        uint64_t live = mach_cycles();
        for (int i = 0; i < RG.nmk; i++) {
            ring_entry_t e;
            int gone = ring_mark_entry(i, &e) < 0;
            sb_put(out, "  ");
            sb_put(out, RG.mk[i].name);
            sb_spaces(out, 16 - m_strlen(RG.mk[i].name));
            if (gone) {
                sb_put(out, " (its frame has left the ring)\n");
                continue;
            }
            sb_put(out, " Cycle ");
            char t[24];
            sbuf_t b;
            sb_init(&b, t, sizeof(t));
            sb_dec(&b, e.cycles);
            sb_spaces(out, 12 - b.len);
            sb_put(out, sb_str(&b));
            sb_put(out, "  Frame ");
            sb_init(&b, t, sizeof(t));
            sb_dec(&b, (uint64_t)e.index + 1);
            sb_spaces(out, 5 - b.len);
            sb_put(out, sb_str(&b));
            sb_put(out, "   -");
            sb_fix(out, live > e.cycles ? live - e.cycles : 0, cpu_hz(), 2);
            sb_put(out, "s\n");
        }
        sb_put(out, "  ");
        sb_dec(out, (uint64_t)RG.nmk);
        sb_put(out, " of 32 marks \xc2\xb7 not pinned on this host");
        return MON_OK;
    }
    if (m_streq(op, "unmark")) {
        if (argc < 2) {
            return fail(out, "unmark", "usage: unmark <name>");
        }
        uint64_t was;
        if (ring_mark_drop(argv[1], &was)) {
            sb_put(out, "unmark: no mark named `");
            sb_put(out, argv[1]);
            sb_putc(out, '`');
            return MON_ERR;
        }
        sb_put(out, "UNMARK ");
        sb_put(out, argv[1]);
        sb_put(out, " (was Cycle ");
        sb_dec(out, was);
        sb_putc(out, ')');
        return MON_OK;
    }
    return -1;
}
