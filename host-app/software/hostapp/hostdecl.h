// The host application's capabilities, declared once (T192 D21: "the declaration generates the functions/endpoints"; T192.2 §6 Q1: constexpr tables compiled in).
// hostdecl.cc holds the tables; hostdomain.cc walks them to build the snapshot (pages, items, rows, with stable ids), to resolve an intent to an action and
// to run it. The routes are generic (state, intent by id, events): a new declared row or action is reachable over the API with no further code.
// Pure C++11: no LVGL, no FreeRTOS.
#ifndef HOST_HOSTDECL_H
#define HOST_HOSTDECL_H

#include "hostmodel.h"

// What an action does when it runs. HA_KEYS: the intent of host_core with the action's name (the keys of the board take the same intents through the one key map
// in host_core.cc). HA_FIELD: the field step (settings_change or the WiFi page's handler). HA_FN is for the few actions that need the model's own call.
enum HActKind { HA_KEYS = 0, HA_FIELD, HA_FN };

enum HActFlag {
    HAF_DESTRUCTIVE = 1,     // over the API it asks first (D19a); the answer runs it
    HAF_FLOW = 2,            // it starts a flow of the app (a job, or a page that waits for an answer)
    HAF_ROW = 4,             // it needs a row of its list source (`target` of the intent)
    HAF_TEXT = 8,            // it takes a text (`value`): typed into the entry the flow opens
    HAF_GLOBAL = 16          // offered on every screen
};

struct HAction {
    const char *name;        // stable: "entry.start"
    const char *title;
    int kind;                // HActKind
    int view;                // HView the keys are meant for (-1: any); the runner presses the page's navigation key first when the menu is elsewhere
    const char *src;         // the list source whose row is focused first ("entry", "file"), or null
    const char *field;       // HA_FIELD: the field; HA_KEYS with src null: a field to focus first (the settings and WiFi rows)
    int dir;                 // HA_FIELD: -1 LEFT, +1 RIGHT/RETURN
    unsigned flags;          // HActFlag
    const char *ask;         // the question a destructive action asks
    int mode;                // -1 any, else the HMode the browser has to be in
    const char *when;        // a note for the catalogue: when it applies
};

enum HItemKind { HI_INFO = 0, HI_TOGGLE, HI_VALUE, HI_CHOICE, HI_TEXT, HI_BUTTON };

// One row of a page that is always the same rows (the settings, the WiFi page, the network page's lines). Its value comes from the model's field.
struct HItem {
    const char *page;        // "settings", "settings/wifi", "net"
    const char *id;          // "direct": the item's id is "<page-prefix>.<id>"
    int kind;                // HItemKind
    const char *title;
    const char *field;       // the model's field key
    const char *activate;    // the action RETURN (activate) runs; null: none (an info line)
    const char *step;        // an action for a UD_SET of this item's value; null: the field is set by name through the model
};

// A page of the application: a view, or a page over a view.
struct HPage {
    const char *id;          // "menu", "browse", "edit", "net", "settings", "settings/wifi", "settings/cores"
    const char *title;
    int view;                // HView
    const char *nav;         // the navigation key's name ("f1") on a view, null on a page over a view
    const char *list;        // the list source its rows come from ("entry", "file"), or null
    const char *prefix;      // row id prefix
    const char *activate;    // the action RETURN on a row runs
};

struct HostDecl {
    const HPage *pages;
    int npages;
    const HItem *items;
    int nitems;
    const HAction *actions;
    int nactions;
    const char *const *row_actions_entry;   // the actions offered on a boot entry row (menu page), edit page, file row
    const char *const *row_actions_edit;
    const char *const *row_actions_file;
};

const HostDecl &host_decl();
const HAction *hd_action(const HostDecl &d, const char *name);
const HPage *hd_page(const HostDecl &d, const char *id);
const HPage *hd_page_of_view(const HostDecl &d, int view);

// The id of an item: "<prefix>.<id>" where prefix is the page id with '/' as '.' ("settings.direct", "settings.wifi.dhcp").
void hd_item_id(const HItem &it, char *out, int cap);
// The page the model is on now: a view, or the settings' subpage.
const HPage &hd_current_page(const HostDecl &d, HostModel &m);

#endif
