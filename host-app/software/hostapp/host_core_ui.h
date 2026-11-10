// What a key or an intent does on the host's screen, as the core runs it (T192.2 P1/P2, T192 D23): one input (a key of the board, a `key` intent of the service, a declared
// action as an intent) is resolved by the key map (host_core.cc) to an intent of the context the screen is in and acted on HERE: the cursor and the pages (host_core_nav),
// the boot menu (hc_entry_*), the settings, the arm of a destructive row action, the browser's pick, delete and add, the questions. What only the screen can do (draw a view,
// run a flow's pages, put a picture mode up, start a core with its progress page) comes through the environment. No LVGL, no FreeRTOS, no key wait.
#ifndef HOST_HOSTCORE_UI_H
#define HOST_HOSTCORE_UI_H

#include <stdint.h>
#include "host_core.h"
#include "host_core_nav.h"
#include "host_core_cm.h"
#include "host_core_rows.h"
#include "host_core_flow_fileops.h"
#include "host_core_flow_wifi.h"
#include "hostmodel.h"
#include "host_core_job.h"
#include "lineedit.h"

enum HcUiFlow { HUF_RENAME = 1, HUF_CONVERT, HUF_RESTORE, HUF_UPDATE, HUF_FORMAT, HUF_INSPECT, HUF_UNZIP };      // the flows whose pages the screen runs (arg: the path, the medium's name, the entry's index)
enum HcUiColors { HUC_REPORT = 1, HUC_SINGLE, HUC_MULTI };                                              // the colour test's buffer actions

struct HcUiEnv {
    void *ctx;
    void (*redraw)(void *ctx);                                               // the view is to be built again (the screen's render flag)
    void (*show)(void *ctx, int view);                                       // the view is up: its widgets are shown (show_view)
    void (*note_out)(void *ctx, const char *text, int tone, bool net);       // a note was set: the console line and the service (net: the WiFi pages' note)
    void (*lazy_reset)(void *ctx);                                           // every entry is read again (icons too)
    void (*lazy_mark)(void *ctx, int i);                                     // entry i is read again
    void (*pq_hide)(void *ctx);                                              // the picture question's box goes
    bool (*picture_set)(void *ctx, int hdmi, const char *how);               // a picture mode is put up and stored; false: the note says why not
    void (*start_any)(void *ctx, const char *path, const char *title, const BootEntry *trusted, bool ask);
    void (*start_core)(void *ctx, const char *path, const char *title);
    void (*flow)(void *ctx, int kind, const char *arg, int index);          // HUF_*: the screen runs the flow's pages
    void (*colors)(void *ctx, int what);                                     // HUC_*
    bool (*busy)(void *ctx);                                                 // a format page is open or a format runs: nothing else starts
    bool (*report)(void *ctx, char *name, int ncap, char *why, int wcap);    // writes the bug report; true: written, name = its file
    void (*report_dir)(void *ctx, char *out, int cap);
    void (*ask)(void *ctx, const char *text, int yes_key, int no_key);       // the service's question (null text: gone)
    void (*history)(void *ctx, const char *value);                           // a typed value for the service's history
    bool (*key)(void *ctx, int k);                                           // the board's key path: queue a key
    const char *(*footer)(void *ctx);                                        // what the footer line says now (null: no screen is up)
    void (*job)(void *ctx, HcJob *j);                                        // a job the core made (the WiFi page's modals): the screen steps it and deletes it
    void (*web_result)(void *ctx, const char *text);                         // what the old web page shows about its last command
    const HcFoEnv *fo;
    const HcCmEnv *cm;
    const HcWifiEnv *wifi;
};
void hc_ui_init(const HcUiEnv *e);
const HcUiEnv &hc_ui_env(void);

// ---- rename an entry (T139 D3) as a job ------------------------------------------------------------------------------------------------------------------
// The modal "Rename menu entry": the entry's path under the title, the typed name with its cursor, a message line. The screen draws the page (and tells the service); the job
// reads the keys, edits, and at RETURN saves the name into boot.cfg and reads it back (a note says how it went).
struct HcRenamePage {
    char sub[112];                                // the entry's path
    char typed[LE_MAX + 1];
    int cur;                                      // the cursor's place in `typed`
    char msg[64];
};
struct HcRenameEnv {
    void (*draw)(void *ctx, const HcRenamePage &p, bool first);     // draws the page (first: it opens) and tells the service
    void (*end)(void *ctx);                                         // the page is gone
    void (*history)(void *ctx, const char *text);                   // a saved name for the service's history
    void *ctx;
};
HcJob *hc_rename_job(const HcFlowEnv &flow, const HcRenameEnv &re, int entry);

// The argument of the old web page's picture and countdown commands (web.video, web.countdown): set just before the intent is queued.
void hc_ui_web_arg(int a);
bool hc_ui_remote_ask(void);                      // true while a remote client's command (web.start) runs: the question it raises times out (D19a)

// What one input leaves for the screen to do.
struct HcUiResult {
    bool redraw;                                  // the view is to be built again
    bool selmove;                                 // only the selection moved: sel0 / bsel0 are the rows to restyle
    int sel0, bsel0;
};
// One input: a key (the board's, or a `key` intent's) or an intent (HC_ACT_KEY, from a declared action).
void hc_ui_input(int k, HcUiResult *r);
unsigned hc_ui_fingerprint(void);                 // what a key may change; a key that leaves it as it was does not rebuild the view (the service's snapshot follows it too)
int hc_ui_ctx(void);                              // the context the key map is read in now
void hc_ui_changed(void);                         // something changed that the fingerprint does not see (a note, a save, a start)
void hc_ui_go(int view);                          // the view changes (a pending question ends with it) and is shown

// The pieces the screen's own flows and callbacks use.
void hc_ui_note(const char *text, int tone);      // the feedback line (and its console line and the service's note)
void hc_ui_net_note(const char *text, int tone);  // the WiFi pages' note
void hc_ui_cfg_after(int r);                      // what a command that wrote boot.cfg left: the note when it could not be written
void hc_ui_refuse(int cor_res, const CorInfo &ci);
void hc_ui_refuse_full(int cfw_res);
void hc_ui_pick_end(void);                        // the browser comes back from the target pick
void hc_ui_pick_confirm(void);                    // RETURN in the target pick
void hc_ui_browse_add(void);                      // A on the selected bundle
void hc_ui_inspect_pick_start(void);
bool hc_ui_picture_step(void);                    // V: the next mode (the stored choice steps through)
bool hc_ui_stepped_to(int hdmi);
void hc_ui_settings_change(int kind, int dir);    // dir: -1 LEFT, +1 RIGHT / RETURN
void hc_ui_wifi_key(int act, int rawk);           // the WiFi page's keys
const char *hc_ui_wifi_forget_arm(void);          // the first F: arms and says so, or the reason it cannot (null: armed)
void hc_ui_entry_remove_arm(int row);
void hc_ui_entry_remove_run(int row);
int hc_ui_delete_check(char *path, int pcap, const char **warn);    // 0 go on, 1 nothing to do, 2 refused (`*warn` is the note)
void hc_ui_delete_arm(const char *path);
void hc_ui_delete_run(const char *path);

#endif
