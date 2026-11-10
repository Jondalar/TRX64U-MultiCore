#include "menutree.h"
#include "contrib.h"
#include "mediamounts.h"
#include "home.h"
#include "partial.h"
#include "ctx.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

const char *const menu_info_labels[] = { "Core", "Application", "Network", "Media", "ROMs", "Libraries", 0 };

static void add(MenuTree *t, EntryKind k, const char *label, int index)
{
    if (t->n < MENU_MAX_ENTRIES) {
        t->e[t->n].kind = k;
        t->e[t->n].label = label;
        t->e[t->n].index = index;
        t->n++;
    }
}

static MenuArea *area(MenuTree *t, const char *key, const char *name, bool shifted)
{
    if (t->nareas >= MENU_AREAS_MAX) {
        return &t->a[MENU_AREAS_MAX - 1];
    }
    MenuArea *a = &t->a[t->nareas++];
    memset(a, 0, sizeof(*a));
    a->key = key;
    a->name = name;
    a->shifted = shifted;
    return a;
}

// Opens a tab; close it with tab_end once its entries are added. A tab with no entry is dropped, and an
// area left without a tab is dropped with it - a core with no settings has no Settings tab, and that is
// the whole of "the menu is a whitelist" in the route's terms.
static MenuTab *tab_begin(MenuTree *t, MenuArea *a, const char *name)
{
    if (a->ntabs >= MENU_TABS_MAX) {
        return 0;
    }
    MenuTab *tb = &a->tabs[a->ntabs];
    tb->name = name;
    tb->n = (uint8_t)(a->ntabs + 1);
    tb->first = t->n;
    tb->count = 0;
    return tb;
}

static void tab_end(MenuTree *t, MenuArea *a, MenuTab *tb)
{
    if (!tb) {
        return;
    }
    tb->count = t->n - tb->first;
    // an application's screen that is only state (the progress of a job) has no entry of its own and is still a screen
    if (tb->count > 0 || (!strcmp(a->key, "APP") && tb->layout && tb->layout[0])) {
        a->ntabs++;
    }
}

static void area_end(MenuTree *t, MenuArea *a)
{
    if (a->ntabs == 0 && t->nareas && &t->a[t->nareas - 1] == a) {
        t->nareas--;
    }
}

// The entries that belong in a tab that lists groups (settings, tasks, media): with `groups`, those that name one of them; without, those that name no
// group or a group no tab of the route lists for this widget (a contribution is never lost for want of a tab: it lands where the ungrouped ones are).
static bool group_in_tab(const MenuMerge *mm, uint8_t widget, const SkelTab &tb, const char *g)
{
    if (tb.groups) {
        return groups_have(tb.groups, g);
    }
    if (!g || !mm) {
        return true;
    }
    for (int a = 0; a < mm->skel->nareas; a++) {
        for (int t = 0; t < mm->skel->areas[a].ntabs; t++) {
            const SkelTab &o = mm->skel->areas[a].tabs[t];
            if (o.widget == widget && groups_have(o.groups, g)) {
                return false;
            }
        }
    }
    return true;
}

static const char *setting_group(const CoreDesc *d, const MenuMerge *mm, int i)
{
    const char *g = mm ? mm->setting_group[i] : 0;
    return g ? g : d->settings[i].group;
}

