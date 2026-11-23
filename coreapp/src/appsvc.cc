// The machine's groups of the service tables (appsvc.h). PROVENANCE: the C64 bus group and the flash read follow vendor/gideon/software/app_loader/app_api_impl.cc
// (GPL-3.0, submodule commit aa2ef5dc): api_c64_*, api_flash_read, api_machine_handover. Changed: the hand-over gives the pixel layer back first, as the update
// jump of hw.cc does.
#include "appsvc.h"
#include "ops_glue.h"
#include "hw.h"
#include "iomap.h"
#include "u64.h"
#include "itu.h"
#include "flash.h"
#include "trx_io.h"
#include "trx_lv_port.h"
#include "trx_ring.h"
#include "lwip/sockets.h"
#include "netdb.h"
#include <errno.h>
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define C64_CARTREGS(off) (*(volatile uint8_t *)(C64_CARTREGS_BASE + (off)))
#define SERVE_WHILE_STOPPED 0x01

static CoreCtx *g_ctx;

void appsvc_init(CoreCtx *c) { g_ctx = c; }

// ---- the C64's bus ---------------------------------------------------------------------------------------------------------------------------------

static uint8_t c64_peek(uint16_t addr) { return *((volatile uint8_t *)(C64_MEMORY_BASE + addr)); }
static void c64_poke(uint16_t addr, uint8_t val) { *((volatile uint8_t *)(C64_MEMORY_BASE + addr)) = val; }

static int c64_dma_read(uint16_t addr, void *dst, int len)
{
    uint8_t *d = (uint8_t *)dst;
    for (int i = 0; i < len; i++) {
        d[i] = *((volatile uint8_t *)(C64_MEMORY_BASE + (uint32_t)(addr + i)));
    }
    return len;
}

static int c64_dma_write(uint16_t addr, const void *src, int len)
{
    const uint8_t *s = (const uint8_t *)src;
    for (int i = 0; i < len; i++) {
        *((volatile uint8_t *)(C64_MEMORY_BASE + (uint32_t)(addr + i))) = s[i];
    }
    return len;
}

static struct {
    uint8_t bus_bridge, bus_internal, bus_external, serve_control;
    uint8_t cpu_ddr, cpu_port;
    uint8_t dma_memonly;
    bool valid;
} g_saved;

static void c64_cart_setup_bus(void)
{
    g_saved.bus_bridge = C64_BUS_BRIDGE;
    g_saved.bus_internal = C64_BUS_INTERNAL;
    g_saved.bus_external = C64_BUS_EXTERNAL;
    g_saved.serve_control = C64_CARTREGS(0xD);
    g_saved.cpu_ddr = c64_peek(0x00);
    g_saved.cpu_port = c64_peek(0x01);
    g_saved.dma_memonly = C64_DMA_MEMONLY;
    g_saved.valid = true;
    C64_DMA_MEMONLY = 0;                                // DMA has to see the C64's memory map (I/O, the cartridge), not raw RAM
    C64_CARTREGS(0xD) = (uint8_t)(g_saved.serve_control | SERVE_WHILE_STOPPED);
    C64_BUS_BRIDGE = 0x03;                              // dynamic + writes
    C64_BUS_INTERNAL = 0;
    C64_BUS_EXTERNAL = 15;
    c64_poke(0x00, 0x2F);
    c64_poke(0x01, 0x37);                               // LORAM = HIRAM = CHAREN = 1
}

static void c64_cart_restore_bus(void)
{
    if (!g_saved.valid) {
        return;
    }
    c64_poke(0x00, g_saved.cpu_ddr);
    c64_poke(0x01, g_saved.cpu_port);
    C64_BUS_BRIDGE = g_saved.bus_bridge;
    C64_BUS_INTERNAL = g_saved.bus_internal;
    C64_BUS_EXTERNAL = g_saved.bus_external;
    C64_CARTREGS(0xD) = g_saved.serve_control;
    C64_DMA_MEMONLY = g_saved.dma_memonly;
    g_saved.valid = false;
}

static void c64_cart_mode(uint8_t mode) { C64_CARTREGS(0x0) = mode; }
static uint8_t c64_cart_detect(void) { return U64_CART_DETECT & 0x03; }

static const SvcC64 g_c64 = {
    sizeof(SvcC64), c64_peek, c64_poke, c64_dma_read, c64_dma_write, c64_cart_setup_bus, c64_cart_restore_bus, c64_cart_mode, c64_cart_detect,
};

