/*
 * ui.c — the monitor screen's model, see ui.h. Layout and keys: T21-monitor.md §8.2, §8.3.
 */
#include "ui.h"

#include "mach.h"
#include "mon.h"
#include "rpc.h"
#include "spans.h"

void ui_init(ui_t *u)
{
    memset(u, 0, sizeof(*u));
    u->ci = -1;
    u->cols = UI_NARROW_COLS;
    u->rows = UI_NARROW_ROWS;
    ui_set_keys(u, 0);
}

static int is_wide(const ui_t *u) { return u->cols >= UI_WIDE_COLS; }

/* The first row of the window that is drawn: the machine running shrinks the window to the rows that stay on top of
 * the C64's picture. With a log observer on, the live log gets more of them. */
static int run_top(const ui_t *u)
{
    if (!M.running) {
        return 0;
    }
    int keep = is_wide(u) ? UI_WIDE_SHRUNK_ROWS : UI_SHRUNK_ROWS;
    if (mach_live_log_active()) {
        keep += UI_LIVE_EXTRA_ROWS;
        if (keep > u->rows - 6) {
            keep = u->rows - 6;
        }
    }
    return u->rows - keep;
}

/* the first and last output row of the window, whose top `top` rows are blank (the machine runs) */
static void out_range(const ui_t *u, int top, int *first, int *last)
{
    if (is_wide(u)) {
        *first = top + 4; /* status, registers, device line, separator */
        *last = u->rows - 4; /* separator, command, hint below */
    } else {
        *first = top + 3;
        *last = u->rows - 2;
    }
}

int ui_out_rows(const ui_t *u)
{
    int f, l;
    out_range(u, 0, &f, &l);
    return l - f + 1;
}

void ui_set_size(ui_t *u, int cols, int rows)
{
    u->cols = cols < UI_NARROW_COLS ? UI_NARROW_COLS : cols > UI_COLS_MAX ? UI_COLS_MAX : cols;
    u->rows = rows < UI_NARROW_ROWS ? UI_NARROW_ROWS : rows > UI_ROWS_MAX ? UI_ROWS_MAX : rows;
    /* `m` and `d` fill what the output area shows, less the echoed command line */
    mon_set_screen(u->cols, ui_out_rows(u) - 1);
}

static void prompt_prefill(ui_t *u);

void ui_open(ui_t *u)
{
    u->clen = 0;
    u->cpos = 0;
    u->cmd[0] = 0;
    u->ci = -1;
    u->scroll = 0;
    u->leave = UI_STAY;
    u->was_run = M.running;
    u->olog_cur = mach_olog_seq(); /* the live log starts here */
    mon_set_screen(u->cols, ui_out_rows(u) - 1);
    prompt_prefill(u);
}

/* ---- the output -------------------------------------------------------- */

static void hist_add(ui_t *u, const char *line, int n, int attr)
{
    char *d = u->hist[u->hhead];
    memcpy(d, line, (size_t)n);
    d[n] = 0;
    u->hattr[u->hhead] = (uint8_t)attr;
    u->hhead = (u->hhead + 1) % UI_HIST;
    if (u->hn < UI_HIST) {
        u->hn++;
    }
}

