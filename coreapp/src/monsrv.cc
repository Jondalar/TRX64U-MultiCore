// The RPC server of the linked monitor (monsrv.h). PROVENANCE: the network half of vendor/gideon/apps/trxmon/src/main.c (Gideon Zweijtzer's firmware with the TRX64 line's
// additions, GPL-3.0; the copy that was apps/trxmon of branch t104-3-loader), over lwIP sockets instead of the app API's net table. Changed: no blocking wait (the menu task polls
// it, monsrv_poll), the holder's buffers are allocated while it is connected, the dispatch is trxmon's rpc.c unchanged (srv_send, srv_broadcast, srv_cork, srv_holder_idle_ms).
#include "monsrv.h"
#include "rest.h"
#include "uic.h"
#include "lwip/sockets.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "trxmon/mlib.h"
#include "trxmon/ws.h"
#include "trxmon/rpc.h"
#include "trxmon/mach.h"
}

#define RPC_PORT      4312
#define MAX_REFUSE    2                  // connections being refused at the same time
#define REFUSE_RX     1024
#define REFUSE_MS     1000               // a refused client that sends no request head is answered after this long
#define HANDSHAKE_MS  5000               // the holder must finish the HTTP upgrade within this, or it does not hold the port
#define KEEPALIVE_IDLE_MS 20000          // a holder that vanished sends no FIN: after this much silence a ping, after KEEPALIVE_DEAD_MS it is gone
#define KEEPALIVE_DEAD_MS 50000
#define RX_SIZE       (20 * 1024)
#define MSG_SIZE      (16 * 1024)
#define SEND_SLOW_MS  1000u              // a holder that stopped reading fills the send buffer: the first send that took this long drops it
#define CORK_MAX      3072

extern "C" int mon_claim(void);          // monitor.cc: the engine is needed now

namespace {

struct Client {
    int h;
    int ws, dead, rxlen, msglen;
    uint32_t t0, t_rx, t_ping;
    char peer[24];
    uint8_t rx[RX_SIZE];
    char msg[MSG_SIZE + 1];
};

struct Refuse {
    int h;                               // -1: free
    int rxlen;
    uint32_t t0;
    char rx[REFUSE_RX];
};

Client *C;                               // the one holder, allocated while it is connected
Refuse X[MAX_REFUSE];
int s_listener = -1;
bool s_taken;                            // the port could not be bound
uint8_t s_cork[CORK_MAX];
int s_cork_n, s_cork_on;

uint32_t now_ms(void) { return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; }

// D19a on the monitor's socket (T192 M1): it has no token and cannot hold a question, so while the UI service is up (the MVP core) the methods that run a monitor
// command line, restore a checkpoint or touch the media are refused with the reason. A core without the service is as before.
bool gate_refused(Client *c);

void nonblocking(int s)
{
    int flags = lwip_fcntl(s, F_GETFL, 0);
    lwip_fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

// 0 bytes: closed by the peer; -1: nothing now; -2: error
int net_recv(int h, void *buf, int len)
{
    int n = lwip_recv(h, buf, len, MSG_DONTWAIT);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        return 0;
    }
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? -1 : -2;
}

int net_send(int h, const void *buf, int len)
{
    const uint8_t *p = (const uint8_t *)buf;
    int done = 0, idle = 0;
    while (done < len) {
        int n = lwip_send(h, p + done, len - done, MSG_DONTWAIT);
        if (n > 0) {
            done += n;
            idle = 0;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && idle < 5000) {
            vTaskDelay(1);                                       // the send buffer is full: let lwIP drain it
            idle++;
            continue;
        }
        return -2;
    }
    return done;
}

void net_peer(int h, char *out, int n)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    out[0] = 0;
    if (lwip_getpeername(h, (struct sockaddr *)&a, &al) < 0 || a.sin_family != AF_INET) {
        return;
    }
    char ip[20];
    inet_ntoa_r(a.sin_addr, ip, sizeof(ip));
    snprintf(out, n, "%s:%u", ip, (unsigned)ntohs(a.sin_port));
}

int send_all(Client *c, const void *p, int n)
{
    if (c->dead) {
        return -1;
    }
    uint32_t t0 = now_ms();
    if (net_send(c->h, p, n) < 0) {
        c->dead = 1;
        return -1;
    }
    if ((uint32_t)(now_ms() - t0) >= SEND_SLOW_MS) {
        printf("[monitor] the RPC holder %s does not read (a send took %u ms): dropped\n", c->peer, (unsigned)(now_ms() - t0));
        c->dead = 1;
        return -1;
    }
    return 0;
}

