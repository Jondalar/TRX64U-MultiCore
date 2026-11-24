// The menu simulator (T236): the core application's real menu - ui.cc, MenuCtl, LVGL, the core's descriptor and menu record - on the Mac, in a
// window, driven by the keyboard. The board is the render bench's rig of fakes (tests/coreapp/menu_rig.h, board_stubs.cc); the picture is the
// output frame the MultiCore host puts on HDMI: the mode's active area, black, with the menu canvas at the place the application's own video
// service gives it (Video's video_place_menu, PxPort::menu_place), scaled by nearest sampling as the pixel layer does.
//
//   tools/menusim/run.sh <core> [--mode M] [--toml FILE] [--ops FILE] [--stick FILE] [--scale N]    window
//   tools/menusim/run.sh <core> --keys F3,DOWN,RETURN --shot out.png [--mode M] ...                  headless: the keys, one PNG, end
//   tools/menusim/run.sh <core> --keys K1,K2,... --stats FILE [--mode M]                              headless: one line of figures per key (speed.sh)
//   tools/menusim/run.sh <core> --keys K1,K2,... --shots DIR [--mode M]                              headless: the picture after every key (tools/menusim/same_pictures.sh)
//                                       (in the list FIND:<text> moves the selection to the first row whose label starts with the text, SEL:<n> to row n)
//
// Keys in the window: cursor keys, RETURN, ESC, BACKSPACE, TAB, HOME, END, PAGE UP/DOWN, DEL, SPACE, letters and digits, F1-F8 (shift+F1 = F2 as
// on a C64 keyboard), F12 opens the menu after a close. Cmd (Ctrl elsewhere) + R reloads the menu TOML, + S writes a screenshot, + Q quits.
// The menu is built as run.cc builds it (menu_build, then bundle_merge with the description record). A core with a menu TOML
// (coreapp/cores/<core>/menu.toml, or --toml) has it compiled by the tree's tools/descrec.py at start and again when the file changes (polled every
// 250 ms); the keys pressed so far are played again on the new menu, so the page stays.
#include "overlay_probe.h"
#include "hostos.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"
#include "trx_lv_port.h"
#include "trxcanvas.h"
#include "ui.h"
#include "menuctl.h"
#include "settings.h"
#include "video.h"
#include "fakes.h"
#include "keyboard.h"
#include "simwin.h"
#include "picture.h"
#include "menubuild.h"
#include "cores.h"
#include "descrec.h"
#include "contrib.h"
#include "bundle.h"
#include "videomode.h"
#include "edid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "table.cc"                                           // the board's own core table, as the render bench takes it
#include "menu_rig.h"

// ---- options ---------------------------------------------------------------------------------------
struct Opts {
    const char *core = 0, *desc = 0, *mode = "1080p", *keys = 0, *shot = 0, *toml = 0, *ops = 0, *stick = 0, *root = ".", *say = 0, *msg_title = 0, *msg_text = 0, *stats = 0, *shots = 0;
    bool paused = false, stopped = false, elements = false;                         // T256: the PAUSED tag over the still; the places of the elements of the UI TOML
    bool picture = false;                                                            // T256: --picture X,Y,W,H, the running picture of a frame-buffer core on the canvas (the still has its geometry)
    int pic[4] = { 0, 0, 0, 0 };
    int scale = 0;
};
static Opts o;

static void usage(void)
{
    fprintf(stderr, "menusim <core>|--desc FILE [--root TREE] [--mode 1080p|1080p60|720p|720p60|576p|480p|800x600|1024x768|1280x1024] [--toml FILE] [--ops FILE]\n"
                    "        [--stick FILE] [--scale N] [--keys K1,K2,... --shot out.png] [--stats FILE] [--shots DIR] [--say TEXT] [--message TITLE TEXT]\n"
                    "        [--paused] [--stopped] [--elements] [--picture X,Y,W,H]\n");
    exit(2);
}

// T239.3 P3-2 (D32): a core that is in no table - the SDK's - is its TOML: --desc FILE is the description (core sections and menu tables). It is compiled by the tree's tools/descrec.py, the
// descriptor is made of it as the service does for a core's .cor (descrec_core), and the same file is the menu TOML. The core's code (its control hooks) is not here: the menu shows the core's
// operations as the TOML names them and a hook does nothing.
static const CoreDesc *g_src;
static CoreDesc g_desc_core;
static DescRecord *g_desc_rec;
static std::vector<uint8_t> g_desc_bytes;
static std::string g_desc_id;
static bool same(const char *a, const char *b) { return a && b && !strcasecmp(a, b); }

