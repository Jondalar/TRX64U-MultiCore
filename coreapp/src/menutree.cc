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

const char *const menu_info_labels[] = { "Core", "Application", "Network", "Media", "ROMs", "Libraries", 0 };

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
            if (o.widget == widget && groups_have(o.groups, g)) {
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
    { "video",   "F5", "video", "Video",   0,         false, "F6" },
    { "audio",   "F5", "audio", "Audio",   0,         false, "F6" },
    { "hotkeys", "F7", "settings", "Hotkeys", "Hotkeys", true, 0 },        // T212 D1/D6: the app's settings stand on F7 (APP), not in the machine's F5
    { "storage", "F7", "settings", "Storage", 0,      false, 0 },
    { "menu",    "F7", "settings", "Menu",    0,      false, 0 },
};

const MenuGroupRoute *menu_route_of(const Setting &s)
{
    const char *g = 0;
    if (!s.apply) {
        return 0;
    }
    if (!strcmp(s.apply, PICTURE_OP) || !strcmp(s.apply, SCALE_OP) || !strcmp(s.apply, SMOOTH_OP) || !strcmp(s.apply, FOLLOW_OP)) {
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
    for (int pass = 0; pass < 2; pass++) {                           // the route's area first, then (a description with a Settings area of its own) the alternative one, by the tab id
        const char *key = pass ? r->alt_area : r->area;
        for (int a = 0; key && a < sk->nareas; a++) {
            if (strcmp(sk->areas[a].key, key)) {
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
                if (!pass && tb.id && !strcmp(tb.id, "settings")) {
                    fb = &tb;
                }
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

void menu_build(const CoreDesc *d, MenuTree *t, bool return_host, bool reload_core, const MenuMerge *mm)
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
                    if (heads) {
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
                add(t, EK_HEADING, "Network", 0);
                add(t, EK_WIFI, "WiFi", 0);
                add(t, EK_HEADING, "Support", 0);
                add(t, EK_BUG_REPORT, "Bug report", 0);
                if (!nodes_first) {
                    add_nodes();
                    nodes_done = true;
                }
                add(t, EK_HEADING, "Reset", 0);
                add(t, EK_RESET_HOST, "Reset to host defaults", 0);
                // T212 D7/D3: what Home's Status box showed and still has a place: the build, the ROMs and the libraries (Core, Network and Media are elsewhere already)
                add(t, EK_HEADING, "Status", 0);
                add(t, EK_INFO, "Build", 1);
                add(t, EK_INFO, "ROMs", 4);
                add(t, EK_INFO, "Libraries", 5);
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
    case EK_HEADING: return "heading";
    case EK_OPENS: return "opens";
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
