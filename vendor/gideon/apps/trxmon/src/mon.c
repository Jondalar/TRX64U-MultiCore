/*
 * mon.c — monitor verbs (see mon.h).
 */
#include "mon.h"
#include "dis6502.h"
#include "spans.h"
#include "ring.h"
#include "trace.h"
#include "asm6502.h"
#include "cond.h"

#define MAXTOK 40

static int s_quit;
static void obs_cmd_run(const obs_t *o);
static int is_one_of(const char *op, const char *const *list);
static int32_t s_asm = -1; /* modal assemble: the next address, -1 off */
/* asm block mode (typed line by line): origin, end tag, the lines so far */
static struct
{
    int on;
    uint16_t origin;
    char end[16];
    char src[4096];
    int len;
} s_blk;
static asm_block_t s_ab;

typedef struct
{
    char buf[512];
    char *t[MAXTOK];
    int n;
} toks_t;

static void tokenize(const char *cmd, toks_t *k)
{
    int n = m_strlen(cmd);
    if (n > (int)sizeof(k->buf) - 1) {
        n = (int)sizeof(k->buf) - 1;
    }
    memcpy(k->buf, cmd, (size_t)n);
    k->buf[n] = 0;
    k->n = 0;
    char *p = k->buf;
    while (*p && (k->n < MAXTOK)) {
        while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) {
            *p++ = 0;
        }
        if (!*p) {
            break;
        }
        k->t[k->n++] = p;
        while (*p && (*p != ' ') && (*p != '\t') && (*p != '\r') && (*p != '\n')) {
            p++;
        }
    }
}

static int addr_of(const char *t, uint16_t *a)
{
    uint32_t v;
    if (!t || !m_parse_hex(t, -1, &v)) {
        return 0;
    }
    *a = (uint16_t)v;
    return 1;
}

static int byte_of(const char *t, uint8_t *b)
{
    uint32_t v;
    if (!t || !m_parse_hex(t, -1, &v) || (v > 0xFF)) {
        return 0;
    }
    *b = (uint8_t)v;
    return 1;
}

/* A lens word (or `default`) at t, else NULL */
static const char *lens_of(const char *t)
{
    static const char *const l[] = { "cpu", "ram", "rom", "io", "cart" };
    if (!t) {
        return 0;
    }
    if (m_strieq(t, "default")) {
        return M.lens;
    }
    for (int i = 0; i < 5; i++) {
        if (m_strieq(t, l[i])) {
            return l[i];
        }
    }
    return 0;
}

static void err(sbuf_t *out, const char *a, const char *b)
{
    sb_put(out, a);
    if (b) {
        sb_put(out, b);
    }
}

static void flags_str(sbuf_t *out, uint8_t p)
{
    static const char n[] = "NV-BDIZC";
    for (int i = 0; i < 8; i++) {
        char c = n[i];
        sb_putc(out, ((p >> (7 - i)) & 1) ? c : m_lower(c));
    }
}

static uint8_t rd_lens(void *ctx, uint16_t a) { return mach_peek8((const char *)ctx, a); }

static uint16_t w16(uint16_t a)
{
    return (uint16_t)(mach_peek8("cpu", a) | (mach_peek8("cpu", (uint16_t)(a + 1)) << 8));
}

void mon_register_dump(sbuf_t *out)
{
    hw_cpu_t c;
    mach_cpu(&c);
    sb_put(out, "  ADDR AC XR YR SP NV-BDIZC\n.;");
    sb_HEX(out, mach_pc(&c), 4);
    sb_putc(out, ' ');
    sb_HEX(out, c.a, 2);
    sb_putc(out, ' ');
    sb_HEX(out, c.x, 2);
    sb_putc(out, ' ');
    sb_HEX(out, c.y, 2);
    sb_putc(out, ' ');
    sb_HEX(out, c.s, 2);
    sb_putc(out, ' ');
    flags_str(out, c.p);
}

void mon_machine_json(jw_t *w)
{
    jw_obj(w);
    jw_kstr(w, "device", "c64");
    jw_ku64(w, "cpuPortDirection", mach_ddr());
    jw_ku64(w, "cpuPortValue", mach_latch());
    jw_ku64(w, "exrom", (uint64_t)mach_exrom());
    jw_ku64(w, "game", (uint64_t)mach_game());
    jw_key(w, "cartBank");
    if (mach_cart_present()) {
        jw_u64(w, (uint64_t)mach_cart_bank());
    } else {
        jw_null(w);
    }
    jw_end_obj(w);
}

int mon_quit_requested(void) { return s_quit; }

/* The screen formats m and d for itself while it runs a line (T21 §8.2, §8.10): s_narrow is "a screen
 * line", s_cols its width (40 or 80), s_page how many rows one `m`/`d` fills (0: TRX64's own defaults) */
static int s_narrow;
static int s_cols = 40;
static int s_page;

void mon_set_narrow(int on) { s_narrow = on; }
void mon_set_screen(int cols, int page_rows)
{
    s_cols = cols;
    s_page = page_rows;
}
static int screen_wide(void) { return s_narrow && (s_cols >= 80); }

/* A byte as the C64's upper case/graphics set shows it, in the characters the overlay has: letters,
 * digits and punctuation as themselves, the shifted letters (0xC1..0xDA) as lower case, the rest '.' */
static char petscii_char(int v)
{
    if (((v >= 0x20) && (v <= 0x5B)) || (v == 0x5D)) {
        return (char)v;
    }
    if ((v >= 0xC1) && (v <= 0xDA)) {
        return (char)(v - 0xC1 + 'a');
    }
    if (v == 0xA0) {
        return ' ';
    }
    return '.';
}

/* Characters a line shows: the span marks print nothing of their own */
static int visible_len(const char *p, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (p[i] == SPAN_START) {
            while ((i < n) && (p[i] != SPAN_SEP)) {
                i++;
            }
        } else if ((p[i] != SPAN_SEP) && (p[i] != SPAN_END)) {
            v++;
        }
    }
    return v;
}

void mon_init(void) { s_quit = 0; mach_cmd_hook = obs_cmd_run; }

/* The landing line of a step: TRX64's land_line, with the flow tag " [irq]" while the stepping verbs know the
 * CPU is inside an interrupt frame. */
static void land_line(sbuf_t *out, const char *tag, uint64_t cyc, const char *why)
{
    hw_cpu_t c;
    mach_cpu(&c);
    uint16_t pc = mach_pc(&c);
    dis_line(rd_lens, (void *)"cpu", pc, out, 1, "cpu");
    if (mach_flow_current() != FL_MAIN) {
        sb_put(out, " [");
        sb_put(out, mach_flow_tag(mach_flow_current()));
        sb_putc(out, ']');
    }
    sb_put(out, " (");
    sb_put(out, tag);
    sb_put(out, ", ");
    sb_dec(out, cyc);
    sb_put(out, " cyc");
    sb_put(out, why);
    sb_putc(out, ')');
    if (!M.stop.at_ib) {
        sb_put(out, "\n  mid-instruction: the stop is not at an instruction boundary (z completes it)");
    }
    M.disasm_cursor = pc;
    M.disasm_cursor_set = 1;
}

static void help(sbuf_t *out)
{
    sb_put(out,
           "monitor (VICE-superset) on TRX64 Ultimate hardware (trxmon):\n"
           "  EXEC\n"
           "    g [addr]         go/resume the run-loop (from addr); Pause halts\n"
           "    x                exit/resume (= g)\n"
           "    until <addr>     run until PC=addr, then stop (synchronous)\n"
           "    z / step [n]     step into — may enter IRQ/NMI (VICE-correct)\n"
           "    n / next         step over — skips JSR\n"
           "    ret / return     run until current frame returns (RTS/RTI)\n"
           "    cs [n]           step n phi2 cycles (stops mid-instruction)\n"
           "    pause | run      stop at the next instruction boundary / resume\n"
           "  MEMORY (bank lens: cpu|ram|rom|io|cart, default cpu = what CPU sees)\n"
           "    m [lens] <a> [b] memory dump ($20/row + ascii; default len $800; the screen:\n"
           "                     16/row + PETSCII, one page, `m` alone goes on)\n"
           "    d [lens] [a] [end] disassemble: a..end range (VICE), or ~16 from a/PC\n"
           "    screen           decode the 40x25 text screen (real screen pointer)\n"
           "    io [1|addr]      I/O area per device: register hex (peek)\n"
           "    bank [lens]      show/set the sticky default lens for m/d\n"
           "    wr [lens] <a> <b..>  write exactly these bytes from a\n"
           "    f <a> <b> <d..>  fill range a..b with repeating data\n"
           "    t <a> <b> <dst>  move/copy a..b to dst (overlap-safe)\n"
           "    c <a> <b> <dst>  compare a..b vs dst (list diffs)\n"
           "    h <a> <b> <d..>  hunt for a byte pattern (xx = wildcard)\n"
           "    a <a> [instr]    assemble a line; `a <a>` = assemble mode (empty line ends)\n"
           "    asm <a> [<<TAG]  assemble a block (labels, = * < > + -, .byte .word), `end`\n"
           "  BREAKPOINTS / OBSERVERS\n"
           "    bk               list breakpoints (#num $addr)\n"
           "    bk <a> | bk -<a> set / remove breakpoint (by addr)\n"
           "    del <n..> | del  delete by #num / delete all\n"
           "    obs <name> when exec|load|store <a[..b]> [if <cond>] do <action>\n"
           "                     <action>: break | log [a x y sp pc fl $addr[:w]] |\n"
           "                     cmd \"<monitor cmds; ...>\"; <cond>: a x y pc sp fl rl\n"
           "                     val addr cy, numbers hex (`$a` for the number a, `a` is\n"
           "                     the register) == != < > <= >= && || ( )\n"
           "    obs              list observers (hw: what the unit holds, soft: the rest)\n"
           "    obs <name> on|off|del   (a name with * is a glob)   obs log: the log\n"
           "    ignore <name> [n]  skip the next n hits\n"
           "    rb <line> [x]    stop at raster line (x selects the cycle x/8) | rb -\n"
           "  CPU\n"
           "    r                registers (+ IRQ/NMI vectors, counters, the last stop)\n"
           "    r a=$42 pc=c000  set registers (a x y sp fl pc; at an instruction boundary)\n"
           "    bt               backtrace: JSR return addresses on the stack (best effort)\n"
           "  FILES (the Ultimate's file system; cwd /USB1)\n"
           "    pwd | cd <dir> | ls [dir] | mkdir <dir> | rmdir <dir>\n"
           "    load \"f\" [a] | save \"f\" a1 a2   PRG;  bload \"f\" a | bsave \"f\" a1 a2  raw\n"
           "    asm-file <path> [a]   assemble a source file (asm syntax)\n"
           "    r a=$42 x=.. pc=..  set registers (at an instruction boundary; pc redirects)\n"
           "    sidefx [on|off]  monitor read side effects (default off = peek)\n"
           "    device [c64|drive8]  the CPU r/m/d (and z, bk) act on\n"
           "  SESSION\n"
           "    status           running/paused, the last stop, armed breakpoints and units\n"
           "    model            which C64 this is\n"
           "    /quit            end trxmon (the machine stays as it is)\n"
           "  REWIND (the T20 frame ring: one anchor per frame start)\n"
           "    rstep [n]        reverse step n instructions (replayed from the frame start)\n"
           "    rewind           transport status and the ring's range\n"
           "    frame [-N|+N]    move N anchors (forward at the head runs N frames)\n"
           "    goto <frame|c<cycle>|mark>  land on an anchor\n"
           "    mark <name> | marks | unmark <name>\n"
           "  TRACE (the instruction trace: the last second of the machine, engine stream 3)\n"
           "    trace [on|off|status]   the trace gate (it runs with the ring)\n"
           "    chis [cycles] | chis <c1> <c2>   cpu history: PC, opcode, registers after\n"
           "    whowrote <addr> [n]     the last n writers of an address (PC, cycle, values)\n"
           "    triage [pc]             causal chain to a wild PC: RTS/RTI/JMP, stack slot\n"
           "    revdepth                the trace's depth (fixed: 1 s)\n"
           "  NOT AVAILABLE ON THIS HOST (C64 Ultimate hardware, trxmon)\n"
           "    obs ... do mark|trace\n"
           "    df, sd, iec, drive, cart,\n"
           "    georam, uci, folder, bitmap, reset, power, warp, turbo,\n"
           "    mount, eject, swapcrt, savecrt\n"
           "    tracedb, traceindex, map, taint, swimlane (analyses on the host:\n"
           "    a trace export to C64RE comes with TODO T66); diff, play, cadence, window,\n"
           "    rawframe\n"
           "  Media, machine and input: the Ultimate's REST API (/v1/...), not trxmon.");
}

/* ---- r ----------------------------------------------------------------- */

/* r a=$42 x=10 pc=c000 fl=..: TRX64's set (verbs.rs), one CPU write for all */
static int verb_r_set(toks_t *k, sbuf_t *out)
{
    hw_regs_t v = { 0, 0, 0, 0, 0 };
    int mask = 0;
    uint16_t pc = 0;
    char done[256];
    sbuf_t d;
    sb_init(&d, done, sizeof(done));
    for (int i = 1; i < k->n; i++) {
        const char *t = k->t[i];
        int eq = -1;
        for (int j = 0; t[j]; j++) {
            if (t[j] == '=') {
                eq = j;
                break;
            }
        }
        if (eq < 0) {
            continue;
        }
        if (d.len) {
            sb_putc(&d, ' ');
        }
        uint32_t x;
        if (!m_parse_hex(t + eq + 1, -1, &x)) {
            sb_put(&d, "bad ");
            sb_put(&d, t);
            continue;
        }
        char reg[8];
        int n = eq < 7 ? eq : 7;
        for (int j = 0; j < n; j++) {
            reg[j] = m_lower(t[j]);
        }
        reg[n] = 0;
        if (m_streq(reg, "a") || m_streq(reg, "ac")) {
            v.a = (uint8_t)x;
            mask |= W_A;
            sb_put(&d, "a=$");
            sb_hex(&d, (uint8_t)x, 2);
        } else if (m_streq(reg, "x") || m_streq(reg, "xr")) {
            v.x = (uint8_t)x;
            mask |= W_X;
            sb_put(&d, "x=$");
            sb_hex(&d, (uint8_t)x, 2);
        } else if (m_streq(reg, "y") || m_streq(reg, "yr")) {
            v.y = (uint8_t)x;
            mask |= W_Y;
            sb_put(&d, "y=$");
            sb_hex(&d, (uint8_t)x, 2);
        } else if (m_streq(reg, "sp")) {
            v.s = (uint8_t)x;
            mask |= W_S;
            sb_put(&d, "sp=$");
            sb_hex(&d, (uint8_t)x, 2);
        } else if (m_streq(reg, "pc")) {
            pc = (uint16_t)x;
            mask |= W_PC;
            sb_put(&d, "pc=$");
            sb_hex(&d, (uint16_t)x, 4);
        } else if (m_streq(reg, "p") || m_streq(reg, "fl") || m_streq(reg, "flags")) {
            v.p = (uint8_t)x;
            mask |= W_P;
            sb_put(&d, "fl=$");
            sb_hex(&d, (uint8_t)x, 2);
        } else {
            sb_put(&d, "unknown reg '");
            sb_put(&d, reg);
            sb_putc(&d, '\'');
        }
    }
    if (mask) {
        const char *e = mach_cpu_write(mask, &v, pc);
        if (e) {
            err(out, "r: ", e);
            return MON_ERR;
        }
        if (mask & W_PC) {
            M.disasm_cursor = pc;
            M.disasm_cursor_set = 1;
        }
    }
    sb_put(out, "set ");
    sb_put(out, sb_str(&d));
    return MON_OK;
}

