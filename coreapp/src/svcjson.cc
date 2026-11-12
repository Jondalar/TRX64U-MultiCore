#include "svcjson.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const JsonVal *JsonObj::get(const char *k) const
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(key[i], k)) {
            return &v[i];
        }
    }
    return 0;
}

namespace {

struct P {
    const char *s;
    int n, i;
    char *why;
    int whylen;
    int fail(const char *m) { snprintf(why, whylen, "%s (at byte %d)", m, i); return -1; }
    void ws() { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; }
};

int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// A string at p->i (on the opening quote) into out[0..max), NUL terminated. \uXXXX below 0x80 is that character, above it '?'
// (the names and paths of this machine are ASCII, as the manifest reader's are).
int str(P *p, char *out, int max, int *len)
{
    if (p->i >= p->n || p->s[p->i] != '"') {
        return p->fail("a string is expected");
    }
    p->i++;
    int o = 0;
    while (p->i < p->n && p->s[p->i] != '"') {
        unsigned char c = (unsigned char)p->s[p->i];
        if (c < 0x20) {
            return p->fail("a control character in a string");
        }
        char ch = (char)c;
        if (c == '\\') {
            if (p->i + 1 >= p->n) {
                return p->fail("a string ends in a backslash");
            }
            char e = p->s[p->i + 1];
            p->i++;
            switch (e) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case '"': case '\\': case '/': ch = e; break;
            case 'u': {
                if (p->i + 4 >= p->n) {
                    return p->fail("a \\u escape is cut off");
                }
                int v = 0;
                for (int k = 1; k <= 4; k++) {
                    int h = hexv(p->s[p->i + k]);
                    if (h < 0) {
                        return p->fail("a \\u escape is not hex");
                    }
                    v = v * 16 + h;
                }
                p->i += 4;
                ch = v && v < 0x80 ? (char)v : '?';
                break;
            }
            default:
                return p->fail("an escape this reader does not know");
            }
        }
        if (o >= max - 1) {
            return p->fail("a string is longer than this reader keeps");
        }
        out[o++] = ch;
        p->i++;
    }
    if (p->i >= p->n) {
        return p->fail("a string is not closed");
    }
    p->i++;
    out[o] = 0;
    *len = o;
    return 0;
}

// Skips one JSON value of any shape (strings honoured), for J_RAW; false when it is not closed.
bool skip_value(P *p)
{
    p->ws();
    int depth = 0;
    int i = p->i;
    if (i >= p->n) {
        return false;
    }
    if (p->s[i] != '[' && p->s[i] != '{') {
        return false;
    }
    for (; i < p->n; i++) {
        char c = p->s[i];
        if (c == '"') {
            for (i++; i < p->n && p->s[i] != '"'; i++) {
                if (p->s[i] == '\\') {
                    i++;
                }
            }
            if (i >= p->n) {
                return false;
            }
        } else if (c == '[' || c == '{') {
            if (++depth > 6) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            if (--depth == 0) {
                p->i = i + 1;
                return true;
            }
        }
    }
    return false;
}

int value(P *p, JsonObj *o, JsonVal *v)
{
    p->ws();
    if (p->i >= p->n) {
        return p->fail("a value is expected");
    }
    char c = p->s[p->i];
    memset(v, 0, sizeof(*v));
    if (c == '[' || c == '{') {
        int start = p->i;
        if (!skip_value(p)) {
            return p->fail("an array or object is not closed or nests too deep");
        }
        v->t = J_RAW;
        v->s = p->s + start;
        v->n = p->i - start;
        return 0;
    }
    if (c == '"') {
        int len;
        char *dst = o->buf + o->used;
        if (str(p, dst, JSON_BUF_MAX - o->used, &len)) {
            return -1;
        }
        v->t = J_STR;
        v->s = dst;
        o->used += len + 1;
        return 0;
    }
    if (p->n - p->i >= 4 && !strncmp(p->s + p->i, "true", 4)) {
        v->t = J_BOOL;
        v->b = true;
        p->i += 4;
        return 0;
    }
    if (p->n - p->i >= 5 && !strncmp(p->s + p->i, "false", 5)) {
        v->t = J_BOOL;
        p->i += 5;
        return 0;
    }
    if (p->n - p->i >= 4 && !strncmp(p->s + p->i, "null", 4)) {
        v->t = J_NULL;
        p->i += 4;
        return 0;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char num[24];
        int k = 0;
        bool frac = false;
        while (p->i < p->n && k < (int)sizeof(num) - 1 &&
               ((p->s[p->i] >= '0' && p->s[p->i] <= '9') || p->s[p->i] == '-' || p->s[p->i] == '+' || p->s[p->i] == '.' || p->s[p->i] == 'e' ||
                p->s[p->i] == 'E')) {
            frac |= p->s[p->i] == '.' || p->s[p->i] == 'e' || p->s[p->i] == 'E';
            num[k++] = p->s[p->i++];
        }
        num[k] = 0;
        char *end;
        long long x = strtoll(num, &end, 10);
        if (k == 0 || (!frac && *end) || x < -2147483648LL || x > 2147483647LL) {
            return p->fail("a number is not an integer of 32 bits");
        }
        v->t = J_NUM;
        v->n = (int32_t)x;
        v->frac = frac;
        return 0;
    }
    return p->fail("a value of this reader's kinds is expected (string, integer, true, false, null)");
}

