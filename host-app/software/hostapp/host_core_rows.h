// What each page of the host's screen shows, as data (T192.2 P4, T192 D23, T192.2-S9): the rows, lines and cells of every page with their tones, produced from the core's state
// (boot.cfg, the entries' states, the navigation, the board's readings, the modules). The screen DRAWS these (it lays them out, colours them by tone, hides what is not due); it
// formats no text of its own, and the service lists the same rows. One table names the pages (HC_PAGES) and `hc_rows_dump` prints every page's rows headless: a bench compares
// the dump with the page list of the declaration and with what the screen drew. No LVGL, no FreeRTOS.
#ifndef HOST_HOSTCORE_ROWS_H
#define HOST_HOSTCORE_ROWS_H

#include <stdint.h>
#include "host_core.h"
#include "host_core_nav.h"
#include "host_core_flow.h"
#include "host_core_flow_wifi.h"
#include "wifinet.h"

// ---- what the board reads (the header chips, the network page, the WiFi page) ---------------------------------------------------------------------
// Read by the screen's loop (a listing of the mount points and the interface list cost real time) and handed over; the WiFi flows refresh the WiFi part themselves.
struct HcStatus {
    bool usb, sd, net, enet, lan;                 // net: any interface has an address (the header chip); enet: the cable's; lan: the chip names the cable (T184)
    char ip[24], eip[24], media[96], chip_txt[48];
    WifiNetStatus wifi;
    WifiNetCfg wcfg;
    bool wcfg_ok;
};
HcStatus &hc_status(void);
// What the rows need from the board's side: the REST route's line on the network page (the service's hostapi_rest_text), the WiFi page's note (the screen prints it to the
// console and tells the service) and the app's version for its chip.
struct HcRowsPlatform {
    void (*rest_text)(bool net, char *out, int n);
    void (*net_note)(const char *text, int tone);
    const char *app_version;
    const char *host_version;       // T221: the host gateware SemVer (the "host" chip); a test build ends in -dev+<sha8>
};
void hc_rows_init(const HcRowsPlatform *p);
const char *hc_item_title(const char *page, const char *id);     // the declaration's label of a fixed row
const char *hc_page_title(const char *id);                        // ... and of a page
const char *hc_nav_title(int view);                               // the name of a view in the navigation (V_MENU .. V_SET as 0..4)
// The canvas that is up is the compact one (the settings value of the picture is shorter there).
void hc_rows_set_compact(bool compact);

// ---- a cell: a text the screen may write, and the tone to colour it with ----------------------------------------------------------------------------
struct HcCell {
    bool set;                                     // the text is to be written (false: the label keeps what it had)
    uint8_t tone;                                 // HcTone; HCT_NONE: the colour is not to be written
    char text[160];
};

// ---- the boot menu entries (menu and editor) -----------------------------------------------------------------------------------------------------
enum { HCP_OK, HCP_CHECK, HCP_BAD };
struct HcEntryRow {
    char name[96];
    char tile[8];                                 // what the icon tile says while the row has no picture: the core's short name, "FW", or the first four letters and digits of the name
    char ver[24];                                 // the menu row's right-aligned version: "v1.0", "full firmware", "" while unknown
    char sub[140];                                // the path
    char badge[32];
    uint8_t badge_tone;
    uint8_t pic;                                  // T209 D10: the boot menu pictogram: HCP_OK (usable), HCP_CHECK (check with R), HCP_BAD (not usable)
    bool def;
    bool full;
    char ctype;                                   // T223 D7: the core-type badge letter: 'L' large, 'P' partial, 0 none (a .cfw, an entry not read)
    bool ok_state;                                // the entry's state is OK or CHANGED (an icon may be read)
    int configs;                                  // T198: the core's configs (menu rows of a .cor; 0 = no badge, D9)
    char cfgmark[8];                              // T223 D6: the boot menu's config column: "C" (or "config" with HC_CFG_MARKER_LONG) on a row that has configs, "" for none
    char cfgbadge[16];                            // "3 configs", "" for none
};
bool hc_row_entry(int i, bool menu_style, HcEntryRow *r);
void hc_entry_ver(int i, char *out, size_t n);          // the displayed version of entry i (T209 D16), empty for none

// The menu page: the line under the title, the hint while there are no entries, the position, the countdown line and its bar.
#define HC_NO_CORES_HINT "No cores yet. Press F3, open a folder on SD or USB\nand press A on a .cor bundle or a .cfw firmware."
struct HcMenuTexts {
    char sub[96];
    bool hint_on;
    char hint[160];
    char pos[24];
};
void hc_menu_texts(int first, int total, HcMenuTexts *t);
struct HcCountdownTexts {
    bool visible;                                 // direct boot on: the line says nothing useful and is hidden
    char text[96];
    int pct;
};
void hc_countdown_texts(HcCountdownTexts *t);
// The editor page's info box (T209 D19: two lines) and the position. Countdown and direct boot are F8 settings, not here.
struct HcEditTexts {
    char autol[200];                              // "Auto: <auto core> (<its auto config>)"; "Auto: (none)" with no auto core; no parenthesis without a config
    char menu[40];                                // "Menu: /Flash/boot.cfg"
    char pos[24];
};
void hc_edit_texts(int first, HcEditTexts *t);
void hc_pos_text(char *out, int n, int first, int total, int rows);   // "a-b of n" for a list that does not fit its window, empty otherwise

