#include "contrib.h"
#include "baseset.h"
#include "bindcheck.h"
#include "ops.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool text(const char *s) { return s && s[0]; }

// ---- the default route (T104 5.8) --------------------------------------------------------------------------------------------
static const SkelTab t_home[] = { { "home", "Home", 0, TW_INFO, 0 } };
static const SkelTab t_lib[] = { { "browse", "Browse", 0, TW_BROWSER, 0 } };
// T205 D1, T212 D1: F5 is the core's machine settings: its own settings first (that tab is named after the core), then Video, Audio, Input (the hotkeys moved to F7); there is no F6. The tab ids
// are the ones the base UI TOML's routes name (coreapp/base_ui.toml). What the old Summary tab showed (the media slots, the configs) is on Home.
static const SkelTab t_machine[] = { { "settings", 0, 0, TW_SETTINGS, 0 }, { "video", "Video", 0, TW_SETTINGS, "video", 0 },
                                     { "audio", "Audio", 0, TW_SETTINGS, "audio", 0 }, { "input", "Input", 0, TW_SETTINGS, "input", 0 } };
// T212 D1/D6: F7 (APP) is the generic app's settings; the groups "app" claim no descriptor setting, the routed rows (the base UI TOML, menutree.cc) and what a library registers
// land here. The drive and reset tasks that were its Quick list are on Home.
static const SkelTab t_app[] = { { "settings", "Settings", 0, TW_SETTINGS, "app" } };
static const SkelTab t_system[] = { { "expert", "System", 0, TW_SYSTEM, 0 } };       // T187 D8: named System; the id stays, anchors name it
static const SkelArea default_areas[] = {
    { "F1", "Home", false, t_home, 1 },
    { "F3", "Library", false, t_lib, 1 },
    { "F5", 0, false, t_machine, 4 },                        // the core's short title
    { "F7", "APP", false, t_app, 1 },
    { "F8", "System", true, t_system, 1 },
};
static const Skeleton default_skel = { default_areas, 5 };

const Skeleton *contrib_default_skeleton() { return &default_skel; }

// ---- small helpers ------------------------------------------------------------------------------------------------------------
bool groups_have(const char *groups, const char *group)
{
    if (!text(groups) || !text(group)) {
        return false;
    }
    size_t gl = strlen(group);
    for (const char *p = groups; *p;) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == gl && !strncmp(p, group, n)) {
            return true;
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return false;
}

// "name;key;key=value": the words are separated by ';', a value starts after the first '=' of its word.
bool ui_attr(const char *attr, const char *key, char *out, int n)
{
    if (out && n > 0) {
        out[0] = 0;
    }
    if (!text(attr) || !text(key)) {
        return false;
    }
    size_t kl = strlen(key);
    for (const char *p = attr; *p;) {
        const char *e = strchr(p, ';');
        size_t wl = e ? (size_t)(e - p) : strlen(p);
        if (wl >= kl && !strncmp(p, key, kl) && (wl == kl || p[kl] == '=')) {
            if (out && n > 0 && wl > kl) {
                size_t vl = wl - kl - 1;
                if (vl >= (size_t)n) {
                    vl = (size_t)n - 1;
                }
                memcpy(out, p + kl + 1, vl);
                out[vl] = 0;
            }
            return true;
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return false;
}

void ui_name(const char *attr, char *out, int n)
{
    out[0] = 0;
    if (!text(attr)) {
        return;
    }
    const char *e = strchr(attr, ';');
    size_t l = e ? (size_t)(e - attr) : strlen(attr);
    if (l >= (size_t)n) {
        l = (size_t)n - 1;
    }
    memcpy(out, attr, l);
    out[l] = 0;
}

const SkelTab *skel_find_tab(const Skeleton *skel, const char *area_key, const char *tab_id, const SkelArea **area_out)
{
    for (int a = 0; skel && a < skel->nareas; a++) {
        if (strcmp(skel->areas[a].key, area_key)) {
            continue;
        }
        for (int t = 0; t < skel->areas[a].ntabs; t++) {
            if (!strcmp(skel->areas[a].tabs[t].id, tab_id)) {
                if (area_out) {
                    *area_out = &skel->areas[a];
                }
                return &skel->areas[a].tabs[t];
            }
        }
    }
    return 0;
}

bool tab_takes_nodes(const SkelTab &t)
{
    return t.widget != TW_INFO && t.widget != TW_BROWSER;
}

bool cond_holds(const char *expr, cond_state_fn get, void *user)
{
    if (!text(expr)) {
        return true;
    }
    char name[96], val[96];
    val[0] = 0;
    int op = 0;                                                    // 0: has a value, 1: ==, 2: !=
    const char *p = expr;
    while (*p == ' ') {
        p++;
    }
    int n = 0;
    while (*p && *p != ' ' && *p != '=' && *p != '!' && n < (int)sizeof(name) - 1) {
        name[n++] = *p++;
    }
    name[n] = 0;
    while (*p == ' ') {
        p++;
    }
    if (p[0] == '=' && p[1] == '=') {
        op = 1;
        p += 2;
    } else if (p[0] == '!' && p[1] == '=') {
        op = 2;
        p += 2;
    }
    while (*p == ' ') {
        p++;
    }
    snprintf(val, sizeof(val), "%s", p);
    for (int i = (int)strlen(val) - 1; i >= 0 && val[i] == ' '; i--) {
        val[i] = 0;
    }
    const char *have = get ? get(user, name) : 0;
    if (!op) {
        return text(have);
    }
    bool same;
    if (!strcmp(val, "empty")) {
        same = !text(have) || !strcmp(have, "empty");
    } else {
        same = have && !strcmp(have, val);
    }
    return op == 1 ? same : !same;
}

static void refuse(MenuMerge *mm, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void refuse(MenuMerge *mm, const char *fmt, ...)
{
    mm->nrefused++;
    if (mm->nerr >= CONTRIB_ERR_MAX) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(mm->err[mm->nerr++], CONTRIB_ERR_LEN, fmt, ap);
    va_end(ap);
}

static bool in_list(const char *const *l, int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        if (l[i] && !strcmp(l[i], id)) {
            return true;
        }
    }
    return false;
}

// The label the description gives the thing with this id, or null.
static const char *renamed(const Overrides *ov, const char *id)
{
    for (int k = 0; ov && ov->rename && k < ov->nrename; k++) {
        if (ov->rename[k].id && !strcmp(ov->rename[k].id, id) && text(ov->rename[k].label)) {
            return ov->rename[k].label;
        }
    }
    return 0;
}

static bool key_taken(const MenuMerge *mm, int n, const char *key)
{
    if (!strcasecmp(key, PICTURE_KEY) || !strcasecmp(key, SCALE_KEY)) {
        return true;
    }
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(mm->settings[i].key, key)) {
            return true;
        }
    }
    return false;
}