static const CoreDesc *find_core(const char *name)
{
    for (int i = 0; core_table[i]; i++) {
        const CoreDesc *d = core_table[i];
        if (same(d->core_id, name) || same(d->cor_name, name) || same(d->folder, name)) {
            return d;
        }
    }
    return 0;
}

static bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }
// ---- the mode --------------------------------------------------------------------------------------
struct Mode { const char *name; int hdmi, sys; };
static const Mode modes[] = {
    { "1080p", VID_HDMI_1080P, VID_SYS_PAL50 }, { "1080p60", VID_HDMI_1080P, VID_SYS_NTSC60 },
    { "720p", VID_HDMI_720P, VID_SYS_PAL50 }, { "720p60", VID_HDMI_720P, VID_SYS_NTSC60 },
    { "576p", VID_HDMI_480_576, VID_SYS_PAL50 }, { "480p", VID_HDMI_480_576, VID_SYS_NTSC60 },
    { "800x600", VID_HDMI_800x600, VID_SYS_NTSC60 }, { "1024x768", VID_HDMI_1024x768, VID_SYS_NTSC60 }, { "1280x1024", VID_HDMI_1280x1024, VID_SYS_NTSC60 },
};
static Mode mode;
static int frame_w, frame_h;

// ---- the menu: the core's descriptor, or its descriptor with the menu record merged in ---------------------------------------------
static bool compile_toml(std::vector<uint8_t> *rec, std::string *why);
static bool core_from_desc(std::string *why)
{
    o.toml = o.desc;
    if (!compile_toml(&g_desc_bytes, why)) { return false; }
    g_desc_rec = new DescRecord();
    char w[200];
    if (descrec_parse(g_desc_bytes.data(), (uint32_t)g_desc_bytes.size(), g_desc_rec, w, sizeof(w)) != 0) { *why = w; return false; }
    if (!g_desc_rec->has_core) { *why = "the description has no core section ([core]): it describes a menu, not a core"; return false; }
    DescCode code = DescCode();
    if (descrec_core(g_desc_rec, &code, &g_desc_core, w, sizeof(w)) != 0) { *why = w; return false; }
    g_desc_id = g_desc_core.core_id ? g_desc_core.core_id : g_desc_core.folder ? g_desc_core.folder : "core";
    o.core = g_desc_id.c_str();
    g_src = &g_desc_core;
    return true;
}
struct Menu {
    const CoreDesc *open;                 // what ui_open() gets: the core's own descriptor
    CoreDesc *desc;                       // what the menu runs on
    const MenuMerge *mm;
};

// tools/descrec.py of the tree compiles the TOML; the record's bytes, or false with the tool's sentence.
static bool compile_toml(std::vector<uint8_t> *rec, std::string *why)
{
    const char *dir = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s/menusim-rec-%d.bin", dir, (int)getpid());
    std::string cmd = std::string("python3 '") + o.root + "/tools/descrec.py' compile '" + o.toml + "' '" + tmp + "'";
    if (o.ops) { cmd += std::string(" --ops '") + o.ops + "'"; }
    cmd += " 2>&1";
    FILE *p = popen(cmd.c_str(), "r");
    std::string out;
    char b[256];
    while (p && fgets(b, sizeof(b), p)) { out += b; }
    int rc = p ? pclose(p) : -1;
    if (rc != 0) {
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) { out.pop_back(); }
        *why = out.empty() ? "descrec.py failed" : out;
        unlink(tmp);
        return false;
    }
    FILE *f = fopen(tmp, "rb");
    rec->clear();
    int c;
    while (f && (c = fgetc(f)) != EOF) { rec->push_back((uint8_t)c); }
    if (f) { fclose(f); }
    unlink(tmp);
    return true;
}

