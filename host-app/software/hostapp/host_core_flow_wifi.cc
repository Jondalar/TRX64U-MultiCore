// The WiFi page's actions and modals as the core runs them (host_core_flow_wifi.h). The text of every note, title and key line is the old hostui.cc's, word for word.
#include "host_core_flow_wifi.h"
#include "host_core_flow_util.h"
#include "host_core_job.h"
#include "host_core.h"
#include "keyboard.h"
#include "listwin.h"
#include <stdio.h>
#include <string.h>

namespace {

const char *const keys_aps = "UP/DOWN   RETURN connect   R rescan   M manual   D disconnect   F forget   ESC close";
const char *const keys_choice = "UP/DOWN select    RETURN choose    ESC cancels";
const char *const keys_entry_save = "RETURN saves    ESC cancels    LEFT/RIGHT HOME END move    INS blank";

char wf_op[72];                                  // what the last action was ("Connecting to Home"): the driver only says busy and ok / not ok
bool wf_op_run_;
char last_sub[96];
HcWifiPick g_pick;                               // one modal at a time: the list and the entry are never up together
HcWifiEntry g_entry;
WifiNetAp g_aps[HCW_PICK_MAX];

enum { JOB_LAST, JOB_DISCONNECT, JOB_FORGET };

void wipe(char *s, int n)
{
    volatile char *v = s;
    for (int i = 0; i < n; i++) {
        v[i] = 0;
    }
}

bool blank_text(const char *s)
{
    while (*s == ' ') {
        s++;
    }
    return !*s;
}

const char *check_ssid(const char *t) { return blank_text(t) ? "The name of the access point is needed." : 0; }

typedef const char *(*WfCheck)(const char *text);

void note(const HcWifiEnv &e, const char *text, int tone) { e.note(e.ctx, text, tone); }
void status_now(const HcWifiEnv &e) { wifi_net_status(e.st); }
int keyin(const HcWifiEnv &e) { return e.base->getch(e.base->ctx); }
void nap(const HcWifiEnv &e) { hcf::sleep_ms(*e.base, 20); }
uint32_t now(const HcWifiEnv &e) { return e.base->now(e.base->ctx); }

// An action on a task of the driver. The page shows it as the WiFi state until it is over.
bool start(const HcWifiEnv &e, const char *op, int job)
{
    if (const char *why = hc_wifi_unavailable(*e.st)) {
        note(e, why, HCT_WARN);
        return false;
    }
    bool ok = job == JOB_LAST ? wifi_net_autoconnect() : job == JOB_DISCONNECT ? wifi_net_disconnect() : wifi_net_forget();
    if (!ok) {
        note(e, "The module is busy with the last action", HCT_WARN);
        return false;
    }
    snprintf(wf_op, sizeof(wf_op), "%s", op);
    wf_op_run_ = true;
    status_now(e);
    return true;
}

// ---- the modals as steps ----------------------------------------------------------------------------------------------------------------------------------
// The entry field, the authentication list, the connect, the manual entry, the access point list and the static address are the loops of the old functions with each wait a place
// where the step returns (host_core_job.h): every one of them is a sub-step with its own state, run by the WifiJob below. The texts, keys and timings are the old ones.

// The sub-steps' waits: the key of the step (or the environment's key source), and the 20 ms nap when there is none (the old `while ((k = keyin(e)) < 0) nap(e);`).
#define WJ_WAIT_KEY(var) do { for (;;) { var = j.poll_key(); if (var >= 0) { break; } nap(*e); pc = __LINE__; return false; case __LINE__:; } } while (0)
#define WJ_SUB(sub) do { pc = __LINE__; case __LINE__:; if (!(sub).step(j)) { return false; } } while (0)

// The entry field. buf is the start value and receives the result (max characters). secret: shown as stars. check: the text, or a message why not (0 = fine).
// open_key: the key that opened this; its auto-repeat is not typed into the field.
struct EntrySub {
    const HcWifiEnv *e;
    const char *title, *sub, *reject, *keys;
    char *buf;
    int max, open_key;
    bool secret;
    WfCheck check;
    bool saved;
    void begin(const HcWifiEnv &env, const char *t, const char *s, char *b, int m, bool sec, const char *rej, const char *k, int ok, WfCheck chk)
    {
        e = &env;
        title = t;
        sub = s;
        buf = b;
        max = m;
        secret = sec;
        reject = rej;
        keys = k;
        open_key = ok;
        check = chk;
        saved = false;
        pc = 0;
    }
    bool step(HcJob &j)
    {
        switch (pc) {
        case 0:
            if (!e->panels(e->ctx)) {
                saved = false;
                pc = -1;
                return true;
            }
            le_init(&le, buf, max, reject);
            list_was_up = e->entry_open(e->ctx);
            vis = e->entry_vis(e->ctx);
            hc_wifi_entry_make(&g_entry, title, sub, keys, le, secret, "", vis);
            e->draw_entry(e->ctx, g_entry, true);
            save = false;
            t0 = now(*e);
            for (;;) {
                WJ_WAIT_KEY(k);
                if (k == open_key && now(*e) - t0 < 300) {
                    continue;
                }
                r = le_key(&le, k);
                if (r == LE_CANCEL) {
                    break;
                }
                if (r == LE_DONE) {
                    why = check ? check(le.buf) : 0;
                    if (why) {
                        hc_wifi_entry_make(&g_entry, title, sub, keys, le, secret, why, vis);
                        e->draw_entry(e->ctx, g_entry, false);
                        continue;
                    }
                    save = true;
                    break;
                }
                hc_wifi_entry_make(&g_entry, title, sub, keys, le, secret, "", vis);
                e->draw_entry(e->ctx, g_entry, false);
            }
            e->entry_end(e->ctx, list_was_up);
            if (save) {
                memcpy(buf, le.buf, le.len + 1);
                if (!secret) {
                    e->history(e->ctx, le.buf);
                }
            }
            wipe(le.buf, sizeof(le.buf));
            saved = save;
            pc = -1;
            return true;
        }
        return true;
    }

private:
    int pc, vis, k, r;
    LineEdit le;
    bool list_was_up, save;
    uint32_t t0;
    const char *why;
};

// The list of the authentication names (the order of network_esp32.cc authmodes); `result` -1 = cancelled.
struct ChooseSub {
    const HcWifiEnv *e;
    const char *title, *sub;
    int sel, open_key, result;
    void begin(const HcWifiEnv &env, const char *t, const char *s, int sl, int ok)
    {
        e = &env;
        title = t;
        sub = s;
        sel = sl;
        open_key = ok;
        result = -2;
        pc = 0;
    }
    bool step(HcJob &j)
    {
        switch (pc) {
        case 0:
            rows = e->pick_rows(e->ctx);
            first = 0;
            listwin_fix(sel, WIFI_AUTH_N, rows, &first);
            hc_wifi_auth_pick(&g_pick, title, sub, sel, first);
            e->draw_pick(e->ctx, g_pick, true);
            t0 = now(*e);
            result = -2;
            while (result == -2) {
                WJ_WAIT_KEY(k);
                if (k == open_key && now(*e) - t0 < 300) {
                    continue;
                }
                if (k == KEY_UP && sel > 0) {
                    sel--;
                } else if (k == KEY_DOWN && sel + 1 < WIFI_AUTH_N) {
                    sel++;
                } else if (k == KEY_RETURN) {
                    result = sel;
                } else if (k == KEY_ESCAPE || k == KEY_BREAK) {
                    result = -1;
                }
                listwin_fix(sel, WIFI_AUTH_N, rows, &first);
                hc_wifi_auth_pick(&g_pick, title, sub, sel, first);
                e->draw_pick(e->ctx, g_pick, false);
            }
            e->pick_end(e->ctx);
            pc = -1;
            return true;
        }
        return true;
    }

private:
    int pc, rows, first, k;
    uint32_t t0;
};

// The password (not for Open), then the connect on the driver's task.
struct ConnectSub {
    const HcWifiEnv *e;
    char ssid[34];
    uint8_t auth;
    bool from_list;
    int open_key;
    bool ok;
    void begin(const HcWifiEnv &env, const char *s, uint8_t a, bool fl, int okey)
    {
        e = &env;
        snprintf(ssid, sizeof(ssid), "%s", s);
        auth = a;
        from_list = fl;
        open_key = okey;
        ok = false;
        pc = 0;
    }
    bool step(HcJob &j)
    {
        switch (pc) {
        case 0:
            pass[0] = 0;
            for (i = 0; i < 33; i++) {                           // control bytes of an access point's name are not shown
                unsigned char ch = (unsigned char)ssid[i];
                shown[i] = (ch && (ch < 32 || ch == 127)) ? '?' : (char)ch;
                if (!ch) {
                    break;
                }
            }
            shown[33] = 0;
            if (auth > 0) {
                snprintf(sub, sizeof(sub), "%s    %s", shown, wifi_auth_names[auth < WIFI_AUTH_N ? auth : 0]);
                esub.begin(*e, "Password", sub, pass, 64, true, "", "RETURN connects    ESC cancels    LEFT/RIGHT HOME END move    INS blank", open_key, 0);
                WJ_SUB(esub);
                if (!esub.saved) {
                    wipe(pass, sizeof(pass));
                    ok = false;
                    pc = -1;
                    return true;
                }
            }
            snprintf(op, sizeof(op), "Connecting to %s", shown);
            ok = false;
            if (const char *why = hc_wifi_unavailable(*e->st)) {
                note(*e, why, HCT_WARN);
            } else if (!wifi_net_connect(ssid, pass, auth, from_list)) {
                note(*e, "The module is busy with the last action", HCT_WARN);
            } else {
                snprintf(wf_op, sizeof(wf_op), "%s", op);
                wf_op_run_ = true;
                status_now(*e);
                ok = true;
            }
            wipe(pass, sizeof(pass));
            pc = -1;
            return true;
        }
        return true;
    }

private:
    int pc, i;
    char pass[65], shown[34], sub[96], op[72];
    EntrySub esub;
};

// "Enter AP manually": the name, the authentication, the password (not for Open).
struct ManualSub {
    const HcWifiEnv *e;
    int open_key;
    bool ok;
    void begin(const HcWifiEnv &env, int okey)
    {
        e = &env;
        open_key = okey;
        ok = false;
        pc = 0;
    }
    bool step(HcJob &j)
    {
        switch (pc) {
        case 0:
            ssid[0] = 0;
            ok = false;
            esub.begin(*e, "Enter access point", "The name of the network (SSID)", ssid, 32, false, "", "RETURN next    ESC cancels    LEFT/RIGHT HOME END move    INS blank", open_key, check_ssid);
            WJ_SUB(esub);
            if (!esub.saved) {
                pc = -1;
                return true;
            }
            csub.begin(*e, "Authentication", ssid, 3, 0);
            WJ_SUB(csub);
            if (csub.result < 0) {
                pc = -1;
                return true;
            }
            conn.begin(*e, ssid, (uint8_t)csub.result, false, 0);
            WJ_SUB(conn);
            ok = conn.ok;
            pc = -1;
            return true;
        }
        return true;
    }

private:
    int pc;
    char ssid[34];
    EntrySub esub;
    ChooseSub csub;
    ConnectSub conn;
};

// "Select AP from list": the scan, as the modal list. Ends when closed or when an action was started.
struct ApListSub {
    const HcWifiEnv *e;
    int open_key;
    void begin(const HcWifiEnv &env, int okey)
    {
        e = &env;
        open_key = okey;
        pc = 0;
    }
    bool step(HcJob &j)
    {
        switch (pc) {
        case 0:
            if (!e->panels(e->ctx)) {
                pc = -1;
                return true;
            }
            rows = e->pick_rows(e->ctx);
            n = 0;
            sel = 0;
            first = 0;
            msg[0] = 0;
            msgtone = HCT_WARN;
            status_now(*e);
            wifi_net_rescan();
            n = wifi_net_aps(g_aps, HCW_PICK_MAX);
            hc_wifi_aps_pick(&g_pick, g_aps, n, sel, first, *e->st, msg, msgtone);
            e->draw_pick(e->ctx, g_pick, true);
            t0 = now(*e);
            last = t0;
            arm = 0;
            done = false;
            while (!done) {
                k = j.poll_key();
                if (k < 0) {
                    nap(*e);
                    t = now(*e);
                    if (t - last >= 400) {                           // the scan fills the list while we wait
                        last = t;
                        status_now(*e);
                        m = wifi_net_aps(g_aps, HCW_PICK_MAX);
                        if (m != n) {
                            n = m;
                            sel = sel >= n ? (n ? n - 1 : 0) : sel;
                        }
                        listwin_fix(sel, n, rows, &first);
                        hc_wifi_aps_pick(&g_pick, g_aps, n, sel, first, *e->st, msg, msgtone);
                        e->draw_pick(e->ctx, g_pick, false);
                    }
                    pc = 1;
                    return false;
    case 1:;
                    continue;
                }
                if (k == open_key && now(*e) - t0 < 300) {
                    continue;
                }
                armed = arm;
                arm = 0;
                msg[0] = 0;
                sub_kind = 0;
                if (k == KEY_UP && sel > 0) {
                    sel--;
                } else if (k == KEY_DOWN && sel + 1 < n) {
                    sel++;
                } else if (k == '+' && n) {
                    sel = sel + rows < n ? sel + rows : n - 1;
                } else if (k == '-' && n) {
                    sel = sel - rows > 0 ? sel - rows : 0;
                } else if (k == KEY_ESCAPE || k == KEY_BREAK) {
                    done = true;
                } else if (k == KEY_RETURN && n > 0) {
                    sub_kind = 1;
                } else if (k == 'm' || k == 'M') {
                    sub_kind = 2;
                } else if (k == 'r' || k == 'R') {
                    wifi_net_rescan();
                    n = 0;
                    sel = first = 0;
                    snprintf(msg, sizeof(msg), "Scanning again ...");
                    msgtone = HCT_MUTED;
                } else if (k == 'd' || k == 'D') {
                    done = start(*e, "Disconnecting", JOB_DISCONNECT);
                } else if (k == 'f' || k == 'F') {
                    if (armed == 'f') {
                        done = start(*e, "Forgetting the stored access points", JOB_FORGET);
                    } else {
                        arm = 'f';
                        snprintf(msg, sizeof(msg), "F again forgets all stored access points (any other key cancels)");
                        msgtone = HCT_WARN;
                    }
                }
                if (sub_kind == 1) {
                    pick = g_aps[sel];
                    conn.begin(*e, pick.ssid, pick.auth, true, k);
                    WJ_SUB(conn);
                    if (conn.ok) {
                        done = true;
                    }
                } else if (sub_kind == 2) {
                    man.begin(*e, k);
                    WJ_SUB(man);
                    if (man.ok) {
                        done = true;
                    }
                }
                if (done) {
                    break;
                }
                listwin_fix(sel, n, rows, &first);
                hc_wifi_aps_pick(&g_pick, g_aps, n, sel, first, *e->st, msg, msgtone);
                e->draw_pick(e->ctx, g_pick, !e->list_up(e->ctx));
            }
            e->pick_end(e->ctx);
            pc = -1;
            return true;
        }
        return true;
    }

private:
    int pc, rows, n, sel, first, msgtone, k, m, armed, arm, sub_kind;
    char msg[96];
    uint32_t t0, last, t;
    bool done;
    WifiNetAp pick;
    ConnectSub conn;
    ManualSub man;
};

// The modal actions of the page as one job: "Select AP from list", "Enter AP manually" and the typed static address.
enum { WM_SCAN = 1, WM_MANUAL, WM_ADDR };

class WifiJob : public HcJob {
public:
    WifiJob(const HcWifiEnv &en, int m, int row_, int okey) : HcJob(*en.base), e(en), mode(m), row(row_), open_key(okey) { kind = 7; }

protected:
    bool run() override
    {
        HJ_BEGIN();
        if (mode == WM_SCAN) {
            ap.begin(e, open_key);
            HJ_SUB(ap);
            return finish(true);
        }
        if (mode == WM_MANUAL) {
            man.begin(e, open_key);
            HJ_SUB(man);
            return finish(man.ok);
        }
        // a static address, typed
        c = *e.cfg;
        f = row == NS_IP ? c.ip : row == NS_MASK ? c.netmask : row == NS_GW ? c.gateway : c.dns;
        nrej = 0;
        for (int ch = 32; ch < 127; ch++) {
            if (!((ch >= '0' && ch <= '9') || ch == '.')) {
                reject[nrej++] = (char)ch;
            }
        }
        reject[nrej] = 0;
        snprintf(buf, sizeof(buf), "%s", f);
        snprintf(sub, sizeof(sub), "%s, used when DHCP is disabled", e.set_name(e.ctx, row));
        esub.begin(e, e.set_name(e.ctx, row), sub, buf, 15, false, reject, keys_entry_save, open_key, hc_wifi_check_ipv4);
        HJ_SUB(esub);
        if (!esub.saved) {
            note(e, "Unchanged", HCT_MUTED);
            return finish(false);
        }
        snprintf(f, 17, "%s", buf);
        if (wifi_net_cfg_set(&c)) {
            wifi_net_cfg_get(e.cfg);
            snprintf(m, sizeof(m), "%s is %s (saved)", e.set_name(e.ctx, row), f);
            note(e, m, HCT_OK);
        } else {
            note(e, "The WiFi settings could not be saved", HCT_BAD);
        }
        return finish(true);
        HJ_END();
    }

private:
    const HcWifiEnv &e;
    int mode, row, open_key, nrej;
    ApListSub ap;
    ManualSub man;
    EntrySub esub;
    WifiNetCfg c;
    char *f;
    char reject[96], buf[17], sub[64], m[96];
};

}  // namespace

