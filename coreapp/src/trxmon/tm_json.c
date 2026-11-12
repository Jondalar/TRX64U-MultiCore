/*
 * json.c — JSON reader and writer (see json.h).
 */
#include "json.h"

typedef struct
{
    const char *s;
    int len, pos;
    jtok_t *t;
    int n, max;
    int err;
} jp_t;

static void skip_ws(jp_t *p)
{
    while ((p->pos < p->len) &&
           ((p->s[p->pos] == ' ') || (p->s[p->pos] == '\t') || (p->s[p->pos] == '\n') || (p->s[p->pos] == '\r'))) {
        p->pos++;
    }
}

static int new_tok(jp_t *p, int type, int start)
{
    if (p->n >= p->max) {
        p->err = -2;
        return -1;
    }
    jtok_t *k = &p->t[p->n];
    k->type = (uint8_t)type;
    k->start = start;
    k->end = start;
    k->size = 0;
    k->next = p->n + 1;
    return p->n++;
}

static int parse_value(jp_t *p, int depth);

static int parse_string(jp_t *p)
{
    /* at the opening quote */
    int i = new_tok(p, JT_STR, p->pos + 1);
    if (i < 0) {
        return -1;
    }
    p->pos++;
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == '"') {
            p->t[i].end = p->pos;
            p->pos++;
            return i;
        }
        if ((unsigned char)c < 0x20) {
            break;
        }
        if (c == '\\') {
            p->pos++;
            if (p->pos >= p->len) {
                break;
            }
            c = p->s[p->pos];
            if (c == 'u') {
                if (p->pos + 4 >= p->len) {
                    break;
                }
                p->pos += 4;
            } else if (!((c == '"') || (c == '\\') || (c == '/') || (c == 'b') || (c == 'f') || (c == 'n') ||
                         (c == 'r') || (c == 't'))) {
                break;
            }
        }
        p->pos++;
    }
    p->err = -1;
    return -1;
}

static int parse_value(jp_t *p, int depth)
{
    if (depth > 20) {
        p->err = -1;
        return -1;
    }
    skip_ws(p);
    if (p->pos >= p->len) {
        p->err = -1;
        return -1;
    }
    char c = p->s[p->pos];
    if (c == '"') {
        return parse_string(p);
    }
    if ((c == '{') || (c == '[')) {
        int obj = (c == '{');
        int i = new_tok(p, obj ? JT_OBJ : JT_ARR, p->pos);
        if (i < 0) {
            return -1;
        }
        p->pos++;
        skip_ws(p);
        if ((p->pos < p->len) && (p->s[p->pos] == (obj ? '}' : ']'))) {
            p->pos++;
            p->t[i].end = p->pos;
            p->t[i].next = p->n;
            return i;
        }
        for (;;) {
            if (obj) {
                skip_ws(p);
                if ((p->pos >= p->len) || (p->s[p->pos] != '"')) {
                    p->err = -1;
                    return -1;
                }
                if (parse_string(p) < 0) {
                    return -1;
                }
                skip_ws(p);
                if ((p->pos >= p->len) || (p->s[p->pos] != ':')) {
                    p->err = -1;
                    return -1;
                }
                p->pos++;
            }
            if (parse_value(p, depth + 1) < 0) {
                return -1;
            }
            p->t[i].size++;
            skip_ws(p);
            if (p->pos >= p->len) {
                p->err = -1;
                return -1;
            }
            c = p->s[p->pos++];
            if (c == ',') {
                continue;
            }
            if (c == (obj ? '}' : ']')) {
                break;
            }
            p->err = -1;
            return -1;
        }
        p->t[i].end = p->pos;
        p->t[i].next = p->n;
        return i;
    }
    if ((c == '-') || ((c >= '0') && (c <= '9'))) {
        int i = new_tok(p, JT_NUM, p->pos);
        if (i < 0) {
            return -1;
        }
        p->pos++;
        while (p->pos < p->len) {
            c = p->s[p->pos];
            if (((c >= '0') && (c <= '9')) || (c == '.') || (c == 'e') || (c == 'E') || (c == '+') || (c == '-')) {
                p->pos++;
            } else {
                break;
            }
        }
        p->t[i].end = p->pos;
        return i;
    }
    static const struct
    {
        const char *w;
        int type;
    } words[] = { { "true", JT_TRUE }, { "false", JT_FALSE }, { "null", JT_NULL } };
    for (int k = 0; k < 3; k++) {
        int n = m_strlen(words[k].w);
        if ((p->pos + n <= p->len) && (memcmp(p->s + p->pos, words[k].w, (size_t)n) == 0)) {
            int i = new_tok(p, words[k].type, p->pos);
            if (i < 0) {
                return -1;
            }
            p->pos += n;
            p->t[i].end = p->pos;
            return i;
        }
    }
    p->err = -1;
    return -1;
}

int json_parse(const char *s, int len, jtok_t *t, int max)
{
    jp_t p;
    p.s = s;
    p.len = len;
    p.pos = 0;
    p.t = t;
    p.n = 0;
    p.max = max;
    p.err = 0;
    if (parse_value(&p, 0) < 0) {
        return p.err ? p.err : -1;
    }
    skip_ws(&p);
    if (p.pos != len) {
        return -1;
    }
    return p.n;
}

int json_is(const jtok_t *t, int i, int type)
{
    return (i >= 0) && (t[i].type == type);
}

int json_streq(const char *s, const jtok_t *t, int i, const char *lit)
{
    if (!json_is(t, i, JT_STR)) {
        return 0;
    }
    int n = t[i].end - t[i].start;
    return (n == m_strlen(lit)) && (memcmp(s + t[i].start, lit, (size_t)n) == 0);
}

