/*
 * app_api_net.cc
 *
 * The ABI 2.1 tables `net` (TCP sockets for an app that serves) and `trx`
 * (the TRX64 Ultimate debug block: ownership and what the firmware has to
 * undo when an app returns). Everything else an app does with the block it
 * does through `trx->regs` itself (T21-monitor.md §7).
 */

#include <errno.h>
#include <string.h>

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"

#include "app_api.h"
#include "app_api_net.h"

/* net                                                                 */

/* Every socket an app opened, closed by app_net_cleanup() when it returns. */
#define APP_NET_MAX 16
static int s_socks[APP_NET_MAX];
static int s_nsocks;

static void track(int s)
{
    if (s_nsocks < APP_NET_MAX) {
        s_socks[s_nsocks++] = s;
    }
}

static int untrack(int s)
{
    for (int i = 0; i < s_nsocks; i++) {
        if (s_socks[i] == s) {
            s_socks[i] = s_socks[--s_nsocks];
            return 1;
        }
    }
    return 0;
}

static void nonblocking(int s)
{
    int flags = lwip_fcntl(s, F_GETFL, 0);
    lwip_fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

static int api_net_listen(uint16_t port, int backlog)
{
    if (s_nsocks >= APP_NET_MAX) {
        return U2P_ERR_BUSY;
    }
    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return U2P_ERR_FAILED;
    }
    int one = 1;
    lwip_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(port);
    if ((lwip_bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) || (lwip_listen(s, backlog > 0 ? backlog : 2) < 0)) {
        lwip_close(s);
        return U2P_ERR_BUSY;
    }
    nonblocking(s);
    track(s);
    return s;
}

static int api_net_accept(int listener)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    if (s_nsocks >= APP_NET_MAX) {
        return U2P_ERR_BUSY;
    }
    int s = lwip_accept(listener, (struct sockaddr *)&a, &al);
    if (s < 0) {
        return U2P_ERR_BUSY;
    }
    nonblocking(s);
    int one = 1;
    lwip_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    track(s);
    return s;
}

static int api_net_recv(int h, void *buf, int len)
{
    int n = lwip_recv(h, buf, len, MSG_DONTWAIT);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        return 0;
    }
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return U2P_ERR_BUSY;
    }
    return U2P_ERR_IO;
}

static int api_net_send(int h, const void *buf, int len)
{
    const uint8_t *p = (const uint8_t *)buf;
    int done = 0;
    int idle = 0;
    while (done < len) {
        int n = lwip_send(h, p + done, len - done, MSG_DONTWAIT);
        if (n > 0) {
            done += n;
            idle = 0;
            continue;
        }
        if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)) && (idle < 5000)) {
            vTaskDelay(1); /* the send buffer is full: let lwIP drain it */
            idle++;
            continue;
        }
        return U2P_ERR_IO;
    }
    return done;
}

static int api_net_close(int h)
{
    if (!untrack(h)) {
        return U2P_ERR_ARG;
    }
    lwip_close(h);
    return U2P_OK;
}

static int api_net_wait(const int *handles, int n, uint32_t timeout_ms)
{
    if ((n < 0) || (n > 16)) {
        return U2P_ERR_ARG;
    }
    fd_set rd;
    FD_ZERO(&rd);
    int maxfd = -1;
    for (int i = 0; i < n; i++) {
        if (handles[i] >= 0) {
            FD_SET(handles[i], &rd);
            if (handles[i] > maxfd) {
                maxfd = handles[i];
            }
        }
    }
    // At least one tick: a shorter timeout became 0 ticks in sys_arch_sem_wait,
    // so select returned at once and the caller's loop never let the network
    // tasks run (portTICK_PERIOD_MS is 5 here).
    if (timeout_ms < portTICK_PERIOD_MS) {
        timeout_ms = portTICK_PERIOD_MS;
    }
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (maxfd < 0) {
        vTaskDelay(timeout_ms / portTICK_PERIOD_MS);
        return 0;
    }
    int r = lwip_select(maxfd + 1, &rd, NULL, NULL, &tv);
    if (r <= 0) {
        return 0;
    }
    int mask = 0;
    for (int i = 0; i < n; i++) {
        if ((handles[i] >= 0) && FD_ISSET(handles[i], &rd)) {
            mask |= 1 << i;
        }
    }
    return mask;
}