// ---- $slot ------------------------------------------------------------------------------------------------------------------
int contrib_slots(const CoreDesc *d, const char **out, int max)
{
    int n = 0;
    for (int i = 0; d->caps && i < d->ncaps && n < max; i++) {
        if (!strncmp(d->caps[i], "drive_", 6) && d->caps[i][6]) {
            out[n++] = d->caps[i];
        }
    }
    return n;
}

static bool has_slot(const char *s) { return s && strstr(s, "$slot") != 0; }

// `s` with every $slot replaced by `slot`, in the merge's pool; null when the pool is full.
static const char *expand(MenuMerge *mm, const char *s, const char *slot)
{
    if (!has_slot(s)) {
        return s;
    }
    int start = mm->slot_used;
    for (const char *p = s; *p;) {
        const char *hit = strstr(p, "$slot");
        size_t n = hit ? (size_t)(hit - p) : strlen(p);
        size_t sl = hit ? strlen(slot) : 0;
        if (mm->slot_used + (int)(n + sl) + 1 > CONTRIB_SLOTPOOL) {
            mm->slot_used = start;
            return 0;
        }
        memcpy(mm->slotpool + mm->slot_used, p, n);
        memcpy(mm->slotpool + mm->slot_used + n, slot, sl);
        mm->slot_used += (int)(n + sl);
        p = hit ? hit + 5 : p + n;
    }
    mm->slotpool[mm->slot_used++] = 0;
    return mm->slotpool + start;
}

static const OpBind *expand_binds(MenuMerge *mm, const OpBind *b, int nb, const char *slot, bool *ok)
{
    bool any = false;
    for (int i = 0; i < nb; i++) {
        any |= b[i].kind == BIND_STR && has_slot(b[i].s);
    }
    if (!any) {
        return b;
    }
    if (mm->bind_used + nb > CONTRIB_BINDPOOL) {
        *ok = false;
        return b;
    }
    OpBind *dst = mm->bindpool + mm->bind_used;
    mm->bind_used += nb;
    for (int i = 0; i < nb; i++) {
        dst[i] = b[i];
        if (b[i].kind == BIND_STR && has_slot(b[i].s)) {
            dst[i].s = expand(mm, b[i].s, slot);
            *ok &= dst[i].s != 0;
        }
    }
    return dst;
}

// A record with the declarations that use $slot made once per slot. The declarations themselves live in the caller's scratch arrays (the merge copies
// what it keeps); the strings and bindings live in the pools of `mm`. The same record is returned when nothing uses $slot.
struct Expanded {
    LibRecord rec;
    LibNodeDecl nodes[MENU_NODES_MAX];
    LibFileActionDecl fas[CONTRIB_ACTIONS_MAX];
    LibSettingDecl sets[CONTRIB_SETTINGS_MAX];
};

