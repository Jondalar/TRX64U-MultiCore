#include "hostdomain.h"
#include "hostdecl.h"
#include "host_core.h"
#include "webui.h"
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

#define SNAP_MAX 12000                    // under the host's WebSocket message limit (webui.cc ws_msg_max 12288) with room for the envelope: a snapshot is one message
#define QMAX 320
#define ROWS_WINDOW 24                    // rows of a list in a snapshot: the window around the cursor
#define PICK_MAX 24
#define CMD_BASE 0x10000                  // queue entries from CMD_BASE to CMD_BASE + CMD_N - 1 are commands; HC_ACT_KEY values (host_core.h) are intents; the rest are keys
#define CMD_N 16

enum DlgKind { DK_NONE = 0, DK_PAGE, DK_ENTRY, DK_PICK };

struct Dlg {
    int kind;
    HostPageInfo page;
    int yes_key, no_key;
    bool destructive;
    char entry_title[96], entry_sub[140], entry_typed[96], entry_hint[124];
    char pick_title[96], pick_hint[124];
    char pick_label[PICK_MAX][60];
    int pick_n, pick_sel;
};

// A command in the queue: it runs when it is popped, in order with the keys before it (a view change is a key, so a focus or a field step that belongs to the
// new view has to wait for it).
enum CmdKind { CMD_FOCUS_ROW = 1, CMD_FOCUS_FIELD, CMD_STEP, CMD_SET, CMD_GOTO, CMD_ARM_TEXT };
struct Cmd {
    int kind;
    char a[40], b[96];
    int n;
};

HostModel *g_m;
HostApiPlatform g_plat;
UicService g_svc;
char g_snap[SNAP_MAX];
bool g_up;

int g_q[QMAX];
int g_qh, g_qn;
Cmd g_cmd[CMD_N];
int g_cmd_next;
bool g_cmd_busy[CMD_N];

Dlg g_dlg;
bool g_waiting;
bool g_in_pump;
char g_ask_text[120];
int g_ask_yes, g_ask_no;
bool g_ask_remote;
char g_arm_text[96];
bool g_arm_text_set;

char g_flow_id[8];
bool g_flow_on;
bool g_flow_cancel_sent;
bool g_flow_bad;
char g_flow_last_note[100];

// The destructive row action that waits for its answer. The screen owns the arm (hostmodel.h, HostModel::arm): the question is open exactly as long as the screen is
// armed with `token`. `used` with token 0: the question is being raised (claimed, the arm not made yet).
struct Pend {
    bool used;
    uint32_t token;
    char ask[120];                         // what UicQuestion::text holds
};
Pend g_pend;
bool g_key_local = true;                   // the key the screen handles now came from the board

const HostDecl &D() { return host_decl(); }

// ---- the queue -------------------------------------------------------------------------------------------------------------------------
// A remote key waits at the tail and may not take the last QLOCAL_RESERVE places; the board's own key goes in front of whatever waits and may use them (T192 L5: the person at
// the device is never behind a remote client, and a key of theirs is not dropped without a word).
#define QLOCAL_RESERVE 16
#define QLOCAL 0x40000000                  // set on a key of the board's own: the screen tells a key at the board from one of the queue's
bool q_push(int v, bool local = false)
{
    if (g_qn >= (local ? QMAX : QMAX - QLOCAL_RESERVE)) {
        return false;
    }
    if (local) {
        g_qh = (g_qh + QMAX - 1) % QMAX;
        g_q[g_qh] = v | QLOCAL;
    } else {
        g_q[(g_qh + g_qn) % QMAX] = v;
    }
    g_qn++;
    return true;
}

// An intent of host_core in the queue, in order with the keys and commands around it; the screen takes it as it takes a key (on_key).
bool q_act(int act, bool local)
{
    return q_push(HC_ACT_KEY(act), local);
}

bool q_cmd(int kind, const char *a, const char *b, int n)
{
    for (int t = 0; t < CMD_N; t++) {
        int i = (g_cmd_next + t) % CMD_N;
        if (!g_cmd_busy[i]) {
            g_cmd_busy[i] = true;
            g_cmd[i].kind = kind;
            snprintf(g_cmd[i].a, sizeof(g_cmd[i].a), "%s", a ? a : "");
            snprintf(g_cmd[i].b, sizeof(g_cmd[i].b), "%s", b ? b : "");
            g_cmd[i].n = n;
            g_cmd_next = (i + 1) % CMD_N;
            if (!q_push(CMD_BASE + i)) {
                g_cmd_busy[i] = false;
                return false;
            }
            return true;
        }
    }
    return false;
}

void run_cmd(const Cmd &c)
{
    char why[120];
    why[0] = 0;
    switch (c.kind) {
    case CMD_FOCUS_ROW: g_m->focus(c.a, c.n); break;
    case CMD_FOCUS_FIELD: g_m->field_focus(c.a); break;
    case CMD_STEP: g_m->field_step(c.a, c.n, why, sizeof(why)); break;
    case CMD_SET: g_m->field_set(c.a, c.b, why, sizeof(why)); break;
    case CMD_GOTO: g_m->browse_goto(c.a, why, sizeof(why)); break;
    case CMD_ARM_TEXT:
        snprintf(g_arm_text, sizeof(g_arm_text), "%s", c.b);
        g_arm_text_set = true;
        break;
    }
    if (why[0]) {
        hostapi_note(why, true);
    }
}