void ui_print(ui_t *u, const char *text, int attr)
{
    char cur[UI_COLS_MAX + 1];
    int n = 0;
    int any = 0;
    const unsigned char *p = (const unsigned char *)text;
    u->scroll = 0;
    for (;; p++) {
        unsigned char c = *p;
        if (c == SPAN_START) {
            while (*p && (*p != SPAN_SEP)) {
                p++;
            }
            if (!*p) {
                break;
            }
            continue;
        }
        if ((c == SPAN_SEP) || (c == SPAN_END) || (c == '\r')) {
            continue;
        }
        if ((c == '\n') || (c == 0)) {
            hist_add(u, cur, n, attr);
            any = 1;
            n = 0;
            if (c == 0) {
                break;
            }
            continue;
        }
        char o = (char)c;
        if (c == '\t') {
            o = ' ';
        } else if (c >= 0xC0) {
            /* UTF-8 (the verbs' text has dashes and arrows): the overlay has the C64's characters */
            int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            o = '?';
            if ((len == 3) && (p[1] == 0x80) && ((p[2] == 0x94) || (p[2] == 0x93))) {
                o = '-';
            } else if ((len == 3) && (p[1] == 0x80) && (p[2] == 0xA6)) {
                o = '.';
            } else if ((len == 3) && (p[1] == 0x86) && (p[2] == 0x92)) {
                o = '>';
            }
            for (int i = 1; (i < len) && p[1]; i++) {
                p++;
            }
        } else if ((c < 0x20) || (c >= 0x7F)) {
            o = '.';
        }
        if (n == u->cols) {
            /* wrap at the last space of the row's second half, else hard at the window's width */
            int cut = n;
            for (int i = n - 1; i >= u->cols / 2; i--) {
                if (cur[i] == ' ') {
                    cut = i;
                    break;
                }
            }
            hist_add(u, cur, cut, attr);
            int rest = n - (cut < n ? cut + 1 : n);
            memmove(cur, cur + (n - rest), (size_t)rest);
            n = rest;
        }
        cur[n++] = o;
    }
    (void)any;
}

/* the i-th line from the newest (0 = newest) */
static const char *hist_back(const ui_t *u, int i, uint8_t *attr)
{
    int idx = (u->hhead - 1 - i + 2 * UI_HIST) % UI_HIST;
    *attr = u->hattr[idx];
    return u->hist[idx];
}

/* ---- the machine's rows ------------------------------------------------ */

static void status_text(const ui_t *u, sbuf_t *b)
{
    if (M.running) {
        sb_put(b, "RUNNING");
        if (u->run_desc[0]) {
            sb_putc(b, ' ');
            sb_put(b, u->run_desc);
        }
        return;
    }
    hw_cpu_t c;
    mach_cpu(&c);
    if (M.have_stop && m_streq(M.stop.reason, "jam")) {
        sb_put(b, "JAM");
    } else {
        sb_put(b, "PAUSED ");
        sb_put(b, (M.have_stop && !M.stop.at_ib) || (!M.have_stop && !c.at_ib) ? "mid" : "IB");
    }
    sb_put(b, " $");
    sb_hex(b, mach_pc(&c), 4);
    sb_put(b, " cy ");
    sb_dec(b, mach_cycles());
    sb_put(b, " r");
    sb_dec(b, mach_raster_line());
    if (M.have_stop) {
        sb_putc(b, '/');
        uint32_t cyc = M.stop.rec.pos & 0x7F;
        if (cyc < 100) {
            sb_putc(b, '0');
        }
        if (cyc < 10) {
            sb_putc(b, '0');
        }
        sb_dec(b, cyc);
    }
}

static void put_row(ui_screen_t *s, int y, const char *t, int n, int attr)
{
    if (n > s->cols) {
        n = s->cols;
    }
    memcpy(s->row[y], t, (size_t)n);
    for (int i = n; i < s->cols; i++) {
        s->row[y][i] = ' ';
    }
    s->row[y][s->cols] = 0;
    s->attr[y] = (uint8_t)attr;
}

static void put_sbuf(ui_screen_t *s, int y, sbuf_t *b, int attr) { put_row(s, y, b->p, b->len, attr); }