static int verb_r(toks_t *k, sbuf_t *out)
{
    for (int i = 1; i < k->n; i++) {
        for (const char *p = k->t[i]; *p; p++) {
            if (*p == '=') {
                return verb_r_set(k, out);
            }
        }
    }
    hw_cpu_t c;
    mach_cpu(&c);
    uint16_t pc = mach_pc(&c);
    sb_put(out, "  ADDR AC XR YR SP NV-BDIZC  flow\n.;");
    char t[6];
    sbuf_t s;
    sb_init(&s, t, sizeof(t));
    sb_hex(&s, pc, 4);
    span_mark(out, t, 4, pc, ROLE_PC, 0, 1);
    sb_putc(out, ' ');
    sb_hex(out, c.a, 2);
    sb_putc(out, ' ');
    sb_hex(out, c.x, 2);
    sb_putc(out, ' ');
    sb_hex(out, c.y, 2);
    sb_putc(out, ' ');
    sb_hex(out, c.s, 2);
    sb_putc(out, ' ');
    flags_str(out, c.p);
    sb_put(out, "  ");
    sb_put(out, mach_flow_tag(mach_flow_current()));
    sb_put(out, "\n  port  $00=$");
    sb_hex(out, c.ddr, 2);
    sb_put(out, " $01=$");
    sb_hex(out, c.latch, 2);
    sb_put(out, "  LORAM=");
    sb_dec(out, c.latch & 1);
    sb_put(out, " HIRAM=");
    sb_dec(out, (c.latch >> 1) & 1);
    sb_put(out, " CHAREN=");
    sb_dec(out, (c.latch >> 2) & 1);
    sb_put(out, "\n  vectors  IRQ hw=");
    span_addr4(out, w16(0xFFFE), ROLE_TARGET);
    sb_put(out, "  CINV ");
    span_addr4(out, 0x0314, ROLE_MEMORY);
    sb_put(out, "->");
    span_addr4(out, w16(0x0314), ROLE_TARGET);
    sb_put(out, "     NMI hw=");
    span_addr4(out, w16(0xFFFA), ROLE_TARGET);
    sb_put(out, "  NMIV ");
    span_addr4(out, 0x0318, ROLE_MEMORY);
    sb_put(out, "->");
    span_addr4(out, w16(0x0318), ROLE_TARGET);
    sb_put(out, "\n  cycles ");
    sb_dec(out, mach_cycles());
    sb_put(out, "  instrs ");
    sb_dec(out, hw_instrs());
    if (M.running) {
        sb_put(out, "  running");
    } else if (M.have_stop) {
        sb_put(out, "  stop ");
        sb_put(out, M.stop.reason);
    }
    if (!c.at_ib && !M.running) {
        sb_put(out, "\n  mid-instruction: ");
        dis_line(rd_lens, (void *)"cpu", pc, out, 0, 0);
        sb_put(out, " (z completes it)");
    }
    return MON_OK;
}

/* ---- memory ------------------------------------------------------------ */

static int verb_m(toks_t *k, sbuf_t *out)
{
    int i = 1;
    const char *lens = lens_of(k->n > 1 ? k->t[1] : 0);
    if (lens) {
        i++;
    } else {
        lens = M.lens;
    }
    if (M.device) {
        lens = "drive8"; /* the drive has no lens */
    }
    uint16_t start = M.mem_cursor_set ? M.mem_cursor : 0, end;
    if (i < k->n && !addr_of(k->t[i], &start)) {
        err(out, "m: bad address '", k->t[i]);
        sb_putc(out, '\'');
        return MON_ERR;
    }
    const int wide = screen_wide();
    const int per = wide ? 16 : s_narrow ? 8 : 32;
    uint32_t e = (uint32_t)start + 0x7FF;
    if (s_narrow && (s_page > 0)) {
        /* on the screen: one page, from the row the start is in; `m` alone goes on from here */
        e = (start & ~(uint32_t)(per - 1)) + (uint32_t)s_page * (uint32_t)per - 1;
    }
    if (e > 0xFFFF) {
        e = 0xFFFF;
    }
    end = (uint16_t)e;
    if ((i + 1 < k->n) && !addr_of(k->t[i + 1], &end)) {
        end = (uint16_t)e;
    }
    char lens_letter = m_streq(lens, "cpu") ? 'C' : (char)(lens[0] - 32);
    uint32_t a = start & ~(uint32_t)(per - 1);
    int first = 1;
    while (a <= end) {
        if (!first) {
            sb_putc(out, '\n');
        }
        first = 0;
        char ascii[33];
        int row_len = 0;
        int col0 = out->len;
        sb_putc(out, '>');
        sb_putc(out, lens_letter);
        sb_putc(out, ':');
        /* the row address, marked after we know the row length: reserve */
        int mark_at = out->len;
        (void)mark_at;
        char hexbuf[100];
        sbuf_t hb;
        sb_init(&hb, hexbuf, sizeof(hexbuf));
        for (uint32_t j = 0; j < (uint32_t)per; j++) {
            uint32_t aj = a + j;
            if (aj > end) {
                break;
            }
            int v = mach_peek(lens, (uint16_t)aj, M.sidefx);
            if (j) {
                sb_putc(&hb, ' ');
            }
            if (v < 0) {
                sb_put(&hb, "--");
                ascii[row_len] = '.';
            } else {
                sb_hex(&hb, (uint32_t)v, 2);
                ascii[row_len] = wide ? petscii_char(v) : ((v >= 0x20) && (v < 0x7F)) ? (char)v : '.';
            }
            row_len++;
        }
        ascii[row_len] = 0;
        char t[6];
        sbuf_t s;
        sb_init(&s, t, sizeof(t));
        sb_hex(&s, a & 0xFFFF, 4);
        span_mark(out, t, 4, (uint16_t)a, ROLE_MEMORY, lens, row_len);
        sb_put(out, (s_narrow && !wide) ? " " : "  ");
        sb_putn(out, hexbuf, hb.len);
        sb_spaces(out, (wide ? 47 : s_narrow ? 23 : 96) - hb.len);
        sb_put(out, (s_narrow && !wide) ? " " : "  ");
        sb_put(out, ascii);
        (void)col0;
        if (out->full) {
            break;
        }
        a += (uint32_t)per;
    }
    M.mem_cursor = (uint16_t)(end + 1);
    M.mem_cursor_set = 1;
    return MON_OK;
}

static int verb_d(toks_t *k, sbuf_t *out)
{
    int i = 1;
    const char *lens = lens_of(k->n > 1 ? k->t[1] : 0);
    if (lens) {
        i++;
    } else {
        lens = M.lens;
    }
    hw_cpu_t c;
    mach_cpu(&c);
    uint16_t pc = mach_pc(&c);
    if (M.device) {
        lens = "drive8";
        pc = (uint16_t)(hw_drv_stat() >> 16);
    }
    uint16_t start = M.disasm_cursor_set ? M.disasm_cursor : pc;
    if (i < k->n) {
        addr_of(k->t[i], &start);
    }
    int ranged = 0;
    uint16_t end = 0;
    if (i + 1 < k->n) {
        if (!addr_of(k->t[i + 1], &end)) {
            err(out, "d: bad end address", 0);
            return MON_ERR;
        }
        if (end < start) {
            sb_put(out, "d: end $");
            sb_hex(out, end, 4);
            sb_put(out, " < start $");
            sb_hex(out, start, 4);
            return MON_ERR;
        }
        ranged = 1;
    }
    uint16_t a = start;
    int n = 0;
    const int wide = screen_wide();
    const int rows = (s_narrow && (s_page > 0)) ? s_page : 16;
    const char lens_letter = m_streq(lens, "cpu") ? 'C' : (char)(lens[0] - 32);
    for (;;) {
        if (ranged ? (a > end) : (n >= rows)) {
            break;
        }
        if (n) {
            sb_putc(out, '\n');
        }
        int lstart = out->len;
        int size;
        if (wide) {
            /* ".C:c005  4c 00 c0  JMP $c000", the PC's line starts with '>' instead of '.' */
            char pre[4] = { (a == pc) ? '>' : '.', lens_letter, ':', 0 };
            dis_set_label(pre);
            size = dis_line(rd_lens, (void *)lens, a, out, 1, lens);
            dis_set_label(0);
        } else {
            size = dis_line(rd_lens, (void *)lens, a, out, 1, lens);
            if (a == pc) {
                sb_put(out, " <-- PC");
            }
        }
        if (s_narrow && !wide && (visible_len(out->p + lstart, out->len - lstart) > 40)) {
            /* short of room: the raw bytes column goes */
            out->len = lstart;
            dis_set_raw(0);
            dis_line(rd_lens, (void *)lens, a, out, 1, lens);
            dis_set_raw(1);
            if (a == pc) {
                sb_put(out, " <-- PC");
            }
        }
        if (out->cap - out->len < 200) {
            out->len = lstart;
            sb_put(out, "… (truncated at $");
            sb_hex(out, a, 4);
            sb_put(out, " — `d $");
            sb_hex(out, a, 4);
            sb_put(out, " $");
            sb_hex(out, end, 4);
            sb_put(out, "` to continue)");
            break;
        }
        uint16_t na = (uint16_t)(a + size);
        n++;
        if (na < a) {
            a = na;
            break; /* wrapped past $FFFF */
        }
        a = na;
    }
    M.disasm_cursor = a;
    M.disasm_cursor_set = 1;
    return MON_OK;
}

static int verb_wr(toks_t *k, sbuf_t *out)
{
    int i = 1;
    const char *lens = lens_of(k->n > 1 ? k->t[1] : 0);
    if (lens) {
        i++;
    } else {
        lens = M.lens;
    }
    uint16_t a;
    if ((i >= k->n) || !addr_of(k->t[i], &a)) {
        err(out, "wr: usage: wr [lens] <addr> <byte..>", 0);
        return MON_ERR;
    }
    uint8_t b[MAXTOK];
    int n = 0;
    for (i++; i < k->n; i++) {
        if (!byte_of(k->t[i], &b[n++])) {
            err(out, "wr: need >=1 byte value ($00-$FF)", 0);
            return MON_ERR;
        }
    }
    if (!n) {
        err(out, "wr: need >=1 byte value ($00-$FF)", 0);
        return MON_ERR;
    }
    const char *e = mach_write(lens, a, b, n);
    if (e) {
        err(out, "wr: ", e);
        return MON_ERR;
    }
    sb_put(out, "wrote ");
    sb_dec(out, (uint64_t)n);
    sb_put(out, " byte(s) @ $");
    sb_hex(out, a, 4);
    sb_put(out, " (");
    sb_put(out, lens);
    sb_putc(out, ')');
    return MON_OK;
}

static int verb_f(toks_t *k, sbuf_t *out)
{
    uint16_t s, e;
    if ((k->n < 3) || !addr_of(k->t[1], &s) || !addr_of(k->t[2], &e)) {
        err(out, "f: usage: f <start> <end> <byte..>", 0);
        return MON_ERR;
    }
    uint8_t d[MAXTOK];
    int nd = 0;
    for (int i = 3; i < k->n; i++) {
        if (!byte_of(k->t[i], &d[nd++])) {
            nd = 0;
            break;
        }
    }
    if (!nd) {
        err(out, "f: need >=1 fill byte", 0);
        return MON_ERR;
    }
    uint32_t n = 0;
    for (uint32_t a = s; a <= e; a++, n++) {
        const char *x = mach_write("cpu", (uint16_t)a, &d[n % (uint32_t)nd], 1);
        if (x) {
            err(out, "f: ", x);
            return MON_ERR;
        }
    }
    sb_put(out, "filled $");
    sb_hex(out, s, 4);
    sb_put(out, "..$");
    sb_hex(out, e, 4);
    sb_put(out, " (");
    sb_dec(out, n);
    sb_put(out, " bytes, pattern ");
    sb_dec(out, (uint64_t)nd);
    sb_putc(out, ')');
    return MON_OK;
}

static uint8_t s_move[65536];

static int verb_t(toks_t *k, sbuf_t *out)
{
    uint16_t s, e, d;
    if ((k->n < 4) || !addr_of(k->t[1], &s) || !addr_of(k->t[2], &e) || !addr_of(k->t[3], &d)) {
        err(out, "t: usage: t <start> <end> <dest>", 0);
        return MON_ERR;
    }
    int32_t len = (int32_t)e - (int32_t)s + 1;
    if (len <= 0) {
        err(out, "t: end < start", 0);
        return MON_ERR;
    }
    for (int32_t i = 0; i < len; i++) {
        int v = mach_peek("cpu", (uint16_t)(s + i), M.sidefx);
        s_move[i] = (v < 0) ? 0 : (uint8_t)v;
    }
    for (int32_t i = 0; i < len; i++) {
        const char *x = mach_write("cpu", (uint16_t)(d + i), &s_move[i], 1);
        if (x) {
            err(out, "t: ", x);
            return MON_ERR;
        }
    }
    sb_put(out, "moved ");
    sb_dec(out, (uint64_t)len);
    sb_put(out, " byte(s) $");
    sb_hex(out, s, 4);
    sb_put(out, "..$");
    sb_hex(out, e, 4);
    sb_put(out, " -> $");
    sb_hex(out, d, 4);
    return MON_OK;
}

static int verb_c(toks_t *k, sbuf_t *out)
{
    uint16_t s, e, d;
    if ((k->n < 4) || !addr_of(k->t[1], &s) || !addr_of(k->t[2], &e) || !addr_of(k->t[3], &d)) {
        err(out, "c: usage: c <start> <end> <dest>", 0);
        return MON_ERR;
    }
    int32_t len = (int32_t)e - (int32_t)s + 1;
    if (len <= 0) {
        err(out, "c: end < start", 0);
        return MON_ERR;
    }
    int nd = 0;
    for (int32_t i = 0; i < len; i++) {
        uint8_t av = mach_peek8("cpu", (uint16_t)(s + i)), bv = mach_peek8("cpu", (uint16_t)(d + i));
        if (av != bv) {
            sb_put(out, nd ? "\n  $" : "differences:\n  $");
            sb_hex(out, (uint16_t)(s + i), 4);
            sb_put(out, ": ");
            sb_hex(out, av, 2);
            sb_put(out, " != ");
            sb_hex(out, bv, 2);
            sb_put(out, " @$");
            sb_hex(out, (uint16_t)(d + i), 4);
            if (++nd > 64) {
                sb_put(out, "\n  ... (truncated)");
                break;
            }
        }
    }
    if (!nd) {
        sb_put(out, "identical ($");
        sb_hex(out, s, 4);
        sb_put(out, "..$");
        sb_hex(out, e, 4);
        sb_put(out, " == $");
        sb_hex(out, d, 4);
        sb_putc(out, ')');
    }
    return MON_OK;
}

