// T93 S11, S3: a new core costs one descriptor - the checker accepts the real ones and the made-up one and names what is missing;
// the menu, the ROM manager and the settings work for the made-up core with no change to a shared source file; the menu dumps
// equal the golden files (the whitelist: nothing else is in it).
#include <strings.h>
#include <string>
#include <fstream>
#include <sstream>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "desc_check.h"
#include "picture.h"
#include "menutree.h"
#include "home.h"
#include "keyboard.h"
#include "boot.h"
#include "ops_glue.h"
#include "engines.h"
#include "mediamounts.h"
#include "record_menu.h"

const CoreDesc *const core_table[] = { &testcore::desc, 0 };    // the shared unit: the made-up core alone (T239.3 D61: no real core is left in the shared app)

static std::string slurp(const std::string &p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }


static void golden(const CoreDesc *d0, const char *name)
{
    const CoreDesc *d = d0;
    MenuTree t;
    menu_build(d, &t, true, false, 0);
    static char buf[262144];                             // a tree is a few hundred entries (the largest held 368; 32 KiB no longer held it)
    int n = menu_dump(d, &t, buf, sizeof(buf));
    CHECK(n > 0);
    std::string path = std::string(GOLDEN_DIR) + "/menu_" + name + ".txt";
    if (getenv("UPDATE_GOLDEN")) { std::ofstream(path) << buf; }
    std::string want = slurp(path);
    CHECK(!want.empty());
    if (want != buf) {
        printf("golden %s differs:\n--- want\n%s--- got\n%s\n", name, want.c_str(), buf);
    }
    CHECK(want == buf);
}

// Every item the descriptor declares reaches exactly one tab, and no tab holds an entry that is not
// declared (T104 5.8: the route is a regrouping of T93's four pages, never a filter). This is the check
// that would catch an area or a tab quietly swallowing a setting, which a golden file alone would only
// show as a diff somebody has to read.
// T212 D8-D11: the place of a task in the built tree, "F1/home" style, or "" when no tab lists it (a drive's list is no tab: see slot_lists)
static std::string task_place(const CoreDesc *d, const char *label)
{
    MenuTree t;
    menu_build(d, &t, true, false, 0);
    for (int a = 0; a < t.nareas; a++) {
        for (int b = 0; b < t.a[a].ntabs; b++) {
            const MenuTab &tb = t.a[a].tabs[b];
            for (int i = 0; !tb.drive_page && i < tb.count; i++) {
                const MenuEntry &e = t.e[tb.first + i];
                if (e.kind == EK_TASK && !strcmp(d->tasks[e.index].label, label)) {
                    return std::string(t.a[a].key) + "/" + (tb.id ? tb.id : "") + "#" + std::to_string(i);
                }
            }
        }
    }
    return "";
}
static bool slot_lists(const CoreDesc *d, const char *slot, bool mounted, const char *label)
{
    for (int i = 0; i < d->nmedia; i++) {
        if (!strcmp(d->media[i].name, slot)) {
            return groups_have(mounted ? d->media[i].mounted_tasks : d->media[i].empty_tasks, label);
        }
    }
    return false;
}
static const Task *find_task(const CoreDesc *d, const char *label)
{
    for (int i = 0; i < d->ntasks; i++) {
        if (!strcmp(d->tasks[i].label, label)) return &d->tasks[i];
    }
    return 0;
}

static void every_item_once(const CoreDesc *d0)
{
    const CoreDesc *d = d0;
    MenuTree t;
    menu_build(d, &t, true, false, 0);
    int seen_task = 0, seen_media = 0, seen_set = 0, seen_info = 0, seen_browser = 0, seen_fixed = 0, seen_mset = 0;
    int in_tabs = 0;
    for (int a = 0; a < t.nareas; a++) {
        for (int b = 0; b < t.a[a].ntabs; b++) {
            const MenuTab &tb = t.a[a].tabs[b];
            CHECK(tb.count > 0 || tb.drive_page);      // an empty tab is never published (the drive list is filled when a drive row opens it)
            CHECK(tb.page || tb.n == b + 1);                      // the printed number is the tab's place, and stable
            in_tabs += tb.drive_page ? DRIVE_PAGE_ROWS : tb.count;      // T212 D5: the drive list's reserved rows
            for (int i = tb.first; i < tb.first + tb.count; i++) {
                switch (t.e[i].kind) {
                case EK_TASK: seen_task++; break;
                case EK_MEDIA: seen_media++; break;
                case EK_SETTING: seen_set++; break;
                case EK_INFO: seen_info++; break;
                case EK_BROWSER: seen_browser++; break;
                case EK_RESET_HOST: case EK_RESTART_CORE: case EK_RETURN_HOST: case EK_BUG_REPORT: case EK_WIFI: case EK_KEYS_HELP: seen_fixed++; break;
                case EK_SAVE_SET: case EK_LOAD_SET: seen_mset++; break;
                case EK_HEADING: case EK_OPENS: case EK_NODE: break;
                }
            }
        }
    }
    CHECK_EQ(in_tabs, t.n);                            // no entry is left out of every tab
    int claimed = 0;                                   // T212 D5: a task that a drive's list reaches is not a row of any tab
    for (int i = 0; i < d->ntasks; i++) {
        claimed += menu_task_claimed(d, i) ? 1 : 0;
    }
    CHECK_EQ(seen_task + claimed, d->ntasks);
    CHECK_EQ(seen_media, d->nmedia);
    int hidden = 0;                                    // T228 D14: a setting of group "hidden" keeps its key and its store value and has no row
    for (int i = 0; i < d->nsettings; i++) {
        hidden += d->settings[i].group && !strcmp(d->settings[i].group, "hidden") ? 1 : 0;
    }
    CHECK_EQ(seen_set + hidden, d->nsettings);
    CHECK(seen_browser >= 1);
    CHECK_EQ(seen_fixed, 6);                           // Reset to host defaults, Restart core, Bug report, Keys and help (T228 D12), WiFi, Return to host menu
    CHECK_EQ(seen_mset, mounts_have_slots(d) ? 2 : 0);   // T180 D5: the media tab of a core with media slots has the two config rows, no other tab does
    CHECK_EQ(seen_info, 3);                            // T212 D7: Build, ROMs, Libraries stand in F8 (Core, Network and Media are dropped)
}