static void regs_rows(ui_screen_t *s, int y0)
{
    char t[160];
    sbuf_t b;
    if (M.running) {
        put_row(s, y0, "registers: running", 18, UA_REGS);
        put_row(s, y0 + 1, "", 0, UA_REGS);
        return;
    }
    sb_init(&b, t, sizeof(t));
    mon_register_dump(&b);
    /* "  ADDR AC XR YR SP NV-BDIZC\n.;PPPP AA XX YY SS flags" + the CPU port ($01 as read) */
    const char *nl = b.p;
    int n1 = 0;
    while ((n1 < b.len) && (nl[n1] != '\n')) {
        n1++;
    }
    char l1[48], l2[48];
    sbuf_t a, c;
    sb_init(&a, l1, sizeof(l1));
    sb_putn(&a, b.p, n1);
    sb_put(&a, " 01");
    sb_init(&c, l2, sizeof(l2));
    sb_putn(&c, b.p + n1 + 1, b.len - n1 - 1);
    sb_putc(&c, ' ');
    sb_HEX(&c, mach_port(), 2);
    put_sbuf(s, y0, &a, UA_REGS);
    put_sbuf(s, y0 + 1, &c, UA_REGS);
}

/* the wide registers row: "PC c012  A 00  X 00  Y 00  SP f6  NV-BDIZC nv-bdIzc" from TRX64's own dump */
static void wide_regs_row(ui_screen_t *s, int y)
{
    char t[160];
    sbuf_t b;
    if (M.running) {
        put_row(s, y, "registers: running", 18, UA_REGS);
        return;
    }
    sb_init(&b, t, sizeof(t));
    mon_register_dump(&b);
    /* line 2 is ".;PPPP AA XX YY SS flags" */
    int n1 = 0;
    while ((n1 < b.len) && (b.p[n1] != '\n')) {
        n1++;
    }
    const char *l2 = b.p + n1 + 1;
    int len2 = b.len - n1 - 1;
    char w[6][16];
    int nw = 0, wl = 0;
    for (int i = 0; i <= len2; i++) {
        char c = (i < len2) ? l2[i] : ' ';
        if ((c == ' ') || (c == '\n')) {
            if (wl && (nw < 6)) {
                w[nw][wl] = 0;
                nw++;
            }
            if (c == '\n') {
                break;
            }
            wl = 0;
        } else if (wl < 15) {
            w[nw < 6 ? nw : 5][wl++] = c;
        }
    }
    char rt[96];
    sbuf_t r;
    sb_init(&r, rt, sizeof(rt));
    if (nw < 6) {
        sb_put(&r, "registers: ?");
    } else {
        sb_put(&r, "PC ");
        sb_put(&r, (w[0][0] == '.') && (w[0][1] == ';') ? w[0] + 2 : w[0]);
        sb_put(&r, "  A ");
        sb_put(&r, w[1]);
        sb_put(&r, "  X ");
        sb_put(&r, w[2]);
        sb_put(&r, "  Y ");
        sb_put(&r, w[3]);
        sb_put(&r, "  SP ");
        sb_put(&r, w[4]);
        sb_put(&r, "  NV-BDIZC ");
        sb_put(&r, w[5]);
    }
    put_sbuf(s, y, &r, UA_REGS);
}

/* the wide device row: the CPU port, the lens `m` and `d` read through, the device, the ring and the trace */
static void wide_info_row(ui_screen_t *s, int y)
{
    char t[120];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    if (!M.running) {
        sb_put(&b, "$00=");
        sb_hex(&b, mach_ddr(), 2);
        sb_put(&b, " $01=");
        sb_hex(&b, mach_latch(), 2);
        sb_put(&b, "  ");
    }
    sb_put(&b, "bank ");
    sb_put(&b, M.lens);
    sb_put(&b, "  device ");
    sb_put(&b, M.device ? "drive8" : "c64");
    hw_ring_status_t rs;
    memset(&rs, 0, sizeof(rs));
    if (!M.running && (hw_ring_status(&rs) == 0) && rs.present) {
        sb_put(&b, "  ring ");
        sb_put(&b, rs.reu_off ? "off(REU) " : rs.on ? "on " : "off ");
        sb_dec(&b, (uint64_t)rs.frames);
        sb_put(&b, " fr");
    } else {
        sb_put(&b, "  ring -");
    }
    hw_trace_status_t ts;
    memset(&ts, 0, sizeof(ts));
    if (!M.running && hw_trace_present() && (hw_trace_status(&ts) == 0) && ts.present) {
        sb_put(&b, "  trace ");
        sb_put(&b, ts.on ? "on " : "off ");
        sb_dec(&b, (uint64_t)ts.records);
        sb_putc(&b, '/');
        sb_dec(&b, (uint64_t)ts.capacity);
    } else {
        sb_put(&b, "  trace -");
    }
    put_sbuf(s, y, &b, UA_REGS);
}

