/*
 * trace.c — the instruction trace as the monitor's history (see trace.h).
 *
 * TRX64 sources: chis crates/trx64-daemon/src/main.rs (format_chis_from_ring),
 * whowrote and triage crates/trx64-monitor/src/verbs.rs, the triage chain
 * crates/trx64-core/src/crash_triage.rs, revdepth verbs.rs. Differences, all from the hardware:
 *  - a record holds the registers BEFORE its instruction (TRX64's history ring
 *    holds them after): chis shows the next record's, the newest the live ones;
 *  - whowrote takes old and new from the write's own record (stream 0 has both);
 *  - no caller chains (callerChain is empty) and no loop-onset pin;
 *  - the depth is fixed (1 s, the 8 MB of engine stream 3).
 */
#include "trace.h"
#include "dis6502.h"
#include "mon.h"
#include "ring.h"
#include "spans.h"

#define EM  "\xe2\x80\x94"
#define EN  "\xe2\x80\x93"
#define ARL "\xe2\x86\x90"
#define ARR "\xe2\x86\x92"
#define HR  "\xe2\x94\x80"

#define MAX_CHIS_LINES 500
#define HC             128

int trace_present(void) { return hw_trace_present(); }

static const char *const NO_TRACE = "needs the instruction trace (T21 phase 4): the bitstream has no trace former, or the "
                                    "ring is not running";

/* ---- the history through a small cache ------------------------------------- */

static struct
{
    uint32_t base;
    int n;
    hw_trace_rec_t r[HC];
} hc;
static uint32_t s_total; /* records in the history now */

static int hist_open(void)
{
    hw_trace_status_t st;
    hc.n = 0;
    if ((hw_trace_status(&st) < 0) || !st.present) {
        s_total = 0;
        return -1;
    }
    s_total = st.records;
    return 0;
}

static int hist_get(uint32_t i, hw_trace_rec_t *r)
{
    if (i >= s_total) {
        return -1;
    }
    if (!hc.n || (i < hc.base) || (i >= hc.base + (uint32_t)hc.n)) {
        hc.base = i - (i % HC);
        hc.n = hw_trace_read(hc.base, HC, hc.r);
        if (hc.n <= 0) {
            hc.n = 0;
            return -1;
        }
    }
    if (i >= hc.base + (uint32_t)hc.n) {
        return -1;
    }
    *r = hc.r[i - hc.base];
    return 0;
}

/* The first index whose record has cycle >= c (the timeline's count), s_total when none */
static uint32_t lower_bound(int64_t c)
{
    uint32_t lo = 0, hi = s_total;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        hw_trace_rec_t r;
        if (hist_get(mid, &r) < 0) {
            return s_total;
        }
        if ((int64_t)r.cycle >= c) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

int trace_rec_back(uint64_t vp, int n, hw_trace_rec_t *r)
{
    if ((n < 1) || (hist_open() < 0)) {
        return -1;
    }
    uint32_t cnt = lower_bound((int64_t)vp);
    if (cnt < (uint32_t)n) {
        return -1;
    }
    return hist_get(cnt - (uint32_t)n, r);
}

/* ---- text helpers ------------------------------------------------------------ */

static int fail(sbuf_t *out, const char *verb, const char *why)
{
    sb_put(out, verb);
    sb_put(out, ": ");
    sb_put(out, why);
    return MON_ERR;
}

static void hex_s(sbuf_t *out, uint32_t v, int digits)
{
    sb_hex(out, v, digits);
}

static void put_dollar(sbuf_t *out, uint32_t v, int digits, int upper)
{
    sb_putc(out, '$');
    if (upper) {
        sb_HEX(out, v, digits);
    } else {
        sb_hex(out, v, digits);
    }
}

/* characters a reader sees: marks are invisible, a UTF-8 sequence counts one */
static int vis_len(const char *s, int n)
{
    int cols = 0;
    int in_mark = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == SPAN_START) {
            in_mark = 1;
        } else if (c == SPAN_SEP) {
            in_mark = 0;
        } else if (c == SPAN_END) {
        } else if (!in_mark && ((c & 0xC0) != 0x80)) {
            cols++;
        }
    }
    return cols;
}

static int all_digits(const char *t)
{
    if (!*t) {
        return 0;
    }
    for (; *t; t++) {
        if ((*t < '0') || (*t > '9')) {
            return 0;
        }
    }
    return 1;
}

static uint64_t to_u64(const char *t)
{
    uint64_t v = 0;
    for (; *t; t++) {
        v = v * 10 + (uint64_t)(*t - '0');
    }
    return v;
}

/* ---- chis ---------------------------------------------------------------------- */

typedef struct
{
    uint16_t pc;
    uint8_t b[3];
} bytes_t;

static uint8_t rd_bytes(void *ctx, uint16_t a)
{
    const bytes_t *b = (const bytes_t *)ctx;
    uint16_t d = (uint16_t)(a - b->pc);
    return d < 3 ? b->b[d] : 0;
}