// Every packet costs the network stack a few milliseconds: a frame goes out in one send (header and payload together), and frames sent between srv_cork(1) and srv_cork(0) in one more.
void cork_flush(void)
{
    if (s_cork_n && C && C->h >= 0) {
        send_all(C, s_cork, s_cork_n);
    }
    s_cork_n = 0;
}

void send_frame(Client *c, int opcode, const void *p, int n)
{
    uint8_t buf[10 + 1400];
    int hl = ws_frame_header(buf, opcode, (uint64_t)n);
    if (n <= 1400) {
        memcpy(buf + hl, p, (size_t)n);
        if (s_cork_on) {
            if (s_cork_n + hl + n > CORK_MAX) {
                cork_flush();
            }
            if (hl + n <= CORK_MAX) {
                memcpy(s_cork + s_cork_n, buf, (size_t)(hl + n));
                s_cork_n += hl + n;
                return;
            }
        }
        send_all(c, buf, hl + n);
        return;
    }
    if (send_all(c, buf, hl) == 0 && n > 0) {
        send_all(c, p, n);
    }
}

bool gate_refused(Client *c)
{
    if (!rest_uic) {
        return false;
    }
    char m[48], id[40];
    if (uic_json_get(c->msg, c->msglen, "method", m, sizeof(m)) != 1) {
        return false;
    }
    if (!monsrv_gated(m)) {
        return false;
    }
    int t = uic_json_get(c->msg, c->msglen, "id", id, sizeof(id));
    char em[110], eid[80];
    monsrv_json_esc(m, em, sizeof(em));
    if (t == 1) {
        monsrv_json_esc(id, eid, sizeof(eid));
    } else {
        bool num = t == 2;                                 // not a string: only a number is echoed, anything else answers null
        for (const char *q = id; *q; q++) {
            num = num && ((*q >= '0' && *q <= '9') || *q == '-' || *q == '+' || *q == '.' || *q == 'e' || *q == 'E');
        }
        snprintf(eid, sizeof(eid), "%s", num ? id : "null");
    }
    char b[360];
    int n = snprintf(b, sizeof(b), "{\"jsonrpc\":\"2.0\",\"id\":%s%s%s,\"error\":{\"code\":-32001,\"message\":\"%s changes the machine: it needs the confirmation gate (POST /v1/ops with the token); this socket has no token\"}}",
                     t == 1 ? "\"" : "", eid, t == 1 ? "\"" : "", em);
    if (n > 0 && n < (int)sizeof(b)) {
        send_frame(c, WS_TEXT, b, n);
    }
    return true;
}

void drop_client(void)
{
    if (!C) {
        return;
    }
    cork_flush();
    if (C->h >= 0) {
        lwip_close(C->h);
    }
    C->h = -1;
    rpc_client_gone(0);
    free(C);
    C = 0;
    s_cork_n = 0;
}

void close_ws(Client *c, int code)
{
    uint8_t b[2] = { (uint8_t)(code >> 8), (uint8_t)code };
    send_frame(c, WS_CLOSE, b, 2);
    c->dead = 1;
}

void http_head(Client *c)
{
    http_req_t r;
    int n = ws_http_parse((const char *)c->rx, c->rxlen, &r);
    if (n == 0) {
        return;
    }
    if (n < 0 || !r.upgrade) {
        static const char no[] = "HTTP/1.1 426 Upgrade Required\r\nUpgrade: websocket\r\nConnection: close\r\n"
                                 "Content-Type: text/plain\r\n\r\n"
                                 "trxmon: the TRX64 runtime contract, WebSocket JSON-RPC 2.0 (ws://<host>:4312/?av=0)\n";
        send_all(c, no, (int)sizeof(no) - 1);
        c->dead = 1;
        return;
    }
    char out[256];
    int ol = ws_http_accept(&r, out, sizeof(out));
    send_all(c, out, ol);
    c->ws = 1;
    memmove(c->rx, c->rx + n, (size_t)(c->rxlen - n));
    c->rxlen -= n;
}