static void sep_row(ui_screen_t *s, int y, int scrolled)
{
    char t[UI_COLS_MAX + 1];
    int n = s->cols;
    memset(t, '-', (size_t)n);
    if (scrolled > 0) {
        char m[40];
        sbuf_t b;
        sb_init(&b, m, sizeof(m));
        sb_put(&b, " history -");
        sb_dec(&b, (uint64_t)scrolled);
        sb_put(&b, " lines, F7/PageDown ");
        int len = b.len;
        if (len > n - 4) {
            len = n - 4;
        }
        memcpy(t + 2, b.p, (size_t)len);
    }
    put_row(s, y, t, n, UA_SEP);
}

void ui_render(const ui_t *u, ui_screen_t *s)
{
    char t[96];
    sbuf_t b;
    memset(s, 0, sizeof(*s));
    s->cols = u->cols;
    s->rows = u->rows;
    const int wide = is_wide(u);
    int top = run_top(u);
    s->blank_rows = top;
    for (int y = 0; y < top; y++) {
        put_row(s, y, "", 0, UA_BLANK);
    }
    sb_init(&b, t, sizeof(t));
    status_text(u, &b);
    put_sbuf(s, top, &b, UA_STATUS);
    int first, last;
    out_range(u, top, &first, &last);
    int rows = last - first + 1;
    int max_scroll = u->hn > rows ? u->hn - rows : 0;
    int sc = u->scroll > max_scroll ? max_scroll : u->scroll;
    if (wide) {
        wide_regs_row(s, top + 1);
        wide_info_row(s, top + 2);
        sep_row(s, top + 3, 0);
    } else {
        regs_rows(s, top + 1);
    }
    for (int r = 0; r < rows; r++) {
        int back = sc + (rows - 1 - r);
        int y = first + r;
        if (back < u->hn) {
            uint8_t at;
            const char *l = hist_back(u, back, &at);
            put_row(s, y, l, m_strlen(l), at);
        } else {
            put_row(s, y, "", 0, UA_NORMAL);
        }
    }
    /* the command line: a '.' prompt, the tail of the line when it is longer than the row. Narrow, it
     * is the last row of the window and its last cell is never written (a write there scrolls the
     * overlay), so there is one cell less. Wide, the hint row is the last. */
    int cy = wide ? u->rows - 2 : u->rows - 1;
    int room = u->cols - (wide ? 2 : 3);
    int from = u->clen > room ? u->clen - room : 0;
    if (u->cpos < from) {
        from = u->cpos; /* the cursor moved left out of the tail: the row starts at it */
    }
    char cl[UI_COLS_MAX + 1];
    int shown = u->clen - from > room ? room : u->clen - from; /* the cursor moved left: the tail does not fit */
    cl[0] = '.';
    memcpy(cl + 1, u->cmd + from, (size_t)shown);
    put_row(s, cy, cl, 1 + shown, UA_PROMPT);
    s->cursor_x = 1 + u->cpos - from;
    s->cursor_y = cy;
    if (wide) {
        sep_row(s, u->rows - 3, sc);
        char ht[UI_COLS_MAX + 1];
        sbuf_t h;
        sb_init(&h, ht, sizeof(ht));
        sb_putc(&h, 'F');
        sb_dec(&h, (uint64_t)u->fk_up);
        sb_put(&h, "/F");
        sb_dec(&h, (uint64_t)u->fk_down);
        sb_put(&h, " scroll  F");
        sb_dec(&h, (uint64_t)u->fk_help);
        sb_put(&h, " help  RUN/STOP pause  x go+leave  /quit end  RET repeats z/n");
        put_sbuf(s, u->rows - 1, &h, UA_HINT);
    }
}

