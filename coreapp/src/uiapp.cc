#include "uiapp.h"
#include "ops_glue.h"
#include "rest.h"
#include "settings.h"
#include "settings_ops.h"
#include "picture.h"
#include "contrib.h"
#include "corefolder.h"
#include "keyboard.h"
#include "board.h"
#include "cores.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

namespace {

#define SNAP_BUF 12000
#define TOKEN_FILE_NAME "ui.token"
#define FOCUS_WINDOW 24

CoreCtx *g_ctx;
UiAppEnv g_env;
UicService g_svc;
char g_snap[SNAP_BUF];
bool g_up;
uint32_t g_toast_rev;
bool g_quiet = true;                                       // no key and no other change was taken in this pass of the run loop
bool g_pumped;                                             // the pump changed the screen: the loop draws it

// ---- the port ------------------------------------------------------------------------------------------------------------------------------
UiAppPlatform g_plat;

uint32_t p_now(void *) { return g_ctx->io->now_ms(); }
void p_lock(void *) { g_plat.lock(); }
void p_unlock(void *) { g_plat.unlock(); }
void p_sleep(void *, int ms) { g_plat.sleep_ms(ms); }
bool p_ui(void *) { return g_plat.on_ui(); }
void p_log(void *, const char *line) { printf("%s\n", line); }

// T192.1 Q3: the raw material is the platform's (the board: timer jitter against the ticks); the core's own path joins it. The service hashes it all.
int p_entropy(void *, uint8_t *out, int n)
{
    int o = g_plat.entropy ? g_plat.entropy(out, n) : 0;
    for (const char *p = g_ctx->cor_path; *p && o < n; p++) {
        out[o++] = (uint8_t)*p;
    }
    return o;
}

bool token_path(char *out, int n)
{
    char dir[HANDOVER_PATH_MAX];
    if (!corefolder_dir(g_ctx->cor_path, dir, sizeof(dir))) {
        return false;
    }
    return snprintf(out, n, "%s/%s", dir, TOKEN_FILE_NAME) < n;
}

int p_load(void *, char *buf, int cap)
{
    char path[HANDOVER_PATH_MAX + 20];
    if (!token_path(path, sizeof(path))) {
        return -1;
    }
    return g_ctx->fs->load(path, buf, (uint32_t)cap);
}

bool p_save(void *, const char *buf, int n)
{
    char path[HANDOVER_PATH_MAX + 20];
    if (!token_path(path, sizeof(path))) {
        return false;
    }
    corefolder_ensure(g_ctx->fs, g_ctx->cor_path);
    return g_ctx->fs->save(path, buf, (uint32_t)n);
}

bool p_remove(void *)
{
    char path[HANDOVER_PATH_MAX + 20];
    return token_path(path, sizeof(path)) && g_ctx->fs->remove(path);
}

// ---- the keys ------------------------------------------------------------------------------------------------------------------------------
int key_code(const char *name)
{
    static const struct { const char *n; int k; } K[] = {
        { "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "ret", KEY_RETURN }, { "return", KEY_RETURN },
        { "enter", KEY_RETURN }, { "back", KEY_ESCAPE }, { "esc", KEY_ESCAPE }, { "del", KEY_BACK }, { "runstop", KEY_BREAK }, { "space", KEY_SPACE },
        { "home", KEY_HOME }, { "pgup", KEY_PAGEUP }, { "pgdn", KEY_PAGEDOWN }, { "tab", KEY_TAB }, { "any", KEY_SPACE },
        { "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f3", KEY_F3 }, { "f4", KEY_F4 }, { "f5", KEY_F5 }, { "f6", KEY_F6 }, { "f7", KEY_F7 }, { "f8", KEY_F8 }
    };
    for (unsigned i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
        if (!strcasecmp(name, K[i].n)) {
            return K[i].k;
        }
    }
    if (name[0] == '#' && name[1]) {                    // a raw code: the board's own keys enter by it
        return atoi(name + 1);
    }
    if (name[0] && !name[1] && (unsigned char)name[0] >= 0x20 && (unsigned char)name[0] < 0x7F) {
        return (unsigned char)name[0];
    }
    return -1;
}

// ---- destructive operations over REST (D19a) -------------------------------------------------------------------------------------------------
struct Destructive {
    char op[40];
    char text[96];
};
Destructive g_dest[48];
int g_ndest;
uint32_t g_dest_at;

void dest_add(const char *op, const char *text)
{
    for (int i = 0; i < g_ndest; i++) {
        if (!strcmp(g_dest[i].op, op)) {
            return;
        }
    }
    if (g_ndest < 48) {
        snprintf(g_dest[g_ndest].op, sizeof(g_dest[0].op), "%s", op);
        snprintf(g_dest[g_ndest].text, sizeof(g_dest[0].text), "%s", text);
        g_ndest++;
    }
}

// The operations the menu itself asks about (a node whose ui says `confirm`), plus the ones the menu runs from a file action or a fixed entry that
// destroy something. The set is kept by the menu task; the REST task reads it under the service lock.
void dest_refresh(MenuCtl *m)
{
    Destructive tmp[48];
    int n = 0;
    auto add = [&](const char *op, const char *text) {
        for (int i = 0; i < n; i++) if (!strcmp(tmp[i].op, op)) return;
        if (n < 48) {
            snprintf(tmp[n].op, sizeof(tmp[0].op), "%s", op);
            snprintf(tmp[n].text, sizeof(tmp[0].text), "%s", text);
            n++;
        }
    };
    add("files.delete_job", "Delete the file(s)?");
    add("settings.reset_to_host", "Reset the settings to the host's?");
    add("settings.reset_defaults", "Reset the settings to their defaults?");
    add("settings.clear", "Clear the configuration?");
    add("machine.restart", "Restart the core?");
    add("machine.cold_reset", "Reset the machine (cold)?");
    add("machine.warm_reset", "Reset the machine (warm)?");
    const MenuTree &t = m->tree();
    for (int i = 0; i < t.nnodes; i++) {
        char q[96];
        if (t.nodes[i].op && ui_attr(t.nodes[i].attr, "confirm", q, sizeof(q))) {
            add(t.nodes[i].op, q);
        }
    }
    g_plat.lock();
    memcpy(g_dest, tmp, sizeof(tmp));
    g_ndest = n;
    g_plat.unlock();
}

struct Pending {
    bool used;
    char op[40];
    char text[96];
    char query[HTTP_QUERY_MAX];
    char body[HTTP_BODY_MAX + 1];
    int body_len;
};
Pending g_pend;                          // the menu task's alone (T192 M5)

// What the REST task hands to the menu task (the request of a destructive operation): a record of its own, claimed under the lock, named in the intent (`aux`), copied
// into g_pend by the menu task when the intent is applied. The REST task never writes g_pend. A record whose intent was dropped (the UI did not answer in time) is
// taken back after REC_STALE_MS.
#define REC_STALE_MS 10000
struct ReqRec {
    bool used;
    uint32_t at;
    char op[40];
    char text[96];
    char query[HTTP_QUERY_MAX];
    char body[HTTP_BODY_MAX + 1];
    int body_len;
};
ReqRec g_rec[2];

// ---- the domain ----------------------------------------------------------------------------------------------------------------------------
MenuCtl *act() { return g_env.active(g_env.user); }
bool is_open() { return g_env.menu_open(g_env.user); }

const char *owner_of(MenuCtl *m)
{
    if (!is_open()) return "closed";
    if (g_env.picture_pending(g_env.user)) return "picture";
    if (g_env.app_dialog(g_env.user)) return "dialog";
    if (m->mapping()) return "dialog";
    if (m->search_editing()) return "search";
    char t[2];
    const char *d = m->dialog(t, sizeof(t));
    if (!strcmp(d, "busy")) return "job";
    if (!strcmp(d, "confirm") || !strcmp(d, "ask") || !strcmp(d, "text") || !strcmp(d, "address") || !strcmp(d, "pick") || !strcmp(d, "viewer")) return "dialog";
    if (m->layout_is("terminal")) return "monitor";
    return "menu";
}

// Who takes a key: the picture question (any key keeps the mode), an application's dialog (its own key table, appdlg.cc), else the menu (MenuCtl::resolve). Every one of them gets
// an INTENT - the table makes it, the owner applies it - and the service carries it (T192.3 D23: the picture question and the dialogs are service questions, not a raw key path).
bool picture_up() { return g_env.picture_pending(g_env.user); }
bool dialog_up() { return g_env.app_dialog(g_env.user); }
// The one dialog that is also the service's question: a yes/no pair (question() offers exactly this).
bool dialog_yes_no()
{
    if (!dialog_up()) return false;
    const DlgView *v = g_env.dialog_view(g_env.user);
    return v->kind == DK_BUTTONS && (v->buttons & SVC_BUTTON_YES) && (v->buttons & SVC_BUTTON_NO);
}

// The answer to the question the service shows (the picture's, an application's yes/no): what a key or a remote `answer` does.
void answer_owner(bool yes)
{
    if (picture_up()) {
        if (yes) picture_confirm_key(); else picture_confirm_revert();
        return;
    }
    DlgIntent di;
    di.id = DI_CHAR;
    di.arg = yes ? 'y' : 'n';
    g_env.dialog_apply(g_env.user, &di);
}

void press(int k)
{
    if (picture_up()) {
        bool no = k == KEY_LEFT || k == KEY_ESCAPE;          // T187 D5: LEFT / ESC send the previous mode back at once, any other key keeps this one
        answer_owner(!no);
        uic_close(&g_svc, "remote", no ? "no" : "yes");
        return;
    }
    if (dialog_up()) {
        DlgIntent di;
        g_env.dialog_key_intent(g_env.user, k, &di);
        g_env.dialog_apply(g_env.user, &di);
        return;
    }
    MenuIntent in = act()->resolve(k);
    g_env.give_intent(g_env.user, &in);
}

// An intent by its name in the table, where the screen stands now ("return" answers a question that is up, as the key does).
void press_named(const char *name, int arg = 0)
{
    int id = menu_intent_by_name(name);
    int k = id == MI_TAB ? '0' + arg : menu_keymap_key_of(id, arg);
    if (k >= 0) {
        press(k);
    }
}

// The dialog's intent as text ("D<id>.<arg>"), and back.
bool dlg_decode(const char *t, DlgIntent *in)
{
    if (t[0] != 'D') return false;
    int v[2];
    const char *p = t + 1;
    for (int i = 0; i < 2; i++) {
        bool neg = *p == '-';
        if (neg) p++;
        if (*p < '0' || *p > '9') return false;
        int n = 0;
        for (int d = 0; *p >= '0' && *p <= '9'; d++) {
            if (d >= 9) return false;                               // no overflow on a remote text
            n = n * 10 + (*p++ - '0');
        }
        v[i] = neg ? -n : n;
        if (i == 0 && *p++ != '.') return false;
    }
    in->id = (uint8_t)v[0];
    in->arg = v[1];
    return true;
}

// What the board's own keyboard sends: the intent the table made of the key, as text ("@id.arg.key.stage.row").
bool intent_decode(const char *t, MenuIntent *in)
{
    if (t[0] != '@') {
        return false;
    }
    int v[5];
    const char *p = t + 1;
    for (int i = 0; i < 5; i++) {                                    // five signed numbers separated by dots (the firmware's C library is small: no sscanf)
        bool neg = *p == '-';
        if (neg) p++;
        if (*p < '0' || *p > '9') return false;
        int n = 0;
        for (int d = 0; *p >= '0' && *p <= '9'; d++) {
            if (d >= 9) return false;
            n = n * 10 + (*p++ - '0');
        }
        v[i] = neg ? -n : n;
        if (i < 4) {
            if (*p++ != '.') return false;
        }
    }
    in->id = (uint8_t)v[0];
    in->arg = v[1];
    in->key = v[2];
    in->stage = (int8_t)v[3];
    in->row = (int16_t)v[4];
    return true;
}

MenuCtl::Item s_row_item, s_snap_item;                   // two, because row_ident runs between the snapshot's own use of one

// the rows of the context the cursor is in, with the id of each: name (what it IS) + kind
struct RowId {
    char id[UIC_ID_MAX + 1];
};

enum Ctx { C_ITEMS, C_LIBRARY, C_SHEET, C_PICK, C_NONE };

Ctx ctx_of(MenuCtl *m)
{
    char t[2];
    const char *d = m->dialog(t, sizeof(t));
    if (!strcmp(d, "pick")) return C_PICK;
    if (!strcmp(d, "busy") || !strcmp(d, "confirm") || !strcmp(d, "ask") || !strcmp(d, "text") || !strcmp(d, "address") || !strcmp(d, "viewer")) return C_NONE;
    if (m->picking()) return C_PICK;                  // the folder browser of a task, the file selector of a setting, the list of configs
    if (m->chooser()) return C_SHEET;
    if (m->browsing()) return C_LIBRARY;
    return C_ITEMS;
}

// The raw identity of row i in the context, "" when it has none.
void row_ident(MenuCtl *m, Ctx c, int i, char *out, int n, bool *enabled, bool *hidden)
{
    out[0] = 0;
    *enabled = true;
    *hidden = false;
    if (c == C_ITEMS) {
        MenuCtl::Item *it = &s_row_item;                        // the menu task's one scratch item: no allocation per row of a render (T192 M6)
        m->item(i, it);
        snprintf(out, n, "%s", it->ident[0] ? it->ident : it->title);
        *enabled = it->enabled && !it->dim;
        *hidden = it->hidden;
    } else if (c == C_LIBRARY) {
        MenuCtl::BrowseRow r;
        m->browse_row(i, &r);
        snprintf(out, n, "fs:%s", r.name);
    } else if (c == C_SHEET) {
        snprintf(out, n, "act:%s", m->chooser_label(i));
    } else if (c == C_PICK) {
        char left[120], right[60];
        bool act2;
        m->row(i, left, sizeof(left), right, sizeof(right), &act2);
        snprintf(out, n, "pick:%s", left);
    }
}

// The ids of all rows (a content-derived id; two rows that still collide get an ordinal); index -> id.
void make_id(const char *ident, char *id, int cap)
{
    const char *colon = strchr(ident, ':');
    char prefix[16];
    const char *raw = ident;
    prefix[0] = 0;
    if (colon && colon - ident < (int)sizeof(prefix) - 1) {
        memcpy(prefix, ident, colon - ident + 1);
        prefix[colon - ident + 1] = 0;
        raw = colon + 1;
    }
    uic_id_make(prefix, raw, id, cap);
}

// ".2", ".3": the ordinal of a row whose identity another row of the screen shares (the board's printf has no precision: the cut is done by hand)
void dup_suffix(char *id, int ord)
{
    char suf[8];
    snprintf(suf, sizeof(suf), ".%d", ord);
    int sl = (int)strlen(suf);
    int l = (int)strlen(id);
    if (l + sl > UIC_ID_MAX) {
        l = UIC_ID_MAX - sl;
    }
    memcpy(id + l, suf, sl + 1);
}

// Finds the row whose id is `id`; its index, -1 when there is none. The same ordinal rule as the snapshot's.
char g_seen[160][UIC_ID_MAX + 1];                         // only the menu task builds ids: one scratch table for both walks
int find_row(MenuCtl *m, Ctx c, const char *id, bool *enabled)
{
    int n = m->rows();
    if (c == C_ITEMS && n > 128) n = 128;
    char (*seen)[UIC_ID_MAX + 1] = g_seen;
    int nseen = 0;
    for (int i = 0; i < n; i++) {
        char ident[160], rid[UIC_ID_MAX + 1];
        bool en, hid;
        row_ident(m, c, i, ident, sizeof(ident), &en, &hid);
        make_id(ident, rid, sizeof(rid));
        int dup = 0;
        if (c == C_ITEMS) {                                         // the names of a folder are unique already; the rows of a tab may share an identity
            for (int k = 0; k < nseen; k++) if (!strcmp(seen[k], rid)) dup++;
            if (nseen < 160) snprintf(seen[nseen++], sizeof(seen[0]), "%s", rid);
        }
        if (dup) {
            dup_suffix(rid, dup + 1);
        }
        if (!hid && !strcmp(rid, id)) {
            *enabled = en;
            return i;
        }
    }
    return -1;
}

// The ids of the rows, for the event of a focus change: the id of row `idx` of the context, with the ordinal rule of the snapshot.
bool focus_id_now(MenuCtl *m, char *out, int cap)
{
    out[0] = 0;
    Ctx c = ctx_of(m);
    int n = m->rows();
    int sel = m->selected();
    if (c == C_NONE || sel < 0 || sel >= n) return false;
    if (c == C_ITEMS && n > 128) n = 128;
    char (*seen)[UIC_ID_MAX + 1] = g_seen;
    int nseen = 0;
    for (int i = 0; i <= sel && i < n; i++) {
        char ident[160], rid[UIC_ID_MAX + 1];
        bool en, hid;
        row_ident(m, c, i, ident, sizeof(ident), &en, &hid);
        make_id(ident, rid, sizeof(rid));
        int dup = 0;
        if (c == C_ITEMS) {
            for (int k = 0; k < nseen; k++) if (!strcmp(seen[k], rid)) dup++;
            if (nseen < 160) snprintf(seen[nseen++], sizeof(seen[0]), "%s", rid);
        }
        if (dup) dup_suffix(rid, dup + 1);
        if (i == sel) {
            snprintf(out, cap, "%s", rid);
            return true;
        }
    }
    return false;
}

// T192.3 S12: what the controller changed is an event, not only a new snapshot: the page, the focus, the dirty mark, the media. Seen once per pass; a client that missed one reads the snapshot.
int g_ev_area = -1, g_ev_tab = -1, g_ev_sel = -2;
int g_ev_dirty = -1;
uint32_t g_ev_media;

void events_scan(MenuCtl *m)
{
    if (m->area() != g_ev_area || m->tab() != g_ev_tab) {
        char scr[80];
        snprintf(scr, sizeof(scr), "%s/%s", m->tree().a[m->area()].key, m->tab_id());
        uic_event(&g_svc, "page", scr);
        g_ev_area = m->area();
        g_ev_tab = m->tab();
        g_ev_sel = -2;
    }
    if (m->selected() != g_ev_sel) {
        char id[UIC_ID_MAX + 1];
        focus_id_now(m, id, sizeof(id));
        uic_event(&g_svc, "focus", id);
        g_ev_sel = m->selected();
    }
    if ((int)m->unsaved() != g_ev_dirty) {
        g_ev_dirty = m->unsaved();
        uic_event(&g_svc, "dirty", g_ev_dirty ? "true" : "false");
    }
    uint32_t sig = (uint32_t)m->nchips();
    for (int i = 0; i < m->nchips(); i++) {
        for (const char *p = m->chip_name(i); *p; p++) sig = sig * 31 + (uint8_t)*p;
    }
    if (sig != g_ev_media) {
        g_ev_media = sig;
        uic_event(&g_svc, "media", "changed");
    }
}

const char *tone_name(int t)
{
    static const char *const n[] = { "muted", "cyan", "violet", "amber", "coral" };
    return t >= 0 && t < 5 ? n[t] : "";
}

class CoreDomain : public UicDomain {
public:
    const char *app() override { return "core"; }
    const char *keys_owner() override { return owner_of(act()); }

    void snapshot(UicW &w) override
    {
        bool open = is_open();
        w.kb("menu_open", open);
        if (dialog_up()) {                                  // an application's dialog (appdlg.h): what it shows and which row is selected; its keys are intents of the same kind (D23)
            const DlgView *v = g_env.dialog_view(g_env.user);
            static const char *const kinds[] = { "none", "message", "buttons", "choose", "text", "pick_file", "pick_folder" };
            w.kopen("appdlg", '{');
            w.ks("kind", v->kind >= 0 && v->kind <= DK_PICK_FOLDER ? kinds[v->kind] : "?");
            w.ks("title", v->title);
            w.ks("text", v->text);
            if (v->kind == DK_TEXT || v->kind == DK_PICK_FILE || v->kind == DK_PICK_FOLDER) w.ks("input", v->input);
            if (v->kind == DK_BUTTONS) {
                w.ki("buttons", v->buttons);
            }
            if (v->remaining_s > 0) w.ki("remaining_s", v->remaining_s);
            w.ki("sel", v->sel);
            w.ki("first", v->first);
            w.ki("total", v->nitems);
            w.kopen("rows", '[');
            for (int i = 0; i < v->rows; i++) {
                w.sep();
                w.open('{');
                w.ks("text", v->row[i]);
                if (v->row_dir[i]) w.kb("dir", true);
                w.kb("selected", v->first + i == v->sel);
                w.close('}');
            }
            w.close(']');
            w.close('}');
        }
        if (!open) {
            w.ks("screen", "closed");
            w.ks("title", g_ctx->desc && g_ctx->desc->cor_name ? g_ctx->desc->cor_name : "");
            return;
        }
        MenuCtl *m = act();
        char tmp[200];
        w.kb("unsaved", m->unsaved());
        // the screen: area key / tab id (the folder in the library)
        char scr[80];
        snprintf(scr, sizeof(scr), "%s/%s", m->tree().a[m->area()].key, m->tab_id());
        w.ks("screen", scr);
        if (!m->layout_param("title", tmp, sizeof(tmp))) {
            snprintf(tmp, sizeof(tmp), "%s", m->tab_name());
        }
        w.ks("title", tmp);
        if (m->layout_param("crumb", tmp, sizeof(tmp))) {
            w.ks("crumb", tmp);
        } else {
            if (!strcasecmp(m->area_name(), m->tab_name())) {         // an area whose one tab has its name (Home): the name once, as the screen shows it
                snprintf(tmp, sizeof(tmp), "%s", m->area_name());
            } else {
                snprintf(tmp, sizeof(tmp), "%s / %s", m->area_name(), m->tab_name());
            }
            w.ks("crumb", tmp);
        }
        w.kopen("topbar", '{');                              // what the top bar shows, from the controller's state (T192.3 S11)
        {
            char md[64], nt[64], ss[24];
            m->info_line(3, md, sizeof(md));                 // menu_info_labels: Core, Application, Network, Media, ROMs, Libraries
            m->info_line(2, nt, sizeof(nt));
            m->spec_text("@session", ss, sizeof(ss));
            w.ks("core", m->core_title());
            w.ks("media", md);
            w.ks("net", nt);
            w.ks("state", ss);
            if (g_plat.shell) {                              // the bar as the screen draws it: the activity and the chips (UNSAVED among them)
                UiShell sh;
                memset(&sh, 0, sizeof(sh));
                g_plat.shell(m, &sh);
                w.ks("activity", sh.activity);
                w.kopen("chips", '[');
                for (int i = 0; i < sh.nchips; i++) {
                    w.sep();
                    w.str(sh.chips[i]);
                }
                w.close(']');
            }
        }
        w.close('}');
        {
            const char *lay = m->layout();
            char ln[24];
            ui_name(lay, ln, sizeof(ln));
            w.ks("layout", lay ? ln : "list");
        }
        w.kopen("areas", '[');
        for (int a = 0; a < m->tree().nareas; a++) {
            w.sep();
            w.open('{');
            w.ks("id", m->tree().a[a].key);
            w.ks("title", m->tree().a[a].name);
            w.kb("active", a == m->area());
            w.close('}');
        }
        w.close(']');
        w.kopen("tabs", '[');
        for (int t = 0; t < m->tree().a[m->area()].ntabs; t++) {
            if (m->tab_is_hidden(m->area(), t)) continue;
            w.sep();
            w.open('{');
            w.ks("id", m->tree().a[m->area()].tabs[t].id ? m->tree().a[m->area()].tabs[t].id : "");
            w.ki("n", m->tree().a[m->area()].tabs[t].n);
            w.ks("title", m->tree().a[m->area()].tabs[t].name);
            w.kb("active", t == m->tab());
            w.close('}');
        }
        w.close(']');
        Ctx c = ctx_of(m);
        int n = m->rows();
        int sel = m->selected();
        int first = 0, last = n;
        if (c == C_LIBRARY || c == C_PICK) {
            first = sel - 8 < 0 ? 0 : sel - 8;
            last = first + FOCUS_WINDOW < n ? first + FOCUS_WINDOW : n;
            if (c == C_LIBRARY) w.ks("path", m->browse_path());
        } else if (c == C_ITEMS && n > 128) {
            last = 128;
        } else if (c == C_NONE) {
            last = 0;
        }
        char focus[UIC_ID_MAX + 1];
        focus[0] = 0;
        char (*seen)[UIC_ID_MAX + 1] = g_seen;
        int nseen = 0;
        w.kopen("items", '[');
        for (int i = (c == C_ITEMS || c == C_SHEET) ? 0 : first; i < last; i++) {
            char ident[160], rid[UIC_ID_MAX + 1];
            bool en, hid;
            row_ident(m, c, i, ident, sizeof(ident), &en, &hid);
            make_id(ident, rid, sizeof(rid));
            int dup = 0;
            if (c == C_ITEMS) {
                for (int k = 0; k < nseen; k++) if (!strcmp(seen[k], rid)) dup++;
                if (nseen < 160) snprintf(seen[nseen++], sizeof(seen[0]), "%s", rid);
            }
            if (dup) {
                dup_suffix(rid, dup + 1);
            }
            if (hid) continue;
            if (i == sel) snprintf(focus, sizeof(focus), "%s", rid);
            if (w.cap - w.p < 700) {
                w.kb("more", true);
                break;
            }
            w.sep();
            w.open('{');
            w.ks("id", rid);
            if (c == C_ITEMS) {
                MenuCtl::Item *it = &s_snap_item;
                m->item(i, it);
                static const char *const kinds[] = { "button", "row", "value", "heading", "setting", "media", "task", "info", "fixed" };
                w.ks("kind", it->kind >= 0 && it->kind <= 8 ? kinds[it->kind] : "?");
                w.ks("title", it->title);
                if (it->sub[0]) w.ks("sub", it->sub);
                if (it->chip[0]) w.ks("chip", it->chip);
                if (it->value[0]) w.ks("value", it->value);
                if (it->tone >= 0) w.ks("tone", tone_name(it->tone));
                if (it->bkey) { char k[2] = { it->bkey, 0 }; w.ks("bkey", k); }
                if (it->slot[0]) w.ks("slot", it->slot);
                w.kb("enabled", it->enabled);
                if (it->dim) w.kb("dim", true);
                if (it->dirty) w.kb("dirty", true);
                if (it->primary) w.kb("primary", true);
                if (it->toggle) { w.kb("toggle", true); w.kb("on", it->on); }
                if (!strncmp(ident, "set:", 4)) {
                    int si = g_ctx->settings ? g_ctx->settings->find(ident + 4) : -1;
                    if (si >= 0) w.kb("live", g_ctx->desc->settings[si].when == WHEN_LIVE);
                }
            } else if (c == C_LIBRARY) {
                MenuCtl::BrowseRow r;
                m->browse_row(i, &r);
                w.ks("kind", r.dir ? "dir" : "file");
                w.ks("title", r.name);
                if (r.ext[0]) w.ks("ext", r.ext);
                if (r.sub[0]) w.ks("sub", r.sub);
                if (r.primary[0]) w.ks("primary", r.primary);
                if (r.badge[0]) w.ks("badge", r.badge);
                if (r.marked) w.kb("marked", true);
                if (r.more) w.kb("more", true);
                w.kb("enabled", true);
            } else if (c == C_PICK) {
                char left[120], right[60], sub[120];
                bool act2;
                m->row(i, left, sizeof(left), right, sizeof(right), &act2, sub, sizeof(sub));
                w.ks("kind", "pick");
                w.ks("title", left);
                if (right[0]) w.ks("value", right);
                if (sub[0]) w.ks("sub", sub);
                w.kb("enabled", act2);
            } else {
                w.ks("kind", "action");
                w.ks("title", m->chooser_label(i));
                char k[2] = { m->chooser_key(i), 0 };
                if (k[0]) w.ks("key", k);
                w.kb("enabled", true);
            }
            if (i == sel) w.kb("focused", true);
            w.close('}');
        }
        w.close(']');
        w.ks("focus", focus);
        w.ks("focus_id", focus);                            // T192.3 S8: the id is the memory of the position; the row index is for drawing
        w.kopen("rows", '{');
        w.ki("first", c == C_LIBRARY || c == C_PICK ? first : 0);
        w.ki("total", n);
        w.close('}');
        if (c == C_SHEET) w.ks("sheet", m->chooser_file());
        if (c == C_LIBRARY && m->media_level()) {            // the media level: the info panel of the medium under the cursor (Size, Contents, Access)
            w.kb("media_level", true);
            MediumInfo mi;
            if (sel >= 0 && sel < n && m->medium_at(sel, &mi)) {
                MenuCtl::BrowseRow r;
                m->browse_row(sel, &r);
                w.kopen("medium", '{');
                w.ks("name", r.name);
                if (mi.ready) w.kb("ready", true);
                if (mi.size[0]) w.ks("size", mi.size);
                w.ks("contents", mi.contents);
                w.ks("access", mi.access());
                w.close('}');
            }
        }
        {                                                   // a prompt, the viewer, the pad wizard: what the screen shows in place of the list
            char dt[260];
            const char *dk = m->dialog(dt, sizeof(dt));
            if (strcmp(dk, "none") && strcmp(dk, "sheet") && strcmp(dk, "pick") && strcmp(dk, "confirm") && strcmp(dk, "busy")) {
                w.kopen("modal", '{');
                w.ks("kind", dk);
                w.ks("text", dt);
                char l[160], r2[100], sb[200];
                bool a2;
                m->row(0, l, sizeof(l), r2, sizeof(r2), &a2, sb, sizeof(sb));
                w.ks("label", l);
                if (r2[0]) w.ks("value", r2);
                if (sb[0]) w.ks("sub", sb);
                if (!strcmp(dk, "viewer")) {
                    w.ks("path", m->view_title());
                    w.kb("hex", m->view_is_hex());
                    char vs[100];
                    m->view_status(vs, sizeof(vs));
                    w.ks("status", vs);
                    w.kopen("lines", '[');
                    for (int vr = 0; vr < MenuCtl::VIEW_ROWS; vr++) {
                        char vl[MenuCtl::VIEW_COLS + 8];
                        m->view_line(vr, vl, sizeof(vl));
                        w.sep();
                        w.str(vl);
                    }
                    w.close(']');
                } else if (m->mapping()) {
                    w.kb("wizard", true);
                }
                w.close('}');
            }
        }
        char legend[200];
        m->footer_legend(legend, sizeof(legend));
        w.kopen("footer", '{');
        w.ks("legend", legend);
        if (g_plat.shell) {
            UiShell sh;
            memset(&sh, 0, sizeof(sh));
            g_plat.shell(m, &sh);
            w.ks("left", sh.footer_left);
        }
        w.close('}');
        w.kopen("status", '{');
        w.ks("text", m->status());
        w.kb("bad", m->status_bad());
        w.close('}');
        const char *h = m->hint();
        if (h) w.ks("hint", h);
        if (m->toast_text()[0]) w.ks("toast", m->toast_text());
    }

    void intent(const UicIntent &in, UicReply *r) override
    {
        MenuCtl *m = act();
        switch (in.kind) {
        case UD_SELECT:
        case UD_ACTIVATE: {
            Ctx c = ctx_of(m);
            bool en = true;
            int idx = c == C_NONE ? -1 : find_row(m, c, in.item, &en);
            if (idx < 0) {
                r->r = UR_GONE;
                snprintf(r->why, sizeof(r->why), "no row with that id on this screen");
                return;
            }
            if (!m->select_index(idx)) {
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "that row takes no focus");
                return;
            }
            if (in.kind == UD_SELECT) {
                r->r = UR_TAKEN;
                return;
            }
            if (!en) {
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "%s", m->hint() ? m->hint() : "that row does nothing now");
                return;
            }
            press_named("return");                              // the same intent as the board's RETURN: confirm, dirty and lock rules included
            char t[2];
            const char *d = act()->dialog(t, sizeof(t));
            r->r = !strcmp(d, "confirm") ? UR_ASKED : UR_TAKEN;
            return;
        }
        case UD_SET: {
            const char *key = in.key[0] ? in.key : in.item;
            if (!strncmp(key, "set:", 4)) {
                key += 4;
            }
            int si = g_ctx->settings ? g_ctx->settings->find(key) : -1;
            if (si < 0) {
                // an id made from the key (lower case, folded): look for the setting it names
                for (int i = 0; i < g_ctx->desc->nsettings && si < 0; i++) {
                    char id[UIC_ID_MAX + 1], want[UIC_ID_MAX + 1];
                    uic_id_make("", g_ctx->desc->settings[i].key, id, sizeof(id));
                    uic_id_make("", key, want, sizeof(want));
                    if (!strcmp(id, want) || !strcmp(g_ctx->desc->settings[i].key, key)) si = i;
                }
            }
            if (si < 0) {
                r->r = UR_GONE;
                snprintf(r->why, sizeof(r->why), "no such setting");
                return;
            }
            const Setting &s = g_ctx->desc->settings[si];
            OpResult res;
            int rc = settings_set(g_ctx, s.key, in.value, &res);
            snprintf(r->why, sizeof(r->why), "%s", res.msg);
            m->say(res.msg, rc != 0);
            if (rc) {
                r->r = UR_REFUSED;
                return;
            }
            r->r = m->unsaved() ? UR_HELD : UR_TAKEN;
            return;
        }
        case UD_KEY: {
            MenuIntent mi;
            DlgIntent di;
            if (dlg_decode(in.key, &di)) {                            // the board's own keyboard, an application's dialog: its table has made the intent
                g_env.dialog_apply(g_env.user, &di);
                r->r = UR_TAKEN;
                return;
            }
            bool typed = intent_decode(in.key, &mi);                 // the board's own keyboard: the key table has made an intent of the key already
            int k = typed ? 0 : key_code(in.key);
            if (!typed && k < 0) {
                r->r = UR_BAD;
                snprintf(r->why, sizeof(r->why), "unknown key name");
                return;
            }
            if (!is_open()) {
                r->r = UR_BLOCKED;
                snprintf(r->why, sizeof(r->why), "the menu is closed: open it first");
                return;
            }
            if (typed) {
                g_env.give_intent(g_env.user, &mi);
            } else {
                press(k);
            }
            r->r = UR_TAKEN;
            return;
        }
        case UD_BACK:
            if (!is_open()) {
                r->r = UR_BLOCKED;
                snprintf(r->why, sizeof(r->why), "the menu is closed");
                return;
            }
            press_named("back");
            r->r = UR_TAKEN;
            return;
        case UD_OPEN:
            if (in.menu || !is_open()) {
                if (!is_open()) {
                    g_env.open_menu(g_env.user);
                }
            }
            if (in.target[0]) {
                if (!is_open() || !act()->goto_spec(in.target)) {
                    r->r = UR_GONE;
                    snprintf(r->why, sizeof(r->why), "no such screen");
                    return;
                }
            }
            r->r = UR_TAKEN;
            return;
        case UD_OP: {
            // An operation of the registry by name, with its arguments as the JSON object POST /v1/ops/<name> takes. Whatever the registry holds is reachable
            // here without code per operation (D21); a destructive one asks (D19a), the others run at once.
            bool from_rest = in.aux != 0;                    // uiapp_op_ask left the request in a record: this takes it over and opens its question
            Pending tmp;
            memset(&tmp, 0, sizeof(tmp));
            if (from_rest) {                                 // copied and freed first, so no return path below can leak the record
                ReqRec *rec = &g_rec[(int)(intptr_t)in.aux - 1];
                g_plat.lock();
                snprintf(tmp.op, sizeof(tmp.op), "%s", rec->op);
                snprintf(tmp.text, sizeof(tmp.text), "%s", rec->text);
                snprintf(tmp.query, sizeof(tmp.query), "%s", rec->query);
                memcpy(tmp.body, rec->body, rec->body_len);
                tmp.body[rec->body_len] = 0;
                tmp.body_len = rec->body_len;
                rec->used = false;
                g_plat.unlock();
            }
            if (strcmp(owner_of(m), "menu") && strcmp(owner_of(m), "closed")) {
                r->r = UR_BUSY;
                snprintf(r->why, sizeof(r->why), "the keys belong to %s now", owner_of(m));
                return;
            }
            if (from_rest) {
                if (g_pend.used) {
                    r->r = UR_BUSY;
                    snprintf(r->why, sizeof(r->why), "another destructive request waits for its answer");
                    return;
                }
                tmp.used = true;
                g_pend = tmp;
            } else {
                if (!ops_of(g_ctx)->find_op(in.item)) {
                    r->r = UR_GONE;
                    snprintf(r->why, sizeof(r->why), "no operation \"%s\" is registered", in.item);
                    return;
                }
                const char *body = in.args[0] ? in.args : "{}";
                if (!uiapp_op_destructive(in.item)) {
                    OpResult res;
                    int rc = rest_op_exec(g_ctx, in.item, "", body, (int)strlen(body), &res);
                    snprintf(r->why, sizeof(r->why), "%s", res.msg);
                    r->r = rc ? UR_REFUSED : UR_TAKEN;
                    return;
                }
                if (g_pend.used) {
                    r->r = UR_BUSY;
                    snprintf(r->why, sizeof(r->why), "another destructive request waits for its answer");
                    return;
                }
                g_pend.used = true;
                snprintf(g_pend.op, sizeof(g_pend.op), "%s", in.item);
                g_plat.lock();
                g_pend.text[0] = 0;
                for (int i = 0; i < g_ndest; i++) {
                    if (!strcmp(g_dest[i].op, in.item)) snprintf(g_pend.text, sizeof(g_pend.text), "%s", g_dest[i].text);
                }
                g_plat.unlock();
                if (!g_pend.text[0]) snprintf(g_pend.text, sizeof(g_pend.text), "Run the operation?");
                g_pend.query[0] = 0;
                snprintf(g_pend.body, sizeof(g_pend.body), "%s", body);
                g_pend.body_len = (int)strlen(g_pend.body);
            }
            if (!is_open()) {
                g_env.open_menu(g_env.user);            // the screen shows the question too
            }
            act()->remote_ask(g_pend.text);
            UicQuestion q;
            memset(&q, 0, sizeof(q));
            snprintf(q.text, sizeof(q.text), "%s", g_pend.text);
            q.destructive = true;
            if (!uic_ask(&g_svc, q, r->q)) {
                act()->remote_close();
                g_pend.used = false;
                r->r = UR_BUSY;
                snprintf(r->why, sizeof(r->why), "a question is open already");
                return;
            }
            r->r = UR_ASKED;
            return;
        }
        case UD_TEXT: {
            if (!is_open() || !m->entering() || !m->replace_text(in.value)) {
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "no text field is open");
                return;
            }
            if (in.submit) {
                press_named("return");
            }
            r->r = UR_TAKEN;
            return;
        }
        default:
            r->r = UR_BAD;
        }
    }

    bool question(UicQuestion *q) override
    {
        if (picture_up()) {                                  // Q7: the mode was changed and waits to be kept or to go back; any key on the screen keeps it
            snprintf(q->text, sizeof(q->text), "Keep this picture? The previous mode comes back when the time runs out.");   // constant: the poll reopens a question whose text changes
            q->destructive = false;
            snprintf(q->def, sizeof(q->def), "no");
            snprintf(q->yes_key, sizeof(q->yes_key), "any");
            snprintf(q->no_key, sizeof(q->no_key), "revert");
            q->timeout_ms = 8000;
            return true;
        }
        if (dialog_up()) {                                   // an application's yes/no question (the others are shown as `appdlg` in the snapshot)
            const DlgView *v = g_env.dialog_view(g_env.user);
            if (v->kind == DK_BUTTONS && (v->buttons & SVC_BUTTON_YES) && (v->buttons & SVC_BUTTON_NO)) {
                snprintf(q->text, sizeof(q->text), "%s", v->text);
                q->destructive = false;
                q->timeout_ms = v->remaining_s > 0 ? (uint32_t)v->remaining_s * 1000 : 0;
                if (q->timeout_ms) snprintf(q->def, sizeof(q->def), "no");
                snprintf(q->yes_key, sizeof(q->yes_key), "y");
                snprintf(q->no_key, sizeof(q->no_key), "n");
                return true;
            }
            return false;
        }
        if (!is_open()) return false;
        MenuCtl *m = act();
        char t[120];
        const char *d = m->dialog(t, sizeof(t));
        if (strcmp(d, "confirm")) return false;
        snprintf(q->text, sizeof(q->text), "%s", t);
        q->destructive = true;
        return true;
    }

    void run_pending()
    {
        OpResult res;
        int rc = rest_op_exec(g_ctx, g_pend.op, g_pend.query, g_pend.body, g_pend.body_len, &res);
        MenuCtl *m = act();
        m->say(res.msg[0] ? res.msg : (rc ? "failed" : "done"), rc != 0);
        uic_toast(&g_svc, res.msg[0] ? res.msg : (rc ? "failed" : "done"));
        g_pend.used = false;
    }

    void answer(const char *choice) override
    {
        if (picture_up() || dialog_yes_no()) {
            answer_owner(!strcmp(choice, "yes"));
            return;
        }
        MenuCtl *m = act();
        if (m->remote_asking() && g_pend.used) {
            m->remote_close();
            if (!strcmp(choice, "yes")) {
                run_pending();
            } else {
                g_pend.used = false;
                m->say("Cancelled", false);
            }
            return;
        }
        press_named(!strcmp(choice, "yes") ? "return" : "back");        // the keys the snapshot advertises (ret / esc: the menu's "<-" is ESC)
    }

    int jobs(UicJobInfo *out, int max) override
    {
        if (!is_open() || max < 1) return 0;
        MenuCtl *m = act();
        if (!m->busy()) return 0;
        char l[120], rt[40];
        bool active = true;
        m->row(0, l, sizeof(l), rt, sizeof(rt), &active, 0, 0);
        snprintf(out[0].key, sizeof(out[0].key), "menu.job");
        snprintf(out[0].kind, sizeof(out[0].kind), "file");
        snprintf(out[0].title, sizeof(out[0].title), "%s", l);
        snprintf(out[0].step, sizeof(out[0].step), "%s", rt);
        out[0].done = m->progress() < 0 ? 0 : m->progress();
        out[0].total = 100;
        out[0].cancelable = true;
        return 1;
    }

    bool cancel_job(const char *) override
    {
        press_named("back");
        return true;
    }

    void job_result(const char *, bool *ok, char *text, int n) override
    {
        MenuCtl *m = act();
        *ok = !m->status_bad();
        snprintf(text, n, "%s", m->status());
    }

    void set_text(const char *text) override { act()->replace_text(text); }

    void tick(uint32_t) override
    {
        MenuCtl *m = act();
        if (is_open()) {
            events_scan(m);
        } else {
            g_ev_area = g_ev_tab = -1;
            g_ev_sel = -2;
        }
        if (is_open() && m->pump(g_quiet)) {                  // the controller's own clock (T192.3 leak 9): a job's chunk, a wizard's press, the relist, the toast that ran out
            g_pumped = true;
        }
        int a = m->take_remote_answer();                  // the screen answered a remote request's question
        if (a >= 0 && g_pend.used) {
            if (a == 1) {
                run_pending();
            } else {
                g_pend.used = false;
            }
        }
        if (m->toast_revision() != g_toast_rev) {
            g_toast_rev = m->toast_revision();
            if (m->toast_text()[0]) {
                uic_toast(&g_svc, m->toast_text());
            }
        }
        if ((int32_t)(g_ctx->io->now_ms() - g_dest_at) >= 3000) {
            g_dest_at = g_ctx->io->now_ms();
            dest_refresh(m);
        }
    }
};