// The menu as run.cc builds it: menu_build extends the core's descriptor with Picture, Volume, Persist, Home and the hotkeys (run.cc:446, the
// bench's board_desc), then bundle_merge lays the description record over it (run.cc:590). The record is the TOML compiled by descrec.py, else
// the one the application carries (bundle_read's fallback for a start without a .cor, bundle.cc:80), else none. No libraries, no applications.
static bool make_menu(const CoreDesc *src, Menu *m, std::string *why, uint32_t *rec_n)
{
    struct Held { DescRecord rec; std::vector<uint8_t> bytes; CoreDesc out; MenuMerge mm; CoreCtx c; };
    Held *h = new Held();                                     // lives for the run: the menu points into it
    PictureEnv env = { EDID_ROW_MASK, 0, true, mode.sys };
    CoreDesc *ext = board_desc(src, &env);
    *rec_n = 0;
    if (o.toml) {
        if (!compile_toml(&h->bytes, why)) { return false; }
    } else if (src->menu_rec) {
        h->bytes.assign(src->menu_rec, src->menu_rec + src->menu_rec_n);
    }
    BundleExtras x = BundleExtras();
    if (!h->bytes.empty()) {
        char w[200];
        if (descrec_parse(h->bytes.data(), (uint32_t)h->bytes.size(), &h->rec, w, sizeof(w)) != 0) {
            *why = w;
            return false;
        }
        x.desc = &h->rec;
        x.desc_bytes = h->bytes.data();
        *rec_n = (uint32_t)h->bytes.size();
    }
    h->c.desc = ext;                                          // bundle_merge reads the core's operations through it (ops_of)
    int refused = bundle_merge(&h->c, ext, &x, 0, 0, &h->out, &h->mm, 0);
    for (int i = 0; i < h->mm.nerr; i++) { printf("[menu] refused: %s\n", h->mm.err[i]); }
    if (refused > h->mm.nerr) { printf("[menu] %d more contributions refused\n", refused - h->mm.nerr); }
    m->open = src;                                            // run.cc:422 opens the UI on the core's own descriptor
    m->desc = &h->out;
    m->mm = &h->mm;
    return true;
}

// ---- the stick -------------------------------------------------------------------------------------
static void fill_stick(Rig *r)
{
    if (o.stick) {
        FILE *f = fopen(o.stick, "r");
        if (!f) { fprintf(stderr, "menusim: cannot read %s\n", o.stick); exit(2); }
        char line[600];
        while (fgets(line, sizeof(line), f)) {
            char path[512];
            unsigned long n = 0;
            if (line[0] == '#' || sscanf(line, "%511s %lu", path, &n) < 1) { continue; }
            r->fs.put_size(path, n ? n : 1);
        }
        fclose(f);
        return;
    }
    static const struct { const char *p; size_t n; } dflt[] = {
        { "/usb0/games/Katakis.d64", 174848 }, { "/usb0/games/Elite.g64", 333744 }, { "/usb0/games/Turrican.crt", 65616 },
        { "/usb0/games/demo.prg", 2049 }, { "/usb0/games/Giana_Sisters.t64", 30000 }, { "/usb0/games/Turrican_II.adf", 901120 },
        { "/usb0/games/Workbench.hdf", 1048576 }, { "/usb0/games/Mario.nes", 40976 }, { "/usb0/games/Boulderdash.atr", 92176 },
        { "/usb0/games/notes.txt", 100 },
    };
    for (auto &f : dflt) { r->fs.put_size(f.p, f.n); }
}

// ---- the stats mode (--stats FILE) -----------------------------------------------------------------
// One line per key press, taken after the UI has settled: the pixels the display's flush callback received (the sum of the areas, as the board's
// software renderer has to draw them), the flush calls, the objects of the screen that lie in those areas (LVGL does not count what it draws; this is
// every visible object whose box meets a flushed area, the objects the renderer has to consider), the path ui.cc took (a selection restyle, a retext in
// place, or the pane built again), the times, and what the rig counts: file lists and writes. Only the flush callback is
// wrapped; the application is not touched. The settle loop runs at least two and up to 20 more LVGL passes after a key, and each pass reads the rig's clock (which steps
// 50 ms per read in the headless mode), so the simulated time runs further per key than in a --keys/--shot run: anything timed (a toast's timeout, an
// animation) can differ between a --stats/--shots run and a run without them. Two trees run with the same press() are compared fairly.
static lv_display_flush_cb_t g_flush_orig;
static long g_fl_px;
static int g_fl_calls;
static std::vector<lv_area_t> g_fl_areas;
static long long g_us_ui, g_us_lv;                            // inside give_key: MenuCtl::key + ui_menu_render, and the LVGL passes

static long long now_us(void)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void stats_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    g_fl_px += (long)lv_area_get_width(a) * lv_area_get_height(a);
    g_fl_calls++;
    g_fl_areas.push_back(*a);
    g_flush_orig(d, a, px);
}

static void stats_hook(void)
{
    lv_display_t *d = lv_display_get_default();
    g_flush_orig = d->flush_cb;
    lv_display_set_flush_cb(d, stats_flush);
}

