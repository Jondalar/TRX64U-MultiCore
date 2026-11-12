/*
 * trxmon.u2a — the TRX64 Ultimate monitor (T21). Serves the TRX64 runtime
 * contract (WebSocket JSON-RPC 2.0, T21-monitor.md §7.4) on TCP 4312 so
 * C64RE can drive the real machine with the TRX64 method names, and answers
 * the monitor verbs over monitor/exec.
 *
 * One loop: wait on the sockets for a few ms, answer what came, look at the
 * debug block's stop record. With a screen (a person started it from the menu)
 * the same loop also reads the keys and draws the monitor (ui.c, T21 §8); `x`
 * leaves it, and with stops armed trxmon stays on as a detached task that
 * asks the firmware to put the screen back when one fires (trx->ui_request).
 * Headless (REST action=serve) it is that detached task from the start. The
 * loader releases the debug block and closes what is left open when it ends.
 */
#include "u2p_app.h"

#include "hw.h"
#include "mach.h"
#include "mon.h"
#include "rpc.h"
#include "ui.h"
#include "ws.h"

#define RPC_PORT    4312
#define MAX_CLIENTS 1 /* exactly one RPC connection (T21 §7.4): a second is refused with the reason */
#define MAX_REFUSE  2 /* connections being refused at the same time */
#define REFUSE_RX   1024
#define REFUSE_MS   1000  /* a refused client that sends no request head is answered after this long */
#define HANDSHAKE_MS 5000 /* the holder must finish the HTTP upgrade within this, or it does not hold the port */
/* A holder that vanished (power off, cable, a sleeping laptop) sends no FIN and would hold the one place until the
 * TCP stack gives up, minutes later. After KEEPALIVE_IDLE_MS without a byte from it trxmon sends a WebSocket ping
 * (RFC 6455: a client answers with a pong); when nothing at all has come for KEEPALIVE_DEAD_MS the holder is gone. */
#ifndef KEEPALIVE_IDLE_MS
#define KEEPALIVE_IDLE_MS 20000
#endif
#ifndef KEEPALIVE_DEAD_MS
#define KEEPALIVE_DEAD_MS 50000
#endif
#define RX_SIZE     (20 * 1024)
#define MSG_SIZE    (16 * 1024)

static const u2p_api_t *A;

typedef struct
{
    int h; /* -1: free */
    int ws;
    int dead;
    int rxlen;
    int msglen;
    uint32_t t0;
    uint32_t t_rx;   /* the last byte that came from it */
    uint32_t t_ping; /* the last keepalive ping sent */
    char peer[24]; /* "a.b.c.d:port", "" when the firmware does not tell */
    uint8_t rx[RX_SIZE];
    char msg[MSG_SIZE + 1];
} client_t;

static client_t C[MAX_CLIENTS];

typedef struct
{
    int h; /* -1: free */
    int rxlen;
    uint32_t t0;
    char rx[REFUSE_RX];
} refuse_t;

static refuse_t X[MAX_REFUSE];
static int s_listener = -1;

uint32_t plat_ms(void) { return A->sys->tick_ms(); }

const char *plat_firmware_git(void) { return U2P_HAS(A, firmware_git) ? A->firmware_git : ""; }

const char *plat_firmware_version(void) { return A->firmware_version ? A->firmware_version : ""; }

void plat_yield(void) { A->sys->delay_ms(0); }

/* A firmware whose delay_ms rounds down to whole ticks (pdMS_TO_TICKS: 2 ms was a bare yield at 200 Hz)
 * would never let the serve loop run: the minimum is two ticks there. */
void plat_sleep(uint32_t ms) { A->sys->delay_ms(ms < PLAT_SLEEP_MIN_MS ? PLAT_SLEEP_MIN_MS : ms); }