// ---- key names ---------------------------------------------------------------------------------------------------------------------------
int key_code(const char *name)
{
    static const struct { const char *n; int k; } K[] = {
        { "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "ret", KEY_RETURN }, { "return", KEY_RETURN },
        { "enter", KEY_RETURN }, { "back", KEY_ESCAPE }, { "esc", KEY_ESCAPE }, { "del", KEY_DELETE }, { "bksp", KEY_BACK }, { "runstop", KEY_BREAK },
        { "space", KEY_SPACE }, { "home", KEY_HOME }, { "pgup", KEY_PAGEUP }, { "pgdn", KEY_PAGEDOWN }, { "tab", KEY_TAB }, { "any", KEY_SPACE },
        { "ins", KEY_INSERT }, { "clear", KEY_CLEAR }, { "end", KEY_END },
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

// A key code as a `key` intent names it (what the snapshot advertises for a question).
void key_name(int k, char *out)
{
    out[0] = out[1] = out[2] = out[3] = out[4] = 0;
    if (k == KEY_RETURN) strcpy(out, "ret");
    else if (k == KEY_ESCAPE) strcpy(out, "esc");
    else if (k == KEY_LEFT) strcpy(out, "left");
    else if (k >= 0x20 && k < 0x7F) { out[0] = (char)k; }
    else snprintf(out, 8, "#%d", k);
}

// The intent that opens a view (the five navigation keys are intents of host_core).
int nav_act(int view)
{
    switch (view) {
    case HVW_MENU: return HCA_NAV_MENU;
    case HVW_BROWSE: return HCA_NAV_BROWSE;
    case HVW_EDIT: return HCA_NAV_EDIT;
    case HVW_NET: return HCA_NAV_NET;
    case HVW_SET: return HCA_NAV_SETTINGS;
    }
    return HCA_NONE;
}

bool q_act(int act, bool local = false);

// ---- rows and their ids -------------------------------------------------------------------------------------------------------------------
struct RowKey {
    char id[UIC_ID_MAX + 1];
};

void make_row_id(const char *prefix, const char *ident, int dup, char *out, int cap)
{
    uic_id_make(prefix, ident, out, cap);
    if (dup) {
        char suf[8];
        snprintf(suf, sizeof(suf), ".%d", dup + 1);
        int sl = (int)strlen(suf), l = (int)strlen(out);
        if (l + sl > UIC_ID_MAX) {
            l = UIC_ID_MAX - sl;
        }
        memcpy(out + l, suf, sl + 1);
    }
}

char g_seen[200][UIC_ID_MAX + 1];

// The id of row i of the list in the page; the same rule in the snapshot and in the lookup (a row whose identity another shares gets an ordinal).
int find_row(const HPage &pg, const char *id)
{
    if (!pg.list) {
        return -1;
    }
    int n = g_m->rows(pg.list);
    int nseen = 0;
    for (int i = 0; i < n; i++) {
        HRow r;
        if (!g_m->row(pg.list, i, &r)) {
            continue;
        }
        int dup = 0;
        for (int k = 0; k < nseen; k++) {
            char tmp[UIC_ID_MAX + 1];
            uic_id_make(pg.prefix, r.ident, tmp, sizeof(tmp));
            if (!strcmp(g_seen[k], tmp)) {
                dup++;
            }
        }
        char rid[UIC_ID_MAX + 1];
        make_row_id(pg.prefix, r.ident, dup, rid, sizeof(rid));
        if (nseen < 200) {
            uic_id_make(pg.prefix, r.ident, g_seen[nseen++], UIC_ID_MAX + 1);
        }
        if (!strcmp(rid, id)) {
            return i;
        }
    }
    return -1;
}

const HItem *find_item_on(const HPage &pg, const char *id)
{
    const HostDecl &d = D();
    for (int i = 0; i < d.nitems; i++) {
        if (strcmp(d.items[i].page, pg.id)) {
            continue;
        }
        char iid[UIC_ID_MAX + 1];
        hd_item_id(d.items[i], iid, sizeof(iid));
        if (!strcmp(iid, id)) {
            return &d.items[i];
        }
    }
    return 0;
}

// An item by its full id, its short id, or its field key, on any page (a setting is set from wherever the menu is).
const HItem *find_item_any(const char *name)
{
    const HostDecl &d = D();
    const HItem *short_hit = 0;
    int shorts = 0;
    for (int i = 0; i < d.nitems; i++) {
        char iid[UIC_ID_MAX + 1];
        hd_item_id(d.items[i], iid, sizeof(iid));
        if (!strcmp(iid, name) || !strcmp(d.items[i].field, name)) {
            return &d.items[i];
        }
        if (!strcmp(d.items[i].id, name)) {
            short_hit = &d.items[i];
            shorts++;
        }
    }
    return shorts == 1 ? short_hit : 0;
}

const char *kind_name(int k)
{
    switch (k) {
    case HI_TOGGLE: return "toggle";
    case HI_VALUE: return "value";
    case HI_CHOICE: return "choice";
    case HI_TEXT: return "text";
    case HI_BUTTON: return "button";
    }
    return "info";
}

// ---- the owner of the keys --------------------------------------------------------------------------------------------------------------
const char *owner()
{
    if (g_m->picture_pending()) {
        return "picture";
    }
    if (g_waiting && (g_dlg.kind == DK_PAGE || g_dlg.kind == DK_ENTRY)) {
        return "dialog";                           // a pick list is rows to select and activate: the menu's own kind of screen
    }
    if (g_ask_text[0]) {
        return "dialog";
    }
    if (g_flow_on) {
        return "job";
    }
    return "menu";
}

bool action_applies(const HAction &a, HostModel &m)
{
    if (a.view >= 0 && a.view != m.view() && !(a.flags & HAF_GLOBAL)) {
        // a view-bound action is offered on its own view and runs from another one after the navigation key
    }
    if (a.mode >= 0 && m.view() == HVW_BROWSE && a.mode != m.mode()) {
        return false;
    }
    return true;
}

// Queues the action: the navigation key if the menu is on another view, the focus of its row or field, then its keys or its field step.
int queue_action(const HAction &a, int row, const char *text, char *why, int whycap)
{
    const int act = a.kind == HA_KEYS ? hc_act_by_name(a.name) : HCA_NONE;
    if (a.kind == HA_KEYS && act == HCA_NONE) {
        snprintf(why, whycap, "%s is not an intent of the host", a.name);
        return UR_BAD;
    }
    const char *list_src = a.src;
    bool nav = a.view >= 0 && a.view != g_m->view() && a.view != HVW_COLORS;
    if (a.mode >= 0 && !nav && g_m->view() == HVW_BROWSE && g_m->mode() != a.mode) {
        snprintf(why, whycap, "not in this mode of the browser");
        return UR_REFUSED;
    }
    if ((a.flags & HAF_ROW) && row < 0) {
        snprintf(why, whycap, "%s needs a row (target)", a.name);
        return UR_BAD;
    }
    if (g_qn + 8 > QMAX) {
        snprintf(why, whycap, "too many keys waiting");
        return UR_BUSY;
    }
    if (nav) {
        int na = nav_act(a.view);
        if (na != HCA_NONE) {
            q_act(na);
        }
    }
    bool ok = true;
    if (list_src && row >= 0) {
        ok = q_cmd(CMD_FOCUS_ROW, list_src, 0, row) && ok;
    }
    if (a.kind == HA_KEYS) {
        if (a.field) {
            ok = q_cmd(CMD_FOCUS_FIELD, a.field, 0, 0) && ok;
        }
        if ((a.flags & HAF_TEXT) && text && text[0]) {
            ok = q_cmd(CMD_ARM_TEXT, 0, text, 0) && ok;
        }
        q_act(act);
    } else if (a.kind == HA_FIELD) {
        ok = q_cmd(CMD_STEP, a.field, 0, a.dir) && ok;
    }
    if (!ok) {
        snprintf(why, whycap, "too many commands waiting");
        return UR_BUSY;
    }
    return UR_TAKEN;
}

}  // namespace

// ---- the domain -----------------------------------------------------------------------------------------------------------------------------
namespace {

void item_json(UicW &w, const HItem &it, const char *focus_field, HostModel &m, bool *focused_out, char *focus_id)
{
    HField f;
    memset(&f, 0, sizeof(f));
    if (!m.field(it.field, &f)) {
        f.visible = true;
        f.enabled = true;
    }
    if (!f.visible) {
        return;
    }
    char id[UIC_ID_MAX + 1];
    hd_item_id(it, id, sizeof(id));
    w.sep();
    w.open('{');
    w.ks("id", id);
    w.ks("kind", kind_name(it.kind));
    w.ks("title", it.title);
    if (f.value[0]) {
        w.ks("value", f.value);
    }
    w.kb("enabled", f.enabled);
    if (it.kind == HI_TOGGLE) {
        w.kb("on", f.on);
    }
    if (it.activate) {
        w.ks("act", it.activate);
    }
    if (focus_field && !strcmp(focus_field, it.field)) {
        w.kb("focused", true);
        if (f.help[0]) {
            w.ks("help", f.help);
        }
        snprintf(focus_id, UIC_ID_MAX + 1, "%s", id);
        *focused_out = true;
    }
    w.close('}');
}

void ops_json(UicW &w, HostModel &m)
{
    const HostDecl &d = D();
    const HPage &pg = hd_current_page(d, m);
    w.kopen("ops", '[');
    for (int i = 0; i < d.nactions; i++) {
        const HAction &a = d.actions[i];
        bool here = (a.flags & HAF_GLOBAL) || a.view < 0 || a.view == pg.view;
        if (!here || !action_applies(a, m)) {
            continue;
        }
        if (w.cap - w.p < 400) {
            break;
        }
        w.sep();
        w.open('{');
        w.ks("id", a.name);
        w.ks("title", a.title);
        if (a.flags & HAF_DESTRUCTIVE) w.kb("destructive", true);
        if (a.flags & HAF_ROW) w.kb("row", true);
        if (a.flags & HAF_TEXT) w.kb("text", true);
        if (a.flags & HAF_FLOW) w.kb("flow", true);
        w.close('}');
    }
    w.close(']');
}

class HostDomain : public UicDomain {
public:
    const char *app() override { return "host"; }
    const char *keys_owner() override { return owner(); }

    void snapshot(UicW &w) override
    {
        const HostDecl &d = D();
        const HPage &pg = hd_current_page(d, *g_m);
        char tmp[200];
        w.ks("screen", pg.id);
        w.ks("title", pg.title);
        if (g_m->text("crumb", tmp, sizeof(tmp))) {
            w.ks("crumb", tmp);
        }
        w.kopen("areas", '[');
        for (int i = 0; i < d.npages; i++) {
            if (!d.pages[i].nav) {
                continue;
            }
            w.sep();
            w.open('{');
            w.ks("id", d.pages[i].id);
            w.ks("title", d.pages[i].title);
            w.ks("key", d.pages[i].nav);
            w.kb("active", d.pages[i].view == pg.view);
            w.close('}');
        }
        w.close(']');
        w.kopen("tabs", '[');
        w.close(']');
        // the cursor
        char cursor[80];
        cursor[0] = 0;
        g_m->text("cursor", cursor, sizeof(cursor));
        char focus[UIC_ID_MAX + 1];
        focus[0] = 0;
        bool focused = false;
        int rows_first = 0, rows_total = 0;
        w.kopen("items", '[');
        if (g_dlg.kind == DK_PICK) {
            for (int i = 0; i < g_dlg.pick_n; i++) {
                char id[UIC_ID_MAX + 1];
                make_row_id("pick:", g_dlg.pick_label[i], 0, id, sizeof(id));
                w.sep();
                w.open('{');
                w.ks("id", id);
                w.ks("kind", "choice");
                w.ks("title", g_dlg.pick_label[i]);
                w.kb("enabled", true);
                if (i == g_dlg.pick_sel) {
                    w.kb("focused", true);
                    snprintf(focus, sizeof(focus), "%s", id);
                    focused = true;
                }
                w.close('}');
            }
        } else {
            for (int i = 0; i < d.nitems; i++) {
                if (!strcmp(d.items[i].page, pg.id)) {
                    item_json(w, d.items[i], cursor, *g_m, &focused, focus);
                }
            }
            int first = 0, total = 0;
            if (pg.list) {
                total = g_m->rows(pg.list);
                int cur = g_m->cursor(pg.list);
                first = cur - 4 < 0 ? 0 : cur - 4;                       // the cursor row is near the top of the window: a cut at the size limit keeps it
                int last = first + ROWS_WINDOW < total ? first + ROWS_WINDOW : total;
                const char *const *acts = pg.view == HVW_EDIT ? d.row_actions_edit : pg.view == HVW_BROWSE ? d.row_actions_file : d.row_actions_entry;
                // the ordinals of rows that share an identity count from the first row, whatever the window
                int nseen = 0;
                for (int i = 0; i < last; i++) {
                    HRow r;
                    if (!g_m->row(pg.list, i, &r)) {
                        continue;
                    }
                    char base[UIC_ID_MAX + 1];
                    uic_id_make(pg.prefix, r.ident, base, sizeof(base));
                    int dup = 0;
                    for (int k = 0; k < nseen; k++) {
                        if (!strcmp(g_seen[k], base)) dup++;
                    }
                    if (nseen < 200) snprintf(g_seen[nseen++], UIC_ID_MAX + 1, "%s", base);
                    if (i < first) {
                        continue;
                    }
                    if (w.cap - w.p < 900) {
                        w.kb("more", true);
                        break;
                    }
                    char rid[UIC_ID_MAX + 1];
                    make_row_id(pg.prefix, r.ident, dup, rid, sizeof(rid));
                    w.sep();
                    w.open('{');
                    w.ks("id", rid);
                    w.ks("kind", !strcmp(pg.list, "entry") ? "entry" : r.dir ? "dir" : "file");
                    w.ks("title", r.title);
                    if (r.sub[0]) w.ks("sub", r.sub);
                    if (r.badge[0]) w.ks("badge", r.badge);
                    if (r.value[0]) w.ks("value", r.value);
                    w.kb("enabled", r.enabled);
                    if (r.def) w.kb("default", true);
                    if (r.configs) w.ki("configs", r.configs);
                    if (!strcmp(pg.list, "entry")) w.kb("startable", r.startable);
                    if (r.marked) w.kb("marked", true);
                    if (acts) {
                        w.kopen("acts", '[');
                        for (const char *const *a = acts; *a; a++) {
                            w.sep();
                            w.str(*a);
                        }
                        w.close(']');
                    }
                    if (i == cur) {
                        w.kb("focused", true);
                        snprintf(focus, sizeof(focus), "%s", rid);
                        focused = true;
                    }
                    w.close('}');
                }
            }
            rows_first = first;
            rows_total = total;
        }
        w.close(']');
        w.ks("focus", focused ? focus : "");
        w.kopen("rows", '{');
        w.ki("first", rows_first);
        w.ki("total", g_dlg.kind == DK_PICK ? g_dlg.pick_n : rows_total);
        w.close('}');
        if (g_m->text("path", tmp, sizeof(tmp))) {
            w.ks("path", tmp);
        }
        if (pg.view == HVW_BROWSE) {
            w.ks("mode", g_m->mode() == HMODE_PICK ? "pick" : g_m->mode() == HMODE_INSPECT ? "inspect" : "normal");
            if (g_m->text("detail", tmp, sizeof(tmp))) {
                w.ks("detail", tmp);
            }
        }
        if (g_m->text("footer", tmp, sizeof(tmp))) {
            w.kopen("footer", '{');
            w.ks("legend", tmp);
            w.close('}');
        }
        w.kopen("status", '{');
        if (g_m->text("note", tmp, sizeof(tmp))) {
            w.ks("text", tmp);
        } else {
            w.ks("text", "");
        }
        w.kb("bad", g_flow_bad);
        w.close('}');
        if (g_m->text("countdown", tmp, sizeof(tmp))) {
            w.ks("countdown", tmp);
        }
        w.kb("picture_pending", g_m->picture_pending());
        // a header: what the chips say
        w.kopen("header", '{');
        if (g_m->text("chips", tmp, sizeof(tmp))) {
            w.ks("chips", tmp);
        }
        w.close('}');
        // the modal the app waits in
        if (g_dlg.kind == DK_PAGE) {
            w.kopen("modal", '{');
            w.ks("kind", g_dlg.page.bar >= 0 ? "progress" : "page");
            w.ks("title", g_dlg.page.title);
            w.kopen("lines", '[');
            for (int i = 0; i < g_dlg.page.nlines; i++) {
                w.sep();
                w.str(g_dlg.page.line[i]);
            }
            w.close(']');
            if (g_dlg.page.hint[0]) w.ks("hint", g_dlg.page.hint);
            if (g_dlg.page.msg[0]) w.ks("msg", g_dlg.page.msg);
            if (g_dlg.page.input) w.ks("typed", g_dlg.page.typed);
            if (g_dlg.page.bar >= 0) w.ki("bar", g_dlg.page.bar);
            if (g_dlg.page.pct[0]) w.ks("pct", g_dlg.page.pct);
            w.close('}');
        } else if (g_dlg.kind == DK_ENTRY) {
            w.kopen("modal", '{');
            w.ks("kind", "entry");
            w.ks("title", g_dlg.entry_title);
            w.ks("sub", g_dlg.entry_sub);
            w.ks("typed", g_dlg.entry_typed);
            w.ks("hint", g_dlg.entry_hint);
            w.close('}');
        } else if (g_dlg.kind == DK_PICK) {
            w.kopen("modal", '{');
            w.ks("kind", "pick");
            w.ks("title", g_dlg.pick_title);
            if (g_dlg.pick_hint[0]) w.ks("hint", g_dlg.pick_hint);
            w.close('}');
        } else if (g_ask_text[0]) {
            w.kopen("modal", '{');
            w.ks("kind", "page");
            w.ks("title", g_ask_text);
            w.close('}');
        }
        ops_json(w, *g_m);
    }

    // ---- intents -------------------------------------------------------------------------------------------------------------------------
    void intent(const UicIntent &in, UicReply *r) override
    {
        switch (in.kind) {
        case UD_SELECT:
        case UD_ACTIVATE:
            select_or_activate(in, r);
            return;
        case UD_SET:
            do_set(in, r);
            return;
        case UD_OP:
            do_op(in, r);
            return;
        case UD_KEY:
            do_key(in, r);
            return;
        case UD_TEXT: {                                   // T192 D21: the open text field gets the text (and RETURN with `submit`)
            if (g_dlg.kind != DK_ENTRY && !(g_dlg.kind == DK_PAGE && g_dlg.page.input)) {
                r->r = UR_BLOCKED;
                snprintf(r->why, sizeof(r->why), "no text field is open");
                return;
            }
            int n = (int)strlen(in.value);
            if (n + 2 + g_qn > QMAX) {
                r->r = UR_BUSY;
                snprintf(r->why, sizeof(r->why), "too much text at once");
                return;
            }
            if (g_dlg.kind == DK_ENTRY) {
                q_push(KEY_CLEAR);
            }
            for (int i = 0; i < n; i++) {
                q_push((unsigned char)in.value[i]);
            }
            if (in.submit) {
                q_push(KEY_RETURN);
            }
            r->r = UR_TAKEN;
            return;
        }
        case UD_BACK: {
            if (g_pend.used) {                                // back on a question the screen shows is its no
                Pend p = g_pend;
                g_pend.used = false;
                if (p.token) {
                    g_m->disarm(p.token);
                }
                uic_close(&g_svc, "remote", "no");
                hostapi_note("Cancelled. Nothing was changed.", false);
                r->r = UR_TAKEN;
                return;
            }
            if (g_dlg.kind == DK_NONE && !g_ask_text[0] && !g_flow_on) {
                q_act(HCA_BACK);
                r->r = UR_TAKEN;
                return;
            }
            q_push(g_dlg.kind == DK_PAGE ? g_dlg.no_key : g_ask_text[0] ? g_ask_no : KEY_ESCAPE);
            r->r = UR_TAKEN;
            return;
        }
        case UD_OPEN:
            do_open(in, r);
            return;
        default:
            r->r = UR_BAD;
        }
    }

    void select_or_activate(const UicIntent &in, UicReply *r)
    {
        const HostDecl &d = D();
        const HPage &pg = hd_current_page(d, *g_m);
        if (g_dlg.kind == DK_PICK) {
            for (int i = 0; i < g_dlg.pick_n; i++) {
                char id[UIC_ID_MAX + 1];
                make_row_id("pick:", g_dlg.pick_label[i], 0, id, sizeof(id));
                if (!strcmp(id, in.item)) {
                    int delta = i - g_dlg.pick_sel;
                    for (int k = 0; k < (delta < 0 ? -delta : delta) && g_qn < QMAX - 2; k++) {
                        q_push(delta < 0 ? KEY_UP : KEY_DOWN);
                    }
                    if (in.kind == UD_ACTIVATE) {
                        q_push(KEY_RETURN);
                    }
                    r->r = UR_TAKEN;
                    return;
                }
            }
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no choice with that id");
            return;
        }
        if (const HItem *it = find_item_on(pg, in.item)) {
            HField f;
            memset(&f, 0, sizeof(f));
            g_m->field(it->field, &f);
            if (!f.visible && f.value[0] == 0 && f.help[0] == 0 && false) {
                r->r = UR_GONE;
                return;
            }
            if (it->kind == HI_INFO && !it->activate) {
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "that line only shows a state");
                return;
            }
            if (in.kind == UD_SELECT) {
                g_m->field_focus(it->field);
                r->r = UR_TAKEN;
                return;
            }
            if (!it->activate) {
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "set it with a value (a typed address)");
                g_m->field_focus(it->field);
                return;
            }
            if (!f.enabled) {
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "%s", f.help[0] ? f.help : "that row does nothing now");
                return;
            }
            g_m->field_focus(it->field);
            run_action_named(it->activate, -1, "", r);
            return;
        }
        int idx = find_row(pg, in.item);
        if (idx < 0) {
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no row or item with that id on this screen");
            return;
        }
        if (in.kind == UD_SELECT) {
            g_m->focus(pg.list, idx);
            r->r = UR_TAKEN;
            return;
        }
        if (!pg.activate) {
            g_m->focus(pg.list, idx);
            r->r = UR_REFUSED;
            snprintf(r->why, sizeof(r->why), "this screen's rows take actions: use op with one of the row's acts");
            return;
        }
        run_action_named(pg.activate, idx, "", r);
    }

    void do_set(const UicIntent &in, UicReply *r)
    {
        const char *name = in.key[0] ? in.key : in.item;
        const HItem *it = find_item_any(name);
        if (!it) {
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no such setting");
            return;
        }
        HField f;
        memset(&f, 0, sizeof(f));
        g_m->field(it->field, &f);
        if (it->kind == HI_INFO || it->kind == HI_BUTTON) {
            r->r = UR_REFUSED;
            snprintf(r->why, sizeof(r->why), "that item has no value to set (activate it)");
            return;
        }
        if (!f.enabled) {
            r->r = UR_REFUSED;
            snprintf(r->why, sizeof(r->why), "%s", f.help[0] ? f.help : "that setting does nothing now");
            return;
        }
        r->why[0] = 0;
        int rc = g_m->field_set(it->field, in.value, r->why, sizeof(r->why));
        r->r = rc == 0 ? UR_TAKEN : UR_REFUSED;
    }

    // An action by name, with the row of its list and a text; the destructive ones ask first (D19a).
    void run_action_named(const char *name, int row, const char *text, UicReply *r)
    {
        const HAction *a = hd_action(D(), name);
        if (!a) {
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no such action");
            return;
        }
        run_action(*a, row, text, r);
    }

    // The screen's arm that belongs to a declared action ("key, same key"), or 0.
    static int arm_act_of(const HAction &a)
    {
        if (!(a.flags & HAF_DESTRUCTIVE) || a.kind != HA_KEYS) {
            return HARM_NONE;
        }
        if (!strcmp(a.name, "entry.remove")) return HARM_ENTRY_REMOVE;
        if (!strcmp(a.name, "browse.delete")) return HARM_FILE_DELETE;
        if (!strcmp(a.name, "wifi.forget")) return HARM_WIFI_FORGET;
        return HARM_NONE;
    }

    void run_action(const HAction &a, int row, const char *text, UicReply *r)
    {
        if (a.flags & HAF_DESTRUCTIVE) {
            const int act = arm_act_of(a);
            if (act) {
                ask_on_screen(a, act, row, r);
                return;
            }
            if (!(a.flags & HAF_FLOW)) {                      // no way to ask: it does not run (a flow's own page is its question: Cores and ROMs, delete)
                r->r = UR_REFUSED;
                snprintf(r->why, sizeof(r->why), "%s has no confirmation", a.name);
                return;
            }
        }
        r->why[0] = 0;
        r->r = queue_action(a, row, text, r->why, sizeof(r->why));
    }

    // T192 D21: the question is the screen's own arm. The question slot is claimed first; the screen arms (its own first key's code, on its own view) and may refuse; only
    // then the service's question is raised, so a refusal leaves nothing open and nothing queued.
    void ask_on_screen(const HAction &a, int act, int row, UicReply *r)
    {
        if (g_pend.used || g_svc.q_open) {
            r->r = UR_BUSY;
            snprintf(r->why, sizeof(r->why), "a confirmation is open already");
            return;
        }
        const char *own = owner();
        if (strcmp(own, "menu") || g_dlg.kind == DK_PICK) {    // a dialog, a typed entry, a pick list, a job: the user is in the middle of something
            r->r = UR_BLOCKED;
            snprintf(r->why, sizeof(r->why), "the keys belong to %s now", strcmp(own, "menu") ? own : "a choice list");
            return;
        }
        if (g_qn > 0) {
            r->r = UR_BUSY;
            snprintf(r->why, sizeof(r->why), "keys are still waiting: try again");
            return;
        }
        if (a.flags & HAF_ROW) {
            if (row < 0) {
                r->r = UR_BAD;
                snprintf(r->why, sizeof(r->why), "%s needs a row (target)", a.name);
                return;
            }
        }
        HRow rr;
        memset(&rr, 0, sizeof(rr));
        if (a.src && row >= 0 && !g_m->row(a.src, row, &rr)) {
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no row to ask about");
            return;
        }
        g_pend.used = true;                                   // claimed
        g_pend.token = 0;
        r->why[0] = 0;
        uint32_t tok = 0;
        int ar = g_m->arm(act, row, rr.ident, &tok, r->why, sizeof(r->why));
        if (ar != HARM_ARMED) {
            g_pend.used = false;                              // released: the screen's words are the reply
            r->r = ar == HARM_BUSY ? UR_BUSY : UR_REFUSED;
            return;
        }
        g_pend.token = tok;
        snprintf(g_pend.ask, sizeof(g_pend.ask), "%s%s%s", a.ask ? a.ask : "Run it?", rr.title[0] ? " " : "", rr.title);
        UicQuestion q;
        memset(&q, 0, sizeof(q));
        snprintf(q.text, sizeof(q.text), "%s", g_pend.ask);
        q.destructive = true;
        snprintf(q.def, sizeof(q.def), "no");
        q.timeout_ms = 60000;
        if (!uic_ask(&g_svc, q, r->q)) {                      // cannot be: the slot was free
            g_pend.used = false;
            g_m->disarm(tok);
            r->r = UR_BUSY;
            snprintf(r->why, sizeof(r->why), "a question is open already");
            return;
        }
        r->why[0] = 0;
        r->r = UR_ASKED;
    }

    void do_op(const UicIntent &in, UicReply *r)
    {
        const HAction *a = hd_action(D(), in.item);
        if (!a) {
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no such action: %s", in.item);
            return;
        }
        const char *own = owner();
        bool free_action = (a->flags & HAF_GLOBAL) || !strcmp(a->name, "back") || !strncmp(a->name, "cursor.", 7);
        if (strcmp(own, "menu") && !free_action) {
            r->r = UR_BLOCKED;
            snprintf(r->why, sizeof(r->why), "the keys belong to %s now", own);
            return;
        }
        int row = -1;
        if (in.target[0] == '#') {                            // a row by its index (the T106 page knows no ids)
            row = atoi(in.target + 1);
            const HostDecl &d = D();
            (void)d;
            if (!a->src || row < 0 || row >= g_m->rows(a->src)) {
                r->r = UR_GONE;
                snprintf(r->why, sizeof(r->why), "no row with that index");
                return;
            }
        } else if (in.target[0]) {
            const HostDecl &d = D();
            const HPage *pg = 0;
            // the row belongs to the list of the action's own view
            for (int i = 0; i < d.npages && !pg; i++) {
                if (d.pages[i].list && a->src && !strcmp(d.pages[i].list, a->src) && (a->view < 0 || d.pages[i].view == a->view)) {
                    pg = &d.pages[i];
                }
            }
            if (!pg) {
                r->r = UR_BAD;
                snprintf(r->why, sizeof(r->why), "%s takes no row", a->name);
                return;
            }
            row = find_row(*pg, in.target);
            if (row < 0) {
                r->r = UR_GONE;
                snprintf(r->why, sizeof(r->why), "no row with that id");
                return;
            }
        } else if ((a->flags & HAF_ROW) && a->src) {
            row = g_m->cursor(a->src);                        // the row the cursor is on
            if (row < 0) {
                r->r = UR_GONE;
                snprintf(r->why, sizeof(r->why), "no row to act on");
                return;
            }
        }
        run_action(*a, row, in.value, r);
    }

    void do_key(const UicIntent &in, UicReply *r)
    {
        if (!strcmp(in.key, "text") || (!in.key[0] && in.value[0])) {
            if (g_dlg.kind != DK_ENTRY && !(g_dlg.kind == DK_PAGE && g_dlg.page.input)) {
                r->r = UR_BLOCKED;
                snprintf(r->why, sizeof(r->why), "no text field is open");
                return;
            }
            int n = (int)strlen(in.value);
            if (n + g_qn > QMAX) {
                r->r = UR_BUSY;
                snprintf(r->why, sizeof(r->why), "too much text at once (%d at most)", QMAX - g_qn);
                return;
            }
            for (int i = 0; i < n; i++) {
                q_push((unsigned char)in.value[i]);
            }
            r->r = UR_TAKEN;
            return;
        }
        if (g_pend.used && !in.local) {
            r->r = UR_BLOCKED;
            snprintf(r->why, sizeof(r->why), "a question is open: answer it first");
            return;
        }
        int k = in.key[0] ? key_code(in.key) : -1;
        if (k < 0) {
            r->r = UR_BAD;
            snprintf(r->why, sizeof(r->why), "unknown key name");
            return;
        }
        if (!q_push(k, in.local)) {
            r->r = UR_BUSY;
            snprintf(r->why, sizeof(r->why), "too many keys waiting");
            if (in.local) {
                hostapi_note("Key dropped: the key queue is full", true);
            }
            return;
        }
        r->r = UR_TAKEN;
    }

    void do_open(const UicIntent &in, UicReply *r)
    {
        const char *t = in.target;
        if (!t[0]) {
            r->r = UR_TAKEN;                                  // the host's menu is always open
            return;
        }
        if (strcmp(owner(), "menu")) {
            r->r = UR_BLOCKED;
            snprintf(r->why, sizeof(r->why), "the keys belong to %s now", owner());
            return;
        }
        const HPage *p = 0;
        if (!strncmp(t, "browse:", 7)) {
            q_act(HCA_NAV_BROWSE);
            q_cmd(CMD_GOTO, t + 7, 0, 0);
            r->r = UR_TAKEN;
            return;
        }
        p = hd_page(D(), t);
        if (!p) {
            r->r = UR_GONE;
            snprintf(r->why, sizeof(r->why), "no such screen");
            return;
        }
        if (p->nav) {
            q_act(nav_act(p->view));
        } else if (!strcmp(p->id, "settings/wifi")) {
            q_act(HCA_NAV_SETTINGS);
            q_cmd(CMD_STEP, "settings.wifi", 0, 1);
        } else if (!strcmp(p->id, "settings/cores")) {
            q_act(HCA_NAV_SETTINGS);
            q_cmd(CMD_STEP, "settings.cores", 0, 1);
        } else {
            q_act(HCA_NAV_NET);
            q_act(HCA_NET_COLORS);
        }
        r->r = UR_TAKEN;
    }

    // ---- questions ------------------------------------------------------------------------------------------------------------------
    bool question(UicQuestion *q) override
    {
        if (g_pend.used) {
            snprintf(q->text, sizeof(q->text), "%s", g_pend.ask);
            q->destructive = true;
            snprintf(q->def, sizeof(q->def), "no");
            q->timeout_ms = 60000;
            return true;
        }
        if (g_waiting && g_dlg.kind == DK_PAGE && g_dlg.page.bar < 0 && g_dlg.yes_key) {
            int o = snprintf(q->text, sizeof(q->text), "%s", g_dlg.page.title);
            for (int i = 0; i < g_dlg.page.nlines && o < (int)sizeof(q->text) - 2; i++) {
                if (g_dlg.page.line[i][0]) {
                    o += snprintf(q->text + o, sizeof(q->text) - o, " %s", g_dlg.page.line[i]);
                }
            }
            if (g_dlg.page.hint[0] && o < (int)sizeof(q->text) - 2) {
                snprintf(q->text + o, sizeof(q->text) - o, " [%s]", g_dlg.page.hint);
            }
            q->destructive = g_dlg.destructive;
            key_name(g_dlg.yes_key, q->yes_key);
            key_name(g_dlg.no_key, q->no_key);
            return true;
        }
        if (g_ask_text[0]) {
            snprintf(q->text, sizeof(q->text), "%s", g_ask_text);
            q->destructive = true;
            if (g_ask_remote) {                                 // D19a: a question a remote client raised answers no after 60 s; the board's own has no time-out
                snprintf(q->def, sizeof(q->def), "no");
                q->timeout_ms = 60000;
            }
            key_name(g_ask_yes, q->yes_key);
            key_name(g_ask_no, q->no_key);
            return true;
        }
        return false;
    }

    void answer(const char *choice) override
    {
        bool yes = !strcmp(choice, "yes");
        if (g_pend.used) {
            Pend p = g_pend;
            g_pend.used = false;
            if (yes) {                                        // the screen runs it only while it is armed with this token
                char why[120];
                why[0] = 0;
                if (!g_m->confirm(p.token, why, sizeof(why))) {
                    hostapi_note(why[0] ? why : "Not done: the screen is not armed for it any more", true);
                }
            } else {
                g_m->disarm(p.token);
                hostapi_note("Cancelled. Nothing was changed.", false);
            }
            return;
        }
        if (g_waiting && g_dlg.kind == DK_PAGE) {
            q_push(yes ? g_dlg.yes_key : g_dlg.no_key);
            return;
        }
        if (g_ask_text[0]) {
            q_push(yes ? g_ask_yes : g_ask_no);
        }
    }

    bool cancel_job(const char *) override { return false; }
    void set_text(const char *text) override
    {
        if (g_dlg.kind != DK_ENTRY) {
            return;
        }
        q_push(KEY_CLEAR);                                    // the field is cleared, then typed
        for (const char *c = text; *c && g_qn < QMAX - 1; c++) {
            q_push((unsigned char)*c);
        }
    }

    void tick(uint32_t) override
    {
        if (g_pend.used && g_pend.token && !g_m->armed(g_pend.token)) {      // the screen lost the arm without a word: the question closes (gone)
            hostapi_arm_end(g_pend.token, HARM_END_GONE);
        }
        if (g_flow_on && !g_flow_cancel_sent && uic_job_cancel_requested(&g_svc, g_flow_id)) {
            g_flow_cancel_sent = true;
            q_push(KEY_ESCAPE);                              // the flows poll the key path for the abort key
        }
    }
};