/* ---- keys -------------------------------------------------------------- */

static void line_set(ui_t *u, const char *t)
{
    u->clen = m_strlen(t);
    memcpy(u->cmd, t, (size_t)u->clen + 1);
    u->cpos = u->clen;
}

/* typing at the cursor pushes the rest of the line right */
static void line_insert(ui_t *u, char c)
{
    memmove(u->cmd + u->cpos + 1, u->cmd + u->cpos, (size_t)(u->clen - u->cpos + 1));
    u->cmd[u->cpos++] = c;
    u->clen++;
}

/* deletes the character at index i */
static void line_delete(ui_t *u, int i)
{
    memmove(u->cmd + i, u->cmd + i + 1, (size_t)(u->clen - i));
    u->clen--;
}

static void hist_push(ui_t *u, const char *line)
{
    if (u->cn && m_streq(u->chist[u->cn - 1], line)) {
        return; /* a repeat of the last line is kept once */
    }
    if (u->cn == UI_CHIST) {
        memmove(u->chist[0], u->chist[1], (size_t)(UI_CHIST - 1) * sizeof(u->chist[0]));
        u->cn--;
    }
    memcpy(u->chist[u->cn], line, (size_t)m_strlen(line) + 1);
    u->cn++;
}

/* the first word of a line, lower case; 0 when it has none */
static int first_word(const char *line, char *w, int cap, const char **rest)
{
    while (*line == ' ') {
        line++;
    }
    int n = 0;
    while (line[n] && (line[n] != ' ')) {
        if (n < cap - 1) {
            w[n] = m_lower(line[n]);
        }
        n++;
    }
    w[n < cap - 1 ? n : cap - 1] = 0;
    line += n;
    while (*line == ' ') {
        line++;
    }
    if (rest) {
        *rest = line;
    }
    return n;
}

static int is_step_verb(const char *w)
{
    return m_streq(w, "z") || m_streq(w, "n") || m_streq(w, "cs") || m_streq(w, "step") || m_streq(w, "next") ||
           m_streq(w, "so") || m_streq(w, "si");
}

static int is_page_verb(const char *w)
{
    return m_streq(w, "d") || m_streq(w, "disass") || m_streq(w, "m") || m_streq(w, "mem");
}

static char s_out[12 * 1024];

/* CTRL+I / TAB: completes the word before the cursor (verbs, then observer names, lenses, marks, paths). One
 * candidate is completed whole (and a space after it, a directory without); several are completed to what they
 * share, and when that adds nothing they are listed. */
static void complete_line(ui_t *u)
{
    static char cands[2048]; /* static: the screen runs on the UI task's stack */
    sbuf_t b;
    sb_init(&b, cands, (int)sizeof(cands) - 1);
    int start = 0;
    int n = mon_complete(u->cmd, u->cpos, &start, &b);
    sb_str(&b);
    if (!n) {
        return;
    }
    int typed = u->cpos - start;
    char lcp[UI_CMD + 1];
    int ln = 0;
    const char *p = cands;
    for (int i = 0; i < n; i++) {
        int wn = 0;
        while (p[wn] && (p[wn] != '\n')) {
            wn++;
        }
        if (!i) {
            ln = wn < UI_CMD ? wn : UI_CMD;
            memcpy(lcp, p, (size_t)ln);
        } else {
            int k = 0;
            while ((k < ln) && (k < wn) && (m_lower(lcp[k]) == m_lower(p[k]))) {
                k++;
            }
            ln = k;
        }
        p += wn + (p[wn] ? 1 : 0);
    }
    int space = (n == 1) && (ln > 0) && (lcp[ln - 1] != '/');
    if (n > 1 && ln <= typed) {
        static char row[2100];
        sbuf_t r;
        sb_init(&r, row, (int)sizeof(row) - 1);
        for (const char *q = cands; *q; q++) {
            sb_putc(&r, *q == '\n' ? ' ' : *q);
            if (*q == '\n') {
                sb_putc(&r, ' ');
            }
        }
        ui_print(u, sb_str(&r), UA_NORMAL);
    }
    if (ln < typed) {
        return;
    }
    int add = ln + (space ? 1 : 0);
    if (u->clen - typed + add > UI_CMD) {
        return;
    }
    char tail[UI_CMD + 1];
    int tn = u->clen - u->cpos;
    memcpy(tail, u->cmd + u->cpos, (size_t)tn);
    memcpy(u->cmd + start, lcp, (size_t)ln);
    if (space) {
        u->cmd[start + ln] = ' ';
    }
    memcpy(u->cmd + start + add, tail, (size_t)tn);
    u->clen = start + add + tn;
    u->cmd[u->clen] = 0;
    u->cpos = start + add;
    u->ci = -1;
}