static void trx_reset_c64(void);

// C64Api (c64_api.h, export "c64" major 1): the members of SvcC64 in their order, then dma_read_frozen and cart_preference, which the base does not have (null: a library
// of the core that has them exports its own table), and reset, the warm reset through the core's operation.
static const C64Api g_c64api = {
    sizeof(C64Api), c64_peek, c64_poke, c64_dma_read, c64_dma_write, c64_cart_setup_bus, c64_cart_restore_bus, c64_cart_mode, c64_cart_detect, 0, 0, trx_reset_c64,
};

const C64Api *appsvc_c64_api(void)
{
    return appsvc_c64() ? &g_c64api : 0;
}

const SvcC64 *appsvc_c64(void)
{
    return g_ctx && g_ctx->desc && !memcmp(g_ctx->desc->id, "TRX2", 4) ? &g_c64 : 0;       // the C64 descriptor of the TRX64 line (cores/c64.cc)
}

// ---- the TRX block ---------------------------------------------------------------------------------------------------------------------------------
// PROVENANCE: the debug block's ownership, the ring table and the video table follow vendor/gideon/software/app_loader/app_api_net.cc (api_trx_*, api_ring_*,
// api_video; GPL-3.0, submodule commit aa2ef5dc). Changed: no detach and no ui_request (a resident application of this base is a task of its own and its
// screens are the base's), and the ring is this application's trx_ring.h.

#define TRX_P0(off) (TRX_IO_BASE + (off))
#define TRX_P3(off) (TRX_IO_BASE + 0x3000 + (off))

static void trx_reset_c64(void)
{
    OpResult r;
    if (g_ctx) {
        ops_call(g_ctx, "machine.warm_reset", 0, 0, &r);
    }
}

static bool s_owned;
static volatile uint16_t s_rpc_port;

static int trx_acquire(uint32_t)
{
    // Hold the machine first, then take run control from C64_STOP: the gate never opens in between.
    trx_io_write32(TRX_P3(0x000), 0x01);                // PAUSE_CB
    trx_io_write32(TRX_P0(0x118), 1);                   // OWNER
    s_owned = true;
    return 0;
}

static void trx_release(void)
{
    if (!s_owned) {
        return;
    }
    trx_io_write32(TRX_P3(0x048), 0);                   // DBG_CTRL: exec bitmap off
    trx_io_write32(TRX_P3(0x080), 0);                   // watch unit 0 off
    trx_io_write32(TRX_P3(0x0A0), 0);                   // watch unit 1 off
    trx_io_write32(TRX_P3(0x0C0), 0);                   // drive comparator off
    trx_io_write32(TRX_P0(0x134), 0);                   // CYC_CMP disarmed
    trx_io_write32(TRX_P0(0x118), 0);                   // OWNER: C64_STOP rules the gate again
    trx_io_write32(TRX_P3(0x000), 0x03);                // RUN: drop the debugger's hold and the stop record
    s_owned = false;
}

static void trx_announce(uint16_t port) { s_rpc_port = port; }
uint16_t appsvc_rpc_port(void) { return s_rpc_port; }

static int ring_status(u2p_ring_status_t *st)
{
    TrxRingStatus rs;
    trx_ring_status(&rs);
    memset(st, 0, sizeof(*st));
    st->present = rs.present;
    st->on = rs.on;
    st->frames = rs.frames;
    st->targets = rs.targets;
    st->head_seq = rs.head_seq;
    st->oldest_seq = rs.oldest_seq;
    st->landed_seq = trx_ring_landed();
    st->landed = st->landed_seq != 0;
    return rs.present ? 0 : -6;
}

static int ring_frame(int index, u2p_ring_frame_t *f)
{
    static TrxFrame tf;                                 // 260 bytes: off the task's stack
    if (!trx_ring_frame(index, &tf)) {
        return -2;
    }
    memset(f, 0, sizeof(*f));
    f->seq = tf.seq;
    f->c24 = tf.c24;
    f->cycles = tf.cycles;
    f->line = tf.line;
    f->x = tf.x;
    f->flags = tf.flags;
    f->target = trx_ring_reachable(index);
    return 0;
}

static int ring_land(int index) { return trx_ring_land(index, NULL); }

typedef char trace_rec_check[(sizeof(u2p_trace_rec_t) == sizeof(TrxTraceRec)) ? 1 : -1];
typedef char trace_hit_check[(sizeof(u2p_trace_hit_t) == sizeof(TrxTraceHit)) ? 1 : -1];