HostDomain g_dom;

// ---- the platform in the service's terms ---------------------------------------------------------------------------------------------------
uint32_t p_now(void *) { return g_plat.now_ms(); }
void p_lock(void *) { g_plat.lock(); }
void p_unlock(void *) { g_plat.unlock(); }
void p_sleep(void *, int ms) { g_plat.sleep_ms(ms); }
bool p_ui(void *) { return g_plat.on_ui(); }
int p_entropy(void *, uint8_t *out, int n) { return g_plat.entropy ? g_plat.entropy(out, n) : 0; }
int p_load(void *, char *b, int c) { return g_plat.file_load(b, c); }
bool p_save(void *, const char *b, int n) { return g_plat.file_save(b, n); }
bool p_remove(void *) { return g_plat.file_remove(); }

}  // namespace

void hostapi_init(HostModel *model, const HostApiPlatform *plat)
{
    if (g_up) {
        return;
    }
    g_m = model;
    g_plat = *plat;
    UicPort port;
    memset(&port, 0, sizeof(port));
    port.now_ms = p_now;
    port.lock = p_lock;
    port.unlock = p_unlock;
    port.sleep_ms = p_sleep;
    port.on_ui_task = p_ui;
    port.entropy = p_entropy;
    if (plat->file_load && plat->file_save && plat->file_remove) {
        port.file_load = p_load;
        port.file_save = p_save;
        port.file_remove = p_remove;
    }
    uic_init(&g_svc, &port, &g_dom, g_snap, sizeof(g_snap));
    g_up = true;
}

