#include "nettransport.h"
#include "wsproto.h"
#include "lwip/sockets.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdlib.h>

// malloc, not new (nothrow): the board images have no libstdc++. A test sets NET_ALLOC to an allocator that can fail.
#ifndef NET_ALLOC
#define NET_ALLOC malloc
#else
extern "C" void *NET_ALLOC(size_t n);
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

namespace {

NetApp *g_app;
NetConfig g_cfg;
bool (*g_net_up)(char *, int);
bool g_up;
volatile int g_clients;
volatile int g_ws_open;
SemaphoreHandle_t g_out_lock;               // cfg.share_out: one response buffer for all clients

// While NetApp::starting(): one request at a time, in these (no heap). 640 holds the 503 with its JSON (body at most 300 bytes) and the headers.
const int GATE_OUT = 640;
HttpReq g_gate_q;
char g_gate_out[GATE_OUT];
volatile bool g_gate_busy;

static const char BUSY[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";


bool send_all(int fd, const void *p, int n)
{
    const char *b = (const char *)p;
    while (n > 0) {
        int w = send(fd, b, n, 0);
        if (w <= 0) {
            return false;
        }
        b += w;
        n -= w;
    }
    return true;
}

void set_timeout(int fd, int ms)
{
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
}

// WS: frames in, JSON-RPC out, notifications between. The receive times out every 100 ms so a queued notification does not wait for the client.
void ws_loop(int fd, NetWs *rs)
{
    uint8_t *in = (uint8_t *)NET_ALLOC(WS_PAYLOAD_MAX + 16);
    int have = 0;
    WsFrame *f = (WsFrame *)NET_ALLOC(sizeof(WsFrame));
    char *msg = (char *)NET_ALLOC(g_cfg.ws_msg_max);
    uint8_t *frame = (uint8_t *)NET_ALLOC(g_cfg.ws_msg_max + 8);
    if (!in || !f || !msg || !frame) {
        free(in);
        free(f);
        free(msg);
        free(frame);
        return;                                                     // no memory: the connection closes
    }
    set_timeout(fd, 100);
    bool open = true;
    while (open) {
        int n = rs->next(msg, g_cfg.ws_msg_max);
        while (n > 0) {
            int fl = ws_encode(WS_TEXT, (const uint8_t *)msg, n, frame, g_cfg.ws_msg_max + 8);
            if (fl < 0 || !send_all(fd, frame, fl)) {
                open = false;
                break;
            }
            n = rs->next(msg, g_cfg.ws_msg_max);
        }
        if (open && rs->done()) {
            uint8_t cp[2] = { 0x03, 0xE9 };                         // 1001 going away: the client reconnects
            int fl = ws_encode(WS_CLOSE, cp, 2, frame, g_cfg.ws_msg_max + 8);
            if (fl > 0) {
                send_all(fd, frame, fl);
            }
            open = false;
        }
        if (!open) {
            break;
        }
        int r = recv(fd, in + have, WS_PAYLOAD_MAX + 16 - have, 0);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ETIMEDOUT)) {
            break;
        }
        if (r > 0) {
            have += r;
        }
        for (;;) {
            int close_code = 0;
            int used = ws_decode(in, have, f, &close_code);
            if (used == 0) {
                if (have >= WS_PAYLOAD_MAX + 16) {
                    open = false;
                }
                break;
            }
            if (used < 0) {
                uint8_t cp[2] = { (uint8_t)(close_code >> 8), (uint8_t)close_code };
                int fl = ws_encode(WS_CLOSE, cp, 2, frame, g_cfg.ws_msg_max + 8);
                if (fl > 0) {
                    send_all(fd, frame, fl);
                }
                open = false;
                break;
            }
            memmove(in, in + used, have - used);
            have -= used;
            if (f->op == WS_TEXT) {
                int len = rs->handle((const char *)f->payload, f->len, msg, g_cfg.ws_msg_max);
                if (len < 0) {
                    len = snprintf(msg, g_cfg.ws_msg_max, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"The answer does not fit\"}}");
                }
                if (len > 0) {
                    int fl = ws_encode(WS_TEXT, (const uint8_t *)msg, len, frame, g_cfg.ws_msg_max + 8);
                    if (fl < 0 || !send_all(fd, frame, fl)) {
                        open = false;
                        break;
                    }
                }
            } else if (f->op == WS_PING) {
                int fl = ws_encode(WS_PONG, f->payload, f->len, frame, g_cfg.ws_msg_max + 8);
                if (fl < 0 || !send_all(fd, frame, fl)) {
                    open = false;
                    break;
                }
            } else if (f->op == WS_CLOSE) {
                int fl = ws_encode(WS_CLOSE, f->payload, f->len > 2 ? 2 : f->len, frame, g_cfg.ws_msg_max + 8);
                if (fl > 0) {
                    send_all(fd, frame, fl);
                }
                open = false;
                break;
            }
        }
    }
    free(in);
    free(f);
    free(msg);
    free(frame);
}

void client_done()
{
    portENTER_CRITICAL();
    g_clients--;
    portEXIT_CRITICAL();
}

