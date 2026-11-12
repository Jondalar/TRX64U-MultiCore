// The WiFi page's actions and modals as the core runs them (T184 D3, T192.2 P2, T192 D23): the radio, DHCP and the static addresses, the access point list, the
// manual entry, the last access point, disconnect and forget. The two modals are DATA (HcWifiPick: a pick list; HcWifiEntry: a text field with its cursor), drawn by the
// screen through HcWifiEnv and told to the service there. The texts, the keys and the timings are the old hostui.cc's. The screen keeps the page's rows, the panels
// and the arm of "forget" (T192 D21: the core returns what happened). No LVGL, no FreeRTOS, no key wait of its own.
#ifndef HOST_HOSTCORE_FLOW_WIFI_H
#define HOST_HOSTCORE_FLOW_WIFI_H

#include <stdint.h>
#include "host_core_flow.h"
#include "wifinet.h"
#include "lineedit.h"

enum { NS_RADIO, NS_DHCP, NS_IP, NS_MASK, NS_GW, NS_DNS, NS_N };   // the rows of the WiFi page; NS_RADIO: the module's radio On/Off (T184 D3 amended), not stored

#define HCW_PICK_MAX 32                                            // the scan list holds this many access points

struct HcWifiItem {                                                // one row: SSID | authentication | signal; the choice list has the first only
    char a[40], b[56], c[16];
    uint8_t ctone;                                                 // HcTone of c
};

struct HcWifiPick {                                                // the pick list (access points, authentication)
    char title[72];                                                // the config overlay: a 47-character entry name + " - start with"
    char sub[96];
    uint8_t subtone;
    HcWifiItem item[HCW_PICK_MAX];
    int n, sel, first;
    char msg[96];
    uint8_t msgtone;
    const char *keys;
    bool cfgov;                                                    // T198: the config overlay (two-line rows, dimmed page, keys in the screen's key bar)
};

struct HcWifiEntry {                                               // the text-entry modal
    char title[96];
    char sub[140];
    char keys[124];
    char shown[LE_MAX + 1];                                        // what the service is told: the text, or stars for a secret
    char row[LE_MAX + 4];                                          // what the field shows: the typed text with its cursor ('_'), masked for a secret
    char msg[96];
    bool secret;
};

// The screen's side. Every function gets `ctx`; tones are HcTone.
struct HcWifiEnv {
    const HcFlowEnv *base;
    WifiNetStatus *st;                                             // the screen's copy of the module's state (the core refreshes it)
    WifiNetCfg *cfg;                                               // ... and of the WiFi store
    const bool *cfg_ok;
    const char *(*set_name)(void *ctx, int row);                   // the name of a row of the page
    void (*note)(void *ctx, const char *text, int tone);           // the page's note line
    bool (*panels)(void *ctx);                                     // the modal panels exist
    int (*pick_rows)(void *ctx);                                   // the rows the pick list holds
    int (*entry_vis)(void *ctx);                                   // the characters the entry field shows
    bool (*list_up)(void *ctx);                                    // the pick list is on screen
    void (*draw_pick)(void *ctx, const HcWifiPick &p, bool fresh); // draws it (fresh: the panel opens) and tells the service
    void (*pick_end)(void *ctx);                                   // the pick list is gone
    bool (*entry_open)(void *ctx);                                 // the entry stands alone: the list goes; returns whether it was up
    void (*draw_entry)(void *ctx, const HcWifiEntry &e, bool first);
    void (*entry_end)(void *ctx, bool list_was_up);                // the entry is gone, the list comes back
    void (*history)(void *ctx, const char *text);                  // a typed text for the service's history
    bool (*arm_yes)(void *ctx);                                    // the key was the board's own, confirming
    void *ctx;
};

enum { HCW_NONE = 0, HCW_FORGET_ARM, HCW_JOB };

// The keys of the page from the radio on (the screen has moved the cursor; `armed` is its arm of "forget"). HCW_FORGET_ARM: the first F, the screen arms.
// The modal actions (the access point list, the manual entry, a typed static address) are jobs (host_core_job.h): with `job` given the call returns HCW_JOB and the job (the caller
// steps it and deletes it); with `job` null it runs the modal to its end (the old blocking call).
class HcJob;
int hc_wifi_key(const HcWifiEnv &env, int act, int nsel, int rawk, bool armed, HcJob **job = 0);
// The service's confirmation of "forget": false with the reason in `why`.
bool hc_wifi_forget_confirm(const HcWifiEnv &env, char *why, int n);
// A static address typed through the API (field_set): 0 done, 1 refused with `why`.
int hc_wifi_set_addr(const HcWifiEnv &env, int row, const char *value, char *why, int n);

const char *hc_wifi_radio_text(const WifiNetStatus &w);            // "No module", "Off", "On"
const char *hc_wifi_unavailable(const WifiNetStatus &w);           // why a module action cannot run (null: it can)
bool hc_wifi_ipv4_ok(const char *s);
const char *hc_wifi_check_ipv4(const char *t);                     // null: fine; else the message

// What the last action was ("Connecting to Home"): the driver only says busy and ok / not ok.
bool hc_wifi_op_run(void);
const char *hc_wifi_op_text(void);
void hc_wifi_op_done(void);
const char *hc_wifi_last_sub(void);                                // what the list said under its title (the bench reads it)

// The data of the screens for the bench's pictures (the modal loops being out of reach of the bench).
void hc_wifi_aps_pick(HcWifiPick *p, const WifiNetAp *aps, int n, int sel, int first, const WifiNetStatus &st, const char *msg, int msgtone);
void hc_wifi_auth_pick(HcWifiPick *p, const char *title, const char *sub, int sel, int first);
void hc_wifi_entry_make(HcWifiEntry *o, const char *title, const char *sub, const char *keys, const LineEdit &e, bool secret, const char *msg, int vis);

#endif
