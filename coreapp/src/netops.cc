// Network (T104.2 UI): the wired interface and the services that run on Gideon's network stack (config stores "Ethernet Settings" and "Network Settings"), and the Wi-Fi of the
// ESP32 control module. The wired interface's address values are this core's settings (the .cfg); net.fx pushes them into Gideon's stores at the start and at every change - never
// into the flash (the stores are not written). The WiFi is set in the host (T184 D3, T228 R26): the core reads its state here and switches the radio.
#include "netops.h"
#include "ops_glue.h"
#include "trx64u_ops.h"
#include "hw.h"
#include "wifinet.h"
#include "config.h"
#include "network_config.h"
#include "network_interface.h"
#include "netsvc.h"
#include "u64.h"
#include "socket.h"
#include "netdb.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- push the settings into Gideon's stores ------------------------------------------------------------------------------------------------------------
static void push_text(ConfigStore *st, uint8_t id, const char *t)
{
    if (t[0]) {                                       // empty: the stack's own default stays (the host name of the product, the NTP servers)
        st->set_string(id, t);
    }
}

static int fx_net(CoreCtx *c, const OpArgs &, OpOut &o)
{
    ConfigManager *cm = ConfigManager::getConfigManager();
    ConfigStore *eth = cm->find_store("Ethernet Settings");
    ConfigStore *net = cm->find_store("Network Settings");
    if (!eth || !net) {
        snprintf(o.msg, o.msglen, "The network stack is not up");
        return OPE_OK;
    }
    // R21: a value is pushed only when the core's settings have its key (Settings::find >= 0). net.fx runs for every core; one that does not list a key must not
    // overwrite the stack with the key's default. The WiFi store is not touched: the host's WiFi settings are read by hostwifi_load (run.cc) (T184 D3, T228 R26).
    auto has = [&](const char *k) { return c->settings && c->settings->find(k) >= 0; };
    auto val = [&](ConfigStore *st, uint8_t id, const char *k, int def) { if (has(k)) st->set_value(id, cfg_value(c, k, def)); };
    auto txt = [&](ConfigStore *st, uint8_t id, const char *k) { if (has(k)) push_text(st, id, cfg_text(c, k)); };
    val(eth, CFG_NET_DHCP_EN, "Use DHCP", 1);
    txt(eth, CFG_NET_IP, "Static IP");
    txt(eth, CFG_NET_NETMASK, "Static Netmask");
    txt(eth, CFG_NET_GATEWAY, "Static Gateway");
    txt(eth, CFG_NET_DNS, "Static DNS");
    txt(net, CFG_NETWORK_HOSTNAME, "Host Name");
    if (has("Network Password")) net->set_string(CFG_NETWORK_PASSWORD, cfg_text(c, "Network Password"));   // FW: an empty password is a value too (clearing it must reach the store)
    txt(net, CFG_NETWORK_UNIQUE_ID, "Unique ID");
    txt(net, CFG_NETWORK_REMOTE_SYSLOG_SERVER, "Log to Syslog Server");
    val(net, CFG_NETWORK_ULTIMATE_IDENT_SERVICE, "Ultimate Ident Service", 1);
    val(net, CFG_NETWORK_ULTIMATE_DMA_SERVICE, "Ultimate DMA Service", 1);
    val(net, CFG_NETWORK_FTP_SERVICE, "FTP File Service", 1);
    val(net, CFG_NETWORK_HTTP_SERVICE, "Web Remote Control Service", 1);
    val(net, CFG_NETWORK_NTP_EN, "SNTP Enable", 1);
    val(net, CFG_NETWORK_NTP_ZONE, "TimeZone", 16);
    txt(net, CFG_NETWORK_NTP_SRV1, "Time Server 1");
    txt(net, CFG_NETWORK_NTP_SRV2, "Time Server 2");
    txt(net, CFG_NETWORK_NTP_SRV3, "Time Server 3");
    eth->effectuate();
    net->effectuate();
    netsvc_apply(c);
    return OPE_OK;
}