static const LibRecord *expand_record(const Contributor &c, const char *const *slots, int nslots, MenuMerge *mm, Expanded *x)
{
    const LibRecord *r = c.rec;
    bool uses = false;
    for (int i = 0; r && i < r->nnodes; i++) {
        const LibNodeDecl &n = r->nodes[i];
        uses |= has_slot(n.anchor) || has_slot(n.label) || has_slot(n.op) || has_slot(n.state) || has_slot(n.when_state) || has_slot(n.id);
        for (int k = 0; k < n.nbind; k++) uses |= n.bind[k].kind == BIND_STR && has_slot(n.bind[k].s);
    }
    for (int i = 0; r && i < r->nfile_actions; i++) {
        const LibFileActionDecl &f = r->file_actions[i];
        uses |= has_slot(f.label) || has_slot(f.op);
        for (int k = 0; k < f.nbind; k++) uses |= f.bind[k].kind == BIND_STR && has_slot(f.bind[k].s);
    }
    for (int i = 0; r && i < r->nsettings; i++) {
        uses |= has_slot(r->settings[i].group) || has_slot(r->settings[i].key) || has_slot(r->settings[i].apply);
    }
    if (!uses) {
        return r;
    }
    x->rec = *r;
    x->rec.nodes = x->nodes; x->rec.nnodes = 0;
    x->rec.file_actions = x->fas; x->rec.nfile_actions = 0;
    x->rec.settings = x->sets; x->rec.nsettings = 0;
    bool ok = true;
    for (int i = 0; i < r->nnodes; i++) {
        const LibNodeDecl &n = r->nodes[i];
        bool u = has_slot(n.anchor) || has_slot(n.label) || has_slot(n.op) || has_slot(n.state) || has_slot(n.when_state) || has_slot(n.id);
        for (int k = 0; k < n.nbind; k++) u |= n.bind[k].kind == BIND_STR && has_slot(n.bind[k].s);
        for (int sl = 0; sl < (u ? nslots : 1); sl++) {
            if (x->rec.nnodes >= MENU_NODES_MAX) { ok = false; break; }
            LibNodeDecl &o = x->nodes[x->rec.nnodes++];
            o = n;
            if (u) {
                o.anchor = expand(mm, n.anchor, slots[sl]); o.label = expand(mm, n.label, slots[sl]); o.op = expand(mm, n.op, slots[sl]);
                o.state = expand(mm, n.state, slots[sl]); o.when_state = expand(mm, n.when_state, slots[sl]); o.id = expand(mm, n.id, slots[sl]);
                o.bind = expand_binds(mm, n.bind, n.nbind, slots[sl], &ok);
                ok &= (!n.anchor || o.anchor) && (!n.label || o.label) && (!n.op || o.op) && (!n.state || o.state) && (!n.when_state || o.when_state) && (!n.id || o.id);
            }
        }
    }
    for (int i = 0; i < r->nfile_actions; i++) {
        const LibFileActionDecl &f = r->file_actions[i];
        bool u = has_slot(f.label) || has_slot(f.op);
        for (int k = 0; k < f.nbind; k++) u |= f.bind[k].kind == BIND_STR && has_slot(f.bind[k].s);
        for (int sl = 0; sl < (u ? nslots : 1); sl++) {
            if (x->rec.nfile_actions >= CONTRIB_ACTIONS_MAX) { ok = false; break; }
            LibFileActionDecl &o = x->fas[x->rec.nfile_actions++];
            o = f;
            if (u) {
                o.label = expand(mm, f.label, slots[sl]); o.op = expand(mm, f.op, slots[sl]);
                o.bind = expand_binds(mm, f.bind, f.nbind, slots[sl], &ok);
                ok &= o.label && o.op;
            }
        }
    }
    for (int i = 0; i < r->nsettings; i++) {
        const LibSettingDecl &st = r->settings[i];
        bool u = has_slot(st.group) || has_slot(st.key) || has_slot(st.apply);
        for (int sl = 0; sl < (u ? nslots : 1); sl++) {
            if (x->rec.nsettings >= CONTRIB_SETTINGS_MAX) { ok = false; break; }
            LibSettingDecl &o = x->sets[x->rec.nsettings++];
            o = st;
            if (u) {
                o.group = expand(mm, st.group, slots[sl]); o.key = expand(mm, st.key, slots[sl]); o.apply = expand(mm, st.apply, slots[sl]);
                ok &= (!st.group || o.group) && (!st.key || o.key) && (!st.apply || o.apply);
            }
        }
    }
    if (!ok) {
        refuse(mm, "%s: the $slot expansion of its declarations has no room", c.name);
    }
    return &x->rec;
}

