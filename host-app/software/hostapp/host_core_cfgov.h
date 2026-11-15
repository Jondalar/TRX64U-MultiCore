// The config overlay of the host (T198 D2/D9/D11/D13/D14, S2-S6, S9, S12, S15): RIGHT on a core row of the boot menu or on a .cor in the File Browser opens a modal list
// "<core> - start with": "Plain start" first, then the core's configs (<core folder>/configs/*.cfg, and media/*.mnt of before T198 through configlist.h). The state is here;
// the screen draws it as the pick list it already has (hc_cfgov_pick) and the keys come through the key map (context HCX_CFG). No LVGL, no FreeRTOS. The host lists,
// starts, renames, deletes and marks the default; it never creates a config (spec section 4).
#ifndef HOST_HOSTCORE_CFGOV_H
#define HOST_HOSTCORE_CFGOV_H

#include "configlist.h"
#include "coremaint.h"
#include "fileops.h"

#define HC_CFGOV_ROWS 8                                  // D11: rows visible in the overlay, the rest scrolls

struct HcCfgOv {
    bool open;
    bool browse;                                         // opened from the File Browser: the sub text names the file that RETURN starts
    int entry;                                           // the boot.cfg entry of the core, -1 when the core is not in the boot menu
    char path[200];                                      // the .cor
    char dir[200];                                       // its core folder
    char title[96];
    ConfigList l;
    char def[CONFIG_NAME_MAX + 1];                       // the default config of the entry ("" = none)
    int cur;                                             // 0 = "Plain start", 1..l.n = config cur-1, l.n + 1 = "Boot safe graphics mode" (T224 D1)
    int top;                                             // first visible row
    int ask;                                             // 1: delete is armed (the second DEL deletes)
    char msg[96];
    int msgtone;
};

// The directory listing and the file operations: the board's (hostfs_cm / hostfs_fo) or a bench's.
void hc_cfgov_io(CmFs *cm, FoFs *fo);

// The number of configs of a core (the badge, `configs: n` of the status). One directory read per generation and path (S15); hc_cfgov_forget() starts a new generation.
int hc_cfgov_count(const char *cor_path);
void hc_cfgov_forget(void);

// Opens the overlay for a core. T224 D1: it opens with no config too (the last row is the rescue start); the caller keeps it from full-firmware cores. `entry`: its boot.cfg index or -1.
bool hc_cfgov_open(const char *cor_path, const char *title, int entry, bool browse = false);
void hc_cfgov_close(void);
const HcCfgOv &hc_cfgov(void);
bool hc_cfgov_is_open(void);

void hc_cfgov_move(int dir);                              // UP / DOWN; the window follows
const char *hc_cfgov_cur_name(void);                     // "" on "Plain start" and on the safe row
bool hc_cfgov_on_safe(void);                             // T224 D1: the cursor is on the last row, "Boot safe graphics mode"
// D: the config under the cursor becomes the default of the entry (again: no default); on "Plain start": no default. HCR_*-like: true when changed.
bool hc_cfgov_default(void);
// DEL: the first call arms, the second deletes the config under the cursor. Returns 1 armed, 2 deleted, 0 nothing (Plain start, or it failed: msg says why).
int hc_cfgov_delete(void);
// The rename of the config under the cursor; false and msg when it cannot be done.
bool hc_cfgov_rename(const char *to);
void hc_cfgov_disarm(void);

// The overlay as the screen's pick list (the rows, the window, the keys line).
struct HcWifiPick;
void hc_cfgov_pick(HcWifiPick *p);

// F5 (D13): the entry's default config stepped through [none, config 1..n] (dir -1/+1, wrapping); `cur` is the present value. Writes the step to `out` ("" = none).
// False when the core has no configs.
bool hc_cfgov_exists(const char *cor_path, const char *name);   // T198 S13: the core has a config of that name (configs/ or the legacy media/)
bool hc_cfgov_cycle(const char *cor_path, const char *cur, int dir, char *out, int cap);

// The text of the badge on a core row: "" for none, else "3 configs" / "1 config".
void hc_cfgov_badge(int n, char *out, int cap);

#endif
