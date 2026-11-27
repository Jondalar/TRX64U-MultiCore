// The model the service sees over the core's state (host_core_model.h): the code of hostui.cc's UiModel, moved with its texts and its order unchanged.
#include "host_core_model.h"
#include "host_core_ui.h"
#include "host_core_nav_rw.h"
#include "hostvideo.h"
#include "flashlock.h"
#include "ftpstate.h"
#include "mediaui.h"
#include "host_core_rows.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef HOSTAPP_FLASH_LOCK
#define HOSTAPP_FLASH_LOCK 0
#endif

#define nv (hc_nav())
#define cfg (hc_cfg())
#define c_wifi (hc_status().wifi)
#define c_wcfg (hc_status().wcfg)
#define c_wcfg_ok (hc_status().wcfg_ok)
#define c_enet (hc_status().enet)
#define c_eip (hc_status().eip)
#define c_net (hc_status().net)
#define c_media (hc_status().media)
#define c_ip (hc_status().ip)

const HcUiEnv &hc_ui_env(void);
#define g_env (hc_ui_env())

namespace {

static bool parse_bool(const char *v, bool *out)
{
    if (!strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcmp(v, "1")) {
        *out = true;
        return true;
    }
    if (!strcasecmp(v, "off") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcmp(v, "0")) {
        *out = false;
        return true;
    }
    return false;
}

// something the person did not do with a key changed the state: the screen draws it
void touched(void)
{
    hc_ui_changed();
    if (g_env.redraw) {
        g_env.redraw(g_env.ctx);
    }
}

class HcModel : public HostModel {
public:
    int view() override { return nv.view; }
    int mode() override { return (int)nv.br_mode; }
    bool wifi_page() override { return nv.wm_mode != 0; }
    bool cores_page() override { return nv.cm_mode != HCM_OFF; }
    bool picture_pending() override { return hv_state()->pending; }
    bool key(int k) override { return g_env.key ? g_env.key(g_env.ctx, k) : false; }

    int rows(const char *src) override
    {
        if (!strcmp(src, "entry")) return cfg.n;
        if (!strcmp(src, "file")) return nv.bn;
        if (!strcmp(src, "cmline")) return nv.cm_mode != HCM_OFF ? hc_cm_lines() : 0;
        return 0;
    }

    bool row(const char *src, int i, HRow *r) override
    {
        memset(r, 0, sizeof(*r));
        r->enabled = true;
        if (!strcmp(src, "entry")) {
            HcEntryRow v;
            if (!hc_row_entry(i, true, &v)) return false;
            snprintf(r->ident, sizeof(r->ident), "%s", v.name);
            snprintf(r->title, sizeof(r->title), "%s", v.name);
            snprintf(r->sub, sizeof(r->sub), "%s", v.sub);
            snprintf(r->badge, sizeof(r->badge), "%s", v.badge);
            snprintf(r->ver, sizeof(r->ver), "%s", v.ver);
            r->def = v.def;
            r->configs = v.configs;
            r->startable = hc_startable(i);
            return true;
        }
        if (!strcmp(src, "file")) {
            if (i < 0 || i >= nv.bn) return false;
            snprintf(r->ident, sizeof(r->ident), "%s", nv.bl[i].name);
            snprintf(r->title, sizeof(r->title), "%s", nv.bl[i].name);
            r->dir = nv.bl[i].dir;
            if (!nv.bl[i].dir) {
                ui_size_text(r->value, sizeof(r->value), nv.bl[i].size64);
            }
            return true;
        }
        if (!strcmp(src, "cmline")) {
            if (i < 0 || i >= hc_cm_lines()) return false;
            snprintf(r->ident, sizeof(r->ident), "%d", i);
            snprintf(r->title, sizeof(r->title), "%s", hc_cm_line(i, 0));
            return true;
        }
        return false;
    }

    int cursor(const char *src) override
    {
        if (!strcmp(src, "entry")) return cfg.n ? nv.sel : -1;
        if (!strcmp(src, "file")) return nv.bn ? nv.bsel : -1;
        if (!strcmp(src, "cmline")) return nv.cm_top;
        return -1;
    }

    void focus(const char *src, int i) override
    {
        hc_arm_drop(HARM_END_GONE);
        if (!strcmp(src, "entry")) {
            hc_nav_focus(0, i);
        } else if (!strcmp(src, "file")) {
            hc_nav_focus(1, i);
        } else if (!strcmp(src, "cmline")) {
            hc_nav_focus(2, i);
        }
        touched();
    }