const char *hc_wifi_radio_text(const WifiNetStatus &w)
{
    return w.state == WN_NO_MODULE ? "No module" : (w.state == WN_DISABLED || w.state == WN_OFF) ? "Off" : "On";
}

const char *hc_wifi_unavailable(const WifiNetStatus &w)
{
    switch (w.state) {
    case WN_NO_MODULE: return "There is no WiFi module";
    case WN_OFF: return "The WiFi driver is not running";
    case WN_DISABLED: return "WiFi is switched off - switch it on above (WiFi row, RETURN)";
    default: return NULL;
    }
}

// Dotted decimal, four numbers 0 to 255 (the static addresses).
bool hc_wifi_ipv4_ok(const char *s)
{
    int parts = 0;
    while (*s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        int v = 0, d = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s++ - '0');
            if (++d > 3 || v > 255) {
                return false;
            }
        }
        parts++;
        if (*s == '.') {
            if (!*++s) {
                return false;
            }
        } else if (*s) {
            return false;
        }
    }
    return parts == 4;
}

const char *hc_wifi_check_ipv4(const char *t) { return hc_wifi_ipv4_ok(t) ? 0 : "Four numbers from 0 to 255 with dots, like 192.168.2.64."; }

bool hc_wifi_op_run(void) { return wf_op_run_; }
const char *hc_wifi_op_text(void) { return wf_op; }
void hc_wifi_op_done(void) { wf_op_run_ = false; }
const char *hc_wifi_last_sub(void) { return last_sub; }

