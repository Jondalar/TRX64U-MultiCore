// What a key or an intent does on the host's screen (host_core_ui.h): the code of hostui.cc's on_key and of the functions it called, moved with its conditions, texts and order
// unchanged. The screen's own work (drawing, the flows' pages, the picture mode, the start page) comes through HcUiEnv.
#include "host_core_ui.h"
#include "host_core_nav_rw.h"
#include "hostvideo.h"
#include "flashlock.h"
#include "ftpstate.h"
#include "discover.h"
#include "cfw.h"
#include "txu.h"
#include "host_core_flow.h"
#include "cor.h"
#include "keyboard.h"
#include "host_core_cfgov.h"
#include <stdio.h>
#include <string.h>

namespace {

HcUiEnv g_env;
bool g_changed;
const int WEB_ARG_NONE = -0x7fffffff;
int g_web_arg = WEB_ARG_NONE;          // set by the web page just before its video / countdown intent; used once

bool env_busy(void) { return g_env.busy && g_env.busy(g_env.ctx); }

const BootCfg &cfg(void) { return hc_cfg(); }

}  // namespace

void hc_ui_init(const HcUiEnv *e)
{
    g_env = *e;
}

void hc_ui_changed(void) { g_changed = true; }
void hc_ui_web_arg(int a) { g_web_arg = a; }
static bool g_remote_ask;
static char g_web_cfg_path[200], g_web_cfg_name[CONFIG_NAME_MAX + 1];
void hc_ui_web_config(const char *path, const char *name)
{
    snprintf(g_web_cfg_path, sizeof(g_web_cfg_path), "%s", path ? path : "");
    snprintf(g_web_cfg_name, sizeof(g_web_cfg_name), "%s", name ? name : "");
}
bool hc_ui_remote_ask(void) { return g_remote_ask; }
const HcUiEnv &hc_ui_env(void) { return g_env; }

// ---- notes -----------------------------------------------------------------------------------------------------------------------------------
void hc_ui_note(const char *text, int tone)
{
    g_changed = true;
    hc_note_set(text, tone);
    if (g_env.note_out) {
        g_env.note_out(g_env.ctx, hc_nav().note, tone, false);
    }
}

void hc_ui_net_note(const char *text, int tone)
{
    hc_nnote_set(text, tone);
    if (g_env.note_out) {
        g_env.note_out(g_env.ctx, hc_nav().nnote, tone, true);
    }
}

void hc_ui_cfg_after(int r)
{
    g_changed = true;
    if (r == HCR_SAVE_FAILED) {
        hc_ui_note("cannot save Flash:/boot.cfg", HCT_BAD);
    }
}

void hc_ui_refuse(int cor_res, const CorInfo &ci)
{
    char t[160];
    int tone;
    hc_fo_refuse((cor_result_t)cor_res, ci, t, sizeof(t), &tone);
    hc_ui_note(t, tone);
}

void hc_ui_refuse_full(int cfw_res)
{
    char t[160];
    int tone;
    hc_fo_refuse_full((cfw_result_t)cfw_res, t, sizeof(t), &tone);
    hc_ui_note(t, tone);
}

// ---- the view -------------------------------------------------------------------------------------------------------------------------------
void hc_ui_go(int view)
{
    if (hc_cfgov_is_open()) {
        hc_cfgov_close();
        if (g_env.wifi && g_env.wifi->pick_end) {
            g_env.wifi->pick_end(g_env.wifi->ctx);
        }
    }
    hc_fo_replace_drop();                     // T139: a pending question ends with the view
    hc_nav_go(view);
    if (g_env.show) {
        g_env.show(g_env.ctx, view);
    }
}

int hc_ui_ctx(void)
{
    const HcNav &v = hc_nav();
    if (hv_state()->pending) {
        return HCX_PICTURE;
    }
    if (hc_cfgov_is_open()) {
        return HCX_CFG;
    }
    if (v.view == HCV_SET && v.pm_mode) {
        return HCX_PICMODES;
    }
    switch (v.view) {
    case HCV_MENU: return HCX_MENU;
    case HCV_BROWSE: return hc_fo_replace_pending() ? HCX_BROWSE_ASK : v.br_mode == HCB_INSPECT ? HCX_BROWSE_INSPECT : v.br_mode == HCB_PICK ? HCX_BROWSE_PICK : HCX_BROWSE;
    case HCV_EDIT: return HCX_EDIT;
    case HCV_SET: return v.cm_mode == HCM_ASK1 ? HCX_CORES_ASK1 : v.cm_mode == HCM_ASK2 ? HCX_CORES_ASK2 : v.cm_mode != HCM_OFF ? HCX_CORES : v.wm_mode ? HCX_WIFI : HCX_SET;
    case HCV_NET: return HCX_NET;
    case HCV_COLORS: return HCX_COLORS;
    }
    return HCX_MENU;
}

// What a key may change; a key that leaves it as it was does not rebuild the view.
unsigned hc_ui_fingerprint(void)
{
    const HcNav &v = hc_nav();
    return (unsigned)v.view * 7 + (unsigned)v.sel * 131 + (unsigned)v.bsel * 17 + (unsigned)cfg().n * 977 + (unsigned)(cfg().def + 1) * 31 +
           (unsigned)cfg().countdown * 3571 + (unsigned)v.bn * 53 + (unsigned)strlen(v.bpath) * 11 + (unsigned)v.note[0] + (unsigned)v.cpat +
           (unsigned)(hc_arm_row() + 1) * 19 + (unsigned)v.br_mode * 23 + (unsigned)hc_fo_replace_pending() * 29 + (unsigned)v.ssel * 41 +
           (unsigned)hc_cfgov_is_open() * 43 + (unsigned)hc_cfgov().cur * 47 + (unsigned)hc_cfgov().ask * 59 + (unsigned)hc_cfgov().l.n * 61 + (unsigned)strlen(hc_cfgov().msg) * 67 + (unsigned)v.pm_mode * 71 + (unsigned)hc_ui_pm().sel * 73;
}

