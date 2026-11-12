// The navigation state of the host's screen and the intents that change it (host_core_nav.h). The text of every note is the screen's, the movements are the old on_key's,
// moved here with their conditions unchanged.
#include "host_core_nav.h"
#include "host_core_nav_rw.h"
#include "host_core_flow.h"
#include "listwin.h"
#include <stdio.h>
#include <string.h>

namespace {

HcNav g_nav;
HcNavPlatform g_plat;
struct StartState {                       // the state a fresh start has, before anything runs
    StartState() { hc_nav_reset(); }
} g_start_state;
int g_mrows = 5, g_erows = 5, g_brows = 13, g_cmrows = 17;

HcArm g_arm;
uint32_t g_arm_seq;
int g_del_arm = -1;                       // T101 D6: the row DEL armed for removal, -1 = none
int g_wf_arm;                             // F pressed once on the WiFi page: a second F in a row forgets
HcKeyArm *g_cur_ka;

void plat_arm_drop(void) { hc_arm_drop(HARM_END_GONE); }

void note_copy(char *dst, const char *s)
{
    snprintf(dst, HC_NOTE_MAX, "%s", s ? s : "");
}

// "/" has no parent; a folder's parent is the path up to its last slash ("/" itself for a medium)
void path_up(char *p)
{
    char *s = strrchr(p, '/');
    if (s == p) {
        p[1] = 0;
    } else {
        *s = 0;
    }
}

}  // namespace

const HcNav &hc_nav(void) { return g_nav; }
HcNav &hc_nav_rw(void) { return g_nav; }
const HcNavPlatform &hc_nav_plat(void) { return g_plat; }

void hc_nav_geometry(int menu_rows, int browse_rows, int cores_rows, int edit_rows)
{
    g_mrows = menu_rows;
    g_erows = edit_rows > 0 ? edit_rows : menu_rows;
    g_brows = browse_rows;
    g_cmrows = cores_rows;
}
int hc_nav_menu_rows(void) { return g_mrows; }
int hc_nav_edit_rows(void) { return g_erows; }
int hc_nav_browse_rows(void) { return g_brows; }
int hc_nav_cores_rows(void) { return g_cmrows; }

void hc_nav_init(const HcNavPlatform *p)
{
    memset(&g_plat, 0, sizeof(g_plat));
    if (p) {
        g_plat = *p;
    }
}

void hc_nav_reset(void)
{
    memset(&g_nav, 0, sizeof(g_nav));
    g_nav.view = HCV_MENU;
    strcpy(g_nav.bpath, "/");
    g_nav.note_tone = HCT_MUTED;
    g_nav.nnote_tone = HCT_NONE;
    g_nav.cm_mode = HCM_OFF;
}

// ---- the arm ----------------------------------------------------------------------------------------------------------------------------------
const HcArm &hc_arm(void) { return g_arm; }

void hc_arm_register(int act, int row, const char *name, const char *note_text)
{
    if (act == AR_WIFI) {
        g_wf_arm = 1;
    } else {
        g_del_arm = row;
    }
    g_arm.act = act;
    g_arm_seq++;
    if (!g_arm_seq) {
        g_arm_seq = 1;
    }
    g_arm.token = g_arm_seq;
    g_arm.row = row;
    snprintf(g_arm.name, sizeof(g_arm.name), "%s", name ? name : "");
    snprintf(g_arm.note, sizeof(g_arm.note), "%s", note_text ? note_text : "");
}

void hc_arm_drop(int how)
{
    uint32_t tok = g_arm.act ? g_arm.token : 0;
    g_arm.act = AR_NONE;
    g_del_arm = -1;
    g_wf_arm = 0;
    if (tok && g_plat.arm_end) {
        g_plat.arm_end(tok, how);
    }
}

void hc_arm_quiet(void)
{
    g_arm.act = AR_NONE;
    g_del_arm = -1;
    g_wf_arm = 0;
}

int hc_arm_take_row(void)
{
    int r = g_del_arm;
    g_del_arm = -1;
    return r;
}
int hc_arm_take_wifi(void)
{
    int r = g_wf_arm;
    g_wf_arm = 0;
    return r;
}
int hc_arm_row(void) { return g_del_arm; }
void hc_arm_clear_keys(void)
{
    g_del_arm = -1;
    g_wf_arm = 0;
}

HcKeyArm::HcKeyArm() : prev(g_cur_ka)
{
    tok = g_arm.act ? g_arm.token : 0;
    g_arm.act = AR_NONE;
    g_cur_ka = this;
}