// ---- settings -----------------------------------------------------------------------------------------------------------------
static void merge_settings(const Contributor &c, const Overrides *ov, const OpRegistry *reg, MenuMerge *mm, CoreDesc *out)
{
    const LibRecord *r = c.rec;
    for (int i = 0; r && i < r->nsettings; i++) {
        const LibSettingDecl &s0 = r->settings[i];
        if (!text(s0.key) || !text(s0.apply)) {
            refuse(mm, "%s: a setting needs a key and an operation that applies it", c.name);
            continue;
        }
        // the id of a setting is its key; the description may hide it, or give it another key (which is also its key in the .cfg)
        if (ov && ov->hide && in_list(ov->hide, ov->nhide, s0.key)) {
            continue;
        }
        LibSettingDecl s = s0;
        if (const char *nk = renamed(ov, s0.key)) {
            s.key = nk;
        }
        if (mm->ncontributed_settings >= CONTRIB_SETTINGS_MAX || out->nsettings >= SETTINGS_MAX) {
            refuse(mm, "%s: setting \"%s\" has no room (the menu holds %d settings)", c.name, s.key, SETTINGS_MAX);
            continue;
        }
        if (key_taken(mm, out->nsettings, s.key)) {
            refuse(mm, "%s: setting \"%s\" has a key that is taken already (the .cfg keys settings by name)", c.name, s.key);
            continue;
        }
        bool range = s.values == 0;
        int nv = range ? (int)(s.hi - s.lo + 1) : s.nvalues;
        if (nv < 2 || nv > CONTRIB_VALUES_MAX || (range && s.hi < s.lo)) {
            refuse(mm, "%s: setting \"%s\" has %d values (2 to %d are allowed)", c.name, s.key, nv, CONTRIB_VALUES_MAX);
            continue;
        }
        int def = range ? (int)(s.def - s.lo) : (int)s.def;
        if (def < 0 || def >= nv) {
            refuse(mm, "%s: the default of setting \"%s\" is not one of its values", c.name, s.key);
            continue;
        }
        int base = range ? (int)s.lo : 0;
        if (reg) {
            const OpDef *o = reg->find_op(s.apply);
            if (!o) {
                refuse(mm, "%s: setting \"%s\" is applied by operation \"%s\", which nobody registered", c.name, s.key, s.apply);
                continue;
            }
            if (o->nargs != 1 || strcmp(o->args[0].name, "value") || (o->args[0].type != AT_INT && o->args[0].type != AT_ENUM) ||
                o->args[0].lo > base || o->args[0].hi < base + nv - 1) {
                refuse(mm, "%s: operation \"%s\" does not take the values of setting \"%s\" (one number or enum \"value\", %d .. %d)", c.name,
                       s.apply, s.key, base, base + nv - 1);
                continue;
            }
        }
        int slot = mm->ncontributed_settings;
        bool labels_ok = true;
        for (int v = 0; v < nv && labels_ok; v++) {
            if (range) {
                char lab[24];
                int len = snprintf(lab, sizeof(lab), "%d", (int)(s.lo + v));
                if (mm->pool_used + len + 1 > CONTRIB_POOL_MAX) {
                    labels_ok = false;
                    break;
                }
                memcpy(mm->pool + mm->pool_used, lab, len + 1);
                mm->values[slot][v] = mm->pool + mm->pool_used;
                mm->pool_used += len + 1;
            } else {
                labels_ok = text(s.values[v]);
                mm->values[slot][v] = s.values[v];
            }
        }
        if (!labels_ok) {
            refuse(mm, "%s: setting \"%s\" has an empty label or no room for its labels", c.name, s.key);
            continue;
        }
        Setting &st = mm->settings[out->nsettings];
        st = Setting();
        st.key = s.key;
        st.values = mm->values[slot];
        st.nvalues = nv;
        st.def = def;
        st.when = WHEN_LIVE;
        st.apply = s.apply;
        st.base = base;
        mm->setting_group[out->nsettings] = text(s.group) ? s.group : 0;
        out->nsettings++;
        mm->ncontributed_settings++;
    }
}

// ---- file actions -------------------------------------------------------------------------------------------------------------
struct Pending {
    int type;
    FileAction a;
};

static int find_type(const MenuMerge *mm, int ntypes, const char *ext)
{
    for (int i = 0; i < ntypes; i++) {
        if (!strcasecmp(mm->filetypes[i].ext, ext)) {
            return i;
        }
    }
    return -1;
}