static int verb_h(toks_t *k, sbuf_t *out)
{
    uint16_t s, e;
    int pat[MAXTOK];
    int np = 0, bad = (k->n < 4);
    if ((k->n < 3) || !addr_of(k->t[1], &s) || !addr_of(k->t[2], &e)) {
        err(out, "h: usage: h <start> <end> <byte/xx..>", 0);
        return MON_ERR;
    }
    for (int i = 3; i < k->n; i++) {
        uint8_t b;
        if (m_strieq(k->t[i], "xx") || m_streq(k->t[i], "*")) {
            pat[np++] = -1;
        } else if (byte_of(k->t[i], &b)) {
            pat[np++] = b;
        } else {
            bad = 1;
        }
    }
    if (!np || bad) {
        err(out, "h: need >=1 pattern byte (xx = wildcard)", 0);
        return MON_ERR;
    }
    int hits = 0;
    char list[2048];
    sbuf_t hl;
    sb_init(&hl, list, sizeof(list));
    for (int32_t a = s; a + np - 1 <= (int32_t)e; a++) {
        int m = 1;
        for (int j = 0; j < np && m; j++) {
            if ((pat[j] >= 0) && (mach_peek8("cpu", (uint16_t)(a + j)) != pat[j])) {
                m = 0;
            }
        }
        if (m) {
            if (hits) {
                sb_putc(&hl, ' ');
            }
            sb_putc(&hl, '$');
            sb_hex(&hl, (uint16_t)a, 4);
            if (++hits > 256) {
                break;
            }
        }
    }
    if (!hits) {
        sb_put(out, "not found");
    } else {
        sb_put(out, "found ");
        sb_dec(out, (uint64_t)hits);
        sb_put(out, ":\n  ");
        sb_putn(out, list, hl.len);
    }
    return MON_OK;
}

static int verb_screen(sbuf_t *out)
{
    uint16_t bank = mach_vic_bank();
    uint8_t d018 = mach_vic_reg(0x18);
    uint16_t base = (uint16_t)(bank + ((d018 >> 4) & 15) * 0x400);
    sb_put(out, "screen @ $");
    sb_hex(out, base, 4);
    sb_put(out, "  (VIC bank $");
    sb_hex(out, bank, 4);
    sb_put(out, ", $D018=$");
    sb_hex(out, d018, 2);
    sb_putc(out, ')');
    uint8_t row[40];
    for (int r = 0; r < 25; r++) {
        hw_ram_read((uint16_t)(base + r * 40), row, 40);
        sb_put(out, "\n|");
        for (int c = 0; c < 40; c++) {
            uint8_t sc = row[c] & 0x7F;
            char ch = (sc == 0) ? '@' : (sc <= 26) ? (char)(64 + sc) : (sc == 32) ? ' ' : (sc <= 63) ? (char)sc : '.';
            sb_putc(out, ch);
        }
        sb_putc(out, '|');
    }
    return MON_OK;
}

static int yn(sbuf_t *out, int b);

static int verb_io(toks_t *k, sbuf_t *out)
{
    static const struct
    {
        const char *name;
        uint16_t start, end;
    } blocks[] = {
        { "VIC-II", 0xD000, 0xD03F },
        { "SID", 0xD400, 0xD41F },
        { "CIA1", 0xDC00, 0xDC0F },
        { "CIA2", 0xDD00, 0xDD0F },
    };
    const char *arg = (k->n > 1) ? k->t[1] : 0;
    if (arg && m_streq(arg, "1541")) {
        /* drive A: what drv_dbg tells (the fetch address, the cycle count, the hold). The VIAs are not on the state
         * bus (ARCH 0004), so there are no register values here. */
        uint32_t st = hw_drv_stat();
        sb_put(out, "1541 (drive A):\n  last opcode fetch $");
        sb_hex(out, st >> 16, 4);
        sb_put(out, "  drive cycles ");
        sb_dec(out, hw_drv_cycles());
        sb_put(out, "\n  halted ");
        yn(out, st & 1);
        sb_put(out, "  held by the debugger ");
        yn(out, st & 2);
        sb_put(out, "  stepping ");
        yn(out, st & 4);
        sb_put(out, "  comparator ");
        if (st & 8) {
            sb_put(out, "on ($");
            sb_hex(out, M.drv_bp, 4);
            sb_putc(out, ')');
        } else {
            sb_put(out, "off");
        }
        sb_put(out, "\n  VIA 1 ($1800) and VIA 2 ($1C00): not on the state bus - no register values (ARCH 0004)");
        return MON_OK;
    }
    int details = (arg != 0);
    int filter = 0;
    uint16_t f = 0;
    if (arg && !m_streq(arg, "1")) {
        if (!addr_of(arg, &f)) {
            err(out, "io: usage: io [1 | <addr>]", 0);
            return MON_ERR;
        }
        filter = 1;
    }
    int any = 0;
    for (int b = 0; b < 4; b++) {
        uint16_t page = blocks[b].start & 0xFF00;
        if (filter && ((f < page) || (f > (page | 0xFF)))) {
            continue;
        }
        if (any) {
            sb_put(out, "\n\n");
        }
        any = 1;
        sb_put(out, blocks[b].name);
        sb_putc(out, ':');
        for (uint32_t row = blocks[b].start; row <= blocks[b].end; row += 16) {
            sb_put(out, "\n  ");
            sb_hex(out, row, 4);
            sb_putc(out, ' ');
            for (uint32_t i = 0; (i < 16) && (row + i <= blocks[b].end); i++) {
                int v = mach_peek("io", (uint16_t)(row + i), 0);
                sb_putc(out, ' ');
                if (v < 0) {
                    sb_put(out, "--");
                } else {
                    sb_hex(out, (uint32_t)v, 2);
                }
            }
        }
        if (details) {
            sb_put(out, "\n  No details available.");
        }
        if (b == 1) {
            sb_put(out, "\n  (SID: not on the state bus; `sidefx on` while paused reads it with bus cycles)");
        }
    }
    if (!any) {
        sb_put(out, "io: no device at $");
        sb_hex(out, f, 4);
        return MON_ERR;
    }
    return MON_OK;
}

/* ---- breakpoints, observers ------------------------------------------- */

static int verb_bk(toks_t *k, sbuf_t *out)
{
    if (k->n < 2) {
        if (!M.nbp) {
            sb_put(out, "no breakpoints (set: bk <addr>)");
            return MON_OK;
        }
        sb_put(out, "breakpoints:");
        for (int i = 0; i < M.nbp; i++) {
            sb_put(out, "\n  #");
            sb_dec(out, M.bp[i].num);
            sb_put(out, "  ");
            span_addr4(out, M.bp[i].pc, ROLE_PC);
        }
        return MON_OK;
    }
    const char *t = k->t[1];
    uint16_t a;
    if (m_strieq(t, "clear")) {
        mach_bp_clear();
        sb_put(out, "breakpoints cleared");
        return MON_OK;
    }
    if (t[0] == '-') {
        if (!addr_of(t + 1, &a)) {
            err(out, "bad address: ", t);
            return MON_ERR;
        }
        mach_bp_del_addr(a);
        sb_put(out, "removed bp $");
        sb_hex(out, a, 4);
        sb_put(out, " (");
        sb_dec(out, (uint64_t)M.nbp);
        sb_put(out, " left)");
        return MON_OK;
    }
    if (!addr_of(t, &a)) {
        err(out, "bad address: ", t);
        return MON_ERR;
    }
    int num = mach_bp_add(a);
    if (num < 0) {
        err(out, "bk: too many breakpoints", 0);
        return MON_ERR;
    }
    sb_put(out, "bk #");
    sb_dec(out, (uint64_t)num);
    sb_put(out, " set at ");
    span_addr4(out, a, ROLE_PC);
    sb_put(out, " (");
    sb_dec(out, (uint64_t)M.nbp);
    sb_put(out, " total)");
    return MON_OK;
}

static int verb_del(toks_t *k, sbuf_t *out)
{
    if (k->n < 2) {
        mach_bp_clear();
        sb_put(out, "all breakpoints deleted");
        return MON_OK;
    }
    for (int i = 1; i < k->n; i++) {
        uint32_t num;
        if (i > 1) {
            sb_putc(out, '\n');
        }
        if (!m_parse_dec(k->t[i], -1, &num)) {
            err(out, "bad checknum: ", k->t[i]);
        } else if (mach_bp_del_num(num)) {
            sb_put(out, "deleted #");
            sb_dec(out, num);
        } else {
            sb_put(out, "no breakpoint #");
            sb_dec(out, num);
        }
    }
    return MON_OK;
}

static void do_text(sbuf_t *out, const obs_t *o)
{
    if (o->act == OA_CMD) {
        sb_put(out, "cmd \"");
        sb_put(out, o->cmd);
        sb_putc(out, '"');
    } else if (o->act == OA_LOG) {
        sb_put(out, "log");
        for (int i = 0; i < o->nlogf; i++) {
            sb_putc(out, ' ');
            const logf_t *f = &o->logf[i];
            if (f->mem) {
                sb_putc(out, '$');
                sb_hex(out, f->addr, 1);
                if (f->word) {
                    sb_put(out, ":w");
                }
            } else {
                sb_put(out, f->reg == 'p' ? "pc" : f->reg == 's' ? "sp" : f->reg == 'f' ? "fl" : (const char[]){ f->reg, 0 });
            }
        }
    } else {
        sb_put(out, "break");
    }
}

static void obs_line(sbuf_t *out, const obs_t *o)
{
    sb_put(out, "\n  ");
    sb_put(out, o->on ? "*" : "o");
    sb_putc(out, ' ');
    sb_put(out, o->name);
    sb_put(out, "  ");
    if (o->trig == OBS_RASTER) {
        sb_put(out, "raster line $");
        sb_hex(out, o->lo >> 7, 3);
        if ((o->lo & 0x7F) || ((o->hi & 0x7F) != 127 && o->hi != o->lo)) {
            sb_put(out, " cycle ");
            sb_dec(out, o->lo & 0x7F);
        }
        if ((o->hi >> 7) != (o->lo >> 7)) {
            sb_put(out, "..$");
            sb_hex(out, o->hi >> 7, 3);
        }
    } else {
        sb_put(out, (o->trig == OBS_EXEC) ? "exec" : (o->trig == OBS_LOAD) ? "load" : "store");
        sb_put(out, " $");
        sb_hex(out, o->lo, 4);
        if (o->hi != o->lo) {
            sb_put(out, "..$");
            sb_hex(out, o->hi, 4);
        }
    }
    if (o->cond.root >= 0) {
        /* the parsed condition, not the typed text: the numbers read the same in every observer (hex) */
        char t[160];
        cond_fmt(&o->cond, o->cond.root, t, sizeof(t));
        sb_put(out, " if ");
        sb_put(out, t);
    }
    sb_put(out, " do ");
    do_text(out, o);
    /* the counts are hex like every number the monitor takes (ignore n) */
    sb_put(out, "  hits=");
    sb_hexmin(out, mach_obs_hits(o));
    if (o->ignore) {
        sb_put(out, " ignore=");
        sb_hexmin(out, o->ignore);
    }
    if (o->pass) {
        sb_put(out, " pass=");
        sb_hexmin(out, o->pass);
    }
    if (!o->on) {
        return;
    }
    if ((o->trig == OBS_EXEC) && (o->unit < 0)) {
        sb_put(out, "  hw: exec bitmap");
    } else if (o->unit >= 0) {
        sb_put(out, "  hw: watch unit ");
        sb_dec(out, (uint64_t)o->unit);
        sb_put(out, o->trig == OBS_RASTER ? " (raster)" : " (addr");
        if (o->trig != OBS_RASTER) {
            if (o->q.has_data) {
                sb_put(out, "+val");
            }
            if (o->q.reg) {
                sb_put(out, "+reg");
            }
            if (o->hw_log) {
                sb_put(out, "+log");
            }
            sb_putc(out, ')');
        }
    }
    int soft = o->q.nrest || (o->cond.root >= 0 && (o->trig == OBS_EXEC));
    if (o->cond.root >= 0 && o->trig == OBS_EXEC) {
        char t[64];
        cond_fmt(&o->cond, o->cond.root, t, sizeof(t));
        sb_put(out, ", soft: ");
        sb_put(out, t);
    } else if (soft) {
        sb_put(out, ", soft: ");
        for (int k = 0; k < o->q.nrest; k++) {
            char t[64];
            cond_fmt(&o->cond, o->q.rest[k], t, sizeof(t));
            sb_put(out, k ? " && " : "");
            sb_put(out, t);
        }
    }
    if (((o->act == OA_LOG) && !o->hw_log) || (o->act == OA_CMD)) {
        sb_put(out, soft ? ", " : ", soft: ");
        sb_put(out, o->act == OA_CMD ? "cmd at the stop" : "log at the stop");
    }
}

/* the monitor commands of a `do cmd` observer, run at its stop (T55): the output goes to the observer log. The
 * verbs that move the machine are refused: the machine runs on by itself afterwards. */
static void obs_cmd_run(const obs_t *o)
{
    static int busy;
    static char outbuf[1536];
    static const char *const deny[] = { "g", "x", "run", "z", "step", "si", "cs", "n", "next", "so", "ret", "return",
                                        "until", "pause", "quit", "obs", "o", "ignore", "rb", "device", "dev", "a",
                                        "asm", "goto", "frame", "rstep", "reverse", 0 };
    if (busy) {
        return;
    }
    busy = 1;
    char part[sizeof(o->cmd)];
    const char *p = o->cmd;
    while (*p) {
        int n = 0;
        while (*p && (*p != ';') && (n < (int)sizeof(part) - 1)) {
            part[n++] = *p++;
        }
        part[n] = 0;
        if (*p == ';') {
            p++;
        }
        const char *c = part;
        while (*c == ' ') {
            c++;
        }
        if (!*c) {
            continue;
        }
        char line[96];
        sbuf_t l;
        sb_init(&l, line, sizeof(line));
        sb_put(&l, "obs ");
        sb_put(&l, o->name);
        sb_put(&l, ": .");
        sb_put(&l, c);
        mach_olog_push(sb_str(&l));
        char verb[16];
        int vn = 0;
        while (c[vn] && (c[vn] != ' ') && (vn < (int)sizeof(verb) - 1)) {
            verb[vn] = m_lower(c[vn]);
            vn++;
        }
        verb[vn] = 0;
        sbuf_t out;
        sb_init(&out, outbuf, (int)sizeof(outbuf) - 1);
        mon_pending_t pend;
        if (is_one_of(verb, deny)) {
            sb_put(&out, "refused: a do cmd observer does not move the machine");
        } else {
            int was_narrow = s_narrow;
            s_narrow = 0;
            mon_exec(c, &out, &pend);
            s_narrow = was_narrow;
        }
        sb_str(&out);
        for (const char *q = outbuf; *q;) {
            char ln[96];
            sbuf_t w;
            sb_init(&w, ln, sizeof(ln));
            sb_put(&w, "  ");
            while (*q && (*q != '\n')) {
                if ((unsigned char)*q == SPAN_START) {
                    while (*q && ((unsigned char)*q != SPAN_SEP)) {
                        q++; /* the address mark's header; the text after it is the text */
                    }
                } else if (((unsigned char)*q >= 0x20) && ((unsigned char)*q < 0x7F)) {
                    sb_putc(&w, *q);
                }
                if (*q) {
                    q++;
                }
            }
            if (*q == '\n') {
                q++;
            }
            mach_olog_push(sb_str(&w));
        }
    }
    busy = 0;
}