static void regs_after(const hw_trace_rec_t *next, int live, sbuf_t *out)
{
    uint8_t a, x, y, s, p;
    if (next) {
        a = next->a;
        x = next->x;
        y = next->y;
        s = next->sp;
        p = next->p;
    } else if (live) {
        hw_cpu_t c;
        mach_cpu(&c);
        a = c.a;
        x = c.x;
        y = c.y;
        s = c.s;
        p = c.p;
    } else {
        sb_put(out, "a=-- x=-- y=-- sp=-- p=--");
        return;
    }
    sb_put(out, "a=");
    hex_s(out, a, 2);
    sb_put(out, " x=");
    hex_s(out, x, 2);
    sb_put(out, " y=");
    hex_s(out, y, 2);
    sb_put(out, " sp=");
    hex_s(out, s, 2);
    sb_put(out, " p=");
    hex_s(out, p, 2);
}

static int verb_chis(int argc, char **argv, sbuf_t *out)
{
    if (hist_open() < 0) {
        return fail(out, "chis", NO_TRACE);
    }
    int win = (argc > 2) && all_digits(argv[1]) && all_digits(argv[2]);
    uint64_t last = 4000;
    if (!win && (argc > 1) && all_digits(argv[1])) {
        last = to_u64(argv[1]);
    }
    if (!s_total) {
        return fail(out, "chis", "no cpu history " EM " the instruction trace holds no record yet (run the machine; `trace on`)");
    }
    int64_t k = ring_cycle_offset();
    hw_trace_rec_t newest;
    if (hist_get(s_total - 1, &newest) < 0) {
        return fail(out, "chis", "the trace could not be read");
    }
    uint64_t ncyc = (uint64_t)((int64_t)newest.cycle + k);
    uint64_t lo, hi;
    if (win) {
        lo = to_u64(argv[1]);
        hi = to_u64(argv[2]);
        if (lo > hi) {
            uint64_t t = lo;
            lo = hi;
            hi = t;
        }
    } else {
        lo = ncyc > last ? ncyc - last : 0;
        hi = ncyc;
    }
    uint32_t i0 = lower_bound((int64_t)lo - k);
    uint32_t i1 = lower_bound((int64_t)hi - k + 1);
    sb_put(out, "# cpuhistory (instruction trace) " EM " ");
    uint32_t n = i1 > i0 ? i1 - i0 : 0;
    if (win) {
        sb_put(out, "cycles ");
        sb_dec(out, lo);
        sb_put(out, EN);
        sb_dec(out, hi);
    } else {
        sb_put(out, "last ");
        sb_dec(out, last);
        sb_put(out, " cycles");
    }
    sb_put(out, ", ");
    sb_dec(out, n);
    sb_put(out, " instr");
    if (!n) {
        sb_put(out, "\n(cpuhistory ring empty)");
        return MON_OK;
    }
    if (n > MAX_CHIS_LINES) {
        i0 = i1 - MAX_CHIS_LINES;
        sb_put(out, " (the newest ");
        sb_dec(out, MAX_CHIS_LINES);
        sb_put(out, " shown; `chis <c1> <c2>` narrows the window)");
    }
    int live = !M.running && M.stop.at_ib;
    for (uint32_t i = i0; i < i1; i++) {
        hw_trace_rec_t r, nx;
        if (hist_get(i, &r) < 0) {
            break;
        }
        int has_next = (i + 1 < s_total) && (hist_get(i + 1, &nx) == 0);
        sb_putc(out, '\n');
        sb_putc(out, 'c');
        char cb[24];
        sbuf_t c;
        sb_init(&c, cb, sizeof(cb));
        sb_dec(&c, (uint64_t)((int64_t)r.cycle + k));
        sb_put(out, sb_str(&c));
        sb_spaces(out, c.len < 10 ? 10 - c.len : 0);
        sb_putc(out, ' ');
        char line[160];
        sbuf_t l;
        sb_init(&l, line, sizeof(line));
        bytes_t bt = { r.pc, { r.opcode, r.b1, r.b2 } };
        dis_line(rd_bytes, &bt, r.pc, &l, 1, "cpu");
        sb_putn(out, l.p, l.len);
        int v = vis_len(l.p, l.len);
        sb_spaces(out, v < 30 ? 30 - v : 0);
        sb_put(out, "  ");
        regs_after(has_next ? &nx : 0, !has_next && live, out);
    }
    return MON_OK;
}

/* ---- whowrote ------------------------------------------------------------------ */

static const char *exhaust_hint(void)
{
    return "the reverse window is full (revdepth=1s, the trace wrapped) " EM " the answer may be OLDER than the ring. The "
           "depth is fixed on this host (the trace area is the 8 MB of engine stream 3); trigger/break earlier, then re-run.";
}

/* the history has wrapped: a miss may mean "older than the ring" */
static int ring_wrapped(void)
{
    hw_trace_status_t st;
    return (hw_trace_status(&st) == 0) && (st.records + 1 >= st.capacity);
}

