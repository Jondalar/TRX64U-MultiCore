// The menu merge (T104.1 D4, D5, D8): what the description, the libraries and the applications contribute, laid onto the route the
// description names. Pure: the registry is only asked whether a name exists, so tests/coreapp/ runs all of it.
//
//   inputs   the core's descriptor, the route (a Skeleton: areas, tabs, widgets; null = the default route), and the contributors:
//            the description's own contributions (rank CR_DESC), the libraries (CR_LIB), the applications (CR_APP), each a LibRecord
//   outputs  a descriptor with the contributed settings and file actions merged in (the menu, Settings and the ROM engine all read the
//            descriptor, so nothing else needs to know), and a MenuMerge: the contributed nodes by anchor, which settings belong to which
//            group, and what was refused
//
// Order, fixed by Alex's point 3 (2026-10-20: a tab's position must not depend on load order or on which USB port a stick sits in):
// contributors sort by rank, then by name; a contributor's own entries keep their declared order. The `order` key of a node sorts inside
// that: nodes of one anchor are ordered by (rank, order, contributor name, position) - libraries before applications (D8 4), the
// description's own nodes first.
//
// A contribution that cannot be placed is refused by name (D8, S3) and is not merged; the others are. Nothing is dropped silently:
// MenuMerge::err holds one sentence for each, and the caller logs them and shows them on the System page.
#ifndef COREAPP_CONTRIB_H
#define COREAPP_CONTRIB_H

#include "desc.h"
#include "settings.h"
#include "libapi.h"
#include "menutree.h"
#include "overlay.h"
#include "menuintent.h"

enum ContribRank { CR_DESC = 0, CR_LIB = 1, CR_APP = 2 };

struct Contributor {
    const char *name;                 // "cbm1541": the contributor name its operations carry; the sort key inside a rank
    uint8_t rank;
    const LibRecord *rec;
};

// ---- the route ---------------------------------------------------------------------------------------------------------------
struct SkelTab {
    const char *id;                   // "drives": what an anchor names (lower case, [a-z0-9_])
    const char *name;                 // what the tab is called on screen
    uint8_t n;                        // the printed number (0: the next free one), stable: never reused or reordered (D8)
    uint8_t widget;                   // TabWidget
    const char *groups;               // settings groups the tab lists, comma separated ("drive_a,drive_b"); null: the ungrouped settings
    const char *layout;               // "name;key=value;..." how the tab is drawn (a screen of the UX concept); null: the flat list
};
struct SkelArea {
    const char *key;                  // "F5"
    const char *name;                 // null: the core's short title
    bool shifted;
    const SkelTab *tabs;
    int ntabs;
};
struct Skeleton {
    const SkelArea *areas;
    int nareas;
};
// A name the description hides, a name it gives another label (D8; Alex 2026-10-26: reaches nodes, settings and file actions). The id of a node is
// its `id` (else its operation or state), of a setting its key (a renamed setting has the new key in the .cfg too), of a file action its label.
struct NodeRename {
    const char *id;
    const char *label;
};
struct Overrides {
    const char *const *hide; int nhide;
    const NodeRename *rename; int nrename;
};

const Skeleton *contrib_default_skeleton();       // T104 5.8's areas; T187 D1 added the Settings area

// ---- the result: the maxima are defaults; an app linked from the SDK sets them in the [sizes] table of its TOML B (tools/app_build.py) ----
#ifndef CONTRIB_SETTINGS_MAX
#define CONTRIB_SETTINGS_MAX 16           // settings contributed by all together
#endif
#ifndef CONTRIB_VALUES_MAX
#define CONTRIB_VALUES_MAX 16             // values of one contributed setting (a range wider than this is refused)
#endif
#ifndef CONTRIB_FILETYPES_MAX
#define CONTRIB_FILETYPES_MAX 40          // extensions in the merged descriptor: its own (a record holds DR_FTYPES of them; the TRX64U has 24 since T228 R21) and the new ones
#endif
#ifndef CONTRIB_ACTIONS_MAX
#define CONTRIB_ACTIONS_MAX 96            // actions over all of those
#endif
#ifndef CONTRIB_POOL_MAX
#define CONTRIB_POOL_MAX 1024             // characters of the labels made for contributed range settings
#endif
#ifndef CONTRIB_ERR_MAX
#define CONTRIB_ERR_MAX 24
#endif
#ifndef CONTRIB_POLLS_MAX
#define CONTRIB_POLLS_MAX 12
#endif
#ifndef CONTRIB_SLOTPOOL
#define CONTRIB_SLOTPOOL 3072             // characters of the strings made by $slot expansion
#endif
#ifndef CONTRIB_BINDPOOL
#define CONTRIB_BINDPOOL 64
#endif
#define CONTRIB_ERR_LEN 200