    bool field(const char *key, HField *f) override
    {
        memset(f, 0, sizeof(*f));
        f->visible = true;
        f->enabled = true;
        int kind = hc_set_kind_of(key);
        if (kind >= 0) {
            int kinds[HC_SET_SLOTS], n = hc_set_kinds(kinds);
            f->visible = false;
            for (int i = 0; i < n; i++) {
                f->visible = f->visible || kinds[i] == kind;
            }
            char v[64];
            const char *name, *help;
            int kinds2[HC_SET_SLOTS], n2 = hc_set_kinds(kinds2);
            bool selected = n2 > 0 && kinds2[nv.ssel < n2 ? nv.ssel : 0] == kind;
            hc_set_row_text(kind, name, v, help, selected);         // the chip is asked for the flash lock's help of the selected row only
            snprintf(f->value, sizeof(f->value), "%s", v);
            snprintf(f->help, sizeof(f->help), "%s", help);
            f->on = hc_set_toggle_state(kind);
            return true;
        }
        int wr = hc_wifi_row_of(key);
        if (wr >= 0) {
            if (wr == NS_RADIO) {
                snprintf(f->value, sizeof(f->value), "%s", hc_wifi_radio_text(c_wifi));
                f->on = !(c_wifi.state == WN_NO_MODULE || c_wifi.state == WN_DISABLED || c_wifi.state == WN_OFF);
                f->enabled = c_wifi.state != WN_NO_MODULE;
            } else if (!c_wcfg_ok) {
                snprintf(f->value, sizeof(f->value), "(no WiFi store)");
                f->enabled = false;
            } else if (wr == NS_DHCP) {
                snprintf(f->value, sizeof(f->value), "%s", c_wcfg.dhcp ? "Enabled" : "Disabled");
                f->on = c_wcfg.dhcp;
            } else {
                snprintf(f->value, sizeof(f->value), "%s", wr == NS_IP ? c_wcfg.ip : wr == NS_MASK ? c_wcfg.netmask : wr == NS_GW ? c_wcfg.gateway : c_wcfg.dns);
                f->enabled = !c_wcfg.dhcp;
                if (c_wcfg.dhcp) snprintf(f->help, sizeof(f->help), "The static addresses are used when DHCP is disabled");
            }
            return true;
        }
        char st[160];
        const char *wnet, *wip;
        if (!strncmp(key, "wifi.", 5) || !strncmp(key, "net.wifi_", 9)) {
            hc_wifi_texts3(st, wnet, wip);
            const char *k = key[0] == 'w' ? key + 5 : key + 9;
            if (!strcmp(k, "state")) { snprintf(f->value, sizeof(f->value), "%s", st); return true; }
            if (!strcmp(k, "net")) { snprintf(f->value, sizeof(f->value), "%s", wnet); return true; }
            if (!strcmp(k, "addr")) { snprintf(f->value, sizeof(f->value), "%s", wip); return true; }
            if (!strcmp(k, "mac")) {
                bool mod = c_wifi.state != WN_NO_MODULE && c_wifi.mac[0];
                snprintf(f->value, sizeof(f->value), "%s%s%s", mod ? c_wifi.mac : "(none)", mod && c_wifi.module[0] ? "    " : "", mod ? c_wifi.module : "");
                return true;
            }
        }
        if (!strcmp(key, "net.eth")) {
            snprintf(f->value, sizeof(f->value), c_enet ? "link up, %s" : "no link or no address yet%s", c_enet ? c_eip : "");
            return true;
        }
        if (!strcmp(key, "net.ftp")) {
            ftp_state_text(ftp_state_now(), c_net, f->value, sizeof(f->value));
            return true;
        }
        if (!strcmp(key, "net.media")) {
            snprintf(f->value, sizeof(f->value), "%s", c_media[0] ? c_media : "(none)");
            return true;
        }
        if (!strcmp(key, "net.note")) {
            snprintf(f->value, sizeof(f->value), "%s", nv.nnote[0] ? nv.nnote : "The WiFi is set under F8 (Settings, WiFi).");
            return true;
        }
        if (!strcmp(key, "cores.head")) {
            char h[HC_CM_LINE];
            int hc;
            hc_cm_head(h, sizeof(h), &hc);
            snprintf(f->value, sizeof(f->value), "%s", h);
            f->enabled = nv.cm_mode == HCM_LIST && hc_cm_ndelete() > 0;
            return true;
        }
        return false;
    }