// T180 D2, D5: the slots of a core's own media engine are keys of the .cfg
static const char *const ms_names[2] = { "Disk", "Cart" };
static const char *ms_spec(CoreCtx *, int) { return 0; }
static int ms_mount(CoreCtx *, int, const char *, char *, int) { return 0; }
static int ms_eject(CoreCtx *, int, char *, int) { return 0; }
static const MountSlots ms_ok = { 2, ms_names, ms_spec, ms_mount, ms_eject };

int main(int, char **argv)
{
    for (const CoreDesc *const *p = core_table; *p; p++) {
        const char *why = desc_check(*p);
        if (why) printf("%s: %s\n", (*p)->cor_name, why);
        CHECK(!why);
        CHECK(!desc_check(*p));
        every_item_once(*p);
    }
    CHECK(!desc_check_table(core_table));

    // T212 D13: the machine line of Home names settings the core has; the cores that have a model/version or a memory setting get one, the others none
    {
        struct Mi { const char *cor; const char *info; };
        static const Mi want[1] = { { "", "" } };                          // the shared table (the made-up core) has no machine line: nothing to find (the count below is skipped)
        static const char *const none[1] = { 0 };
        const int n_none = 0;
        int found = 0, nones = 0;
        for (const CoreDesc *const *p = core_table; *p; p++) {
            const CoreDesc *d = *p;
            for (const Mi &w : want) {
                if (strcmp(d->cor_name, w.cor)) continue;
                found++;
                CHECK(d->machine_info && !strcmp(d->machine_info, w.info));
                for (const char *q = d->machine_info; d->machine_info && *q;) {          // every name is a choice setting of the core
                    const char *e = strchr(q, ',');
                    int l = e ? (int)(e - q) : (int)strlen(q), hit = 0;
                    for (int i = 0; i < d->nsettings; i++) {
                        const char *nm = d->settings[i].label ? d->settings[i].label : d->settings[i].key;
                        hit += !d->settings[i].kind && (int)strlen(nm) == l && !strncasecmp(nm, q, l);
                    }
                    if (hit != 1) printf("T212 D13: %s: machine_info names '%.*s' (%d settings)\n", d->cor_name, l, q, hit);
                    CHECK_EQ(hit, 1);
                    if (!e) break;
                    q = e + 1;
                }
            }
            for (int k = 0; k < n_none; k++) if (!strcmp(d->cor_name, none[k])) { nones++; CHECK(!d->machine_info); }
        }
        if (want[0].cor[0]) CHECK_EQ(found, (int)(sizeof(want) / sizeof(want[0])));   // the placeholder row ("") has nothing to find: the shared table's cores and the apps without a machine line
        CHECK_EQ(nones, n_none);
    }

    // T206 S1, S2: wherever a disk image is mounted the same "Run Disk" is offered beside "Mount Disk", with the same names in the same order in every core.
    // The CBM drive images (d64, g64, d81) have one reference table each; "Run Disk" needs the core to be able to type at READY (type_op). The machines with
    // other disks offer their own mount-and-start (Atari, Apple, CPC, ZX Next: "... and cold start / and reset"; the Amiga's ADF: "Mount DF0 and
    // reset"). No core is exempt (T206 D1): a core that mounts a disk and has no such action fails here.
    {
        static const char *const ref_d64[] = { "Mount Disk", "Mount Disk Read Only", "Mount Disk Unlinked", "Run Disk", "Browse Disk" };
        static const char *const ref_g64[] = { "Mount Disk", "Mount Disk Read Only", "Mount Disk Unlinked", "Run Disk" };
        static const char *const ref_d81[] = { "Mount Disk", "Mount Disk Read Only", "Run Disk", "Browse Disk" };
        struct Ref { const char *ext; const char *const *names; int n; };
        static const Ref refs[] = { { ".d64", ref_d64, 5 }, { ".g64", ref_g64, 4 }, { ".d81", ref_d81, 4 } };
        int seen_cbm = 0, seen_other = 0;
        for (const CoreDesc *const *p = core_table; *p; p++) {
            const CoreDesc *d = *p;
            for (int i = 0; i < d->nfiletypes; i++) {
                const FileType &ft = d->filetypes[i];
                bool mounts = false, runs = false, starts = false;
                for (int k = 0; k < ft.nactions; k++) {
                    const FileAction &a = ft.actions[k];
                    mounts = mounts || !strncmp(a.label, "Mount", 5);
                    runs = runs || (a.op && !strcmp(a.op, "drive.mount_run"));
                    starts = starts || strstr(a.label, "and cold start") || strstr(a.label, "and reset");
                }
                if (!mounts || !strcmp(ft.ext, ".hdf") || *p == &testcore::desc) continue;     // a hard disk image has nothing to run; the made-up core is no machine
                const Ref *ref = 0;
                bool cbm_ref = true;                                    // the CBM drive images have a reference table
                for (const Ref &r : refs) if (cbm_ref && !strcmp(ft.ext, r.ext)) ref = &r;
                if (ref) {                                          // a CBM drive image
                    seen_cbm++;
                    // the reference names first, in this order; a core may add its own after them
                    bool same = ft.nactions >= ref->n;
                    for (int k = 0; same && k < ref->n; k++) same = !strcasecmp(ft.actions[k].label, ref->names[k]);
                    if (!same || !runs || !d->type_op) printf("T206: %s %s: names/order differ, or no Run Disk, or the core cannot type\n", d->cor_name, ft.ext);
                    CHECK(same);
                    CHECK(runs);
                    CHECK(d->type_op != 0);
                } else {
                    seen_other++;
                    if (!runs && !starts) printf("T206: %s %s: Mount without a run\n", d->cor_name, ft.ext);
                    CHECK(runs || starts);
                }
            }
        }
        printf("T206: walked %d CBM image types, %d others\n", seen_cbm, seen_other);
        CHECK(seen_cbm == 0 && seen_other == 0);                    // the walk saw the made-up core alone: no image type, measured 2026-11-27 (T239.5 D13, T239.3 D61: every real core's types are its app's)
    }

    // T184 D3: the WiFi is set in the host. A core keeps no WiFi key of its own (no "WiFi ...", no "Wi-Fi SSID" / "Wi-Fi Password": the host's settings are the store's)
    // and shows exactly one row WiFi: the fixed row of the system tab (EK_WIFI) of a classic core, the node of a record's menu; whatever else the record has of
    // the WiFi is a state line (no button, no operation but the switch).
    for (const CoreDesc *const *p = core_table; *p; p++) {
        const CoreDesc *d = *p;
        for (int i = 0; i < d->nsettings; i++) {
            const char *k = d->settings[i].key;
            if (!strncasecmp(k, "WiFi", 4) || !strncasecmp(k, "Wi-Fi", 5)) printf("%s: setting %s\n", (*p)->cor_name, k);
            CHECK(strncasecmp(k, "WiFi", 4) && strncasecmp(k, "Wi-Fi", 5));
        }
        MenuTree t;
        menu_build(d, &t, true, false, 0);
        int rows = 0;
        for (int i = 0; i < t.n; i++) {
            const MenuEntry &e = t.e[i];
            if (e.kind == EK_WIFI || (e.kind == EK_NODE && !strcmp(e.label, "WiFi"))) {
                rows++;
                CHECK(e.kind == EK_WIFI || (t.nodes[e.index].op && !strcmp(t.nodes[e.index].op, "wifi.toggle")));
            }
            if (e.kind == EK_NODE && t.nodes[e.index].op && !strncmp(t.nodes[e.index].op, "wifi.", 5)) {
                CHECK(!strcmp(t.nodes[e.index].op, "wifi.toggle"));
            }
        }
        if (rows != 1) printf("%s: %d WiFi rows\n", (*p)->cor_name, rows);
        CHECK_EQ(rows, 1);
    }

    // T205 D1 (S1, S2, S3): every core on the default route has ONE settings area. F5 is named after the core and its tabs are the core's own settings (named after the
    // core), Video, Audio, Input; there is no F6 (nor a key for it); "Menu opens at" sits in the core's tab; F8 holds only actions (Reload/Restart core, Return to host menu,
    // Bug report, WiFi, Reset to host defaults) and no setting; the Summary tab is gone, and the media slots and the configs it showed are on Home. A description with a route
    // of its own keeps its areas; it is not on the default route.
    {
        int n205 = 0;
        for (const CoreDesc *const *p = core_table; *p; p++) {
            const MenuMerge *mm = 0;
            const CoreDesc *d = *p;
            MenuTree t;
            menu_build(d, &t, true, false, mm);
            n205++;
            int f5 = -1, f8 = -1, f1 = -1;
            for (int a = 0; a < t.nareas; a++) {
                if (!strcmp(t.a[a].key, "F5")) f5 = a;
                if (!strcmp(t.a[a].key, "F8")) f8 = a;
                if (!strcmp(t.a[a].key, "F1")) f1 = a;
                CHECK(strcmp(t.a[a].key, "F6"));                                       // S2: F6 is gone from the rail ...
            }
            CHECK(menu_area_of_key(&t, KEY_F6) == -1);                              // ... and from the keys
            if (f5 < 0 || f8 < 0 || f1 < 0) printf("%s: F1 %d F5 %d F8 %d (%d settings)\n", (*p)->cor_name, f1, f5, f8, d->nsettings);
            CHECK(f8 >= 0 && f1 >= 0 && (f5 >= 0 || d->nsettings == 0));         // a core with no setting has no F5 (an area with no tab is dropped)
            if (f5 < 0 || f8 < 0 || f1 < 0) continue;
            // S1: the tabs of F5, in this order; the first is named like the area (the core)
            const MenuArea &a5 = t.a[f5];
            CHECK(a5.ntabs >= 1 && a5.tabs[0].id && !strcmp(a5.tabs[0].id, "settings") && !strcmp(a5.tabs[0].name, a5.name));
            static const char *const want[] = { "Video", "Audio", "Input" };
            int w = 0;
            for (int i = 1; i < a5.ntabs; i++) {
                if (a5.tabs[i].page) continue;
                while (w < 3 && strcmp(a5.tabs[i].name, want[w])) w++;
                CHECK(w < 3);                                                          // only Video, Audio, Input follow, in this order
                w++;
            }
            bool menu_opens = false, summary = false;
            for (int i = 0; i < a5.ntabs; i++) {
                summary |= !strcasecmp(a5.tabs[i].name, "Summary");
                for (int k = a5.tabs[i].first; k < a5.tabs[i].first + a5.tabs[i].count; k++) {
                    const MenuEntry &e = t.e[k];
                    CHECK(e.kind != EK_MEDIA && e.kind != EK_SAVE_SET && e.kind != EK_LOAD_SET);   // S3: what Summary showed is not in F5
                    if (e.kind == EK_SETTING && !strcasecmp(d->settings[e.index].key, REOPEN_KEY)) {
                        menu_opens = i == 0;                                           // S2: "Menu opens at" is in the core's tab
                    }
                }
            }
            CHECK(!summary);
            bool has_reopen = false;
            for (int i = 0; i < d->nsettings; i++) has_reopen |= !strcasecmp(d->settings[i].key, REOPEN_KEY);
            CHECK(!has_reopen || menu_opens);
            // S2: F8 is actions: no setting, no media
            int act = 0, wifi = 0, bug = 0, ret = 0, rst = 0, keys = 0;
            const MenuArea &a8 = t.a[f8];
            for (int i = 0; i < a8.ntabs; i++) {
                for (int k = a8.tabs[i].first; k < a8.tabs[i].first + a8.tabs[i].count; k++) {
                    const MenuEntry &e = t.e[k];
                    CHECK(e.kind != EK_SETTING && e.kind != EK_MEDIA);
                    act += e.kind == EK_RESTART_CORE;
                    ret += e.kind == EK_RETURN_HOST;
                    wifi += e.kind == EK_WIFI;
                    bug += e.kind == EK_BUG_REPORT;
                    rst += e.kind == EK_RESET_HOST;
                    keys += e.kind == EK_KEYS_HELP;
                }
            }
            CHECK(act == 1 && ret == 1 && wifi == 1 && bug == 1 && rst == 1 && keys == 1);
            // T212 D7: Home has no information lines: the media slots of the core, then the configs when the core has slots
            const MenuTab &h = t.a[f1].tabs[0];
            int nmedia = 0, ninfo = 0, saves = 0;
            bool info_first = true;
            for (int k = h.first; k < h.first + h.count; k++) {
                const MenuEntry &e = t.e[k];
                if (e.kind == EK_INFO) { ninfo++; info_first &= nmedia == 0 && saves == 0; }
                nmedia += e.kind == EK_MEDIA;
                saves += e.kind == EK_SAVE_SET || e.kind == EK_LOAD_SET;
            }
            CHECK(ninfo == 0 && info_first && nmedia == d->nmedia);
        }
        CHECK(n205 == 1);                                          // T239.5 D13, D61: the shared table is the made-up core alone; each real core's row is its app's unit, measured 2026-11-27
        printf("T205: %d cores walked\n", n205);
    }

    // T180 D4: a core that keeps media of its own (an engine the base does not know: the Apple's and the Atari's drives, the CPC's FDC, the drive library of the
    // Commodore cores, the SNES and Neo Geo saves) hands its write-back to the base, which calls it on every exit that lets a medium go (media_writeback)
    {
        static const char *const own[] = { 0 };                              // the shared table (the made-up core) has no flush of its own
        for (const char *id : own) {
            if (!id) continue;
            const CoreDesc *d = 0;
            for (const CoreDesc *const *p = core_table; *p; p++) {
                if (!memcmp((*p)->id, id, 4)) d = *p;
            }
            if (!d || !d->media_flush || !d->media_pending) printf("%s: no media_flush / media_pending\n", id);
            CHECK(d && d->media_flush && d->media_pending);
        }
    }

    // T107 5.9: what the menu shows for a reset-time setting does not say "at next reset" itself: the second line of the row says it, from `when`
    for (const CoreDesc *const *p = core_table; *p; p++) {
        for (int i = 0; i < (*p)->nsettings; i++) {
            const Setting &s = (*p)->settings[i];
            const char *shown = s.label ? s.label : s.key;
            if (s.when == WHEN_RESET && (strstr(shown, "reset") || strstr(shown, "reload"))) {
                printf("%s: \"%s\" says by itself what the row's second line says\n", (*p)->cor_name, shown);
            }
            CHECK(!(s.when == WHEN_RESET && (strstr(shown, "reset") || strstr(shown, "reload"))));
        }
    }

    // each mandatory field, missing, is named
    CoreDesc d = testcore::desc;
    d.cor_name = ""; CHECK(desc_check(&d) && strstr(desc_check(&d), "cor_name"));
    d = testcore::desc; d.init = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "init"));
    d = testcore::desc; d.id[0] = ' '; CHECK(desc_check(&d) && strstr(desc_check(&d), "id"));
    d = testcore::desc; d.video.scale = 4; CHECK(desc_check(&d) && strstr(desc_check(&d), "scale"));
    d = testcore::desc; d.video.x0 = 1500; CHECK(desc_check(&d) && strstr(desc_check(&d), "fit"));
    d = testcore::desc; d.freeze.mask = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "freeze"));
    static const Setting bad_def[1] = { { "X", testcore::av, 2, 5, WHEN_LIVE, "test.set_a" } };
    d = testcore::desc; d.settings = bad_def; d.nsettings = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "default"));
    static const RomSpec bad_rom[1] = { { "R", { "R.ROM", 0 }, { 0 }, true, 0, "test.put" } };
    d = testcore::desc; d.roms = bad_rom; CHECK(desc_check(&d) && strstr(desc_check(&d), "roms"));
    // T185: more ROMs than the scan's result arrays take (boot.cc, run.cc, libboard.cc) is refused here, on the Mac, not met as an overrun on the board
    static const RomSpec many_rom[ROMS_MAX + 1] = { };
    d = testcore::desc; d.roms = many_rom; d.nroms = ROMS_MAX + 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "ROMS_MAX"));
    static const FileType bad_ft[1] = { { "tst", testcore::acts, 1 } };
    d = testcore::desc; d.filetypes = bad_ft; CHECK(desc_check(&d) && strstr(desc_check(&d), "dot"));
    d = testcore::desc; d.needs_program = "load"; d.program_ext = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "needs_program"));
    {   // T180: a table of slots needs its three operations, 1..8 slots that fit the .cfg, and names that make keys
        CoreDesc m = testcore::desc;
        m.mount_slots = &ms_ok;
        CHECK(!desc_check(&m));
        MountSlots t = ms_ok;
        t.eject = 0; m.mount_slots = &t; CHECK(desc_check(&m) && strstr(desc_check(&m), "mount_slots"));
        static const char *const nine[9] = { "A", "B", "C", "D", "E", "F", "G", "H", "I" };
        t = ms_ok; t.n = MOUNT_SLOTS_MAX + 1; t.names = nine; m.mount_slots = &t; CHECK(desc_check(&m) && strstr(desc_check(&m), "mount_slots"));
        static const char *const dup[2] = { "Disk", "disk" };
        t = ms_ok; t.names = dup; m.mount_slots = &t; CHECK(desc_check(&m) && strstr(desc_check(&m), "one name"));
        static const char *const eq[2] = { "A=B", "C" };
        t = ms_ok; t.names = eq; m.mount_slots = &t; CHECK(desc_check(&m) && strstr(desc_check(&m), "key cannot hold"));
        static const char *const lng[2] = { "TwelveChars!", "C" };
        t = ms_ok; t.names = lng; m.mount_slots = &t; CHECK(desc_check(&m) && strstr(desc_check(&m), "1..9"));
    }
    // T104.1 D14: every name the descriptor holds has to be registered, and what it binds has to fit; each refusal names the entry and
    // the operation. A setting still needs two values.
    static const Task ghost_task[1] = { { "Ghost", "test.ghost", 0, 0, false } };
    d = testcore::desc; d.tasks = ghost_task; CHECK(desc_check(&d) && strstr(desc_check(&d), "Ghost") && strstr(desc_check(&d), "test.ghost") && strstr(desc_check(&d), "registered"));
    static const Setting ghost_set[1] = { { "G", testcore::av, 2, 0, WHEN_LIVE, "test.nothing" } };
    d = testcore::desc; d.settings = ghost_set; d.nsettings = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "test.nothing"));
    static const RomSpec ghost_rom[1] = { { "R", { "R.ROM", 0 }, { 4096, 0 }, true, 0, "test.noput" } };
    d = testcore::desc; d.roms = ghost_rom; CHECK(desc_check(&d) && strstr(desc_check(&d), "test.noput"));
    static const FileAction ghost_act[1] = { { "G", "test.nomount", testcore::mount_bind, 1, false } };
    static const FileType ghost_ft[1] = { { ".tst", ghost_act, 1 } };
    d = testcore::desc; d.filetypes = ghost_ft; CHECK(desc_check(&d) && strstr(desc_check(&d), "test.nomount"));
    static const MediaSlot ghost_slot[1] = { { "S", "test.noslot", 0, 0, 0 } };
    d = testcore::desc; d.media = ghost_slot; CHECK(desc_check(&d) && strstr(desc_check(&d), "test.noslot"));
    // T92 D-17b: the labels a slot mounts and replaces by name file actions of its own type (a typo would drop to the action sheet unnoticed)
    static const MediaSlot slot_ok[1] = { { "Slot", "test.slot", 0, 0, 0, 0, ".tst", "Mount", 0, 0, false, "Mount" } };
    d = testcore::desc; d.media = slot_ok; CHECK(!desc_check(&d));
    static const MediaSlot slot_bad_mount[1] = { { "Slot", "test.slot", 0, 0, 0, 0, ".tst", "Mnt", 0, 0, false, 0 } };
    d = testcore::desc; d.media = slot_bad_mount; CHECK(desc_check(&d) && strstr(desc_check(&d), "Mnt") && strstr(desc_check(&d), "no file action"));
    static const MediaSlot slot_bad_replace[1] = { { "Slot", "test.slot", 0, 0, 0, 0, ".tst", "Mount", 0, 0, false, "Swap" } };
    d = testcore::desc; d.media = slot_bad_replace; CHECK(desc_check(&d) && strstr(desc_check(&d), "Swap"));
    static const MediaSlot slot_other_type[1] = { { "Slot", "test.slot", 0, 0, 0, 0, ".xyz", "Mount", 0, 0, false, 0 } };
    d = testcore::desc; d.media = slot_other_type; CHECK(desc_check(&d) && strstr(desc_check(&d), "Mount"));
    d = testcore::desc; d.register_ops = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "test."));      // nobody registers what it names
    d = testcore::desc; d.service_op = "test.nosvc"; d.service_period_ms = 5; CHECK(desc_check(&d) && strstr(desc_check(&d), "test.nosvc"));
    d = testcore::desc; d.service_op = "test.mount"; d.service_period_ms = 5; CHECK(desc_check(&d) && strstr(desc_check(&d), "without arguments"));
    // bound arguments against their types
    static const OpBind wrong_arg[1] = { B_FILE("pfad") };
    static const FileAction a1[1] = { { "A", "test.mount", wrong_arg, 1, false } };
    static const FileType f1[1] = { { ".tst", a1, 1 } };
    d = testcore::desc; d.filetypes = f1; CHECK(desc_check(&d) && strstr(desc_check(&d), "pfad"));
    static const OpBind num_for_path[1] = { B_INT("path", 1) };
    static const FileAction a2[1] = { { "A", "test.mount", num_for_path, 1, false } };
    static const FileType f2[1] = { { ".tst", a2, 1 } };
    d = testcore::desc; d.filetypes = f2; CHECK(desc_check(&d) && strstr(desc_check(&d), "not a number"));
    static const FileAction a3[1] = { { "A", "test.mount", 0, 0, false } };
    static const FileType f3[1] = { { ".tst", a3, 1 } };
    d = testcore::desc; d.filetypes = f3; CHECK(desc_check(&d) && strstr(desc_check(&d), "unbound"));
    static const Task file_in_task[1] = { { "T", "test.mount", testcore::mount_bind, 1, false } };
    d = testcore::desc; d.tasks = file_in_task; CHECK(desc_check(&d) && strstr(desc_check(&d), "no file"));
    // one value is no setting
    static const Setting one_value[1] = { { "One", testcore::av, 1, 0, WHEN_LIVE, "test.set_a" } };
    d = testcore::desc; d.settings = one_value; d.nsettings = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "two values"));
    // a ROM put that does not take the bytes
    static const RomSpec path_put[1] = { { "R", { "R.ROM", 0 }, { 4096, 0 }, true, 0, "test.mount" } };
    d = testcore::desc; d.roms = path_put; CHECK(desc_check(&d) && strstr(desc_check(&d), "blob"));
    // one owner per name: a core that registers a name of the base is refused, naming both
    struct Thief { static void reg(OpRegistry &r) { r.contributor("thief"); r.add_op("machine.cold_reset", 0, 0, testcore::op_task); } };
    d = testcore::desc; d.register_ops = Thief::reg; CHECK(desc_check(&d) && strstr(desc_check(&d), "base") && strstr(desc_check(&d), "thief"));

    // T104.1 Q10: the capability list. The real descriptors name theirs; a library asks for one by name.
    CHECK(!desc_has_capability(&testcore::desc, "sio") && !desc_has_capability(&testcore::desc, ""));         // a core that claims none has none
    static const char *const good_caps[] = { "drive_a", "iec", "sio2", "a23456789012345" };
    d = testcore::desc; d.caps = good_caps; d.ncaps = 4; CHECK(!desc_check(&d));
    static const char *const up_caps[] = { "Drive_A" };
    d = testcore::desc; d.caps = up_caps; d.ncaps = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "Drive_A") && strstr(desc_check(&d), "capabilities"));
    static const char *const sp_caps[] = { "drive a" };
    d = testcore::desc; d.caps = sp_caps; d.ncaps = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "drive a"));
    static const char *const empty_caps[] = { "" };
    d = testcore::desc; d.caps = empty_caps; d.ncaps = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "capabilities"));
    static const char *const long_caps[] = { "abcdefghijklmnop" };               // 16 characters: the field has room for 15 and a NUL
    d = testcore::desc; d.caps = long_caps; d.ncaps = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "abcdefghijklmnop"));
    static const char *const null_caps[] = { 0 };
    d = testcore::desc; d.caps = null_caps; d.ncaps = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "capabilities"));
    static const char *const dup_caps[] = { "sio", "iec", "sio" };
    d = testcore::desc; d.caps = dup_caps; d.ncaps = 3; CHECK(desc_check(&d) && strstr(desc_check(&d), "twice") && strstr(desc_check(&d), "sio"));
    d = testcore::desc; d.caps = 0; d.ncaps = 2; CHECK(desc_check(&d) && strstr(desc_check(&d), "count without a table"));
    d = testcore::desc; d.caps = good_caps; d.ncaps = -1; CHECK(desc_check(&d) && strstr(desc_check(&d), "count without a table"));

    // two cores with one ID
    CoreDesc twin = testcore::desc;
    twin.cor_name = "Another Machine";
    const CoreDesc *const dup[] = { &testcore::desc, &twin, 0 };
    CHECK(desc_check_table(dup) != 0);              // the same ID block
    CoreDesc twin2 = testcore::desc;
    twin2.id[3] = 'X';
    const CoreDesc *const dup2[] = { &testcore::desc, &twin2, 0 };
    CHECK(desc_check_table(dup2) != 0);             // the same manifest name

    // T104.2 D5: a descriptor with w == 0 draws its own picture; the rest of the video block then has to be empty, not half filled
    d = testcore::desc; d.video.w = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "own picture"));      // the made-up core's buffers are still there
    d = testcore::desc; d.video.h = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "size"));              // a picture needs both sides
    // T104.2 D6: an action that asks for an address binds it once, to a number that takes 0 .. 65535; one that asks nothing binds no address
    static const ArgSpec ask_arg[] = { ARG_PATH("path"), ARG_INT("start", 0, 0xFFFF) };
    static const ArgSpec small_arg[] = { ARG_PATH("path"), ARG_INT("start", 0, 255) };
    struct Ask {
        static int op(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
        static void reg(OpRegistry &r) { r.contributor("test"); r.add_op("test.put", testcore::blob_arg, 1, testcore::op_put); r.add_op("test.set_a", testcore::a_arg, 1, testcore::op_a); r.add_op("test.set_b", testcore::b_arg, 1, testcore::op_b); r.add_op("test.task", 0, 0, testcore::op_task); r.add_op("test.ask", ask_arg, 2, op); r.add_op("test.small", small_arg, 2, op); r.add_state("test.slot", testcore::what); }
    };
    static const OpBind ask_bind[] = { B_FILE("path"), B_ASK("start") };
    static const OpBind ask_twice[] = { B_FILE("path"), B_ASK("start"), B_ASK("start") };
    static const OpBind ask_fixed[] = { B_FILE("path"), B_INT("start", 3) };
    static const FileAction ok_ask[1] = { { "Go", "test.ask", ask_bind, 2, false, "Start address", 0 } };
    static const FileType ft_ok[1] = { { ".tst", ok_ask, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_ok; CHECK(!desc_check(&d));
    static const FileAction no_prompt[1] = { { "Go", "test.ask", ask_bind, 2, false, 0, 0 } };
    static const FileType ft_np[1] = { { ".tst", no_prompt, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_np; CHECK(desc_check(&d) && strstr(desc_check(&d), "asks for none"));
    static const FileAction unbound_prompt[1] = { { "Go", "test.ask", ask_fixed, 2, false, "Start address", 0 } };
    static const FileType ft_ub[1] = { { ".tst", unbound_prompt, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_ub; CHECK(desc_check(&d) && strstr(desc_check(&d), "binds it 0 times"));
    static const FileAction twice_prompt[1] = { { "Go", "test.ask", ask_twice, 3, false, "Start address", 0 } };
    static const FileType ft_tw[1] = { { ".tst", twice_prompt, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_tw; CHECK(desc_check(&d) && strstr(desc_check(&d), "twice"));
    static const FileAction small_prompt[1] = { { "Go", "test.small", ask_bind, 2, false, "Start address", 0 } };
    static const FileType ft_sm[1] = { { ".tst", small_prompt, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_sm; CHECK(desc_check(&d) && strstr(desc_check(&d), "0 .. 65535"));
    static const OpBind ask_task[] = { B_ASK("start") };
    static const Task t_ask[1] = { { "T", "test.task", ask_task, 1, false } };
    d = testcore::desc; d.tasks = t_ask; CHECK(desc_check(&d));            // the operation has no such argument: refused for that, not for asking

    // T104.1 D15 scope: a task that asks for a name binds it once, to a string argument; its default is a state that exists; a file type may
    // hold actions for the files inside it (a disk image), with an extension for them; run_op is an operation of one path
    static const ArgSpec name_arg[] = { ARG_STRING("name", 24), ARG_INT("n", 0, 9) };
    struct Name {
        static void reg(OpRegistry &r) { Ask::reg(r); r.add_op("test.name", name_arg, 2, Ask::op); }
    };
    static const OpBind nm_ok[] = { B_TEXT("name"), B_INT("n", 1) };
    static const OpBind nm_twice[] = { B_TEXT("name"), B_TEXT("name") };
    static const OpBind nm_int[] = { B_TEXT("n"), B_STR("name", "x") };
    static const OpBind nm_none[] = { B_STR("name", "x"), B_INT("n", 1) };
    static const Task t_nm_ok[1] = { { "Name", "test.name", nm_ok, 2, false, "Name?", "test.slot" } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_ok; CHECK(!desc_check(&d));
    static const Task t_nm_noask[1] = { { "Name", "test.name", nm_ok, 2, false, 0, 0 } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_noask; CHECK(desc_check(&d) && strstr(desc_check(&d), "asks for none"));
    static const Task t_nm_unbound[1] = { { "Name", "test.name", nm_none, 2, false, "Name?", 0 } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_unbound; CHECK(desc_check(&d) && strstr(desc_check(&d), "binds it 0 times"));
    static const Task t_nm_twice[1] = { { "Name", "test.name", nm_twice, 2, false, "Name?", 0 } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_twice; CHECK(desc_check(&d));
    static const Task t_nm_int[1] = { { "Name", "test.name", nm_int, 2, false, "Name?", 0 } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_int; CHECK(desc_check(&d) && strstr(desc_check(&d), "not a string"));
    static const Task t_nm_state[1] = { { "Name", "test.name", nm_ok, 2, false, "Name?", "test.nowhere" } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_state; CHECK(desc_check(&d) && strstr(desc_check(&d), "takes its default from state"));
    static const Task t_nm_dflt[1] = { { "Name", "test.name", nm_none, 2, false, 0, "test.slot" } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_dflt; CHECK(desc_check(&d) && strstr(desc_check(&d), "default for a name it does not ask"));
    // T122 §5: a folder is picked for a name that is asked
    static const Task t_nm_pick[1] = { { "Name", "test.name", nm_ok, 2, false, "Name?", 0, true } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_pick; CHECK(!desc_check(&d));
    static const Task t_nm_pick_noask[1] = { { "Reset", "machine.warm_reset", 0, 0, false, 0, 0, true } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Name::reg; d.tasks = t_nm_pick_noask; CHECK(desc_check(&d) && strstr(desc_check(&d), "picks a folder for a name it does not ask"));
    // the files inside an image
    static const OpBind in_bind[] = { B_FILE("path"), B_INT("start", 0) };
    static const FileAction in_ok[1] = { { "Inner", "test.ask", in_bind, 2, false, 0, 0 } };
    static const FileAction in_ghost[1] = { { "Inner", "test.ghost", in_bind, 2, false, 0, 0 } };
    static const FileAction outer[1] = { { "Go", "test.ask", ask_bind, 2, false, "Start address", 0 } };
    static const FileType ft_in_ok[1] = { { ".img", outer, 1, ".prg", in_ok, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_in_ok; CHECK(!desc_check(&d));
    static const FileType ft_in_noext[1] = { { ".img", outer, 1, 0, in_ok, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_in_noext; CHECK(desc_check(&d) && strstr(desc_check(&d), "no extension"));
    static const FileType ft_in_ghost[1] = { { ".img", outer, 1, ".prg", in_ghost, 1 } };
    d = testcore::desc; d.register_ops = Ask::reg; d.filetypes = ft_in_ghost; CHECK(desc_check(&d) && strstr(desc_check(&d), "test.ghost"));
    // run_op
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Ask::reg; d.run_op = "test.small"; CHECK(desc_check(&d) && strstr(desc_check(&d), "one path argument"));
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = Ask::reg; d.run_op = "test.nowhere"; CHECK(desc_check(&d) && strstr(desc_check(&d), "not registered"));
    static const ArgSpec path_only[] = { ARG_PATH("path") };
    struct RunOp { static void reg(OpRegistry &r) { Ask::reg(r); r.add_op("test.run", path_only, 1, Ask::op); } };
    d = testcore::desc; d.filetypes = ft_ok; d.register_ops = RunOp::reg; d.run_op = "test.run"; CHECK(!desc_check(&d));



    // T104.2 D10: tasks, media and settings name a group, a tab lists groups, an information tab names its lines - two tabs of one widget keep their entries apart
    {
        static const MediaSlot media2[2] = { { "M1", "test.slot", 0, 0, 0, "g1" }, { "M2", "test.slot", 0, 0, 0, "g2" } };
        static const Task tasks2[3] = { { "T1", "test.task", 0, 0, false, 0, 0, false, 0, "g1" }, { "T2", "test.task", 0, 0, false, 0, 0, false, 0, "g2" },
                                        { "T3", "test.task" } };
        static const char *const vals[2] = { "a", "b" };
        static const Setting sets2[2] = { { "S1", vals, 2, 0, WHEN_LIVE, "test.set_a", 0, 0, "g1" }, { "S2", vals, 2, 0, WHEN_LIVE, "test.set_a", 0, 0, "g2" } };
        static const SkelTab tabs[] = { { "m1", "M1", 1, TW_MEDIA, "g1" }, { "m2", "M2", 2, TW_MEDIA, "g2" }, { "t1", "T1", 3, TW_TASKS, "g1" },
                                        { "t2", "T2", 4, TW_TASKS, "g2" }, { "t0", "T0", 5, TW_TASKS, 0 }, { "s1", "S1", 6, TW_SETTINGS, "g1" },
                                        { "i", "I", 1, TW_INFO, "Media,Core" } };
        static const SkelArea areas[] = { { "F5", "X", false, tabs, 6 }, { "F1", "Y", false, tabs + 6, 1 } };
        static const Skeleton sk = { areas, 2 };
        static MenuMerge mm;
        memset(&mm, 0, sizeof(mm));
        mm.skel = &sk;
        CoreDesc g = testcore::desc;
        g.media = media2; g.nmedia = 2; g.tasks = tasks2; g.ntasks = 3; g.settings = sets2; g.nsettings = 2;
        MenuTree t;
        menu_build(&g, &t, true, false, &mm);
        static char gb[4096];
        menu_dump(&g, &t, gb, sizeof(gb));
        std::string dump = gb;
        CHECK(dump.find("tab 1 M1\n    media M1\n  tab 2 M2\n    media M2\n") != std::string::npos);
        CHECK(dump.find("tab 3 T1\n    task T1\n  tab 4 T2\n    task T2\n  tab 5 T0\n    task T3\n") != std::string::npos);   // T3 names no group: where no group is listed
        CHECK(dump.find("tab 6 S1\n    setting S1 = ") != std::string::npos && dump.find("setting S2") == std::string::npos);   // S2's group is listed by no settings tab: it lands where the ungrouped go, and there is none
        CHECK(dump.find("area F1 Y\n  tab 1 I\n    info Core\n    info Media\n") == std::string::npos);      // the lines come in the order the tab names them ...
        CHECK(dump.find("area F1 Y\n  tab 1 I\n    info Media\n    info Core\n") != std::string::npos);      // ... and no others
    }

    // golden menu lists
    golden(&testcore::desc, "test");

    // the whitelist as a property: every entry is one of the descriptor's own or a fixed one; nothing of the C64 machine anywhere
    static const char *const forbidden[] = { "Drive A", "SID", "REU", "Monitor", "Printer", "Modem", "LED", "Power", "Stream",
                                             "Assembly", ".u2a", "Flash Config", "Save to Flash", "Interface Type", "Tape", "Turbo", 0 };
    for (const CoreDesc *const *p0 = core_table; *p0; p0++) {
        const CoreDesc *d1 = *p0;
        const CoreDesc *const *p = &d1;
        MenuTree t;
        menu_build(*p, &t, true, false, 0);
        for (int i = 0; i < t.n; i++) {
            const MenuEntry &e = t.e[i];
            bool own = false;
            switch (e.kind) {
            case EK_TASK: own = e.label == (*p)->tasks[e.index].label || !e.label[0]; break;   // T212 D5: an empty label = a row reserved for the drive list (its tab lists none)
            case EK_MEDIA: own = e.label == (*p)->media[e.index].name; break;
            case EK_SETTING: own = e.label == ((*p)->settings[e.index].label ? (*p)->settings[e.index].label : (*p)->settings[e.index].key); break;
            case EK_INFO: own = e.label == menu_info_labels[e.index] || !strcmp(e.label, "Build") || !strcmp(e.label, "ROMs") || !strcmp(e.label, "Libraries"); break;
            case EK_BROWSER: own = !strcmp(e.label, "Browse"); break;
            case EK_RESET_HOST: own = !strcmp(e.label, "Reset to host defaults"); break;
            case EK_RESTART_CORE: own = !strcmp(e.label, "Restart core"); break;
            case EK_RETURN_HOST: own = !strcmp(e.label, "Return to host menu"); break;
            case EK_BUG_REPORT: own = !strcmp(e.label, "Bug report"); break;
            case EK_WIFI: own = !strcmp(e.label, "WiFi"); break;
            case EK_KEYS_HELP: own = !strcmp(e.label, "Keys and help"); break;
            case EK_SAVE_SET: own = !strcmp(e.label, "Save as config..."); break;
            case EK_LOAD_SET: own = !strcmp(e.label, "Load config..."); break;
            case EK_HEADING: own = !strcmp(e.label, "Machine") || !strcmp(e.label, "Storage") || !strcmp(e.label, "Actions") || !strcmp(e.label, "Network") ||
                                   !strcmp(e.label, "Support") || !strcmp(e.label, "Reset") || !strcmp(e.label, "Status"); break;
            case EK_DRIVE_MOUNT: case EK_DRIVE_EJECT: own = true; break;      // T212 D5: the drive list's own rows
            case EK_OPENS: own = !strcmp(e.label, "Hotkeys") || !strcmp(e.label, "Storage") || !strcmp(e.label, "Menu") || !strcmp(e.label, "Keys") || !strcmp(e.label, "FTP hosts"); break;
            case EK_NODE: own = false; break;
            }
            if (!own) printf("not own: kind %d label [%s] idx %d core %s i %d\n", (int)e.kind, e.label, e.index, (*p)->id, i);
            CHECK(own);
            for (int f = 0; forbidden[f]; f++) {
                if (!strcmp(forbidden[f], "Tape") && desc_has_capability(*p, "tape")) continue;   // a Spectrum or a CPC has a tape (T91, T92); the C64's datasette is not theirs
                if (!strcmp(forbidden[f], "Drive A") && desc_has_capability(*p, "drive_a")) continue;   // the cores with the capability carry the cbm1541 library's drive A (T104.1 D15)
                if (!strcmp(forbidden[f], "LED") && !strcmp(e.label, "LED Load Indicator")) continue;   // T159: the case LED 1 load indicator, the one LED item a core may carry
                if (strstr(e.label, forbidden[f])) { printf("forbidden word %s in %s\n", forbidden[f], e.label); CHECK(false); }
            }
        }
        char buf[8192];
        menu_dump(*p, &t, buf, sizeof(buf));
        for (int f = 0; forbidden[f]; f++) {
            if (!strcmp(forbidden[f], "Tape") && desc_has_capability(*p, "tape")) continue;
            if (!strcmp(forbidden[f], "Drive A") && desc_has_capability(*p, "drive_a")) continue;
            if (!strcmp(forbidden[f], "LED")) {   // T159: the LED Load Indicator line is the one place the word may stand
                std::string dump = buf;
                for (size_t k; (k = dump.find("LED Load Indicator")) != std::string::npos;) dump.erase(k, 18);
                if (dump.find("LED") != std::string::npos) { printf("forbidden word LED in the dump of %s\n", (*p)->cor_name); CHECK(false); }
                continue;
            }
            if (strstr(buf, forbidden[f])) { printf("forbidden word %s in the dump of %s\n", forbidden[f], (*p)->cor_name); CHECK(false); }
        }
    }
    // S11: the made-up core boots through the shared code, ROMs from a stick, settings and all
    FakeIo io; FakeFs fs; PxRec px;
    fs.put_size("/usb0/roms/test/T.BIN", 8192);
    CoreCtx c; memset(&c, 0, sizeof(c));
    c.desc = &testcore::desc; c.io = &io; c.fs = &fs;
    Settings st(testcore::desc.settings, 2, testcore::desc.store, &fs, "/usb0/cores/test.cfg", 0);
    st.load();
    c.settings = &st;
    Video v(testcore::desc.video, &px, &io);
    c.video = &v;
    struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
    CHECK_EQ(boot_start(&c, &ui), BOOT_RUNNING);
    CHECK_EQ(testcore::g_loaded, 1);
    CHECK_EQ(testcore::g_hold, 1);
    CHECK_EQ(testcore::g_release, 1);
    CHECK_EQ(testcore::g_set_a, 1);
    CHECK_EQ(testcore::g_set_b, 1);
    CHECK(st.set(1, 2));
    CHECK(fs.text("/usb0/cores/test.cfg").find("Speed=Fast") != std::string::npos);
    // S1, the menu's way in: a descriptor entry's call goes through the registry by name, with the file bound; a call that does not fill an
    // argument is refused before the handler runs
    CoreCtx mc; memset(&mc, 0, sizeof(mc));
    mc.desc = &testcore::desc;
    OpResult res;
    CHECK_EQ(ops_run_bound(&mc, "test.mount", testcore::mount_bind, 1, "/usb0/x.tst", &res), OPE_OK);
    CHECK_EQ(testcore::g_mounted, 1);
    CHECK_EQ(ops_run_bound(&mc, "test.mount", 0, 0, "/usb0/x.tst", &res), OPE_ARG);
    CHECK_EQ(ops_run_bound(&mc, "test.nothing", 0, 0, 0, &res), OPE_NOT_FOUND);
    CHECK_EQ(testcore::g_mounted, 1);
    CHECK(ops_state(&mc, "test.slot") == 0);
    // T212 D10: the tape keys of the Spectrum's, the CPC's and the PET's decks are the units zx_desc, cpc_desc and pet_desc
    DONE("test_desc");
}