static int verb_whowrote(int argc, char **argv, sbuf_t *out)
{
    if (argc < 2) {
        return fail(out, "whowrote", "need an address, e.g. `whowrote 01f5`");
    }
    uint32_t a;
    if (!m_parse_hex(argv[1], -1, &a) || (a > 0xFFFF)) {
        return fail(out, "whowrote", "need an address, e.g. `whowrote 01f5`");
    }
    uint32_t limit = 8;
    if (argc > 2) {
        m_parse_dec(argv[2], -1, &limit);
    }
    if (limit < 1) {
        limit = 1;
    }
    if (limit > 64) {
        limit = 64;
    }
    if (!hw_trace_present()) {
        return fail(out, "whowrote", NO_TRACE);
    }
    static hw_trace_hit_t hits[64];
    int ex;
    int n = hw_trace_whowrote((uint16_t)a, (int)limit, hits, &ex);
    int64_t k = ring_cycle_offset();
    if (n <= 0) {
        sb_put(out, "whowrote ");
        put_dollar(out, a, 4, 0);
        sb_put(out, ": no writer in the live delta ring (window not covered, or never written since the last reset). "
                    "Older history lives only in a finalized trace.");
        if (ex & HW_WHO_UNRESOLVED) {
            sb_put(out, "\nnote: stores through a zero page pointer ((zp),Y / (zp,X)) are in the history; their target is not in "
                        "the trace record, so they were not checked");
        }
        if ((ex & HW_WHO_EXHAUSTED) && ring_wrapped()) {
            sb_put(out, "\nring_exhausted: true   revdepth=1s   hint: ");
            sb_put(out, exhaust_hint());
        }
        return MON_OK;
    }
    sb_put(out, "whowrote ");
    span_addr4(out, (uint16_t)a, ROLE_MEMORY);
    sb_put(out, ": ");
    sb_dec(out, (uint64_t)n);
    sb_put(out, " writer(s) in the live ring (newest first):");
    for (int i = 0; i < n; i++) {
        sb_put(out, "\n  ");
        span_addr4(out, hits[i].addr, ROLE_MEMORY);
        sb_put(out, " <- written by ");
        span_addr4(out, hits[i].pc, ROLE_PC);
        sb_put(out, " @ cyc ");
        sb_dec(out, (uint64_t)((int64_t)hits[i].cycle + k));
        sb_put(out, "  ($");
        if (hits[i].flags & HW_HIT_OLD_UNKNOWN) {
            sb_put(out, "??");
        } else {
            hex_s(out, hits[i].old_value, 2);
        }
        sb_put(out, " -> $");
        if (hits[i].flags & HW_HIT_NEW_UNKNOWN) {
            sb_put(out, "??");
        } else {
            hex_s(out, hits[i].new_value, 2);
        }
        sb_putc(out, ')');
    }
    if (ex & HW_WHO_UNRESOLVED) {
        sb_put(out, "\nnote: stores through a zero page pointer ((zp),Y / (zp,X)) are in the history; their target is not in "
                    "the trace record, so they were not checked");
    }
    return MON_OK;
}

/* ---- triage (crates/trx64-core/src/crash_triage.rs) --------------------------------- */

enum { TK_RTS, TK_RTI, TK_JMPIND, TK_JMP, TK_JSR, TK_UNKNOWN };
enum { CF_CERTAIN, CF_LIKELY, CF_LOW };
static const char *const kind_str[] = { "RTS", "RTI", "JMP(ind)", "JMP", "JSR", "?" };
static const char *const conf_str[] = { "certain", "likely", "low" };

typedef struct
{
    uint16_t addr;
    uint8_t value;
    int has_writer;
    uint16_t writer_pc;
    uint64_t writer_cycle;
    uint8_t writer_old, writer_new;
    int conf;
    char note[160];
} slot_t;

typedef struct
{
    uint16_t crash_pc;
    uint8_t crash_op;
    struct
    {
        uint16_t pc;
        uint8_t opcode;
        uint64_t cycle;
    } lead[6];
    int nlead;
    int kind;
    uint16_t at_pc, landed_pc;
    int has_pre_sp;
    uint8_t pre_sp;
    int has_vec;
    uint16_t vec;
    int conf;
    char note[320];
    int ring_bound;
    slot_t slot[2];
    int nslots;
    int pinned;
    char summary[900];
} triage_t;

static void note_put(char *dst, int cap, const char *a)
{
    sbuf_t b;
    sb_init(&b, dst, cap);
    sb_put(&b, a);
    sb_str(&b);
}