static int trace_status(u2p_trace_status_t *st)
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
    return ok ? 0 : -6;
}

static int trace_read(uint32_t first, uint32_t n, u2p_trace_rec_t *out) { return trx_ring_trace_read(first, n, (TrxTraceRec *)out); }

static int trace_whowrote(uint16_t addr, int limit, u2p_trace_hit_t *hits, int *exhausted)
{
    bool ex = true;
    int n = trx_ring_trace_whowrote(addr, limit, (TrxTraceHit *)hits, &ex);
    *exhausted = ex ? 1 : 0;
    return n;
}

static int ring_video(u2p_video_t *v)
{
    memset(v, 0, sizeof(*v));
    v->frame_cycles = trx_ring_frame_cycles();
    v->fps_x100 = trx_ring_fps_x100();
    v->hz60 = trx_ring_fps() == 60;
    v->lines = v->hz60 ? 263 : 312;
    v->cycles_per_line = v->hz60 ? 65 : 63;
    v->cpu_hz = v->hz60 ? 1022727 : 985248;
    return 0;
}

static SvcRing g_ring = {
    sizeof(SvcRing), ring_status, ring_frame, trx_ring_find, ring_land, trx_ring_commit, trx_ring_error, { 0, 0, 0 }, trx_ring_trace_ctl, trace_status, trace_read, 0,
    trace_whowrote, 0,
};
static SvcTrx g_trx;

const SvcTrx *appsvc_trx(void)
{
    volatile uint8_t *id = (volatile uint8_t *)TRX_IO_BASE;
    if (id[0] != 'T' || id[1] != 'R' || id[2] != 'X' || id[3] != '2') {
        return 0;
    }
    memset(&g_trx, 0, sizeof(g_trx));
    g_trx.size = sizeof(g_trx);
    g_trx.regs = (volatile uint8_t *)TRX_IO_BASE;
    g_trx.caps = trx_io_caps();
    g_trx.acquire = trx_acquire;
    g_trx.release = trx_release;
    g_trx.announce = trx_announce;
    TrxRingStatus rs;
    trx_ring_status(&rs);
    g_trx.ring = rs.present ? &g_ring : 0;
    g_trx.video = ring_video;
    g_trx.reset_c64 = trx_reset_c64;
    return &g_trx;
}

void appsvc_trx_ring_update(void)
{
    if (g_trx.size) {                                    // only the member changes: a library may hold the table and read it meanwhile
        TrxRingStatus rs;
        trx_ring_status(&rs);
        g_trx.ring = rs.present ? &g_ring : 0;
    }
}

// ---- net -------------------------------------------------------------------------------------------------------------------------------------------
// PROVENANCE: app_api_net.cc's api_net_* (same source as above). The loader (libload.cc) keeps the owner of every handle and closes the sockets of an image that ends.

#define NET_MAX 16
static int s_socks[NET_MAX];
static int s_nsocks;

static void net_track(int s)
{
    if (s_nsocks < NET_MAX) {
        s_socks[s_nsocks++] = s;
    }
}

static bool net_untrack(int s)
{
    for (int i = 0; i < s_nsocks; i++) {
        if (s_socks[i] == s) {
            s_socks[i] = s_socks[--s_nsocks];
            return true;
        }
    }
    return false;
}

static void net_nonblocking(int s)
{
    int flags = lwip_fcntl(s, F_GETFL, 0);
    lwip_fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

static int net_listen(uint16_t port, int backlog)
{
    if (s_nsocks >= NET_MAX) {
        return -5;
    }
    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -7;
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
        return -5;
    }
    net_nonblocking(s);
    net_track(s);
    return s;
}

static int net_accept(int listener)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    if (s_nsocks >= NET_MAX) {
        return -5;
    }
    int s = lwip_accept(listener, (struct sockaddr *)&a, &al);
    if (s < 0) {
        return -5;
    }
    net_nonblocking(s);
    int one = 1;
    lwip_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    net_track(s);
    return s;
}

static int net_recv(int h, void *buf, int len)
{
    int n = lwip_recv(h, buf, len, MSG_DONTWAIT);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        return 0;
    }
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return -5;
    }
    return -3;
}

static int net_send(int h, const void *buf, int len)
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
            vTaskDelay(1);                              // the send buffer is full: let lwIP drain it
            idle++;
            continue;
        }
        return -3;
    }
    return done;
}

