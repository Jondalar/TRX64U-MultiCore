// T192.2-S10 (the part that needs no radio): the WiFi page's modal actions as jobs (host_core_flow_wifi.cc: hc_wifi_key). The access point list, the manual entry, the
// authentication list, the password and the static address fields are run by two drivers against a rig with no screen and a module model:
//   - blocking: hc_wifi_key, the old call, the keys pushed into the environment's key queue;
//   - stepped: hc_wifi_key with a job out, `step(input)` by hand, the keys given as the step's input.
// Both record what the screen would be told (every modal page as data, the notes, the history) and what the module was asked (connect with its arguments, the
// stored settings, ...); the record of every scenario equals the golden file (tests/hostapp/golden/hostwifi.txt, captured from the flows before they were cut into
// steps). The radio itself (the module's answers, the driver's task) is a board step.
//   test_hostwifi <golden file> [--write-golden]
#include "host_core_flow_wifi.h"
#include "host_core_job.h"
#include "host_core.h"
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <signal.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// ---- the module model ---------------------------------------------------------------------------------------------------------------------------------
static std::vector<std::string> g_log;                      // the record of the run in progress
static WifiNetStatus g_st;
static WifiNetCfg g_cfg;
static bool g_cfg_ok = true;
static WifiNetAp g_aps[4];
static int g_naps = 3;
static int g_rescans, g_ap_calls;
const char *const wifi_auth_names[WIFI_AUTH_N] = { "Open", "WEP", "WPA PSK", "WPA2 PSK", "WPA/WPA2 PSK", "WPA2 Enterprise", "WPA3 PSK", "WPA2/WPA3 PSK", "WAPI PSK" };

static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
    char b[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    g_log.push_back(b);
}

void wifi_net_status(WifiNetStatus *s) { *s = g_st; }
void wifi_net_rescan(void)
{
    g_rescans++;
    g_ap_calls = 0;
    L("module|rescan");
}
int wifi_net_aps(WifiNetAp *out, int max)
{
    g_ap_calls++;
    if (g_rescans > 1 && g_ap_calls < 3) {
        return 0;                                            // a second scan (R) is under way; the first one found the three at once
    }
    int n = g_naps < max ? g_naps : max;
    for (int i = 0; i < n; i++) out[i] = g_aps[i];
    return n;
}
bool wifi_net_connect(const char *ssid, const char *password, uint8_t auth, bool from_list)
{
    L("module|connect|ssid=%s|pass=%s|auth=%d|list=%d", ssid, password, (int)auth, (int)from_list);
    return true;
}
bool wifi_net_autoconnect(void) { L("module|autoconnect"); return true; }
bool wifi_net_disconnect(void) { L("module|disconnect"); return true; }
bool wifi_net_forget(void) { L("module|forget"); return true; }
bool wifi_net_radio(bool on) { L("module|radio|%d", (int)on); return g_st.state != WN_NO_MODULE; }
bool wifi_net_cfg_get(WifiNetCfg *c) { *c = g_cfg; return g_cfg_ok; }
bool wifi_net_cfg_set(const WifiNetCfg *c)
{
    L("module|cfg_set|dhcp=%d|ip=%s|mask=%s|gw=%s|dns=%s", (int)c->dhcp, c->ip, c->netmask, c->gateway, c->dns);
    g_cfg = *c;
    return true;
}

// ---- the rig ----------------------------------------------------------------------------------------------------------------------------------------------
struct Rig {
    std::vector<int> script;                      // the keys the person presses, one per page the screen is told of
    size_t at;
    std::vector<int> keys;                        // the environment's key queue (the blocking driver)
    int pending;                                  // the next step's key (the stepped driver)
    bool blocking;
    uint32_t now;
    bool list_up;
};
static Rig R;

static const char *tone_name(int t)
{
    static const char *const n[] = { "-", "ink", "muted", "ok", "warn", "bad", "host" };
    return t >= 0 && t < 7 ? n[t] : "?";
}

static void feed(void)
{
    if (R.at < R.script.size()) {
        int k = R.script[R.at++];
        R.now += 120;                                        // a person presses a key every 120 ms: the opening key's repeat window (300 ms) is over by the third key
        if (R.blocking) R.keys.push_back(k);
        else R.pending = k;
    }
}