// The scan list as rows: signal in dBm, green from -60, plain to -75, amber below.
void hc_wifi_aps_pick(HcWifiPick *p, const WifiNetAp *aps, int n, int sel, int first, const WifiNetStatus &st, const char *msg, int msgtone)
{
    for (int i = 0; i < n && i < HCW_PICK_MAX; i++) {
        HcWifiItem &it = p->item[i];
        snprintf(it.a, sizeof(it.a), "%s", aps[i].name[0] ? aps[i].name : "(hidden)");
        snprintf(it.b, sizeof(it.b), "%s", wifi_auth_names[aps[i].auth < WIFI_AUTH_N ? aps[i].auth : 0]);
        snprintf(it.c, sizeof(it.c), "%d dBm", (int)aps[i].rssi);
        it.ctone = aps[i].rssi >= -60 ? HCT_OK : (aps[i].rssi >= -75 ? HCT_INK : HCT_WARN);
    }
    char sub[96];
    if ((st.scan == WS_RUNNING || st.scan == WS_NONE || st.state == WN_SCANNING) && n == 0) {
        snprintf(sub, sizeof(sub), "Scanning ...");
    } else if (n == 0 && st.scan == WS_FAILED) {
        snprintf(sub, sizeof(sub), "The scan failed (no valid reply). R scans again.");
    } else if (n == 0) {
        snprintf(sub, sizeof(sub), "No access point found. R scans again.");
    } else {
        snprintf(sub, sizeof(sub), "%d access point%s found%s%s", n, n == 1 ? "" : "s", st.ssid[0] ? ", connected to " : "", st.ssid);
    }
    memcpy(last_sub, sub, sizeof(sub));
    snprintf(p->title, sizeof(p->title), "Access points");
    memcpy(p->sub, sub, sizeof(sub));
    p->subtone = HCT_MUTED;
    p->n = n;
    p->sel = sel;
    p->first = first;
    snprintf(p->msg, sizeof(p->msg), "%s", msg);
    p->msgtone = (uint8_t)msgtone;
    p->keys = keys_aps;
}