static int parse_log_field(const char *t, logf_t *f)
{
    memset(f, 0, sizeof(*f));
    if (m_strieq(t, "a") || m_strieq(t, "x") || m_strieq(t, "y")) {
        f->reg = m_lower(t[0]);
    } else if (m_strieq(t, "sp")) {
        f->reg = 's';
    } else if (m_strieq(t, "pc")) {
        f->reg = 'p';
    } else if (m_strieq(t, "fl")) {
        f->reg = 'f';
    } else {
        int n = m_strlen(t);
        if ((n > 2) && (t[n - 2] == ':') && (m_lower(t[n - 1]) == 'w')) {
            f->word = 1;
            n -= 2;
        }
        uint32_t v;
        if (!m_parse_hex(t, n, &v)) {
            return 0;
        }
        f->mem = 1;
        f->addr = (uint16_t)v;
    }
    return 1;
}

static int verb_obs(toks_t *k, const char *cmd, sbuf_t *out)
{
    if (m_strieq(k->t[0], "ignore")) {
        if (k->n < 2) {
            err(out, "ignore: usage: ignore <name> [n]", 0);
            return MON_ERR;
        }
        int i = mach_obs_find(k->t[1]);
        uint32_t n = 1;
        if ((k->n > 2) && !m_parse_hex(k->t[2], -1, &n)) {
            err(out, "ignore: usage: ignore <name> [n]   (n in hex)", 0);
            return MON_ERR;
        }
        if (i < 0) {
            sb_put(out, "no observer '");
            sb_put(out, k->t[1]);
            sb_putc(out, '\'');
            return MON_OK;
        }
        if (!M.obs[i].on) {
            sb_put(out, "ignore ");
            sb_put(out, k->t[1]);
            sb_put(out, ": observer is off - `obs ");
            sb_put(out, k->t[1]);
            sb_put(out, " on` first, then set the count");
            return MON_OK;
        }
        const char *how = mach_obs_ignore(i, n);
        sb_put(out, "ignore ");
        sb_put(out, k->t[1]);
        sb_put(out, ": skip next ");
        sb_hexmin(out, n);
        sb_put(out, how[0] == 'h' ? " (counted by the watch unit)" : " (counted after each stop)");
        return MON_OK;
    }
    if (k->n < 2) {
        if (!M.nobs) {
            sb_put(out, "no observers (obs <name> when exec|load|store|raster <addr> [if <cond>] do break|log|cmd)");
            return MON_OK;
        }
        sb_put(out, "observers:");
        for (int i = 0; i < M.nobs; i++) {
            obs_line(out, &M.obs[i]);
        }
        return MON_OK;
    }
    if (m_strieq(k->t[1], "log")) {
        mach_olog_drain();
        if (!mach_olog_count()) {
            sb_put(out, "obs log: (empty)");
            return MON_OK;
        }
        for (int i = 0; i < mach_olog_count(); i++) {
            if (i) {
                sb_putc(out, '\n');
            }
            sb_put(out, mach_olog_line(i));
        }
        return MON_OK;
    }
    const char *name = k->t[1];
    const char *sub = (k->n > 2) ? k->t[2] : "";
    if (m_strieq(sub, "on") || m_strieq(sub, "off") || m_strieq(sub, "del") || m_strieq(sub, "rm")) {
        int found = 0;
        for (int i = 0; i < M.nobs;) {
            int match = m_streq(name, "*") || m_streq(M.obs[i].name, name);
            if (!match) {
                i++;
                continue;
            }
            found++;
            if (m_strieq(sub, "del") || m_strieq(sub, "rm")) {
                mach_obs_del(i);
                continue;
            }
            const char *e = mach_obs_set(i, m_strieq(sub, "on"));
            if (e) {
                err(out, "obs: ", e);
                return MON_ERR;
            }
            i++;
        }
        if (!found) {
            sb_put(out, "no observer '");
            sb_put(out, name);
            sb_putc(out, '\'');
            return MON_OK;
        }
        sb_put(out, "obs ");
        sb_put(out, name);
        sb_putc(out, ' ');
        sb_put(out, sub);
        return MON_OK;
    }
    /* obs <name> when exec|load|store <a[..b]> | raster <line[..line]>  [if <cond>] do break|log [fields]|cmd "..." */
    static const char usage[] =
        "obs: usage: obs <name> when exec|load|store <a[..b]> [if <cond>] do break|log [a x y sp pc fl $addr[:w]]|cmd \"<cmds>\"";
    if (!m_strieq(sub, "when") || (k->n < 5)) {
        err(out, usage, 0);
        return MON_ERR;
    }
    static obs_t proto; /* static: the monitor runs on the UI task's stack */
    memset(&proto, 0, sizeof(proto));
    int nm = m_strlen(name);
    if (nm > (int)sizeof(proto.name) - 1) {
        nm = (int)sizeof(proto.name) - 1;
    }
    memcpy(proto.name, name, (size_t)nm);
    int trig = m_strieq(k->t[3], "exec") ? OBS_EXEC : m_strieq(k->t[3], "load") ? OBS_LOAD
               : m_strieq(k->t[3], "store") ? OBS_STORE : m_strieq(k->t[3], "raster") ? OBS_RASTER : -1;
    if (trig < 0) {
        err(out, "obs: trigger must be exec|load|store|raster, got ", k->t[3]);
        return MON_ERR;
    }
    proto.trig = (uint8_t)trig;
    const char *r = k->t[4];
    const char *dots = r;
    while (*dots && !((dots[0] == '.') && (dots[1] == '.'))) {
        dots++;
    }
    uint32_t v, v2 = 0;
    if (!m_parse_hex(r, (int)(dots - r), &v) || (*dots && !m_parse_hex(dots + 2, -1, &v2))) {
        err(out, "obs: bad address ", r);
        return MON_ERR;
    }
    if (!*dots) {
        v2 = v;
    }
    if (v2 < v) {
        err(out, "obs: range end < start", 0);
        return MON_ERR;
    }
    if (trig == OBS_RASTER) {
        hw_video_t vd;
        hw_video(&vd);
        if (v2 >= vd.lines) {
            err(out, "obs: raster line out of range for this video mode", 0);
            return MON_ERR;
        }
        if (*dots) {
            err(out, "obs: a raster observer takes one line (a range would stop at every cycle of it)", 0);
            return MON_ERR;
        }
        proto.lo = proto.hi = (uint16_t)(v << 7); /* the first cycle of the line */
    } else {
        proto.lo = (uint16_t)v;
        proto.hi = (uint16_t)v2;
    }
    /* "if" .. "do": the condition; after "do": the action. A quoted command is cut off first: its words are no tokens */
    int di = -1, ii = -1;
    for (int i = 5; i < k->n; i++) {
        if ((ii < 0) && (di < 0) && m_strieq(k->t[i], "if")) {
            ii = i;
        } else if ((di < 0) && m_strieq(k->t[i], "do")) {
            di = i;
        }
    }
    if ((ii >= 0) && (di < 0)) {
        err(out, usage, 0);
        return MON_ERR;
    }
    if (ii >= 0) {
        sbuf_t cb;
        sb_init(&cb, proto.cond_src, (int)sizeof(proto.cond_src));
        for (int i = ii + 1; i < di; i++) {
            if (i > ii + 1) {
                sb_putc(&cb, ' ');
            }
            sb_put(&cb, k->t[i]);
        }
        sb_str(&cb);
        if (cb.full) {
            err(out, "obs: condition too long", 0);
            return MON_ERR;
        }
        static cond_t probe;
        const char *ce = cond_parse(proto.cond_src, &probe);
        if (ce) {
            err(out, "obs: condition: ", ce);
            return MON_ERR;
        }
    }
    proto.act = OA_BREAK;
    if (di >= 0) {
        const char *act = (di + 1 < k->n) ? k->t[di + 1] : "";
        if (m_strieq(act, "break")) {
            if (di + 2 < k->n) {
                err(out, "obs: 'break' takes no fields (got '", k->t[di + 2]);
                sb_putc(out, '\'');
                return MON_ERR;
            }
        } else if (m_strieq(act, "log")) {
            proto.act = OA_LOG;
            for (int i = di + 2; i < k->n; i++) {
                if ((proto.nlogf >= MAX_LOGF) || !parse_log_field(k->t[i], &proto.logf[proto.nlogf])) {
                    err(out, "obs: log: bad field '", k->t[i]);
                    sb_put(out, "' (use a/x/y/sp/pc/fl or $addr[:w], at most 6)");
                    return MON_ERR;
                }
                proto.nlogf++;
            }
        } else if (m_strieq(act, "cmd")) {
            proto.act = OA_CMD;
            const char *q = cmd;
            while (*q && (*q != '"')) {
                q++;
            }
            const char *e = *q ? q + 1 : q;
            while (*e && (*e != '"')) {
                e++;
            }
            if (!*q || (e - q - 1 <= 0) || (e - q - 1 >= (int)sizeof(proto.cmd))) {
                err(out, "obs: cmd: usage: ... do cmd \"<monitor command>[;<command>]\" (at most 63 characters)", 0);
                return MON_ERR;
            }
            memcpy(proto.cmd, q + 1, (size_t)(e - q - 1));
        } else {
            err(out, "obs: action must be break|log|cmd (mark and trace need T20/T66), got '", act);
            sb_putc(out, '\'');
            return MON_ERR;
        }
    }
    const char *e = mach_obs_add(&proto);
    if (e) {
        err(out, "obs: ", e);
        return MON_ERR;
    }
    sb_put(out, "obs ");
    sb_put(out, name);
    sb_put(out, " armed:");
    obs_line(out, &M.obs[M.nobs - 1]);
    return MON_OK;
}

/* rb <line> [x] | rb - | rb : the raster break (T21 §4.4, T55): an observer "rb" on a watch unit with the raster
 * source. The line is hex like every number here; x selects the cycle x/8 of the line. */
static int verb_rb(toks_t *k, sbuf_t *out)
{
    int i = mach_obs_find("rb");
    if (k->n < 2) {
        if (i < 0) {
            sb_put(out, "rb: none (rb <line> [x] sets one, rb - removes it)");
        } else {
            sb_put(out, "rb:");
            obs_line(out, &M.obs[i]);
        }
        return MON_OK;
    }
    if (m_streq(k->t[1], "-")) {
        if (i < 0) {
            sb_put(out, "rb: none");
        } else {
            mach_obs_del(i);
            sb_put(out, "rb removed");
        }
        return MON_OK;
    }
    uint32_t line, x = 0;
    hw_video_t vd;
    hw_video(&vd);
    if (!m_parse_hex(k->t[1], -1, &line) || ((k->n > 2) && !m_parse_hex(k->t[2], -1, &x))) {
        err(out, "rb: usage: rb <line> [x]  |  rb -", 0);
        return MON_ERR;
    }
    if (line >= vd.lines) {
        err(out, "rb: raster line out of range for this video mode (lines 0..", 0);
        sb_dec(out, (uint64_t)vd.lines - 1);
        sb_putc(out, ')');
        return MON_ERR;
    }
    uint32_t cyc = x >> 3;
    if (cyc >= vd.cycles_per_line) {
        err(out, "rb: x is beyond the line", 0);
        return MON_ERR;
    }
    if (i >= 0) {
        mach_obs_del(i);
    }
    static obs_t proto;
    memset(&proto, 0, sizeof(proto));
    memcpy(proto.name, "rb", 3);
    proto.trig = OBS_RASTER;
    proto.lo = proto.hi = (uint16_t)((line << 7) | cyc);
    const char *e = mach_obs_add(&proto);
    if (e) {
        err(out, "rb: ", e);
        return MON_ERR;
    }
    sb_put(out, "rb set:");
    obs_line(out, &M.obs[M.nobs - 1]);
    return MON_OK;
}

/* ---- flow, focus, sf, nf (TRX64 FlowTracker, T55) ------------------------------ */

static int verb_flow(sbuf_t *out)
{
    sb_put(out, "flow: current=");
    sb_put(out, mach_flow_tag(mach_flow_current()));
    sb_put(out, "  focus=");
    sb_put(out, M.focus);
    sb_put(out, "\nframes:");
    if (!M.nflow) {
        sb_put(out, "\n  (main \xe2\x80\x94 no interrupt/trap frame active)");
    }
    for (int i = 0; i < M.nflow; i++) {
        sb_put(out, "\n  ");
        sb_put(out, mach_flow_tag(M.flow[i].kind));
        sb_put(out, "  enter=");
        span_addr4(out, M.flow[i].pc, ROLE_PC);
        sb_put(out, " -> ret=");
        span_addr4(out, M.flow[i].ret, ROLE_PC);
        sb_put(out, "  cyc=");
        sb_dec(out, M.flow[i].cyc);
    }
    return MON_OK;
}

static int verb_focus(toks_t *k, sbuf_t *out)
{
    if (k->n < 2) {
        sb_put(out, "focus = ");
        sb_put(out, M.focus);
        sb_put(out, " (current flow: ");
        sb_put(out, mach_flow_tag(mach_flow_current()));
        sb_put(out, ")\nflow stack:");
        if (!M.nflow) {
            sb_put(out, "\n  (main \xe2\x80\x94 no interrupt/trap frame active)");
        }
        for (int i = 0; i < M.nflow; i++) {
            sb_put(out, "\n  ");
            sb_put(out, mach_flow_tag(M.flow[i].kind));
            sb_put(out, "  enter=");
            span_addr4(out, M.flow[i].pc, ROLE_PC);
            sb_put(out, " ret=");
            span_addr4(out, M.flow[i].ret, ROLE_PC);
        }
        return MON_OK;
    }
    static const char *const ok[] = { "auto", "main", "irq", "nmi", "brk", "none", "clear", 0 };
    char w[8];
    int n = m_strlen(k->t[1]);
    if (n > 7) {
        n = 7;
    }
    for (int i = 0; i < n; i++) {
        w[i] = m_lower(k->t[1][i]);
    }
    w[n] = 0;
    if (!is_one_of(w, ok)) {
        err(out, "focus: expected auto|main|irq|nmi|brk|clear, got '", k->t[1]);
        sb_putc(out, '\'');
        return MON_ERR;
    }
    const char *fv = m_streq(w, "clear") ? "none" : w;
    memcpy(M.focus, fv, (size_t)m_strlen(fv) + 1);
    sb_put(out, "focus = ");
    sb_put(out, M.focus);
    return MON_OK;
}