void menu_build(const CoreDesc *d, MenuTree *t, bool return_host, bool reload_core, const MenuMerge *mm)
{
    memset(t, 0, sizeof(*t));
    const Skeleton *sk = mm && mm->skel ? mm->skel : contrib_default_skeleton();
    for (int ai = 0; ai < sk->nareas; ai++) {
        const SkelArea &sa = sk->areas[ai];
        MenuArea *a = area(t, sa.key, sa.name ? sa.name : (d->short_title ? d->short_title : d->cor_name), sa.shifted);
        for (int ti = 0; ti < sa.ntabs; ti++) {
            const SkelTab &st = sa.tabs[ti];
            MenuTab *tb = tab_begin(t, a, st.name);
            if (!tb) {
                continue;
            }
            tb->id = st.id;
            tb->layout = st.layout;
            if (st.n) {
                tb->n = st.n;
            }
            // `nodes_first` (a tab's ui): what the description puts into the tab stands above the widget's own rows (a page of actions with its settings below)
            const char *lay = st.layout ? strchr(st.layout, ';') : 0;
            bool nodes_first = lay && ui_attr(lay + 1, "nodes_first");
            auto add_nodes = [&]() {
                for (int k = 0; mm && k < mm->nnodes; k++) {
                    const MergedNode &m = mm->nodes[k];
                    if (strcmp(m.area, sa.key) || strcmp(m.tab, st.id) || t->nnodes >= MENU_NODES_MAX) {
                        continue;
                    }
                    t->nodes[t->nnodes] = m.n;
                    add(t, EK_NODE, m.label, t->nnodes);
                    t->nnodes++;
                }
            };
            if (nodes_first) {
                add_nodes();
            }
            switch (st.widget) {
            case TW_INFO:
                if (st.groups) {                                             // a description names the lines of this tab (descrec.h), in its order
                    for (const char *p = st.groups; *p;) {
                        const char *e = strchr(p, ',');
                        size_t n = e ? (size_t)(e - p) : strlen(p);
                        for (int i = 0; menu_info_labels[i]; i++) {
                            if (strlen(menu_info_labels[i]) == n && !strncmp(menu_info_labels[i], p, n)) {
                                add(t, EK_INFO, menu_info_labels[i], i);
                            }
                        }
                        p = e ? e + 1 : p + n;
                    }
                    break;
                }
                for (int i = 0; menu_info_labels[i]; i++) {
                    add(t, EK_INFO, menu_info_labels[i], i);
                }
                break;
            case TW_BROWSER:
                add(t, EK_BROWSER, "Browse", 0);
                break;
            case TW_MEDIA:
                for (int i = 0; i < d->nmedia; i++) {
                    if (group_in_tab(mm, TW_MEDIA, st, d->media[i].group)) {
                        add(t, EK_MEDIA, d->media[i].name, i);
                    }
                }
                if (mounts_have_slots(d)) {                                 // T180 D5: the sets of media
                    add(t, EK_SAVE_SET, "Save media set...", 0);
                    add(t, EK_LOAD_SET, "Load media set...", 0);
                }
                break;
            case TW_SETTINGS:
                for (int i = 0; i < d->nsettings; i++) {
                    // Persist medium mounts is F8's: the System tab of the default route lists it; a description's tab lists it by its group ("system")
                    if (((!persist_is_generic(d->settings[i]) && !home_is_generic(d->settings[i])) || st.groups) && group_in_tab(mm, TW_SETTINGS, st, setting_group(d, mm, i))) {
                        add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                    }
                }
                break;
            case TW_TASKS:
                for (int i = 0; i < d->ntasks; i++) {
                    if (group_in_tab(mm, TW_TASKS, st, d->tasks[i].group)) {
                        add(t, EK_TASK, d->tasks[i].label, i);
                    }
                }
                break;
            case TW_SYSTEM:
                add(t, EK_RESET_HOST, "Reset to host defaults", 0);
                add(t, EK_RESTART_CORE, reload_core ? "Reload core" : "Restart core", reload_core ? 1 : 0);
                add(t, EK_BUG_REPORT, "Bug report", 0);
                add(t, EK_WIFI, "WiFi", 0);
                for (int i = 0; i < d->nsettings; i++) {
                    if (persist_is_generic(d->settings[i]) || home_is_generic(d->settings[i])) {   // T180 D2, D10
                        add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                    }
                }
                if (return_host) {
                    add(t, EK_RETURN_HOST, "Return to host menu", 0);
                }
                break;
            default:
                break;
            }
            // what libraries, applications and the description put into this tab, in the merge's order
            if (!nodes_first) {
                add_nodes();
            }
            tab_end(t, a, tb);
        }
        area_end(t, a);
    }
    printf("[menu] %d entries of %d, %d nodes of %d\n", t->n, MENU_MAX_ENTRIES, t->nnodes, MENU_NODES_MAX);
}