    void field_focus(const char *key) override
    {
        hc_arm_drop(HARM_END_GONE);
        int kind = hc_set_kind_of(key);
        if (kind >= 0) {
            int kinds[HC_SET_SLOTS], n = hc_set_kinds(kinds);
            for (int i = 0; i < n; i++) {
                if (kinds[i] == kind) {
                    hc_nav_set_ssel(i);
                }
            }
        } else {
            int wr = hc_wifi_row_of(key);
            if (wr >= 0) {
                hc_nav_set_nsel(wr);
            }
        }
        touched();
    }

    int field_step(const char *key, int dir, char *why, int n) override
    {
        hc_arm_drop(HARM_END_GONE);
        if (!strcmp(key, "pick.here")) {
            if (nv.br_mode != HCB_PICK) {
                snprintf(why, n, "no folder pick is open");
                return 1;
            }
            hc_ui_pick_confirm();
            touched();
            return 0;
        }
        int kind = hc_set_kind_of(key);
        if (kind >= 0) {
            int kinds[HC_SET_SLOTS], cnt = hc_set_kinds(kinds), at = -1;
            for (int i = 0; i < cnt; i++) {
                if (kinds[i] == kind) {
                    at = i;
                }
            }
            if (at < 0) {
                snprintf(why, n, "that row is not on the page now");
                return 1;
            }
            hc_nav_set_ssel(at);
            hc_ui_settings_change(kind, dir);
            touched();
            return 0;
        }
        int wr = hc_wifi_row_of(key);
        if (wr >= 0 && nv.wm_mode) {
            hc_nav_set_nsel(wr);
            hc_ui_wifi_key(dir < 0 ? HCA_WIFI_DEC : HCA_WIFI_INC, -1);
            touched();
            return 0;
        }
        snprintf(why, n, "no such field");
        return 1;
    }

    int field_set(const char *key, const char *value, char *why, int n) override
    {
        int kind = hc_set_kind_of(key);
        if (kind == SK_DIRECT || kind == SK_AUTO || kind == SK_PROTECT || kind == SK_FTP || kind == SK_AV) {
            bool want;
            if (!parse_bool(value, &want)) {
                snprintf(why, n, "on or off");
                return 1;
            }
            if (kind == SK_PROTECT && !HOSTAPP_FLASH_LOCK) {
                snprintf(why, n, "this build has no flash lock");
                return 1;
            }
            if (kind == SK_AUTO && cfg.direct) {
                snprintf(why, n, "autostart needs direct boot off");
                return 1;
            }
            if (hc_set_toggle_state(kind) != want) {
                return field_step(key, 1, why, n);
            }
            return 0;
        }
        if (kind == SK_SECS) {
            int v = atoi(value);
            if (cfg.direct || cfg.countdown <= 0) {
                snprintf(why, n, "the delay exists while autostart is on");
                return 1;
            }
            if (v < 1 || v > 60) {
                snprintf(why, n, "1 to 60 seconds");
                return 1;
            }
            hc_ui_cfg_after(hc_autostart(true, v));
            touched();
            return 0;
        }
        if (kind == SK_HDMI) {
            int id = -1;
            if (value[0] && strcasecmp(value, "screen") && strcasecmp(value, "auto")) {
                id = -2;
                for (int i = 0; i < VID_HDMI_COUNT; i++) {
                    if (!strcasecmp(value, video_hdmi_name(i))) {
                        id = i;
                    }
                }
                if (id == -2) {
                    snprintf(why, n, "no such picture mode (the names the screen offers, or screen)");
                    return 1;
                }
            }
            if (!(g_env.picture_set && g_env.picture_set(g_env.ctx, id, "saved"))) {
                snprintf(why, n, "%s", nv.note);
                return 1;
            }
            return 0;
        }
        int wr = hc_wifi_row_of(key);
        if (wr == NS_RADIO || wr == NS_DHCP) {
            bool want;
            if (!parse_bool(value, &want)) {
                snprintf(why, n, "on or off");
                return 1;
            }
            HField f;
            field(key, &f);
            if (!f.enabled) {
                snprintf(why, n, "does nothing now");
                return 1;
            }
            if (f.on != want) {
                return field_step(key, 1, why, n);
            }
            return 0;
        }
        if (wr >= NS_IP) {
            if (hc_wifi_set_addr(*g_env.wifi, wr, value, why, n)) {
                return 1;
            }
            if (g_env.history) g_env.history(g_env.ctx, value);
            touched();
            return 0;
        }
        snprintf(why, n, "no such setting");
        return 1;
    }