static int api_net_peer(int h, char *buf, int len)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    if (!buf || (len < 1) || (lwip_getpeername(h, (struct sockaddr *)&a, &al) < 0) || (a.sin_family != AF_INET)) {
        return U2P_ERR_ARG;
    }
    char t[32];
    char ip[20];
    inet_ntoa_r(a.sin_addr, ip, sizeof(ip));
    snprintf(t, sizeof(t), "%s:%u", ip, (unsigned)ntohs(a.sin_port));
    strncpy(buf, t, (size_t)len - 1);
    buf[len - 1] = 0;
    return (int)strlen(buf);
}

extern "C" const u2p_net_api_t g_app_net = {
    sizeof(u2p_net_api_t),
    api_net_listen,
    api_net_accept,
    api_net_recv,
    api_net_send,
    api_net_close,
    api_net_wait,
    api_net_peer,
};

/* resident app task (trx->detach)                                     */

extern "C" void app_alloc_cleanup(void);

static volatile bool s_resident;
static volatile bool s_stop_req;
static char s_cur_id[APP_ID_MAX + 1]; /* manifest id of the app that runs app_main now */
static char s_res_id[APP_ID_MAX + 1]; /* ... and of the one whose detached task runs */

typedef struct
{
    void (*fn)(void *);
    void *arg;
} resident_t;

static resident_t s_res;

extern "C" bool app_resident_active(void) { return s_resident; }