UicService *hostapi_service() { return g_up ? &g_svc : 0; }

void hostapi_apply(const UicIntent &in, UicReply *r)
{
    memset(r, 0, sizeof(*r));
    if (!g_m) {
        r->r = UR_BLOCKED;
        snprintf(r->why, sizeof(r->why), "the service is not up");
        return;
    }
    g_dom.intent(in, r);
}

void hostapi_bind_model(HostModel *model)
{
    if (!g_m) {
        g_m = model;
    }
}

bool hostapi_pass(bool changed)
{
    if (!g_up) {
        return false;
    }
    g_waiting = false;
    if (g_arm_text_set && g_qn == 0 && g_dlg.kind != DK_ENTRY && !g_flow_on) {
        g_arm_text_set = false;                               // the action's keys are spent and no entry opened: the text is not kept for the next one
    }
    if (changed) {
        uic_note(&g_svc);
    }
    return uic_service(&g_svc) > 0;
}

void hostapi_pump()
{
    if (!g_up || g_in_pump) {
        return;
    }
    g_in_pump = true;
    uic_service(&g_svc);
    g_in_pump = false;
}

void hostapi_waiting()
{
    if (!g_up) {
        return;
    }
    g_waiting = true;
    hostapi_pump();
}

void hostapi_local_key(int k)
{
    if (!g_up) {
        q_push(k);
        return;
    }
    if (k == KEY_ESCAPE && g_flow_on && !g_flow_cancel_sent && uic_job_cancel_local(&g_svc, g_flow_id)) {
        g_flow_cancel_sent = true;                            // the abort key of a cancelable flow, pressed at the board: the flow ends as cancelled, like after the cancel intent
    }
    UicIntent in;
    memset(&in, 0, sizeof(in));
    in.kind = UD_KEY;
    in.local = true;
    snprintf(in.key, sizeof(in.key), "#%d", k);
    UicReply r;
    uic_local(&g_svc, in, &r);
}