void hc_wifi_auth_pick(HcWifiPick *p, const char *title, const char *sub, int sel, int first)
{
    for (int i = 0; i < WIFI_AUTH_N; i++) {
        HcWifiItem &it = p->item[i];
        snprintf(it.a, sizeof(it.a), "%s", wifi_auth_names[i]);
        it.b[0] = it.c[0] = 0;
        it.ctone = HCT_INK;
    }
    snprintf(p->title, sizeof(p->title), "%s", title);
    snprintf(p->sub, sizeof(p->sub), "%s", sub);
    p->subtone = HCT_MUTED;
    p->n = WIFI_AUTH_N;
    p->sel = sel;
    p->first = first;
    p->msg[0] = 0;
    p->msgtone = HCT_WARN;
    p->keys = keys_choice;
}

void hc_wifi_entry_make(HcWifiEntry *o, const char *title, const char *sub, const char *keys, const LineEdit &e, bool secret, const char *msg, int vis)
{
    snprintf(o->title, sizeof(o->title), "%s", title);
    snprintf(o->sub, sizeof(o->sub), "%s", sub);
    snprintf(o->keys, sizeof(o->keys), "%s", keys);
    o->secret = secret;
    if (secret) {                                             // a password never leaves the device, not even masked by its length
        snprintf(o->shown, sizeof(o->shown), "%s", e.len ? "********" : "");
    } else {
        snprintf(o->shown, sizeof(o->shown), "%s", e.buf);
    }
    le_row(const_cast<LineEdit *>(&e), vis, secret ? '*' : 0, o->row, sizeof(o->row));
    snprintf(o->msg, sizeof(o->msg), "%s", msg);
}

