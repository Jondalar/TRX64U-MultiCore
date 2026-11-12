#include "wsproto.h"
#include <string.h>

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void ws_sha1(const uint8_t *data, int n, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    // the message, a 0x80, zeros, the length in bits as 64 bit big endian, in blocks of 64: walked without a copy of the whole message
    int total = ((n + 8) / 64 + 1) * 64;
    for (int off = 0; off < total; off += 64) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            int pos = off + i;
            uint8_t b = 0;
            if (pos < n) {
                b = data[pos];
            } else if (pos == n) {
                b = 0x80;
            } else if (pos >= total - 8) {
                int shift = (total - 1 - pos) * 8;
                uint64_t bits = (uint64_t)n * 8;
                b = shift < 64 ? (uint8_t)(bits >> shift) : 0;
            }
            blk[i] = b;
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 | (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}

void ws_accept_key(const char *client_key, char out[32])
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t msg[48 + sizeof(guid)];
    int kl = (int)strlen(client_key);
    if (kl > 47) {
        kl = 47;
    }
    memcpy(msg, client_key, kl);
    memcpy(msg + kl, guid, sizeof(guid) - 1);
    uint8_t d[20];
    ws_sha1(msg, kl + (int)sizeof(guid) - 1, d);
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < 20; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < 20 ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < 20 ? d[i + 2] : 0);
        out[o++] = b64[v >> 18 & 63];
        out[o++] = b64[v >> 12 & 63];
        out[o++] = i + 1 < 20 ? b64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < 20 ? b64[v & 63] : '=';
    }
    out[o] = 0;
}

int ws_decode(const uint8_t *buf, int n, WsFrame *f, int *close_code)
{
    if (n < 2) {
        return 0;
    }
    bool fin = buf[0] & 0x80;
    int rsv = buf[0] & 0x70, op = buf[0] & 0x0F;
    bool masked = buf[1] & 0x80;
    uint32_t len = buf[1] & 0x7F;
    int p = 2;
    if (rsv || !masked) {
        *close_code = 1002;
        return -1;
    }
    if (len == 126) {
        if (n < 4) {
            return 0;
        }
        len = (uint32_t)buf[2] << 8 | buf[3];
        p = 4;
    } else if (len == 127) {
        *close_code = 1009;                                        // 64 bit lengths are never this small
        return -1;
    }
    bool control = op >= 8;
    if (len > WS_PAYLOAD_MAX || (control && len > 125)) {
        *close_code = control ? 1002 : 1009;
        return -1;
    }
    if (!fin || op == WS_CONT || op == WS_BINARY || (op > WS_TEXT && op < WS_CLOSE) || op > WS_PONG) {
        *close_code = 1003;
        return -1;
    }
    if (n < p + 4 + (int)len) {
        return 0;
    }
    const uint8_t *mask = buf + p;
    for (uint32_t i = 0; i < len; i++) {
        f->payload[i] = buf[p + 4 + i] ^ mask[i & 3];
    }
    f->payload[len] = 0;
    f->op = op;
    f->len = (int)len;
    return p + 4 + (int)len;
}

int ws_encode(int op, const uint8_t *payload, int len, uint8_t *out, int cap)
{
    if (len < 0 || len > 65535) {
        return -1;
    }
    int h = len < 126 ? 2 : 4;
    if (h + len > cap) {
        return -1;
    }
    out[0] = (uint8_t)(0x80 | op);
    if (len < 126) {
        out[1] = (uint8_t)len;
    } else {
        out[1] = 126;
        out[2] = (uint8_t)(len >> 8);
        out[3] = (uint8_t)len;
    }
    memcpy(out + h, payload, len);
    return h + len;
}