int hostapi_pop_key()
{
    while (g_qn > 0) {
        int v = g_q[g_qh];
        g_qh = (g_qh + 1) % QMAX;
        g_qn--;
        const bool local = (v & QLOCAL) != 0;
        v &= ~QLOCAL;
        if (v >= CMD_BASE && v < CMD_BASE + CMD_N) {
            int i = v - CMD_BASE;
            Cmd c = g_cmd[i];
            g_cmd_busy[i] = false;
            run_cmd(c);
            continue;
        }
        g_key_local = local || !g_up;
        return v;
    }
    g_key_local = true;
    return -1;
}

bool hostapi_key_local() { return g_key_local; }

void hostapi_arm_end(uint32_t token, int how)
{
    if (!g_up || !g_pend.used || !token || g_pend.token != token) {
        return;
    }
    g_pend.used = false;
    uic_close(&g_svc, "screen", how == HARM_END_YES ? "yes" : how == HARM_END_NO ? "no" : "");
    if (how == HARM_END_GONE) {
        hostapi_note("Cancelled. Nothing was changed.", false);
    }
}

bool hostapi_push_key(int k) { return q_push(k); }

void hostapi_rest_text(bool net, char *out, int n)
{
    if (!g_up) {
        snprintf(out, n, "off");
    } else if (!net) {
        snprintf(out, n, "waiting for the network");
    } else if (!g_svc.port.file_save) {
        snprintf(out, n, "on, port %d, no token", WEB_PORT);
    } else {
        uic_hold(&g_svc);
        bool have = g_svc.tok_have;
        uic_release(&g_svc);
        snprintf(out, n, "on, port %d, %s", WEB_PORT, have ? "token required" : "token required, none granted yet");
    }
}

