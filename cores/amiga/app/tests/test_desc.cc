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
#include "amiga_regs.h"
#include "record_menu.h"
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)

const CoreDesc *const core_table[] = { &amiga_desc, 0 };

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
        static const Mi want[] = { { "Amiga (OCS/ECS, 68000)", "Chipset,Chip RAM,Slow RAM,Fast RAM" } };
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
        if (want[0].cor[0]) CHECK_EQ(found, (int)(sizeof(want) / sizeof(want[0])));   // the placeholder row ("") has nothing to find: the shared table's cores and the apps without a machine line (DDT, the Next, diag)
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
        CHECK(seen_cbm == 0 && seen_other == 1);                    // the walk saw the A500's ADF (Mount DF0 and reset), no CBM image (the HDF has nothing to run)
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
        CHECK(n205 == 1);
        printf("T205: %d cores walked\n", n205);
    }

    // T180 D4: a core that keeps media of its own (an engine the base does not know: the Apple's and the Atari's drives, the CPC's FDC, the drive library of the
    // Commodore cores, the SNES and Neo Geo saves) hands its write-back to the base, which calls it on every exit that lets a medium go (media_writeback)
    {
        static const char *const own[] = { 0 };                              // the A500's media are the base engines' (window and sector media): no flush of its own to hand over
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


    // T104 5.2: the records the Amiga needed are checked like every other part of the descriptor, and a refusal names the field
    {
        CoreDesc d;                                                            // (the shared region that declares d is not in this app)
        CHECK(!desc_check(&amiga_desc));
        {   // T180: the Amiga's own slots are DF0.. and HD0..: an engine's name may not look like one (the slot table ms_ok of the shared case above)
            MountSlots t;
            CoreDesc a = amiga_desc;
            static const char *const df[1] = { "DF9" };
            t = ms_ok; t.n = 1; t.names = df; a.mount_slots = &t; CHECK(desc_check(&a) && strstr(desc_check(&a), "like a slot of the base"));
            static const char *const many[7] = { "A", "B", "C", "D", "E", "F", "G" };
            t = ms_ok; t.n = 7; t.names = many; a.mount_slots = &t; CHECK(desc_check(&a) && strstr(desc_check(&a), "more media than the .cfg"));
        }
        // D25: the Amiga's own mouse rows pass only with their own operation; a base key with another operation is refused
        {
            int sens = -1, acc = -1;
            for (int i = 0; i < amiga_desc.nsettings; i++) {
                if (!strcmp(amiga_desc.settings[i].key, "Mouse Sensitivity")) sens = i;
                if (!strcmp(amiga_desc.settings[i].key, "Mouse Acceleration")) acc = i;
            }
            CHECK(sens >= 0 && acc >= 0);
            static Setting mix[1];
            mix[0] = amiga_desc.settings[acc];
            mix[0].key = "Mouse Sensitivity";                       // Sensitivity's key on Acceleration's operation
            d = amiga_desc; d.settings = mix; d.nsettings = 1; d.romset = 0;
            CHECK(desc_check(&d) && strstr(desc_check(&d), "belong to the application"));
            mix[0] = amiga_desc.settings[sens];
            mix[0].key = "Joystick Input";                    // a look key on the mouse operation
            d = amiga_desc; d.settings = mix; d.nsettings = 1; d.romset = 0;
            CHECK(desc_check(&d) && strstr(desc_check(&d), "belong to the application"));
        }
        // a setting is exactly one of: an operation, register bits, the rom_set's choice
        static Setting both[1];
        both[0] = amiga_desc.settings[1];
        both[0].apply = "dummy800.set_ram";
        d = amiga_desc; d.settings = both; d.nsettings = 1; d.romset = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "exactly one of"));
        static Setting neither[1];
        neither[0] = amiga_desc.settings[1];
        neither[0].bind = 0;
        d = amiga_desc; d.settings = neither; d.nsettings = 1; d.romset = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "exactly one of"));
        // register bits: a register outside the block, bits that leave the byte, a value that does not fit, two settings on the same bits
        static RegBind far_reg = { 0x1234, 0, 3, 0 }, wide = { AM_MEM_CFG, 7, 3, 0 }, tight = { AM_MEM_CFG, 0, 1, 0 };
        static Setting bad_bind[1];
        bad_bind[0] = amiga_desc.settings[1];
        bad_bind[0].bind = &far_reg;
        d = amiga_desc; d.settings = bad_bind; d.nsettings = 1; d.romset = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "outside the register block"));
        bad_bind[0].bind = &wide;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "not in one byte"));
        bad_bind[0].bind = &tight;                                                  // four values in a one bit mask
        CHECK(desc_check(&d) && strstr(desc_check(&d), "does not fit its bits"));
        static RegBind same = { AM_MEM_CFG, 1, 3, 0 };                              // bits 2:1 against chip's 1:0
        static Setting clash[2];
        clash[0] = amiga_desc.settings[1];
        clash[1] = amiga_desc.settings[2];
        clash[1].bind = &same;
        d = amiga_desc; d.settings = clash; d.nsettings = 2; d.romset = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "own the same bits"));
        // the rom_set: its choice is one setting with max + 1 values, and the record is complete
        RomSetSpec rs = *amiga_desc.romset;
        d = amiga_desc; d.romset = &rs;
        rs.choice_key = "Nothing";
        CHECK(desc_check(&d) && strstr(desc_check(&d), "not one setting"));
        rs = *amiga_desc.romset;
        rs.max = 4;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "max + 1"));
        rs = *amiga_desc.romset;
        rs.sizes[0] = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "romset"));
        rs = *amiga_desc.romset;
        rs.mirror_stride = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "mirror"));
        // the media records
        WindowMediaSpec w = *amiga_desc.window_media;
        d = amiga_desc; d.window_media = &w;
        w.nslots = 5;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "window_media"));
        w = *amiga_desc.window_media;
        w.stride = 1000;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "stride"));
        w = *amiga_desc.window_media;
        w.dlog_pop = 0x2000;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "register block"));
        w = *amiga_desc.window_media;
        w.base = 0x7F0000;                                                          // into the Kickstart
        CHECK(desc_check(&d) && strstr(desc_check(&d), "overlaps"));
        SectorMediaSpec sm = *amiga_desc.sector_media;
        d = amiga_desc; d.sector_media = &sm;
        sm.stage_off = AM_W_ADF + 0x100;                                            // into a floppy slot
        CHECK(desc_check(&d) && strstr(desc_check(&d), "overlaps") && strstr(desc_check(&d), "the sector staging"));
        sm = *amiga_desc.sector_media;
        sm.nunits = 3;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "sector_media"));
        sm = *amiga_desc.sector_media;
        sm.stage_off = 0x01000000u;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "outside the DDR2 window"));
        MouseSinkSpec ms = *amiga_desc.mouse;
        d = amiga_desc; d.mouse = &ms;
        ms.step = 200;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "mouse"));
        ms = *amiga_desc.mouse;
        ms.reg_btn = 0x1000;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "register block"));
        // T104.1 5.6: a core that names the 1351 uses no register; a backend nobody knows is refused
        {
            MouseSinkSpec pot = {};
            pot.kind = MOUSE_POT1351;
            CoreDesc dp = testcore::desc;
            dp.mouse = &pot;
            CHECK(!desc_check(&dp));
        }
        ms = *amiga_desc.mouse;
        ms.kind = (MouseKind)7;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "backend"));
        static const RegWrite far_write[1] = { { 0x2000, 1 } };
        d = amiga_desc; d.reset_writes = far_write; d.nreset_writes = 1;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "reset_writes"));
        // the video: both frame buffer registers or neither (a lone one would write the ID bytes)
        d = amiga_desc; d.video.reg_fb_base = 0xC4;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "both given or both 0"));
        d = amiga_desc; d.video.nbuf = 5;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "four buffers"));
        d = amiga_desc; d.video.reg_latest = 0;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "FB_LATEST"));
        // the shared ID word: only the records that say so may share it (a twin of the record stands in for the second Amiga; the A1200 has its own app since T213.3)
        CoreDesc twin = amiga_desc;
        twin.cor_name = "Amiga twin";
        twin.id_shared = false;
        const CoreDesc *const unflagged[] = { &amiga_desc, &twin, 0 };
        CHECK(desc_check_table(unflagged) && strstr(desc_check_table(unflagged), "share an ID"));
        twin.id_shared = true;
        const CoreDesc *const flagged[] = { &amiga_desc, &twin, 0 };
        CHECK(!desc_check_table(flagged));
        CoreDesc same_name = twin;
        same_name.cor_name = amiga_desc.cor_name;
        const CoreDesc *const dup[] = { &amiga_desc, &same_name, 0 };
        CHECK(desc_check_table(dup) && strstr(desc_check_table(dup), "manifest name"));
    }

    // golden menu lists
    golden(&amiga_desc, "amiga");

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
    // T212 D10: the tape keys of the Spectrum's, the CPC's and the PET's decks are the units zx_desc, cpc_desc and pet_desc
    DONE("test_desc");
}
