#include "hostui.h"
#include "hostdomain.h"
#include "hostdecl.h"
#include "host_core.h"
#include "host_core_cfgov.h"
#include "host_core_flow.h"
#include "host_core_flow_restore.h"
#include "host_core_flow_format.h"
#include "host_core_flow_convert.h"
#include "host_core_flow_esp.h"
#include "host_core_flow_fileops.h"
#include "host_core_flow_wifi.h"
#include "host_core_flow_zip.h"
#include "hostzip_board.h"
#include "host_core_job.h"
#include "host_core_nav.h"
#include "host_core_cm.h"
#include "host_core_rows.h"
#include "menurows.h"
#include "menu_pictos.h"
#include "host_core_ui.h"
#include "host_core_model.h"
#include "bootcfg.h"
#include "fileops.h"
#include "coremaint.h"
#include "lineedit.h"
#include "sha256.h"
#include "coreboot.h"
#include "hostfs.h"
#include "cor.h"
#include "startcheck.h"
#include "cfw.h"
#include "cfwicon.h"
#include "legacy.h"
#include "fwconvert.h"
#include "fwknown.h"
#include "fwrestore.h"
#include "fwconfirm.h"
#include "flashfw.h"
#include "txu.h"
#include "espinspect.h"
#include "espflash.h"
#include "espreboot.h"
#include "flashlock.h"
#include "flash.h"
#include "discover.h"
#include "fmt.h"
#include "fmtconfirm.h"
#include "mediaglue.h"
#include "mediaui.h"
#include "listwin.h"
#include "wifinet.h"
#include "ftpstate.h"
#include "trx_lv_port.h"
#include "trx_io.h"
#include "src/display/lv_display_private.h"
#include "src/core/lv_refr_private.h"
#include "lvgl.h"
#include "trxview.h"
#include "trxcanvas.h"
#include "videomode.h"
#include "hostvideo.h"
#include "picmodes.h"
#include "directboot.h"
#include "menukeys.h"
#include "startfile.h"
#include "ovtext.h"
#include "handover.h"
#include "webui.h"
#include "webnet.h"
#include "hostreport_live.h"
#include "hostreport.h"
#include "keyboard_usb.h"
#include "keyboard_c64.h"
#include "u64.h"
#include "network_interface.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "gitinfo.h"
#include "menusnap.h"
#include "startlog.h"

// ---- palette and fonts: the shared view layer's, so there is one of them (T104 D8) -------------------------------
// They were written twice, here and in the core application, from the same source (trex64-dev.github.io
// assets/site.css, which is also the prototype's globals.xml). app/view/trxview.cc holds them now; these names stay
// so the 1,900 lines below do not change meaning. Sixtyfour is not in the build (PROVENANCE/T84).
#define C_BG       (trx_theme().bg)
#define C_PANEL    (trx_theme().surface)
#define C_PANEL2   (trx_theme().surface2)
#define C_INK      (trx_theme().ink)
#define C_MUTED    (trx_theme().muted)
#define C_LINE     (trx_theme().line)
#define C_HOST     (trx_theme().accent)
#define C_HOSTSOFT (trx_theme().accent_soft)
#define C_OK       (trx_theme().ok)
#define C_WARN     (trx_theme().warn)
#define C_BAD      (trx_theme().bad)
#define F_TITLE   (&lv_font_montserrat_24)
#define F_TEXT    (&lv_font_montserrat_16)
#define F_SMALL   (&lv_font_montserrat_14)

// The canvas is the view's (T164 D10a): 960 x 540, or the mode's own size below 960 wide (the compact layout: a narrower navigation, thinner bars, a footer
// of two lines, fewer rows in the lists). Every page of it is laid out from these measures; nothing is cut.
#define SW trx_canvas_w()
#define SH trx_canvas_h()
#define COMPACT (trx_compact())
#define TOP_H (COMPACT ? 34 : 40)
#define NAV_W (COMPACT ? nav_w_compact() : 170)
#define FOOT_H (COMPACT ? 40 : 30)