void *plat_fopen(const char *path, const char *mode) { return U2P_HAS(A, fs) ? A->fs->fopen(path, mode) : 0; }
int plat_fread(void *buf, int n, void *f) { return A->fs->fread(buf, 1, n, f); }
int plat_fwrite(const void *buf, int n, void *f) { return A->fs->fwrite(buf, 1, n, f); }
void plat_fclose(void *f) { A->fs->fclose(f); }
int plat_mkdir(const char *path) { return U2P_HAS(A, fs) ? A->fs->mkdir(path) : -1; }
int plat_exists(const char *dir, const char *name) { return U2P_HAS(A, fs) ? A->fs->exists(dir, name) : 0; }
int plat_dir(const char *path, plat_dir_cb cb, void *ctx)
{
    return (U2P_HAS(A, fs) && U2P_HAS(A->fs, dir)) ? A->fs->dir(path, cb, ctx) : -1;
}
int plat_config(const char *category, const char *item, char *buf, int len)
{
    return U2P_HAS(A, config) ? A->config->get(category, item, buf, len) : -1;
}
int plat_rmdir(const char *path) { return (U2P_HAS(A, fs) && U2P_HAS(A->fs, rmdir)) ? A->fs->rmdir(path) : -1; }

int plat_media(int role, char *buf, int len)
{
    if (!U2P_HAS(A->media, source)) {
        return 0;
    }
    return A->media->source(role, buf, len);
}

/* A holder that stopped reading fills the socket's send buffer; the firmware's send then waits (up to 25 s) for it
 * to drain. The first send that took this long drops the holder, so it costs one wait and not one per notification. */
#define SEND_SLOW_MS 1000u

static int send_all(client_t *c, const void *p, int n)
{
    if (c->dead) {
        return -1;
    }
    uint32_t t0 = plat_ms();
    int r = A->net->send(c->h, p, n);
    if (r < 0) {
        c->dead = 1;
        return -1;
    }
    if ((uint32_t)(plat_ms() - t0) >= SEND_SLOW_MS) {
        A->sys->log("trxmon: the RPC holder %s does not read (a send took %u ms): dropped\n", c->peer,
                    (unsigned)(plat_ms() - t0));
        c->dead = 1;
        return -1;
    }
    return 0;
}

/* Every packet costs the firmware's network stack a few milliseconds of CPU: a frame goes out in one send (header and
 * payload together), and frames sent between srv_cork(1) and srv_cork(0) go out together in one more. */
#define CORK_MAX 3072

static uint8_t s_cork[CORK_MAX];
static int s_cork_n, s_cork_on, s_cork_cid;

static void cork_flush(void)
{
    if (s_cork_n && (s_cork_cid >= 0) && (s_cork_cid < MAX_CLIENTS) && (C[s_cork_cid].h >= 0)) {
        send_all(&C[s_cork_cid], s_cork, s_cork_n);
    }
    s_cork_n = 0;
}

void srv_cork(int on)
{
    if (!on) {
        cork_flush();
    }
    s_cork_on = on;
}

uint32_t srv_holder_idle_ms(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if ((C[i].h >= 0) && C[i].ws && !C[i].dead) {
            return plat_ms() - C[i].t_rx;
        }
    }
    return 0xFFFFFFFFu;
}