// ---- states: the interface ---------------------------------------------------------------------------------------------------------------------------------
static char sb[2][64];
static NetworkInterface *wired(void)
{
    for (int i = 0; i < NetworkInterface::getNumberOfInterfaces(); i++) {
        NetworkInterface *ni = NetworkInterface::getInterface(i);
        if (ni && !ni->is_wireless()) {
            return ni;
        }
    }
    return 0;
}
static const char *st_eth_status(CoreCtx *)
{
    NetworkInterface *n = wired();
    return !n ? "No interface" : (n->is_link_up() ? "Link up" : "No link");
}
static const char *st_eth_ip(CoreCtx *)
{
    NetworkInterface *n = wired();
    if (!n || !n->is_link_up()) {
        return "-";
    }
    n->getIpAddrString(sb[0], sizeof(sb[0]));
    return sb[0];
}
static const char *st_eth_mac(CoreCtx *)
{
    NetworkInterface *n = wired();
    if (!n) {
        return "-";
    }
    uint8_t m[6];
    n->getMacAddr(m);
    snprintf(sb[1], sizeof(sb[1]), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return sb[1];
}

// ---- Wi-Fi (T184 D3): the host sets it; the core shows its state, network and address, and switches the radio on and off (live, not stored) -------------------------------
static char g_wifi_ip[24];
static const char *wifi_text(void)
{
    WifiNetStatus w;
    wifi_net_status(&w);
    snprintf(g_wifi_ip, sizeof(g_wifi_ip), "%s", w.ip);
    return wifi_state_word(w);
}
static const char *st_wifi(CoreCtx *) { return wifi_text(); }
static const char *st_wifi_ssid(CoreCtx *)
{
    static WifiNetStatus w;
    wifi_net_status(&w);
    return w.ssid[0] ? w.ssid : "(none)";
}
static const char *st_wifi_ip(CoreCtx *)
{
    wifi_text();
    return g_wifi_ip[0] ? g_wifi_ip : "(none)";
}
// The WiFi row of F8: On / Off / No module.
static const char *st_wifi_onoff(CoreCtx *)
{
    WifiNetStatus w;
    wifi_net_status(&w);
    return w.state == WN_NO_MODULE ? "No module" : (w.state == WN_DISABLED || w.state == WN_OFF) ? "Off" : "On";
}
// The one line under the row: state, network, address.
static const char *st_wifi_line(CoreCtx *)
{
    static char l[96];
    WifiNetStatus w;
    wifi_net_status(&w);
    snprintf(l, sizeof(l), "%s, %s, %s", wifi_state_word(w), w.ssid[0] ? w.ssid : "no network", w.ip[0] ? w.ip : "no address");
    return l;
}
static int op_wifi_toggle(CoreCtx *, const OpArgs &, OpOut &o)
{
    WifiNetStatus w;
    wifi_net_status(&w);
    bool on = w.state == WN_DISABLED || w.state == WN_OFF;           // it is off: switch it on
    if (!wifi_net_radio(on)) {
        snprintf(o.msg, o.msglen, "There is no WiFi module");
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, on ? "WiFi is on" : "WiFi is off");
    return OPE_OK;
}

// ---- the data streams: VIC, audio and the debug bus as UDP from the hardware (Gideon's DataStreamer, vendor/gideon/software/io/network/data_streamer.cc: the header of the
// packet is built here and written to U64_UDP_BASE, the stream is enabled in U64_ETHSTREAM_ENA) -------------------------------------------------------------------
struct Stream { uint32_t dest_ip; int dest_port; uint8_t dest_mac[6]; bool on; };
static Stream g_stream[3];
static uint32_t g_my_ip;
static uint8_t g_my_mac[6];
static const uint8_t g_modebytes[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x09, 0x0B, 0x00 };

static void stream_headers(CoreCtx *c, int id)
{
    uint8_t header[42] = { 0x01, 0x00, 0x5E, 0x00, 0x01, 0x40, 0x00, 0x15, 0x41, 0xAA, 0xAA, 0x01, 0x08, 0x00, 0x45, 0x00, 0x00, 0x1C, 0xBB, 0xBB, 0x40, 0x00, 0x40, 0x11,
                           0x00, 0x00, 0xEF, 0x00, 0x01, 0x40, 0xC0, 0xA8, 0x01, 0x40, 0xD0, 0x00, 0x2A, 0xF8, 0x00, 0x08, 0x00, 0x00 };
    Stream &s = g_stream[id];
    if (!g_my_ip || !s.on) {
        U64_ETHSTREAM_ENA &= (uint8_t)~(1 << id);
        return;
    }
    volatile uint8_t *hw = (volatile uint8_t *)U64_UDP_BASE + 64 * id;
    static const uint8_t sport[3][2] = { { 0xD0, 0x00 }, { 0xD4, 0x00 }, { 0x19, 0x6E } };
    header[34] = sport[id][0];
    header[35] = sport[id][1];
    memcpy(header + 6, g_my_mac, 6);
    for (int i = 0; i < 4; i++) {
        header[29 - i] = (uint8_t)(g_my_ip >> (8 * (3 - i)));
        header[33 - i] = (uint8_t)(s.dest_ip >> (8 * (3 - i)));
    }
    if ((s.dest_ip & 0xF8) == 0xE8) {
        header[3] = header[31] & 0x7F;
        header[4] = header[32];
        header[5] = header[33];
    } else if (s.dest_ip == 0xFFFFFFFF) {
        memset(header, 0xFF, 6);
    } else {
        memcpy(header, s.dest_mac, 6);
    }
    header[36] = (uint8_t)(s.dest_port >> 8);
    header[37] = (uint8_t)(s.dest_port & 0xFF);
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += ((uint32_t)header[2 * i + 14] << 8) | header[2 * i + 15];
    }
    while (sum & 0xFFFF0000) {
        sum = (sum >> 16) + (sum & 0xFFFF);
    }
    header[24] = 0xFF ^ (uint8_t)(sum >> 8);
    header[25] = 0xFF ^ (uint8_t)sum;
    for (int i = 0; i < 42; i++) {
        hw[i] = header[i];
    }
    if (id == 2) {                                              // the debug stream: which bus
        U64_ETHSTREAM_ENA &= 0x0F;
        U64_ETHSTREAM_ENA |= (uint8_t)(g_modebytes[cfg_value(c, "Debug Stream Mode", 0) & 7] << 4);
    }
    U64_ETHSTREAM_ENA |= (uint8_t)(1 << id);
}