void ws_frames(Client *c)
{
    while (!c->dead) {
        ws_frame_t f;
        int h = ws_frame_parse(c->rx, c->rxlen, &f);
        if (h == 0) {
            if (c->rxlen >= RX_SIZE) {
                close_ws(c, 1009);
            }
            return;
        }
        if (h < 0 || !f.masked) {
            close_ws(c, 1002);
            return;
        }
        if (f.len > (uint64_t)(RX_SIZE - h)) {
            close_ws(c, 1009);
            return;
        }
        int total = h + (int)f.len;
        if (c->rxlen < total) {
            return;                                              // the payload is not all here yet
        }
        uint8_t *p = c->rx + h;
        ws_unmask(p, (int)f.len, f.mask);
        switch (f.opcode) {
        case WS_TEXT:
        case WS_CONT:
            if (c->msglen + (int)f.len > MSG_SIZE) {
                close_ws(c, 1009);
                return;
            }
            memcpy(c->msg + c->msglen, p, (size_t)f.len);
            c->msglen += (int)f.len;
            if (f.fin) {
                c->msg[c->msglen] = 0;
                if (!mon_claim()) {                              // no debug block in this bitstream: nothing to talk to
                    close_ws(c, 1011);
                    return;
                }
                if (gate_refused(c)) {
                    c->msglen = 0;
                    break;
                }
                rpc_request(0, c->msg, c->msglen);
                c->msglen = 0;
            }
            break;
        case WS_PING: send_frame(c, WS_PONG, p, (int)f.len); break;
        case WS_CLOSE:
            send_frame(c, WS_CLOSE, p, (int)(f.len >= 2 ? 2 : 0));
            c->dead = 1;
            break;
        default: break;                                          // binary and pong frames are not for us
        }
        memmove(c->rx, c->rx + total, (size_t)(c->rxlen - total));
        c->rxlen -= total;
    }
}

void client_read(Client *c)
{
    int room = RX_SIZE - c->rxlen;
    if (room <= 0) {
        c->dead = 1;
        return;
    }
    int n = net_recv(c->h, c->rx + c->rxlen, room);
    if (n == -1) {
        return;
    }
    if (n <= 0) {
        c->dead = 1;
        return;
    }
    c->rxlen += n;
    c->t_rx = now_ms();
    if (!c->ws) {
        http_head(c);
    }
    if (c->ws) {
        ws_frames(c);
    }
}

// The text a refused client reads: the port, who holds it, how to get in
int refuse_text(char *out, int n)
{
    sbuf_t b;
    sb_init(&b, out, n);
    sb_put(&b, "port 4312 is held");
    if (C && C->peer[0]) {
        sb_put(&b, " by ");
        sb_put(&b, C->peer);
    }
    sb_put(&b, " (trxmon serves one RPC connection at a time)");
    sb_str(&b);
    return b.len;
}

// The refusal over the answer a client can read: a WebSocket client gets the upgrade, one JSON-RPC error (id null, code RPC_BUSY_CODE), a close frame with code 1013 and the
// reason; anything else gets HTTP 503 with the text. The connection is closed afterwards.
void refuse_answer(Refuse *x, int upgrade, const http_req_t *r)
{
    char text[160];
    refuse_text(text, sizeof(text));
    uint8_t hdr[10];
    if (upgrade) {
        char out[256];
        int ol = ws_http_accept(r, out, sizeof(out));
        net_send(x->h, out, ol);
        char msg[320];
        sbuf_t b;
        sb_init(&b, msg, sizeof(msg));
        sb_put(&b, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":" RPC_BUSY_CODE_STR ",\"message\":\"");
        sb_put(&b, text);
        sb_put(&b, "\",\"data\":{\"port\":4312}}}");
        int hl = ws_frame_header(hdr, WS_TEXT, (uint64_t)b.len);
        net_send(x->h, hdr, hl);
        net_send(x->h, msg, b.len);
        uint8_t cl[2 + 123];
        int tl = m_strlen(text);
        if (tl > 123) {
            tl = 123;
        }
        cl[0] = (uint8_t)(WS_BUSY_CLOSE >> 8);
        cl[1] = (uint8_t)WS_BUSY_CLOSE;
        memcpy(cl + 2, text, (size_t)tl);
        hl = ws_frame_header(hdr, WS_CLOSE, (uint64_t)(2 + tl));
        net_send(x->h, hdr, hl);
        net_send(x->h, cl, 2 + tl);
    } else {
        char out[400];
        sbuf_t b;
        sb_init(&b, out, sizeof(out));
        sb_put(&b, "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 5\r\nConnection: close\r\n"
                   "Content-Type: text/plain\r\nContent-Length: ");
        sb_dec(&b, (uint64_t)(m_strlen(text) + 1));
        sb_put(&b, "\r\n\r\n");
        sb_put(&b, text);
        sb_putc(&b, '\n');
        net_send(x->h, out, b.len);
    }
    lwip_close(x->h);
    x->h = -1;
}

void refuse_read(Refuse *x)
{
    int n = net_recv(x->h, x->rx + x->rxlen, REFUSE_RX - x->rxlen);
    if (n == -1) {
        return;
    }
    if (n <= 0) {                                                // gone before it heard anything
        lwip_close(x->h);
        x->h = -1;
        return;
    }
    x->rxlen += n;
    http_req_t r;
    int hn = ws_http_parse(x->rx, x->rxlen, &r);
    if (hn == 0 && x->rxlen < REFUSE_RX) {
        return;                                                  // the head is not all here yet
    }
    refuse_answer(x, hn > 0 && r.upgrade, &r);
}