static void gather_actions(const Contributor &c, const Overrides *ov, const OpRegistry *reg, MenuMerge *mm, int *ntypes, Pending *pend, int *npend)
{
    const LibRecord *r = c.rec;
    for (int i = 0; r && i < r->nfile_actions; i++) {
        const LibFileActionDecl &f0 = r->file_actions[i];
        if (!text(f0.label) || !text(f0.op) || f0.next < 1 || !f0.ext) {
            refuse(mm, "%s: a file action needs a label, an operation and an extension", c.name);
            continue;
        }
        // the id of a file action is its label
        if (ov && ov->hide && in_list(ov->hide, ov->nhide, f0.label)) {
            continue;
        }
        LibFileActionDecl f = f0;
        if (const char *nl = renamed(ov, f0.label)) {
            f.label = nl;
        }
        bool bad_ext = false;
        for (int e = 0; e < f.next; e++) {
            bad_ext |= !text(f.ext[e]) || f.ext[e][0] != '.';
        }
        if (bad_ext) {
            refuse(mm, "%s: file action \"%s\" names an extension that does not start with a dot", c.name, f.label);
            continue;
        }
        if (reg) {
            const char *why = bind_check(*reg, "file actions", f.label, f.op, f.bind, f.nbind, true);
            if (why) {
                refuse(mm, "%s: %s", c.name, why);
                continue;
            }
        }
        int new_types = 0;
        for (int e = 0; e < f.next; e++) {
            new_types += find_type(mm, *ntypes, f.ext[e]) < 0;
        }
        if (*npend + f.next > CONTRIB_ACTIONS_MAX || *ntypes + new_types > CONTRIB_FILETYPES_MAX) {
            refuse(mm, "%s: file action \"%s\" has no room (%d extensions and %d actions at most)", c.name, f.label, CONTRIB_FILETYPES_MAX,
                   CONTRIB_ACTIONS_MAX);
            continue;
        }
        for (int e = 0; e < f.next; e++) {
            int t = find_type(mm, *ntypes, f.ext[e]);
            if (t < 0) {
                t = (*ntypes)++;
                mm->filetypes[t] = FileType();
                mm->filetypes[t].ext = f.ext[e];
            }
            Pending &p = pend[(*npend)++];
            p.type = t;
            p.a = FileAction();
            p.a.label = f.label;
            p.a.op = f.op;
            p.a.bind = f.bind;
            p.a.nbind = f.nbind;
            p.a.closes_menu = f.closes_menu;
            p.a.size_in = f.size_in;
            p.a.nsize_in = f.nsize_in;
        }
    }
}