// `dest` is the destination the caller names (FW data_streamer.cc:129-131: a start that does not come from the menu takes it from the path, up to 36 characters); empty or null: the setting.
static int stream_start(CoreCtx *c, int id, const char *dest, OpOut &o)
{
    static const char *const keys[3] = { "Stream VIC to", "Stream Audio to", "Stream Debug to" };
    NetworkInterface *ni = wired();
    if (!ni || !ni->is_link_up()) {
        snprintf(o.msg, o.msglen, "No network link");
        return OPE_FAILED;
    }
    union { uint32_t w[3]; uint8_t b[12]; } ip;
    ni->getIpAddr(ip.b);
    ni->getMacAddr(g_my_mac);
    g_my_ip = ip.w[0];
    if (!g_my_ip) {
        snprintf(o.msg, o.msglen, "No valid IP address yet");
        return OPE_FAILED;
    }
    char host[40];
    snprintf(host, sizeof(host), "%.36s", dest && dest[0] ? dest : cfg_text(c, keys[id]));
    char *colon = strchr(host, ':');
    int port = 11000 + id;
    if (colon) {
        *colon = 0;
        port = atoi(colon + 1);
    }
    if (!host[0] || !port) {
        snprintf(o.msg, o.msglen, "Set \"%s\" first (host:port)", keys[id]);
        return OPE_FAILED;
    }
    struct hostent he, *rh = 0;
    char buf[128];
    int err = 0;
    gethostbyname_r(host, &he, buf, sizeof(buf), &rh, &err);
    if (!rh) {
        snprintf(o.msg, o.msglen, "The host %s could not be resolved", host);
        return OPE_FAILED;
    }
    Stream &s = g_stream[id];
    s.dest_ip = ((uint32_t *)*(rh->h_addr_list))[0];
    s.dest_port = port;
    uint32_t query = s.dest_ip;
    if ((s.dest_ip & ip.w[1]) != (ip.w[2] & ip.w[1])) {
        query = ip.w[2];                                        // outside the subnet: the router's MAC
    }
    if ((s.dest_ip & 0xF8) != 0xE8 && s.dest_ip != 0xFFFFFFFF) {
        bool ok = ni->peekArpTable(query, s.dest_mac);
        int sock = ok ? -1 : socket(AF_INET, SOCK_DGRAM, 0);
        for (int i = 0; !ok && sock >= 0 && i < 25; i++) {
            struct sockaddr_in sv;
            memset(&sv, 0, sizeof(sv));
            sv.sin_family = AF_INET;
            sv.sin_addr.s_addr = query;
            sv.sin_port = htons((uint16_t)port);
            uint8_t z[2] = { 0, 0 };
            sendto(sock, z, 2, 0, (const struct sockaddr *)&sv, sizeof(sv));
            vTaskDelay(pdMS_TO_TICKS(100));
            ok = ni->peekArpTable(query, s.dest_mac);
        }
        if (sock >= 0) {
            lwip_close(sock);
        }
        if (!ok) {
            snprintf(o.msg, o.msglen, "The MAC of %s could not be found", host);
            return OPE_FAILED;
        }
    }
    s.on = true;
    stream_headers(c, id);
    snprintf(o.msg, o.msglen, "Streaming to %s:%d", host, port);
    return OPE_OK;
}

static uint32_t g_stream_gen[3];                                // a start or a stop calls off the stop-after of the one before

// FW data_streamer.cc:228-241: a start with a buffer size stops the stream after that many ticks (xTimerChangePeriod); here a task that sleeps them.
struct StopAfter { CoreCtx *c; int id; uint32_t gen; uint32_t ticks; };
static void stop_after_task(void *p)
{
    StopAfter a = *(StopAfter *)p;
    free(p);
    vTaskDelay(a.ticks);
    if (g_stream_gen[a.id] == a.gen && g_stream[a.id].on) {
        g_stream[a.id].on = false;
        stream_headers(a.c, a.id);
    }
    vTaskDelete(NULL);
}

