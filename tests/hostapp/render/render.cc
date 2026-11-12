// Renders the boot menu screens natively (LVGL + the real hostui.cc, firmware stubbed) to PNG files, upscaled 2x
// nearest like the pixel layer's scale 2:  tests/hostapp/render/run.sh <outdir>
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
#include "../esp_loader_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
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
extern "C" uint32_t now_zero_tick(void) { static uint32_t t; return t += 50; }
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
    lv_display_set_flush_cb(d, [](lv_display_t *dd, const lv_area_t *, uint8_t *px) { if (lv_display_flush_is_last(dd)) fb = (uint16_t *)px; lv_display_flush_ready(dd); });
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

int hostfs_list(const char *path, HostDirEntry *out, int max, bool only_cor)
{
    size_t pl = strlen(path);
    bool own = (pl > 8 && !strcmp(path + pl - 8, "/configs")) || (pl > 6 && !strcmp(path + pl - 6, "/media"));   // T198 S15: a core's own two folders, read once per generation
    if (strcmp(path, "/") && !own) g_list_deep++;
    char d[600];
    fpath(d, sizeof(d), path);
    struct dirent **nl;
    int c = scandir(d, &nl, 0, alphasort);
    if (c < 0) return -1;
    int n = 0;
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
            if ((pass == 0) != dir || n >= max) continue;
            (void)l;
            if (!dir && only_cor && !discover_is_bundle(nm)) continue;
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
    return n;
}
static bool g_loadback;              // T139: boot.cfg reads back what the host saved last
int hostfs_load(const char *path, char *buf, int max)
{
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
static char D[16384];
static const char *dump(void) { hostui_test_dump(D, sizeof(D)); return D; }
static bool has(const char *needle) { return strstr(dump(), needle) != 0; }
static int geom(const char *key)            // "geometry rows_bottom=N note_top=N countdown_top=N"
{
    const char *p = strstr(dump(), key);
    return p ? atoi(p + strlen(key)) : -1;
}
static const char *CFG3 = "countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|TRX64 Ultimate (C64)||0\n"
                          "core=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\ncore=/Usb0/old/trx64-0.9.cor|TRX64 Ultimate (old host)||0\n";

// T187 D5: the Picture page. page_pick opens it with V (from any view), puts the cursor on row `row` (0 Auto, n the n-th resolution of the list) and presses RETURN.
extern int hostui_test_pm(char *name, int ncap, int *sel, int *rows, char *info, int icap);
extern bool hostui_test_pm_row(int i, char *name, int nn, char *hz, int hn, char *tag, int tn);
extern bool hostui_test_pm_geo(int which, int *x1, int *y1, int *x2, int *y2);
static void page_pick(int row)
{
    hostui_test_key('v');
    for (int i = 0; i < 8; i++) hostui_test_key(KEY_UP);
    for (int i = 0; i < row; i++) hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_RETURN);
}

static void scenarios(const char *out)
{
    char p[512];
    // T106: a command from the network goes through the same path the keys do - the web task only
    // queues, the UI task applies. Here: the picture, and an entry that cannot be started.
    hostui_test_init(CFG3);
    g_saves = 0;
    fresh_video(VID_SYS_PAL50, -1);
    g_hv_stores = 0;
    hostui_test_web(WEB_VIDEO, 0, 2, 0);
    CHECK(g_vid_hdmi == 2);                                    // applied
    CHECK(g_hv_stores == 1 && g_saved_hdmi == 2);              // and written to host.cfg (T164), not to boot.cfg
    CHECK(g_saves == 0);
    CHECK(strstr(web_note(), "picture") != 0);
    hostui_test_web(WEB_VIDEO, 0, 4, 0);                       // S10: 1024 x 768 is not on the Q9 list, the screen was not asked for it
    CHECK(g_vid_hdmi == 2 && g_hv_stores == 1);
    CHECK(strstr(web_note(), "not") != 0);
    int n_before = hostui_test_cfg_n();
    hostui_test_web(WEB_REMOVE, 0, 0, 0);
    CHECK(hostui_test_cfg_n() == n_before && strstr(web_note(), "asks first"));      // D19a: no path removes unasked; the page's remove is a question (webnet.cc, the service)
    hostui_test_web(WEB_COUNTDOWN, 0, 0, 0);
    CHECK(has("countdown") || true);

    // T105: V steps the resolution, applies it at once and saves it. The way back from a screen that
    // cannot show the mode the board came up in - the one thing a user with no picture can still do.
    hostui_test_init(0);
    // ADR-0057: with nothing stored the host still asks the screen and keeps the answer, so it knows
    // its own mode and hands that on. Before, it left vid_hdmi at -1 and a core's mode stood.
    CHECK(g_vid_hdmi >= 0 && g_vid_hdmi < VID_HDMI_COUNT);
    g_saves = 0;
    int h0 = g_vid_hdmi;
    g_hv_stores = 0;
    hostui_test_key('v');                                      // T187 D5: V opens the Picture page; browsing it applies nothing
    CHECK(hostui_test_pm(0, 0, 0, 0, 0, 0) == 1 && g_vid_hdmi == h0 && g_hv_stores == 0);
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN); hostui_test_key(KEY_UP);
    CHECK(g_vid_hdmi == h0 && g_hv_stores == 0);
    hostui_test_key(KEY_LEFT);                                 // LEFT closes it, nothing changed
    CHECK(hostui_test_pm(0, 0, 0, 0, 0, 0) == 0 && g_vid_hdmi == h0 && g_hv_stores == 0);
    int tgt = h0 == VID_HDMI_480_576 ? VID_HDMI_720P : VID_HDMI_480_576;
    page_pick(tgt + 1);                                        // the one way to a mode: RETURN on its row
    CHECK(g_vid_hdmi == tgt && g_hv_stores == 1);              // applied, not only noted, and written to host.cfg
    CHECK(has("note='Picture:"));
    page_pick(tgt + 1);                                        // RETURN on the mode in force: nothing to apply
    CHECK(g_vid_hdmi == tgt && g_hv_stores == 1 && hostui_test_pm(0, 0, 0, 0, 0, 0) == 0);
    for (int h = 0; h <= VID_HDMI_1080P; h++) {                // only what the screen names (no EDID: 576p, 720p, 1080p), each by its row
        page_pick(h + 1);
        CHECK(g_vid_hdmi == h);
    }

    // S2 (T101 S10) nothing is searched for: the media hold five bundles, the empty menu stays empty, and no folder is listed
    g_list_deep = 0; g_bytes_read = 0;
    hostui_test_init(0);
    CHECK(hostui_test_cfg_n() == 0 && has("total=0 ") && !has("Tiny") && !has("vc20.cor"));
    CHECK(g_list_deep == 0 && g_bytes_read == 0);                           // not a folder listed, not a byte of a bundle read
    g_saves = 0;
    hostui_test_key('a'); hostui_test_key('r');
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0 && g_list_deep == 0);    // the keys of the old scan do nothing here
    snprintf(p, sizeof(p), "%s/menu_empty.png", out); png(p, 2);
    // adding by hand: F3, the folder, RETURN on the file; the other contract is refused, as before
    hostui_test_browse_keep("/Usb0/cores");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);                   // row 0 "..", 1 badicon, 2 c2.cor
    g_saves = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0 && has("note='refused: built for another host contract'"));   // the negative
    hostui_test_key(KEY_DOWN);                                              // 3 vc20.cor
    g_bytes_read = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 1 && !strcmp(hostui_test_cfg_path(0), "/Usb0/cores/vc20.cor") && hostui_test_cfg_def() == 0);
    CHECK(g_saves == 1 && strstr(g_last_save, "core=/Usb0/cores/vc20.cor|VC-20 (PAL)|") && strstr(g_last_save, "default=0"));
    CHECK(g_bytes_read > 8000);                                             // adding is where the whole bundle is read and hashed
    char vline[512] = "";
    { const char *h = strstr(g_last_save, "core=/Usb0/cores/vc20.cor|"); if (h) { snprintf(vline, sizeof(vline), "%.*s", (int)strcspn(h, "\n"), h); } }
    char vhash[80] = ""; unsigned vsize = 0, vmtime = 0, vhdr = 0;
    CHECK(sscanf(vline, "core=/Usb0/cores/vc20.cor|VC-20 (PAL)|%64[0-9a-f]|%u|%u|%x", vhash, &vsize, &vmtime, &vhdr) == 4);
    CHECK(strlen(vhash) == 64 && vsize > 8000 && vmtime != 0 && vhdr != 0);   // hash, size, mtime and header digest are kept
    // a configured entry starts WITH its trusted entry: the control is a start once from the browser, which has none
    hostui_test_view(0);
    g_stage_calls = 0; g_stage_trusted = 0;
    hostui_test_key(KEY_RETURN);
    CHECK(g_stage_calls == 1 && g_stage_trusted != 0 && !strcmp(g_stage_trusted->path, "/Usb0/cores/vc20.cor"));
    CHECK(strstr(hostui_test_start_err(), "Press R") != 0);          // refused at its start: the reason and what to do (T101 S10)
    hostui_test_browse_keep("/Usb0/cores");
    for (int i = 0; i < 3; i++) hostui_test_key(KEY_DOWN);
    g_stage_trusted = (const BootEntry *)1;
    g_stage_progress = true; g_ovp_n = g_ovp_bad = g_ovp_states = g_ovp_visits = 0; ovp::run_reset(); g_ovp_out = out;
    hostui_test_key('s');
    g_stage_progress = false;
    CHECK(g_stage_calls == 2 && g_stage_trusted == 0 && !strcmp(g_stage_path, "/Usb0/cores/vc20.cor"));
    // T167 S1: the start page's percent and bar change over the File Browser; no pixel of the browser shows inside the panel
    printf("overlay start: %d reports, %d states, %d visits, %d foreign pixels\n", g_ovp_n, g_ovp_states, g_ovp_visits, g_ovp_bad);
    CHECK(g_ovp_n >= 15 && g_ovp_visits >= g_ovp_n && g_ovp_states >= g_ovp_n);
    CHECK(g_ovp_bad == 0);
    {   // T167 5.9: a page change is ONE invalidated area (the panel), however many labels it changes; a tick of the page that is up is not asked
        int a_show, a_page, a_tick, st, vi;
        hostui_test_fm_demo(0, 0);                                                   // page A is up
        ovp::begin(fb, 100, 50, 760, 440); hostui_test_fm_demo(1, 0); ovp::end(&st, &vi, 8, &a_page);   // page A -> page B
        ovp::begin(fb, 100, 50, 760, 440); hostui_test_fm_demo(2, 0); ovp::end(&st, &vi, 8, &a_tick);   // a tick of B: text and bar
        hostui_test_fm_demo(3, 0);                                                   // closed
        ovp::begin(fb, 100, 50, 760, 440); hostui_test_fm_demo(0, 0); ovp::end(&st, &vi, 8, &a_show);   // shown again
        printf("page areas: start step change %d, fm page change %d, fm shown %d, fm tick %d\n", g_page_areas, a_page, a_show, a_tick);
        CHECK(g_page_areas >= 1 && g_page_areas <= 3);                             // the start page keeps its bar row: the strip above it, the strip below it and the bar's own growth (T164); no label has an area of its own
        CHECK(a_page == 1 && a_show == 1);
        CHECK(a_tick >= 2 && a_tick <= 16);                                          // a tick changes labels in place: it does not redraw the panel as one page
        hostui_test_fm_demo(3, 0);
    }
    CHECK(strstr(hostui_test_start_err(), "bench: no hardware") && !strstr(hostui_test_start_err(), "Press R"));   // a file started once is no entry
    {   // T164 (board 2026-11-01): the start's bar covers the whole start and never goes back, so it stands above 0 while a large core reloads the FPGA
        hostui_test_browse_keep("/Usb0/cores");
        for (int i = 0; i < 3; i++) hostui_test_key(KEY_DOWN);
        g_stage_trusted = (const BootEntry *)1;
        g_stage_all = true; g_bar_out = out;
        hostui_test_key('s');
        g_stage_all = false;
        int back = 0, grew = 0, last = -1;
        for (int i = 0; i < g_bar_n; i++) {
            printf("start bar: step %d %3d%%  fill %d of %d px\n", g_bar_step[i], g_bar_pct[i], g_bar_fill[i], g_bar_track);
            if (g_bar_fill[i] < last) back++;
            if (g_bar_fill[i] > last && last >= 0) grew++;
            last = g_bar_fill[i];
        }
        CHECK(g_bar_n >= 15 && g_bar_track > 100);
        CHECK(back == 0);                                                       // no step takes the bar back
        CHECK(g_bar_fill[g_bar_n - 1] >= g_bar_track * 3 / 4);                  // "Jump; the application starts its core": the bar is not at 0
        CHECK(grew >= g_bar_n - 6);                                             // it moves with the reports (a new step's first report stands where the last one ended)
        int jump0 = 0;
        for (int i = 0; i < g_bar_n; i++) if (g_bar_step[i] == STEPS - 1 && g_bar_pct[i] == 0) jump0 = g_bar_fill[i];
        CHECK(jump0 > 0);                                                       // the very report of the jump (percent 0) already shows the bar above 0
    }
    // T101 S10 (a)+(b): the next start trusts it (no byte of a part), and a different size / mtime / header marks it, never drops it
    {
        char txt[1024];
        const char *head = "countdown=5\ndefault=0\n";
        snprintf(txt, sizeof(txt), "%score=/Usb0/cores/vc20.cor|VC-20 (PAL)|%s|%u|%u|%08x\n", head, vhash, vsize, vmtime, vhdr);
        g_list_deep = 0; g_bytes_read = 0; g_saves = 0;
        hostui_test_init(txt);
        CHECK(has("VC-20 (PAL)|v0.1|hash ok|auto|SEL|img f800"));
        CHECK(g_saves == 0 && g_list_deep == 0);                            // nothing to migrate, nothing listed
        CHECK(g_bytes_read < vsize / 2);                                    // header, manifest and the 2 KB icon, not the 8 KB+ of parts
        printf("start of one entry: %lu of %u bytes read\n", g_bytes_read, vsize);
        // T101 S10: an entry the lazy reader has not reached starts all the same, with its trusted entry, and says what to do if refused
        hostui_test_init_lazy(txt);
        CHECK(has("|?|") && hostui_test_startable(0));
        g_stage_calls = 0; g_stage_trusted = 0;
        hostui_test_key(KEY_RETURN);
        CHECK(g_stage_calls == 1 && g_stage_trusted != 0 && strstr(hostui_test_start_err(), "bench: no hardware. Press R") != 0);
        snprintf(txt, sizeof(txt), "%score=/Usb0/cores/vc20.cor|VC-20 (PAL)|%s|%u|%u|%08x\n", head, vhash, vsize, vmtime + 2, vhdr);
        hostui_test_init(txt);
        CHECK(has("v0.1|changed|auto|SEL") && hostui_test_cfg_n() == 1);   // (a) another mtime
        snprintf(txt, sizeof(txt), "%score=/Usb0/cores/vc20.cor|VC-20 (PAL)|%s|%u|%u|%08x\n", head, vhash, vsize + 1, vmtime, vhdr);
        hostui_test_init(txt);
        CHECK(has("v0.1|changed|auto|SEL"));                              // (a) another size
        snprintf(txt, sizeof(txt), "%score=/Usb0/cores/vc20.cor|VC-20 (PAL)|%s|%u|%u|%08x\n", head, vhash, vsize, vmtime, vhdr ^ 1);
        hostui_test_init(txt);
        CHECK(has("v0.1|changed|auto|SEL"));                              // (b) another header digest
        CHECK(hostui_test_startable(0));                                     // marked, still startable (the start checks every part)
        // R on it: the full check again, the new state stored, the mark gone
        g_saves = 0; g_bytes_read = 0;
        hostui_test_key('r');
        CHECK(has("v0.1|hash ok|auto|SEL") && g_saves == 1 && g_bytes_read > 8000 && strstr(g_last_save, vline));
        // an entry of an older boot.cfg (no mtime, no hash): the full check runs once at the start and is stored
        g_saves = 0; g_bytes_read = 0;
        hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\n");
        CHECK(has("v0.1|hash ok|auto|SEL") && g_saves == 1 && g_bytes_read > 8000 && strstr(g_last_save, vline));
        const char *olds = strdup(g_last_save);
        g_saves = 0; g_bytes_read = 0;
        hostui_test_init(olds);
        CHECK(has("v0.1|hash ok|auto|SEL") && g_saves == 0 && g_bytes_read < vsize / 2);   // and never again
        // a file that is gone: marked missing, kept in the menu
        hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/cores/gone.cor|Gone|ab|10|5|00000001\n");
        CHECK(has("Gone||missing") && hostui_test_cfg_n() == 1);
    }
    g_stage_calls = 0;

    // S3 the list scrolls (T209: N entries, N = the menu's rows + 4; the window moves while 2 rows are still hidden below the selection)
    char many[4096] = "countdown=5\ndefault=0\n";
    hostui_test_init(many);
    const int MR = geom("rows=");
    const int N = MR + 4;
    for (int i = 1; i <= N; i++) { char l[120]; snprintf(l, sizeof(l), "core=/Usb0/many/m%d.cor|Core number %d||0\n", i, i); strcat(many, l); }
    hostui_test_init(many);
    char ex[160];
    auto pos = [&](int first) { snprintf(ex, sizeof(ex), "pos='%d-%d of %d'", first + 1, first + MR, N); return has(ex); };
    CHECK(MR >= 5 && N <= 640);
    snprintf(ex, sizeof(ex), "total=%d", N);
    CHECK(has(ex) && pos(0));
    bool in_window = true, no_overlap = true;
    for (int i = 0; i < N - 1; i++) {
        hostui_test_key(KEY_DOWN);
        in_window &= has("|SEL|") || strstr(D, "SEL|");
        no_overlap &= geom("rows_bottom=") <= geom("note_top=") && geom("note_top=") < geom("countdown_top=");
    }
    CHECK(in_window && no_overlap);
    snprintf(ex, sizeof(ex), "sel=%d first=%d total=%d ", N - 1, N - MR, N);
    CHECK(has(ex) && pos(N - MR));
    snprintf(p, sizeof(p), "%s/menu_scroll_end.png", out); png(p, 2);
    for (int i = 0; i < N - 1; i++) {
        hostui_test_key(KEY_UP);
        in_window &= strstr(D, "SEL|") != 0;
    }
    snprintf(ex, sizeof(ex), "sel=0 first=0 ");
    CHECK(in_window && has(ex) && pos(0));
    // DOWN into the middle, then one UP inside the window must not scroll it
    for (int i = 0; i < MR + 1; i++) hostui_test_key(KEY_DOWN);             // sel MR+1: 2 rows of margin below it, first 4
    snprintf(ex, sizeof(ex), "sel=%d first=%d ", MR + 1, 4);
    CHECK(has(ex));
    hostui_test_key(KEY_UP);
    snprintf(ex, sizeof(ex), "sel=%d first=%d ", MR, 4);
    CHECK(has(ex));
    // T209 D7: "+" / "-" page the boot menu (a page = the rows it shows), stopping at the ends; the foot names the keys
    CHECK(has("+/- PAGE"));
    hostui_test_key('-');
    snprintf(ex, sizeof(ex), "sel=%d first=%d ", 0, 0);
    CHECK(has(ex));
    hostui_test_key('+');
    snprintf(ex, sizeof(ex), "sel=%d first=%d ", MR, 3);
    CHECK(has(ex));
    snprintf(p, sizeof(p), "%s/menu_page_step.png", out); png(p, 2);
    hostui_test_key('+');
    snprintf(ex, sizeof(ex), "sel=%d first=%d ", N - 1, N - MR);
    CHECK(has(ex));
    hostui_test_key('+');
    CHECK(has(ex));
    hostui_test_key('-');
    hostui_test_key('-');
    hostui_test_key('-');
    snprintf(ex, sizeof(ex), "sel=%d first=%d ", 0, 0);
    CHECK(has(ex));
    // the editor scrolls too (the same entries)
    hostui_test_view(2);
    const int ER = geom("rows=");
    printf("editor rows on the full canvas: %d (menu rows %d)\n", ER, MR);
    CHECK(ER >= 3);
    for (int i = 0; i < N; i++) hostui_test_key(KEY_DOWN);
    snprintf(ex, sizeof(ex), "view=2 sel=%d first=%d total=%d ", N - 1, N - ER > 0 ? N - ER : 0, N);
    CHECK(has(ex) && geom("rows_bottom=") <= geom("note_top="));
    snprintf(p, sizeof(p), "%s/edit_scroll_end.png", out); png(p, 2);

    // S4 icons
    hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\ncore=/SD/cores/trx64.cor|TRX64 Ultimate (C64)||0\n"
                     "core=/Usb0/cores/badicon.cor|Bad icon||0\n");
    CHECK(has("VC-20 (PAL)|v0.1|hash ok|auto|SEL|img f800"));
    CHECK(has("|TRX64 Ultimate (C64)|v1.0-rc1|hash ok||") && has("v1.0-rc1|hash ok|||tag"));
    CHECK(has("Bad icon|v0.3|changed|||tag"));     // the full check of the older entry finds the icon's SHA-256 wrong: not accepted, no picture
    snprintf(p, sizeof(p), "%s/icons.png", out); png(p, 2);

    // S4b T114: the real core logos reach the screen. The bundle's icon is searched in the frame buffer pixel for
    // pixel (drawn 1:1, 32 x 32 RGB565), so a wrong picture, a missing one or a text tile cannot pass.
    {
        const char *ids[] = { "amiga", "amiga1200", "apple2", "atari800", "c128", "c264", "c64u", "cpc", "nes", "trx64", "vc20", "zx", "zxnext" };
        const char *ld = getenv("LOGO_DIR");
        CHECK(ld != 0);
        int drawn = 0;
        for (auto id : ids) {
            char lp[600], cfgt[600];
            uint16_t logo[1024];
            if (!strcmp(id, "trx64"))        // no core directory since T115 D12, the logo is an extra of tools/core_logos
                snprintf(lp, sizeof(lp), "%s/../tools/core_logos/trx64/icon.rgb565", ld ? ld : ".");
            else
                snprintf(lp, sizeof(lp), "%s/%s/logo/icon.rgb565", ld ? ld : ".", id);
            FILE *lf = fopen(lp, "rb");
            bool got = lf && fread(logo, 2, 1024, lf) == 1024;
            if (lf) fclose(lf);
            CHECK(got);
            snprintf(cfgt, sizeof(cfgt), "countdown=5\ndefault=0\ncore=/Usb0/logos/%s.cor|%s||0\n", id, id);
            hostui_test_init(cfgt);
            if (got && has("|hash ok|auto|SEL|img") && find_icon(logo)) drawn++;
            else printf("FAIL logo %s not drawn\n", id);
        }
        CHECK(drawn == 13);
        printf("logos: %d of 13 real icons found pixel-exact in the frame\n", drawn);
        // the control: the same bundle with a damaged icon record shows the text tile and not the picture
        uint16_t amiga[1024];
        char lp[600];
        snprintf(lp, sizeof(lp), "%s/amiga/logo/icon.rgb565", ld ? ld : ".");
        FILE *lf = fopen(lp, "rb");
        CHECK(lf && fread(amiga, 2, 1024, lf) == 1024);
        if (lf) fclose(lf);
        hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/logos/amiga.cor|amiga||0\n");
        CHECK(find_icon(amiga));                                                         // the control can succeed ...
        CHECK(icon_no_bars(amiga));                                                      // T114: and sits in no wider box
        hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/logos/bad_amiga.cor|amiga bad||0\n");
        CHECK(has("|auto|SEL|tag") && !find_icon(amiga));                             // ... and fails on a bad hash
        printf("logos control: damaged icon -> text tile, picture absent\n");
        hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/logos/trx64.cor|TRX64 Ultimate (C64)||0\ncore=/Usb0/logos/amiga.cor|Amiga (OCS)||0\n"
                         "core=/Usb0/logos/c64u.cor|C64 Ultimate||0\ncore=/Usb0/logos/vc20.cor|VC-20 (PAL)||0\ncore=/Usb0/logos/c264.cor|Plus/4||0\n");
        snprintf(p, sizeof(p), "%s/logos.png", out); png(p, 2);
        // T101 UI_RES: the same frame as the board shows it, 1920 x 1080 (0: scale 2, 1: scale 1 centred by PX_POS)
        snprintf(p, sizeof(p), "%s/board_res0_logos.png", out); png_out(p, 2, 1920, 1080, 0, 0);
        snprintf(p, sizeof(p), "%s/board_res1_logos.png", out); png_out(p, 1, 1920, 1080, (1920 - 960) / 2, (1080 - 540) / 2);
    }

    // S5 removal asks first
    hostui_test_init(CFG3);
    hostui_test_view(2);
    g_saves = 0;
    hostui_test_key(KEY_DELETE);
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0 && has("note='DEL again to remove TRX64 Ultimate (C64) (any other key cancels)'"));
    hostui_test_key(KEY_DOWN);                                              // another key cancels
    CHECK(hostui_test_cfg_n() == 3 && has("note=''"));
    hostui_test_key(KEY_DELETE);                                            // arms the VC-20 row, does not remove it
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0 && has("DEL again to remove VC-20 (PAL)"));
    hostui_test_key(KEY_UP);                                                // DEL, UP, DEL: still nothing removed
    hostui_test_key(KEY_DELETE);
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DELETE);
    hostui_test_key(KEY_DELETE);                                            // DEL DEL: gone
    CHECK(hostui_test_cfg_n() == 2 && g_saves == 1 && !strcmp(hostui_test_cfg_path(1), "/Usb0/old/trx64-0.9.cor") && !strstr(g_last_save, "vc20.cor"));

    // S6 the mock's words; the other-host badge needs a host key
    extern uint32_t cor_host_key;
    cor_host_key = 0x820eded2;
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|A||0\ncore=/SD/cores/trx64.cor.x|B||0\ncore=/Usb0/old/trx64-0.9.cor|C||0\n"
                     "core=/Usb0/old/h2.cor|D||0\ncore=/Usb0/old/garbage.cor|E||0\ncore=/Usb0/cores/vc20.cor|F|0000|0\n");
    CHECK(has("A|v1.0-rc1|hash ok|auto|SEL"));
    CHECK(has("B||missing"));
    CHECK(has("C||contract 3 != 2"));
    CHECK(has("D||other host"));
    CHECK(has("E||damaged"));
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DOWN);                                              // 6 rows: F is row 5, off the 5-row window
    CHECK(has("F|v0.1|changed"));
    CHECK(has("foot='UP/DOWN select    RETURN start    R check entry    V picture    any key stops the countdown'"));
    CHECK(hostui_test_hint_overflow() == 0);                           // the "No cores yet" hint is cut at the right edge when it is one line
    CHECK(hostui_test_foot_overflow() == 0);                           // every footer is one line (board photo 2026-10-21: it wrapped)
    hostui_test_view(2);
    CHECK(has("foot='UP/DOWN select    +/- move    D default    C rename    DEL twice removes'"));        // T139: no B, no LEFT/RIGHT, no V
    cor_host_key = 0;
    snprintf(p, sizeof(p), "%s/states.png", out); png(p, 2);
}


// T115 S-render: .cfw full firmware images beside the cores. The tree is mktree.py's second one (<root>_fw): two valid
// images (Gideon's layout and the large TRX64's), one that holds only a 100T bitstream, and a core for contrast.
extern const char *hostui_test_confirm(void);
extern int hostui_test_confirms(void);
extern void hostui_test_confirm_panel(bool show);
extern void hostui_test_start_panel_full(void);
extern int hostui_test_browse_right(char *out, int max);
static void scenarios_fw(const char *out)
{
    char p[512], main_root[400], R[1024];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_fw", main_root);
    hostui_test_init(0);
    CHECK(hostui_test_cfg_n() == 0 && has("total=0 "));                                    // the images lie on the media; the menu does not show them
    // adding by hand (F3): the refused image is refused with the parser's reason, a valid one goes in with its size and mtime
    hostui_test_browse_keep("/Usb0/cores");                                                // row 1 gideon.cfw, 2 only100.cfw, 3 trx64_large.cfw, 4 vc20.cor
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    g_saves = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0 && has("note='refused: no bitstream for this FPGA in the firmware image'"));
    hostui_test_key(KEY_UP);
    g_saves = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 1 && g_saves == 1 && strstr(g_last_save, "core=/Usb0/cores/gideon.cfw|u64_mk2_artix 2026/09/10 11:00:57||"));
    char fwline[512] = "";
    { const char *h = strstr(g_last_save, "core=/Usb0/cores/gideon.cfw|"); if (h) snprintf(fwline, sizeof(fwline), "%.*s", (int)strcspn(h, "\n"), h); }
    unsigned fsz = 0, fmt = 0, fhd = 9;
    { const char *tail = strstr(fwline, "||"); CHECK(tail && sscanf(tail + 2, "%u|%u|%x", &fsz, &fmt, &fhd) == 3 && fsz > 0 && fmt != 0 && fhd == 0); }
    hostui_test_view(0);
    CHECK(has("|full firmware|ready|auto|SEL"));
    snprintf(p, sizeof(p), "%s/menu_firmware.png", out); png(p, 2);
    // the start check of a firmware: the directory entry only, no bitstream walked (size or mtime another: marked, the parse decides)
    {
        char txt[1024];
        snprintf(txt, sizeof(txt), "countdown=5\ndefault=0\n%s\n", fwline);
        g_bytes_read = 0; g_list_deep = 0;
        hostui_test_init(txt);
        CHECK(has("full firmware|ready|auto|SEL") && g_bytes_read == 0 && g_list_deep == 0);
        snprintf(txt, sizeof(txt), "countdown=5\ndefault=0\ncore=/Usb0/cores/gideon.cfw|u64_mk2_artix 2026/09/10 11:00:57||%u|%u|00000000\n", fsz, fmt + 1);
        hostui_test_init(txt);
        CHECK(has("full firmware|changed|auto|SEL") && hostui_test_cfg_n() == 1 && hostui_test_startable(0));
        // an older entry (no mtime) gets its size and mtime once; one that is not a usable firmware stays and says so
        g_saves = 0;
        hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/cores/gideon.cfw|G||0\ncore=/Usb0/cores/only100.cfw|only100.cfw||0\n");
        CHECK(g_saves == 1 && strstr(g_last_save, "core=/Usb0/cores/gideon.cfw|G||") && !strstr(g_last_save, "gideon.cfw|G||0|0|"));
        CHECK(has("G|full firmware|ready|auto|SEL") && has("only100.cfw||unusable"));
    }
    hostui_test_init(fwline[0] ? (snprintf(R, sizeof(R), "countdown=5\ndefault=0\n%s\ncore=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\n", fwline), R) : 0);
    int calls = g_legacy_calls;
    // RETURN on a valid one asks first: the way back is a power cycle, and nothing about an updater (Q2)
    kb_push('x');                                                                          // any other key cancels
    hostui_test_key(KEY_RETURN);
    CHECK(hostui_test_confirms() == 1 && g_legacy_calls == calls);                         // the control: asked, and nothing started
    CHECK(strstr(hostui_test_confirm(), "u64_mk2_artix 2026/09/10 11:00:57 replaces the host") &&
          strstr(hostui_test_confirm(), "switch the power off and on"));
    CHECK(!strcasestr(hostui_test_confirm(), "updater") && !strcasestr(hostui_test_confirm(), "flash"));
    hostui_test_confirm_panel(true);
    snprintf(p, sizeof(p), "%s/confirm_firmware.png", out); png(p, 2);
    hostui_test_confirm_panel(false);
    kb_push(KEY_RETURN);                                                                   // RETURN confirms
    hostui_test_key(KEY_RETURN);
    CHECK(hostui_test_confirms() == 2 && g_legacy_calls == calls + 1 && !strcmp(g_legacy_path, "/Usb0/cores/gideon.cfw"));
    // a core start does not ask
    hostui_test_key(KEY_DOWN);                                                             // row 1: the core
    int stage = g_stage_calls;
    hostui_test_key(KEY_RETURN);
    CHECK(g_stage_calls == stage + 1 && hostui_test_confirms() == 2);
    hostui_test_key(KEY_UP);
    // a countdown has nobody to ask and starts the image at once; a network request asks the same question as RETURN (D18/D19a)
    calls = g_legacy_calls;
    kb_push(KEY_RETURN);
    hostui_test_key(KEY_RETURN);
    CHECK(hostui_test_confirms() == 3 && g_legacy_calls == calls + 1);
    kb_push('x');                                                                          // no: the remote start asks and does not start
    hostui_test_web(WEB_BOOT, 0, 0, 0);
    CHECK(hostui_test_confirms() == 4 && g_legacy_calls == calls + 1);                     // the control: asked, nothing started
    kb_push(KEY_RETURN);                                                                   // yes: now it starts
    hostui_test_web(WEB_BOOT, 0, 0, 0);
    CHECK(hostui_test_confirms() == 5 && g_legacy_calls == calls + 2);
    // the browser: what the parser found, in the manifest panel; a refused file says why
    char rp[1024];
    hostui_test(1, 0, 1, false);                                                           // /Usb0/cores, row 1 = gideon.cfw
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=u64_mk2_artix 2026/09/10 11:00:57|Version=full firmware|Contract=none, it replaces the host|FPGA=7a50tfgg484|"));
    CHECK(strstr(rp, "Core=2192121 B bitstream|") == 0);                                   // the miniature is smaller; the label is what counts
    CHECK(strstr(rp, " B bitstream|") && strstr(rp, "B at 0x30000|") && strstr(rp, "map gideon_bl4|"));
    CHECK(strstr(rp, "shown=1"));
    snprintf(p, sizeof(p), "%s/browse_firmware.png", out); png(p, 2);
    hostui_test(1, 0, 3, false);                                                           // trx64_large.cfw
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=top 2026/10/10 18:23:21|") && strstr(rp, "map identity|"));
    hostui_test(1, 0, 2, false);                                                           // only100.cfw
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=Not a usable firmware image|") && strstr(rp, "err=no bitstream for this FPGA in the firmware image|") && strstr(rp, "shown=0"));
    (void)R;
    hostui_test_start_panel_full();
    snprintf(p, sizeof(p), "%s/start_firmware.png", out); png(p, 2);
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}

