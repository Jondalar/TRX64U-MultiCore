/*
 * mlib.c — string and number helpers (see mlib.h).
 */
#include "mlib.h"

int m_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

int m_streq(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

char m_lower(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? (char)(c + 32) : c;
}

int m_strieq(const char *a, const char *b)
{
    while (*a && (m_lower(*a) == m_lower(*b))) {
        a++;
        b++;
    }
    return m_lower(*a) == m_lower(*b);
}

int m_strnieq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (m_lower(a[i]) != m_lower(b[i])) {
            return 0;
        }
        if (!a[i]) {
            return 1;
        }
    }
    return 1;
}

static int hexval(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    c = m_lower(c);
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    return -1;
}

int m_parse_hex(const char *t, int n, uint32_t *out)
{
    if (n < 0) {
        n = m_strlen(t);
    }
    if ((n > 0) && (t[0] == '$')) {
        t++;
        n--;
    }
    if ((n <= 0) || (n > 8)) {
        return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        int d = hexval(t[i]);
        if (d < 0) {
            return 0;
        }
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return 1;
}

int m_parse_dec(const char *t, int n, uint32_t *out)
{
    if (n < 0) {
        n = m_strlen(t);
    }
    if ((n <= 0) || (n > 9)) {
        return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if ((t[i] < '0') || (t[i] > '9')) {
            return 0;
        }
        v = v * 10 + (uint32_t)(t[i] - '0');
    }
    *out = v;
    return 1;
}

void sb_init(sbuf_t *b, char *mem, int cap)
{
    b->p = mem;
    b->cap = cap;
    b->len = 0;
    b->full = 0;
    if (cap > 0) {
        mem[0] = 0;
    }
}

void sb_reset(sbuf_t *b)
{
    b->len = 0;
    b->full = 0;
}

void sb_putc(sbuf_t *b, char c)
{
    if (b->len + 1 < b->cap) {
        b->p[b->len++] = c;
    } else {
        b->full = 1;
    }
}

void sb_putn(sbuf_t *b, const char *s, int n)
{
    int room = b->cap - 1 - b->len;
    if (n > room) {
        n = room;
        b->full = 1;
    }
    if (n > 0) {
        memcpy(b->p + b->len, s, (size_t)n);
        b->len += n;
    }
}

void sb_put(sbuf_t *b, const char *s)
{
    sb_putn(b, s, m_strlen(s));
}

static void hex_digits(sbuf_t *b, uint32_t v, int digits, const char *set)
{
    char t[8];
    if (digits > 8) {
        digits = 8;
    }
    for (int i = digits - 1; i >= 0; i--) {
        t[i] = set[v & 15];
        v >>= 4;
    }
    sb_putn(b, t, digits);
}

void sb_hex(sbuf_t *b, uint32_t v, int digits)
{
    hex_digits(b, v, digits, "0123456789abcdef");
}

void sb_HEX(sbuf_t *b, uint32_t v, int digits)
{
    hex_digits(b, v, digits, "0123456789ABCDEF");
}

void sb_hexmin(sbuf_t *b, uint32_t v)
{
    int d = 1;
    while ((d < 8) && (v >> (4 * d))) {
        d++;
    }
    hex_digits(b, v, d, "0123456789abcdef");
}

void sb_dec(sbuf_t *b, uint64_t v)
{
    char t[20];
    int n = 0;
    do {
        t[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v && (n < 20));
    while (n) {
        sb_putc(b, t[--n]);
    }
}

void sb_sdec(sbuf_t *b, int64_t v)
{
    if (v < 0) {
        sb_putc(b, '-');
        sb_dec(b, (uint64_t)(-v));
    } else {
        sb_dec(b, (uint64_t)v);
    }
}

void sb_spaces(sbuf_t *b, int n)
{
    while (n-- > 0) {
        sb_putc(b, ' ');
    }
}

const char *sb_str(sbuf_t *b)
{
    if (b->cap > 0) {
        b->p[b->len] = 0;
    }
    return b->p;
}