bool HcKeyArm::yes()
{
    if (!tok) {
        return false;
    }
    uint32_t t = tok;
    tok = 0;
    if (g_plat.arm_end) {
        g_plat.arm_end(t, HARM_END_YES);
    }
    return true;
}

HcKeyArm::~HcKeyArm()
{
    g_cur_ka = prev;
    if (tok && g_plat.arm_end) {
        g_plat.arm_end(tok, g_plat.key_local && g_plat.key_local() ? HARM_END_NO : HARM_END_GONE);
    }
}

bool hc_arm_yes(void) { return g_cur_ka && g_plat.key_local && g_plat.key_local() && g_cur_ka->yes(); }

// ---- the feedback lines --------------------------------------------------------------------------------------------------------------------
void hc_note_set(const char *text, int tone)
{
    note_copy(g_nav.note, text);
    g_nav.note_tone = (uint8_t)tone;
}
void hc_note_clear(void) { g_nav.note[0] = 0; }
void hc_nnote_set(const char *text, int tone)
{
    note_copy(g_nav.nnote, text);
    g_nav.nnote_tone = (uint8_t)tone;
}
void hc_nnote_clear(void) { g_nav.nnote[0] = 0; }

// ---- the browser -----------------------------------------------------------------------------------------------------------------------------
void hc_browse_load(void)
{
    plat_arm_drop();                          // the list is read anew: an arm on one of its rows does not hold
    HcNav &n = g_nav;
    n.gen++;
    n.bup = strcmp(n.bpath, "/") != 0;
    n.bn = 0;
    if (n.bup) {
        strcpy(n.bl[0].name, "..");
        n.bl[0].dir = true;
        n.bl[0].size = 0;
        n.bn = 1;
    }
    int c = g_plat.list ? g_plat.list(n.bpath, n.bl + n.bn, HC_BR_MAX - n.bn) : -1;
    if (c > 0) {
        n.bn += c;
    }
    if (n.bsel >= n.bn) {
        n.bsel = n.bn ? n.bn - 1 : 0;
    }
    if (g_plat.loaded) {
        g_plat.loaded();
    }
}

void hc_browse_up(void)
{
    path_up(g_nav.bpath);
    g_nav.bsel = 0;
    hc_browse_load();
}

void hc_browse_enter(void)
{
    HcNav &n = g_nav;
    if (!strcmp(n.bl[n.bsel].name, "..")) {
        path_up(n.bpath);
    } else {
        char np[160];
        snprintf(np, sizeof(np), "%s%s%s", n.bpath, strcmp(n.bpath, "/") ? "/" : "", n.bl[n.bsel].name);
        strcpy(n.bpath, np);
    }
    n.bsel = 0;
    hc_browse_load();
}

bool hc_browse_goto(const char *path, char *why, int cap)
{
    if (path[0] != '/') {
        snprintf(why, cap, "an absolute path, like /SD/cores");
        return false;
    }
    if (strcmp(path, "/") && !(g_plat.exists && g_plat.exists(path))) {
        snprintf(why, cap, "%s is not there", path);
        return false;
    }
    snprintf(g_nav.bpath, sizeof(g_nav.bpath), "%s", path);
    g_nav.bsel = 0;
    hc_browse_load();
    return true;
}

void hc_browse_set_path(const char *path) { snprintf(g_nav.bpath, sizeof(g_nav.bpath), "%s", path); }
void hc_nav_set_bsel(int i) { g_nav.bsel = i; }
void hc_nav_bsel_clamp(void)
{
    if (g_nav.bsel >= g_nav.bn) {
        g_nav.bsel = g_nav.bn ? g_nav.bn - 1 : 0;
    }
}
bool hc_browse_at_media(void) { return !strcmp(g_nav.bpath, "/"); }
void hc_nav_set_br_mode(int mode) { g_nav.br_mode = mode; }

// ---- go ------------------------------------------------------------------------------------------------------------------------------------
void hc_nav_go(int v)
{
    HcNav &n = g_nav;
    n.view = v;
    plat_arm_drop();
    hc_note_clear();
    n.cm_mode = HCM_OFF;                      // T148: the Cores and ROMs page ends with the view
    if (g_plat.ask_gone) {
        g_plat.ask_gone();
    }
    n.wm_mode = 0;                            // T184 D3: so does the WiFi page
    n.pm_mode = 0;                            // T187 D5: and the Picture page
    n.br_mode = HCB_NORMAL;                   // T139: a target pick or a pending question ends with the view
    if (g_plat.ask_gone) {
        g_plat.ask_gone();
    }
    if (v == HCV_BROWSE) {
        hc_browse_load();
    }
    if (v == HCV_MENU || v == HCV_EDIT) {
        int total = hc_cfg().n;
        if (n.sel >= total) {
            n.sel = total ? total - 1 : 0;
        }
    }
}