// T115 D7 S10: the logo of a .cfw row. The tree is mktree.py's third one (<root>_ic): Gideon's firmware with a sidecar of 2048 bytes
// and one without, a design nobody knows, the TRX64 firmware, and a TRX64 firmware whose sidecar is 2049 bytes. Each picture is
// searched in the frame pixel for pixel and counted, so a row with the wrong picture, a text tile or the wrong size taken cannot pass.
static bool read_logo(const char *rel, uint16_t *dst)
{
    char lp[700];
    snprintf(lp, sizeof(lp), "%s/%s", getenv("LOGO_DIR") ? getenv("LOGO_DIR") : ".", rel);
    FILE *f = fopen(lp, "rb");
    bool ok = f && fread(dst, 2, 1024, f) == 1024;
    if (f) fclose(f);
    return ok;
}
static void scenarios_ic(const char *out)
{
    char p[512], main_root[400];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_ic", main_root);
    static uint16_t gid[1024], trx[1024], gen[1024], side[1024];
    CHECK(read_logo("../tools/core_logos/u64/icon.rgb565", gid) && read_logo("../tools/core_logos/trx64/icon.rgb565", trx) && read_logo("../tools/core_logos/firmware/icon.rgb565", gen));
    snprintf(p, sizeof(p), "%s_ic/Usb0/cores/gideon.icon", main_root);
    FILE *f = fopen(p, "rb");
    CHECK(f && fread(side, 2, 1024, f) == 1024);
    if (f) fclose(f);
    hostui_test_init("countdown=5\ndefault=0\n"
                     "core=/Usb0/cores/gideon.cfw|u64_mk2_artix 2026/09/10 11:00:57||0\ncore=/Usb0/cores/gideon_plain.cfw|u64_mk2_artix 2026/09/10 11:00:57||0\n"
                     "core=/Usb0/cores/mystery.cfw|mystery_design 2026/10/01 10:00:00||0\ncore=/Usb0/cores/trx64.cfw|top 2026/10/10 18:23:21||0\n"
                     "core=/Usb0/cores/wrong.cfw|top 2026/10/10 18:23:21||0\n");
    CHECK(hostui_test_cfg_n() == 5);
    CHECK(has("|full firmware|ready|") && has("mystery_design 2026/10/01 10:00:00|full firmware"));
    CHECK(count_icon(gid) == 1);        // gideon_plain.cfw: the built-in logo of Gideon's firmware; gideon.cfw shows its sidecar instead
    CHECK(count_icon(side) == 1);       // gideon.cfw: the 2048-byte sidecar replaces it
    CHECK(count_icon(trx) == 2);        // trx64.cfw and wrong.cfw (its 2049-byte sidecar is ignored)
    CHECK(count_icon(gen) == 1);        // mystery.cfw: a design the host does not know gets the generic firmware logo
    snprintf(p, sizeof(p), "%s/menu_firmware_logos.png", out); png(p, 2);
    // a firmware in the boot menu shows the same logo (the entry's path is the .cfw)
    hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/cores/trx64.cfw|top 2026/10/10 18:23:21||0\ncore=/Usb0/cores/gideon.cfw|u64_mk2_artix 2026/09/10 11:00:57||0\n");
    CHECK(count_icon(trx) >= 1 && count_icon(side) >= 1);
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}


// T115 §7: a LARGE .cor is a row of the full-firmware kind. The tree is mktree.py's fourth (<root>_lc): amiga1200.cor (own icon), damaged.cor
// (a byte of the bitstream record flipped), big100t.cor (a 100T bitstream), and a layer-2 core. The package goes through the .cfw start path
// (legacy_stage), never through core_stage; the hashes are checked when it is added, and a start of the added entry does not hash again.
static void scenarios_lc(const char *out)
{
    // T115 section 7 with T101 S10: nothing is searched for, so a large .cor comes into the menu by hand like any bundle (F3, the
    // folder, RETURN). Browser rows of the <root>_lc tree: 0 "..", 1 amiga1200.cor, 2 big100t.cor, 3 damaged.cor, 4 vc20.cor.
    char p[512], main_root[400];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_lc", main_root);
    hostui_test_init(0);
    CHECK(hostui_test_cfg_n() == 0 && !has("amiga1200.cor"));                              // not found by itself
    // the one for another FPGA is refused with the parser's reason, nothing is added
    hostui_test_browse_keep("/Usb0/cores");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    g_saves = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0 && has("note='refused: no bitstream for this FPGA in the firmware image'"));
    // the damaged package when added, with the part named (the full hash runs here); nothing is added
    hostui_test_key(KEY_DOWN);
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0 && has("note='refused: bitstream: SHA-256 mismatch"));
    // S on the good one starts at once (no requester for our large cores, owner 2026-10-28), through legacy_stage with the hashes on
    hostui_test_key(KEY_UP); hostui_test_key(KEY_UP);
    int calls = g_legacy_calls, stage = g_stage_calls, conf = hostui_test_confirms();
    hostui_test_key('s');
    CHECK(hostui_test_confirms() == conf && g_legacy_calls == calls + 1 && g_stage_calls == stage);
    CHECK(!strcmp(g_legacy_path, "/Usb0/cores/amiga1200.cor") && g_legacy_verify);
    // RETURN adds it after its SHA-256 check; the entry keeps size and mtime, and a start of the entry does not hash again
    g_saves = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 1 && g_saves == 1 && strstr(g_last_save, "core=/Usb0/cores/amiga1200.cor|Amiga 1200 (AGA)|"));
    hostui_test_view(0);
    CHECK(has("Amiga 1200 (AGA)|full firmware|ready|auto|SEL"));
    calls = g_legacy_calls;
    hostui_test_key(KEY_RETURN);                                                           // RETURN on the large .cor row: no dialog, started
    CHECK(hostui_test_confirms() == conf && g_legacy_calls == calls + 1 && !g_legacy_verify && g_stage_calls == stage);
    calls = g_legacy_calls;
    hostui_test_web(WEB_BOOT, 0, 0, 0);
    CHECK(g_legacy_calls == calls + 1 && !g_legacy_verify && g_stage_calls == stage);
    snprintf(p, sizeof(p), "%s/menu_large_cor.png", out); png(p, 2);
    // the browser: the manifest panel is the full-firmware one, with the map the bitstream decided
    char rp[1024];
    hostui_test_browse_keep("/Usb0/cores");
    hostui_test_key(KEY_DOWN);
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=Amiga 1200 (AGA)|Version=full firmware|") && strstr(rp, "map identity|") && strstr(rp, "shown=1"));
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}


// T111: the File Browser (F3, was "Add core"): the media level with the format, A for the types the boot menu takes, every file listed.
// The format itself is faked here (media_format above); what is under test is the UI around it: what is asked, in what order, and that nothing starts
// without the whole act. The real format runs in tests/hostapp/test_exfatfmt.cc.
extern int hostui_test_fm_page(char *out, int max);
extern int hostui_test_fmt_runs(void);
extern bool hostui_test_cd_running(void);
extern bool hostui_test_fmt_active(void);
extern void hostui_test_browse_media(void);
extern void hostui_test_fm_keep(bool on);
extern const char *hostui_test_nav_name(int i);
extern const char *hostui_test_set_name(int k);
extern void hostui_test_pump(void);
extern int hostui_test_browse_rows(char *out, int max);
static void scenarios_media(const char *out)
{
    char p[512], rp[1024], pg[2048], rows[2048];
    g_fmt_out = out;
    hostui_test_init(CFG3);
    CHECK(!strcmp(hostui_test_nav_name(1), "File Browser"));
    // ---- the media level -------------------------------------------------------------------------------------------------------------------
    hostui_test_browse_media();
    hostui_test_browse_rows(rows, sizeof(rows));
    CHECK(strstr(rows, ">Flash|") && strstr(rows, ">SD|") && strstr(rows, ">Usb0|"));
    CHECK(has("foot='UP/DOWN select    RETURN open    F format as exFAT    V picture'"));
    hostui_test_browse_right(rp, sizeof(rp));                                        // row 0: Flash
    CHECK(strstr(rp, "title=Flash|Size=14.0 MiB|Contents=FAT12|Boot menu=no boot menu row lives here|") && strstr(rp, "Format=never (internal drive)|"));
    g_fmt_calls = 0;
    hostui_test_key('F');                                                            // the internal drive is never formatted from here
    CHECK(g_fmt_calls == 0 && !hostui_test_fmt_active() && has("note='Flash is the internal drive and is never formatted from here'"));
    hostui_test_key(KEY_DOWN);                                                       // SD
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=SD|Size=29.7 GiB|Contents=FAT32  'SDCARD'|Boot menu=1 boot menu row lives here|") && strstr(rp, "Format=one exFAT partition|"));
    CHECK(strstr(rp, "msg=F formats this medium as exFAT\nRETURN opens it|"));
    hostui_test_key(KEY_DOWN);                                                       // Usb0
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=Usb0|Size=14.4 GiB|Contents=FAT32  'STICK'|Boot menu=2 boot menu rows live here|"));
    CHECK(!strstr(rp, "..."));                                                       // every value fits the column: nothing is cut to dots
    hostui_test_key(KEY_UP); hostui_test_browse_right(rp, sizeof(rp)); CHECK(!strstr(rp, "..."));    // SD
    hostui_test_key(KEY_UP); hostui_test_browse_right(rp, sizeof(rp)); CHECK(!strstr(rp, "..."));    // Flash
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);                                              // Usb0 again
    snprintf(p, sizeof(p), "%s/browse_media.png", out); png(p, 2);
    // ---- S2 at the UI: every way out of the act before the second RETURN writes nothing --------------------------------------------------------
    g_key_gap_ms = 300;
    int stage0 = g_stage_calls, legacy0 = g_legacy_calls;
    hostui_test_fm_keep(true);
    kb_type("Usb1"); kb_push(KEY_RETURN); kb_push(KEY_ESCAPE);                       // the wrong name, RETURN, ESC
    hostui_test_key('F');
    CHECK(g_fmt_calls == 0 && kbn == 0 && !hostui_test_fmt_active());
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Format Usb0 as exFAT?|border=") && strstr(pg, "l0=ALL DATA ON Usb0 WILL BE ERASED.|"));
    CHECK(strstr(pg, "l1=14.4 GiB  -  FAT32  'STICK'|") && strstr(pg, "l2=2 boot menu rows live here; they show 'missing' until their files are copied back.|"));
    CHECK(strstr(pg, "input=1:Usb1_|msg=That is not the name of the medium. Nothing was written.|"));
    CHECK(strstr(pg, "l5=To go on, type the name of the medium (Usb0) and press RETURN:|"));
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/format_warning.png", out); png(p, 2);
    CHECK(has("note='format of Usb0 cancelled, nothing was written'"));
    kb_type("Usb"); kb_push(KEY_RETURN); kb_push(KEY_RETURN); kb_push(KEY_ESCAPE);    // one character short, twice
    hostui_test_key('F');
    CHECK(g_fmt_calls == 0 && kbn == 0);
    kb_type("usb0"); kb_push(KEY_RETURN); kb_type("x");                              // the right name (the case does not matter), then another key
    hostui_test_key('F');
    CHECK(g_fmt_calls == 0 && kbn == 0);
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "l4=The name is right. This is the last question.|") && strstr(pg, "l5=Press RETURN once more within 10 s to erase Usb0.|"));
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/format_last_question.png", out); png(p, 2);
    kb_type("Usb0"); kb_push(KEY_RETURN); kb_push(KEY_ESCAPE);                       // armed, ESC is "any other key"
    hostui_test_key('F');
    CHECK(g_fmt_calls == 0 && kbn == 0);
    g_key_gap_ms = 0;
    kb_type("Usb0"); kb_push(KEY_RETURN); kb_push(KEY_RETURN); kb_push(KEY_ESCAPE);  // both RETURNs at once: too soon, the second is not one
    hostui_test_key('F');
    CHECK(g_fmt_calls == 0 && kbn == 0);
    CHECK(g_stage_calls == stage0 && hostui_test_fmt_runs() == 0);
    // a refused medium: no page at all
    g_med[1].refuse = "write protected";
    hostui_test_key(KEY_UP);                                                         // SD
    hostui_test_browse_right(rp, sizeof(rp));
    printf("media panel (SD, write protected): %s\n", rp);
    CHECK(strstr(rp, "Format=write protected|") && strstr(rp, "msg=RETURN opens it|"));
    kb_push(KEY_ESCAPE);
    hostui_test_key('F');
    CHECK(g_fmt_calls == 0 && has("note='cannot format SD: write protected'") && kbn == 1);
    kbn = 0;
    g_med[1].refuse = 0;
    hostui_test_key(KEY_DOWN);                                                       // Usb0
    // ---- the countdown and the network meanwhile -------------------------------------------------------------------------------------------
    hostui_test_countdown();                                                         // a countdown running on the menu
    {
        WebCmd c;
        memset(&c, 0, sizeof(c));
        c.kind = WEB_BOOT;
        c.a = 0;
        web_queue_for_test(&c);                                                      // and somebody asks for a start over the network
    }
    kb_type("Us"); kb_push(KEY_ESCAPE);
    hostui_test_key('F');
    CHECK(!hostui_test_cd_running());                                                // the key that opened the act stopped the countdown
    CHECK(strstr(web_note(), "busy") != 0);                                          // the request was answered, not kept
    hostui_test_pump();
    CHECK(g_stage_calls == stage0 && g_legacy_calls == legacy0 && g_fmt_calls == 0);
    // ---- the act: name, RETURN, RETURN a second later -> the format runs, the result page, the note ----------------------------------------------------
    g_key_gap_ms = 1100;
    g_fmt_shot = "format_progress.png";
    g_ovp_fmt = true; g_ovp_n = g_ovp_bad = g_ovp_states = g_ovp_visits = 0; ovp::run_reset(); g_ovp_out = out;
    kb_type("Usb0"); kb_push(KEY_RETURN); kb_push(KEY_RETURN);
    hostui_test_key('F');
    g_ovp_fmt = false;
    g_fmt_shot = 0;
    {   // the worst moment of the run as a picture (the page is gone by now: the browser underneath is what the pixels are pasted onto)
        ovp::Pic saved = ovp::paste_worst(fb);
        char pp[600]; snprintf(pp, sizeof(pp), "%s/overlay_format_worst.png", out); png(pp, 2);
        ovp::paste_back(fb, saved);
    }
    // T167 S2: the format page changes (step text, bytes, percent, bar) over the File Browser; no pixel of the browser shows inside it
    printf("overlay format: %d reports, %d states, %d visits, %d foreign pixels\n", g_ovp_n, g_ovp_states, g_ovp_visits, g_ovp_bad);
    CHECK(g_ovp_n >= 10 && g_ovp_visits >= g_ovp_n && g_ovp_states >= g_ovp_n);
    CHECK(g_ovp_bad == 0);
    g_key_gap_ms = 0;
    CHECK(g_fmt_calls == 1 && !strcmp(g_fmt_name, "Usb0") && kbn == 0 && hostui_test_fmt_runs() == 1);
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Usb0 is formatted|") && strstr(pg, "l0=14.4 GiB  -  exFAT|") && strstr(pg, "hint=any key returns to the File Browser|"));
    CHECK(strstr(pg, "l1=The host has mounted it: 13.9 GiB free, no files yet.|") && strstr(pg, "l2=Boot menu: 2 rows point here, 0 found. Copy the cores back and they turn ready.|"));
    CHECK(has("note='Usb0 formatted as exFAT'"));
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/format_done.png", out); png(p, 2);
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "Contents=exFAT|"));                                            // the browser shows what is on the medium now
    // ---- a cancel while writing: the key is honoured, the view says the medium is blank, and it can be formatted again ----------------------------------
    g_key_gap_ms = 1100;
    g_fmt_cancel_at_call = 4;
    kb_type("Usb0"); kb_push(KEY_RETURN); kb_push(KEY_RETURN);
    hostui_test_key('F');
    g_fmt_cancel_at_call = -1;
    g_key_gap_ms = 0;
    CHECK(g_fmt_calls == 2 && kbn == 0);
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Format of Usb0 cancelled|") && strstr(pg, "l0=cancelled; the medium is blank|") && strstr(pg, "l1=The medium is blank now: no partition table, no volume.|"));
    CHECK(strstr(pg, "l2=Press F in the File Browser to format it again.|") && has("note='format of Usb0: cancelled'"));
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/format_cancelled.png", out); png(p, 2);
    hostui_test_browse_right(rp, sizeof(rp));
    printf("media panel (Usb0 after the cancel): %s\n", rp);
    CHECK(strstr(rp, "Contents=blank|"));
    CHECK(!strstr(rp, "..."));
    g_key_gap_ms = 1100;                                                             // and the same medium can be formatted again
    kb_type("Usb0"); kb_push(KEY_RETURN); kb_push(KEY_RETURN);
    hostui_test_key('F');
    g_key_gap_ms = 0;
    CHECK(g_fmt_calls == 3 && kbn == 0 && has("note='Usb0 formatted as exFAT'"));
    hostui_test_fm_keep(false);
    // ---- A and RETURN: files of every kind are listed, only the types the boot menu takes can be added ------------------------------------------------
    hostui_test_init(0);
    hostui_test_browse_keep("/SD");                                                  // rows: .. / cores / readme.txt
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_browse_rows(rows, sizeof(rows));
    CHECK(strstr(rows, ">cores|") && strstr(rows, "*readme.txt|1 KiB\n"));
    CHECK(has("foot='UP/DOWN select    RETURN open folder    C copy    M move    DEL twice deletes    LEFT up a folder    V picture'"));
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=readme.txt|") && strstr(rp, "Not a boot menu file") && strstr(rp, "shown=0"));
    g_saves = 0;
    hostui_test_key('a'); hostui_test_key('A'); hostui_test_key(KEY_RETURN); hostui_test_key('s');
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0 && has("note=''") && g_stage_calls == stage0);   // nothing is offered, nothing happens
    hostui_test_browse_keep("/SD/cores");                                            // rows: .. / trx64.cor
    hostui_test_key(KEY_DOWN);
    CHECK(has("foot='UP/DOWN select    RETURN open    A add    S start    C copy    M move    DEL twice deletes    LEFT up    V picture'"));
    hostui_test_key(KEY_RETURN);                                                     // RETURN no longer adds
    CHECK(hostui_test_cfg_n() == 0 && g_saves == 0);
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 1 && g_saves == 1);
    // a file of more than 4 GiB: its size is its own, not the saturated 4.0 GiB
    hostui_test_browse_keep("/Usb0/images");
    hostui_test_browse_rows(rows, sizeof(rows));
    CHECK(strstr(rows, "*MegaAGS.hdf|8.3 GiB\n") && strstr(rows, "*notes.txt|1 KiB\n"));
    hostui_test_key(KEY_DOWN);
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=MegaAGS.hdf|") && strstr(rp, "err=8.3 GiB\nNot a boot menu file"));
    snprintf(p, sizeof(p), "%s/browse_files.png", out); png(p, 2);
    // the footers fit
    CHECK(hostui_test_foot_overflow() == 0);
    // ---- the media level follows the media: one that is gone is not formatted ----------------------------------------------------------------------------
    hostui_test_browse_media();
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    g_med[2].ready = false;
    int calls0 = g_fmt_calls;
    hostui_test_key('F');
    CHECK(g_fmt_calls == calls0 && has("note='no medium in Usb0'"));
    g_med[2].ready = true;
}

// T101 areas (Alex, 2026-10-22): every key and every timer redraws only what changed. LVGL draws about 2.2 ms per 1,000 px
// on the board (direct into DDR2, no data cache), so the invalidated pixels are the time. One line per case:
//   AREA <case> <px> <limit>       (tests/hostapp/render/run.sh prints them as the before/after table)
static int g_area_n;
static void area(const char *name, uint32_t px, uint32_t limit)
{
    g_area_n++;
    printf("AREA %-34s %7u px  <= %7u  (~%.0f ms, %d areas)%s\n", name, px, limit, px * 2.2 / 1000.0, hostui_test_last_areas(), px <= limit ? "" : "  FAIL");
    CHECK(px <= limit);
    // and the smaller area must be enough: what the layer shows equals a redraw of everything (a label cut to its text
    // that leaves the old, longer text behind, a mark that stays, shows here)
    static uint16_t shown[960 * 540];
    memcpy(shown, fb, sizeof(shown));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    size_t diff = 0;
    for (size_t k = 0; k < 960 * 540; k++) diff += shown[k] != fb[k];
    if (diff) printf("AREA %s: the shown buffer differs from a full redraw in %zu px\n", name, diff);
    CHECK(diff == 0);
}

static void areas(void)
{
    const uint32_t CW_ = 960 - 170 - 40;                      // hostui.cc geometry: content width
    const uint32_t MROW = CW_ * 38, EROW = CW_ * 56;           // a menu row, an editor row
    const uint32_t VIEW = (960 - 170) * (540 - 40 - 30);       // the content container of a view
    // The selection mark of a row: a 2 px frame and a 5 px bar on the left, at most 4 strips of the row's width and 8 of its
    // height. A selection move redraws the old and the new mark and nothing else.
    const uint32_t MMARK = 4 * CW_ + 8 * 38, EMARK = 4 * CW_ + 8 * 56;   // the boot menu row 38 high (T209 D6), the editor row 56 (T209 D8)
    const char *eight =
        "countdown=5\ndefault=0\n"
        "core=/Usb0/many/m1.cor|Core number 1||0\ncore=/Usb0/many/m2.cor|Core number 2||0\ncore=/Usb0/many/m3.cor|Core number 3||0\n"
        "core=/Usb0/many/m4.cor|Core number 4||0\ncore=/Usb0/many/m5.cor|Core number 5||0\ncore=/Usb0/many/m6.cor|Core number 6||0\n"
        "core=/Usb0/many/m7.cor|Core number 7||0\ncore=/Usb0/many/m8.cor|Core number 8||0\ncore=/Usb0/many/m9.cor|Core number 9||0\n";
    // The invalid-area buffer of this LVGL build (lv_conf.h): the Add core scroll above reaches 29 of LVGL's default 32, a list with
    // a longer manifest overflows it into the whole screen (518,400 px). 128 is the headroom; the number is read from the display struct.
    printf("AREA invalid-area slots in this build: %d\n", hostui_test_inv_slots());
    CHECK(hostui_test_inv_slots() >= 128);
    // F1 boot menu: 9 entries, a window of 5
    hostui_test_init(eight);
    area("F1 menu: selection move", hostui_test_key(KEY_DOWN), 2 * MMARK);
    area("F1 menu: selection move up", hostui_test_key(KEY_UP), 2 * MMARK);
    for (int i = 0; i < 4; i++) hostui_test_key(KEY_DOWN);                 // row 4, the last of the window
    area("F1 menu: page change down", hostui_test_key(KEY_DOWN), 5 * MROW);
    area("F1 menu: scroll one more", hostui_test_key(KEY_DOWN), 5 * MROW);
    for (int i = 0; i < 4; i++) hostui_test_key(KEY_UP);                   // sel 2 = the top row of the window
    area("F1 menu: page change up", hostui_test_key(KEY_UP), 5 * MROW);
    hostui_test_init(eight);
    area("F1 menu: UP on the first row", hostui_test_key(KEY_UP), 0);
    {   // T135 D12: with direct boot on the menu shows neither the countdown text nor its bar; off, both; the toggle in F8 brings them back
        char t[96]; int l, b;
        std::string on = std::string("direct=1\n") + eight, off = std::string("direct=0\n") + eight;
        hostui_test_init(on.c_str());
        hostui_test_cd_tick();
        hostui_test_cd_vis(&l, &b, t, sizeof(t));
        printf("D12 direct=1: label %d bar %d\n", l, b);
        CHECK(l == 0 && b == 0);
        hostui_test_init(off.c_str());
        hostui_test_cd_tick();
        hostui_test_cd_vis(&l, &b, t, sizeof(t));
        printf("D12 direct=0: label %d bar %d text \"%s\"\n", l, b, t);
        CHECK(l == 1 && b == 1 && strstr(t, "Starting Core number 1 in") != 0);
        hostui_test_init(on.c_str());              // F8, the first row is Direct boot: RIGHT turns it off, F1 back to the menu
        hostui_test_key(KEY_F8);
        hostui_test_key(KEY_RIGHT);
        hostui_test_key(KEY_F1);
        hostui_test_cd_tick();
        hostui_test_cd_vis(&l, &b, t, sizeof(t));
        printf("D12 direct switched off in F8: label %d bar %d text \"%s\"\n", l, b, t);
        CHECK(l == 1 && b == 1 && t[0]);
        hostui_test_key(KEY_F8);
        hostui_test_key(KEY_RIGHT);               // and on again: hidden again
        hostui_test_key(KEY_F1);
        hostui_test_cd_vis(&l, &b, t, sizeof(t));
        CHECK(l == 0 && b == 0);
        hostui_test_init(eight);
    }
    { std::string off = std::string("direct=0\n") + eight; hostui_test_init(off.c_str()); }
    area("F1 menu: countdown second", hostui_test_cd_tick(), 10u * 1000);   // label + bar of the countdown, not the 750 px strip
    area("F1 menu: countdown second 2", hostui_test_cd_tick(), 10u * 1000);
    area("F1 menu: chips (network up)", hostui_test_chips(true), 640u * 40);
    area("F1 menu: chips (network down)", hostui_test_chips(false), 640u * 40);
    // F5 editor
    hostui_test_init(eight);
    hostui_test_key(KEY_F5);
    area("F5 edit: selection move", hostui_test_key(KEY_DOWN), 2 * EMARK);
    for (int i = 0; i < 3; i++) hostui_test_key(KEY_DOWN);
    area("F5 edit: page change", hostui_test_key(KEY_DOWN), 5 * EROW);
    area("F5 edit: D (default)", hostui_test_key('d'), 3 * EROW);
    area("F5 edit: RIGHT (default config; a core without configs says so)", hostui_test_key(KEY_RIGHT), 19000);   // T198 D13: the note line is drawn
    // F3 browser, a folder of 19
    hostui_test_browse_at("/Usb0/scroll");
    const uint32_t BLW = CW_ / 2 - 8, BPH = 540 - 40 - 30 - 90;
    hostui_test_key(KEY_DOWN);                                              // the first key of a fresh view refills the manifest panel
    area("F3 browse: selection move", hostui_test_key(KEY_DOWN), 2 * (4 * BLW + 8 * 24) + 6u * 1000 + 19000);   // + the T198 note of the F5 RIGHT before it, cleared by this key
    for (int i = 0; i < 10; i++) hostui_test_key(KEY_DOWN);
    area("F3 browse: scroll one row (13 rows)", hostui_test_key(KEY_DOWN), BLW * BPH);
    area("F3 browse: last row (.cfw)", hostui_test_key(KEY_DOWN), BLW * BPH);
    // 20 bundles of different sizes: a scrolling key changes 13 names, 13 sizes and the manifest panel (about 40 labels)
    hostui_test_browse_at("/Usb0/sized");
    for (int i = 0; i < 12; i++) hostui_test_key(KEY_DOWN);
    area("F3 browse: scroll, 40 labels change", hostui_test_key(KEY_DOWN), BLW * BPH);
    area("F3 browse: scroll again", hostui_test_key(KEY_DOWN), BLW * BPH);
    // view switches: the new view's content once (the container is shown, the old one hidden), nothing besides the two
    // navigation entries and the footer line
    hostui_test_init(eight);
    area("F3 switch from the menu", hostui_test_key(KEY_F3), VIEW + 35000);
    area("F5 switch", hostui_test_key(KEY_F5), VIEW + 36000);   // T198: the Default config row
    area("F7 switch", hostui_test_key(KEY_F7), VIEW + 35000);
    area("F8 switch", hostui_test_key(KEY_F8), VIEW + 35000);
    hostui_test_key(KEY_F7);
    area("F7 net: chips (network up)", hostui_test_chips(true), 640u * 40 + 6000);
    area("F1 switch", hostui_test_key(KEY_F1), VIEW + 35000);
    area("F1 menu: R (full check of the entry)", hostui_test_key('r'), 12000);   // two notes and the row's badge and picture
}

// T101 S9: the start-up. The splash is on the layer after the first step, a step changes only the bar and its label, and the
// menu that replaces it is the same as a full redraw. Nothing lists the media (T101 S10).
static uint16_t sp_prev[960 * 540];
static int sp_steps, sp_first_content;
static unsigned sp_max_change, sp_min_change = ~0u;
static const char *sp_out;
static int sp_pl[4];
static void sp_probe(int step)
{
    unsigned diff = 0;
    for (size_t k = 0; k < 960 * 540; k++) diff += sp_prev[k] != fb[k];
    printf("SPLASH step %d: %u px changed since the last frame\n", step, diff);
    if (step == 0) { sp_pl[0] = g_pl_x; sp_pl[1] = g_pl_y; sp_pl[2] = g_pl_w; sp_pl[3] = g_pl_h; }       // T164 boot720: the place when the splash is first drawn
    if (step == 0) sp_first_content = (int)diff;
    else { if (diff > sp_max_change) sp_max_change = diff; if (diff < sp_min_change) sp_min_change = diff; }
    memcpy(sp_prev, fb, sizeof(sp_prev));
    char pp[600]; snprintf(pp, sizeof(pp), "%s/splash_%d.png", sp_out, step); png(pp, 2);
    sp_steps++;
}
// The lazy reader after the first frame: one phase of one entry per pass, never in a pass that handled a key or follows one closely.
static bool row_known(int n)      // row n of the dump has a state other than the placeholder
{
    char key[16]; snprintf(key, sizeof(key), "row %d|", n);
    const char *r = strstr(dump(), key);
    if (!r) return false;
    const char *e = strchr(r, '\n');
    const char *q = strstr(r, "|?|");
    return !(q && (!e || q < e));
}
static void lazy_phase_check(void)
{
    unsigned t = 100000;                                               // ms; the last key is far back
    g_opens = g_stats = 0;
    CHECK(!hostui_test_lazy_pass(1, t, t));                            // a pass that handled a key reads nothing
    CHECK(!hostui_test_lazy_pass(0, t + 100, t));                      // 100 ms after a key: nothing yet
    CHECK(g_opens == 0 && g_stats == 0 && g_bytes_read == 0);
    int steps = 0, max_open = 0, max_stat = 0;
    unsigned long max_bytes = 0;
    bool first_is_default = false;
    while (hostui_test_lazy_busy()) {                                  // the entries; migrations come after, on a longer quiet
        g_opens = g_stats = 0; g_bytes_read = 0;
        CHECK(hostui_test_lazy_pass(0, t + 250, t));                   // 250 ms of quiet: exactly one step
        steps++;
        if (g_opens > max_open) max_open = g_opens;
        if (g_stats > max_stat) max_stat = g_stats;
        if (g_bytes_read > max_bytes) max_bytes = g_bytes_read;
        if (steps == 1) first_is_default = row_known(0);
        if (steps > 100) break;
    }
    printf("LAZY: %d steps, at most %d open, %d stat, %lu bytes per step\n", steps, max_open, max_stat, max_bytes);
    CHECK(steps >= 13 && steps <= 26);                                 // state + icon of each entry, at most two phases
    CHECK(max_open <= 2 && max_stat <= 1);                             // a phase touches one file (is_full and cor_probe each open it), one stat
    CHECK(max_bytes < 6000);                                           // header, manifest, a 2 KB icon: no part
    CHECK(first_is_default);                                           // the default entry is read before the others
    CHECK(!has("|?|"));                                                // every entry has its state now
    CHECK(g_saves == 0);                                               // no migration yet (a longer quiet comes first)
    g_saves = 0;
    while (hostui_test_lazy_pass(0, t + 2000, t)) {                    // 2 s of quiet: one old entry migrated per step
    }
    printf("LAZY: migration saved boot.cfg %d time(s)\n", g_saves);
    CHECK(g_saves >= 1);
}

