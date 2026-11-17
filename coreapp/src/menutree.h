// The menu is a whitelist (T93 D8, S3): generated from the descriptor plus a fixed set, never pruned from a bigger menu. Three
// sources and nothing else - the descriptor's tasks, settings, media slots and file types; the shared fixed entries below;
// (a mouse group, only for a core with a mouse sink: none of the cores built here has one). The dump of this tree is what the
// golden files in tests/coreapp/golden/ are compared with, so a new fixed entry is a visible diff in every core's list.
//
// Since T104 the tree is a *route* in the prototype's grammar (T104 5.8): a function key picks an area, a number key picks a
// tab inside it, and published numbers never reorder. The four pages of T93 are the same entries under five areas - Home
// (T212: the reset rows, the drive rows and the configs), Library (the browser), the core (F5: its own settings, Video, Audio, Input),
// APP (F7: the generic app's settings), System (the actions and the status). Nothing is added or dropped on the way, which is what the golden files check.
#ifndef COREAPP_MENUTREE_H
#define COREAPP_MENUTREE_H

#include "desc.h"

enum EntryKind {
    EK_TASK,            // a descriptor task
    EK_MEDIA,           // a descriptor media slot (shows what is in it, RETURN ejects when the slot can)
    EK_BROWSER,         // the file browser (the descriptor's file types decide which files it shows and what they do)
    EK_SETTING,         // a descriptor setting
    EK_INFO,            // a read-only line of the system information
    EK_RESET_HOST,      // fixed: Reset to host defaults (D10)
    EK_RESTART_CORE,    // fixed: Restart core / Reload core - the start path again without the host (T104 Alex 2026-10-23, 5.14)
    EK_RETURN_HOST,     // fixed: Return to host menu (D13)
    EK_BUG_REPORT,      // fixed: Bug report - the state dump to /sd/report (T162 P1)
    EK_WIFI,            // fixed, in the system tab (F8) of every core: WiFi On / Off, switched live and not stored; the line under it shows state, network, address (T184 D3)
    EK_SAVE_SET,        // fixed, in the Summary (media) tab: Save as config... (T198 D3, was Save media set... of T180 D5)
    EK_LOAD_SET,        // fixed, the same: Load config...
    EK_NODE,            // a node a library, an application or the description contributed (T104.1 D4, D8); `index` is into MenuTree::nodes
    EK_HEADING,         // T187 D1: a quiet divider that names the group of the rows below it; never selectable, `label` is its text
    EK_DRIVE_MOUNT,     // T212 D5, in the drive list: mount from the Library (the Library opens on the slot's file types); `index` is the media slot
    EK_DRIVE_EJECT,     // T212 D5, in the drive list: eject the slot's medium
    EK_OPENS            // T187 D2: a row that opens a page (a hidden tab of the same area); `index` is the descriptor setting whose value the row shows
};

// What a tab shows of the descriptor (T104.1 D8: the description names, for each tab, the widget). Contributed nodes can join a tab of the kinds
// that list rows (media, settings, tasks, system, nodes); the browser's tab and the information lines take none.
enum TabWidget {
    TW_INFO = 0,        // the six information lines
    TW_BROWSER,         // the file browser
    TW_MEDIA,           // the descriptor's media slots
    TW_SETTINGS,        // the settings: those that name no group, or the groups the tab lists
    TW_TASKS,           // the descriptor's tasks
    TW_SYSTEM,          // the fixed entries
    TW_NODES            // only what is contributed
};

#define MENU_AREAS_MAX 8
#define MENU_TABS_MAX 8                  // tabs of one area in the tree; the strip of the view has room for MENU_STRIP_TABS of them: an application's hidden screens are tabs that are not on it
#define MENU_STRIP_TABS 8                // the tab strip of the view has room for eight (= TRX_TABS_MAX, checked in ui.cc): a library or an application adds no tab beyond them to a core's area
#define MENU_MAX_ENTRIES 640
#define MENU_NODES_MAX 320

// What a fixed entry is, as data: the operation RETURN runs, the states its row shows, the words that tell how it went. The controller reads this and knows no operation by name.
struct FixedDef {
    int kind;                       // EntryKind
    int variant;                    // MenuEntry::index (a build variant: "Reload core" and "Restart core" differ in the words)
    const char *op;                 // RETURN runs it (null: a flow of the controller's own: the sets, the way back to the host)
    const char *pre_op;             // runs before it and stops the entry when it fails (the media are written back before the core is left)
    const char *value_state;        // the row's value: this state's text
    const char *value_none;         // ... when the state has none
    const char *value_suffix;       // ... with this behind it
    const char *sub_state;          // the row's second line: this state's text
    const char *ok_text;            // the status line when it worked (null: the operation's own message); with it, a failure with no message says `fail_text`
    const char *fail_text;
    bool closes;                    // the menu closes when it worked
};
// The definition of a fixed entry, null for a kind that has none.
const FixedDef *menu_fixed(EntryKind kind, int variant);
// T212 D5: the slot is a drive (it can be ejected or a file can be mounted into it); other slots only show a state.
bool menu_slot_is_drive(const MediaSlot &m);
// T212 D5: the task is reached from a drive's list (named by a media slot, or that slot's eject), so Home does not list it.
bool menu_task_claimed(const CoreDesc *d, int task);