static int focus_kind(void)
{
    return m_streq(M.focus, "main") ? FL_MAIN : m_streq(M.focus, "irq") ? FL_IRQ : m_streq(M.focus, "nmi") ? FL_NMI
           : m_streq(M.focus, "brk") ? FL_BRK : mach_flow_current();
}

/* sf / nf: step (over calls for nf) until the CPU is in the focused flow. A walk is bounded: SF_STEPS steps or SF_MS. */
#define SF_STEPS 20000
#define SF_MS    3000

static int verb_sf(int over, sbuf_t *out)
{
    int want = focus_kind();
    if (M.running) {
        mach_pause();
    }
    uint32_t t0 = plat_ms();
    uint64_t c_last = 0;
    const char *why = "";
    int n;
    for (n = 0; n < SF_STEPS; n++) {
        hw_cpu_t c;
        mach_cpu(&c);
        uint16_t pc0 = mach_pc(&c);
        uint8_t s0 = c.s;
        int jsr = over && c.at_ib && (c.op == 0x20);
        uint64_t c0 = mach_cycles();
        if (mach_step_flow() < 0) {
            err(out, "sf: the machine did not stop (timeout)", 0);
            return MON_ERR;
        }
        c_last = M.stop.cycles - c0;
        if (jsr) {
            /* run the callee out, as `next` does */
            mach_arm_next((uint16_t)(pc0 + 3), s0);
            mach_run();
            int polled = 0;
            while (!polled && (plat_ms() - t0 < SF_MS)) {
                polled = mach_poll() == 1;
                if (!polled) {
                    plat_sleep(PLAT_SLEEP_MIN_MS);
                }
            }
            if (!polled) {
                mach_pause();
                why = ", CAP";
                break;
            }
        }
        hw_cpu_t c1;
        mach_cpu(&c1);
        if (mach_bp_at(mach_pc(&c1))) {
            why = ", hit user bp";
            break;
        }
        if (mach_flow_current() == want) {
            break;
        }
        if (plat_ms() - t0 >= SF_MS) {
            why = ", CAP";
            break;
        }
    }
    if (n == SF_STEPS) {
        why = ", CAP";
    }
    char tag[16];
    sbuf_t t;
    sb_init(&t, tag, sizeof(tag));
    sb_put(&t, over ? "nextf:" : "stepf:");
    sb_put(&t, mach_flow_tag(want));
    land_line(out, sb_str(&t), c_last, why);
    return MON_OK;
}

/* ---- reu, pot ----------------------------------------------------------- */

static int yn(sbuf_t *out, int b)
{
    sb_put(out, b ? "yes" : "no");
    return b;
}

/* reu: the registers through the lens unit's look (a read of $DF00-$DF0A that does not clear the status), the
 * size from the firmware's setting. TRX64's layout (verbs.rs reu_report); what the core does not show
 * (armed for $FF00, transfer pending) is left out. */
static int verb_reu(toks_t *k, sbuf_t *out)
{
    (void)k;
    char en[24], sz[24];
    if (plat_config("C64 and Cartridge Settings", "RAM Expansion Unit", en, sizeof(en)) < 0) {
        err(out, "reu: the firmware does not tell its REU setting here", 0);
        return MON_ERR;
    }
    if (!m_strieq(en, "Enabled")) {
        sb_put(out, m_strieq(en, "Disabled")
                        ? "reu: nothing on the expansion port (C64 and Cartridge Settings: RAM Expansion Unit is Disabled)"
                        : "reu: GeoRAM mode, not an REU (the registers are write only at $DFFE/$DFFF)");
        return MON_OK;
    }
    if (plat_config("C64 and Cartridge Settings", "REU Size", sz, sizeof(sz)) < 0) {
        sz[0] = '?';
        sz[1] = 0;
    }
    uint8_t r[11];
    for (int i = 0; i < 11; i++) {
        int v = mach_peek("io", (uint16_t)(0xDF00 + i), 0);
        if (v < 0) {
            err(out, "reu: the registers are not readable without a bus cycle on this bitstream (no lens unit)", 0);
            return MON_ERR;
        }
        r[i] = (uint8_t)v;
    }
    sb_put(out, "reu: ");
    sb_put(out, sz);
    sb_put(out, (r[0] & 0x10) ? ", 1764/1750-class (256K chips)" : ", 1700-class (64K chips)");
    sb_put(out, "\n  status   $");
    sb_HEX(out, r[0], 2);
    sb_put(out, "  verify-error ");
    yn(out, r[0] & 0x20);
    sb_put(out, "  end-of-block ");
    yn(out, r[0] & 0x40);
    sb_put(out, "  irq-pending ");
    yn(out, r[0] & 0x80);
    static const char *const ttype[] = { "stash (C64 -> REU)", "fetch (REU -> C64)", "swap", "verify" };
    sb_put(out, "\n  command  $");
    sb_HEX(out, r[1], 2);
    sb_put(out, "  ");
    sb_put(out, ttype[r[1] & 3]);
    sb_put(out, "  autoload ");
    yn(out, r[1] & 0x20);
    sb_put(out, "  $FF00-trigger ");
    sb_put(out, (r[1] & 0x10) ? "disabled" : "enabled");
    sb_put(out, "\n  C64 $");
    sb_HEX(out, r[2] | (r[3] << 8), 4);
    sb_put(out, "  REU $");
    sb_HEX(out, r[6] & 7, 2);
    sb_putc(out, ':');
    sb_HEX(out, r[4] | (r[5] << 8), 4);
    sb_put(out, "  length $");
    sb_HEX(out, r[7] | (r[8] << 8), 4);
    sb_put(out, "\n  int-mask $");
    sb_HEX(out, r[9], 2);
    sb_put(out, "  addr-control $");
    sb_HEX(out, r[10], 2);
    return MON_OK;
}

/* pot: the select lines are CIA 1 port A (on the state bus); the SID's POTX/POTY are not (SID is read with bus
 * cycles only: `sidefx on` while paused) */
static int verb_pot(toks_t *k, sbuf_t *out)
{
    if (k->n > 1) {
        err(out, "pot: read-only here - the paddles come from the control ports of the board", 0);
        return MON_ERR;
    }
    uint32_t w = hw_sb(3, 4);
    uint8_t pa = (uint8_t)((w & 0xFF) | ~(uint8_t)(w >> 16)); /* PA out, an input pin counts as high */
    static const char *const which[] = { "neither port", "port 1", "port 2", "both ports (parallel)" };
    sb_put(out, "pot: selected ");
    sb_put(out, which[(pa >> 6) & 3]);
    sb_put(out, " (CIA1 port A out $");
    sb_HEX(out, pa, 2);
    sb_put(out, ": PA6=");
    sb_dec(out, (pa >> 6) & 1);
    sb_put(out, " PA7=");
    sb_dec(out, (pa >> 7) & 1);
    sb_putc(out, ')');
    int x = mach_peek("io", 0xD419, M.sidefx);
    int y = mach_peek("io", 0xD41A, M.sidefx);
    sb_put(out, "\n  latch: $d419=");
    if (x < 0) {
        sb_put(out, "--");
    } else {
        sb_putc(out, '$');
        sb_hex(out, (uint32_t)x, 2);
    }
    sb_put(out, " $d41a=");
    if (y < 0) {
        sb_put(out, "--");
    } else {
        sb_putc(out, '$');
        sb_hex(out, (uint32_t)y, 2);
    }
    if ((x < 0) || (y < 0)) {
        sb_put(out, "\n  (the SID is not on the state bus: `sidefx on` while paused reads the latch with bus cycles)");
    }
    return MON_OK;
}

/* ---- run control ------------------------------------------------------- */

static int verb_g(toks_t *k, sbuf_t *out)
{
    if ((k->n > 1) && m_strieq(k->t[0], "g")) {
        uint16_t a;
        if (!addr_of(k->t[1], &a)) {
            err(out, "g: bad address '", k->t[1]);
            sb_putc(out, '\'');
            return MON_ERR;
        }
        if (M.running) {
            mach_pause();
        }
        hw_cpu_t mid;
        mach_cpu(&mid);
        if (!M.running && M.have_stop && m_streq(M.stop.reason, "jam")) {
            /* a JAM never ends its instruction: no step, no redirect gets it out */
            err(out, "g: the CPU is stuck on a JAM opcode at ", 0);
            span_addr4(out, M.stop.pc, ROLE_PC);
            sb_put(out, " - only a reset gets it out");
            return MON_ERR;
        }
        if (mach_own() && !M.running && !mid.at_ib) {
            /* a stop in the middle of an instruction (a cycle step, a watch hit): finish it (`z`) so the
             * redirect has a boundary to land on. A watch unit may fire again on the cycles that are left (the
             * store of an INC under a store observer): then the step stops mid-instruction again, so go on. */
            for (int tries = 0; tries < 4 && !mid.at_ib; tries++) {
                if (mach_step_flow() < 0) {
                    err(out, "g: could not finish the instruction before the jump (timeout)", 0);
                    return MON_ERR;
                }
                mach_cpu(&mid);
            }
        }
        const char *e = mach_cpu_write(W_PC, 0, a);
        if (e) {
            err(out, "g: ", e);
            return MON_ERR;
        }
    }
    mach_run();
    hw_cpu_t c;
    mach_cpu(&c);
    sb_put(out, "continuing at .C:");
    sb_hex(out, mach_pc(&c), 4);
    sb_put(out, " (running — Pause to halt)");
    return MON_OK;
}

static int verb_z(toks_t *k, sbuf_t *out, int cycles)
{
    uint32_t n = 1;
    if ((k->n > 1) && !m_parse_hex(k->t[1], -1, &n)) {
        err(out, cycles ? "cs: usage: cs [n]" : "z: usage: z [n]", 0);
        return MON_ERR;
    }
    uint64_t c0;
    if (M.running) {
        mach_pause();
    }
    c0 = mach_cycles();
    if (!cycles && (n <= 256)) {
        /* instruction by instruction, so the flow tracker sees an interrupt entered on the way */
        for (uint32_t i = 0; i < (n ? n : 1); i++) {
            if (mach_step_flow() < 0) {
                err(out, "step: the machine did not stop (timeout)", 0);
                return MON_ERR;
            }
        }
    } else if (mach_step((int)n, cycles) < 0) {
        err(out, "step: the machine did not stop (timeout)", 0);
        return MON_ERR;
    }
    land_line(out, cycles ? "cycle" : "step", M.stop.cycles - c0, "");
    return MON_OK;
}

static int verb_until(toks_t *k, sbuf_t *out, mon_pending_t *p)
{
    uint16_t a;
    if ((k->n < 2) || !addr_of(k->t[1], &a)) {
        err(out, "until: usage: until <addr>", 0);
        return MON_ERR;
    }
    if (M.running) {
        mach_pause();
    }
    p->kind = PK_UNTIL;
    p->addr = a;
    p->cyc0 = mach_cycles();
    p->instr0 = hw_instrs();
    mach_arm_until(a);
    mach_run();
    return MON_PENDING;
}

static int verb_next(sbuf_t *out, mon_pending_t *p)
{
    if (M.running) {
        mach_pause();
    }
    hw_cpu_t c;
    mach_cpu(&c);
    uint16_t pc = mach_pc(&c);
    uint8_t s0 = c.s;
    int jsr = (c.op == 0x20) && c.at_ib;
    uint64_t c0 = mach_cycles();
    if (mach_step(1, 0) < 0) {
        err(out, "next: the machine did not stop (timeout)", 0);
        return MON_ERR;
    }
    uint32_t r_cyc = (uint32_t)(M.stop.cycles - c0);
    if (!jsr) {
        land_line(out, "next", r_cyc, "");
        return MON_OK;
    }
    /* the JSR ran; a breakpoint on the routine's first instruction counts */
    hw_cpu_t c1;
    mach_cpu(&c1);
    if (mach_bp_at(mach_pc(&c1))) {
        land_line(out, "next", r_cyc, ", hit user bp");
        return MON_OK;
    }
    p->kind = PK_NEXT;
    p->r_cyc = r_cyc;
    p->cyc0 = mach_cycles();
    p->instr0 = hw_instrs();
    mach_arm_next((uint16_t)(pc + 3), s0);
    mach_run();
    return MON_PENDING;
}

static int verb_ret(sbuf_t *out, mon_pending_t *p)
{
    (void)out;
    if (M.running) {
        mach_pause();
    }
    hw_cpu_t c;
    mach_cpu(&c);
    p->kind = PK_RET;
    p->cyc0 = mach_cycles();
    p->instr0 = hw_instrs();
    mach_arm_ret(c.s);
    mach_run();
    return MON_PENDING;
}

void mon_finish(const mon_pending_t *p, int capped, sbuf_t *out)
{
    const stop_t *s = &M.stop;
    uint64_t cyc = s->cycles - p->cyc0;
    uint32_t ins = s->instrs - p->instr0;
    int user = m_streq(s->reason, "breakpoint") || m_streq(s->reason, "observer");
    switch (p->kind) {
    case PK_UNTIL:
        if (!capped && (user || m_streq(s->reason, "until"))) {
            sb_put(out, "until $");
            sb_hex(out, p->addr, 4);
            sb_put(out, " reached -> .C:");
            sb_hex(out, s->pc, 4);
            sb_put(out, " (");
            sb_dec(out, ins);
            sb_put(out, " instr, ");
            sb_dec(out, cyc);
            sb_put(out, " cyc)");
        } else {
            sb_put(out, "until $");
            sb_hex(out, p->addr, 4);
            sb_put(out, " NOT reached (");
            sb_dec(out, ins);
            sb_put(out, " instr, ");
            sb_dec(out, cyc);
            sb_put(out, " cyc, pc=$");
            sb_hex(out, s->pc, 4);
            sb_putc(out, ')');
        }
        M.disasm_cursor = s->pc;
        M.disasm_cursor_set = 1;
        break;
    case PK_NEXT:
        land_line(out, "next", p->r_cyc, capped ? ", CAP" : user ? ", hit user bp" : "");
        break;
    case PK_RET:
        land_line(out, "return", cyc, capped ? ", CAP" : user ? ", hit user bp" : "");
        break;
    default: break;
    }
}

/* The screen's words for a stop nobody asked a verb about: a breakpoint or an observer fired, a
 * pause came (T21 §8.5). Then the instruction at PC, like the landing line of a step. */