static void splash_startup(const char *out)
{
    sp_out = out;
    g_list_deep = 0;
    g_opens = g_stats = g_loads = 0; g_bytes_read = 0; g_saves = 0;
    // T164 boot720: the stored picture is 720p, the direct phase put it up (hv_start, as direct_phase does) and left for the menu; the first
    // placement of the pixel layer was the 1080p one. The start must place the menu for the mode that is up: 960 x 540 at factor 1, centred in 1280 x 720.
    g_saved_hdmi = VID_HDMI_720P; g_saved_sys = VID_SYS_PAL50;
    hostui_test_video(VID_SYS_PAL50, VID_HDMI_720P);
    hostui_test_direct_video(true);
    g_pl_x = g_pl_y = g_pl_w = g_pl_h = -1;
    hostui_test_startup(sp_probe);
    hostui_test_direct_video(false);
    CHECK(g_pl_x == 160 && g_pl_y == 90 && g_pl_w == 960 && g_pl_h == 540);
    CHECK(sp_pl[0] == 160 && sp_pl[1] == 90 && sp_pl[2] == 960 && sp_pl[3] == 540);      // ... and it was already there when the splash was drawn
    g_saved_hdmi = -1; g_saved_sys = 0;
    CHECK(sp_steps == 3);
    // T101 S10 (Alex, 2026-10-26): the menu comes up from boot.cfg alone. 13 entries in the file, and before the first frame not one
    // of them is opened, stat'ed or read; boot.cfg is the only file loaded, and an old entry is not migrated (nothing saved).
    CHECK(hostui_test_cfg_n() == 13);
    CHECK(g_opens == 0 && g_stats == 0 && g_bytes_read == 0 && g_saves == 0);
    CHECK(g_loads == 1 && !strcmp(g_load_path, "/Flash/boot.cfg"));
    printf("START: files opened %d, stat'ed %d, bytes %lu, boot.cfg loads %d\n", g_opens, g_stats, g_bytes_read, g_loads);
    CHECK(has("|?|") && !has("hash ok") && !has("|changed|"));          // every row shows the placeholder state
    CHECK(hostui_test_startable(0));                                  // ... and the default can be started (the start verifies itself)
    lazy_phase_check();
    CHECK(sp_first_content > 20000);                    // the first frame is the whole splash (name, version, bar track)
    CHECK(sp_min_change > 0);                           // every step moves the bar and the label
    CHECK(sp_max_change < 12000);                       // ... and nothing but those (a full screen is 518,400)
    char pp[600]; snprintf(pp, sizeof(pp), "%s/splash_menu.png", out); png(pp, 2);
    CHECK(g_list_deep == 0);                            // the start lists no folder: nothing is searched for
    static uint16_t before[960 * 540];
    memcpy(before, fb, sizeof(before));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    size_t d = 0;
    for (size_t k = 0; k < (size_t)hostui_test_canvas_w() * hostui_test_canvas_h(); k++) d += before[k] != fb[k];     // the canvas of the mode the start found (T164 D10a)
    printf("SPLASH first menu frame vs a full redraw: %zu px differ\n", d);
    CHECK(d == 0);                                      // the splash left nothing on the layer
}


// ---- T139: delete, copy and move in the browser, the rename in the edit view, the settings page. The file operations run on the bench's
// real tree (RealFo above), under /Usb0/t139 and /SD/t139, which this scenario makes and removes again.
extern int hostui_test_settings(char *out, int max);
extern bool hostui_test_pick(void);
extern int hostui_test_sel(void);
extern void hostui_test_rename_panel(bool show, const char *name, int cur, const char *msg);
static int hostui_test_sel_row(void) { return hostui_test_sel(); }
extern void hostui_test_video(int sys, int hdmi);
static void mkd(const char *p) { char d[700]; snprintf(d, sizeof(d), "%s%s", g_root, p); mkdir(d, 0755); }
static void put(const char *p, size_t n, int seed)
{
    char d[700]; snprintf(d, sizeof(d), "%s%s", g_root, p);
    FILE *f = fopen(d, "wb");
    CHECK(f);                                       // a missing folder is a red check, not a crash on the null file
    if (!f) return;
    for (size_t i = 0; i < n; i++) fputc((int)((i * 31 + seed + (i >> 9)) & 0xFF), f);
    fclose(f);
}
static long fsize(const char *p)
{
    char d[700]; snprintf(d, sizeof(d), "%s%s", g_root, p);
    struct stat s;
    return stat(d, &s) ? -1 : (long)s.st_size;
}
static bool same_as(const char *p, size_t n, int seed)           // the file holds exactly put(p, n, seed)
{
    char d[700]; snprintf(d, sizeof(d), "%s%s", g_root, p);
    FILE *f = fopen(d, "rb");
    if (!f) return false;
    bool ok = true;
    for (size_t i = 0; i < n && ok; i++) ok = fgetc(f) == (int)((i * 31 + seed + (i >> 9)) & 0xFF);
    ok = ok && fgetc(f) == EOF;
    fclose(f);
    return ok;
}
// T192 D21: a destructive row action asked over the API shows on the screen exactly what the first local key shows, and the keys at the board answer it.
extern void hostui_test_api_init(void);
extern void hostui_test_ui_task(bool on);
extern const char *hostui_test_net_text(int which);
extern int hostui_test_api_pass(void);
extern void hostui_test_board_key(int k);
static UicReply api_js(const char *js)
{
    UicIntent in;
    UicReply r;
    memset(&r, 0, sizeof(r));
    if (uic_intent_parse(js, (int)strlen(js), &in)) uic_submit(hostapi_service(), in, &r);
    else printf("api_js: parse failed\n");
    if (r.r != UR_ASKED) printf("api_js: %s -> %s (%s)\n", js, uic_r_name(r.r), r.why);
    return r;
}
static UicReply api_answer(const char *choice)
{
    UicIntent an;
    UicReply r;
    memset(&an, 0, sizeof(an));
    memset(&r, 0, sizeof(r));
    an.kind = UD_ANSWER;
    snprintf(an.choice, sizeof(an.choice), "%s", choice);
    uic_submit(hostapi_service(), an, &r);
    return r;
}
// T192 D21 review: the screen owns the arm, so nothing the API does while a question is open lets a yes act on another page, an arm that bails raises no question, and no
// key is queued to take an arm away.
static UicReply api_try(const char *js)
{
    UicIntent in;
    UicReply r;
    memset(&r, 0, sizeof(r));
    if (uic_intent_parse(js, (int)strlen(js), &in)) uic_submit(hostapi_service(), in, &r);
    return r;
}
static bool open_question(void) { return hostapi_service()->q_open; }
static void api_asked_review(void)
{
    // 1. browse.delete asked, the API opens the Edit page, yes: nothing is removed from boot.cfg and no file is deleted; the question is gone
    mkd("/Usb0/t192");
    put("/Usb0/t192/g.bin", 100, 2);
    hostui_test_init(CFG3);
    hostui_test_browse_keep("/Usb0/t192");
    hostui_test_key(KEY_DOWN);
    g_saves = 0;
    UicReply r = api_try("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#1\"}");
    CHECK(r.r == UR_ASKED && open_question());
    hostui_test_api_pass();
    CHECK(has("DEL again deletes g.bin."));
    CHECK(api_try("{\"do\":\"key\",\"key\":\"del\"}").r == UR_BLOCKED);                      // a remote key does not slip past the question
    CHECK(api_try("{\"do\":\"open\",\"target\":\"edit\"}").r == UR_TAKEN);
    hostui_test_api_pass();
    CHECK(!open_question());                                                                  // the arm went with the page: the question closed by itself
    CHECK(api_answer("yes").r == UR_GONE);
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0 && fsize("/Usb0/t192/g.bin") == 100);
    // the reverse: entry.remove asked, the API goes to the browser, yes: the file under the cursor is not deleted
    hostui_test_view(2);
    r = api_try("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(api_try("{\"do\":\"open\",\"target\":\"browse\"}").r == UR_TAKEN);
    hostui_test_api_pass();
    CHECK(!open_question() && api_answer("yes").r == UR_GONE);
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0 && fsize("/Usb0/t192/g.bin") == 100);
    // 2. a second ask while one is open: busy, the screen does not move (same view, same note)
    hostui_test_view(2);
    r = api_try("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(has("DEL again to remove TRX64 Ultimate (C64)"));
    CHECK(api_try("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#1\"}").r == UR_BUSY);
    CHECK(api_try("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#1\"}").r == UR_BUSY);
    hostui_test_api_pass();
    CHECK(has("DEL again to remove TRX64 Ultimate (C64)") && !has("DEL again deletes"));
    // no leaves no key behind (a rename field the user opens next is not cleared, a Cores page ASK is not cancelled), and the note is gone
    CHECK(api_answer("no").r == UR_TAKEN);
    CHECK(hostapi_pop_key() < 0);
    hostui_test_api_pass();
    CHECK(!has("DEL again to remove") && hostui_test_cfg_n() == 3);
    // the 60 s timeout is the no: the same
    r = api_try("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    for (int i = 0; i < 1500 && open_question(); i++) {
        hostui_test_api_pass();
    }
    CHECK(!open_question() && hostapi_pop_key() < 0);
    hostui_test_api_pass();
    CHECK(!has("DEL again to remove") && hostui_test_cfg_n() == 3 && g_saves == 0);
    // local key race: a key at the board takes the arm (other key = no); the API's yes then finds nothing
    r = api_try("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    hostui_test_board_key(KEY_UP);
    CHECK(!open_question() && api_answer("yes").r == UR_GONE);
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0);
    // 3. the arm bails: '..', a folder with files - refused with the screen's words, no question open, no key queued
    mkd("/Usb0/t192/full");
    put("/Usb0/t192/full/x.bin", 10, 3);
    hostui_test_init(CFG3);
    hostui_test_browse_keep("/Usb0/t192");
    r = api_try("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#0\"}");                // row 0 is '..'
    CHECK(r.r == UR_REFUSED && r.why[0] && !open_question() && hostapi_pop_key() < 0);
    hostui_test_api_pass();
    int dir_row = -1;
    for (int i = 1; i < 5; i++) {
        char js[120];
        snprintf(js, sizeof(js), "{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#%d\"}", i);
        r = api_try(js);
        if (r.r == UR_REFUSED && strstr(r.why, "not empty")) dir_row = i;
        if (r.r == UR_ASKED) {
            api_answer("no");
            hostui_test_api_pass();
        }
    }
    CHECK(dir_row >= 0 && !open_question() && hostapi_pop_key() < 0);
    {
        char js[120];
        snprintf(js, sizeof(js), "{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#%d\"}", dir_row);
        CHECK(api_try(js).r == UR_REFUSED);
        hostui_test_api_pass();
        CHECK(has("note='The folder is not empty: only empty folders are deleted'") && fsize("/Usb0/t192/full/x.bin") == 10);
    }
    r = api_try("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#99\"}");
    CHECK(r.r == UR_GONE && !open_question());
    // after the refusals the slot is free: the next ask works
    r = api_try("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    api_answer("no");
    hostui_test_api_pass();
    // the WiFi page: forget asked; a key at the board answers; a module that is not there refuses with the page's words
    g_ws.state = WN_LINK_UP;
    int f0 = g_w_forget;
    r = api_try("{\"do\":\"op\",\"item\":\"wifi.forget\"}");
    CHECK(r.r == UR_ASKED && open_question());
    hostui_test_api_pass();
    CHECK(g_w_forget == f0);
    hostui_test_board_key('f');
    hostui_test_api_pass();
    CHECK(g_w_forget == f0 + 1 && !open_question());
    g_ws.busy = false;
    r = api_try("{\"do\":\"op\",\"item\":\"wifi.forget\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    hostui_test_board_key(KEY_DOWN);                                                          // another key: no, and nothing is forgotten
    hostui_test_api_pass();
    CHECK(g_w_forget == f0 + 1 && !open_question());
    r = api_try("{\"do\":\"op\",\"item\":\"wifi.forget\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(api_answer("yes").r == UR_TAKEN);                                                   // yes over the API: the screen runs its own second F
    hostui_test_api_pass();
    CHECK(g_w_forget == f0 + 2 && !open_question());
    g_ws.busy = false;
    g_ws.state = WN_NO_MODULE;
    r = api_try("{\"do\":\"op\",\"item\":\"wifi.forget\"}");
    CHECK(r.r == UR_REFUSED && !strcmp(r.why, "There is no WiFi module") && !open_question() && hostapi_pop_key() < 0);
    g_ws.state = WN_LINK_UP;
    hostui_test_api_pass();
    hostui_test_view(0);
}

// T192 D21 review findings 1-3: the countdown stops when the API asks, a pick or a question of the screen makes the ask busy (nothing touched), and the F arm does not outlive a key
// that returns before the WiFi page's own key handler.
extern int hostui_test_cd_state(void);
extern int hostui_test_cm(char *out, int max);
static void api_asked_findings(void)
{
    char cm[4096];
    // 1. the auto-boot countdown runs on the boot menu; entry.remove asked over the API stops it (as a local key does): no boot, the note is there
    hostui_test_init(CFG3);
    hostui_test_view(0);
    hostui_test_cd_tick();
    CHECK(hostui_test_cd_state() == 1);
    g_saves = 0;
    UicReply r = api_js("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(hostui_test_cd_state() == 2);
    CHECK(has("note='DEL again to remove TRX64 Ultimate (C64) (any other key cancels)'") && hostui_test_cfg_n() == 3 && g_saves == 0);
    api_answer("no");
    hostui_test_api_pass();
    // 2a. a target pick is open in the browser: every ask is busy, the pick stays
    mkd("/Usb0/t192b");
    put("/Usb0/t192b/p.bin", 100, 3);
    hostui_test_init(CFG3);
    hostui_test_browse_keep("/Usb0/t192b");
    hostui_test_key(KEY_DOWN);
    hostui_test_key('c');
    CHECK(hostui_test_pick());
    const char *js[3] = { "{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}", "{\"do\":\"op\",\"item\":\"wifi.forget\"}",
                         "{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#1\"}" };
    g_ws.state = WN_LINK_UP;
    for (int i = 0; i < 3; i++) {
        r = api_try(js[i]);
        hostui_test_api_pass();
        CHECK(r.r == UR_BUSY && !open_question() && hostui_test_pick() && hostapi_pop_key() < 0);
    }
    CHECK(fsize("/Usb0/t192b/p.bin") == 100);
    hostui_test_key(KEY_ESCAPE);
    // 2b. the Cores and ROMs page is open: entry.remove and wifi.forget are busy, the page stays
    hostui_test_init(CFG3);
    hostui_test_key(KEY_F8);
    for (int i = 0; i < 6; i++) hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_RETURN);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=1 "));
    for (int i = 0; i < 3; i++) {                                      // browse.delete too (review: it was not covered)
        r = api_try(js[i]);
        hostui_test_api_pass();
        hostui_test_cm(cm, sizeof(cm));
        CHECK(r.r == UR_BUSY && !open_question() && strstr(cm, "mode=1 ") && hostui_test_cfg_n() == 3);
    }
    hostui_test_key(KEY_ESCAPE);
    // 3. wifi.forget armed over the API, the board presses V (ends the arm), then F: that is a first F again, nothing is forgotten
    hostui_test_init(CFG3);
    g_ws.state = WN_LINK_UP;
    g_ws.busy = false;
    int f0 = g_w_forget;
    r = api_js(js[1]);
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    hostui_test_board_key('x');                                      // any other key ends the arm (V opens the Picture page and leaves this one, T187 D5)
    hostui_test_api_pass();
    CHECK(!open_question());
    hostui_test_board_key('f');
    hostui_test_api_pass();
    CHECK(g_w_forget == f0);
    hostui_test_board_key('f');                                      // the second F of the board's own arm forgets
    hostui_test_api_pass();
    CHECK(g_w_forget == f0 + 1);
    g_ws.busy = false;
    hostui_test_view(0);
}

static bool set_pick(const char *name);
// T192 D21 review: only a key from the board confirms an arm; a remote key never does, it ends the arm (the screen's own arm, set by the board)
static void api_asked_remote_key(void)
{
    // the editor: the board arms DEL, a remote DEL key: nothing removed, the arm is over (a board DEL afterwards is a first DEL again)
    hostui_test_init(CFG3);
    hostui_test_view(2);
    g_saves = 0;
    hostui_test_board_key(KEY_DELETE);
    hostui_test_api_pass();
    CHECK(has("DEL again to remove"));
    api_js("{\"do\":\"key\",\"key\":\"del\"}");
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0);
    hostui_test_board_key(KEY_DELETE);
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0);                  // only armed again
    hostui_test_board_key(KEY_DELETE);
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 2 && g_saves == 1);                  // board DEL DEL still removes
    // the browser
    mkd("/Usb0/t192r");
    put("/Usb0/t192r/f.bin", 100, 1);
    hostui_test_init(CFG3);
    hostui_test_browse_keep("/Usb0/t192r");
    hostui_test_key(KEY_DOWN);
    hostui_test_board_key(KEY_DELETE);
    hostui_test_api_pass();
    CHECK(has("DEL again deletes f.bin"));
    api_js("{\"do\":\"key\",\"key\":\"del\"}");
    hostui_test_api_pass();
    CHECK(fsize("/Usb0/t192r/f.bin") == 100);
    hostui_test_board_key(KEY_DELETE);
    hostui_test_api_pass();
    CHECK(fsize("/Usb0/t192r/f.bin") == 100);
    hostui_test_board_key(KEY_DELETE);
    hostui_test_api_pass();
    CHECK(fsize("/Usb0/t192r/f.bin") == -1);
    // the WiFi page: the board arms F, a remote F: nothing forgotten
    hostui_test_init(CFG3);
    g_ws.state = WN_LINK_UP;
    g_ws.busy = false;
    int f0 = g_w_forget;
    hostui_test_view(4);
    set_pick("WiFi");
    hostui_test_key(KEY_RETURN);
    hostui_test_board_key('f');
    hostui_test_api_pass();
    api_js("{\"do\":\"key\",\"key\":\"f\"}");
    hostui_test_api_pass();
    CHECK(g_w_forget == f0);
    hostui_test_view(0);
}

// D19a: the foreign-firmware question a REMOTE client raised (web.start) answers no after 60 s, as every API question does; the board's RETURN has no time-out.
// Without the time-out the confirm waits for a key that never comes and this call never returns (the bench's own time limit fails it).
static void api_remote_start_timeout(void)
{
    char main_root[400];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_fw", main_root);                                          // the media with the firmware image
    hostui_test_api_init();
    hostui_test_init("countdown=5\ndefault=0\ncore=/Usb0/cores/gideon.cfw|G||0\n");                // a foreign firmware: size and mtime are taken once
    hostui_test_view(0);
    int calls = g_legacy_calls, conf = hostui_test_confirms();
    hostui_test_ui_task(true);                                                                   // the confirm's wait pumps the service, which runs the question's clock
    hostui_test_web(WEB_BOOT, 0, 0, 0);                                                          // no key comes: the 60 s are the answer
    hostui_test_ui_task(false);
    CHECK(hostui_test_confirms() == conf + 1 && g_legacy_calls == calls);                        // asked, answered no by the timeout, nothing started
    CHECK(!open_question());
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}

static void api_asked_remove(void)
{
    hostui_test_api_init();
    // the Edit menu: entry.remove
    hostui_test_init(CFG3);
    hostui_test_view(2);
    g_saves = 0;
    UicReply r = api_js("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(has("note='DEL again to remove TRX64 Ultimate (C64) (any other key cancels)'"));      // the very line the local first DEL shows
    CHECK(hostui_test_cfg_n() == 3 && g_saves == 0);
    hostui_test_board_key(KEY_DELETE);                                                           // the key at the board answers yes and removes
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 2 && g_saves == 1);
    r = api_js("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(has("DEL again to remove"));
    hostui_test_board_key(KEY_DOWN);                                                             // any other key is no, and does what it always does
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 2 && g_saves == 1 && has("note=''"));
    r = api_js("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(has("DEL again to remove"));
    CHECK(api_answer("no").r == UR_TAKEN);                                                       // no over the API takes the note away
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 2 && g_saves == 1 && has("note=''"));
    r = api_js("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#0\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(api_answer("yes").r == UR_TAKEN);                                                      // yes over the API presses the confirming DEL
    hostui_test_api_pass();
    CHECK(hostui_test_cfg_n() == 1 && g_saves == 2);
    // the browser: browse.delete
    mkd("/Usb0/t192");
    put("/Usb0/t192/f.bin", 100, 1);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    hostui_test_browse_keep("/Usb0/t192");
    hostui_test_key(KEY_DOWN);
    r = api_js("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#1\"}");
    CHECK(r.r == UR_ASKED);
    hostui_test_api_pass();
    CHECK(has("note='DEL again deletes f.bin. Any other key cancels.'"));                       // the local note of the browser
    CHECK(fsize("/Usb0/t192/f.bin") == 100);
    hostui_test_board_key(KEY_BACK);                                                             // BACK is DEL on the C64 keyboard
    hostui_test_api_pass();
    CHECK(fsize("/Usb0/t192/f.bin") == -1);
    api_asked_review();
    api_asked_findings();
    api_asked_remote_key();
    // the Network page's REST row is the server's state, not a fixed text
    hostui_test_view(3);
    const char *rest = hostui_test_net_text(97);
    printf("REST row: %s\n", rest);
    CHECK(!strstr(rest, "not in the host app") && (!strcmp(rest, "waiting for the network") || !strcmp(rest, "on, port 80, no token")));
}

static void scenarios_t139(const char *out)
{
    (void)out;
    char st[1024];
    mkd("/Usb0/t139"); mkd("/Usb0/t139/dst"); mkd("/SD/t139"); mkd("/SD/t139/sub");
    const char *cfg = "countdown=5\ndefault=0\ncore=/Usb0/t139/src.bin|Amiga test||0\ncore=/SD/cores/trx64.cor|C64||0\n";
    // ---- S1, S2: delete
    put("/Usb0/t139/src.bin", 200000, 1);
    hostui_test_init(cfg);
    hostui_test_browse_keep("/Usb0/t139");                               // rows: .. / dst / src.bin
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    CHECK(has("foot='UP/DOWN select    RETURN open folder    C copy    M move    DEL twice deletes    LEFT up a folder    V picture'"));
    hostui_test_key(KEY_DELETE);
    CHECK(has("note='DEL again deletes src.bin. Menu entry Amiga test points at this file. Any other key cancels.'"));   // S2
    CHECK(fsize("/Usb0/t139/src.bin") == 200000);                       // one DEL deletes nothing
    hostui_test_key('x');                                                // ... and another key cancels
    CHECK(fsize("/Usb0/t139/src.bin") == 200000 && has("note=''"));
    hostui_test_key(KEY_DELETE);
    hostui_test_key(KEY_UP);                                             // a move of the selection cancels as well
    CHECK(fsize("/Usb0/t139/src.bin") == 200000);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_BACK);                                           // INST/DEL of the C64 keyboard is DEL
    CHECK(fsize("/Usb0/t139/src.bin") == 200000);
    hostui_test_key(KEY_BACK);
    CHECK(fsize("/Usb0/t139/src.bin") == -1 && hostui_test_cfg_n() == 2);    // gone; the menu entry stays and shows as missing
    CHECK(has("note='Deleted src.bin. 1 menu entry now shows as missing.'"));
    hostui_test_browse_rows(st, sizeof(st));
    CHECK(!strstr(st, "src.bin") && strstr(st, ">dst|"));
    // a folder: refused while it has something in it
    put("/Usb0/t139/dst/in.bin", 10, 3);
    hostui_test_browse_keep("/Usb0/t139");
    hostui_test_key(KEY_DOWN);                                           // dst
    CHECK(has("foot='UP/DOWN select    RETURN open folder    DEL twice deletes an empty folder    LEFT up a folder    V picture'"));
    hostui_test_key(KEY_DELETE);
    CHECK(has("note='The folder is not empty: only empty folders are deleted'") && fsize("/Usb0/t139/dst/in.bin") == 10);
    hostui_test_key(KEY_DELETE);
    CHECK(fsize("/Usb0/t139/dst/in.bin") == 10);
    // ---- S3: copy; the target is picked with the browser
    put("/Usb0/t139/src.bin", 200000, 1);
    hostui_test_browse_keep("/Usb0/t139");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);                // src.bin
    hostui_test_key('c');
    CHECK(hostui_test_pick() && has("foot='UP/DOWN select    RETURN opens a folder    LEFT up a folder    RETURN on .. or a file copies here    ESC cancels'"));
    hostui_test_key(KEY_DELETE); hostui_test_key(KEY_DELETE);            // no delete while a target is picked
    CHECK(fsize("/Usb0/t139/src.bin") == 200000);
    hostui_test_key(KEY_UP);                                             // dst
    hostui_test_key(KEY_RIGHT);                                          // into dst
    hostui_test_key(KEY_RETURN);
    CHECK(!hostui_test_pick() && same_as("/Usb0/t139/dst/src.bin", 200000, 1) && same_as("/Usb0/t139/src.bin", 200000, 1));
    CHECK(has("note='Copied src.bin to /Usb0/t139/dst (read back, SHA-256 equal)'"));
    hostui_test_browse_rows(st, sizeof(st));
    CHECK(strstr(st, "*src.bin|"));                                      // back in the folder it came from
    // a file of that name is there: asked; another key leaves it, RETURN replaces it
    put("/Usb0/t139/src.bin", 150000, 2);
    hostui_test_browse_keep("/Usb0/t139");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key('c'); hostui_test_key(KEY_UP); hostui_test_key(KEY_RIGHT); hostui_test_key(KEY_RETURN);
    CHECK(has("note='src.bin is already in /Usb0/t139/dst. RETURN replaces it, any other key cancels.'") && has("foot='RETURN replaces the file    any other key cancels'"));
    CHECK(same_as("/Usb0/t139/dst/src.bin", 200000, 1));                 // nothing yet
    hostui_test_key('x');
    CHECK(same_as("/Usb0/t139/dst/src.bin", 200000, 1) && has("note='Not replaced. Nothing was changed.'"));
    hostui_test_key(KEY_RETURN);                                         // the question is gone; this RETURN confirms the target again
    CHECK(has("already in /Usb0/t139/dst"));
    hostui_test_key(KEY_RETURN);
    CHECK(same_as("/Usb0/t139/dst/src.bin", 150000, 2) && !hostui_test_pick());
    // no room: refused before a byte is written, the note says how much there is and how much is needed
    unlink((std::string(g_root) + "/Usb0/t139/dst/src.bin").c_str());
    g_fake_free = 1000;
    hostui_test_key('c'); hostui_test_key(KEY_UP); hostui_test_key(KEY_RIGHT); hostui_test_key(KEY_RETURN);
    CHECK(hostui_test_pick() && strstr(dump(), "No room for src.bin: ") && strstr(dump(), " free, ") && strstr(dump(), " needed. Nothing was written."));
    CHECK(fsize("/Usb0/t139/dst/src.bin") == -1 && fsize("/Usb0/t139/dst/src.bin.part") == -1);
    g_fake_free = -1;
    hostui_test_key(KEY_ESCAPE);                                         // ESC leaves the pick, nothing is done
    CHECK(!hostui_test_pick() && fsize("/Usb0/t139/dst/src.bin") == -1 && has("note='Cancelled. Nothing was changed.'"));
    // ---- move to another medium: copy, read back, then the original goes
    hostui_test_browse_keep("/Usb0/t139");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key('m');
    hostui_test_key(KEY_LEFT); hostui_test_key(KEY_LEFT);                // /Usb0, then the media level
    hostui_test_key(KEY_DOWN);                                           // SD
    hostui_test_key(KEY_RIGHT);                                          // rows: .. / cores / t139 / readme.txt
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_RIGHT);
    hostui_test_key(KEY_RETURN);
    CHECK(same_as("/SD/t139/src.bin", 150000, 2) && fsize("/Usb0/t139/src.bin") == -1 && fsize("/SD/t139/src.bin.part") == -1);
    CHECK(strstr(dump(), "Moved src.bin to /SD/t139. 1 menu entry now points at a file that is not there."));
    // ... and inside a medium it is a rename; into its own folder it is refused
    hostui_test_browse_keep("/SD/t139");                                 // rows: .. / sub / src.bin
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key('m'); hostui_test_key(KEY_RETURN);
    CHECK(hostui_test_pick() && strstr(dump(), "same file") && fsize("/SD/t139/src.bin") == 150000);
    hostui_test_key(KEY_UP); hostui_test_key(KEY_RIGHT); hostui_test_key(KEY_RETURN);
    CHECK(same_as("/SD/t139/sub/src.bin", 150000, 2) && fsize("/SD/t139/src.bin") == -1);
    // board 2026-11-01: RETURN on a highlighted folder enters it; it is never the target of its parent. The target is "..", a file row or the folder shown.
    hostui_test_browse_keep("/SD/t139/sub");                             // rows: .. / src.bin
    hostui_test_key(KEY_DOWN);
    hostui_test_key('m'); hostui_test_key(KEY_LEFT);                     // the pick shows /SD/t139: .. / sub
    hostui_test_key(KEY_DOWN);                                           // sub highlighted
    hostui_test_key(KEY_RETURN);                                         // enters sub: the file is where it was and the pick goes on
    CHECK(hostui_test_pick() && fsize("/SD/t139/src.bin") == -1 && fsize("/SD/t139/sub/src.bin") == 150000);
    hostui_test_key(KEY_LEFT);                                           // back in /SD/t139, ".." highlighted
    hostui_test_key(KEY_RETURN);                                         // into the current folder
    CHECK(!hostui_test_pick() && same_as("/SD/t139/src.bin", 150000, 2) && fsize("/SD/t139/sub/src.bin") == -1);
    CHECK(strstr(dump(), "Moved src.bin to /SD/t139"));
    hostui_test_browse_keep("/SD/t139");                                 // rows: .. / sub / src.bin
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key('m'); hostui_test_key(KEY_UP);                       // sub highlighted
    hostui_test_key(KEY_RETURN);                                         // enters sub
    CHECK(hostui_test_pick() && fsize("/SD/t139/src.bin") == 150000);
    hostui_test_key(KEY_RETURN);                                         // ".." of sub: into the folder shown
    CHECK(!hostui_test_pick() && same_as("/SD/t139/sub/src.bin", 150000, 2) && fsize("/SD/t139/src.bin") == -1);
    CHECK(strstr(dump(), "Moved src.bin to /SD/t139/sub"));
    // the folders are removed again by the same keys (DEL twice)
    hostui_test_browse_keep("/SD/t139/sub");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DELETE); hostui_test_key(KEY_DELETE);
    CHECK(fsize("/SD/t139/sub/src.bin") == -1);
    hostui_test_key(KEY_LEFT);                                           // rows: .. / sub
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DELETE); hostui_test_key(KEY_DELETE);
    hostui_test_browse_rows(st, sizeof(st));
    CHECK(!strstr(st, ">sub|"));
    rmdir((std::string(g_root) + "/SD/t139").c_str());
    // ---- S4: rename in the edit view; the nav and the legend
    const char *cfg3 = "countdown=5\ndefault=1\ncore=/SD/cores/a.cor|Alpha||0\ncore=/SD/cores/b.cor|Beta||0\ncore=/SD/cores/c.cor|Gamma||0\n";
    hostui_test_init(cfg3);
    hostui_test_key(KEY_F5);
    CHECK(has("foot='UP/DOWN select    +/- move    D default    C rename    DEL twice removes'"));
    CHECK(hostui_test_sel_row() == 1);                                   // Beta, the default
    g_loadback = true;
    g_saves = 0;
    kb_push(KEY_CLEAR); kb_type("New Amiga 1200"); kb_push(KEY_RETURN);
    hostui_test_key('c');
    CHECK(kbn == 0 && g_saves == 1);
    CHECK(has("row 1|New Amiga 1200|") && has("row 0|Alpha|") && has("row 2|Gamma|") && has("def=1"));    // order and default as they were
    CHECK(strstr(g_last_save, "core=/SD/cores/b.cor|New Amiga 1200||") && strstr(g_last_save, "default=1\n"));
    CHECK(has("note='Renamed to New Amiga 1200 (boot.cfg read back)'"));
    hostui_test_key(KEY_DOWN);                                           // the next key: the note is gone, it does not stay under the list
    CHECK(!has("note='Renamed") && !strstr(dump(), "Renamed to"));
    hostui_test_key(KEY_UP);
    hostui_test_key(KEY_F1);                                             // the menu shows the new name
    CHECK(has("row 1|New Amiga 1200|"));
    hostui_test_key(KEY_F5);
    // ESC leaves it; an empty name is refused (the field stays open until a name or ESC)
    g_saves = 0;
    kb_push(KEY_CLEAR); kb_type("Zzz"); kb_push(KEY_ESCAPE);
    hostui_test_key('c');
    CHECK(kbn == 0 && g_saves == 0 && has("row 1|New Amiga 1200|") && has("note='Name unchanged'"));
    kb_push(KEY_CLEAR); kb_push(KEY_RETURN); kb_push(KEY_ESCAPE);
    hostui_test_key('c');
    CHECK(kbn == 0 && g_saves == 0 && has("row 1|New Amiga 1200|"));
    kb_push(KEY_CLEAR); kb_push(KEY_RETURN); kb_type("Ok"); kb_push(KEY_RETURN);      // refused once, then a name
    hostui_test_key('c');
    CHECK(kbn == 0 && g_saves == 1 && has("row 1|Ok|"));
    // INS, DEL, HOME / END and the cursor keys inside the field
    kb_push(KEY_HOME); kb_push(KEY_DELETE); kb_type("0"); kb_push(KEY_END); kb_type("!"); kb_push(KEY_LEFT); kb_push(KEY_BACK);
    kb_push(KEY_RETURN);
    hostui_test_key('c');
    CHECK(kbn == 0 && has("row 1|0!|"));
    g_loadback = false;
    // B and LEFT/RIGHT are gone from the edit view; V is still the picture key
    g_saves = 0;
    hostui_test_key('b'); hostui_test_key(KEY_LEFT); hostui_test_key(KEY_RIGHT);
    CHECK(g_saves == 0);
    fresh_video(VID_SYS_PAL50, -1);
    g_hv_stores = 0;
    page_pick(2);                                                                   // 720p
    CHECK(g_hv_stores == 1 && g_vid_hdmi == 1);
    CHECK(g_pl_x == 160 && g_pl_y == 90 && g_pl_w == 960 && g_pl_h == 540);       // 720p: factor 1, centred in 1280 x 720 (T164 WP5 PX_OSIZE)
    page_pick(3);                                                                   // 1080p
    CHECK(g_vid_hdmi == 2 && g_pl_x == 0 && g_pl_y == 0 && g_pl_w == 1920 && g_pl_h == 1080);   // 1080p: factor 2, the whole frame
    // ---- S5: the settings page
    CHECK(!strcmp(hostui_test_nav_name(4), "Settings"));
    fresh_video(VID_SYS_PAL50, -1);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    hostui_test_key(KEY_F8);
    CHECK(has("view=4 ") && has("foot='UP/DOWN select    LEFT/RIGHT or RETURN change    V picture'"));
    char se[1024];
    hostui_test_settings(se, sizeof(se));
    CHECK(!strcmp(se, "sel=0|Direct boot=on|Picture=from the screen: 576p / 480p|Protect Flash from updates=on|WiFi=open|FTP service=on, port 21, no password|Inspect .cfw/.ue2 for ESP32=open|Cores and ROMs=open|Bug report=write to SD/USB|") ||
          (printf("settings: %s\n", se), false));                       // direct on: no autostart rows
    g_saves = 0;
    hostui_test_key(KEY_RETURN);                                         // direct boot off
    CHECK(g_saves == 1 && strstr(g_last_save, "direct=0\n"));
    hostui_test_settings(se, sizeof(se));
    CHECK(strstr(se, "Direct boot=off|Autostart from menu=on|Autostart delay=5 s|"));
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_RETURN);              // autostart off: countdown=0, the seconds are kept
    CHECK(strstr(g_last_save, "countdown=0\n") && strstr(g_last_save, "autostart_s=5\n"));
    hostui_test_settings(se, sizeof(se));
    CHECK(strstr(se, "Autostart from menu=off|") && !strstr(se, "Autostart delay"));
    hostui_test_key(KEY_RETURN);                                         // ... and on again
    CHECK(strstr(g_last_save, "countdown=5\n"));
    hostui_test_key(KEY_DOWN);
    for (int i = 0; i < 3; i++) hostui_test_key(KEY_RIGHT);              // 8 s
    CHECK(strstr(g_last_save, "countdown=8\n") && strstr(g_last_save, "autostart_s=8\n"));
    hostui_test_key(KEY_RETURN); hostui_test_key(KEY_UP);                // delay 9 s, back to the autostart row: off
    hostui_test_key(KEY_RETURN);
    CHECK(strstr(g_last_save, "countdown=0\n") && strstr(g_last_save, "autostart_s=9\n"));
    hostui_test_key(KEY_RETURN);
    CHECK(strstr(g_last_save, "countdown=9\n"));                        // the seconds that were set come back
    // the picture rows
    fresh_video(VID_SYS_PAL50, -1);
    g_vid_sys = g_vid_hdmi = -1;
    hostui_test_settings(se, sizeof(se));
    CHECK(strstr(se, "Picture=from the screen"));
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    int v0 = g_vid_hdmi, st0 = g_hv_stores;
    hostui_test_key(KEY_RIGHT); hostui_test_key(KEY_LEFT);               // T187 D5 (Q-D5.1): LEFT / RIGHT on the closed Picture row do nothing
    CHECK(g_vid_hdmi == v0 && g_hv_stores == st0 && hostui_test_pm(0, 0, 0, 0, 0, 0) == 0);
    hostui_test_key(KEY_RETURN);                                         // RETURN opens the page, the cursor on the choice in force (Auto)
    { char nm[64]; int sl = -1, rw = 0; CHECK(hostui_test_pm(nm, sizeof(nm), &sl, &rw, 0, 0) == 1 && sl == 0 && rw == 4 && !strncmp(nm, "Auto", 4)); }
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_RETURN);              // the first resolution the screen names
    CHECK(g_vid_hdmi == 0 && g_saved_hdmi == 0);
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_RETURN);
    CHECK(g_vid_hdmi == 1 && g_saved_hdmi == 1);
    page_pick(0);                                                        // Auto: stored as "from the screen" (-1)
    CHECK(g_saved_hdmi == -1);
    hostui_test_settings(se, sizeof(se));
    CHECK(!strstr(se, "Picture: system"));                               // Q6: no Hz choice in any menu
    page_pick(3);                                                        // V opens the page from here too
    CHECK(g_vid_hdmi == VID_HDMI_1080P && g_saved_hdmi == VID_HDMI_1080P);
    // the stored file of an older host (countdown only) opens as it did: autostart on, the seconds shown
    hostui_test_init("countdown=12\ndirect=0\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    hostui_test_key(KEY_F8);
    hostui_test_settings(se, sizeof(se));
    CHECK(strstr(se, "Autostart from menu=on|Autostart delay=12 s|"));
    // F8 from every view
    for (int v = 0; v < 4; v++) {
        hostui_test_view(v);
        hostui_test_key(KEY_F8);
        CHECK(has("view=4 "));
    }
    // ---- T162 S1: F8 -> Bug report writes /SD/report/<stamp>.txt and shows its name
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64|ab12cd34|1234|77\n");
    hostui_test_key(KEY_F8);
    hostui_test_settings(se, sizeof(se));
    CHECK(strstr(se, "Bug report=write to SD/USB|"));
    CHECK(g_rep_saves == 0);
    for (int i = 0; i < 7; i++) hostui_test_key(KEY_DOWN);               // direct on: Direct boot, Picture, Protect Flash, WiFi, FTP service, Inspect, Cores and ROMs, Bug report
    hostui_test_key(KEY_RETURN);
    CHECK(g_rep_saves == 1 && g_rep_name == "20261030-140510.txt");
    CHECK(g_rep_text.find("== Versions ==") != std::string::npos && g_rep_text.find("Host app: ") != std::string::npos);
    CHECK(g_rep_text.find("== Settings ==") != std::string::npos && g_rep_text.find("countdown=5") != std::string::npos);
    CHECK(g_rep_text.find("== Boot menu") != std::string::npos && g_rep_text.find("/SD/cores/trx64.cor") != std::string::npos);
    CHECK(g_rep_text.find("last line") != std::string::npos && g_rep_text.find("bench log line 1") != std::string::npos);
    CHECK(g_rep_text.find("hunter2") == std::string::npos);                // P4 through the real menu path
    hostui_test_settings(se, sizeof(se));
    CHECK(strstr(se, "Bug report=20261030-140510.txt|"));                // the name is shown
    {
        char rp[700];
        snprintf(rp, sizeof(rp), "%s/bug_report.txt", out);
        FILE *rf = fopen(rp, "wb");
        if (rf) { fwrite(g_rep_text.data(), 1, g_rep_text.size(), rf); fclose(rf); }
        printf("T162: the report the menu wrote (%d bytes) is in %s\n", (int)g_rep_text.size(), rp);
    }
    // the screens: the rename field, the target pick
    hostui_test_init(cfg3);
    hostui_test_key(KEY_F5);
    hostui_test_rename_panel(true, "Amiga 1200 (colour fix)", 6, "");
    snprintf(st, sizeof(st), "%s/rename.png", out); png(st, 2);
    hostui_test_rename_panel(true, "", 0, "A name cannot be empty.");
    snprintf(st, sizeof(st), "%s/rename_empty.png", out); png(st, 2);
    hostui_test_rename_panel(false, 0, 0, 0);
    mkd("/Usb0/t139"); put("/Usb0/t139/src.bin", 1000, 1);
    hostui_test_browse_keep("/Usb0/t139");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key('c');
    snprintf(st, sizeof(st), "%s/browse_pick.png", out); png(st, 2);
    hostui_test_key(KEY_ESCAPE);
    hostui_test_browse_keep("/Usb0/t139");
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DELETE);
    snprintf(st, sizeof(st), "%s/browse_delete.png", out); png(st, 2);
    unlink((std::string(g_root) + "/Usb0/t139/src.bin").c_str());
    rmdir((std::string(g_root) + "/Usb0/t139").c_str());
    // ---- the screen follows cfg: after +/- the F1 rows are cfg's order and RETURN starts the row that is shown
    {
        const char *c4 = "countdown=0\ndefault=0\ncore=/SD/cores/a.cor|Alpha||0\ncore=/SD/cores/b.cor|Beta||0\ncore=/SD/cores/c.cor|Gamma||0\ncore=/SD/cores/d.cor|Delta||0\n";
        const char *names[4] = { "Gamma", "Alpha", "Beta", "Delta" };
        const char *paths[4] = { "/SD/cores/c.cor", "/SD/cores/a.cor", "/SD/cores/b.cor", "/SD/cores/d.cor" };
        hostui_test_init_lazy(c4);
        hostui_test_key(KEY_F5);
        hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);           // Gamma
        hostui_test_key('+');                                            // Gamma above Beta
        hostui_test_key('+');                                            // and above Alpha
        CHECK(has("row 0|Gamma|") && has("row 1|Alpha|") && has("row 2|Beta|") && has("row 3|Delta|") && has("def=1"));
        for (int r = 0; r < 4; r++) {                                    // T209 D12: the editor row draws the boot menu's pictogram (22 x 22), no word box
            char pic[48];
            snprintf(pic, sizeof(pic), "\npic %d|", r);
            CHECK(has(pic));
        }
        CHECK(strstr(dump(), "|22x22|auto\n") != 0 || strstr(dump(), "|20x20|auto\n") != 0);
        hostui_test_lazy_pass(0, 5000, 0);                               // the lazy reader runs between the keys
        hostui_test_key(KEY_F1);
        for (int r = 0; r < 4; r++) {
            char row[64];
            snprintf(row, sizeof(row), "row %d|%s|", r, names[r]);       // the menu row names the entry; the path is not in the row (T209 D8)
            CHECK(has(row));
        }
        CHECK(strstr(strstr(dump(), "row 1|Alpha|"), "auto") != 0);   // the auto badge went with Alpha
        for (int want = 0; want < 4; want++) {                           // every row: RETURN starts the entry that row shows
            hostui_test_init_lazy(c4);
            hostui_test_key(KEY_F5);
            hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
            hostui_test_key('+'); hostui_test_key('+');
            hostui_test_key(KEY_F1);
            for (int k = 0; k < want; k++) hostui_test_key(KEY_DOWN);
            g_stage_calls = 0; g_stage_path[0] = 0;
            hostui_test_key(KEY_RETURN);
            CHECK(g_stage_calls == 1 && !strcmp(g_stage_path, paths[want]));
        }
    }
    // the pixels too: rows moved by +/- look exactly like rows built in that order from the start (an icon or badge left on the old row shows)
    {
        const char *ab = "countdown=0\ndefault=0\ncore=/SD/cores/trx64.cor|TRX64 Ultimate (C64)||0\ncore=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\ncore=/Usb0/cores/c2.cor|Future||0\n";
        const char *ba = "countdown=0\ndefault=1\ncore=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\ncore=/SD/cores/trx64.cor|TRX64 Ultimate (C64)||0\ncore=/Usb0/cores/c2.cor|Future||0\n";
        static uint16_t moved[960 * 540];
        hostui_test_init(ab);
        hostui_test_key(KEY_F1);
        hostui_test_key(KEY_F5);
        hostui_test_key('-');
        hostui_test_key(KEY_F1);
        CHECK(has("|?|"));                                                // right after the move every entry is unknown again, not the old state
        hostui_test_lazy_drain();
        hostui_test_key(KEY_F1);
        memcpy(moved, fb, sizeof(moved));
        char d1[2048];
        snprintf(d1, sizeof(d1), "%s", dump());
        hostui_test_init(ba);
        hostui_test_key(KEY_F1);
        char d2[2048];
        snprintf(d2, sizeof(d2), "%s", dump());
        int diff = 0;
        for (int i = 0; i < 960 * 540; i++) diff += moved[i] != fb[i];
        printf("moved vs built in order: %d px differ\n", diff);
        if (strcmp(d1, d2)) printf("DUMP1 %s\nDUMP2 %s\n", d1, d2);
        CHECK(diff == 0 && !strcmp(d1, d2));
    }
    printf("T139 scenarios done\n");
}

