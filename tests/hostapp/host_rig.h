// The host application's menu on a native machine, shared by the render bench (tests/hostapp/render) and the menu simulator (tools/menusim,
// T236 D12): the firmware stubbed (WiFi, video, the file layer over the tree mktree.py makes, the media, the keyboard), the pixel layer as
// LVGL's native display, the PNG writers and the bench's CHECK. Included once per program.
#ifndef TESTS_HOSTAPP_HOST_RIG_H
#define TESTS_HOSTAPP_HOST_RIG_H

#include "host_core.h"
#include <string>
#include "lvgl.h"
#include "hostfs.h"
#include "discover.h"
#include "legacy.h"
#include "bootcfg.h"
#include "videomode.h"
#include "hostvideo.h"
#include "webui.h"
#include "hostdomain.h"
#include "hostdecl.h"
#include "host_core_rows.h"
#include "hostreport_live.h"
#include "mediaglue.h"
#include "mediaui.h"
#include "fmt.h"
#include "trx_lv_port.h"
#include "menusnap.h"
#include "menurows.h"
#include "view_bounds.h"
#include "trxcanvas.h"
#include "trxview.h"
#include <vector>
#include "keyboard_usb.h"
#include "keyboard_c64.h"
#include "u64.h"
#include "flash.h"
#include "overlay_probe.h"
#include "flashfw.h"
#include "esp_loader_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <string>
#include <sys/statvfs.h>
#include <errno.h>
#include "wifinet.h"
#include "network_interface.h"
#include "ftpstate.h"

// T184: the WiFi driver (wifinet.cc, Gideon's wifi.cc) runs on the board only; the bench has a scripted module. g_ws is what the status reads, g_aps the
// scan, g_wc the "WiFi settings" store; the counters tell which calls the page made.
const char *const wifi_auth_names[WIFI_AUTH_N] = { "Open", "WEP", "WPA PSK", "WPA2 PSK", "WPA/WPA2 PSK", "WPA2 Enterprise", "WPA3 PSK", "WPA2/WPA3 PSK", "WAPI PSK" };
static WifiNetStatus g_ws;
static WifiNetAp g_aps[8];
static int g_naps;
static WifiNetCfg g_wc;
static bool g_wc_present = true;
static int g_w_radio_n, g_w_radio_last = -1;
static int g_w_rescan, g_w_conn, g_w_last, g_w_disc, g_w_forget, g_w_cfgset;
static bool g_w_busy_refuse;
void wifi_net_status(WifiNetStatus *s) { *s = g_ws; }
void wifi_net_rescan(void) { g_w_rescan++; }
bool wifi_net_radio(bool on) { g_w_radio_n++; g_w_radio_last = on; return g_ws.state != WN_NO_MODULE; }
int wifi_net_aps(WifiNetAp *out, int max) { int n = g_naps < max ? g_naps : max; for (int i = 0; i < n; i++) out[i] = g_aps[i]; return n; }
static bool job_start(int &n) { n++; if (g_w_busy_refuse) return false; g_ws.busy = true; g_ws.last = 1; return true; }       // the driver sets busy before its task runs
bool wifi_net_connect(const char *, const char *, uint8_t, bool) { return job_start(g_w_conn); }
bool wifi_net_autoconnect(void) { return job_start(g_w_last); }
bool wifi_net_disconnect(void) { return job_start(g_w_disc); }
bool wifi_net_forget(void) { return job_start(g_w_forget); }
bool wifi_net_cfg_get(WifiNetCfg *c) { *c = g_wc; if (!g_wc_present) c->dhcp = true; return g_wc_present; }
bool wifi_net_cfg_set(const WifiNetCfg *c) { g_w_cfgset++; if (g_wc_present) g_wc = *c; return g_wc_present; }

UsbKb system_usb_keyboard;
volatile uint8_t U64II_KEYB_ROW, U64II_KEYB_COL, U64II_KEYB_JOY;
static uint16_t fbs[2][960 * 540];
static int cvw = 960, cvh = 540;                               // T164 D10a: the canvas of the display (the port's trx_lv_set_canvas)
static uint16_t *fb = fbs[0];        // the buffer the layer would show (flush of the last area), as in trx_lv_port.cc
// T236: the bench's clock steps 50 ms per call; the simulator's window sets the wall clock. rig_on_frame is called with every finished frame (fb
// holds it, cvw x cvh); the bench leaves it empty.
static uint32_t rig_tick_steps(void) { static uint32_t t; return t += 50; }
static uint32_t (*rig_tick)(void) = rig_tick_steps;
static void (*rig_on_frame)(void) = nullptr;
extern "C" uint32_t now_zero_tick(void) { return rig_tick(); }
uint32_t g_bench_ticks;               // see stub/task.h
static uint32_t g_key_gap_ms;         // the time that passes before each key the bench hands over

// The board half of webui.cc (WEBUI_NO_BOARD): the bench has no socket. The page and the routes are
// the same code; only the task is missing.
int g_web_starts;
void web_start(void) { g_web_starts++; }