    bool text(const char *what, char *out, int n) override
    {
        out[0] = 0;
        if (!strcmp(what, "path")) {
            if (nv.view != HCV_BROWSE) return false;
            snprintf(out, n, "%s", nv.bpath);
            return true;
        }
        if (!strcmp(what, "bpath")) {                       // the browser's folder, whatever the view
            snprintf(out, n, "%s", nv.bpath);
            return true;
        }
        if (!strcmp(what, "crumb")) {
            if (nv.view < HCV_COLORS) {
                snprintf(out, n, "Host / %s", hc_nav_title(nv.view));
            } else {
                snprintf(out, n, "Host / Colour test");
            }
            return true;
        }
        if (!strcmp(what, "footer")) {
            const char *sf = g_env.footer ? g_env.footer(g_env.ctx) : 0;
            const char *f = sf ? sf : (nv.view == HCV_MENU ? hc_menu_foot() : (nv.view == HCV_EDIT ? hc_edit_foot() : nv.view < HCV_COLORS ? hc_foot_of_view(nv.view) : ""));
            snprintf(out, n, "%s", f ? f : "");
            return true;
        }
        if (!strcmp(what, "note")) {
            snprintf(out, n, "%s", nv.note);
            return true;
        }
        if (!strcmp(what, "countdown")) {
            if (!hc_cd_running()) return false;
            int left = hc_cd_left_ms(hc_now_ms()) / 1000;
            snprintf(out, n, "%d", left < 0 ? 0 : left);
            return true;
        }
        if (!strcmp(what, "chips")) {
            snprintf(out, n, "%s%s%s", hc_chip_net_text(), c_media[0] ? " | " : "", c_media);
            return true;
        }
        if (!strcmp(what, "cursor")) {
            if (nv.view != HCV_SET) return false;
            if (nv.cm_mode != HCM_OFF) return false;
            if (nv.wm_mode) {
                snprintf(out, n, "%s", hc_wifi_field_name(nv.nsel < NS_N ? nv.nsel : 0));
                return true;
            }
            int kinds[HC_SET_SLOTS], cnt = hc_set_kinds(kinds);
            snprintf(out, n, "%s", hc_set_key_name(kinds[nv.ssel < cnt ? nv.ssel : 0]));
            return true;
        }
        if (!strcmp(what, "detail")) {
            if (nv.bsel < 0 || nv.bsel >= nv.bn) return false;
            char sz[24];
            ui_size_text(sz, sizeof(sz), nv.bl[nv.bsel].size64);
            if (nv.bl[nv.bsel].dir) {
                snprintf(out, n, "%s (folder)", nv.bl[nv.bsel].name);
            } else {
                const CorInfo *ci = 0;
                if (hc_browse_probed_cor(&ci)) {
                    snprintf(out, n, "%s v%s, %s", ci->name, ci->version, sz);
                } else {
                    snprintf(out, n, "%s, %s", nv.bl[nv.bsel].name, sz);
                }
            }
            return true;
        }
        return false;
    }

    int browse_goto(const char *path, char *why, int n) override
    {
        if (!hc_browse_goto(path, why, n)) {
            return 1;
        }
        touched();
        return 0;
    }