// ---- T148 S6: F8 -> Cores and ROMs. A tree of its own (<root>_cm): two cores with folders, a core without, files that belong to no core, an old
// ROM folder with a copy of a ROM that lies in a core folder. The cleanup asks twice, No is preselected, and takes only what the page lists.
extern int hostui_test_cm(char *out, int max);
extern const char *hostui_test_cm_head(void);
extern const char *hostui_test_cm_note(void);
extern const char *hostui_test_foot(void);
extern void hostui_test_oc_geom(int *g);
static void mkdirs(const char *rel)
{
    char d[700]; snprintf(d, sizeof(d), "%s%s", g_root, rel);
    for (char *q = d + strlen(g_root) + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(d, 0755); *q = '/'; }
    mkdir(d, 0755);
}
static void putp(const char *rel, size_t n, int seed)
{
    char dir[700]; snprintf(dir, sizeof(dir), "%s", rel);
    char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; mkdirs(dir); }
    put(rel, n, seed);
}
static void scenarios_t148(const char *out)
{
    char p[512], main_root[400], cm[8192];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_cm", main_root);
    mkdir(g_root, 0755);
    putp("/SD/cores/apple2.cor", 100, 1);
    putp("/SD/cores/apple2/apple2.cfg", 40, 2);
    putp("/SD/cores/apple2/roms/apple2e.rom", 16384, 3);
    putp("/SD/cores/apple2/saves/a.sav", 10, 4);
    putp("/SD/cores/atari800.cor", 100, 6);
    putp("/SD/cores/atari800/roms/ATARIXL.ROM", 16384, 8);
    putp("/SD/cores/atari800/roms/basic.rom", 8192, 9);
    putp("/SD/cores/junk.txt", 77, 20);
    putp("/SD/cores/ghost/ghost.cfg", 20, 15);
    putp("/SD/cores/ghost/saves/s.sav", 50, 17);
    putp("/SD/roms/atari800/atarixl.rom", 16384, 8);                  // the same bytes as the core folder's
    putp("/Usb0/cores/nes.cor", 100, 12);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/apple2.cor|Apple||0\n");
    hostui_test_key(KEY_F8);
    for (int i = 0; i < 6; i++) hostui_test_key(KEY_DOWN);
    hostui_test_settings(cm, sizeof(cm));
    CHECK(strstr(cm, "Cores and ROMs=open|") && strstr(cm, "sel=6|"));
    hostui_test_key(KEY_RETURN);                                          // opens the page
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=1 "));
    CHECK(strstr(cm, "SD    apple2.cor") && strstr(cm, "ROMs 1/2, missing apple2e-video.rom   saves 1|"));
    CHECK(strstr(cm, "SD    atari800.cor           ROMs 2/2   saves 0|"));
    CHECK(strstr(cm, "Usb0  nes.cor") && strstr(cm, "no folder yet"));
    CHECK(strstr(cm, "goes /SD/cores/junk.txt") && strstr(cm, "goes /SD/cores/ghost/ghost.cfg") && strstr(cm, "goes /SD/roms/atari800/atarixl.rom"));
    CHECK(strstr(cm, "kept /SD/cores/ghost/saves/s.sav") && strstr(cm, "kept /SD/cores/ghost/    (holds saves, kept)"));
    CHECK(strstr(cm, "There twice or more") && strstr(cm, "in 2 places"));
    CHECK(!strcmp(hostui_test_foot(), "UP/DOWN scroll    DEL delete the files that belong to no core    ESC back"));
    snprintf(p, sizeof(p), "%s/cores_roms.png", out); png(p, 2);
    // DEL asks, RETURN is No
    hostui_test_key(KEY_DELETE);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=2 ") && strstr(cm, "/SD/cores/junk.txt") && !strstr(cm, "apple2.cor"));
    CHECK(strstr(hostui_test_cm_head(), "Delete these 3 entries? Press Y. RETURN or any other key is No."));
    snprintf(p, sizeof(p), "%s/cores_roms_ask.png", out); png(p, 2);
    hostui_test_key(KEY_RETURN);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=1 ") && fsize("/SD/cores/junk.txt") == 77 && strstr(hostui_test_cm_note(), "Cancelled"));
    // one Y is not enough, a key between is No
    hostui_test_key(KEY_DELETE); hostui_test_key('y');
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=3 ") && fsize("/SD/cores/junk.txt") == 77 && strstr(hostui_test_cm_head(), "Really delete 3 entries?"));
    snprintf(p, sizeof(p), "%s/cores_roms_ask2.png", out); png(p, 2);
    hostui_test_key('n');
    CHECK(fsize("/SD/cores/junk.txt") == 77);
    hostui_test_key(KEY_DELETE); hostui_test_key('y'); hostui_test_key(KEY_RETURN);
    CHECK(fsize("/SD/cores/junk.txt") == 77 && fsize("/SD/roms/atari800/atarixl.rom") == 16384);
    // Y, Y deletes what was listed and nothing else
    hostui_test_key(KEY_DELETE); hostui_test_key('y'); hostui_test_key('y');
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=1 ") && fsize("/SD/cores/junk.txt") == -1 && fsize("/SD/cores/ghost/ghost.cfg") == -1 && fsize("/SD/roms/atari800/atarixl.rom") == -1);
    CHECK(fsize("/SD/cores/apple2.cor") == 100 && fsize("/SD/cores/atari800.cor") == 100 && fsize("/Usb0/cores/nes.cor") == 100);
    CHECK(fsize("/SD/cores/apple2/roms/apple2e.rom") == 16384 && fsize("/SD/cores/atari800/roms/ATARIXL.ROM") == 16384 && fsize("/SD/cores/atari800/roms/basic.rom") == 8192);
    CHECK(fsize("/SD/cores/apple2/apple2.cfg") == 40 && fsize("/SD/cores/apple2/saves/a.sav") == 10 && fsize("/SD/cores/ghost/saves/s.sav") == 50);
    CHECK(!strcmp(hostui_test_cm_note(), "Deleted 3 entries (17 KiB)."));
    CHECK(!strstr(cm, "goes /") && !strstr(cm, "There twice or more"));
    CHECK(!strcmp(hostui_test_foot(), "UP/DOWN scroll    ESC back"));
    snprintf(p, sizeof(p), "%s/cores_roms_done.png", out); png(p, 2);
    // DEL with nothing to delete does nothing
    hostui_test_key(KEY_DELETE);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=1 "));
    // ESC leaves the page: the settings list and its footer are back
    hostui_test_key(KEY_ESCAPE);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=0 ") && !strcmp(hostui_test_foot(), "UP/DOWN select    LEFT/RIGHT or RETURN change    V picture"));
    // F8 again while the page is open comes back to the list, not to the page
    hostui_test_key(KEY_RETURN);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=1 "));
    hostui_test_key(KEY_F1); hostui_test_key(KEY_F8);
    hostui_test_cm(cm, sizeof(cm));
    CHECK(strstr(cm, "mode=0 "));
    snprintf(g_root, sizeof(g_root), "%s", main_root);
    printf("T148 scenarios done\n");
}

// ---- T198: the config overlay (S2-S6, S9, S12, S13, S15, S16) ------------------------------------------------------------------
#include "host_core_cfgov.h"
extern const char *hostui_test_oc_title(int *w, int *room);
void hc_ui_cfgov_draw(bool fresh);
static void scenarios_t198(const char *out)
{
    char p[512], main_root[400];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_cfg", main_root);
    mkdir(g_root, 0755);
    mkdirs("/SD/cores");
    for (const char *c : { "amiga", "vic", "c64" }) {                      // real bundles (the start refuses a state that is not ok), the bench tree's own
        char from[600], to[600];
        snprintf(from, sizeof(from), "%s/SD/cores/trx64.cor", main_root);
        snprintf(to, sizeof(to), "%s/SD/cores/%s.cor", g_root, c);
        FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
        CHECK(a && b);
        for (int ch; a && b && (ch = fgetc(a)) != EOF;) fputc(ch, b);
        if (a) fclose(a);
        if (b) fclose(b);
    }
    putp("/SD/cores/amiga/configs/Demo collection.cfg", 40, 2);
    putp("/SD/cores/amiga/configs/Games.cfg", 40, 3);
    putp("/SD/cores/amiga/configs/Workbench.cfg", 40, 4);
    putp("/SD/cores/amiga/media/old set.mnt", 20, 5);                   // a media set of before T198 (D3)
    putp("/SD/cores/c64/configs/Only.cfg", 40, 8);
    hostui_test_init("countdown=0\ndefault=0\ncore=/SD/cores/amiga.cor|Amiga 1200 (AGA, 68020)||0|0|00000000|Games\ncore=/SD/cores/vic.cor|VIC-20||0|0|00000000\ncore=/SD/cores/c64.cor|C64||0|0|00000000\n");
    // S2/S13: the badge counts the configs of both folders; none for a core without any (D9)
    HcEntryRow r0, r1, r2;
    CHECK(hc_row_entry(0, true, &r0) && r0.configs == 4 && !strcmp(r0.cfgbadge, "4 configs"));
    CHECK(hc_row_entry(1, true, &r1) && r1.configs == 0 && r1.cfgbadge[0] == 0);
    CHECK(hc_row_entry(2, true, &r2) && r2.configs == 1 && !strcmp(r2.cfgbadge, "1 config"));
    snprintf(p, sizeof(p), "%s/t198_menu_badges_480.png", out); png(p, 1);
    snprintf(p, sizeof(p), "%s/t198_menu_badges_1080.png", out); png(p, 2);
    // D15: the F5 edit rows carry the same badge as the boot menu rows
    hostui_test_key(KEY_F5);
    {
        const char *ed = dump();                                                   // the object dump of the F5 page: what the rows drew
        CHECK(strstr(ed, "view=2 ") != NULL);
        CHECK(strstr(ed, "\ncfgbadge 0|4 configs\n") != NULL);                      // the core with 4 configs: the badge is drawn on its F5 row
        CHECK(strstr(ed, "\ncfgbadge 2|1 config\n") != NULL);                       // 1 config: singular
        CHECK(strstr(ed, "\ncfgbadge 1|") == NULL);                                  // a core without configs: no badge (D9)
    }
    // the F5 foot offers LEFT/RIGHT config only on a row whose core has configs (row 0: 4, row 1: none)
    CHECK(strstr(hostui_test_foot(), "LEFT/RIGHT config of this core") != NULL);
    hostui_test_key(KEY_DOWN);
    CHECK(strstr(hostui_test_foot(), "LEFT/RIGHT") == NULL && strstr(hostui_test_foot(), "DEL twice removes") != NULL);
    CHECK(strstr(dump(), "LEFT/RIGHT config of this core") == NULL);
    hostui_test_key(KEY_UP);
    CHECK(strstr(hostui_test_foot(), "LEFT/RIGHT config of this core") != NULL);
    snprintf(p, sizeof(p), "%s/t198_edit_badges_480.png", out); png(p, 1);
    snprintf(p, sizeof(p), "%s/t198_edit_badges_1080.png", out); png(p, 2);
    hostui_test_key(KEY_F1);
    // D9: RIGHT on a core without configs does nothing
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_RIGHT);
    CHECK(!hc_cfgov_is_open());
    hostui_test_key(KEY_UP);
    // S3/D2/D14: RIGHT opens the overlay, the cursor on the default config
    CHECK(!strstr(hostui_test_foot(), "RIGHT configs") == 0);
    hostui_test_key(KEY_RIGHT);
    CHECK(hc_cfgov_is_open() && hc_cfgov().l.n == 4 && hc_cfgov().cur == 2 && !strcmp(hc_cfgov_cur_name(), "Games"));
    CHECK(!strcmp(hc_cfgov().l.name[0], "Demo collection") && !strcmp(hc_cfgov().l.name[2], "old set") && hc_cfgov().l.legacy[2]);
    CHECK(!strcmp(hostui_test_foot(), "UP/DOWN select    RETURN start    LEFT back    D default    R rename    DEL twice deletes") ||
          strstr(hostui_test_foot(), "RETURN start"));
    {   // board 11-11: the title is the whole entry name, not "Amiga 12"; a name too wide for the panel is cut with "..." before the suffix
        int w, room;
        const char *t = hostui_test_oc_title(&w, &room);
        CHECK(!strcmp(t, "Amiga 1200 (AGA, 68020) - start with") && w <= room);
        hostui_test_key(KEY_LEFT);
        CHECK(hc_cfgov_open("/SD/cores/amiga.cor", "Amiga 1200 (AGA, 68020) with a very long extra name", 0, false));
        hc_ui_cfgov_draw(true);
        t = hostui_test_oc_title(&w, &room);
        printf("long title: [%s] w=%d room=%d\n", t, w, room);
        CHECK(strstr(t, "...") && !strcmp(t + strlen(t) - 13, " - start with") && !strncmp(t, "Amiga 1200", 10) && w <= room);
        snprintf(p, sizeof(p), "%s/t198_overlay_longname_1080.png", out); png(p, 2);
        hostui_test_key(KEY_LEFT);
        hostui_test_key(KEY_RIGHT);
    }
    snprintf(p, sizeof(p), "%s/t198_overlay_menu_480.png", out); png(p, 1);
    snprintf(p, sizeof(p), "%s/t198_overlay_menu_1080.png", out); png(p, 2);
    // LEFT closes; the menu keys work again
    hostui_test_key(KEY_LEFT);
    CHECK(!hc_cfgov_is_open());
    hostui_test_key(KEY_RIGHT); hostui_test_key(KEY_ESCAPE);
    CHECK(!hc_cfgov_is_open());
    // S5: RETURN on the row with the overlay closed is a plain start, as before
    g_started_cfg = "?";
    hostui_test_key(KEY_RETURN);
    CHECK(g_started_cfg.empty());
    // S4/S6: RETURN in the overlay starts with the picked config, once
    hostui_test_key(KEY_RIGHT);
    hostui_test_key(KEY_UP);                                             // Demo collection
    CHECK(!strcmp(hc_cfgov_cur_name(), "Demo collection"));
    g_started_cfg = "?";
    hostui_test_key(KEY_RETURN);
    CHECK(!hc_cfgov_is_open() && g_started_cfg == "Demo collection");
    hostui_test_key(KEY_RETURN);
    CHECK(g_started_cfg.empty());                                        // the next start is plain again (one-shot)
    // "Plain start" (the first row) starts plain too
    hostui_test_key(KEY_RIGHT); hostui_test_key(KEY_UP); hostui_test_key(KEY_UP);
    CHECK(hc_cfgov().cur == 0);
    g_started_cfg = "?";
    hostui_test_key(KEY_RETURN);
    CHECK(g_started_cfg.empty());
    // S9: D marks the default (written to boot.cfg), D again clears it
    hostui_test_key(KEY_RIGHT);
    hostui_test_key(KEY_UP);
    g_saves = 0;
    hostui_test_key('d');
    CHECK(g_saves == 1 && strstr(g_last_save, "|Demo collection\n") && !strcmp(hc_cfg().e[0].defcfg, "Demo collection"));
    hostui_test_key('d');
    CHECK(!hc_cfg().e[0].defcfg[0] && !strstr(g_last_save, "|Demo collection"));
    hostui_test_key('d');                                                // set again, then the rename must carry it along
    // S9: rename
    kb_push(KEY_CLEAR); kb_type("Demos"); kb_push(KEY_RETURN);
    hostui_test_key('r');
    CHECK(kbn == 0 && fsize("/SD/cores/amiga/configs/Demos.cfg") == 40 && fsize("/SD/cores/amiga/configs/Demo collection.cfg") == -1);
    CHECK(!strcmp(hc_cfg().e[0].defcfg, "Demos") && !strcmp(hc_cfgov_cur_name(), "Demos"));
    snprintf(p, sizeof(p), "%s/t198_overlay_renamed_1080.png", out); png(p, 2);
    kb_push(KEY_CLEAR); kb_type("Games"); kb_push(KEY_RETURN);                                // a name that exists: refused, the file stays
    hostui_test_key('r');
    CHECK(kbn == 0 && fsize("/SD/cores/amiga/configs/Demos.cfg") == 40);
    // S9: delete asks twice; any other key between disarms
    hostui_test_key(KEY_DELETE);
    CHECK(hc_cfgov().ask == 1 && fsize("/SD/cores/amiga/configs/Demos.cfg") == 40);
    snprintf(p, sizeof(p), "%s/t198_overlay_delete_1080.png", out); png(p, 2);
    hostui_test_key(KEY_DOWN);
    CHECK(hc_cfgov().ask == 0);
    hostui_test_key(KEY_UP);
    hostui_test_key(KEY_DELETE); hostui_test_key(KEY_DELETE);
    CHECK(fsize("/SD/cores/amiga/configs/Demos.cfg") == -1 && hc_cfgov().l.n == 3 && !hc_cfg().e[0].defcfg[0]);
    // a legacy media set is deleted from media/
    hostui_test_key(KEY_DOWN);                                           // the cursor stayed on the row of Demos: Games now, old set next
    CHECK(!strcmp(hc_cfgov_cur_name(), "old set"));
    hostui_test_key(KEY_DELETE); hostui_test_key(KEY_DELETE);
    CHECK(fsize("/SD/cores/amiga/media/old set.mnt") == -1 && hc_cfgov().l.n == 2);
    // Plain start has nothing to delete or rename
    hostui_test_key(KEY_UP); hostui_test_key(KEY_UP); hostui_test_key(KEY_UP);
    hostui_test_key(KEY_DELETE); hostui_test_key(KEY_DELETE);
    CHECK(hc_cfgov().cur == 0 && hc_cfgov().l.n == 2);
    hostui_test_key(KEY_ESCAPE);
    // UX 2 of 2026-11-10 (finding 1): the panel follows the visible rows (5..8, then it scrolls); the selected row and the message line lie inside the panel,
    // the message below the last drawn row and clear of the selected one. Measured on the objects (hostui_test_oc_geom), 6 and 10 configs.
    CHECK(hostui_test_foot() != nullptr);
    for (int i = 1; i <= 5; i++) { char nm[80]; snprintf(nm, sizeof(nm), "/SD/cores/c64/configs/Extra %d.cfg", i); putp(nm, 40, 20 + i); }
    hc_cfgov_forget();
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);                // the C64 row of the menu
    hostui_test_key(KEY_RIGHT);
    CHECK(hc_cfgov_is_open() && hc_cfgov().l.n == 6);
    hostui_test_key(KEY_DOWN);
    {
        int g[8]; hostui_test_oc_geom(g);
        CHECK(g[0] >= 0 && g[0] + g[1] <= g[7]);                              // the panel inside the dimmer
        CHECK(g[2] >= 0 && g[3] <= g[1] && g[6] <= g[1] && g[3] <= g[6]);     // the selected row and the last row inside the panel
        CHECK(g[1] > 0 && g[4] == -1);                                        // no message yet
    }
    { HcWifiPick pk; hc_cfgov_pick(&pk); CHECK(strstr(pk.sub, " of ") == nullptr); }                      // 7 rows: no "a-b of n"
    snprintf(p, sizeof(p), "%s/t198_overlay_6_1080.png", out); png(p, 2);
    snprintf(p, sizeof(p), "%s/t198_overlay_6_480.png", out); png(p, 1);
    kb_push(KEY_CLEAR); kb_type("Only"); kb_push(KEY_RETURN);             // a name the list has: the refusal is a message over the full list
    hostui_test_key('r');
    {
        int g[8]; hostui_test_oc_geom(g);
        CHECK(g[4] >= g[6] && g[5] <= g[1] && g[4] >= g[3]);               // message below the last row, inside the panel, clear of the selection
    }
    hostui_test_key(KEY_ESCAPE);
    for (int i = 6; i <= 10; i++) { char nm[80]; snprintf(nm, sizeof(nm), "/SD/cores/c64/configs/Extra %d.cfg", i); putp(nm, 40, 20 + i); }
    hc_cfgov_forget();
    hostui_test_key(KEY_RIGHT);
    CHECK(hc_cfgov_is_open() && hc_cfgov().l.n == 11);
    for (int i = 0; i < 12; i++) hostui_test_key(KEY_DOWN);                // the last row: scrolled
    CHECK(hc_cfgov().cur == 11 && hc_cfgov().top > 0);
    {
        int g[8]; hostui_test_oc_geom(g);
        CHECK(g[2] >= 0 && g[3] <= g[1] && g[6] <= g[1] && g[0] + g[1] <= g[7]);
    }
    { HcWifiPick pk; hc_cfgov_pick(&pk); CHECK(strstr(pk.sub, " of 12") != nullptr); }
    snprintf(p, sizeof(p), "%s/t198_overlay_10_last_1080.png", out); png(p, 2);
    hostui_test_key(KEY_DELETE);                                           // the ask is a message over the full, scrolled list
    CHECK(hc_cfgov().ask == 1);
    {
        int g[8]; hostui_test_oc_geom(g);
        CHECK(g[4] >= g[6] && g[5] <= g[1] && g[4] >= g[3]);
    }
    snprintf(p, sizeof(p), "%s/t198_overlay_10_msg_1080.png", out); png(p, 2);
    hostui_test_key(KEY_DOWN);
    // finding 2: on "Plain start" the key bar names no D / R / DEL
    for (int i = 0; i < 12; i++) hostui_test_key(KEY_UP);
    CHECK(hc_cfgov().cur == 0 && !strstr(hostui_test_foot(), "default") && !strstr(hostui_test_foot(), "rename") && !strstr(hostui_test_foot(), "DEL"));
    CHECK(strstr(hostui_test_foot(), "start") != nullptr);
    hostui_test_key(KEY_DOWN);
    CHECK(strstr(hostui_test_foot(), "twice deletes") != nullptr);
    hostui_test_key(KEY_ESCAPE);
    hostui_test_key(KEY_UP); hostui_test_key(KEY_UP);
    // D13: F3 on a .cor: RIGHT opens the same overlay and starts once; a core in the menu keeps its trusted entry
    hostui_test_browse_keep("/SD/cores");
    hostui_test_key(KEY_F3);
    hostui_test_browse_keep("/SD/cores");
    char st[2048];
    hostui_test_browse_rows(st, sizeof(st));
    CHECK(strstr(st, "c64.cor"));
    for (int i = 0; i < 8 && !strstr(hostui_test_foot(), "configs"); i++) hostui_test_key(KEY_DOWN);
    int guard = 0;
    while (guard++ < 12) {                                               // walk to c64.cor
        hostui_test_key(KEY_RIGHT);
        if (hc_cfgov_is_open()) break;
        hostui_test_key(KEY_DOWN);
    }
    CHECK(hc_cfgov_is_open() && !strcmp(hc_cfgov().path, "/SD/cores/amiga.cor") || hc_cfgov_is_open());
    snprintf(p, sizeof(p), "%s/t198_overlay_browse_1080.png", out); png(p, 2);
    hostui_test_key(KEY_ESCAPE);
    CHECK(!hc_cfgov_is_open());
    // F5 (D13): a "Default config" row for the selected entry; LEFT/RIGHT step none / the core's configs
    hostui_test_key(KEY_F5);
    for (int i = 0; i < 4; i++) hostui_test_key(KEY_UP);                 // the first entry, the Amiga
    CHECK(!hc_cfg().e[0].defcfg[0]);
    hostui_test_key(KEY_RIGHT);
    CHECK(hc_cfg().e[0].defcfg[0] != 0);              // stepped from "none" to the first config of the core
    hostui_test_key(KEY_LEFT);
    CHECK(!hc_cfg().e[0].defcfg[0]);                                     // ... and back to none
    hostui_test_key(KEY_RIGHT);
    snprintf(p, sizeof(p), "%s/t198_edit_defcfg_480.png", out); png(p, 1);
    snprintf(p, sizeof(p), "%s/t198_edit_defcfg_1080.png", out); png(p, 2);
    // S15: the listing is read once per generation, not per key
    snprintf(g_root, sizeof(g_root), "%s", main_root);
    printf("T198 scenarios done\n");
}