// The board half of videomode.cc (VIDEOMODE_NO_BOARD): the bench has no HDMI and no I2C. It records
// what the application asked for, so a test can read it back.
int g_vid_sys = -1, g_vid_hdmi = -1;
bool video_read_edid(uint8_t *, int, int *len) { *len = 0; return false; }
void video_apply(int sys, int hdmi) { g_vid_sys = sys; g_vid_hdmi = hdmi; }
void video_start(int s, int h, int *us, int *uh)
{
    g_vid_sys = (s >= 0) ? s : 0;
    g_vid_hdmi = (h >= 0) ? h : 0;
    if (us) { *us = g_vid_sys; }
    if (uh) { *uh = g_vid_hdmi; }
}

extern void hostui_test_video(int sys, int hdmi);
extern void hostui_test_direct_video(bool done);
// T164 WP17: the hooks of hostvideo.h. No screen answers (the Q9 list: 576p/480p, 720p, 1080p), no mode-request register (the old path), and the
// stored choice lives in these two ints, counted.
int g_hv_stores, g_saved_hdmi = -1, g_saved_sys = 0;
static uint32_t g_hv_ms;
void hv_saved_load(int *h, int *s) { *h = g_saved_hdmi; *s = g_saved_sys; }
void hv_saved_store(int h, int s) { g_saved_hdmi = h; g_saved_sys = s; g_hv_stores++; }
static bool fk_hpd(void) { return true; }
static uint8_t g_fk_edid[256];
static int g_fk_edid_len;                                     // 0: nobody answers (the Q9 list)
static bool fk_edid(uint8_t *b, int max, int *len)
{
    if (!g_fk_edid_len) { *len = 0; return false; }
    memcpy(b, g_fk_edid, g_fk_edid_len < max ? g_fk_edid_len : max);
    *len = g_fk_edid_len;
    return true;
}
// T164 Q7: with g_gw_model the host answers the mode request (the board's 0x4F00..09), so a change waits for its key; without it the old path stores at once.
static bool g_gw_model;
static uint8_t g_gw_wr, g_gw_pr, g_gw_mode;
static bool fk_gwp(void) { return g_gw_model; }
static void fk_gww(int row) { g_gw_wr++; g_gw_pr = g_gw_wr; g_gw_mode = (uint8_t)row; }
static bool fk_gws(HvGwStatus *s)
{
    if (!g_gw_model) { return false; }
    s->written = g_gw_wr; s->processed = g_gw_pr; s->mode = g_gw_mode;
    s->flags = HV_GW_RUNNING | HV_GW_PIXLOCK | HV_GW_GTREADY;
    return true;
}
static void fk_legacy(int sys, int hdmi) { video_apply(sys, hdmi); }
static void fk_en(void) {}
static uint32_t fk_ms(void) { return g_hv_ms; }
static void fk_pause(int ms) { g_hv_ms += ms; }
static const HostVideoIo fake_io = { fk_hpd, fk_edid, fk_gwp, fk_gww, fk_gws, fk_legacy, fk_en, fk_ms, fk_pause };
const HostVideoIo *hostvideo_board_io(void) { return &fake_io; }
// T164 WP17: where the menu was put in the output (trx_lv_set_place), as the bench sees it
static int g_pl_x = -1, g_pl_y = -1, g_pl_w = -1, g_pl_h = -1, g_pl_calls;
void trx_lv_set_place(int x, int y, int w, int h) { g_pl_x = x; g_pl_y = y; g_pl_w = w; g_pl_h = h; g_pl_calls++; }
static void fresh_video(int sys, int hdmi) { g_saved_hdmi = -1; g_saved_sys = 0; hostui_test_video(sys, hdmi); }