// A modal action is a job: the caller steps it (job given), or the call runs it to its end (job null: the old blocking call).
static int run_modal(const HcWifiEnv &e, HcJob **job, int mode, int row, int open_key)
{
    HcJob *j = new WifiJob(e, mode, row, open_key);
    if (job) {
        *job = j;
        return HCW_JOB;
    }
    hc_job_run(j);
    return HCW_NONE;
}

int hc_wifi_key(const HcWifiEnv &e, int act, int nsel, int rawk, bool armed, HcJob **job)
{
    if (job) {
        *job = 0;
    }
    if (act == HCA_WIFI_DEC || act == HCA_WIFI_INC || act == HCA_WIFI_ACTIVATE) {
        if (nsel == NS_RADIO) {
            status_now(e);
            bool on = e.st->state == WN_DISABLED || e.st->state == WN_OFF;      // it is off: switch it on
            if (!wifi_net_radio(on)) {
                note(e, "There is no WiFi module", HCT_WARN);
            } else {
                note(e, on ? "WiFi is on" : "WiFi is off", HCT_OK);
                status_now(e);
            }
        } else if (!*e.cfg_ok) {
            note(e, "The WiFi settings are not available", HCT_WARN);
        } else if (nsel == NS_DHCP) {
            WifiNetCfg c = *e.cfg;
            c.dhcp = !c.dhcp;
            if (wifi_net_cfg_set(&c)) {
                wifi_net_cfg_get(e.cfg);
                note(e, e.cfg->dhcp ? "DHCP enabled (saved)" : "DHCP disabled, the static addresses are used (saved)", HCT_OK);
            } else {
                note(e, "The WiFi settings could not be saved", HCT_BAD);
            }
        } else if (act == HCA_WIFI_ACTIVATE) {
            if (e.cfg->dhcp) {
                note(e, "The static addresses are used when DHCP is disabled (LEFT/RIGHT on Use DHCP)", HCT_WARN);
            } else {
                return run_modal(e, job, WM_ADDR, nsel, rawk);
            }
        }
    } else if (act == HCA_WIFI_SCAN) {
        status_now(e);
        if (const char *why = hc_wifi_unavailable(*e.st)) {
            note(e, why, HCT_WARN);
        } else {
            return run_modal(e, job, WM_SCAN, 0, rawk);
        }
    } else if (act == HCA_WIFI_MANUAL) {
        status_now(e);
        if (const char *why = hc_wifi_unavailable(*e.st)) {
            note(e, why, HCT_WARN);
        } else {
            return run_modal(e, job, WM_MANUAL, 0, rawk);
        }
    } else if (act == HCA_WIFI_LAST) {
        start(e, "Connecting to the last access point", JOB_LAST);
    } else if (act == HCA_WIFI_DISCONNECT) {
        start(e, "Disconnecting", JOB_DISCONNECT);
    } else if (act == HCA_WIFI_FORGET) {
        if (armed) {
            if (e.arm_yes(e.ctx)) {
                start(e, "Forgetting the stored access points", JOB_FORGET);
            }
        } else {
            return HCW_FORGET_ARM;
        }
    }
    return HCW_NONE;
}