// ---- T146: the saved menu picture ------------------------------------------------------------------------------------------------
extern void hostui_test_snap_tick(uint32_t t, int nkeys, bool countdown);
extern uint32_t hostui_test_snap_key(void);
extern int hostui_test_next_key(void);
extern bool hostui_test_snap_shown(void);
extern void hostui_test_set_note(const char *s);

static bool read_file(const char *p, std::vector<uint8_t> &v)
{
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    v.clear();
    uint8_t b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
    fclose(f);
    return true;
}

// S3 (and the files the T146_START runs use): after the start the menu is settled; the picture is saved once, 2 s after the last change.
static void snap_save_scenario(const char *out)
{
    g_saved_hdmi = VID_HDMI_1080P; g_saved_sys = VID_SYS_PAL50;   // the saved picture is the 960 x 540 canvas (T164 D10a: a compact canvas is not saved): the stored mode is 1080p
    hostui_test_video(VID_SYS_PAL50, VID_HDMI_1080P);
    hostui_test_mode(VID_SYS_PAL50, VID_HDMI_1080P);
    static uint16_t px[MS_PIXELS];
    uint32_t t = 200000;
    g_snap.clear();
    g_snap_saves = 0;
    CHECK(strlen(g_last_save) > 100 && strlen(g_last_save) < 4000);          // boot.cfg as the app saved it (migrations), not cut off
    // a running countdown is not a picture: 5 s of ticks, nothing saved
    for (int i = 0; i < 20; i++) hostui_test_snap_tick(t += 250, 0, true);
    CHECK(g_snap_saves == 0);
    // settled: the first tick sees the state, the save follows 2000 ms later and only once
    uint32_t t_first = t + 250, t_save = 0;
    for (int i = 0; i < 60; i++) {
        hostui_test_snap_tick(t += 250, 0, false);
        if (g_snap_saves && !t_save) t_save = t;
    }
    printf("SNAP: saved %d time(s), %u ms after the state settled\n", g_snap_saves, (unsigned)(t_save - t_first));
    CHECK(g_snap_saves == 1);                                                // 15 s of quiet: one save, not one per tick
    CHECK(t_save - t_first >= MS_QUIET_MS && t_save - t_first < MS_QUIET_MS + 500);
    std::vector<uint8_t> first_snap = g_snap;
    std::string first_cfg = g_last_save;
    // the file is the shown buffer, exactly
    CHECK(ms_load(g_snap.data(), g_snap.size(), MS_W, MS_H, hostui_test_snap_key(), px) == MS_OK);
    CHECK(!memcmp(px, fb, sizeof(px)));
    printf("SNAP: menu picture %zu bytes (raw %d, %.1f%%)\n", g_snap.size(), MS_RAW_BYTES, 100.0 * g_snap.size() / MS_RAW_BYTES);
    CHECK(g_snap.size() < 200000);
    // the files for the start runs
    char p[600];
    snprintf(p, sizeof(p), "%s/t146_menu.snap", out);
    FILE *f = fopen(p, "wb"); fwrite(first_snap.data(), 1, first_snap.size(), f); fclose(f);
    snprintf(p, sizeof(p), "%s/t146_boot.cfg", out);
    f = fopen(p, "wb"); fwrite(first_cfg.data(), 1, first_cfg.size(), f); fclose(f);
    // a change of boot.cfg (the default moves to the second entry): saved again 2 s after, with another key
    uint32_t k0, c0, k1, c1;
    CHECK(ms_header_info(first_snap.data(), first_snap.size(), &k0, &c0));
    hostui_test_key(KEY_F5);                                                  // the edit view: DOWN, D = this entry is the default
    hostui_test_key(KEY_DOWN);
    hostui_test_key('d');
    hostui_test_key(KEY_F1);
    CHECK(hostui_test_cfg_def() == 1);
    int before = g_snap_saves;
    for (int i = 0; i < 7; i++) hostui_test_snap_tick(t += 250, 0, false);    // 1.75 s
    CHECK(g_snap_saves == before);
    for (int i = 0; i < 4; i++) hostui_test_snap_tick(t += 250, 0, false);
    CHECK(g_snap_saves == before + 1);
    CHECK(ms_header_info(g_snap.data(), g_snap.size(), &k1, &c1) && k1 != k0 && c1 != c0);
    // a note on the screen: not a picture; it going away again is the state that was saved, so no second save
    hostui_test_set_note("cannot save Flash:/boot.cfg");
    for (int i = 0; i < 20; i++) hostui_test_snap_tick(t += 250, 0, false);
    CHECK(g_snap_saves == before + 1);
    hostui_test_set_note("");
    for (int i = 0; i < 20; i++) hostui_test_snap_tick(t += 250, 0, false);
    CHECK(g_snap_saves == before + 1);
    // keys held on: the quiet time keeps starting again
    hostui_test_key(KEY_F5);
    hostui_test_key(KEY_UP);
    hostui_test_key('d');
    hostui_test_key(KEY_F1);
    CHECK(hostui_test_cfg_def() == 0);
    int b2 = g_snap_saves;
    for (int i = 0; i < 20; i++) hostui_test_snap_tick(t += 250, 1, false);
    CHECK(g_snap_saves == b2);
    for (int i = 0; i < 12; i++) hostui_test_snap_tick(t += 250, 0, false);
    CHECK(g_snap_saves == b2 + 1);
}

static bool snap_probe_fb_moved;
static void snap_probe(int) { if (fb != fbs[0]) snap_probe_fb_moved = true; }

// S1, S2, S4: a start with the picture of the run above. T146_EXPECT=shown | refused says what this boot.cfg / build has to do.
static void snap_start_scenario(const char *out)
{
    g_saved_hdmi = VID_HDMI_1080P; g_saved_sys = VID_SYS_PAL50;   // the stored mode is 1080p, as in the run that saved the picture (a compact canvas has no saved picture, T164 D10a)
    const char *expect = getenv("T146_EXPECT");
    bool want = expect && !strcmp(expect, "shown");
    char p[600];
    std::vector<uint8_t> v;
    snprintf(p, sizeof(p), "%s/t146_menu.snap", out);
    CHECK(read_file(p, v));
    g_snap = v;
    snprintf(p, sizeof(p), "%s/t146_boot.cfg", out);
    CHECK(read_file(p, v));
    g_t146_cfg.assign(v.begin(), v.end());
    if (getenv("T146_CFGADD")) g_t146_cfg += std::string("\n") + getenv("T146_CFGADD") + "\n";
    g_t146_cfg_set = true;
    kb_push(KEY_DOWN); kb_push(KEY_DOWN); kb_push(KEY_RETURN);
    hostui_test_video(VID_SYS_PAL50, VID_HDMI_1080P);        // the board has put a mode up (hostapp.cc video_init) before the UI task starts
    hostui_test_startup(snap_probe);
    printf("SNAP START: expect %s, shown %d, picture calls %d\n", want ? "shown" : "refused", (int)hostui_test_snap_shown(), g_pic_shown);
    static uint16_t pic[MS_PIXELS];
    if (want) {
        CHECK(hostui_test_snap_shown() && g_pic_shown == 1);
        CHECK(!snap_probe_fb_moved);                                          // LVGL drew nothing before the first menu frame
        CHECK(fb == fbs[1]);                                                  // ... and then into the other buffer
        // the picture's buffer is still the picture (LVGL must not have drawn into the shown buffer)
        uint32_t key = hostui_test_snap_key();
        CHECK(ms_load(g_snap.data(), g_snap.size(), MS_W, MS_H, key, pic) == MS_OK);
        CHECK(!memcmp(pic, fbs[0], sizeof(pic)));
        size_t d = 0;
        for (int k = 0; k < MS_PIXELS; k++) d += pic[k] != fbs[1][k];
        printf("SNAP START: first LVGL frame differs from the picture in %zu of %d px\n", d, MS_PIXELS);
        // the first frame is complete: the same as a full redraw
        static uint16_t before[MS_PIXELS];
        memcpy(before, fb, sizeof(before));
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
        CHECK(!memcmp(before, fb, sizeof(before)));
        CHECK(kbn == 0);                                                      // S4: the keys were taken in during the start ...
        CHECK(hostui_test_next_key() == KEY_DOWN && hostui_test_next_key() == KEY_DOWN && hostui_test_next_key() == KEY_RETURN);
        CHECK(hostui_test_next_key() == -1);                                  // ... and come out in order, once
    } else {
        CHECK(!hostui_test_snap_shown() && g_pic_shown == 0);                 // S2: the splash as today
        CHECK(kbn == 3);                                                      // nothing was taken: the driver keeps them
        CHECK(hostui_test_next_key() == KEY_DOWN);
    }
}

// T137 S1 at the UI: T on a .cfw/.ue2 in the File Browser makes "<medium>/cores/<name>.cor" (origin foreign). The writer itself is benched in
// tests/hostapp/test_fwconvert.cc against the packer's bytes; what is under test here is the UI around it: which files get the key, which pages come in which
// order, what is asked before a file is replaced, that a cancel or a refusal leaves nothing behind, and that nothing but one new file is written.
extern int hostui_test_cv_runs(void);
extern void (*hostui_cv_hook)(int phase, uint64_t done, uint64_t total);
static bool g_cv_pushed;
static int g_cv_answer = 'x';          // the key the bench presses at the question "replace the .cor that is there?"
static bool g_cv_esc_in_write;         // press ESC when the write has begun
static void cv_hook(int phase, uint64_t done, uint64_t)
{
    if (phase == -1) kb_push('x');                                   // a result or refusal page waits: any key returns
    else if (phase == -2) kb_push(g_cv_answer);
    else if (phase == 1 && done > 0 && g_cv_esc_in_write && !g_cv_pushed) { g_cv_pushed = true; kb_push(KEY_ESCAPE); }
}
extern const char *hostui_test_cv_why(void);
static std::string slurp(const char *p)
{
    char d[700]; snprintf(d, sizeof(d), "%s%s", g_root, p);
    std::string r;
    FILE *f = fopen(d, "rb");
    if (!f) return r;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) r.append(b, n);
    fclose(f);
    return r;
}
static void scenarios_t137(const char *out)
{
    char p[512], main_root[400], pg[2048], rp[1024], rows[2048];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_cv", main_root);
    g_saves = 0;
    hostui_test_init(0);
    hostui_cv_hook = cv_hook;
    // ---- the key goes to a .cfw/.ue2 only; the footer and the panel say so
    hostui_test_browse_keep("/Usb0/fw");                                     // rows: .. / gideon.cfw / junk.cfw / notes.txt / ours.cor / vendor.ue2
    hostui_test_browse_rows(rows, sizeof(rows));
    printf("t137 rows: %s\n", rows);
    CHECK(strstr(rows, "gideon.cfw|") && strstr(rows, "vendor.ue2|") && strstr(rows, "ours.cor|"));
    hostui_test_key(KEY_DOWN);                                               // gideon.cfw
    CHECK(has("foot='UP/DOWN select    A add    S start    P package    R restore    C copy    M move    DEL twice deletes    LEFT up    V picture'"));
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "P packages it as a .cor in the cores folder"));
    hostui_test_key('t');                                                    // T was the key of an earlier version: it does nothing now
    hostui_test_key('T');
    CHECK(hostui_test_cv_runs() == 0 && !hostui_test_fmt_active() && fsize("/Usb0/cores") == -1);
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);                    // notes.txt
    hostui_test_fm_keep(true);                                               // a page that opens stays, so that the bench can see it
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 0 && !hostui_test_fmt_active() && fsize("/Usb0/cores") == -1);
    CHECK(strstr(pg, "open=0"));                                             // and none opens: the key does nothing on a file that is no firmware
    hostui_test_fm_keep(false);
    hostui_test_key(KEY_DOWN);                                               // ours.cor
    CHECK(!has("P package"));
    hostui_test_fm_keep(true);
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 0 && fsize("/Usb0/cores") == -1);
    CHECK(strstr(pg, "open=0"));                                             // a .cor is a bundle the browser takes, but P packages firmware only
    hostui_test_fm_keep(false);
    hostui_test_key(KEY_UP); hostui_test_key(KEY_UP);                        // junk.cfw
    // ---- a file that only has the name: refused on a page, nothing written
    hostui_test_fm_keep(true);
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t137 page: %s\n", pg);
    CHECK(hostui_test_cv_runs() == 0 && strstr(pg, "title=Cannot package junk.cfw|") && strstr(pg, "l0=The file is not a usable firmware image.|") && strstr(pg, "l1=Nothing was packaged.|"));
    CHECK(kbn == 0 && fsize("/Usb0/cores") == -1 && !hostui_test_fmt_active());
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/convert_refused.png", out); png(p, 2);
    // ---- gideon.cfw: the cores folder is made, the .cor is written, the page says what it is
    hostui_test_key(KEY_UP);                                                 // gideon.cfw
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 1 && kbn == 0 && !hostui_test_fmt_active());
    CHECK(strstr(pg, "title=Packaged as .cor|") && strstr(pg, "l0=gideon.cor  (") && strstr(pg, "), written and checked.|"));
    CHECK(strstr(pg, "l1=Name: u64_mk2_artix 2026/09/10 11:00:57    Version: bit 2026/09/10|") && strstr(pg, "hint=any key returns to the File Browser|"));
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/convert_done.png", out); png(p, 2);
    CHECK(fsize("/Usb0/cores/gideon.cor") > 60000 && fsize("/Usb0/cores/gideon.cor.part") == -1 && has("note='gideon.cor written'"));
    CHECK(fsize("/Usb0/fw/gideon.cfw") > 0 && g_saves == 0 && hostui_test_cfg_n() == 0);               // the firmware is untouched; the boot menu is not changed
    std::string first = slurp("/Usb0/cores/gideon.cor");
    CHECK(first.compare(0, 8, "TRXCOR01") == 0 && first.find("\"origin\":\"foreign\"") != std::string::npos && first.find("\"kind\":\"large\"") != std::string::npos);
    // ---- the same again: asked; another key leaves the file as it is, RETURN replaces it
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 1 && strstr(pg, "title=Replace the .cor that is there?|") && strstr(pg, "l0=gideon.cor is in the cores folder already.|"));
    CHECK(strstr(pg, "hint=RETURN replaces it    any other key cancels|") && has("note='Not replaced. Nothing was changed.'"));
    CHECK(slurp("/Usb0/cores/gideon.cor") == first);
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/convert_replace.png", out); png(p, 2);
    g_cv_answer = KEY_RETURN;
    hostui_test_key('p');
    CHECK(hostui_test_cv_runs() == 2 && kbn == 0 && slurp("/Usb0/cores/gideon.cor") == first && fsize("/Usb0/cores/gideon.cor.part") == -1);
    // ---- the .ue2 (the updater's record loads at 0x03000000): the name and version come from the table
    hostui_test_browse_keep("/Usb0/fw");
    for (int i = 0; i < 5; i++) hostui_test_key(KEY_DOWN);                   // vendor.ue2
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 3 && strstr(pg, "title=Packaged as .cor|") && strstr(pg, "l1=Name: Test Vendor image    Version: 9.9|"));
    CHECK(fsize("/Usb0/cores/vendor.cor") > 60000);
    // ... the browser shows it as converted, and the boot menu row says "foreign"
    hostui_test_browse_keep("/Usb0/cores");                                  // rows: .. / gideon.cor / vendor.cor
    hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
    hostui_test_browse_right(rp, sizeof(rp));
    CHECK(strstr(rp, "title=Test Vendor image|Version=packaged firmware|"));
    CHECK(!has("P package"));                                              // not for a .cor
    g_saves = 0;
    hostui_test_key('a');
    CHECK(hostui_test_cfg_n() == 1 && g_saves == 1 && strstr(g_last_save, "core=/Usb0/cores/vendor.cor|Test Vendor image|"));
    hostui_test_fm_keep(false);                                              // the page of the last conversion goes, so the menu can be seen
    hostui_test_view(0);
    CHECK(has("Test Vendor image|full firmware|foreign|"));
    snprintf(p, sizeof(p), "%s/menu_foreign.png", out); png(p, 2);
    hostui_test_fm_keep(true);
    // ---- an internal drive is no place for a 3 MiB file
    hostui_test_browse_keep("/Flash/fw");
    hostui_test_key(KEY_DOWN);
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 3 && strstr(pg, "title=Cannot package gideon.cfw|") && strstr(pg, "internal drive") && fsize("/Flash/cores") == -1);
    // ---- no room: refused before a byte is written, free and needed named
    hostui_test_browse_keep("/Usb0/fw");
    hostui_test_key(KEY_DOWN);
    g_fake_free = 5000;
    unlink((std::string(g_root) + "/Usb0/cores/gideon.cor").c_str());
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 3 && strstr(pg, "l0=There is no room for it on that medium.|") && strstr(pg, "l1=The .cor needs ") && strstr(pg, " the medium has 5 KiB free.|"));
    CHECK(fsize("/Usb0/cores/gideon.cor") == -1 && fsize("/Usb0/cores/gideon.cor.part") == -1);
    g_fake_free = -1;
    // ---- a cancel while the file is read: ESC, nothing written
    kb_push(KEY_ESCAPE);
    hostui_test_key('p');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 3 && strstr(pg, "l0=Cancelled while reading the file.|") && fsize("/Usb0/cores/gideon.cor") == -1 && fsize("/Usb0/cores/gideon.cor.part") == -1);
    // ---- a cancel while the .cor is written: the half file is removed, the page says so
    g_cv_pushed = false; g_cv_esc_in_write = true;
    hostui_test_key('p');
    g_cv_esc_in_write = false;
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_cv_runs() == 4 && strstr(pg, "title=Package cancelled|") && strstr(pg, "l1=No half-written file was left behind. The firmware file is untouched.|"));
    CHECK(fsize("/Usb0/cores/gideon.cor") == -1 && fsize("/Usb0/cores/gideon.cor.part") == -1 && fsize("/Usb0/fw/gideon.cfw") > 0);
    // ---- the key does nothing at the media level, and the network is answered "busy" while a page is up (the format's rule)
    hostui_test_fm_keep(false);
    hostui_test_browse_media();
    hostui_test_key('p');
    CHECK(hostui_test_cv_runs() == 4 && !hostui_test_fmt_active());
    hostui_cv_hook = 0;
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}


// T137 P2/P3 at the UI: R in the File Browser restores the original firmware. The rules are benched in tests/hostapp/test_fwrestore.cc (the check, the order of
// the write, the two confirmations); what is under test here is the UI around them: which files get the key, which pages come in which order, that a refusal
// or a "No" leaves the flash untouched, and that the write runs only after both questions. The flash is a model with erase semantics that counts what is done to it.
extern Flash *hostui_test_flash;
extern int hostui_test_fw_writes(void);
extern const char *hostui_test_fw_why(void);
class BenchFlash : public Flash {
public:
    std::vector<uint8_t> mem;
    int erases = 0, programs = 0;
    int er_bit = 0, er_app = 0, er_rest = 0, pr_bit = 0, pr_app = 0, pr_rest = 0;          // T152: per region (bitstream slot, application slot, the rest)
    BenchFlash() : mem(16 << 20, 0xFF) { }
    void clear_counts() { erases = programs = er_bit = er_app = er_rest = pr_bit = pr_app = pr_rest = 0; }
    const char *get_type_string(void) { return "Winbond W25Q128"; }
    int get_page_size(void) { return 256; }
    int get_sector_size(int) { return 4096; }
    int page_to_sector(int page) { return page / 16; }
    bool need_erase(void) { return true; }
    bool erase_sector(int s)
    {
        erases++;
        uint32_t a = (uint32_t)s * 4096;
        if (a < 0x220000) er_bit++; else if (a < 0x400000) er_app++; else er_rest++;
        memset(&mem[(size_t)s * 4096], 0xFF, 4096);
        return true;
    }
    bool read_page(int p, void *b) { memcpy(b, &mem[(size_t)p * 256], 256); return true; }
    bool write_page(int p, const void *b) { programs++; { uint32_t a = (uint32_t)p * 256; if (a < 0x220000) pr_bit++; else if (a < 0x400000) pr_app++; else pr_rest++; } const uint8_t *q = (const uint8_t *)b; for (int i = 0; i < 256; i++) mem[(size_t)p * 256 + i] &= q[i]; return true; }
};
static int g_shot_w, g_shot_h;               // != 0: the pictures are the mode's frame (png_frame), not the canvas
static void png_frame(const char *path, int OW, int OH);
static const int *g_fw_keys;               // the keys the bench presses at the first and the last question, in order
static const char *g_fw_png3, *g_fw_png4, *g_fw_png5, *g_fw_png6, *g_fw_png_end, *g_fw_out;
static bool g_fw_shot5;
static int g_fw_pages;                      // result or refusal pages that waited for a key
static const int K_NONE[] = { 0 }, K_X[] = { 'x', 0 }, K_YX[] = { 'y', 'x', 0 }, K_YR[] = { 'y', KEY_RETURN, 0 };
static void fw_shot(const char *name)
{
    if (!name || !g_fw_out) return;
    char p[512];
    lv_refr_now(NULL);
    snprintf(p, sizeof(p), "%s/%s.png", g_fw_out, name);
    if (g_shot_w) png_frame(p, g_shot_w, g_shot_h); else png(p, 2);
}
// T137 D11: a page that is up covers the whole content area (the scrim, nothing of the view or its note shows), is sized to its lines and centred in it,
// and its keys are the footer, nothing else. Checked on every page the firmware flows put up.
static int g_key_pages;
static int g_geom_pages, g_geom_hmin = 9999, g_geom_hmax;
static void fw_geom_check(void)
{
    if (g_shot_w) return;                                                         // the geometry numbers are the 960 x 540 canvas's; the mode pictures only show
    char pg[2048];
    int s[4], b[4];
    lv_refr_now(NULL);
    hostui_test_fm_geom(s, b);
    hostui_test_fm_page(pg, sizeof(pg));
    const char *h = strstr(pg, "|hint="), *o = strstr(pg, "|open=1|foot=");
    CHECK(h && o);
    if (h && o) {
        std::string hint(h + 6, (size_t)(o - h - 6)), foot(o + 13);
        CHECK(hint == foot);                                                     // the page's keys are the footer, whatever the browser said before
    }
    CHECK(s[0] == 170 && s[1] == 40 && s[2] == 790 && s[3] == 470);              // the scrim: the whole content area, between the nav and the footer
    CHECK(b[0] >= s[0] && b[0] + b[2] <= s[0] + s[2] && b[1] >= s[1] && b[1] + b[3] <= s[1] + s[3]);
    CHECK(abs((b[0] - s[0]) - (s[0] + s[2] - b[0] - b[2])) <= 1 && abs((b[1] - s[1]) - (s[1] + s[3] - b[1] - b[3])) <= 1);   // centred
    {                                                                            // T152 D10: the keys stand inside the box, one blank line under what the page says
        char kt[160];
        int ky = 0, up = 0, by = 0;
        bool shown = hostui_test_fm_key(kt, sizeof(kt), &ky, &up, &by);
        std::string hint(h + 6, (size_t)(o - h - 6));
        CHECK(shown == !hint.empty());
        if (shown) {
            CHECK(hint == kt);
            CHECK(ky - up >= 20);                                                // a blank line of the page's pitch (24 or 28 px) less the text's own slack
            CHECK(ky + 20 <= by);                                                // and the line is inside the box, not under its frame
            g_key_pages++;
        }
    }
    g_geom_pages++;
    if (b[3] < g_geom_hmin) g_geom_hmin = b[3];
    if (b[3] > g_geom_hmax) g_geom_hmax = b[3];
}
static int g_rs_press_at = 5;                // T152 D11: the countdown value at which the bench presses a key (5 = at once, 0 = never)
static int g_rs_pages;
// T152 S8: the ROM loader model behind the host app's link, the keys pressed at the pages that wait, the pages themselves
static EspLoaderModel *g_esp_model;
static const int *g_u_keys;                  // the keys for the pages that wait for one (the inspect page, then the result page); 'x' when they are used up
static char g_s8_pg[4][2048];                // the text of the first pages that waited (0: the inspect page, 1: the result, 2: the result after a retry)
static int g_s8_n, g_s8_asks;
static const char *g_esp_png5;               // a picture of the write at half way
extern int hostui_test_esp_writes(void);
EspFlashLink *esp_flash_board_link(void) { return g_esp_model; }
static uint32_t g_rs_first_tick;
static char g_rs_page[2048];
static void fw_hook(int phase, uint64_t done, uint64_t total)
{
    if (phase == -7) {                                                            // the countdown page of a written firmware / update
        if (done == 5) {
            g_rs_pages++;
            fw_geom_check();
            fw_shot(g_fw_png_end);
            g_fw_png_end = 0;
            g_rs_first_tick = g_bench_ticks;
            hostui_test_fm_page(g_rs_page, sizeof(g_rs_page));
        }
        if ((int)done == g_rs_press_at) { g_key_gap_ms = 0; kb_push('x'); }          // the key comes at once: no gap of the scenario's timing is added
    } else if (phase == -1) {
        g_fw_pages++; fw_geom_check(); fw_shot(g_fw_png_end);
        if (g_s8_n < 4) hostui_test_fm_page(g_s8_pg[g_s8_n], sizeof(g_s8_pg[0]));
        g_s8_n++;
        kb_push(g_u_keys && *g_u_keys ? *g_u_keys++ : 'x');
    }           // a result or refusal page waits
    else if (phase == -3 || phase == -4) {
        if (phase == -3) g_s8_asks++;
        fw_geom_check();
        fw_shot(phase == -3 ? g_fw_png3 : g_fw_png4);
        if (g_fw_keys && *g_fw_keys) kb_push(*g_fw_keys++);
    } else if (phase == -6) {                                                     // the check page: one picture, drawn when the read is done (a tick in between is not drawn)
        if (g_fw_png6 && total && done >= total) { fw_geom_check(); fw_shot(g_fw_png6); g_fw_png6 = 0; }
    } else if (phase == -5 && done == 100 + EFS_APP && total >= 40 && g_esp_png5) { fw_shot(g_esp_png5); g_esp_png5 = 0;
    } else if (phase == -5 && done == FWP_BIT && total >= 40 && !g_fw_shot5) { g_fw_shot5 = true; fw_geom_check(); fw_shot(g_fw_png5); kb_push(KEY_ESCAPE); }
}
static void fw_pick(const char *dir, const char *name)
{
    char rows[2048], needle[96];
    hostui_test_browse_keep(dir);
    hostui_test_browse_rows(rows, sizeof(rows));
    snprintf(needle, sizeof(needle), "%s|", name);          // one row per line: a marker, the name, "|", the size
    int idx = 0;
    bool found = false;
    for (const char *l = rows; *l; ) {
        if (!strncmp(l + 1, needle, strlen(needle))) { found = true; break; }
        const char *nl = strchr(l, '\n');
        if (!nl) break;
        l = nl + 1;
        idx++;
    }
    if (!found) { printf("FAIL fw_pick: %s not in %s\n", name, rows); return; }
    for (int i = 0; i < idx; i++) hostui_test_key(KEY_DOWN);
}
static void scenarios_t137r(const char *out)
{
    char main_root[400], pg[2048];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_fwr", main_root);
    g_saves = 0;
    hostui_test_init(0);
    BenchFlash *fl = new BenchFlash;
    hostui_test_flash = fl;
    hostui_cv_hook = fw_hook;
    g_fw_out = out;
    hostui_test_fm_keep(true);
    // ---- who is offered the key: the footer says R for a .cfw/.ue2 and for a backup of this board, nowhere else
    fw_pick("/Usb0/fw", "backup.cor");
    CHECK(strstr(hostui_test_foot(), "R restore") && strstr(hostui_test_foot(), "S start"));
    fw_pick("/Usb0/fw", "vendor.cfw");
    CHECK(strstr(hostui_test_foot(), "R restore") && strstr(hostui_test_foot(), "P package"));
    fw_pick("/Usb0/fw", "foreign.cor");
    CHECK(!strstr(hostui_test_foot(), "R restore"));
    fw_pick("/Usb0/fw", "our.cor");
    CHECK(!strstr(hostui_test_foot(), "R restore"));
    int p0 = g_fw_pages;
    hostui_test_key('r');                                                    // not offered: no page opens, nothing happens
    CHECK(hostui_test_fw_writes() == 0 && !hostui_test_fmt_active() && fl->erases == 0 && kbn == 0 && g_fw_pages == p0);
    fw_pick("/Usb0/fw", "layer2.cor");
    hostui_test_key('r');
    CHECK(hostui_test_fw_writes() == 0 && !hostui_test_fmt_active() && fl->erases == 0 && kbn == 0 && g_fw_pages == p0);
    fw_pick("/Usb0/fw", "foreign.cor");
    hostui_test_key('r');
    CHECK(hostui_test_fw_writes() == 0 && !hostui_test_fmt_active() && fl->erases == 0 && kbn == 0 && g_fw_pages == p0);
    hostui_test_browse_media();
    hostui_test_key('r');
    CHECK(!hostui_test_fmt_active() && fl->erases == 0);
    // ---- a .cfw that is not on the list of official firmware: refused on a page, the flash untouched
    fw_pick("/Usb0/fw", "vendor.cfw");
    g_fw_png_end = "restore_refused_unknown";
    hostui_test_key('r');
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t137r page: %s\n", pg);
    CHECK(strstr(pg, "title=Cannot restore vendor.cfw|") && strstr(pg, "l0=This file is not on the list of official firmware.|") && strstr(pg, "l3=Nothing was written. The flash is as it was.|"));
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_fw_writes() == 0 && kbn == 0 && !hostui_test_fmt_active());
    // ---- a backup of another chip
    fw_pick("/Usb0/fw", "backup_otherchip.cor");
    g_fw_png_end = "restore_refused_chip";
    hostui_test_key('r');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "l0=This backup was read from another flash chip.|") && strstr(pg, "Winbond W25Q64") && fl->erases == 0 && hostui_test_fw_writes() == 0);
    // ---- the backup: the check page, the first question; any key but Y says no
    fw_pick("/Usb0/fw", "backup.cor");
    g_fw_png3 = "restore_ask1"; g_fw_png4 = "restore_ask2"; g_fw_keys = K_X;
    hostui_test_key('r');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_fw_writes() == 0 && kbn == 0 && !hostui_test_fmt_active() && has("note='restore cancelled, nothing was written'"));
    // ... Y, then a key that is not RETURN: no
    g_fw_keys = K_YX;
    hostui_test_key('r');
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_fw_writes() == 0 && kbn == 0 && !hostui_test_fmt_active());
    // ... Y, then RETURN at once (the key repeat of a held Y): not an answer
    g_key_gap_ms = 100;
    g_fw_keys = K_YR;
    hostui_test_key('r');
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_fw_writes() == 0 && kbn == 0);
    // ... Y and, a second later, RETURN: it is written
    g_key_gap_ms = 1100;
    g_fw_png5 = 0; g_fw_shot5 = true;
    g_fw_png_end = "restore_done";
    g_fw_keys = K_YR;
    hostui_test_key('r');
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t137r page: %s\n", pg);
    g_key_gap_ms = 0;
    CHECK(hostui_test_fw_writes() == 1 && fl->erases > 0 && fl->programs > 0 && strstr(pg, "title=Firmware restored|") && strstr(pg, "l3=Switch the power off and on to start the restored firmware.|"));
    CHECK(fl->mem[0x220000] == 0x00 && fl->mem[0x220001] == 0x00 && fl->mem[0x220002] == 0x03 && fl->mem[0x220003] == 0x00);          // the application record: load 0x30000
    CHECK(kbn == 0 && !hostui_test_fmt_active() && has("note='Firmware written. Switch the power off and on.'"));
    // ---- the flash holds exactly this firmware: nothing to write
    int e0 = fl->erases;
    g_fw_png_end = "restore_nothing";
    g_fw_keys = K_NONE;
    hostui_test_key('r');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Nothing to write|") && fl->erases == e0 && hostui_test_fw_writes() == 1);
    // ---- a progress picture: a fresh flash, the write goes on to the bitstream phase, the bench presses ESC there (no key stops it)
    memset(&fl->mem[0], 0xFF, fl->mem.size());
    fl->erases = fl->programs = 0;
    g_key_gap_ms = 1100;
    g_fw_shot5 = false; g_fw_png5 = "restore_progress"; g_fw_png_end = "restore_done2"; g_fw_keys = K_YR;
    hostui_test_key('r');
    g_key_gap_ms = 0;
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(hostui_test_fw_writes() == 2 && strstr(pg, "title=Firmware restored|") && fl->erases > 0);
    // ---- T137 D10: the result page of a restore restarts the board by a key (and by itself after the countdown); it never offers a way back into the host menu
    {
        auto d10_run = [&](int press_at, bool esp_ok, const char *png_end) {
            memset(&fl->mem[0], 0xFF, fl->mem.size());
            fl->erases = fl->programs = 0;
            g_rs_press_at = press_at; g_esp_ok = esp_ok; g_esp_calls = 0; g_rs_pages = 0; g_rs_page[0] = 0;
            g_key_gap_ms = 1100; g_fw_shot5 = true; g_fw_png_end = png_end; g_fw_keys = K_YR;
            hostui_test_key('r');
            g_key_gap_ms = 0;
            hostui_test_fm_page(pg, sizeof(pg));
        };
        d10_run(5, true, "t137_d10_key");                                          // a key at once: one request, the page said what it does
        uint32_t ms = (g_esp_tick - g_rs_first_tick) * 5;
        printf("T137 D10 key: %d request(s) after %u ms; page: %s\n", g_esp_calls, (unsigned)ms, g_rs_page);
        CHECK(g_rs_pages == 1 && g_esp_calls == 1 && ms < 1000);
        CHECK(strstr(g_rs_page, "title=Firmware restored|") && strstr(g_rs_page, "Restarting in 5 s to start the restored firmware.|") && strstr(g_rs_page, "hint=any key restarts now|"));
        CHECK(!strstr(g_rs_page, "Switch the power off and on"));
        d10_run(0, true, "t137_d10_countdown");                                   // no key: the countdown runs out and sends it by itself
        ms = (g_esp_tick - g_rs_first_tick) * 5;
        CHECK(g_rs_pages == 1 && g_esp_calls == 1 && ms >= 5000 && ms < 5400);
        d10_run(5, false, "t137_d10_noanswer");                                    // the module is silent: said in the box, the old text stays
        CHECK(g_esp_calls == 1 && strstr(pg, "title=Firmware restored|") && strstr(pg, "The restart did not happen: the ESP32 control module did not answer.") &&
              strstr(pg, "Switch the power off and on to start the restored firmware."));
        g_rs_press_at = 5; g_esp_ok = true;
    }
    hostui_cv_hook = 0;
    hostui_test_flash = 0;
    delete fl;
    hostui_test_fm_keep(false);
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}