bool trx_lv_open(void)
{
    lv_init();
    lv_tick_set_cb(now_zero_tick);
    lv_display_t *d = lv_display_create(cvw, cvh);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fbs[0], fbs[1], sizeof(fbs[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, [](lv_display_t *dd, const lv_area_t *, uint8_t *px) {
        if (lv_display_flush_is_last(dd)) fb = (uint16_t *)px;
        lv_display_flush_ready(dd);
        if (lv_display_flush_is_last(dd) && rig_on_frame) { rig_on_frame(); }
    });
    ovp::install();
    ovp::g_single = true;                 // T167: the product shows the buffer LVGL draws into; the probe runs switch this display to one buffer
    return true;
}
int trx_lv_handler(void) { return (int)lv_timer_handler(); }
void trx_lv_set_canvas(int w, int h)
{
    if (w == cvw && h == cvh) { return; }
    cvw = w; cvh = h;
    lv_display_t *d = lv_display_get_default();
    if (d) {
        lv_display_set_resolution(d, w, h);
        lv_display_set_buffers(d, fbs[0], fbs[1], sizeof(fbs[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    }
}
// T146: the port's two buffers and the first-picture call; the bench counts them
static int g_pic_shown;
void trx_lv_buffers(uint16_t **a, uint16_t **b) { *a = fbs[0]; *b = fbs[1]; }
bool trx_lv_show_picture(const uint16_t *f) { g_pic_shown++; fb = (uint16_t *)f; return true; }
// T146: Flash:/menu.snap lives in memory; T146_START runs get a boot.cfg text and a picture from the run that saved them
static std::vector<uint8_t> g_snap;
static std::vector<uint8_t> g_rows;                 // T229: Flash:/menu.rows lives in memory too
static int g_rows_saves;
static int g_snap_saves;
static std::string g_t146_cfg;
static bool g_t146_cfg_set;

// ---- the file layer over a real directory tree (argv[2], made by mktree.py) ----
static char g_root[400];
static int g_saves;                  // writes of boot.cfg
static char g_last_save[4096];
static char g_stage_path[256];       // what the start was asked to stage
static const BootEntry *g_stage_trusted;
static int g_list_deep;              // directory listings below "/" (a search of the media would show here)
static unsigned long g_bytes_read;   // bytes the host app read from files through FileSource
static int g_stage_calls;
static int g_opens, g_stats, g_loads;   // files opened / stat'ed / loaded through the stub FS (T101 S10: the start opens none of an entry)
static char g_load_path[256];
static int kbq[64], kbn;
static void kb_push(int k) { if (kbn < 64) kbq[kbn++] = k; }
static void kb_type(const char *s) { for (; *s; s++) kb_push((unsigned char)*s); }
int UsbKb::getch() { if (!kbn) return -1; int k = kbq[0]; memmove(kbq, kbq + 1, sizeof(int) * --kbn); g_bench_ticks += g_key_gap_ms / 5; return k; }

static void fpath(char *out, size_t n, const char *p) { snprintf(out, n, "%s%s", g_root, p); }

int hostfs_list_n(const char *path, HostDirEntry *out, int max, bool only_cor, int *total);
int hostfs_list(const char *path, HostDirEntry *out, int max, bool only_cor) { return hostfs_list_n(path, out, max, only_cor, 0); }
int hostfs_list_n(const char *path, HostDirEntry *out, int max, bool only_cor, int *total)
{
    size_t pl = strlen(path);
    bool own = (pl > 8 && !strcmp(path + pl - 8, "/configs")) || (pl > 6 && !strcmp(path + pl - 6, "/media"));   // T198 S15: a core's own two folders, read once per generation
    if (strcmp(path, "/") && !own) g_list_deep++;
    char d[600];
    fpath(d, sizeof(d), path);
    struct dirent **nl;
    int c = scandir(d, &nl, 0, alphasort);
    if (c < 0) return -1;
    int n = 0, all = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < c; i++) {
            const char *nm = nl[i]->d_name;
            if (nm[0] == '.') continue;
            char full[700];
            snprintf(full, sizeof(full), "%s/%s", d, nm);
            struct stat st;
            if (stat(full, &st)) continue;
            bool dir = S_ISDIR(st.st_mode);
            int l = strlen(nm);
            if ((pass == 0) != dir) continue;
            (void)l;
            if (!dir && only_cor && !discover_is_bundle(nm)) continue;
            all++;
            if (n >= max) continue;
            strncpy(out[n].name, nm, sizeof(out[n].name) - 1);
            out[n].name[sizeof(out[n].name) - 1] = 0;
            out[n].dir = dir;
            out[n].size = st.st_size > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)st.st_size;     // saturated, as hostfs.cc
            out[n].size64 = (uint64_t)st.st_size;
            n++;
        }
    }
    for (int i = 0; i < c; i++) free(nl[i]);
    free(nl);
    if (total) *total = all;
    return n;
}
static bool g_loadback;              // T139: boot.cfg reads back what the host saved last
int hostfs_load(const char *path, char *buf, int max)
{
    if (!strcmp(path, MR_PATH)) {
        if (g_rows.empty()) return -1;
        int n = (int)g_rows.size() < max ? (int)g_rows.size() : max;
        memcpy(buf, g_rows.data(), n);
        return n;
    }
    if (!strcmp(path, MS_PATH)) {
        if (g_snap.empty()) return -1;
        int n = (int)g_snap.size() < max ? (int)g_snap.size() : max;
        memcpy(buf, g_snap.data(), n);
        return n;
    }
    if (g_t146_cfg_set && !strcmp(path, BOOTCFG_PATH)) {
        int n = (int)g_t146_cfg.size() < max ? (int)g_t146_cfg.size() : max;
        memcpy(buf, g_t146_cfg.data(), n);
        return n;
    }
    if (g_loadback && !strcmp(path, BOOTCFG_PATH)) {
        int n = (int)strlen(g_last_save);
        if (n > max) n = max;
        memcpy(buf, g_last_save, n);
        return n;
    }
    g_loads++;
    snprintf(g_load_path, sizeof(g_load_path), "%s", path);
    char d[600]; fpath(d, sizeof(d), path);
    FILE *f = fopen(d, "rb");
    if (!f) return -1;
    int n = fread(buf, 1, max, f);
    fclose(f);
    return n;
}
// T162: the bug report the menu writes (the file of /SD/report) and what the board would have supplied for it
static std::string g_rep_text, g_rep_name;
static int g_rep_saves;
extern "C" uint32_t get_fattime(void) { return (46u << 25) | (10u << 21) | (30u << 16) | (14u << 11) | (5u << 5) | 5u; }   // 2026-10-30 14:05:10
extern "C" bool rtc_time_valid(void) { return true; }
const char *hostreport_board(void) { return "C64 Ultimate (bench)"; }
int hostlog_tail(char *out, int max)
{
    static const char t[] = "[host] bench log line 1\n[host] direct boot on\n[host] password=hunter2 must not appear\n[host] last line\n";
    int n = (int)sizeof(t) - 1 < max ? (int)sizeof(t) - 1 : max;
    memcpy(out, t, n);
    return n;
}
bool hostfs_save(const char *dir, const char *name, const void *buf, int len)
{
    if (!strcmp(dir, "/SD/report")) {
        g_rep_text.assign((const char *)buf, len);
        g_rep_name = name;
        g_rep_saves++;
        return true;
    }
    if (!strcmp(name, MR_FILE)) {
        g_rows.assign((const uint8_t *)buf, (const uint8_t *)buf + len);
        g_rows_saves++;
        return true;
    }
    if (!strcmp(name, MS_FILE)) {
        g_snap.assign((const uint8_t *)buf, (const uint8_t *)buf + len);
        g_snap_saves++;
        return true;
    }
    g_saves++;
    snprintf(g_last_save, sizeof(g_last_save), "%.*s", len < (int)sizeof(g_last_save) - 1 ? len : (int)sizeof(g_last_save) - 1, (const char *)buf);
    return true;                                      // recorded, not written: the tree stays as mktree.py made it
}
bool hostfs_exists(const char *) { return false; }
// T139: the file layer of delete / copy / move over the same real tree. g_fake_free >= 0 makes every volume report that much room.
static int64_t g_fake_free = -1;
struct RealFo : FoFs {
    static void fp(char *out, size_t n, const char *p) { snprintf(out, n, "%s%s", g_root, p); }
    bool stat(const char *path, FoStat *st)
    {
        char d[700]; fp(d, sizeof(d), path);
        struct stat s;
        if (::stat(d, &s)) return false;
        st->dir = S_ISDIR(s.st_mode);
        st->size = (uint64_t)s.st_size;
        return true;
    }
    int dir_entries(const char *path)
    {
        char d[700]; fp(d, sizeof(d), path);
        DIR *dp = opendir(d);
        if (!dp) return -1;
        int n = 0;
        while (struct dirent *e = readdir(dp)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
        closedir(dp);
        return n;
    }
    bool free_bytes(const char *path, uint64_t *b)
    {
        if (g_fake_free >= 0) { *b = (uint64_t)g_fake_free; return true; }
        char d[700]; fp(d, sizeof(d), path);
        struct statvfs v;
        if (statvfs(d, &v)) return false;
        *b = (uint64_t)v.f_bavail * v.f_frsize;
        return true;
    }
    bool same_volume(const char *a, const char *b)
    {
        const char *ea = strchr(a + 1, '/'), *eb = strchr(b + 1, '/');
        size_t la = ea ? (size_t)(ea - a) : strlen(a), lb = eb ? (size_t)(eb - b) : strlen(b);
        return la == lb && !strncasecmp(a, b, la);
    }
    void *open_read(const char *path) { char d[700]; fp(d, sizeof(d), path); return fopen(d, "rb"); }
    void *open_write(const char *path) { char d[700]; fp(d, sizeof(d), path); return fopen(d, "wb"); }
    int read(void *h, void *buf, uint32_t n) { return (int)fread(buf, 1, n, (FILE *)h); }
    bool write(void *h, const void *buf, uint32_t n) { return fwrite(buf, 1, n, (FILE *)h) == n; }
    bool close(void *h) { return fclose((FILE *)h) == 0; }
    bool remove(const char *path)
    {
        char d[700]; fp(d, sizeof(d), path);
        FoStat st;
        if (!stat(path, &st)) return false;
        return st.dir ? rmdir(d) == 0 : unlink(d) == 0;
    }
    bool make_dir(const char *path) { char d[700]; fp(d, sizeof(d), path); return mkdir(d, 0755) == 0 || errno == EEXIST; }
    bool rename(const char *from, const char *to)
    {
        char a[700], b[700]; fp(a, sizeof(a), from); fp(b, sizeof(b), to);
        FoStat st;
        if (stat(to, &st)) return false;                 // the FileManager does not rename over a file either
        return ::rename(a, b) == 0;
    }
};
FoFs *hostfs_fo(void) { static RealFo fo; return &fo; }
// T148: the listing of the Cores and ROMs page over the bench's tree; the manifest names of the .cor files the scenario makes (they hold no manifest)
struct BenchCm : CmFs {
    int list(const char *dir, CmEntry *out, int max)
    {
        static HostDirEntry tmp[64];
        int n = hostfs_list(dir, tmp, max < 64 ? max : 64, false);
        for (int i = 0; i < n; i++) {
            snprintf(out[i].name, sizeof(out[i].name), "%s", tmp[i].name);
            out[i].dir = tmp[i].dir;
            out[i].size = tmp[i].size64;
        }
        return n;
    }
    bool core_name(const char *path, char *name, int n)
    {
        const char *b = strrchr(path, '/');
        b = b ? b + 1 : path;
        if (!strcasecmp(b, "apple2.cor")) { snprintf(name, n, "Apple IIe"); return true; }
        if (!strcasecmp(b, "atari800.cor")) { snprintf(name, n, "Atari 800 XL"); return true; }
        return false;
    }
};
CmFs *hostfs_cm(void) { static BenchCm cm; return &cm; }
bool hostfs_stat(const char *path, uint32_t *size, uint32_t *mtime)
{
    g_stats++;
    char d[600]; fpath(d, sizeof(d), path);
    struct stat st;
    if (stat(d, &st) || S_ISDIR(st.st_mode)) return false;
    *size = (uint32_t)st.st_size;
    *mtime = (uint32_t)st.st_mtime ? (uint32_t)st.st_mtime : 1;
    return true;
}
FileSource::FileSource(const char *path) : file(0), sz(0)
{
    g_opens++;
    char d[600]; fpath(d, sizeof(d), path);
    FILE *f = fopen(d, "rb");
    if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); file = f; }
}
FileSource::~FileSource() { if (file) fclose((FILE *)file); }
bool FileSource::read(uint32_t off, void *buf, uint32_t len)
{
    FILE *f = (FILE *)file;
    if (!f || off > sz || len > sz - off) return false;
    g_bytes_read += len;
    return !fseek(f, off, SEEK_SET) && fread(buf, 1, len, f) == len;
}
// T167: the overlay probe's totals over a run (every report of a progress page is one measurement of the states between its draw tasks)
static int g_ovp_n, g_ovp_bad, g_ovp_states, g_ovp_visits;
static const char *g_ovp_out;             // the bench output directory: the worst state of each overlay run goes there as a PNG
static void png(const char *path, int scale);
static void ovp_note(int bad, int st, int vi) { g_ovp_n++; g_ovp_bad += bad; g_ovp_states += st; g_ovp_visits += vi; }
static int g_page_areas = -1;            // T167 5.9: areas LVGL held when the start page changed to its next step (-1: not seen)
extern int hostui_test_fm_demo(int which, int *areas_unused);
extern int hostui_test_start_bar(int *track_w);
static bool g_stage_all;                 // T164: core_stage reports every step of a start (verify .. jump) and records the start bar's fill after each report
static int g_bar_fill[64], g_bar_step[64], g_bar_pct[64], g_bar_n, g_bar_track;
static const char *g_bar_out;            // the bench output directory: pictures of the bar in the middle of the flash write and at the jump
static bool g_stage_progress;            // T167: core_stage reports 0..100 % of two steps to the start page, as the real staging does
bool core_stage(const char *path, CorInfo *, const char **why, core_progress_t cb, void *ctx, const BootEntry *trusted, bool)
{
    snprintf(g_stage_path, sizeof(g_stage_path), "%s", path);
    g_stage_trusted = trusted;
    g_stage_calls++;
    if (g_stage_all) {
        g_bar_n = 0;
        for (int step = 0; step < STEPS; step++) {
            for (int pct = 0; pct <= 100; pct += (step < 3 ? 25 : 100)) {
                g_bench_ticks += 40;
                cb(step, pct, NULL, ctx);
                if (g_bar_n < 64) {
                    g_bar_fill[g_bar_n] = hostui_test_start_bar(&g_bar_track);
                    g_bar_step[g_bar_n] = step;
                    g_bar_pct[g_bar_n++] = pct;
                }
                char pp[600];
                if (g_bar_out && ((step == 1 && pct == 50) || (step == STEPS - 1))) {
                    snprintf(pp, sizeof(pp), "%s/start_bar_s%d_p%d.png", g_bar_out, step, pct); png(pp, 2);
                }
            }
        }
    }
    if (g_stage_progress) {
        for (int step = 0; step < 2; step++) {
            for (int pct = 0; pct <= 100; pct += 9) {
                g_bench_ticks += 40;                      // 200 ms between two reports: the page redraws on each
                bool first = step == 0 && pct == 0;
                if (!first) ovp::begin(fb, 180, 130, 600, 270);
                cb(step, pct, NULL, ctx);
                if (!first) {
                    int st, vi, ar;
                    int bad = ovp::end(&st, &vi, 8, &ar);
                    ovp_note(bad, st, vi);
                    if (step == 1 && pct == 0) g_page_areas = ar;
                }
            }
        }
        if (g_ovp_out) {                                  // the worst moment of the run, as the person saw it: the PNG that goes with the number
            ovp::Pic saved = ovp::paste_worst(fb);
            char pp[600]; snprintf(pp, sizeof(pp), "%s/overlay_start_worst.png", g_ovp_out); png(pp, 2);
            ovp::paste_back(fb, saved);
        }
    }
    *why = "bench: no hardware";
    kb_push('x');                                     // "any key returns to the menu"
    return false;
}
bool core_swap(const CorInfo *, const char **, core_progress_t, void *) { return false; }
std::string g_started_cfg = "?";               // T198: what the last core start asked the jump to hand over
void core_set_start_config(const char *n) { g_started_cfg = n ? n : ""; }
void core_set_start_safe(bool) {}                                   // T224 D2: the render bench starts no core; the safe flag has nothing to reach
void core_jump(const CorInfo *) { exit(1); }
// T115: the firmware start is recorded, not run
static char g_legacy_path[256];
static int g_legacy_calls;
static bool g_legacy_verify;
// T152: with g_legacy_ok the stage succeeds and the jump is recorded - it returns to the bench by an exception (the real one never returns)
static bool g_legacy_ok;
struct JumpReached { };
static int g_jump_calls, g_lock_calls_at_jump;
bool legacy_stage(const char *path, LegacyPlan *, const char **why, core_progress_t, void *, bool verify, bool)
{
    snprintf(g_legacy_path, sizeof(g_legacy_path), "%s", path);
    g_legacy_calls++;
    g_legacy_verify = verify;
    if (g_legacy_ok) {
        *why = NULL;
        return true;
    }
    *why = "bench: no hardware";
    kb_push('x');                                     // "any key returns to the menu"
    return false;
}
// T152 D4/D6: the flash protection before the handover, as the host app sees it (the logic is flashlock.cc's, benched in test_flashlock.cc)
static bool g_prot_on = true, g_lock_fail;
static int g_lock_calls, g_lock_off_calls;
bool flashprot_enabled(void) { return g_prot_on; }
void flashprot_set(bool on) { g_prot_on = on; }
void flashprot_register(void) { }
void flashprot_status_text(char *out, int n) { snprintf(out, n, "Flash chip ef4018: status registers SR1 %s, SR2 %s now.", g_lock_calls ? "34" : "00", g_lock_calls ? "01" : "00"); }
bool flashprot_before_handover(const char **msg)
{
    if (!g_prot_on) {
        g_lock_off_calls++;
        return true;
    }
    g_lock_calls++;
    if (g_lock_fail) {
        *msg = "the flash status registers do not read back as set";
        kb_push('x');                                 // "any key returns to the menu" (the start screen's error waits for one)
        return false;
    }
    return true;
}
// T152 D11: the power cycle through the ESP32 control module, as the host app sees it (the request itself is espreboot.cc's, board only)
static int g_esp_calls;
static bool g_esp_ok = true;
static uint32_t g_esp_tick;
bool esp_power_cycle(const char **why)
{
    g_esp_calls++;
    g_esp_tick = g_bench_ticks;
    if (!g_esp_ok) {
        *why = "the ESP32 control module did not answer";
        return false;
    }
    return true;
}
void legacy_jump(const LegacyPlan *)
{
    if (g_legacy_ok) {
        g_jump_calls++;
        g_lock_calls_at_jump = g_lock_calls;
        throw JumpReached();
    }
    exit(1);
}
uint32_t core_cycles(void) { return 0; }


