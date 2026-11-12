// T184: see wifinet.h. Everything here goes through Gideon's wifi.cc / wifi_cmd.cc as his menu does (network_esp32.cc, wifi.cc connect_ap); there is no
// call into esp32.cc's Download / Flash (FLASH_EXTRA_PROTECTION leaves them out of the build).
#include "wifinet.h"
#include "wifi.h"
#include "wifi_cmd.h"
#include "network_interface.h"
#include "wifi_connection_feedback.h"
#include "itu.h"
#include "rpc_calls.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

extern ultimate_ap_records_t wifi_aps;     // wifi.cc

const char *const wifi_auth_names[WIFI_AUTH_N] = { "Open", "WEP", "WPA PSK", "WPA2 PSK", "WPA/WPA2 PSK", "WPA2 Enterprise", "WPA3 PSK", "WPA2/WPA3 PSK", "WAPI PSK" };

static NetworkInterface *wifi_if(void)
{
    for (int i = 0; i < NetworkInterface::getNumberOfInterfaces(); i++) {
        NetworkInterface *ni = NetworkInterface::getInterface(i);
        if (ni && ni->is_wireless()) {
            return ni;
        }
    }
    return NULL;
}

static volatile bool job_busy;
static volatile int job_last = 1;
static char job_why[96];

// SSIDs are the AP's bytes: what is shown has no control bytes (wifi.h BrowsableWifiAP does the same)
static void display_name(char *out, const char *ssid)
{
    int i = 0;
    for (; i < 32 && ssid[i]; i++) {
        unsigned char c = ssid[i];
        out[i] = (c < 32 || c == 127) ? '?' : c;
    }
    out[i] = 0;
}

static void first_line(char *out, int max, const char *text)
{
    int i = 0;
    for (; i < max - 1 && text[i] && text[i] != '\n'; i++) {
        out[i] = text[i];
    }
    out[i] = 0;
}

