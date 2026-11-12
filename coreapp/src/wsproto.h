// WebSocket (RFC 6455) for the WS JSON-RPC adapter (T104.1 D3, ADR-0002): the opening handshake's accept key and the framing of text messages. Pure.
// What it does not do, on purpose: fragmented messages, binary messages, extensions - a client that sends one is closed with the code that says so.
#ifndef COREAPP_WSPROTO_H
#define COREAPP_WSPROTO_H

#include <stdint.h>

#define WS_PAYLOAD_MAX 1024

enum WsOp { WS_CONT = 0, WS_TEXT = 1, WS_BINARY = 2, WS_CLOSE = 8, WS_PING = 9, WS_PONG = 10 };

// base64(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")): 28 characters and a NUL into out[32].
void ws_accept_key(const char *client_key, char out[32]);

struct WsFrame {
    int op;
    int len;
    uint8_t payload[WS_PAYLOAD_MAX + 1];      // NUL behind the payload, so a text message is a string
};

// One frame from a client in buf[0..n). Returns the bytes it used (the frame is in *f), 0 when more bytes are needed, or -1 when the frame is not
// acceptable: *close_code is then the code to close with (1002 protocol error: unmasked or a reserved bit; 1003: a kind not supported: fragments,
// binary; 1009: larger than WS_PAYLOAD_MAX).
int ws_decode(const uint8_t *buf, int n, WsFrame *f, int *close_code);

// One unmasked frame of a server into out[0..cap): returns its length, -1 when it does not fit or the payload is above 65535 bytes.
int ws_encode(int op, const uint8_t *payload, int len, uint8_t *out, int cap);

// SHA-1 and base64, exported for the test.
void ws_sha1(const uint8_t *data, int n, uint8_t out[20]);

#endif
