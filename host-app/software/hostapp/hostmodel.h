// What the declaration (hostdecl.h) and the domain (hostdomain.h) see of the host application (T192.1 MVP, T192 D21): a few accessors and one way to act.
// hostui.cc implements it over its own state (UiModel at the end of that file); a bench implements it over a fake. Pure C++11: no LVGL, no FreeRTOS.
//
// Everything the menu does is reachable through `key` (the board's own key path, on_key) and through the named fields (settings_change and the WiFi page);
// the model adds no behaviour of its own, except the arm of a destructive row action (arm / confirm / disarm below): there the screen owns the state between the two keys.
#ifndef HOST_HOSTMODEL_H
#define HOST_HOSTMODEL_H

#include <stdint.h>

enum HView { HVW_MENU = 0, HVW_BROWSE, HVW_EDIT, HVW_NET, HVW_SET, HVW_COLORS, HVW_N };
enum HMode { HMODE_NORMAL = 0, HMODE_PICK, HMODE_INSPECT };

// One row of a list source ("entry": the boot menu entries, "file": the File Browser's directory listing).
struct HRow {
    char ident[96];                       // what the row IS: the id is made from it
    char title[96], sub[140], badge[28], value[40];
    bool dir, def, startable, enabled, marked;
};

// One named field: a row of the settings page, of the WiFi page, or a line of the network page.
struct HField {
    char value[96];
    char help[240];
    bool enabled;                         // false: the row is greyed (it does nothing now)
    bool visible;                         // false: the page does not show it now
    bool on;                              // a toggle's state
};

// The destructive row actions whose keys are "key, same key" (entry.remove DEL DEL, browse.delete DEL DEL, wifi.forget F F): the screen owns the arm between the two
// keys (T192 D21). The API asks the screen to arm, and later to confirm or to drop the arm by the token the arm returned.
enum HArmAct { HARM_NONE = 0, HARM_ENTRY_REMOVE, HARM_FILE_DELETE, HARM_WIFI_FORGET };
enum HArmR { HARM_ARMED = 0, HARM_REFUSED, HARM_BUSY };
// How an arm ended on the screen's own account (hostapi_arm_end): the confirming key, another key at the board, or anything else (a page change, a reload, a remote key).
enum HArmEnd { HARM_END_YES = 1, HARM_END_NO, HARM_END_GONE };

class HostModel {
public:
    virtual ~HostModel() { }
    virtual int view() = 0;               // HView
    virtual int mode() = 0;               // HMode: the browser's
    virtual bool wifi_page() = 0;         // the WiFi page is up over the settings
    virtual bool cores_page() = 0;        // the page "Cores and ROMs" is up over the settings
    virtual bool picture_pending() = 0;   // a picture change waits for a key (8 s)
    // The board's key path: the key is queued and handled by on_key in the next pass. Returns false when the queue is full.
    virtual bool key(int k) = 0;
    // List sources
    virtual int rows(const char *src) = 0;
    virtual bool row(const char *src, int i, HRow *r) = 0;
    virtual int cursor(const char *src) = 0;
    virtual void focus(const char *src, int i) = 0;
    // Named fields
    virtual bool field(const char *key, HField *f) = 0;
    virtual void field_focus(const char *key) = 0;
    // A value of the field set by name ("on", "off", "5", "720p", an address). 0 taken, else the reason in `why`.
    virtual int field_set(const char *key, const char *value, char *why, int n) = 0;
    // The same as the key the row takes: dir -1 LEFT, +1 RIGHT or RETURN. Runs settings_change for the settings page, the WiFi page's own handler for its rows.
    virtual int field_step(const char *key, int dir, char *why, int n) = 0;
    // Named texts: "path" (browser), "title", "crumb", "footer", "note" (the line at the bottom), "detail" (the right panel of the browser), "countdown".
    virtual bool text(const char *what, char *out, int n) = 0;
    // The browser opens a folder (an absolute path of the host's file system view). 0 taken.
    virtual int browse_goto(const char *path, char *why, int n) = 0;
    // Arms `act` on row `row` of its list (the row's `ident` is checked, the list may have moved): the same code as the first local key, after the switch to the action's view.
    // The view is not left when the user is in a modal state or a typed entry. HARM_ARMED: *token names this arm. HARM_REFUSED: the screen's own words in `why` (what its
    // note says when the key bails). HARM_BUSY: not now, `why` says what holds the keys.
    virtual int arm(int act, int row, const char *ident, uint32_t *token, char *why, int n) = 0;
    // The armed action runs, only while the screen is still armed with that token. False (reason in `why`): it is not armed any more, or the action cannot run now.
    virtual bool confirm(uint32_t token, char *why, int n) = 0;
    // The arm goes away (and its note), only while the screen is still armed with that token.
    virtual void disarm(uint32_t token) = 0;
    virtual bool armed(uint32_t token) = 0;
};

#endif