// The views' names in the navigation. The compact navigation is as wide as its longest name needs (key box 42, the name, 6 spare) and at least 132: a name is
// never cut to dots, in 800 x 600 as in 480p (T164 D10q).
// The labels of the fixed rows are the declaration's (hostdecl.cc: pages and items): the screen and the API say the same words.
static const char *item_title(const char *page, const char *id)
{
    const HostDecl &d = host_decl();
    for (int i = 0; i < d.nitems; i++) {
        if (!strcmp(d.items[i].page, page) && !strcmp(d.items[i].id, id)) {
            return d.items[i].title;
        }
    }
    return "";
}
static const char *page_title(const char *id)
{
    const HPage *pg = hd_page(host_decl(), id);
    return pg ? pg->title : "";
}
static const char *nav_names_arr[5];
static const char *nav_title(int i)
{
    if (!nav_names_arr[i]) {
        const HPage *pg = hd_page_of_view(host_decl(), i);
        nav_names_arr[i] = pg ? pg->title : "";
    }
    return nav_names_arr[i];
}
static int nav_w_compact(void)
{
    static int w;
    if (!w) {
        int mx = 0;
        for (int ni = 0; ni < 5; ni++) {
            const char *nm = nav_title(ni);
            lv_point_t sz;
            lv_text_get_size(&sz, nm, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            mx = sz.x > mx ? sz.x : mx;
        }
        w = 42 + mx + 6 > 132 ? 42 + mx + 6 : 132;
    }
    return w;
}

enum View { V_MENU = HCV_MENU, V_BROWSE = HCV_BROWSE, V_EDIT = HCV_EDIT, V_NET = HCV_NET, V_SET = HCV_SET, V_COLORS = HCV_COLORS };
#define NAV_N 5          // the views with an entry in the navigation (V_COLORS has none)

#define BR_MAX 200
#define ICON_W 32
#define ICON_TILE_W 48          // the text tile shown while a row has no picture
#define ICON_PX 1024            // the bundle's icon: 32 x 32 RGB565, little endian
#define BOOT_WAIT_MS HC_BOOT_WAIT_MS      // how long a missing default may be waited for before the countdown gives up

// boot.cfg and the settings in it are the core's (host_core.h): the screen reads them here, read only, and changes them through hc_* only.
#define cfg (hc_cfg())
// Where the person is (view, cursors, the browser's folder and rows, the modes of the browser and of the settings pages, the notes) is the core's (host_core_nav.h): the
// screen reads it under these names, changes it through hc_nav_* / hc_browse_* / hc_note_* only (the compiler holds it: they are const; the gate N1 holds the rest).
#define nv (hc_nav())
static inline View cur_view(void) { return (View)nv.view; }
static uint32_t tone_col(int t);
static inline uint32_t note_color(void) { return tone_col(hc_nav().note_tone); }

// T101 D5: icons stay in per-entry buffers, drawn as RGB565 images
static uint16_t icon_c[BOOT_MAX][ICON_PX] __attribute__((aligned(64)));
static bool icon_c_ok[BOOT_MAX];
static int icons_gen;                     // bumped when icon buffers are rewritten, so a row redraws its image
#define MROWS_MAX 12
#define MENU_ROW0 (COMPACT ? 66 : 76)
#define MROWS (mrows_n)                         // rows that fit above the note and the countdown (T101 D4): from the height below it, 9 on the full canvas (8 while the countdown line shows)
static int mrows_n = 5, erows_n = 5;          // T209: the editor's panel above its list takes room: its list is shorter than the menu's
static int mrows_full = 5, mrows_cd = 5;      // menu rows built = the count without the countdown line; mrows_cd = the count while the line shows
#define EROWS (erows_n)
static bool need_full = true;             // the render flag
// T192 D21: the arm of a destructive row action that takes its key twice (DEL DEL in the editor and the browser, F F on the WiFi page) is the core's (host_core_nav.h:
// hc_arm_*, HcKeyArm); the core says what the first key shows (the note) and runs what the second one confirms.
static void arm_drop(int how) { hc_arm_drop(how); }
static bool have_ui;
static bool snap_shown;       // T146: the saved menu picture is on the layer (no splash; LVGL's one buffer is the second)

// countdown
static int cd_last_pct = -1;

// browser
static int rk_mode = -1;         // which labels the right panel's key column shows: 0 the manifest's, 1 the medium's
static bool fmt_active;          // T111: a format page is open or a format runs: no start from anywhere
// T139: copy and move pick their target folder with the browser itself
enum BrMode { BM_NORMAL = HCB_NORMAL, BM_PICK = HCB_PICK, BM_INSPECT = HCB_INSPECT };      // BM_INSPECT (T152 D8): the browser picks a .cfw/.ue2 to look into
// the pick's state (the file, where the browser comes back to, the question "replace?") is the core's: hc_fo_pick_*, hc_fo_replace_*

// The C64U's own keyboard: scanned in the host, read through the matrix registers U64II_KEYB_ROW/COL/JOY by
// Gideon's Keyboard_C64 (the class the Ultimate app's menu uses; it needs a host object that says the matrix
// may be scanned, and nothing else of the C64 side). Its codes are the ones the USB keyboard gives.
class MatrixHost : public GenericHost {
public:
    bool is_accessible(void) { return true; }
};
static MatrixHost matrix_host;
static Keyboard_C64 *matrix_kb;

// Keys without any waiting: USB first, then what the matrix timer scan has queued. (Keyboard_C64::getch()
// sleeps up to 4 ticks per call; draining a queue through it costs 20 ms a key.)
// T211 D1: the host's keys are our own names, F1-F8 are the navigation. Nothing is folded: a real Page Up / Page Down arrives as KEY_PAGEUP / KEY_PAGEDOWN
// (no entry of the key map, so no action until T211 S3 is decided), F1/F3/F5/F7 arrive as KEY_F1/F3/F5/F7 from both keyboards.

// T192 D21: every key reaches the handlers through the service's key path (hostdomain.h): the board's own key enters as a `key` intent, a remote one is
// queued by its intent, and what comes out of the queue is what on_key and the modal waits read. Nothing here changes what a key does.
static int host_getch_nowait(void)
{
    int q = hostapi_pop_key();
    if (q >= 0) {
        return q;
    }
    int k = system_usb_keyboard.getch();
    if (k < 0 && matrix_kb) {
        k = matrix_kb->getch_nowait();
    }
    if (k >= 0) {
        hostapi_local_key(k);
        return hostapi_pop_key();
    }
    return -1;
}

static int host_getch(void)
{
    hostapi_waiting();                  // a modal wait: the service runs, so a remote answer or key arrives
    int q = hostapi_pop_key();
    if (q >= 0) {
        return q;
    }
    int k = system_usb_keyboard.getch();
    if (k < 0 && matrix_kb) {
        k = matrix_kb->getch();
    }
    if (k >= 0) {
        hostapi_local_key(k);
        return hostapi_pop_key();
    }
    return -1;
}

static uint32_t now_ms(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }

// A flow of the app as a job of the service (T192 D21): progress and cancel for a client, an end with its result. It adds no behaviour of its own; a flow inside
// a flow is not a second job.
struct FlowScope {
    bool own;
    FlowScope(const char *kind, const char *title, bool cancelable = false) { own = hostapi_flow_begin(kind, title, cancelable); }
    ~FlowScope()
    {
        if (own) {
            hostapi_flow_end(true, "");
        }
    }
};

// ---- small widget helpers ------------------------------------------------------------------------------------
static lv_obj_t *scr;

static lv_obj_t *box(lv_obj_t *par, int x, int y, int w, int h, uint32_t bg, int border = 0, uint32_t bcol = 0)
{
    return trx_box(par, x, y, w, h, bg, border, bcol);
}

static lv_obj_t *txt(lv_obj_t *par, int x, int y, const char *s, const lv_font_t *f, uint32_t col, int w = 0)
{
    return trx_txt(par, x, y, s, f, col, w);
}

// A label of one line: its height is the font's line height, so LV_LABEL_LONG_MODE_DOTS (trx_txt, whenever a width is
// given) cuts the text with "..." instead of wrapping into the row below (a label of automatic height wraps).
static lv_obj_t *line1(lv_obj_t *l)
{
    lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
    return l;
}

// T101 areas: a one-line label that changes while the view stays (a name, a path, a note, the countdown) is as wide as its
// text, at most `maxw`: LVGL redraws the label's whole box when its text changes, and a box of the column's width costs
// 2.2 ms per 1,000 px on the board whatever the text. The widest width is kept in the label's user data; set_txt() uses it.
static lv_obj_t *fit1(lv_obj_t *l, int maxw)
{
    line1(l);
    lv_obj_set_width(l, maxw);
    lv_obj_set_user_data(l, (void *)(intptr_t)maxw);
    return l;
}

// returns the chip's width (lv_obj_get_width() is the last laid-out size, not the one just set)

// ---- what the media look like ----------------------------------------------------------------------------------
// What the header chips and the network page show. Read once a second in the main loop (a directory listing of
// the mount points and the interface list cost real time: measured, they made every full redraw take about 1.5 s),
// never while a view is built.
// The readings live in the core (host_core_rows.h: HcStatus), where the rows are made from them; this loop is what reads the board.
#define c_usb (hc_status().usb)
#define c_sd (hc_status().sd)
#define c_net (hc_status().net)                       // any interface has an address (the header chip)
#define c_enet (hc_status().enet)                     // the cable's
#define c_lan (hc_status().lan)                       // the chip names the cable (T184)
#define c_ip (hc_status().ip)
#define c_eip (hc_status().eip)
#define c_media (hc_status().media)
#define c_wifi (hc_status().wifi)                     // T184: from refresh_status() (every 3 s) and after an action: the page never waits for the module
#define c_wcfg (hc_status().wcfg)
#define c_wcfg_ok (hc_status().wcfg_ok)

static bool has_root(const char *prefix)
{
    HostDirEntry r[16];
    int n = hostfs_list("/", r, 16, false);
    for (int i = 0; i < n; i++) {
        if (!strncasecmp(r[i].name, prefix, strlen(prefix))) {
            return true;
        }
    }
    return false;
}

// The address the header chip names (T184 D5): the cable's when it has a usable one, else the WiFi's (the driver's status says when that is usable, not a cached
// lease). The same order as net_chip_text(); `lan` tells which of the two it is.
static bool ip_string(char *buf, int len, bool *lan = 0)
{
    buf[0] = 0;
    for (int pass = 0; pass < 2; pass++) {                     // 0: wired interfaces, 1: wireless ones
        for (int i = 0; i < 4; i++) {
            NetworkInterface *ni = NetworkInterface::getInterface(i);
            if (!ni) {
                break;
            }
            if (ni->is_wireless() != (pass == 1) || !ni->is_link_up()) {
                continue;
            }
            if (pass == 1) {
                WifiNetStatus w;
                wifi_net_status(&w);
                if (w.ip[0]) {
                    snprintf(buf, len, "%s", w.ip);
                    if (lan) { *lan = false; }
                    return true;
                }
                continue;
            }
            ni->getIpAddrString(buf, len);
            if (net_address_usable(buf)) {
                if (lan) { *lan = true; }
                return true;
            }
        }
    }
    buf[0] = 0;
    return false;
}

// The wired interface's own address (the network page's Ethernet row); the WiFi interface has its row.
static bool eth_ip_string(char *buf, int len)
{
    for (int i = 0; i < 4; i++) {
        NetworkInterface *ni = NetworkInterface::getInterface(i);
        if (!ni) {
            break;
        }
        if (!ni->is_wireless() && ni->is_link_up()) {
            ni->getIpAddrString(buf, len);
            return buf[0] && strcmp(buf, "0.0.0.0");
        }
    }
    return false;
}

static bool refresh_status(void)
{
    uint32_t t0 = core_cycles();
    bool usb = has_root("usb"), sd = has_root("sd");
    uint32_t t1 = core_cycles();
    char ip[24] = "";
    bool lan = false;
    bool net = ip_string(ip, sizeof(ip), &lan);
    char eip[24] = "";
    bool enet = eth_ip_string(eip, sizeof(eip));
    uint32_t t2 = core_cycles();
    char media[96] = "";
    HostDirEntry r[12];
    int n = hostfs_list("/", r, 12, false);
    uint32_t t3 = core_cycles();
    if (t3 - t0 >= 20) {
        printf("[host] status: roots %u ms, network %u ms, media list %u ms\n", (unsigned)(t1 - t0), (unsigned)(t2 - t1), (unsigned)(t3 - t2));
    }
    for (int i = 0; i < n; i++) {
        strncat(media, "/", sizeof(media) - strlen(media) - 1);
        strncat(media, r[i].name, sizeof(media) - strlen(media) - 1);
        strncat(media, "  ", sizeof(media) - strlen(media) - 1);
    }
    bool changed = false;
    if (usb != c_usb) { printf("[host] status change: usb %d -> %d\n", c_usb, usb); changed = true; }
    if (sd != c_sd) { printf("[host] status change: sd %d -> %d\n", c_sd, sd); changed = true; }
    if (net != c_net) { printf("[host] status change: link %d -> %d\n", c_net, net); changed = true; }
    if (strcmp(ip, c_ip)) { printf("[host] status change: ip '%s' -> '%s'\n", c_ip, ip); changed = true; }
    if (strcmp(media, c_media)) { printf("[host] status change: media '%s' -> '%s'\n", c_media, media); changed = true; }
    if (enet != c_enet || strcmp(eip, c_eip)) { changed = true; }
    WifiNetStatus w;
    wifi_net_status(&w);                                // T184: the driver's own state, no call to the module
    if (w.state != c_wifi.state || w.busy != c_wifi.busy || w.last != c_wifi.last || w.scan != c_wifi.scan || strcmp(w.ssid, c_wifi.ssid) || strcmp(w.ip, c_wifi.ip) ||
        strcmp(w.mac, c_wifi.mac) || strcmp(w.module, c_wifi.module)) {
        changed = true;
    }
    c_wifi = w;
    WifiNetCfg wc;
    bool wok = wifi_net_cfg_get(&wc);
    if (wok != c_wcfg_ok || memcmp(&wc, &c_wcfg, sizeof(wc))) { changed = true; }
    c_wcfg = wc;
    c_wcfg_ok = wok;
    c_usb = usb; c_sd = sd; c_net = net; c_enet = enet; c_lan = lan;
    strcpy(c_eip, eip);
    strcpy(c_ip, ip);
    strcpy(c_media, media);
    return changed;
}

// ---- entry status ------------------------------------------------------------------------------------------------------
// The bundle's icon, when it has one of the right size and its SHA-256 matches the manifest (T101 D5).
static bool load_icon(CorSource &s, const CorInfo &ci, uint16_t *dst)
{
    if (ci.size[COR_PART_ICON] != ICON_PX * 2) {
        return false;
    }
    return cor_read_part(s, ci, COR_PART_ICON, (uint8_t *)dst, 0, 0) == COR_OK;
}

// T115: a row that is started as full firmware: a .cfw, or a .cor that is a LARGE core (manifest "kind":"large", T115 section 7).
static bool is_full(const char *path) { return hc_is_full(path); }

// T115 D7: the logo of a .cfw row, built in by the design name or a <name>.icon beside the file (exactly 2048 bytes).
// A large .cor carries its own icon record.
static void load_full_icon(const char *path, const char *name, uint16_t *dst, int *origin = 0)
{
    char ip[112];
    if (!cfw_is_name(path)) {
        static CorInfo lci;
        FileSource ls(path);
        if (ls.ok() && cor_probe(ls, lci) == COR_ERR_LARGE) {
            if (origin) {
                *origin = lci.origin;                        // T137 D1: the manifest is read here anyway: the row's badge says who made the file
            }
            if (load_icon(ls, lci, dst)) {
                return;
            }
        }
    }
    if (cfw_icon_sidecar_path(path, ip, sizeof(ip))) {
        FileSource side(ip);
        cfw_icon_load(name, side.ok() ? &side : 0, dst);
    } else {
        cfw_icon_load(name, 0, dst);
    }
}

// The file system as startcheck.h sees it: the FileManager.
class BoardFs : public StartFs {
public:
    bool stat(const char *path, FileStat *st) { return hostfs_stat(path, &st->size, &st->mtime); }
    CorSource *open(const char *path)
    {
        FileSource *f = new FileSource(path);
        if (!f->ok()) {
            delete f;
            return 0;
        }
        return f;
    }
    int list(const char *dir)
    {
        HostDirEntry r[1];
        return hostfs_list(dir, r, 1, false);
    }
};
static BoardFs board_fs;



// T101 S10: what is read per entry, and when. Not at the start: the menu comes up from boot.cfg alone and an entry is read afterwards,
// one phase per main-loop pass (lazy_pass). Phase 0 = (a) size and mtime of the directory entry, (b) the package header: the state.
// Phase 1 = the icon. No part is hashed, no medium is listed. An entry that differs from what was stored at adding is marked "changed"
// (it still starts, with the full check); F1 R checks it again and stores the new state.
static void probe_state(int i)
{
    icon_c_ok[i] = false;
    hc_probe_state(i);
}

static void probe_icon(int i)
{
    if (hc_efull(i)) {
        if (hc_est(i) == ES_OK || hc_est(i) == ES_CHANGED) {
            int origin = hc_einfo(i).origin;
            load_full_icon(cfg.e[i].path, cfg.e[i].name, icon_c[i], &origin);
            hc_entry_origin(i, origin);
            icon_c_ok[i] = true;
        }
    } else if (hc_est(i) == ES_OK || hc_est(i) == ES_CHANGED || hc_est(i) == ES_CONTRACT || hc_est(i) == ES_HOST) {
        FileSource s(cfg.e[i].path);
        icon_c_ok[i] = s.ok() && load_icon(s, hc_einfo(i), icon_c[i]);
    }
    icons_gen++;
}

// Both phases at once: for the places where the person asked for exactly this entry (adding, R).
static void probe_entry(int i)
{
    probe_state(i);
    probe_icon(i);
}

// What a command of the core that wrote boot.cfg left: the screen redraws, and says so when the file could not be written.
static void cfg_after(int r);

// Every entry is unknown again (a move, a removal, a late boot.cfg: the arrays no longer line up with the entries).
static void lazy_reset(void)
{
    hc_lazy_reset();
    hc_cfgov_forget();                       // T198: the badges are counted again
    for (int i = 0; i < BOOT_MAX; i++) {
        icon_c_ok[i] = false;
    }
    icons_gen++;
}

// Entry i is read again, its old state stays on screen until the new one is in.
static void lazy_mark(int i) { hc_lazy_mark(i); }

static void lazy_window(void)
{
    if (nv.view == V_MENU || nv.view == V_EDIT) {
        hc_lazy_window(nv.view == V_MENU ? nv.mfirst : nv.efirst, nv.view == V_MENU ? MROWS : EROWS);
    } else {
        hc_lazy_window(0, 0);                // another view: nothing but the default and the selection
    }
}

static bool lazy_busy(void) { lazy_window(); return hc_lazy_next(nv.sel) >= 0; }

static bool row_visible(int i)
{
    if (nv.view != V_MENU && nv.view != V_EDIT) {
        return false;
    }
    int first = nv.view == V_MENU ? nv.mfirst : nv.efirst;
    return i >= first && i < first + (nv.view == V_MENU ? MROWS : EROWS);
}


// ---- T229 S1/D9: where the start's time goes. Marks of the start phases, one record per read unit, the longest main-loop pass; written once to
// /SD/report/menutime.txt (the start block of startlog.txt holds 1 KB, this holds the rows) when the reader first goes idle. ----------------------------
static void slog_flush(void);
#define MT_MARKS 56
struct MtRow { uint16_t state, icon, verify, redraw; uint8_t units; };
static struct {
    const char *mark_n[MT_MARKS];
    uint32_t mark_t[MT_MARKS];
    int nmark;
    MtRow row[BOOT_MAX];
    int order[BOOT_MAX], norder;
    int redraw_row1;                 // 1 + the row whose last unit asked for a redraw (0: none): the loop adds the time of that redraw
    uint32_t t_unit_end;
    uint32_t pass_max, pass_at, unit_max;
    uint32_t t_pic, t_built, t_frame, t_idle;
    int rows_loaded, rows_total;     // the cache: entries taken from it / entries in boot.cfg (rows_total < 0: no cache file)
    const char *rows_why;
    bool reported;
    int tries;
    char area_name[8][32];           // the mark names of the areas built after the first frame
    uint32_t t_loop, t_key1;         // the first main-loop pass (keys are read from here), the first key handled
} g_mt;
static uint32_t mt_now(void) { return startlog_ms(); }
static void mt_mark(const char *name)
{
    if (g_mt.nmark < MT_MARKS) {
        g_mt.mark_n[g_mt.nmark] = name;
        g_mt.mark_t[g_mt.nmark++] = mt_now();
        printf("[mt] t+%u %s\n", g_mt.mark_t[g_mt.nmark - 1], name);
    }
}
static void mt_unit(int i, int kind, uint32_t ms)       // kind 0 state, 1 icon, 2 verify
{
    MtRow &r = g_mt.row[i];
    uint16_t v = ms > 65535 ? 65535 : (uint16_t)ms;
    if (!r.units) {
        g_mt.order[g_mt.norder++] = i;
    }
    if (r.units < 255) {
        r.units++;
    }
    (kind == 0 ? r.state : kind == 1 ? r.icon : r.verify) += v;
    if (ms > g_mt.unit_max) {
        g_mt.unit_max = ms;
    }
}
static void mt_report(void)
{
    static char b[4096];
    int n = snprintf(b, sizeof(b), "T229 menu timing, ms since the host began\npicture shown t+%u, menu built t+%u, first frame t+%u, reader idle t+%u\n",
                     g_mt.t_pic, g_mt.t_built, g_mt.t_frame, g_mt.t_idle);
    n += snprintf(b + n, sizeof(b) - n, "row cache: %s\n", g_mt.rows_total < 0 ? g_mt.rows_why : "");
    if (g_mt.rows_total >= 0) {
        n += snprintf(b + n - 1, sizeof(b) - n, "%d of %d entries from the cache file\n", g_mt.rows_loaded, g_mt.rows_total) - 1;
    }
    n += snprintf(b + n, sizeof(b) - n, "phases (+ms since the mark before):\n");
    for (int k = 0; k < g_mt.nmark && n < (int)sizeof(b) - 80; k++) {
        n += snprintf(b + n, sizeof(b) - n, "  t+%u +%u %s\n", g_mt.mark_t[k], k ? g_mt.mark_t[k] - g_mt.mark_t[k - 1] : 0u, g_mt.mark_n[k]);
    }
    for (int k = 0; k < g_mt.norder && n < (int)sizeof(b) - 80; k++) {
        const MtRow &r = g_mt.row[g_mt.order[k]];
        n += snprintf(b + n, sizeof(b) - n, "row %d state %u ms icon %u ms verify %u ms redraw %u ms (%u units)\n", g_mt.order[k], r.state, r.icon, r.verify, r.redraw, r.units);
    }
    n += snprintf(b + n, sizeof(b) - n, "longest main-loop pass %u ms (at t+%u), longest read unit %u ms\n", g_mt.pass_max, g_mt.pass_at, g_mt.unit_max);
    printf("%s", b);
    FoFs *fo = hostfs_fo();
    if (fo) {
        fo->make_dir(HOST_REPORT_DIR);
    }
    bool ok = hostfs_save(HOST_REPORT_DIR, "menutime.txt", b, n);
    if (ok) {
        g_mt.reported = true;
        startrec_event("menu timing: picture t+%u, built t+%u, frame t+%u, idle t+%u, longest pass %u ms (menutime.txt)", g_mt.t_pic, g_mt.t_built, g_mt.t_frame, g_mt.t_idle, g_mt.pass_max);
        slog_flush();
    } else if (++g_mt.tries >= 5) {
        g_mt.reported = true;                      // no SD card: not tried again
    }
}

// ---- T229 D1-D7: the row cache (Flash:/menu.rows) -----------------------------------------------------------------------------------------------
static MsSettle rows_st;
static bool rows_file_known;
static uint32_t rows_file_crc;
static uint32_t rows_key(void) { return ms_crc32(0, APP_VERSION_HASH, (uint32_t)strlen(APP_VERSION_HASH)); }

// At the start, after lazy_reset(): every entry whose path and boot.cfg line a record names takes the record's state, icon and config count. It waits for its stat.
static void rows_load(void)
{
    ms_settle_init(&rows_st);
    rows_file_known = false;
    g_mt.rows_total = -1;
    g_mt.rows_loaded = 0;
    g_mt.rows_why = "no cache file";
    if (!hc_cfg_loaded() || cfg.n == 0) {
        return;
    }
    uint32_t cap = mr_max_encoded(MR_MAX);
    uint8_t *buf = (uint8_t *)malloc(cap);
    MrRec *rec = (MrRec *)malloc(sizeof(MrRec) * MR_MAX);
    if (buf && rec) {
        int len = hostfs_load(MR_PATH, (char *)buf, (int)cap);
        if (len >= 0) {
            uint32_t crc;
            int cnt = 0;
            MrResult r = mr_decode(buf, (uint32_t)len, rows_key(), rec, MR_MAX, &cnt);
            if (r == MR_OK && mr_file_crc(buf, (uint32_t)len, &crc)) {      // only a file that was taken is "the file on the drive": a refused one is written over
                rows_file_known = true;
                rows_file_crc = crc;
            }
            if (r != MR_OK) {
                g_mt.rows_why = mr_result_text(r);
                STARTLOG("menu rows: refused (%s), today's path", mr_result_text(r));
            } else {
                g_mt.rows_total = cfg.n;
                for (int i = 0; i < cfg.n; i++) {
                    for (int j = 0; j < cnt; j++) {
                        if (strcmp(rec[j].path, cfg.e[i].path) || !hc_row_import(i, rec[j])) {
                            continue;
                        }
                        if (rec[j].has_icon && rec[j].icon) {
                            memcpy(icon_c[i], rec[j].icon, MR_ICON_BYTES);
                            icon_c_ok[i] = true;
                        }
                        if (rec[j].cfgn >= 0 && hc_ecfg_ok(i)) {
                            hc_cfgov_seed(cfg.e[i].path, rec[j].cfgn, rec[j].cfgstamp);
                        }
                        g_mt.rows_loaded++;
                        break;
                    }
                }
                icons_gen++;
                STARTLOG("menu rows: %d of %d entries from the cache (%d records)", g_mt.rows_loaded, cfg.n, cnt);
            }
        } else {
            STARTLOG("menu rows: none");
        }
    }
    free(buf);
    free(rec);
}

// The cache as it is now: the settled rows. Written only when the encoded file differs from the one on the drive (wear, D6).
static void rows_save(void)
{
    uint32_t cap = mr_max_encoded(MR_MAX);
    uint8_t *buf = (uint8_t *)malloc(cap);
    MrRec *rec = (MrRec *)malloc(sizeof(MrRec) * MR_MAX);
    if (buf && rec) {
        int n = 0;
        for (int i = 0; i < cfg.n && n < MR_MAX; i++) {
            if (!hc_row_export(i, &rec[n])) {
                continue;
            }
            MrRec &r = rec[n];
            int cn = 0;
            uint32_t stamp = 0;
            if (hc_ecfg_ok(i) && hc_cfgov_peek(cfg.e[i].path, &cn, &stamp)) {
                r.cfgn = (int16_t)cn;
                r.cfgstamp = stamp;
            }
            r.has_icon = icon_c_ok[i];
            r.icon = (const uint8_t *)icon_c[i];                 // little endian in memory, as the file holds it
            n++;
        }
        int len = mr_encode(rec, n, rows_key(), buf, cap);
        uint32_t crc;
        if (len > 0 && mr_file_crc(buf, (uint32_t)len, &crc)) {
            if (rows_file_known && rows_file_crc == crc) {
                printf("[host] menu rows: same as the file, not written (%d records, %d bytes)\n", n, len);
            } else {
                uint32_t t0 = now_ms();
                bool ok = hostfs_save(MR_DIR, MR_FILE, buf, len);
                if (ok) {
                    rows_file_known = true;
                    rows_file_crc = crc;
                }
                uint64_t fb = 0;
                FoFs *fo = hostfs_fo();
                bool have_free = fo && fo->free_bytes(MR_DIR, &fb);
                printf("[host] menu rows: %s, %d records, %d bytes, %u ms, flash free %u KiB%s\n", ok ? "saved" : "NOT saved", n, len, (unsigned)(now_ms() - t0),
                       (unsigned)(fb >> 10), have_free ? "" : " (unknown)");
            }
        }
    }
    free(buf);
    free(rec);
}

// The loop's side: every 250 ms the rows boil down to a signature; unchanged for MR_QUIET_MS with the reader idle, the file is made (D6).
static void rows_tick(uint32_t t, int nkeys)
{
    static uint32_t last;
    if (!have_ui) {
        return;
    }
    if (nkeys) {
        ms_settle_touch(&rows_st, t);
    }
    if (t - last < 250) {
        return;
    }
    last = t;
    uint32_t sig = ms_crc32(0, &icons_gen, sizeof(icons_gen));
    int32_t cc = 0;
    for (int i = 0; i < cfg.n; i++) {
        int32_t e = hc_est(i) | (icon_c_ok[i] ? 0x100 : 0);
        sig = ms_crc32(sig, &e, sizeof(e));
        sig = ms_crc32(sig, cfg.e[i].path, (uint32_t)strlen(cfg.e[i].path));
        sig = ms_crc32(sig, cfg.e[i].hash, (uint32_t)strlen(cfg.e[i].hash));
        int cn;
        uint32_t st;
        cc += hc_cfgov_peek(cfg.e[i].path, &cn, &st) ? cn + 1 : 0;
    }
    sig = ms_crc32(sig, &cc, sizeof(cc));
    bool ok = hc_cfg_loaded() && cfg.n > 0 && !nkeys && !lazy_busy();
    if (ms_settle(&rows_st, t, sig, ok, MR_QUIET_MS)) {
        rows_save();
        ms_settle_saved(&rows_st);
    }
}

// One unit of the lazy read: one phase of one entry, or (last, when quiet) one migration. Returns whether it did anything.
// A phase is a stat + a header read, or the icon read: the unit a key can have to wait for.
static bool lazy_step(bool quiet)
{
    HcLazy z;
    uint32_t t0 = now_ms();
    lazy_window();
    if (!hc_lazy_step(quiet, nv.sel, &z)) {
        return false;
    }
    if (z.unit == HCL_MIGRATE) {
        if (z.save != HCR_REFUSED) {
            cfg_after(z.save);
        }
        return true;
    }
    int i = z.index;
    bool icon_next = false;
    if (z.unit == HCL_VERIFY) {                      // T229 D5: one stat said whether the cached row still stands; nothing is drawn unless the config count went stale
        bool stale = hc_ecfg_ok(i) && hc_cfgov_stale(cfg.e[i].path);
        uint32_t ms = now_ms() - t0;
        mt_unit(i, 2, ms);
        printf("[host] entry %d %s: verify %s %u ms\n", i, cfg.e[i].path, z.changed ? "changed" : "same", (unsigned)ms);
        if (stale && (row_visible(i) || i == cfg.def)) {
            need_full = true;
            g_mt.redraw_row1 = i + 1;
        }
        g_mt.t_unit_end = now_ms();
        return true;
    }
    if (z.unit == HCL_STATE) {
        icon_c_ok[i] = false;
        icon_next = z.icon_next;
    } else {
        probe_icon(i);
        hc_lazy_icon_done(i);
    }
    uint32_t ums = now_ms() - t0;
    printf("[host] entry %d %s: %s read %u ms\n", i, cfg.e[i].path, z.unit == HCL_STATE ? "state" : "icon", (unsigned)ums);
    mt_unit(i, z.unit == HCL_STATE ? 0 : 1, ums);
    if (row_visible(i) || i == cfg.def) {
        need_full = true;
        g_mt.redraw_row1 = i + 1;
    }
    g_mt.t_unit_end = now_ms();
    return true;
}

// The main loop's call. A pass that handled a key, or follows one closely, reads nothing: the reader never takes the time of a key. The
// migration waits for a longer quiet.
#define LAZY_QUIET_MS 200
#define LAZY_MIG_QUIET_MS 1500
static bool lazy_pass(int nkeys, uint32_t now, uint32_t last_key)
{
    if (nkeys || now - last_key < LAZY_QUIET_MS) {
        return false;
    }
    return lazy_step(now - last_key >= LAZY_MIG_QUIET_MS);
}

// An entry the lazy reader has not reached yet starts too: the start verifies and refuses by itself (T101 S10).
static bool startable(int i) { return hc_startable(i); }

static void set_note(const char *s, int tone) { hc_ui_note(s, tone); }

static void cfg_after(int r) { hc_ui_cfg_after(r); }

// The core's side of the board: where boot.cfg is, and what a change of the entries ends on the screen.
static int core_cfg_load(char *buf, int cap) { return hostfs_load(BOOTCFG_PATH, buf, cap); }
static bool core_cfg_save(const char *text, int n) { return hostfs_save(BOOTCFG_DIR, BOOTCFG_FILE, text, n); }
static void core_cfg_changed(void) { arm_drop(HARM_END_GONE); }       // the entries changed: an arm on one of them does not hold
static void core_log(const char *line) { printf("%s\n", line); }
static uint32_t core_now(void) { return now_ms(); }
static bool core_start_file(const char *cor_path, const char *text, int n)      // T223 D4: "<medium>/trx64-start.txt" beside the large core's medium root
{
    char path[HANDOVER_PATH_MAX + 32];
    if (!startfile_path(cor_path, path, sizeof(path))) {
        return false;
    }
    char *slash = strrchr(path, '/');
    *slash = 0;
    return hostfs_save(path, slash + 1, text, n);
}
static const HcPlatform core_plat = { core_cfg_load, core_cfg_save, core_cfg_changed, &board_fs, core_log, core_now, core_start_file };

// ... and of the navigation: the folder listing, and what the screen drops when the list is read anew.
static int nav_list(const char *path, HostDirEntry *out, int max) { return hostfs_list(path, out, max, false); }
static bool nav_exists(const char *path) { return hostfs_exists(path); }
static void nav_arm_end(uint32_t token, int how) { hostapi_arm_end(token, how); }
static bool nav_key_local(void) { return hostapi_key_local(); }
static void nav_ask_gone(void) { hostapi_ask("", 0, 0); }
static const HcNavPlatform nav_plat = { nav_list, nav_exists, nav_arm_end, nav_key_local, 0, nav_ask_gone };

static void browse_load(void) { hc_browse_load(); }


static bool at_media_level(void) { return hc_browse_at_media(); }

static int in_menu(const char *path) { return hc_in_menu(path); }

// ---- the rows of the menu (T101 D1-D4) ----------------------------------------------------------------------------------
// The manifest panel of the selected file; rebuilt alone when the selection moves.

// ---- resident widgets ------------------------------------------------------------------------------------------------------
// Everything is built once (build_ui) and kept; views are switched with the hidden flag and updated by changing
// only the labels, colours and flags whose value changed. No lv_obj_clean + recreate on the hot path: the host CPU
// has no data cache (a load from DDR2 is ~0.4 us), so object creation and layout cost hundreds of ms
// (T24-ui-project-review.md 2.3). No animations, no opacity, no shadows: solid colours only.
#define CHIPS_W (COMPACT ? SW - 130 : 770)      // the header's chip holder, right aligned
#define CX0 (COMPACT ? 12 : 20)                 // content-relative geometry
#define CW  (SW - NAV_W - 2 * CX0)
#define CH  (SH - TOP_H - FOOT_H)
#define BROWS_MAX 16
#define BROWS (brows_n)                         // browser rows: what the panel holds (12 on the full canvas), from the height below it
static int brows_n = 13;
// the pitch of the menu rows and the editor rows, and where the editor's panel ends (compact: its settings take a line each)
#define ED_PN_H 56                              // T209 D19: the editor's info box is two lines
#define ED_ROW0 (ED_PN_H + 86)
#define BADGE_H (COMPACT ? 20 : 22)                // the badges of the boot menu's row (T209 D6)

// The page's explanatory line under the title: its width is the column's in the compact layout (a line that does not fit ends in dots, never in the edge of the canvas).
static lv_obj_t *sub1(lv_obj_t *p, const char *text, const char *compact_text = 0)
{
    if (COMPACT && compact_text) {                             // a shorter sentence that fits the narrow column whole, not one ending in dots
        text = compact_text;
    }
    lv_obj_t *l = txt(p, CX0, COMPACT ? 38 : 46, text, F_SMALL, C_MUTED, COMPACT ? CW : 0);
    return COMPACT ? line1(l) : l;
}


struct Row {
    lv_obj_t *box, *a, *b, *ic, *icl, *img, *defb, *defl, *stb, *stl, *cfb, *cfl, *vl, *tb, *tl;   // vl: the editor row's version (the menu row's is b)
    int w;                                      // the row's width (T198: the text lines' room depends on the badge)
    lv_obj_t *mk[4];                            // the selection mark: top, bottom, left bar, right (T101 areas)
    lv_draw_buf_t dsc;                          // the icon image's source (T101 D5)
    const uint16_t *ip;
    int gen;
    int pcs;                                    // the boot menu row's pictogram group (HCP_*) as drawn
};

static lv_obj_t *vwin[6];                       // one container per view, VIEW order
static lv_obj_t *chips_box, *nav_box[NAV_N], *nav_acc[NAV_N], *foot_label;
static char chips_sig[64];
static int nav_shown = -1;                      // the navigation entry that is drawn as the current one
static Row mrow[MROWS_MAX], erow[MROWS_MAX];
static lv_obj_t *m_hint, *m_sub, *m_pos, *m_note, *m_cdbox;
static lv_obj_t *e_def, *e_menu, *e_note, *e_pos;
static lv_obj_t *b_left, *b_path, *b_empty, *b_note, *b_right, *b_sub;
static lv_obj_t *b_hl[4];                       // the selection mark of the list (4 strips, see marks_build)
static lv_obj_t *b_mark[BROWS_MAX], *b_name[BROWS_MAX], *b_size[BROWS_MAX];
static lv_obj_t *r_title, *r_key[7], *r_val[7], *r_msg, *r_err;
static lv_obj_t *n_eth, *n_ftp, *n_media, *n_rest;
static lv_obj_t *c_pat;                         // colour screen: solid / stripe layer

static void set_txt(lv_obj_t *l, const char *s)
{
    if (!l) {
        return;                                 // an area that is not built yet (T229): its view draws everything when it is built and shown
    }
    if (strcmp(lv_label_get_text(l), s)) {
        int maxw = (int)(intptr_t)lv_obj_get_user_data(l);
        if (maxw > 0) {
            lv_point_t sz;
            lv_text_get_size(&sz, s, lv_obj_get_style_text_font(l, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            int w = sz.x + 4 < maxw ? sz.x + 4 : maxw;
            if (w < 1) {
                w = 1;
            }
            if (lv_obj_get_style_width(l, LV_PART_MAIN) != w) {
                lv_obj_set_width(l, w);
            }
        }
        lv_label_set_text(l, s);
    }
}

// T209: a text that is wider than the label is cut in the middle ("Long na...me.cor"): the head and the end both stay readable. The label's room is
// its fit1 width; the cut text goes through set_txt.
static void set_txt_mid(lv_obj_t *l, const char *s)
{
    static char buf[256];
    const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    const int room = (int)(intptr_t)lv_obj_get_user_data(l) - 4;
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (room <= 0 || sz.x <= room || strlen(s) >= sizeof(buf) - 4) {
        set_txt(l, s);
        return;
    }
    const int n = (int)strlen(s);
    int head = n / 2, tail = n - head;                 // start from the whole text and take one character off the longer side per step
    for (;;) {
        int h = head, t = tail;
        while (h > 0 && ((unsigned char)s[h] & 0xC0) == 0x80) {
            h--;                                       // never cut inside a UTF-8 sequence
        }
        while (t < n && ((unsigned char)s[n - t] & 0xC0) == 0x80) {
            t--;
        }
        memcpy(buf, s, (size_t)h);
        memcpy(buf + h, "...", 3);
        memcpy(buf + h + 3, s + n - t, (size_t)t);
        buf[h + 3 + t] = 0;
        lv_text_get_size(&sz, buf, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= room || h + t <= 2) {
            break;
        }
        if (head >= tail) {
            head--;
        } else {
            tail--;
        }
    }
    set_txt(l, buf);
}

// T209: the key legend. The compact foot has two lines; a line breaks between two keys, never inside a phrase.
static void set_foot(lv_obj_t *l, const char *s)
{
    static char buf[256];
    if (!COMPACT || strlen(s) >= sizeof(buf)) {
        set_txt(l, s);
        return;
    }
    const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= SW - 24) {
        set_txt(l, s);
        return;
    }
    snprintf(buf, sizeof(buf), "%s", s);
    char *cut = 0;
    for (char *c = strstr(buf, "    "); c; c = strstr(c + 4, "    ")) {
        char keep = *c;
        *c = 0;
        lv_text_get_size(&sz, buf, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        *c = keep;
        if (sz.x > SW - 24) {
            break;
        }
        cut = c;
    }
    if (cut) {
        *cut = '\n';
        memmove(cut + 1, cut + 4, strlen(cut + 4) + 1);
    }
    set_txt(l, buf);
}

static void set_col(lv_obj_t *l, uint32_t col)
{
    if (!l) {
        return;
    }
    lv_color_t c = lv_color_hex(col);
    lv_color_t o = lv_obj_get_style_text_color(l, LV_PART_MAIN);
    if (o.red != c.red || o.green != c.green || o.blue != c.blue) {
        lv_obj_set_style_text_color(l, c, 0);
    }
}

static void set_hidden(lv_obj_t *o, bool h)
{
    if (!o) {
        return;
    }
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != h) {
        lv_obj_set_flag(o, LV_OBJ_FLAG_HIDDEN, h);
    }
}

// T167: a panel over the screen sits in the overlay container (last child of the screen), where LVGL's search for the topmost covering
// object finds it: a changed label inside it never draws the screen below into the picture that is being shown. Every page change of such a
// panel (show, new page, hide) goes through TrxPage / trx_page_hide() (trxview.h, T167 5.9); tests/hostapp/pages_lint.sh keeps it so.

// The selection is four thin objects (a frame and a wider bar on the left), shown or hidden: moving it redraws those strips
// and not the two rows, whose tinted background the first version restyled (84,000 px for one key press; T101 areas).
#define MARK_T 2                                // frame thickness
#define MARK_BAR 5                              // the bar on the left
static void marks_build(lv_obj_t **mk, lv_obj_t *par, int x, int y, int w, int h)
{
    mk[0] = box(par, x, y, w, MARK_T, C_HOST, 0);
    mk[1] = box(par, x, y + h - MARK_T, w, MARK_T, C_HOST, 0);
    mk[2] = box(par, x, y, MARK_BAR, h, C_HOST, 0);
    mk[3] = box(par, x + w - MARK_T, y, MARK_T, h, C_HOST, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_set_flag(mk[i], LV_OBJ_FLAG_CLICKABLE, false);
        set_hidden(mk[i], true);
    }
}

static void marks_show(lv_obj_t **mk, bool on)
{
    if (!mk[0]) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        set_hidden(mk[i], !on);
    }
}

static void row_style(Row &r, bool on)
{
    marks_show(r.mk, on);
}

#define BADGE_W (COMPACT ? 112 : 150)
#define DEF_W (COMPACT ? 70 : 78)
#ifdef HC_CFG_MARKER_LONG
#define CFGM_W (COMPACT ? 52 : 60)         // T223 D6: the boot menu's config column ("config")
#else
#define CFGM_W 26                          // T223 D6: the boot menu's config column ("C")
#endif
#define CFG_W (COMPACT ? 84 : 98)          // T198: the "N configs" badge, between the status and the default badge (mock-up order: default, N configs, status)

// T209 D6 (Alex 2026-11-12): the boot menu row is the mock-up's: the logo at its full size (32 x 32, as before T209) on the left, then the name, then the
// version right-aligned on the same line, then the badges. No path in the row (it is the detail line under the list). The row is as high as the logo needs.
#define LROW_H 38                                  // 32 px logo + 3 px above and below inside the 1 px frame
#define LROW_P (LROW_H + 2)
#define LROW_TX 70                                 // the name starts right of the 48 px logo tile (as before T209)
#define PIC_D 22                                    // T209 D10: the pictogram (menu_pictos.h, 22 x 22)
#define PIC_GREEN 0x35C25F
#define PIC_YELLOW 0xF2B01E
#define PIC_RED 0xE5484D
static lv_draw_buf_t pic_buf[3];
static void pic_init(void)
{
    static const uint8_t *const d[3] = { PICTO_OK, PICTO_CHECK, PICTO_BAD };
    if (!pic_buf[0].data) {
        for (int k = 0; k < 3; k++) {
            lv_draw_buf_init(&pic_buf[k], PIC_D, PIC_D, LV_COLOR_FORMAT_A8, 24, (void *)d[k], 24 * PIC_D);
        }
    }
}
static uint32_t pic_col(int g) { return g == HCP_OK ? PIC_GREEN : g == HCP_CHECK ? PIC_YELLOW : PIC_RED; }
#define LROW_VER_W (COMPACT ? 96 : 150)           // the version's widest label ("0.3.0-dev+85acf2b0" of a test build, T221, cut to its SemVer at 480p so the name keeps its room; "full firmware"); the label is as wide as its text (T209 D16)
#define RGRP_GAP 6                                // left of the version: to the config marker (boot row, T223 D7) or the pictogram (editor row)
// T209 D16: the width the version text takes (label width as set_txt would give it), 0 for none
static int ver_w(const char *s)
{
    if (!s[0]) {
        return 0;
    }
    lv_point_t sz;
    lv_text_get_size(&sz, s, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x + 4 < LROW_VER_W ? sz.x + 4 : LROW_VER_W;
}
// T209 D20: one version column for the whole list (the widest version of any entry, not of the visible page: it does not jump while scrolling); the pictograms stand left of it
static int ver_col_w(void)
{
    int m = 0;
    for (int i = 0; i < cfg.n; i++) {
        char b[24];
        hc_entry_ver(i, b, sizeof(b));
        const int w = ver_w(b);
        m = w > m ? w : m;
    }
    return m;
}
// T223 D7 (T221 D16): the core-type badge, a small purple square right of the version ("L" large, "P" partial), as on mock-up t1b. Its slot is kept on rows
// without a type (a .cfw) so the version column does not move. The status pictogram stands at the far right end, after the version and the badge
// (Alex 2026-11-16, from pictures A/B: A).
#define TB_W (COMPACT ? 18 : 22)
#define TB_GAP 6
#define TB_BG 0x5B49C8
#define TB_INK 0xEDE9FF
static int vcol_w;                                  // set by update_menu / update_edit before the rows are filled
static void build_mrow(Row &r, lv_obj_t *par, int x, int y, int w, int h)
{
    r.box = box(par, x, y, w, h, C_PANEL, 1, C_LINE);
    r.w = w;
    r.ic = box(r.box, 10, (h - 2 - ICON_W) / 2, ICON_TILE_W, ICON_W, C_PANEL2, 0);
    r.icl = fit1(txt(r.ic, 4, (ICON_W - lv_font_get_line_height(F_SMALL)) / 2, "", F_SMALL, C_MUTED, 40), 40);
    r.img = lv_image_create(r.ic);
    lv_obj_set_pos(r.img, 0, 0);
    lv_obj_set_size(r.img, ICON_W, ICON_W);       // the picture's own size: no bars, no fractional zoom
    lv_image_set_scale(r.img, 256);               // 1:1 in the 960 x 540 layer; the pixel layer's own x2 (PX_CTRL 0x22) is whole
    set_hidden(r.img, true);
    const int ty = (h - 2 - lv_font_get_line_height(F_TEXT)) / 2;
    r.a = fit1(txt(r.box, LROW_TX, ty, "", F_TEXT, C_INK, w - LROW_TX - 10), w - LROW_TX - 10);
    r.b = fit1(txt(r.box, w - 10, (h - 2 - lv_font_get_line_height(F_SMALL)) / 2, "", F_SMALL, C_MUTED, LROW_VER_W), LROW_VER_W);       // the version; x is set by fill_mrow
    lv_obj_set_style_text_align(r.b, LV_TEXT_ALIGN_RIGHT, 0);
    const int by = (h - 2 - BADGE_H) / 2;
    const int bty = (BADGE_H - 2 - lv_font_get_line_height(F_SMALL)) / 2;
    // T209 D10: the state is a round pictogram (A8 mask, tinted: a disc with the glyph cut out), all the same size; x is set by fill_mrow
    pic_init();
    r.stb = lv_image_create(r.box);
    lv_obj_set_pos(r.stb, w - 10 - PIC_D, (h - 2 - PIC_D) / 2);             // x is set by fill_mrow
    lv_obj_set_size(r.stb, PIC_D, PIC_D);
    lv_image_set_src(r.stb, &pic_buf[HCP_OK]);
    lv_obj_set_style_image_recolor_opa(r.stb, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(r.stb, lv_color_hex(PIC_GREEN), 0);
    r.pcs = HCP_OK;
    r.stl = NULL;
    r.tb = box(r.box, w - 10 - TB_W, (h - 2 - TB_W) / 2, TB_W, TB_W, TB_BG, 0);
    r.tl = txt(r.tb, 0, (TB_W - lv_font_get_line_height(F_SMALL)) / 2, "", F_SMALL, TB_INK, TB_W);
    lv_obj_set_style_text_align(r.tl, LV_TEXT_ALIGN_CENTER, 0);
    r.defb = box(r.box, w - 10 - BADGE_W - DEF_W - 6, by, DEF_W, BADGE_H, C_HOST, 0);
    r.defl = txt(r.defb, 0, bty + 1, "auto", F_SMALL, C_BG, DEF_W);
    lv_obj_set_style_text_align(r.defl, LV_TEXT_ALIGN_CENTER, 0);
    r.cfb = box(r.box, w - 10 - BADGE_W - CFG_W - 6, by, CFG_W, BADGE_H, C_PANEL2, 1, C_HOST);
    r.cfl = fit1(txt(r.cfb, 6, bty, "", F_SMALL, C_HOST, CFG_W - 12), CFG_W - 12);
    marks_build(r.mk, r.box, -1, -1, w, h);          // last: on top of the row's own widgets
}

// Row i of the boot menu: logo (or the text tile), name, version, badges.
static void fill_mrow(Row &r, int i)
{
    HcEntryRow v;
    if (!hc_row_entry(i, true, &v)) {
        set_hidden(r.box, true);
        return;
    }
    set_hidden(r.box, false);
    const uint16_t *icon = icon_c_ok[i] ? icon_c[i] : 0;
    {   // T209 D15: the tile text is centred in the tile and shortened by whole letters until it fits (never cut by dots)
        char tl[sizeof(v.tile)];
        snprintf(tl, sizeof(tl), "%s", v.tile);
        lv_point_t tsz = { 0, 0 };
        for (int n = (int)strlen(tl); n > 0; n--) {
            lv_text_get_size(&tsz, tl, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (tsz.x + 4 <= 40) break;           // the label box is 40 wide (set_txt adds 4)
            tl[n - 1] = 0;
        }
        set_txt(r.icl, tl);
        lv_obj_set_x(r.icl, (ICON_TILE_W - tsz.x - 4) / 2);
    }
    if (icon) {
        if (r.ip != icon || r.gen != icons_gen) {
            lv_draw_buf_init(&r.dsc, 32, 32, LV_COLOR_FORMAT_RGB565, 64, (void *)icon, ICON_PX * 2);
            lv_image_set_src(r.img, &r.dsc);
            lv_obj_invalidate(r.img);
            r.ip = icon;
            r.gen = icons_gen;
        }
    } else {
        r.ip = 0;
    }
    set_hidden(r.img, !icon);
    set_hidden(r.icl, icon != 0);
    // a picture sits in a box of exactly its size (the 48 wide box is the text tile's); the text column stays where it is
    const int ix = icon ? 10 + (ICON_TILE_W - ICON_W) / 2 : 10, iw = icon ? ICON_W : ICON_TILE_W;
    const int iy = (LROW_H - 2 - ICON_W) / 2;       // not the live height: a row hidden at its first fill has none yet, which put the tile 17 px above the row
    if (lv_obj_get_x(r.ic) != ix) {
        lv_obj_set_pos(r.ic, ix, iy);
    }
    if (lv_obj_get_style_width(r.ic, LV_PART_MAIN) != iw) {
        lv_obj_set_width(r.ic, iw);
    }
    // from the right (T209 D16, T223 D7, Alex 2026-11-16: "the tick pictograms at the far right edge"): the pictogram at the right edge, the type badge
    // left of it, the version left of that (as wide as its text); then "auto", the config marker; the name takes what is left
    const int vw = vcol_w;
    const int px = r.w - 10 - PIC_D;
    const int tx = px - TB_GAP - TB_W;
    const int vx = tx - TB_GAP - vw;
    const int lx = vx - (vw ? RGRP_GAP : 0);
    if (lv_obj_get_x(r.b) != vx) {
        lv_obj_set_x(r.b, vx);
    }
    if (lv_obj_get_x(r.stb) != px) {
        lv_obj_set_x(r.stb, px);
    }
    if (lv_obj_get_x(r.tb) != tx) {
        lv_obj_set_x(r.tb, tx);
    }
    {
        const char lt[2] = { v.ctype, 0 };
        set_txt(r.tl, lt);                         // the label is the square's width at x 0 and centred (build_mrow): the letter sits in the middle
        set_hidden(r.tb, !v.ctype);
    }
    // T223 D5/D6: a table, the columns keep their place whether the row has a mark or not: name | auto | config | pictogram | version
    const int cx = lx - 6 - CFGM_W;
    const int dx = cx - 6 - DEF_W;
    const int nleft = dx - 8;
    if (lv_obj_get_x(r.defb) != dx) {
        lv_obj_set_x(r.defb, dx);
    }
    if (lv_obj_get_x(r.cfb) != cx) {
        lv_obj_set_x(r.cfb, cx);
    }
    if (lv_obj_get_width(r.cfb) != CFGM_W) {
        lv_obj_set_width(r.cfb, CFGM_W);
    }
    const int want = nleft - LROW_TX;
    if (want != (int)(intptr_t)lv_obj_get_user_data(r.a)) {
        lv_obj_set_width(r.a, want);
        lv_obj_set_user_data(r.a, (void *)(intptr_t)want);
    }
    set_txt_mid(r.a, v.name);
    set_txt(r.b, v.ver);
    if (vw && lv_obj_get_style_width(r.b, LV_PART_MAIN) != vw) {   // the label is the column's width; its text stands at the right (D20)
        lv_obj_set_width(r.b, vw);
    }
    if (r.pcs != v.pic) {
        lv_image_set_src(r.stb, &pic_buf[v.pic]);
        lv_obj_set_style_image_recolor(r.stb, lv_color_hex(pic_col(v.pic)), 0);
        r.pcs = v.pic;
    }
    set_hidden(r.defb, !v.def);
    set_txt(r.cfl, v.cfgmark);
    {
        lv_point_t msz = { 0, 0 };
        lv_text_get_size(&msz, v.cfgmark, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_set_x(r.cfl, (CFGM_W - 2 - (msz.x + 4)) / 2);
    }
    set_hidden(r.cfb, !v.cfgmark[0]);
}

// T209 D8 (Alex 2026-11-12): the editor row is the boot menu's row as it was before T209 (fcd8abd9a^1), copied, without the logo: the name (F_TEXT), under it the
// path (F_SMALL), the version, the pictogram and the badges at the right (D16). Same row height (56) as that row; the pitch is 60 so five rows fit above the status line on the full canvas.
#define EROW_H 56
#define EROW_P 60
#define EROW_TX 12                            // the text starts at 12 (no logo)
static void build_erow(Row &r, lv_obj_t *par, int x, int y, int w, int h)
{
    r.box = box(par, x, y, w, h, C_PANEL, 1, C_LINE);
    r.w = w;
    const int tx = EROW_TX, room = w - tx - 10;        // the widest the name can be; fill_erow sets what the right group leaves (T209 D16)
    r.a = fit1(txt(r.box, tx, 6, "", F_TEXT, C_INK, room), room);
    r.b = fit1(txt(r.box, tx, 30, "", F_SMALL, C_MUTED, room), room);
    r.vl = fit1(txt(r.box, w - 10, (h - 2 - lv_font_get_line_height(F_SMALL)) / 2, "", F_SMALL, C_MUTED, LROW_VER_W), LROW_VER_W);   // x set by fill_erow
    lv_obj_set_style_text_align(r.vl, LV_TEXT_ALIGN_RIGHT, 0);
    const int by = 17;
    // T209 D12: the state is the boot menu's pictogram, at the right end, no word box
    pic_init();
    r.stb = lv_image_create(r.box);
    lv_obj_set_pos(r.stb, w - 10 - PIC_D, (h - 2 - PIC_D) / 2);
    lv_obj_set_size(r.stb, PIC_D, PIC_D);
    lv_image_set_src(r.stb, &pic_buf[HCP_OK]);
    lv_obj_set_style_image_recolor_opa(r.stb, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(r.stb, lv_color_hex(PIC_GREEN), 0);
    r.pcs = HCP_OK;
    r.stl = NULL;
    r.defb = box(r.box, w - 10 - PIC_D - DEF_W - 6, by, DEF_W, 22, C_HOST, 0);
    r.defl = txt(r.defb, 0, 2, "auto", F_SMALL, C_BG, DEF_W);
    lv_obj_set_style_text_align(r.defl, LV_TEXT_ALIGN_CENTER, 0);
    r.cfb = box(r.box, w - 10 - PIC_D - CFG_W - 6, by, CFG_W, 22, C_PANEL2, 1, C_HOST);
    r.cfl = fit1(txt(r.cfb, 6, 2, "", F_SMALL, C_HOST, CFG_W - 12), CFG_W - 12);
    marks_build(r.mk, r.box, -1, -1, w, h);          // last: on top of the row's own widgets
}

// Row i of the editor: the same entries as the menu, the subline is the path. Right group as in the menu (T209 D16): version (far right), the pictogram directly
// beside it, "auto", "N configs"; the name and the path take what is left of the row.
static void fill_erow(Row &r, int i)
{
    HcEntryRow v;
    if (!hc_row_entry(i, true, &v)) {
        set_hidden(r.box, true);
        return;
    }
    set_hidden(r.box, false);
    const int vw = vcol_w;
    const int vx = r.w - 10 - vw;
    const int px = vx - (vw ? RGRP_GAP : 0) - PIC_D;
    const int dx = px - 6 - DEF_W;
    const int cx = (v.def ? dx : px) - 6 - CFG_W;
    const int nleft = (v.cfgbadge[0] ? cx : v.def ? dx : px) - 8;
    {
        const int want = nleft - EROW_TX;
        if (want != (int)(intptr_t)lv_obj_get_user_data(r.a)) {
            lv_obj_set_width(r.a, want);
            lv_obj_set_user_data(r.a, (void *)(intptr_t)want);
            lv_obj_set_width(r.b, want);
            lv_obj_set_user_data(r.b, (void *)(intptr_t)want);
        }
    }
    set_txt_mid(r.a, v.name);
    set_txt(r.b, v.sub);
    set_txt(r.vl, v.ver);
    if (vw && lv_obj_get_style_width(r.vl, LV_PART_MAIN) != vw) {
        lv_obj_set_width(r.vl, vw);
    }
    if (lv_obj_get_x(r.vl) != vx) {
        lv_obj_set_x(r.vl, vx);
    }
    if (lv_obj_get_x(r.stb) != px) {
        lv_obj_set_x(r.stb, px);
    }
    if (r.pcs != v.pic) {
        lv_image_set_src(r.stb, &pic_buf[v.pic]);
        lv_obj_set_style_image_recolor(r.stb, lv_color_hex(pic_col(v.pic)), 0);
        r.pcs = v.pic;
    }
    set_hidden(r.defb, !v.def);
    if (lv_obj_get_x(r.defb) != dx) {
        lv_obj_set_x(r.defb, dx);
    }
    if (lv_obj_get_x(r.cfb) != cx) {
        lv_obj_set_x(r.cfb, cx);
    }
    set_txt(r.cfl, v.cfgbadge);
    set_hidden(r.cfb, !v.cfgbadge[0]);
}

// ---- header chips: rebuilt only when their text changed --------------------------------------------------------
static int chip(lv_obj_t *par, int xr, const char *s, bool ok)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = sz.x + 18;
    if (xr - w < 0) {
        return -6;                              // not enough room left in the holder (a narrow canvas): the chip is left out; the caller's `+ 6` gives the gap back
    }
    lv_obj_t *c = box(par, xr - w, COMPACT ? 5 : 8, w, 24, C_PANEL2, 1);
    lv_obj_t *l = txt(c, 8, 2, s, F_SMALL, ok ? C_OK : C_MUTED);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(l, sz.x + 2);
    return w;
}

static void update_chips(void)
{
    char sig[64];
    hc_chips_sig(sig, sizeof(sig));
    if (!chips_box || !strcmp(sig, chips_sig)) {
        return;
    }
    strcpy(chips_sig, sig);
    lv_obj_clean(chips_box);
    HcChip ch[8];
    int n = hc_chips(ch, 8);
    int xr = CHIPS_W;
    for (int i = 0; i < n; i++) {
        xr -= chip(chips_box, xr, ch[i].text, ch[i].ok) + 6;
    }
}

// ---- countdown ---------------------------------------------------------------------------------------------------------
static lv_obj_t *cd_label_obj, *cd_bar_obj;
static void cd_update(void)
{
    if (!cd_label_obj) {
        return;
    }
    HcCountdownTexts t;
    hc_countdown_texts(&t);
    if (!t.visible) {
        lv_obj_add_flag(cd_label_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cd_bar_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_cdbox, LV_OBJ_FLAG_HIDDEN);     // the opaque box sits over the status line (CH - 26) while direct boot moves it down
        return;
    }
    lv_obj_clear_flag(m_cdbox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cd_label_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cd_bar_obj, LV_OBJ_FLAG_HIDDEN);
    set_txt(cd_label_obj, t.text);
    if (t.pct != cd_last_pct) {
        lv_bar_set_value(cd_bar_obj, t.pct, LV_ANIM_OFF);
        cd_last_pct = t.pct;
    }
}

// ---- views: build once ------------------------------------------------------------------------------------------------
// The legends of the screens are generated from the key map (host_core.cc, hc_foot): no key is named here. The F keys stand in the navigation on the left
// (Alex 2026-10-21). One line, never wrapped (foot_label is DOTS); hostui_test_foot_overflow() measures each against the footer.
// T111: the footer of the File Browser follows what is selected: on the media level the format, on a boot-menu file the add and the start,
// elsewhere only the keys that do something. (HCF_BROWSE is the longest of them.)
#define foot_br_media hc_foot(HCF_BR_MEDIA)
#define foot_br_plain hc_foot(HCF_BR_PLAIN)
#define foot_br_file hc_foot(HCF_BR_FILE)
#define foot_br_dir hc_foot(HCF_BR_DIR)
#define foot_br_pick_copy hc_foot(HCF_BR_PICK_COPY)
#define foot_br_pick_move hc_foot(HCF_BR_PICK_MOVE)
#define foot_br_ask hc_foot(HCF_BR_ASK)
#define foot_br_backup hc_foot(HCF_BR_BACKUP)
#define foot_br_txu hc_foot(HCF_BR_TXU)
#define foot_br_inspect hc_foot(HCF_BR_INSPECT)
#define foot_br_cfw hc_foot(HCF_BR_CFW)
#define foot_br_zip hc_foot(HCF_BR_ZIP)
#define foot_cm_list hc_foot(HCF_CM_LIST)
#define foot_cm_ask1 hc_foot(HCF_CM_ASK1)
#define foot_cm_ask2 hc_foot(HCF_CM_ASK2)
#define foot_wm hc_foot(HCF_WM)
#define foot_pm hc_foot(HCF_PM)
static const int foot_all[] = { HCF_MENU, HCF_BROWSE, HCF_EDIT, HCF_NET, HCF_SET, HCF_BR_TXU, HCF_BR_ZIP, HCF_BR_INSPECT, HCF_BR_CFW, HCF_BR_BACKUP, HCF_BR_MEDIA, HCF_BR_PLAIN, HCF_BR_FILE,
                                HCF_BR_DIR, HCF_BR_PICK_COPY, HCF_BR_PICK_MOVE, HCF_BR_ASK, HCF_CM_LIST, HCF_CM_EMPTY, HCF_CM_ASK1, HCF_CM_ASK2, HCF_WM };

static lv_obj_t *view_box(int v)
{
    lv_obj_t *o = box(scr, NAV_W, TOP_H, SW - NAV_W, CH, C_BG, 0);
    vwin[v] = o;
    set_hidden(o, true);
    return o;
}

// T229 (build phase): what the first frame does not show is built after it. An AREA is a view (browser, editor, network, settings, colours) or a group of
// overlay panels (rename, wifi, start); its builder is written as STEPS: each BSTEP block runs in one call, the builder runs through again for the next
// one (the values outside the blocks are computed again, they only read). A step is a few objects (about 7 ms each on the board), so the main loop can
// build for 40 ms between two key checks; a key that opens an area not built yet builds the rest of it at once (EN).
enum { A_BROWSE, A_EDIT, A_NET, A_SET, A_COLORS, A_RENAME, A_WIFI, A_START, A_N };
#ifdef HOSTUI_RENDER_TEST
static bool lazy_build = false;                 // the benches build everything at the start, as before (the lazy bench turns it on)
#else
static bool lazy_build = true;
#endif
static bool b_done[A_N];
static int b_stage[A_N];
static struct { int target, idx; } bs;
#define BSTEP if (bs.idx++ == bs.target)
static void ensure_area(int a);
#define EN(a) do { if (!b_done[a]) ensure_area(a); } while (0)
static lv_obj_t *bview(int v)
{
    return vwin[v] ? vwin[v] : view_box(v);
}

// Two lines: one line of F_TEXT is wider than the content column (emulator screenshot 2026-10-30: it was cut at the right edge).

static void build_menu(void)
{
    lv_obj_t *p = view_box(V_MENU);
    txt(p, CX0, COMPACT ? 8 : 14, page_title("menu"), F_TITLE, C_INK);
    m_sub = fit1(txt(p, CX0, COMPACT ? 38 : 46, "", F_SMALL, C_MUTED, CW), CW);
    m_pos = txt(p, CX0 + CW - 200, COMPACT ? 14 : 22, "", F_SMALL, C_MUTED, 200);      // T209: on the title's line, so the subtitle has the whole column
    lv_obj_set_style_text_align(m_pos, LV_TEXT_ALIGN_RIGHT, 0);
    m_hint = txt(p, CX0, 100, HC_NO_CORES_HINT, F_TEXT, C_MUTED);
    for (int i = 0; i < mrows_full; i++) {
        build_mrow(mrow[i], p, CX0, MENU_ROW0 + i * LROW_P, CW, LROW_H);
    }
    m_note = fit1(txt(p, CX0, CH - 26, "", F_SMALL, C_MUTED, CW), CW);      // menu_rows_sync moves it up while the countdown line shows
    m_cdbox = box(p, CX0, CH - 34, CW, 30, C_BG, 0);
    cd_label_obj = fit1(txt(m_cdbox, 0, 0, "", F_SMALL, C_MUTED, CW), CW);
    cd_bar_obj = lv_bar_create(m_cdbox);
    lv_obj_set_pos(cd_bar_obj, 0, 22);
    lv_obj_set_size(cd_bar_obj, CW, 6);
    lv_bar_set_range(cd_bar_obj, 0, 100);
    lv_obj_set_style_bg_opa(cd_bar_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(cd_bar_obj, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(cd_bar_obj, lv_color_hex(C_PANEL2), 0);
    lv_obj_set_style_bg_color(cd_bar_obj, lv_color_hex(C_HOST), LV_PART_INDICATOR);
    lv_obj_set_style_radius(cd_bar_obj, 0, 0);
    lv_obj_set_style_radius(cd_bar_obj, 0, LV_PART_INDICATOR);
}

// T209 D10/D14: the status line (the note slot) of the boot menu and of the editor shows the selected row's state word while there is no note
static void state_status(lv_obj_t *slot)
{
    HcEntryRow v;
    if (nv.note[0] || !hc_row_entry(nv.sel, true, &v)) {
        set_txt(slot, nv.note);
        set_col(slot, note_color());
        return;
    }
    set_txt(slot, v.badge);
    set_col(slot, pic_col(v.pic));
}
static void menu_status(void) { state_status(m_note); }

// The countdown line (a second line under the status line) is there unless direct boot is on: with it the list holds one row less and the status line
// moves up by its height; without it the status line sits on the pane's bottom edge above the key bar.
static void menu_rows_sync(void)
{
    HcCountdownTexts t;
    hc_countdown_texts(&t);
    int want = t.visible ? mrows_cd : mrows_full;
    if (m_note) {
        lv_obj_set_y(m_note, t.visible ? CH - 56 : CH - 26);
    }
    if (want != mrows_n) {
        mrows_n = want;
        hc_nav_geometry(mrows_n, hc_nav_browse_rows(), hc_nav_cores_rows(), hc_nav_edit_rows());
    }
}

static void update_menu(void)
{
    menu_rows_sync();
    int total = cfg.n;
    hc_nav_menu_first(total);
    HcMenuTexts t;
    vcol_w = ver_col_w();
    hc_menu_texts(nv.mfirst, total, &t);
    set_hidden(m_hint, !t.hint_on);
    set_txt(m_hint, t.hint);
    set_txt(m_sub, t.sub);
    set_txt(m_pos, t.pos);
    for (int i = 0; i < mrows_full; i++) {
        if (i >= MROWS) {
            set_hidden(mrow[i].box, true);
            continue;
        }
        fill_mrow(mrow[i], nv.mfirst + i);
        if (nv.mfirst + i < total) {
            row_style(mrow[i], nv.mfirst + i == nv.sel);
        }
    }
    menu_status();
    if (!hc_cfgov_is_open()) {
        set_foot(foot_label, hc_menu_foot());              // T198 D9: "RIGHT configs" only while the selected row has configs
    }
    cd_update();
}

static void build_edit(void)
{
    lv_obj_t *p = bview(V_EDIT);
    BSTEP {
        txt(p, CX0, COMPACT ? 8 : 14, page_title("edit"), F_TITLE, C_INK);
        sub1(p, "Order, auto entry and names (boot.cfg on the flash disk). Countdown and direct boot: F8.", "Order, auto entry and names (boot.cfg). Countdown: F8.");
        e_pos = txt(p, CX0 + CW - 200, COMPACT ? 14 : 22, "", F_SMALL, C_MUTED, 200);
        lv_obj_set_style_text_align(e_pos, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_t *pn = box(p, CX0, COMPACT ? 64 : 74, CW, ED_PN_H, C_PANEL, 1);
        // T209 D19 (Alex 2026-11-13): two lines, the auto core with its auto config and the file of the menu; countdown and direct boot are F8 settings
        e_def = fit1(txt(pn, 12, 8, "", F_SMALL, C_INK, CW - 24), CW - 24);
        e_menu = fit1(txt(pn, 12, 30, "", F_SMALL, C_INK, CW - 24), CW - 24);
    }
    for (int i = 0; i < EROWS; i++) {
        BSTEP {
            build_erow(erow[i], p, CX0, (COMPACT ? 64 : 74) + ED_PN_H + 12 + i * EROW_P, CW, EROW_H);
        }
    }
    BSTEP {
        e_note = fit1(txt(p, CX0, CH - 26, "", F_SMALL, C_MUTED, CW), CW);
    }
}

static void update_edit(void)
{
    EN(A_EDIT);
    hc_nav_edit_first(cfg.n);
    HcEditTexts t;
    vcol_w = ver_col_w();
    hc_edit_texts(nv.efirst, &t);
    set_txt_mid(e_def, t.autol);
    set_txt(e_menu, t.menu);
    set_txt(e_pos, t.pos);
    for (int i = 0; i < EROWS; i++) {
        fill_erow(erow[i], nv.efirst + i);
        if (nv.efirst + i < cfg.n) {
            row_style(erow[i], nv.efirst + i == nv.sel);
        }
    }
    state_status(e_note);
    set_foot(foot_label, hc_edit_foot());              // the hint follows the selected row (config of this core only if it has configs)
}

static void build_browse(void)
{
    lv_obj_t *p = bview(V_BROWSE);
    // the list on the left, the file's panel on the right; the compact layout gives the panel less (a third) and the list the rest
    int rw = COMPACT ? (CW - 16) / 2 : CW / 2 - 8;
    int lw = COMPACT ? CW - 16 - rw : CW / 2 - 8;
    int top = COMPACT ? 64 : 74;
    int ph = CH - top - 30;                               // T223 R2: the panels end 4 px above the note line (CH - 26), not under it
    // the value column of the right panel: compact, it starts after the longest key of either mode of the panel (a key is never cut to dots)
    static const char *const key_words[] = { "Version", "Contract", "FPGA", "Core", "Application", "SHA-256", "Status", "Size", "Contents", "Boot menu", "Access", "Format" };
    int rk = 130;
    if (COMPACT) {
        int kw = 0;
        for (const char *k : key_words) {
            lv_point_t ksz;
            lv_text_get_size(&ksz, k, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            kw = ksz.x > kw ? ksz.x : kw;
        }
        rk = 12 + kw + 10;
    }
    int szw = COMPACT ? 70 : 86;
    BSTEP {
        txt(p, CX0, COMPACT ? 8 : 14, page_title("browse"), F_TITLE, C_INK);
        b_sub = sub1(p, "Media and files. A adds to the menu, C copy, M move, DEL delete, F format a medium.", "A adds to the menu, C copy, M move, DEL delete, F format.");
        lv_obj_t *left = box(p, CX0, top, lw, ph, C_PANEL, 1);
        b_left = left;
        b_path = fit1(txt(left, 10, 6, "", F_TEXT, C_OK, lw - 20), lw - 20);
        b_empty = txt(left, 10, 40, "", F_SMALL, C_MUTED);
        marks_build(b_hl, left, 4, 34, lw - 8, 24);
    }
    for (int i = 0; i < BROWS; i++) {
        BSTEP {
            lv_obj_t *left = b_left;
            int y = 36 + i * 26;
            b_mark[i] = txt(left, 12, y, "", F_SMALL, C_MUTED);
            b_name[i] = fit1(txt(left, 34, y, "", F_SMALL, C_INK, lw - 34 - szw - 20), lw - 34 - szw - 20);
            b_size[i] = fit1(txt(left, lw - szw - 10, y, "", F_SMALL, C_MUTED, szw), szw);
        }
    }
    BSTEP {
        b_right = box(p, CX0 + lw + 16, top, rw, ph, C_PANEL, 1);
        r_title = fit1(txt(b_right, 12, COMPACT ? 8 : 10, "", F_TEXT, C_INK, rw - 24), rw - 24);
        static const char *keys[7] = { "Version", "Contract", "FPGA", "Core", "Application", "SHA-256", "Status" };
        int ky = COMPACT ? 36 : 44, kp = COMPACT ? 20 : 22;
        for (int i = 0; i < 7; i++) {
            r_key[i] = fit1(txt(b_right, 12, ky + i * kp, keys[i], F_SMALL, C_MUTED, COMPACT ? rk - 12 : 0), COMPACT ? rk - 12 : 200);
            r_val[i] = fit1(txt(b_right, rk, ky + i * kp, "", F_SMALL, C_INK, rw - rk - 12), rw - rk - 12);
        }
        r_msg = txt(b_right, 12, ky + 7 * kp + 8, "", F_SMALL, C_OK, rw - 24);
        r_err = txt(b_right, 12, 40, "", F_SMALL, C_MUTED, rw - 24);
        b_note = fit1(txt(p, CX0, CH - 26, "", F_SMALL, C_MUTED, CW), CW);
    }
}

static void set_rkeys(int mode)
{
    EN(A_BROWSE);
    if (mode == rk_mode) {
        return;
    }
    for (int i = 0; i < 7; i++) {
        set_txt(r_key[i], hc_panel_key(mode, i));
    }
    rk_mode = mode;
}

// A cell of a page: its text when the core says it is to be written, its colour when it names one.
static void put_cell(lv_obj_t *l, const HcCell &c)
{
    if (c.set) {
        set_txt(l, c.text);
    }
    if (c.tone) {
        set_col(l, tone_col(c.tone));
    }
}

// The right panel of the browser (the core makes it: the manifest of the selection, the medium, the update, the note).
static void browse_right_update(void)
{
    EN(A_BROWSE);
    static HcPanel p;
    hc_browse_panel(&p);
    set_rkeys(p.keys_mode);
    for (int i = 0; i < 7; i++) {
        set_hidden(r_key[i], !p.kv_vis[i]);
        set_hidden(r_val[i], !p.kv_vis[i]);
    }
    set_hidden(r_msg, !p.msg_vis);
    set_hidden(r_err, !p.err_vis);
    put_cell(r_title, p.title);
    for (int i = 0; i < 7; i++) {
        put_cell(r_val[i], p.val[i]);
    }
    put_cell(r_msg, p.msg);
    put_cell(r_err, p.err);
}

// Whole list: path, the visible window of rows, mark, manifest panel. Only the labels whose text changes are touched; LVGL keeps
// 128 invalid areas (lv_conf.h), enough for 13 rows of three labels, so no rectangle is saved first any more.
// The mark follows the selection: its four strips move to the row (the old and the new place are redrawn, nothing else).
static void browse_mark(void)
{
    EN(A_BROWSE);
    static int shown_y = 34;
    int y = 36 + (nv.bsel - nv.bfirst) * 26 - 2;
    for (int i = 0; i < 4; i++) {
        set_hidden(b_hl[i], !nv.bn);
    }
    if (y != shown_y) {
        static const int dy[4] = { 0, 22, 0, 0 };
        for (int i = 0; i < 4; i++) {
            lv_obj_set_y(b_hl[i], y + dy[i]);
        }
        shown_y = y;
    }
}

// The footer of the File Browser for what is selected (T111).
static void browse_foot(void)
{
    if (!hc_cfgov_is_open()) {                            // T198: the overlay's keys are the footer while it is open
        set_foot(foot_label, hc_browse_foot());
    }
}

// The key line under the title; with a .zip selected it names Z too, when the longer sentence still fits the column whole (T197).
static void browse_sub_update(void)
{
    EN(A_BROWSE);
    static const char *const base_full = "Media and files. A adds to the menu, C copy, M move, DEL delete, F format a medium.";
    static const char *const zip_full = "Media and files. A adds to the menu, C copy, M move, DEL delete, F format, Z unzip.";
    static const char *const base_compact = "A adds to the menu, C copy, M move, DEL delete, F format.";
    static const char *const zip_compact = "A adds to the menu, C copy, M move, DEL delete, F format, Z unzip.";
    HcBrowseRow sel;
    bool zip = hc_browse_row(nv.bsel, &sel) && sel.mark[0] != '>' && hc_is_zip_name(sel.name) && !hc_browse_at_media();
    const char *t = COMPACT ? base_compact : base_full;
    if (zip) {
        const char *z = COMPACT ? zip_compact : zip_full;
        lv_point_t sz;
        lv_text_get_size(&sz, z, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= CW) {
            t = z;
        }
    }
    set_txt(b_sub, t);
}

static void update_browse(void)
{
    EN(A_BROWSE);
    char t[200];
    browse_sub_update();
    hc_browse_title(t, sizeof(t));
    set_txt(b_path, t);
    hc_nav_browse_first(BROWS);                              // T209 D3: scrolls while 2 rows are still hidden ahead of the selection (the model moves it; hc_nav() is const)
    hc_browse_empty_text(t, sizeof(t));
    set_txt(b_empty, t);
    for (int i = 0; i < BROWS; i++) {
        HcBrowseRow r;
        bool on = hc_browse_row(nv.bfirst + i, &r);
        set_hidden(b_mark[i], !on);
        set_hidden(b_name[i], !on);
        set_hidden(b_size[i], !on);
        if (!on) {
            continue;
        }
        set_txt(b_mark[i], r.mark);
        set_txt_mid(b_name[i], r.name);                       // T209: a long name is cut in the middle
        set_txt(b_size[i], r.size);
    }
    browse_mark();
    browse_right_update();
    browse_foot();
    set_txt(b_note, nv.note);
    set_col(b_note, note_color());
}

// ---- T184: the network page (F7): the host app's Ethernet and FTP, and the board's WiFi state; the WiFi is set on the settings page (D3) ---------
// F7 shows Status, Connected to, IP address and MAC read-only (the items of Gideon's WiFi menu, network_esp32.cc wifi_config; with U64 == 2 there is no
// "WiFi Enabled"). The WiFi page of F8 (build_wm) holds Use DHCP and the four static addresses as rows you change, and Select AP from list, Enter AP
// manually, Connect to last AP, Disconnect and Forget APs as keys (its footer). The module is only asked by the 3 s status refresh and by the modal
// lists, and never while a view is built; connect, disconnect and forget run on a task of their own (wifinet.cc).
static lv_obj_t *n_wst, *n_wnet, *n_wip, *n_wmac, *n_note, *n_snm[NS_N], *n_sval[NS_N];
static lv_obj_t *n_smk[NS_N][4];
static lv_obj_t *wm_root, *wm_a, *wm_b, *wm_c, *wm_note;   // the WiFi page of the settings (F8): its root, the three state lines, its note

static void build_net(void)
{
    lv_obj_t *p = bview(V_NET);
    int pitch = COMPACT ? 19 : 22, ph = 4 * pitch + (COMPACT ? 12 : 14), y0 = COMPACT ? 62 : 74;
    int vx = COMPACT ? 126 : 160, vw = CW - vx - 12;
    BSTEP {
        txt(p, CX0, COMPACT ? 8 : 14, page_title("net"), F_TITLE, C_INK);
        sub1(p, "The host app's own stack (lwIP). Lets a PC copy .cor bundles onto SD, USB and the flash disk.", "A PC can copy .cor bundles onto SD, USB and flash.");
        lv_obj_t *pn = box(p, CX0, y0, CW, ph, C_PANEL, 1);
        int y = COMPACT ? 6 : 7;
        txt(pn, 12, y, item_title("net", "ethernet"), F_SMALL, C_MUTED);
        n_eth = fit1(txt(pn, vx, y, "", F_SMALL, C_OK, vw), vw);
        y += pitch;
        txt(pn, 12, y, item_title("net", "ftp"), F_SMALL, C_MUTED);
        n_ftp = fit1(txt(pn, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pn, 12, y, item_title("net", "media"), F_SMALL, C_MUTED);
        n_media = fit1(txt(pn, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pn, 12, y, "REST", F_SMALL, C_MUTED);
        n_rest = fit1(txt(pn, vx, y, "", F_SMALL, C_INK, vw), vw);
    }
    BSTEP {
        int y1 = y0 + ph + (COMPACT ? 6 : 8);
        lv_obj_t *pw = box(p, CX0, y1, CW, ph, C_PANEL, 1);
        int y = COMPACT ? 6 : 7;
        txt(pw, 12, y, item_title("net", "wifi_state"), F_SMALL, C_MUTED);
        n_wst = fit1(txt(pw, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pw, 12, y, item_title("net", "wifi_net"), F_SMALL, C_MUTED);
        n_wnet = fit1(txt(pw, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pw, 12, y, item_title("net", "wifi_addr"), F_SMALL, C_MUTED);
        n_wip = fit1(txt(pw, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pw, 12, y, item_title("net", "wifi_mac"), F_SMALL, C_MUTED);
        n_wmac = fit1(txt(pw, vx, y, "", F_SMALL, C_MUTED, vw), vw);
        n_note = fit1(txt(p, CX0, CH - (COMPACT ? 20 : 24), "", F_SMALL, C_MUTED, CW), CW);
    }
}

static void net_note(const char *s, int tone)
{
    hc_nnote_set(s, tone);
    printf("[host] wifi: %s\n", nv.nnote);
    hostapi_note(nv.nnote, tone == HCT_BAD);
}

static const HcRowsPlatform rows_plat = { hostapi_rest_text, net_note, APP_VERSION_STRING, APP_HOST_VERSION_STRING };

static void update_net(void)
{
    EN(A_NET);
    static HcNetTexts t;
    hc_net_texts(&t);
    put_cell(n_eth, t.eth);
    put_cell(n_ftp, t.ftp);
    put_cell(n_media, t.media);
    put_cell(n_rest, t.rest);
    put_cell(n_wst, t.wst);
    put_cell(n_wnet, t.wnet);
    put_cell(n_wip, t.wip);
    put_cell(n_wmac, t.wmac);
    put_cell(n_note, t.note);
}

// ---- T139 D4: the settings page (F8) ---------------------------------------------------------------------------------------
// Direct boot, autostart from the menu (and its seconds) and the picture, in one place. Every change is written to boot.cfg at once.
// The autostart rows only exist while direct boot is off: with it on, the menu does not come up by itself, so there is nothing to count down.
#define SET_SLOTS HC_SET_SLOTS
static lv_obj_t *s_box[SET_SLOTS], *s_name[SET_SLOTS], *s_val[SET_SLOTS], *s_help, *s_note;
static lv_obj_t *s_mk[SET_SLOTS][4];
static void fill_snapshot(WebSnapshot *w);

// ---- T148 D7: the page "Cores and ROMs" in the settings (coremaint.cc does the reading and the removing) -----------------------
// Opens over the settings rows. LIST: one line per core, then what belongs to no core, then what is there twice; UP/DOWN scroll. DEL asks to
// delete the files that belong to no core: ASK1 lists them and asks, ASK2 asks again; only Y answers yes, RETURN and every other key is No.
enum { CMM_OFF = HCM_OFF, CMM_LIST = HCM_LIST, CMM_ASK1 = HCM_ASK1, CMM_ASK2 = HCM_ASK2 };
#define CM_ROWS_MAX 24
#define CM_ROWS (cm_rows_n)                    // layout_counts(): what the canvas height holds (22 on 960x540)
static int cm_rows_n = 1;                      // set by layout_counts() before any view is built
#define CM_LINE 112
#define CM_LINES 200
static lv_obj_t *cm_root, *cm_head, *cm_row[CM_ROWS_MAX], *cm_note;

static uint32_t tone_col(int t);
static const HcFlowEnv &flow_env_ref(void);
static void cm_ask(void *, const char *text, int yes, int no) { hostapi_ask(text, yes, no); }
static const HcCmEnv &cm_env_ref(void)
{
    static HcCmEnv env;
    env.cm = hostfs_cm();
    env.fo = hostfs_fo();
    env.flow = &flow_env_ref();
    env.ask = cm_ask;
    env.ctx = 0;
    return env;
}

static void build_cm(lv_obj_t *p)
{
    BSTEP {
        cm_root = box(p, 0, 0, SW - NAV_W, CH, C_BG, 0);
        set_hidden(cm_root, true);
        txt(cm_root, CX0, COMPACT ? 8 : 14, page_title("settings/cores"), F_TITLE, C_INK);
        cm_head = fit1(txt(cm_root, CX0, COMPACT ? 38 : 46, "", F_SMALL, C_MUTED, CW), CW);
    }
    for (int i0 = 0; i0 < CM_ROWS; i0 += 4) {
        BSTEP {
            for (int i = i0; i < CM_ROWS && i < i0 + 4; i++) {
                cm_row[i] = fit1(txt(cm_root, CX0, (COMPACT ? 60 : 70) + i * 20, "", F_SMALL, C_INK, CW), CW);
            }
        }
    }
    BSTEP {
        cm_note = fit1(txt(cm_root, CX0, CH - 26, "", F_SMALL, C_MUTED, CW), CW);
    }
}

static void cm_update(void)
{
    set_hidden(cm_root, nv.cm_mode == CMM_OFF);
    if (nv.cm_mode == CMM_OFF) {
        set_foot(foot_label, hc_foot(HCF_SET));
        return;
    }
    hc_cm_clamp_top();
    int hone;
    char h[HC_CM_LINE];
    hc_cm_head(h, sizeof(h), &hone);
    set_txt(cm_head, h);
    set_col(cm_head, tone_col(hone));
    for (int i = 0; i < CM_ROWS; i++) {
        int t = HCT_INK;
        const char *l = hc_cm_line(nv.cm_top + i, &t);
        set_txt(cm_row[i], l);
        set_col(cm_row[i], nv.cm_top + i < hc_cm_lines() ? tone_col(t) : C_INK);
    }
    int mt;
    const char *m = hc_cm_msg(&mt);
    set_txt(cm_note, nv.cm_mode == CMM_LIST ? m : "");
    set_col(cm_note, tone_col(mt));
    set_foot(foot_label, hc_cm_foot());
}

// ---- T184 D3: the WiFi page of the settings (F8) ------------------------------------------------------------------------------
// Opens over the settings rows (like Cores and ROMs): the state of the WiFi, then Use DHCP and the four static addresses as rows you change; the
// access point list, the manual entry, the last access point, disconnect and forget are its keys (foot_wm). Every change is written to the WiFi store.
static void build_wm(lv_obj_t *p)
{
    int pitch = COMPACT ? 19 : 22, ph = 3 * pitch + (COMPACT ? 12 : 14), y0 = COMPACT ? 62 : 74;
    int vx = COMPACT ? 126 : 160, vw = CW - vx - 12;
    int sh = COMPACT ? 24 : 26, sp = sh + 3, y2 = y0 + ph + (COMPACT ? 6 : 8);
    int nw = 200, sx = 14 + nw + 10;
    BSTEP {
        wm_root = box(p, 0, 0, SW - NAV_W, CH, C_BG, 0);
        set_hidden(wm_root, true);
        txt(wm_root, CX0, COMPACT ? 8 : 14, page_title("settings/wifi"), F_TITLE, C_INK);
        sub1(wm_root, "Set here, saved at once. The cores show the WiFi and switch it on and off.", "Set here. The cores show it and switch it on and off.");
        lv_obj_t *pw = box(wm_root, CX0, y0, CW, ph, C_PANEL, 1);
        int y = COMPACT ? 6 : 7;
        txt(pw, 12, y, item_title("settings/wifi", "state"), F_SMALL, C_MUTED);
        wm_a = fit1(txt(pw, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pw, 12, y, item_title("settings/wifi", "net"), F_SMALL, C_MUTED);
        wm_b = fit1(txt(pw, vx, y, "", F_SMALL, C_INK, vw), vw);
        y += pitch;
        txt(pw, 12, y, item_title("settings/wifi", "addr"), F_SMALL, C_MUTED);
        wm_c = fit1(txt(pw, vx, y, "", F_SMALL, C_INK, vw), vw);
    }
    for (int i = 0; i < NS_N; i++) {
        BSTEP {
            lv_obj_t *b = box(wm_root, CX0, y2 + i * sp, CW, sh, C_PANEL, 1, C_LINE);
            n_snm[i] = fit1(txt(b, 14, COMPACT ? 2 : 3, hc_wifi_net_set_name(i), F_TEXT, C_INK, nw), nw);
            n_sval[i] = fit1(txt(b, sx, COMPACT ? 2 : 3, "", F_TEXT, C_INK, CW - sx - 16), CW - sx - 16);
            marks_build(n_smk[i], b, 0, 0, CW, sh);
        }
    }
    BSTEP {
        wm_note = fit1(txt(wm_root, CX0, CH - (COMPACT ? 20 : 24), "", F_SMALL, C_MUTED, CW), CW);
    }
}

static void wm_update(void)
{
    set_hidden(wm_root, !nv.wm_mode);
    if (!nv.wm_mode) {
        return;
    }
    static HcWifiTexts t;
    hc_wifi_texts(&t);
    put_cell(wm_a, t.a);
    put_cell(wm_b, t.b);
    put_cell(wm_c, t.c);
    for (int i = 0; i < NS_N; i++) {
        put_cell(n_sval[i], t.val[i]);
        set_col(n_snm[i], t.name_dim[i] ? C_MUTED : C_INK);
        marks_show(n_smk[i], i == nv.nsel);
    }
    put_cell(wm_note, t.note);
    set_foot(foot_label, foot_wm);
}

// ---- T187 D5: the Picture page of the settings (F8 > Picture, the V key) ----------------------------------------------------------------------
// The modes the screen names, one row each (Auto first): name, refresh, and at the end the tags "now" / "native". The same rows, texts, cursor and keys as the core's
// page (picmodes.cc). Browsing applies nothing; RETURN uses the mode under the cursor and the 8-second question comes up (pq_*).
#define PM_ROWS (1 + VID_HDMI_COUNT)
static lv_obj_t *pm_root, *pm_info, *pm_note;
static lv_obj_t *pm_box[PM_ROWS], *pm_name[PM_ROWS], *pm_hz[PM_ROWS], *pm_tag[PM_ROWS], *pm_mk[PM_ROWS][4];

static void build_pm(lv_obj_t *p)
{
    int sh = COMPACT ? 24 : 30, sp = sh + (COMPACT ? 3 : 4), y0 = COMPACT ? 76 : 96;
    int nw = COMPACT ? 250 : 330, hx = 14 + nw + 10, hw = COMPACT ? 90 : 120, tx = hx + hw + 10;
    BSTEP {
        pm_root = box(p, 0, 0, SW - NAV_W, CH, C_BG, 0);
        set_hidden(pm_root, true);
        txt(pm_root, CX0, COMPACT ? 8 : 14, "Settings > Picture", F_TITLE, C_INK);
        pm_info = txt(pm_root, CX0, COMPACT ? 40 : 48, "", F_SMALL, C_MUTED, CW);
    }
    for (int i = 0; i < PM_ROWS; i++) {
        BSTEP {
            pm_box[i] = box(pm_root, CX0, y0 + i * sp, CW, sh, C_PANEL, 1, C_LINE);
            pm_name[i] = fit1(txt(pm_box[i], 14, COMPACT ? 2 : 4, "", F_TEXT, C_INK, nw), nw);
            pm_hz[i] = fit1(txt(pm_box[i], hx, COMPACT ? 2 : 4, "", F_TEXT, C_MUTED, hw), hw);
            pm_tag[i] = fit1(txt(pm_box[i], tx, COMPACT ? 2 : 4, "", F_TEXT, C_HOST, CW - tx - 16), CW - tx - 16);
            marks_build(pm_mk[i], pm_box[i], 0, 0, CW, sh);
        }
    }
    BSTEP {
        pm_note = fit1(txt(pm_root, CX0, CH - 26, "", F_SMALL, C_MUTED, CW), CW);      // where the settings rows put theirs
    }
}

static void pm_update(void)
{
    set_hidden(pm_root, !nv.pm_mode);
    if (!nv.pm_mode) {
        return;
    }
    const PicPage &pp = hc_ui_pm();
    char s[160], hz[24], tag[24];
    picmodes_info(&pp.pm, s, sizeof(s));
    set_txt(pm_info, s);
    for (int i = 0; i < PM_ROWS; i++) {
        set_hidden(pm_box[i], i >= pp.pm.n);
        if (i >= pp.pm.n) {
            continue;
        }
        picmodes_refresh_text(&pp.pm, i, hz, sizeof(hz));
        picmodes_tag_text(&pp.pm, i, tag, sizeof(tag));
        set_txt(pm_name[i], pp.pm.row[i].name);
        set_txt(pm_hz[i], hz);
        set_txt(pm_tag[i], tag);
        marks_show(pm_mk[i], i == pp.sel);
    }
    set_txt(pm_note, nv.note);
    set_col(pm_note, note_color());
    set_foot(foot_label, hv_state()->pending ? picmodes_confirm_keys() : foot_pm);       // T187 D9: while the question is up the bar says its keys
}

static void build_settings(void)
{
    lv_obj_t *p = bview(V_SET);
    int pitch = COMPACT ? 29 : 34, sh = COMPACT ? 26 : 30, y0 = COMPACT ? 62 : 70;           // ten rows (T163 S4: FTP service) in the height nine had
    int nw = 290, vx = 330;
    if (COMPACT) {                                  // the label column is the width of the longest label (measured, as the File Browser's keys), the value takes the rest
        int lw = 0;
        for (int sk = 0; sk < SET_SLOTS; sk++) {
            const char *nm = hc_set_name(sk);
            lv_point_t sz;
            lv_text_get_size(&sz, nm, F_TEXT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lw = sz.x > lw ? sz.x : lw;
        }
        nw = lw + 4;
        vx = 14 + nw + 12;
    }
    BSTEP {
        txt(p, CX0, COMPACT ? 8 : 14, page_title("settings"), F_TITLE, C_INK);
        sub1(p, "Stored on the flash disk (boot.cfg) and saved at once. UP/DOWN select, LEFT/RIGHT or RETURN change.", "Saved at once. UP/DOWN select, LEFT/RIGHT change.");
    }
    for (int i = 0; i < SET_SLOTS; i++) {
        BSTEP {
            s_box[i] = box(p, CX0, y0 + i * pitch, CW, sh, C_PANEL, 1, C_LINE);
            s_name[i] = fit1(txt(s_box[i], 14, COMPACT ? 3 : 5, "", F_TEXT, C_INK, nw), nw);
            s_val[i] = fit1(txt(s_box[i], vx, COMPACT ? 3 : 5, "", F_TEXT, C_INK, CW - vx - 20), CW - vx - 20);
            marks_build(s_mk[i], s_box[i], 0, 0, CW, sh);
        }
    }
    BSTEP {
        s_help = txt(p, CX0, y0 + SET_SLOTS * pitch + 4, "", F_SMALL, C_MUTED, CW);
        s_note = fit1(txt(p, CX0, CH - 26, "", F_SMALL, C_MUTED, CW), CW);
    }
    build_cm(p);
    build_wm(p);
    build_pm(p);
}

static void update_settings(void)
{
    EN(A_SET);
    cm_update();
    if (nv.cm_mode != CMM_OFF) {
        return;
    }
    wm_update();
    pm_update();
    if (nv.wm_mode || nv.pm_mode) {
        return;
    }
    int k[SET_SLOTS], n = hc_set_kinds(k);
    hc_nav_clamp_ssel(n);
    const char *help = "";
    for (int i = 0; i < SET_SLOTS; i++) {
        set_hidden(s_box[i], i >= n);
        if (i >= n) {
            continue;
        }
        char v[64];
        const char *name = "";
        const char *row_help = "";
        hc_set_row_text(k[i], name, v, row_help, i == nv.ssel);
        if (i == nv.ssel) {
            help = row_help;
        }
        set_txt(s_name[i], name);
        set_txt(s_val[i], v);
        marks_show(s_mk[i], i == nv.ssel);
    }
    set_txt(s_help, help);
    set_txt(s_note, nv.note);
    set_col(s_note, note_color());
}

// The menu canvas (SW x SH, laid out for 540 lines) in the mode that is up: a whole factor only, max(1, active height / 540), centred in the
// active area, the rest black; a canvas larger than the frame starts at 0 and is cut by it (the core app's Video::place_menu does the same). The
// pixel layer scales to this size (PX_OSIZE).
static bool ui_built;                                  // build_ui() and build_start() ran: the widgets exist
static void build_ui(void);
static void build_start(void);
static void areas_forget(void);
static void build_rest_all(void);
static void buffers_mode(bool single);
static void overlay_state_forget(void);             // what the overlay panels remember of what they show (below, after their statics)

// How many rows each list holds on the canvas that is up (the full canvas: 9 menu rows, 5 editor rows, 12 browser rows, 18 list rows; the compact layout
// takes what its height holds, from the same positions the builders use).
static void layout_counts(void)
{
    hc_rows_set_compact(COMPACT);
    // T209 D8: the editor holds what fits between its panel and its note, at the pitch of the boot menu's old row.
    // T209 D6: the boot menu keeps its full-size logo (LROW_H high, LROW_P pitch); both canvases hold what fits between the list's top and its detail line - the row count gives, not the logo.
    int m = (CH - 26 - 6 - MENU_ROW0 + 2) / LROW_P;      // down to the status line at CH - 26
    int mc = (CH - 56 - 6 - MENU_ROW0 + 2) / LROW_P;      // down to the status line at CH - 56, above the countdown line
    mrows_full = m < 1 ? 1 : (m > MROWS_MAX ? MROWS_MAX : m);
    mrows_cd = mc < 1 ? 1 : (mc > mrows_full ? mrows_full : mc);
    mrows_n = mrows_full;
    int e = (CH - 26 - ((COMPACT ? 64 : 74) + ED_PN_H + 12) + 4) / EROW_P;
    erows_n = e < 1 ? 1 : (e > MROWS_MAX ? MROWS_MAX : e);
    if (!COMPACT) {
        brows_n = (CH - 74 - 30 - 40) / 26;                // T223 R2: the panel ends above the note line; the last row sits inside it
        cm_rows_n = (CH - 26 - 6 - 70 + 2) / 20;
        hc_nav_geometry(mrows_n, brows_n, cm_rows_n, erows_n);
        return;
    }
    brows_n = (CH - 64 - 30 - 40) / 26;                  // the panel's height (see the browser builder) less its first row's offset
    cm_rows_n = (CH - 26 - 6 - 60 + 2) / 20;
    brows_n = brows_n < 1 ? 1 : (brows_n > BROWS_MAX ? BROWS_MAX : brows_n);
    cm_rows_n = cm_rows_n < 1 ? 1 : (cm_rows_n > CM_ROWS_MAX ? CM_ROWS_MAX : cm_rows_n);
    hc_nav_geometry(mrows_n, brows_n, cm_rows_n, erows_n);
}

// The canvas changed (a mode below 960 wide, or back): the view, the port and the display take it, every widget is built again for it and drawn at
// the next render. The state of the pages (selection, windows, notes) is kept; the widgets are not.
static void show_view(View v);
static void canvas_apply(const TrxCanvas &c)
{
    if (c.w == trx_canvas_w() && c.h == trx_canvas_h()) {
        return;
    }
    trx_canvas_set(c.w, c.h);
    trx_lv_set_canvas(c.w, c.h);
    layout_counts();
    if (have_ui && trx_splash_up()) {
        trx_splash_relayout(trx_overlay());          // the start-up splash is up while the mode is found: drawn again for the canvas
    }
    if (!have_ui || !ui_built) {
        return;
    }
    buffers_mode(true);
    snap_shown = false;                              // a saved picture of the other canvas is not on the layer any more
    lv_obj_clean(scr);                               // the pages, the overlay container and its panels
    memset(mrow, 0, sizeof(mrow));
    memset(erow, 0, sizeof(erow));
    areas_forget();                                  // T229: an area that is not built again yet has no widgets (its pointers would dangle)
    chips_sig[0] = 0;
    rk_mode = -1;
    nav_shown = -1;
    cd_last_pct = -1;
    overlay_state_forget();
    build_ui();
    if (!lazy_build) {
        build_rest_all();
    }
    show_view(cur_view());                                 // build_ui() makes every view's container hidden: without this the page in view (and its message line) stays blank
    need_full = true;
}

// The menu canvas (trxcanvas.h) in the mode that is up: 960 x 540 at a whole factor, max(1, active height / 540), or the mode's own size below 960 wide,
// centred in the active area, the rest black (the core app's Video::place_menu does the same). The pixel layer scales to this size (PX_OSIZE).
static void menu_place(void)
{
    int fw = 0, fh = 0;
    video_active_size(hc_picture_hdmi() < 0 ? VID_HDMI_1080P : hc_picture_hdmi(), hc_picture_sys(), &fw, &fh);
    if (fw <= 0 || fh <= 0) {
        video_active_size(VID_HDMI_1080P, hc_picture_sys(), &fw, &fh);
    }
    TrxCanvas c = trx_canvas_for(fw, fh);
    canvas_apply(c);
    trx_lv_set_place(c.x0, c.y0, c.w * c.factor, c.h * c.factor);
}

// T164 WP17: the picture the host runs follows hostvideo.cc. hc_picture_sys() / hc_picture_hdmi() mirror what is up.
static void picture_sync(void)
{
    hc_picture_sync();
    menu_place();
}

// The V key's work, shared with the settings page and the web page: `hdmi` (-1 = from the screen) is asked of the screen's list and put up; with
// the mode-request gateware the choice waits for a key (Q7), without it it is stored at once.
static int pic_shown = -1;          // the second the question shows (picture_tick)

// T164 Q7 (amended 2026-11-04): the picture question is a box in the middle of the screen: the picture, the seconds left and, after a blank line, its key
// (T152 D10). It is up as long as a change waits (hv_state()->pending) and goes with the key, the revert or a new question.
static lv_obj_t *pq_root, *pq_title, *pq_l0, *pq_l1, *pq_key;
#define PQ_P (COMPACT ? 24 : 28)
static void pq_hide(void)
{
    if (pq_root && !lv_obj_has_flag(pq_root, LV_OBJ_FLAG_HIDDEN)) {
        trx_page_hide(pq_root);
        trx_lv_handler();
    }
}
static void pq_update(void)
{
    if (!hv_state()->pending) {
        pq_hide();
        return;
    }
    EN(A_START);
    if (!pq_root) {
        return;
    }
    char now[64], prev[64], body[240], l0[96], l1[96], keys[96];
    const HvState *hs = hv_state();
    int w = SW - 40 < 560 ? SW - 40 : 560;
    int h = 58 + 4 * PQ_P + 14;
    bool fresh = lv_obj_has_flag(pq_root, LV_OBJ_FLAG_HIDDEN);
    picmodes_mode_text(hs->mask, hs->def_sys, hc_picture_hdmi(), now, sizeof(now));
    picmodes_mode_text(hs->mask, hs->def_sys, hs->prev_hdmi, prev, sizeof(prev));
    picmodes_confirm(hv_seconds_left(), now, prev, body, sizeof(body));     // T187 D5: the text both apps show; its lines: the mode, the seconds, a blank, the keys
    const char *p1 = strchr(body, '\n');
    const char *p2 = p1 ? strchr(p1 + 1, '\n') : 0;
    {   // the RISC-V image's printf ignores "%.*s": the lines are cut by hand
        size_t n0 = p1 ? (size_t)(p1 - body) : 0, n1 = p2 ? (size_t)(p2 - p1 - 1) : 0;
        n0 = n0 < sizeof(l0) - 1 ? n0 : sizeof(l0) - 1;
        n1 = n1 < sizeof(l1) - 1 ? n1 : sizeof(l1) - 1;
        memcpy(l0, body, n0);
        l0[n0] = 0;
        if (n1) {
            memcpy(l1, p1 + 1, n1);
        }
        l1[n1] = 0;
    }
    snprintf(keys, sizeof(keys), "%s", p2 && p2[1] == '\n' ? p2 + 2 : "");
    if (fresh) {
        TrxPage page(pq_root);
        lv_obj_set_size(pq_root, w, h);
        lv_obj_set_pos(pq_root, (SW - w) / 2, (SH - h) / 2);
        set_txt(pq_title, "Keep this picture?");
        set_txt(pq_l0, l0);
        set_txt(pq_l1, l1);
        set_txt(pq_key, keys);
    } else {
        set_txt(pq_l0, l0);
        set_txt(pq_l1, l1);
    }
    trx_lv_handler();
}
static bool picture_set(int hdmi, const char *how)
{
    char why[64];
    char m[112];
    if (!hc_picture_set(hdmi, why, sizeof(why))) {
        snprintf(m, sizeof(m), "Picture not changed: %s", why);
        set_note(m, HCT_BAD);
        need_full = true;
        return false;
    }
    picture_sync();
    video_current = video_pack(hc_picture_sys(), hc_picture_hdmi());
    if (hv_state()->pending) {
        pic_shown = hv_seconds_left();
        snprintf(m, sizeof(m), "Picture: %s", video_hdmi_name(hc_picture_hdmi()));
        pq_update();
    } else {
        snprintf(m, sizeof(m), "Picture: %s (%s)", video_hdmi_name(hc_picture_hdmi()), how);
        pq_hide();
    }
    set_note(m, HCT_HOST);
    need_full = true;
    return true;
}

// The loop's side of the picture: Q7's countdown and the hot-plug follow (D11b).
// A picture event (the loop's tick, or a key: HV_REVERTED): the mirror follows, the question's box goes, the note says what came up.
static void picture_event(HvEvent e)
{
    picture_sync();
    char m[112];
    if (e == HV_FOLLOWED || e == HV_REVERTED) {
        pq_hide();
    }
    if (e == HV_FOLLOWED) {
        video_current = video_pack(hc_picture_sys(), hc_picture_hdmi());
        snprintf(m, sizeof(m), "The screen changed: Picture %s", video_hdmi_name(hc_picture_hdmi()));
        set_note(m, HCT_HOST);
        need_full = true;
    } else if (e == HV_REVERTED) {
        video_current = video_pack(hc_picture_sys(), hc_picture_hdmi());
        picmodes_back_text(video_hdmi_name(hc_picture_hdmi()), m, sizeof(m));
        set_note(m, HCT_HOST);
        need_full = true;
    } else if (e == HV_CONFIRM_TICK) {
        int left = hv_seconds_left();
        if (left != pic_shown) {
            pic_shown = left;
            pq_update();
        }
    }
    if (nv.pm_mode && (e == HV_FOLLOWED || e == HV_REVERTED)) {
        hc_ui_pm_refresh(e == HV_REVERTED || e == HV_FOLLOWED);     // the page follows: a hot-plug list, or the old mode back; the cursor keeps its mode by name
        need_full = true;
    }
}
static void ue_picture_event(void *, int ev) { picture_event((HvEvent)ev); }

static void picture_tick(void)
{
    HvEvent e = hc_picture_tick();
    if (e != HV_NONE) {
        picture_event(e);
    }
}

static void go(View v);

// ---- T139 D3: rename an entry ---------------------------------------------------------------------------------------------
static lv_obj_t *rn_root, *rn_head, *rn_sub, *rn_in, *rn_cur, *rn_msg;
static char rn_for_path[sizeof(((BootEntry *)0)->path)];      // the entry being renamed, for the service's entry modal

static void build_rename(void)
{
    int w = SW - 40 < 700 ? SW - 40 : 700;
    BSTEP {
        rn_root = box(trx_overlay(), (SW - w) / 2, 150 + (SH - 540) / 2, w, 220, C_PANEL, 3, C_HOST);
        trx_page_hide(rn_root);
        rn_head = txt(rn_root, 20, 12, "Rename menu entry", F_TITLE, C_INK, w - 40);
        rn_sub = fit1(txt(rn_root, 20, 46, "", F_SMALL, C_MUTED, w - 40), w - 40);
        lv_obj_t *inbox = box(rn_root, 20, 76, w - 40, 40, C_BG, 1, C_LINE);
        rn_in = fit1(txt(inbox, 10, 10, "", F_TEXT, C_INK, w - 80), w - 80);
        rn_cur = box(inbox, 10, 7, 2, 24, C_HOST, 0);
        rn_msg = fit1(txt(rn_root, 20, 128, "", F_SMALL, C_WARN, w - 40), w - 40);
        line1(txt(rn_root, 20, 168, "RETURN saves    ESC cancels    LEFT/RIGHT HOME END move    INS blank", F_SMALL, C_MUTED, w - 40));
    }
}

// The modal itself is the core's (host_core_ui.cc, RenameJob); this draws its page.
static void rename_set(const HcRenamePage &p)
{
    EN(A_RENAME);
    set_txt(rn_head, p.head);
    hostapi_entry(p.head, rn_for_path, p.typed, "RETURN saves    ESC cancels    LEFT/RIGHT HOME END move    INS blank");
    set_txt(rn_in, p.typed);
    char left[LE_MAX + 1];
    memcpy(left, p.typed, p.cur);
    left[p.cur] = 0;
    lv_point_t sz;
    sz.x = 0;
    if (p.cur) {
        lv_text_get_size(&sz, left, F_TEXT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    }
    lv_obj_set_x(rn_cur, 10 + sz.x);
    set_txt(rn_msg, p.msg);
}
static void rn_draw(void *, const HcRenamePage &p, bool first)
{
    if (first) {
        {
            TrxPage page(rn_root);
            set_txt(rn_sub, p.sub);
            snprintf(rn_for_path, sizeof(rn_for_path), "%s", p.sub);
            rename_set(p);
        }
        trx_lv_handler();
    } else {
        rename_set(p);
        trx_lv_handler();
    }
}
static void rn_end(void *)
{
    trx_page_hide(rn_root);
    trx_lv_handler();
    hostapi_entry_end();
}
static void rn_history(void *, const char *text) { hostapi_history(text); }
static const HcRenameEnv rename_env = { rn_draw, rn_end, rn_history, 0 };

// ---- T184: the WiFi flows of the network page ---------------------------------------------------------------------------------
// Two panels over the screen, both built once: a pick list (the access points, the authentication) and an entry field (SSID, password, the static
// addresses). Modal like rename_flow: each returns when the person has chosen or cancelled. A password is masked, and its buffer is wiped after use.
#define WL_MAX 10
#define WL_APS 32
static lv_obj_t *wl_root, *wl_title, *wl_sub, *wl_a[WL_MAX], *wl_b[WL_MAX], *wl_c[WL_MAX], *wl_msg, *wl_keys;
static lv_obj_t *wl_mk[WL_MAX][4];
// T198: the config overlay - its own panel (two-line rows, a height that follows the row count) under a dimmer that covers the page but not the key bar
#define OC_MAX HC_CFGOV_ROWS
#define OC_MIN 5
static lv_obj_t *oc_root, *oc_panel, *oc_title, *oc_sub, *oc_msg, *oc_a[OC_MAX], *oc_df[OC_MAX], *oc_mk[OC_MAX][4];
static int oc_rp, oc_hdr, oc_ftr, oc_h, oc_ay;           // oc_ay: the name's offset inside its row (centred)
static int wl_rows;                               // the rows this canvas holds
static lv_obj_t *we_root, *we_title, *we_sub, *we_in, *we_msg, *we_keys;

static void build_wifi_panels(void)
{
    int w = SW - 40 < 700 ? SW - 40 : 700;
    int rp = COMPACT ? 22 : 26, hdr = 76, ftr = 54;
    wl_rows = (SH - 24 - hdr - ftr) / rp;
    wl_rows = wl_rows > WL_MAX ? WL_MAX : (wl_rows < 3 ? 3 : wl_rows);
    int h = hdr + wl_rows * rp + ftr;
    int xb = w - 20 - 250, xc = w - 20 - 90;
    int yb = hdr + wl_rows * rp + 6;
    BSTEP {
        wl_root = box(trx_overlay(), (SW - w) / 2, (SH - h) / 2, w, h, C_PANEL, 3, C_HOST);
        trx_page_hide(wl_root);
        wl_title = fit1(txt(wl_root, 20, 12, "", F_TITLE, C_INK, w - 40), w - 40);
        wl_sub = fit1(txt(wl_root, 20, 46, "", F_SMALL, C_MUTED, w - 40), w - 40);
    }
    for (int i = 0; i < WL_MAX; i++) {
        BSTEP {
            int y = hdr + i * rp;
            marks_build(wl_mk[i], wl_root, 12, y, w - 24, rp - 2);
            wl_a[i] = fit1(txt(wl_root, 24, y + (COMPACT ? 2 : 3), "", F_TEXT, C_INK, xb - 36), xb - 36);
            wl_b[i] = fit1(txt(wl_root, xb, y + (COMPACT ? 3 : 4), "", F_SMALL, C_MUTED, 150), 150);
            wl_c[i] = fit1(txt(wl_root, xc, y + (COMPACT ? 3 : 4), "", F_SMALL, C_INK, 80), 80);
            lv_obj_set_style_text_align(wl_c[i], LV_TEXT_ALIGN_RIGHT, 0);
        }
    }
    BSTEP {
        wl_msg = fit1(txt(wl_root, 20, yb, "", F_SMALL, C_WARN, w - 40), w - 40);
        wl_keys = line1(txt(wl_root, 20, yb + 22, "", F_SMALL, C_MUTED, w - 40));
    }
    // T198: the config overlay. The dimmer comes first (below the panel); it stops above the key bar, which shows the overlay's keys and stays bright.
    const int ow = SW - 40 < 600 ? SW - 40 : 600;
    oc_hdr = 76;
    oc_ftr = 34;
    oc_rp = (SH - FOOT_H - 24 - oc_hdr - oc_ftr) / OC_MAX;
    oc_rp = oc_rp > (COMPACT ? 26 : 32) ? (COMPACT ? 26 : 32) : oc_rp;            // one line per row (T198 D16), T209 D2: closer
    oc_ay = (oc_rp - 2 - lv_font_get_line_height(F_TEXT)) / 2;
    oc_h = oc_hdr + OC_MIN * oc_rp + oc_ftr;
    BSTEP {
        oc_root = box(trx_overlay(), 0, 0, SW, SH - FOOT_H, 0x05080e, 0);        // the dimmer is the page's root: one TrxPage shows both
        lv_obj_set_style_bg_opa(oc_root, 184, 0);                                 // 72 %, as the mock-up's dimmer
        trx_page_hide(oc_root);
        oc_panel = box(oc_root, (SW - ow) / 2, (SH - FOOT_H - oc_h) / 2, ow, oc_h, C_PANEL, 3, C_HOST);
        oc_title = fit1(txt(oc_panel, 20, 12, "", F_TITLE, C_INK, ow - 40), ow - 40);
        oc_sub = fit1(txt(oc_panel, 20, 46, "", F_SMALL, C_MUTED, ow - 40), ow - 40);
    }
    for (int i = 0; i < OC_MAX; i++) {
        BSTEP {
            int y = oc_hdr + i * oc_rp;
            marks_build(oc_mk[i], oc_panel, 12, y, ow - 24, oc_rp - 2);
            oc_a[i] = fit1(txt(oc_panel, 24, y + oc_ay, "", F_TEXT, C_INK, ow - 48 - DEF_W - 8), ow - 48 - DEF_W - 8);
            oc_df[i] = box(oc_panel, ow - 24 - DEF_W, y + (oc_rp - 2 - 22) / 2, DEF_W, 22, C_HOST, 0);      // the boot menu row's "auto" badge
            lv_obj_set_style_text_align(txt(oc_df[i], 0, 2, "auto", F_SMALL, C_BG, DEF_W), LV_TEXT_ALIGN_CENTER, 0);
            set_hidden(oc_df[i], true);
        }
    }
    BSTEP {
        oc_msg = fit1(txt(oc_panel, 20, oc_hdr + OC_MIN * oc_rp + 6, "", F_SMALL, C_WARN, ow - 40), ow - 40);
    }
    BSTEP {
        we_root = box(trx_overlay(), (SW - w) / 2, 150 + (SH - 540) / 2, w, 220, C_PANEL, 3, C_HOST);
        trx_page_hide(we_root);
        we_title = fit1(txt(we_root, 20, 12, "", F_TITLE, C_INK, w - 40), w - 40);
        we_sub = fit1(txt(we_root, 20, 46, "", F_SMALL, C_MUTED, w - 40), w - 40);
        lv_obj_t *inbox = box(we_root, 20, 76, w - 40, 40, C_BG, 1, C_LINE);
        we_in = fit1(txt(inbox, 10, 10, "", F_TEXT, C_INK, w - 80), w - 80);
        we_msg = fit1(txt(we_root, 20, 128, "", F_SMALL, C_WARN, w - 40), w - 40);
        we_keys = line1(txt(we_root, 20, 168, "", F_SMALL, C_MUTED, w - 40));
    }
}

static const HcWifiEnv &wifi_env_ref(void);       // the flows' environment (host_core_flow_wifi.h), defined with the other flows' next to flow_env

static void wl_fill(const HcWifiPick &p)
{
    EN(A_WIFI);
    const HcWifiItem *it = p.item;
    set_txt(wl_title, p.title);
    set_txt(wl_sub, p.sub);
    set_col(wl_sub, tone_col(p.subtone));
    for (int i = 0; i < WL_MAX; i++) {
        int k = p.first + i;
        bool on = i < wl_rows && k < p.n;
        set_hidden(wl_a[i], !on);
        set_hidden(wl_b[i], !on);
        set_hidden(wl_c[i], !on);
        marks_show(wl_mk[i], on && k == p.sel);
        if (on) {
            set_txt(wl_a[i], it[k].a);
            set_txt(wl_b[i], it[k].b);
            set_txt(wl_c[i], it[k].c);
            set_col(wl_c[i], tone_col(it[k].ctone));
        }
    }
    set_txt(wl_msg, p.msg);
    set_col(wl_msg, tone_col(p.msgtone));
    set_txt(wl_keys, p.keys);
}

// T198: the config overlay's page. Rows: one line (the name), the "default" badge right; the panel is as high as its rows (OC_MIN..OC_MAX).
static void oc_fill(const HcWifiPick &p)
{
    EN(A_WIFI);
    const int rows = p.n < OC_MIN ? OC_MIN : (p.n > OC_MAX ? OC_MAX : p.n);
    const int h = oc_hdr + rows * oc_rp + oc_ftr;
    if (h != oc_h) {
        oc_h = h;
        lv_obj_set_height(oc_panel, h);
        lv_obj_set_y(oc_panel, (SH - FOOT_H - h) / 2);
        lv_obj_set_y(oc_msg, oc_hdr + rows * oc_rp + 6);
    }
    {
        // the title is "<entry name> - start with": a name that does not fit is cut with "..." in front of the suffix, the suffix stays
        char t[sizeof(p.title)];
        snprintf(t, sizeof(t), "%s", p.title);
        const char *suf = strstr(p.title, " - start with");
        const lv_font_t *f = lv_obj_get_style_text_font(oc_title, LV_PART_MAIN);
        const int room = (int)(intptr_t)lv_obj_get_user_data(oc_title) - 4;
        char *dots = t + (suf ? suf - p.title : (int)strlen(t));
        for (;;) {
            lv_point_t sz;
            lv_text_get_size(&sz, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (sz.x <= room || dots - t <= 1) {
                break;
            }
            dots--;                                                         // drop one more character of the name (UTF-8 continuation bytes with it)
            while (dots > t + 1 && ((unsigned char)*dots & 0xC0) == 0x80) {
                dots--;
            }
            snprintf(dots, sizeof(t) - (size_t)(dots - t), "...%s", suf ? suf : "");
        }
        set_txt(oc_title, t);
    }
    set_txt(oc_sub, p.sub);
    set_col(oc_sub, tone_col(p.subtone));
    for (int i = 0; i < OC_MAX; i++) {
        const bool on = i < p.n;
        set_hidden(oc_a[i], !on);
        marks_show(oc_mk[i], on && i == p.sel);
        set_hidden(oc_df[i], !(on && p.item[i].c[0]));
        if (on) {
            set_txt(oc_a[i], p.item[i].a);
        }
    }
    set_txt(oc_msg, p.msg);
    set_col(oc_msg, tone_col(p.msgtone));
    set_foot(foot_label, p.keys);                          // one key line: the screen's, as the mock-up
}

// The pick list as the screen draws it and tells the service (fresh: the panel opens).
static void wifi_pick_show(const HcWifiPick &p, bool fresh)
{
    EN(A_WIFI);
    const char *labels[HCW_PICK_MAX];
    for (int i = 0; i < p.n && i < HCW_PICK_MAX; i++) {
        labels[i] = p.item[i].a;
    }
    hostapi_pick(p.title, labels, p.n < HCW_PICK_MAX ? p.n : HCW_PICK_MAX, p.sel, p.sub);
    if (p.cfgov) {
        if (fresh) {
            TrxPage page(oc_root);
            oc_fill(p);
        } else {
            oc_fill(p);
        }
        return;
    }
    if (fresh) {
        TrxPage page(wl_root);
        wl_fill(p);
    } else {
        wl_fill(p);
    }
}

static void we_draw(const HcWifiEntry &e)
{
    EN(A_WIFI);
    hostapi_entry(e.title, e.sub, e.shown, e.keys);
    set_txt(we_in, e.row);
    set_txt(we_msg, e.msg);
}

static int wifi_entry_vis(void)
{
    EN(A_WIFI);
    lv_point_t sz;
    lv_text_get_size(&sz, "0", F_TEXT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return (lv_obj_get_width(we_root) - 100) / (sz.x > 0 ? sz.x + 1 : 10);
}

// ---- layer proof (colour screen), built when first used ---------------------------------------------------------
static void buffer_report(void);
static void buffers_mode(bool single);
static void build_colors(void)
{
    static const uint32_t sw[] = { 0x000000, 0x070a12, 0x0d1220, 0x111827, 0x273047, 0x404040, 0x808080, 0xc0c0c0, 0xffffff,
                                   0xff0000, 0x00ff00, 0x0000ff, 0x800000, 0x008000, 0x000080, 0x8376ff, 0x6de7ff, 0xff7a72, 0xffc15a };
    BSTEP {
        vwin[V_COLORS] = box(scr, 0, 0, SW, SH, C_BG, 0);
        set_hidden(vwin[V_COLORS], true);
    }
    lv_obj_t *p = vwin[V_COLORS];
    for (int i0 = 0; i0 < (int)(sizeof(sw) / sizeof(sw[0])); i0 += 5) {
        BSTEP {
            for (int i = i0; i < (int)(sizeof(sw) / sizeof(sw[0])) && i < i0 + 5; i++) {
                int cols = COMPACT ? 4 : 5, pitch = COMPACT ? 150 : 180, bw = COMPACT ? 134 : 160, bh = COMPACT ? 62 : 90, py = COMPACT ? 76 : 110;
                int x = 20 + (i % cols) * pitch, y = (COMPACT ? 48 : 60) + (i / cols) * py;
                box(p, x, y, bw, bh, sw[i], 0);
                char t[16];
                snprintf(t, sizeof(t), "%06x", (unsigned)sw[i]);
                txt(p, x, y + bh + 2, t, F_SMALL, C_INK);
            }
        }
    }
    BSTEP {
        lv_obj_t *ct = txt(p, 20, 12, HC_COLORS_TEXT, F_TEXT, C_INK, COMPACT ? SW - 40 : 0);
        if (COMPACT) {
            line1(ct);
        }
        c_pat = box(p, 0, 0, SW, SH, 0x000000, 0);
        set_hidden(c_pat, true);
    }
}

static void update_colors(void)
{
    EN(A_COLORS);
    if (!nv.cpat) {
        set_hidden(c_pat, true);
        return;
    }
    lv_obj_clean(c_pat);
    static const uint32_t col[] = { 0xff0000, 0x00ff00, 0x0000ff, 0x000000 };
    static const char names[] = "rgbk";
    const char *f = strchr(names, nv.cpat);
    if (f) {
        lv_obj_set_style_bg_color(c_pat, lv_color_hex(col[f - names]), 0);
    } else {
        lv_obj_set_style_bg_color(c_pat, lv_color_hex(0x000000), 0);
        if (nv.cpat == 'v') {
            for (int x = 0; x < SW; x += 2) box(c_pat, x, 0, 1, SH, 0xff0000, 0);
        } else if (nv.cpat == 'h') {
            for (int y = 0; y < SH; y += 2) box(c_pat, 0, y, SW, 1, 0xff0000, 0);
        }
    }
    set_hidden(c_pat, false);
}

// ---- shell and switching ------------------------------------------------------------------------------------------------
static void build_ui(void)
{
    lv_obj_t *top = box(scr, 0, 0, SW, TOP_H, C_PANEL, 0);
    box(top, 0, TOP_H - 1, SW, 1, C_LINE);
    txt(top, COMPACT ? 10 : 14, COMPACT ? 3 : 6, "TRX64", F_TITLE, C_INK);
    int brand_x = 100;                                                     // compact: "host" follows the measured word with a gap, at any canvas width
    if (COMPACT) {
        lv_point_t bsz;
        lv_text_get_size(&bsz, "TRX64", F_TITLE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        brand_x = 10 + bsz.x + 6;
    }
    txt(top, brand_x, COMPACT ? 7 : 10, "host", F_TEXT, C_OK);
    chips_box = lv_obj_create(top);                // transparent holder for the chips, 300 wide at the right
    lv_obj_set_flag(chips_box, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_pos(chips_box, SW - CHIPS_W - 10, 0);
    lv_obj_set_size(chips_box, CHIPS_W, TOP_H - 1);
    lv_obj_set_style_pad_all(chips_box, 0, 0);
    lv_obj_set_style_border_width(chips_box, 0, 0);
    lv_obj_set_style_radius(chips_box, 0, 0);
    lv_obj_set_style_bg_opa(chips_box, LV_OPA_TRANSP, 0);
    chips_sig[0] = 0;
    update_chips();

    lv_obj_t *nav = box(scr, 0, TOP_H, NAV_W, SH - TOP_H - FOOT_H, C_PANEL, 0);
    box(nav, NAV_W - 1, 0, 1, SH - TOP_H - FOOT_H, C_LINE);
    for (int i = 0; i < NAV_N; i++) {
        nav_box[i] = box(nav, 0, 14 + i * 42, NAV_W - 1, 38, C_PANEL, 0);
        nav_acc[i] = box(nav_box[i], 0, 0, 3, 38, C_HOST, 0);
        set_hidden(nav_acc[i], true);
        lv_obj_t *k = box(nav_box[i], COMPACT ? 8 : 14, 8, COMPACT ? 28 : 30, 22, C_PANEL, 1);
        txt(k, 4, 2, hc_nav_key(i), F_SMALL, C_MUTED);
        lv_obj_t *nl = txt(nav_box[i], COMPACT ? 42 : 54, COMPACT ? 10 : 8, nav_title(i), COMPACT ? F_SMALL : F_TEXT, C_MUTED, COMPACT ? NAV_W - 42 - 2 : 0);
        if (COMPACT) {
            line1(nl);                              // one line: the name is cut with dots before it wraps into the entry below
        }
    }
    lv_obj_t *foot = box(scr, 0, SH - FOOT_H, SW, FOOT_H, C_PANEL, 0);
    box(foot, 0, 0, SW, 1, C_LINE);
    if (COMPACT) {                                  // two lines: the keys of a page are not cut to fit 640 pixels
        foot_label = txt(foot, 10, 4, "", F_SMALL, C_MUTED, SW - 20);
        lv_label_set_long_mode(foot_label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_height(foot_label, 2 * lv_font_get_line_height(F_SMALL));
    } else {
        foot_label = fit1(txt(foot, 14, 6, "", F_SMALL, C_MUTED, SW - 28), SW - 28);
        lv_label_set_long_mode(foot_label, LV_LABEL_LONG_MODE_DOTS);
    }

    mt_mark("build_ui: header, nav, foot");
    build_menu();
    mt_mark("build_ui: menu view");
    // T229: the other areas are built after the first frame (build_pass), or when a key needs one (EN)
    memset(b_done, 0, sizeof(b_done));
    memset(b_stage, 0, sizeof(b_stage));
}

static void update_nav(void)
{
    int &shown = nav_shown;                         // only the two entries that change are restyled
    for (int i = 0; i < NAV_N; i++) {
        bool cur = (int)nv.view == i;
        if (cur != (shown == i) || shown < 0) {
            lv_obj_set_style_bg_color(nav_box[i], lv_color_hex(cur ? C_HOSTSOFT : C_PANEL), 0);
            set_hidden(nav_acc[i], !cur);
        }
    }
    shown = (int)nv.view;
}

// Brings the current view's widgets up to date (changes only what differs).
static void refresh_current(void)
{
    if (!have_ui) {
        return;
    }
    switch (nv.view) {
    case V_MENU: update_menu(); break;
    case V_BROWSE: update_browse(); break;
    case V_EDIT: update_edit(); break;
    case V_NET: update_net(); break;
    case V_SET: update_settings(); break;
    case V_COLORS: update_colors(); break;
    }
}

static void show_view(View v)
{
    if (!have_ui) {
        return;
    }
    switch (v) {                                    // T229: the view is built when it is first shown (the rest of it, when the main loop has started on it)
    case V_BROWSE: EN(A_BROWSE); break;
    case V_EDIT: EN(A_EDIT); break;
    case V_NET: EN(A_NET); break;
    case V_SET: EN(A_SET); break;
    case V_COLORS: EN(A_COLORS); break;
    default: break;
    }
    for (int i = 0; i < 6; i++) {
        set_hidden(vwin[i], i != (int)v);
    }
    update_nav();
    if (v != V_COLORS && v != V_BROWSE) {
        set_foot(foot_label, v == V_MENU ? hc_menu_foot() : v == V_EDIT ? hc_edit_foot() : hc_foot_of_view((int)v));
    }
    refresh_current();
}

static void draw(void)          // everything of the current view, now (start-up and after bulk changes)
{
    if (!have_ui) {
        return;
    }
    uint32_t t0 = core_cycles();
    show_view(cur_view());
    uint32_t t1 = core_cycles();
    if (!g_mt.t_frame) {
        mt_mark("first frame: widgets updated");
    }
    trx_lv_handler();
    if (!g_mt.t_frame) {
        mt_mark("first frame: LVGL handler done");
    }
    printf("[host] view %d: update %u ms, handler %u ms\n", (int)nv.view, (unsigned)(t1 - t0), (unsigned)(core_cycles() - t1));
}

static void buffer_report(void)
{
    lv_display_t *d = lv_display_get_default();
    if (!d || !d->buf_1) {
        printf("[host] no display\n");
        return;
    }
    const uint16_t *b[2] = { (const uint16_t *)d->buf_1->data, d->buf_2 ? (const uint16_t *)d->buf_2->data : 0 };
    for (int i = 0; i < 2; i++) {
        if (b[i]) {
            printf("[host] buffer %d at %p: bg %04x header %04x nav %04x line %04x red-test %04x\n", i + 1, (const void *)b[i],
                   b[i][300 * 960 + 500], b[i][20 * 960 + 300], b[i][200 * 960 + 20], b[i][(TOP_H - 1) * 960 + 300], b[i][5 * 960 + 5]);
        }
    }
    if (b[0] && b[1]) {
        uint32_t diff = 0;
        for (uint32_t k = 0; k < 960u * 540u; k++) {
            diff += b[0][k] != b[1][k];
        }
        printf("[host] buffers differ in %u of %u pixels; LVGL draws next into buffer %d\n", (unsigned)diff, 960 * 540,
               lv_display_get_buf_active(d) == d->buf_1 ? 1 : 2);
    }
}

static void buffers_mode(bool single)
{
    lv_display_t *d = lv_display_get_default();
    if (!d || !d->buf_1) {
        return;
    }
    static lv_draw_buf_t *b1, *b2;
    if (!b1) {
        b1 = d->buf_1;
        b2 = d->buf_2;
    }
    uint32_t sz = 960u * 540u * 2u;
    // T146: with the saved picture up (in buffer 1), LVGL's one buffer is the second; its first flush flips to it
    lv_display_set_buffers(d, (single && snap_shown) ? b2->data : b1->data, single ? NULL : b2->data, sz, LV_DISPLAY_RENDER_MODE_DIRECT);
    printf("[host] LVGL renders into %s\n", single ? "ONE buffer (shown directly)" : "two buffers (flipped)");
}


// ---- starting a core ------------------------------------------------------------------------------------------------------
// The start panel is resident (built in build_start, hidden) on LVGL's top layer: a solid 600x270 panel, no full-screen
// dimmer (a blend reads the destination: ~100 ms for a full screen). Progress only changes labels and the bar, rendered at
// most every UPDATE_MS. Everything runs in this one task, so LVGL is never entered while a staging loop holds the CPU.
#define UPDATE_MS 100
static const char *step_name[STEPS] = { "Check the bundle (SHA-256, contract)", "Cut off the old core",
                                        "Core through the ICAP", "Hand over", "Jump; the application starts its core" };
// T115: the steps of a full-firmware start have their own words; the numbers are the same
static const char *step_name_full[STEPS] = { "Check the firmware image, stage it", "Bitstream into the flash drive",
                                             "Check the flash file (one run, equal)", "Hand over", "Reload the FPGA; the firmware starts" };
static bool st_full;
static int st_step, st_pct;
static lv_obj_t *cf_root, *cf_title, *cf_text, *cf_hint;     // T115: the confirm panel of a full firmware
// T111: the pages of the format (warning and name, last chance, progress, result): one resident panel, red while it asks, on the top layer
#define FM_LINES 8
// fm_root is the scrim: the whole content area in the page's solid background (the view behind and its note do not show, D11); fm_box is the page,
// sized to its lines and centred in it. A page change is one TrxPage on fm_root. The page's keys are the footer's text while it is up (fm_draw).
static lv_obj_t *fm_root, *fm_box, *fm_title, *fm_l[FM_LINES], *fm_pctl, *fm_inbox, *fm_in, *fm_msg, *fm_bar, *fm_key;
static char fm_foot_saved[200];       // the footer the page covers (its key line takes the place: one place for keys)
#define FM_W (COMPACT ? SW - NAV_W - 20 : 740)          // the page is as wide as the content column less 10 a side on the compact canvas
#define FM_P (COMPACT ? 24 : 28)                          // the pitch of its lines
static lv_obj_t *st_root, *st_mark[STEPS], *st_name[STEPS], *st_pctl[STEPS], *st_err, *st_title, *st_bar;
static uint32_t st_last_draw;
static int st_shown_step = -1, st_shown_pct = -2;

static void build_start(void)
{
    int w = 600;
    BSTEP {
        st_root = box(trx_overlay(), (SW - 600) / 2, 130 + (SH - 540) / 2, 600, 270, C_PANEL, 1);
        trx_page_hide(st_root);
        st_title = txt(st_root, 20, 14, "", F_TITLE, C_INK, w - 40);
    }
    for (int i = 0; i < STEPS; i++) {
        BSTEP {
            int yy = 62 + i * 26;
            st_mark[i] = txt(st_root, 20, yy, ".", F_TEXT, C_MUTED);
            st_name[i] = txt(st_root, 46, yy, step_name[i], F_TEXT, C_MUTED, w - 150);
            st_pctl[i] = txt(st_root, w - 80, yy, "", F_TEXT, C_MUTED);
        }
    }
    BSTEP {
        st_bar = trx_pbar(st_root, 20, 206, w - 40, 6, C_PANEL2, C_HOST);
        st_err = txt(st_root, 20, 226, "", F_SMALL, C_BAD, w - 40);
    }
    BSTEP {
        cf_root = box(trx_overlay(), (SW - 600) / 2, 150 + (SH - 540) / 2, 600, 200, C_PANEL, 1, C_HOST);
        trx_page_hide(cf_root);
        cf_title = txt(cf_root, 20, 14, "", F_TITLE, C_INK, w - 40);
        cf_text = txt(cf_root, 20, 60, "", F_TEXT, C_INK, w - 40);
        cf_hint = txt(cf_root, 20, 160, "RETURN starts    any other key cancels", F_SMALL, C_MUTED, w - 40);
    }
    BSTEP {
        int qw = 560 < SW - 40 ? 560 : SW - 40;
        pq_root = box(trx_overlay(), (SW - qw) / 2, 150, qw, 58 + 4 * PQ_P + 14, C_PANEL, 3, C_HOST);
        trx_page_hide(pq_root);
        pq_title = fit1(txt(pq_root, 20, 12, "", F_TITLE, C_INK, qw - 40), qw - 40);
        pq_l0 = fit1(txt(pq_root, 20, 58, "", F_TEXT, C_INK, qw - 40), qw - 40);
        pq_l1 = fit1(txt(pq_root, 20, 58 + PQ_P, "", F_TEXT, C_MUTED, qw - 40), qw - 40);
        pq_key = fit1(txt(pq_root, 20, 58 + 3 * PQ_P, "", F_TEXT, C_HOST, qw - 40), qw - 40);
    }
    int fw = FM_W;
    BSTEP {
        fm_root = box(trx_overlay(), NAV_W, TOP_H, SW - NAV_W, CH, C_BG, 0, 0);
        trx_page_hide(fm_root);
        fm_box = box(fm_root, (SW - NAV_W - fw) / 2, 20, fw, COMPACT ? CH - 20 : 440, C_PANEL, 3, C_BAD);
        fm_title = fit1(txt(fm_box, 20, 12, "", F_TITLE, C_INK, fw - 40), fw - 40);
    }
    for (int i = 0; i < FM_LINES; i++) {
        BSTEP {
            fm_l[i] = fit1(txt(fm_box, 20, 58 + i * FM_P, "", F_TEXT, C_INK, fw - 40), fw - 40);
        }
    }
    BSTEP {
        fm_pctl = txt(fm_box, fw - 140, 58, "", F_TEXT, C_INK, 120);
        lv_obj_set_style_text_align(fm_pctl, LV_TEXT_ALIGN_RIGHT, 0);
        fm_inbox = box(fm_box, 20, 236, fw - 40, 34, C_BG, 1, C_LINE);
        fm_in = fit1(txt(fm_inbox, 10, 6, "", F_TEXT, C_INK, fw - 60), fw - 60);
        fm_msg = fit1(txt(fm_box, 20, 282, "", F_TEXT, C_WARN, fw - 40), fw - 40);
        fm_bar = trx_pbar(fm_box, 20, 330, fw - 40, 10, C_PANEL2, C_HOST);
        fm_key = fit1(txt(fm_box, 20, 380, "", F_TEXT, C_HOST, fw - 40), fw - 40);   // T152 D10: the page's keys, one blank line under what the page says
    }
}

static void areas_forget(void)
{
    memset(vwin, 0, sizeof(vwin));
    rn_root = wl_root = oc_root = we_root = st_root = cf_root = pq_root = fm_root = 0;
    b_left = b_right = c_pat = 0;
    memset(b_mark, 0, sizeof(b_mark));
    memset(b_hl, 0, sizeof(b_hl));
    memset(s_box, 0, sizeof(s_box));
    memset(s_mk, 0, sizeof(s_mk));
    cm_root = wm_root = pm_root = 0;
    memset(pm_box, 0, sizeof(pm_box));
    memset(n_smk, 0, sizeof(n_smk));
}

// One step of an area; true when the area is complete.
static bool area_step(int a)
{
    bs.target = b_stage[a];
    bs.idx = 0;
    switch (a) {
    case A_BROWSE: build_browse(); break;
    case A_EDIT: build_edit(); break;
    case A_NET: build_net(); break;
    case A_SET: build_settings(); break;
    case A_COLORS: build_colors(); break;
    case A_RENAME: build_rename(); break;
    case A_WIFI: build_wifi_panels(); break;
    case A_START: build_start(); break;
    }
    b_stage[a]++;
    if (b_stage[a] < bs.idx) {
        return false;
    }
    b_done[a] = true;
    // the panels sit in one container, in the order the eager build made them: rename, wifi list, config overlay, entry, start, confirm, picture, format
    lv_obj_t *const ord[] = { rn_root, wl_root, oc_root, we_root, st_root, cf_root, pq_root, fm_root };
    if (a >= A_RENAME) {
        for (lv_obj_t *o : ord) {
            if (o) {
                lv_obj_move_foreground(o);
            }
        }
    }
    trx_overlay();                                   // the container is the last child of the screen again
    static const char *const names[A_N] = { "browser", "editor", "network", "settings", "colours", "rename", "wifi panels", "start panels" };
    if (g_mt.t_frame) {
        char *nm = g_mt.area_name[a];
        snprintf(nm, sizeof(g_mt.area_name[a]), "area built: %s", names[a]);
        mt_mark(nm);
    }
    return true;
}

static void ensure_area(int a)
{
    while (!b_done[a]) {
        area_step(a);
    }
}

static void build_rest_all(void)
{
    for (int a = 0; a < A_N; a++) {
        EN(a);
    }
}

// The main loop's share: steps of the areas still to build, until 40 ms are used (a step is a few objects, tens of ms at most). True while something is left.
static bool build_pending(void)
{
    for (int a = 0; a < A_N; a++) {
        if (!b_done[a]) {
            return true;
        }
    }
    return false;
}
static void build_pass(uint32_t budget_ms = 40)
{
    uint32_t t0 = now_ms();
    for (int a = 0; a < A_N; a++) {
        while (!b_done[a]) {
            area_step(a);
            if (now_ms() - t0 >= budget_ms) {
                return;
            }
        }
    }
}

static void start_screen_open(const char *title, bool full = false)
{
    st_full = full;
    if (!have_ui) {
        return;
    }
    EN(A_START);
    {
        TrxPage page(st_root);
        set_txt(st_title, title);
        set_txt(st_err, "");
        for (int i = 0; i < STEPS; i++) {
            set_txt(st_name[i], full ? step_name_full[i] : step_name[i]);
            set_txt(st_mark[i], ".");
            set_col(st_mark[i], C_MUTED);
            set_col(st_name[i], C_MUTED);
            set_txt(st_pctl[i], "");
        }
        trx_pbar_set(st_bar, 0, 100);
    }
    st_shown_step = -1;
    st_shown_pct = -2;
    trx_lv_handler();
    st_last_draw = now_ms();
}

// T164 (board 2026-11-01): the bar is the whole start, STEPS equal shares; the number at the step's row is that step's own percent. A bar of the
// current step alone fell back to 0 at "Hand over" and "Jump" (their percent is 0) and stood there for the whole reload of a large core.
static int start_bar_pct(int step, int pct)
{
    int s = step < 0 ? 0 : (step >= STEPS ? STEPS - 1 : step);
    int p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    return (s * 100 + p) / STEPS;
}

static void start_screen_update(bool force)
{
    if (have_ui) {
        EN(A_START);
    }
    if (!have_ui || !st_root) {
        return;
    }
    uint32_t t = now_ms();
    if (!force && t - st_last_draw < UPDATE_MS) {
        return;
    }
    st_last_draw = t;
    if (st_shown_step != st_step) {
        TrxPage page(st_root, st_bar);       // a new step recolours every row: one redraw of the panel (its bar row stays as it is), not row by row
        for (int i = 0; i < STEPS; i++) {
            set_txt(st_mark[i], i < st_step ? "+" : (i == st_step ? ">" : "."));
            set_col(st_mark[i], i < st_step ? C_OK : (i == st_step ? C_HOST : C_MUTED));
            set_col(st_name[i], i <= st_step ? C_INK : C_MUTED);
            set_txt(st_pctl[i], "");
        }
        st_shown_step = st_step;
        st_shown_pct = -2;
    }
    if (st_shown_pct != st_pct && st_step < STEPS) {
        char s[8];
        snprintf(s, sizeof(s), "%d", st_pct < 0 ? 0 : st_pct);
        strcat(s, "%");              // not through the format: the firmware's printf has no %%
        set_txt(st_pctl[st_step], s);
        trx_pbar_set(st_bar, start_bar_pct(st_step, st_pct), 100);
        st_shown_pct = st_pct;
    }
    trx_lv_handler();
}

// The bench: the start bar's fill in pixels (0: hidden) and the track's width.
int hostui_test_start_bar(int *track_w)
{
    lv_obj_t *f = st_bar ? lv_obj_get_child(st_bar, 0) : NULL;
    if (track_w) {
        *track_w = st_bar ? lv_obj_get_width(st_bar) : 0;
    }
    return f && !lv_obj_has_flag(f, LV_OBJ_FLAG_HIDDEN) ? lv_obj_get_width(f) : 0;
}

static void start_screen_close(void)
{
    if (st_root) {
        trx_page_hide(st_root);
        trx_lv_handler();
    }
}

static bool abort_key(void)
{
    int k;
    while ((k = host_getch_nowait()) >= 0) {
        if (k == KEY_BREAK || k == KEY_ESCAPE) {
            return true;
        }
    }
    return false;
}

static int progress(int step, int pct, const char *msg, void *)
{
    bool newstep = step != st_step;
    if (newstep) {
        printf("[host] step %d: %s\n", step, st_full ? step_name_full[step] : step_name[step]);
    }
    st_step = step;
    st_pct = pct;
    hostapi_flow_progress(pct, 100, st_full ? step_name_full[step] : step_name[step]);
    start_screen_update(newstep);
    static uint32_t last_poll;
    uint32_t t = now_ms();
    if (!newstep && t - last_poll < 50) {
        return 0;
    }
    last_poll = t;
    if (abort_key()) {
        printf("[host] RUN/STOP: abort requested at step %d (%d percent)\n", step, pct);
        return 1;
    }
    return 0;
}

// hint_r: the start came from a menu entry and was refused while it read the file (missing, changed, damaged): the person is told
// what to do about the entry. The check at the start of the host is gone (T101 S10); this is where a bad file is found.
static char st_err_last[240];
static void start_fail(const char *why, bool hint_r = false)
{
    printf("[host] start failed: %s\n", why ? why : "failed");
    hint_r = hint_r && why && strcmp(why, CORE_ABORTED) != 0;
    char em[240];
    snprintf(em, sizeof(em), "%s. %sAny key returns to the menu.", why ? why : "failed",
             hint_r ? "Press R on the entry in the menu to check it again. " : "");
    snprintf(st_err_last, sizeof(st_err_last), "%s", em);
    if (st_err) {
        set_txt(st_err, em);
        start_screen_update(true);
    }
    hostapi_ask(em, KEY_RETURN, KEY_RETURN);                   // any key leaves: the service answers either way with RETURN
    while (host_getch() < 0) {
        vTaskDelay(20);
    }
    hostapi_ask("", 0, 0);
    start_screen_close();
}

// The menu's way of starting (hostui.cc paints, host_core.cc decides the order: hc_start_sequence). The same sequence serves the direct boot.
static bool st_abort_key(void *) { return abort_key(); }
static void st_pause(void *, int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void st_settle(void *) { start_screen_update(true); }
static uint32_t lap_t0;
static void st_lap(void *, const char *what)
{
    if (!what) {
        lap_t0 = core_cycles();
        return;
    }
    printf("[host] %s in %u ms\n", what, (unsigned)((core_cycles() - lap_t0) / CORE_CYC_PER_MS));
}
static void st_before_jump(void *, bool) { }
static const HcStartHooks menu_start_hooks = { progress, st_abort_key, st_pause, st_settle, st_lap, st_before_jump, NULL };

static void start_core(const char *path, const char *title, const BootEntry *trusted = 0)
{
    FlowScope flow_("start", title, true);
    hc_ui_changed();
    char tt[80];
    snprintf(tt, sizeof(tt), "Starting %s", title);
    st_step = STEP_VERIFY;
    st_pct = 0;
    printf("[host] %s from %s\n", tt, path);
    start_screen_open(tt);
    start_screen_update(true);
    const HcStartEntry entry = { path, false, trusted, false };
    HcStartOutcome oc;
    const char *why = hc_start_sequence(entry, HC_START_MENU, menu_start_hooks, &oc);
    start_fail(why, trusted && !oc.staged);
}

// T115: a full firmware (.cfw). Same screen, its own steps; nothing is handed over unless every check passed.
static void start_full(const char *path, const char *title, bool verify)
{
    FlowScope flow_("start", title, true);
    hc_ui_changed();
    char tt[96];
    snprintf(tt, sizeof(tt), "Starting %s", title);
    st_step = STEP_VERIFY;
    st_pct = 0;
    printf("[host] %s from %s (full firmware)\n", tt, path);
    start_screen_open(tt, true);
    start_screen_update(true);
    const HcStartEntry entry = { path, true, 0, verify };
    HcStartOutcome oc;
    const char *why = hc_start_sequence(entry, HC_START_MENU, menu_start_hooks, &oc);
    start_fail(why, !verify && !oc.staged);
}

// T115 D5 (Q2): the way back is a power cycle, and nothing else is said. RETURN starts, any other key leaves it.
static char last_confirm[200];
static int n_confirms;
static bool confirm_full(const char *title)
{
    snprintf(last_confirm, sizeof(last_confirm), "%s replaces the host. The way back is to switch the power off and on.", title);
    n_confirms++;
    EN(A_START);
    if (cf_root) {
        {
            TrxPage page(cf_root);
            set_txt(cf_title, "Start full firmware?");
            set_txt(cf_text, last_confirm);
        }
        trx_lv_handler();
    }
    int k;
    hostapi_ask(last_confirm, KEY_RETURN, KEY_ESCAPE, hc_ui_remote_ask());      // a remote start: no after 60 s; RETURN at the board: no time-out
    while ((k = host_getch()) < 0) {
        vTaskDelay(20);
    }
    hostapi_ask("", 0, 0);
    if (cf_root) {
        trx_page_hide(cf_root);
        trx_lv_handler();
    }
    hc_ui_changed();
    return k == KEY_RETURN;
}

// ---- T203: the start log (startfile.h) ---------------------------------------------------------------------------------------
// The record is kept in RAM during the start and written once, when the decision is made; a later event (a countdown, a key or the network
// starting a core) adds its line and writes the block again. No SD card or a read-only one: nothing is written, the next call tries again.
static void slog_flush(void)
{
    if (startrec_decided()) {
        hostreport_startlog_store(APP_VERSION_STRING, APP_BUILD_DATE);
    }
}
static const char *start_origin = "menu (key or network request)";
static void slog_started(void)
{
    startrec_event("started by: %s", start_origin);
    slog_flush();
}

// A start from the menu: a .cfw goes the full-firmware way, a .cor the core way. ask: the person pressed RETURN or S
// (a countdown has no one to ask; a network request asks the same question, which is a service question).
static void start_any(const char *path, const char *title, const BootEntry *trusted, bool ask)
{
    slog_started();
    if (is_full(path)) {
        // Alex 2026-10-28: "We don't need this requester before the start of a large core." Only foreign firmware (.cfw / .ue2) asks.
        if (!ask || !cfw_is_name(path) || confirm_full(title)) {
            start_full(path, title, !trusted);          // a menu entry was checked when it was added; a file started once is checked now
        }
        return;
    }
    start_core(path, title, trusted);
}


// ---- the format (T111) ------------------------------------------------------------------------------------------------------------------
// Reached from the media level of the File Browser with F. One modal loop in the UI task (like a core start): while it runs nothing else of
// the UI runs, so no countdown ends and no key starts a core; a command from the network is answered "busy" and dropped, never kept for later.
// There is no route for it in webui.cc and no state of it in the snapshot (D9, S3).
struct FmPage {
    char title[72];
    uint32_t border;
    char line[FM_LINES][124];
    uint32_t lcol[FM_LINES];
    bool input;
    char typed[FMTC_NAME_MAX + 4];
    char msg[124];
    uint32_t mcol;
    int bar;                         // -1 = hidden
    char hint[124];                  // the page's keys: the footer says them while the page is up
    char pct[16];                    // "37 %", right-aligned on the last line (the one progress number, beside what is being done)
};

static FmPage fm_last;               // what the panel shows now (the bench reads it)
static int fm_pages;                 // pages drawn, for the bench
static uint32_t fm_last_draw;
static uint32_t fm_border_shown;

// A new page (T167 5.9) and a tick of the page that is up are told apart by what is drawn: the title, the frame colour, the input field, the bar
// being there and the colour of any line are the page; the text of a line, the message, the hint and the bar's value are what a tick changes.
// A page is drawn through TrxPage (one redraw of the panel); a tick changes its labels in place, so a number does not redraw 760x440 pixels.
static bool fm_up;                   // the panel shows a page (fm_shown): false after fm_close()
static void overlay_state_forget(void)
{
    st_shown_step = -1;
    st_shown_pct = -2;
    fm_up = false;
    fm_border_shown = 0xFFFFFFFFu;
}
static FmPage fm_shown;
static bool fm_is_new_page(const FmPage &pg)
{
    if (!fm_up || strcmp(fm_shown.title, pg.title) || fm_shown.border != pg.border || fm_shown.input != pg.input || (fm_shown.bar < 0) != (pg.bar < 0) ||
        fm_shown.mcol != pg.mcol) {
        return true;
    }
    for (int i = 0; i < FM_LINES; i++) {
        if (fm_shown.lcol[i] != pg.lcol[i]) {
            return true;
        }
    }
    return false;
}

// The page is as high as what it shows: title, the lines up to the last one, the input box, the bar, the message under it. A page with a bar keeps the
// message row (it appears and goes within one page), so a tick never changes the height. The lines have their own rows; only the position of what comes
// after them depends on how many there are.
static void fm_place(lv_obj_t *o, int x, int y)
{
    if (lv_obj_get_x(o) != x || lv_obj_get_y(o) != y) {
        lv_obj_set_pos(o, x, y);
    }
}

static void fm_apply(const FmPage &pg)
{
    if (fm_border_shown != pg.border) {
        lv_obj_set_style_border_color(fm_box, lv_color_hex(pg.border), 0);
        fm_border_shown = pg.border;
    }
    set_txt(fm_title, pg.title);
    int n = 0;
    for (int i = 0; i < FM_LINES; i++) {
        set_txt(fm_l[i], pg.line[i]);
        set_col(fm_l[i], pg.lcol[i]);
        if (pg.line[i][0]) {
            n = i + 1;
        }
    }
    int y = 58 + n * FM_P + 6;
    if (pg.input) {
        fm_place(fm_inbox, 20, y);
        y += 44;
    }
    set_hidden(fm_inbox, !pg.input);
    set_txt(fm_in, pg.typed);
    set_txt(fm_pctl, pg.pct);
    fm_place(fm_pctl, FM_W - 140, 58 + (n ? n - 1 : 0) * FM_P);
    set_hidden(fm_pctl, !pg.pct[0]);
    set_hidden(fm_bar, pg.bar < 0);
    if (pg.bar >= 0) {
        fm_place(fm_bar, 20, y + 4);
        trx_pbar_set(fm_bar, pg.bar, 100);
        y += 26;
    }
    fm_place(fm_msg, 20, y);
    set_txt(fm_msg, pg.msg);
    set_col(fm_msg, pg.mcol);
    if (pg.msg[0] || pg.bar >= 0) {
        y += 34;
    }
    set_hidden(fm_key, !pg.hint[0]);                    // T152 D10: a page's keys end the box, one blank line apart (the footer says them too)
    if (pg.hint[0]) {
        y += FM_P;
        fm_place(fm_key, 20, y);
        set_txt(fm_key, pg.hint);
        y += FM_P;
    }
    int h = y + 14 < 140 ? 140 : y + 14;
    if (lv_obj_get_height(fm_box) != h) {
        lv_obj_set_height(fm_box, h);
    }
    fm_place(fm_box, lv_obj_get_x(fm_box), (CH - h) / 2);
}

// The key a page takes as its "yes" (T192: the service answers a page's question with it); RETURN unless the page's flow sets another before it draws.
static int fm_yes_key = KEY_RETURN;

// The page as the service sees it (hostdomain.h): the same lines the panel shows, without colours.
static void fm_tell(const FmPage &pg)
{
    HostPageInfo hp;
    memset(&hp, 0, sizeof(hp));
    snprintf(hp.title, sizeof(hp.title), "%s", pg.title);
    for (int i = 0; i < FM_LINES && i < 8; i++) {
        if (pg.line[i][0]) {
            snprintf(hp.line[hp.nlines++], sizeof(hp.line[0]), "%s", pg.line[i]);
        }
    }
    snprintf(hp.hint, sizeof(hp.hint), "%s", pg.hint);
    snprintf(hp.msg, sizeof(hp.msg), "%s", pg.msg);
    hp.input = pg.input;
    snprintf(hp.typed, sizeof(hp.typed), "%s", pg.typed);
    hp.bar = pg.bar;
    snprintf(hp.pct, sizeof(hp.pct), "%s", pg.pct);
    // A page that only says what happened ("any key returns ...") asks nothing destructive; every other page that waits is a question whose yes writes or replaces.
    bool destructive = pg.bar < 0 && strncmp(pg.hint, "any key", 7) != 0;
    hostapi_page(hp, fm_yes_key, KEY_ESCAPE, destructive);
    fm_yes_key = KEY_RETURN;                                  // one draw only
    hostapi_pump();                                           // a progress page is drawn between steps: the service runs, a remote cancel arrives
}

static void fm_draw(const FmPage &pg, bool force = true)
{
    fm_last = pg;
    fm_pages++;
    fm_tell(pg);
    if (have_ui) {
        EN(A_START);
    }
    if (!have_ui || !fm_root) {
        return;
    }
    uint32_t t = now_ms();
    if (!force && t - fm_last_draw < UPDATE_MS) {
        return;
    }
    fm_last_draw = t;
    bool newpage = fm_is_new_page(pg);
    if (!fm_up) {                                       // the page comes up: its keys are the footer from now on
        snprintf(fm_foot_saved, sizeof(fm_foot_saved), "%s", lv_label_get_text(foot_label));
    }
    if (strcmp(lv_label_get_text(foot_label), pg.hint)) {
        set_foot(foot_label, pg.hint);
        if (newpage) {
            trx_lv_handler();                           // the footer is its own small refresh, so that the page change below stays one area
        }
    }
    if (newpage) {
        TrxPage page(fm_root);
        fm_apply(pg);
    } else {
        fm_apply(pg);
    }
    fm_shown = pg;
    fm_up = true;
    trx_lv_handler();
}

#ifdef HOSTUI_RENDER_TEST
static bool fm_keep;                 // the bench keeps the page up to take its picture
#endif
static void fm_close(void)
{
    hostapi_page_end();
    fm_yes_key = KEY_RETURN;
#ifdef HOSTUI_RENDER_TEST
    if (fm_keep) {
        return;
    }
#endif
    if (fm_root) {
        trx_page_hide(fm_root);
        if (fm_up) {
            set_foot(foot_label, fm_foot_saved);
        }
        fm_up = false;
        trx_lv_handler();
    }
}

// A command from the network meanwhile: answered, not kept (the mailbox holds one; left in it, it would start a core the moment the format ends).
static void fm_service(void)
{
    hostapi_waiting();                                  // every page that waits for a key calls this: the service runs from here
    WebCmd c;
    if (web_take(&c)) {
        web_result("busy: a format is open on the device");
        printf("[host] network command %d dropped: a format is open\n", (int)c.kind);
    }
}

// ---- T139 D1/D2: delete, copy and move in the File Browser (the rules are fileops.cc's, the operations are host_core_flow_fileops.cc's; the glue is below flow_env) ------
static uint8_t fo_buf[32768] __attribute__((aligned(64)));

static const char *base_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// ---- the shared page helpers of the modal flows (restore, below) and the bench's hook ----------------------------------------------------------
// The conversion (T137 D9) and the ESP32 flows (T152 D8/S8/S9) are the core's: host_core_flow_convert.cc, host_core_flow_esp.cc.
#ifdef HOSTUI_RENDER_TEST
void (*hostui_cv_hook)(int phase, uint64_t done, uint64_t total);       // the bench presses a key in the middle of the write
#define CV_HOOK(p, d, t) do { if (hostui_cv_hook) hostui_cv_hook(p, d, t); } while (0)
#else
#define CV_HOOK(p, d, t) do { } while (0)
#endif

// ---- the flash and the staging areas the firmware flows work on (the flows themselves are the core's: host_core_flow_*.cc) ------------------------------------------
#ifdef HOSTUI_RENDER_TEST
Flash *hostui_test_flash;                                     // the bench's flash: a model with erase semantics
static uint8_t fw_bit_area[FW_BIT_SLOT + 8192], fw_app_area[FW_APP_SLOT + 8192];
#define FW_FLASH()   (hostui_test_flash)
#define FW_BIT_BUF   (fw_bit_area)
#define FW_APP_BUF   (fw_app_area)
#else
#define FW_FLASH()   (get_flash())
#define FW_BIT_BUF   ((uint8_t *)FW_STAGE_BIT)
#define FW_APP_BUF   ((uint8_t *)FW_STAGE_APP)
#endif

static bool fw_written;                                       // a restore was written in this session: the running firmware is not what is in the flash
static char last_fw_why[160];

// ---- T152 D3/D5: the MultiCore update from a .txu (U) -------------------------------------------------------------------------------------
// Our own host and/or host app, separate from restoring the vendors' firmware (D2): its own module (txu.cc), its own pages. Shares only the lowest layer with the
// restore: the flash chip check, the check before the first erase, the write and its read-back (flashfw.cc), the two questions (fwconfirm.cc).
//   check page     the file is read into DDR2 and every part checked against its SHA-256; ESC cancels, nothing is written
//   question 1/2   what is replaced, what it does to the cores (D1), what is skipped; "Y"; "RETURN within 10 s"; every other key is No
//   progress       only the parts that are written; from the first erase on every key is read and dropped
//   result         written and read back, or the page that says the flash may not boot and offers the write again from memory
// The flows' pages and their order are host_core_flow.cc's (hc_update_run, ...): they come as data (HcPage), this file draws them and reads the keys.
static char last_up_why[160];                                 // the reason of the last failed write of the update or the ESP32 (the bench reads it)
static uint32_t tone_col(int t)
{
    switch (t) {
    case HCT_INK: return C_INK;
    case HCT_MUTED: return C_MUTED;
    case HCT_OK: return C_OK;
    case HCT_WARN: return C_WARN;
    case HCT_BAD: return C_BAD;
    case HCT_HOST: return C_HOST;
    }
    return 0;
}
static void page_to_fm(const HcPage &p, FmPage *pg)
{
    memset(pg, 0, sizeof(*pg));
    snprintf(pg->title, sizeof(pg->title), "%s", p.title);
    pg->border = tone_col(p.border);
    for (int i = 0; i < FM_LINES && i < HC_PAGE_LINES; i++) {
        snprintf(pg->line[i], sizeof(pg->line[i]), "%s", p.line[i]);
        pg->lcol[i] = tone_col(p.lcol[i]);
    }
    pg->input = p.input;
    snprintf(pg->typed, sizeof(pg->typed), "%s", p.typed);
    snprintf(pg->msg, sizeof(pg->msg), "%s", p.msg);
    pg->mcol = tone_col(p.mcol);
    pg->bar = p.bar;
    snprintf(pg->hint, sizeof(pg->hint), "%s", p.hint);
    snprintf(pg->pct, sizeof(pg->pct), "%s", p.pct);
}
static void fe_draw(void *, const HcPage &p, bool force)
{
    FmPage pg;
    page_to_fm(p, &pg);
    fm_draw(pg, force);
}
static int fe_getch_nowait(void *) { return host_getch_nowait(); }
static int fe_getch(void *) { return host_getch(); }
static void fe_idle(void *) { fm_service(); }
static void fe_sleep(void *, int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void fe_hook(void *, int phase, uint64_t a, uint64_t b) { CV_HOOK(phase, a, b); }
static void fe_note(void *, const char *text, int tone) { set_note(text, tone); }
static void fe_refused(void *, const char *l0) { snprintf(last_fw_why, sizeof(last_fw_why), "%s", l0); }
static void fe_write_failed(void *, const char *why) { snprintf(last_up_why, sizeof(last_up_why), "%s", why); }
static void fe_log(void *, const char *line) { printf("%s\n", line); }
static uint32_t fe_now(void *) { return now_ms(); }
static Flash *fe_flash(void *) { return FW_FLASH(); }
static uint8_t *fe_bit_buf(void *) { return FW_BIT_BUF; }
static uint8_t *fe_app_buf(void *) { return FW_APP_BUF; }
static CorSource *fe_open(void *, const char *path) { return board_fs.open(path); }
static int fe_list(void *, const char *dir, HostDirEntry *out, int max, bool only_bundles) { return hostfs_list(dir, out, max, only_bundles); }
static bool fe_power_cycle(void *, const char **why) { return esp_power_cycle(why); }
static const HcFlowEnv flow_env = { fe_draw, fe_getch_nowait, fe_getch, fe_idle, fe_sleep,
#ifdef HOSTUI_RENDER_TEST
                                    fe_hook,
#else
                                    0,
#endif
                                    fe_note, fe_refused, fe_write_failed, fe_log, fe_now, fe_flash, fe_bit_buf, fe_app_buf, fe_open, fe_list, fe_power_cycle, 0 };
static const HcFlowEnv &flow_env_ref(void) { return flow_env; }

// ---- the WiFi page's actions and modals: the screen's side (host_core_flow_wifi.cc runs them) ----
static void ww_note(void *, const char *text, int tone) { net_note(text, tone); }
static const char *ww_set_name(void *, int row) { return hc_wifi_net_set_name(row); }
static bool ww_panels(void *)
{
    EN(A_WIFI);
    return wl_root && we_root && oc_root;
}
static int ww_pick_rows(void *)
{
    EN(A_WIFI);
    return wl_rows;
}
static int ww_entry_vis(void *) { return wifi_entry_vis(); }
static bool ww_list_up(void *) { return wl_root && !lv_obj_has_flag(wl_root, LV_OBJ_FLAG_HIDDEN); }
static void ww_draw_pick(void *, const HcWifiPick &p, bool fresh)
{
    wifi_pick_show(p, fresh);
    trx_lv_handler();
}
static void ww_pick_end(void *)
{
    hostapi_pick_end();
    trx_page_hide(wl_root);
    trx_page_hide(oc_root);
    trx_lv_handler();
    hc_ui_changed();
}
static bool ww_entry_open(void *)
{
    bool was_up = ww_list_up(0);                          // the entry stands alone, the list comes back after it
    if (was_up) {
        trx_page_hide(wl_root);
    }
    return was_up;
}
static void ww_draw_entry(void *, const HcWifiEntry &e, bool first)
{
    if (first) {
        TrxPage page(we_root);
        set_txt(we_title, e.title);
        set_txt(we_sub, e.sub);
        set_txt(we_keys, e.keys);
        we_draw(e);
    } else {
        we_draw(e);
    }
    trx_lv_handler();
}
static void ww_entry_end(void *, bool list_was_up)
{
    trx_page_hide(we_root);
    if (list_was_up) {
        TrxPage page(wl_root);
    }
    trx_lv_handler();
    hostapi_entry_end();
    hc_ui_changed();
}
static void ww_history(void *, const char *text) { hostapi_history(text); }
static bool ww_arm_yes(void *) { return hc_arm_yes(); }
static const HcWifiEnv wifi_env = { &flow_env, &c_wifi, &c_wcfg, &c_wcfg_ok, ww_set_name, ww_note, ww_panels, ww_pick_rows, ww_entry_vis, ww_list_up, ww_draw_pick,
                                    ww_pick_end, ww_entry_open, ww_draw_entry, ww_entry_end, ww_history, ww_arm_yes, 0 };
static const HcWifiEnv &wifi_env_ref(void) { return wifi_env; }

// ---- the File Browser's file operations: the screen's side (host_core_flow_fileops.cc runs them) ----------------------------------------------------------------------
static bool fo_scope_begin(void *, const char *kind, const char *title, bool cancelable) { return hostapi_flow_begin(kind, title, cancelable); }
static void fo_scope_end(void *, bool own)
{
    if (own) {
        hostapi_flow_end(true, "");
    }
}
static void fo_progress_ui(void *, int pct, const char *label, const char *text)
{
    hostapi_flow_progress(pct, 100, label);
    if (text) {
        hc_note_set(text, HCT_MUTED);
        set_txt(b_note, nv.note);
        set_col(b_note, note_color());
        trx_lv_handler();
    }
}
static void fo_busy(void *, const char *text)
{
    set_note(text, HCT_MUTED);
    refresh_current();
    trx_lv_handler();
}
static void fo_pick_end(void *) { hc_ui_pick_end(); }
static void fo_mark(void *, int entry) { lazy_mark(entry); }
static void fo_after_add(void *, int added, const char *path)
{
    cfg_after(added);
    int at = in_menu(path);
    if (at >= 0) {
        probe_entry(at);
    }
}
static void fo_reload(void *) { browse_load(); }
static void fo_ask(void *, const char *text) { hostapi_ask(text, KEY_RETURN, KEY_ESCAPE); }
static const HcFoEnv fo_env = { &flow_env, fo_buf, sizeof(fo_buf), fo_scope_begin, fo_scope_end, fo_progress_ui, fo_busy, fo_pick_end, fo_mark, fo_after_add, fo_reload, fo_ask, 0 };

static bool re_file_stat(void *, const char *path, uint32_t *size, uint32_t *mtime) { return hostfs_stat(path, size, mtime); }
static void re_write_failed(void *, const char *why) { snprintf(last_fw_why, sizeof(last_fw_why), "%s", why); }
static const HcRestoreEnv restore_env = { &flow_env, re_file_stat, fe_refused, re_write_failed, 0 };

// ---- the format (T111): the pages and the order are host_core_flow_format.cc's; this is the screen's side ---------------------------------
// One modal loop in the UI task (like a core start): while it runs nothing else of the UI runs, so no countdown ends and no key starts a core; a command from the
// network is answered "busy" and dropped (fe_idle -> fm_service), never kept for later. There is no route for it in webui.cc and no state of it in the snapshot (D9, S3).
static int ff_boot_rows(void *, const char *name) { return hc_boot_rows_on(name); }
static void ff_begin(void *)
{
    fmt_active = true;
    hc_ui_changed();
}
static void ff_end(void *)
{
    fm_close();
    fmt_active = false;
}
static const HcFormatEnv format_env = { &flow_env, ff_boot_rows, ff_begin, ff_end, 0 };

static const uint8_t *cve_icon(void *, const char *path, const char *name)
{
    static uint16_t icon[ICON_PX] __attribute__((aligned(4)));
    load_full_icon(path, name, icon);
    return (const uint8_t *)icon;
}
static uint8_t *cve_buf(void *, uint32_t *len)
{
    *len = sizeof(fo_buf);
    return fo_buf;
}
static const HcConvertEnv convert_env = { &flow_env, cve_icon, cve_buf, 0 };

// ---- T152 D8/S8/S9: the ESP32 inspection and write are the core's (host_core_flow_esp.cc); this is the screen's side. ------------------------------
static void ese_yes_key(void *, int key) { fm_yes_key = key; }
static const HcEspEnv esp_env = { &flow_env, ese_yes_key, 0 };

// ---- the flows as jobs the loop steps (host_core_job.h) ---------------------------------------------------------------------------------------------------
// ue_flow starts a job and returns; the loop steps it (sj_pump) instead of reading keys, so the UI task is never inside a flow: a flow waits where it asks and the loop
// goes round. While a job runs nothing else of the UI runs (no countdown, no key starts a core: fmt_active), as when the flows were one modal call each; what the person
// sees and presses is the same. sj_finish is what each flow's function did after the call.
enum { SJ_RESTORE = 1, SJ_UPDATE, SJ_CONVERT, SJ_INSPECT, SJ_FORMAT, SJ_RENAME, SJ_WIFI, SJ_UNZIP };
static struct {
    HcJob *job;
    FlowScope *scope;
    int kind;
    char arg[200];
    int index;
} sj;
static bool sj_active(void) { return sj.job != 0; }
#ifdef HOSTUI_RENDER_TEST
bool hostui_test_async_jobs;                              // the bench steps the job itself (hostui_test_job_*)
#endif
static void sj_finish(void)
{
    HcJob *j = sj.job;
    bool ok = j->ok;
    switch (sj.kind) {
    case SJ_RESTORE:
        fm_close();
        fmt_active = false;
        if (ok) {
            fw_written = true;
            set_note("Firmware written. Switch the power off and on.", HCT_OK);
        }
        refresh_status();
        browse_load();
        break;
    case SJ_UPDATE:
        fm_close();
        fmt_active = false;
        if (ok) {
            fw_written = true;
            set_note("Update written. Switch the power off and on.", HCT_OK);
        }
        refresh_status();
        browse_load();
        break;
    case SJ_CONVERT:
        fm_close();
        fmt_active = false;
        if (ok) {
            char n[96];
            snprintf(n, sizeof(n), "%s written", base_of(hc_convert_dst(j)));
            set_note(n, HCT_OK);
        }
        browse_load();
        break;
    case SJ_INSPECT:
        fm_close();
        fmt_active = false;
        break;
    case SJ_UNZIP:
        fmt_active = false;
        if (ok) {
            set_note("Unzipped", HCT_OK);
        }
        browse_load();                                     // the folder holds what was unpacked
        break;
    case SJ_RENAME:
    case SJ_WIFI:
        break;
    case SJ_FORMAT:
        if (hc_format_outcome(j) == HCFO_RAN) {
            refresh_status();
            for (int i = 0; i < cfg.n; i++) {              // the medium changed under the menu rows: read every entry again (T101 S10b)
                lazy_mark(i);
            }
            browse_load();
        }
        break;
    }
    delete j;
    sj.job = 0;
    delete sj.scope;                                       // the service's flow ends with the call, as the old function's scope did
    sj.scope = 0;
    need_full = true;
}
// One step of the running flow. Returns true when it ended (the aftermath is done).
static bool sj_pump(void)
{
    if (!sj.job) {
        return true;
    }
    HcJobIn in;
    in.key = -1;
    if (!sj.job->step(in)) {
        return false;
    }
    sj_finish();
    return true;
}
// A job the core made (the WiFi page's modals) is adopted: stepped like the others.
static void sj_adopt(int kind, HcJob *j)
{
    if (sj.job) {
        delete j;
        return;
    }
    sj.kind = kind;
    sj.job = j;
}
static void sj_start(int kind, const char *arg, int index = 0)
{
    if (sj.job) {
        return;
    }
    snprintf(sj.arg, sizeof(sj.arg), "%s", arg);
    sj.kind = kind;
    sj.index = index;
    switch (kind) {
    case SJ_RESTORE:
        sj.scope = new FlowScope("restore", base_of(sj.arg));
        fmt_active = true;                                          // nothing starts while this runs (the same flag as the format)
        hc_ui_changed();
        sj.job = hc_restore_job(restore_env, sj.arg);
        break;
    case SJ_UPDATE:
        sj.scope = new FlowScope("update", base_of(sj.arg));
        fmt_active = true;
        hc_ui_changed();
        sj.job = hc_update_job(flow_env, sj.arg);
        break;
    case SJ_CONVERT:
        sj.scope = new FlowScope("convert", base_of(sj.arg), true);
        fmt_active = true;
        hc_ui_changed();
        sj.job = hc_convert_job(convert_env, sj.arg);
        break;
    case SJ_INSPECT:
        sj.scope = new FlowScope("inspect", base_of(sj.arg), true);
        fmt_active = true;
        hc_ui_changed();
        sj.job = hc_inspect_job(esp_env, sj.arg);
        break;
    case SJ_UNZIP:
        sj.scope = new FlowScope("unzip", base_of(sj.arg), true);
        fmt_active = true;
        hc_ui_changed();
        sj.job = hc_zip_job(*hostzip_env(&flow_env), sj.arg);
        break;
    case SJ_RENAME:
        if (have_ui) {
            EN(A_RENAME);
        }
        if (rn_root) {                                              // the panel exists (a build with no pixel layer has none)
            sj.job = hc_rename_job(flow_env, rename_env, sj.index);
        }
        break;
    case SJ_FORMAT:
        sj.scope = new FlowScope("format", sj.arg, true);
        sj.job = hc_format_job(format_env, sj.arg);                 // the job's begin() sets fmt_active
        break;
    }
}
// ---- keys -----------------------------------------------------------------------------------------------------------------------

static void go(View v)
{
    hc_fo_replace_drop();                     // T139: a pending question ends with the view
    hc_nav_go((int)v);
    show_view(v);
}

// ---- the network side (T106): one writer, and it is this task --------------------------------------
// The web task only reads the snapshot below and leaves a command behind; nothing there touches the
// menu, the settings or the picture. Published whenever anything it shows has changed.
static void fill_snapshot(WebSnapshot *wp)
{
    WebSnapshot &w = *wp;
    memset(&w, 0, sizeof(w));
    snprintf(w.app, sizeof(w.app), "%s", APP_VERSION_STRING);
    snprintf(w.host_ver, sizeof(w.host_ver), "%s", APP_HOST_VERSION_STRING);
    snprintf(w.app_date, sizeof(w.app_date), "%s", APP_BUILD_DATE);
    snprintf(w.board, sizeof(w.board), "%s", hostreport_board());
    w.direct = cfg.direct;
    w.autostart_s = cfg.autostart_s;
    w.lib_attempt = cfg.lib_attempt;
    {
        // T162: the settings as the store writes them, minus the core= lines (the report lists those with their size and hash)
        static char txt[BOOT_MAX * 260 + 128];
        int tn = bootcfg_format(&cfg, txt, sizeof(txt));
        int o = 0;
        for (int i = 0; i < tn && txt[i];) {
            int e = i;
            while (e < tn && txt[e] && txt[e] != '\n') {
                e++;
            }
            if (strncmp(txt + i, "core=", 5) && o + (e - i) + 1 < (int)sizeof(w.cfgtext)) {
                memcpy(w.cfgtext + o, txt + i, e - i);
                o += e - i;
                w.cfgtext[o++] = '\n';
            }
            i = e + 1;
        }
        w.cfgtext[o] = 0;
    }
    snprintf(w.host_key, sizeof(w.host_key), "%08x", (unsigned)cor_host_key);
    w.net = c_net;
    snprintf(w.ip, sizeof(w.ip), "%s", c_ip);
    w.usb = c_usb;
    w.sd = c_sd;
    snprintf(w.media, sizeof(w.media), "%s", c_media);
    w.vid_sys = hc_picture_sys();
    w.vid_hdmi = hc_picture_hdmi();
    w.vid_mask = hv_state()->mask;
    w.vid_def = hv_state()->def_hdmi;
    w.countdown = cfg.countdown;
    w.def = cfg.def;
    w.n = cfg.n < BOOT_MAX ? cfg.n : BOOT_MAX;
    for (int i = 0; i < w.n; i++) {
        snprintf(w.name[i], WEB_NAME_MAX, "%s", cfg.e[i].name);
        snprintf(w.path[i], WEB_PATH_MAX, "%s", cfg.e[i].path);
        HcEntryRow v;
        char t[24];
        w.configs[i] = 0;
        if (hc_row_entry(i, true, &v)) {
            w.configs[i] = v.configs;
            snprintf(t, sizeof(t), "%s", v.badge);
            snprintf(w.state[i], sizeof(w.state[i]), "%s", t);
        }
        w.startable[i] = startable(i);
        w.size[i] = cfg.e[i].size;
        snprintf(w.hash[i], sizeof(w.hash[i]), "%s", cfg.e[i].hash);
    }
}

static void web_snapshot(void)
{
    static WebSnapshot w;
    fill_snapshot(&w);
    web_publish(&w);
}

// The old web page's commands (T106) go in as intents of the service (hostdomain.h: hostapi_apply), the same dispatcher a remote client's `entry.default` takes: the row is
// focused, the intent is queued with the keys, and the screen acts on it in the core (host_core_ui.cc, web.*), which also sets the answer the page shows (ue_web_result).
// What the service refuses (a flow or a question owns the keys; the row is not there) is the answer here.
static bool web_apply(const char *action, int row, const char *always_text)
{
    UicIntent in;
    memset(&in, 0, sizeof(in));
    in.kind = UD_OP;
    snprintf(in.item, sizeof(in.item), "%s", action);
    if (row >= 0) {
        snprintf(in.target, sizeof(in.target), "#%d", row);
    }
    UicReply r;
    hostapi_apply(in, &r);
    if (r.r != UR_TAKEN) {
        web_result(always_text ? always_text : (r.why[0] ? r.why : "not done"));
        return false;
    }
    return true;
}


static void web_pump(void)
{
    static bool started;
    static unsigned last_fp;
    WebCmd c;
    if (web_take(&c)) {
        switch (c.kind) {
        case WEB_VIDEO:
            hc_ui_web_arg(c.b);
            web_apply("web.video", -1, 0);
            break;
        case WEB_BOOT: {
            // T198 S13: the intent only queues keys (hostapi_pass), the start runs a few passes later in HCA_WEB_START, which arms the name for that entry's start
            // alone (path-bound, around the synchronous start_any, question included) and clears it when the start returns, yes or no, started or refused.
            // The name is set on every web start (also ""), so one that never reached HCA_WEB_START is overwritten by the next.
            const char *wpath = c.a >= 0 && c.a < cfg.n ? cfg.e[c.a].path : "";
            hc_ui_web_config(wpath, c.config);
            if (c.config[0] && !hc_cfgov_exists(wpath, c.config)) {
                hc_ui_web_config("", "");
                web_result("that entry has no config of that name");
                break;
            }
            if (!web_apply("web.start", c.a, "that entry cannot be started")) {
                hc_ui_web_config("", "");                // refused by the service: nothing stays armed
            }
            break;
        }
        case WEB_REMOVE:                                          // D19a: the page's remove is a question (webnet.cc); no path removes unasked
            web_result("a removal asks first: answer the question on the page");
            break;
        case WEB_DEFAULT:
            web_apply("web.default", c.a, 0);
            break;
        case WEB_COUNTDOWN:
            hc_ui_web_arg(c.a);
            web_apply("web.countdown", -1, 0);
            break;
        case WEB_NONE:
            break;
        }
        web_snapshot();
    }
    unsigned fp = hc_ui_fingerprint() + (unsigned)c_net * 7919 + (unsigned)hc_picture_hdmi() * 131;
    if (fp != last_fp) {
        last_fp = fp;
        web_snapshot();
    }
    if (!started && c_net) {
        started = true;
        web_start();                             // D6: only once there is an address
    }
}

// What the keys of one batch asked for: a rebuild of the view, or only a selection move (two rows restyled /
// the highlight bar moved and the manifest panel refilled; no full-screen render).
static uint32_t last_inv;
static int last_inv_n;                  // invalid areas LVGL held at the last count (the bench prints them)
static uint32_t inv_pixels(void)
{
    lv_display_t *d = lv_display_get_default();
    uint32_t n = 0;
    last_inv_n = d ? d->inv_p : 0;
    if (d) {
        for (int i = 0; i < d->inv_p; i++) {
            if (!d->inv_area_joined[i]) {
#ifdef HOSTUI_RENDER_TEST
                if (getenv("AREA_DUMP")) {          // the bench: which rectangles a key left (x1 y1 x2 y2)
                    printf("  area %d,%d..%d,%d\n", (int)d->inv_areas[i].x1, (int)d->inv_areas[i].y1, (int)d->inv_areas[i].x2, (int)d->inv_areas[i].y2);
                }
#endif
                n += (uint32_t)lv_area_get_width(&d->inv_areas[i]) * lv_area_get_height(&d->inv_areas[i]);
            }
        }
    }
    return n;
}


static bool need_sel;
static int sel_old, bsel_old;

static int g_redraws;                   // T229: full refills of the current view (the bench counts what a cache hit costs)
static void apply_updates(void)
{
    if (need_full) {
        g_redraws++;
        refresh_current();
    } else if (need_sel && have_ui) {
        if (nv.view == V_MENU || nv.view == V_EDIT) {
            Row *rows = nv.view == V_MENU ? mrow : erow;
            int first = nv.view == V_MENU ? nv.mfirst : nv.efirst;
            int o = sel_old - first, n = nv.sel - first;
            const int cnt = nv.view == V_MENU ? MROWS : EROWS;
            if (o >= 0 && o < cnt) row_style(rows[o], false);
            if (n >= 0 && n < cnt) row_style(rows[n], true);
            if (nv.view == V_MENU) {
                menu_status();
            } else {
                state_status(e_note);
            }
            if (nv.view == V_MENU && !hc_cfgov_is_open()) {
                set_foot(foot_label, hc_menu_foot());       // T198 D9: the hint follows the selected row
            } else if (nv.view == V_EDIT) {
                set_foot(foot_label, hc_edit_foot());
            }
        } else if (nv.view == V_BROWSE) {
            int first = nv.bfirst;
            listwin_fix(nv.bsel, nv.bn, BROWS, &first);
            if (first != nv.bfirst) {
                update_browse();                    // the list scrolls: refill the 13 rows
            } else {
                browse_mark();
                browse_right_update();
                browse_foot();
                browse_sub_update();
            }
        }
    }
    need_full = need_sel = false;
}

// One input of the screen: a key (the board's, or a `key` intent's) or an intent (HC_ACT_KEY, from a declared action). The core resolves it by the key map and acts on the
// intent (host_core_ui.cc); this is what is left to do on the screen afterwards.
static void on_key(int k)
{
    HcUiResult r;
    hc_ui_input(k, &r);
    if (r.selmove && !need_full) {
        if (!need_sel) {
            sel_old = r.sel0;
            bsel_old = r.bsel0;
        }
        need_sel = true;
    } else if (r.redraw) {
        need_full = true;
    }
#ifdef HOSTUI_RENDER_TEST
    while (sj_active() && !hostui_test_async_jobs) {          // the bench's scenarios are written as one call per flow: the job is stepped to its end here
        sj_pump();
    }
#endif
}

// ---- boot.cfg: the Flash disk may not be mounted when the UI starts, so a failed load is retried ----
static bool try_load_cfg(void)
{
    int sr;
    if (!hc_cfg_try_load(&sr)) {
        return false;
    }
    if (sr != HCR_REFUSED) {
        cfg_after(sr);
    }
    STARTLOG("[host] %s: %d cores, default %d, countdown %d", BOOTCFG_PATH, cfg.n, cfg.def, cfg.countdown);
    if (startrec_cfg(cfg.n, cfg.def, cfg.direct, cfg.countdown)) {
        slog_flush();                                    // a read after the decision (the menu's retry) is a line of its own
    }
    return true;
}

// ---- T135: the direct start --------------------------------------------------------------------------------------------------
// Before any LVGL work: boot.cfg, the return marker, the menu keys and a cheap look at the default decide (directboot.h). A start goes
// through the same code as the countdown's, without its checks (D10): core_stage with skip_checks, legacy_stage with direct (a stamped
// scratch file is only checked for completeness). Feedback is the hardware text layer (ovtext.h); LVGL is not started on a direct start.
#define DIRECT_CFG_WAIT_MS    1500      // how long a boot.cfg that cannot be read yet (the flash disk is mounted late) is waited for
#define DIRECT_KEY_WINDOW_MS  500       // Q4: the keys are taken for this long from the start of the decision
#define DIRECT_MEDIUM_WAIT_MS BOOT_WAIT_MS   // issue #14: a default on SD / USB whose file is not there yet: the medium may still be coming up (a USB stick takes many seconds)
static bool direct_video_done;          // the picture was put up by the direct phase: the menu's start-up must not do it again
#ifndef HOSTUI_RENDER_TEST             // the render bench has no hardware to decide with: it always shows the menu

static DbDefault direct_default_state(void)
{
    probe_state(cfg.def);
    switch (hc_est(cfg.def)) {
    case ES_OK: return DD_OK;
    case ES_CHANGED: return cfg.e[cfg.def].mtime ? DD_CHANGED : DD_OK;     // an entry without a stored check (an installer wrote it) starts as it is
    case ES_MISSING: return DD_MISSING;
    default: return DD_REFUSED;
    }
}

// Issue #14: the hardware behind directboot_look (directboot.h).
static uint32_t direct_env_now(void *) { return now_ms(); }
static void direct_env_sleep(void *, uint32_t ms) { menukeys_sample(); vTaskDelay(pdMS_TO_TICKS(ms)); }
static DbDefault direct_env_look(void *) { return direct_default_state(); }
static bool direct_env_key(void *) { menukeys_sample(); return menukeys_cbm() || menukeys_fire(); }
static void direct_env_waiting(void *ctx, bool on)
{
    if (!*(bool *)ctx) {                                   // the text layer is not up
        return;
    }
    if (on) {
        const char *p = cfg.e[cfg.def].path;
        char medium[24] = "";
        if (*p == '/') {
            p++;
        }
        for (size_t i = 0; *p && *p != '/' && i < sizeof(medium) - 1; i++) {
            medium[i] = *p++;
        }
        char line[48];
        snprintf(line, sizeof(line), "Waiting for %s - hold C= for the menu", medium);
        ovtext_line(line);
    } else {
        ovtext_start_line(cfg.e[cfg.def].name);
    }
}
static bool direct_env_root(void *)
{
    const char *p = cfg.e[cfg.def].path;
    if (*p == '/') {
        p++;
    }
    size_t l = 0;
    while (p[l] && p[l] != '/') {
        l++;
    }
    HostDirEntry r[16];
    int n = hostfs_list("/", r, 16, false);
    for (int i = 0; i < n; i++) {
        if (strlen(r[i].name) == l && !strncasecmp(r[i].name, p, l)) {
            return true;
        }
    }
    return false;
}

// T135 Q4b: the keys are polled on through the staging, up to the jump; C= or fire asks the stage to stop (CORE_ABORTED).
static int direct_keys_cb(int step, int percent, const char *msg, void *ctx)
{
    (void)step; (void)percent; (void)msg; (void)ctx;
    menukeys_sample();
    return menukeys_cbm() || menukeys_fire();
}

// Returns only when the default was not started: the text says what was wrong, or CORE_ABORTED when a key stopped it (Q4b).
static bool dr_no_key(void *) { return false; }
static void dr_no_pause(void *, int) { }
static void dr_no_settle(void *) { }
static void dr_before_jump(void *, bool full)
{
    ovtext_off();
    STARTLOG(full ? "direct boot: jump (full firmware)" : "direct boot: jump");
}
static int dr_progress(int step, int percent, const char *msg, void *ctx) { return direct_keys_cb(step, percent, msg, ctx); }
static const HcStartHooks direct_start_hooks = { dr_progress, dr_no_key, dr_no_pause, dr_no_settle, st_lap, dr_before_jump, NULL };

static const char *direct_run(void)
{
    const BootEntry &e = cfg.e[cfg.def];
    const HcStartEntry entry = { e.path, hc_efull(cfg.def), hc_efull(cfg.def) ? NULL : &e, false, e.defcfg };
    const char *why = hc_start_sequence(entry, HC_START_DIRECT, direct_start_hooks, NULL);
    return why ? why : "failed";
}

static void direct_phase(void)
{
    uint32_t t_begin = now_ms();
    DirectIn in;
    memset(&in, 0, sizeof(in));
    startrec_init(now_ms);
    startrec_window_open(DIRECT_KEY_WINDOW_MS);
    STARTLOG("direct boot: decision begins");
    while (!try_load_cfg() && now_ms() - t_begin < DIRECT_CFG_WAIT_MS) {
        menukeys_sample();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    in.cfg_loaded = hc_cfg_loaded();
    in.direct = cfg.direct;
    in.n = cfg.n;
    in.def = cfg.def;
    in.marker = handover_menu_take((void *)HANDOVER_ADDR);
    menukeys_sample();
    in.key_cbm = menukeys_cbm();
    in.key_fire = menukeys_fire();
    in.dstate = DD_OK;
    // everything but the default's file says "start": the picture and the one line go up now, so the person sees what to hold during the window
    bool text = directboot_decide(in) == DB_START;
    if (text) {
        hc_picture_start(true, 0, 0);
        direct_video_done = true;
        uint32_t ms = ovtext_start_line(cfg.e[cfg.def].name);
        STARTLOG("direct boot: text line up (%u ms to write it)", (unsigned)ms);
    }
    while (now_ms() - t_begin < DIRECT_KEY_WINDOW_MS && !menukeys_cbm() && !menukeys_fire()) {
        menukeys_sample();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    menukeys_sample();
    startrec_window_close();
    in.key_cbm = menukeys_cbm();
    in.key_fire = menukeys_fire();
    if (directboot_needs_file(in)) {
        bool removable = strncasecmp(cfg.e[cfg.def].path, "/Flash", 6) != 0;
        DbEnv env = { direct_env_now, direct_env_sleep, direct_env_look, direct_env_root, direct_env_key, direct_env_waiting, &text };
        in.dstate = directboot_look(env, removable, DIRECT_MEDIUM_WAIT_MS, DB_GRACE_MS);
        in.key_cbm = menukeys_cbm();
        in.key_fire = menukeys_fire();
    }
    DbReason why = directboot_decide(in);
    startrec_decide(in, why);
    slog_flush();                                                // on the SD card before the core is loaded
    STARTLOG("direct boot: decision %s (cfg %d, direct %d, marker %d, C= %d, fire %d, %u key samples)", why == DB_START ? "START" : directboot_text(why),
             in.cfg_loaded, in.direct, in.marker, in.key_cbm, in.key_fire, (unsigned)menukeys_samples());
    if (why == DB_START) {
        printf("[host] direct boot: %s\n", cfg.e[cfg.def].name);
        const char *fail = direct_run();
        if (fail == CORE_ABORTED || !strcmp(fail, CORE_ABORTED)) {    // Q4b: a key during the staging, the start is cancelled
            DbReason k = menukeys_cbm() ? DB_KEY_CBM : DB_KEY_FIRE;
            STARTLOG("direct boot: cancelled, %s (%u key samples)", directboot_text(k), (unsigned)menukeys_samples());
            startrec_event("direct start cancelled: %s", directboot_text(k));
            slog_flush();
            set_note(directboot_text(k), HCT_WARN);
        } else {
            printf("[host] direct boot failed: %s\n", fail);
            startrec_event("direct start failed: %s", fail);
            slog_flush();
            char nb[96];
            snprintf(nb, sizeof(nb), "Direct start failed: %s", fail);
            set_note(nb, HCT_BAD);
        }
        hc_countdown_give_up();
    } else {
        if (why == DB_MISSING || why == DB_CHANGED || why == DB_REFUSED || why == DB_NODEFAULT) {
            set_note(directboot_text(why), HCT_WARN);
        }
        if (directboot_stops_countdown(why)) {
            hc_countdown_give_up();
        }
    }
    if (text) {
        ovtext_off();
    }
}

#else
static void direct_phase(void) { }
#endif

// ---- T146: the saved picture of the menu --------------------------------------------------------------------------------------
// Flash:/menu.snap holds the menu as the last settled frame showed it (menusnap.h). At a start into the menu it is decoded into one
// frame buffer and put on the layer before LVGL builds anything; LVGL renders into the other buffer and its first flush flips.
static MsSettle snap_st;
static MsKeyQ snap_keys;                    // D4: keys read while the picture is up
static bool snap_file_known;                // the file's key and data CRC at the last load / save
static uint32_t snap_file_key, snap_file_crc;

static int snap_text(char *out, int max) { return bootcfg_format(&cfg, out, max); }

static uint32_t snap_key_now(void)
{
    static char text[BOOT_MAX * 260 + 128];
    int n = snap_text(text, sizeof(text));
    return ms_key(text, n, hv_state()->def_sys, hv_state()->def_hdmi, APP_VERSION_HASH);
}

// At the start, with the layer's buffers allocated and nothing on the layer. True when the picture is up.
static bool snap_try_show(void)
{
    ms_settle_init(&snap_st);
    ms_keyq_init(&snap_keys);
    if (!hc_cfg_loaded()) {
        try_load_cfg();
    }
    uint16_t *a, *b;
    trx_lv_buffers(&a, &b);
    if (!hc_cfg_loaded() || !a || !b) {
        return false;
    }
    int n = hostfs_load(MS_PATH, (char *)b, MS_RAW_BYTES);        // the idle buffer is the read buffer
    if (n < 0) {
        STARTLOG("menu picture: none");
        return false;
    }
    uint32_t hk, hc;
    if (ms_header_info((const uint8_t *)b, n, &hk, &hc)) {
        snap_file_known = true;
        snap_file_key = hk;
        snap_file_crc = hc;
    }
    MsResult r = ms_load((const uint8_t *)b, n, MS_W, MS_H, snap_key_now(), a);
    if (r != MS_OK) {
        STARTLOG("menu picture: refused (%s), splash", ms_result_text(r));
        return false;
    }
    if (!trx_lv_show_picture(a)) {
        STARTLOG("menu picture: the layer did not take it");
        return false;
    }
    STARTLOG("menu picture shown (%d bytes)", n);
    return true;
}

// Keys pressed while the picture is up are taken in between the start-up phases and handed to the loop in order.
static void snap_poll_keys(void)
{
    int k;
    while ((k = host_getch_nowait()) > 0) {
        ms_keyq_push(&snap_keys, k);
    }
}

static int menu_getch(void)
{
    int k = ms_keyq_pop(&snap_keys);
    return k > 0 ? k : host_getch_nowait();
}

static void snap_save(void)
{
    if (COMPACT) {
        return;                                      // the saved picture is the 960 x 540 canvas (MS_W x MS_H); a compact canvas is not saved
    }
    lv_display_t *d = lv_display_get_default();
    uint16_t *a, *b;
    trx_lv_buffers(&a, &b);
    if (!d || !d->buf_1 || !a || !b) {
        return;
    }
    const uint16_t *px = (const uint16_t *)d->buf_1->data;           // the single buffer LVGL renders into and shows
    uint16_t *scratch = (px == a) ? b : a;                           // the other one is idle
    uint32_t t0 = now_ms();
    uint32_t key = snap_key_now();
    int n = ms_encode(px, MS_W, MS_H, key, (uint8_t *)scratch, MS_RAW_BYTES);
    if (n < 0) {
        printf("[host] menu picture: not saved, above %d bytes\n", (int)(MS_RAW_BYTES < MS_LIMIT ? MS_RAW_BYTES : MS_LIMIT));
        return;
    }
    uint32_t k2, c2;
    ms_header_info((const uint8_t *)scratch, n, &k2, &c2);
    if (snap_file_known && snap_file_key == k2 && snap_file_crc == c2) {
        printf("[host] menu picture: same as the file, not written (%d bytes)\n", n);
        return;
    }
    bool ok = hostfs_save(MS_DIR, MS_FILE, scratch, n);
    if (ok) {
        snap_file_known = true;
        snap_file_key = k2;
        snap_file_crc = c2;
    }
    printf("[host] menu picture: %s, %d bytes, %u ms\n", ok ? "saved" : "NOT saved", n, (unsigned)(now_ms() - t0));
}

// The loop calls this every pass: every 250 ms the state of the screen is boiled down to a signature; when it has not changed for
// MS_QUIET_MS (and is a state a picture may show: the menu, no countdown, no note, nothing left to draw) the picture is saved.
static void snap_tick(uint32_t t, int nkeys)
{
    static uint32_t last;
    if (!have_ui) {
        return;
    }
    if (nkeys) {
        ms_settle_touch(&snap_st, t);
    }
    if (t - last < 250) {
        return;
    }
    last = t;
    static char text[BOOT_MAX * 260 + 128];
    uint32_t sig = ms_crc32(0, text, snap_text(text, sizeof(text)) > 0 ? strlen(text) : 0);
    int32_t st[8] = { hv_state()->def_sys, hv_state()->def_hdmi, nv.sel, nv.mfirst, (int32_t)nv.view, hc_cd_running(), nv.note[0], hc_cd_dead() };
    sig = ms_crc32(sig, st, sizeof(st));
    int32_t es[BOOT_MAX];
    for (int i = 0; i < BOOT_MAX; i++) {
        es[i] = hc_est(i);
    }
    sig = ms_crc32(sig, es, sizeof(es));
    bool ok = nv.view == V_MENU && !hc_cd_running() && hc_cd_dead() && !nv.note[0] && !lazy_busy() && !nkeys && inv_pixels() == 0 && nv.mfirst == 0 &&
              nv.sel == (cfg.def >= 0 ? cfg.def : 0) && hc_cfg_loaded();
    if (ms_settle(&snap_st, t, sig, ok, MS_QUIET_MS)) {
        snap_save();
        ms_settle_saved(&snap_st);
    }
}

// ---- the loop -----------------------------------------------------------------------------------------------------------------
// T101 S9: the splash is the first thing on the pixel layer; each step is one bar value and one label, rendered at once.
// The steps are the work that precedes the first menu frame, in order. Nothing searches the media (T101 S10).
#define HS_STEPS 3
#ifdef HOSTUI_RENDER_TEST
static void (*hs_probe)(int step);                   // the bench looks at the frame after every step
#endif
static void hs_step(int step, const char *label)
{
    STARTLOG("phase %s", label);
    if (snap_shown) {
        snap_poll_keys();
    } else if (have_ui) {
        trx_splash_step(step, label);
        lv_refr_now(NULL);
    }
#ifdef HOSTUI_RENDER_TEST
    if (hs_probe) {
        hs_probe(step);
    }
#endif
}

static const HcUiEnv &ui_env_ref(void);

// Everything up to the first menu frame; the loop is hostui_run()'s.
static void startup(void)
{
    STARTLOG("phase UI task starts");
    mt_mark("startup begins");
    hc_init(&core_plat);
    hc_cfgov_io(hostfs_cm(), hostfs_fo());
    hc_nav_init(&nav_plat);
    hc_rows_init(&rows_plat);
    snprintf(txu_running_version, sizeof(txu_running_version), "%s", APP_BUNDLE_VERSION_STRING);   // T221 D19
    hc_ui_init(&ui_env_ref());
    hc_cfg_clear();
    direct_phase();                                  // T135: a direct start never returns; otherwise the menu below, with the reason in its note
    matrix_kb = new Keyboard_C64(&matrix_host, &U64II_KEYB_ROW, &U64II_KEYB_COL, &U64II_KEYB_JOY);
    matrix_kb->set_keyboard_only(true);                 // T135 Q9: the joystick never drives the host menu; port-1 fire is only the power-on key (menukeys.cc)

    have_ui = trx_lv_open();
    if (have_ui) {
        picture_sync();   // T164 boot720: the placement of the mode that is up, before the saved picture or the splash is shown (else the layer opens with the 1080p one)
        scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_flag(scr, LV_OBJ_FLAG_SCROLLABLE, false);
        mt_mark("layer open, colours");
        snap_shown = snap_try_show();   // T146: the last settled menu on the layer at once, instead of the splash
        g_mt.t_pic = snap_shown ? mt_now() : 0;
        mt_mark(snap_shown ? "picture shown (boot.cfg + picture file read, decoded)" : "no picture");
        buffers_mode(true);   // one buffer: no sync copy of the previous frame (that copy reads DDR2, the CPU has no data cache)
        if (!snap_shown) {
            char ver[64];
            snprintf(ver, sizeof(ver), "%s  %s", APP_VERSION_STRING, APP_BUILD_DATE);
            trx_splash_open(trx_overlay(), "TRX64", "host", ver, HS_STEPS);
        }
    } else {
        printf("[host] no pixel layer in this bitstream: the menu runs on the console only\n");
    }
    hs_step(0, "Reading boot.cfg");
    STARTLOG("first frame (splash) shown: %d", (int)have_ui);
    if (!hc_cfg_loaded()) {
        try_load_cfg();
    }
    mt_mark("boot.cfg");
    // The picture the user chose, if there is one: hostapp.cc had to bring a mode up before the flash
    // disk was mounted, and that one came from the screen (T105).
    // ADR-0057: the host puts its own mode up whenever it is the one running - a core that changed the
    // picture for itself must not leave the board in it. With nothing stored, the screen is asked again
    // and the answer is kept, so the host knows what it has and hands that to the next core instead of
    // a -1 that means "nobody chose".
    hs_step(1, "Setting the screen mode");
    if (!direct_video_done) {
        hc_picture_start(true, 0, 0);               // the stored picture is host.cfg's, boot.cfg or not (T164 picsave)
    }
    mt_mark("screen mode");
    menu_place();                                    // T164 boot720: also when the direct phase put the mode up; the layer was opened with the 1080p placement
    hc_countdown_boot(now_ms());
    hs_step(2, "Building the menu");   // the last label: what follows only builds objects, drawn once by draw()
    if (have_ui) {
        mt_mark("menu place");
        layout_counts();                             // T209: the rows of the full canvas are computed too (one line per row), not only after a canvas change
        mt_mark("layout counts");
        build_ui();
        mt_mark("build_ui total (first screen)");
        ui_built = true;
        if (!lazy_build) {
            build_rest_all();                        // the benches: everything before the first frame, as it was
            mt_mark("all areas built");
        }
    }
    lazy_reset();                                    // no file of an entry is opened before the first frame (T101 S10)
    rows_load();                                     // T229: the rows the last settled menu held, taken at once; the files are only stat-checked afterwards
    mt_mark("lazy reset + row cache");
    refresh_status();
    hc_nav_sel_default();
    g_mt.t_built = mt_now();
    STARTLOG("phase menu built, drawing the first frame");
    if (snap_shown) {
        snap_poll_keys();
        lv_obj_invalidate(scr);                      // LVGL renders into the buffer the picture is not in: all of it
    } else {
        trx_splash_close();
    }
    draw();
    g_mt.t_frame = mt_now();
    mt_mark("first frame drawn");
    STARTLOG("first menu frame shown");
    if (snap_shown) {
        snap_poll_keys();
    }
    if (have_ui) {
        // colour check for the log: LVGL's first buffer must hold 0x0042 (#070a12) at the background and
        // 0x0884 (#0d1220) in the header. (The layer registers are write-only, they cannot be read back.)
        lv_display_t *dp = lv_display_get_default();
        const uint16_t *px = dp ? (const uint16_t *)((lv_display_t *)dp)->buf_1->data : 0;
        if (px) {
            printf("[host] pixels (buffer 1): bg %04x header %04x nav %04x line %04x\n", px[300 * 960 + 500], px[20 * 960 + 300],
                   px[200 * 960 + 20], px[(TOP_H - 1) * 960 + 300]);
        }
    }
}

// ---- the screen's side of the core's intents (host_core_ui.h) --------------------------------------------------------------------------------------
// The core acts on every key and intent; what only the screen can do comes through here: draw, run a flow's pages, put a picture mode up, start a core with its page. The
// model the service sees (host_core_model.cc) is the core's too.
static void ue_redraw(void *) { need_full = true; }
static void ue_show(void *, int v) { show_view((View)v); }
static void ue_note_out(void *, const char *text, int tone, bool net)
{
    printf(net ? "[host] wifi: %s\n" : "[host] %s\n", text);
    hostapi_note(text, tone == HCT_BAD);
}
static void ue_lazy_reset(void *) { lazy_reset(); }
static void ue_lazy_mark(void *, int i) { lazy_mark(i); }
static void ue_pq_hide(void *) { pq_hide(); }
static bool ue_picture_set(void *, int hdmi, const char *how) { return picture_set(hdmi, how); }
static void ue_start_any(void *, const char *path, const char *title, const BootEntry *trusted, bool ask) { start_any(path, title, trusted, ask); }
static void ue_start_core(void *, const char *path, const char *title) { slog_started(); start_core(path, title); }
static void ue_flow(void *, int kind, const char *arg, int index)
{
    switch (kind) {
    case HUF_RENAME:
    case HUF_CFGRENAME: sj_start(SJ_RENAME, "", index); break;
    case HUF_CONVERT: sj_start(SJ_CONVERT, arg); break;
    case HUF_RESTORE: sj_start(SJ_RESTORE, arg); break;
    case HUF_UPDATE: sj_start(SJ_UPDATE, arg); break;
    case HUF_FORMAT: sj_start(SJ_FORMAT, arg); break;
    case HUF_INSPECT: sj_start(SJ_INSPECT, arg); break;
    case HUF_UNZIP: sj_start(SJ_UNZIP, arg); break;
    }
}
static void ue_colors(void *, int what)
{
    switch (what) {
    case HUC_REPORT: buffer_report(); break;
    case HUC_SINGLE: buffers_mode(true); break;
    case HUC_MULTI: buffers_mode(false); break;
    }
}
static bool ue_busy(void *) { return fmt_active; }
static bool ue_report(void *, char *name, int ncap, char *why, int wcap)
{
    static WebSnapshot rs;
    fill_snapshot(&rs);
    return hostreport_write(&rs, name, ncap, why, wcap);
}
static void ue_report_dir(void *, char *out, int cap) { snprintf(out, cap, "%s", hostreport_dir()); }
static void ue_ask(void *, const char *text, int yes, int no) { hostapi_ask(text, yes, no); }
static void ue_history(void *, const char *value) { hostapi_history(value); }
static void ue_web_result(void *, const char *text) { web_result(text); }
static void ue_job(void *, HcJob *j) { sj_adopt(SJ_WIFI, j); }
static bool ue_key(void *, int k) { return hostapi_push_key(k); }
static const char *ue_footer(void *) { return have_ui && foot_label ? lv_label_get_text(foot_label) : 0; }
static const HcUiEnv &ui_env_ref(void)
{
    static HcUiEnv env;
    env.ctx = 0;
    env.redraw = ue_redraw;
    env.show = ue_show;
    env.note_out = ue_note_out;
    env.lazy_reset = ue_lazy_reset;
    env.lazy_mark = ue_lazy_mark;
    env.pq_hide = ue_pq_hide;
    env.picture_set = ue_picture_set;
    env.picture_event = ue_picture_event;
    env.start_any = ue_start_any;
    env.start_core = ue_start_core;
    env.flow = ue_flow;
    env.colors = ue_colors;
    env.busy = ue_busy;
    env.report = ue_report;
    env.report_dir = ue_report_dir;
    env.ask = ue_ask;
    env.history = ue_history;
    env.key = ue_key;
    env.footer = ue_footer;
    env.web_result = ue_web_result;
    env.job = ue_job;
    env.fo = &fo_env;
    env.cm = &cm_env_ref();
    env.wifi = &wifi_env;
    return env;
}


void hostui_run(void)
{
    startup();
#ifndef HOSTUI_RENDER_TEST
    hostapi_init(hc_model(), hostapi_board_platform());      // T192 D21: the service of the host; its routes are served once the network is up (web_pump)
    webnet_set_service(hostapi_service());
#endif
    uint32_t last = 0, last_key_ms = 0;
    bool redraw_pending = false;
    bool build_turn = false;
    mt_mark("main loop begins (keys are read from here)");
    for (;;) {
        uint32_t pass0 = now_ms();
        if (sj_active()) {                                  // a flow runs: it is stepped, nothing else of the UI runs (as when each flow was one modal call)
            if (sj_pump()) {                                // the flow ended: its aftermath is drawn here, as the key that ran the old call did (no pass between sees need_full)
                apply_updates();
                if (have_ui) {
                    lv_refr_now(NULL);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        redraw_pending = hostapi_pass(redraw_pending || need_full || need_sel) || redraw_pending;      // the service: remote intents queue keys, the snapshot follows
        if (redraw_pending) {
            apply_updates();
            redraw_pending = false;
        }
        // all queued keys first (a held key's repeats collapse into one move), then one update and one render
        int k, nk = 0;
        uint32_t t_key = 0;
        int last_key = 0;
        while (nk < 24 && !sj_active() && (k = menu_getch()) > 0) {      // a key that opened a job leaves the rest of the queue to the job
            if (!nk) {
                t_key = core_cycles();
            }
            on_key(k);
            last_key = k;
            nk++;
        }
        if (nk) {
            if (!g_mt.t_key1) {
                g_mt.t_key1 = mt_now();
                mt_mark("first key handled");
            }
            last_key_ms = now_ms();
            uint32_t t0 = core_cycles();
            apply_updates();
            uint32_t t1 = core_cycles();
            last_inv = inv_pixels();
            if (have_ui) {
                lv_refr_now(NULL);                  // render now, not at the next refresh tick
            }
            uint32_t t2 = core_cycles();
            printf("[host] key %02x (%d queued): update %u ms, render+flush %u ms, key to screen %u ms, area %u px\n", last_key, nk,
                   (unsigned)(t1 - t0), (unsigned)(t2 - t1), (unsigned)(t2 - t_key), (unsigned)last_inv);
        }
        uint32_t t = now_ms();
        if (t - last >= 3000 && t - last_key_ms >= 1500) {   // not while the user is typing
            last = t;
            // an entry whose medium has not shown up yet is looked at again
            bool redraw = false;
            {
                uint32_t ts = core_cycles();
                if (refresh_status() && have_ui) {
                    update_chips();                  // only the header chips (and the network page) change
                    if (nv.view == V_NET) {
                        update_net();
                    }
                    if (nv.view == V_SET && nv.wm_mode) {
                        update_settings();           // the WiFi page shows the module's state
                    }
                    if (nv.view == V_BROWSE && at_media_level()) {
                        browse_load();               // a medium came or went: the media level lists it
                        need_full = true;
                    }
                }
                uint32_t dt = core_cycles() - ts;
                if (dt >= 20) {
                    printf("[host] status refresh took %u ms\n", (unsigned)dt);
                }
            }
            if (!hc_cfg_loaded() && !hc_cfg_edited() && try_load_cfg()) {
                lazy_reset();
                hc_nav_sel_default();
                redraw = true;
            }
            for (int i = 0; i < cfg.n; i++) {
                if (hc_est(i) == ES_MISSING) {
                    lazy_mark(i);                    // its medium may have shown up since: looked at again, lazily
                }
            }
            if (redraw) {
                need_full = true;
            }
        }
        int cdev = hc_countdown_tick(t);         // the countdown begins once the default can be started; it is given up or spent
        if ((cdev & HCC_GAVE_UP) && nv.view == V_MENU) {
            need_full = true;
        }
        if (cdev & HCC_EXPIRED) {
            hc_start_config_set(cfg.e[cfg.def].defcfg);          // T198 D8: the countdown starts the entry with its default config
            start_origin = "countdown";
            start_any(cfg.e[cfg.def].path, cfg.e[cfg.def].name, &cfg.e[cfg.def], false);
            start_origin = "menu (key or network request)";
            hc_start_config_set("");
            go(V_MENU);
        } else if (hc_cd_running() && nv.view == V_MENU && have_ui) {
            cd_update();
        }
        snap_tick(t, nk);
        rows_tick(t, nk);                    // T229 D6: the file of the rows, when they have settled
        // T229: what the first frame did not need is built in steps of at most 40 ms, when no key came in this pass; with rows to read, in turn with them
        bool built_now = false;
        if (have_ui && !nk && build_pending() && (!lazy_busy() || (build_turn = !build_turn))) {
            build_pass();
            built_now = true;
        }
        if (!built_now) {
            lazy_pass(nk, t, last_key_ms);   // after the keys of this pass: one phase of one entry, when no key is near
        }
        web_pump();                          // T106 D2: the one place the web task's commands are applied
        picture_tick();                      // T164: the hot-plug follow and the question's timer
        if (!nk && (need_full || need_sel)) {
            apply_updates();
        }
        uint32_t r0 = core_cycles();
        uint32_t px_before = inv_pixels();
        int ms = have_ui ? trx_lv_handler() : 20;
        uint32_t r1 = core_cycles();
        if (r1 - r0 >= 5) {
            printf("[host] render %u ms, %u px invalid\n", (unsigned)(r1 - r0), (unsigned)px_before);
        }
        if (ms < 0) {
            have_ui = false;
            ui_built = false;
        }
        {
            uint32_t pass1 = now_ms();
            if (g_mt.redraw_row1) {                  // T229 S1: what the redraw after a unit cost
                MtRow &mr = g_mt.row[g_mt.redraw_row1 - 1];
                uint32_t d = pass1 - g_mt.t_unit_end;
                mr.redraw += d > 65535 ? 65535 : (uint16_t)d;
                g_mt.redraw_row1 = 0;
            }
            if (pass1 - pass0 > g_mt.pass_max) {
                g_mt.pass_max = pass1 - pass0;
                g_mt.pass_at = mt_now();
            }
            if (!g_mt.t_idle && g_mt.t_frame && !lazy_busy()) {
                g_mt.t_idle = mt_now();                  // the reader has nothing left to read or check
            }
            if (!g_mt.reported && g_mt.t_idle && !nk && !build_pending() && mt_now() - g_mt.t_idle >= 1000) {
                mt_report();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


#ifdef HOSTUI_RENDER_TEST
// T101 S9: the start-up sequence as the app runs it (splash steps, then the first menu frame); `probe` sees the frame after each step.
int hostui_test_fw_writes(void) { return hc_restore_writes(); }
// The flow as a job the bench steps by hand (hostui_test_job_async): is one running, where it stands, one step (true: it ended).
// T229 build phase: the benches start with everything built; the lazy bench turns the lazy build on before the start and asks which areas exist
void hostui_test_lazy_build(bool on) { lazy_build = on; }
int hostui_test_built(void)
{
    int m = 0;
    for (int a = 0; a < A_N; a++) {
        m |= b_done[a] ? 1 << a : 0;
    }
    return m;
}
int hostui_test_build_steps(void)                // one main-loop pass of the build; steps are bounded by 40 ms: the number of passes to the end
{
    int n = 0;
    while (build_pending() && n < 10000) {
        build_pass(0);                                 // one step per pass: what the board does in the worst case
        n++;
    }
    return n;
}
bool hostui_test_job_active(void) { return sj_active(); }
int hostui_test_job_state(void) { return sj.job ? sj.job->state : (int)HCJ_DONE; }
bool hostui_test_job_step(void) { return sj_pump(); }
void hostui_test_job_async(bool on) { hostui_test_async_jobs = on; }
int hostui_test_up_writes(void) { return hc_update_writes(); }
const char *hostui_test_up_why(void) { return last_up_why; }
const char *hostui_test_esp_line(void) { return hc_inspect_line(); }
int hostui_test_esp_writes(void) { return hc_esp_writes(); }
const char *hostui_test_oc_title(int *w, int *room)       // T198: what the overlay's title label shows, its box width, the width it may use
{
    lv_obj_update_layout(oc_title);
    *w = (int)lv_obj_get_width(oc_title);
    *room = (int)(intptr_t)lv_obj_get_user_data(oc_title);
    return lv_label_get_text(oc_title);
}
bool hostui_test_inspect(void) { return nv.br_mode == BM_INSPECT; }
const char *hostui_test_fw_why(void) { return last_fw_why; }
void hostui_test_direct_video(bool done) { direct_video_done = done; }      // T164 boot720: the direct phase has put the picture up (and left)
void hostui_test_startup(void (*probe)(int step))
{
    hs_probe = probe;
    startup();
    hs_probe = 0;
}
// T146: the settle timer as the loop calls it (cd_run: the countdown runs), the key the picture would carry now, the key queue.
void hostui_test_snap_tick(uint32_t t, int nkeys, bool countdown)
{
    hc_cd_set(countdown, !countdown, 0);
    snap_tick(t, nkeys);
    rows_tick(t, nkeys);
}
// T229: the row cache as the bench sees it
int hostui_test_rows_loaded(void) { return g_mt.rows_loaded; }
int hostui_test_redraws(void) { return g_redraws; }
int hostui_test_est(int i) { return (int)hc_est(i); }
void hostui_test_lazy_reset(void) { lazy_reset(); }
uint32_t hostui_test_rows_key(void) { return rows_key(); }
void hostui_test_rows_save_now(void) { rows_save(); }
void hostui_test_rows_rect(int *y0, int *y1)           // the screen rows the menu's table covers (top of the first row to the bottom of the last)
{
    *y0 = lv_obj_get_y(mrow[0].box) + (int)lv_obj_get_y(lv_obj_get_parent(mrow[0].box));
    *y1 = *y0 + (int)(lv_obj_get_y(mrow[MROWS - 1].box) - lv_obj_get_y(mrow[0].box)) + (int)lv_obj_get_height(mrow[MROWS - 1].box);
}
uint32_t hostui_test_snap_key(void) { return snap_key_now(); }
int hostui_test_next_key(void) { return menu_getch(); }
bool hostui_test_snap_shown(void) { return snap_shown; }
void hostui_test_set_note(const char *s)
{
    if (s[0]) set_note(s, HCT_BAD); else hc_note_clear();
    need_full = true;
    apply_updates();
    lv_refr_now(NULL);
}
const char *hostui_test_start_err(void) { return st_err_last; }
const char *hostui_test_hint(void) { return lv_label_get_text(m_hint); }
bool hostui_test_hint_shown(void) { return !lv_obj_has_flag(m_hint, LV_OBJ_FLAG_HIDDEN); }
// The lazy reader to its end (the old synchronous start): every phase of every entry, then the migrations.
void hostui_test_lazy_drain(void)
{
    while (lazy_step(true)) {
    }
}
// What the loop does in one pass: keys handled (nkeys), the time now and the time of the last key.
bool hostui_test_lazy_pass(int nkeys, uint32_t now, uint32_t last_key)
{
    bool r = lazy_pass(nkeys, now, last_key);
    if (need_full) {                                 // what the loop does next: draw what the step changed
        apply_updates();
        lv_refr_now(NULL);
    }
    return r;
}
bool hostui_test_lazy_busy(void) { return lazy_busy(); }
// tests/hostapp/render: drives the real UI over a real directory tree (no hardware, no loop).
static void test_open(void)
{
    if (!have_ui) {
        trx_lv_open();
        have_ui = true;
        scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_flag(scr, LV_OBJ_FLAG_SCROLLABLE, false);
        layout_counts();
        build_ui();
        ui_built = true;
        if (!lazy_build) {
            build_rest_all();
        }
    }
}

// The state a fresh start has: boot.cfg text `cfgtext` (NULL = empty), probe, scan, menu view.
void hostui_test_init_lazy(const char *cfgtext);
void hostui_test_init(const char *cfgtext)
{
    hostui_test_init_lazy(cfgtext);
    hostui_test_lazy_drain();
    show_view(cur_view());
    lv_refr_now(NULL);
}
// The same, but no entry is read yet: the state a fresh start has at its first menu frame.
void hostui_test_init_lazy(const char *cfgtext)
{
    test_open();
    hc_init(&core_plat);
    hc_cfgov_io(hostfs_cm(), hostfs_fo());
    hc_nav_init(&nav_plat);
    hc_rows_init(&rows_plat);
    snprintf(txu_running_version, sizeof(txu_running_version), "%s", APP_BUNDLE_VERSION_STRING);   // T221 D19
    hc_ui_init(&ui_env_ref());
    hc_cfg_reset(cfgtext);
    lazy_reset();
    refresh_status();
    hc_cd_set(false, true, 0);
    arm_drop(HARM_END_GONE);
    hc_nav_test_init();
    hc_fo_replace_drop();
    start_screen_close();
    update_chips();
    show_view(cur_view());
    lv_refr_now(NULL);
}

void hostui_test_view(int v) { go((View)v); lv_refr_now(NULL); }
// The picture state of a fresh boot: the hooks of the bench, nothing stored, the Q9 list, the start sequence run once.
void hostui_test_video(int sys, int hdmi)
{
    (void)sys;
    (void)hdmi;
    video_current = 0;
    hc_picture_init(hostvideo_board_io());
    hc_picture_load_default(-1, -1);
    hc_picture_start(true, 0, 0);
}

// T164 Q7: where the message line of the page in view is on the canvas (absolute, inclusive) and what it says. false: the line is hidden, or its
// page is. The bench looks for the line's pixels in the frame, the label's text alone does not show that anything was drawn.
// T164 Q7: the picture question's box: its labels, the top of the key line, the bottom of what stands above it, the box's bottom, its centre's offset from the canvas centre
bool hostui_test_pq(char *title, char *l0, char *l1, char *key, int n, int *key_y, int *above_y2, int *box_y2, int *cx_off, int *cy_off)
{
    if (!pq_root) {
        return false;
    }
    for (lv_obj_t *p = pq_root; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
    }
    lv_area_t a, k, u;
    lv_obj_get_coords(pq_root, &a);
    lv_obj_get_coords(pq_key, &k);
    lv_obj_get_coords(pq_l1, &u);
    snprintf(title, n, "%s", lv_label_get_text(pq_title));
    snprintf(l0, n, "%s", lv_label_get_text(pq_l0));
    snprintf(l1, n, "%s", lv_label_get_text(pq_l1));
    snprintf(key, n, "%s", lv_label_get_text(pq_key));
    *key_y = k.y1;
    *above_y2 = u.y2;
    *box_y2 = a.y2;
    *cx_off = (a.x1 + a.x2) / 2 - SW / 2;
    *cy_off = (a.y1 + a.y2) / 2 - SH / 2;
    return true;
}
// T209 D15: the text tile of boot menu row i (a visible row): its label's text and both rectangles (label, tile), as drawn
bool hostui_test_tile(int i, char *text, int tn, int *lx1, int *lx2, int *tx1, int *tx2)
{
    const int r = i - nv.mfirst;
    if (nv.view != V_MENU || r < 0 || r >= MROWS || lv_obj_has_flag(mrow[r].icl, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    lv_area_t a, b;
    lv_obj_get_coords(mrow[r].icl, &a);
    lv_obj_get_coords(mrow[r].ic, &b);
    *lx1 = a.x1; *lx2 = a.x2; *tx1 = b.x1; *tx2 = b.x2;
    snprintf(text, tn, "%s", lv_label_get_text(mrow[r].icl));
    return true;
}
bool hostui_test_note_rect(int *x1, int *y1, int *x2, int *y2, char *text, int tn)
{
    lv_obj_t *o = nv.view == V_MENU ? m_note : nv.view == V_BROWSE ? b_note : nv.view == V_EDIT ? e_note : nv.view == V_SET ? (nv.pm_mode ? pm_note : s_note) : NULL;
    if (!o) {
        return false;
    }
    for (lv_obj_t *p = o; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
    }
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    *x1 = a.x1; *y1 = a.y1; *x2 = a.x2; *y2 = a.y2;
    snprintf(text, tn, "%s", lv_label_get_text(o));
    return true;
}

// How many footer texts are wider than the footer (one line, F_SMALL). 0: all fit.
// T164 D10a: the picture mode is up (as the V key or the stored choice puts it): the canvas, the port and the pages follow, as on the board.
int hostui_test_canvas_w(void) { return trx_canvas_w(); }
int hostui_test_canvas_h(void) { return trx_canvas_h(); }
void hostui_test_mode(int sys, int hdmi)
{
    hc_picture_mirror_test(sys, hdmi);
    menu_place();                       // as the V key does: when the canvas changes, the page in view is built again and shown by canvas_apply()
    need_full = true;
    refresh_current();
    lv_refr_now(NULL);
}
void hostui_test_panels_hide(void)
{
    build_rest_all();
    trx_page_hide(st_root);
    trx_page_hide(cf_root);
    trx_page_hide(rn_root);
    trx_page_hide(wl_root);
    trx_page_hide(oc_root);
    trx_page_hide(we_root);
    trx_page_hide(pq_root);
    lv_refr_now(NULL);
}
int hostui_test_foot_overflow(void)
{
    int bad = 0;
    for (unsigned v = 0; v < sizeof(foot_all) / sizeof(foot_all[0]); v++) {
        const char *f = hc_foot(foot_all[v]);
        lv_point_t sz;
        lv_text_get_size(&sz, f, F_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        bad += COMPACT ? sz.x > 2 * (SW - 20) - 40 : sz.x > SW - 28;     // the compact footer has two lines
    }
    return bad;
}

// 1 if a line of the "No cores yet" hint is wider than the content column.
int hostui_test_hint_overflow(void)
{
    lv_point_t sz;
    lv_text_get_size(&sz, HC_NO_CORES_HINT, F_TEXT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x > CW;
}

// One key as the loop handles it: on_key, one update, one render. Returns the invalidated pixels.
uint32_t hostui_test_key(int k)
{
    on_key(k);
    apply_updates();
    uint32_t px = inv_pixels();
    lv_refr_now(NULL);
    return px;
}

// T192 D21: the host's API in the bench, with the screen's own model: a remote intent queues keys, the loop takes them as the board's main loop does, and the board's
// own key enters through the same key path (hostapi_local_key) as in host_getch.
static bool tp_on = false;                    // true while the bench plays the UI task; a submit from outside waits for a pass
static void tp_nop(void) { }
static bool tp_ui(void) { return tp_on; }
static void tp_pass(void)
{
    bool was = tp_on;
    tp_on = true;
    hostapi_pass(false);
    tp_on = was;
}
static void tp_sleep(int) { tp_pass(); }
static uint32_t tp_now(void) { static uint32_t t = 1000; return t += 50; }
void hostui_test_ui_task(bool on) { tp_on = on; }           // the bench plays the UI task while a modal wait pumps the service (the question's time-out runs then)
void hostui_test_api_init(void)
{
    static HostApiPlatform pl = { tp_nop, tp_nop, tp_ui, tp_sleep, 0, tp_now, 0, 0, 0 };
    hostapi_init(hc_model(), &pl);
}
// One main-loop pass of the service: the keys that come out of the queue go to the screen. Returns how many.
static void tp_redraw(void)
{
    if (need_full || need_sel) {                  // what the main loop does after an intent was applied
        apply_updates();
        lv_refr_now(NULL);
    }
}
int hostui_test_api_pass(void)
{
    int n = 0, k;
    tp_on = true;
    hostapi_pass(false);
    tp_redraw();
    while ((k = hostapi_pop_key()) >= 0) {
        hostui_test_key(k);
        n++;
    }
    hostapi_pass(false);
    tp_redraw();
    tp_on = false;
    return n;
}
void hostui_test_board_key(int k)
{
    tp_on = true;
    hostapi_local_key(k);
    int q = hostapi_pop_key();
    if (q >= 0) {
        hostui_test_key(q);
    }
    hostapi_pass(false);
    tp_redraw();
    tp_on = false;
}

// T101 areas: the timers of the main loop as the bench drives them, each returns the invalidated pixels.
// A countdown second (the label and the bar), and a change of the header chips (a medium or the network came or went).
uint32_t hostui_test_cd_tick(void)
{
    if (!hc_cd_running()) {
        hc_cd_set(true, false, now_ms() + 5000);
    } else {
        uint32_t now = now_ms();
        hc_cd_set(true, false, now + (uint32_t)hc_cd_left_ms(now) - 1000);
    }
    cd_update();
    uint32_t px = inv_pixels();
    lv_refr_now(NULL);
    return px;
}

// T135 D12: whether the countdown label and its bar are shown (hidden flag of the object or any parent), and the label's text.
void hostui_test_cd_vis(int *label, int *bar, char *text, int tn)
{
    int v[2] = { 1, 1 };
    lv_obj_t *o[2] = { cd_label_obj, cd_bar_obj };
    for (int i = 0; i < 2; i++) {
        for (lv_obj_t *p = o[i]; p; p = lv_obj_get_parent(p)) {
            if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) {
                v[i] = 0;
            }
        }
    }
    *label = v[0];
    *bar = v[1];
    snprintf(text, tn, "%s", lv_label_get_text(cd_label_obj));
}

// T184: the status refresh as the board runs it (interfaces from the stub), the header chip's state and address.
bool hostui_test_status_refresh(char *ip, int n)
{
    refresh_status();
    update_chips();
    snprintf(ip, n, "%s", c_ip);
    return c_net;
}

const char *hostui_test_chip_text(void) { return hc_chip_net_text(); }

uint32_t hostui_test_chips(bool net)
{
    c_net = c_enet = c_lan = net;
    snprintf(c_eip, sizeof(c_eip), net ? "192.168.1.77" : "");
    snprintf(c_ip, sizeof(c_ip), net ? "192.168.1.77" : "");
    update_chips();
    if (nv.view == V_NET) {
        update_net();
    }
    uint32_t px = inv_pixels();
    lv_refr_now(NULL);
    return px;
}

// One command from the network as the loop handles it (T106 S2/S3/S6): the web task would only have
// queued it; this is the UI task picking it up.
void hostui_test_web(int kind, int a, int b, const char *path)
{
    WebCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = (WebCmdKind)kind;
    c.a = a;
    c.b = b;
    (void)path;
    web_queue_for_test(&c);
    hostapi_bind_model(hc_model());                          // the page's commands go through the service's dispatcher (the service itself need not run)
    web_pump();
    {
        int k;                                               // the intents the service queued are taken as the loop takes keys
        while ((k = hostapi_pop_key()) >= 0) {
            on_key(k);
        }
    }
    apply_updates();
    lv_refr_now(NULL);
}

void hostui_test_countdown(void)
{
    hc_cd_set(true, false, now_ms() + 5000);
    refresh_current();
    lv_refr_now(NULL);
}

void hostui_test_start_panel(void)
{
    st_step = STEP_ICAP; st_pct = 42;
    start_screen_open("Starting VC-20 (PAL)");
    start_screen_update(true);
    lv_refr_now(NULL);
}

// What is on the screen, as text, read from the LVGL objects: one header line, then one line per visible row.
int hostui_test_dump(char *out, int max)
{
    Row *rows = nv.view == V_EDIT ? erow : mrow;
    int first = nv.view == V_EDIT ? nv.efirst : nv.mfirst;
    int total = nv.view == V_EDIT ? cfg.n : cfg.n;
    int n = snprintf(out, max, "view=%d sel=%d first=%d total=%d entries=%d def=%d pos='%s' sub='%s' note='%s' foot='%s' rows=%d detail='%s'\n",
                     (int)nv.view, nv.sel, first, total, cfg.n, cfg.def,
                     nv.view == V_EDIT ? lv_label_get_text(e_pos) : lv_label_get_text(m_pos), lv_label_get_text(m_sub), nv.note,
                     lv_label_get_text(foot_label), nv.view == V_EDIT ? erows_n : mrows_n,
                     lv_label_get_text(nv.view == V_EDIT ? e_note : m_note));
    int bottom = 0;
    for (int r = 0; r < (nv.view == V_EDIT ? EROWS : MROWS); r++) {
        if (lv_obj_has_flag(rows[r].box, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        int b = lv_obj_get_y(rows[r].box) + lv_obj_get_height(rows[r].box);
        if (b > bottom) bottom = b;
        char ic[24] = "tag";                            // the menu's rows carry the logo (T209 D6), the editor's rows none
        char subb[200];
        const char *sub = lv_label_get_text(rows[r].b);   // the menu: the version; the editor: the path, then its right group's version (D16)
        if (nv.view == V_EDIT && lv_label_get_text(rows[r].vl)[0]) {
            snprintf(subb, sizeof(subb), "%s  %s", sub, lv_label_get_text(rows[r].vl));
            sub = subb;
        }
        if (nv.view != V_EDIT && !lv_obj_has_flag(rows[r].img, LV_OBJ_FLAG_HIDDEN)) {
            snprintf(ic, sizeof(ic), "img %04x", rows[r].dsc.data ? ((const uint16_t *)rows[r].dsc.data)[0] : 0);
        }
        HcEntryRow er;
        const char *word = "";                                 // the state word; both lists draw a pictogram instead (T209 D10/D12)
        if (hc_row_entry(first + r, true, &er)) {
            word = er.badge;
        }
        n += snprintf(out + n, max - n, "row %d|%s|%s|%s|%s|%s|%s\n", first + r, lv_label_get_text(rows[r].a), sub, word,
                      lv_obj_has_flag(rows[r].defb, LV_OBJ_FLAG_HIDDEN) ? "" : "auto", first + r == nv.sel ? "SEL" : "", ic);
        {                                                                  // T209 D10/D11/D12: the pictogram as drawn (group, tint, size) and the marker's text
            n += snprintf(out + n, max - n, "pic %d|%s|%06x|%dx%d|%s\n", first + r, rows[r].pcs == HCP_OK ? "ok" : rows[r].pcs == HCP_CHECK ? "check" : "bad",
                          (unsigned)lv_color_to_u32(lv_obj_get_style_image_recolor(rows[r].stb, LV_PART_MAIN)) & 0xffffff,
                          (int)lv_obj_get_width(rows[r].stb), (int)lv_obj_get_height(rows[r].stb), lv_label_get_text(rows[r].defl));
        }
        if (!lv_obj_has_flag(rows[r].cfb, LV_OBJ_FLAG_HIDDEN)) {           // T198 D15: the "N configs" badge as drawn, a line of its own (the row line above is matched by prefix)
            n += snprintf(out + n, max - n, "cfgbadge %d|%s\n", first + r, lv_label_get_text(rows[r].cfl));
        }
    }
    int note_y = lv_obj_get_y(nv.view == V_EDIT ? e_note : m_note);
    n += snprintf(out + n, max - n, "geometry rows_bottom=%d note_top=%d countdown_top=%d\n", bottom, note_y,
                  lv_obj_has_flag(m_cdbox, LV_OBJ_FLAG_HIDDEN) ? -1 : lv_obj_get_y(m_cdbox));
    return n;
}

// T115: the text the confirm panel showed last, the panel itself for a screenshot, and the manifest panel of the browser
const char *hostui_test_confirm(void) { return last_confirm; }
int hostui_test_confirms(void) { return n_confirms; }

void hostui_test_confirm_panel(bool show)
{
    EN(A_START);
    if (show) {
        snprintf(last_confirm, sizeof(last_confirm), "%s replaces the host. The way back is to switch the power off and on.", "u64_mk2_artix 2026/09/10 11:00:57");
        TrxPage page(cf_root);
        set_txt(cf_title, "Start full firmware?");
        set_txt(cf_text, last_confirm);
    } else {
        trx_page_hide(cf_root);
    }
    lv_refr_now(NULL);
}

void hostui_test_start_panel_full(void)
{
    st_step = STEP_DECOUPLE; st_pct = 37;
    start_screen_open("Starting u64_mk2_artix 2026/09/10 11:00:57", true);
    start_screen_update(true);
    lv_refr_now(NULL);
}

const char *hostui_test_browse_sub(void) { return lv_label_get_text(b_sub); }

int hostui_test_browse_right(char *out, int max)
{
    int n = snprintf(out, max, "title=%s|", lv_label_get_text(r_title));
    for (int i = 0; i < 7; i++) {
        n += snprintf(out + n, max - n, "%s=%s|", lv_label_get_text(r_key[i]), lv_label_get_text(r_val[i]));
    }
    n += snprintf(out + n, max - n, "msg=%s|err=%s|shown=%d", lv_label_get_text(r_msg), lv_label_get_text(r_err), !lv_obj_has_flag(r_msg, LV_OBJ_FLAG_HIDDEN));
    return n;
}

// T139: the settings page as the person reads it: "sel=N|name=value|..." for the visible rows.
// T148: the Cores and ROMs page as the person reads it: "mode=N top=N|line|line|..." with every line of the list, and the visible head, note and footer.
int hostui_test_cm(char *out, int max)
{
    int n = snprintf(out, max, "mode=%d top=%d nlines=%d|", nv.cm_mode, nv.cm_top, hc_cm_lines());
    for (int i = 0; i < hc_cm_lines() && n < max; i++) {
        n += snprintf(out + n, max - n, "%s|", hc_cm_line(i, 0));
    }
    return n;
}
const char *hostui_test_cm_head(void) { return lv_label_get_text(cm_head); }
const char *hostui_test_cm_note(void) { return lv_label_get_text(cm_note); }
const char *hostui_test_foot(void) { return lv_label_get_text(foot_label); }
// T198: the overlay's geometry in panel coordinates (render bench): g[0..1] panel y/h, g[2..3] the selected row's top/bottom (-1 none), g[4..5] the message's
// top/bottom (-1 empty), g[6] last drawn row's bottom, g[7] the dimmer's height. Read from the objects, not from the layout variables.
void hostui_test_oc_geom(int *g)
{
    g[0] = lv_obj_get_y(oc_panel);
    g[1] = lv_obj_get_style_height(oc_panel, LV_PART_MAIN);
    g[2] = g[3] = g[6] = -1;
    for (int i = 0; i < OC_MAX; i++) {
        if (!lv_obj_has_flag(oc_a[i], LV_OBJ_FLAG_HIDDEN)) {
            g[6] = lv_obj_get_y(oc_a[i]) - oc_ay + oc_rp - 2;
        }
        if (!lv_obj_has_flag(oc_mk[i][0], LV_OBJ_FLAG_HIDDEN)) {
            g[2] = lv_obj_get_y(oc_mk[i][0]);
            g[3] = lv_obj_get_y(oc_mk[i][1]) + lv_obj_get_style_height(oc_mk[i][1], LV_PART_MAIN);
        }
    }
    const bool m = lv_label_get_text(oc_msg)[0] != 0;
    g[4] = m ? lv_obj_get_y(oc_msg) : -1;
    g[5] = m ? g[4] + lv_font_get_line_height(F_SMALL) : -1;
    g[7] = lv_obj_get_style_height(oc_root, LV_PART_MAIN);
}
int hostui_test_settings(char *out, int max)
{
    int n = snprintf(out, max, "sel=%d|", nv.ssel);
    for (int i = 0; i < SET_SLOTS; i++) {
        if (!lv_obj_has_flag(s_box[i], LV_OBJ_FLAG_HIDDEN)) {
            n += snprintf(out + n, max - n, "%s=%s|", lv_label_get_text(s_name[i]), lv_label_get_text(s_val[i]));
        }
    }
    return n;
}
bool hostui_test_pick(void) { return nv.br_mode == BM_PICK; }
int hostui_test_cd_state(void) { return (hc_cd_running() ? 1 : 0) | (hc_cd_dead() ? 2 : 0); }      // bit 0 the countdown runs, bit 1 it is dead
// The rename field for a screenshot: `name` with the cursor at `cur` (the bench cannot look into the modal loop); show=false closes it.
void hostui_test_rename_panel(bool show, const char *name, int cur, const char *msg)
{
    EN(A_RENAME);
    if (!show) {
        trx_page_hide(rn_root);
        lv_refr_now(NULL);
        return;
    }
    LineEdit e;
    le_init(&e, name, BOOT_NAME_MAX, "|");
    e.cur = cur < e.len ? cur : e.len;
    {
        TrxPage page(rn_root);
        set_txt(rn_sub, "/SD/cores/amiga1200.cor");
        HcRenamePage p;
        memset(&p, 0, sizeof(p));
        memcpy(p.typed, e.buf, sizeof(p.typed));
        p.cur = e.cur;
        snprintf(p.msg, sizeof(p.msg), "%s", msg);
        rename_set(p);
    }
    lv_refr_now(NULL);
}
// T184: the WiFi status and settings as the stubs give them (the 3 s refresh does this on the board), the selected setting row, the page's note.
void hostui_test_wifi_sync(void)
{
    wifi_net_status(&c_wifi);
    c_wcfg_ok = wifi_net_cfg_get(&c_wcfg);
    update_net();
    update_settings();
    lv_refr_now(NULL);
}
int hostui_test_net_sel(void) { return nv.nsel; }
const char *hostui_test_net_note(void) { return nv.nnote; }
const char *hostui_test_wl_sub(void) { return hc_wifi_last_sub(); }
bool hostui_test_wf_op_run(void) { return hc_wifi_op_run(); }
const char *hostui_test_net_text(int which)
{
    lv_obj_t *l = which == 97 ? n_rest : which == 98 ? n_ftp : which == 99 ? n_note : which == 0 ? n_wst : which == 1 ? n_wnet : which == 2 ? n_wip : which == 3 ? n_wmac : which == 4 ? n_eth : n_sval[which - 5];
    return lv_label_get_text(l);
}
const char *hostui_test_net_label(int i) { return i < NS_N ? hc_wifi_net_set_name(i) : 0; }
// The WiFi panels for a screenshot, the modal loops being out of reach of the bench: 0 closes both, 1 the access point list, 2 the password field
// (masked), 3 the field for a name, 4 the authentication list, 5 a field for an address with its message.
void hostui_test_wifi_panel(int what, int sel_)
{
    EN(A_WIFI);
    if (what == 0) {
        trx_page_hide(wl_root);
        trx_page_hide(oc_root);
        trx_page_hide(we_root);
        lv_refr_now(NULL);
        return;
    }
    static HcWifiPick pk;
    static HcWifiEntry en;
    LineEdit e;
    if (what == 1) {
        static WifiNetAp aps[HCW_PICK_MAX];
        int n = wifi_net_aps(aps, HCW_PICK_MAX);
        int first = 0;
        listwin_fix(sel_, n, wl_rows, &first);
        wifi_net_status(&c_wifi);
        hc_wifi_aps_pick(&pk, aps, n, sel_, first, c_wifi, "", HCT_WARN);
        wifi_pick_show(pk, true);
    } else if (what == 4) {
        int first = 0;
        listwin_fix(sel_, WIFI_AUTH_N, wl_rows, &first);
        trx_page_hide(we_root);
        hc_wifi_auth_pick(&pk, "Authentication", "Cafe Lumiere", sel_, first);
        TrxPage page(wl_root);
        wl_fill(pk);
    } else {
        trx_page_hide(wl_root);                                       // as the entry: it stands alone
        TrxPage page(we_root);
        int vis = wifi_entry_vis();
        if (what == 2) {
            le_init(&e, "correct horse battery", 62, "");
            set_txt(we_title, "Password");
            set_txt(we_sub, "Home WLAN    WPA2 PSK");
            set_txt(we_keys, "RETURN connects    ESC cancels    LEFT/RIGHT HOME END move    INS blank");
            hc_wifi_entry_make(&en, "Password", "Home WLAN    WPA2 PSK", "RETURN connects    ESC cancels    LEFT/RIGHT HOME END move    INS blank", e, true, "", vis);
        } else if (what == 3) {
            le_init(&e, "Cafe Lumiere", 32, "");
            set_txt(we_title, "Enter access point");
            set_txt(we_sub, "The name of the network (SSID)");
            set_txt(we_keys, "RETURN next    ESC cancels    LEFT/RIGHT HOME END move    INS blank");
            hc_wifi_entry_make(&en, "Enter access point", "The name of the network (SSID)", "RETURN next    ESC cancels    LEFT/RIGHT HOME END move    INS blank", e, false, "", vis);
        } else {
            le_init(&e, "192.168.2.", 15, "");
            set_txt(we_title, hc_wifi_net_set_name(NS_IP));
            set_txt(we_sub, "Static IP, used when DHCP is disabled");
            set_txt(we_keys, "RETURN saves    ESC cancels    LEFT/RIGHT HOME END move    INS blank");
            hc_wifi_entry_make(&en, hc_wifi_net_set_name(NS_IP), "Static IP, used when DHCP is disabled", "RETURN saves    ESC cancels    LEFT/RIGHT HOME END move    INS blank", e, false, hc_wifi_check_ipv4(e.buf), vis);
        }
        we_draw(en);
    }
    lv_refr_now(NULL);
}
int hostui_test_sel(void) { return nv.sel; }
int hostui_test_bsel(void) { return nv.bsel; }
int hostui_test_bn(void) { return nv.bn; }
int hostui_test_brows(void) { return BROWS; }
bool hostui_test_startable(int i) { return startable(i); }
int hostui_test_cfg_n(void) { return cfg.n; }

// T111: what the format page shows (the bench reads the words), how many formats were started, the countdown, the browser's footer.
// T167 5.9: three states of the format panel for the bench to measure what LVGL has to redraw: 0 = page A, 1 = page B (new title, colours, bar), 2 = a tick of B
// (texts and the bar's value), 3 = closed.
int hostui_test_fm_demo(int which, int *)
{
    FmPage pg;
    memset(&pg, 0, sizeof(pg));
    pg.bar = -1;
    pg.border = C_BAD;
    pg.mcol = C_WARN;
    if (which == 3) {
        bool k = fm_keep;
        fm_keep = false;
        fm_close();
        fm_keep = k;
        return 0;
    }
    if (which == 0) {
        snprintf(pg.title, sizeof(pg.title), "Demo page A");
        for (int i = 0; i < FM_LINES; i++) {
            snprintf(pg.line[i], sizeof(pg.line[i]), "A line %d of the first page", i);
            pg.lcol[i] = C_INK;
        }
        snprintf(pg.hint, sizeof(pg.hint), "any key");
    } else {
        snprintf(pg.title, sizeof(pg.title), "Demo page B");           // the frame colour stays: a changed one invalidates the whole panel by itself
        for (int i = 0; i < FM_LINES; i++) {
            snprintf(pg.line[i], sizeof(pg.line[i]), "B line %d of the second page %d", i, which == 2 ? 50 : 40);
            pg.lcol[i] = i & 1 ? C_OK : C_INK;
        }
        pg.bar = which == 2 ? 50 : 40;
        snprintf(pg.msg, sizeof(pg.msg), "%d percent", pg.bar);
        snprintf(pg.hint, sizeof(pg.hint), "ESC cancels");
    }
    fm_draw(pg);
    return 0;
}

int hostui_test_fm_page(char *out, int max)
{
    int n = snprintf(out, max, "title=%s|border=%06x|", fm_last.title, (unsigned)fm_last.border);
    for (int i = 0; i < FM_LINES; i++) {
        n += snprintf(out + n, max - n, "l%d=%s|", i, fm_last.line[i]);
    }
    n += snprintf(out + n, max - n, "input=%d:%s|msg=%s|pct=%s|bar=%d|hint=%s|open=%d|foot=%s", (int)fm_last.input, fm_last.typed, fm_last.msg, fm_last.pct, fm_last.bar,
                  fm_last.hint, (int)(fm_root && !lv_obj_has_flag(fm_root, LV_OBJ_FLAG_HIDDEN)), lv_label_get_text(foot_label));
    return n;
}
// D11: where the scrim and the page are on the screen (x, y, w, h each), after a refresh
void hostui_test_fm_geom(int *scrim, int *box_)
{
    lv_area_t a;
    lv_obj_get_coords(fm_root, &a);
    scrim[0] = a.x1; scrim[1] = a.y1; scrim[2] = a.x2 - a.x1 + 1; scrim[3] = a.y2 - a.y1 + 1;
    lv_obj_get_coords(fm_box, &a);
    box_[0] = a.x1; box_[1] = a.y1; box_[2] = a.x2 - a.x1 + 1; box_[3] = a.y2 - a.y1 + 1;
}
// T152 D10: the key line inside the page: its text, its top, the bottom of what stands above it, the page's bottom (screen y; false = no key line shown)
bool hostui_test_fm_key(char *text, int max, int *key_y, int *above_y2, int *box_y2)
{
    if (!fm_key || lv_obj_has_flag(fm_key, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    lv_area_t a, k;
    lv_obj_get_coords(fm_key, &k);
    lv_obj_get_coords(fm_box, &a);
    snprintf(text, max, "%s", lv_label_get_text(fm_key));
    *key_y = k.y1;
    *box_y2 = a.y2;
    int up = a.y1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(fm_box); i++) {
        lv_obj_t *c = lv_obj_get_child(fm_box, i);
        lv_area_t ca;
        if (c == fm_key || lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) || (lv_obj_check_type(c, &lv_label_class) && !lv_label_get_text(c)[0])) {
            continue;
        }
        lv_obj_get_coords(c, &ca);
        if (ca.y2 > up) {
            up = ca.y2;
        }
    }
    *above_y2 = up;
    return true;
}
int hostui_test_fmt_runs(void) { return hc_format_runs(); }
int hostui_test_cv_runs(void) { return hc_convert_runs(); }
const char *hostui_test_cv_why(void) { return hc_convert_why(); }
const char *hostui_test_fmt_why(void) { return hc_format_why(); }
bool hostui_test_cd_running(void) { return hc_cd_running(); }
bool hostui_test_fmt_active(void) { return fmt_active; }
void hostui_test_fm_keep(bool on) { fm_keep = on; if (!on) { fm_close(); } }
const char *hostui_test_nav_name(int i) { return nav_title(i); }
const char *hostui_test_set_name(int k) { return k >= 0 && k < 10 ? hc_set_name(k) : 0; }
// T187 D5: the Picture page as the bench reads it: open (1/0), the cursor, the name under it, the rows and the info line
int hostui_test_pm(char *name, int ncap, int *sel, int *rows, char *info, int icap)
{
    const PicPage &pp = hc_ui_pm();
    if (name && ncap > 0) {
        snprintf(name, ncap, "%s", pp.sel < pp.pm.n ? pp.pm.row[pp.sel].name : "");
    }
    if (sel) {
        *sel = pp.sel;
    }
    if (rows) {
        *rows = pp.pm.n;
    }
    if (info && icap > 0) {
        picmodes_info(&pp.pm, info, icap);
    }
    return nv.pm_mode;
}
// The page's texts and places for the render bench: row i (name, refresh, tag), and the rectangles of row i (which >= 0), the page (-1), the info line (-2), the note (-3), the key bar (-4).
bool hostui_test_pm_row(int i, char *name, int nn, char *hz, int hn, char *tag, int tn)
{
    const PicPage &pp = hc_ui_pm();
    if (i < 0 || i >= pp.pm.n) {
        return false;
    }
    snprintf(name, nn, "%s", pp.pm.row[i].name);
    picmodes_refresh_text(&pp.pm, i, hz, hn);
    picmodes_tag_text(&pp.pm, i, tag, tn);
    return true;
}
bool hostui_test_pm_geo(int which, int *x1, int *y1, int *x2, int *y2)
{
    lv_obj_t *o = which == -1 ? pm_root : which == -2 ? pm_info : which == -3 ? pm_note : which == -4 ? foot_label : (which >= 0 && which < PM_ROWS) ? pm_box[which] : NULL;
    if (!o) {
        return false;
    }
    for (lv_obj_t *q = o; q; q = lv_obj_get_parent(q)) {
        if (lv_obj_has_flag(q, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
    }
    lv_obj_update_layout(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    *x1 = a.x1; *y1 = a.y1; *x2 = a.x2; *y2 = a.y2;
    return true;
}
void hostui_test_pump(void) { web_pump(); picture_tick(); apply_updates(); lv_refr_now(NULL); }
// The browser's rows as the person reads them: "name|size" per visible row.
int hostui_test_browse_rows(char *out, int max)
{
    int n = 0;
    for (int i = 0; i < BROWS; i++) {
        if (lv_obj_has_flag(b_name[i], LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        n += snprintf(out + n, max - n, "%s%s|%s\n", lv_label_get_text(b_mark[i]), lv_label_get_text(b_name[i]), lv_label_get_text(b_size[i]));
    }
    return n;
}
void hostui_test_browse_media(void)
{
    hc_nav_test_view(HCV_BROWSE, nv.sel, 0);
    hc_browse_set_path("/");
    browse_load();
    show_view(V_BROWSE);
    lv_refr_now(NULL);
}
const char *hostui_test_cfg_path(int i) { return cfg.e[i].path; }
int hostui_test_cfg_def(void) { return cfg.def; }

// Screenshots: the state of the mock (three entries), one view.
void hostui_test(int v, int sel_, int browse_sel, bool start)
{
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|TRX64 Ultimate (C64)||0\n"
                     "core=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\ncore=/Usb0/old/trx64-0.9.cor|TRX64 Ultimate (old host)||0\n");
    hc_cd_set(true, false, now_ms() + 5000);
    hc_nav_test_view(v, sel_, browse_sel);
    hc_browse_set_path(v == 1 ? "/Usb0/cores" : "/");
    if (v == 1) {
        browse_load();
        hc_nav_set_bsel(browse_sel);
    }
    show_view(cur_view());
    if (start) {
        st_step = STEP_ICAP; st_pct = 42;
        start_screen_open("Starting VC-20 (PAL)");
        start_screen_update(true);
    } else {
        start_screen_close();
    }
    lv_refr_now(NULL);
}
// Is the folder title in the font of the rest of the view (the photo of 2026-10-22 had it in a 16 px pixel font)?
bool hostui_test_path_font_is_ui(void)
{
    return lv_obj_get_style_text_font(b_path, LV_PART_MAIN) == lv_obj_get_style_text_font(r_title, LV_PART_MAIN) &&
           lv_obj_get_style_text_font(b_path, LV_PART_MAIN) == F_TEXT;
}
// Lines the hint under the manifest takes (the photo of 2026-10-22 had two long keys wrap into three).
int hostui_test_msg_lines(void)
{
    lv_obj_update_layout(r_msg);
    return (int)lv_obj_get_height(r_msg) / (int)lv_font_get_line_height(lv_obj_get_style_text_font(r_msg, LV_PART_MAIN));
}
// The Add core view on `path` (selection 0).
void hostui_test_browse_at(const char *path)
{
    hostui_test(1, 0, 0, false);
    hc_browse_set_path(path);
    browse_load();
    hc_nav_set_bsel(0);
    show_view(V_BROWSE);
    lv_refr_now(NULL);
}
// The Add core browser at `path` over the menu as it is (hostui_test_browse_at starts from the bench's own three entries).
void hostui_test_browse_keep(const char *path)
{
    hc_nav_test_view(HCV_BROWSE, nv.sel, 0);
    hc_browse_set_path(path);
    browse_load();
    show_view(V_BROWSE);
    lv_refr_now(NULL);
}
int hostui_test_last_areas(void) { return last_inv_n; }
int hostui_test_inv_slots(void) { return (int)(sizeof(((lv_display_t *)0)->inv_areas) / sizeof(lv_area_t)); }
// A selection move or a view switch, rendered at once: returns the invalidated pixels.
uint32_t hostui_test_move(bool full)
{
    if (full) {
        go(nv.view == V_MENU ? V_NET : V_MENU);
    } else {
        on_key(nv.sel + 1 < cfg.n ? KEY_DOWN : KEY_UP);
        apply_updates();
    }
    uint32_t px = inv_pixels();
    lv_refr_now(NULL);
    return px;
}
#endif