static void build_summary(triage_t *t)
{
    sbuf_t s;
    sb_init(&s, t->summary, sizeof(t->summary));
    sb_put(&s, "JAM @ ");
    put_dollar(&s, t->crash_pc, 4, 1);
    sb_put(&s, " (op ");
    put_dollar(&s, t->crash_op, 2, 1);
    sb_putc(&s, ')');
    switch (t->kind) {
    case TK_RTS:
    case TK_RTI: {
        uint16_t wild = (uint16_t)((t->nslots > 0 ? t->slot[0].value : 0) | ((t->nslots > 1 ? t->slot[1].value : 0) << 8));
        sb_put(&s, " " ARL " wild ");
        sb_put(&s, kind_str[t->kind]);
        sb_put(&s, " @ ");
        put_dollar(&s, t->at_pc, 4, 1);
        sb_put(&s, " popped ");
        put_dollar(&s, wild, 4, 1);
        sb_put(&s, " from ");
        put_dollar(&s, t->nslots > 0 ? t->slot[0].addr : 0, 4, 1);
        if (t->pinned) {
            for (int i = 0; i < t->nslots; i++) {
                if (t->slot[i].has_writer) {
                    sb_put(&s, " " ARL " ");
                    put_dollar(&s, t->slot[i].addr, 4, 1);
                    sb_put(&s, " written by ");
                    put_dollar(&s, t->slot[i].writer_pc, 4, 1);
                    sb_put(&s, " @ cyc ");
                    sb_dec(&s, t->slot[i].writer_cycle);
                    sb_put(&s, " (");
                    put_dollar(&s, t->slot[i].writer_old, 2, 1);
                    sb_put(&s, ARR);
                    put_dollar(&s, t->slot[i].writer_new, 2, 1);
                    sb_putc(&s, ')');
                }
            }
        } else {
            sb_put(&s, "  [corruptor NOT pinned: no writer for the popped slots in the live ring " EM " the bad byte was "
                       "stacked before the reverse window, or is the genuine (non-smashed) return]");
        }
        break;
    }
    case TK_JMPIND:
        sb_put(&s, " " ARL " wild JMP(ind) @ ");
        put_dollar(&s, t->at_pc, 4, 1);
        sb_put(&s, " through vector ");
        put_dollar(&s, t->has_vec ? t->vec : 0, 4, 1);
        sb_put(&s, " " ARR " ");
        put_dollar(&s, t->crash_pc, 4, 1);
        sb_put(&s, "  [NOT a stack smash " EM " corruptor = whoever wrote the vector bytes; indeterminate from the stack]");
        break;
    case TK_JMP:
    case TK_JSR:
        sb_put(&s, " " ARL " ");
        sb_put(&s, kind_str[t->kind]);
        sb_put(&s, " @ ");
        put_dollar(&s, t->at_pc, 4, 1);
        sb_put(&s, " " ARR " ");
        put_dollar(&s, t->crash_pc, 4, 1);
        sb_put(&s, "  [direct transfer to the wild target; not a stack smash " EM " if unexpected, the operand bytes were "
                   "self-modified]");
        break;
    default:
        sb_put(&s, " " ARL " ");
        sb_put(&s, t->note);
        sb_put(&s, "  [the wild control transfer is indeterminate from the live ring]");
        break;
    }
    sb_str(&s);
}

/* the newest first: i counts down; each distinct consecutive PC once, last 6 */
static void collapse_tail(triage_t *t, int64_t k)
{
    int n = 0;
    uint16_t last_pc = 0;
    int have = 0;
    struct
    {
        uint16_t pc;
        uint8_t op;
        uint64_t cyc;
    } tmp[6];
    for (uint32_t i = s_total; i > 0; i--) {
        hw_trace_rec_t r;
        if (hist_get(i - 1, &r) < 0) {
            break;
        }
        /* TRX64 collapses oldest -> newest keeping the FIRST of a run; walking back, the run's first is the
         * last one seen before the PC changes: keep the older entry when the next older has the same PC */
        if (have && (r.pc == last_pc)) {
            tmp[n - 1].pc = r.pc;
            tmp[n - 1].op = r.opcode;
            tmp[n - 1].cyc = (uint64_t)((int64_t)r.cycle + k);
            continue;
        }
        if (n == 6) {
            break;
        }
        tmp[n].pc = r.pc;
        tmp[n].op = r.opcode;
        tmp[n].cyc = (uint64_t)((int64_t)r.cycle + k);
        n++;
        last_pc = r.pc;
        have = 1;
    }
    t->nlead = n;
    for (int i = 0; i < n; i++) {
        t->lead[i].pc = tmp[n - 1 - i].pc;
        t->lead[i].opcode = tmp[n - 1 - i].op;
        t->lead[i].cycle = tmp[n - 1 - i].cyc;
    }
}