// ---- T111: the media the File Browser lists, as fakes (the board half is mediaglue.cc; its real code runs in test_exfatfmt) ----
struct FakeMed { const char *name; bool formattable, ready; uint32_t sectors; MediaType type; const char *label; const char *refuse; int parts; };
static FakeMed g_med[] = {
    { "Flash", false, true, 14 * 2048, MT_FAT12, "", 0, 1 },
    { "SD", true, true, 62333952, MT_FAT32, "SDCARD", 0, 1 },          // 29.7 GiB
    { "Usb0", true, true, 30310400, MT_FAT32, "STICK", 0, 1 },         // 14.4 GiB
};
static void png(const char *path, int scale);
static int g_fmt_calls;                  // formats started: the bench's count of writes
static char g_fmt_name[24];
static int g_fmt_cancel_at_call = -1;    // the bench presses the cancel key at this report of a running format
static bool g_ovp_fmt;                   // T167: the format page is measured while it changes
extern bool hostui_test_fm_key(char *text, int max, int *key_y, int *above_y2, int *box_y2);   // T152 D10
extern void hostui_test_fm_geom(int *scrim, int *box_);          // D11: the page is sized to its lines, so the probe asks where it is
static const char *g_fmt_shot;           // a picture of the progress page at the middle of the format
static const char *g_fmt_out;
static FakeMed *fake(const char *n) { for (auto &m : g_med) if (!strcasecmp(m.name, n)) return &m; return 0; }
bool media_info(const char *name, MediaInfo *mi)
{
    memset(mi, 0, sizeof(*mi));
    snprintf(mi->name, sizeof(mi->name), "%s", name);
    FakeMed *m = fake(name);
    if (!m) return false;
    mi->found = true;
    mi->formattable = m->formattable;
    mi->ready = m->ready;
    mi->sectors = m->sectors;
    mi->sector_size = 512;
    mi->sn.type = m->type;
    snprintf(mi->sn.label, sizeof(mi->sn.label), "%s", m->label);
    mi->sn.parts = m->type == MT_NONE ? 0 : 1;
    mi->sn.mbr = true;
    mi->sn.sectors = m->sectors;
    mi->refuse = m->formattable ? m->refuse : 0;
    return true;
}
bool media_present(const char *name) { FakeMed *m = fake(name); return m && m->ready; }
bool media_free_bytes(const char *, uint64_t *b) { *b = 15000000000ULL; return true; }
uint32_t media_free_heap(void) { return 8000000; }
FmtResult media_format(const char *name, FmtProgressCb cb, void *ctx, char *why, int n)
{
    g_fmt_calls++;
    snprintf(g_fmt_name, sizeof(g_fmt_name), "%s", name);
    FakeMed *m = fake(name);
    why[0] = 0;
    uint32_t total = 4u << 20, written = 0;
    int calls = 0;
    auto report = [&](int step) -> bool {
        calls++;
        g_bench_ticks += 40;                              // 200 ms pass between two reports
        if (calls == g_fmt_cancel_at_call) kb_push(KEY_ESCAPE);
        bool probe = calls > 1 && g_ovp_fmt;
        if (probe) { int sc[4], bx[4]; lv_refr_now(NULL); hostui_test_fm_geom(sc, bx); ovp::begin(fb, bx[0], bx[1], bx[2], bx[3]); }
        bool c = cb(ctx, step, written, total) != 0;
        if (probe) { int st, vi; int bad = ovp::end(&st, &vi); ovp_note(bad, st, vi); }
        if (g_fmt_shot && calls == 5) { char pp[600]; snprintf(pp, sizeof(pp), "%s/%s", g_fmt_out, g_fmt_shot); png(pp, 2); }
        return c;
    };
    auto cancelled = [&]() {
        snprintf(why, n, "%s", written ? "cancelled; the medium is blank" : "cancelled before the first write; the medium is as it was");
        if (written) m->type = MT_NONE;
        kb_push('x');                                   // the person leaves the result page
        return FMT_CANCELLED;
    };
    if (report(FST_RELEASE)) return cancelled();
    written = 512;
    if (report(FST_WRITE)) return cancelled();
    while (written < total - (256u << 10)) {
        written += 256u << 10;
        if (report(FST_WRITE)) return cancelled();
    }
    written = total;
    report(FST_SYNC);
    report(FST_VERIFY);
    m->type = MT_EXFAT;
    m->label = "";
    kb_push('x');                                       // the person leaves the result page
    return FMT_OK;
}

