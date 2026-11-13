// The host application's headless core (T192.2 P1, T192 D23): what the host's screen is a client of. No LVGL, no FreeRTOS, no trx_*, no key waits.
//
// The key map is ONE table (host_core.cc, KEYMAP): (context, key) -> intent. The board's keys, a remote `key` intent and a declared action all end in an intent
// of this file's enum; hostui.cc runs the intent (on_key) and draws what the state says. The footer legends of the screen are generated from the same table
// (hc_foot), so a key that moves in the table moves in the legend.
#ifndef HOST_HOSTCORE_H
#define HOST_HOSTCORE_H

#include <stdint.h>
#include "bootcfg.h"
#include "startcheck.h"
#include "hostvideo.h"
#include "coreboot.h"
#include "cor.h"
#include "cfw.h"

// ---- the contexts: which part of the keymap the screen takes a key with -------------------------------------------------------------------
enum HcCtx {
    HCX_PICTURE = 0,       // a picture change waits for a key: the first key of any view is spent on it (RETURN or any key keeps it, LEFT / ESC goes back)
    HCX_MENU,              // F1
    HCX_BROWSE,            // F3, normal mode
    HCX_BROWSE_PICK,       // F3, the copy / move target pick
    HCX_BROWSE_INSPECT,    // F3, the pick of a .cfw/.ue2 to look into
    HCX_BROWSE_ASK,        // F3, "replace the file that is there?"
    HCX_EDIT,              // F5
    HCX_SET,               // F8
    HCX_WIFI,              // F8, the WiFi page
    HCX_PICMODES,          // F8, the Picture page (T187 D5): the modes the screen names
    HCX_CORES,             // F8, Cores and ROMs, the list
    HCX_CORES_ASK1,        // ... the first question
    HCX_CORES_ASK2,        // ... the second question
    HCX_NET,               // F7
    HCX_COLORS,            // the colour test
    HCX_CFG,               // T198: the config overlay over F1 or F3
    HCX_N,
    HCX_GLOBAL = HCX_N     // the keys of every context (the F keys, V); consulted first (not in HCX_PICTURE)
};

// ---- the intents -------------------------------------------------------------------------------------------------------------------------
// The names are the declared actions' names (hostdecl.cc) where there is one; the others are the keys of the pages that have no row to name (the WiFi page's
// cursor, the colour test's patterns).
#define HC_ACT_LIST(X) \
    X(NONE, "") \
    X(NAV_MENU, "nav.menu") X(NAV_BROWSE, "nav.browse") X(NAV_EDIT, "nav.edit") X(NAV_NET, "nav.net") X(NAV_SETTINGS, "nav.settings") \
    X(PICTURE_STEP, "picture.step") X(PICTURE_KEEP, "picture.keep") X(PICTURE_REVERT, "picture.revert") X(PM_APPLY, "picture.apply") \
    X(CURSOR_UP, "cursor.up") X(CURSOR_DOWN, "cursor.down") X(MENU_PAGE_DOWN, "menu.page_down") X(MENU_PAGE_UP, "menu.page_up") X(BACK, "back") \
    X(ENTRY_START, "entry.start") X(ENTRY_RECHECK, "entry.recheck") X(ENTRY_MOVE_UP, "entry.move_up") X(ENTRY_MOVE_DOWN, "entry.move_down") \
    X(ENTRY_DEFAULT, "entry.default") X(ENTRY_REMOVE, "entry.remove") X(ENTRY_RENAME, "entry.rename") X(ENTRY_DEFCFG_PREV, "entry.config_prev") X(ENTRY_DEFCFG_NEXT, "entry.config_next") \
    X(BROWSE_OPEN, "browse.open") X(BROWSE_ENTER, "browse.enter") X(BROWSE_UP, "browse.up") X(BROWSE_PAGE_DOWN, "browse.page_down") X(BROWSE_PAGE_UP, "browse.page_up") \
    X(BROWSE_ADD, "browse.add") X(BROWSE_START, "browse.start") X(BROWSE_COPY, "browse.copy") X(BROWSE_MOVE, "browse.move") X(BROWSE_DELETE, "browse.delete") \
    X(BROWSE_CONVERT, "browse.convert") X(BROWSE_RESTORE, "browse.restore") X(BROWSE_UPDATE, "browse.update") X(BROWSE_FORMAT, "browse.format") X(BROWSE_UNZIP, "browse.unzip") \
    X(BROWSE_PICK_CANCEL, "browse.pick_cancel") X(BROWSE_REPLACE, "browse.replace") X(BROWSE_REPLACE_CANCEL, "browse.replace_cancel") \
    X(SET_DEC, "set.dec") X(SET_INC, "set.inc") X(SET_ACTIVATE, "set.activate") \
    X(WIFI_DEC, "wifi.dec") X(WIFI_INC, "wifi.inc") X(WIFI_ACTIVATE, "wifi.activate") X(WIFI_SCAN, "wifi.scan") X(WIFI_MANUAL, "wifi.manual") \
    X(WIFI_LAST, "wifi.last") X(WIFI_DISCONNECT, "wifi.disconnect") X(WIFI_FORGET, "wifi.forget") \
    X(CORES_DELETE, "cores.delete") X(CORES_YES, "cores.yes") X(CORES_NO, "cores.no") \
    X(NET_COLORS, "net.colors") \
    X(CONFIG_LIST, "config.list") X(CONFIG_START, "config.start") X(CONFIG_RENAME, "config.rename") X(CONFIG_DELETE, "config.delete") X(CONFIG_DEFAULT, "config.default") \
    X(WEB_START, "web.start") X(WEB_DEFAULT, "web.default") X(WEB_VIDEO, "web.video") X(WEB_COUNTDOWN, "web.countdown") \
    X(COLORS_REPORT, "colors.report") X(COLORS_SINGLE, "colors.single") X(COLORS_MULTI, "colors.multi") X(COLORS_PATTERN, "colors.pattern") X(COLORS_CLOSE, "colors.close")