bool hc_wifi_forget_confirm(const HcWifiEnv &e, char *why, int n)
{
    status_now(e);
    if (const char *reason = hc_wifi_unavailable(*e.st)) {
        note(e, reason, HCT_WARN);
        snprintf(why, n, "Not done: %s", reason);
        return false;
    }
    if (!start(e, "Forgetting the stored access points", JOB_FORGET)) {
        snprintf(why, n, "Not done: the module is busy with the last action");
        return false;
    }
    return true;
}

int hc_wifi_set_addr(const HcWifiEnv &e, int row, const char *value, char *why, int n)
{
    if (!*e.cfg_ok) {
        snprintf(why, n, "The WiFi settings are not available");
        return 1;
    }
    if (!hc_wifi_ipv4_ok(value)) {
        snprintf(why, n, "%s", hc_wifi_check_ipv4(value));
        return 1;
    }
    WifiNetCfg c = *e.cfg;
    char *f = row == NS_IP ? c.ip : row == NS_MASK ? c.netmask : row == NS_GW ? c.gateway : c.dns;
    snprintf(f, 17, "%s", value);
    if (!wifi_net_cfg_set(&c)) {
        note(e, "The WiFi settings could not be saved", HCT_BAD);
        snprintf(why, n, "The WiFi settings could not be saved");
        return 1;
    }
    wifi_net_cfg_get(e.cfg);
    char m[96];
    snprintf(m, sizeof(m), "%s is %s (saved)", e.set_name(e.ctx, row), f);
    note(e, m, HCT_OK);
    return 0;
}
