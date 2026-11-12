#include "uic.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---- writer --------------------------------------------------------------------------------------------------------------------------
void UicW::raw(const char *s)
{
    int n = (int)strlen(s);
    if (!ok || p + n >= cap) {
        ok = false;
        return;
    }
    memcpy(out + p, s, n + 1);
    p += n;
}

void UicW::fmt(const char *f, ...)
{
    if (!ok) {
        return;
    }
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(out + p, cap - p, f, ap);
    va_end(ap);
    if (n < 0 || n >= cap - p) {
        ok = false;
        out[p] = 0;
        return;
    }
    p += n;
}

// The length of the valid UTF-8 sequence at s, 0 when the byte does not start one.
static int utf8_len(const unsigned char *s)
{
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF && (s[1] & 0xC0) == 0x80) return 2;
    if (s[0] >= 0xE0 && s[0] <= 0xEF && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) return 3;
    if (s[0] >= 0xF0 && s[0] <= 0xF4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) return 4;
    return 0;
}

void UicW::str(const char *text)
{
    raw("\"");
    for (const unsigned char *s = (const unsigned char *)(text ? text : ""); *s && ok;) {
        if (*s == '"' || *s == '\\') {
            char e[3] = { '\\', (char)*s, 0 };
            raw(e);
            s++;
        } else if (*s < 0x20) {
            fmt("\\u%04x", (unsigned)*s);
            s++;
        } else if (*s < 0x80) {
            char e[2] = { (char)*s, 0 };
            raw(e);
            s++;
        } else {
            int l = utf8_len(s);
            if (!l) {
                raw("?");
                s++;
            } else {
                if (!ok || p + l >= cap) {
                    ok = false;
                    return;
                }
                memcpy(out + p, s, l);
                p += l;
                out[p] = 0;
                s += l;
            }
        }
    }
    raw("\"");
}

void UicW::sep()
{
    if (p > 0 && out[p - 1] != '{' && out[p - 1] != '[') {
        raw(",");
    }
}
void UicW::open(char c) { char e[2] = { c, 0 }; raw(e); }
void UicW::close(char c) { char e[2] = { c, 0 }; raw(e); }
void UicW::key(const char *k) { raw("\""); raw(k); raw("\":"); }
void UicW::ks(const char *k, const char *v) { sep(); key(k); str(v); }
void UicW::ki(const char *k, int v) { sep(); key(k); fmt("%d", v); }
void UicW::ku(const char *k, uint32_t v) { sep(); key(k); fmt("%u", (unsigned)v); }
void UicW::kb(const char *k, bool v) { sep(); key(k); raw(v ? "true" : "false"); }
void UicW::kopen(const char *k, char c) { sep(); key(k); open(c); }

// ---- flat reader -----------------------------------------------------------------------------------------------------------------------
static const char *skip_ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

// p at an opening quote; copies the unescaped text (cut to cap-1) and returns the position after the closing quote, 0 on a broken string.
static const char *read_string(const char *p, const char *e, char *out, int cap)
{
    int o = 0;
    p++;
    while (p < e && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < e) {
            char x = *p++;
            switch (x) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned v = 0;
                for (int i = 0; i < 4 && p < e; i++, p++) {
                    char h = *p;
                    v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0);
                }
                c = v < 0x80 ? (char)v : '?';
                break;
            }
            default: c = x;
            }
        }
        if (out && o < cap - 1) out[o++] = c;
    }
    if (p >= e) return 0;
    if (out && cap > 0) out[o] = 0;
    return p + 1;
}

// Skips one value; returns the position after it, 0 when broken.
static const char *skip_value(const char *p, const char *e)
{
    p = skip_ws(p, e);
    if (p >= e) return 0;
    if (*p == '"') return read_string(p, e, 0, 0);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < e) {
            if (*p == '"') {
                p = read_string(p, e, 0, 0);
                if (!p) return 0;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            if (*p == '}' || *p == ']') {
                if (--depth == 0) return p + 1;
            }
            p++;
        }
        return 0;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n') p++;
    return p;
}