int members(P *p, JsonObj *o, JsonObj *sub_for_params, bool *saw_params, JsonRpcReq *rpc)
{
    p->ws();
    if (p->i >= p->n || p->s[p->i] != '{') {
        return p->fail("an object is expected");
    }
    p->i++;
    p->ws();
    if (p->i < p->n && p->s[p->i] == '}') {
        p->i++;
        return 0;
    }
    for (;;) {
        p->ws();
        char key[JSON_KEY_MAX];
        int kl;
        if (str(p, key, sizeof(key), &kl)) {
            return -1;
        }
        p->ws();
        if (p->i >= p->n || p->s[p->i] != ':') {
            return p->fail("':' is expected");
        }
        p->i++;
        p->ws();
        if (rpc && !strcmp(key, "params")) {
            if (members(p, sub_for_params, 0, 0, 0)) {
                return -1;
            }
            *saw_params = true;
        } else if (rpc && !strcmp(key, "id")) {
            // the id is echoed as it came: a number or a string, kept as JSON text
            int start = p->i;
            JsonVal v;
            if (value(p, o, &v)) {
                return -1;
            }
            if (v.t != J_NUM && v.t != J_STR) {
                return p->fail("an id is a number or a string");
            }
            if (p->i - start >= (int)sizeof(rpc->id)) {
                return p->fail("an id is too long");
            }
            memcpy(rpc->id, p->s + start, p->i - start);
            rpc->id[p->i - start] = 0;
            rpc->has_id = true;
        } else {
            if (o->n >= JSON_KV_MAX) {
                return p->fail("more members than this reader keeps");
            }
            for (int k = 0; k < o->n; k++) {
                if (!strcmp(o->key[k], key)) {
                    return p->fail("a name twice");
                }
            }
            if (value(p, o, &o->v[o->n])) {
                return -1;
            }
            memcpy(o->key[o->n], key, kl + 1);
            o->n++;
        }
        p->ws();
        if (p->i < p->n && p->s[p->i] == ',') {
            p->i++;
            continue;
        }
        if (p->i < p->n && p->s[p->i] == '}') {
            p->i++;
            return 0;
        }
        return p->fail("',' or '}' is expected");
    }
}

}

int json_parse_object(const char *s, int n, JsonObj *o, char *why, int whylen)
{
    memset(o, 0, sizeof(*o));
    P p = { s, n, 0, why, whylen };
    if (members(&p, o, 0, 0, 0)) {
        return -1;
    }
    p.ws();
    if (p.i != n) {
        return p.fail("text behind the object");
    }
    return 0;
}

int json_parse_rpc(const char *s, int n, JsonRpcReq *r, int *code, char *why, int whylen)
{
    memset(r, 0, sizeof(*r));
    JsonObj top;
    memset(&top, 0, sizeof(top));
    P p = { s, n, 0, why, whylen };
    bool params = false;
    *code = -32700;
    if (members(&p, &top, &r->params, &params, r)) {
        return -1;
    }
    p.ws();
    if (p.i != n) {
        p.fail("text behind the request");
        return -1;
    }
    *code = -32600;
    const JsonVal *m = top.get("method");
    const JsonVal *ver = top.get("jsonrpc");
    if (!m || m->t != J_STR || !m->s[0] || strlen(m->s) >= sizeof(r->method)) {
        snprintf(why, whylen, "the request has no method");
        return -1;
    }
    memcpy(r->method, m->s, strlen(m->s) + 1);
    r->version_ok = ver && ver->t == J_STR && !strcmp(ver->s, "2.0");
    r->has_params = params;
    return 0;
}

bool json_put_string(char *out, int cap, int *pos, const char *text)
{
    int o = *pos;
    if (o + 1 >= cap) {
        return false;
    }
    out[o++] = '"';
    for (const char *p = text ? text : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char esc[8];
        int el = 0;
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
            el = 2;
        } else if (c < 0x20) {
            el = snprintf(esc, sizeof(esc), "\\u%04x", c);
        } else {
            esc[0] = (char)c;
            el = 1;
        }
        if (o + el + 2 > cap) {
            return false;
        }
        memcpy(out + o, esc, el);
        o += el;
    }
    out[o++] = '"';
    out[o] = 0;
    *pos = o;
    return true;
}

bool json_put(char *out, int cap, int *pos, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (w < 0 || w >= cap - *pos) {
        out[*pos] = 0;
        return false;
    }
    *pos += w;
    return true;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_encode(const uint8_t *d, int n, char *out, int cap)
{
    int need = (n + 2) / 3 * 4;
    if (need + 1 > cap) {
        return -1;
    }
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[o++] = B64[v >> 18 & 63];
        out[o++] = B64[v >> 12 & 63];
        out[o++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? B64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

int b64_decode(const char *s, uint8_t *out, int cap)
{
    int o = 0, bits = 0;
    uint32_t acc = 0;
    for (; *s && *s != '='; s++) {
        const char *p = (const char *)memchr(B64, *s, 64);
        if (!p) {
            return -1;
        }
        acc = acc << 6 | (uint32_t)(p - B64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) {
                return -1;
            }
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return o;
}

bool json_array_item(const char *raw, int n, int idx, const char **item, int *ilen)
{
    if (n < 2 || raw[0] != '[') {
        return false;
    }
    char why[8];
    P p = { raw, n, 1, why, (int)sizeof(why) };
    JsonObj dummy;
    memset(&dummy, 0, sizeof(dummy));
    for (int k = 0;; k++) {
        p.ws();
        if (p.i >= n || raw[p.i] == ']') {
            return false;
        }
        int start = p.i;
        JsonVal v;
        if (value(&p, &dummy, &v)) {
            return false;
        }
        if (k == idx) {
            *item = raw + start;
            *ilen = p.i - start;
            return true;
        }
        p.ws();
        if (p.i < n && raw[p.i] == ',') {
            p.i++;
        } else {
            return false;
        }
    }
}