struct MergedNode {
    MenuNode n;
    const char *label;
    const char *id;
    const char *area;                 // the anchor's parts
    const char *tab;
    int16_t order;
    uint8_t rank;
    int pos;                          // the contributor's declaration order
};

// A poll a contributor declared (T104.1 D6): the operation, called every period_ms by a task of its own (service.h)
struct ContribPoll {
    const char *owner;
    const char *op;
    uint16_t period_ms;
    bool when_frozen;                 // the declaration of the record (LibPollDecl); the menu record has no such field, so false there
};

struct MenuMerge {
    const Skeleton *skel;             // the route the tree is built on (never null after contrib_merge): a copy of the given route plus the contributed tabs
    SkelArea own_areas[MENU_AREAS_MAX];
    SkelTab own_tabs[MENU_AREAS_MAX][MENU_TABS_MAX];
    Skeleton own_skel;
    ContribPoll polls[CONTRIB_POLLS_MAX];
    int npolls;
    char slotpool[CONTRIB_SLOTPOOL];  // $slot: "Reset Drive $slot" once for every slot the core has
    int slot_used;
    OpBind bindpool[CONTRIB_BINDPOOL];
    int bind_used;
    MergedNode nodes[MENU_NODES_MAX];
    int nnodes;
    // for each setting of the merged descriptor: the group it names, null for the descriptor's own and for an ungrouped contribution
    const char *setting_group[SETTINGS_MAX];
    int ncontributed_settings;
    int nrefused;                     // contributions refused, however many sentences fit in err
    char err[CONTRIB_ERR_MAX][CONTRIB_ERR_LEN];
    int nerr;
    // storage the merged descriptor and the nodes point into
    Setting settings[SETTINGS_MAX];
    const char *values[CONTRIB_SETTINGS_MAX][CONTRIB_VALUES_MAX];
    char pool[CONTRIB_POOL_MAX];
    int pool_used;
    FileType filetypes[CONTRIB_FILETYPES_MAX];
    FileAction actions[CONTRIB_ACTIONS_MAX];
    int nactions;
    // T239.3 base 3.3: the key table of the core's description (descrec.h section 18; the rows live in the record, which outlives this). contrib_merge clears it; the caller that has the
    // description sets it (bundle_merge, the tests' record menu: tests/coreapp/record_menu.h).
    const CoreKey *keys;
    int nkeys;
    // T239.3 D32: where the application's rows stand: the base's table with the core's routes and places over it (menu_place_merge), in place_store, when has_place (false after contrib_merge: the base's table alone). A struct, not a pointer into itself: a MenuMerge may be copied.
    bool has_place;
    UiPlace place_store;
    // T256 D8: the elements of the overlay service: the base's table with the core's over it (ovl_merge), in elem_store, when has_elem (false after contrib_merge: the base's table stands)
    bool has_elem;
    UiElements elem_store;
};

// Merges the contributions into `d`. `reg` (optional) is asked whether the operations and states the contributions name exist and whether
// their bindings fit; without it the names are taken as given. `out` receives a copy of `d` with the contributed settings (after its own, in
// merge order) and the file actions (after the descriptor's, per extension). The caller installs `out` where it had `d`.
// Returns the number of contributions refused (also in mm->err).
int contrib_merge(const CoreDesc *d, const Skeleton *skel, const Overrides *ov, const Contributor *c, int nc, const OpRegistry *reg, CoreDesc *out,
                  MenuMerge *mm);

// The slots a core has (Alex 2026-10-26, D15): its capabilities named drive_<x>. A declaration that has `$slot` in a label, anchor, key, group, operation,
// state, condition, id or text binding is made once for each of them, with `$slot` replaced by the slot's name ("drive_a"); a core with none gets none.
int contrib_slots(const CoreDesc *d, const char **out, int max);

// "state != value", "state == value", or just "state" (it has a value). The word `empty` stands for no value at all. `get` gives the state's
// text for a name (null: none). An empty or null expression holds.
typedef const char *(*cond_state_fn)(void *user, const char *name);
bool cond_holds(const char *expr, cond_state_fn get, void *user);

// The tab of `skel` with this id in this area, or null; the widget decides whether it takes nodes.
const SkelTab *skel_find_tab(const Skeleton *skel, const char *area_key, const char *tab_id, const SkelArea **area_out = 0);
bool tab_takes_nodes(const SkelTab &t);
// Does the comma separated list name `group`?
bool groups_have(const char *groups, const char *group);

// The attribute strings of nodes and layouts: "key;key=value;key=value". A bare key is a flag. Returns whether `key` is there; its value (empty for a flag)
// is copied to `out` when given. The first word of a layout is its name: ui_name().
bool ui_attr(const char *attr, const char *key, char *out = 0, int n = 0);
void ui_name(const char *attr, char *out, int n);

#endif