// ---- nodes --------------------------------------------------------------------------------------------------------------------
static void merge_nodes(const CoreDesc *d, const Contributor &c, const Skeleton *skel, const Overrides *ov, const OpRegistry *reg, MenuMerge *mm)
{
    const LibRecord *r = c.rec;
    for (int i = 0; r && i < r->nnodes; i++) {
        const LibNodeDecl &n = r->nodes[i];
        const char *id = text(n.id) ? n.id : text(n.op) ? n.op : text(n.state) ? n.state : n.label;
        if (!text(n.label) || !text(id)) {
            refuse(mm, "%s: a node needs a label", c.name);
            continue;
        }
        if (ov && ov->hide && in_list(ov->hide, ov->nhide, id)) {
            continue;                                              // hidden on purpose by the description: not a refusal
        }
        if (n.kind == LN_SETTING) {
            refuse(mm, "%s: node \"%s\" is a setting; a setting is declared as one and lands in the tab that lists its group (D5), not as a node", c.name, id);
            continue;
        }
        if (n.kind == LN_WIDGET) {
            refuse(mm, "%s: node \"%s\" names a widget; this base serves the browser and the settings list through the route, and no other widget", c.name, id);
            continue;
        }
        if (n.kind != LN_OP && n.kind != LN_STATE && n.kind != LN_GROUP && n.kind != LN_LIST) {
            refuse(mm, "%s: node \"%s\" has a kind this base does not know", c.name, id);
            continue;
        }
        bool shown_only = ui_attr(n.attr, "goto") || ui_attr(n.attr, "off");          // a button that only goes somewhere, or one that is greyed with its reason
        if (n.kind == LN_OP && !text(n.op) && !shown_only) {
            refuse(mm, "%s: node \"%s\" is a button without an operation", c.name, id);
            continue;
        }
        if (n.kind == LN_STATE && !text(n.state) && !ui_attr(n.attr, "sub") && !ui_attr(n.attr, "chip")) {
            refuse(mm, "%s: node \"%s\" is a state line without a state", c.name, id);
            continue;
        }
        if (n.kind == LN_LIST && (!text(n.state) || !text(n.op))) {
            refuse(mm, "%s: node \"%s\" is a list without a state that holds its rows or without an operation for a row", c.name, id);
            continue;
        }
        char area[8], tab[32];
        const char *group = "";
        {
            const char *a = n.anchor ? n.anchor : "";
            const char *s1 = strchr(a, '/');
            const char *s2 = s1 ? strchr(s1 + 1, '/') : 0;
            size_t la = s1 ? (size_t)(s1 - a) : 0, lt = s1 ? (s2 ? (size_t)(s2 - s1 - 1) : strlen(s1 + 1)) : 0;
            if (!s1 || la == 0 || la >= sizeof(area) || lt == 0 || lt >= sizeof(tab)) {
                refuse(mm, "%s: node \"%s\" has the anchor \"%s\"; an anchor is area/tab or area/tab/group", c.name, id, n.anchor ? n.anchor : "");
                continue;
            }
            memcpy(area, a, la);
            area[la] = 0;
            memcpy(tab, s1 + 1, lt);
            tab[lt] = 0;
            if (s2) {
                group = s2 + 1;                                    // points into the record's anchor text, which stays loaded
            }
        }
        const SkelArea *sa = 0;
        const SkelTab *st = skel_find_tab(skel, area, tab, &sa);
        if (!st) {
            refuse(mm, "%s: node \"%s\" anchors %s/%s, which the route does not have", c.name, id, area, tab);
            continue;
        }
        const bool source = n.kind == LN_LIST && ui_attr(n.attr, "search") && st->widget == TW_BROWSER && !strcmp(tab, "search");     // T262 P5: a list that stands for a source of the F3 search
        if (source) {
            char nm[40], fo[40], fl[96];
            if (!ui_attr(n.attr, "search", nm, sizeof(nm)) || !nm[0] || !ui_attr(n.attr, "find", fo, sizeof(fo)) || !fo[0]) {
                refuse(mm, "%s: node \"%s\" is a search source without a name (search=) or without the operation that searches (find=)", c.name, id);
                continue;
            }
            int nf = 0;
            if (ui_attr(n.attr, "fields", fl, sizeof(fl)) && fl[0]) {
                nf = 1;
                for (const char *q = fl; *q; q++) {
                    nf += *q == ',';
                }
            }
            if (nf > CONTRIB_SEARCH_FIELDS) {
                refuse(mm, "%s: node \"%s\" names %d search fields; %d at most", c.name, id, nf, CONTRIB_SEARCH_FIELDS);
                continue;
            }
            if (reg && (!reg->find_op(fo) || !reg->find_op(n.op) || !reg->find_state(n.state))) {
                refuse(mm, "%s: node \"%s\" names an operation or a state of the search that nobody registered", c.name, id);
                continue;
            }
            if (mm->nsrcs >= CONTRIB_SEARCH_MAX) {
                refuse(mm, "%s: node \"%s\" has no room (%d search sources at most)", c.name, id, CONTRIB_SEARCH_MAX);
                continue;
            }
            MergedNode &sn = mm->srcs[mm->nsrcs++];
            memset(&sn, 0, sizeof(sn));
            sn.n.kind = n.kind;
            sn.n.op = n.op;
            sn.n.state = n.state;
            sn.n.when_state = n.when_state;
            sn.n.attr = n.attr;
            sn.n.owner = c.name;
            sn.label = n.label;
            sn.id = id;
            continue;
        }
        if (!tab_takes_nodes(*st)) {
            refuse(mm, "%s: node \"%s\" anchors %s/%s, a tab that shows the %s and takes no nodes", c.name, id, area, tab,
                   st->widget == TW_BROWSER ? "file browser" : "information lines");
            continue;
        }
        if (text(n.capability) && !desc_has_capability(d, n.capability)) {
            refuse(mm, "%s: node \"%s\" needs the capability \"%s\", which the core \"%s\" does not have", c.name, id, n.capability, d->cor_name);
            continue;
        }
        if (reg) {
            if (n.kind == LN_OP && text(n.op)) {
                char pk[8];
                const bool text_ok = ui_attr(n.attr, "ask") || (ui_attr(n.attr, "pick", pk, sizeof(pk)) && pk[0] == '@');     // the typed text, or the chosen value of a pick (T262 P2)
                const char *why = bind_check(*reg, "nodes", n.label, n.op, n.bind, n.nbind, false, false, text_ok);
                if (why) {
                    refuse(mm, "%s: %s", c.name, why);
                    continue;
                }
            }
            if (n.kind == LN_STATE && text(n.state) && !reg->find_state(n.state)) {
                refuse(mm, "%s: node \"%s\" shows state \"%s\", which nobody registered", c.name, id, n.state);
                continue;
            }
            if (n.kind == LN_LIST) {                                       // T262 P1: the state holds the rows, the operation takes a row, `more` (if any) asks for more
                char mo[40];
                if (!reg->find_state(n.state) || !reg->find_op(n.op) || (ui_attr(n.attr, "more", mo, sizeof(mo)) && !reg->find_op(mo))) {
                    refuse(mm, "%s: node \"%s\" names a state or an operation of its list that nobody registered", c.name, id);
                    continue;
                }
            }
        }
        if (mm->nnodes >= MENU_NODES_MAX) {
            refuse(mm, "%s: node \"%s\" has no room (%d nodes at most)", c.name, id, MENU_NODES_MAX);
            continue;
        }
        MergedNode &m = mm->nodes[mm->nnodes++];
        memset(&m, 0, sizeof(m));
        m.n.kind = n.kind;
        m.n.op = n.op;
        m.n.state = n.state;
        m.n.when_state = n.when_state;
        m.n.bind = n.bind;
        m.n.nbind = n.nbind;
        m.n.closes_menu = n.closes_menu;
        m.n.attr = text(n.attr) ? n.attr : 0;
        m.n.owner = c.name;
        m.n.group = group;
        m.label = n.label;
        if (const char *nl = renamed(ov, id)) {
            m.label = nl;
        }
        m.id = id;
        m.area = sa->key;                                          // the route's own strings name the tab
        m.tab = st->id;
        m.order = n.order;
        m.rank = c.rank;
        m.pos = i;
    }
}