extern void hostui_test(int view, int sel, int browse_sel, bool start);
extern uint32_t hostui_test_move(bool full);
extern void hostui_test_init(const char *cfgtext);
extern void hostui_test_view(int v);
extern int hostui_test_canvas_w(void);
extern int hostui_test_canvas_h(void);
extern void hostui_test_mode(int sys, int hdmi);
extern void hostui_test_browse_media(void);
extern uint32_t hostui_test_key(int k);
extern uint32_t hostui_test_cd_tick(void);
extern void hostui_test_cd_vis(int *label, int *bar, char *text, int tn);
extern int hostui_test_last_areas(void);
extern int hostui_test_inv_slots(void);
extern uint32_t hostui_test_chips(bool net);
extern void hostui_test_browse_at(const char *path);
extern void hostui_test_browse_keep(const char *path);
extern bool hostui_test_path_font_is_ui(void);
extern int hostui_test_msg_lines(void);
extern int hostui_test_foot_overflow(void);
extern int hostui_test_hint_overflow(void);
extern void hostui_test_web(int kind, int a, int b, const char *path);
extern void hostui_test_countdown(void);
extern int hostui_test_dump(char *out, int max);
extern bool hostui_test_startable(int i);
extern int hostui_test_cfg_n(void);
extern const char *hostui_test_cfg_path(int i);
extern int hostui_test_cfg_def(void);
extern void hostui_test_startup(void (*probe)(int step));
extern const char *hostui_test_hint(void);
extern void hostui_test_init_lazy(const char *cfgtext);
extern bool hostui_test_lazy_pass(int nkeys, uint32_t now, uint32_t last_key);
extern void hostui_test_lazy_drain(void);
extern bool hostui_test_lazy_busy(void);
extern const char *hostui_test_start_err(void);
extern bool hostui_test_hint_shown(void);