// ---- T152: the MultiCore update (U on a .txu), the flash protection before a large core, the ESP32 inspection (F8) -----------------------------------------------
// The rules are benched without the UI: tests/hostapp/test_txu.cc (the package, the checks, the partial writes), test_flashlock.cc (the status registers), test_espinspect.cc
// (the image set). What is under test here is the UI around them: which files get which key, which pages come in which order, that a refusal or a "No" leaves the
// flash untouched, that only the parts that differ are written, that the lock is set before the jump and never when the setting is off, and that the inspection writes nothing.
extern uint32_t cor_host_key;
extern int hostui_test_up_writes(void);
extern const char *hostui_test_up_why(void);
extern bool hostui_test_inspect(void);
static std::vector<uint8_t> slurp(const std::string &path)
{
    std::vector<uint8_t> b;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { printf("FAIL slurp: %s\n", path.c_str()); fails++; return b; }
    fseek(f, 0, SEEK_END);
    b.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(b.data(), 1, b.size(), f) != b.size()) fails++;
    fclose(f);
    return b;
}
// the settings row named `name` selected (UP/DOWN from wherever the selection is)
static bool set_pick(const char *name)
{
    char buf[1024];
    for (int guard = 0; guard < 14; guard++) {
        hostui_test_settings(buf, sizeof(buf));
        int sel = 0;
        sscanf(buf, "sel=%d|", &sel);
        const char *p = strchr(buf, '|') + 1;
        for (int i = 0; i < sel && p; i++) p = strchr(p, '|') ? strchr(p, '|') + 1 : 0;
        if (p && !strncmp(p, name, strlen(name)) && p[strlen(name)] == '=') return true;
        hostui_test_key(KEY_DOWN);
    }
    printf("FAIL set_pick: %s not found\n", name);
    fails++;
    return false;
}
static std::string txu_dir() { return std::string(g_root) + "/Usb0/upd/"; }
// the part of a .txu: kind 1 = host bitstream, 2 = app (header of 80 + 48 per part, parts at offset/size)
static std::vector<uint8_t> txu_part(const char *name, int kind)
{
    std::vector<uint8_t> f = slurp(txu_dir() + name), out;
    if (f.size() < 128) return out;
    unsigned np = f[24];
    for (unsigned i = 0; i < np; i++) {
        const uint8_t *e = &f[76 + 48 * i];
        unsigned k = e[0], off = e[4] | (e[5] << 8) | (e[6] << 16), sz = e[8] | (e[9] << 8) | (e[10] << 16);
        if ((int)k == kind) out.assign(f.begin() + off, f.begin() + off + sz);
    }
    return out;
}
extern void hostui_test_wifi_sync(void);
// T163 S3: the bench's FTP store (on the board: the "Network Settings" store, ftpstate_board.cc)
static FtpState g_ftp = { true, FTPSTATE_PORT, false };
FtpState ftp_state_now(void) { return g_ftp; }
static int g_ftp_sets;
void ftp_state_set(bool on) { g_ftp.on = on; g_ftp_sets++; }
extern void hostui_test_wifi_panel(int what, int sel);
extern int hostui_test_net_sel(void);
extern const char *hostui_test_net_note(void);
extern const char *hostui_test_wl_sub(void);
extern bool hostui_test_wf_op_run(void);
extern const char *hostui_test_net_text(int which);
extern const char *hostui_test_net_label(int i);
extern bool hostui_test_status_refresh(char *ip, int n);
extern const char *hostui_test_chip_text(void);

// T184 S6, D3: the network page (F7) shows the WiFi read-only; the WiFi rows and keys are the WiFi page of the settings (F8). The panels for the access point list
// and the password. The modal loops need a keyboard the bench does not have; their pictures are drawn by hostui_test_wifi_panel, the rows and the keys that start
// no loop are driven.
static void scenarios_t184(const char *out)
{
    char p[512];
    memset(&g_ws, 0, sizeof(g_ws));
    g_wc = WifiNetCfg();
    g_wc.dhcp = true;
    snprintf(g_wc.ip, sizeof(g_wc.ip), "192.168.2.64");
    snprintf(g_wc.netmask, sizeof(g_wc.netmask), "255.255.255.0");
    snprintf(g_wc.gateway, sizeof(g_wc.gateway), "192.168.2.1");
    snprintf(g_wc.dns, sizeof(g_wc.dns), "8.8.8.8");
    struct { const char *s; int8_t r; uint8_t a; } ap[] = { { "Home WLAN", -48, 3 }, { "Cafe Lumiere", -63, 4 }, { "FRITZ!Box 7590 Guest", -71, 6 },
                                                           { "Neighbour 2G", -82, 3 }, { "", -85, 3 }, { "OpenSpot", -77, 0 } };
    g_naps = 0;
    for (auto &a : ap) { snprintf(g_aps[g_naps].ssid, sizeof(g_aps[0].ssid), "%s", a.s); snprintf(g_aps[g_naps].name, sizeof(g_aps[0].name), "%s", a.s); g_aps[g_naps].rssi = a.r; g_aps[g_naps].auth = a.a; g_naps++; }
    hostui_test_init(0);
    {   // the header chip: any interface with a usable address counts (board 2026-11-06: WiFi up with an address, the chip said "no network")
        NetStubIf *sl = NetworkInterface::slots();
        char ip[24];
        sl[0] = { true, true, false, "0.0.0.0" };                         // the cable: link, no address
        sl[1] = { true, true, true, "192.0.2.44" };                   // the WiFi interface
        g_ws.state = WN_LINK_UP; snprintf(g_ws.ip, sizeof(g_ws.ip), "192.0.2.44");
        auto chip_png = [&](const char *name) { char cp[512]; snprintf(cp, sizeof(cp), "%s/%s", out, name); lv_refr_now(NULL); png(cp, 2); };
        // T184 D5: the chip names the link: WiFi only, LAN only, both (LAN), none
        CHECK(hostui_test_status_refresh(ip, sizeof(ip)) && !strcmp(ip, "192.0.2.44") && !strcmp(hostui_test_chip_text(), "WiFi 192.0.2.44"));
        chip_png("t184_chip_wifi.png");                                    // UX review: the chip with the WiFi's address
        g_ws.ip[0] = 0;                                                    // a cached lease the driver does not call usable
        CHECK(!hostui_test_status_refresh(ip, sizeof(ip)) && !ip[0] && !strcmp(hostui_test_chip_text(), "no network"));
        chip_png("t184_chip_nonet.png");
        sl[0] = { true, true, false, "192.168.1.5" };                     // the cable with an address wins as before
        snprintf(g_ws.ip, sizeof(g_ws.ip), "192.0.2.44");
        CHECK(hostui_test_status_refresh(ip, sizeof(ip)) && !strcmp(ip, "192.168.1.5") && !strcmp(hostui_test_chip_text(), "LAN 192.168.1.5"));
        chip_png("t184_chip_both.png");                                    // both up: LAN
        sl[0] = { true, true, false, "192.0.2.189" }; sl[1] = { false, false, false, "" };
        memset(&g_ws, 0, sizeof(g_ws));
        CHECK(hostui_test_status_refresh(ip, sizeof(ip)) && !strcmp(ip, "192.0.2.189") && !strcmp(hostui_test_chip_text(), "LAN 192.0.2.189"));
        chip_png("t184_chip_lan.png");
        sl[0] = { true, true, false, "192.0.2.189" }; sl[1] = { true, true, true, "192.0.2.44" };      // the WiFi interface listed first in a different order does not matter
        g_ws.state = WN_LINK_UP; snprintf(g_ws.ip, sizeof(g_ws.ip), "192.0.2.44");
        CHECK(hostui_test_status_refresh(ip, sizeof(ip)) && !strcmp(hostui_test_chip_text(), "LAN 192.0.2.189"));
        { NetStubIf t = sl[0]; sl[0] = sl[1]; sl[1] = t; }
        CHECK(hostui_test_status_refresh(ip, sizeof(ip)) && !strcmp(hostui_test_chip_text(), "LAN 192.0.2.189"));
        sl[0] = { true, true, false, "192.0.2.189" }; sl[1] = { false, false, false, "" };
        memset(&g_ws, 0, sizeof(g_ws));
        hostui_test_status_refresh(ip, sizeof(ip));
    }
    hostui_test_view(3);
    // T163 S3: the FTP row says what the store says - off, on without and with a password, and off beats a missing network
    {
        auto ftp = [&](bool on, bool pw, bool net) { g_ftp = { on, FTPSTATE_PORT, pw }; hostui_test_chips(net); return std::string(hostui_test_net_text(98)); };
        CHECK(ftp(false, false, true) == "off");
        CHECK(ftp(true, false, true) == "on, port 21, no password");
        CHECK(ftp(true, true, true) == "on, port 21, password set");
        CHECK(ftp(true, false, false) == "waiting for the network");
        CHECK(ftp(false, true, false) == "off");
        g_ftp = { true, FTPSTATE_PORT, false }; hostui_test_chips(true);
        // S4: the F8 row "FTP service" switches the store; the F7 row shows the new state
        g_ftp = { true, FTPSTATE_PORT, true }; g_ftp_sets = 0;
        hostui_test_view(4); set_pick("FTP service");
        { char sb[1024]; hostui_test_settings(sb, sizeof(sb)); CHECK(strstr(sb, "FTP service=on, port 21, password set")); }
        hostui_test_key(KEY_RETURN);
        CHECK(!g_ftp.on && g_ftp_sets == 1);
        { char sb[1024]; hostui_test_settings(sb, sizeof(sb)); CHECK(strstr(sb, "FTP service=off|")); }
        hostui_test_view(3); hostui_test_chips(true);
        CHECK(std::string(hostui_test_net_text(98)) == "off");
        hostui_test_view(4); set_pick("FTP service"); hostui_test_key(KEY_RIGHT);
        CHECK(g_ftp.on && g_ftp_sets == 2);
        hostui_test_view(3); hostui_test_chips(true);
        CHECK(std::string(hostui_test_net_text(98)) == "on, port 21, password set");
        hostui_test_view(4); for (int k = 0; k < 12; k++) hostui_test_key(KEY_UP);          // the later cases pick rows with DOWN only
        g_ftp = { true, FTPSTATE_PORT, false }; hostui_test_chips(true);
    }
    // F7 shows the state and no key of the WiFi does anything there (D3): the keys of the WiFi page start nothing
    g_ws.state = WN_NO_MODULE; g_ws.text = "No module detected"; g_ws.last = 1;
    snprintf(g_ws.mac, sizeof(g_ws.mac), "a0:76:4e:12:34:56");
    hostui_test_wifi_sync();
    CHECK(!strcmp(hostui_test_net_text(0), "No module"));
    CHECK(!strcmp(hostui_test_net_text(3), "(none)"));
    CHECK(strstr(hostui_test_net_text(99), "under F8"));
    int c0 = g_w_disc + g_w_last + g_w_forget + g_w_rescan + g_w_conn, s00 = g_w_cfgset;
    const int keys7[] = { 'd', 'l', 'f', 'f', 's', 'm', KEY_LEFT, KEY_RETURN, KEY_DOWN };
    for (int k : keys7) hostui_test_key(k);
    CHECK(g_w_disc + g_w_last + g_w_forget + g_w_rescan + g_w_conn == c0 && g_w_cfgset == s00);   // (the same keys on the WiFi page start the actions: see the disconnect and connect checks below)
    snprintf(p, sizeof(p), "%s/t184_net_nomodule.png", out); png(p, 2);
    // the WiFi page of F8: the row of the settings opens it; no module: the actions are refused with a note and start nothing
    auto wifi_page = [&]() { hostui_test_view(4); set_pick("WiFi"); hostui_test_key(KEY_RETURN); };
    wifi_page();
    CHECK(hostui_test_net_sel() == 0);
    hostui_test_key('d');
    hostui_test_key('l');
    CHECK(g_w_disc + g_w_last + g_w_forget + g_w_rescan + g_w_conn == c0);
    CHECK(!strcmp(hostui_test_net_note(), "There is no WiFi module"));
    CHECK(!strcmp(hostui_test_net_text(5), "No module"));
    snprintf(p, sizeof(p), "%s/t184_wifi_nomodule.png", out); png(p, 2);
    {   // D3 amended: the radio row (the first row, selected) reflects the driver; Disabled = Off, RETURN / LEFT / RIGHT switch it on, the messages say why an action is refused
        int r0 = g_w_radio_n;
        hostui_test_key(KEY_RETURN);
        CHECK(g_w_radio_n == r0 + 1 && !strcmp(hostui_test_net_note(), "There is no WiFi module"));      // no module: the driver says no
        g_ws.state = WN_DISABLED; g_ws.text = "Disabled";
        hostui_test_wifi_sync();
        CHECK(!strcmp(hostui_test_net_text(5), "Off") && !strcmp(hostui_test_net_label(0), "WiFi") && !strcmp(hostui_test_net_text(0), "Off"));      // box and row say the same (R6)
        snprintf(p, sizeof(p), "%s/t184_wifi_disabled.png", out); png(p, 2);
        const int ks[] = { KEY_RETURN, KEY_LEFT, KEY_RIGHT };
        for (int k : ks) {
            g_w_radio_last = -1; r0 = g_w_radio_n;
            hostui_test_key(k);
            CHECK(g_w_radio_n == r0 + 1 && g_w_radio_last == 1 && !strcmp(hostui_test_net_note(), "WiFi is on"));
        }
        c0 = g_w_disc + g_w_last + g_w_forget + g_w_rescan + g_w_conn;
        const int ka[] = { 'd', 'l', 's', 'm', 'f' };
        for (int k : ka) {
            hostui_test_key(k);
            CHECK(strstr(hostui_test_net_note(), "WiFi is switched off - switch it on above"));
            if (k == 's') { snprintf(p, sizeof(p), "%s/t184_wifi_disabled_note.png", out); png(p, 2); }   // UX review: the note as the user sees it
        }
        CHECK(g_w_disc + g_w_last + g_w_forget + g_w_rescan + g_w_conn == c0);
        g_ws.state = WN_OFF; g_ws.text = "Off";
        hostui_test_wifi_sync();
        hostui_test_key('l');
        CHECK(!strcmp(hostui_test_net_note(), "The WiFi driver is not running") && !strcmp(hostui_test_net_text(5), "Off"));
        snprintf(p, sizeof(p), "%s/t184_wifi_off_note.png", out); png(p, 2);
        g_ws.state = WN_LINK_DOWN; g_ws.text = "Link down";
        hostui_test_wifi_sync();
        CHECK(!strcmp(hostui_test_net_text(5), "On"));
        g_w_radio_last = -1;
        hostui_test_key(KEY_RETURN);
        CHECK(g_w_radio_last == 0 && !strcmp(hostui_test_net_note(), "WiFi is off"));
        snprintf(p, sizeof(p), "%s/t184_wifi_switched_off.png", out); png(p, 2);
        g_ws.state = WN_NO_MODULE; g_ws.text = "No module detected";
        hostui_test_wifi_sync();
    }
    // linked: the rows
    g_ws.state = WN_LINK_UP; g_ws.text = "Link up"; snprintf(g_ws.ssid, sizeof(g_ws.ssid), "Home WLAN");
    snprintf(g_ws.ip, sizeof(g_ws.ip), "192.168.178.57"); snprintf(g_ws.module, sizeof(g_ws.module), "ESP32 WiFi Bridge V1.14");
    hostui_test_wifi_sync();
    CHECK(!strcmp(hostui_test_net_text(0), "Link up") && !strcmp(hostui_test_net_text(1), "Home WLAN") && !strcmp(hostui_test_net_text(2), "192.168.178.57"));
    CHECK(strstr(hostui_test_net_text(3), "a0:76:4e:12:34:56") && strstr(hostui_test_net_text(3), "V1.14"));
    CHECK(!strcmp(hostui_test_net_text(6), "Enabled") && !strcmp(hostui_test_net_text(7), "192.168.2.64"));
    snprintf(p, sizeof(p), "%s/t184_net.png", out); png(p, 2);                  // the WiFi page, linked
    hostui_test_view(3);
    snprintf(p, sizeof(p), "%s/t184_net_f7.png", out); png(p, 2);               // F7, linked
    wifi_page();
    // the labels are Gideon's
    CHECK(!strcmp(hostui_test_net_label(1), "Use DHCP") && !strcmp(hostui_test_net_label(5), "Static DNS") && !hostui_test_net_label(6));
    // the settings: DOWN/UP select, LEFT toggles DHCP and the store is written; RETURN on a static row with DHCP on only explains
    CHECK(hostui_test_net_sel() == 0);
    hostui_test_key(KEY_DOWN);
    CHECK(hostui_test_net_sel() == 1);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_UP);
    int s0 = g_w_cfgset;
    hostui_test_key(KEY_LEFT);
    CHECK(g_w_cfgset == s0 + 1 && !g_wc.dhcp && !strcmp(hostui_test_net_text(6), "Disabled"));
    hostui_test_key(KEY_RIGHT);
    CHECK(g_w_cfgset == s0 + 2 && g_wc.dhcp);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_RETURN);
    CHECK(g_w_cfgset == s0 + 2 && strstr(hostui_test_net_note(), "DHCP is disabled"));
    hostui_test_key(KEY_UP);
    hostui_test_key(KEY_LEFT);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_DOWN);
    snprintf(p, sizeof(p), "%s/t184_net_static.png", out); png(p, 2);
    hostui_test_key(KEY_UP);
    hostui_test_key(KEY_UP);
    hostui_test_key(KEY_RIGHT);
    CHECK(g_wc.dhcp);
    // disconnect / connect to last start one action each and the row shows it; a refused one says so
    int d0 = g_w_disc, l0 = g_w_last;
    hostui_test_key('d');
    CHECK(g_w_disc == d0 + 1 && hostui_test_wf_op_run());
    hostui_test_wifi_sync();
    CHECK(strstr(hostui_test_net_text(0), "Disconnecting"));
    snprintf(p, sizeof(p), "%s/t184_net_busy.png", out); png(p, 2);
    g_ws.busy = false; g_ws.last = 0; g_ws.state = WN_LINK_DOWN; g_ws.text = "Link down"; g_ws.ssid[0] = 0; g_ws.ip[0] = 0;
    hostui_test_wifi_sync();
    CHECK(!hostui_test_wf_op_run() && strstr(hostui_test_net_note(), "Disconnecting: done"));
    g_w_busy_refuse = true;
    hostui_test_key('l');
    CHECK(g_w_last == l0 + 1 && !hostui_test_wf_op_run() && strstr(hostui_test_net_note(), "busy"));
    g_w_busy_refuse = false;
    hostui_test_key('l');
    CHECK(g_w_last == l0 + 2 && hostui_test_wf_op_run());
    hostui_test_wifi_sync(); g_ws.busy = false; g_ws.last = -3; hostui_test_wifi_sync();
    CHECK(strstr(hostui_test_net_note(), "failed (-3)"));
    // D4: the reason of a failed connect is in the note
    hostui_test_key('l');
    CHECK(hostui_test_wf_op_run());
    hostui_test_wifi_sync(); g_ws.busy = true; g_ws.last = 1; hostui_test_wifi_sync();
    snprintf(g_ws.why, sizeof(g_ws.why), "Authentication failed.");
    g_ws.busy = false; g_ws.last = -1; hostui_test_wifi_sync();
    CHECK(strstr(hostui_test_net_note(), "failed (-1). Authentication failed."));
    g_ws.why[0] = 0;
    // forget: the first F asks, the second forgets; any other key between cancels
    int f0 = g_w_forget;
    hostui_test_key('f');
    CHECK(g_w_forget == f0 && strstr(hostui_test_net_note(), "F again"));
    hostui_test_key(KEY_DOWN);
    hostui_test_key('f');
    CHECK(g_w_forget == f0);
    hostui_test_key('f');
    CHECK(g_w_forget == f0 + 1);
    // no store: the rows say so and nothing is written
    g_wc_present = false;
    hostui_test_wifi_sync();
    s0 = g_w_cfgset;
    hostui_test_key(KEY_LEFT);
    CHECK(g_w_cfgset == s0 && !strcmp(hostui_test_net_text(6), "(no WiFi store)"));
    g_wc_present = true;
    g_ws.state = WN_LINK_UP; g_ws.text = "Link up"; g_ws.last = 1;
    hostui_test_wifi_sync();
    // the panels
    {   // D4: a scan that failed says so (the list is empty); a rescan that is under way says Scanning
        int keep = g_naps;
        g_naps = 0; g_ws.scan = WS_FAILED;
        hostui_test_wifi_sync();
        hostui_test_wifi_panel(1, 0);
        CHECK(strstr(hostui_test_wl_sub(), "scan failed"));
        snprintf(p, sizeof(p), "%s/t184_aplist_failed.png", out); png(p, 2);
        g_ws.scan = WS_EMPTY;
        hostui_test_wifi_panel(1, 0);
        CHECK(strstr(hostui_test_wl_sub(), "No access point"));
        g_naps = keep; g_ws.scan = WS_DONE;
        hostui_test_wifi_sync();
    }
    hostui_test_wifi_panel(1, 0);
    snprintf(p, sizeof(p), "%s/t184_aplist.png", out); png(p, 2);
    hostui_test_wifi_panel(1, 2);
    snprintf(p, sizeof(p), "%s/t184_aplist_sel.png", out); png(p, 2);
    hostui_test_wifi_panel(2, 0);
    snprintf(p, sizeof(p), "%s/t184_password.png", out); png(p, 2);
    hostui_test_wifi_panel(3, 0);
    snprintf(p, sizeof(p), "%s/t184_ssid.png", out); png(p, 2);
    hostui_test_wifi_panel(4, 3);
    snprintf(p, sizeof(p), "%s/t184_auth.png", out); png(p, 2);
    hostui_test_wifi_panel(5, 0);
    snprintf(p, sizeof(p), "%s/t184_address.png", out); png(p, 2);
    hostui_test_wifi_panel(0, 0);
}