// ---- the cursor keys -------------------------------------------------------------------------------------------------------------------------
namespace {

// the browser's UP / DOWN: a note on the line ends with the key (and then the rows are redrawn: no selection-only move)
int browse_cursor(int act)
{
    HcNav &n = g_nav;
    if (act == HCA_CURSOR_UP && n.bsel > 0) {
        n.bsel--;
    } else if (act == HCA_CURSOR_DOWN && n.bsel + 1 < n.bn) {
        n.bsel++;
    } else {
        return HCN_NONE;
    }
    int r = HCN_DONE | (n.note[0] ? 0 : HCN_SELMOVE);
    n.note[0] = 0;
    return r;
}

// T180 D7: "+" moves the selection one page down, "-" one page up (a page = the rows the list shows), stopping at the ends.
int browse_page(int act)
{
    HcNav &n = g_nav;
    if ((act != HCA_BROWSE_PAGE_DOWN && act != HCA_BROWSE_PAGE_UP) || n.bn <= 0) {
        return HCN_NONE;
    }
    int t = act == HCA_BROWSE_PAGE_DOWN ? n.bsel + g_brows : n.bsel - g_brows;
    t = t >= n.bn ? n.bn - 1 : (t < 0 ? 0 : t);
    int r = HCN_DONE;
    if (t != n.bsel) {
        n.bsel = t;
        r |= n.note[0] ? 0 : HCN_SELMOVE;
        n.note[0] = 0;
    }
    return r;
}

bool row_is_dir(void) { return g_nav.bsel < g_nav.bn && g_nav.bl[g_nav.bsel].dir; }

}  // namespace

int hc_nav_act(int ctx, int act)
{
    HcNav &n = g_nav;
    switch (ctx) {
    case HCX_MENU:
        if (act == HCA_CURSOR_UP && n.sel > 0) {
            n.sel--;
            return HCN_DONE | HCN_SELMOVE;
        }
        if (act == HCA_CURSOR_DOWN && n.sel + 1 < hc_cfg().n) {
            n.sel++;
            return HCN_DONE | HCN_SELMOVE;
        }
        return HCN_NONE;
    case HCX_EDIT:
        if (act == HCA_CURSOR_UP && n.sel > 0) {
            n.sel--;
            return HCN_DONE | HCN_SELMOVE;
        }
        if (act == HCA_CURSOR_DOWN && n.sel + 1 < hc_cfg().n) {
            n.sel++;
            return HCN_DONE | HCN_SELMOVE;
        }
        return HCN_NONE;
    case HCX_BROWSE_INSPECT:                                  // T152 D8: the browser picks a .cfw/.ue2 to look into
    case HCX_BROWSE_PICK:                                     // T139: the browser picks the target folder
    case HCX_BROWSE: {
        int r = browse_cursor(act);
        if (r) {
            return r;
        }
        r = browse_page(act);
        if (r) {
            return r;
        }
        if (act == HCA_BROWSE_UP && !hc_browse_at_media()) {
            hc_browse_up();
            return HCN_DONE;
        }
        if (ctx != HCX_BROWSE && act == HCA_BROWSE_ENTER && row_is_dir()) {
            hc_browse_enter();
            return HCN_DONE;
        }
        if (act == HCA_BROWSE_OPEN && row_is_dir() && (ctx != HCX_BROWSE_PICK || strcmp(n.bl[n.bsel].name, ".."))) {
            hc_browse_enter();                                // in the pick a highlighted folder is entered, never the target of its parent (T139 D1)
            return HCN_DONE;
        }
        return HCN_NONE;
    }
    }
    return HCN_NONE;
}

void hc_nav_clamp_ssel(int cnt)
{
    if (g_nav.ssel >= cnt) {
        g_nav.ssel = cnt - 1;
    }
    if (g_nav.ssel < 0) {
        g_nav.ssel = 0;
    }
}