void client_task(void *arg)
{
    int fd = (int)(intptr_t)arg;
    bool gated = false;
    if (g_app->starting()) {
        portENTER_CRITICAL();
        if (!g_gate_busy) {
            g_gate_busy = true;
            gated = true;
        }
        portEXIT_CRITICAL();
        if (!gated) {                                               // the one start-time client is being served
            send_all(fd, BUSY, sizeof(BUSY) - 1);
            closesocket(fd);
            client_done();
            vTaskDelete(NULL);
        }
    }
    HttpReq *q = gated ? &g_gate_q : (HttpReq *)NET_ALLOC(sizeof(HttpReq));
    if (!q) {
        send_all(fd, BUSY, sizeof(BUSY) - 1);
        closesocket(fd);
        client_done();
        vTaskDelete(NULL);
    }
    http_init(q);
    set_timeout(fd, 5000);
    char buf[256];
    while (q->state == HS_HEAD || q->state == HS_BODY) {
        int r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) {
            break;                                                  // closed, or the client was too slow
        }
        http_feed(q, buf, r);
    }
    bool ws = false;
    char wspath[HTTP_PATH_MAX];
    wspath[0] = 0;
    if (q->state == HS_READY || q->state == HS_BAD) {
        snprintf(wspath, sizeof(wspath), "%s", q->path);
        const int OUT = gated ? GATE_OUT : g_cfg.out_max;     // T162: the bug report goes out in one piece
        bool locked = false;
        char *out = 0;
        if (gated) {
            out = g_gate_out;
        } else if (g_out_lock) {
            locked = xSemaphoreTake(g_out_lock, 5000 / portTICK_PERIOD_MS) == pdTRUE;
        }
        if (!gated && (!g_out_lock || locked)) {
            out = (char *)NET_ALLOC(OUT);
        }
        if (!out) {
            send_all(fd, BUSY, sizeof(BUSY) - 1);                   // no buffer to build the answer in
        } else {
            int n = q->state == HS_READY ? g_app->serve(*q, out, OUT, &ws) : g_app->serve_bad(*q, out, OUT);
            if (n < 0) {
                n = snprintf(out, OUT, "HTTP/1.1 507 Insufficient Storage\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                ws = false;
            }
            if (ws) {
                portENTER_CRITICAL();
                bool room = g_ws_open < g_cfg.ws_max;
                if (room) {
                    g_ws_open++;
                }
                portEXIT_CRITICAL();
                if (!room) {
                    n = snprintf(out, OUT, "%s", BUSY);             // the last slots stay for plain requests
                    ws = false;
                }
            }
            if (!send_all(fd, out, n) && ws) {
                portENTER_CRITICAL();
                g_ws_open--;
                portEXIT_CRITICAL();
                ws = false;
            }
        }
        if (!gated) {
            free(out);
        }
        if (locked) {
            xSemaphoreGive(g_out_lock);
        }
    }
    if (gated) {
        g_gate_busy = false;
    } else {
        free(q);
    }                                                              // (the request and the buffer are not kept for the length of a WS connection)
    if (ws) {
        NetWs *rs = g_app->ws_open_path(wspath);
        if (rs) {
            ws_loop(fd, rs);
            g_app->ws_close(rs);
        }
        portENTER_CRITICAL();
        g_ws_open--;
        portEXIT_CRITICAL();
    }
    closesocket(fd);
    client_done();
    vTaskDelete(NULL);
}

void listen_task(void *)
{
    for (;;) {
        char ip[24];
        while (!g_net_up(ip, sizeof(ip))) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);                  // no address, nothing to serve
        }
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = INADDR_ANY;
        a.sin_port = htons(g_cfg.port);
        if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
            printf("[%s] port %d is not free; trying again\n", g_cfg.name, g_cfg.port);
            closesocket(s);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        listen(s, 2);
        printf("[%s] http://%s:%d/ and ws://%s:%d/v1/ws\n", g_cfg.name, ip, g_cfg.port, ip, g_cfg.port);
        for (;;) {
            struct sockaddr_in cli;
            socklen_t cl = sizeof(cli);
            int fd = accept(s, (struct sockaddr *)&cli, &cl);
            if (fd < 0) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
            if (g_clients >= g_cfg.clients_max || (g_clients >= 1 && g_app->starting())) {
                send_all(fd, BUSY, sizeof(BUSY) - 1);
                closesocket(fd);
                continue;
            }
            portENTER_CRITICAL();
            g_clients++;
            portEXIT_CRITICAL();
            if (xTaskCreate(client_task, "Net client", g_cfg.stack_words, (void *)(intptr_t)fd, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
                portENTER_CRITICAL();
                g_clients--;
                portEXIT_CRITICAL();
                closesocket(fd);
            }
        }
    }
}

}

void nettransport_start(NetApp *app, const NetConfig &cfg, bool (*net_up)(char *, int))
{
    if (g_up) {
        return;
    }
    g_up = true;
    g_app = app;
    g_cfg = cfg;
    g_net_up = net_up;
    if (g_cfg.share_out) {
        g_out_lock = xSemaphoreCreateMutex();
    }
    xTaskCreate(listen_task, "Net server", 4 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
}