static void r_draw(void *, const HcPage &, bool) { }
static int g_fallbacks;                                      // ESC pressed for the person when the script ran out (a scenario that did not end by itself)
static int r_getch_nowait(void *)
{
    if (R.keys.empty()) {
        if (R.at >= R.script.size() && R.pending < 0) {
            g_fallbacks++;
            return KEY_ESCAPE;
        }
        return -1;
    }
    int k = R.keys.front();
    R.keys.erase(R.keys.begin());
    if (getenv("HW_TRACE")) fprintf(stderr, "key %d\n", k);
    return k;
}
static int r_getch(void *) { return r_getch_nowait(0); }
static void r_idle(void *) { }
static void r_sleep(void *, int ms) { R.now += ms; }
static uint32_t r_now(void *) { return R.now; }
static void r_note_base(void *, const char *, int) { }

static const char *e_set_name(void *, int row)
{
    static const char *const n[NS_N] = { "WiFi", "Use DHCP", "Static IP", "Static Netmask", "Static Gateway", "Static DNS" };
    return n[row];
}
static void e_note(void *, const char *text, int tone) { L("note|%s|%s", tone_name(tone), text); }
static bool e_panels(void *) { return true; }
static int e_pick_rows(void *) { return 10; }
static int e_entry_vis(void *) { return 30; }
static bool e_list_up(void *) { return R.list_up; }
static void e_draw_pick(void *, const HcWifiPick &p, bool fresh)
{
    std::string items;
    for (int i = 0; i < p.n; i++) {
        char b[160];
        snprintf(b, sizeof(b), "[%s;%s;%s;%s]", p.item[i].a, p.item[i].b, p.item[i].c, tone_name(p.item[i].ctone));
        items += b;
    }
    L("pick|%s|%s|%s|n=%d sel=%d first=%d|msg=%s:%s|keys=%s|fresh=%d|%s", p.title, p.sub, tone_name(p.subtone), p.n, p.sel, p.first, tone_name(p.msgtone), p.msg, p.keys, (int)fresh,
      items.c_str());
    R.list_up = true;
    feed();
}
static void e_pick_end(void *)
{
    L("pick_end");
    R.list_up = false;
}
static bool e_entry_open(void *)
{
    bool was = R.list_up;
    R.list_up = false;
    return was;
}
static void e_draw_entry(void *, const HcWifiEntry &e, bool first)
{
    L("entry|%s|%s|keys=%s|shown=%s|row=%s|msg=%s|secret=%d|first=%d", e.title, e.sub, e.keys, e.shown, e.row, e.msg, (int)e.secret, (int)first);
    feed();
}
static void e_entry_end(void *, bool list_was_up)
{
    L("entry_end|list_was_up=%d", (int)list_was_up);
    R.list_up = list_was_up;
}
static void e_history(void *, const char *text) { L("history|%s", text); }
static bool e_arm_yes(void *) { return true; }

static HcFlowEnv base_env()
{
    HcFlowEnv b;
    memset(&b, 0, sizeof(b));
    b.draw = r_draw;
    b.getch_nowait = r_getch_nowait;
    b.getch = r_getch;
    b.idle = r_idle;
    b.sleep_ms = r_sleep;
    b.note = r_note_base;
    b.now = r_now;
    return b;
}

static HcFlowEnv g_base;
static HcWifiEnv make_env()
{
    g_base = base_env();
    HcWifiEnv e;
    memset(&e, 0, sizeof(e));
    e.base = &g_base;
    e.st = &g_st;
    e.cfg = &g_cfg;
    e.cfg_ok = &g_cfg_ok;
    e.set_name = e_set_name;
    e.note = e_note;
    e.panels = e_panels;
    e.pick_rows = e_pick_rows;
    e.entry_vis = e_entry_vis;
    e.list_up = e_list_up;
    e.draw_pick = e_draw_pick;
    e.pick_end = e_pick_end;
    e.entry_open = e_entry_open;
    e.draw_entry = e_draw_entry;
    e.entry_end = e_entry_end;
    e.history = e_history;
    e.arm_yes = e_arm_yes;
    return e;
}

// ---- the scenarios ---------------------------------------------------------------------------------------------------------------------------------------
struct Scn {
    const char *name;
    int act;
    int nsel;
    int rawk;
    bool armed;
    std::vector<int> script;
    int auth_of_first;                            // WPA2 for the access points by default
};