void mon_stop_text(sbuf_t *out)
{
    const stop_t *s = &M.stop;
    if (m_streq(s->reason, "breakpoint")) {
        sb_put(out, "breakpoint ");
        sb_dec(out, (uint64_t)s->bp_num);
        sb_put(out, " hit at .C:");
    } else if (m_streq(s->reason, "observer")) {
        sb_put(out, "observer ");
        sb_put(out, (s->obs >= 0) ? M.obs[s->obs].name : "?");
        sb_put(out, " hit at .C:");
    } else if (m_streq(s->reason, "jam")) {
        sb_put(out, "JAM at .C:");
    } else {
        sb_put(out, "stopped (");
        sb_put(out, s->reason);
        sb_put(out, ") at .C:");
    }
    sb_hex(out, s->pc, 4);
    if (m_streq(s->reason, "jam")) {
        sb_put(out, " - the CPU is stuck, no interrupt wakes a JAM (opcode $");
        sb_hex(out, mach_peek8("cpu", s->pc), 2);
        sb_put(out, "); a reset frees it");
    } else if (!s->at_ib) {
        sb_put(out, " (mid-instruction)");
    }
    sb_putc(out, '\n');
    int at = out->len;
    dis_line(rd_lens, (void *)"cpu", s->pc, out, 1, "cpu");
    if (s_narrow && (visible_len(out->p + at, out->len - at) > s_cols)) {
        out->len = at;
        dis_set_raw(0);
        dis_line(rd_lens, (void *)"cpu", s->pc, out, 1, "cpu");
        dis_set_raw(1);
    }
    M.disasm_cursor = s->pc;
    M.disasm_cursor_set = 1;
}

static int verb_status(sbuf_t *out)
{
    uint32_t st = hw_status();
    sb_put(out, M.running ? "state: running" : "state: paused");
    sb_put(out, M.owned ? " (trxmon owns run control)" : " (C64_STOP rules the machine; trxmon has not taken it)");
    if (M.have_stop) {
        sb_put(out, "\nlast stop: ");
        sb_put(out, M.stop.reason);
        sb_put(out, " at $");
        sb_hex(out, M.stop.pc, 4);
        sb_put(out, ", cycle ");
        sb_dec(out, M.stop.cycles);
        sb_put(out, M.stop.at_ib ? " (instruction boundary)" : " (mid-instruction)");
        sb_put(out, ", raster ");
        sb_dec(out, M.stop.rec.pos >> 7);
        sb_putc(out, '/');
        sb_dec(out, M.stop.rec.pos & 0x7F);
    }
    sb_put(out, "\ncycles ");
    sb_dec(out, mach_cycles());
    sb_put(out, "  instrs ");
    sb_dec(out, hw_instrs());
    sb_put(out, "  stops ");
    sb_dec(out, st >> 24);
    if (st & ST_HELD) {
        sb_put(out, "\nthe CPU is parked at a read cycle by a bus DMA (the next step releases it)");
    }
    sb_put(out, "\nbreakpoints ");
    sb_dec(out, (uint64_t)M.nbp);
    sb_put(out, " (exec bitmap ");
    sb_put(out, (st & (1u << 18)) ? "on)" : "off)");
    for (int u = 0; u < 2; u++) {
        sb_put(out, "\nwatch unit ");
        sb_dec(out, (uint64_t)u);
        sb_put(out, ": ");
        const char *who = M.transient ? "run-to (until/next/return)" : "free";
        for (int i = 0; !M.transient && (i < M.nobs); i++) {
            if (M.obs[i].on && (M.obs[i].unit == u)) {
                who = M.obs[i].name;
            }
        }
        sb_put(out, who);
    }
    return MON_OK;
}

static int verb_model(sbuf_t *out)
{
    hw_video_t v;
    hw_video(&v);
    sb_put(out, v.hz60 ? "c64-ntsc  NTSC  VIC-II 6567 (kawari)  " : "c64-pal  PAL  VIC-II 6569 (kawari)  ");
    sb_dec(out, v.cycles_per_line);
    sb_put(out, " x ");
    sb_dec(out, v.lines);
    sb_put(out, " = ");
    sb_dec(out, v.frame_cycles);
    sb_put(out, " cycles/frame  ");
    sb_dec(out, v.cpu_hz);
    sb_put(out, " Hz  ");
    sb_dec(out, v.fps_x100 / 100);
    sb_putc(out, '.');
    sb_putc(out, (char)('0' + (v.fps_x100 / 10) % 10));
    sb_putc(out, (char)('0' + v.fps_x100 % 10));
    sb_put(out, " fps\n"
                "the C64 Ultimate core follows the System Mode (Machine menu): `model <row>` is not available on this host");
    return MON_OK;
}

static int verb_bank(toks_t *k, sbuf_t *out)
{
    if (k->n < 2) {
        sb_put(out, "bank = ");
        sb_put(out, M.lens);
        sb_put(out, "  (lens for m/d; one of cpu|ram|rom|io|cart)");
        return MON_OK;
    }
    const char *l = lens_of(k->t[1]);
    if (!l || m_strieq(k->t[1], "default")) {
        err(out, "bank: expected cpu|ram|rom|io|cart, got '", k->t[1]);
        sb_putc(out, '\'');
        return MON_ERR;
    }
    memcpy(M.lens, l, (size_t)m_strlen(l) + 1);
    sb_put(out, "bank = ");
    sb_put(out, l);
    return MON_OK;
}

static int verb_sidefx(toks_t *k, sbuf_t *out)
{
    const char *a = (k->n > 1) ? k->t[1] : "toggle";
    int next;
    if (m_strieq(a, "on")) {
        next = 1;
    } else if (m_strieq(a, "off")) {
        next = 0;
    } else if (m_strieq(a, "toggle")) {
        next = !M.sidefx;
    } else {
        err(out, "sidefx: on|off|toggle", 0);
        return MON_ERR;
    }
    M.sidefx = next;
    sb_put(out, next ? "sidefx = on (m/c/t/h read LIVE — I/O side effects, one bus cycle per byte, only while paused; d stays a peek)"
                     : "sidefx = off (peek — side-effect-free, default)");
    return MON_OK;
}

static int verb_device(toks_t *k, sbuf_t *out)
{
    if (k->n < 2) {
        sb_put(out, "device: ");
        sb_put(out, M.device ? "drive8" : "c64");
        sb_put(out, "   (c64 | drive8 \xe2\x80\x94 anything but c64 is read-inspect r/m/d; here also z and bk)");
        return MON_OK;
    }
    if (m_strieq(k->t[1], "c64")) {
        M.device = 0;
        sb_put(out, "device: c64");
        return MON_OK;
    }
    if (m_strieq(k->t[1], "drive8") || m_strieq(k->t[1], "8")) {
        M.device = 8;
        M.disasm_cursor_set = 0;
        M.mem_cursor_set = 0;
        sb_put(out, "device: drive8");
        return MON_OK;
    }
    sb_put(out, "device: usage: device c64 | drive8");
    return MON_ERR;
}

/* r under device drive8: TRX64's 1541 layout; this host has the drive's PC
 * (the address of its last opcode fetch) and cycle count, not its registers */
static int verb_r_drive(sbuf_t *out)
{
    uint32_t st = hw_drv_stat();
    uint16_t pc = (uint16_t)(st >> 16);
    sb_put(out, "1541 (drive 8)\n  ADDR AC XR YR SP NV-BDIZC  clk\n.;");
    char t[6];
    sbuf_t s;
    sb_init(&s, t, sizeof(t));
    sb_hex(&s, pc, 4);
    span_mark(out, t, 4, pc, ROLE_PC, "drive8", 1);
    sb_put(out, " -- -- -- -- --------  ");
    sb_dec(out, hw_drv_cycles());
    sb_put(out, "\n  track ? (halftrack ?)  led ?");
    sb_put(out, (st & 1) ? "\n  halted" : "\n  running");
    if (st & 2) {
        sb_put(out, " (held by the debugger)");
    }
    sb_put(out, "\n  (this host reads the drive's PC and cycle count; its registers, VIAs and head are not on the state bus)");
    return MON_OK;
}

static int verb_z_drive(toks_t *k, sbuf_t *out)
{
    uint32_t n = 1;
    if ((k->n > 1) && !m_parse_hex(k->t[1], -1, &n)) {
        err(out, "z: usage: z [n]", 0);
        return MON_ERR;
    }
    uint32_t c0 = hw_drv_cycles();
    if (mach_drv_step((int)n) < 0) {
        err(out, "step: the drive did not stop (timeout)", 0);
        return MON_ERR;
    }
    uint16_t pc = (uint16_t)(hw_drv_stat() >> 16);
    dis_line(rd_lens, (void *)"drive8", pc, out, 1, "drive8");
    sb_put(out, " (drive step, ");
    sb_dec(out, (uint32_t)(hw_drv_cycles() - c0));
    sb_put(out, " cyc)");
    M.disasm_cursor = pc;
    M.disasm_cursor_set = 1;
    return MON_OK;
}

/* bk under device drive8: the one drive comparator (exec at an IB) */
static int verb_bk_drive(toks_t *k, sbuf_t *out)
{
    if (k->n < 2) {
        if (!M.drv_bp_on) {
            sb_put(out, "no breakpoints on drive8");
        } else {
            sb_put(out, "breakpoints (drive8):\n  #1  $");
            sb_hex(out, M.drv_bp, 4);
        }
        return MON_OK;
    }
    const char *t = k->t[1];
    int del = t[0] == '-';
    uint16_t a;
    if (!addr_of(t + del, &a)) {
        err(out, "bk: bad address '", t);
        sb_putc(out, '\'');
        return MON_ERR;
    }
    if (!mach_own()) {
        err(out, "bk: the debug block is not available", 0);
        return MON_ERR;
    }
    if (del) {
        if (!M.drv_bp_on || (M.drv_bp != a)) {
            sb_put(out, "no breakpoint at $");
            sb_hex(out, a, 4);
            return MON_ERR;
        }
        M.drv_bp_on = 0;
        hw_drv_cmp(0, 0);
        sb_put(out, "bk removed $");
        sb_hex(out, a, 4);
        sb_put(out, " (drive8)");
        return MON_OK;
    }
    /* quasi-static: written while the drive stands */
    if (M.running) {
        mach_pause();
    }
    M.drv_bp = a;
    M.drv_bp_on = 1;
    hw_drv_cmp(a, 1);
    sb_put(out, "bk #1 set at $");
    sb_hex(out, a, 4);
    sb_put(out, " on drive8 (one drive comparator on this host: it replaces any other)");
    return MON_OK;
}

/* TRX64 assemble_at: "aaaa  bb bb bb     <the disassembly line>" */
static int assemble_at(uint16_t addr, const char *text, sbuf_t *out)
{
    uint8_t b[3];
    char et[128];
    sbuf_t e;
    sb_init(&e, et, sizeof(et));
    int n = asm6502_line(text, addr, b, &e);
    if (!n) {
        err(out, "a: ", sb_str(&e));
        return MON_ERR;
    }
    const char *we = mach_write("cpu", addr, b, n);
    if (we) {
        err(out, "a: ", we);
        return MON_ERR;
    }
    s_asm = (uint16_t)(addr + n);
    M.disasm_cursor = (uint16_t)(addr + n);
    M.disasm_cursor_set = 1;
    sb_hex(out, addr, 4);
    sb_put(out, "  ");
    for (int i = 0; i < 3; i++) {
        if (i < n) {
            if (i) {
                sb_putc(out, ' ');
            }
            sb_hex(out, b[i], 2);
        }
    }
    sb_spaces(out, 11 - (3 * n - 1));
    sb_put(out, "  ");
    dis_line(rd_lens, (void *)"cpu", addr, out, 1, "cpu");
    return MON_OK;
}

static int verb_a(toks_t *k, sbuf_t *out)
{
    uint16_t a;
    if ((k->n < 2) || !addr_of(k->t[1], &a)) {
        err(out, "a: usage: a <addr> [instruction]  \xe2\x80\x94 enter assemble mode (empty line exits)", 0);
        return MON_ERR;
    }
    if (k->n < 3) {
        s_asm = a;
        M.disasm_cursor = a;
        M.disasm_cursor_set = 1;
        return MON_OK;
    }
    char instr[128];
    sbuf_t b;
    sb_init(&b, instr, sizeof(instr));
    for (int i = 2; i < k->n; i++) {
        if (i > 2) {
            sb_putc(&b, ' ');
        }
        sb_put(&b, k->t[i]);
    }
    return assemble_at(a, sb_str(&b), out);
}

int mon_asm_mode(void) { return (s_asm >= 0) || s_blk.on; }

int mon_asm_next(uint16_t *addr)
{
    if ((s_asm < 0) || s_blk.on) {
        return 0;
    }
    *addr = (uint16_t)s_asm;
    return 1;
}

static int name_cmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* TRX64 assemble_block_at: nothing is written unless the whole block assembles */
static int assemble_block_at(uint16_t origin, const char *src, const char *who, sbuf_t *out)
{
    asm6502_block(src, origin, &s_ab);
    if (s_ab.nerr) {
        const char *e = s_ab.err;
        while (*e) {
            sb_put(out, who);
            sb_put(out, ": ");
            while (*e && (*e != '\n')) {
                sb_putc(out, *e++);
            }
            if (*e) {
                e++;
            }
            sb_putc(out, '\n');
        }
        sb_put(out, who);
        sb_put(out, ": nothing written");
        return MON_ERR;
    }
    if (!s_ab.nbytes) {
        sb_put(out, who);
        sb_put(out, ": the block assembled to no bytes \xe2\x80\x94 nothing written");
        return MON_OK;
    }
    const char *we = mach_write("cpu", s_ab.origin, s_ab.bytes, s_ab.nbytes);
    if (we) {
        err(out, who, ": ");
        sb_put(out, we);
        return MON_ERR;
    }
    M.disasm_cursor = s_ab.origin;
    M.disasm_cursor_set = 1;
    sb_put(out, who);
    sb_put(out, ": $");
    sb_hex(out, s_ab.origin, 4);
    sb_put(out, "-$");
    sb_hex(out, (uint16_t)(s_ab.origin + s_ab.nbytes - 1), 4);
    sb_put(out, "  ");
    sb_dec(out, (uint64_t)s_ab.nbytes);
    sb_put(out, " bytes");
    if (s_ab.nlab) {
        /* by name, as TRX64's BTreeMap */
        int order[ASM_LABELS];
        for (int i = 0; i < s_ab.nlab; i++) {
            order[i] = i;
        }
        for (int i = 1; i < s_ab.nlab; i++) {
            for (int j = i; (j > 0) && (name_cmp(s_ab.lab[order[j - 1]].name, s_ab.lab[order[j]].name) > 0); j--) {
                int t = order[j];
                order[j] = order[j - 1];
                order[j - 1] = t;
            }
        }
        sb_putc(out, '\n');
        sb_put(out, who);
        sb_put(out, ": labels ");
        for (int i = 0; i < s_ab.nlab; i++) {
            sb_put(out, " ");
            sb_put(out, s_ab.lab[order[i]].name);
            sb_put(out, "=$");
            sb_hex(out, s_ab.lab[order[i]].v, 4);
            if (i + 1 < s_ab.nlab) {
                sb_putc(out, ' ');
            }
        }
    }
    return MON_OK;
}

/* asm <addr> [<<TAG]: the body is the rest of the command (RPC), or the lines
 * typed after it up to `end` (or TAG) */
