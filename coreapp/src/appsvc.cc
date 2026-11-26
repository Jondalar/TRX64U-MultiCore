// The machine's groups of the service tables (appsvc.h): the network, the flash the firmware boots from, the hand-over (base API 4.0 has no C64 bus and no TRX
// block; T239.3 D57). PROVENANCE: the flash read follows vendor/gideon/software/app_loader/app_api_impl.cc (GPL-3.0, submodule commit aa2ef5dc): api_flash_read,
// api_machine_handover. Changed: the hand-over gives the pixel layer back first, as the update jump of hw.cc does.
#include "appsvc.h"
#include "hw.h"
#include "iomap.h"
#include "itu.h"
#include "flash.h"
#include "trx_lv_port.h"
#include "lwip/sockets.h"
#include "netdb.h"
#include <errno.h>
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

void appsvc_init(CoreCtx *) { }

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

static volatile uint16_t s_rpc_port;

// SvcNet::announce: the TCP port of the core's runtime service. The base keeps it; trx_io's identity block (app_trx_rpc_port) and the UDP ident reply report it as `rpc`.
static void net_announce(uint16_t port) { s_rpc_port = port; }
uint16_t appsvc_rpc_port(void) { return s_rpc_port; }

static const SvcNet g_net = { sizeof(SvcNet), net_listen, net_accept, net_recv, net_send, net_close, net_wait, net_peer, net_connect, net_resolve, net_announce };
const SvcNet *appsvc_net(void) { return &g_net; }

// An application ended: the port it announced goes back (the port is on the account of the image that announced it). Its sockets and jobs are not here: the loader closes the
// ones on its account before this runs (AppHost::end_application), so the listener of a resident library survives it.
void appsvc_end(void)
{
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