extern "C" bool app_resident_stop(uint32_t timeout_ms)
{
    if (!s_resident) {
        return true;
    }
    printf("AppLoader: asking the resident app task to end\n");
    s_stop_req = true;
    TickType_t start = xTaskGetTickCount();
    while (s_resident && ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return !s_resident;
}

static int api_trx_stop_requested(void) { return s_stop_req ? 1 : 0; }

/* trx->ui_request: one queued "open the overlay and call fn(arg)". The UI task runs it
 * (app_loader.cc, UserInterface::run_once) when it is free: a person in a menu or in another app
 * has the task until that returns, which is the queue. */
extern "C" bool app_ui_overlay_ready(void);

static void (*volatile s_uireq_fn)(void *);
static void *volatile s_uireq_arg;

extern "C" bool app_ui_request_pending(void) { return s_uireq_fn != NULL; }

extern "C" bool app_ui_request_take(void (**fn)(void *), void **arg)
{
    if (!s_uireq_fn) {
        return false;
    }
    *fn = s_uireq_fn;
    *arg = s_uireq_arg;
    s_uireq_fn = NULL;
    return true;
}

extern "C" void app_set_current_id(const char *id)
{
    if (!s_resident) {
        s_stop_req = false; /* a fresh start never sees a request left from an earlier one */
    }
    strncpy(s_cur_id, id ? id : "", APP_ID_MAX);
    s_cur_id[APP_ID_MAX] = 0;
}

extern "C" const char *app_resident_id(void) { return s_res_id; }

extern "C" void app_trx_cleanup(void);

static void resident_entry(void *pv)
{
    (void)pv;
    s_res.fn(s_res.arg);
    printf("AppLoader: resident app task ends\n");
    s_uireq_fn = NULL; /* a screen nobody will serve any more */
    app_alloc_cleanup();
    app_net_cleanup();
    app_trx_cleanup();
    s_stop_req = false; /* answered: the next app must not see it (it ended this task, not that one) */
    s_resident = false;
    vTaskDelete(NULL);
}

static int api_trx_detach(void (*fn)(void *), void *arg)
{
    if (s_resident || !fn) {
        return U2P_ERR_BUSY;
    }
    s_res.fn = fn;
    s_res.arg = arg;
    s_stop_req = false;
    memcpy(s_res_id, s_cur_id, sizeof(s_res_id));
    s_resident = true;
    // Below the network service tasks (PRIO_NETSERVICE, PRIO_TCPIP): the app
    // only runs when they have nothing to do.
    if (xTaskCreate(resident_entry, "app_resident", configMINIMAL_STACK_SIZE * 16, NULL, tskIDLE_PRIORITY,
                    NULL) != pdPASS) {
        s_resident = false;
        return U2P_ERR_FAILED;
    }
    return U2P_OK;
}

/* trx                                                                 */

static volatile uint16_t s_rpc_port; /* read by the ident service and GET /v1/info */

extern "C" uint16_t app_trx_rpc_port(void) { return s_rpc_port; }

#if U64 == 2
#include "trx_io.h"
#include "trx_cart.h"
#include "trx_ring.h"

#define TRX_P0(off)  (TRX_IO_BASE + (off))
#define TRX_P3(off)  (TRX_IO_BASE + 0x3000 + (off))
#define TRX_CAPS_DBG 0x08 /* page 3: debug (trx_dbg.v, drv_dbg.v) */

static bool s_owned;

static int api_trx_acquire(uint32_t flags)
{
    (void)flags;
    // Hold the machine first, then take run control from C64_STOP: the gate
    // never opens in between.
    trx_io_write32(TRX_P3(0x000), 0x01); // PAUSE_CB
    trx_io_write32(TRX_P0(0x118), 1);    // OWNER
    s_owned = true;
    return U2P_OK;
}

static void api_trx_release(void)
{
    if (!s_owned) {
        return;
    }
    trx_io_write32(TRX_P3(0x048), 0);    // DBG_CTRL: exec bitmap off
    trx_io_write32(TRX_P3(0x080), 0);    // watch unit 0 off
    trx_io_write32(TRX_P3(0x0A0), 0);    // watch unit 1 off
    trx_io_write32(TRX_P3(0x0C0), 0);    // drive comparator off
    trx_io_write32(TRX_P0(0x134), 0);    // CYC_CMP disarmed
    trx_io_write32(TRX_P0(0x118), 0);    // OWNER: C64_STOP rules the gate again
    trx_io_write32(TRX_P3(0x000), 0x03); // RUN: drop the debugger's hold and the stop record
    s_owned = false;
}

static void api_trx_announce(uint16_t port)
{
    s_rpc_port = port;
}

/* ring: trx_ring.h for the debugger (T21 §4.10) */

static int api_ring_status(u2p_ring_status_t *st)
{
    TrxRingStatus rs;
    trx_ring_status(&rs);
    memset(st, 0, sizeof(*st));
    st->present = rs.present;
    st->on = rs.on;
    st->reu_off = rs.reu_off;
    st->frames = rs.frames;
    st->targets = rs.targets;
    st->head_seq = rs.head_seq;
    st->oldest_seq = rs.oldest_seq;
    st->landed_seq = trx_ring_landed();
    st->landed = st->landed_seq != 0;
    return rs.present ? U2P_OK : U2P_ERR_UNSUPPORTED;
}

static int api_ring_frame(int index, u2p_ring_frame_t *f)
{
    static TrxFrame tf; /* 260 bytes: off the app's stack */
    if (!trx_ring_frame(index, &tf)) {
        return U2P_ERR_NOT_FOUND;
    }
    memset(f, 0, sizeof(*f));
    f->seq = tf.seq;
    f->c24 = tf.c24;
    f->cycles = tf.cycles;
    f->line = tf.line;
    f->x = tf.x;
    f->flags = tf.flags;
    f->target = trx_ring_reachable(index);
    return U2P_OK;
}

static int api_ring_land(int index) { return trx_ring_land(index, NULL); }

static_assert(sizeof(u2p_trace_rec_t) == sizeof(TrxTraceRec), "trace record layout");
static_assert(sizeof(u2p_trace_hit_t) == sizeof(TrxTraceHit), "hit layout");

static int api_trace_status(u2p_trace_status_t *st)
{
    TrxTraceStatus ts;
    bool ok = trx_ring_trace_status(&ts);
    memset(st, 0, sizeof(*st));
    st->present = ts.present;
    st->on = ts.on;
    st->gap = ts.gap;
    st->records = ts.records;
    st->capacity = ts.capacity;
    st->lost = ts.lost;
    st->oldest_cycle = ts.oldest_cycle;
    st->newest_cycle = ts.newest_cycle;
    return ok ? U2P_OK : U2P_ERR_UNSUPPORTED;
}

static int api_trace_read(uint32_t first, uint32_t n, u2p_trace_rec_t *out)
{
    return trx_ring_trace_read(first, n, (TrxTraceRec *)out);
}


static int api_trace_whowrote(uint16_t addr, int limit, u2p_trace_hit_t *hits, int *exhausted)
{
    bool ex = true;
    uint32_t unres = 0;
    int n = trx_ring_trace_whowrote(addr, limit, (TrxTraceHit *)hits, &ex, &unres);
    *exhausted = (ex ? 1 : 0) | (unres ? 2 : 0);
    return n;
}

static int api_log_read(uint64_t *out, int max, uint32_t *lost) { return trx_ring_log_read(out, max, lost); }

static int api_video(u2p_video_t *v)
{
    memset(v, 0, sizeof(*v));
    v->frame_cycles = trx_ring_frame_cycles();
    v->fps_x100 = trx_ring_fps_x100();
    v->hz60 = trx_ring_fps() == 60;
    v->lines = v->hz60 ? 263 : 312;
    v->cycles_per_line = v->hz60 ? 65 : 63;
    v->cpu_hz = v->hz60 ? 1022727 : 985248;
    return U2P_OK;
}

static u2p_ring_api_t s_ring = {
    sizeof(u2p_ring_api_t),
    api_ring_status,
    api_ring_frame,
    trx_ring_find,
    api_ring_land,
    trx_ring_commit,
    trx_ring_error,
    { 0, 0, 0 },
    trx_ring_trace_ctl,
    api_trace_status,
    api_trace_read,
    0,
    api_trace_whowrote,
    api_log_read,
};

static int api_trx_ui_request(void (*fn)(void *), void *arg)
{
    if (!fn || !app_ui_overlay_ready()) {
        return U2P_ERR_UNSUPPORTED;
    }
    if (s_uireq_fn) {
        return U2P_ERR_BUSY;
    }
    s_uireq_arg = arg;
    s_uireq_fn = fn;
    return U2P_OK;
}

static u2p_trx_api_t s_trx = {
    sizeof(u2p_trx_api_t),
    (volatile uint8_t *)TRX_IO_BASE,
    0,
    api_trx_acquire,
    api_trx_release,
    api_trx_announce,
    api_trx_detach,
    NULL,
    api_video,
    api_trx_stop_requested,
    api_trx_ui_request,
};

extern "C" const u2p_trx_api_t *app_trx_table(void)
{
    uint8_t caps = trx_io_caps();
    if (!(caps & TRX_CAPS_DBG)) {
        return NULL;
    }
    s_trx.caps = caps;
    TrxRingStatus rs;
    trx_ring_status(&rs);
    s_trx.ring = rs.present ? &s_ring : NULL;
    return &s_trx;
}

extern "C" void app_trx_cleanup(void)
{
    trx_ring_commit(); /* a land the app left open: the ring records again */
    api_trx_release();
    s_rpc_port = 0;
    trx_slot_apply_policy_now(); /* an app that left the port's write gate open: the policy again (T57 §4.1) */
}

#else

extern "C" const u2p_trx_api_t *app_trx_table(void) { return NULL; }
extern "C" void app_trx_cleanup(void) { s_rpc_port = 0; }

#endif

extern "C" void app_net_cleanup(void)
{
    while (s_nsocks > 0) {
        lwip_close(s_socks[--s_nsocks]);
    }
}