static int objs_in_areas(lv_obj_t *obj)
{
    int n = 0;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) { return 0; }
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    for (const lv_area_t &a : g_fl_areas) {
        if (a.x1 <= c.x2 && c.x1 <= a.x2 && a.y1 <= c.y2 && c.y1 <= a.y2) { n++; break; }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) { n += objs_in_areas(lv_obj_get_child(obj, i)); }
    return n;
}

struct Sum { int n = 0; long long px = 0, px_max = 0, calls = 0, us = 0, us_max = 0, objs = 0, finds = 0, cmps = 0, lin = 0; };
static Sum s_all, s_move;
static FILE *s_out;

static void sum_add(Sum *s, long px, int calls, int objs, long long us, const Settings::FindStats &f)
{
    s->n++; s->px += px; s->calls += calls; s->objs += objs; s->us += us;
    s->finds += f.calls; s->cmps += f.compares; s->lin += f.linear;
    if (px > s->px_max) { s->px_max = px; }
    if (us > s->us_max) { s->us_max = us; }
}

static void sum_line(const char *tag, const Sum &s)
{
    int n = s.n ? s.n : 1;
    fprintf(s_out, "%s keys=%d px_sum=%lld px_mean=%lld px_max=%lld calls_sum=%lld calls_mean=%.1f objs_mean=%lld us_sum=%lld us_mean=%lld us_max=%lld finds_mean=%.1f cmps_mean=%.1f lin_mean=%.1f\n",
            tag, s.n, s.px, s.px / n, s.px_max, s.calls, (double)s.calls / n, s.objs / n, s.us, s.us / n, s.us_max, (double)s.finds / n, (double)s.cmps / n, (double)s.lin / n);
}

// ---- the session -----------------------------------------------------------------------------------
static Menu g_menu;
static Rig *g_rig;
static bool g_open = true;                                    // the menu is up
static std::vector<int> g_keys;                               // every key given, played again after a reload

// One key as run.cc's give_key hands it to the core's menu: MenuCtl::key, then the render; R_CLOSE closes the menu. F12 opens a closed menu
// (the board opens it with the menu button or the core's menu hotkey, neither of which reaches the simulator as a key).
static void give_key(int k, bool log)
{
    if (!g_open) {
        if (k == KEY_F12) {
            g_open = true;
            g_rig->m->open();
            ui_menu_visible(true);
            ui_menu_render(*g_rig->m);
            if (log) { printf("key F12 -> menu open\n"); }
        }
        return;
    }
    long long t0 = now_us();
    MenuCtl::Result r = g_rig->m->key(k);
    if (r == MenuCtl::R_CLOSE && !g_rig->c.desc->menu_first) {
        g_open = false;
        ui_menu_visible(false);
        if (log) { printf("key %s -> menu closed (F12 opens it)\n", sim_key_name(k)); }
        return;
    }
    ui_menu_render(*g_rig->m);
    long long t1 = now_us();
    lv_timer_handler();                                       // LVGL runs between two keys, as on the board (and in the bench's steps)
    g_us_ui += t1 - t0;
    g_us_lv += now_us() - t1;
    if (log) {
        printf("key %s -> area %d tab %d \"%s\" row %d\n", sim_key_name(k), g_rig->m->area(), g_rig->m->tab(), g_rig->m->title(), g_rig->m->selected());
    }
}

// The first row of the current view whose label starts with `text`, -1 when there is none.
static int find_row(const char *text)
{
    for (int i = 0; i < g_rig->m->rows(); i++) {
        char l[160], v[160];
        bool active = false;
        g_rig->m->row(i, l, sizeof(l), v, sizeof(v), &active);
        if (!strncmp(l, text, strlen(text))) { return i; }
    }
    return -1;
}

// --shots DIR: the picture as the display holds it after a key press has settled - the frame buffer the flush callbacks left, NOT drawn again, so a
// pixel the menu forgot to repaint shows. One PNG per key press (NNN-KEY.png), written by main's loop after the press.
static void place(int *x, int *y, int *w, int *h);
static void shot_now(const char *dir, int n, const char *key)
{
    int x, y, w, h;
    place(&x, &y, &w, &h);
    unsigned char *rgb = (unsigned char *)malloc((size_t)frame_w * frame_h * 3);
    rig_compose(rgb, frame_w, frame_h, x, y, w, h);
    char path[1024], k[40];
    snprintf(k, sizeof(k), "%s", key);
    for (char *c = k; *c; c++) { if (*c == ':' || *c == '/') { *c = '_'; } }
    snprintf(path, sizeof(path), "%s/%03d-%s.png", dir, n, k);
    png_rgb(path, rgb, frame_w, frame_h);
    free(rgb);
}