/* "a c101 " while `a` assembles line by line: the prompt is prefilled the way VICE does. 0 outside assemble mode. */
static int asm_prefill(char *t)
{
    uint16_t a;
    if (!mon_asm_next(&a)) {
        return 0;
    }
    sbuf_t b;
    sb_init(&b, t, UI_CMD);
    sb_put(&b, "a ");
    sb_hex(&b, a, 4);
    sb_putc(&b, ' ');
    sb_str(&b);
    return 1;
}

static void prompt_prefill(ui_t *u)
{
    char t[UI_CMD + 1];
    if (!u->clen && asm_prefill(t)) {
        line_set(u, t);
    }
}

static void run_line(ui_t *u, const char *line)
{
    char w[16];
    const char *rest;
    first_word(line, w, sizeof(w), &rest);
    sbuf_t o;
    sb_init(&o, s_out, (int)sizeof(s_out) - 1);
    int rc = rpc_local_exec(line, &o);
    if (rc == MON_PENDING) {
        sbuf_t d;
        sb_init(&d, u->run_desc, (int)sizeof(u->run_desc));
        if (m_streq(w, "until")) {
            sb_put(&d, "until ");
            sb_put(&d, rest);
        } else {
            sb_put(&d, (w[0] == 'r') ? "ret" : "next");
        }
        sb_str(&d);
    } else {
        sb_str(&o);
        u->run_desc[0] = 0;
        ui_print(u, s_out, rc == MON_ERR ? UA_ERR : UA_NORMAL);
    }
    if (is_step_verb(w)) {
        memcpy(u->last_step, line, (size_t)m_strlen(line) + 1);
    } else if ((rc == MON_OK) && is_page_verb(w)) {
        /* RETURN goes on where `d` / `m` stopped: the bare verb continues from its cursor */
        memcpy(u->last_step, w, (size_t)m_strlen(w) + 1);
    }
    if (m_streq(w, "x") && (rc == MON_OK) && !*rest) {
        u->leave = UI_LEAVE_X;
    }
    if (mon_quit_requested()) {
        u->leave = UI_LEAVE_QUIT;
    }
    prompt_prefill(u);
}

/* the output rows on the screen now (the machine running shrinks the window) */
static int page(const ui_t *u)
{
    int top = run_top(u);
    int f, l;
    out_range(u, top, &f, &l);
    return l - f + 1;
}

