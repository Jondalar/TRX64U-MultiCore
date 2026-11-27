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
#if defined(TEST_APP_A1200) || defined(TEST_APP_AMIGA)          // the two Amiga apps (run.sh a12app with its shim, amigaapp); T239.5 D13: the shared unit has no Amiga
#include "amiga_regs.h"
#endif
#include "record_menu.h"
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)
#endif
#ifdef TEST_APP_ATARI800
extern const CoreDesc atari800_desc;            // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
#endif
#ifdef TEST_APP_APPLE2
extern const CoreDesc apple2_desc;              // cores.h lists the shared app's cores; the Apple IIe is its own app (cores/apple2/app/lib)
#endif
#ifdef TEST_APP_NES
extern const CoreDesc nes_desc;                 // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#endif
#ifdef TEST_APP_C264
extern const CoreDesc c264_desc;                // the Commodore 264 is its own app (cores/c264/app/lib): no shared header declares it
#endif
#ifdef TEST_APP_C128
extern const CoreDesc c128_desc;                // cores.h lists the shared app's cores; the C128 is its own app (cores/c128/app/lib)
#endif
#ifdef TEST_APP_PET
extern const CoreDesc pet_desc;                 // cores.h lists the shared app's cores; the PET is its own app (cores/pet/app/lib)
#endif
#ifdef TEST_APP_CPC
extern const CoreDesc cpc_desc;                 // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)
#endif
#ifdef TEST_APP_ZX
extern const CoreDesc zx_desc;                  // cores.h lists the shared app's cores; the ZX Spectrum is its own app (cores/zx/app/lib)
#endif
#ifdef TEST_APP_DIAG
extern const CoreDesc portdiag_desc;           // cores.h lists the shared app's cores; the diag core is its own app (cores/diag/app/lib)
#endif
#ifdef TEST_APP_DDT
extern const CoreDesc ddt_novic_desc;           // cores.h lists the shared app's cores; DDT Look Ma, No VIC is its own app (cores/ddt_novic/app/lib)
#endif