static void send_frame(client_t *c, int opcode, const void *p, int n)
{
    uint8_t buf[10 + 1400];
    int hl = ws_frame_header(buf, opcode, (uint64_t)n);
    if (n <= 1400) {
        memcpy(buf + hl, p, (size_t)n);
        int cid = (int)(c - C);
        if (s_cork_on) {
            if ((s_cork_cid != cid) || (s_cork_n + hl + n > CORK_MAX)) {
                cork_flush();
            }
            if (hl + n <= CORK_MAX) {
                s_cork_cid = cid;
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

void srv_send(int cid, const char *text, int len)
{
    if ((cid >= 0) && (cid < MAX_CLIENTS) && (C[cid].h >= 0) && C[cid].ws) {
        send_frame(&C[cid], WS_TEXT, text, len);
    }
}

void srv_broadcast(const char *text, int len)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        srv_send(i, text, len);
    }
}

static void drop(int i)
{
    client_t *c = &C[i];
    if (c->h >= 0) {
        A->net->close(c->h);
    }
    c->h = -1;
    rpc_client_gone(i);
}

static void close_ws(client_t *c, int code)
{
    uint8_t b[2] = { (uint8_t)(code >> 8), (uint8_t)code };
    send_frame(c, WS_CLOSE, b, 2);
    c->dead = 1;
}

static void http_head(client_t *c)
{
    http_req_t r;
    int n = ws_http_parse((const char *)c->rx, c->rxlen, &r);
    if (n == 0) {
        return;
    }
    if ((n < 0) || !r.upgrade) {
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

static void ws_frames(int cid)
{
    client_t *c = &C[cid];
    while (!c->dead) {
        ws_frame_t f;
        int h = ws_frame_parse(c->rx, c->rxlen, &f);
        if (h == 0) {
            if (c->rxlen >= RX_SIZE) {
                close_ws(c, 1009);
            }
            return;
        }
        if ((h < 0) || !f.masked) {
            close_ws(c, 1002);
            return;
        }
        if (f.len > (uint64_t)(RX_SIZE - h)) {
            close_ws(c, 1009);
            return;
        }
        int total = h + (int)f.len;
        if (c->rxlen < total) {
            return; /* the payload is not all here yet */
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
                rpc_request(cid, c->msg, c->msglen);
                c->msglen = 0;
            }
            break;
        case WS_PING: send_frame(c, WS_PONG, p, (int)f.len); break;
        case WS_CLOSE:
            send_frame(c, WS_CLOSE, p, (int)(f.len >= 2 ? 2 : 0));
            c->dead = 1;
            break;
        default: break; /* binary and pong frames are not for us */
        }
        memmove(c->rx, c->rx + total, (size_t)(c->rxlen - total));
        c->rxlen -= total;
    }
}

static void client_read(int i)
{
    client_t *c = &C[i];
    int room = RX_SIZE - c->rxlen;
    if (room <= 0) {
        c->dead = 1;
        return;
    }
    int n = A->net->recv(c->h, c->rx + c->rxlen, room);
    if (n == U2P_ERR_BUSY) {
        return;
    }
    if (n <= 0) {
        c->dead = 1;
        return;
    }
    c->rxlen += n;
    c->t_rx = plat_ms();
    if (!c->ws) {
        http_head(c);
    }
    if (c->ws) {
        ws_frames(i);
    }
}

static void peer_of(int h, char *out, int n)
{
    out[0] = 0;
    if (U2P_HAS(A->net, peer) && (A->net->peer(h, out, n) < 0)) {
        out[0] = 0;
    }
}

/* The text a refused client reads: the port, who holds it, how to get in */
static int refuse_text(char *out, int n)
{
    sbuf_t b;
    sb_init(&b, out, n);
    sb_put(&b, "port 4312 is held");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if ((C[i].h >= 0) && C[i].peer[0]) {
            sb_put(&b, " by ");
            sb_put(&b, C[i].peer);
        }
    }
    sb_put(&b, " (trxmon serves one RPC connection at a time)");
    sb_str(&b); /* the callers read it as a C string */
    return b.len;
}

/* The refusal over the answer a client can read: a WebSocket client gets the upgrade, one JSON-RPC
 * error (id null, code RPC_BUSY_CODE), a close frame with code 1013 and the reason; anything else gets
 * HTTP 503 with the text. The connection is closed afterwards. */
static void refuse_answer(refuse_t *x, int upgrade, const http_req_t *r)
{
    char text[160];
    refuse_text(text, sizeof(text));
    uint8_t hdr[10];
    if (upgrade) {
        char out[256];
        int ol = ws_http_accept(r, out, sizeof(out));
        A->net->send(x->h, out, ol);
        char msg[320];
        sbuf_t b;
        sb_init(&b, msg, sizeof(msg));
        sb_put(&b, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":" RPC_BUSY_CODE_STR ",\"message\":\"");
        sb_put(&b, text);
        sb_put(&b, "\",\"data\":{\"port\":4312}}}");
        int hl = ws_frame_header(hdr, WS_TEXT, (uint64_t)b.len);
        A->net->send(x->h, hdr, hl);
        A->net->send(x->h, msg, b.len);
        uint8_t cl[2 + 123];
        int tl = m_strlen(text);
        if (tl > 123) {
            tl = 123;
        }
        cl[0] = (uint8_t)(WS_BUSY_CLOSE >> 8);
        cl[1] = (uint8_t)WS_BUSY_CLOSE;
        memcpy(cl + 2, text, (size_t)tl);
        hl = ws_frame_header(hdr, WS_CLOSE, (uint64_t)(2 + tl));
        A->net->send(x->h, hdr, hl);
        A->net->send(x->h, cl, 2 + tl);
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
        A->net->send(x->h, out, b.len);
    }
    A->net->close(x->h);
    x->h = -1;
}

static void refuse_read(refuse_t *x)
{
    int n = A->net->recv(x->h, x->rx + x->rxlen, REFUSE_RX - x->rxlen);
    if (n == U2P_ERR_BUSY) {
        return;
    }
    if (n <= 0) { /* gone before it heard anything */
        A->net->close(x->h);
        x->h = -1;
        return;
    }
    x->rxlen += n;
    http_req_t r;
    int hn = ws_http_parse(x->rx, x->rxlen, &r);
    if ((hn == 0) && (x->rxlen < REFUSE_RX)) {
        return; /* the head is not all here yet */
    }
    refuse_answer(x, (hn > 0) && r.upgrade, &r);
}

static void refuse_start(int h)
{
    for (int i = 0; i < MAX_REFUSE; i++) {
        if (X[i].h < 0) {
            X[i].h = h;
            X[i].rxlen = 0;
            X[i].t0 = plat_ms();
            return;
        }
    }
    A->net->close(h); /* a flood of refused connections: the extra ones are dropped */
}

static void accept_one(void)
{
    int h = A->net->accept(s_listener);
    if (h < 0) {
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (C[i].h < 0) {
            memset(&C[i], 0, sizeof(C[i]) - sizeof(C[i].rx) - sizeof(C[i].msg));
            C[i].h = h;
            C[i].t0 = C[i].t_rx = C[i].t_ping = plat_ms();
            peer_of(h, C[i].peer, sizeof(C[i].peer));
            return;
        }
    }
    refuse_start(h);
}

/* ---- the network, one pass --------------------------------------------- */

/* The app runs at a priority above the firmware's network and HTTP tasks: a loop that works without waiting starves them
 * (REST answers in seconds, or not at all). Each pass of a loop therefore sleeps as long as it worked, at most
 * DUTY_MAX_MS, so the app never takes more than about half of the processor, whatever the machine does (a hit flood,
 * a screen full of changing log lines). `s_waited` is the time the pass spent blocked on the sockets. */
#define DUTY_MAX_MS 60u

static uint32_t s_waited;

static void duty_sleep(uint32_t t_start)
{
    uint32_t spent = plat_ms() - t_start;
    uint32_t busy = spent > s_waited ? spent - s_waited : 0;
    if (busy > 5) {
        plat_sleep(busy > DUTY_MAX_MS ? DUTY_MAX_MS : busy);
    }
}

/* Waits up to 5 ms for the sockets, answers what came, looks at the stop record */
static void net_pass(void)
{
    int handles[1 + MAX_CLIENTS + MAX_REFUSE];
    handles[0] = s_listener;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        handles[1 + i] = C[i].h;
    }
    for (int i = 0; i < MAX_REFUSE; i++) {
        handles[1 + MAX_CLIENTS + i] = X[i].h;
    }
    /* blocks for at least one tick (5 ms) unless a socket has something */
    uint32_t dsel = plat_ms();
    int mask = A->net->wait(handles, 1 + MAX_CLIENTS + MAX_REFUSE, 5);
    s_waited = plat_ms() - dsel;
    if (mask & 1) {
        accept_one();
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if ((C[i].h >= 0) && (mask & (2 << i))) {
            client_read(i);
        }
    }
    uint32_t now = plat_ms();
    for (int i = 0; i < MAX_REFUSE; i++) {
        if (X[i].h < 0) {
            continue;
        }
        if (mask & (2 << (MAX_CLIENTS + i))) {
            refuse_read(&X[i]);
        }
        if ((X[i].h >= 0) && ((uint32_t)(now - X[i].t0) >= REFUSE_MS)) {
            http_req_t none = { 0 };
            refuse_answer(&X[i], 0, &none); /* a client that never says anything: plain text */
        }
    }
    rpc_poll();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if ((C[i].h >= 0) && !C[i].ws && !C[i].dead && ((uint32_t)(now - C[i].t0) >= HANDSHAKE_MS)) {
            C[i].dead = 1; /* no upgrade: it must not hold the one place */
        }
        if ((C[i].h >= 0) && C[i].ws && !C[i].dead) {
            if ((uint32_t)(now - C[i].t_rx) >= KEEPALIVE_DEAD_MS) {
                A->sys->log("trxmon: the RPC holder %s sent nothing for %u ms and did not answer a ping: dropped\n",
                            C[i].peer, (unsigned)(now - C[i].t_rx));
                C[i].dead = 1;
            } else if (((uint32_t)(now - C[i].t_rx) >= KEEPALIVE_IDLE_MS) &&
                       ((uint32_t)(now - C[i].t_ping) >= KEEPALIVE_IDLE_MS)) {
                send_frame(&C[i], WS_PING, 0, 0);
                C[i].t_ping = now;
            }
        }
        if ((C[i].h >= 0) && C[i].dead) {
            drop(i);
        }
    }
}