struct MenuEntry {
    EntryKind kind;
    const char *label;
    int index;          // into the descriptor's table of that kind
    bool dialog;        // T216 D11: a task row of a settings tab that opens a dialog: the row shows ">" at the right
};

struct MenuTab {
    const char *id;     // the name in lower case ("summary"): what an anchor and the description say, the printed number is only the place
    const char *name;
    uint8_t n;          // the printed number, the stable half of the route
    const char *layout; // how the tab is drawn, "name;key=value;..." (contrib.h ui_attr), or null: the flat list
    int first, count;   // into MenuTree::e
    bool page;          // T187 D2: a page a row opens (Hotkeys): no place on the tab strip, no number; leaving it returns to `parent`
    int8_t parent;      // the tab (index in the area) whose row opens this page, -1: none
    bool home;          // T212: the session tab (Home): reset rows, drive rows, configs
    bool drive_page;    // T212 D5: the drive list, filled by MenuCtl when a drive row is opened (its rows are reserved here: DRIVE_PAGE_ROWS)
};
#define DRIVE_PAGE_ROWS 12

struct MenuArea {
    const char *key;    // "F1".."F8", the logical key as it is printed
    const char *name;
    bool shifted;       // the skeleton's flag for F2/F4/F6/F8; the rail draws one list in the route's order and does not read it (T228 D4)
    MenuTab tabs[MENU_TABS_MAX];
    uint8_t ntabs;
};

// A contributed node as the tree keeps it (kinds are LibNodeKind of libapi.h: an operation button, a state line, a group heading).
struct MenuNode {
    uint8_t kind;
    const char *op;                   // button: the operation, every argument filled from `bind`
    const char *state;                // state line: the value shown
    const char *when_state;           // "name != value": the row works only while it holds
    const OpBind *bind; int nbind;
    bool closes_menu;
    const char *attr;                 // how it is shown (libapi.h LibNodeDecl::attr), or null
    const char *owner;                // the contributor, for the dump and the System page
    const char *group;                // the anchor's third part, "" when none
};

#define MENU_HEADS_MAX 24

struct MenuTree {
    MenuArea a[MENU_AREAS_MAX];
    uint8_t nareas;
    MenuEntry e[MENU_MAX_ENTRIES];
    int n;
    MenuNode nodes[MENU_NODES_MAX];
    int nnodes;
    char heads[MENU_HEADS_MAX][32];  // the text of the headings made from a group id (T228: a tab with `sections`), the entries point here
    int nheads;
};

// The fixed system information lines, in this order. Their values are the application's to fill in at run time.
extern const char *const menu_info_labels[];     // null-terminated

// return_host: the fixed "Return to host menu" entry exists only where an ICAP loader can take the FPGA back to the host (T104.2 D2): not on
// the monolith, which has no host to return to.
//
// reload_core: the F8 entry of a core that came from a .cor on a host with an ICAP loader is "Reload core" (its partial bitstream is loaded again,
// T104 5.14); a flat build keeps "Restart core". Same entry kind either way.
//
// mm: what libraries, applications and the description contribute and how the description lays out the route (contrib.h); null: the default
// route with nothing added - every core of today, and its golden dump.
struct MenuMerge;
void menu_build(const CoreDesc *d, MenuTree *t, bool return_host = true, bool reload_core = false, const MenuMerge *mm = 0);
// The tree of a running core application: return_host, reload_core and the merge are the context's (the board, the .cor, the libraries).
struct CoreCtx;
void menu_build_for(const CoreCtx *c, MenuTree *t);

// T187 D1: the one table that says where a generic row lands in EVERY core, with or without a description record. A row the application adds to every
// core (Picture, Scale, Smoothing, Volume, the hotkeys, Persist medium mounts, Home directory, Enter home on startup) is named by what it does (its
// operation), not by the group a descriptor wrote on it. `area` is the key of the area, `tab` the tab's id there, `heading` the group's name; a core whose
// route has no such tab (a record without the area) lists the row where the ungrouped settings are.
struct MenuGroupRoute {
    const char *group;      // "video", "audio", "hotkeys", "storage", "menu"
    const char *area;       // "F5" machine (video, audio), "F7" the app (T212)
    const char *tab;        // "video", "audio", "input", "settings" (a core without that tab lists the row in the area's tab "settings", under the heading)
    const char *heading;    // the group as a person reads it: "Video", "Audio", "Input", "Storage", "Menu"
    const char *label;      // a page's row in the tab ("Hotkeys")
    bool page;              // the rows are not in the tab: one row there opens a page of them (Hotkeys)
};
const MenuGroupRoute *menu_route_of(const Setting &s);

// The area a key selects, or -1. Takes the logical key (KEY_F1..KEY_F8), not a shift state.
int menu_area_of_key(const MenuTree *t, int key);
// The tab whose printed number is `n` in area `area`, or -1.
int menu_tab_of_number(const MenuTree *t, int area, int n);

// One line per area, tab and entry: "area F1 Home" / "  tab 1 Home" / "    task Reset" ...
// The golden format. Returns the length.
int menu_dump(const CoreDesc *d, const MenuTree *t, char *out, int max);

#endif