CoreDomain g_dom;

void entry_done(const char *text) { uic_history_add(&g_svc, text); }

}

// T192.3 D31 (Alex, 2026-11-10): the service is on for every core that runs this application (Gideon's application, trx64g, is a different program and
// untouched). Only a CoreCtx without a descriptor has none.

static bool service_core(const CoreCtx *c)
{
    return c && c->desc;
}


void uiapp_init(CoreCtx *c, const UiAppEnv *env, const UiAppPlatform *plat)
{
    g_env = *env;                                     // before the gate: uiapp_local_key calls give_key while the service is not up
    if (g_up || !service_core(c)) {
        return;
    }
    g_ctx = c;
    g_plat = *plat;
    static const UicPort port = { 0, p_now, p_lock, p_unlock, p_sleep, p_ui, p_entropy, p_load, p_save, p_remove, p_log };
    UicPort p = port;
    char probe[HANDOVER_PATH_MAX + 20];
    if (!token_path(probe, sizeof(probe))) {
        p.file_save = 0;                              // no core folder (JTAG, developer): no token file, no authentication (T192.1 §5)
        p.file_load = 0;
        p.file_remove = 0;
    }
    uic_init(&g_svc, &p, &g_dom, g_snap, sizeof(g_snap));
    MenuCtl::entry_hook = entry_done;
    dest_refresh(env->active(env->user));
    g_dest_at = c->io->now_ms();
    g_up = true;
    rest_ui_op_destructive = uiapp_op_destructive;
    rest_ui_op_ask = uiapp_op_ask;
    __asm__ volatile("" ::: "memory");               // the hooks first: whoever sees rest_uic finds them set
    rest_uic = &g_svc;
}