    // ---- T192 D21: the arm of a destructive row action, for the API. The code that arms and that runs is the board's own key's (entry_remove_arm, browse_delete_arm,
    // wf_forget_try_arm; the *_run functions); this part only gets the screen to the right place and keeps the token.
    int arm(int act, int row, const char *ident, uint32_t *token, char *why, int n) override
    {
        hc_countdown_stop();                                   // an API ask is activity like a local key: the auto-boot countdown would boot away and drop the arm
        if (hv_state()->pending) {
            snprintf(why, n, "a picture change waits for a key");
            return HARM_BUSY;
        }
        if (nv.br_mode != HCB_NORMAL || hc_fo_replace_pending() || nv.cm_mode != HCM_OFF) {   // go() would end these silently: a target pick, a pending question, the ask-twice state
            snprintf(why, n, "a pick, a question or a confirmation of the screen is open");
            return HARM_BUSY;
        }
        hc_arm_drop(HARM_END_GONE);                            // an arm of the board's own keys gives way
        touched();
        switch (act) {
        case HARM_ENTRY_REMOVE:
            if (row < 0 || row >= cfg.n || strcmp(cfg.e[row].name, ident)) {
                snprintf(why, n, "that entry is not there any more");
                return HARM_REFUSED;
            }
            if (nv.view != HCV_EDIT) {
                hc_ui_go(HCV_EDIT);
            }
            hc_nav_set_sel(row);
            hc_nav_edit_first(cfg.n);
            hc_ui_entry_remove_arm(nv.sel);
            break;
        case HARM_FILE_DELETE: {
            if (nv.view != HCV_BROWSE) {
                hc_ui_go(HCV_BROWSE);
            }
            int at = -1;
            for (int i = 0; i < nv.bn; i++) {
                if (!strcmp(nv.bl[i].name, ident)) {
                    at = i;
                }
            }
            if (at < 0) {
                snprintf(why, n, "that file is not there any more");
                return HARM_REFUSED;
            }
            hc_nav_set_bsel(at);
            hc_note_clear();
            char path[200];
            const char *warn;
            int c = hc_ui_delete_check(path, sizeof(path), &warn);
            if (c == 1) {
                snprintf(why, n, "%s", hc_browse_at_media() ? "a medium is not deleted here" : "the parent folder row cannot be deleted");
                return HARM_REFUSED;
            }
            if (c == 2) {
                hc_ui_note(warn, HCT_WARN);
                snprintf(why, n, "%s", warn);
                return HARM_REFUSED;
            }
            hc_ui_delete_arm(path);
            break;
        }
        case HARM_WIFI_FORGET:
            if (!(nv.view == HCV_SET && nv.wm_mode)) {
                hc_ui_go(HCV_SET);
                hc_nav_wifi_open();
            }
            wifi_net_status(&hc_status().wifi);                      // the reason must be the module's state now, not the last look
            if (const char *reason = hc_ui_wifi_forget_arm()) {
                snprintf(why, n, "%s", reason);
                return HARM_REFUSED;
            }
            break;
        default:
            snprintf(why, n, "no such action");
            return HARM_REFUSED;
        }
        *token = hc_arm().token;
        return HARM_ARMED;
    }

    // The arm's note is taken away when it is still the line on the screen.
    static void arm_note_off(void)
    {
        if (hc_arm().act == AR_WIFI) {
            if (!strcmp(nv.nnote, hc_arm().note)) hc_nnote_clear();
        } else if (!strcmp(nv.note, hc_arm().note)) {
            hc_note_clear();
        }
    }

    bool confirm(uint32_t token, char *why, int n) override
    {
        if (!hc_arm().act || hc_arm().token != token) {
            snprintf(why, n, "Not done: the screen is not armed for it any more");
            return false;
        }
        const int act = hc_arm().act, row = hc_arm().row;
        char name[sizeof(hc_arm().name)];
        snprintf(name, sizeof(name), "%s", hc_arm().name);
        arm_note_off();
        hc_arm_quiet();
        touched();
        if (act == AR_ENTRY && nv.view == HCV_EDIT && row >= 0 && row < cfg.n && !strcmp(cfg.e[row].name, name)) {
            hc_nav_set_sel(row);
            hc_ui_entry_remove_run(nv.sel);
            return true;
        }
        if (act == AR_FILE && nv.view == HCV_BROWSE && nv.br_mode == HCB_NORMAL && !hc_fo_replace_pending() && row >= 0 && row < nv.bn && !strcmp(nv.bl[row].name, name)) {
            char path[200];
            const char *warn;
            hc_nav_set_bsel(row);
            if (hc_ui_delete_check(path, sizeof(path), &warn) == 0) {
                hc_ui_delete_run(path);
                return true;
            }
        }
        if (act == AR_WIFI && nv.view == HCV_SET && nv.wm_mode) {
            return hc_wifi_forget_confirm(*g_env.wifi, why, n);
        }
        snprintf(why, n, "Not done: the row is not there any more");
        return false;
    }

    void disarm(uint32_t token) override
    {
        if (!hc_arm().act || hc_arm().token != token) {
            return;
        }
        arm_note_off();
        hc_arm_quiet();
        touched();
    }

    bool armed(uint32_t token) override { return hc_arm().act && hc_arm().token == token; }
};

HcModel g_model;

}  // namespace

HostModel *hc_model(void) { return &g_model; }