int ui_key(ui_t *u, int key)
{
    switch (key) {
    case UI_K_RETURN: {
        /* trim */
        char line[UI_CMD + 1];
        int a = 0, n = u->clen;
        while ((a < n) && (u->cmd[a] == ' ')) {
            a++;
        }
        while ((n > a) && (u->cmd[n - 1] == ' ')) {
            n--;
        }
        memcpy(line, u->cmd + a, (size_t)(n - a));
        line[n - a] = 0;
        u->clen = 0;
        u->cpos = 0;
        u->cmd[0] = 0;
        u->ci = -1;
        {
            /* the prefilled prompt with nothing typed after it is the empty line that ends assemble mode */
            char pf[UI_CMD + 1];
            int pn;
            if (asm_prefill(pf) && ((pn = m_strlen(pf) - 1), m_strlen(line) == pn) && m_strnieq(line, pf, pn)) {
                line[0] = 0;
            }
        }
        if (!line[0]) {
            if (mon_asm_mode()) {
                /* the empty line ends assemble mode (mon.c); it never repeats a step there */
                run_line(u, "");
                break;
            }
            if (u->last_step[0] && (!M.running || is_page_verb(u->last_step))) {
                char echo[UI_CMD + 3];
                sbuf_t e;
                sb_init(&e, echo, sizeof(echo));
                sb_putc(&e, '.');
                sb_put(&e, u->last_step);
                ui_print(u, sb_str(&e), UA_ECHO);
                run_line(u, u->last_step);
            }
            break;
        }
        hist_push(u, line);
        char echo[UI_CMD + 3];
        sbuf_t e;
        sb_init(&e, echo, sizeof(echo));
        sb_putc(&e, '.');
        sb_put(&e, line);
        ui_print(u, sb_str(&e), UA_ECHO);
        run_line(u, line);
        break;
    }
    case UI_K_BACK:
        if (u->cpos) {
            line_delete(u, --u->cpos);
        }
        u->ci = -1;
        break;
    case UI_K_KILL:
        u->clen = 0;
        u->cpos = 0;
        u->cmd[0] = 0;
        u->ci = -1;
        break;
    case UI_K_LEFT:
        if (u->cpos) {
            u->cpos--;
        }
        break;
    case UI_K_RIGHT:
        if (u->cpos < u->clen) {
            u->cpos++;
        }
        break;
    case UI_K_COMPLETE: complete_line(u); break;
    case UI_K_HOME: u->cpos = 0; break;
    case UI_K_END: u->cpos = u->clen; break;
    case UI_K_UP:
        if (u->cn) {
            u->ci = (u->ci < 0) ? u->cn - 1 : (u->ci > 0 ? u->ci - 1 : 0);
            line_set(u, u->chist[u->ci]);
        }
        break;
    case UI_K_DOWN:
        if (u->ci >= 0) {
            if (u->ci + 1 < u->cn) {
                u->ci++;
                line_set(u, u->chist[u->ci]);
            } else {
                u->ci = -1;
                u->clen = 0;
                u->cpos = 0;
                u->cmd[0] = 0;
            }
        }
        break;
    case UI_K_PGUP: {
        /* no further than the oldest line at the top of the output area: ui_render clamps the same way, and a
         * count above that would take extra PageDowns to come back */
        int max = u->hn > page(u) ? u->hn - page(u) : 0;
        u->scroll += page(u);
        if (u->scroll > max) {
            u->scroll = max;
        }
        break;
    }
    case UI_K_PGDN:
        u->scroll -= page(u);
        if (u->scroll < 0) {
            u->scroll = 0;
        }
        break;
    case UI_K_HELP:
        ui_print(u, ".help", UA_ECHO);
        run_line(u, "help");
        break;
    case UI_K_STOP:
        if (M.running) {
            ui_print(u, ".pause", UA_ECHO);
            run_line(u, "pause");
        }
        break;
    default:
        if ((key >= 0x20) && (key < 0x7F) && (u->clen < UI_CMD)) {
            line_insert(u, (char)key);
            u->ci = -1;
        }
        break;
    }
    return u->leave;
}