static void net_shutdown(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (C[i].h >= 0) {
            if (C[i].ws && !C[i].dead) {
                close_ws(&C[i], 1001);
            }
            drop(i);
        }
    }
    for (int i = 0; i < MAX_REFUSE; i++) {
        if (X[i].h >= 0) {
            A->net->close(X[i].h);
            X[i].h = -1;
        }
    }
    A->net->close(s_listener);
    hw_announce(0);
    A->sys->log("trxmon: ends; the machine is %s\n", M.running ? "running" : "paused");
}

/* ---- the screen ---------------------------------------------------------- */

static void *s_win;
static ui_t s_ui; /* the output and the line history stay for as long as trxmon lives */
static ui_screen_t s_scr;
static char s_shadow[UI_ROWS_MAX][UI_COLS_MAX + 1];
static int s_shadow_cx, s_shadow_cy;

_Static_assert(KEY_RETURN == 0x0D && KEY_BACK == 0x08 && KEY_BREAK == 0x11 && KEY_DOWN == 0x12 && KEY_UP == 0x91 &&
                   KEY_PAGEUP == 0x92 && KEY_PAGEDOWN == 0x95 && KEY_DELETE == 0x7F && KEY_LEFT == 0x9D &&
                   KEY_RIGHT == 0x1D && KEY_HOME == 0x13 && KEY_END == 0x96 && KEY_TAB == 0x09 && KEY_CTRL_TAP == 0x1F8,
               "ui.c's FK_* are the firmware's key codes");