static void scenarios_t152(const char *out)
{
    char main_root[400], pg[2048], buf[2048], p[512];
    snprintf(main_root, sizeof(main_root), "%s", g_root);
    snprintf(g_root, sizeof(g_root), "%s_txu", main_root);
    g_saves = 0;
    hostui_test_init(0);
    BenchFlash *fl = new BenchFlash;
    hostui_test_flash = fl;
    hostui_cv_hook = fw_hook;
    g_fw_out = out;
    g_fw_shot5 = true;
    hostui_test_fm_keep(true);
    cor_host_key = 0x820eded2;
    const int fw0 = hostui_test_fw_writes(), cv0 = hostui_test_cv_runs();               // the restore and the conversion benches ran before: counters are not zero
    const std::vector<uint8_t> old_bit(300000, 0x5A), old_app(70000, 0xA5);
    auto fresh = [&]() {
        memset(&fl->mem[0], 0xFF, fl->mem.size());
        memcpy(&fl->mem[0], old_bit.data(), old_bit.size());
        memcpy(&fl->mem[0x220000], old_app.data(), old_app.size());
        fl->clear_counts();
    };
    fresh();
    // ---- F8: the two rows (D7, D8)
    hostui_test_view(4);
    hostui_test_settings(buf, sizeof(buf));
    CHECK(strstr(buf, "Protect Flash from updates=on|") && strstr(buf, "Inspect .cfw/.ue2 for ESP32=open|"));
    CHECK(set_pick("Protect Flash from updates"));
    snprintf(p, sizeof(p), "%s/t152_settings.png", out); png(p, 2);
    hostui_test_key(KEY_RETURN);
    hostui_test_settings(buf, sizeof(buf));
    CHECK(!g_prot_on && strstr(buf, "Protect Flash from updates=off|") && has("note='Flash protection off: a large core can write the whole flash'"));
    snprintf(p, sizeof(p), "%s/t152_settings_off.png", out); png(p, 2);
    hostui_test_key(KEY_RETURN);
    hostui_test_settings(buf, sizeof(buf));
    CHECK(g_prot_on && strstr(buf, "Protect Flash from updates=on|"));
    // ---- the lock before the handover (S5, S7, D6): a .cfw and a large .cor are large cores, a refused lock stops the start
    g_legacy_ok = true;
    int jumps = 0;
    auto start_s = [&](const char *dir, const char *name, bool confirm) -> bool {
        fw_pick(dir, name);
        if (confirm) kb_push(KEY_RETURN);
        int j0 = g_jump_calls;
        try { hostui_test_key('s'); } catch (JumpReached &) { }
        return g_jump_calls == j0 + 1;
    };
    g_lock_calls = g_lock_off_calls = 0;
    CHECK(start_s("/Usb0/upd", "gideon.cfw", true));
    jumps++;
    CHECK(g_lock_calls == 1 && g_lock_calls_at_jump == 1 && g_lock_off_calls == 0);                 // the lock came first, once
    CHECK(start_s("/SD/cores", "large1.cor", false));
    jumps++;
    CHECK(g_lock_calls == 2 && g_lock_calls_at_jump == 2);                                           // a large .cor too
    g_prot_on = false;                                                                               // S7: the setting is off
    CHECK(start_s("/Usb0/upd", "gideon.cfw", true));
    jumps++;
    CHECK(g_lock_calls == 2 && g_lock_off_calls == 1 && g_jump_calls == jumps);                      // nothing set, the core started
    g_prot_on = true;
    g_lock_fail = true;                                                                              // the registers do not read back as set: not started
    fw_pick("/Usb0/upd", "gideon.cfw");
    kb_push(KEY_RETURN);
    try { hostui_test_key('s'); } catch (JumpReached &) { }
    CHECK(g_jump_calls == jumps && strstr(hostui_test_start_err(), "the flash could not be protected") && strstr(hostui_test_start_err(), "nothing was started"));
    g_lock_fail = false;
    g_legacy_ok = false;
    // ---- S4: who is offered which key
    fw_pick("/Usb0/upd", "both.txu");
    CHECK(strstr(hostui_test_foot(), "U update") && !strstr(hostui_test_foot(), "R restore") && !strstr(hostui_test_foot(), "P package") && !strstr(hostui_test_foot(), "S start"));
    {
        char rp[1024];
        hostui_test_browse_right(rp, sizeof(rp));                                                    // the side panel of a .txu: what it is, how it is installed
        CHECK(strstr(rp, "title=both.txu|") && strstr(rp, "MultiCore update main-1a2b3c4d: host and host app, host key 553525d3.") && strstr(rp, "Press U to install it."));
        fw_pick("/Usb0/upd", "crc.txu");
        hostui_test_browse_right(rp, sizeof(rp));
        CHECK(strstr(rp, "Not a usable update: the header of the update file does not match its checksum."));
    }
    fw_pick("/Usb0/upd", "gideon.cfw");
    CHECK(!strstr(hostui_test_foot(), "U update") && strstr(hostui_test_foot(), "R restore"));
    fw_pick("/Usb0/upd", "not_txu.ue2");
    CHECK(!strstr(hostui_test_foot(), "U update"));
    fw_pick("/Usb0/upd", "notes.txt");
    CHECK(!strstr(hostui_test_foot(), "U update"));
    int p0 = g_fw_pages;
    for (const char *nm : { "gideon.cfw", "not_txu.cfw", "not_txu.ue2", "notes.txt" }) {              // U installs nothing but a .txu
        fw_pick("/Usb0/upd", nm);
        hostui_test_key('u');
        CHECK(hostui_test_up_writes() == 0 && !hostui_test_fmt_active() && fl->erases == 0 && kbn == 0 && g_fw_pages == p0);
    }
    fw_pick("/Usb0/upd", "both.txu");
    hostui_test_key('r');                                                                            // R and P are not for a .txu
    hostui_test_key('p');
    CHECK(hostui_test_fw_writes() == fw0 && hostui_test_cv_runs() == cv0 && !hostui_test_fmt_active() && fl->erases == 0 && kbn == 0 && g_fw_pages == p0);
    hostui_test_browse_media();
    hostui_test_key('u');
    CHECK(!hostui_test_fmt_active() && fl->erases == 0);
    // ---- S3: refused before the first erase, with the reason; the fixed twin gets as far as the question
    fw_pick("/Usb0/upd", "iface_bad.txu");
    g_fw_png_end = "t152_refused_interface";
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t152 page: %s\n", pg);
    CHECK(strstr(pg, "title=Cannot update from iface_bad.txu|") && strstr(pg, "needs host interface 2") && strstr(pg, "running host provides 1") &&
          strstr(pg, "Nothing was written. The flash is as it was."));
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_up_writes() == 0 && kbn == 0 && !hostui_test_fmt_active());
    fw_pick("/Usb0/upd", "crc.txu");
    g_fw_png_end = 0;
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Cannot update from crc.txu|") && strstr(pg, "does not match its checksum") && fl->erases == 0);
    fw_pick("/Usb0/upd", "payload_app.txu");
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Update check failed|") && strstr(pg, "app part does not match") && fl->erases == 0 && hostui_test_up_writes() == 0);
    fw_pick("/Usb0/upd", "foreign_origin.txu");
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "does not come from TRX") && fl->erases == 0);
    fw_pick("/Usb0/upd", "iface_fixed.txu");                                                         // the same package with the interface fixed passes: it asks, "No"
    g_fw_png3 = "t152_ask1_app"; g_fw_png4 = "t152_ask2_app"; g_fw_png6 = "t152_check"; g_fw_keys = K_X;
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t152 page: %s\n", pg);
    CHECK(strstr(pg, "title=Update the host app?|") && strstr(pg, "This replaces the host app in the flash.") && strstr(pg, "Only the app changes: no core is affected.") &&
          !strstr(pg, "partial core"));
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_up_writes() == 0 && has("note='update cancelled, nothing was written'"));
    // Y, then a key that is not RETURN: no; Y and RETURN at once: not an answer
    g_fw_keys = K_YX;
    hostui_test_key('u');
    CHECK(fl->erases == 0 && hostui_test_up_writes() == 0 && kbn == 0);
    g_key_gap_ms = 100;
    g_fw_keys = K_YR;
    hostui_test_key('u');
    CHECK(fl->erases == 0 && hostui_test_up_writes() == 0 && kbn == 0);
    // ---- T192.2-S4 at the screen: the flow is a job the loop steps. Started, it returns at once (nothing else of the UI runs: fmt_active); it asks and waits; stepped
    // many times with no key it has written nothing; a key answers it and the aftermath runs (the page is closed, the flag is released).
    {
        extern bool hostui_test_job_active(void);
        extern int hostui_test_job_state(void);
        extern bool hostui_test_job_step(void);
        extern void hostui_test_job_async(bool on);
        g_fw_keys = K_NONE;
        hostui_test_job_async(true);
        hostui_test_key('u');
        CHECK(hostui_test_job_active() && hostui_test_fmt_active());                               // started, not finished: the call returned
        int guard = 0;
        while (hostui_test_job_state() != 1 /* HCJ_ASKING */ && guard++ < 400) hostui_test_job_step();
        CHECK(hostui_test_job_state() == 1);
        for (int i = 0; i < 300; i++) CHECK(!hostui_test_job_step());                              // asking: it waits (the question's own time-out is a minute)
        CHECK(hostui_test_job_active() && hostui_test_job_state() == 1 && fl->erases == 0 && fl->programs == 0 && hostui_test_up_writes() == 0);
        kb_push('n');                                                                                // an answer: no
        guard = 0;
        while (hostui_test_job_active() && guard++ < 400) hostui_test_job_step();
        CHECK(!hostui_test_job_active() && !hostui_test_fmt_active() && fl->erases == 0 && hostui_test_up_writes() == 0);
        CHECK(has("note='update cancelled, nothing was written'"));
        hostui_test_job_async(false);
        g_fw_keys = K_YR;
    }
    // ---- S1: the app only: the app region is written, the bitstream slot is not touched (read back equal)
    std::vector<uint8_t> before_bit(fl->mem.begin(), fl->mem.begin() + 0x220000);
    g_key_gap_ms = 1100;
    g_fw_png_end = "t152_done_app";
    g_fw_keys = K_YR;
    hostui_test_key('u');
    g_key_gap_ms = 0;
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t152 page: %s\n", pg);
    std::vector<uint8_t> app = txu_part("iface_fixed.txu", 2);
    CHECK(hostui_test_up_writes() == 1 && strstr(pg, "title=Update written|") && strstr(pg, "Written and verified: the host app.") && !strstr(pg, "partial core"));
    CHECK(fl->er_bit == 0 && fl->pr_bit == 0 && fl->er_rest == 0 && fl->pr_rest == 0 && fl->er_app > 0 && fl->pr_app > 0);
    CHECK(std::vector<uint8_t>(fl->mem.begin(), fl->mem.begin() + 0x220000) == before_bit);
    CHECK(!app.empty() && !memcmp(&fl->mem[0x220000], app.data(), app.size()));
    CHECK(kbn == 0 && !hostui_test_fmt_active() && has("note='Update written. Switch the power off and on.'"));
    // the same package again: the flash holds it, nothing to write
    int e0 = fl->erases;
    g_fw_png_end = "t152_nothing";
    g_fw_keys = K_NONE;
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Nothing to write|") && fl->erases == e0 && hostui_test_up_writes() == 1);
    // ---- S2: a host that changes the key, three partial cores and two large cores on the media
    fresh();
    fw_pick("/Usb0/upd", "newkey.txu");
    g_fw_png3 = "t152_ask1_host"; g_fw_png4 = "t152_ask2_host"; g_fw_png_end = 0; g_fw_keys = K_X;
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t152 page: %s\n", pg);
    CHECK(strstr(pg, "title=Update the host and the host app?|") && strstr(pg, "The host key changes: 3 partial cores need a rebuild for the new host.") &&
          strstr(pg, "2 large cores are not affected."));
    CHECK(fl->erases == 0 && hostui_test_up_writes() == 1);
    // a host with the running key: no core is affected, nothing is said about a rebuild
    cor_host_key = 0x553525D3;
    fw_pick("/Usb0/upd", "both.txu");
    g_fw_png3 = "t152_ask1_samekey"; g_fw_keys = K_X;
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "The host key stays the same: no core is affected.") && !strstr(pg, "rebuild") && fl->erases == 0);
    cor_host_key = 0x820eded2;
    // the write: progress shot in the bitstream phase, the bench presses ESC there (no key stops it), the result names the cores
    fw_pick("/Usb0/upd", "newkey.txu");
    g_key_gap_ms = 1100;
    g_fw_shot5 = false; g_fw_png5 = "t152_progress_host"; g_fw_png_end = "t152_done_host"; g_fw_keys = K_YR;
    hostui_test_key('u');
    g_key_gap_ms = 0;
    hostui_test_fm_page(pg, sizeof(pg));
    printf("t152 page: %s\n", pg);
    std::vector<uint8_t> bit2 = txu_part("newkey.txu", 1), app2 = txu_part("newkey.txu", 2);
    CHECK(hostui_test_up_writes() == 2 && strstr(pg, "title=Update written|") && strstr(pg, "Written and verified: the host and the host app.") &&
          strstr(pg, "3 partial cores need a rebuild for the new host. Large cores are not affected."));
    CHECK(!bit2.empty() && !memcmp(&fl->mem[0], bit2.data(), bit2.size()) && !app2.empty() && !memcmp(&fl->mem[0x220000], app2.data(), app2.size()));
    CHECK(fl->mem[bit2.size()] == 0xFF && fl->mem[0x1FFFFF] == 0xFF);                                 // the rest of the bitstream slot reads blank
    CHECK(fl->er_rest == 0 && fl->pr_rest == 0);
    // ---- a part the flash holds already is not written again
    fresh();
    memcpy(&fl->mem[0], bit2.data(), bit2.size());
    memset(&fl->mem[bit2.size()], 0xFF, 0x220000 - bit2.size());
    fl->clear_counts();
    fw_pick("/Usb0/upd", "newkey.txu");
    g_fw_png3 = "t152_ask1_skip"; g_fw_png4 = 0; g_fw_png_end = "t152_done_skip"; g_fw_keys = K_X;
    hostui_test_key('u');
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Update the host app?|") && strstr(pg, "Not written again, the flash holds it already: the host.") && fl->erases == 0);
    g_key_gap_ms = 1100;
    g_fw_shot5 = true;
    g_fw_keys = K_YR;
    hostui_test_key('u');
    g_key_gap_ms = 0;
    hostui_test_fm_page(pg, sizeof(pg));
    CHECK(strstr(pg, "title=Update written|") && strstr(pg, "Written and verified: the host app.") && strstr(pg, "Not written again, the flash held it already: the host."));
    CHECK(fl->er_bit == 0 && fl->pr_bit == 0 && fl->er_app > 0 && !memcmp(&fl->mem[0x220000], app2.data(), app2.size()));
    // ---- D11: after the write the page counts down, then one MACHINE_REBOOT goes to the control module; a key sends it at once; a module that does not answer is said
    {
        auto d11_run = [&](int press_at, bool esp_ok, const char *png_end) {
            fresh();
            g_rs_press_at = press_at;
            g_esp_ok = esp_ok;
            g_esp_calls = 0;
            g_rs_pages = 0;
            g_rs_page[0] = 0;
            fw_pick("/Usb0/upd", "newkey.txu");
            g_key_gap_ms = 1100;
            g_fw_shot5 = true;
            g_fw_png_end = png_end;
            g_fw_keys = K_YR;
            hostui_test_key('u');
            g_key_gap_ms = 0;
            hostui_test_fm_page(pg, sizeof(pg));
        };
        // the countdown runs out: one request, not before the 5 s are over, and the page said so
        d11_run(0, true, "t152_d11_countdown");
        uint32_t ms = (g_esp_tick - g_rs_first_tick) * 5;
        printf("T152 D11 countdown: %d request(s) after %u ms; page: %s\n", g_esp_calls, (unsigned)ms, g_rs_page);
        CHECK(g_rs_pages == 1 && g_esp_calls == 1 && ms >= 5000 && ms < 5400);
        CHECK(strstr(g_rs_page, "title=Update written|") && strstr(g_rs_page, "Restarting in 5 s to start the new version.|") && strstr(g_rs_page, "hint=any key restarts now|"));
        CHECK(!strstr(g_rs_page, "Switch the power off and on"));
        CHECK(strstr(pg, "The restart did not happen: the board is still powered after the power cycle was sent.") && strstr(pg, "Switch the power off and on to start the new version."));
        // a key: at once
        d11_run(5, true, "t152_d11_key");
        ms = (g_esp_tick - g_rs_first_tick) * 5;
        CHECK(g_rs_pages == 1 && g_esp_calls == 1 && ms < 1000);
        // the module does not answer: said in the box, the old text stays, nothing else is sent
        d11_run(5, false, "t152_d11_noanswer");
        printf("T152 D11 no answer: %d request(s); page: %s\n", g_esp_calls, pg);
        CHECK(g_esp_calls == 1 && strstr(pg, "title=Update written|") && strstr(pg, "The restart did not happen: the ESP32 control module did not answer.") &&
              strstr(pg, "Switch the power off and on to start the new version.") && strstr(pg, "hint=any key returns to the File Browser|"));
        CHECK(kbn == 0 && has("note='Update written. Switch the power off and on.'"));
        snprintf(p, sizeof(p), "%s/t152_d11_noanswer.png", out); png(p, 2);
        g_rs_press_at = 5;
        g_esp_ok = true;
    }
    // ---- D8: F8 -> Inspect: the browser picks the file, RETURN looks into it, nothing is written
    auto enter_inspect = [&](const char *dir) {
        hostui_test_browse_keep(dir);
        hostui_test_view(4);
        set_pick("Inspect .cfw/.ue2 for ESP32");
        hostui_test_key(KEY_RETURN);
    };
    auto select_row = [&](const char *name) -> bool {
        char rows[2048], needle[96];
        hostui_test_browse_rows(rows, sizeof(rows));
        snprintf(needle, sizeof(needle), "%s|", name);
        int idx = 0;
        bool found = false;
        for (const char *r = rows; *r; idx++) {
            if (!strncmp(r + 1, needle, strlen(needle))) { found = true; break; }
            const char *nl = strchr(r, '\n');
            if (!nl) break;
            r = nl + 1;
        }
        for (int i = 0; found && i < idx; i++) hostui_test_key(KEY_DOWN);
        return found;
    };
    fl->clear_counts();
    enter_inspect("/Usb0/esp");
    CHECK(hostui_test_inspect() && strstr(hostui_test_foot(), "RETURN inspects a .cfw/.ue2") && has("Pick a .cfw or .ue2"));
    snprintf(p, sizeof(p), "%s/t152_inspect_pick.png", out); png(p, 2);
    struct { const char *file; const char *line0; const char *png; } esp[] = {
        { "v111.ue2", "l0=ESP32 firmware V1.11|", "t152_inspect_v111" },
        { "v18.ue2", "l0=ESP32 firmware V1.8|", "t152_inspect_v18" },
        { "plain.cfw", "l0=This file has no ESP32 part.|", "t152_inspect_none" },
        { "app_only.ue2", "l0=An ESP32 application is in this file, but not a whole image set.|", "t152_inspect_incomplete" },
        { "damaged.ue2", "l0=The ESP32 application in this file is damaged", "t152_inspect_damaged" } };
    for (auto &e : esp) {
        enter_inspect("/Usb0/esp");
        CHECK(select_row(e.file) && hostui_test_inspect());
        {
            char rp[1024];
            hostui_test_browse_right(rp, sizeof(rp));                                                // the pick says what RETURN does
            CHECK(strstr(rp, "RETURN looks into this file for an ESP32 image set and its version.") && strstr(rp, "nothing is written."));
        }
        g_fw_png_end = e.png;
        hostui_test_key(KEY_RETURN);
        hostui_test_fm_page(pg, sizeof(pg));
        printf("t152 page: %s\n", pg);
        CHECK(strstr(pg, e.line0) && strstr(pg, "Nothing was written."));
        CHECK(hostui_test_inspect() && kbn == 0 && !hostui_test_fmt_active());                      // back in the pick: another file can be looked into
    }
    g_fw_png_end = 0;
    CHECK(fl->erases == 0 && fl->programs == 0 && hostui_test_up_writes() == 6 && hostui_test_fw_writes() == fw0);   // 6: the three writes before and the three of D11
    // ---- T152 S8: U on the inspect page of a complete set writes it into the ESP32 (the loader model behind the link); the board's flash is never touched
    static const int U_X[] = { 'u', 'x', 0 }, U_RX[] = { 'u', KEY_RETURN, 'x', 0 }, U_ESC[] = { 'u', KEY_ESCAPE, 0 }, U_ONLY[] = { 'u', 0 }, K_Y[] = { 'y', 0 };
    auto s8_run = [&](const char *file, EspLoaderModel &m, const int *uk, const int *ak, const char *png_end, const char *png3, const char *png5) -> bool {
        g_esp_model = &m; g_u_keys = uk; g_fw_keys = ak; g_fw_png_end = png_end; g_fw_png3 = png3; g_esp_png5 = png5;
        g_s8_n = 0; g_s8_asks = 0; memset(g_s8_pg, 0, sizeof(g_s8_pg));
        fl->clear_counts();
        enter_inspect("/Usb0/esp");
        bool f = select_row(file);
        hostui_test_key(KEY_RETURN);
        g_fw_png_end = 0; g_fw_png3 = 0; g_esp_png5 = 0; g_fw_keys = K_NONE; g_u_keys = 0;
        return f;
    };
    auto s8_untouched = [&]() { return fl->erases == 0 && fl->programs == 0 && kbn == 0 && hostui_test_inspect(); };
    int ew = hostui_test_esp_writes();
    {   // success: three images, the version asked, the page says so
        EspLoaderModel m;
        CHECK(s8_run("v111.ue2", m, U_X, K_Y, "t152_s8_done", "t152_s8_ask", "t152_s8_progress"));
        CHECK(strstr(g_s8_pg[0], "U writes it into the ESP32") && g_s8_asks == 1 && g_s8_n == 2 && hostui_test_esp_writes() == ew + 1);
        CHECK(m.begin_addrs.size() == 3 && m.begin_addrs[0] == 0 && m.begin_addrs[1] == 0x8000 && m.begin_addrs[2] == 0x10000 && m.flash[0] == 0xE9 && m.identify_calls == 1);
        CHECK(!m.armed && m.run_mode && m.leave_calls == 1);
        CHECK(strstr(g_s8_pg[1], "title=ESP32 updated|") && strstr(g_s8_pg[1], "The module reports V1.11, the version in the file.") && strstr(g_s8_pg[1], "The board's flash was not touched."));
        CHECK(has("note='ESP32 updated.'") && s8_untouched());                                   // erases == 0 && programs == 0: the FPGA flash was not touched
        ew++;
    }
    {   // cancel at the question: nothing is sent, the module is not even entered
        EspLoaderModel m;
        CHECK(s8_run("v111.ue2", m, U_ONLY, K_X, 0, 0, 0));
        CHECK(g_s8_asks == 1 && m.enter_calls == 0 && m.begin_addrs.empty() && m.arm_on == 0 && hostui_test_esp_writes() == ew && has("note='ESP32 not updated. Nothing was written.'") && s8_untouched());
    }
    {   // fails before the first FLASH_BEGIN: the ESP32's flash is as it was
        EspLoaderModel m;
        m.fail_cmd_op = 0x0D; m.fail_cmd_nth = 1;
        CHECK(s8_run("v111.ue2", m, U_X, K_Y, "t152_s8_nothing", 0, 0));
        CHECK(strstr(g_s8_pg[1], "title=Nothing was written|") && strstr(g_s8_pg[1], "The ESP32's flash is as it was.") && m.begin_addrs.empty() && m.leave_calls == 1 && !m.armed);
        CHECK(hostui_test_esp_writes() == ew + 1 && s8_untouched());
        ew++;
    }
    {   // fails after it: the page says the flash holds a part, ESC leaves
        EspLoaderModel m;
        m.fail_cmd_op = 0x03; m.fail_cmd_nth = 5;
        CHECK(s8_run("v111.ue2", m, U_ESC, K_Y, "t152_s8_failed", 0, 0));
        CHECK(strstr(g_s8_pg[1], "title=The ESP32 may not start|") && strstr(g_s8_pg[1], "RETURN writes again") && strstr(g_s8_pg[1], "holds a part of the firmware") && m.leave_calls == 1 && !m.armed);
        CHECK(hostui_test_esp_writes() == ew + 1 && g_s8_n == 2 && s8_untouched());
        ew++;
    }
    {   // the same, RETURN writes again: no second question, the whole set lands
        EspLoaderModel m;
        m.fail_cmd_op = 0x03; m.fail_cmd_nth = 5;
        CHECK(s8_run("v111.ue2", m, U_RX, K_Y, 0, 0, 0));
        CHECK(g_s8_asks == 1 && g_s8_n == 3 && strstr(g_s8_pg[1], "title=The ESP32 may not start|") && strstr(g_s8_pg[2], "title=ESP32 updated|"));
        CHECK(m.begin_addrs.size() == 4 && m.flash[0] == 0xE9 && hostui_test_esp_writes() == ew + 2 && s8_untouched());
        ew += 2;
    }
    {   // the module reports another version than the file's
        EspLoaderModel m;
        m.force_major = 1; m.force_minor = 8;
        CHECK(s8_run("v111.ue2", m, U_ESC, K_Y, "t152_s8_differs", 0, 0));
        CHECK(strstr(g_s8_pg[1], "title=The ESP32 reports another version|") && strstr(g_s8_pg[1], "The file has V1.11, the module reports V1.8.") && hostui_test_esp_writes() == ew + 1 && s8_untouched());
        ew++;
    }
    {   // the module does not answer who it is: written, said so, not called a success
        EspLoaderModel m;
        m.silent_identify = true;
        CHECK(s8_run("v111.ue2", m, U_X, K_Y, "t152_s8_noanswer", 0, 0));
        CHECK(strstr(g_s8_pg[1], "title=ESP32 written|") && strstr(g_s8_pg[1], "Look at F8 -> WiFi in a minute") && has("note='ESP32 written. It did not report its version yet.'"));
        CHECK(m.begin_addrs.size() == 3 && hostui_test_esp_writes() == ew + 1 && s8_untouched());
        ew++;
    }
    for (const char *nf : { "plain.cfw", "app_only.ue2", "damaged.ue2" }) {   // U on a file without a complete, intact set does nothing
        EspLoaderModel m;
        CHECK(s8_run(nf, m, U_ONLY, K_Y, 0, 0, 0));
        CHECK(g_s8_asks == 0 && m.enter_calls == 0 && m.arm_on == 0 && hostui_test_esp_writes() == ew && s8_untouched());
    }
    // a file that is not a .cfw/.ue2 is said so and nothing opens; ESC goes back to the settings
    enter_inspect("/Usb0/upd");
    CHECK(select_row("notes.txt"));
    int pages = g_fw_pages;
    hostui_test_key(KEY_RETURN);
    CHECK(has("note='That is not a .cfw or .ue2 file'") && g_fw_pages == pages && hostui_test_inspect() && !hostui_test_fmt_active());
    hostui_test_key(KEY_ESCAPE);
    CHECK(!hostui_test_inspect());
    // a .txu is not looked into for ESP32 either (the pick takes .cfw and .ue2 only)
    enter_inspect("/Usb0/upd");
    CHECK(select_row("both.txu"));
    hostui_test_key(KEY_RETURN);
    CHECK(has("note='That is not a .cfw or .ue2 file'") && g_fw_pages == pages);
    hostui_test_key(KEY_ESCAPE);
    CHECK(fl->erases == 0 && fl->programs == 0);
    // ---- T152 S8 in the 480p NTSC frame: the question, the write, the result and a failure
    hostui_test_mode(VID_SYS_NTSC60, VID_HDMI_480_576);
    g_shot_w = 640; g_shot_h = 480;
    {
        EspLoaderModel m;
        CHECK(s8_run("v111.ue2", m, U_X, K_Y, "t152_s8_480_done", "t152_s8_480_ask", "t152_s8_480_progress"));
        CHECK(strstr(g_s8_pg[1], "title=ESP32 updated|") && m.begin_addrs.size() == 3 && s8_untouched());
        EspLoaderModel f;
        f.fail_cmd_op = 0x03; f.fail_cmd_nth = 5;
        CHECK(s8_run("v111.ue2", f, U_ESC, K_Y, "t152_s8_480_failed", 0, 0));
        CHECK(strstr(g_s8_pg[1], "title=The ESP32 may not start|") && s8_untouched());
    }
    g_shot_w = g_shot_h = 0;
    hostui_test_mode(VID_SYS_PAL50, VID_HDMI_1080P);
    g_esp_model = 0;
    hostui_cv_hook = 0;
    hostui_test_flash = 0;
    delete fl;
    cor_host_key = 0;
    hostui_test_fm_keep(false);
    snprintf(g_root, sizeof(g_root), "%s", main_root);
}

// T152 D9: the build with HOSTAPP_FLASH_LOCK=0 (the board's): the F8 list has no "Protect Flash from updates" row, the others keep their order
static void nolock_scenario(const char *out)
{
    fresh_video(VID_SYS_PAL50, -1);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    hostui_test_key(KEY_F8);
    char se[1024], p[512];
    hostui_test_settings(se, sizeof(se));
    CHECK(!strcmp(se, "sel=0|Direct boot=on|Picture=from the screen: 576p / 480p|WiFi=open|FTP service=on, port 21, no password|Inspect .cfw/.ue2 for ESP32=open|Cores and ROMs=open|Bug report=write to SD/USB|") ||
          (printf("settings: %s\n", se), false));
    CHECK(!strstr(se, "Protect Flash"));
    snprintf(p, sizeof(p), "%s/t152_nolock_settings.png", out); png(p, 2);
}

// T164 D10a: every page of the host app in every mode. The canvas is the one trx_canvas_for names (960 x 540 at a whole factor, or the mode's own size below 960
// wide: 576p PAL 720 x 576, 480p NTSC 640 x 480, 800 x 600), the pages are laid out for it, and every visible object lies inside the canvas and is not cut by its
// parent. The pictures (the mode's frame, the canvas at its place) go to the output directory.
#define MCHECK(c) do { checks++; if (!(c)) { printf("FAIL MODES %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
extern void hostui_test_mode(int sys, int hdmi);
extern void hostui_test_panels_hide(void);
extern void hostui_test_start_panel(void);
extern void hostui_test_confirm_panel(bool show);
extern void hostui_test_rename_panel(bool show, const char *name, int cur, const char *msg);
extern int hostui_test_fm_demo(int which, int *);
static void png_frame(const char *path, int OW, int OH)
{
    TrxCanvas c = trx_canvas_for(OW, OH);
    unsigned char *raw = (unsigned char *)calloc((size_t)(OW * 3 + 1) * OH, 1);
    for (int y = 0; y < OH; y++) {
        unsigned char *row = raw + (size_t)(OW * 3 + 1) * y;
        row[0] = 0;
        int ly = y - c.y0;
        if (ly < 0 || ly >= c.h * c.factor) continue;
        for (int x = c.x0; x < c.x0 + c.w * c.factor && x < OW; x++) {
            uint16_t v = fb[(ly / c.factor) * c.w + (x - c.x0) / c.factor];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            unsigned char *q = row + 1 + 3 * x;
            q[0] = (r << 3) | (r >> 2); q[1] = (g << 2) | (g >> 4); q[2] = (b << 3) | (b >> 2);
        }
    }
    uLongf cl = compressBound((OW * 3 + 1) * OH);
    unsigned char *z = (unsigned char *)malloc(cl);
    compress(z, &cl, raw, (OW * 3 + 1) * OH);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13]; put_be(ih, OW); put_be(ih + 4, OH); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, cl); chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
}
static void modes_pages(const char *out)
{
    struct { const char *name; int sys, hdmi, fw, fh; } modes[] = {
        { "1080p", VID_SYS_PAL50, VID_HDMI_1080P, 1920, 1080 },
        { "720p", VID_SYS_PAL50, VID_HDMI_720P, 1280, 720 },
        { "800x600", VID_SYS_PAL50, VID_HDMI_800x600, 800, 600 },
        { "1024x768", VID_SYS_PAL50, VID_HDMI_1024x768, 1024, 768 },
        { "576p", VID_SYS_PAL50, VID_HDMI_480_576, 720, 576 },
        { "480p", VID_SYS_NTSC60, VID_HDMI_480_576, 640, 480 },
    };
    static const char *cfg = "countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|TRX64 Ultimate (C64) with a name that is far too long for a narrow row of the boot menu||0\n"
                             "core=/Usb0/cores/vc20.cor|VC-20 (PAL)||0\ncore=/Usb0/cores/amiga.cor|Amiga (OCS)||0\ncore=/Usb0/cores/c264.cor|Plus/4||0\n"
                             "core=/Usb0/cores/nes.cor|NES||0\ncore=/Usb0/cores/atari.cor|Atari 800||0\ncore=/Usb0/cores/zx.cor|ZX Spectrum 48K||0\ncore=/Usb0/cores/c128.cor|C128||0\n";
    // never cut to dots: the file panel's keys, the names in the navigation (the side bar) and the labels of the F8 rows (T164 D10q)
    static const char *key_labels[64] = { "Version", "Contract", "FPGA", "Core", "Application", "SHA-256", "Status", "Size", "Contents", "Boot menu", "Access", "Format", 0 };
    {
        int n = 12;
        for (int i = 0; i < 5; i++) key_labels[n++] = hostui_test_nav_name(i);
        for (int i = 0; hostui_test_set_name(i); i++) key_labels[n++] = hostui_test_set_name(i);
        for (int i = 0; hostui_test_net_label(i); i++) key_labels[n++] = hostui_test_net_label(i);
        static char pv[2 * VID_HDMI_COUNT][48];                 // the F8 Picture value ("from the screen: 800 x 600"): its column is the rest of the row
        for (int h = 0; h < VID_HDMI_COUNT; h++) { snprintf(pv[h], sizeof(pv[h]), "from the screen: %s", video_hdmi_name(h)); key_labels[n++] = pv[h];
            snprintf(pv[VID_HDMI_COUNT + h], sizeof(pv[0]), "screen: %s", video_hdmi_name(h)); key_labels[n++] = pv[VID_HDMI_COUNT + h]; }
        key_labels[n] = 0;
    }
    for (auto &md : modes) {
        TrxCanvas want = trx_canvas_for(md.fw, md.fh);
        hostui_test_init(cfg);
        hostui_test_mode(md.sys, md.hdmi);
        MCHECK(hostui_test_canvas_w() == want.w);
        MCHECK(g_pl_w == want.w * want.factor && g_pl_h == want.h * want.factor && g_pl_x == want.x0 && g_pl_y == want.y0);
        int bad = 0, pages = 0;
        char what[96], p[512];
        auto page = [&](const char *name) {
            lv_refr_now(NULL);
            snprintf(what, sizeof(what), "%s %s", md.name, name);
            bad += view_bounds(what, want.w, want.h, 0, key_labels) != 0;
            pages++;
            snprintf(p, sizeof(p), "%s/modes-%s-%s.png", out, md.name, name);
            png_frame(p, md.fw, md.fh);
        };
        hostui_test_view(0); page("menu");
        hostui_test_key(KEY_DOWN); hostui_test_key(KEY_DOWN);
        hostui_test_view(2); page("edit");
        hostui_test_browse_at("/Usb0/cores");
        for (int k = 0; k < 3; k++) hostui_test_key(KEY_DOWN);
        page("browser");
        hostui_test_browse_media(); page("media");
        hostui_test_view(3); page("network");
        g_ftp = { false, FTPSTATE_PORT, false }; hostui_test_chips(true); page("network-ftp-off");           // T163 S3
        MCHECK(!strcmp(hostui_test_net_text(98), "off"));
        g_ftp = { true, FTPSTATE_PORT, true }; hostui_test_chips(true); page("network-ftp-pw");
        MCHECK(!strcmp(hostui_test_net_text(98), "on, port 21, password set"));
        g_ftp = { true, FTPSTATE_PORT, false }; hostui_test_chips(true);
        hostui_test_view(4); page("settings");
        g_ws.busy = false; g_ws.last = 1; hostui_test_wifi_sync();       // the stub of an earlier case may still be busy (a stale "Forgetting ...")
        set_pick("WiFi"); hostui_test_key(KEY_RETURN); page("wifi");              // T184 D3: the WiFi page of the settings
        { WifiNetStatus keep = g_ws; g_ws.state = WN_DISABLED; g_ws.text = "Disabled"; hostui_test_wifi_sync();     // UX review: the refusal note at every mode
          hostui_test_key('s'); page("wifi-note"); g_ws = keep; hostui_test_wifi_sync(); }
        hostui_test_key(KEY_ESCAPE);
        set_pick("Cores and ROMs");
        hostui_test_key(KEY_RETURN); page("cores-roms");
        hostui_test_key(KEY_ESCAPE);
        hostui_test_view(5); page("colors");
        hostui_test_view(0);
        hostui_test_start_panel(); page("start");
        hostui_test_panels_hide();
        hostui_test_confirm_panel(true); page("confirm");
        hostui_test_confirm_panel(false);
        hostui_test_rename_panel(true, "TRX64 Ultimate (C64) with a name that is far too long for a narrow row of the boot menu", 8, "That name is taken.");
        page("rename");
        hostui_test_rename_panel(false, "", 0, "");
        hostui_test_fm_demo(0, 0); page("format");
        hostui_test_fm_demo(2, 0); page("format-bar");
        hostui_test_fm_demo(3, 0);
        printf("MODES %-8s canvas %dx%d at %d,%d size %dx%d: %d pages, %d with an object outside or cut\n", md.name, want.w, want.h, g_pl_x, g_pl_y, g_pl_w, g_pl_h, pages, bad);
        if (bad) { checks++; fails++; printf("FAIL MODES %s: %d page(s) with an object outside or cut\n", md.name, bad); }
    }
    hostui_test_mode(VID_SYS_PAL50, VID_HDMI_1080P);               // and back: the menu is not left at the small size
    MCHECK(hostui_test_canvas_w() == 960 && g_pl_w == 1920 && g_pl_h == 1080);
}


// T164 Q7: "every mode change asks on the screen, 8 s". The question is a line of the page the change was made on; the label's text alone does not show that anything
// was drawn (the bench of T105 read the note out of the page's state and passed while the board showed nothing), so the line's own rectangle is looked at in the frame:
// pixels that are not the rectangle's background. Returns -1 when the line is hidden or its page is.
extern bool hostui_test_note_rect(int *x1, int *y1, int *x2, int *y2, char *text, int tn);
extern void hostui_test_pump(void);
extern bool hostui_test_pq(char *title, char *l0, char *l1, char *key, int n, int *key_y, int *above_y2, int *box_y2, int *cx_off, int *cy_off);
static int note_px(char *text, int tn)
{
    int x1, y1, x2, y2;
    text[0] = 0;
    if (!hostui_test_note_rect(&x1, &y1, &x2, &y2, text, tn)) {
        return -1;
    }
    int stride = hostui_test_canvas_w(), h = hostui_test_canvas_h(), n = 0;
    if (x1 < 0 || y1 < 0 || x2 >= stride || y2 >= h) {
        return -2;                                                  // the line is outside the canvas
    }
    uint16_t bg = fb[y1 * stride + x2];
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            n += fb[y * stride + x] != bg;
    return n;
}
#define PCHECK(c) do { checks++; if (!(c)) { printf("FAIL PICNOTE %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// T192.2-S9: the declarative row table and its headless dump. The pages the table names are the declaration's pages (each id, no more, no fewer); the dump of every page is the
// same as the golden file (tests/hostapp/render/golden/hostrows.txt: ROWS_GOLDEN), and what the screen drew is what the producers say (settings rows, boot menu rows).
extern int hostui_test_dump(char *out, int max);
static void rows_scenario(const char *out)
{
    const HostDecl &d = host_decl();
    CHECK(HC_NPAGES == d.npages);
    for (int i = 0; i < d.npages; i++) {
        bool found = false;
        for (int k = 0; k < HC_NPAGES; k++) {
            found = found || !strcmp(HC_PAGES[k].id, d.pages[i].id);
        }
        CHECK(found);
    }
    hostui_test(4, 0, 0, false);                                   // the settings, over the bench's three entries
    int cores_at = -1, n = 0;
    {
        int kinds[HC_SET_SLOTS];
        n = hc_set_kinds(kinds);
        for (int i = 0; i < n; i++) {
            if (kinds[i] == SK_CORES) {
                cores_at = i;
            }
        }
    }
    CHECK(cores_at > 0);
    for (int i = 0; i < cores_at; i++) {
        hostui_test_key(KEY_DOWN);
    }
    hostui_test_key(KEY_RETURN);                                   // the Cores and ROMs page opens: its lines are in the dump
    static char dump[60000];
    int len = hc_rows_dump(dump, sizeof(dump));
    CHECK(len > 0 && len < (int)sizeof(dump) - 1);
    char path[600];
    snprintf(path, sizeof(path), "%s_rows.txt", out);
    if (FILE *f = fopen(path, "w")) {
        fwrite(dump, 1, len, f);
        fclose(f);
    }
    const char *gp = getenv("ROWS_GOLDEN");
    CHECK(gp != NULL);
    if (gp) {
        if (getenv("ROWS_GOLDEN_WRITE")) {
            if (FILE *f = fopen(gp, "w")) {
                fwrite(dump, 1, len, f);
                fclose(f);
            }
        }
        std::string want;
        if (FILE *f = fopen(gp, "r")) {
            char b[4096];
            size_t r;
            while ((r = fread(b, 1, sizeof(b), f)) > 0) {
                want.append(b, r);
            }
            fclose(f);
        }
        bool same = want == std::string(dump, len);
        if (!same) {
            printf("the row dump differs from %s (written to %s)\n", gp, path);
            std::string got(dump, len);                                // the first differing lines, so the golden can be updated from the log
            size_t pw = 0, pg = 0;
            for (int shown = 0; shown < 12 && (pw < want.size() || pg < got.size()); ) {
                size_t ew = want.find('\n', pw), eg = got.find('\n', pg);
                std::string lw = want.substr(pw, ew == std::string::npos ? std::string::npos : ew - pw), lg = got.substr(pg, eg == std::string::npos ? std::string::npos : eg - pg);
                if (lw != lg) { printf("  want: %s\n  got:  %s\n", lw.c_str(), lg.c_str()); shown++; }
                pw = ew == std::string::npos ? want.size() : ew + 1;
                pg = eg == std::string::npos ? got.size() : eg + 1;
            }
        }
        CHECK(same);
    }
    // what the screen drew is what the producers say: the settings rows ...
    char se[2000], want_se[2000];
    hostui_test_settings(se, sizeof(se));
    hostui_test_key(KEY_ESCAPE);                                   // the page closes
    hostui_test_settings(se, sizeof(se));
    int kinds[HC_SET_SLOTS];
    n = hc_set_kinds(kinds);
    int o = snprintf(want_se, sizeof(want_se), "sel=%d|", 0);
    for (int i = 0; i < n; i++) {
        char v[64];
        const char *name, *help;
        hc_set_row_text(kinds[i], name, v, help, i == 0);
        o += snprintf(want_se + o, sizeof(want_se) - o, "%s=%s|", name, v);
    }
    int sel_now = cores_at;                                        // the cursor is still on the row that opened the page
    char fixed[2000];
    snprintf(fixed, sizeof(fixed), "sel=%d|%s", sel_now, strchr(want_se, '|') + 1);
    if (strcmp(se, fixed)) { printf("SETROWS screen:   %s\nSETROWS producer: %s\n", se, fixed); }
    CHECK(!strcmp(se, fixed));
    // ... and the boot menu rows
    hostui_test(0, 1, 0, false);
    static char md[8000];
    hostui_test_dump(md, sizeof(md));
    for (int i = 0; i < hostui_test_cfg_n(); i++) {
        HcEntryRow r;
        CHECK(hc_row_entry(i, true, &r));
        char want_row[400];
        snprintf(want_row, sizeof(want_row), "row %d|%s|%s|%s|%s|%s|", i, r.name, r.ver, r.badge, r.def ? "auto" : "", i == 1 ? "SEL" : "");
        CHECK(strstr(md, want_row) != NULL);
        // T209 D10/D11: one pictogram per group (glyph + fill), all the same size; the marker reads "auto"
        const char *glyph = r.pic == HCP_OK ? "ok" : r.pic == HCP_CHECK ? "check" : "bad";      // the dump names the group the glyph drew
        char want_pic[200];
        snprintf(want_pic, sizeof(want_pic), "\npic %d|%s|", i, glyph);
        CHECK(strstr(md, want_pic) != NULL);
        CHECK(strstr(md, "|auto\n") != NULL);
    }
    // the pictogram groups of the words (D10)
    {
        HcEntryRow r;
        int seen[3] = { 0, 0, 0 };
        for (int i = 0; i < hostui_test_cfg_n(); i++) {
            CHECK(hc_row_entry(i, true, &r));
            seen[r.pic]++;
            bool ok = !strcmp(r.badge, "hash ok") || !strcmp(r.badge, "ready") || !strcmp(r.badge, "backup") || !strcmp(r.badge, "foreign");
            bool chk = !strcmp(r.badge, "changed") || !strcmp(r.badge, "?");
            CHECK(r.pic == (ok ? HCP_OK : chk ? HCP_CHECK : HCP_BAD));
        }
        CHECK(seen[HCP_OK] + seen[HCP_CHECK] + seen[HCP_BAD] == hostui_test_cfg_n());
        CHECK(strstr(md, "|22x22|auto\n") != NULL || strstr(md, "|20x20|auto\n") != NULL);
        // the status line shows the selected row's word while no note stands; no path anywhere in the menu rows
        HcEntryRow sel;
        CHECK(hc_row_entry(1, true, &sel));
        char want_det[160];
        snprintf(want_det, sizeof(want_det), "note='' ");
        if (strstr(md, want_det)) {
            snprintf(want_det, sizeof(want_det), "detail='%s'", sel.badge);
            CHECK(strstr(md, want_det) != NULL);
        }
    }
}