void uiapp_reset_for_test()
{
    g_ev_area = g_ev_tab = -1;
    g_ev_sel = -2;
    g_ev_dirty = -1;
    g_ev_media = 0;
    g_up = false;
    g_ndest = 0;
    g_pend.used = false;
    memset(g_rec, 0, sizeof(g_rec));
    g_toast_rev = 0;
    g_dest_at = 0;
    MenuCtl::entry_hook = 0;
    rest_uic = 0;
    rest_ui_op_destructive = 0;
    rest_ui_op_ask = 0;
}

UicService *uiapp_service() { return g_up ? &g_svc : 0; }

bool uiapp_pass(bool changed)
{
    if (!g_up) return false;
    if (changed) uic_note(&g_svc);
    g_quiet = !changed;
    g_pumped = false;
    bool applied = uic_service(&g_svc) > 0;
    if (g_pumped) uic_note(&g_svc);
    return applied || g_pumped;
}

void uiapp_changed()
{
    if (g_up) uic_note(&g_svc);
}

void uiapp_local_key(int key)
{
    if (!g_up) {
        g_env.give_key(g_env.user, key);
        return;
    }
    UicIntent in;
    memset(&in, 0, sizeof(in));
    in.kind = UD_KEY;
    in.local = true;
    if (picture_up()) {                                                // any key keeps the picture: the answer "yes" of the service's question (opened now when the pass has not yet)
        UicQuestion q;
        memset(&q, 0, sizeof(q));
        if (!g_svc.q_open && g_dom.question(&q)) {
            uic_ask(&g_svc, q, 0);
        }
        in.kind = UD_ANSWER;
        snprintf(in.choice, sizeof(in.choice), (key == KEY_LEFT || key == KEY_ESCAPE) ? "no" : "yes");   // T187 D5
    } else if (dialog_up()) {                                         // an application's dialog: its own key table makes the intent
        DlgIntent di;
        g_env.dialog_key_intent(g_env.user, key, &di);
        snprintf(in.key, sizeof(in.key), "D%d.%d", (int)di.id, di.arg);
    } else {
        MenuIntent mi = act()->resolve(key);                          // the key table makes the intent; the service applies it, local first (T192.3 S6, S10)
        snprintf(in.key, sizeof(in.key), "@%d.%d.%d.%d.%d", (int)mi.id, (int)mi.arg, mi.key, (int)mi.stage, (int)mi.row);
    }
    UicReply r;
    uic_local(&g_svc, in, &r);
}