static int verb_asm(const char *cmd, sbuf_t *out)
{
    const char *nl = cmd;
    while (*nl && (*nl != '\n')) {
        nl++;
    }
    toks_t k;
    char head[128];
    int hn = (int)(nl - cmd) < 127 ? (int)(nl - cmd) : 127;
    memcpy(head, cmd, (size_t)hn);
    head[hn] = 0;
    tokenize(head, &k);
    uint16_t origin;
    if ((k.n < 2) || !addr_of(k.t[1], &origin)) {
        err(out, "asm: usage: asm <addr> [<<TAG]  \xe2\x80\x94 then the source, then `end` (or TAG)", 0);
        return MON_ERR;
    }
    const char *end = "end";
    if ((k.n > 2) && (k.t[2][0] == '<') && (k.t[2][1] == '<') && k.t[2][2]) {
        end = k.t[2] + 2;
    }
    if (!*nl) {
        s_blk.on = 1;
        s_blk.origin = origin;
        int en = m_strlen(end) < 15 ? m_strlen(end) : 15;
        memcpy(s_blk.end, end, (size_t)en);
        s_blk.end[en] = 0;
        s_blk.len = 0;
        s_blk.src[0] = 0;
        return MON_OK;
    }
    /* the body up to the end tag */
    static char src[4096];
    int sn = 0;
    const char *l = nl + 1;
    while (*l) {
        const char *le = l;
        while (*le && (*le != '\n')) {
            le++;
        }
        const char *t = l;
        int tn = (int)(le - l);
        while (tn && ((*t == ' ') || (*t == '\t'))) {
            t++;
            tn--;
        }
        while (tn && ((t[tn - 1] == ' ') || (t[tn - 1] == '\t') || (t[tn - 1] == '\r'))) {
            tn--;
        }
        if ((tn == m_strlen(end)) && m_strnieq(t, end, tn)) {
            break;
        }
        if (sn + (le - l) + 2 < (int)sizeof(src)) {
            memcpy(src + sn, l, (size_t)(le - l));
            sn += (int)(le - l);
            src[sn++] = '\n';
        }
        l = *le ? le + 1 : le;
    }
    src[sn] = 0;
    return assemble_block_at(origin, src, "asm", out);
}

/* bt: TRX64's buildBacktrace, a scan of the stack page for JSR return
 * addresses above SP (16 at most); no flow frames on this host */
static int verb_bt(sbuf_t *out)
{
    hw_cpu_t c;
    mach_cpu(&c);
    sb_put(out, "backtrace (live stack scan \xe2\x80\x94 best-effort; refine with `chis`):");
    int found = 0;
    for (uint32_t a = 0x0100 + ((c.s + 1u) & 0xFF); (a <= 0x01FF) && (found < 16); a += 2) {
        uint32_t lo = mach_peek8("cpu", (uint16_t)a), hi = mach_peek8("cpu", (uint16_t)(a + 1));
        uint16_t ret = (uint16_t)(((hi << 8) | lo) + 1);
        sb_put(out, "\n  ");
        span_addr4(out, (uint16_t)a, ROLE_MEMORY);
        sb_put(out, ": -> ");
        span_addr4(out, ret, ROLE_PC);
        sb_put(out, "  (JSR return?)");
        found++;
    }
    if (!found) {
        sb_put(out, "\n  (stack empty \xe2\x80\x94 SP at top)");
    }
    return MON_OK;
}

/* ---- files (TRX64's mini-shell: pwd cd mkdir load save bload bsave asm-file),
 * through the firmware's file system; the working directory is the app's ---- */

static char s_cwd[128] = "/USB1";
static uint8_t s_fbuf[65536 + 2];

/* `"<file>" rest..` or `file rest..` after the verb, as TRX64's parse_file_cmd */
static int file_args(const char *cmd, char *file, int fcap, toks_t *rest)
{
    const char *p = cmd;
    while (*p == ' ') {
        p++;
    }
    while (*p && (*p != ' ')) {
        p++; /* the verb */
    }
    while (*p == ' ') {
        p++;
    }
    int n = 0;
    if (*p == '"') {
        p++;
        while (*p && (*p != '"') && (n < fcap - 1)) {
            file[n++] = *p++;
        }
        if (*p == '"') {
            p++;
        }
    } else {
        while (*p && (*p != ' ') && (n < fcap - 1)) {
            file[n++] = *p++;
        }
    }
    file[n] = 0;
    tokenize(p, rest);
    return n;
}

static void resolve(const char *f, char *out, int cap)
{
    sbuf_t b;
    sb_init(&b, out, cap);
    if (f[0] != '/') {
        sb_put(&b, s_cwd);
        if (b.len && (out[b.len - 1] != '/')) {
            sb_putc(&b, '/');
        }
    }
    sb_put(&b, f);
    sb_str(&b);
}

static const char *base_name(const char *f)
{
    const char *b = f;
    for (const char *p = f; *p; p++) {
        if (*p == '/') {
            b = p + 1;
        }
    }
    return b;
}

static int read_file(const char *path, int cap)
{
    void *fp = plat_fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    int n = 0, r;
    while ((n < cap) && ((r = plat_fread(s_fbuf + n, cap - n, fp)) > 0)) {
        n += r;
    }
    plat_fclose(fp);
    return n;
}

/* `ls`: the entries of a directory sorted without regard to case (TRX64's order), at most LS_MAX of them */
#define LS_MAX  160
#define LS_NAME 48
static struct
{
    char name[LS_MAX][LS_NAME];
    uint8_t dir[LS_MAX];
    int n, total;
} s_ls;

static int ls_add(void *ctx, const char *name, uint32_t size, int is_dir)
{
    (void)ctx;
    (void)size;
    s_ls.total++;
    if (s_ls.n >= LS_MAX) {
        return 0;
    }
    int at = s_ls.n;
    while (at > 0) {
        const char *a = s_ls.name[at - 1];
        const char *b = name;
        int c = 0;
        while (*a && *b && !(c = (int)m_lower(*a) - (int)m_lower(*b))) {
            a++;
            b++;
        }
        if (!c) {
            c = (int)(uint8_t)m_lower(*a) - (int)(uint8_t)m_lower(*b);
        }
        if (c <= 0) {
            break;
        }
        memcpy(s_ls.name[at], s_ls.name[at - 1], LS_NAME);
        s_ls.dir[at] = s_ls.dir[at - 1];
        at--;
    }
    int n = m_strlen(name);
    if (n > LS_NAME - 1) {
        n = LS_NAME - 1;
    }
    memcpy(s_ls.name[at], name, (size_t)n);
    s_ls.name[at][n] = 0;
    s_ls.dir[at] = (uint8_t)is_dir;
    s_ls.n++;
    return 0;
}

static int verb_files(const char *op, const char *cmd, sbuf_t *out)
{
    char f[128], path[260];
    toks_t r;
    int have = file_args(cmd, f, sizeof(f), &r) > 0;
    if (m_streq(op, "pwd")) {
        sb_put(out, s_cwd);
        return MON_OK;
    }
    if (m_streq(op, "cd")) {
        resolve(have ? f : "/", path, sizeof(path));
        int n = m_strlen(path);
        while ((n > 1) && (path[n - 1] == '/')) {
            path[--n] = 0;
        }
        /* the parent must list it */
        char dir[260];
        int cut = 0;
        for (int i = 0; i < n; i++) {
            if (path[i] == '/') {
                cut = i;
            }
        }
        memcpy(dir, path, (size_t)cut);
        dir[cut ? cut : 1] = 0;
        if (cut == 0) {
            dir[0] = '/';
        }
        if ((n > 1) && !plat_exists(dir, path + cut + 1)) {
            err(out, "cd: no such directory: ", path);
            return MON_ERR;
        }
        memcpy(s_cwd, path, (size_t)(n < 127 ? n : 127));
        s_cwd[n < 127 ? n : 127] = 0;
        sb_put(out, s_cwd);
        return MON_OK;
    }
    if (m_streq(op, "mkdir")) {
        if (!have) {
            err(out, "mkdir: usage: mkdir <dir>", 0);
            return MON_ERR;
        }
        resolve(f, path, sizeof(path));
        if (plat_mkdir(path) < 0) {
            err(out, "mkdir: could not create ", path);
            return MON_ERR;
        }
        sb_put(out, "mkdir ");
        sb_put(out, f);
        return MON_OK;
    }
    if (m_streq(op, "ls") || m_streq(op, "dir")) {
        if (have) {
            resolve(f, path, sizeof(path));
        } else {
            resolve(s_cwd, path, sizeof(path));
        }
        s_ls.n = s_ls.total = 0;
        if (plat_dir(path, ls_add, 0) < 0) {
            err(out, "ls: no such directory (or the firmware has no directory listing): ", path);
            return MON_ERR;
        }
        sb_put(out, path);
        sb_putc(out, ':');
        if (!s_ls.n) {
            sb_put(out, "\n  (empty)");
        }
        for (int i = 0; i < s_ls.n; i++) {
            sb_put(out, s_ls.dir[i] ? "\n  d " : "\n  - ");
            sb_put(out, s_ls.name[i]);
        }
        if (s_ls.total > s_ls.n) {
            sb_put(out, "\n  ... ");
            sb_dec(out, (uint64_t)(s_ls.total - s_ls.n));
            sb_put(out, " more (not listed)");
        }
        return MON_OK;
    }
    if (m_streq(op, "rmdir")) {
        if (!have) {
            err(out, "rmdir: usage: rmdir <dir>", 0);
            return MON_ERR;
        }
        resolve(f, path, sizeof(path));
        if (plat_rmdir(path) < 0) {
            err(out, "rmdir: could not remove (not an empty directory, or the firmware has no rmdir): ", path);
            return MON_ERR;
        }
        sb_put(out, "rmdir ");
        sb_put(out, f);
        return MON_OK;
    }
    if (m_streq(op, "load") || m_streq(op, "bload")) {
        int prg = op[0] == 'l';
        uint16_t at = 0;
        int at_ok = (r.n > 0) && addr_of(r.t[0], &at);
        if (!have || (!prg && !at_ok)) {
            err(out, prg ? "load: usage: load \"<file>\" [addr]" : "bload: usage: bload \"<file>\" <addr>", 0);
            return MON_ERR;
        }
        resolve(f, path, sizeof(path));
        int n = read_file(path, (int)sizeof(s_fbuf));
        if (n < 0) {
            err(out, prg ? "load: no such file: " : "bload: no such file: ", path);
            return MON_ERR;
        }
        const uint8_t *body = s_fbuf;
        if (prg) {
            if (n < 2) {
                err(out, "load: PRG too short (need 2-byte header)", 0);
                return MON_ERR;
            }
            if (!at_ok) {
                at = (uint16_t)(s_fbuf[0] | (s_fbuf[1] << 8));
            }
            body += 2;
            n -= 2;
        }
        if ((uint32_t)at + (uint32_t)n > 0x10000u) {
            n = (int)(0x10000u - at); /* as TRX64's bload: no wrap */
        }
        mach_write("ram", at, body, n);
        M.disasm_cursor = at;
        M.disasm_cursor_set = 1;
        uint16_t end = (uint16_t)(at + n - 1);
        if (prg) {
            sb_put(out, "loaded ");
            sb_put(out, base_name(f));
            sb_put(out, ": $");
            sb_hex(out, at, 4);
            sb_put(out, "..$");
            sb_hex(out, end, 4);
            sb_put(out, " (");
            sb_dec(out, (uint64_t)n);
            sb_put(out, " bytes)");
        } else {
            sb_put(out, "bloaded ");
            sb_put(out, base_name(f));
            sb_put(out, ": ");
            sb_dec(out, (uint64_t)n);
            sb_put(out, " bytes -> $");
            sb_hex(out, at, 4);
            sb_put(out, "..$");
            sb_hex(out, n ? end : at, 4);
        }
        return MON_OK;
    }
    if (m_streq(op, "save") || m_streq(op, "bsave")) {
        int prg = op[0] == 's';
        uint16_t a1, a2;
        if (!have || (r.n < 2) || !addr_of(r.t[0], &a1) || !addr_of(r.t[1], &a2) || (a2 < a1)) {
            err(out, prg ? "save: usage: save \"<file>\" <a1> <a2>" : "bsave: usage: bsave \"<file>\" <a1> <a2>", 0);
            return MON_ERR;
        }
        int n = 0;
        if (prg) {
            s_fbuf[n++] = (uint8_t)a1;
            s_fbuf[n++] = (uint8_t)(a1 >> 8);
        }
        hw_ram_read(a1, s_fbuf + n, (int)a2 - (int)a1 + 1);
        n += (int)a2 - (int)a1 + 1;
        resolve(f, path, sizeof(path));
        void *fp = plat_fopen(path, "wb");
        if (!fp || (plat_fwrite(s_fbuf, n, fp) != n)) {
            if (fp) {
                plat_fclose(fp);
            }
            err(out, prg ? "save: could not write " : "bsave: could not write ", path);
            return MON_ERR;
        }
        plat_fclose(fp);
        sb_put(out, prg ? "saved " : "bsaved ");
        sb_put(out, base_name(f));
        sb_put(out, ": $");
        sb_hex(out, a1, 4);
        sb_put(out, "..$");
        sb_hex(out, a2, 4);
        sb_put(out, " (");
        sb_dec(out, (uint64_t)(a2 - a1 + 1));
        sb_put(out, prg ? " bytes + load addr)" : " bytes, raw)");
        return MON_OK;
    }
    /* asm-file <path> [addr]: the address given, or the file's own *= / .org */
    if (!have) {
        err(out, "asm-file: usage: asm-file <path> [addr]", 0);
        return MON_ERR;
    }
    resolve(f, path, sizeof(path));
    int n = read_file(path, 4095);
    if (n < 0) {
        err(out, "asm-file: ", path);
        sb_put(out, ": no such file");
        return MON_ERR;
    }
    s_fbuf[n] = 0;
    uint16_t origin = 0;
    if (r.n > 0) {
        if (!addr_of(r.t[0], &origin)) {
            err(out, "asm-file: usage: asm-file <path> [addr]", 0);
            return MON_ERR;
        }
    } else {
        int has_org = 0;
        for (const char *l = (const char *)s_fbuf; *l && !has_org;) {
            while ((*l == ' ') || (*l == '\t')) {
                l++;
            }
            has_org = (l[0] == '*' && (l[1] == '=' || (l[1] == ' ' && l[2] == '='))) || m_strnieq(l, ".org", 4);
            while (*l && (*l != '\n')) {
                l++;
            }
            if (*l) {
                l++;
            }
        }
        if (!has_org) {
            err(out, "asm-file: no address \xe2\x80\x94 give one (`asm-file ", f);
            sb_put(out, " c000`) or start the file with `*= $c000`");
            return MON_ERR;
        }
    }
    return assemble_block_at(origin, (const char *)s_fbuf, "asm-file", out);
}

static int is_one_of(const char *op, const char *const *list)
{
    for (int i = 0; list[i]; i++) {
        if (m_streq(op, list[i])) {
            return 1;
        }
    }
    return 0;
}

