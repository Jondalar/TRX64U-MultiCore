#include "httpreq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---- HTTP parsing ----------------------------------------------------------------------------------------------------------------
void http_init(HttpReq *q)
{
    memset(q, 0, sizeof(*q));
    q->state = HS_HEAD;
}

static int hexv(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// Percent decoding (and '+' as a space when `plus`), into out[0..max). False when it does not fit or an escape is bad.
bool http_unescape(const char *s, int n, char *out, int max, bool plus)
{
    int o = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '%') {
            if (i + 2 >= n) {
                return false;
            }
            int h = hexv(s[i + 1]), l = hexv(s[i + 2]);
            if (h < 0 || l < 0) {
                return false;
            }
            c = (char)(h * 16 + l);
            i += 2;
        } else if (c == '+' && plus) {
            c = ' ';
        }
        if (o >= max - 1) {
            return false;
        }
        out[o++] = c;
    }
    out[o] = 0;
    return true;
}

static bool ieq_prefix(const char *line, const char *name)
{
    size_t l = strlen(name);
    return !strncasecmp(line, name, l) && line[l] == ':';
}

static void bad(HttpReq *q, int status)
{
    q->state = HS_BAD;
    q->bad_status = status;
}

// T192.1: `t=<token>` is the gate's, not an argument of an operation: it is cut out of the query and kept apart.
static void cut_token_param(HttpReq *q)
{
    char rest[HTTP_QUERY_MAX];
    int o = 0;
    const char *s = q->query;
    while (*s) {
        const char *amp = strchr(s, '&');
        int len = amp ? (int)(amp - s) : (int)strlen(s);
        if (len >= 2 && s[0] == 't' && s[1] == '=') {
            int l = len - 2 < (int)sizeof(q->tok_q) - 1 ? len - 2 : (int)sizeof(q->tok_q) - 1;
            memcpy(q->tok_q, s + 2, l);
            q->tok_q[l] = 0;
        } else {
            if (o && o < (int)sizeof(rest) - 1) rest[o++] = '&';
            for (int i = 0; i < len && o < (int)sizeof(rest) - 1; i++) rest[o++] = s[i];
        }
        s = amp ? amp + 1 : s + len;
    }
    rest[o] = 0;
    memcpy(q->query, rest, o + 1);
}

static void parse_head(HttpReq *q)
{
    char *line = q->head;
    char *eol = strstr(line, "\r\n");
    if (!eol) {
        bad(q, 400);
        return;
    }
    *eol = 0;
    // request line: METHOD SP target SP HTTP/1.x
    char *sp1 = strchr(line, ' ');
    char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : 0;
    if (!sp1 || !sp2 || strncmp(sp2 + 1, "HTTP/1.", 7) != 0 || sp1 - line >= (int)sizeof(q->method)) {
        bad(q, 400);
        return;
    }
    memcpy(q->method, line, sp1 - line);
    q->method[sp1 - line] = 0;
    char *target = sp1 + 1;
    *sp2 = 0;
    char *qm = strchr(target, '?');
    int plen = (int)(qm ? qm - target : (int)strlen(target));
    if (!http_unescape(target, plen, q->path, sizeof(q->path), false) || q->path[0] != '/') {
        bad(q, 400);
        return;
    }
    if (qm) {
        if (strlen(qm + 1) >= sizeof(q->query)) {
            bad(q, 414);
            return;
        }
        strcpy(q->query, qm + 1);
        cut_token_param(q);
    }
    bool upgrade = false, conn_upgrade = false;
    char *p = eol + 2;
    while (*p) {
        char *e = strstr(p, "\r\n");
        if (!e) {
            break;
        }
        *e = 0;
        if (ieq_prefix(p, "Content-Length")) {
            char *v = strchr(p, ':') + 1;
            while (*v == ' ') v++;
            char *end;
            long n = strtol(v, &end, 10);
            if (*end || n < 0) {
                bad(q, 400);
                return;
            }
            if (n > HTTP_BODY_MAX) {
                bad(q, 413);
                return;
            }
            q->content_length = (int)n;
        } else if (ieq_prefix(p, "Authorization")) {
            char *v = strchr(p, ':') + 1;
            while (*v == ' ') v++;
            if (!strncasecmp(v, "Bearer ", 7)) {
                v += 7;
                while (*v == ' ') v++;
                snprintf(q->tok_bearer, sizeof(q->tok_bearer), "%s", v);
            }
        } else if (ieq_prefix(p, "X-Trx-Token")) {
            char *v = strchr(p, ':') + 1;
            while (*v == ' ') v++;
            snprintf(q->tok_x, sizeof(q->tok_x), "%s", v);
        } else if (ieq_prefix(p, "X-Password")) {
            char *v = strchr(p, ':') + 1;
            while (*v == ' ') v++;
            snprintf(q->password, sizeof(q->password), "%s", v);
        } else if (ieq_prefix(p, "Transfer-Encoding")) {
            bad(q, 501);                                           // chunked requests are not read here
            return;
        } else if (ieq_prefix(p, "Upgrade")) {
            char *v = strchr(p, ':') + 1;
            while (*v == ' ') v++;
            upgrade = !strcasecmp(v, "websocket");
        } else if (ieq_prefix(p, "Connection")) {
            conn_upgrade = false;                                  // "Connection: keep-alive, Upgrade": a token of the list, in any case
            for (const char *w = p; *w; w++) {
                conn_upgrade |= !strncasecmp(w, "upgrade", 7);
            }
        } else if (ieq_prefix(p, "Sec-WebSocket-Key")) {
            char *v = strchr(p, ':') + 1;
            while (*v == ' ') v++;
            if (strlen(v) >= sizeof(q->ws_key)) {
                bad(q, 400);
                return;
            }
            strcpy(q->ws_key, v);
        }
        p = e + 2;
    }
    q->ws_upgrade = upgrade && conn_upgrade && q->ws_key[0];
    q->state = q->content_length ? HS_BODY : HS_READY;
}

int http_feed(HttpReq *q, const char *data, int n)
{
    int used = 0;
    while (used < n && q->state == HS_HEAD) {
        if (q->head_len >= HTTP_HEAD_MAX) {
            bad(q, 431);
            return used;
        }
        q->head[q->head_len++] = data[used++];
        q->head[q->head_len] = 0;
        if (q->head_len >= 4 && !memcmp(q->head + q->head_len - 4, "\r\n\r\n", 4)) {
            parse_head(q);
        }
    }
    while (used < n && q->state == HS_BODY) {
        int want = q->content_length - q->body_len;
        int take = n - used < want ? n - used : want;
        memcpy(q->body + q->body_len, data + used, take);
        q->body_len += take;
        used += take;
        if (q->body_len == q->content_length) {
            q->body[q->body_len] = 0;
            q->state = HS_READY;
        }
    }
    return used;
}