// One key press of the headless run, as the --keys loop gives it: the key, the application's ticks, the LVGL pass. With --stats the UI is then let
// settle (LVGL passes until one flushes nothing) and the line of figures is written.
static int g_press_n;
static void press(int code, bool log, const char *name)
{
    if (!s_out && !o.shots) {
        give_key(code, log);
        for (int i = 0; i < 4 && g_rig->m->tick(); i++) { ui_menu_render(*g_rig->m); }
        lv_timer_handler();
        return;
    }
    g_fl_px = 0; g_fl_calls = 0; g_fl_areas.clear(); g_us_ui = g_us_lv = 0;
    int ls0 = g_rig->fs.lists, wr0 = g_rig->fs.writes;
    const Settings::FindStats fs0 = Settings::find_stats();
    uint32_t ch0 = ui_cheap_moves(), ip0 = ui_inplace_moves();
    long long t0 = now_us();
    give_key(code, log);
    for (int i = 0; i < 4 && g_rig->m->tick(); i++) { ui_menu_render(*g_rig->m); }
    for (int i = 0, quiet = 0; i < 20 && quiet < 2; i++) {
        int c0 = g_fl_calls;
        long long t = now_us();
        lv_timer_handler();
        g_us_lv += now_us() - t;
        quiet = g_fl_calls == c0 ? quiet + 1 : 0;
    }
    long long us = now_us() - t0;
    int objs = objs_in_areas(lv_screen_active()) + objs_in_areas(lv_display_get_layer_top(lv_display_get_default())) +
               objs_in_areas(lv_display_get_layer_sys(lv_display_get_default()));
    const char *path = !g_open ? "closed" : ui_cheap_moves() != ch0 ? "cheap" : ui_inplace_moves() != ip0 ? "inplace" : g_fl_px ? "build" : "none";
    ++g_press_n;
    const Settings::FindStats fs1 = Settings::find_stats();
    const Settings::FindStats fd = { fs1.calls - fs0.calls, fs1.compares - fs0.compares, fs1.linear - fs0.linear };     // Settings::find in this key: calls, key comparisons, what the linear scan would have compared
    if (s_out) {
        fprintf(s_out, "KEY n=%d key=%s path=%s px=%ld calls=%d objs=%d us=%lld ui_us=%lld lv_us=%lld fs_lists=%d fs_writes=%d finds=%u cmps=%u lin=%u\n", g_press_n, name, path,
                g_fl_px, g_fl_calls, objs, us, g_us_ui, g_us_lv, g_rig->fs.lists - ls0, g_rig->fs.writes - wr0, fd.calls, fd.compares, fd.linear);
    }
    if (o.shots) { shot_now(o.shots, g_press_n, name); }
    if (s_out) {
        sum_add(&s_all, g_fl_px, g_fl_calls, objs, us, fd);
        if (code == KEY_UP || code == KEY_DOWN) { sum_add(&s_move, g_fl_px, g_fl_calls, objs, us, fd); }
    }
}

// A new rig on the current menu, placed for the mode as run.cc places it (ui_open, then video_place_menu), and the keys of the session played again.
static void start_rig(bool replay)
{
    char cfg[160];
    snprintf(cfg, sizeof(cfg), "/usb0/cores/menusim_%s.cfg", g_src->folder ? g_src->folder : "core");
    g_rig = new Rig(g_menu.desc, cfg, g_menu.mm);                // the old rig stays allocated: the view may still point into it until the render below
    g_rig->c.started = true;
    g_rig->c.program_loaded = true;
    fill_stick(g_rig);
    video_place_menu(&g_rig->px, mode.hdmi, mode.sys);
    g_open = true;
    ui_menu_visible(true);
    ui_menu_render(*g_rig->m);
    if (replay) {
        for (int k : g_keys) { give_key(k, false); }
    }
    lv_obj_invalidate(lv_screen_active());
    sim_dirty = true;
}

static void place(int *x, int *y, int *w, int *h)
{
    PxRec::Pl p = g_rig->px.place.empty() ? PxRec::Pl{ 0, 0, cvw, cvh } : g_rig->px.place.back();
    *x = p.x; *y = p.y; *w = p.w; *h = p.h;
}