int json_get(const char *s, const jtok_t *t, int obj, const char *key)
{
    if (!json_is(t, obj, JT_OBJ)) {
        return -1;
    }
    int k = obj + 1;
    for (int m = 0; m < t[obj].size; m++) {
        int v = t[k].next;
        if (json_streq(s, t, k, key)) {
            return v;
        }
        k = t[v].next;
    }
    return -1;
}

int json_item(const jtok_t *t, int arr, int k)
{
    if (!json_is(t, arr, JT_ARR) || (k < 0) || (k >= t[arr].size)) {
        return -1;
    }
    int i = arr + 1;
    while (k--) {
        i = t[i].next;
    }
    return i;
}

int json_u64(const char *s, const jtok_t *t, int i, uint64_t *v)
{
    if (!json_is(t, i, JT_NUM)) {
        return 0;
    }
    uint64_t x = 0;
    int n = t[i].end - t[i].start;
    const char *p = s + t[i].start;
    if ((n <= 0) || (n > 19)) {
        return 0;
    }
    for (int k = 0; k < n; k++) {
        if ((p[k] < '0') || (p[k] > '9')) {
            return 0; /* negative, fraction or exponent */
        }
        x = x * 10 + (uint64_t)(p[k] - '0');
    }
    *v = x;
    return 1;
}

int json_u32(const char *s, const jtok_t *t, int i, uint32_t *v)
{
    uint64_t x;
    if (!json_u64(s, t, i, &x) || (x > 0xFFFFFFFFull)) {
        return 0;
    }
    *v = (uint32_t)x;
    return 1;
}

int json_str(const char *s, const jtok_t *t, int i, char *buf, int cap)
{
    if (!json_is(t, i, JT_STR) || (cap <= 0)) {
        return -1;
    }
    int o = 0;
    for (int k = t[i].start; k < t[i].end; k++) {
        char c = s[k];
        if (c == '\\') {
            c = s[++k];
            switch (c) {
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 'n': c = 10; break;
            case 'r': c = 13; break;
            case 't': c = 9; break;
            case 'u': {
                uint32_t u = 0;
                m_parse_hex(s + k + 1, 4, &u);
                k += 4;
                c = (u < 0x80) ? (char)u : '?';
                break;
            }
            default: break; /* " \ / */
            }
        }
        if (o + 1 >= cap) {
            return -1;
        }
        buf[o++] = c;
    }
    buf[o] = 0;
    return o;
}

/* Writer */

void jw_init(jw_t *w, sbuf_t *b)
{
    w->b = b;
    w->depth = 0;
    w->after_key = 0;
    w->first[0] = 1;
}

static void sep(jw_t *w)
{
    if (w->after_key) {
        w->after_key = 0;
        return;
    }
    if (w->depth > 0) {
        if (!w->first[w->depth]) {
            sb_putc(w->b, ',');
        }
        w->first[w->depth] = 0;
    }
}

static void jw_open(jw_t *w, char c)
{
    sep(w);
    sb_putc(w->b, c);
    if (w->depth < (int)sizeof(w->first) - 1) {
        w->depth++;
    }
    w->first[w->depth] = 1;
}

void jw_obj(jw_t *w) { jw_open(w, '{'); }
void jw_arr(jw_t *w) { jw_open(w, '['); }

void jw_end_obj(jw_t *w)
{
    sb_putc(w->b, '}');
    if (w->depth > 0) {
        w->depth--;
    }
}

void jw_end_arr(jw_t *w)
{
    sb_putc(w->b, ']');
    if (w->depth > 0) {
        w->depth--;
    }
}

static void put_escaped(sbuf_t *b, const char *s, int n)
{
    sb_putc(b, '"');
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            sb_put(b, "\\\"");
        } else if (c == '\\') {
            sb_put(b, "\\\\");
        } else if (c == '\n') {
            sb_put(b, "\\n");
        } else if (c == '\r') {
            sb_put(b, "\\r");
        } else if (c == '\t') {
            sb_put(b, "\\t");
        } else if ((c < 0x20) || (c == 0x7F)) { /* bytes from 0x80 are UTF-8, passed on */
            sb_put(b, "\\u00");
            sb_hex(b, c, 2);
        } else {
            sb_putc(b, (char)c);
        }
    }
    sb_putc(b, '"');
}

void jw_key(jw_t *w, const char *k)
{
    sep(w);
    put_escaped(w->b, k, m_strlen(k));
    sb_putc(w->b, ':');
    w->after_key = 1;
}

void jw_strn(jw_t *w, const char *s, int n)
{
    sep(w);
    put_escaped(w->b, s, n);
}

void jw_str(jw_t *w, const char *s) { jw_strn(w, s, m_strlen(s)); }

void jw_u64(jw_t *w, uint64_t v)
{
    sep(w);
    sb_dec(w->b, v);
}

void jw_i64(jw_t *w, int64_t v)
{
    sep(w);
    sb_sdec(w->b, v);
}

void jw_bool(jw_t *w, int v)
{
    sep(w);
    sb_put(w->b, v ? "true" : "false");
}

void jw_null(jw_t *w)
{
    sep(w);
    sb_put(w->b, "null");
}

void jw_raw(jw_t *w, const char *s, int n)
{
    sep(w);
    sb_putn(w->b, s, n);
}

void jw_kstr(jw_t *w, const char *k, const char *v)
{
    jw_key(w, k);
    jw_str(w, v);
}

void jw_ku64(jw_t *w, const char *k, uint64_t v)
{
    jw_key(w, k);
    jw_u64(w, v);
}

void jw_kbool(jw_t *w, const char *k, int v)
{
    jw_key(w, k);
    jw_bool(w, v);
}

void jw_knull(jw_t *w, const char *k)
{
    jw_key(w, k);
    jw_null(w);
}
