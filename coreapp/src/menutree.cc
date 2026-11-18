#include "menutree.h"
#include "contrib.h"
#include "mediamounts.h"
#include "home.h"
#include "partial.h"
#include "ctx.h"
#include "keyboard.h"
#include "picture.h"
#include "volume.h"
#include "tone.h"
#include "hotkeys.h"
#include <stdio.h>
#include <string.h>

#define MENU_SECTIONS_MAX 16

const char *const menu_info_labels[] = { "Core", "Application", "Network", "Media", "ROMs", "Libraries", 0 };

// Calls f(word, length) for each word of `s` (l characters, not terminated) between the separators `sep`.
template <class F> static void each_word(const char *s, size_t l, char sep, F f)
{
    for (const char *p = s, *end = s + l; s && p < end;) {
        const char *e = (const char *)memchr(p, sep, (size_t)(end - p));
        size_t wl = e ? (size_t)(e - p) : (size_t)(end - p);
        f(p, wl);
        p = e ? e + 1 : end;
    }
}

// T228 D18, R6: the pages a tab's `pages` names, in that order. "G" is the page of the group G; "Name=G1+G2" is a page named Name that holds the groups G1 and
// G2, each under its heading. A group a page holds is no section of the tab, and a node anchored to it (area/tab/G) stands on that page after the group's settings.
struct TabPages {
    int n = 0;
    const char *name[MENU_SECTIONS_MAX];
    size_t name_l[MENU_SECTIONS_MAX];
    const char *groups[MENU_SECTIONS_MAX];                              // '+' separated, not terminated
    size_t groups_l[MENU_SECTIONS_MAX];

    explicit TabPages(const char *attr)
    {
        size_t l = attr ? strlen(attr) : 0;
        each_word(attr, l, ',', [&](const char *w, size_t wl) {
            if (n >= MENU_SECTIONS_MAX) {
                printf("[menu] a tab names more than %d pages: the rest are not drawn\n", MENU_SECTIONS_MAX);
                return;
            }
            const char *eq = (const char *)memchr(w, '=', wl);
            name[n] = w;
            name_l[n] = eq ? (size_t)(eq - w) : wl;
            groups[n] = eq ? eq + 1 : w;
            groups_l[n] = eq ? wl - (size_t)(eq - w) - 1 : wl;
            n++;
        });
    }
    // the page that holds the group g (gl characters), or -1
    int of(const char *g, size_t gl) const
    {
        for (int k = 0; g && k < n; k++) {
            bool in = false;
            each_word(groups[k], groups_l[k], '+', [&](const char *w, size_t wl) { in = in || (wl == gl && !strncmp(w, g, gl)); });
            if (in) {
                return k;
            }
        }
        return -1;
    }
};

// A group id as a heading: '_' reads as a space. The text lives in the tree (an entry keeps the pointer); null when the pool is full.
static const char *menu_heading_text(MenuTree *t, const char *g, size_t n)
{
    if (t->nheads >= MENU_HEADS_MAX) {
        printf("[menu] more than %d section headings: the rest are not drawn\n", MENU_HEADS_MAX);
        return 0;
    }
    char *o = t->heads[t->nheads++];
    if (n > sizeof(t->heads[0]) - 1) {
        n = sizeof(t->heads[0]) - 1;
    }
    for (size_t i = 0; i < n; i++) {
        o[i] = g[i] == '_' ? ' ' : g[i];
    }
    o[n] = 0;
    return o;
}