static void do_triage(triage_t *t, uint16_t crash_pc, int64_t k)
{
    memset(t, 0, sizeof(*t));
    t->crash_pc = crash_pc;
    t->crash_op = mach_peek8("cpu", crash_pc);
    t->kind = TK_UNKNOWN;
    t->landed_pc = crash_pc;
    t->conf = CF_LOW;
    note_put(t->note, sizeof(t->note), "no control-flow instruction found before the crash PC in the live history ring "
                                       "(the derail predates the ring, or PC walked off a block)");
    if (!s_total) {
        note_put(t->note, sizeof(t->note), "the instruction trace is empty (trace off, or cleared by a restart)");
        build_summary(t);
        return;
    }
    collapse_tail(t, k);
    /* the newest record at the crash PC, then the first of its run */
    int found = 0;
    uint32_t landing = 0;
    for (uint32_t i = s_total; i > 0; i--) {
        hw_trace_rec_t r;
        if (hist_get(i - 1, &r) < 0) {
            break;
        }
        if (!found) {
            if (r.pc == crash_pc) {
                found = 1;
                landing = i - 1;
            }
        } else if (r.pc == crash_pc) {
            landing = i - 1;
        } else {
            break;
        }
    }
    uint32_t tidx;
    if (found && (landing == 0)) {
        t->conf = CF_LOW;
        t->ring_bound = 1;
        sbuf_t b;
        sb_init(&b, t->note, sizeof(t->note));
        sb_put(&b, "the crash PC ");
        put_dollar(&b, crash_pc, 4, 1);
        sb_put(&b, " is the oldest instruction still in the ring " EM " the transfer that jumped here is older than the "
                   "reverse window");
        sb_str(&b);
        build_summary(t);
        return;
    }
    tidx = found ? landing - 1 : s_total - 1;
    hw_trace_rec_t tr;
    if (hist_get(tidx, &tr) < 0) {
        build_summary(t);
        return;
    }
    t->at_pc = tr.pc;
    t->has_pre_sp = 1;
    t->pre_sp = tr.sp; /* the record holds the registers before the instruction: the SP before the pop */
    switch (tr.opcode) {
    case 0x60:
        t->kind = TK_RTS;
        t->conf = CF_CERTAIN;
        note_put(t->note, sizeof(t->note), "RTS popped a 2-byte return address off the stack");
        break;
    case 0x40:
        t->kind = TK_RTI;
        t->conf = CF_CERTAIN;
        note_put(t->note, sizeof(t->note), "RTI popped status + a 2-byte return address off the stack");
        break;
    case 0x6C: {
        t->has_pre_sp = 0;
        t->kind = TK_JMPIND;
        t->has_vec = 1;
        t->vec = (uint16_t)(tr.b1 | (tr.b2 << 8));
        t->conf = CF_CERTAIN;
        sbuf_t b;
        sb_init(&b, t->note, sizeof(t->note));
        sb_put(&b, "JMP (");
        put_dollar(&b, t->vec, 4, 1);
        sb_put(&b, ") jumped through a memory vector " EM " not a stack pop; the corruptor is whoever wrote the vector "
                   "bytes (indeterminate as a stack smash)");
        sb_str(&b);
        break;
    }
    case 0x4C:
        t->has_pre_sp = 0;
        t->kind = TK_JMP;
        t->conf = CF_CERTAIN;
        note_put(t->note, sizeof(t->note), "direct JMP to the crash PC " EM " the target is the literal operand, not a stack "
                                           "value (if this is wrong, the operand bytes themselves were self-modified)");
        break;
    case 0x20:
        t->has_pre_sp = 0;
        t->kind = TK_JSR;
        t->conf = CF_CERTAIN;
        note_put(t->note, sizeof(t->note), "direct JSR to the crash PC " EM " the target is the literal operand, not a stack value");
        break;
    default: {
        t->has_pre_sp = 0;
        t->kind = TK_UNKNOWN;
        t->conf = CF_LOW;
        sbuf_t b;
        sb_init(&b, t->note, sizeof(t->note));
        sb_put(&b, "the instruction before the crash PC (");
        put_dollar(&b, tr.pc, 4, 1);
        sb_put(&b, ", op ");
        put_dollar(&b, tr.opcode, 2, 1);
        sb_put(&b, ") is not a control transfer " EM " execution likely fell through into the wild rather than jumping; no "
                   "stack pop to attribute");
        sb_str(&b);
        break;
    }
    }
    if (t->has_pre_sp && ((t->kind == TK_RTS) || (t->kind == TK_RTI))) {
        uint8_t lo_off = t->kind == TK_RTI ? 2 : 1, hi_off = t->kind == TK_RTI ? 3 : 2;
        static const char *const label[2] = { "ret-lo", "ret-hi" };
        for (int s = 0; s < 2; s++) {
            slot_t *sl = &t->slot[s];
            sl->addr = (uint16_t)(0x0100u | (uint8_t)(t->pre_sp + (s ? hi_off : lo_off)));
            sl->value = mach_peek8("ram", sl->addr);
            hw_trace_hit_t h;
            int ex;
            sbuf_t b;
            sb_init(&b, sl->note, sizeof(sl->note));
            if (hw_trace_whowrote(sl->addr, 1, &h, &ex) > 0) {
                sl->has_writer = 1;
                sl->writer_pc = h.pc;
                sl->writer_cycle = (uint64_t)((int64_t)h.cycle + k);
                sl->writer_old = h.old_value;
                sl->writer_new = h.new_value;
                sl->conf = CF_CERTAIN;
                sb_put(&b, label[s]);
                sb_put(&b, ": last written by ");
                put_dollar(&b, h.pc, 4, 1);
                t->pinned = 1;
            } else {
                sl->conf = CF_LOW;
                sb_put(&b, label[s]);
                sb_put(&b, ": no writer in the live ring (the corruptor predates the reverse window, or this byte is the "
                           "original stacked value)");
            }
            sb_str(&b);
            t->nslots++;
        }
    }
    build_summary(t);
}