static int start_stream(CoreCtx *c, int id, const char *dest, uint32_t stop_after, OpOut &o)
{
    int rc = stream_start(c, id, dest, o);
    if (rc != OPE_OK) {
        return rc;
    }
    uint32_t gen = ++g_stream_gen[id];
    if (stop_after) {
        StopAfter *a = (StopAfter *)malloc(sizeof(StopAfter));
        if (a) {
            *a = StopAfter{ c, id, gen, stop_after };
            if (xTaskCreate(stop_after_task, "StreamStop", configMINIMAL_STACK_SIZE, a, PRIO_NETSERVICE, NULL) != pdPASS) {
                free(a);
            }
        }
    }
    return OPE_OK;
}
static int op_stream_start(CoreCtx *c, const OpArgs &a, OpOut &o) { return start_stream(c, a.i(0), 0, 0, o); }
static int op_stream_start_to(CoreCtx *c, const OpArgs &a, OpOut &o) { return start_stream(c, a.i(0), a.s(1), (uint32_t)a.i(2), o); }
static int op_stream_stop(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    g_stream_gen[a.i(0)]++;
    g_stream[a.i(0)].on = false;
    stream_headers(c, a.i(0));
    snprintf(o.msg, o.msglen, "Stream stopped");
    return OPE_OK;
}
static const char *st_stream0(CoreCtx *) { return g_stream[0].on ? "streaming" : "off"; }
static const char *st_stream1(CoreCtx *) { return g_stream[1].on ? "streaming" : "off"; }
static const char *st_stream2(CoreCtx *) { return g_stream[2].on ? "streaming" : "off"; }
static const char *st_stream0_on(CoreCtx *) { return g_stream[0].on ? "yes" : 0; }
static const char *st_stream1_on(CoreCtx *) { return g_stream[1].on ? "yes" : 0; }
static const char *st_stream2_on(CoreCtx *) { return g_stream[2].on ? "yes" : 0; }
static const char *st_stream0_off(CoreCtx *) { return g_stream[0].on ? 0 : "yes"; }
static const char *st_stream1_off(CoreCtx *) { return g_stream[1].on ? 0 : "yes"; }
static const char *st_stream2_off(CoreCtx *) { return g_stream[2].on ? 0 : "yes"; }

static const ArgSpec stream_arg[] = { ARG_INT("id", 0, 2) };
static const ArgSpec stream_to_args[] = { ARG_INT("id", 0, 2), ARG_STRING("dest", 36), ARG_INT("stop_after", 0, 65535) };
static const ArgSpec value_arg[] = { ARG_INT("value", -2000, 2000) };

static const char *st_net_static(CoreCtx *c) { return cfg_value(c, "Use DHCP", 1) ? 0 : "yes"; }
static const char *st_pw_scope(CoreCtx *) { return "FTP, web remote control (X-Password header), Ultimate DMA service (TCP 64: authenticate first)"; }   // what net_password guards in this application; ident only reports that

void netops_register(OpRegistry &r)
{
    r.contributor("net");
    r.add_op("net.fx", value_arg, 1, fx_net);
    r.add_op("wifi.toggle", 0, 0, op_wifi_toggle);
    r.add_state("net.eth_status", st_eth_status);
    r.add_state("net.eth_ip", st_eth_ip);
    r.add_state("net.eth_mac", st_eth_mac);
    r.add_state("wifi.state", st_wifi);
    r.add_state("wifi.ssid", st_wifi_ssid);
    r.add_state("wifi.ip", st_wifi_ip);
    r.add_state("wifi.onoff", st_wifi_onoff);
    r.add_state("wifi.line", st_wifi_line);
    r.add_state("net.static", st_net_static);
    r.add_state("net.pw_scope", st_pw_scope);
    r.add_op("stream.start", stream_arg, 1, op_stream_start);
    r.add_op("stream.stop", stream_arg, 1, op_stream_stop);
    r.add_op("stream.start_to", stream_to_args, 3, op_stream_start_to);       // the Ultimate DMA service's start commands (dmasvc.cc)
    r.add_state("stream.vic", st_stream0);
    r.add_state("stream.audio", st_stream1);
    r.add_state("stream.debug", st_stream2);
    r.add_state("stream.vic_on", st_stream0_on);
    r.add_state("stream.audio_on", st_stream1_on);
    r.add_state("stream.debug_on", st_stream2_on);
    r.add_state("stream.vic_off", st_stream0_off);
    r.add_state("stream.audio_off", st_stream1_off);
    r.add_state("stream.debug_off", st_stream2_off);
}