int menu_area_of_key(const MenuTree *t, int key)
{
    static const struct { int k; const char *name; } map[] = {
        { KEY_F1, "F1" }, { KEY_F2, "F2" }, { KEY_F3, "F3" }, { KEY_F4, "F4" },
        { KEY_F5, "F5" }, { KEY_F6, "F6" }, { KEY_F7, "F7" }, { KEY_F8, "F8" },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (map[i].k != key) {
            continue;
        }
        for (int a = 0; a < t->nareas; a++) {
            if (!strcmp(t->a[a].key, map[i].name)) {
                return a;
            }
        }
        return -1;
    }
    return -1;
}

int menu_tab_of_number(const MenuTree *t, int area, int n)
{
    if (area < 0 || area >= t->nareas) {
        return -1;
    }
    const MenuArea &a = t->a[area];
    for (int i = 0; i < a.ntabs; i++) {
        if (a.tabs[i].n == n) {
            return i;
        }
    }
    return -1;
}

// ---- the fixed entries as data -------------------------------------------------------------------------------------------------------------------
static const FixedDef g_fixed[] = {
    // kind, variant, op, pre_op, value_state, value_none, value_suffix, sub_state, ok_text, fail_text, closes
    { EK_RESET_HOST, 0, "settings.reset_to_host", 0, 0, 0, 0, 0, 0, 0, false },
    // a cold boot of the core without the host: on a DFX core its partial bitstream loaded again, then every ROM scanned and put, the RAM cleared, every setting written, released
    { EK_RESTART_CORE, 0, "machine.restart", 0, 0, 0, 0, 0, "Core restarted: ROMs loaded again", "The core did not restart", true },
    { EK_RESTART_CORE, 1, "machine.restart", 0, 0, 0, 0, 0, "Core reloaded: bitstream and ROMs loaded again", "The core did not reload", true },
    { EK_BUG_REPORT, 0, "diag.bug_report", 0, 0, 0, 0, 0, 0, 0, false },
    { EK_WIFI, 0, "wifi.toggle", 0, "wifi.onoff", "Not set", 0, "wifi.line", 0, 0, false },
    { EK_SAVE_SET, 0, "media.save_set", 0, 0, 0, 0, 0, 0, 0, false },
    { EK_LOAD_SET, 0, "media.load_set", 0, "media.sets", "none", " saved", 0, 0, 0, false },
    // T180 D4: the media first; one that cannot be written back keeps the core. The way back is the host's (MenuHost::return_to_host).
    { EK_RETURN_HOST, 0, 0, "media.flush_for_core", 0, 0, 0, 0, 0, 0, false },
};

const FixedDef *menu_fixed(EntryKind kind, int variant)
{
    for (unsigned i = 0; i < sizeof(g_fixed) / sizeof(g_fixed[0]); i++) {
        if (g_fixed[i].kind == kind && (g_fixed[i].variant == variant || kind != EK_RESTART_CORE)) {
            return &g_fixed[i];
        }
    }
    return 0;
}

void menu_build_for(const CoreCtx *c, MenuTree *t)
{
    menu_build(c->desc, t, !c->board || c->board->can_return_host, reload_is_dfx(c), c->merge);
}

static const char *kind_name(EntryKind k)
{
    switch (k) {
    case EK_TASK: return "task";
    case EK_MEDIA: return "media";
    case EK_BROWSER: return "browser";
    case EK_SETTING: return "setting";
    case EK_INFO: return "info";
    case EK_RESET_HOST: return "fixed";
    case EK_RESTART_CORE: return "fixed";
    case EK_RETURN_HOST: return "fixed";
    case EK_BUG_REPORT: return "fixed";
    case EK_WIFI: return "fixed";
    case EK_SAVE_SET: return "fixed";
    case EK_LOAD_SET: return "fixed";
    case EK_NODE: return "node";
    }
    return "?";
}