// The picture as the bench's mock_shot() takes it: the menu rendered, LVGL run, then the whole page drawn again.
static void shot(const char *path)
{
    if (g_open) { ui_menu_render(*g_rig->m); }
    lv_timer_handler();
    lv_obj_invalidate(lv_screen_active());
    lv_timer_handler();
    int x, y, w, h;
    place(&x, &y, &w, &h);
    png_out(path, frame_w, frame_h, x, y, w, h);
}

// ---- T256: the overlay service in the simulator ------------------------------------------------------------------------------------
// The still of a stopped C64-type machine is made up here (the VIC colours in bands, as a picture to see the menu and the elements over); the menu key calls do nothing.
struct SimOverlayHost : OverlayHost {
    void *alloc(uint32_t n) { return malloc(n); }
    void release(void *p) { free(p); }
    bool grab_still(uint16_t *dst, int w, int h, int fw, int fh, int ox, int oy, int stride = 0, OvPalette lib = 0)
    {
        (void)lib;
        if (stride < w) {
            stride = w;
        }
        static const uint16_t pal[8] = { 0x2933, 0x5569, 0x7679, 0xEF8E, 0x8A85, 0xC36E, 0x737D, 0x4A49 };
        if (fw < 1 || fh < 1) {
            fw = w;
            fh = h;
            ox = oy = 0;
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int px = (ox + x) * 960 / fw, py = (oy + y) * 540 / fh;             // the same checks whatever the size the picture is fitted to
                dst[y * stride + x] = ((px / 40 + py / 40) & 1) ? pal[(py / 68) & 7] : pal[((py / 68) + 3) & 7];
            }
        }
        return true;
    }
    bool picture_rect(int, int, OvRect *r)
    {
        if (!o.picture) {
            return false;
        }
        *r = OvRect{ o.pic[0], o.pic[1], o.pic[2], o.pic[3] };
        return true;
    }
    void menu_hide() { }
    void menu_show() { }
    void canvas(int *w, int *h) { *w = ui_canvas_w(); *h = ui_canvas_h(); }
};

// ---- reload ----------------------------------------------------------------------------------------
static void reload(void)
{
    uint32_t t0 = SDL_GetTicks(), n = 0;
    std::string why;
    Menu m;
    if (!make_menu(g_src, &m, &why, &n)) {
        printf("reload refused: %s\n", why.c_str());
        g_rig->m->say(("menu.toml refused: " + why).substr(0, 150).c_str(), true);
        ui_menu_render(*g_rig->m);
        return;
    }
    g_menu = m;
    start_rig(true);
    printf("reload: %s -> record %u bytes, menu rebuilt in %u ms\n", o.toml, (unsigned)n, (unsigned)(SDL_GetTicks() - t0));
}

// ---- the window ------------------------------------------------------------------------------------
static long g_seen = -1;                                      // the menu TOML's change time the menu was built from
static uint32_t g_next_poll;

static void win_key(int k) { g_keys.push_back(k); give_key(k, true); }
static void win_reload(void) { reload(); g_seen = sim_mtime(o.toml); }
static void win_idle(void)
{
    if (o.toml && SDL_GetTicks() >= g_next_poll) {            // the menu TOML, polled every 250 ms
        g_next_poll = SDL_GetTicks() + 250;
        long now = sim_mtime(o.toml);
        if (now != g_seen && now >= 0) { g_seen = now; reload(); }
    }
    if (g_rig->m->tick()) { ui_menu_render(*g_rig->m); }
    lv_timer_handler();
}
static void win_compose(unsigned char *rgb)
{
    int x, y, w, h;
    place(&x, &y, &w, &h);
    rig_compose(rgb, frame_w, frame_h, x, y, w, h);
}

static int run_window(void)
{
    char title[160], name[160];
    snprintf(title, sizeof(title), "menusim - %s - %s %dx%d - F12 opens a closed menu", g_src->title ? g_src->title : o.core, mode.name, frame_w, frame_h);
    snprintf(name, sizeof(name), "menusim-%s-%s", o.core, mode.name);
    g_seen = o.toml ? sim_mtime(o.toml) : -1;
    g_next_poll = SDL_GetTicks() + 250;
    SimApp a = { title, frame_w, frame_h, win_key, win_idle, win_compose, shot, o.toml ? win_reload : 0, name };
    return sim_window(a, o.scale);
}