// ---- the browser's pick, delete and add ----------------------------------------------------------------------------------------------------
namespace {

HcFoRow fo_row(void)
{
    const HcNav &v = hc_nav();
    HcFoRow r;
    r.valid = !hc_browse_at_media() && v.bsel >= 0 && v.bsel < v.bn;
    r.dir = v.bpath;
    r.name = r.valid ? v.bl[v.bsel].name : "";
    r.is_dir = r.valid && v.bl[v.bsel].dir;
    return r;
}

void pick_start(bool move)
{
    const HcNav &v = hc_nav();
    char path[200];
    bool media = hc_browse_at_media();
    if (hc_fo_pick_start(*g_env.fo, move, media, !media && hc_browse_sel_file(path, sizeof(path)) ? path : 0, v.bpath, v.bsel)) {
        hc_nav_set_br_mode(HCB_PICK);
        hc_note_clear();
        g_changed = true;
    }
}

// DEL in the browser; armed: the row the previous key armed (-1 = none).
void browse_delete(int armed)
{
    char path[200];
    const char *warn;
    int c = hc_ui_delete_check(path, sizeof(path), &warn);
    if (c == 1) {
        return;
    }
    g_changed = true;
    if (c == 2) {
        hc_ui_note(warn, HCT_WARN);
        return;
    }
    if (armed != hc_nav().bsel) {
        hc_ui_delete_arm(path);
        return;
    }
    if (hc_arm_yes()) {
        hc_ui_delete_run(path);
    }
}

}  // namespace

void hc_ui_pick_end(void)
{
    hc_nav_set_br_mode(HCB_NORMAL);
    hc_fo_replace_drop();
    hc_browse_set_path(hc_fo_pick_back());
    hc_browse_load();
    hc_nav_set_bsel(hc_fo_pick_bsel());
    hc_nav_bsel_clamp();                              // after a move the neighbour moved up into the row
}

void hc_ui_pick_confirm(void)
{
    g_changed = true;
    hc_fo_pick_confirm(*g_env.fo, hc_browse_at_media(), hc_nav().bpath);
}

// DEL on a browser row, the checks the first and the second DEL share. 0: go on (`path` is the row's); 1: nothing to do, the key says nothing; 2: refused, `*warn` is the note.
int hc_ui_delete_check(char *path, int pcap, const char **warn) { return hc_fo_delete_check(fo_row(), path, pcap, warn); }

// The first DEL: arms the row and says what the second one does.
void hc_ui_delete_arm(const char *path)
{
    const HcNav &v = hc_nav();
    char t[160];
    hc_fo_delete_arm_text(fo_row(), path, t, sizeof(t));
    hc_arm_register(AR_FILE, v.bsel, v.bl[v.bsel].name, t);
    hc_ui_note(t, HCT_WARN);
}

// The second DEL: the row is deleted.
void hc_ui_delete_run(const char *path) { hc_fo_delete_run(*g_env.fo, fo_row(), path); }

void hc_ui_browse_add(void)
{
    char path[200];
    if (!hc_browse_sel_bundle(path, sizeof(path))) {
        return;
    }
    HcBrowseProbe pr;
    hc_browse_probe(&pr);                               // the manifest of the selection (the panel's probe, once per row)
    hc_fo_browse_add(*g_env.fo, path, hc_is_full(path), (cfw_result_t)hc_browse_cw_res(), hc_browse_cw(), (cor_result_t)hc_browse_ci_res(), hc_browse_ci());
}

// T152 D8: the File Browser picks the file; RETURN on a .cfw/.ue2 runs the inspection, ESC goes back to the settings.
void hc_ui_inspect_pick_start(void)
{
    hc_ui_go(HCV_BROWSE);
    hc_nav_set_br_mode(HCB_INSPECT);
    hc_ui_note("Pick a .cfw or .ue2 and press RETURN. ESC goes back to the settings.", HCT_HOST);
}

// ---- the entries ---------------------------------------------------------------------------------------------------------------------------------
// DEL in the editor on row `s`: the first one arms it, the second one removes it.
void hc_ui_entry_remove_arm(int s)
{
    char nb[96];
    snprintf(nb, sizeof(nb), "DEL again to remove %s (any other key cancels)", cfg().e[s].name);
    hc_arm_register(AR_ENTRY, s, cfg().e[s].name, nb);
    hc_ui_note(nb, HCT_WARN);
}

void hc_ui_entry_remove_run(int s)
{
    hc_ui_cfg_after(hc_entry_remove(s));
    if (g_env.lazy_reset) {
        g_env.lazy_reset(g_env.ctx);
    }
    hc_nav_sel_after_remove();
    hc_note_clear();
}

