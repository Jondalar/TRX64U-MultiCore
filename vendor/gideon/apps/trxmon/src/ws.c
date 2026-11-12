/*
 * ws.c — WebSocket handshake and framing (see ws.h).
 */
#include "ws.h"

static uint32_t rol(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) |
               (uint32_t)p[4 * i + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void ws_sha1(const uint8_t *d, int n, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint8_t blk[64];
    int i = 0;
    for (; i + 64 <= n; i += 64) {
        sha1_block(h, d + i);
    }
    int r = n - i;
    memset(blk, 0, sizeof(blk));
    memcpy(blk, d + i, (size_t)r);
    blk[r] = 0x80;
    if (r >= 56) {
        sha1_block(h, blk);
        memset(blk, 0, sizeof(blk));
    }
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) {
        blk[63 - k] = (uint8_t)(bits >> (8 * k));
    }
    sha1_block(h, blk);
    for (int k = 0; k < 20; k++) {
        out[k] = (uint8_t)(h[k / 4] >> (24 - 8 * (k % 4)));
    }
}

int ws_base64(const uint8_t *in, int n, char *out)
{
    static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    int i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = tb[(v >> 18) & 63];
        out[o++] = tb[(v >> 12) & 63];
        out[o++] = tb[(v >> 6) & 63];
        out[o++] = tb[v & 63];
    }
    if (n - i == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = tb[(v >> 18) & 63];
        out[o++] = tb[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (n - i == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = tb[(v >> 18) & 63];
        out[o++] = tb[(v >> 12) & 63];
        out[o++] = tb[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = 0;
    return o;
}

/* Header field value: after "name:" up to the line end, trimmed. */
static int header_value(const char *line, int n, const char *name, const char **val, int *vlen)
{
    int nl = m_strlen(name);
    if ((n <= nl) || !m_strnieq(line, name, nl) || (line[nl] != ':')) {
        return 0;
    }
    int s = nl + 1;
    while ((s < n) && ((line[s] == ' ') || (line[s] == '\t'))) {
        s++;
    }
    int e = n;
    while ((e > s) && ((line[e - 1] == ' ') || (line[e - 1] == '\t'))) {
        e--;
    }
    *val = line + s;
    *vlen = e - s;
    return 1;
}

/* Whether the comma-separated list v contains token tok (any case). */
static int has_token(const char *v, int n, const char *tok)
{
    int tl = m_strlen(tok);
    int i = 0;
    while (i < n) {
        while ((i < n) && ((v[i] == ' ') || (v[i] == ','))) {
            i++;
        }
        int s = i;
        while ((i < n) && (v[i] != ',')) {
            i++;
        }
        int e = i;
        while ((e > s) && (v[e - 1] == ' ')) {
            e--;
        }
        if ((e - s == tl) && m_strnieq(v + s, tok, tl)) {
            return 1;
        }
    }
    return 0;
}

static void parse_av(http_req_t *r)
{
    r->av = 1;
    const char *q = r->path;
    while (*q && (*q != '?')) {
        q++;
    }
    while (*q) {
        q++; /* past ? or & */
        if ((q[0] == 'a') && (q[1] == 'v') && (q[2] == '=')) {
            r->av = (q[3] != '0');
        }
        while (*q && (*q != '&')) {
            q++;
        }
    }
}

int ws_http_parse(const char *buf, int len, http_req_t *r)
{
    int end = -1;
    for (int i = 0; i + 3 < len; i++) {
        if ((buf[i] == '\r') && (buf[i + 1] == '\n') && (buf[i + 2] == '\r') && (buf[i + 3] == '\n')) {
            end = i + 4;
            break;
        }
    }
    if (end < 0) {
        return (len > 4096) ? -1 : 0;
    }
    memset(r, 0, sizeof(*r));
    /* request line: GET <target> HTTP/1.1 */
    if ((len < 4) || (memcmp(buf, "GET ", 4) != 0)) {
        return -1;
    }
    int i = 4;
    int p = 0;
    while ((i < end) && (buf[i] != ' ') && (buf[i] != '\r')) {
        if (p < (int)sizeof(r->path) - 1) {
            r->path[p++] = buf[i];
        }
        i++;
    }
    r->path[p] = 0;
    parse_av(r);
    while ((i < end) && (buf[i] != '\n')) {
        i++;
    }
    i++;
    int up = 0, conn = 0;
    while (i < end - 2) {
        int s = i;
        while ((i < end) && (buf[i] != '\r')) {
            i++;
        }
        int n = i - s;
        const char *v;
        int vl;
        if (header_value(buf + s, n, "Upgrade", &v, &vl) && has_token(v, vl, "websocket")) {
            up = 1;
        } else if (header_value(buf + s, n, "Connection", &v, &vl) && has_token(v, vl, "upgrade")) {
            conn = 1;
        } else if (header_value(buf + s, n, "Sec-WebSocket-Key", &v, &vl) && (vl < (int)sizeof(r->key))) {
            memcpy(r->key, v, (size_t)vl);
            r->key[vl] = 0;
        }
        i += 2; /* \r\n */
    }
    r->upgrade = up && conn && r->key[0];
    return end;
}

int ws_http_accept(const http_req_t *r, char *out, int cap)
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t cat[64 + 36];
    int kl = m_strlen(r->key);
    memcpy(cat, r->key, (size_t)kl);
    memcpy(cat + kl, guid, 36);
    uint8_t dig[20];
    ws_sha1(cat, kl + 36, dig);
    char acc[32];
    ws_base64(dig, 20, acc);
    sbuf_t b;
    sb_init(&b, out, cap);
    sb_put(&b, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Accept: ");
    sb_put(&b, acc);
    sb_put(&b, "\r\n\r\n");
    return b.len;
}

int ws_frame_parse(const uint8_t *buf, int len, ws_frame_t *f)
{
    if (len < 2) {
        return 0;
    }
    if (buf[0] & 0x70) {
        return -1; /* no extension was negotiated */
    }
    f->fin = (buf[0] >> 7) & 1;
    f->opcode = buf[0] & 15;
    f->masked = (buf[1] >> 7) & 1;
    uint64_t n = buf[1] & 127;
    int h = 2;
    if (n == 126) {
        if (len < 4) {
            return 0;
        }
        n = ((uint64_t)buf[2] << 8) | buf[3];
        h = 4;
    } else if (n == 127) {
        if (len < 10) {
            return 0;
        }
        n = 0;
        for (int i = 0; i < 8; i++) {
            n = (n << 8) | buf[2 + i];
        }
        h = 10;
    }
    if (f->masked) {
        if (len < h + 4) {
            return 0;
        }
        memcpy(f->mask, buf + h, 4);
        h += 4;
    }
    f->len = n;
    switch (f->opcode) {
    case WS_CONT:
    case WS_TEXT:
    case WS_BIN:
        break;
    case WS_CLOSE:
    case WS_PING:
    case WS_PONG:
        if (!f->fin || (n > 125)) {
            return -1;
        }
        break;
    default:
        return -1;
    }
    return h;
}

void ws_unmask(uint8_t *p, int n, const uint8_t mask[4])
{
    for (int i = 0; i < n; i++) {
        p[i] ^= mask[i & 3];
    }
}

int ws_frame_header(uint8_t *out, int opcode, uint64_t len)
{
    out[0] = (uint8_t)(0x80 | (opcode & 15));
    if (len < 126) {
        out[1] = (uint8_t)len;
        return 2;
    }
    if (len < 65536) {
        out[1] = 126;
        out[2] = (uint8_t)(len >> 8);
        out[3] = (uint8_t)len;
        return 4;
    }
    out[1] = 127;
    for (int i = 0; i < 8; i++) {
        out[2 + i] = (uint8_t)(len >> (56 - 8 * i));
    }
    return 10;
}