static void triage_lines(const triage_t *t, sbuf_t *out)
{
    sb_put(out, HR HR " crash triage (reverse-debug Phase 2) ");
    for (int i = 0; i < 22; i++) {
        sb_put(out, HR);
    }
    sb_putc(out, '\n');
    sb_put(out, t->summary);
    sb_put(out, "\n  crash:    JAM @ ");
    put_dollar(out, t->crash_pc, 4, 1);
    sb_put(out, "  op ");
    put_dollar(out, t->crash_op, 2, 1);
    if (t->nlead) {
        sb_put(out, "\n  lead-in:  ");
        for (int i = 0; i < t->nlead; i++) {
            if (i) {
                sb_put(out, " " ARR " ");
            }
            put_dollar(out, t->lead[i].pc, 4, 1);
        }
    }
    sb_put(out, "\n  transfer: ");
    sb_put(out, kind_str[t->kind]);
    sb_put(out, " @ ");
    put_dollar(out, t->at_pc, 4, 1);
    sb_put(out, " " ARR " ");
    put_dollar(out, t->landed_pc, 4, 1);
    sb_put(out, "  [");
    sb_put(out, conf_str[t->conf]);
    sb_put(out, "]\n            ");
    sb_put(out, t->note);
    if ((t->kind == TK_RTS) || (t->kind == TK_RTI)) {
        for (int i = 0; i < t->nslots; i++) {
            const slot_t *s = &t->slot[i];
            sb_put(out, "\n  slot ");
            put_dollar(out, s->addr, 4, 1);
            sb_putc(out, '=');
            put_dollar(out, s->value, 2, 1);
            if (s->has_writer) {
                sb_put(out, "  " ARL " written by ");
                put_dollar(out, s->writer_pc, 4, 1);
                sb_put(out, " @ cyc ");
                sb_dec(out, s->writer_cycle);
                sb_put(out, " (");
                put_dollar(out, s->writer_old, 2, 1);
                sb_put(out, ARR);
                put_dollar(out, s->writer_new, 2, 1);
                sb_put(out, ")  [");
            } else {
                sb_put(out, "  " ARL " no writer in the live ring  [");
            }
            sb_put(out, conf_str[s->conf]);
            sb_putc(out, ']');
        }
        if (t->pinned) {
            sb_put(out, "\n  \xe2\x87\x92 corruptor PINNED " EM " the cited instruction put the bad byte on the stack.");
        } else {
            sb_put(out, "\n  \xe2\x87\x92 corruptor NOT pinned (low confidence) " EM " the bad return byte was stacked before the "
                        "reverse window, or is a genuine return. Inspect manually.");
        }
    } else if (t->kind != TK_UNKNOWN) {
        sb_put(out, "\n  \xe2\x87\x92 not a stack smash " EM " no stack corruptor to attribute (see the transfer note).");
    }
    if ((t->kind == TK_UNKNOWN) && t->ring_bound && ring_wrapped()) {
        sb_put(out, "\nring_exhausted: true   revdepth=1s   hint: ");
        sb_put(out, exhaust_hint());
    }
}

static int verb_triage(int argc, char **argv, sbuf_t *out)
{
    if (!hw_trace_present() || (hist_open() < 0)) {
        return fail(out, "triage", NO_TRACE);
    }
    uint16_t pc;
    if (argc > 1) {
        uint32_t a;
        if (!m_parse_hex(argv[1], -1, &a) || (a > 0xFFFF)) {
            return fail(out, "triage", "usage: triage [pc]");
        }
        pc = (uint16_t)a;
    } else {
        hw_cpu_t c;
        mach_cpu(&c);
        pc = mach_pc(&c);
    }
    static triage_t t;
    do_triage(&t, pc, ring_cycle_offset());
    triage_lines(&t, out);
    return MON_OK;
}