// ---- the settings -------------------------------------------------------------------------------------------------------------------------------
// dir: -1 LEFT, +1 RIGHT / RETURN
void hc_ui_settings_change(int kind, int dir)
{
    switch (kind) {
    case SK_DIRECT:
        hc_ui_cfg_after(hc_direct_toggle());
        hc_ui_note(cfg().direct ? "Direct boot on: the auto core starts at power-on" : "Direct boot off: the menu comes up first", HCT_HOST);
        break;
    case SK_AUTO:
        hc_ui_cfg_after(hc_autostart(cfg().countdown == 0, 0));
        hc_ui_note(cfg().countdown ? "Autostart on" : "Autostart off: the menu waits for a key", HCT_HOST);
        break;
    case SK_SECS: {
        int v = cfg().autostart_s + dir;
        hc_ui_cfg_after(hc_autostart(true, v < 1 ? 1 : v));
        break;
    }
    case SK_HDMI:                                        // T187 D5 (Q-D5.1): LEFT / RIGHT on the closed row do nothing, RETURN opens the page (settings_key)
        break;
    case SK_PROTECT:
        hc_protect_set(!flashprot_enabled());
        hc_ui_note(flashprot_enabled() ? "Flash protection on: a large core cannot write the host or its app" : "Flash protection off: a large core can write the whole flash", HCT_HOST);
        break;
    case SK_WIFI:
        if (dir > 0) {
            hc_nav_wifi_open();
        }
        break;
    case SK_FTP: {
        bool on = !ftp_state_now().on;
        hc_ftp_set(on);                                 // stored in host.cfg and effectuated (ftpd.cc follows its setting)
        hc_ui_note(on ? "FTP service on" : "FTP service off", HCT_HOST);
        break;
    }
    case SK_ESP:
        if (dir > 0) {
            hc_ui_inspect_pick_start();
        }
        break;
    case SK_CORES:
        if (dir > 0) {
            hc_cm_open(*g_env.cm);
        }
        break;
    case SK_REPORT:
        if (dir > 0 && g_env.report) {
            char nm[40], why[64];
            if (g_env.report(g_env.ctx, nm, sizeof(nm), why, sizeof(why))) {
                hc_set_report_name(nm);
                char m[96], dir_[48] = "";
                if (g_env.report_dir) {
                    g_env.report_dir(g_env.ctx, dir_, sizeof(dir_));
                }
                snprintf(m, sizeof(m), "Bug report written: %s/%s", dir_, nm);
                hc_ui_note(m, HCT_OK);
            } else {
                char m[96];
                snprintf(m, sizeof(m), "Bug report not written: %s", why);
                hc_ui_note(m, HCT_BAD);
            }
        }
        break;
    }
}

namespace {

void settings_key(int act, int rawk)
{
    if (hc_nav().cm_mode != HCM_OFF) {
        hc_cm_key(*g_env.cm, act);
        g_changed = true;
        return;
    }
    if (hc_nav().pm_mode) {
        hc_ui_pm_key(act);
        g_changed = true;
        return;
    }
    if (hc_nav().wm_mode) {
        hc_ui_wifi_key(act, rawk);
        g_changed = true;
        return;
    }
    int kinds[HC_SET_SLOTS], n = hc_set_kinds(kinds);
    if (hc_nav_settings_act(act, n)) {
    } else if (kinds[hc_nav().ssel] == SK_HDMI && act == HCA_SET_ACTIVATE) {
        hc_ui_pm_open();
    } else if (act == HCA_SET_DEC || act == HCA_SET_INC || act == HCA_SET_ACTIVATE) {
        hc_ui_settings_change(kinds[hc_nav().ssel], act == HCA_SET_DEC ? -1 : 1);
    }
    g_changed = true;
}

}  // namespace

// The first F of "forget the stored access points": arms and says so, or bails with the reason the page gives (no module, nothing to forget now). Returns that reason, null when armed.
const char *hc_ui_wifi_forget_arm(void)
{
    if (const char *why = hc_wifi_unavailable(hc_status().wifi)) {
        hc_ui_net_note(why, HCT_WARN);
        return why;
    }
    static const char text[] = "F again forgets all stored access points (any other key cancels)";
    hc_arm_register(AR_WIFI, 0, "", text);
    hc_ui_net_note(text, HCT_WARN);
    return 0;
}

// The keys of the WiFi page of the settings (F8).
void hc_ui_wifi_key(int act, int rawk)
{
    int armed = hc_arm_take_wifi();
    hc_nnote_clear();
    if (act == HCA_BACK) {
        hc_nav_wifi_close();
        return;
    }
    if (hc_nav_wifi_act(act, NS_N)) {
    } else {
        HcJob *job = 0;
        int wr = hc_wifi_key(*g_env.wifi, act, hc_nav().nsel, rawk, armed != 0, &job);
        if (wr == HCW_FORGET_ARM) {
            hc_ui_wifi_forget_arm();
        } else if (wr == HCW_JOB && job) {
            g_env.job(g_env.ctx, job);                       // the access point list, the entry fields: modal pages the screen steps
        }
    }
    g_changed = true;
}

// ---- the Picture page (T187 D5, GitHub #50) ---------------------------------------------------------------------------------------------------
// F8 > Picture and the V key open the page of the modes the screen names (the rows, texts, cursor and keys are picmodes.cc, the core's page too). Browsing applies
// nothing; RETURN on another row puts the mode up through the one path (picture_set -> hv_user_set) and starts the 8-second question.
static PicPage g_pp;
static int g_pm_list[VID_HDMI_COUNT];                   // the resolution of row n+1, as the page was read