// ---- main ------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (a == "--mode") { o.mode = val(); }
        else if (a == "--keys") { o.keys = val(); }
        else if (a == "--shot") { o.shot = val(); }
        else if (a == "--stats") { o.stats = val(); }
        else if (a == "--shots") { o.shots = val(); }
        else if (a == "--toml") { o.toml = val(); }
        else if (a == "--desc") { o.desc = val(); }
        else if (a == "--ops") { o.ops = val(); }
        else if (a == "--stick") { o.stick = val(); }
        else if (a == "--say") { o.say = val(); }
        else if (a == "--paused") { o.paused = true; }
        else if (a == "--stopped") { o.stopped = true; }
        else if (a == "--elements") { o.elements = true; }
        else if (a == "--picture") { if (sscanf(val(), "%d,%d,%d,%d", &o.pic[0], &o.pic[1], &o.pic[2], &o.pic[3]) != 4) usage(); o.picture = true; }
        else if (a == "--message") { o.msg_title = val(); o.msg_text = val(); }
        else if (a == "--root") { o.root = val(); }
        else if (a == "--scale") { o.scale = atoi(val()); }
        else if (a == "--list") {
            for (int k = 0; core_table[k]; k++) { printf("%-12s %s\n", core_table[k]->core_id ? core_table[k]->core_id : core_table[k]->folder, core_table[k]->title); }
            return 0;
        }
        else if (a[0] == '-') { usage(); }
        else if (!o.core) { o.core = argv[i]; }
        else { usage(); }
    }
    if (!o.core && !o.desc) { usage(); }
    if (o.desc) {
        std::string why_desc;
        if (!core_from_desc(&why_desc)) { fprintf(stderr, "menusim: %s: %s\n", o.desc, why_desc.c_str()); return 1; }
    } else {
        g_src = find_core(o.core);
    }
    if (!g_src) {
        fprintf(stderr, "menusim: no core \"%s\"; the core table has:", o.core);
        for (int i = 0; core_table[i]; i++) { fprintf(stderr, " %s", core_table[i]->core_id ? core_table[i]->core_id : core_table[i]->folder); }
        fprintf(stderr, "\n");
        return 2;
    }
    bool found = false;
    for (auto &m : modes) { if (same(m.name, o.mode)) { mode = m; found = true; } }
    if (!found) { usage(); }
    video_active_size(mode.hdmi, mode.sys, &frame_w, &frame_h);
    // the menu TOML: --toml, else coreapp/cores/<core>/menu.toml of the tree; its ops list: --ops, else tests/coreapp/golden/ops_<core>.txt
    static std::string toml_s, ops_s;
    if (!o.toml && !o.desc) {
        toml_s = std::string(o.root) + "/coreapp/cores/" + o.core + "/menu.toml";
        if (exists(toml_s)) { o.toml = toml_s.c_str(); }
    }
    if (o.toml && !o.ops) {
        ops_s = std::string(o.root) + "/tests/coreapp/golden/ops_" + o.core + ".txt";
        if (exists(ops_s)) { o.ops = ops_s.c_str(); }
    }
    if (!o.keys && !o.shot && !o.stats && !o.shots) { rig_tick = sim_wall_tick; }             // the window runs on the wall clock; headless keeps the bench's steps
    rig_on_frame = sim_on_frame;

    std::string why;
    uint32_t n = 0;
    if (!make_menu(g_src, &g_menu, &why, &n)) {
        fprintf(stderr, "menusim: the menu of %s: %s\n", o.core, why.c_str());
        return 1;
    }
    printf("menusim: %s (%s), mode %s %dx%d, menu %s%s%s\n", o.core, g_src->title, mode.name, frame_w, frame_h,
           o.toml ? o.toml : (g_src->menu_rec ? "the built-in record" : "the C descriptor"), o.ops ? ", ops " : "", o.ops ? o.ops : "");
    ui_open(g_menu.open);
    start_rig(false);
    {
        int x, y, w, h;
        place(&x, &y, &w, &h);
        printf("canvas %dx%d at %d,%d shown %dx%d (frame %dx%d)\n", cvw, cvh, x, y, w, h, frame_w, frame_h);
    }
    if (o.stats || o.shots) {
        if (o.stats) {
            s_out = fopen(o.stats, "w");
            if (!s_out) { fprintf(stderr, "menusim: cannot write %s\n", o.stats); return 2; }
        }
        stats_hook();
        for (int i = 0, quiet = 0; i < 20 && quiet < 2; i++) {          // the first frame (start_rig invalidates the whole screen) is not the first key's
            int c0 = g_fl_calls;
            lv_timer_handler();
            quiet = g_fl_calls == c0 ? quiet + 1 : 0;
        }
    }
    if (o.keys || o.shot || o.stats || o.shots) {
        for (const std::string &k : sim_split_keys(o.keys)) {
            if (!k.compare(0, 5, "FIND:") || !k.compare(0, 4, "SEL:")) {   // the selection to a row by UP and DOWN: the first whose label starts with the text, or row n
                bool find = !k.compare(0, 5, "FIND:");
                int row = find ? find_row(k.c_str() + 5) : atoi(k.c_str() + 4);
                if (row < 0 || row >= g_rig->m->rows()) { fprintf(stderr, "menusim: no row \"%s\" here\n", k.c_str()); return 1; }
                for (int guard = 0; guard < 200 && g_rig->m->selected() != row; guard++) {
                    int code = g_rig->m->selected() > row ? KEY_UP : KEY_DOWN;
                    g_keys.push_back(code);
                    press(code, false, code == KEY_UP ? "UP" : "DOWN");
                }
                ui_menu_render(*g_rig->m);
                printf("%s -> row %d\n", k.c_str(), g_rig->m->selected());
                continue;
            }
            int code = sim_key_of(k.c_str());
            if (code < 0) { fprintf(stderr, "menusim: unknown key %s\n", k.c_str()); return 2; }
            g_keys.push_back(code);
            press(code, true, k.c_str());
        }
        if (s_out) {
            fprintf(s_out, "# core=%s mode=%s canvas=%dx%d\n", o.core, mode.name, cvw, cvh);
            sum_line("SUMMARY", s_all);
            sum_line("MOVES", s_move);
            fclose(s_out);
        }
        if (o.shots) {                                            // the same page drawn again whole: what the key presses left must equal it
            if (g_open) { ui_menu_render(*g_rig->m); }
            lv_timer_handler();
            lv_obj_invalidate(lv_screen_active());
            lv_timer_handler();
            shot_now(o.shots, 999, "redraw");
        }
        if (o.say) {                                              // the status line as an error of a start leaves it (MenuCtl::set_status, bad)
            g_rig->m->say(o.say, true);
            ui_menu_render(*g_rig->m);
        }
        if (o.msg_title) {                                        // the message pane of the application (ui_message), as run.cc raises it
            g_open = false;
            ui_message(o.msg_title, o.msg_text, true);
        }
        if (o.paused || o.stopped || o.elements) {                             // T256 D8: the elements as the merged UI TOML places them (the base's and the core's)
            static UiElements table;
            char why[160];
            ovl_merge(menu_elem_base(), g_menu.mm && g_menu.mm->has_elem ? &g_menu.mm->elem_store : 0, &table, why, sizeof(why));
            static SimOverlayHost host;
            ui_overlay_start(&host, &table, true);
            if (o.stopped) {                                      // the menu open on the stopped machine: the still under it, the menu blended over it in software (D10)
                ui_overlay_frozen(true);
                ui_show();
            } else {
                g_open = false;
                ui_menu_visible(false);
                ui_hide();
            }
            if (o.paused) {
                ui_paused_tag(true);                              // the machine stands: the still, and the tag, a base element shown through the service
            }
            if (o.elements) {                                     // every element's rectangle and name, on LVGL's top layer
                lv_obj_t *top = lv_display_get_layer_top(lv_display_get_default());
                for (int i = 0; i < table.n; i++) {
                    if (ovl_row_check(table.rows[i])) { continue; }
                    const OvRect r = ovl_place_on(table.rows[i], ui_canvas_w(), ui_canvas_h());          // on the canvas of this video mode
                    lv_obj_t *b = lv_obj_create(top);
                    lv_obj_remove_style_all(b);
                    lv_obj_set_pos(b, r.x, r.y);
                    lv_obj_set_size(b, r.w, r.h);
                    lv_obj_set_style_border_width(b, 2, 0);
                    lv_obj_set_style_border_color(b, lv_color_hex(table.rows[i].still ? 0x00FFFF : 0xFFFF00), 0);
                    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
                    lv_obj_t *l = lv_label_create(top);
                    lv_label_set_text_fmt(l, "%s %dx%d%s", table.rows[i].name, r.w, r.h, table.rows[i].still ? " (still)" : "");
                    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFF00), 0);
                    lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
                    lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
                    lv_obj_set_pos(l, table.rows[i].still ? r.x + 10 : r.x, table.rows[i].still ? r.y + 10 : r.y + r.h + 2);
                }
            }
        }
        if (o.shot) { shot(o.shot); }
        return 0;
    }
    return run_window();
}