int uic_json_get(const char *js, int n, const char *key, char *out, int cap)
{
    const char *p = js, *e = js + n;
    if (cap > 0) out[0] = 0;
    p = skip_ws(p, e);
    if (p >= e || *p != '{') return 0;
    p++;
    for (;;) {
        p = skip_ws(p, e);
        if (p >= e || *p == '}') return 0;
        if (*p == ',') { p++; continue; }
        if (*p != '"') return 0;
        char k[40];
        p = read_string(p, e, k, sizeof(k));
        if (!p) return 0;
        p = skip_ws(p, e);
        if (p >= e || *p != ':') return 0;
        p = skip_ws(p + 1, e);
        if (p >= e) return 0;
        bool want = !strcmp(k, key);
        if (*p == '"') {
            const char *q = read_string(p, e, want ? out : 0, want ? cap : 0);
            if (!q) return 0;
            if (want) return 1;
            p = q;
        } else if (*p == '{' || *p == '[') {
            p = skip_value(p, e);
            if (!p) return 0;
        } else {
            const char *q = skip_value(p, e);
            if (!q) return 0;
            if (want) {
                int l = (int)(q - p);
                if (l >= cap) l = cap - 1;
                memcpy(out, p, l);
                out[l] = 0;
                return (*p == '-' || (*p >= '0' && *p <= '9')) ? 2 : 3;
            }
            p = q;
        }
    }
}

bool uic_json_raw(const char *js, int n, const char *key, char *out, int cap)
{
    const char *p = js, *e = js + n;
    if (cap > 0) out[0] = 0;
    p = skip_ws(p, e);
    if (p >= e || *p != '{') return false;
    p++;
    for (;;) {
        p = skip_ws(p, e);
        if (p >= e || *p == '}') return false;
        if (*p == ',') { p++; continue; }
        if (*p != '"') return false;
        char k[40];
        p = read_string(p, e, k, sizeof(k));
        if (!p) return false;
        p = skip_ws(p, e);
        if (p >= e || *p != ':') return false;
        p = skip_ws(p + 1, e);
        if (p >= e) return false;
        const char *q = skip_value(p, e);
        if (!q) return false;
        if (!strcmp(k, key)) {
            if (*p != '{' && *p != '[') return false;
            int l = (int)(q - p);
            if (l >= cap) return false;
            memcpy(out, p, l);
            out[l] = 0;
            return true;
        }
        p = q;
    }
}

// ---- ids -------------------------------------------------------------------------------------------------------------------------------
static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    return h;
}

void uic_id_make(const char *prefix, const char *raw, char *out, int cap)
{
    // room for "-xxxx" behind the body
    int pl = (int)strlen(prefix);
    int lim = (cap < UIC_ID_MAX + 1 ? cap : UIC_ID_MAX + 1) - 1;
    int o = 0;
    bool changed = false;
    for (int i = 0; i < pl && o < lim; i++) out[o++] = prefix[i];
    const int body_max = lim - 5;
    for (const char *s = raw; *s; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') { c = (char)(c + 32); }          // case alone changes nothing: the names of one folder differ by more than case
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) {
            c = '_';
            changed = true;
        }
        if (o >= body_max) { changed = true; break; }
        out[o++] = c;
    }
    if (changed) {
        static const char X[] = "0123456789abcdef";                 // by hand: the board's printf writes %x in capitals
        uint32_t h = fnv(raw);
        if (o + 6 <= cap) {
            out[o++] = '-';
            for (int sh = 12; sh >= 0; sh -= 4) out[o++] = X[(h >> sh) & 15];
        }
    }
    out[o] = 0;
}

bool uic_id_valid(const char *id)
{
    int n = (int)strlen(id);
    if (n < 1 || n > UIC_ID_MAX) return false;
    for (; *id; id++) {
        char c = *id;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) return false;
    }
    return true;
}