void refuse_start(int h)
{
    for (int i = 0; i < MAX_REFUSE; i++) {
        if (X[i].h < 0) {
            X[i].h = h;
            X[i].rxlen = 0;
            X[i].t0 = now_ms();
            return;
        }
    }
    lwip_close(h);                                               // a flood of refused connections: the extra ones are dropped
}

void accept_one(void)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    int h = lwip_accept(s_listener, (struct sockaddr *)&a, &al);
    if (h < 0) {
        return;
    }
    nonblocking(h);
    int one = 1;
    lwip_setsockopt(h, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (!C) {
        C = (Client *)calloc(1, sizeof(Client));
        if (C) {
            C->h = h;
            C->t0 = C->t_rx = C->t_ping = now_ms();
            net_peer(h, C->peer, sizeof(C->peer));
            return;
        }
        lwip_close(h);                                           // no memory for a holder
        return;
    }
    refuse_start(h);
}

}   // namespace

extern "C" {
void srv_cork(int on)
{
    if (!on) {
        cork_flush();
    }
    s_cork_on = on;
}

uint32_t srv_holder_idle_ms(void)
{
    if (C && C->h >= 0 && C->ws && !C->dead) {
        return now_ms() - C->t_rx;
    }
    return 0xFFFFFFFFu;
}

void srv_send(int cid, const char *text, int len)
{
    if (cid == 0 && C && C->h >= 0 && C->ws) {
        send_frame(C, WS_TEXT, text, len);
    }
}

void srv_broadcast(const char *text, int len) { srv_send(0, text, len); }
}

void monsrv_start(void)
{
    if (s_listener >= 0) {
        return;
    }
    for (int i = 0; i < MAX_REFUSE; i++) {
        X[i].h = -1;
    }
    s_taken = false;
    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        s_taken = true;
        return;
    }
    int one = 1;
    lwip_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(RPC_PORT);
    if (lwip_bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || lwip_listen(s, 2) < 0) {
        lwip_close(s);
        s_taken = true;
        printf("[monitor] TCP port %d is taken: no RPC\n", RPC_PORT);
        return;
    }
    nonblocking(s);
    s_listener = s;
    printf("[monitor] serving the TRX64 runtime contract on TCP %d\n", RPC_PORT);
}

void monsrv_poll(void)
{
    if (s_listener < 0) {
        return;
    }
    accept_one();
    if (C && C->h >= 0 && !C->dead) {
        client_read(C);
    }
    uint32_t now = now_ms();
    for (int i = 0; i < MAX_REFUSE; i++) {
        if (X[i].h < 0) {
            continue;
        }
        refuse_read(&X[i]);
        if (X[i].h >= 0 && (uint32_t)(now - X[i].t0) >= REFUSE_MS) {
            http_req_t none;
            memset(&none, 0, sizeof(none));
            refuse_answer(&X[i], 0, &none);                      // a client that never says anything: plain text
        }
    }
    if (C) {
        if (!C->ws && !C->dead && (uint32_t)(now - C->t0) >= HANDSHAKE_MS) {
            C->dead = 1;                                         // no upgrade: it must not hold the one place
        }
        if (C->ws && !C->dead) {
            if ((uint32_t)(now - C->t_rx) >= KEEPALIVE_DEAD_MS) {
                printf("[monitor] the RPC holder %s sent nothing for %u ms and did not answer a ping: dropped\n", C->peer, (unsigned)(now - C->t_rx));
                C->dead = 1;
            } else if ((uint32_t)(now - C->t_rx) >= KEEPALIVE_IDLE_MS && (uint32_t)(now - C->t_ping) >= KEEPALIVE_IDLE_MS) {
                send_frame(C, WS_PING, 0, 0);
                C->t_ping = now;
            }
        }
        if (C->dead) {
            drop_client();
        }
    }
}

void monsrv_stop(void)
{
    if (C) {
        if (C->ws && !C->dead) {
            close_ws(C, 1001);
        }
        drop_client();
    }
    for (int i = 0; i < MAX_REFUSE; i++) {
        if (X[i].h >= 0) {
            lwip_close(X[i].h);
            X[i].h = -1;
        }
    }
    if (s_listener >= 0) {
        lwip_close(s_listener);
        s_listener = -1;
    }
}

const char *monsrv_status(void)
{
    if (s_listener >= 0) {
        return "RPC on TCP 4312";
    }
    return s_taken ? "TCP port 4312 is taken" : "";
}

bool monsrv_listening(void) { return s_listener >= 0; }
bool monsrv_has_client(void) { return C != 0; }
void monsrv_drop_client(void)
{
    if (C) {
        if (C->ws && !C->dead) {
            close_ws(C, 1001);
        }
        drop_client();
    }
}
