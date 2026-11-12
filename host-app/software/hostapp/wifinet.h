// T184: the host app's and the core app's side of Gideon's WiFi driver (io/wifi/wifi.cc, io/network/network_esp32.cc). The driver and the lwIP interface
// are his, 1:1 (the lists of the Ultimate menu are not: no tree browser here); this is what a page needs from them. Nothing here flashes the ESP32.
#ifndef HOST_WIFINET_H
#define HOST_WIFINET_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum WifiNetState {
    WN_OFF, WN_NO_MODULE, WN_STARTING, WN_SCANNING, WN_LINK_DOWN, WN_LINK_UP, WN_DISABLED
};

enum WifiNetScan {                 // T184 D4: what the last scan came to
    WS_NONE,                       // none yet
    WS_RUNNING,
    WS_FAILED,                     // no valid reply from the module (the list is empty)
    WS_EMPTY,                      // a valid reply with no access point
    WS_DONE
};

struct WifiNetStatus {
    WifiNetState state;
    const char *text;           // "Link up", "Link down", ... (static)
    char ssid[34];              // the access point the module is on ("" when not connected); control bytes shown as '?'
    char ip[20];                // the interface's address ("" until it has a usable one: with DHCP, one the server gave)
    WifiNetScan scan;
    char why[96];               // one line: why the last connect failed ("" when it did not)
    char mac[20];
    char module[34];            // what the module reported (CMD_IDENTIFY)
    bool busy;                  // a connect / disconnect / forget is under way
    int last;                   // result of the last of those: 0 ok, <0 error, 1 none yet
};

// T184 R6: the one name of a state, for every screen of both apps: "No module", "Off", otherwise the driver's own words (Link up, Link down, Scanning, ...).
static inline const char *wifi_state_word(const WifiNetStatus &w)
{
    return w.state == WN_NO_MODULE ? "No module" : (w.state == WN_DISABLED || w.state == WN_OFF) ? "Off" : w.text ? w.text : "";
}

// T184 D5: the top-bar network chip names the link with its address, "LAN 192.168.x.x" or "WiFi 192.168.x.x"; both up: LAN. A usable address is a non-empty
// one other than 0.0.0.0 (the cable's own; the WiFi's comes from WifiNetStatus.ip, which the driver fills only once it is usable). Returns whether there is one;
// without, `none` is the text. One function for the host's header and the core application's Network line.
static inline bool net_address_usable(const char *ip)
{
    return ip && ip[0] && strcmp(ip, "0.0.0.0");
}
// The chip text when neither link has a usable address: one word for the host and every core (T184 D5, UX review 2026-11-07).
#define NET_NONE_TEXT "no network"
static inline bool net_chip_text(char *out, int n, const char *lan_ip, const char *wifi_ip, const char *none)
{
    if (net_address_usable(lan_ip)) {
        snprintf(out, n, "LAN %s", lan_ip);
        return true;
    }
    if (net_address_usable(wifi_ip)) {
        snprintf(out, n, "WiFi %s", wifi_ip);
        return true;
    }
    snprintf(out, n, "%s", none);
    return false;
}

struct WifiNetAp {
    char ssid[33];              // the bytes the module sent (what a connect needs)
    char name[33];              // the same for display: control bytes shown as '?'
    int8_t rssi;
    uint8_t auth;               // index into wifi_auth_names
};

#define WIFI_AUTH_N 9
extern const char *const wifi_auth_names[WIFI_AUTH_N];   // the order of network_esp32.cc authmodes

void wifi_net_status(WifiNetStatus *s);
void wifi_net_rescan(void);                          // asks the driver's task to scan; the list fills when it is done
int  wifi_net_aps(WifiNetAp *out, int max);          // copy of the last scan; 0 while scanning
// The next four run on a task of their own (they wait for the module for seconds); wifi_net_status().busy / .last tell. false = one is under way already.
bool wifi_net_connect(const char *ssid, const char *password, uint8_t auth, bool from_list);   // from_list: unused since D4 (no fake connected event)
bool wifi_net_autoconnect(void);                     // "Connect to last AP"
bool wifi_net_disconnect(void);
bool wifi_net_forget(void);                          // "Forget APs": the access points the module stored

// The module's radio on or off at once (the core application's WiFi row, T184 D3); false: there is no module. Not stored. The state follows from wifi_net_status().
bool wifi_net_radio(bool on);

// Gideon's "WiFi settings" store (id 0x57494649; with U64 == 2 it has no "WiFi Enabled"): Use DHCP, Static IP / Netmask / Gateway / DNS. The host app keeps it in its
// settings file (T163). set writes the store, saves it and applies it to the interface; false when the store is not there.
struct WifiNetCfg {
    bool dhcp;
    char ip[17], netmask[17], gateway[17], dns[17];
};
bool wifi_net_cfg_get(WifiNetCfg *c);
bool wifi_net_cfg_set(const WifiNetCfg *c);

#endif