static void put_be(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *t, const unsigned char *d, uint32_t n)
{
    unsigned char h[8]; put_be(h, n); memcpy(h + 4, t, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uLong c = crc32(0, (const Bytef *)t, 4); if (n) c = crc32(c, d, n);
    unsigned char e[4]; put_be(e, c); fwrite(e, 1, 4, f);
}
// The frame as a screen shows it: the 960 x 540 layer, each pixel S x S output pixels, its top left at (x0, y0) of an
// OW x OH output (pix_layer.v: PX_CTRL b5:4 scale, PX_POS); outside the layer the picture below, here black. png(path, 2)
// is the layer at 2 x 2 in its own size (OW = 1920, OH = 1080, x0 = y0 = 0 is the same picture).
static void png_out(const char *path, int scale, int OW, int OH, int x0, int y0)
{
    int W = OW, H = OH;
    unsigned char *raw = (unsigned char *)malloc((size_t)(W * 3 + 1) * H), *p = raw;
    for (int y = 0; y < H; y++) {
        *p++ = 0;
        for (int x = 0; x < W; x++) {
            int lx = x - x0, ly = y - y0;
            if (lx < 0 || ly < 0 || lx >= 960 * scale || ly >= 540 * scale) { *p++ = 0; *p++ = 0; *p++ = 0; continue; }
            uint16_t v = fb[(ly / scale) * 960 + lx / scale];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            *p++ = (r << 3) | (r >> 2); *p++ = (g << 2) | (g >> 4); *p++ = (b << 3) | (b >> 2);   // the layer's bit replication
        }
    }
    uLongf cl = compressBound((W * 3 + 1) * H);
    unsigned char *z = (unsigned char *)malloc(cl);
    compress(z, &cl, raw, (W * 3 + 1) * H);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13]; put_be(ih, W); put_be(ih + 4, H); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, cl); chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
}
static void png(const char *path, int scale) { png_out(path, scale, 960 * scale, 540 * scale, 0, 0); }