void wifi_net_status(WifiNetStatus *s)
{
    memset(s, 0, sizeof(*s));
    s->busy = job_busy;
    s->last = job_last;
    if (!job_busy && job_last != 0 && job_last != 1) {
        strncpy(s->why, job_why, sizeof(s->why) - 1);
    }
    WifiState_t ws = wifi.getState();
    if (ws == eWifi_Scanning && wifi.canTransmit()) {                  // a rescan keeps the link; it is still up
        ws = eWifi_Connected;
        s->scan = WS_RUNNING;
    } else if (ws == eWifi_Scanning) {
        s->scan = WS_RUNNING;
    } else if (!wifi.scanTried()) {
        s->scan = WS_NONE;
    } else if (wifi.scanStatus() != 0) {
        s->scan = WS_FAILED;
    } else {
        s->scan = wifi_aps.num_records ? WS_DONE : WS_EMPTY;
    }
    switch (ws) {
    case eWifi_Off:            s->state = WN_OFF;       s->text = "Off"; break;
    case eWifi_Download:       s->state = WN_STARTING;  s->text = "Busy"; break;
    case eWifi_NotDetected:    s->state = WN_NO_MODULE; s->text = "No module detected"; break;
    case eWifi_ModuleDetected: s->state = WN_STARTING;  s->text = "Module found, waiting for its firmware"; break;
    case eWifi_AppDetected:
    case eWifi_Scanning:       s->state = WN_SCANNING;  s->text = "Scanning"; break;
    case eWifi_NotConnected:   s->state = WN_LINK_DOWN; s->text = "Link down"; break;
    case eWifi_Connected:      s->state = WN_LINK_UP;   s->text = "Link up"; break;
    case eWifi_Disabled:       s->state = WN_DISABLED;  s->text = "Disabled"; break;
    default:                   s->state = WN_OFF;       s->text = "?"; break;
    }
    if (strcmp(wifi.getLastAP(), "-")) {
        display_name(s->ssid, wifi.getLastAP());
    }
    strncpy(s->module, wifi.getModuleName(), sizeof(s->module) - 1);
    uint8_t mac[8];
    wifi.getMacAddr(mac);
    snprintf(s->mac, sizeof(s->mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    NetworkInterface *ni = wifi_if();
    if (ni && ni->is_link_up() && wifi.hasUsableAddress()) {      // not an address cached from an earlier lease
        ni->getIpAddrString(s->ip, sizeof(s->ip));
        if (!strcmp(s->ip, "0.0.0.0")) {
            s->ip[0] = 0;
        }
    }
}

void wifi_net_rescan(void)
{
    WifiState_t st = wifi.getState();
    if (st == eWifi_NotConnected || st == eWifi_Connected) {      // as cfg_show_aps: only when active; the driver keeps the link (D4)
        wifi.sendEvent(EVENT_RESCAN);
    }
}

// The scan runs on the driver's task (wifi_scan(&wifi_aps)); it zeroes the count first and decodes only a reply that is complete (wifi_decode_scan).
// Reading it meanwhile is a plain copy of at most max records.
int wifi_net_aps(WifiNetAp *out, int max)
{
    int n = wifi_aps.num_records;
    if (n < 0 || n > DEFAULT_SCAN_LIST_SIZE) {
        n = 0;
    }
    if (n > max) {
        n = max;
    }
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        strncpy(out[i].ssid, (const char *)wifi_aps.aps[i].ssid, sizeof(out[i].ssid) - 1);
        display_name(out[i].name, out[i].ssid);
        out[i].rssi = wifi_aps.aps[i].rssi;
        out[i].auth = wifi_aps.aps[i].authmode < WIFI_AUTH_N ? wifi_aps.aps[i].authmode : WIFI_AUTH_N - 1;
    }
    return n;
}

struct Job {
    int kind;                    // 0 connect, 1 autoconnect, 2 disconnect, 3 forget
    char ssid[33];
    char pass[65];                // 64 characters and the terminator
    uint8_t auth;
    bool from_list;
};

// A connect request that the module accepted is not a connection: the link comes from the module's EVENT_CONNECTED (the driver brings it up), a failed
// attempt from its EVENT_DISCONNECTED with a reason. Association and the DHCP lease each get a bounded wait (as the Ultimate menu's connectWithFeedback).
#define JOB_ASSOC_WAIT_MS   30000
#define JOB_ADDRESS_WAIT_MS 30000

static int connect_and_wait(const char *ssid, const char *pass, uint8_t auth)
{
    WifiConnectionFeedback &fb = wifi.getConnectionFeedback();
    ENTER_SAFE_SECTION;
    fb.begin(ssid);
    LEAVE_SAFE_SECTION;
    int r = wifi_wifi_connect(ssid, pass, auth);
    if (r != 0) {
        first_line(job_why, sizeof(job_why), wifi_connection_request_failure(r));
    } else {
        int assoc = 0, addr = 0;
        for (;;) {
            WifiConnectionFeedback::Phase ph = fb.phase;
            if (ph == WifiConnectionFeedback::Failed) {
                first_line(job_why, sizeof(job_why), wifi_connection_failure(fb.reason));
                r = -1;
                break;
            }
            if (ph == WifiConnectionFeedback::WaitingForIP) {
                if (wifi.getState() == eWifi_Connected && wifi.hasUsableAddress()) {
                    r = 0;
                    break;
                }
                if ((addr += 100) > JOB_ADDRESS_WAIT_MS) {
                    snprintf(job_why, sizeof(job_why), "Connected, but no IP yet");
                    r = -2;
                    break;
                }
            } else if ((assoc += 100) > JOB_ASSOC_WAIT_MS) {
                snprintf(job_why, sizeof(job_why), "Connection timed out");
                r = -3;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    ENTER_SAFE_SECTION;
    fb.finish();
    LEAVE_SAFE_SECTION;
    return r;
}

static void job_task(void *p)
{
    Job *j = (Job *)p;
    int r = 0;
    job_why[0] = 0;
    switch (j->kind) {
    case 0: r = connect_and_wait(j->ssid, j->pass, j->auth); break;
    case 1: r = wifi_wifi_autoconnect(); break;
    case 2: r = wifi_wifi_disconnect(); break;
    case 3: r = wifi_forget_aps(); break;
    }
    if (r != 0 && !job_why[0]) {
        first_line(job_why, sizeof(job_why), wifi_connection_request_failure(r));
    }
    job_last = r;
    job_busy = false;
    memset(j, 0, sizeof(*j));
    vPortFree(j);
    vTaskDelete(NULL);
}

static bool start_job(int kind, const char *ssid, const char *pass, uint8_t auth, bool from_list)
{
    if (job_busy) {
        return false;
    }
    Job *j = (Job *)pvPortMalloc(sizeof(Job));
    if (!j) {
        return false;
    }
    memset(j, 0, sizeof(*j));
    j->kind = kind;
    j->auth = auth;
    j->from_list = from_list;
    if (ssid) {
        strncpy(j->ssid, ssid, sizeof(j->ssid) - 1);
    }
    if (pass) {
        strncpy(j->pass, pass, sizeof(j->pass) - 1);
    }
    job_why[0] = 0;
    job_busy = true;
    job_last = 1;
    if (xTaskCreate(job_task, "wifi job", 2 * configMINIMAL_STACK_SIZE, j, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        job_busy = false;
        vPortFree(j);
        return false;
    }
    return true;
}

bool wifi_net_connect(const char *ssid, const char *password, uint8_t auth, bool from_list) { return start_job(0, ssid, password, auth, from_list); }
bool wifi_net_autoconnect(void) { return start_job(1, NULL, NULL, 0, false); }
bool wifi_net_disconnect(void) { return start_job(2, NULL, NULL, 0, false); }
bool wifi_net_forget(void) { return start_job(3, NULL, NULL, 0, false); }

// T184 D3: the core application's WiFi row. The radio of the module on or off, live; the driver's state decides (Disabled is the module's event). Nothing is stored:
// the next start follows the host's setting.
bool wifi_net_radio(bool on)
{
    if (wifi.getState() == eWifi_NotDetected) {
        return false;
    }
    if (on) {
        wifi.RadioOn();
    } else {
        wifi.RadioOff();
    }
    return true;
}

// ---- the settings ----
#include "config.h"

static ConfigStore *wifi_store(void)
{
    return ConfigManager::getConfigManager()->find_store((uint32_t)0x57494649);
}

bool wifi_net_cfg_get(WifiNetCfg *c)
{
    memset(c, 0, sizeof(*c));
    ConfigStore *s = wifi_store();
    if (!s) {
        c->dhcp = true;
        return false;
    }
    c->dhcp = s->get_value(CFG_NET_DHCP_EN) != 0;
    strncpy(c->ip, s->get_string(CFG_NET_IP), sizeof(c->ip) - 1);
    strncpy(c->netmask, s->get_string(CFG_NET_NETMASK), sizeof(c->netmask) - 1);
    strncpy(c->gateway, s->get_string(CFG_NET_GATEWAY), sizeof(c->gateway) - 1);
    strncpy(c->dns, s->get_string(CFG_NET_DNS), sizeof(c->dns) - 1);
    return true;
}

bool wifi_net_cfg_set(const WifiNetCfg *c)
{
    ConfigStore *s = wifi_store();
    if (!s) {
        return false;
    }
    s->set_value(CFG_NET_DHCP_EN, c->dhcp ? 1 : 0);
    s->set_string(CFG_NET_IP, c->ip);
    s->set_string(CFG_NET_NETMASK, c->netmask);
    s->set_string(CFG_NET_GATEWAY, c->gateway);
    s->set_string(CFG_NET_DNS, c->dns);
    s->write();
    s->effectuate();
    return true;
}