// stable: nodes are in (rank, contributor name, declaration) order already, so sorting by (rank, order) leaves name and position as the rest
static void sort_nodes(MenuMerge *mm)
{
    for (int i = 1; i < mm->nnodes; i++) {
        MergedNode x = mm->nodes[i];
        int j = i - 1;
        while (j >= 0 && (mm->nodes[j].rank > x.rank || (mm->nodes[j].rank == x.rank && mm->nodes[j].order > x.order))) {
            mm->nodes[j + 1] = mm->nodes[j];
            j--;
        }
        mm->nodes[j + 1] = x;
    }
}

// ---- contributed tabs --------------------------------------------------------------------------------------------------------
// The route is copied into the merge's own storage, so a tab can be appended to an area without touching the (const) route it came from.
static void copy_route(const Skeleton *src, MenuMerge *mm)
{
    int na = src->nareas < MENU_AREAS_MAX ? src->nareas : MENU_AREAS_MAX;
    for (int a = 0; a < na; a++) {
        const SkelArea &s = src->areas[a];
        SkelArea &d = mm->own_areas[a];
        d = s;
        int nt = s.ntabs < MENU_TABS_MAX ? s.ntabs : MENU_TABS_MAX;
        for (int t = 0; t < nt; t++) {
            mm->own_tabs[a][t] = s.tabs[t];
        }
        d.tabs = mm->own_tabs[a];
        d.ntabs = nt;
    }
    mm->own_skel.areas = mm->own_areas;
    mm->own_skel.nareas = na;
    mm->skel = &mm->own_skel;
}

static void merge_tabs(const Contributor &c, MenuMerge *mm)
{
    const LibRecord *r = c.rec;
    for (int i = 0; r && i < r->ntabs; i++) {
        const LibTabDecl &t = r->tabs[i];
        if (!text(t.area) || !text(t.id) || !text(t.name) || t.widget > TW_NODES) {
            refuse(mm, "%s: a tab needs an area, an id, a name and a widget", c.name);
            continue;
        }
        SkelArea *a = 0;
        for (int k = 0; k < mm->own_skel.nareas; k++) {
            if (!strcmp(mm->own_areas[k].key, t.area)) {
                a = &mm->own_areas[k];
            }
        }
        if (!a) {
            refuse(mm, "%s: tab \"%s\" is for area %s, which the route does not have", c.name, t.id, t.area);
            continue;
        }
        bool dup = false;
        // a tab without a number is numbered by its place (the menu counts the tabs it shows), an explicit number is the description's own
        for (int k = 0; k < a->ntabs; k++) {
            dup |= !strcmp(a->tabs[k].id, t.id);
        }
        if (dup) {
            refuse(mm, "%s: area %s has a tab \"%s\" already", c.name, t.area, t.id);
            continue;
        }
        if (a->ntabs >= MENU_STRIP_TABS) {
            refuse(mm, "%s: area %s has %d tabs; tab \"%s\" does not fit (the tab strip has room for %d)", c.name, t.area, a->ntabs, t.id, MENU_STRIP_TABS);
            continue;
        }
        bool nclash = false;
        for (int k = 0; t.n && k < a->ntabs; k++) {
            nclash |= (a->tabs[k].n ? a->tabs[k].n : k + 1) == t.n;
        }
        if (nclash) {
            refuse(mm, "%s: area %s has a tab numbered %d already (tab \"%s\")", c.name, t.area, (int)t.n, t.id);
            continue;
        }
        SkelTab &d = mm->own_tabs[a - mm->own_areas][a->ntabs];
        d.id = t.id;
        d.name = t.name;
        d.n = t.n;                                                // 0: numbered by its place among the tabs shown (empty ones are dropped)
        d.widget = t.widget;
        d.groups = text(t.groups) ? t.groups : 0;
        d.layout = LIB_RECORD_COVERS(r->struct_size, tab_layouts) && r->tab_layouts && text(r->tab_layouts[i]) ? r->tab_layouts[i] : 0;
        a->ntabs++;
    }
}

static void merge_poll(const Contributor &c, const OpRegistry *reg, MenuMerge *mm)
{
    const LibRecord *r = c.rec;
    if (!r || !text(r->poll.op)) {
        return;
    }
    if (r->poll.period_ms < 1 || mm->npolls >= CONTRIB_POLLS_MAX) {
        refuse(mm, "%s: poll \"%s\" has no period or no room (%d polls at most)", c.name, r->poll.op, CONTRIB_POLLS_MAX);
        return;
    }
    if (reg) {
        const OpDef *o = reg->find_op(r->poll.op);
        if (!o) {
            refuse(mm, "%s: poll operation \"%s\" is not registered", c.name, r->poll.op);
            return;
        }
        if (o->nargs != 0) {
            refuse(mm, "%s: poll operation \"%s\" takes arguments; a poll takes none", c.name, r->poll.op);
            return;
        }
    }
    mm->polls[mm->npolls].owner = c.name;
    mm->polls[mm->npolls].op = r->poll.op;
    mm->polls[mm->npolls].period_ms = r->poll.period_ms;
    mm->polls[mm->npolls].when_frozen = r->poll.when_frozen;
    mm->npolls++;
}