int ui_tick(ui_t *u)
{
    int printed = 0;
    /* the log observers' lines as they come (metered in mach.c, so a flood is a few lines and "+K skipped") */
    const char *ll;
    for (int n = 0; (n < 24) && ((ll = mach_olog_next(&u->olog_cur)) != 0); n++) {
        ui_print(u, ll, UA_ECHO);
        printed = 1;
    }
    sbuf_t o;
    sb_init(&o, s_out, (int)sizeof(s_out) - 1);
    if (rpc_local_take(&o)) {
        sb_str(&o);
        u->run_desc[0] = 0;
        ui_print(u, s_out, UA_NORMAL);
        printed = 1;
    }
    int unclaimed = rpc_take_unclaimed();
    int stopped = u->was_run && !M.running;
    u->was_run = M.running;
    rpc_take_user_stop(); /* the screen is open: it saw this stop */
    if ((unclaimed || stopped) && !printed && !M.running) {
        sb_init(&o, s_out, (int)sizeof(s_out) - 1);
        mon_set_narrow(1);
        mon_stop_text(&o);
        mon_set_narrow(0);
        sb_str(&o);
        ui_print(u, s_out, UA_NORMAL);
        printed = 1;
    }
    return printed;
}

void ui_show_stop(ui_t *u)
{
    sbuf_t o;
    sb_init(&o, s_out, (int)sizeof(s_out) - 1);
    mon_set_narrow(1);
    mon_stop_text(&o);
    mon_set_narrow(0);
    sb_str(&o);
    ui_print(u, s_out, UA_NORMAL);
    u->was_run = M.running;
    rpc_take_unclaimed(); /* this stop is shown now: ui_tick must not print it a second time */
}

/* The firmware's key codes (u2p_app.h, keyboard.h; main.c checks them at build time) */
#define FK_BACK     0x08
#define FK_RETURN   0x0D
#define FK_BREAK    0x11
#define FK_DOWN     0x12
#define FK_UP       0x91
#define FK_PAGEUP   0x92
#define FK_PAGEDOWN 0x95
#define FK_DELETE   0x7F
#define FK_LARROW   0x60 /* the C64's left arrow key: the keymap gives '`' */
#define FK_HELP     0x1FB /* KEY_HELP (KEY_TASKS 0x1FC is ignored) */
#define FK_LEFT     0x9D
#define FK_RIGHT    0x1D
#define FK_HOME     0x13
#define FK_END      0x96
#define FK_TAB      0x09 /* a USB keyboard's TAB, and CTRL+I on the C64's */
#define FK_CTRL_TAP 0x1F8 /* CTRL alone on the C64's keyboard (ui->key_ctrl_tap) */

void ui_set_keys(ui_t *u, const int *fkey)
{
    u->fk_up = 1;
    u->fk_down = 7;
    u->fk_help = 3;
    for (int n = 1; fkey && (n <= 8); n++) {
        if (fkey[n] == FK_PAGEUP) {
            u->fk_up = n;
        } else if (fkey[n] == FK_PAGEDOWN) {
            u->fk_down = n;
        } else if (fkey[n] == FK_HELP) {
            u->fk_help = n;
        }
    }
}

int ui_map_key(int k)
{
    if (k < 0) {
        return 0;
    }
    switch (k) {
    case FK_RETURN: return UI_K_RETURN;
    case FK_BACK:
    case FK_DELETE: return UI_K_BACK;
    case FK_LEFT: return UI_K_LEFT;
    case FK_RIGHT: return UI_K_RIGHT;
    case FK_HOME: return UI_K_HOME;
    case FK_END: return UI_K_END;
    case FK_TAB:
    case FK_CTRL_TAP: return UI_K_COMPLETE;
    case FK_LARROW: return UI_K_KILL;
    case FK_BREAK: return UI_K_STOP;
    case FK_UP: return UI_K_UP;
    case FK_DOWN: return UI_K_DOWN;
    case FK_PAGEUP: return UI_K_PGUP;
    case FK_PAGEDOWN: return UI_K_PGDN;
    case FK_HELP: return UI_K_HELP;
    default: break;
    }
    return ((k >= 0x20) && (k < 0x7F)) ? k : 0;
}