static void reset_world()
{
    memset(&g_st, 0, sizeof(g_st));
    g_st.state = WN_LINK_UP;
    g_st.text = "Link up";
    snprintf(g_st.ssid, sizeof(g_st.ssid), "Home WLAN");
    snprintf(g_st.ip, sizeof(g_st.ip), "192.168.2.20");
    g_st.scan = WS_DONE;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.dhcp = false;
    snprintf(g_cfg.ip, sizeof(g_cfg.ip), "192.168.2.64");
    snprintf(g_cfg.netmask, sizeof(g_cfg.netmask), "255.255.255.0");
    snprintf(g_cfg.gateway, sizeof(g_cfg.gateway), "192.168.2.1");
    snprintf(g_cfg.dns, sizeof(g_cfg.dns), "8.8.8.8");
    g_cfg_ok = true;
    memset(g_aps, 0, sizeof(g_aps));
    snprintf(g_aps[0].ssid, sizeof(g_aps[0].ssid), "Home WLAN");   snprintf(g_aps[0].name, sizeof(g_aps[0].name), "Home WLAN");   g_aps[0].rssi = -48; g_aps[0].auth = 3;
    snprintf(g_aps[1].ssid, sizeof(g_aps[1].ssid), "Cafe Lumiere"); snprintf(g_aps[1].name, sizeof(g_aps[1].name), "Cafe Lumiere"); g_aps[1].rssi = -71; g_aps[1].auth = 0;
    snprintf(g_aps[2].ssid, sizeof(g_aps[2].ssid), "Neighbour");    snprintf(g_aps[2].name, sizeof(g_aps[2].name), "Neighbour");    g_aps[2].rssi = -83; g_aps[2].auth = 2;
    g_naps = 3;
    g_rescans = 0;
    g_ap_calls = 0;
}