bool hc_nav_settings_act(int act, int cnt)
{
    if (g_nav.ssel >= cnt) {
        g_nav.ssel = cnt - 1;
    }
    if (act == HCA_CURSOR_UP && g_nav.ssel > 0) {
        g_nav.ssel--;
        return true;
    }
    if (act == HCA_CURSOR_DOWN && g_nav.ssel + 1 < cnt) {
        g_nav.ssel++;
        return true;
    }
    return act == HCA_CURSOR_UP || act == HCA_CURSOR_DOWN;
}

bool hc_nav_wifi_act(int act, int rows)
{
    if (act == HCA_CURSOR_UP && g_nav.nsel > 0) {
        g_nav.nsel--;
        return true;
    }
    if (act == HCA_CURSOR_DOWN && g_nav.nsel + 1 < rows) {
        g_nav.nsel++;
        return true;
    }
    return act == HCA_CURSOR_UP || act == HCA_CURSOR_DOWN;
}

void hc_nav_pm_open(void)
{
    g_nav.cm_mode = HCM_OFF;
    g_nav.wm_mode = 0;
    g_nav.pm_mode = 1;
}

void hc_nav_pm_close(void)
{
    g_nav.pm_mode = 0;
}

void hc_nav_wifi_open(void)
{
    g_nav.wm_mode = 1;
    g_nav.nsel = 0;
    g_nav.nnote[0] = 0;
}

void hc_nav_wifi_close(void)
{
    g_nav.wm_mode = 0;
    g_nav.note[0] = 0;
}

void hc_nav_clamp_nsel(int rows)
{
    if (g_nav.nsel >= rows) {
        g_nav.nsel = rows - 1;
    }
}

void hc_nav_colors_open(void) { g_nav.view = HCV_COLORS; }
void hc_nav_colors_pattern(char c) { g_nav.cpat = c; }

// ---- cursors set from outside ----------------------------------------------------------------------------------------------------------------
void hc_nav_set_sel(int i) { g_nav.sel = i; }
void hc_nav_set_ssel(int i) { g_nav.ssel = i; }
void hc_nav_set_nsel(int i) { g_nav.nsel = i; }
void hc_nav_sel_default(void) { g_nav.sel = hc_cfg().def >= 0 ? hc_cfg().def : 0; }
void hc_nav_sel_clamp(int total)
{
    if (g_nav.sel >= total) {
        g_nav.sel = total ? total - 1 : 0;
    }
}
void hc_nav_sel_step(int d) { g_nav.sel += d; }
void hc_nav_sel_after_remove(void)
{
    if (g_nav.sel >= hc_cfg().n && g_nav.sel) {
        g_nav.sel--;
    }
}
void hc_nav_set_cm(int mode, int top)
{
    g_nav.cm_mode = mode;
    g_nav.cm_top = top;
}

void hc_nav_focus(int src, int i)
{
    HcNav &n = g_nav;
    if (src == 0 && i >= 0 && i < hc_cfg().n) {
        n.sel = i;
        if (n.view == HCV_MENU) {
            listwin_fix(n.sel, hc_cfg().n, g_mrows, &n.mfirst);
        } else if (n.view == HCV_EDIT) {
            listwin_fix(n.sel, hc_cfg().n, g_erows, &n.efirst);
        }
    } else if (src == 1 && i >= 0 && i < n.bn) {
        n.bsel = i;
        n.note[0] = 0;
    } else if (src == 2 && i >= 0) {
        n.cm_top = i;
    }
}

int hc_nav_menu_first(int total)
{
    listwin_fix(g_nav.sel, total, g_mrows, &g_nav.mfirst);
    return g_nav.mfirst;
}

int hc_nav_edit_first(int total)
{
    listwin_fix(g_nav.sel, total, g_erows, &g_nav.efirst);
    return g_nav.efirst;
}

bool hc_nav_window_follow(void)
{
    HcNav &n = g_nav;
    if (n.view == HCV_MENU) {
        return listwin_fix(n.sel, hc_cfg().n, g_mrows, &n.mfirst);
    }
    if (n.view == HCV_EDIT) {
        return listwin_fix(n.sel, hc_cfg().n, g_erows, &n.efirst);
    }
    return false;
}

void hc_nav_test_view(int view, int sel, int bsel)
{
    g_nav.view = view;
    g_nav.sel = sel;
    g_nav.bsel = bsel;
}

void hc_nav_test_init(void)
{
    hc_nav_sel_default();
    g_nav.mfirst = g_nav.efirst = 0;
    hc_note_clear();
    g_nav.ssel = 0;
    g_nav.br_mode = HCB_NORMAL;
    g_nav.view = HCV_MENU;
    g_nav.pm_mode = 0;                  // a bench case may end with the Picture page open
}