// Is a 32 x 32 RGB565 picture somewhere in the frame, pixel for pixel (T114)?
static int count_icon(const uint16_t *ic)
{
    int n = 0;
    for (int y = 0; y + 32 <= 540; y++)
        for (int x = 0; x + 32 <= 960; x++) {
            if (fb[y * 960 + x] != ic[0]) continue;
            bool ok = true;
            for (int r = 0; r < 32 && ok; r++) ok = !memcmp(&fb[(y + r) * 960 + x], ic + r * 32, 64);
            if (ok) n++;
        }
    return n;
}
static bool find_icon(const uint16_t *ic) { return count_icon(ic) > 0; }

// T114 icon scale: the picture is found 1:1 (an LVGL zoom would not match pixel for pixel) and the pixels around it, left,
// right and at the corners, are the row's own background: no box wider than the picture.
static bool icon_no_bars(const uint16_t *ic)
{
    for (int y = 1; y + 33 <= 540; y++)
        for (int x = 1; x + 33 <= 960; x++) {
            if (fb[y * 960 + x] != ic[0]) continue;
            bool ok = true;
            for (int r = 0; r < 32 && ok; r++) ok = !memcmp(&fb[(y + r) * 960 + x], ic + r * 32, 64);
            if (!ok) continue;
            uint16_t bg = fb[(y - 1) * 960 + x - 1];
            bool clean = fb[(y + 32) * 960 + x + 32] == bg && fb[(y - 1) * 960 + x + 32] == bg && fb[(y + 32) * 960 + x - 1] == bg;
            for (int r = 0; r < 32; r++) clean = clean && fb[(y + r) * 960 + x - 1] == bg && fb[(y + r) * 960 + x + 32] == bg;
            return clean;
        }
    return false;
}

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// T152 S8: the ROM loader model behind the host app's link (a scenario sets it); none: the link is absent
static EspLoaderModel *g_esp_model;
EspFlashLink *esp_flash_board_link(void) { return g_esp_model; }