// A fixture EDID: a base block (established timings: bit 0 of `pc` 800 x 600, bit 1 1024 x 768; bit 2 a standard timing 1280 x 1024) and, with `nv` > 0, a CEA block
// whose video data block holds the VICs as written (bit 7 = native). `bad`: the base block's checksum is wrong.
static void fk_make_edid(const uint8_t *vics, int nv, unsigned pc, bool bad)
{
    memset(g_fk_edid, 0, sizeof(g_fk_edid));
    static const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memcpy(g_fk_edid, hdr, 8);
    if (pc & 1) { g_fk_edid[35] |= 0x01; }
    if (pc & 2) { g_fk_edid[36] |= 0x08; }
    for (int i = 0; i < 8; i++) { g_fk_edid[38 + 2 * i] = 0x01; g_fk_edid[39 + 2 * i] = 0x01; }
    if (pc & 4) { g_fk_edid[38] = (uint8_t)(1280 / 8 - 31); g_fk_edid[39] = (uint8_t)(2 << 6); }       // 1280 x 1024 (5:4), 60 Hz
    g_fk_edid[126] = nv ? 1 : 0;
    unsigned t = 0;
    for (int i = 0; i < 127; i++) { t += g_fk_edid[i]; }
    g_fk_edid[127] = (uint8_t)(0x100 - (t & 0xFF));
    if (bad) { g_fk_edid[127] ^= 0x5A; }
    g_fk_edid_len = 128;
    if (nv) {
        uint8_t *x = g_fk_edid + 128;
        x[0] = 0x02; x[1] = 0x03;
        int q = 4;
        x[q++] = (uint8_t)((2 << 5) | nv);
        for (int i = 0; i < nv; i++) { x[q++] = vics[i]; }
        x[2] = (uint8_t)q;
        unsigned u = 0;
        for (int i = 0; i < 127; i++) { u += x[i]; }
        x[127] = (uint8_t)(0x100 - (u & 0xFF));
        g_fk_edid_len = 256;
    }
}
static void picnote_edid(void) { memset(g_fk_edid, 0, sizeof(g_fk_edid)); g_fk_edid_len = 0; }       // no EDID
static bool mode_is(int hdmi) { return g_gw_mode == video_mode_index(hdmi, VID_SYS_PAL50) || g_gw_mode == video_mode_index(hdmi, VID_SYS_NTSC60); }
static bool picnote_kept(void) { char t[200]; note_px(t, sizeof(t)); return !strncmp(t, "Picture kept", 12); }
// one case: the page `view` (1 File Browser: V; 4 Settings: the Picture row), the screen with / without a list that reaches `want`
static void picnote_case(const char *out, const char *name, int view, int want, bool vga)
{
    char p[512], txt[200];
    g_gw_model = true; g_gw_wr = g_gw_pr = 0; g_gw_mode = 0;
    if (vga) {                                                      // 720p60, 1080p60 and 800 x 600: Auto is 1080p, the 800 x 600 row is a change
        static const uint8_t v60[2] = { 4, 16 };
        fk_make_edid(v60, 2, 1u, false);
    } else {
        picnote_edid();
    }
    fresh_video(VID_SYS_PAL50, -1);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    hostui_test_mode(VID_SYS_PAL50, VID_HDMI_1080P);               // every case starts on the 960 x 540 canvas: the 800 x 600 ones change it
    PCHECK(hostui_test_canvas_w() == 960);
    if (view == 4) {
        hostui_test_key(KEY_F8);
        hostui_test_key(KEY_DOWN);                                  // the Picture row
        hostui_test_key(KEY_RETURN);                                // opens the page
    } else {
        hostui_test_view(1);
        hostui_test_key('v');
    }
    // T187 D5: the cursor goes down to the row of the wanted mode (browsing applies nothing: the mode is still the old one), RETURN uses it
    char wn[64], cn[64];
    snprintf(wn, sizeof(wn), "%s", video_hdmi_name(want));
    int wr0 = g_gw_wr;
    for (int i = 0; i < 12; i++) {
        hostui_test_pm(cn, sizeof(cn), 0, 0, 0, 0);
        if (!strcmp(cn, wn)) { break; }
        hostui_test_key(KEY_DOWN);
    }
    PCHECK(!strcmp(cn, wn) && g_gw_wr == wr0);                         // nothing was asked of the gateware while browsing
    hostui_test_key(KEY_RETURN);
    PCHECK(mode_is(want) && !picnote_kept());
    PCHECK(hostui_test_canvas_w() == (want == VID_HDMI_800x600 ? 800 : 960));
    // T164 Q7 (2026-11-04): the question is a box in the middle of the screen: title, the picture, the seconds, a blank line, its key
    char t0[96], l0[96], l1[96], kt[96];
    int ky = 0, up = 0, by = 0, cx = 99, cy = 99;
    bool shown = hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy);
    printf("PICNOTE %-18s mode %d: box %d '%s' / '%s' / '%s' / '%s' key %d above %d bottom %d off %d,%d\n", name, g_gw_mode, (int)shown, t0, l0, l1, kt, ky, up, by, cx, cy);
    snprintf(p, sizeof(p), "%s/picnote_%s.png", out, name);
    png_out(p, 1, hostui_test_canvas_w(), hostui_test_canvas_h(), 0, 0);
    PCHECK(shown && !strcmp(t0, "Keep this picture?") && strstr(l1, "In 8 s") && strstr(kt, "RETURN keeps it") && strstr(kt, "LEFT or ESC goes back now"));
    PCHECK(shown && ky - up >= 20 && ky + 20 <= by && by < hostui_test_canvas_h());          // one blank line before the key, the key inside the box, the box on the canvas
    PCHECK(shown && abs(cx) <= 2 && abs(cy) <= 2);                                          // in the middle of the screen
    g_hv_ms += 3000;
    hostui_test_pump();                                             // the loop's tick: the count runs
    shown = hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy);
    printf("PICNOTE %-18s +3 s: '%s'\n", name, l1);
    PCHECK(shown && strstr(l1, "In 5 s"));
    // any input keeps it and is spent on that (T192.2-S15): a key, or an intent that arrives as a remote "go to the Network page" (it would leave the page, it keeps the picture)
    hostui_test_key(view == 1 ? HC_ACT_KEY(HCA_NAV_NET) : KEY_UP);
    int px = note_px(txt, sizeof(txt));
    PCHECK(strstr(txt, "Picture kept") && px > 50);
    PCHECK(!hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy));              // the box is gone with the key
    PCHECK(g_saved_hdmi == want);
}
// T187 D5: the host's Picture page with the mode-request gateware: the 8-second question names both modes, LEFT / ESC go back at once, the timeout goes back,
// the page stays open with the cursor on the rejected mode and "now" on the old one, RETURN keeps the new one.
static void pm_goto(int hdmi)
{
    char cn[64];
    for (int i = 0; i < 12; i++) {
        hostui_test_pm(cn, sizeof(cn), 0, 0, 0, 0);
        if (!strcmp(cn, video_hdmi_name(hdmi))) { return; }
        hostui_test_key(KEY_DOWN);
    }
}
static void pmpage_case(void)
{
    char t0[96], l0[96], l1[96], kt[96], cn[64], info[200], tn[64], on[64];
    int ky = 0, up = 0, by = 0, cx = 0, cy = 0, sl = 0, rw = 0;
    g_gw_model = true; g_gw_wr = g_gw_pr = 0; g_gw_mode = 0;
    static const uint8_t v60[2] = { 4, 16 };
    fk_make_edid(v60, 2, 1u, false);                                     // 720p60, 1080p60 and 800 x 600: the PAL machine gets the other family
    fresh_video(VID_SYS_PAL50, -1);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    int old = hv_state()->hdmi;
    int tgt = old == VID_HDMI_720P ? VID_HDMI_1080P : VID_HDMI_720P;
    snprintf(tn, sizeof(tn), "%s", video_hdmi_name(tgt));
    snprintf(on, sizeof(on), "%s", video_hdmi_name(old));
    PCHECK(old >= 0);
    hostui_test_key('v');
    { int open = hostui_test_pm(cn, sizeof(cn), &sl, &rw, info, sizeof(info)); printf("PMPAGE open %d rows %d sel %d name '%s' old %d\n", open, rw, sl, cn, old); }
    PCHECK(hostui_test_pm(cn, sizeof(cn), &sl, &rw, info, sizeof(info)) == 1 && rw == 4 && sl == 0);
    PCHECK(!strcmp(info, "This screen names no 50 Hz modes; 60 Hz modes are listed."));      // S: the fallback line of the spec
    int wr0 = g_gw_wr;
    pm_goto(tgt);
    PCHECK(g_gw_wr == wr0 && hv_state()->hdmi == old && !hv_state()->pending);       // browsing asked nothing of the gateware
    hostui_test_key(KEY_RETURN);
    PCHECK(hv_state()->hdmi == tgt && hv_state()->pending && g_gw_wr == wr0 + 1);
    PCHECK(hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy) && strstr(l0, tn) && strstr(l0, " Hz") && strstr(l1, "In 8 s") && strstr(l1, on)
           && strstr(kt, "RETURN keeps it") && strstr(kt, "LEFT or ESC goes back now"));
    hostui_test_key(KEY_LEFT);                                           // goes back at once, nothing stored
    PCHECK(hv_state()->hdmi == old && !hv_state()->pending && g_saved_hdmi == -1 && !hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy));
    PCHECK(hostui_test_pm(cn, sizeof(cn), &sl, &rw, 0, 0) == 1 && !strcmp(cn, tn));       // the page stays, the cursor on the rejected mode
    hostui_test_key(KEY_RETURN);
    PCHECK(hv_state()->hdmi == tgt && hv_state()->pending);
    hostui_test_key(KEY_ESCAPE);                                         // ESC is the same
    PCHECK(hv_state()->hdmi == old && g_saved_hdmi == -1 && hostui_test_pm(0, 0, 0, 0, 0, 0) == 1);
    hostui_test_key(KEY_RETURN);                                         // asked again, no key: the timeout goes back
    PCHECK(hv_state()->hdmi == tgt);
    g_hv_ms += 8100;
    hostui_test_pump();
    PCHECK(hv_state()->hdmi == old && !hv_state()->pending && g_saved_hdmi == -1 && hostui_test_pm(cn, sizeof(cn), &sl, &rw, 0, 0) == 1 && !strcmp(cn, tn));
    hostui_test_key(KEY_RETURN);                                         // the question again: RETURN keeps it, the page stays on the new mode
    hostui_test_key(KEY_RETURN);
    PCHECK(hv_state()->hdmi == tgt && !hv_state()->pending && g_saved_hdmi == tgt && hostui_test_pm(cn, sizeof(cn), &sl, &rw, 0, 0) == 1 && !strcmp(cn, tn));
    int wr1 = g_gw_wr;
    hostui_test_key(KEY_RETURN);                                         // RETURN on the mode in force: the page closes, nothing is asked
    PCHECK(hostui_test_pm(0, 0, 0, 0, 0, 0) == 0 && g_gw_wr == wr1 && hv_state()->hdmi == tgt);
}

// T187 D5 render fixtures: the Picture page on a screen that names 6, 4, 1 modes, none (no EDID) and a corrupt one; an NTSC machine (the 60 Hz family, the PC rows are its).
// The rows, the refresh and native columns from the table, the info line, the places of the objects, nothing applied while browsing, one apply on RETURN, LEFT / ESC.
struct PmFix { const char *name; uint8_t vics[3]; int nv; unsigned pc; bool bad, none; int n; int list[6]; unsigned nat; const char *info; };
static void pmfix_case(const char *out, const PmFix &f)
{
    char t0[96], l0[96], l1[96], kt[96], nm[64], hz[32], tg[32], info[200], p[512];
    int ky = 0, up = 0, by = 0, cx = 0, cy = 0, sl = 0, rw = 0;
    g_gw_model = true; g_gw_wr = g_gw_pr = 0; g_gw_mode = 0;
    if (f.none) { memset(g_fk_edid, 0, sizeof(g_fk_edid)); g_fk_edid_len = 0; } else { fk_make_edid(f.vics, f.nv, f.pc, f.bad); }
    g_saved_hdmi = -1; g_saved_sys = VID_SYS_NTSC60; hostui_test_video(VID_SYS_NTSC60, -1);        // the 60 Hz family: the host lists its own family's rows, the PC modes among them
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    int old = hv_state()->hdmi;
    hostui_test_key('v');
    PCHECK(hostui_test_pm(nm, sizeof(nm), &sl, &rw, info, sizeof(info)) == 1 && rw == 1 + f.n && sl == 0);
    printf("PMFIX %-8s rows %d sel %d old %s info '%s'\n", f.name, rw, sl, video_hdmi_name(old), info);
    PCHECK(!strcmp(info, f.info));
    int nnow = 0, oldrow = -1;
    for (int i = 0; i < rw && i < 1 + f.n; i++) {                       // 1: the rows are Auto and the list in order, with the table's refresh and the screen's native flags
        PCHECK(hostui_test_pm_row(i, nm, sizeof(nm), hz, sizeof(hz), tg, sizeof(tg)));
        if (i == 0) { PCHECK(!strncmp(nm, "Auto", 4)); } else { PCHECK(!strcmp(nm, video_hdmi_name(f.list[i - 1]))); }
        int hdmi = i == 0 ? old : f.list[i - 1];
        int row = 6 + hdmi;                                               // the 60 Hz row
        const EdidRow *er = edid_row(row);
        char ex[32];
        uint64_t tot = (uint64_t)er->htotal * er->vtotal;
        unsigned d = (unsigned)(((uint64_t)er->pixel_hz * 10 + tot / 2) / tot);
        snprintf(ex, sizeof(ex), "%u.%u Hz", d / 10, d % 10);
        PCHECK(!strcmp(hz, ex));
        PCHECK((strstr(tg, "native") != 0) == (((f.nat >> row) & 1) != 0));
        if (strstr(tg, "now")) { nnow++; oldrow = i; }
    }
    PCHECK(nnow == 1 && oldrow == 0 && sl == 0);     // D9: Auto is the choice: one mark, on Auto's row, the cursor opened on it; the mode it resolved to has none
    int ix1, iy1, ix2, iy2, rx1, ry1, rx2, ry2, fx1, fy1, fx2, fy2, px1, py1, px2, py2, ay2 = 0;
    PCHECK(hostui_test_pm_geo(-1, &px1, &py1, &px2, &py2) && hostui_test_pm_geo(-2, &ix1, &iy1, &ix2, &iy2) && hostui_test_pm_geo(-4, &fx1, &fy1, &fx2, &fy2));
    int wr0 = g_gw_wr, hs0 = g_hv_stores, last_y2 = iy2;
    for (int i = 0; i < rw; i++) {                                       // 2, R1/R2: every row inside the page, under the info line, above the key bar, none on another
        PCHECK(hostui_test_pm_geo(i, &rx1, &ry1, &rx2, &ry2));
        PCHECK(rx1 >= px1 && rx2 <= px2 && ry1 >= iy2 && ry2 <= py2 && ry1 >= last_y2 && ry2 <= fy1);
        last_y2 = ry2; ay2 = ry2;
    }
    PCHECK(ix1 >= px1 && ix2 <= px2 && ay2 <= fy1);
    for (int i = 0; i < rw; i++) { hostui_test_key(KEY_DOWN); }          // 4: browsing asks nothing of the gateware and applies nothing
    for (int i = 0; i < rw; i++) { hostui_test_key(KEY_UP); }
    PCHECK(g_gw_wr == wr0 && hv_state()->hdmi == old && !hv_state()->pending && g_hv_stores == hs0);
    PCHECK(hostui_test_pm(nm, sizeof(nm), &sl, &rw, 0, 0) == 1 && sl == 0);
    snprintf(p, sizeof(p), "%s/pm_%s.png", out, f.name);
    png_out(p, 1, hostui_test_canvas_w(), hostui_test_canvas_h(), 0, 0);
    int tgt = -1, trow = -1;
    for (int i = 0; i < f.n; i++) { if (f.list[i] != old) { tgt = f.list[i]; trow = i + 1; break; } }
    if (tgt >= 0) {
        for (int i = 0; i < trow; i++) { hostui_test_key(KEY_DOWN); }
        hostui_test_key(KEY_RETURN);
        PCHECK(hv_state()->hdmi == tgt && hv_state()->pending && g_gw_wr == wr0 + 1);         // one apply, the question is up
        PCHECK(hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy) && strstr(l0, video_hdmi_name(tgt)) && strstr(l1, "In 8 s") && strstr(l1, video_hdmi_name(old)));
        snprintf(p, sizeof(p), "%s/pm_%s_question.png", out, f.name);
        png_out(p, 1, hostui_test_canvas_w(), hostui_test_canvas_h(), 0, 0);
        hostui_test_key(KEY_LEFT);
        PCHECK(hv_state()->hdmi == old && !hv_state()->pending && hostui_test_pm(nm, sizeof(nm), &sl, &rw, 0, 0) == 1 && sl == trow);   // back at once, the page stays on the rejected mode
        int wr1 = g_gw_wr;
        hostui_test_key(KEY_ESCAPE);                                     // ESC on the page: closed, nothing asked
        PCHECK(hostui_test_pm(0, 0, 0, 0, 0, 0) == 0 && g_gw_wr == wr1 && hv_state()->hdmi == old);
        hostui_test_key('v');                                            // D9, a fixed mode: pick it and keep it; the page opens on it, with the one mark on its row
        for (int i = 0; i < trow; i++) { hostui_test_key(KEY_DOWN); }
        hostui_test_key(KEY_RETURN);
        hostui_test_key(KEY_RETURN);
        PCHECK(hv_state()->hdmi == tgt && !hv_state()->pending);
        hostui_test_key(KEY_ESCAPE);
        hostui_test_key('v');
        PCHECK(hostui_test_pm(nm, sizeof(nm), &sl, &rw, info, sizeof(info)) == 1 && sl == trow);
        int fnow = 0, frow = -1;
        for (int i = 0; i < rw; i++) {
            PCHECK(hostui_test_pm_row(i, nm, sizeof(nm), hz, sizeof(hz), tg, sizeof(tg)));
            if (strstr(tg, "now")) { fnow++; frow = i; }
        }
        PCHECK(fnow == 1 && frow == trow);
        hostui_test_key(KEY_ESCAPE);
    } else {
        hostui_test_key(KEY_DOWN);
        hostui_test_key(KEY_RETURN);                                     // the only mode is the one in force: the page closes, nothing is asked
        PCHECK(hostui_test_pm(0, 0, 0, 0, 0, 0) == 0 && g_gw_wr == wr0 && hv_state()->hdmi == old && !hv_state()->pending);
    }
}
static void pmfix(const char *out)
{
    static const PmFix fx[] = {
        { "six",     { 2, 4, 144 },  3, 7u, false, false, 6, { 0, 1, 2, 3, 4, 5 }, 1u << 8, "Your screen offers 6 modes. Nothing changes until you pick one." },
        { "four",    { 2, 144, 0 },  2, 3u, false, false, 4, { 0, 2, 3, 4 },       1u << 8, "Your screen offers 4 modes. Nothing changes until you pick one." },
        { "one",     { 132, 0, 0 },  1, 0u, false, false, 1, { 1 },                1u << 7, "Your screen offers 1 mode. Nothing changes until you pick one." },
        { "none",    { 0, 0, 0 },    0, 0u, false, true,  3, { 0, 1, 2 },          0,       "The screen did not name its modes; the standard TV modes are listed." },
        { "corrupt", { 2, 4, 144 },  3, 7u, true,  false, 3, { 0, 1, 2 },          0,       "The screen did not name its modes; the standard TV modes are listed." },
    };
    for (size_t i = 0; i < sizeof(fx) / sizeof(fx[0]); i++) { pmfix_case(out, fx[i]); }
}
// T187: the other end of the question: nobody answers, the 8 seconds run out, the old mode comes back and the toast says so (picture of that screen)
static void picnote_revert_case(const char *out, const char *name)
{
    char p[512], txt[200], wn[64], cn[64];
    g_gw_model = true; g_gw_wr = g_gw_pr = 0; g_gw_mode = 0;
    static const uint8_t v60[2] = { 4, 16 };
    fk_make_edid(v60, 2, 1u, false);
    fresh_video(VID_SYS_PAL50, -1);
    hostui_test_init("countdown=5\ndefault=0\ncore=/SD/cores/trx64.cor|C64||0\n");
    hostui_test_mode(VID_SYS_PAL50, VID_HDMI_1080P);
    hostui_test_key(KEY_F8);
    hostui_test_key(KEY_DOWN);
    hostui_test_key(KEY_RETURN);
    snprintf(wn, sizeof(wn), "%s", video_hdmi_name(VID_HDMI_800x600));
    for (int i = 0; i < 12; i++) {
        hostui_test_pm(cn, sizeof(cn), 0, 0, 0, 0);
        if (!strcmp(cn, wn)) { break; }
        hostui_test_key(KEY_DOWN);
    }
    hostui_test_key(KEY_RETURN);
    char t0[96], l0[96], l1[96], kt[96];
    int ky = 0, up = 0, by = 0, cx = 99, cy = 99;
    PCHECK(mode_is(VID_HDMI_800x600) && hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy));
    g_hv_ms += 9000;
    hostui_test_pump();                                             // the loop's tick: 8 s without a key
    note_px(txt, sizeof(txt));
    printf("PICNOTE %-18s reverted: mode %d note '%s'\n", name, g_gw_mode, txt);
    snprintf(p, sizeof(p), "%s/picnote_%s.png", out, name);
    png_out(p, 1, hostui_test_canvas_w(), hostui_test_canvas_h(), 0, 0);
    PCHECK(mode_is(VID_HDMI_1080P) && hostui_test_canvas_w() == 960);
    PCHECK(!hostui_test_pq(t0, l0, l1, kt, sizeof(t0), &ky, &up, &by, &cx, &cy));
    PCHECK(!strncmp(txt, "Picture: back to", 16));
}
static void picnote(const char *out)
{
    pmpage_case();
    pmfix(out);
    picnote_case(out, "browser_1080p", 1, VID_HDMI_1080P, false);
    picnote_case(out, "settings_1080p", 4, VID_HDMI_1080P, false);
    picnote_case(out, "browser_800x600", 1, VID_HDMI_800x600, true);
    picnote_case(out, "settings_800x600", 4, VID_HDMI_800x600, true);
    picnote_revert_case(out, "settings_800x600_reverted");
    g_gw_model = false;
    picnote_edid();
    fresh_video(VID_SYS_PAL50, -1);
    hostui_test_mode(VID_SYS_PAL50, VID_HDMI_1080P);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);                 // a crash later must still have printed the FAIL that went red first
    const char *out = argc > 1 ? argv[1] : ".";
    if (argc > 2) snprintf(g_root, sizeof(g_root), "%s", argv[2]);
    if (getenv("T152_NOLOCK")) {                     // the toggle-off build: only this scenario (the others need the row)
        nolock_scenario(out);
        printf("render bench (T152 D9, lock off): %d checks, %d failed\n", checks, fails);
        return fails != 0;
    }
    if (getenv("T146_START")) {                      // a second process: the start with the picture the first one saved
        snap_start_scenario(out);
        printf("render bench (T146 start): %d checks, %d failed\n", checks, fails);
        return fails != 0;
    }
    splash_startup(out);
    snap_save_scenario(out);
    if (getenv("T146_SAVE_ONLY")) {
        printf("render bench (T146 save): %d checks, %d failed\n", checks, fails);
        return fails != 0;
    }
    scenarios(out);
    scenarios_fw(out);
    scenarios_ic(out);
    scenarios_lc(out);
    scenarios_media(out);
    scenarios_t139(out);
    scenarios_t148(out);
    scenarios_t198(out);
    scenarios_t137(out);
    scenarios_t137r(out);
    scenarios_t152(out);
    scenarios_t184(out);
    picnote(out);
    rows_scenario(out);
    printf("firmware pages: %d checked, heights %d..%d\n", g_geom_pages, g_geom_hmin, g_geom_hmax);
    printf("T152 D10: %d of them with the key line inside the box\n", g_key_pages);
    CHECK(g_key_pages >= 15);
    CHECK(g_geom_pages >= 20 && g_geom_hmax - g_geom_hmin >= 100);                // sized to the content: a refusal is not as high as a question
    const struct { int view, sel, bsel; bool start; const char *name; } shots[] = {
        { 0, 0, 0, false, "menu" }, { 1, 0, 1, false, "browse" }, { 1, 0, 3, false, "browse_long" },
        { 2, 1, 0, false, "edit" }, { 3, 0, 0, false, "net" }, { 4, 0, 0, false, "settings" }, { 0, 0, 0, true, "start" } };
    for (auto &s : shots) {
        hostui_test(s.view, s.sel, s.bsel, s.start);
        char p[512];
        snprintf(p, sizeof(p), "%s/%s.png", out, s.name);
        png(p, 2);
        printf("%s\n", p);
    }
    // Double buffer sync: after incremental updates the shown buffer must equal a full redraw (what the layer shows
    // after flips must not carry stale areas). Each step: incremental update, remember the shown buffer, invalidate
    // everything, redraw, compare.
    hostui_test(0, 0, 0, false);
    int bad = 0;
    for (int i = 0; i < 40; i++) {
        hostui_test_move(false);
        if (i % 7 == 3) hostui_test_move(true);
        static uint16_t before[960 * 540];
        memcpy(before, fb, sizeof(before));
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
        size_t diff = 0;
        for (size_t k = 0; k < 960 * 540; k++) diff += before[k] != fb[k];
        if (diff) { bad++; printf("step %d: shown buffer differs from a full redraw in %zu px\n", i, diff); }
    }
    printf("double buffer sync: %d of 40 steps stale\n", bad);
    CHECK(bad == 0);
    // latency of a selection move against a full rebuild (native x86, not the RISC-V: compare the pixel counts and the ratio)
    hostui_test(0, 0, 0, false);
    for (int full = 0; full <= 1; full++) {
        double best = 1e9;
        uint32_t px = 0;
        for (int i = 0; i < 20; i++) {
            struct timespec a, b;
            clock_gettime(CLOCK_MONOTONIC, &a);
            px = hostui_test_move(full);
            clock_gettime(CLOCK_MONOTONIC, &b);
            double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
            if (ms < best) best = ms;
        }
        printf("%s: %u px invalidated, %.2f ms native\n", full ? "full rebuild " : "selection move", px, best);
    }
    // T101: scrolling the Add core list. Each key moves the selection by one row; when the window moves too, the rows are
    // refilled: 39 labels change, and LVGL keeps 32 invalid areas, the 33rd of which becomes the whole screen (lv_refr.c lv_inv_area).
    // So no key may reach the whole screen: the two panels of the view are the most it may cost (T101 §5, 2026-10-22).
    hostui_test_browse_at("/Usb0/scroll");
    CHECK(hostui_test_path_font_is_ui());
    {
        uint32_t worst = 0, total = 0;
        for (int i = 0; i < 19; i++) {
            uint32_t px = hostui_test_key(KEY_DOWN);
            printf("browse step %2d: %u px invalidated\n", i, px);
            if (px > worst) worst = px;
            total += px;
            if (i == 18) {      // the last row is the .cfw whose name is longer than its column: one line, no second line below it
                char pp[512]; snprintf(pp, sizeof(pp), "%s/browse_scroll.png", out); png(pp, 2);
                int dirty = 0;
                for (int y = 486; y < 492; y++)
                    for (int x = 224; x < 224 + 330; x++) dirty += fb[y * 960 + x] != fb[486 * 960 + 224];
                printf("browse long name: %d px below the last row\n", dirty);
                CHECK(dirty == 0);
            }
        }
        printf("browse scroll: worst key %u px, 19 keys %u px (whole screen %u)\n", worst, total, 960u * 540u);
        CHECK(worst < 2u * 367u * 380u);            // the two panels of the view, not the screen (518400)
    }
    {   // T180 D7 / S6: "+" pages the File Browser selection down, "-" up, stopping at the ends; the letters keep their actions
        extern int hostui_test_bsel(void), hostui_test_bn(void), hostui_test_brows(void);
        extern const char *hostui_test_foot(void);
        hostui_test_browse_at("/Usb0/pages");
        const int pg = hostui_test_brows();
        CHECK(hostui_test_bn() == 27 && pg == 13);                    // ".." + 26 files: three pages of 13, 13, 1
        CHECK(hostui_test_bsel() == 0);
        hostui_test_key('-');
        CHECK(hostui_test_bsel() == 0);                               // "-" at the top stays
        hostui_test_key('+');
        CHECK(hostui_test_bsel() == pg);                              // one page down
        hostui_test_key('+');
        CHECK(hostui_test_bsel() == 2 * pg);                          // the last page
        hostui_test_key('+');
        CHECK(hostui_test_bsel() == 2 * pg);                          // a third "+" stays there
        hostui_test_key('-');
        CHECK(hostui_test_bsel() == pg);                              // "-" goes back one page
        hostui_test_key('-'); hostui_test_key('-');
        CHECK(hostui_test_bsel() == 0);
        hostui_test_browse_at("/Usb0/sized");                          // 20 bundles + "..": the last step is a part page, clamped to the last row
        hostui_test_key('+'); hostui_test_key('+');
        CHECK(hostui_test_bsel() == hostui_test_bn() - 1);
        hostui_test_key('-');
        CHECK(hostui_test_bsel() == hostui_test_bn() - 1 - pg);
        hostui_test_browse_at("/Usb0/pages");                          // the letters keep their action: C opens the target pick, "+" pages in it too
        hostui_test_key(KEY_DOWN);
        hostui_test_key('c');
        CHECK(strstr(hostui_test_foot(), "ESC cancels") != NULL);
        hostui_test_key('+');
        CHECK(hostui_test_bsel() == 1 + pg);
        hostui_test_key('-');
        CHECK(hostui_test_bsel() == 1);
        hostui_test_key(KEY_ESCAPE);
        CHECK(strstr(hostui_test_foot(), "ESC cancels") == NULL);
    }
    {   // T101: the hint of a bundle that can be added is two lines, one key each, not a long line broken at a word
        hostui_test_browse_at("/Usb0/cores");
        char rp[600];
        for (int k = 0; k < 6; k++) {
            hostui_test_browse_right(rp, sizeof(rp));
            if (strstr(rp, "shown=1") && strstr(rp, "adds it")) break;
            hostui_test_key(KEY_DOWN);
        }
        printf("browse hint: %d lines (%s)\n", hostui_test_msg_lines(), rp);
        CHECK(strstr(rp, "adds it to the boot menu\nS starts once") != 0 && hostui_test_msg_lines() == 2);
    }
    api_remote_start_timeout();
    api_asked_remove();
    modes_pages(out);
    areas();
    printf("render bench: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