enum HcAct {
#define HC_X(e, n) HCA_##e,
    HC_ACT_LIST(HC_X)
#undef HC_X
    HCA_N
};

const char *hc_act_name(int act);              // "entry.start"; "" for HCA_NONE
int hc_act_by_name(const char *name);          // HCA_NONE when there is none

// ---- the key map -------------------------------------------------------------------------------------------------------------------------
#define HC_KEY_ANY (-2)                         // any key that no entry before it in the context names
#define HC_KEY_NONE (-3)                        // no key (-3 and below, one each): an intent only the service queues (the old web page's commands, web.*)
// An intent travels in the key queue as a value no key has (the service queues intents and keys alike; on_key takes both).
#define HC_ACT_KEY_BASE 0x20000
#define HC_ACT_KEY(a) (HC_ACT_KEY_BASE + (a))
#define HC_IS_ACT_KEY(k) ((k) >= HC_ACT_KEY_BASE && (k) < HC_ACT_KEY_BASE + HCA_N)
#define HC_ACT_OF_KEY(k) ((k) - HC_ACT_KEY_BASE)

struct HcKey {
    uint8_t ctx;                               // HcCtx or HCX_GLOBAL
    int key;                                   // a KEY_ code, a character (a letter in both cases is two entries), or HC_KEY_ANY
    uint16_t act;                              // HcAct
};
const HcKey *hc_keymap(int *n);
// The intent a key takes in a context: the global entries first (not in HCX_PICTURE), then the context's own, the first match wins; HCA_NONE when there is none.
int hc_resolve(int ctx, int key);
// Is `act` bound to some key in `ctx` (or globally)? An intent that no key reaches there does nothing there.
bool hc_act_in_ctx(int ctx, int act);
// The intent "back" is whatever the escape key does on the page: HCA_NONE when it does nothing there.
int hc_back_act(int ctx);
// The name the legends give the first key bound to `act` in `ctx` (global entries too): "RETURN", "UP", "A", "F1", "+". Empty when there is none.
void hc_key_name(int ctx, int act, char *out, int cap);