int mon_exec(const char *cmd, sbuf_t *out, mon_pending_t *p)
{
    static const char *const t20[] = { "diff", "cdiff", "play", "cadence", "window", "rawframe", 0 };
    static const char *const trace[] = { "tracedb", "traceindex", "map", "taint", "swimlane", "sd", 0 };
    static const char *const na[] = { "df", "iec",
                                      "drive", "cart", "georam", "uci", "folder", "bitmap", "bm",
                                      "reset", "power", "warp", "turbo", "mount", "eject", "swapcrt", "savecrt",
                                      "drivepower", "recent", "identify", "traprules", 0 };
    toks_t k;
    p->kind = PK_NONE;
    if (s_blk.on) {
        /* asm block mode: lines up to the end tag */
        const char *t = cmd;
        int tn = m_strlen(cmd);
        while (tn && ((*t == ' ') || (*t == '\t'))) {
            t++;
            tn--;
        }
        while (tn && ((t[tn - 1] == ' ') || (t[tn - 1] == '\t') || (t[tn - 1] == '\r') || (t[tn - 1] == '\n'))) {
            tn--;
        }
        if ((tn == m_strlen(s_blk.end)) && m_strnieq(t, s_blk.end, tn)) {
            s_blk.on = 0;
            return assemble_block_at(s_blk.origin, s_blk.src, "asm", out);
        }
        int n = m_strlen(cmd);
        if (s_blk.len + n + 2 < (int)sizeof(s_blk.src)) {
            memcpy(s_blk.src + s_blk.len, cmd, (size_t)n);
            s_blk.len += n;
            s_blk.src[s_blk.len++] = '\n';
            s_blk.src[s_blk.len] = 0;
        }
        return MON_OK;
    }
    tokenize(cmd, &k);
    if (s_asm >= 0) {
        /* modal assemble (TRX64): every line is an instruction, an empty one ends it */
        if (!k.n) {
            s_asm = -1;
            return MON_OK;
        }
        if (m_strieq(k.t[0], "a") || m_strieq(k.t[0], ".a")) {
            return verb_a(&k, out); /* the prefilled prompt: "a c101 nop" assembles there and goes on */
        }
        return assemble_at((uint16_t)s_asm, cmd, out);
    }
    if (!k.n) {
        return MON_OK;
    }
    char opbuf[24];
    int ol = m_strlen(k.t[0]);
    if (ol > (int)sizeof(opbuf) - 1) {
        ol = (int)sizeof(opbuf) - 1;
    }
    for (int i = 0; i < ol; i++) {
        opbuf[i] = m_lower(k.t[0][i]);
    }
    opbuf[ol] = 0;
    const char *op = opbuf;
    int slash = (op[0] == '/');
    if (slash) {
        static const char *const machine[] = { "/run", "/pause", "/step", "/quit", "/reset", "/power", "/mount",
                                               "/eject", 0 };
        if (!is_one_of(op, machine)) {
            err(out, "unknown command: ", k.t[0]);
            sb_put(out, ". Try 'help'.");
            return MON_ERR;
        }
        op++;
    }

    if (m_streq(op, "help") || m_streq(op, "?")) {
        help(out);
        return MON_OK;
    }
    if (m_streq(op, "quit")) {
        s_quit = 1;
        sb_put(out, "trxmon ends; the machine stays ");
        sb_put(out, M.running ? "running" : "paused");
        return MON_OK;
    }
    if (m_streq(op, "device") || m_streq(op, "dev")) return verb_device(&k, out);
    if (M.device) {
        /* TRX64: every device but the C64 is read-inspect (r/m/d); here also z and bk */
        if (m_streq(op, "r") || m_streq(op, "registers")) return verb_r_drive(out);
        if (m_streq(op, "m") || m_streq(op, "mem")) return verb_m(&k, out);
        if (m_streq(op, "d") || m_streq(op, "disass")) return verb_d(&k, out);
        if (m_streq(op, "z") || m_streq(op, "step") || m_streq(op, "si")) return verb_z_drive(&k, out);
        if (m_streq(op, "bk") || m_streq(op, "break") || m_streq(op, "b")) return verb_bk_drive(&k, out);
        if (m_streq(op, "io") && (k.n > 1) && m_streq(k.t[1], "1541")) return verb_io(&k, out);
        if (!m_streq(op, "help") && !m_streq(op, "?") && !m_streq(op, "status") && !m_streq(op, "quit")) {
            err(out, "device drive8: read-inspect only (r/m/d; here also z, bk). `device c64` first to use `", k.t[0]);
            sb_put(out, "`.");
            return MON_ERR;
        }
    }
    if (m_streq(op, "r") || m_streq(op, "registers")) return verb_r(&k, out);
    if (m_streq(op, "a")) return verb_a(&k, out);
    if (m_streq(op, "bt")) return verb_bt(out);
    {
        static const char *const files[] = { "pwd", "cd", "ls", "dir", "mkdir", "rmdir", "load", "save", "bload", "bsave", "asm-file", 0 };
        if (is_one_of(op, files)) {
            return verb_files(op, cmd, out);
        }
    }
    if (m_streq(op, "asm")) return verb_asm(cmd, out);
    if (m_streq(op, "m") || m_streq(op, "mem")) return verb_m(&k, out);
    if (m_streq(op, "d") || m_streq(op, "disass")) return verb_d(&k, out);
    if (m_streq(op, "wr")) return verb_wr(&k, out);
    if (m_streq(op, "f") || m_streq(op, "fill")) return verb_f(&k, out);
    if (m_streq(op, "t") || m_streq(op, "move")) return verb_t(&k, out);
    if (m_streq(op, "c") || m_streq(op, "compare")) return verb_c(&k, out);
    if (m_streq(op, "h") || m_streq(op, "hunt")) return verb_h(&k, out);
    if (m_streq(op, "screen")) return verb_screen(out);
    if (m_streq(op, "io")) return verb_io(&k, out);
    if (m_streq(op, "bank")) return verb_bank(&k, out);
    if (m_streq(op, "sidefx")) return verb_sidefx(&k, out);
    if (m_streq(op, "bk") || m_streq(op, "break") || m_streq(op, "b")) return verb_bk(&k, out);
    if (m_streq(op, "del") || m_streq(op, "delete")) return verb_del(&k, out);
    if (m_streq(op, "obs") || m_streq(op, "o") || m_streq(op, "ignore")) return verb_obs(&k, cmd, out);
    if (m_streq(op, "rb")) return verb_rb(&k, out);
    if (m_streq(op, "flow")) return verb_flow(out);
    if (m_streq(op, "focus")) return verb_focus(&k, out);
    if (m_streq(op, "sf") || m_streq(op, "stepf")) return verb_sf(0, out);
    if (m_streq(op, "nf") || m_streq(op, "nextf")) return verb_sf(1, out);
    if (m_streq(op, "reu")) return verb_reu(&k, out);
    if (m_streq(op, "pot")) return verb_pot(&k, out);
    if (m_streq(op, "g") || m_streq(op, "x") || m_streq(op, "run")) return verb_g(&k, out);
    if (m_streq(op, "z") || m_streq(op, "step") || m_streq(op, "si")) return verb_z(&k, out, 0);
    if (m_streq(op, "cs")) return verb_z(&k, out, 1);
    if (m_streq(op, "n") || m_streq(op, "next") || m_streq(op, "so")) return verb_next(out, p);
    if (m_streq(op, "ret") || m_streq(op, "return")) return verb_ret(out, p);
    if (m_streq(op, "until")) return verb_until(&k, out, p);
    if (m_streq(op, "status")) return verb_status(out);
    if (m_streq(op, "model")) return verb_model(out);
    if (m_streq(op, "pause")) {
        mach_pause();
        sb_put(out, "paused at .C:");
        sb_hex(out, M.stop.pc, 4);
        sb_put(out, M.stop.at_ib ? "" : " (mid-instruction)");
        return MON_OK;
    }
    int rv = ring_verb(op, k.n, k.t, out);
    if (rv >= 0) {
        return rv;
    }
    rv = trace_verb(op, k.n, k.t, out);
    if (rv >= 0) {
        return rv;
    }
    if (is_one_of(op, t20)) {
        err(out, k.t[0], ": not available on this host (the board's ring has no per-frame thumbnails, cadence or "
                         "paced playback; frame -N|+N, goto and rstep move on it)");
        return MON_ERR;
    }
    if (is_one_of(op, trace)) {
        err(out, k.t[0], ": not available on this host " "\xe2\x80\x94" " an analysis over a trace on the host: a trace export to C64RE/TRX64 is not in M1 (TODO T66)");
        return MON_ERR;
    }
    if (is_one_of(op, na)) {
        err(out, k.t[0], ": not available on this host");
        if (m_streq(op, "reset") || m_streq(op, "power") || m_streq(op, "mount") || m_streq(op, "eject")) {
            sb_put(out, " (trxmon; the Ultimate's REST API does media and machine: /v1/machine, /v1/drives, /v1/runners)");
        }
        return MON_ERR;
    }
    err(out, "unknown command: ", k.t[0]);
    sb_put(out, ". Try 'help'.");
    return MON_ERR;
}

/* ---- completion -------------------------------------------------------- */

typedef struct
{
    const char *pre; /* what is typed of the word */
    int pn;
    sbuf_t *out;
    int n;
} cand_t;

static void cand_add(cand_t *c, const char *w, int wn)
{
    if ((wn < c->pn) || !m_strnieq(w, c->pre, c->pn)) {
        return;
    }
    if (c->n) {
        sb_putc(c->out, '\n');
    }
    sb_putn(c->out, w, wn);
    c->n++;
}

static void cand_list(cand_t *c, const char *const *list)
{
    for (int i = 0; list[i]; i++) {
        cand_add(c, list[i], m_strlen(list[i]));
    }
}

static int cand_dir(void *ctx, const char *name, uint32_t size, int is_dir)
{
    cand_t *c = (cand_t *)ctx;
    (void)size;
    char w[LS_NAME + 2];
    int n = m_strlen(name);
    if (n > LS_NAME - 1) {
        n = LS_NAME - 1;
    }
    memcpy(w, name, (size_t)n);
    if (is_dir) {
        w[n++] = '/';
    }
    w[n] = 0;
    cand_add(c, w, n);
    return 0;
}

int mon_complete(const char *line, int len, int *start, sbuf_t *out)
{
    static const char *const verbs[] = {
        "a", "asm", "asm-file", "bank", "bk", "bload", "bsave", "bt", "c", "cd", "chis", "cs", "d", "del", "device",
        "f", "focus", "flow", "frame", "g", "goto", "h", "help", "ignore", "io", "load", "m", "mark", "marks", "mkdir",
        "model", "n", "nf", "obs", "pause", "pot", "pwd", "r", "rb", "reu", "ret", "revdepth", "rewind", "rmdir",
        "rstep", "save", "screen", "sf", "sidefx", "status", "t", "trace", "triage", "unmark", "until", "wr", "whowrote",
        "x", "z", "ls", 0 };
    static const char *const lenses[] = { "cpu", "ram", "rom", "io", "cart", "default", 0 };
    static const char *const obs2[] = { "when", "on", "off", "del", 0 };
    static const char *const trig[] = { "exec", "load", "store", 0 };
    static const char *const onoff[] = { "on", "off", 0 };
    static const char *const traces[] = { "on", "off", "status", 0 };
    static const char *const devs[] = { "c64", "drive8", 0 };
    /* the word being typed and the words before it */
    int ws = len;
    while ((ws > 0) && (line[ws - 1] != ' ') && (line[ws - 1] != '\t')) {
        ws--;
    }
    *start = ws;
    cand_t c = { line + ws, len - ws, out, 0 };
    toks_t k;
    char head[240];
    int hn = ws < (int)sizeof(head) - 1 ? ws : (int)sizeof(head) - 1;
    memcpy(head, line, (size_t)hn);
    head[hn] = 0;
    tokenize(head, &k);
    if (k.n == 0) {
        cand_list(&c, verbs);
        return c.n;
    }
    char vb[16];
    int vl = m_strlen(k.t[0]);
    if (vl > (int)sizeof(vb) - 1) {
        vl = (int)sizeof(vb) - 1;
    }
    for (int i = 0; i < vl; i++) {
        vb[i] = m_lower(k.t[0][i]);
    }
    vb[vl] = 0;
    const char *v = vb;
    if (v[0] == '.') {
        v++;
    }
    int arg = k.n; /* the argument being typed: 1-based */
    if (m_streq(v, "obs") || m_streq(v, "o") || m_streq(v, "ignore")) {
        if (arg == 1) {
            if (!m_streq(v, "ignore")) {
                cand_add(&c, "log", 3);
            }
            for (int i = 0; i < M.nobs; i++) {
                cand_add(&c, M.obs[i].name, m_strlen(M.obs[i].name));
            }
        } else if ((arg == 2) && !m_streq(v, "ignore")) {
            cand_list(&c, obs2);
        } else if ((arg == 3) && m_strieq(k.t[2], "when")) {
            cand_list(&c, trig);
        }
        return c.n;
    }
    if (m_streq(v, "m") || m_streq(v, "mem") || m_streq(v, "d") || m_streq(v, "disass") || m_streq(v, "bank") ||
        m_streq(v, "wr")) {
        if (arg == 1) {
            cand_list(&c, lenses);
        }
        return c.n;
    }
    if (m_streq(v, "sidefx")) {
        cand_list(&c, onoff);
    } else if (m_streq(v, "trace")) {
        cand_list(&c, traces);
    } else if (m_streq(v, "device") || m_streq(v, "dev")) {
        cand_list(&c, devs);
    } else if (m_streq(v, "goto") || m_streq(v, "unmark")) {
        for (int i = 0; i < ring_marks(); i++) {
            cand_add(&c, ring_mark_name(i), m_strlen(ring_mark_name(i)));
        }
    } else {
        static const char *const pathverbs[] = { "cd", "ls", "dir", "mkdir", "rmdir", "load", "save", "bload", "bsave",
                                                 "asm-file", 0 };
        if (!is_one_of(v, pathverbs) || (arg != 1)) {
            return 0;
        }
        /* a path: the directory part is listed, the rest is the prefix */
        const char *w = line + ws;
        if (*w == '"') {
            w++;
            *start = ws + 1;
        }
        int wn = len - (int)(w - line);
        int cut = 0;
        for (int i = 0; i < wn; i++) {
            if (w[i] == '/') {
                cut = i + 1;
            }
        }
        char dirpath[260];
        char pathw[200];
        int dn = cut < (int)sizeof(pathw) - 1 ? cut : (int)sizeof(pathw) - 1;
        memcpy(pathw, w, (size_t)dn);
        pathw[dn] = 0;
        if (!dn) {
            resolve(s_cwd, dirpath, sizeof(dirpath));
        } else {
            resolve(pathw, dirpath, sizeof(dirpath));
        }
        *start = (int)(w - line) + cut;
        c.pre = w + cut;
        c.pn = wn - cut;
        if (plat_dir(dirpath, cand_dir, &c) < 0) {
            return 0;
        }
    }
    return c.n;
}
