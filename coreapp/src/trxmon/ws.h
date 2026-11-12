/*
 * ws.h — RFC 6455 pieces trxmon needs as a server: the HTTP upgrade
 * (Sec-WebSocket-Accept = base64(sha1(key + GUID))) and frame headers.
 */
#ifndef TRXMON_WS_H
#define TRXMON_WS_H

#include "mlib.h"

void ws_sha1(const uint8_t *d, int n, uint8_t out[20]);
int ws_base64(const uint8_t *in, int n, char *out); /* returns the length; out needs 4*ceil(n/3)+1 */

typedef struct
{
    int upgrade;    /* Upgrade: websocket and a key */
    char key[64];   /* Sec-WebSocket-Key */
    char path[128]; /* request target, query included */
    int av;         /* the av= query value; 1 when absent */
} http_req_t;

/* Parses an HTTP request head. Returns the head's length (through the blank
 * line) when it is complete, 0 when more bytes are needed, -1 when it is not
 * a GET request this server can answer. */
int ws_http_parse(const char *buf, int len, http_req_t *r);

/* Writes the 101 answer. Returns its length. */
int ws_http_accept(const http_req_t *r, char *out, int cap);

#define WS_BUSY_CLOSE 1013 /* "try again later": the one RPC connection is taken */

enum { WS_CONT = 0, WS_TEXT = 1, WS_BIN = 2, WS_CLOSE = 8, WS_PING = 9, WS_PONG = 10 };

typedef struct
{
    int fin, opcode, masked;
    uint64_t len;
    uint8_t mask[4];
} ws_frame_t;

/* Parses a frame header at buf. Returns the header length when the header is
 * complete (the payload may not be), 0 when more bytes are needed, -1 for a
 * frame this server refuses (reserved bits, an unknown opcode, a fragmented
 * control frame). */
int ws_frame_parse(const uint8_t *buf, int len, ws_frame_t *f);
void ws_unmask(uint8_t *p, int n, const uint8_t mask[4]);
/* An unmasked server frame header for len payload bytes. Returns its length (2, 4 or 10). */
int ws_frame_header(uint8_t *out, int opcode, uint64_t len);

#endif