// ---- the legends -------------------------------------------------------------------------------------------------------------------------
// The footer of each screen, generated: "UP/DOWN select    RETURN start    ...". The key names come from the key map; the words from the table in host_core.cc.
enum HcFoot {
    HCF_MENU = 0, HCF_BROWSE,                  // HCF_BROWSE: a boot-menu file (the longest of the browser's)
    HCF_BR_MEDIA, HCF_BR_PLAIN, HCF_BR_FILE, HCF_BR_DIR, HCF_BR_PICK_COPY, HCF_BR_PICK_MOVE, HCF_BR_ASK, HCF_BR_BACKUP, HCF_BR_TXU, HCF_BR_INSPECT, HCF_BR_CFW,
    HCF_EDIT, HCF_NET, HCF_SET, HCF_CM_LIST, HCF_CM_ASK1, HCF_CM_ASK2, HCF_WM, HCF_CM_EMPTY, HCF_PM, HCF_CFG, HCF_BR_ZIP,
    HCF_MENU_PLAIN, HCF_BROWSE_PLAIN, HCF_CFG_PLAIN, HCF_EDIT_PLAIN,          // T198 D9: the same without "configs", for a selected row that has none
    HCF_N
};
const char *hc_foot(int foot);                 // generated once, kept
// The footer of a view of the navigation (V_MENU .. V_SET as 0..4).
const char *hc_foot_of_view(int view);
// "F1" .. "F8": the navigation key of a view, from the key map.
const char *hc_nav_key(int view);


// ---- the boot menu: boot.cfg and the settings it holds --------------------------------------------------------------------------------------------
// The state of the boot menu lives here and nowhere else (T192.2 P1). The screen reads it through hc_cfg() (const) and changes it only through the commands
// below; each command is the old call path of hostui.cc (bootcfg_*, then the save), with the same result. Pure C++11: the file comes through HcPlatform.
struct HcPlatform {
    int (*cfg_load)(char *buf, int cap);                 // Flash:/boot.cfg: its length, -1 when it cannot be read (the flash disk may not be mounted yet)
    bool (*cfg_save)(const char *text, int n);
    void (*cfg_changed)(void);                           // the entries or the default are about to be saved: whatever the screen armed on a row does not hold any more
    StartFs *fs;                                         // the file system the entries are checked on (stat, open); the core never touches a file but through it
    void (*log)(const char *line);                       // the console line ("[host] ..."), one per call, no newline; null: none
    uint32_t (*now_ms)(void);
};
void hc_init(const HcPlatform *p);
StartFs *hc_fs(void);                                  // the file system the entries are checked on (null: none given)
uint32_t hc_now_ms(void);                              // the platform clock (0 when none is given)

enum HcResult { HCR_OK = 0, HCR_REFUSED, HCR_SAVE_FAILED };     // HCR_SAVE_FAILED: the change is made in memory, boot.cfg could not be written

const BootCfg &hc_cfg(void);
bool hc_cfg_loaded(void);                                // boot.cfg was read
bool hc_cfg_edited(void);                                // the person changed the menu: a late boot.cfg must not overwrite it
void hc_cfg_clear(void);                                 // the start state: empty, not loaded
void hc_cfg_reset(const char *text);                     // the start state of a bench: empty, then `text` (null: nothing), loaded
bool hc_cfg_try_load(int *save = 0);                     // reads boot.cfg; true when it was read. The old `video=` moves to host.cfg once (then boot.cfg is written: *save = HCR_OK / HCR_SAVE_FAILED, else HCR_REFUSED).
int hc_cfg_save(void);                                   // writes boot.cfg (HCR_OK / HCR_SAVE_FAILED); marks the menu as edited by the person
bool hc_cfg_readback_ok(int i);                          // boot.cfg as the board holds it now has as many entries as the menu and the same name at i