static void add(MenuTree *t, EntryKind k, const char *label, int index)
{
    if (t->n < MENU_MAX_ENTRIES) {
        t->e[t->n].kind = k;
        t->e[t->n].label = label;
        t->e[t->n].index = index;
        t->e[t->n].dialog = false;
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
    tb->page = false;
    tb->parent = -1;
    tb->home = false;
    tb->drive_page = false;
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
static bool group_in_tab(const Skeleton *sk, uint8_t widget, const SkelTab &tb, const char *g)
{
    if (tb.groups) {
        return groups_have(tb.groups, g);
    }
    if (!g) {
        return true;
    }
    for (int a = 0; a < sk->nareas; a++) {
        for (int t = 0; t < sk->areas[a].ntabs; t++) {
            const SkelTab &o = sk->areas[a].tabs[t];
            if ((o.widget == widget || (widget == TW_SETTINGS && o.widget == TW_SYSTEM)) && groups_have(o.groups, g)) {      // a System tab's groups are its pages (R6)
                return false;
            }
        }
    }
    return true;
}

// T226 S2: a task that names a group which a settings tab lists and no tasks tab lists is a row of that settings tab (the pad's "Map buttons" on Input), not of Actions.
static bool task_in_settings_tab(const Skeleton *sk, const SkelTab &tb, const char *g)
{
    if (!g || tb.widget != TW_SETTINGS || !tb.groups || !groups_have(tb.groups, g)) {
        return false;
    }
    for (int a = 0; a < sk->nareas; a++) {
        for (int t = 0; t < sk->areas[a].ntabs; t++) {
            if (sk->areas[a].tabs[t].widget == TW_TASKS && groups_have(sk->areas[a].tabs[t].groups, g)) {
                return false;
            }
        }
    }
    return true;
}
static bool task_in_any_settings_tab(const Skeleton *sk, const char *g)
{
    for (int a = 0; a < sk->nareas; a++) {
        for (int t = 0; t < sk->areas[a].ntabs; t++) {
            if (task_in_settings_tab(sk, sk->areas[a].tabs[t], g)) {
                return true;
            }
        }
    }
    return false;
}

static bool skel_has_widget(const Skeleton *sk, uint8_t widget)
{
    for (int a = 0; a < sk->nareas; a++) {
        for (int t = 0; t < sk->areas[a].ntabs; t++) {
            if (sk->areas[a].tabs[t].widget == widget) {
                return true;
            }
        }
    }
    return false;
}

// ---- T187 D1: where a generic row lands, one table for every core ---------------------------------------------------------------------------
static const MenuGroupRoute g_routes[] = {
    { "video",   "F5", "video", "Video",   0,         false },
    { "audio",   "F5", "audio", "Audio",   0,         false },
    { "hotkeys", "F7", "settings", "Hotkeys", "Hotkeys", true },        // T212 D1/D6: the app's settings stand on F7 (APP), not in the machine's F5
    { "storage", "F7", "settings", "Storage", 0,      false },
    { "menu",    "F7", "settings", "Menu",    0,      false },
};

const MenuGroupRoute *menu_route_of(const Setting &s)
{
    const char *g = 0;
    if (!s.apply) {
        return 0;
    }
    // the application's own Picture, Scale, Smoothing and Frame follow items, matched by op and key: this branch routes them into the Video tab.
    // desc_check refuses a core setting that uses either the key or the op (tests only); on the board this match is the guard
    if ((!strcmp(s.apply, PICTURE_OP) && !strcasecmp(s.key, PICTURE_KEY)) || (!strcmp(s.apply, SCALE_OP) && !strcasecmp(s.key, SCALE_KEY)) ||
        (!strcmp(s.apply, SMOOTH_OP) && !strcasecmp(s.key, SMOOTH_KEY)) || (!strcmp(s.apply, FOLLOW_OP) && !strcasecmp(s.key, FOLLOW_KEY))) {
        g = "video";
    } else if (volume_is_generic(s) || tone_is_generic(s)) {
        g = "audio";
    } else if (hk_is_generic(s)) {
        g = "hotkeys";
    } else if (home_is_generic(s) && !strcasecmp(s.key, REOPEN_KEY)) {
        g = "menu";
    } else if (persist_is_generic(s) || home_is_generic(s)) {
        g = "storage";
    }
    for (unsigned i = 0; g && i < sizeof(g_routes) / sizeof(g_routes[0]); i++) {
        if (!strcmp(g_routes[i].group, g)) {
            return &g_routes[i];
        }
    }
    return 0;
}

// The tab of the route where the rows of `r` are listed: the area's tab of that id, else (a core that has no such tab) its tab "settings"; null when the area has neither,
// and the rows are then listed where the ungrouped settings are (a contribution is never lost for want of a tab).
static const SkelTab *route_tab(const Skeleton *sk, const MenuGroupRoute *r)
{
    const SkelTab *fb = 0;
    for (int a = 0; a < sk->nareas; a++) {
        if (strcmp(sk->areas[a].key, r->area)) {
            continue;
        }
        for (int i = 0; i < sk->areas[a].ntabs; i++) {
            const SkelTab &tb = sk->areas[a].tabs[i];
            if (tb.widget != TW_SETTINGS) {
                continue;
            }
            if (tb.id && !strcmp(tb.id, r->tab)) {
                return &tb;
            }
            if (tb.id && !strcmp(tb.id, "settings")) {
                fb = &tb;
            }
        }
    }
    return fb;
}

static const char *setting_group(const CoreDesc *d, const MenuMerge *mm, int i)
{
    const char *g = mm ? mm->setting_group[i] : 0;
    return g ? g : d->settings[i].group;
}

// T212 D5: a task belongs to a drive when a media slot names it in its lists, or when it is that slot's eject (the list's own Eject row does that).
bool menu_task_claimed(const CoreDesc *d, int ti)
{
    const Task &t = d->tasks[ti];
    for (int m = 0; m < d->nmedia; m++) {
        const MediaSlot &s = d->media[m];
        if (groups_have(s.empty_tasks, t.label) || groups_have(s.mounted_tasks, t.label)) {
            return true;
        }
        if (s.eject_op && t.op && !strcmp(t.op, s.eject_op) && strstr(t.label, "Eject")) {
            return true;
        }
    }
    return false;
}

// T212 D8: where a task of the default route stands besides Home's reset block: group "top" = on top of Home beside the resets, "machine" = F5's first tab,
// "input" = F5's Input tab. A description with a tasks tab of its own keeps every task there (the callers check that).
static bool task_in_group(const Task &t, const char *g) { return t.group && !strcmp(t.group, g); }
static bool task_on_f5(const Task &t) { return task_in_group(t, "machine") || task_in_group(t, "input"); }

bool menu_slot_is_drive(const MediaSlot &m)
{
    return m.eject_op || m.mount || m.empty_tasks || m.mounted_tasks;
}

void menu_build(const CoreDesc *d, MenuTree *t, bool return_host, bool reload_core, const MenuMerge *mm, MenuVisFn vis, void *vis_user)
{
    memset(t, 0, sizeof(*t));
    const Skeleton *sk = mm && mm->skel ? mm->skel : contrib_default_skeleton();
    for (int ai = 0; ai < sk->nareas; ai++) {
        const SkelArea &sa = sk->areas[ai];
        MenuArea *a = area(t, sa.key, sa.name ? sa.name : (d->short_title ? d->short_title : d->cor_name), sa.shifted);
        for (int ti = 0; ti < sa.ntabs; ti++) {
            const SkelTab &st = sa.tabs[ti];
            MenuTab *tb = tab_begin(t, a, st.name ? st.name : a->name);        // T205 D1: a tab with no name is named after its area (F5: the core)
            if (!tb) {
                continue;
            }
            tb->id = st.id;
            tb->layout = st.layout && st.layout[0] != ';' ? st.layout : 0;      // a layout with no name only carries attributes (`sections`): the tab is a list
            if (st.n) {
                tb->n = st.n;
            }
            // `nodes_first` (a tab's ui): what the description puts into the tab stands above the widget's own rows (a page of actions with its settings below)
            const char *lay = st.layout ? strchr(st.layout, ';') : 0;
            // T228 R1 (`page`, a tab's ui): a screen a row opens (menu.screen) is a page of the tab it names, an earlier one of this area: no number, no place
            // on the strip, and leaving it returns there (the Timewarp and Monitor screens of Home)
            char page_of[32];
            if (lay && ui_attr(lay + 1, "page", page_of, sizeof(page_of))) {
                for (int b = 0; b < a->ntabs; b++) {
                    if (a->tabs[b].id && !strcmp(a->tabs[b].id, page_of)) {
                        tb->page = true;
                        tb->parent = (int8_t)b;
                        tb->n = 0;
                    }
                }
            }
            bool nodes_first = lay && ui_attr(lay + 1, "nodes_first");
            // `sections` (a tab's ui, T228 D13/D14/D16/DC6): a settings tab that lists several groups draws each one under a heading made from the group's id
            const bool sections = lay && ui_attr(lay + 1, "sections");
            const char *tab_title = st.name ? st.name : a->name;
            // T228 D18, R6: the pages of the tab (`pages`, TabPages); the k-th row that opens one is entry -(k+1), its page the k-th page tab after this one
            char pages_attr[256];
            pages_attr[0] = 0;
            if (lay && (st.widget == TW_SYSTEM || (st.widget == TW_SETTINGS && sections))) {     // a plain list shows the groups as they are
                ui_attr(lay + 1, "pages", pages_attr, sizeof(pages_attr));
            }
            const TabPages pages(pages_attr);
            int npg = 0;
            int pg_of[MENU_SECTIONS_MAX];                                // the k-th page row: its index in `pages`
            const char *pg_name[MENU_SECTIONS_MAX];
            auto node_here = [&](const MergedNode &m) { return !strcmp(m.area, sa.key) && !strcmp(m.tab, st.id); };
            auto node_in = [&](const MergedNode &m, const char *g, size_t gl) { return node_here(m) && m.n.group && strlen(m.n.group) == gl && !strncmp(m.n.group, g, gl); };
            auto add_node = [&](const MergedNode &m) {
                if (t->nnodes < MENU_NODES_MAX) {
                    t->nodes[t->nnodes] = m.n;
                    add(t, EK_NODE, m.label, t->nnodes);
                    t->nnodes++;
                }
            };
            // want: 0 every node of the tab, 1 those that are not state lines, 2 the state lines (T228: the System tab's Status block takes the description's state lines);
            // a node anchored to a group a page holds stands on that page
            auto add_nodes = [&](int want = 0) {
                for (int k = 0; mm && k < mm->nnodes; k++) {
                    const MergedNode &m = mm->nodes[k];
                    if (!node_here(m) || (m.n.group && pages.of(m.n.group, strlen(m.n.group)) >= 0)) {
                        continue;
                    }
                    if (st.widget == TW_SYSTEM && node_in(m, "actions", 7)) {   // gate fix 2: the System tab's own Actions heading takes these (below)
                        continue;
                    }
                    if ((want == 1 && m.n.kind == LN_STATE) || (want == 2 && m.n.kind != LN_STATE)) {
                        continue;
                    }
                    if (m.n.kind == LN_GROUP && !tb->layout) {                // a group node names the rows after it: in a flat list a heading, as a screen draws it (IK_HEADING)
                        add(t, EK_HEADING, m.label, 0);
                        continue;
                    }
                    add_node(m);
                }
            };
            // a setting of this tab in the group g (gl characters): not one the route places in a tab of its own
            auto setting_in = [&](int i, const char *g, size_t gl) {
                const MenuGroupRoute *r = menu_route_of(d->settings[i]);
                const char *sg = setting_group(d, mm, i);
                return !(r && route_tab(sk, r)) && sg && strlen(sg) == gl && !strncmp(sg, g, gl) && group_in_tab(sk, TW_SETTINGS, st, sg);
            };
            // Does the group have a row to show? With `vis` a setting whose show_state does not hold now is not one. A group that has settings is made or not by them
            // alone (its state lines and nodes only stand on a page that is there); a group of nodes only by its nodes, minus those whose `hide` holds.
            auto group_has_rows = [&](const char *g, size_t gl) {
                bool own = false;
                for (int i = 0; i < d->nsettings; i++) {
                    if (setting_in(i, g, gl)) {
                        own = true;
                        if (!(vis && d->settings[i].show_state && !vis(vis_user, d->settings[i].show_state))) {
                            return true;
                        }
                    }
                }
                for (int k = 0; !own && mm && k < mm->nnodes; k++) {
                    char hs[64];
                    const bool off = vis && mm->nodes[k].n.attr && ui_attr(mm->nodes[k].n.attr, "hide", hs, sizeof(hs)) && hs[0] == '@' && vis(vis_user, hs + 1);
                    if (node_in(mm->nodes[k], g, gl) && !off) {
                        return true;
                    }
                }
                return false;
            };
            // the rows that open the pages, in the order `pages` names them; a page with no row of its own has none. `pages_head` (a tab's ui) is a heading above them
            auto add_page_rows = [&]() {
                char head[32];
                bool head_done = !(lay && ui_attr(lay + 1, "pages_head", head, sizeof(head)) && head[0]);
                for (int k = 0; k < pages.n && npg < MENU_SECTIONS_MAX; k++) {
                    bool any = false;
                    each_word(pages.groups[k], pages.groups_l[k], '+', [&](const char *g, size_t gl) { any = any || group_has_rows(g, gl); });
                    if (!any) {
                        continue;
                    }
                    if (!head_done) {
                        head_done = true;
                        const char *h = menu_heading_text(t, head, strlen(head));
                        if (h) {
                            add(t, EK_HEADING, h, 0);
                        }
                    }
                    const char *h = menu_heading_text(t, pages.name[k], pages.name_l[k]);
                    if (h) {
                        pg_of[npg] = k;
                        pg_name[npg] = h;
                        add(t, EK_OPENS, h, -(npg + 1));
                        npg++;
                    }
                }
            };
            if (nodes_first) {
                add_nodes();
            }
            bool nodes_done = false;
            bool drive_page_wanted = false;                              // T212 D5: Home has drive rows that open a list
            const MenuGroupRoute *page_route = 0;                       // set when a row of this tab opens a page (T187 D2)
            int opens_index = -1;
            switch (st.widget) {
            case TW_INFO: {
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
                // T212 D1/D5/D7: Home is the session and its quick actions, no information lines. The core's reset tasks (they close the menu) stand on top, then
                // the drive rows (RETURN opens the list of that drive), the tasks no drive claims, and the configs.
                tb->home = true;
                const bool tasks_here = !skel_has_widget(sk, TW_TASKS);        // a description with a tasks tab of its own keeps its tasks there
                for (int i = 0; tasks_here && i < d->ntasks; i++) {
                    if ((d->tasks[i].closes_menu || task_in_group(d->tasks[i], "top")) && !menu_task_claimed(d, i) && !task_on_f5(d->tasks[i])) {
                        add(t, EK_TASK, d->tasks[i].label, i);
                    }
                }
                if (!skel_has_widget(sk, TW_MEDIA)) {
                    for (int i = 0; i < d->nmedia; i++) {
                        add(t, EK_MEDIA, d->media[i].name, i);
                        drive_page_wanted |= menu_slot_is_drive(d->media[i]);
                    }
                    for (int i = 0; tasks_here && i < d->ntasks; i++) {
                        if (!d->tasks[i].closes_menu && !task_in_group(d->tasks[i], "top") && !menu_task_claimed(d, i) && !task_on_f5(d->tasks[i])) {
                            add(t, EK_TASK, d->tasks[i].label, i);
                        }
                    }
                    if (mounts_have_slots(d)) {
                        add(t, EK_SAVE_SET, "Save as config...", 0);
                        add(t, EK_LOAD_SET, "Load config...", 0);
                    }
                }
                break;
            }
            case TW_BROWSER:
                add(t, EK_BROWSER, "Browse", 0);
                break;
            case TW_MEDIA:
                for (int i = 0; i < d->nmedia; i++) {
                    if (group_in_tab(sk, TW_MEDIA, st, d->media[i].group)) {
                        add(t, EK_MEDIA, d->media[i].name, i);
                    }
                }
                if (mounts_have_slots(d)) {                                 // T198 D3: the configs (media and settings) replace the media sets of T180 D5
                    add(t, EK_SAVE_SET, "Save as config...", 0);
                    add(t, EK_LOAD_SET, "Load config...", 0);
                }
                break;
            case TW_SETTINGS: {
                // the rows the table places in this tab (routed), and the machine's own (the descriptor's groups); with both a heading names each block
                auto routed_here = [&](int i) -> const MenuGroupRoute * {
                    const MenuGroupRoute *r = menu_route_of(d->settings[i]);
                    return r && route_tab(sk, r) == &st ? r : 0;
                };
                auto machine_here = [&](int i) -> bool {
                    const MenuGroupRoute *r = menu_route_of(d->settings[i]);
                    if (r && route_tab(sk, r)) {
                        return false;
                    }
                    return group_in_tab(sk, TW_SETTINGS, st, setting_group(d, mm, i));
                };
                int nmachine = 0, nrouted = 0;
                for (int i = 0; i < d->nsettings; i++) {
                    if (machine_here(i)) {
                        nmachine++;
                    } else if (routed_here(i)) {
                        nrouted++;
                    }
                }
                // headings only in the catch-all tab of a plain list (the default route's Settings): two blocks are told apart there; a tab that names its groups is one block
                // T212: the APP tab (groups "app") holds nothing but routed rows, each block under its heading
                bool app_tab = st.groups && groups_have(st.groups, "app");
                bool heads = nrouted > 0 && ((nmachine > 0 && !st.groups && !(st.layout && st.layout[0])) || app_tab);
                auto add_routed = [&](int i, const MenuGroupRoute *r) {
                    if (r->page) {                                           // T187 D2: one row opens the page; the page is a tab of its own after this one
                        if (!page_route) {
                            page_route = r;                                  // T216 D12: the row that opens the page stands last in the tab, after the task rows
                            opens_index = i;
                        }
                    } else {
                        add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                    }
                };
                // T228 D13/D14/D16: a tab with `sections` that lists several groups of the description's own (not "app") shows each group's block under a heading made
                // from the group's id ("Picture", "SID_sockets" -> "SID sockets"; not drawn when it repeats the tab's title, DC6), in the order the tab lists them;
                // the routed rows (Picture, Scale, Volume) follow the first block, in the APP tab the routed blocks come after all of them
                int sec_n = 0;
                const char *sec_g[MENU_SECTIONS_MAX];
                size_t sec_l[MENU_SECTIONS_MAX];
                for (const char *p = sections ? st.groups : 0; p && *p;) {
                    const char *e = strchr(p, ',');
                    size_t n = e ? (size_t)(e - p) : strlen(p);
                    if (!(n == 3 && !strncmp(p, "app", 3))) {
                        if (sec_n >= MENU_SECTIONS_MAX) {
                            printf("[menu] tab %s lists more than %d groups: the rest are not drawn\n", st.id ? st.id : "?", MENU_SECTIONS_MAX);
                            break;
                        }
                        sec_g[sec_n] = p;
                        sec_l[sec_n++] = n;
                    }
                    p = e ? e + 1 : p + n;
                }
                if (sec_n > 1 || (app_tab && sec_n >= 1)) {
                    auto in_section = [&](const char *sg) {
                        for (int k = 0; sg && k < sec_n; k++) {
                            if (strlen(sg) == sec_l[k] && !strncmp(sg, sec_g[k], sec_l[k])) {
                                return true;
                            }
                        }
                        return false;
                    };
                    for (int g = 0; g < sec_n; g++) {
                        bool head_done = false;
                        if (pages.of(sec_g[g], sec_l[g]) >= 0) {
                            continue;
                        }
                        for (int i = 0; i < d->nsettings; i++) {
                            const MenuGroupRoute *rt = routed_here(i);
                            // a routed row that names one of the tab's groups stands in it, unless it opens a page (its EK_OPENS row is made below)
                            const char *sg = (machine_here(i) || (!heads && rt && !rt->page)) ? setting_group(d, mm, i) : 0;
                            if (!sg || strlen(sg) != sec_l[g] || strncmp(sg, sec_g[g], sec_l[g])) {
                                continue;
                            }
                            if (!head_done) {
                                head_done = true;
                                const char *h = menu_heading_text(t, sec_g[g], sec_l[g]);
                                if (h && strcasecmp(h, tab_title ? tab_title : "")) {
                                    add(t, EK_HEADING, h, 0);
                                } else if (h) {
                                    t->nheads--;                         // DC6: the heading repeats the tab's title
                                }
                            }
                            add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                        }
                        if (g == 0 && !heads) {
                            for (int i = 0; i < d->nsettings; i++) {
                                const MenuGroupRoute *rt = routed_here(i);
                                if (!machine_here(i) && rt && (rt->page || !in_section(setting_group(d, mm, i)))) {
                                    add_routed(i, rt);
                                }
                            }
                        }
                    }
                    add_page_rows();                                         // T228 D18: the rows that open the pages stand after the sections
                    for (int i = 0; i < d->nsettings; i++) {                 // a row of group "app" in a tab that lists other groups too has no block: it stands last, under none
                        const char *sg = setting_group(d, mm, i);
                        if (machine_here(i) && sg && !strcmp(sg, "app")) {
                            add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                        }
                    }
                } else {
                if (heads && nmachine > 0) {
                    add(t, EK_HEADING, "Machine", 0);
                }
                for (int i = 0; i < d->nsettings; i++) {                     // the descriptor's order; without headings the routed rows keep their place among them
                    if (machine_here(i)) {
                        add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                    } else if (!heads && routed_here(i)) {
                        add_routed(i, routed_here(i));
                    }
                }
                }
                if (heads) {                                                 // one block per route group (Storage, Menu), each under its heading
                    for (int g = 0; g < (int)(sizeof(g_routes) / sizeof(g_routes[0])); g++) {
                        bool head_done = false;
                        for (int i = 0; i < d->nsettings; i++) {
                            const MenuGroupRoute *r = machine_here(i) ? 0 : routed_here(i);
                            if (r != &g_routes[g]) {
                                continue;
                            }
                            if (!head_done && !r->page) {                    // a page row brings its heading where the row stands (below), not before it is placed
                                add(t, EK_HEADING, r->heading, 0);
                                head_done = true;
                            }
                            add_routed(i, r);
                        }
                    }
                }
                // T212 D8: the tasks that are the machine's own settings (Map buttons, 50 / 60 Hz, CPU speed) stand below the settings of the tab named for their group;
                // T226 S2: so does a task whose group a settings tab lists and no tasks tab does
                for (int i = 0; i < d->ntasks; i++) {
                    bool here = task_in_settings_tab(sk, st, d->tasks[i].group);
                    if (!here && !skel_has_widget(sk, TW_TASKS)) {
                        here = st.groups ? (groups_have(st.groups, "input") && task_in_group(d->tasks[i], "input"))
                                         : (st.id && !strcmp(st.id, "settings") && task_in_group(d->tasks[i], "machine"));
                    }
                    if (here) {
                        add(t, EK_TASK, d->tasks[i].label, i);
                        if (t->n > 0 && t->e[t->n - 1].kind == EK_TASK && t->e[t->n - 1].index == i) {
                            t->e[t->n - 1].dialog = true;                    // T216 D11
                        }
                    }
                }
                if (page_route) {
                    if (heads) {                                                // T212 D12: the page's own section, T216 D12: after the task rows
                        add(t, EK_HEADING, page_route->heading, 0);           // T212 D12: the page's own section, T216 D12: after the task rows
                    }
                    add(t, EK_OPENS, page_route->label, opens_index);
                }
                break;
            }
            case TW_TASKS:
                for (int i = 0; i < d->ntasks; i++) {
                    if (!task_in_any_settings_tab(sk, d->tasks[i].group) && group_in_tab(sk, TW_TASKS, st, d->tasks[i].group)) {
                        add(t, EK_TASK, d->tasks[i].label, i);
                    }
                }
                break;
            case TW_SYSTEM:
                // T187 D8: System actions, Network, Support; what the route contributes to the tab; Reset to host defaults is the last row. The first row that takes the focus is
                // Restart core (a heading is never selected), not the reset.
                add(t, EK_HEADING, "Actions", 0);                      // the tab and the title say System already (gate 1)
                add(t, EK_RESTART_CORE, reload_core ? "Reload core" : "Restart core", reload_core ? 1 : 0);
                if (return_host) {
                    add(t, EK_RETURN_HOST, "Return to host menu", 0);
                }
                for (int k = 0; mm && k < mm->nnodes; k++) {                 // gate fix 2: a node anchored to <area>/<tab>/actions stands under Actions, after the generic rows
                    if (node_in(mm->nodes[k], "actions", 7)) {
                        add_node(mm->nodes[k]);
                    }
                }
                add(t, EK_HEADING, "Network", 0);
                add(t, EK_WIFI, "WiFi", 0);
                add_page_rows();                                             // T228 R6: the core's network settings, streams and hosts, one page each
                add(t, EK_HEADING, "Support", 0);
                add(t, EK_BUG_REPORT, "Bug report", 0);
                add(t, EK_KEYS_HELP, "Keys and help", 0);                 // T228 D12
                if (!nodes_first) {
                    add_nodes(1);
                    nodes_done = true;
                }
                add(t, EK_HEADING, "Reset", 0);
                add(t, EK_RESET_HOST, "Reset to host defaults", 0);
                // T212 D7/D3: what Home's Status box showed and still has a place: the build, the ROMs and the libraries (Core, Network and Media are elsewhere already)
                add(t, EK_HEADING, "Status", 0);
                add(t, EK_INFO, "Build", 1);
                add(t, EK_INFO, "ROMs", 4);
                add(t, EK_INFO, "Libraries", 5);
                if (!nodes_first) {                                      // with `nodes_first` every node of the tab stands on top already
                    add_nodes(2);
                }
                break;
            default:
                break;
            }
            // what libraries, applications and the description put into this tab, in the merge's order
            if (!nodes_first && !nodes_done) {
                add_nodes();
            }
            tab_end(t, a, tb);
            if (drive_page_wanted && tb->count > 0) {                    // T212 D5: the drive list: a page of this area, its rows are made by MenuCtl when a drive row is opened
                int parent = a->ntabs - 1;
                MenuTab *pg = tab_begin(t, a, "Drive");
                if (pg) {
                    pg->id = "drive";
                    pg->n = 0;
                    pg->page = true;
                    pg->drive_page = true;
                    pg->parent = (int8_t)parent;
                    for (int i = 0; i < DRIVE_PAGE_ROWS; i++) {
                        add(t, EK_TASK, "", 0);
                    }
                    tab_end(t, a, pg);
                    pg->count = 0;                                       // the rows are reserved, not listed: MenuCtl::open_drive sets the count
                }
            }
            if (npg > 0 && tb->count > 0) {                              // T228 D18, R6: the pages: a tab with no number, back to this tab
                int parent = a->ntabs - 1;
                for (int k = 0; k < npg; k++) {
                    MenuTab *pg = tab_begin(t, a, pg_name[k]);
                    if (!pg) {
                        break;
                    }
                    pg->id = pg_name[k];
                    pg->n = 0;
                    pg->page = true;
                    pg->parent = (int8_t)parent;
                    const int p = pg_of[k];
                    const bool several = memchr(pages.groups[p], '+', pages.groups_l[p]) != 0;
                    // each group of the page: its heading (a page of several groups; not drawn when it repeats the page's title), its settings, the nodes anchored to it
                    each_word(pages.groups[p], pages.groups_l[p], '+', [&](const char *g, size_t gl) {
                        if (!group_has_rows(g, gl)) {
                            return;
                        }
                        const char *h = several ? menu_heading_text(t, g, gl) : 0;
                        if (h && strcasecmp(h, pg_name[k])) {
                            add(t, EK_HEADING, h, 0);
                        } else if (h) {
                            t->nheads--;
                        }
                        for (int i = 0; i < d->nsettings; i++) {
                            if (setting_in(i, g, gl)) {
                                add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                            }
                        }
                        for (int n = 0; mm && n < mm->nnodes; n++) {
                            if (node_in(mm->nodes[n], g, gl)) {
                                add_node(mm->nodes[n]);
                            }
                        }
                    });
                    tab_end(t, a, pg);
                }
            }
            if (page_route && tb->count > 0) {                           // the page the row opens: a tab with no number and no place on the strip, back to this tab
                int parent = a->ntabs - 1;
                MenuTab *pg = tab_begin(t, a, page_route->label);
                if (pg) {
                    pg->id = page_route->group;
                    pg->n = 0;
                    pg->page = true;
                    pg->parent = (int8_t)parent;
                    for (int i = 0; i < d->nsettings; i++) {
                        const MenuGroupRoute *r = menu_route_of(d->settings[i]);
                        if (r && r == page_route && route_tab(sk, r) == &st) {
                            add(t, EK_SETTING, d->settings[i].label ? d->settings[i].label : d->settings[i].key, i);
                        }
                    }
                    tab_end(t, a, pg);
                }
            }
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
    { EK_WIFI, 0, "wifi.toggle", 0, "wifi.onoff", "No module", 0, "wifi.line", 0, 0, false },
    { EK_SAVE_SET, 0, "config.save", 0, 0, 0, 0, 0, 0, 0, false },
    { EK_LOAD_SET, 0, "config.load", 0, "config.count", "none", " saved", 0, 0, 0, false },
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

void menu_build_for(const CoreCtx *c, MenuTree *t, MenuVisFn vis, void *vis_user)
{
    menu_build(c->desc, t, !c->board || c->board->can_return_host, reload_is_dfx(c), c->merge, vis, vis_user);
}

void menu_drop_entry(MenuTree *t, int area, int tab, int i)
{
    MenuTab &tb = t->a[area].tabs[tab];
    const int at = tb.first + i;
    if (i < 0 || i >= tb.count || at >= t->n) {
        return;
    }
    memmove(&t->e[at], &t->e[at + 1], (size_t)(t->n - at - 1) * sizeof(t->e[0]));
    t->n--;
    tb.count--;
    for (int a = 0; a < t->nareas; a++) {                               // the tabs behind it (and a page's reserved rows) move up with their entries
        for (int b = 0; b < t->a[a].ntabs; b++) {
            if (t->a[a].tabs[b].first > at) {
                t->a[a].tabs[b].first--;
            }
        }
    }
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
    case EK_HEADING: return "heading";
    case EK_OPENS: return "opens";
    case EK_KEYS_HELP: return "fixed";
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
            EMIT("  tab %u %s%s\n", (unsigned)tb.n, tb.name, tb.page ? " (a page)" : "");
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
                    if (s.show_state) {
                        EMIT(" [listed while %s]", s.show_state);          // T216 S3: a row that depends on a state is told in the dump
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