const char *trace_triage_json(jw_t *w, int have_pc, uint16_t pc)
{
    if (!hw_trace_present() || (hist_open() < 0)) {
        return NO_TRACE;
    }
    if (!have_pc) {
        hw_cpu_t c;
        mach_cpu(&c);
        pc = mach_pc(&c);
    }
    static triage_t t;
    do_triage(&t, pc, ring_cycle_offset());
    jw_obj(w);
    jw_kstr(w, "summary", t.summary);
    jw_kbool(w, "pinnedCorruptor", t.pinned);
    jw_knull(w, "loopOnset");
    jw_key(w, "crash");
    jw_obj(w);
    jw_ku64(w, "pc", t.crash_pc);
    jw_ku64(w, "opcode", t.crash_op);
    jw_key(w, "leadIn");
    jw_arr(w);
    for (int i = 0; i < t.nlead; i++) {
        jw_obj(w);
        jw_ku64(w, "pc", t.lead[i].pc);
        jw_ku64(w, "opcode", t.lead[i].opcode);
        jw_ku64(w, "cycle", t.lead[i].cycle);
        jw_end_obj(w);
    }
    jw_end_arr(w);
    jw_end_obj(w);
    jw_key(w, "transfer");
    jw_obj(w);
    jw_kstr(w, "kind", kind_str[t.kind]);
    jw_ku64(w, "atPc", t.at_pc);
    jw_ku64(w, "landedPc", t.landed_pc);
    jw_key(w, "preSp");
    if (t.has_pre_sp) {
        jw_u64(w, t.pre_sp);
    } else {
        jw_null(w);
    }
    jw_key(w, "vectorAddr");
    if (t.has_vec) {
        jw_u64(w, t.vec);
    } else {
        jw_null(w);
    }
    jw_kbool(w, "isStackPop", (t.kind == TK_RTS) || (t.kind == TK_RTI));
    jw_kstr(w, "confidence", conf_str[t.conf]);
    jw_kstr(w, "note", t.note);
    jw_kbool(w, "ringBound", t.ring_bound);
    jw_end_obj(w);
    jw_key(w, "corruptorSlots");
    jw_arr(w);
    for (int i = 0; i < t.nslots; i++) {
        const slot_t *s = &t.slot[i];
        jw_obj(w);
        jw_ku64(w, "addr", s->addr);
        jw_ku64(w, "value", s->value);
        if (s->has_writer) {
            jw_ku64(w, "writerPc", s->writer_pc);
            jw_ku64(w, "writerCycle", s->writer_cycle);
            jw_ku64(w, "writerOld", s->writer_old);
            jw_ku64(w, "writerNew", s->writer_new);
        } else {
            jw_knull(w, "writerPc");
            jw_knull(w, "writerCycle");
            jw_knull(w, "writerOld");
            jw_knull(w, "writerNew");
        }
        jw_kstr(w, "confidence", conf_str[s->conf]);
        jw_kstr(w, "note", s->note);
        jw_end_obj(w);
    }
    jw_end_arr(w);
    /* the formatted lines, as TRX64 attaches them */
    static char lines[3072];
    sbuf_t lb;
    sb_init(&lb, lines, sizeof(lines));
    triage_lines(&t, &lb);
    jw_key(w, "lines");
    jw_arr(w);
    int s0 = 0;
    for (int i = 0; i <= lb.len; i++) {
        if ((i == lb.len) || (lines[i] == '\n')) {
            jw_strn(w, lines + s0, i - s0);
            s0 = i + 1;
        }
    }
    jw_end_arr(w);
    jw_end_obj(w);
    return 0;
}

const char *trace_who_wrote_json(jw_t *w, uint16_t addr, int limit)
{
    if (!hw_trace_present()) {
        return NO_TRACE;
    }
    if (limit < 1) {
        limit = 1;
    }
    if (limit > 64) {
        limit = 64;
    }
    static hw_trace_hit_t hits[64];
    int ex;
    int n = hw_trace_whowrote(addr, limit, hits, &ex);
    int64_t k = ring_cycle_offset();
    int exhausted = (n <= 0) && (ex & HW_WHO_EXHAUSTED) && ring_wrapped();
    jw_obj(w);
    jw_ku64(w, "addr", addr);
    jw_key(w, "writers");
    jw_arr(w);
    for (int i = 0; i < n; i++) {
        jw_obj(w);
        jw_ku64(w, "pc", hits[i].pc);
        jw_ku64(w, "cycle", (uint64_t)((int64_t)hits[i].cycle + k));
        jw_ku64(w, "addr", hits[i].addr);
        jw_ku64(w, "old", hits[i].old_value);
        jw_ku64(w, "new", hits[i].new_value);
        if (hits[i].flags & HW_HIT_OLD_UNKNOWN) {
            jw_kbool(w, "oldKnown", 0); /* an I/O write found from the instruction: the record has no old byte */
        }
        if (hits[i].flags & HW_HIT_NEW_UNKNOWN) {
            jw_kbool(w, "newKnown", 0);
        }
        jw_key(w, "callerChain");
        jw_arr(w);
        jw_end_arr(w);
        jw_end_obj(w);
    }
    jw_end_arr(w);
    jw_kbool(w, "ringExhausted", exhausted);
    jw_kbool(w, "pointerStoresUnchecked", (ex & HW_WHO_UNRESOLVED) != 0);
    jw_ku64(w, "revdepthSeconds", 1);
    jw_kstr(w, "ringExhaustedHint", exhausted ? exhaust_hint() : "");
    jw_end_obj(w);
    return 0;
}

/* ---- revdepth, trace ------------------------------------------------------------ */

static void sb_fix2(sbuf_t *b, uint64_t num, uint64_t den)
{
    uint64_t v = (num * 100 + den / 2) / den;
    sb_dec(b, v / 100);
    sb_putc(b, '.');
    sb_putc(b, (char)('0' + (v / 10) % 10));
    sb_putc(b, (char)('0' + v % 10));
}

