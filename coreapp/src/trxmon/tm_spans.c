/*
 * spans.c — address marks and their spans (see spans.h).
 *
 * Mark: START role,lens,addr4,len4 SEP visible END (the space is always
 * c64 here: no drive device yet).
 */
#include "spans.h"

static const char *const role_names[] = { "pc", "target", "operand", "memory" };

void span_mark(sbuf_t *b, const char *visible, int vlen, uint16_t addr, int role, const char *lens, int len)
{
    sb_putc(b, SPAN_START);
    sb_putc(b, (char)('0' + role));
    sb_putc(b, ',');
    if (lens && !m_streq(lens, "cpu")) {
        sb_put(b, lens);
    }
    sb_putc(b, ',');
    sb_hex(b, addr, 4);
    sb_putc(b, ',');
    sb_hex(b, (uint32_t)(len < 1 ? 1 : len), 4);
    sb_putc(b, SPAN_SEP);
    sb_putn(b, visible, vlen);
    sb_putc(b, SPAN_END);
}

void span_addr4(sbuf_t *b, uint16_t addr, int role)
{
    char t[6];
    sbuf_t s;
    sb_init(&s, t, sizeof(t));
    sb_putc(&s, '$');
    sb_hex(&s, addr, 4);
    span_mark(b, t, 5, addr, role, 0, 1);
}

int span_strip(char *text, int len, jw_t *w)
{
    int o = 0;
    int line = 0, col = 0;
    jw_arr(w);
    for (int i = 0; i < len;) {
        char c = text[i];
        if (c != SPAN_START) {
            text[o++] = c;
            i++;
            if (c == '\n') {
                line++;
                col = 0;
            } else {
                col++;
            }
            continue;
        }
        /* START role , lens , addr4 , len4 SEP */
        int k = i + 1;
        int role = text[k] - '0';
        k += 2;
        int ls = k;
        while ((k < len) && (text[k] != ',')) {
            k++;
        }
        int le = k;
        k++;
        uint32_t addr = 0, n = 1;
        m_parse_hex(text + k, 4, &addr);
        k += 5;
        m_parse_hex(text + k, 4, &n);
        k += 4;
        if ((k >= len) || (text[k] != SPAN_SEP) || (role < 0) || (role > 3)) {
            i++; /* not a mark after all */
            continue;
        }
        k++;
        char lens[8];
        int ln = le - ls;
        if (ln > (int)sizeof(lens)) {
            ln = (int)sizeof(lens);
        }
        memcpy(lens, text + ls, (size_t)ln); /* the copy below may overwrite it */
        int start = col;
        while ((k < len) && (text[k] != SPAN_END)) {
            text[o++] = text[k++];
            col++;
        }
        i = k + 1;
        jw_obj(w);
        jw_ku64(w, "line", (uint64_t)line);
        jw_ku64(w, "start", (uint64_t)start);
        jw_ku64(w, "end", (uint64_t)col);
        jw_ku64(w, "addr", addr);
        jw_kstr(w, "space", "c64");
        jw_kstr(w, "role", role_names[role]);
        if (ln > 0) {
            jw_key(w, "lens");
            jw_strn(w, lens, ln);
        }
        if (n > 1) {
            jw_ku64(w, "len", n);
        }
        jw_end_obj(w);
    }
    jw_end_arr(w);
    return o;
}