static int net_close(int h)
{
    if (!net_untrack(h)) {
        return -1;
    }
    lwip_close(h);
    return 0;
}

static int net_wait(const int *handles, int n, uint32_t timeout_ms)
{
    if ((n < 0) || (n > 16)) {
        return -1;
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
    if (timeout_ms < portTICK_PERIOD_MS) {              // at least one tick: a shorter timeout became 0 ticks in sys_arch_sem_wait
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

static int net_peer(int h, char *buf, int len)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    if (!buf || (len < 1) || (lwip_getpeername(h, (struct sockaddr *)&a, &al) < 0) || (a.sin_family != AF_INET)) {
        return -1;
    }
    char t[32];
    char ip[20];
    inet_ntoa_r(a.sin_addr, ip, sizeof(ip));
    snprintf(t, sizeof(t), "%s:%u", ip, (unsigned)ntohs(a.sin_port));
    strncpy(buf, t, (size_t)len - 1);
    buf[len - 1] = 0;
    return (int)strlen(buf);
}

// The outgoing half (base 3.3). The name goes through lwIP's resolver (a dotted address is taken as it is).
static int net_resolve(const char *name, char *buf, int len)
{
    struct hostent he, *rh = 0;
    char hb[256];
    int err = 0;
    if (!name || !buf || len < 8 || lwip_gethostbyname_r(name, &he, hb, sizeof(hb), &rh, &err) != 0 || !rh || rh->h_addrtype != AF_INET || !rh->h_addr) {
        return -1;
    }
    struct in_addr a;
    memcpy(&a, rh->h_addr, sizeof(a));
    char ip[20];
    inet_ntoa_r(a, ip, sizeof(ip));
    if ((int)strlen(ip) >= len) {
        return -1;
    }
    strcpy(buf, ip);
    return (int)strlen(buf);
}

static int net_connect(const char *host, uint16_t port, uint32_t timeout_ms)
{
    if (s_nsocks >= NET_MAX) {
        return -5;
    }
    char ip[20];
    if (net_resolve(host, ip, sizeof(ip)) < 0) {
        return -6;
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (!inet_aton(ip, &a.sin_addr)) {
        return -6;
    }
    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -7;
    }
    net_nonblocking(s);
    int r = lwip_connect(s, (struct sockaddr *)&a, sizeof(a));
    if (r < 0 && errno == EINPROGRESS) {
        if (timeout_ms < portTICK_PERIOD_MS) {
            timeout_ms = portTICK_PERIOD_MS;
        }
        fd_set wr;
        FD_ZERO(&wr);
        FD_SET(s, &wr);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int so = 0;
        socklen_t sl = sizeof(so);
        if (lwip_select(s + 1, NULL, &wr, NULL, &tv) > 0 && lwip_getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &sl) == 0 && so == 0) {
            r = 0;
        }
    }
    if (r < 0) {
        lwip_close(s);
        return -3;
    }
    int one = 1;
    lwip_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    net_track(s);
    return s;
}

static const SvcNet g_net = { sizeof(SvcNet), net_listen, net_accept, net_recv, net_send, net_close, net_wait, net_peer, net_connect, net_resolve };
const SvcNet *appsvc_net(void) { return &g_net; }

// An application ended: the debug block goes back. Its sockets and jobs are not here: the loader closes the ones on its account before this runs (AppHost::end_application),
// so the listener of a resident library survives it.
void appsvc_end(void)
{
    trx_release();
    s_rpc_port = 0;
}

// ---- the flash, the hand-over ----------------------------------------------------------------------------------------------------------------------

int appsvc_flash_read(uint32_t addr, void *buf, int len)
{
    Flash *flash = get_flash();
    if (!flash) {
        return -6;
    }
    if (!buf || len < 0) {
        return -1;
    }
    uint32_t size = (uint32_t)flash->get_number_of_pages() * (uint32_t)flash->get_page_size();
    if (addr > size || (uint32_t)len > size - addr) {
        return -1;
    }
    flash->read_linear_addr((int)addr, len, buf);
    return len;
}

// The steps the update jump takes before it leaves this firmware: the layer is given back, the rocker's link is quiet, the interrupts are off,
// the receive DMAs stopped.
void appsvc_handover(void)
{
    printf("[app] hand-over: this firmware is left\n");
    trx_lv_yield_layer();
    hw_rocker_quiesce();
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    portDISABLE_INTERRUPTS();
    hw_net_rx_stop();
}