void uiapp_local_open()
{
    if (!g_up) {
        g_env.open_menu(g_env.user);
        return;
    }
    UicIntent in;
    memset(&in, 0, sizeof(in));
    in.kind = UD_OPEN;
    in.menu = true;
    in.local = true;
    UicReply r;
    uic_local(&g_svc, in, &r);
}

void uiapp_local_leave()
{
    if (!g_up) {
        g_env.leave(g_env.user);
        return;
    }
    UicIntent in;
    memset(&in, 0, sizeof(in));
    in.kind = UD_KEY;
    in.local = true;
    snprintf(in.key, sizeof(in.key), "@%d.0.0.%d.-1", (int)MI_LEAVE, (int)KS_MAIN);
    UicReply r;
    uic_local(&g_svc, in, &r);
}

int uiapp_render(char *out, int cap)
{
    if (!g_up) {
        return -1;
    }
    return uic_render(&g_svc, out, cap);
}

bool uiapp_op_destructive(const char *op)
{
    bool d = false;
    g_plat.lock();
    for (int i = 0; i < g_ndest; i++) {
        if (!strcmp(g_dest[i].op, op)) {
            d = true;
        }
    }
    g_plat.unlock();
    return d;
}

bool uiapp_op_ask(const char *op, const char *query, const char *body, int body_len, char *reply, int cap, int *status)
{
    char text[96] = "Run the operation?";
    g_plat.lock();
    for (int i = 0; i < g_ndest; i++) {
        if (!strcmp(g_dest[i].op, op)) {
            snprintf(text, sizeof(text), "%s", g_dest[i].text);
        }
    }
    g_plat.unlock();
    if (!strcmp(op, "files.delete_job")) {                      // the words the menu asks with for what the operation will delete (the marks, else the file under the cursor)
        FileMgr::delete_question(g_ctx, text, sizeof(text));
    }
    int idx = -1;
    g_plat.lock();
    uint32_t now = g_ctx->io->now_ms();
    for (int i = 0; i < 2; i++) {
        if (g_rec[i].used && (int32_t)(now - g_rec[i].at) > REC_STALE_MS) {
            g_rec[i].used = false;                        // its intent was dropped: nobody will free it
        }
        if (idx < 0 && !g_rec[i].used) {
            idx = i;
        }
    }
    if (idx >= 0) {                                       // claimed and filled in one go: the stale reclaim never sees a half-written record
        ReqRec &rec = g_rec[idx];
        rec.used = true;
        rec.at = now;
        snprintf(rec.op, sizeof(rec.op), "%s", op);
        snprintf(rec.text, sizeof(rec.text), "%s", text);
        snprintf(rec.query, sizeof(rec.query), "%s", query ? query : "");
        int bl = body_len < HTTP_BODY_MAX ? body_len : HTTP_BODY_MAX;
        memcpy(rec.body, body, bl);
        rec.body[bl] = 0;
        rec.body_len = bl;
    }
    g_plat.unlock();
    if (idx < 0) {
        *status = 409;
        return snprintf(reply, cap, "{\"r\":\"busy\",\"why\":\"another destructive request waits for its answer\"}") > 0;
    }
    UicIntent in;
    memset(&in, 0, sizeof(in));
    in.kind = UD_OP;
    snprintf(in.item, sizeof(in.item), "%s", op);
    in.aux = (void *)(intptr_t)(idx + 1);
    UicReply r;
    uic_submit(&g_svc, in, &r);
    UicW w(reply, cap);
    w.open('{');
    w.ks("r", uic_r_name(r.r));
    if (r.q[0]) w.ks("q", r.q);
    if (r.why[0]) w.ks("why", r.why);
    w.ks("op", op);
    w.close('}');
    *status = r.r == UR_ASKED ? 202 : 409;
    return w.ok;
}