static void pm_fill(PictureModes *pm)
{
    const HvState *hs = hv_state();
    memset(pm, 0, sizeof(*pm));
    int n = hv_list(hs->mask, hs->def_sys, g_pm_list);
    pm->n = 1 + n;
    pm->no_edid = !hs->have_edid || hs->mask == EDID_LIST_NONE;       // no EDID, a corrupt one, or one that names none of our rows: the television rows are listed
    int choice = hs->pending ? hs->pend_def : hs->def_hdmi;
    for (int i = 0; i < n; i++) {
        if (g_pm_list[i] == choice) {
            pm->cur = i + 1;
        }
    }
    int now = hc_picture_hdmi();
    int a = hv_auto();
    char nm[48];
    snprintf(nm, sizeof(nm), a >= 0 ? "Auto (%s from the screen)" : "Auto", video_hdmi_name(a));
    picmodes_set_row(pm, 0, nm, a, hs->mask, hs->native, hs->def_sys, now, pm->cur == 0);
    for (int i = 0; i < n; i++) {
        picmodes_set_row(pm, i + 1, video_hdmi_name(g_pm_list[i]), g_pm_list[i], hs->mask, hs->native, hs->def_sys, now, pm->cur == 0);
    }
}

const PicPage &hc_ui_pm(void) { return g_pp; }

void hc_ui_pm_refresh(bool keep)
{
    PictureModes pm;
    pm_fill(&pm);
    picpage_refresh(&g_pp, &pm, keep);
    g_changed = true;
}

void hc_ui_pm_open(void)
{
    PictureModes pm;
    pm_fill(&pm);
    picpage_open(&g_pp, &pm);
    hc_nav_pm_open();
}

void hc_ui_pm_key(int act)
{
    int pk = act == HCA_CURSOR_UP ? PMK_UP : act == HCA_CURSOR_DOWN ? PMK_DOWN : act == HCA_PM_APPLY ? PMK_OK : act == HCA_BACK ? PMK_BACK : PMK_OTHER;
    switch (pk == PMK_OTHER ? PMA_NONE : picpage_key(&g_pp, pk)) {
    case PMA_CLOSE:
        hc_nav_pm_close();
        break;
    case PMA_APPLY:
        if (g_env.picture_set) {
            int hdmi = g_pp.sel == 0 ? -1 : g_pm_list[g_pp.sel - 1];
            g_env.picture_set(g_env.ctx, hdmi, "saved");
        }
        hc_ui_pm_refresh(true);
        break;
    default:
        break;
    }
}

// V: opens the Picture page, from any view (the footer's "V picture"). T187 D5 replaces the blind step through the modes.
bool hc_ui_picture_step(void)
{
    if (hc_nav().view != HCV_SET) {
        hc_ui_go(HCV_SET);
    }
    hc_ui_pm_open();
    return true;
}

// ---- one input ---------------------------------------------------------------------------------------------------------------------------------

// ---- the config overlay (T198) -----------------------------------------------------------------------------------------------------------------
void hc_ui_cfgov_draw(bool fresh)
{
    if (g_env.wifi && g_env.wifi->draw_pick && hc_cfgov_is_open()) {
        HcWifiPick p;
        hc_cfgov_pick(&p);
        p.keys = hc_foot(hc_cfgov().cur == 0 ? HCF_CFG_PLAIN : HCF_CFG);
        g_env.wifi->draw_pick(g_env.wifi->ctx, p, fresh);
    }
}

namespace {
void cfgov_close(void)
{
    hc_cfgov_close();
    if (g_env.wifi && g_env.wifi->pick_end) {
        g_env.wifi->pick_end(g_env.wifi->ctx);
    }
}

// RIGHT on a core row (F1) or on a .cor (F3): the overlay opens when the core has configs; otherwise nothing happens (D9).
void cfgov_open(int ctx, HcUiResult *res)
{
    const HcNav &v = hc_nav();
    char path[200];
    char title[BOOT_NAME_MAX + 1];
    int entry = -1;
    if (ctx == HCX_MENU) {
        if (v.sel < 0 || v.sel >= cfg().n) {
            return;
        }
        entry = v.sel;
        snprintf(path, sizeof(path), "%s", cfg().e[entry].path);
        snprintf(title, sizeof(title), "%s", cfg().e[entry].name);
    } else {
        if (hc_browse_at_media() || !hc_browse_sel_bundle(path, sizeof(path))) {
            return;
        }
        entry = hc_in_menu(path);
        snprintf(title, sizeof(title), "%s", entry >= 0 ? cfg().e[entry].name : v.bl[v.bsel].name);
        char *dot = strrchr(title, '.');
        if (entry < 0 && dot) {
            *dot = 0;
        }
    }
    if (hc_cfgov_open(path, title, entry, ctx != HCX_MENU)) {
        hc_ui_cfgov_draw(true);
        res->redraw = true;
        g_changed = true;
    }
}

// The keys of the overlay. True: handled here; false: the key is a global one (F keys, V) and goes on.
bool cfgov_input(int act, HcUiResult *res)
{
    if (act != HCA_CONFIG_DELETE) {
        hc_cfgov_disarm();
    }
    switch (act) {
    case HCA_CURSOR_UP: hc_cfgov_move(-1); break;
    case HCA_CURSOR_DOWN: hc_cfgov_move(+1); break;
    case HCA_BACK:
        cfgov_close();
        res->redraw = true;
        g_changed = true;
        return true;
    case HCA_CONFIG_DEFAULT:
        hc_cfgov_default();
        break;
    case HCA_CONFIG_DELETE:
        if (hc_cfgov_delete() == 2) {
            hc_ui_changed();
        }
        break;
    case HCA_CONFIG_RENAME:
        if (hc_cfgov().cur > 0) {
            g_env.flow(g_env.ctx, HUF_CFGRENAME, "", -2);
        }
        return true;
    case HCA_CONFIG_START: {
        char path[200], name[CONFIG_NAME_MAX + 1];
        const HcCfgOv &o = hc_cfgov();
        const int entry = o.entry;
        snprintf(path, sizeof(path), "%s", o.path);
        snprintf(name, sizeof(name), "%s", hc_cfgov_cur_name());
        char title[96];
        snprintf(title, sizeof(title), "%s", o.title);
        cfgov_close();
        res->redraw = true;
        g_changed = true;
        hc_start_config_set(name);
        if (entry >= 0 && entry < cfg().n && hc_startable(entry)) {
            g_env.start_any(g_env.ctx, cfg().e[entry].path, cfg().e[entry].name, &cfg().e[entry], true);
        } else if (entry < 0) {
            g_env.start_core(g_env.ctx, path, title);
        }
        hc_start_config_set("");                      // a start that came back (refused, aborted) leaves nothing armed
        return true;
    }
    default:
        return false;
    }
    hc_ui_cfgov_draw(false);
    res->redraw = true;
    g_changed = true;
    return true;
}
}

