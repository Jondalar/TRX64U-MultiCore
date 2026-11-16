// Where the person is on the host's screen, as the core holds it (T192.2 P1, T192 D23): the view, the cursors, the lists' windows, the File Browser's folder and
// its rows, the browser's mode, the pages' sub-modes (the WiFi page, the Cores and ROMs page), the colour test's pattern and the two feedback lines. The screen READS this
// (hc_nav(), const) and the intents change it (hc_nav_*, hc_browse_*, hc_note_*); hostui.cc holds none of it (tools/check_host_cut.py rule N1). No LVGL, no FreeRTOS.
#ifndef HOST_HOSTCORE_NAV_H
#define HOST_HOSTCORE_NAV_H

#include <stdint.h>
#include "host_core.h"
#include "hostfs.h"
#include "hostmodel.h"

enum HcView { HCV_MENU = 0, HCV_BROWSE, HCV_EDIT, HCV_NET, HCV_SET, HCV_COLORS, HCV_N };      // HCV_MENU .. HCV_SET are the navigation's 0..4; the colour test has no entry
enum HcBrMode { HCB_NORMAL = 0, HCB_PICK, HCB_INSPECT };                                    // the browser: ordinary, picking the target folder of a copy / move, picking a .cfw/.ue2 to look into
enum HcCmMode { HCM_OFF = 0, HCM_LIST, HCM_ASK1, HCM_ASK2 };                                // the page "Cores and ROMs" over the settings: off, the list, the first / the second question
#ifndef HC_BR_START                                                                         // (a mock-up build for the pictures of the "more" row sets both small: -DHC_BR_START=40 -DHC_BR_HARD=60)
#define HC_BR_START 256                                                                     // rows the browser buffer holds first (the parent row included); a longer folder grows it
#endif
#ifndef HC_BR_HARD
#define HC_BR_HARD 32768                                                                    // the most rows it ever holds, however much heap is free (T229-free bound: a real memory limit is the heap quarter below)
#endif
#define HC_NOTE_MAX 160

struct HcNav {
    int view;                           // HcView
    int sel;                            // menu / editor cursor
    int ssel;                           // settings: the selected slot
    int nsel;                           // the WiFi page: the selected row
    int bsel;                           // browser cursor
    int mfirst, efirst, bfirst;         // the first visible row of the menu / the editor / the browser window
    char bpath[160];                    // the browser's folder ("/" = the media)
    HostDirEntry *bl;                   // its rows (heap, bcap + 1 entries: the one after the last is zeroed)
    int bcap;                           // the rows bl can hold
    int bn;                             // ... and their number
    int bmore;                          // entries of the folder that did not fit the heap budget (0 = the list is whole); shown as one last row
    bool bup;                           // a synthetic ".." row is row 0
    uint32_t gen;                       // counts the times the list was read anew: what is cached per row is stale when it changes
    int br_mode;                        // HcBrMode
    int wm_mode;                        // 1: the WiFi page is up over the settings rows
    int pm_mode;                        // 1: the Picture page (T187 D5, the modes the screen names) is up over the settings rows
    int cm_mode;                        // HcCmMode
    int cm_top;                         // the first visible line of the Cores and ROMs page
    char cpat;                          // the colour test: r g b k v h, 0 = swatches
    char note[HC_NOTE_MAX];             // the line of feedback at the bottom of the main area
    uint8_t note_tone;                  // HcTone
    char nnote[HC_NOTE_MAX];            // the WiFi pages' note
    uint8_t nnote_tone;
};

const HcNav &hc_nav(void);

// The window sizes the canvas that is up holds (the screen tells; they change with the canvas).
void hc_nav_geometry(int menu_rows, int browse_rows, int cores_rows, int edit_rows = 0);   // edit_rows 0: as many as the menu (T209: the editor's panel takes room the menu does not need)
int hc_nav_menu_rows(void);
int hc_nav_edit_rows(void);
int hc_nav_browse_rows(void);
void hc_browse_more_text(char *out, int n);   // the text of that last row
void hc_browse_pos_text(char *out, int n, int first, int rows);   // the browser's "a-b of n" (entries only), empty while the list fits
int hc_browse_nsel(void);                   // the rows the cursor stands on (the entries; not the "more not shown" line)
int hc_browse_nrows(void);                  // the rows the list shows: the entries plus the "more not shown" row when the folder did not fit
int hc_nav_cores_rows(void);

// What the core cannot do alone: the board's side.
struct HcNavPlatform {
    int (*list)(const char *path, HostDirEntry *out, int max, int *total);   // the folder's entries, folders first (hostfs_list without the bundle filter), at most max; *total = all of them; -1 when it cannot be read
    bool (*exists)(const char *path);
    void (*arm_end)(uint32_t token, int how);                    // an arm ended on the screen's own account (HArmEnd): the service learns how (hostapi_arm_end)
    bool (*key_local)(void);                                     // the key being handled is the board's own (not a queued remote one)
    void (*loaded)(void);                                        // the browser's list was read anew: whatever the screen cached of the old rows is stale
    void (*ask_gone)(void);                                      // the page's question for the service is gone
    uint32_t (*free_heap)(void);                                 // the heap that is free now (the browser grows its list by a quarter of it); 0 = unknown
};
void hc_nav_init(const HcNavPlatform *p);
void hc_nav_reset(void);                                         // the start state