static int verb_revdepth(int argc, char **argv, sbuf_t *out)
{
    hw_trace_status_t st;
    if ((hw_trace_status(&st) < 0) || !st.present) {
        return fail(out, "revdepth", NO_TRACE);
    }
    hw_video_t v;
    hw_video(&v);
    uint64_t span = st.records ? st.newest_cycle - st.oldest_cycle : 0;
    sb_put(out, "revdepth: 1s (~8.0 MB) " EM " the instruction trace holds ");
    sb_dec(out, st.records);
    sb_put(out, " of ");
    sb_dec(out, st.capacity);
    sb_put(out, " instructions, ");
    sb_dec(out, span);
    sb_put(out, " cycles (");
    sb_fix2(out, span, v.cpu_hz);
    sb_put(out, " s); the writes live in stream 0 (16 MB)");
    if (argc > 1) {
        sb_put(out, "\n  the depth is fixed at 1 s on this host (the trace area is the 8 MB of engine stream 3): `revdepth ");
        sb_put(out, argv[1]);
        sb_put(out, "` changed nothing, the history is kept");
    } else {
        sb_put(out, "\n  (the depth is fixed: 1 s of the 1 MHz machine)");
    }
    return MON_OK;
}

const char *trace_depth_json(jw_t *w)
{
    hw_trace_status_t st;
    if ((hw_trace_status(&st) < 0) || !st.present) {
        return NO_TRACE;
    }
    jw_obj(w);
    jw_ku64(w, "seconds", 1);
    jw_ku64(w, "deltaEntryCapacity", st.capacity);
    jw_ku64(w, "deltaEntries", st.records);
    jw_ku64(w, "deltaWriteCapacity", 4194240);
    jw_ku64(w, "cpuHistoryCapacity", 0);
    jw_key(w, "estimatedRamMb");
    jw_raw(w, "8.0", 3);
    jw_kbool(w, "discardedHistory", 0);
    jw_kstr(w, "note", "the reverse depth is fixed at 1 s on the C64 Ultimate (the 8 MB of engine stream 3); the trace "
                       "holds the instructions, stream 0 the writes. `seconds` changes nothing.");
    jw_end_obj(w);
    return 0;
}

static int verb_trace(int argc, char **argv, sbuf_t *out)
{
    hw_trace_status_t st;
    if ((hw_trace_status(&st) < 0) || !st.present) {
        return fail(out, "trace", NO_TRACE);
    }
    const char *sub = argc > 1 ? argv[1] : "status";
    if (m_streq(sub, "on") || m_streq(sub, "off")) {
        hw_trace_ctl(m_streq(sub, "on"));
        hw_trace_status(&st);
        sb_put(out, m_streq(sub, "on") ? "trace on" : "trace off");
        sb_put(out, " " EM " the instruction trace (engine stream 3) ");
        sb_put(out, st.on ? "records with the ring" : "is off; the history stays until it is overwritten");
        return MON_OK;
    }
    if (m_streq(sub, "mark")) {
        sb_put(out, "trace mark: the trace needs no marks " EM " the timeline's are `mark <name>` (they hold a frame)");
        return MON_OK;
    }
    if (!m_streq(sub, "status")) {
        return fail(out, "trace", "usage: trace on|off|status|mark");
    }
    hw_video_t v;
    hw_video(&v);
    sb_put(out, "trace: ");
    sb_put(out, st.on ? "ON" : "off");
    sb_put(out, "  instruction trace (engine stream 3), ");
    sb_dec(out, st.records);
    sb_put(out, " of ");
    sb_dec(out, st.capacity);
    sb_put(out, " instructions");
    if (st.records) {
        int64_t k = ring_cycle_offset();
        sb_put(out, ", cycles ");
        sb_dec(out, (uint64_t)((int64_t)st.oldest_cycle + k));
        sb_put(out, EN);
        sb_dec(out, (uint64_t)((int64_t)st.newest_cycle + k));
        sb_put(out, " (");
        sb_fix2(out, st.newest_cycle - st.oldest_cycle, v.cpu_hz);
        sb_put(out, " s)");
    }
    sb_put(out, ", lost ");
    sb_dec(out, st.lost);
    if (st.gap) {
        sb_put(out, "\n  the history has a gap (a forward land past what the replays reached): the window across it is not contiguous");
    }
    return MON_OK;
}

int trace_verb(const char *op, int argc, char **argv, sbuf_t *out)
{
    if (m_streq(op, "chis")) {
        return verb_chis(argc, argv, out);
    }
    if (m_streq(op, "whowrote")) {
        return verb_whowrote(argc, argv, out);
    }
    if (m_streq(op, "triage")) {
        return verb_triage(argc, argv, out);
    }
    if (m_streq(op, "revdepth")) {
        return verb_revdepth(argc, argv, out);
    }
    if (m_streq(op, "trace")) {
        return verb_trace(argc, argv, out);
    }
    return -1;
}