void hc_ui_input(int k, HcUiResult *res)
{
    memset(res, 0, sizeof(*res));
    g_changed = false;
    hc_countdown_stop();
    const HcNav &v = hc_nav();
    const int ctx = hc_ui_ctx();
    int act;
    const int rawk = HC_IS_ACT_KEY(k) ? -1 : k;            // the key itself, for the pages that type or ignore the repeat of the key that opened them
    if (HC_IS_ACT_KEY(k)) {
        act = HC_ACT_OF_KEY(k);
        if (act == HCA_BACK) {
            act = hc_back_act(ctx);                         // the intent "back" is whatever ESC does on this page
        }
        if (!hc_act_in_ctx(ctx, act)) {
            act = HCA_NONE;                                 // no key reaches it here: it does nothing here
        }
    } else {
        act = hc_resolve(ctx, k);
    }
    if (ctx == HCX_PICTURE && act != HCA_PICTURE_REVERT) {
        act = HCA_PICTURE_KEEP;                             // Q7: the first input of any kind keeps the picture the question was about; it is spent on that (LEFT / ESC goes back, Q-D5.2)
    }
    if (act == HCA_PICTURE_REVERT && hv_revert()) {
        if (g_env.picture_event) {
            g_env.picture_event(g_env.ctx, HV_REVERTED);
        }
        if (hc_nav().pm_mode) {
            hc_ui_pm_refresh(true);                         // the cursor stays on the rejected mode, "now" is back on the old one
        }
        res->redraw = true;
        g_changed = true;
        hc_arm_clear_keys();
        HcKeyArm spent;
        return;
    }
    if (act == HCA_PICTURE_KEEP && hc_picture_keep()) {
        if (g_env.pq_hide) {
            g_env.pq_hide(g_env.ctx);
        }
        hc_ui_note("Picture kept", HCT_OK);
        if (hc_nav().pm_mode) {
            hc_ui_pm_refresh(true);
        }
        res->redraw = true;
        g_changed = true;
        hc_arm_clear_keys();                // the key is spent on the picture: it ends any arm like every other non-confirming key (an arm survived it before; same on main)
        HcKeyArm spent;
        return;
    }
    if (ctx == HCX_CFG && cfgov_input(act, res)) {
        return;
    }
    if (act == HCA_CONFIG_LIST && ctx == HCX_BROWSE && hc_nav().bsel < hc_nav().bn && hc_nav().bl[hc_nav().bsel].dir) {
        act = HCA_BROWSE_OPEN;              // GitHub #90 (T219): RIGHT on a folder goes in, as RETURN does; ".." goes up like RETURN
    } else if (act == HCA_CONFIG_LIST && (ctx == HCX_MENU || ctx == HCX_BROWSE)) {
        cfgov_open(ctx, res);
        return;
    }
    int armed = hc_arm_take_row();          // T101 D6: only an unbroken DEL, DEL removes
    if (armed < 0 && ctx == HCX_EDIT && v.note[0]) {
        hc_note_clear();                        // the editor's note (Renamed to ...) lasts until the next key; this key's own outcome sets a new one after this
        g_changed = true;
    }
    HcKeyArm key_arm;                       // T192 D21: the arm ends with this key (yes when it is the confirming one)
    switch (act) {
    case HCA_NAV_MENU: hc_ui_go(HCV_MENU); res->redraw = true; return;
    case HCA_NAV_BROWSE: hc_ui_go(HCV_BROWSE); res->redraw = true; return;
    case HCA_NAV_EDIT: hc_ui_go(HCV_EDIT); res->redraw = true; return;
    case HCA_NAV_NET: hc_ui_go(HCV_NET); res->redraw = true; return;
    case HCA_NAV_SETTINGS: hc_ui_go(HCV_SET); res->redraw = true; return;       // SHIFT+F7 on the C64 keyboard
    // The old web page's commands (T106), as intents of the service: from any view, the screen stays where it is. The answer the page shows is set here.
    case HCA_WEB_START:
        if (v.sel >= 0 && v.sel < cfg().n && hc_startable(v.sel)) {
            if (g_env.web_result) {
                g_env.web_result(g_env.ctx, cfw_is_name(cfg().e[v.sel].path) ? "start requested; foreign firmware asks first (answer on the screen or by the service)" : "starting");
            }
            g_remote_ask = true;                         // the question belongs to a remote client: it answers no after 60 s
            if (g_web_cfg_name[0] && !strcmp(g_web_cfg_path, cfg().e[v.sel].path)) {
                hc_start_config_set(g_web_cfg_name);     // armed for this entry's start only; start_any is synchronous (the question included)
            }
            g_web_cfg_name[0] = 0;
            g_env.start_any(g_env.ctx, cfg().e[v.sel].path, cfg().e[v.sel].name, &cfg().e[v.sel], true);      // D18/D19a: the same question as RETURN, a service question; nothing starts before yes
            hc_start_config_set("");                     // answered no or refused: nothing stays armed
            g_remote_ask = false;
        } else {
            g_web_cfg_name[0] = 0;
            if (g_env.web_result) {
                g_env.web_result(g_env.ctx, "that entry cannot be started");
            }
        }
        res->redraw = true;
        return;
    case HCA_WEB_DEFAULT:
        if (v.sel >= 0 && v.sel < cfg().n) {
            hc_ui_cfg_after(hc_entry_default(v.sel));
            if (g_env.web_result) {
                g_env.web_result(g_env.ctx, "default changed");
            }
        }
        res->redraw = true;
        return;
    case HCA_WEB_VIDEO: {
        if (g_web_arg == WEB_ARG_NONE) {                 // no argument was handed over (a client other than the web page): nothing to apply
            return;
        }
        int arg = g_web_arg;
        g_web_arg = WEB_ARG_NONE;
        bool pic = g_env.picture_set && g_env.picture_set(g_env.ctx, arg, "from the network");
        if (g_env.web_result) {
            g_env.web_result(g_env.ctx, pic ? (hv_state()->pending ? "picture changed, kept after a key on the host within 8 s" : "picture changed")
                                            : "picture not changed: the screen does not list it");
        }
        res->redraw = true;
        return;
    }
    case HCA_WEB_COUNTDOWN: {
        if (g_web_arg == WEB_ARG_NONE) {
            return;
        }
        int secs = g_web_arg;
        g_web_arg = WEB_ARG_NONE;
        hc_ui_cfg_after(hc_autostart(secs > 0, secs));
        if (g_env.web_result) {
            g_env.web_result(g_env.ctx, "countdown changed");
        }
        res->redraw = true;
        return;
    }
    // The way back from a black screen: V opens the Picture page from any view (hc_ui_picture_step); RETURN or SPACE on a mode applies it and asks "Keep this picture?" (T105, T187 D5).
    case HCA_PICTURE_STEP:
        hc_arm_take_wifi();                 // this key returns before the WiFi page's keys: it ends the F arm like any other key (the arm's token ends in HcKeyArm)
        hc_ui_picture_step();
        res->redraw = true;
        return;
    default:
        break;
    }
    bool selmove = false;
    int sel0 = v.sel, bsel0 = v.bsel;
    // a key that changes nothing must not rebuild the view (with one core, UP/DOWN do nothing at all)
    unsigned fp0 = hc_ui_fingerprint();
    char bp0[160];
    strcpy(bp0, v.bpath);
    char path[200];
    switch (ctx) {
    case HCX_MENU:
        if (int nr = hc_nav_act(ctx, act)) { selmove = (nr & HCN_SELMOVE) != 0; }
        else if (act == HCA_ENTRY_START) {
            if (v.sel < 0 || v.sel >= cfg().n) {
            } else if (hc_startable(v.sel)) {
                g_env.start_any(g_env.ctx, cfg().e[v.sel].path, cfg().e[v.sel].name, &cfg().e[v.sel], true);
            } else {
                hc_ui_note("this entry cannot be started (see its badge)", HCT_WARN);
            }
        } else if (act == HCA_ENTRY_RECHECK) {
            hc_fo_recheck(*g_env.fo, v.sel);                    // R: the full check of the selected entry again, storing size, mtime and header digest anew
            if (g_env.lazy_mark) {
                g_env.lazy_mark(g_env.ctx, v.sel);
            }
        }
        break;
    case HCX_BROWSE_ASK:                                  // T139: the question "replace the file that is there?"
        hc_fo_replace_drop();
        if (g_env.ask) {
            g_env.ask(g_env.ctx, "", 0, 0);
        }
        g_changed = true;
        if (act == HCA_BROWSE_REPLACE) {
            hc_fo_replace_run(*g_env.fo);
        } else {
            hc_ui_note("Not replaced. Nothing was changed.", HCT_MUTED);
        }
        break;
    case HCX_BROWSE_INSPECT:                              // T152 D8: the browser picks a .cfw/.ue2 to look into
        if (int nr = hc_nav_act(ctx, act)) { selmove = (nr & HCN_SELMOVE) != 0; }
        else if (act == HCA_BROWSE_OPEN) {
            if (!hc_browse_at_media() && !env_busy() && hc_browse_sel_file(path, sizeof(path)) && hc_esp_pickable(v.bl[v.bsel].name)) {
                g_env.flow(g_env.ctx, HUF_INSPECT, path, 0);
                hc_browse_load();
            } else {
                hc_ui_note("That is not a .cfw or .ue2 file", HCT_WARN);
            }
        } else if (act == HCA_BROWSE_PICK_CANCEL) {
            hc_ui_go(HCV_SET);
            res->redraw = true;
        }
        break;
    case HCX_BROWSE_PICK:                                 // T139: the browser picks the target folder
        if (int nr = hc_nav_act(ctx, act)) { selmove = (nr & HCN_SELMOVE) != 0; }
        else if (act == HCA_BROWSE_OPEN) { hc_ui_pick_confirm(); }
        else if (act == HCA_BROWSE_PICK_CANCEL) {
            hc_ui_pick_end();
            hc_ui_note("Cancelled. Nothing was changed.", HCT_MUTED);
        }
        break;
    case HCX_BROWSE:
        if (int nr = hc_nav_act(ctx, act)) { selmove = (nr & HCN_SELMOVE) != 0; }
        else if (act == HCA_BROWSE_DELETE) {
            browse_delete(armed);
        } else if (act == HCA_BROWSE_COPY) {
            pick_start(false);
        } else if (act == HCA_BROWSE_MOVE) {
            pick_start(true);
        } else if (act == HCA_BROWSE_ADD) {
            if (hc_browse_sel_bundle(path, sizeof(path))) {       // offered for the types the boot menu takes, nowhere else (T111 Q1)
                hc_ui_browse_add();
            }
        } else if (act == HCA_BROWSE_CONVERT) {
            if (!hc_browse_at_media() && !env_busy() && hc_browse_sel_bundle(path, sizeof(path)) && cfw_is_name(path)) {     // T137: only a .cfw/.ue2
                g_env.flow(g_env.ctx, HUF_CONVERT, path, 0);
            }
        } else if (act == HCA_BROWSE_RESTORE) {
            if (!hc_browse_at_media() && !env_busy() && hc_browse_sel_bundle(path, sizeof(path)) && (cfw_is_name(path) || hc_is_backup_cor(path))) {     // T137: R restores
                g_env.flow(g_env.ctx, HUF_RESTORE, path, 0);
            }
        } else if (act == HCA_BROWSE_UPDATE) {
            if (!hc_browse_at_media() && !env_busy() && hc_browse_sel_file(path, sizeof(path)) && txu_is_name(v.bl[v.bsel].name)) {     // T152: U installs a .txu
                g_env.flow(g_env.ctx, HUF_UPDATE, path, 0);
            }
        } else if (act == HCA_BROWSE_UNZIP) {
            if (!hc_browse_at_media() && !env_busy() && hc_browse_sel_file(path, sizeof(path)) && hc_is_zip_name(v.bl[v.bsel].name)) {     // T197: Z unpacks a .zip
                g_env.flow(g_env.ctx, HUF_UNZIP, path, 0);
            }
        } else if (act == HCA_BROWSE_FORMAT && hc_browse_at_media() && v.bsel < v.bn) {
            g_env.flow(g_env.ctx, HUF_FORMAT, v.bl[v.bsel].name, 0);
        } else if (act == HCA_BROWSE_START) {
            if (hc_browse_sel_bundle(path, sizeof(path))) {
                HcBrowseProbe pr;
                hc_browse_probe(&pr);                    // the manifest of the selection (the panel's probe, once per row)
                if (hc_is_full(path)) {
                    if (pr.res != CFW_OK) {
                        hc_ui_refuse_full(pr.res);
                    } else {
                        g_env.start_any(g_env.ctx, path, pr.cw->name, 0, true);
                    }
                } else if (pr.res != COR_OK) {
                    hc_ui_refuse(pr.res, *pr.ci);
                } else {
                    g_env.start_core(g_env.ctx, path, pr.ci->name);
                }
            }
        }
        break;
    case HCX_EDIT:
        if (int nr = hc_nav_act(ctx, act)) { selmove = (nr & HCN_SELMOVE) != 0; }
        else if (act == HCA_ENTRY_MOVE_UP) {
            int r = hc_entry_move(v.sel, -1);
            if (v.sel > 0) hc_nav_sel_step(-1);
            hc_ui_cfg_after(r);
            g_env.lazy_reset(g_env.ctx);
        } else if (act == HCA_ENTRY_MOVE_DOWN) {
            if (v.sel + 1 < cfg().n) {
                int r = hc_entry_move(v.sel, +1);
                hc_nav_sel_step(+1);
                hc_ui_cfg_after(r);
                g_env.lazy_reset(g_env.ctx);
            }
        } else if (act == HCA_ENTRY_DEFAULT && v.sel < cfg().n) {
            hc_ui_cfg_after(hc_entry_default(v.sel));
        } else if ((act == HCA_ENTRY_DEFCFG_PREV || act == HCA_ENTRY_DEFCFG_NEXT) && v.sel < cfg().n) {
            char nx[CONFIG_NAME_MAX + 1];
            if (!hc_cfgov_cycle(cfg().e[v.sel].path, cfg().e[v.sel].defcfg, act == HCA_ENTRY_DEFCFG_PREV ? -1 : +1, nx, sizeof(nx))) {
                hc_ui_note("This core has no configs.", HCT_MUTED);
            } else {
                hc_ui_cfg_after(hc_entry_defcfg(v.sel, nx));
            }
        } else if (act == HCA_ENTRY_REMOVE && v.sel < cfg().n) {
            if (armed == v.sel) {
                if (hc_arm_yes()) {
                    hc_ui_entry_remove_run(v.sel);
                }
            } else {
                hc_ui_entry_remove_arm(v.sel);
            }
        } else if (act == HCA_ENTRY_RENAME && v.sel < cfg().n) {
            g_env.flow(g_env.ctx, HUF_RENAME, "", v.sel);
        }
        break;
    case HCX_SET:
    case HCX_WIFI:
    case HCX_PICMODES:
    case HCX_CORES:
    case HCX_CORES_ASK1:
    case HCX_CORES_ASK2:
        settings_key(act, rawk);
        break;
    case HCX_NET:
        if (act == HCA_NET_COLORS) {                         // the network page shows the state only; its one key opens the colour test
            hc_nav_colors_open();
        }
        g_changed = true;
        break;
    case HCX_COLORS:
        // stays until Esc / RUN/STOP / RETURN (or an F key, handled above): a key's auto-repeat or a stray key must not end it (the repeat of the C that opened it did)
        if (act == HCA_COLORS_REPORT) {
            g_env.colors(g_env.ctx, HUC_REPORT);
        } else if (act == HCA_COLORS_SINGLE) {
            g_env.colors(g_env.ctx, HUC_SINGLE);
        } else if (act == HCA_COLORS_MULTI) {
            g_env.colors(g_env.ctx, HUC_MULTI);
        } else if (act == HCA_COLORS_PATTERN && rawk > 0) {
            hc_nav_colors_pattern((char)rawk);
        } else if (act == HCA_COLORS_CLOSE) {
            hc_nav_colors_pattern(0);
            hc_ui_go(HCV_NET);
        }
        break;
    default:
        break;
    }
    if (armed >= 0 && hc_arm_row() < 0 && v.note[0] && (v.view == HCV_EDIT || (v.view == HCV_BROWSE && act != HCA_BROWSE_DELETE))) {
        hc_note_clear();                                 // another key than DEL: the question goes away
        g_changed = true;
        selmove = false;
    }
    if (selmove && (v.view == HCV_MENU || v.view == HCV_EDIT)) {
        if (hc_nav_window_follow()) {
            selmove = false;                         // the window scrolls: the rows are refilled
        }
    }
    unsigned fp1 = hc_ui_fingerprint();
    if (!selmove && fp0 == fp1 && !strcmp(bp0, v.bpath) && !g_changed) {
        return;                                      // nothing changed
    }
    res->selmove = selmove;
    res->sel0 = sel0;
    res->bsel0 = bsel0;
    res->redraw = !selmove;
}