static int attr_color(int a)
{
    switch (a) {
    case UA_STATUS: return A->ui->color(4);
    case UA_REGS: return A->ui->color(0);
    case UA_ECHO: return A->ui->color(5);
    case UA_ERR: return 2; /* red in the overlay's C64 palette */
    case UA_PROMPT: return A->ui->color(2);
    case UA_BLANK: return 0;
    case UA_SEP: return A->ui->color(6);
    case UA_HINT: return A->ui->color(5);
    default: return A->ui->color(0);
    }
}

/* Draws the rows that changed. The last cell of the last row is never written: the overlay
 * scrolls when the cursor leaves it. */
static void draw(int force)
{
    ui_render(&s_ui, &s_scr);
    for (int y = 0; y < s_scr.rows; y++) {
        int cur = (y == s_scr.cursor_y);
        if (!force && m_streq(s_shadow[y], s_scr.row[y]) && !(cur && (s_shadow_cx != s_scr.cursor_x))) {
            continue;
        }
        char t[UI_COLS_MAX + 1];
        memcpy(t, s_scr.row[y], sizeof(t));
        if (y == s_scr.rows - 1) {
            t[s_scr.cols - 1] = 0;
        }
        A->ui->win_set_background(s_win, 0);
        A->ui->screen_puts(s_win, 0, y, t, attr_color(s_scr.attr[y]));
        if (cur) {
            A->ui->win_reverse_mode(s_win, 1);
            A->ui->win_set_char_at(s_win, s_scr.cursor_x, y, ' ');
            A->ui->win_reverse_mode(s_win, 0);
        }
        memcpy(s_shadow[y], s_scr.row[y], sizeof(s_shadow[y]));
    }
    s_shadow_cx = s_scr.cursor_x;
    s_shadow_cy = s_scr.cursor_y;
}

/* One key from the firmware as a screen key (ui.h): through the monitor's own keymap when the firmware has
 * one (getch_monitor: w a s d stay letters under "WASD Cursors"), else through getch. The returned code is
 * -2 when the screen is gone, -1 when no key is waiting, else 0; *mapped is the screen key (0: ignored). */
static int next_key(int *mapped)
{
    int k = U2P_HAS(A->ui, getch_monitor) ? A->ui->getch_monitor() : A->ui->getch();
    *mapped = (k < 0) ? 0 : ui_map_key(k);
    return k < 0 ? k : 0;
}

/* Drawing a full screen of changed rows costs tens of milliseconds (every character is a write to the overlay) and
 * the live log changes all of them: output is drawn at most this often. */
#define UI_REDRAW_MS 150u

