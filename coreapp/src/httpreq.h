// The HTTP/1.1 request reader both apps' network servers share (T192.1 P6/P7, Q5): bytes in, a parsed request out. Pure - no socket, no RTOS.
#ifndef COREAPP_HTTPREQ_H
#define COREAPP_HTTPREQ_H

#define HTTP_HEAD_MAX 1536
#define HTTP_BODY_MAX 1024
#define HTTP_PATH_MAX 192
#define HTTP_QUERY_MAX 256

enum HttpState { HS_HEAD = 0, HS_BODY, HS_READY, HS_BAD };

struct HttpReq {
    int state;
    int bad_status;                       // HS_BAD: the status to answer (400, 413, 431, 501)
    char head[HTTP_HEAD_MAX + 1];
    int head_len;
    char method[8];
    char path[HTTP_PATH_MAX];             // percent-decoded
    char query[HTTP_QUERY_MAX];           // as sent
    int content_length;
    bool ws_upgrade;
    char ws_key[48];
    char tok_q[40];                       // the token as `t=` of the query (cut out of `query`), as Authorization: Bearer, as X-Trx-Token (T192.1-S16)
    char tok_bearer[40];
    char tok_x[40];
    char password[40];                    // X-Password (T228 R21 row 58)
    char body[HTTP_BODY_MAX + 1];
    int body_len;
};

void http_init(HttpReq *q);
// Takes bytes of a request; returns how many it used (the rest is for the next request or the WS frames behind a handshake). state tells what is
// needed: HS_HEAD and HS_BODY want more, HS_READY is complete, HS_BAD is refused (bad_status).
int http_feed(HttpReq *q, const char *data, int n);

// Percent decoding (and '+' as a space when `plus`) of s[0..n) into out[0..max). False when it does not fit or an escape is bad.
bool http_unescape(const char *s, int n, char *out, int max, bool plus);

#endif