int hostapi_render(char *out, int cap) { return g_up ? uic_render(&g_svc, out, cap) : -1; }

void hostapi_page(const HostPageInfo &p, int yes_key, int no_key, bool destructive)
{
    bool same = g_dlg.kind == DK_PAGE && g_dlg.yes_key == yes_key && g_dlg.no_key == no_key && !memcmp(&g_dlg.page, &p, sizeof(p));
    g_dlg.kind = DK_PAGE;
    g_dlg.page = p;
    g_dlg.yes_key = yes_key;
    g_dlg.no_key = no_key;
    g_dlg.destructive = destructive;
    if (g_flow_on && p.bar >= 0) {
        uic_job_step(&g_svc, g_flow_id, p.bar, 100, p.line[0]);
    }
    if (g_up && !same) {
        uic_note(&g_svc);
    }
}

void hostapi_page_end()
{
    if (g_dlg.kind == DK_PAGE) {
        g_dlg.kind = DK_NONE;
        if (g_up) uic_note(&g_svc);
    }
}

void hostapi_entry(const char *title, const char *sub, const char *typed, const char *hint)
{
    bool fresh = g_dlg.kind != DK_ENTRY;
    g_dlg.kind = DK_ENTRY;
    snprintf(g_dlg.entry_title, sizeof(g_dlg.entry_title), "%s", title ? title : "");
    snprintf(g_dlg.entry_sub, sizeof(g_dlg.entry_sub), "%s", sub ? sub : "");
    snprintf(g_dlg.entry_typed, sizeof(g_dlg.entry_typed), "%s", typed ? typed : "");
    snprintf(g_dlg.entry_hint, sizeof(g_dlg.entry_hint), "%s", hint ? hint : "");
    if (fresh && g_arm_text_set) {                           // an action brought its text along: it is typed and sent
        g_arm_text_set = false;
        q_push(KEY_CLEAR);
        for (const char *c = g_arm_text; *c && g_qn < QMAX - 2; c++) {
            q_push((unsigned char)*c);
        }
        q_push(KEY_RETURN);
    }
    if (g_up) uic_note(&g_svc);
}