// ---- rename an entry as a job ---------------------------------------------------------------------------------------------------------------------------
// The code of hostui.cc's rename_flow: modal, it returns when the person has saved or cancelled. The name goes into boot.cfg and is read back.
namespace {

class RenameJob : public HcJob {
public:
    RenameJob(const HcFlowEnv &f, const HcRenameEnv &r, int i) : HcJob(f), re(r), idx(i) { kind = 6; }

protected:
    bool run() override;

private:
    const HcRenameEnv &re;
    int idx;
    LineEdit le;
    HcRenamePage pg;
    bool save, lead;
    uint32_t t0;
    int k, rr;
    bool ok_rb;
    char m[120];

    void draw(const char *msg, bool first)
    {
        memcpy(pg.typed, le.buf, sizeof(pg.typed));
        pg.cur = le.cur;
        snprintf(pg.msg, sizeof(pg.msg), "%s", msg);
        re.draw(re.ctx, pg, first);
    }
};

bool RenameJob::run()
{
    HJ_BEGIN();
    if (idx == -2 && !(hc_cfgov_is_open() && hc_cfgov().cur > 0)) {
        return finish(false);
    }
    if (idx != -2 && (idx < 0 || idx >= hc_cfg().n)) {
        return finish(false);
    }
    // T198: idx -2 renames the config under the overlay's cursor (the name rules of configlist.h, the file renamed on its folder)
    le_init(&le, idx == -2 ? hc_cfgov_cur_name() : hc_cfg().e[idx].name, idx == -2 ? CONFIG_NAME_MAX : BOOT_NAME_MAX, "|");
    snprintf(pg.head, sizeof(pg.head), "%s", idx == -2 ? "Rename config" : "Rename menu entry");
    snprintf(pg.sub, sizeof(pg.sub), "%s", idx == -2 ? hc_cfgov().title : hc_cfg().e[idx].path);
    draw("", true);
    save = false;
    lead = true;
    t0 = env.now(env.ctx);
    for (;;) {
        k = poll_key();
        if (k < 0) {
            env.sleep_ms(env.ctx, 20);
            HJ_YIELD();
            continue;
        }
        if (lead && (k == 'c' || k == 'C') && env.now(env.ctx) - t0 < 300) {
            continue;                                // the auto-repeat of the C that opened this is not typed into the field
        }
        lead = false;
        rr = le_key(&le, k);
        if (rr == LE_CANCEL) {
            break;
        }
        if (rr == LE_DONE) {
            if (le_blank(&le)) {
                draw("A name cannot be empty.", false);
                continue;
            }
            save = true;
            break;
        }
        draw("", false);
    }
    re.end(re.ctx);
    hc_ui_changed();
    if (idx == -2) {
        if (save) {
            hc_cfgov_rename(le.buf);                 // its message line says how it went
        }
        hc_ui_cfgov_draw(true);
        return finish(save);
    }
    if (!save) {
        hc_ui_note("Name unchanged", HCT_MUTED);
        return finish(false);
    }
    if (re.history) {
        re.history(re.ctx, le.buf);
    }
    rr = hc_entry_rename(idx, le.buf);
    if (rr == HCR_REFUSED) {
        hc_ui_note("That name cannot be used", HCT_WARN);
        return finish(false);
    }
    hc_ui_cfg_after(rr);
    ok_rb = hc_cfg_readback_ok(idx);
    snprintf(m, sizeof(m), ok_rb ? "Renamed to %s (boot.cfg read back)" : "Renamed to %s, but boot.cfg does not hold it", hc_cfg().e[idx].name);
    hc_ui_note(m, ok_rb ? HCT_OK : HCT_BAD);
    return finish(true);
    HJ_END();
}

}  // namespace

HcJob *hc_rename_job(const HcFlowEnv &flow, const HcRenameEnv &re, int entry) { return new RenameJob(flow, re, entry); }