/* The monitor screen, in the foreground, until the person leaves (UI_LEAVE_X, UI_LEAVE_QUIT) or
 * something ends it (-1: the screen is gone, trxmon/quit, the loader wants the app to end).
 * The RPC server is served in this same loop. `stop`: a stop put the screen up. */
static int ui_session(int stop)
{
    /* 80 columns where the firmware can: the overlay is made wide for as long as the window is open and
     * closing the window (every way out of this function) gives the menu its 40 x 25 back */
    int cols = UI_NARROW_COLS, rows = UI_NARROW_ROWS;
    s_win = U2P_HAS(A->ui, screen_open_wide) ? A->ui->screen_open_wide(UI_WIDE_ROWS) : 0;
    if (s_win) {
        cols = UI_WIDE_COLS;
        rows = UI_WIDE_ROWS;
        if (U2P_HAS(A->ui, win_get_size_x) && U2P_HAS(A->ui, win_get_size_y)) {
            cols = A->ui->win_get_size_x(s_win);
            rows = A->ui->win_get_size_y(s_win);
        }
    } else {
        s_win = A->ui->screen_open_full();
    }
    if (!s_win) {
        return -1;
    }
    ui_set_size(&s_ui, cols, rows);
    int fkey[9];
    for (int n = 1; n <= 8; n++) {
        fkey[n] = A->ui->fkey(n);
    }
    ui_set_keys(&s_ui, fkey);
    if (U2P_HAS(A->ui, key_ctrl_tap)) {
        A->ui->key_ctrl_tap(1); /* CTRL alone completes; the firmware turns it off when the app returns */
    }
    ui_open(&s_ui);
    rpc_take_user_stop();
    if (stop) {
        ui_show_stop(&s_ui);
    } else if (!s_ui.hn) {
        char hello[96];
        sbuf_t hb;
        sb_init(&hb, hello, sizeof(hello));
        sb_put(&hb, "TRX64 Monitor (trxmon). F");
        sb_dec(&hb, (uint64_t)s_ui.fk_help);
        sb_put(&hb, " help, F");
        sb_dec(&hb, (uint64_t)s_ui.fk_up);
        sb_putc(&hb, '/');
        sb_putc(&hb, 'F');
        sb_dec(&hb, (uint64_t)s_ui.fk_down);
        sb_put(&hb, " scroll, x resume+leave, /quit end.");
        ui_print(&s_ui, sb_str(&hb), UA_NORMAL);
    }
    memset(s_shadow, 0, sizeof(s_shadow));
    int result = -1;
    uint32_t last = plat_ms();
    draw(1);
    int log_dirty = 0;
    for (;;) {
        uint32_t t0 = plat_ms();
        net_pass();
        if (rpc_quit_requested() && !s_ui.leave) {
            result = (mon_quit_requested() ? UI_LEAVE_QUIT : -1);
            break;
        }
        if (hw_stop_requested()) {
            break;
        }
        int dirty = 0;
        for (int i = 0; i < 8; i++) {
            int m;
            int k = next_key(&m);
            if (k == -2) {
                result = -1;
                goto done;
            }
            if (k < 0) {
                break;
            }
            if (m) {
                int r = ui_key(&s_ui, m);
                dirty = 1;
                if (r != UI_STAY) {
                    result = r;
                    draw(0);
                    goto done;
                }
            }
        }
        if (ui_tick(&s_ui)) {
            log_dirty = 1; /* output arrived (the live log): drawn at most UI_REDRAW_MS apart, a key at once */
        }
        uint32_t now = plat_ms();
        if (dirty || (log_dirty && ((uint32_t)(now - last) >= UI_REDRAW_MS)) || ((uint32_t)(now - last) >= 250)) {
            last = now;
            log_dirty = 0;
            draw(0);
        }
        duty_sleep(t0);
    }
done:
    if (U2P_HAS(A->ui, key_ctrl_tap)) {
        A->ui->key_ctrl_tap(0);
    }
    A->ui->screen_close(s_win);
    s_win = 0;
    return result;
}

/* ---- the detached task -------------------------------------------------- */

#define UI_PARK_WAIT_MS 1000 /* how long the screen callback waits for the serve loop to park */

static volatile int s_want_ui;  /* the screen callback wants the sockets */
static volatile int s_in_ui;    /* the serve loop is parked for it */
static volatile int s_end;      /* the person ended trxmon from the screen */
static volatile int s_alive;    /* the serve loop runs */
static volatile int s_requested; /* a ui_request is queued */