// ---- the arm of a destructive row action (T192 D21) ------------------------------------------------------------------------------------------
// The keys that take two presses (DEL DEL in the editor and the browser, F F on the WiFi page): the first arms (the row, a note), the second confirms; any other key, a page
// change or a reload of the list ends it. Every arm, from the board or from the API, has a token; the service learns how an arm ended through the platform's arm_end.
enum { AR_NONE = 0, AR_ENTRY, AR_FILE, AR_WIFI };
struct HcArm {
    int act;                                // AR_*
    uint32_t token;
    int row;
    char name[96];
    char note[HC_NOTE_MAX];
};
const HcArm &hc_arm(void);
void hc_arm_register(int act, int row, const char *name, const char *note);   // arms: AR_ENTRY / AR_FILE remember the row, AR_WIFI the F
void hc_arm_drop(int how);                 // the arm ends (not by a key: see HcKeyArm)
void hc_arm_quiet(void);                   // the arm is over and nobody is told (the service confirmed or dropped it itself)
int hc_arm_take_row(void);                 // the row the previous key armed (-1 none); the memory is cleared
int hc_arm_take_wifi(void);                // F pressed once on the WiFi page (1) and cleared
int hc_arm_row(void);
void hc_arm_clear_keys(void);              // no armed row, no F
// One key meets the arm: the confirming key answers yes (yes()), every other key ends the arm as no when it came from the board, as gone otherwise.
class HcKeyArm {
public:
    HcKeyArm();
    ~HcKeyArm();
    bool yes();
private:
    uint32_t tok;
    HcKeyArm *prev;
};
bool hc_arm_yes(void);                     // only the board's own key says yes; a queued remote key ends the arm as gone

// ---- the feedback lines --------------------------------------------------------------------------------------------------------------------
void hc_note_set(const char *text, int tone);                    // HcTone
void hc_note_clear(void);
void hc_nnote_set(const char *text, int tone);
void hc_nnote_clear(void);

// ---- the intents that move the person --------------------------------------------------------------------------------------------------------
// go(): the view changes. The pages' sub-modes end with it, a browser gets read, the cursor is brought into the list. (The screen shows the view after it.)
void hc_nav_go(int view);
enum { HCN_NONE = 0, HCN_DONE = 1, HCN_SELMOVE = 2 };           // bits of hc_nav_act's result
// The cursor keys of a context: the menu, the editor, the browser in its three modes. HCN_NONE: the intent is not one of the context's movements and the screen goes on with it.
// HCN_SELMOVE: only the selection moved (two rows to restyle, no rebuild).
int hc_nav_act(int ctx, int act);
// The settings page (n visible slots) and the WiFi page.
bool hc_nav_settings_act(int act, int n);                        // UP / DOWN; the slot is brought into 0..n-1 first
void hc_nav_clamp_ssel(int n);
bool hc_nav_wifi_act(int act, int rows);                         // UP / DOWN
void hc_nav_pm_open(void);                                       // T187 D5: the Picture page opens over the settings rows (the caller is in HCV_SET)
void hc_nav_pm_close(void);
void hc_nav_wifi_open(void);                                     // the WiFi page opens over the settings rows
void hc_nav_wifi_close(void);                                    // ESC on it
void hc_nav_clamp_nsel(int rows);
void hc_nav_colors_open(void);                                   // the colour test (F7 page)
void hc_nav_colors_pattern(char c);
// The cursors set from outside (an intent that names a row, a start state, a change of the list).
void hc_nav_set_sel(int i);
void hc_nav_set_ssel(int i);
void hc_nav_set_nsel(int i);
void hc_nav_sel_default(void);                                   // the cursor stands on the default entry (the start, a late boot.cfg)
void hc_nav_sel_clamp(int total);                                // the list shrank (or the cursor stands outside it)
void hc_nav_sel_step(int d);                                     // an entry moved one row: the cursor goes with it
void hc_nav_sel_after_remove(void);                              // an entry was removed: the cursor stays inside the list
void hc_nav_focus(int src, int i);                               // 0 entry (the window of the view that is up follows), 1 file, 2 cores line
// The windows follow the cursor; the number is the first visible row.
int hc_nav_menu_first(int total);
int hc_nav_edit_first(int total);
int hc_nav_browse_first(int rows);                              // the browser's window follows its cursor (T209 D3); rows = the rows the panel shows
bool hc_nav_window_follow(void);                                 // after a key: the window of the menu / editor follows the cursor; true when it scrolled

// ---- the browser -----------------------------------------------------------------------------------------------------------------------------
void hc_browse_load(void);                                       // reads the folder anew; the cursor stays inside the list
void hc_browse_up(void);                                         // the parent folder, cursor on row 0
void hc_browse_enter(void);                                      // the folder under the cursor (".." goes up), cursor on row 0
bool hc_browse_goto(const char *path, char *why, int cap);       // an absolute path (the API's); false and the reason when it is not there
void hc_browse_set_path(const char *path);                       // the folder, read at the next load
void hc_nav_set_bsel(int i);
void hc_nav_bsel_clamp(void);                                    // inside the list
bool hc_browse_at_media(void);                                   // the folder is "/" (the media level)
void hc_nav_set_br_mode(int mode);

// The Cores and ROMs page's state is the core's (host_core_cm.cc); these two are the page's cursor and mode for it.
void hc_nav_set_cm(int mode, int top);

// The bench's way to a state (hostui_test_*): the view and the cursors set as they are.
void hc_nav_test_view(int view, int sel, int bsel);
void hc_nav_test_init(void);                                   // the cursor on the default entry, the windows at the top, no note, settings row 0, the browser ordinary, the menu view

#endif