// T163 S3: the bench's FTP store (on the board: the "Network Settings" store, ftpstate_board.cc)
static FtpState g_ftp = { true, FTPSTATE_PORT, false };
FtpState ftp_state_now(void) { return g_ftp; }
static int g_ftp_sets;
void ftp_state_set(bool on) { g_ftp.on = on; g_ftp_sets++; }

// ---- T197 P3: Unzip in the File Browser. The job is the board's (host_core_flow_zip.cc over the loader of coreapp/src); what stands in for the machine is a CoreFs over the bench's
// tree and a LibHost whose "application" asks what unzip asks (the bar, one page). The real unzip + miniz run in tests/hostapp/test_hostzip.cc (S3); this part judges the screens.
#include "host_core_flow_zip.h"
#include "hostzip_board.h"
class BenchMemFile : public CoreFile {
public:
    std::string d;
    uint64_t size() { return d.size(); }
    bool writable() { return false; }
    bool read(uint64_t off, void *buf, uint32_t len) { if (off + len > d.size()) return false; memcpy(buf, d.data() + off, len); return true; }
    bool write(uint64_t, const void *, uint32_t) { return false; }
    bool sync() { return true; }
};
class BenchZipFs : public CoreFs {
public:
    int list(const char *dir, FsEntry *out, int max)
    {
        HostDirEntry *h = (HostDirEntry *)malloc(sizeof(HostDirEntry) * 64);
        int n = hostfs_list(dir, h, 64, false);
        for (int i = 0; i < n && i < max; i++) {
            memset(&out[i], 0, sizeof(out[i]));
            snprintf(out[i].name, sizeof(out[i].name), "%s", h[i].name);
            out[i].dir = h[i].dir;
            out[i].size = h[i].size64;
        }
        free(h);
        return n < max ? n : max;
    }
    bool stat(const char *path, uint64_t *size, bool *dir) { char d[700]; fpath(d, sizeof(d), path); struct stat st; if (::stat(d, &st)) return false; if (size) *size = st.st_size; if (dir) *dir = S_ISDIR(st.st_mode); return true; }
    int load(const char *, void *, uint32_t) { return -1; }
    bool save(const char *, const void *, uint32_t) { return false; }
    bool remove(const char *) { return false; }
    CoreFile *open(const char *path, bool rw)
    {
        if (rw) return 0;
        char d[700]; fpath(d, sizeof(d), path);
        FILE *f = fopen(d, "rb");
        if (!f) return 0;
        BenchMemFile *m = new BenchMemFile;
        char b[4096]; size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0) m->d.append(b, n);
        fclose(f);
        return m;
    }
    void close(CoreFile *f) { delete f; }
};
extern const char *hostui_test_browse_sub(void);
static bool g_zip_one, g_zip_refuse_pic;                                                    // the bench's application plays the one-file picker
class BenchLibHost : public LibHost {
public:
    void *alloc(uint32_t n) { return malloc(n); }
    void release(void *p) { free(p); }
    uint32_t addr_of(const void *p) { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) { }
    bool has_capability(const char *) { return false; }
    const char *core_name() { return "host"; }
    int call_register(const Entry &, const LibApi *, LibRecord *) { return 0; }
    int call_main(const Entry &, const LibApi *api, AppCtx *ctx)         // what unzip does on screen: a bar, then its page
    {
        if (g_zip_one) {
            const char *items[3] = { "readme.txt", "sub/data.bin", "sub/deep/more.bin" };
            if (api->dlg->choose("Unzip set.zip", items, 3, 0) < 0) return 0;
            api->dlg->message("Unzip", "Extracted 1 file into /Usb0/zip");
            return 0;
        }
        void *bar = api->dlg->progress_open("Unzipping", 3);
        const char *names[3] = { "readme.txt", "sub/data.bin", "sub/deep/more.bin" };
        for (int i = 1; i <= 3; i++) api->dlg->progress_set(bar, i, names[i - 1]);
        api->dlg->progress_close(bar);
        api->dlg->message("Unzip", "Extracted 3 files into /Usb0/zip/set");   // the hook presses the key once the page is up
        (void)ctx;
        return 0;
    }
};
HcZipEnv *hostzip_env(const HcFlowEnv *base)
{
    static BenchZipFs fs;
    static BenchLibHost host;
    static HcZipEnv env;
    if (!env.base) { env.base = base; env.fs = &fs; env.libhost = &host; env.rig = 0; }
    return &env;
}

#endif