/* Runs in the UI task (trx->ui_request): the screen, with the serve loop parked */
static void ui_callback(void *arg)
{
    (void)arg;
    s_requested = 0;
    if (!s_alive) {
        return;
    }
    s_want_ui = 1;
    if (!plat_wait_flag(&s_in_ui, 1, &s_alive, UI_PARK_WAIT_MS)) {
        s_want_ui = 0;
        return;
    }
    int r = ui_session(1);
    if (!((r == UI_LEAVE_X) && rpc_stops_armed())) {
        s_end = 1; /* /quit, nothing armed, trxmon/quit, the screen is gone: trxmon ends */
    }
    s_want_ui = 0;
    plat_wait_flag(&s_in_ui, 0, &s_alive, 0);
}

/* The server as a task of its own (trx->detach): the menu belongs to the firmware again and it
 * never touches the ui table. A breakpoint or observer stop asks the firmware for the screen. */
static void serve(void *arg)
{
    (void)arg;
    s_alive = 1;
    s_end = 0;
    for (;;) {
        if (s_want_ui) {
            s_in_ui = 1;
            plat_wait_flag(&s_want_ui, 0, 0, 0);
            s_in_ui = 0;
            continue;
        }
        uint32_t t_pass = plat_ms();
        net_pass();
        if (rpc_quit_requested() || hw_stop_requested() || s_end) {
            break;
        }
        if (rpc_take_user_stop() && !M.running && !s_requested) {
            int r = hw_ui_request(ui_callback, 0);
            s_requested = (r == U2P_OK) || (r == U2P_ERR_BUSY);
        }
        duty_sleep(t_pass);
    }
    s_alive = 0;
    net_shutdown();
}

int app_main(const u2p_api_t *api, const u2p_app_context_t *ctx)
{
    A = api;
    if (!U2P_HAS(api, trx) || !hw_init(api->trx)) {
        api->ui->popup("TRX64 Monitor", "trxmon needs the TRX64 Ultimate bitstream\n(the debug block is not there).");
        return 1;
    }
    if (!U2P_HAS(api, net) || !U2P_HAS(api->net, wait)) {
        api->ui->popup("TRX64 Monitor", "trxmon needs a firmware with the\nnet table (app ABI 2.1).");
        return 1;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        C[i].h = -1;
    }
    for (int i = 0; i < MAX_REFUSE; i++) {
        X[i].h = -1;
    }
    s_listener = api->net->listen(RPC_PORT, 2);
    if (s_listener < 0) {
        api->ui->popup("TRX64 Monitor", "TCP port 4312 is taken.");
        return 1;
    }
    mach_init();
    mon_init();
    rpc_init();
    ui_init(&s_ui);
    hw_announce(RPC_PORT);
    api->sys->log("trxmon: serving the TRX64 runtime contract on TCP %d\n", RPC_PORT);

    if (ctx->has_ui) {
        /* A person started it: the monitor screen, in the foreground. */
        int r = ui_session(0);
        if ((r == UI_LEAVE_X) && !hw_stop_requested() && U2P_HAS(api->ui, close_menu)) {
            /* `x` is "resume and leave" (T21 §8.4): not back to the file browser, the whole overlay
             * goes, so the next break opens the monitor straight away (trx->ui_request waits for a
             * menu that is still open). */
            api->ui->close_menu();
        }
        if ((r == UI_LEAVE_X) && rpc_stops_armed() && !hw_stop_requested()) {
            /* Leaving with stops armed: trxmon stays and watches. The overlay closes, the
             * machine runs; a breakpoint puts the screen back. */
            if (hw_detach(serve, 0) == U2P_OK) {
                ctx->reply("trxmon watches the stops in the background; the screen comes back when one fires");
                return 0;
            }
        }
        net_shutdown();
        return 0;
    }

    /* No screen (REST action=serve): serve in a task of our own, so the menu, the network and the
     * machine go on (the menu is left as usual and the machine runs until a client pauses it). */
    if (hw_detach(serve, 0) == U2P_OK) {
        ctx->reply("trxmon serves on TCP 4312 in the background; trxmon/quit, the monitor's /quit or starting it again from the menu ends it");
        return 0;
    }
    serve(0);
    return 0;
}