static std::vector<Scn> scenarios()
{
    std::vector<Scn> v;
    auto typed = [](const char *s) { std::vector<int> k; for (; *s; s++) k.push_back((unsigned char)*s); return k; };
    auto cat = [](std::vector<int> a, std::vector<int> b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    const int RET = KEY_RETURN, ESC = KEY_ESCAPE, DN = KEY_DOWN, UP = KEY_UP;
    // the access point list
    v.push_back({ "scan_escape", HCA_WIFI_SCAN, 0, 's', false, { ESC }, 0 });
    v.push_back({ "scan_open_ap", HCA_WIFI_SCAN, 0, 's', false, { DN, RET }, 0 });                                           // Cafe Lumiere: no password
    v.push_back({ "scan_wpa_password", HCA_WIFI_SCAN, 0, 's', false, cat({ RET }, cat(typed("secret"), { RET })), 0 });       // Home WLAN
    v.push_back({ "scan_wpa_password_cancel", HCA_WIFI_SCAN, 0, 's', false, cat({ RET }, cat(typed("sec"), { ESC, ESC })), 0 });
    v.push_back({ "scan_rescan_then_esc", HCA_WIFI_SCAN, 0, 's', false, { 'r', DN, ESC }, 0 });
    v.push_back({ "scan_paging", HCA_WIFI_SCAN, 0, 's', false, { '+', '-', UP, DN, DN, DN, ESC }, 0 });
    v.push_back({ "scan_disconnect", HCA_WIFI_SCAN, 0, 's', false, { 'd' }, 0 });
    v.push_back({ "scan_forget_armed", HCA_WIFI_SCAN, 0, 's', false, { 'f', 'f' }, 0 });
    v.push_back({ "scan_forget_cancelled", HCA_WIFI_SCAN, 0, 's', false, { 'f', DN, ESC }, 0 });
    v.push_back({ "scan_manual_wpa2", HCA_WIFI_SCAN, 0, 's', false, cat({ 'm' }, cat(typed("Guest net"), cat({ RET, DN, DN, RET }, cat(typed("hunter2"), { RET })))), 0 });
    v.push_back({ "scan_manual_cancel", HCA_WIFI_SCAN, 0, 's', false, cat({ 'm' }, cat(typed("Guest"), { ESC, ESC })), 0 });
    v.push_back({ "scan_no_ap_then_rescan", HCA_WIFI_SCAN, 0, 's', false, { 'r', ESC }, 0 });
    // the manual entry from the page
    v.push_back({ "manual_open_network", HCA_WIFI_MANUAL, 0, 'm', false, cat(typed("Cafe"), std::vector<int>{ RET, UP, UP, UP, RET }), 0 });     // auth list: UP to Open
    v.push_back({ "manual_blank_name_refused", HCA_WIFI_MANUAL, 0, 'm', false, cat({ RET }, cat(typed("X"), cat({ RET, RET }, typed("pw")))), 0 });
    v.push_back({ "manual_escape", HCA_WIFI_MANUAL, 0, 'm', false, { ESC }, 0 });
    v.push_back({ "manual_auth_cancel", HCA_WIFI_MANUAL, 0, 'm', false, cat(typed("Net"), { RET, ESC }), 0 });
    // the static address
    v.push_back({ "addr_valid", HCA_WIFI_ACTIVATE, NS_IP, -1, false, cat({ KEY_END }, cat({ KEY_BACK, KEY_BACK }, cat(typed("99"), { RET }))), 0 });
    v.push_back({ "addr_invalid_then_fixed", HCA_WIFI_ACTIVATE, NS_GW, -1, false, cat({ KEY_END }, cat({ KEY_BACK }, cat({ RET }, cat({ KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK, KEY_BACK }, cat(typed("10.0.0.1"), { RET }))))), 0 });
    v.push_back({ "addr_cancel", HCA_WIFI_ACTIVATE, NS_DNS, -1, false, { ESC }, 0 });
    return v;
}

static void run_one(const Scn &s, bool stepped, std::string *out)
{
    reset_world();
    g_log.clear();
    R.script = s.script;
    R.at = 0;
    R.keys.clear();
    R.pending = -1;
    R.blocking = true;                                // the keys come through the environment's queue in both drivers (explicit step input is tested by test_hostjobs); the stepped one returns at every wait
    R.now = 5000;
    R.list_up = false;
    g_fallbacks = 0;
    HcWifiEnv e = make_env();
    int r;
#ifdef OLD_FLOW
    (void)stepped;
    r = hc_wifi_key(e, s.act, s.nsel, s.rawk, s.armed);
#else
    if (!stepped) {
        r = hc_wifi_key(e, s.act, s.nsel, s.rawk, s.armed);
    } else {
        HcJob *job = 0;
        r = hc_wifi_key(e, s.act, s.nsel, s.rawk, s.armed, &job);
        if (job) {
            HcJobIn in;
            in.key = -1;
            int steps = 0;
            while (!job->step(in) && steps++ < 100000) {
            }
            CHECK(job->done());
            delete job;
            r = HCW_NONE;                                    // the job ran the modal: what the blocking call returns
        }
    }
#endif
    char b[100];
    snprintf(b, sizeof(b), "result|%d|keys_left=%d|esc_fallback=%d", r, (int)(s.script.size() - R.at), g_fallbacks);
    g_log.push_back(b);
    *out = std::string("case ") + s.name + "\n";
    for (auto &l : g_log) *out += l + "\n";
    if (getenv("HW_TRACE")) fprintf(stderr, "%s", out->c_str());
}

static void on_alarm(int)
{
    for (size_t i = g_log.size() > 6 ? g_log.size() - 6 : 0; i < g_log.size(); i++) printf("  last: %s\n", g_log[i].c_str());
    printf("FAIL the flow did not end within 30 s\n");
    fflush(stdout);
    _exit(1);
}

int main(int argc, char **argv)
{
    signal(SIGALRM, on_alarm);
    alarm(30);
    if (argc < 2) { printf("usage: test_hostwifi <golden> [--write-golden]\n"); return 2; }
    std::string golden_path = argv[1];
    bool write = argc > 2 && !strcmp(argv[2], "--write-golden");
    std::string all;
    for (auto &s : scenarios()) {
        std::string a, b;
        if (getenv("HW_TRACE")) { fprintf(stderr, "scenario %s\n", s.name); }
        if (getenv("HW_ONLY") && strcmp(getenv("HW_ONLY"), s.name)) continue;
        run_one(s, false, &a);
        all += a;
#ifndef OLD_FLOW
        run_one(s, true, &b);
        if (a != b) {
            printf("the stepped job differs from the blocking call in %s\n--- blocking\n%s--- stepped\n%s", s.name, a.c_str(), b.c_str());
        }
        CHECK(a == b);
#endif
    }
    if (write) {
        if (FILE *f = fopen(golden_path.c_str(), "w")) { fwrite(all.data(), 1, all.size(), f); fclose(f); }
        printf("golden written: %s (%zu bytes)\n", golden_path.c_str(), all.size());
    } else {
        std::string want;
        if (FILE *f = fopen(golden_path.c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) want.append(buf, n);
            fclose(f);
        }
        if (want != all) {
            printf("the record differs from the golden file %s\n", golden_path.c_str());
            std::string mine = golden_path + ".new";
            if (FILE *f = fopen(mine.c_str(), "w")) { fwrite(all.data(), 1, all.size(), f); fclose(f); }
        }
        CHECK(want == all);
    }
    printf("%s (%d failed)\n", fails ? "test_hostwifi FAILED" : "test_hostwifi ok", fails);
    return fails ? 1 : 0;
}