#ifdef TEST_APP_VC20
const CoreDesc *const core_table[] = { &vc20_desc, 0 };
#elif defined(TEST_APP_A1200)           // the Amiga 1200's app is its own build (run.sh a12app) with its core only
const CoreDesc *const core_table[] = { &amiga1200_desc, 0 };
#elif defined(TEST_APP_DDT)             // DDT Look Ma, No VIC's app is its own build (run.sh ddtapp) with its core only
const CoreDesc *const core_table[] = { &ddt_novic_desc, 0 };
#elif defined(TEST_APP_ZX)              // the ZX Spectrum's app is its own build (run.sh zxapp) with its core only
const CoreDesc *const core_table[] = { &zx_desc, 0 };
#elif defined(TEST_APP_CPC)             // the CPC's app is its own build (run.sh cpcapp) with its core only
const CoreDesc *const core_table[] = { &cpc_desc, 0 };
#elif defined(TEST_APP_PET)             // the PET's app is its own build (run.sh petapp) with its core only
const CoreDesc *const core_table[] = { &pet_desc, 0 };
#elif defined(TEST_APP_C128)            // the C128's app is its own build (run.sh c128app) with its core only
const CoreDesc *const core_table[] = { &c128_desc, 0 };
#elif defined(TEST_APP_AMIGA)           // the Amiga 500's app is its own build (run.sh amigaapp) with its core only
const CoreDesc *const core_table[] = { &amiga_desc, 0 };
#elif defined(TEST_APP_ATARI800)        // the Atari's app is its own build (run.sh a8app) with its core only
const CoreDesc *const core_table[] = { &atari800_desc, 0 };
#elif defined(TEST_APP_NES)             // the NES's app is its own build (run.sh nesapp) with its core only
const CoreDesc *const core_table[] = { &nes_desc, 0 };
#elif defined(TEST_APP_C264)            // the Commodore 264's app is its own build (run.sh c264app) with its core only
const CoreDesc *const core_table[] = { &c264_desc, 0 };
#elif defined(TEST_APP_DIAG)            // the diag core's app is its own build (run.sh diagapp) with its core only
const CoreDesc *const core_table[] = { &portdiag_desc, 0 };
#elif defined(TEST_APP_APPLE2)          // the Apple IIe's app is its own build (run.sh apple2app) with its core only
const CoreDesc *const core_table[] = { &apple2_desc, 0 };
#elif defined(TEST_APP_SNES)
extern const CoreDesc snes_desc;         // cores.h lists the shared app's cores; the SNES is its own app (cores/snes/app/lib)
const CoreDesc *const core_table[] = { &snes_desc, 0 };
#else
const CoreDesc *const core_table[] = { &testcore::desc, 0 };    // the shared unit: the made-up core alone (T239.3 D61: no real core is left in the shared app)
#endif

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
#ifdef TEST_APP_VC20
        static const Mi want[] = { { "VIC-20", "Memory Expansion" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_A1200)
        static const Mi want[] = { { "Amiga 1200 (AGA, 68020)", "Chipset,Chip RAM,Slow RAM,Fast RAM" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_DDT)
        static const Mi want[1] = { { "", "" } };                          // DDT Look Ma, No VIC has no setting, so no machine line: nothing to find (the count below is skipped)
        static const char *const none[] = { "DDT Look Ma, No VIC" };
        const int n_none = 1;
#elif defined(TEST_APP_ZX)
        static const Mi want[] = { { "ZX Spectrum 48K / 128K", "Model" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_CPC)
        static const Mi want[] = { { "Amstrad CPC 464 / 664 / 6128", "Model" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_PET)
        static const Mi want[] = { { "Commodore PET", "Model,RAM" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_C128)
        static const Mi want[] = { { "Commodore 128", "40/80 DISPLAY" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_AMIGA)
        static const Mi want[] = { { "Amiga (OCS/ECS, 68000)", "Chipset,Chip RAM,Slow RAM,Fast RAM" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_ATARI800)
        static const Mi want[] = { { "Atari 800 XL", "RAM size,Video standard" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_NES)
        static const Mi want[] = { { "Nintendo Entertainment System", "Console type" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_C264)
        static const Mi want[] = { { "Commodore 264 (C16, Plus/4)", "Model,RAM" } };
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#elif defined(TEST_APP_DIAG)
        static const Mi want[1] = { { "", "" } };                          // the diag core has no setting, so no machine line: nothing to find (the count below is skipped)
        static const char *const none[] = { "Port Diagnostics" };
        const int n_none = 1;
#elif defined(TEST_APP_APPLE2)
        static const Mi want[1] = { { "", "" } };                          // the Apple IIe has no machine line (no model or memory setting): nothing to find (the count below is skipped)
        static const char *const none[] = { "Apple IIe" };
        const int n_none = 1;
#elif defined(TEST_APP_SNES)
        static const Mi want[1] = { { "", "" } };                          // the SNES has no machine line: nothing to find (the count below is skipped)
        static const char *const none[] = { "Super Nintendo" };
        const int n_none = 1;
#else
        static const Mi want[1] = { { "", "" } };                          // the shared table (the made-up core) has no machine line: nothing to find (the count below is skipped)
        static const char *const none[1] = { 0 };
        const int n_none = 0;
#endif
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
#ifdef TEST_APP_PET                                                 // the PET's Run Disk is its own operation (its app, run.sh petapp)
                    runs = runs || (a.op && !strcasecmp(a.label, "Run Disk") && !strcmp(a.op, "pet.mount_run"));
#endif
                    starts = starts || strstr(a.label, "and cold start") || strstr(a.label, "and reset");
                }
                if (!mounts || !strcmp(ft.ext, ".hdf") || *p == &testcore::desc) continue;     // a hard disk image has nothing to run; the made-up core is no machine
                const Ref *ref = 0;
                bool cbm_ref = true;                                    // the CBM drive images have a reference table
#ifdef TEST_APP_PET
                cbm_ref = strcmp(d->cor_name, "Commodore PET") != 0;    // T206: two drives of one unit (Mount in Drive 8 / 8.1, Run Disk = pet.mount_run), no reference table
#endif
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
#ifdef TEST_APP_VC20
        CHECK(seen_cbm >= 2 && seen_other == 0);                    // the walk saw the VIC-20's two image types (.d64, .g64)
#elif defined(TEST_APP_A1200)
        CHECK(seen_cbm == 0 && seen_other >= 1);                    // the walk saw the A1200's ADF (Mount DF0 and reset), no CBM image
#elif defined(TEST_APP_DDT)
        CHECK(seen_cbm == 0 && seen_other == 0);                    // DDT has no file type
#elif defined(TEST_APP_ZX)
        CHECK(seen_cbm == 0 && seen_other == 0);                    // the Spectrum has no disk image type: snapshots and tapes are no image the walk counts
#elif defined(TEST_APP_CPC)
        CHECK(seen_cbm == 0 && seen_other >= 1);                    // the walk saw the CPC's DSK (Mount A and reset), no CBM image
#elif defined(TEST_APP_PET)
        CHECK(seen_cbm == 0 && seen_other >= 1);                    // the PET's drive images (.d64, .d80, .d82: Mount in Drive 8 / 8.1, Run Disk = pet.mount_run) are no reference table's image
#elif defined(TEST_APP_ATARI800)
        CHECK(seen_cbm == 0 && seen_other >= 1);                    // the Atari's images (.atr, .xfd, .xex, the cartridge) are no reference table's image
#elif defined(TEST_APP_C128)
        CHECK(seen_cbm == 3 && seen_other == 0);                     // the walk saw the C128's three drive image types (.d64, .g64, .d81), measured 2026-11-25
#elif defined(TEST_APP_AMIGA)
        CHECK(seen_cbm == 0 && seen_other == 1);                    // the walk saw the A500's ADF (Mount DF0 and reset), no CBM image (the HDF has nothing to run)
#elif defined(TEST_APP_NES)
        CHECK(seen_cbm == 0 && seen_other == 0);                    // the NES has no disk image type: a cartridge (.nes) is no image the walk counts
#elif defined(TEST_APP_C264)
        CHECK(seen_cbm == 2 && seen_other == 0);                     // the walk saw the 264's two drive image types (.d64, .g64); its .prg, .p00, .bin and .rom mount nothing
#elif defined(TEST_APP_DIAG)
        CHECK(seen_cbm == 0 && seen_other == 0);                    // the diag core has no file type
#elif defined(TEST_APP_APPLE2)
        CHECK(seen_cbm == 0 && seen_other >= 1);                    // the Apple's images (.dsk, .do, .po, .nib: Mount D1 and cold start) are no reference table's image
#elif defined(TEST_APP_SNES)
        CHECK(seen_cbm == 0 && seen_other == 0);                    // the SNES's game is run, not mounted: no image the walk counts
#else
        CHECK(seen_cbm == 0 && seen_other == 0);                    // the walk saw the made-up core alone: no image type, measured 2026-11-27 (T239.5 D13, T239.3 D61: every real core's types are its app's)
#endif
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
#ifdef TEST_APP_VC20
        CHECK(n205 == 1);
#elif defined(TEST_APP_A1200)
        CHECK(n205 == 1);
#elif defined(TEST_APP_DDT)
        CHECK(n205 == 1);
#elif defined(TEST_APP_ZX)
        CHECK(n205 == 1);
#elif defined(TEST_APP_CPC)
        CHECK(n205 == 1);
#elif defined(TEST_APP_PET)
        CHECK(n205 == 1);
#elif defined(TEST_APP_C128)
        CHECK(n205 == 1);
#elif defined(TEST_APP_AMIGA)
        CHECK(n205 == 1);
#elif defined(TEST_APP_ATARI800)
        CHECK(n205 == 1);
#elif defined(TEST_APP_NES)
        CHECK(n205 == 1);
#elif defined(TEST_APP_C264)
        CHECK(n205 == 1);
#elif defined(TEST_APP_DIAG)
        CHECK(n205 == 1);
#elif defined(TEST_APP_APPLE2)
        CHECK(n205 == 1);
#elif defined(TEST_APP_SNES)
        CHECK(n205 == 1);
#else
        CHECK(n205 == 1);                                          // T239.5 D13, D61: the shared table is the made-up core alone; each real core's row is its app's unit, measured 2026-11-27
#endif
        printf("T205: %d cores walked\n", n205);
    }

    // T180 D4: a core that keeps media of its own (an engine the base does not know: the Apple's and the Atari's drives, the CPC's FDC, the drive library of the
    // Commodore cores, the SNES and Neo Geo saves) hands its write-back to the base, which calls it on every exit that lets a medium go (media_writeback)
    {
#ifdef TEST_APP_VC20
        static const char *const own[] = { "VC20" };
#elif defined(TEST_APP_A1200)
        static const char *const own[] = { 0 };                              // the A1200's media are the base engines' (window and sector media): no flush of its own to hand over
#elif defined(TEST_APP_DDT)
        static const char *const own[] = { 0 };                              // DDT has no medium
#elif defined(TEST_APP_ZX)
        static const char *const own[] = { 0 };                              // the tape deck has no flush of its own to hand over
#elif defined(TEST_APP_CPC)
        static const char *const own[] = { "CPC_" };                         // the FDC's dirty chunks are the core's own write-back
#elif defined(TEST_APP_PET)
        static const char *const own[] = { "PET1" };                         // the dual drive unit's dirty images are the core's own write-back
#elif defined(TEST_APP_C128)
        static const char *const own[] = { "C128" };                         // the drive library's write-back (the drive window is the core's own)
#elif defined(TEST_APP_AMIGA)
        static const char *const own[] = { 0 };                              // the A500's media are the base engines' (window and sector media): no flush of its own to hand over
#elif defined(TEST_APP_ATARI800)
        static const char *const own[] = { "A8XL" };                         // the SIO drives' write-back
#elif defined(TEST_APP_NES)
        static const char *const own[] = { 0 };                              // the cartridge is read, never written back: no flush of its own to hand over
#elif defined(TEST_APP_C264)
        static const char *const own[] = { "C264" };                         // the drive library's write-back (the drive window is the core's own block drive_a)
#elif defined(TEST_APP_DIAG)
        static const char *const own[] = { 0 };                              // the diag core has no medium
#elif defined(TEST_APP_APPLE2)
        static const char *const own[] = { "APL2" };                         // the floppies' and the slot 7 volumes' write-back
#elif defined(TEST_APP_SNES)
        static const char *const own[] = { "SNES" };                         // the battery RAM's write-back
#else
        static const char *const own[] = { 0 };                              // the shared table (the made-up core) has no flush of its own
#endif
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

#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_AMIGA) && !defined(TEST_APP_ATARI800) && !defined(TEST_APP_NES) && !defined(TEST_APP_C264) && !defined(TEST_APP_DIAG) && !defined(TEST_APP_APPLE2) && !defined(TEST_APP_SNES)          // stands on the shared app's cores; the VIC-20's, the A1200's, DDT's and the ZX Spectrum's app are their own builds (run.sh vc20app, a12app, ddtapp, zxapp) with their core only
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
#endif
#ifdef TEST_APP_VC20
    // the real core: the disk's actions and its prompts
    {
        const FileType *d64 = 0;
        for (int i = 0; i < vc20_desc.nfiletypes; i++) if (!strcmp(vc20_desc.filetypes[i].ext, ".d64")) d64 = &vc20_desc.filetypes[i];
        CHECK(d64 && d64->ninner == 2 && !strcmp(d64->inner_ext, ".prg"));
        CHECK(d64 && !strcmp(d64->inner[0].label, "Mount & Run") && !strcmp(d64->inner[1].label, "Real Run"));
        CHECK(d64 && !strcmp(d64->actions[d64->nactions - 1].label, "Browse Disk") && !strcmp(d64->actions[d64->nactions - 1].op, "menu.browse"));
        int asked = 0;
        for (int i = 0; i < vc20_desc.ntasks; i++) asked += vc20_desc.tasks[i].ask != 0;
        CHECK_EQ(asked, 4);                                                // Create D64 / G64, Save as D64 / G64
        CHECK_STR(vc20_desc.run_op, "vc20.run_prg");
    }
#elif defined(TEST_APP_DDT)
    // the real core: one task, Reset, and nothing to ask (the records that the shared app's cores have are tested in the shared unit)
    CHECK(ddt_novic_desc.ntasks == 1 && !ddt_novic_desc.tasks[0].ask && ddt_novic_desc.nsettings == 0);
#elif defined(TEST_APP_ZX)
    // the real core: Reset, Reload ROMs and the five tape keys + Eject, nothing to ask; the model, two joystick ports and the tape sound as settings
    CHECK(zx_desc.ntasks == 8 && zx_desc.nsettings == 4);
    for (int i = 0; i < zx_desc.ntasks; i++) CHECK(!zx_desc.tasks[i].ask);
    // T157 D1: the joystick ports are two settings of the core's own, each there once (the shared unit joyswap has the cores that keep the other form)
    for (const char *k : { "Joystick port 1", "Joystick port 2" }) {
        int n = 0;
        for (int i = 0; i < zx_desc.nsettings; i++) if (!strcmp(zx_desc.settings[i].key, k)) n++;
        CHECK_EQ(n, 1);
    }
    // T212 D10: the tape keys are in the mounted Tape list; Play closes the menu, the others do not
    for (const char *k : { "Tape: Play", "Tape: Stop", "Tape: Rewind", "Tape: Previous block", "Tape: Next block" }) {
        CHECK(slot_lists(&zx_desc, "Tape", true, k) && !slot_lists(&zx_desc, "Tape", false, k));
        CHECK_STR(task_place(&zx_desc, k).c_str(), "");
        const Task *t = find_task(&zx_desc, k);
        CHECK(t && t->closes_menu == !strcmp(k, "Tape: Play"));
    }
#elif defined(TEST_APP_CPC)
    // T212 D10: the tape keys are in the mounted Tape list; Play closes the menu, the others do not (the PET's are in the unit pet_desc)
    for (const char *k : { "Tape: Play", "Tape: Stop", "Tape: Rewind", "Tape: Previous block", "Tape: Next block" }) {
        CHECK(slot_lists(&cpc_desc, "Tape", true, k) && !slot_lists(&cpc_desc, "Tape", false, k));
        CHECK_STR(task_place(&cpc_desc, k).c_str(), "");
        const Task *t = find_task(&cpc_desc, k);
        CHECK(t && t->closes_menu == !strcmp(k, "Tape: Play"));
    }
#elif defined(TEST_APP_C128)
    // the C128 has no tape deck of its own (an external datasette sits on the C2N port, op c128.set_dset): no tape keys to walk
#elif defined(TEST_APP_C264)
    // the 264 has no tape deck of its own (an external datasette sits on the C2N port, the setting Datasette port): no tape keys to walk
#elif defined(TEST_APP_ATARI800)
    // the real core: what desc_check refuses on its records (a drive its operation does not take, a drive bound twice, a setting with more values than its operation takes),
    // and its capability list (T104.1 Q10: the real descriptors name theirs; a library asks for one by name)
    {
        CoreDesc d;
    static const OpBind atari_bad_drive[2] = { B_FILE("path"), B_INT("drive", 4) };
    static const FileAction a4[1] = { { "A", "atari800.mount", atari_bad_drive, 2, false } };
    static const FileType f4[1] = { { ".atr", a4, 1 } };
    d = atari800_desc; d.filetypes = f4; d.nfiletypes = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "binds 4") && strstr(desc_check(&d), "0 .. 3"));
    static const OpBind atari_twice[3] = { B_FILE("path"), B_INT("drive", 1), B_INT("drive", 2) };
    static const FileAction a5[1] = { { "A", "atari800.mount", atari_twice, 3, false } };
    static const FileType f5[1] = { { ".atr", a5, 1 } };
    d = atari800_desc; d.filetypes = f5; d.nfiletypes = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "twice"));
    // a setting whose operation takes fewer values than the item has
    static const Setting too_few[1] = { { "RAM", atari800_desc.settings[0].values, 7, 0, WHEN_RESET, "atari800.set_speed" } };
    d = atari800_desc; d.settings = too_few; d.nsettings = 1; CHECK(desc_check(&d) && strstr(desc_check(&d), "takes 0 .. 3"));
    CHECK(desc_has_capability(&atari800_desc, "sio") && desc_has_capability(&atari800_desc, "cartridge"));
    CHECK(!desc_has_capability(&atari800_desc, "drive_a") && !desc_has_capability(&atari800_desc, "iec"));    // no Commodore drive on an Atari
    CHECK(!desc_has_capability(&atari800_desc, "si") && !desc_has_capability(&atari800_desc, "sio2"));         // whole names, not prefixes
    }
#elif defined(TEST_APP_DIAG)
    // T175: a core with no machine and no picture (the diag core): only such a core may have no PAUSED bit, and its freeze waits for nothing
    CHECK(!desc_check(&portdiag_desc));
    { CoreDesc d = portdiag_desc; d.menu_first = false; CHECK(desc_check(&d) && strstr(desc_check(&d), "freeze")); }
    { CoreDesc d = portdiag_desc; CHECK(!desc_check(&d)); d.freeze.timeout_ms = 0; CHECK(desc_check(&d) && strstr(desc_check(&d), "freeze")); }
    CHECK(portdiag_desc.menu_first);
    CHECK_EQ((int)portdiag_desc.video.w, 0);
    CHECK_STR(portdiag_desc.core_id, "diag");
#elif defined(TEST_APP_NES)
    // T104.1 Q10: the capability list of the real descriptor
    CHECK(desc_has_capability(&nes_desc, "cartridge") && !desc_has_capability(&nes_desc, "sio"));
    // T228 §5 (picture fix): the application's Picture row is the one the picture code finds; desc_check refuses a record setting that uses the key or the op of Picture, Scale or Smoothing, so the application's row is the only match
    {
        static CoreDesc rbase = nes_desc;
        RecordMenu rm(&rbase, argv[1]);                                       // golden/desc_own.toml over the NES
        const CoreDesc *td = &rm.desc;
        int pi = picture_index(td);
        CHECK(pi >= 0 && !strcasecmp(td->settings[pi].key, PICTURE_KEY));
        int si = scale_index(td), mi = smooth_index(td);
        CHECK(si < 0 || !strcasecmp(td->settings[si].key, SCALE_KEY));
        CHECK(mi < 0 || !strcasecmp(td->settings[mi].key, SMOOTH_KEY));
        int dup = 0;
        for (int i = 0; i < td->nsettings; i++)
            for (int j = i + 1; j < td->nsettings; j++)
                if (!strcasecmp(td->settings[i].key, td->settings[j].key)) { dup++; printf("duplicate setting key %s\n", td->settings[i].key); }
        CHECK_EQ(dup, 0);
    }
    // the NES declares no drive and no ROM
    { MenuTree t; menu_build(&nes_desc, &t); char b[8192]; menu_dump(&nes_desc, &t, b, sizeof(b)); CHECK(!strstr(b, "D1:")); CHECK(!strstr(b, "Eject")); }
    // T212 D8: the pad maps stand in F5 Input
    CHECK(task_place(&nes_desc, "Map buttons, pad 1").rfind("F5/input#", 0) == 0);
    CHECK(task_place(&nes_desc, "Map buttons, pad 2").rfind("F5/input#", 0) == 0);
#elif defined(TEST_APP_PET)
    // T212 D10: the tape keys are in the mounted Tape list; Play closes the menu, the others do not (the Spectrum's and the CPC's are the units zx_desc and cpc_desc)
    for (const char *k : { "Tape: Play", "Tape: Stop", "Tape: Rewind" }) {
        CHECK(slot_lists(&pet_desc, "Tape", true, k) && !slot_lists(&pet_desc, "Tape", false, k));
        CHECK_STR(task_place(&pet_desc, k).c_str(), "");
        const Task *t = find_task(&pet_desc, k);
        CHECK(t && t->closes_menu == !strcmp(k, "Tape: Play"));
    }
#elif defined(TEST_APP_APPLE2)
    // T104.1 Q10: the capability list of the real descriptor (a floppy on slot 6 and a hard disk on slot 7: no SIO, no IEC, no cartridge)
    CHECK(desc_has_capability(&apple2_desc, "floppy"));
    CHECK(!desc_has_capability(&apple2_desc, "sio") && !desc_has_capability(&apple2_desc, "iec") && !desc_has_capability(&apple2_desc, "cartridge") && !desc_has_capability(&apple2_desc, "drive_a"));
#elif defined(TEST_APP_SNES)
    // T104.1 Q10: the capability list of the real descriptor (the game cartridge and the pad: no floppy, no SIO, no IEC, no drive)
    CHECK(desc_has_capability(&snes_desc, "cartridge") && desc_has_capability(&snes_desc, "pad"));
    CHECK(!desc_has_capability(&snes_desc, "floppy") && !desc_has_capability(&snes_desc, "sio") && !desc_has_capability(&snes_desc, "iec") && !desc_has_capability(&snes_desc, "drive_a"));
#else

#if defined(TEST_APP_A1200) || defined(TEST_APP_AMIGA)          // T239.5 D13: the Amiga 500's case runs as its app (unit amiga_desc), no longer in the shared unit
    // T104 5.2: the records the Amiga needed are checked like every other part of the descriptor, and a refusal names the field
    {
#ifdef TEST_APP_A1200
        const CoreDesc &amiga_desc = amiga1200_desc;                           // the A1200's app has the one record: the checks of this block run on it (the Amiga 500's amiga_desc is not linked: it is its own app, T239.5 D13)
        CoreDesc d;                                                            // (the shared app's d is declared in the block this app does not run)
        CHECK(!desc_check(&amiga1200_desc));
#else
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
#endif
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
#ifdef TEST_APP_A1200
        CHECK(amiga1200_desc.id_shared && !memcmp(amiga1200_desc.id, "AMIG", 4) && !strcmp(amiga1200_desc.store, "Amiga 1200 Settings"));   // the ID word it shares with the A500, and its own .cfg section
#endif
    }
#endif
#ifndef TEST_APP_AMIGA


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
#endif

#endif
    // golden menu lists
#ifdef TEST_APP_VC20
    golden(&vc20_desc, "vc20");
#elif defined(TEST_APP_A1200)
    golden(&amiga1200_desc, "amiga1200");
#elif defined(TEST_APP_DDT)
    golden(&ddt_novic_desc, "ddt_novic");
#elif defined(TEST_APP_ZX)
    golden(&zx_desc, "zx");
#elif defined(TEST_APP_CPC)
    golden(&cpc_desc, "cpc");
#elif defined(TEST_APP_PET)
    golden(&pet_desc, "pet");
#elif defined(TEST_APP_C128)
    golden(&c128_desc, "c128");
#elif defined(TEST_APP_C264)
    golden(&c264_desc, "c264");
#elif defined(TEST_APP_NES)
    golden(&nes_desc, "nes");
#elif defined(TEST_APP_AMIGA)
    golden(&amiga_desc, "amiga");
#elif defined(TEST_APP_ATARI800)
    golden(&atari800_desc, "atari800");
#elif defined(TEST_APP_DIAG)
    golden(&portdiag_desc, "portdiag");
#elif defined(TEST_APP_APPLE2)
    golden(&apple2_desc, "apple2");
#elif defined(TEST_APP_SNES)
    golden(&snes_desc, "snes");
#else
    golden(&testcore::desc, "test");
#endif

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
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_PET) && !defined(TEST_APP_C128) && !defined(TEST_APP_AMIGA) && !defined(TEST_APP_ATARI800) && !defined(TEST_APP_NES) && !defined(TEST_APP_C264) && !defined(TEST_APP_DIAG) && !defined(TEST_APP_APPLE2) && !defined(TEST_APP_SNES)          // stands on the shared app's cores; the VIC-20's, the A1200's, DDT's and the ZX Spectrum's app are their own builds (run.sh vc20app, a12app, ddtapp, zxapp) with their core only
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
#endif
#ifdef TEST_APP_SNES
    // T212 D8: tasks that fit no drive. Pad maps stand in F5 Input, Sound check on top of Home with the reset (the Next's 50 / 60 Hz, CPU speed and NMI: the unit zxnext_desc)
    {
        for (const CoreDesc *d : { &snes_desc }) {                           // the NES's and the Neo Geo's: the units nes_desc and neogeo_desc
            CHECK(task_place(d, "Map buttons, pad 1").rfind("F5/input#", 0) == 0);
            CHECK(task_place(d, "Map buttons, pad 2").rfind("F5/input#", 0) == 0);
        }
        CHECK_STR(task_place(&snes_desc, "Reset").c_str(), "F1/home#0");
        CHECK_STR(task_place(&snes_desc, "Sound check").c_str(), "F1/home#1");           // beside Reset, before the drive rows
        for (const char *tk : { "Map buttons, pad 1", "Sound check" }) CHECK(task_place(&snes_desc, tk).find("#") != std::string::npos);
        CHECK_STR(task_place(&snes_desc, "Save the battery RAM now").c_str(), "");     // no task is under the drive rows any more: D9 puts it in the list
    }
    // T212 D9: the saves belong to the cartridge slot's list (the Neo Geo's saves and ROM set folder: the unit neogeo_desc)
    {
        CHECK(slot_lists(&snes_desc, "Cartridge", true, "Save the battery RAM now") && !slot_lists(&snes_desc, "Cartridge", false, "Save the battery RAM now"));
    }
#endif
    // T212 D10: the tape keys of the Spectrum's, the CPC's and the PET's decks are the units zx_desc, cpc_desc and pet_desc
#ifdef TEST_APP_ATARI800
    // T212 D11: the Atari's cartridge removal is the slot's Eject (cold start, closes the menu), no task on top of Home
    {
        CHECK(find_task(&atari800_desc, "Remove cartridge, cold start") == 0);
        const MediaSlot *cs = 0;
        for (int i = 0; i < atari800_desc.nmedia; i++) if (!strcmp(atari800_desc.media[i].name, "Cartridge")) cs = &atari800_desc.media[i];
        CHECK(cs && cs->eject_op && !strcmp(cs->eject_op, "atari800.remove_cart") && cs->eject_closes);
        for (int i = 0; i < atari800_desc.nmedia; i++) if (&atari800_desc.media[i] != cs) CHECK(!atari800_desc.media[i].eject_closes);
        CHECK_STR(task_place(&atari800_desc, "Reset").c_str(), "F1/home#0");
        CHECK_STR(task_place(&atari800_desc, "Reload ROMs, cold start").c_str(), "F1/home#2");
    }
#endif
    DONE("test_desc");
}