int menu_dump(const CoreDesc *d, const MenuTree *t, char *out, int max)
{
    int pos = 0;
#define EMIT(...) do { int w_ = snprintf(out + pos, max - pos, __VA_ARGS__); if (w_ < 0 || w_ >= max - pos) { out[pos] = 0; return -1; } pos += w_; } while (0)
    EMIT("core %c%c%c%c %s\n", d->id[0], d->id[1], d->id[2], d->id[3], d->cor_name);
    for (int ai = 0; ai < t->nareas; ai++) {
        const MenuArea &a = t->a[ai];
        EMIT("area %s %s%s\n", a.key, a.name, a.shifted ? " (shifted)" : "");
        for (int ti = 0; ti < a.ntabs; ti++) {
            const MenuTab &tb = a.tabs[ti];
            EMIT("  tab %u %s\n", (unsigned)tb.n, tb.name);
            for (int i = tb.first; i < tb.first + tb.count; i++) {
                const MenuEntry &e = t->e[i];
                EMIT("    %s %s", kind_name(e.kind), e.label);
                if (e.kind == EK_SETTING) {
                    const Setting &s = d->settings[e.index];
                    EMIT(" = ");
                    if (s.kind == 3) {
                        EMIT("a file the person selects (none by default, %s)", s.when == WHEN_RESET ? "at next reset" : "live");
                    }
                    for (int v = 0; v < (s.kind == 3 ? 0 : s.nvalues); v++) {
                        EMIT("%s%s", v ? " | " : "", s.values[v]);
                    }
                    if (s.kind != 3) {
                        EMIT(" (default %s, %s)", s.values[s.def], s.when == WHEN_RESET ? "at next reset" : "live");
                    }
                } else if (e.kind == EK_TASK) {
                    EMIT("%s%s", d->tasks[e.index].closes_menu ? " (closes the menu)" : "", d->tasks[e.index].ask ? " (asks a name)" : "");
                } else if (e.kind == EK_NODE) {
                    const MenuNode &n = t->nodes[e.index];
                    static const char *const kinds[] = { "button", "setting", "state", "widget", "group" };
                    EMIT(" = %s", n.kind < 5 ? kinds[n.kind] : "?");
                    if (n.op) {
                        EMIT(" calls %s", n.op);
                    }
                    if (n.state) {
                        EMIT(" shows %s", n.state);
                    }
                    if (n.when_state && n.when_state[0]) {
                        EMIT(" when %s", n.when_state);
                    }
                    if (n.group && n.group[0]) {
                        EMIT(" in group %s", n.group);
                    }
                    EMIT("%s from %s", n.closes_menu ? " (closes the menu)" : "", n.owner ? n.owner : "?");
                } else if (e.kind == EK_BROWSER) {
                    EMIT("\n");
                    for (int f = 0; f < d->nfiletypes; f++) {
                        EMIT("      filetype %s:", d->filetypes[f].ext);
                        for (int ac = 0; ac < d->filetypes[f].nactions; ac++) {
                            EMIT("%s %s", ac ? " |" : "", d->filetypes[f].actions[ac].label);
                        }
                        if (d->filetypes[f].ninner) {
                            EMIT("\n        inside %s:", d->filetypes[f].inner_ext);
                            for (int ac = 0; ac < d->filetypes[f].ninner; ac++) {
                                EMIT("%s %s", ac ? " |" : "", d->filetypes[f].inner[ac].label);
                            }
                        }
                        EMIT("%s", f + 1 < d->nfiletypes ? "\n" : "");
                    }
                }
                EMIT("\n");
            }
        }
    }
#undef EMIT
    return pos;
}