int hc_entry_move(int i, int dir);                       // dir -1 up, +1 down; the default follows
int hc_entry_default(int i);
int hc_entry_defcfg(int i, const char *name);           // T198 D8: the entry's default config ("" = none); HCR_REFUSED for a name config_name_ok refuses
void hc_start_config_set(const char *name);             // T198: the config the next hc_start_sequence hands over (one-shot, "" = plain)
int hc_entry_remove(int i);
int hc_entry_rename(int i, const char *name);            // HCR_REFUSED: the name cannot be used (nothing changed)
int hc_entry_add(const char *name, const char *path, const char *hash, unsigned size, unsigned mtime, unsigned hdr);   // HCR_REFUSED: full, or a bad name
int hc_entry_migrate(int i, StartFs &fs, bool fw);       // the full check of an old entry stored once (HCR_REFUSED: nothing stored); the host's own rewrite, not an edit by the person
int hc_direct_toggle(void);                              // direct boot on/off
int hc_autostart(bool on, int secs);                     // autostart on/off; secs 1..60 (0: keep), clamped by bootcfg_autostart

// ---- the settings that live in modules ----------------------------------------------------------------------------------------------------------
// The picture (hostvideo.cc), the flash lock (flashlock.cc) and the FTP switch (ftpstate.h): their state is the modules'; the screen changes it here only.
bool hc_picture_set(int hdmi, char *why, int why_n);     // a change by the person: false and `why` when the screen does not name it
void hc_picture_init(const HostVideoIo *io);             // probes the host once (the bench brings its own hooks)
bool hc_picture_load_default(int boot_sys, int boot_hdmi);   // the stored choice (host.cfg); boot.cfg's old `video=` moves over once (true when it moved)
void hc_picture_start(bool use_saved, int *sys, int *hdmi);   // the start sequence of the picture: reads the screen, picks the default, applies it
HvEvent hc_picture_tick(void);                           // the loop's side of the picture: the hot-plug follow and the revert timer
int hc_picture_sys(void);                                // what the picture is set to now: the mirror the screen and the rows read (hc_picture_start and hc_picture_sync set it)
int hc_picture_hdmi(void);
void hc_picture_sync(void);                              // the mirror follows hostvideo's state
void hc_picture_mirror_test(int sys, int hdmi);          // the bench's way to a state
bool hc_picture_keep(void);                              // any input during the question: keeps the mode and stores it; false when there was no question
void hc_protect_set(bool on);
void hc_ftp_set(bool on);


// ---- the start sequence (T192.2 P3, D13) ---------------------------------------------------------------------------------------------------------
// ONE sequence starts a core or a firmware, for the menu (RETURN, S, the countdown, the old web page) and for the direct boot. The order is what the screen's
// start and the direct start did, kept:
//   core      stage -> the check after the stage -> swap (the ICAP) -> [menu: the last steps, a pause, the last key check] -> jump
//   firmware  stage -> [menu: the last steps] -> the check -> [menu: a pause, the last key check] -> lock the flash -> jump
// The flash is locked for a firmware (a .cfw or a large .cor) and never for a partial core (D13): `full` says which. A start while one runs is refused.
enum HcStartMode { HC_START_MENU = 0, HC_START_DIRECT };
struct HcStartEntry {
    const char *path;
    bool full;                      // a .cfw or a large .cor: the full-firmware way, with the flash lock
    const BootEntry *trusted;       // a core: the boot.cfg entry whose stored check stands in for hashing the parts (null: hash everything)
    bool verify;                    // a firmware started once (not from the menu): the image is checked and compared with the flash
    const char *config;             // T198: a core started with this config (""/null = plain start); handed over in the hand-over page
};
struct HcStartHooks {
    core_progress_t progress;                   // stage and swap report here; non-zero asks to abort. The menu paints the steps and polls RUN/STOP, a direct start samples the held keys
    bool (*abort_key)(void *ctx);               // the last key check of the menu, after the pause
    void (*pause_ms)(void *ctx, int ms);        // the menu waits 200 ms before it (the screen shows the last step)
    void (*settle)(void *ctx);                  // the menu draws the last steps once
    void (*lap)(void *ctx, const char *what);   // null `what`: start the stopwatch; else log "<what> in N ms"
    void (*before_jump)(void *ctx, bool full);  // a direct start puts its overlay text away
    void *ctx;
};
struct HcStartOutcome {
    bool staged;                    // the file was read and accepted (a failure after it is not the entry's fault)
};
#define HC_START_BUSY "another start is under way"
// Returns only when nothing was started: the reason (CORE_ABORTED, CORE_LOADED_NOT_STARTED, the module's words), or null with no word. A start jumps and does not return.
const char *hc_start_sequence(const HcStartEntry &e, int mode, const HcStartHooks &h, HcStartOutcome *out);
bool hc_start_running(void);
// T152 D4/D6: before the handover to a firmware the flash is protected, read back, and the start is refused when it did not take (off sets nothing).
bool hc_lock_flash(const char **why);