// ---- the File Browser --------------------------------------------------------------------------------------------------------------------------------
struct HcBrowseRow {
    char mark[2];
    char name[56];
    char size[24];
};
void hc_browse_title(char *out, int n);           // the folder, or "Copy to: <folder>" while a target is picked
void hc_browse_empty_text(char *out, int n);      // "", "(empty)" or "No media found"
bool hc_browse_row(int k, HcBrowseRow *r);
const char *hc_edit_foot(void);                   // the editor footer for its selected row: "LEFT/RIGHT config of this core" only while that core has configs
const char *hc_menu_foot(void);                   // the boot menu's footer for its selected row (T198 D9); hc_foot_of_view(0) is the general one
const char *hc_browse_foot(void);                 // the footer for what is selected (T111)
bool hc_browse_sel_file(char *path, int n);       // the selected row is a file: its path
bool hc_browse_sel_bundle(char *path, int n);     // ... and the boot menu takes it (.cor, .cfw)
bool hc_is_backup_cor(const char *path);
bool hc_esp_pickable(const char *name);
int hc_boot_rows_on(const char *name);           // boot menu rows whose file lives on the medium `name` (T111)
// The right panel: the manifest of the selected bundle or firmware, the medium under the selection, the update, the note. Probes the file at most once per selection
// (the cache is dropped when the folder is read anew); what it did not decide is not written (set false), as before the cut.
struct HcPanel {
    int keys_mode;                                // 0: the manifest's key column, 1: the medium's
    bool kv_vis[7];
    bool msg_vis, err_vis;
    HcCell title, val[7], msg, err;
};
void hc_browse_panel(HcPanel *p);
const char *hc_panel_key(int mode, int i);
// What the browser's selection is as a bundle (the panel's probe): for the start and the add, and for the API's detail line.
struct HcBrowseProbe {
    bool full;                                    // a .cfw or a large .cor
    int res;                                      // cfw_result_t / cor_result_t
    const CorInfo *ci;
    const CfwInfo *cw;
    bool probed;                                  // the panel has probed this row
};
void hc_browse_probe(HcBrowseProbe *p);           // probes now when it has not
bool hc_browse_probed_cor(const CorInfo **ci);    // the panel probed the selected row and it is a good .cor
const CorInfo &hc_browse_ci(void);
int hc_browse_ci_res(void);
const CfwInfo &hc_browse_cw(void);
int hc_browse_cw_res(void);

// ---- the network page and the WiFi page ---------------------------------------------------------------------------------------------------------
struct HcNetTexts {
    HcCell eth, ftp, media, rest, wst, wnet, wip, wmac, note;
};
void hc_net_texts(HcNetTexts *t);                 // also ends the WiFi action that was under way (its result is the note)
struct HcWifiTexts {
    HcCell a, b, c;                               // the three state lines
    HcCell val[NS_N];                             // the rows' values; tone MUTED when the row is greyed
    bool name_dim[NS_N];
    HcCell note;
};
void hc_wifi_texts(HcWifiTexts *t);
void hc_wifi_texts3(char (&st)[160], const char *&net, const char *&ip);
const char *hc_wifi_net_set_name(int i);          // the label of a row of the WiFi page (the declaration's)
void hc_wifi_op_poll(void);                       // the action that was under way has ended: its result is the page's note

// ---- the settings page ---------------------------------------------------------------------------------------------------------------------------
enum { SK_DIRECT, SK_AUTO, SK_SECS, SK_HDMI, SK_PROTECT, SK_WIFI, SK_FTP, SK_ESP, SK_CORES, SK_REPORT };
#define HC_SET_SLOTS 10
int hc_set_kinds(int *k);                         // the kinds on the page now, in order
const char *hc_set_name(int kind);                // the label of a row (the declaration's)
// The text of one row: its name, its value and its help (the line under the rows when it is selected). One function for the screen and for the service. with_help: the help of the flash
// lock asks the chip, so the screen asks for it only for the selected row.
void hc_set_row_text(int kind, const char *&name, char (&v)[64], const char *&help, bool with_help);
bool hc_set_toggle_state(int kind);
const char *hc_set_key_name(int kind);            // "settings.direct" ...
int hc_set_kind_of(const char *key);
int hc_wifi_row_of(const char *key);
const char *hc_wifi_field_name(int row);
void hc_set_report_name(const char *name);        // the file the last bug report wrote, shown on the row

// ---- the header chips ----------------------------------------------------------------------------------------------------------------------------
struct HcChip {
    char text[48];
    bool ok;
};
int hc_chips(HcChip *out, int cap);               // network, USB, SD, host SemVer (and key on a test build), app: left to right as the screen lays them from the right
void hc_chips_sig(char *out, int n);              // changes when the chips do
const char *hc_chip_net_text(void);

// ---- the colour test ----------------------------------------------------------------------------------------------------------------------------
#define HC_COLORS_TEXT "Colour test (RGB565 from LVGL). r g b k solid, v h stripes, p buffers, x/X 1/2 buffers, Esc returns."

// ---- the pages and the dump ----------------------------------------------------------------------------------------------------------------------
// Every page of the screen, by the declaration's id; `dump` prints its rows as text. hc_rows_dump prints them all, in table order.
struct HcPageDef {
    const char *id;                               // hostdecl.cc's page id ("menu", "settings/wifi", ...)
    int (*dump)(char *out, int cap);
};
extern const HcPageDef HC_PAGES[];
extern const int HC_NPAGES;
int hc_rows_dump(char *out, int cap);

#endif