void hostapi_entry_end()
{
    if (g_dlg.kind == DK_ENTRY) {
        g_dlg.kind = DK_NONE;
        if (g_up) uic_note(&g_svc);
    }
}

void hostapi_pick(const char *title, const char *const *labels, int n, int sel, const char *hint)
{
    g_dlg.kind = DK_PICK;
    snprintf(g_dlg.pick_title, sizeof(g_dlg.pick_title), "%s", title ? title : "");
    snprintf(g_dlg.pick_hint, sizeof(g_dlg.pick_hint), "%s", hint ? hint : "");
    g_dlg.pick_n = n < PICK_MAX ? n : PICK_MAX;
    for (int i = 0; i < g_dlg.pick_n; i++) {
        snprintf(g_dlg.pick_label[i], sizeof(g_dlg.pick_label[i]), "%s", labels[i]);
    }
    g_dlg.pick_sel = sel;
    if (g_up) uic_note(&g_svc);
}

void hostapi_pick_end()
{
    if (g_dlg.kind == DK_PICK) {
        g_dlg.kind = DK_NONE;
        if (g_up) uic_note(&g_svc);
    }
}

void hostapi_ask(const char *text, int yes_key, int no_key, bool remote)
{
    snprintf(g_ask_text, sizeof(g_ask_text), "%s", text ? text : "");
    g_ask_yes = yes_key;
    g_ask_no = no_key;
    g_ask_remote = remote && text && text[0];
    if (g_up) uic_note(&g_svc);
}