// ---- the state of the boot menu's entries ----------------------------------------------------------------------------------------------------------
// What is read per entry, and when (T101 S10). Not at the start: the menu comes up from boot.cfg alone and an entry is read afterwards, one unit per pass of
// the loop (hc_lazy_step). The state is the core's; the icon (a picture) is the screen's cache: the core says when the icon of an entry is due.
enum EState { ES_UNKNOWN, ES_OK, ES_CHANGED, ES_MISSING, ES_BAD, ES_CONTRACT, ES_HOST, ES_FWBAD };
EState hc_est(int i);
const CorInfo &hc_einfo(int i);
bool hc_efull(int i);                                    // T115: the entry is a .cfw full firmware or a large .cor, not a partial bundle
void hc_entry_origin(int i, int origin);                 // a large .cor's icon record names who made it (read with the icon)
bool hc_startable(int i);                                // an entry the lazy reader has not reached yet starts too: the start verifies and refuses by itself
int hc_in_menu(const char *path);                        // the index of the entry with that path, -1
bool hc_is_full(const char *path);                       // a .cfw, or a .cor that is a LARGE core (the full-firmware way)
void hc_probe_state(int i);                              // (a) size and mtime of the directory entry, (b) the package header: the state. No part is hashed, no medium is listed.
void hc_lazy_reset(void);                                // every entry is unknown again (a move, a removal, a late boot.cfg)
void hc_lazy_mark(int i);                                // entry i is read again, its old state stays until the new one is in
int hc_lazy_next(int sel_hint);                          // the entry the reader would take next (the default first, then `sel_hint`), -1
enum { HCL_STATE = 1, HCL_ICON, HCL_MIGRATE };
struct HcLazy {
    int index;
    int unit;                                            // HCL_STATE: the state of `index` was read; HCL_ICON: its icon is due, the screen loads it and calls hc_lazy_icon_done;
                                                         // HCL_MIGRATE: an old entry was checked in full once (save: HCR_OK / HCR_SAVE_FAILED, HCR_REFUSED: nothing stored)
    bool icon_next;                                      // HCL_STATE: the icon is due next
    int save;
};
bool hc_lazy_step(bool quiet, int sel_hint, HcLazy *out);   // one unit, or (last, when quiet) one migration; false when there is nothing to do
void hc_lazy_icon_done(int i);


// ---- the menu's auto-start countdown (boot.cfg countdown=) -------------------------------------------------------------------------------------------
// It begins once the default entry can be started, a key (or any other input) stops it for good, and it is given up when the countdown is 0, there is no default,
// or the default's medium did not come in BOOT_WAIT_MS. The loop calls hc_countdown_tick; what the screen does with the result is its own.
#define HC_BOOT_WAIT_MS 30000                            // how long a missing default may be waited for before the countdown gives up
enum { HCC_BEGAN = 1, HCC_GAVE_UP = 2, HCC_EXPIRED = 4 };
void hc_countdown_boot(uint32_t now);                    // the menu's start-up began (the wait for a missing default counts from here)
int hc_countdown_tick(uint32_t now);                     // HCC_* bits; HCC_EXPIRED: the default is to be started now (the countdown is spent)
void hc_countdown_stop(void);                            // any input: says so when it ran or could still run
void hc_countdown_give_up(void);                         // dead without a word (the direct boot has tried, or said why not)
bool hc_cd_running(void);
bool hc_cd_dead(void);                                   // stopped by a key or given up
int hc_cd_left_ms(uint32_t now);                         // while it runs: what is left (may be 0 or less)
void hc_cd_set(bool run, bool dead, uint32_t end_ms);    // the bench's way to a state

#endif