int contrib_add_keys(MenuMerge *mm, const char *owner, const CoreKey *rows, int n)
{
    int added = 0;
    for (int i = 0; i < n; i++) {
        if (mm->nlibkeys >= CONTRIB_KEYS_MAX) {
            refuse(mm, "%s: key row %d has no room (%d library key rows at most)", owner, i + 1, CONTRIB_KEYS_MAX);
            break;
        }
        mm->libkeys[mm->nlibkeys++] = &rows[i];
        added++;
    }
    return added;
}

// ---- the merge ----------------------------------------------------------------------------------------------------------------
#define CONTRIBUTORS_MAX 32

int contrib_merge(const CoreDesc *d, const Skeleton *skel, const Overrides *ov, const Contributor *c, int nc, const OpRegistry *reg, CoreDesc *out,
                  MenuMerge *mm)
{
    memset(mm, 0, sizeof(*mm));
    copy_route(skel ? skel : contrib_default_skeleton(), mm);
    *out = *d;
    if (out->nsettings > SETTINGS_MAX) {
        out->nsettings = SETTINGS_MAX;
    }
    int nkept = 0;
    for (int i = 0; i < out->nsettings; i++) {
        // T228 R7: the description hides a row of the application's own (baseset.h) by its key, as it hides a contributed setting
        if (ov && ov->hide && baseset_is_generic(d->settings[i]) && in_list(ov->hide, ov->nhide, d->settings[i].key)) {
            continue;
        }
        mm->settings[nkept++] = d->settings[i];
    }
    out->nsettings = nkept;
    out->settings = mm->settings;
    int ntypes = 0;
    for (int i = 0; i < d->nfiletypes && i < CONTRIB_FILETYPES_MAX; i++) {
        mm->filetypes[ntypes++] = d->filetypes[i];
    }
    const int orig_types = ntypes;

    // the contributors in a fixed order: rank, then name (Alex's point 3: not the order they happened to load in)
    int order[CONTRIBUTORS_MAX];
    int no = nc < CONTRIBUTORS_MAX ? nc : CONTRIBUTORS_MAX;
    for (int i = 0; i < no; i++) {
        int j = i - 1;
        while (j >= 0 && (c[order[j]].rank > c[i].rank || (c[order[j]].rank == c[i].rank && strcmp(c[order[j]].name, c[i].name) > 0))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = i;
    }
    if (nc > CONTRIBUTORS_MAX) {
        refuse(mm, "%d contributors; the menu merges at most %d", nc, CONTRIBUTORS_MAX);
    }

    static Pending pend[CONTRIB_ACTIONS_MAX];
    int npend = 0;
    // the tabs first: a node, a setting's group and a poll may all belong to a tab a contributor adds
    for (int k = 0; k < no; k++) {
        if (c[order[k]].rec) {
            merge_tabs(c[order[k]], mm);
        }
    }
    const char *slots[8];
    int nslots = contrib_slots(d, slots, 8);
    static Expanded ex;
    for (int k = 0; k < no; k++) {
        const Contributor &ct0 = c[order[k]];
        if (!ct0.rec) {
            continue;
        }
        Contributor ct = ct0;
        ct.rec = expand_record(ct0, slots, nslots, mm, &ex);
        merge_settings(ct, ov, reg, mm, out);
        gather_actions(ct, ov, reg, mm, &ntypes, pend, &npend);
        merge_nodes(d, ct, mm->skel, ov, reg, mm);
        merge_poll(ct0, reg, mm);
    }
    sort_nodes(mm);

    // per extension: the descriptor's actions, then the contributed ones in merge order, in one run of mm->actions
    int used = 0;
    for (int t = 0; t < ntypes; t++) {
        FileType &ft = mm->filetypes[t];
        int own = t < orig_types ? ft.nactions : 0;
        const FileAction *own_a = t < orig_types ? ft.actions : 0;
        int add = 0;
        for (int p = 0; p < npend; p++) {
            add += pend[p].type == t;
        }
        if (!add) {
            continue;                                              // untouched: keeps the descriptor's own array
        }
        if (used + own + add > CONTRIB_ACTIONS_MAX) {
            refuse(mm, "the actions of \"%s\" have no room (%d at most)", ft.ext, CONTRIB_ACTIONS_MAX);
            ft.nactions = own;                                     // a new type with none is dropped below
            continue;
        }
        FileAction *dst = mm->actions + used;
        for (int i = 0; i < own; i++) {
            dst[i] = own_a[i];
        }
        int w = own;
        for (int p = 0; p < npend; p++) {
            if (pend[p].type == t) {
                dst[w++] = pend[p].a;
            }
        }
        ft.actions = dst;
        ft.nactions = w;
        used += w;
    }
    mm->nactions = used;
    int kept = 0;
    for (int t = 0; t < ntypes; t++) {
        if (t < orig_types || mm->filetypes[t].nactions > 0) {
            mm->filetypes[kept++] = mm->filetypes[t];
        }
    }
    out->filetypes = mm->filetypes;
    out->nfiletypes = kept;
    return mm->nrefused;
}