bool hostapi_flow_begin(const char *kind, const char *title, bool cancelable)
{
    if (!g_up || g_flow_on) {
        return false;
    }
    if (!uic_job_begin(&g_svc, kind, title, cancelable, g_flow_id)) {
        return false;
    }
    g_flow_on = true;
    uic_quiet(&g_svc, true);                                  // no token file read in the middle of a flash write
    g_flow_cancel_sent = false;
    g_flow_bad = false;
    g_flow_last_note[0] = 0;
    return true;
}

void hostapi_flow_progress(int done, int total, const char *step)
{
    if (!g_flow_on) {
        return;
    }
    uic_job_step(&g_svc, g_flow_id, done, total, step);
    hostapi_pump();
}

void hostapi_flow_end(bool ok, const char *text)
{
    if (!g_flow_on) {
        return;
    }
    g_flow_on = false;
    uic_quiet(&g_svc, false);
    bool good = ok && !g_flow_bad && !g_flow_cancel_sent;     // a flow that was asked to cancel ends as cancelled (the service sets that phase for a not-ok end after a cancel)
    uic_job_end(&g_svc, g_flow_id, good, text && text[0] ? text : g_flow_last_note);
    g_dlg.kind = g_dlg.kind == DK_PAGE ? DK_NONE : g_dlg.kind;
    uic_note(&g_svc);
}

bool hostapi_flow_running() { return g_flow_on; }

void hostapi_note(const char *text, bool bad)
{
    if (!g_up || !text || !text[0]) {
        return;
    }
    snprintf(g_flow_last_note, sizeof(g_flow_last_note), "%s", text);
    if (g_flow_on && bad) {
        g_flow_bad = true;
    }
    uic_toast(&g_svc, text);
}

void hostapi_history(const char *text)
{
    if (g_up) {
        uic_history_add(&g_svc, text);
    }
}
