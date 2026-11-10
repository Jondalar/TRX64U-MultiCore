// Renders the core application's menu natively (the real ui.cc, MenuCtl and LVGL, the board stubbed) to PNG,
// upscaled 2x like the pixel layer's scale 2. The same picture a core shows on the board, without one.
//   tests/coreapp/render/run.sh <outdir>
#include "overlay_probe.h"
#include "hostos.h"
#include "appdlg.h"
#include <atomic>
#include <thread>
#include <chrono>
#include "lvgl.h"
#include "src/display/lv_display_private.h"      // inv_areas: the area a key asked LVGL to redraw
#include "trx_lv_port.h"
#include "view_bounds.h"
#include "trxcanvas.h"
#include "ui.h"
#include "menuctl.h"
#include "hotkeys.h"
#include "boot.h"
#include "settings.h"
#include "video.h"
#include "fakes.h"
#include "trxview.h"
#include "keyboard.h"
#include "trx64u_menu.h"
#include "portdiag.h"
#include "ops_glue.h"
#include "diagtest.h"
#include "diag_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <zlib.h>

extern const CoreDesc atari800_desc;
extern const CoreDesc nes_desc;
extern const CoreDesc c64_desc;
extern const CoreDesc c128_desc;
extern const CoreDesc amiga1200_desc;
extern const CoreDesc amiga_desc;
extern const CoreDesc ddt_mandel_desc;
extern const CoreDesc ddt_novic_desc;
#include "picture.h"
#include "volume.h"
#include "videomode.h"
const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &c64_desc, &c128_desc, 0 };

// ---- the pixel layer, native ----------------------------------------------------------------------
static uint16_t fbs[2][960 * 540];
static uint16_t *fb = fbs[0];
static int cvw = 960, cvh = 540;                               // T164 D10a: the canvas of the display (the port's trx_lv_set_canvas)
static uint32_t tick_cb(void) { static uint32_t t; return t += 50; }

bool trx_lv_open(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(cvw, cvh);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fbs[0], fbs[1], sizeof(fbs[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, [](lv_display_t *dd, const lv_area_t *, uint8_t *px) {
        if (lv_display_flush_is_last(dd)) { fb = (uint16_t *)px; }
        lv_display_flush_ready(dd);
    });
    ovp::install();
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
void trx_lv_suspend(void) { }
void trx_lv_resume(void) { }
void trx_lv_refresh_now(void) { lv_timer_handler(); }
void trx_lv_yield_layer(void) { }
void trx_lv_set_key(bool, uint16_t) { }
bool trx_lv_set_alpha(uint8_t) { return true; }

// ---- PNG -------------------------------------------------------------------------------------------
static void put_be(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *t, const unsigned char *d, unsigned n)
{
    unsigned char h[4];
    put_be(h, n);
    fwrite(h, 1, 4, f);
    uLong c = crc32(0, (const Bytef *)t, 4);
    fwrite(t, 1, 4, f);
    if (n) { fwrite(d, 1, n, f); c = crc32(c, d, n); }
    put_be(h, (unsigned)c);
    fwrite(h, 1, 4, f);
}

static void png(const char *path, int scale)
{
    int W = 960 * scale, H = 540 * scale;
    unsigned char *raw = (unsigned char *)malloc((size_t)(W * 3 + 1) * H), *p = raw;
    for (int y = 0; y < H; y++) {
        *p++ = 0;
        for (int x = 0; x < W; x++) {
            uint16_t v = fb[(y / scale) * 960 + x / scale];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            *p++ = (r << 3) | (r >> 2); *p++ = (g << 2) | (g >> 4); *p++ = (b << 3) | (b >> 2);
        }
    }
    uLongf cl = compressBound((W * 3 + 1) * H);
    unsigned char *z = (unsigned char *)malloc(cl);
    compress(z, &cl, raw, (W * 3 + 1) * H);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13];
    put_be(ih, W); put_be(ih + 4, H); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, cl); chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
    printf("%s\n", path);
}

// T107 5.9: what the monitor gets. The menu canvas (fb, 960 x 540) scaled by nearest sampling into the box the video service gave it (the layer does
// the same with PX_OSIZE), on a black frame of the mode's size.
static void png_out(const char *path, int W, int H, int bx, int by, int bw, int bh)
{
    unsigned char *raw = (unsigned char *)calloc((size_t)(W * 3 + 1) * H, 1);
    for (int y = 0; y < H; y++) {
        unsigned char *row = raw + (size_t)(W * 3 + 1) * y;
        row[0] = 0;
        if (y < by || y >= by + bh) { continue; }
        for (int x = bx; x < bx + bw && x < W; x++) {
            uint16_t v = fb[((y - by) * cvh / bh) * cvw + (x - bx) * cvw / bw];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            unsigned char *q = row + 1 + 3 * x;
            q[0] = (r << 3) | (r >> 2); q[1] = (g << 2) | (g >> 4); q[2] = (b << 3) | (b >> 2);
        }
    }
    uLongf cl = compressBound((W * 3 + 1) * H);
    unsigned char *z = (unsigned char *)malloc(cl);
    compress(z, &cl, raw, (W * 3 + 1) * H);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13];
    put_be(ih, W); put_be(ih + 4, H); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, cl); chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
    printf("%s\n", path);
}

// ---- checks ----------------------------------------------------------------------------------------
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static int ovfails;                                              // T167: the failures of the overlay checks
#define OVCHECK(c) do { int f0_ = fails; CHECK(c); ovfails += fails - f0_; } while (0)

// The area LVGL was asked to redraw since the last handler pass: the number that says whether a key
// touched two rows or the whole pane (the host bench uses the same one).
static uint32_t inv_pixels(void)
{
    lv_display_t *d = lv_display_get_default();
    uint32_t n = 0;
    if (d) {
        for (int i = 0; i < d->inv_p; i++) {
            if (!d->inv_area_joined[i]) {
                n += (uint32_t)lv_area_get_width(&d->inv_areas[i]) * lv_area_get_height(&d->inv_areas[i]);
            }
        }
    }
    return n;
}

// ---- the rig ---------------------------------------------------------------------------------------
struct Host : MenuHost {
    int returns;
    Host() : returns(0), media_gen(0) { }
    int media_gen;                                                   // the bench "mounts a stick": the media chip's text changes
    void info(int which, char *out, int n) { if (which == 3) snprintf(out, n, "USB %d", media_gen); else snprintf(out, n, "line %d", which); }
    bool return_to_host(char *msg, int n) { returns++; snprintf(msg, n, "this host has no ICAP"); return false; }
};

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    Rig(const CoreDesc *d, const char *cfg, const MenuMerge *mm = 0)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.merge = mm;
        px.on_canvas = ui_set_canvas;                   // the video service's canvas reaches the real view, as on the board
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        st->load();
        m = new MenuCtl(&c, &host);
    }
};


// T104 areas (Alex, 2026-10-22): every key of the core application redraws only what changes. LVGL draws about 2.2 ms per 1,000 px on the
// board (direct into DDR2, no data cache), so the invalidated pixels are the time. One line per case:
//   AREA <case> <px> <limit>     (tests/coreapp/render/run.sh prints them; the before/after table of T104 5.11 is made from them)
static int g_area_n;
static uint32_t inv_areas_n(void)
{
    lv_display_t *d = lv_display_get_default();
    return d ? (uint32_t)d->inv_p : 0;
}
static uint32_t g_last_areas;
static uint16_t kept_fb[960 * 540];
// After a case: what the layer shows must equal the same state built from scratch (the screen cleaned, every row and the frame built again)
static void same_as_scratch(const char *name, Rig &c)
{
    memcpy(kept_fb, fb, sizeof(kept_fb));
    ui_menu_visible(false);
    ui_menu_render(*c.m);
    lv_timer_handler();
    size_t diff = 0;
    int x0 = 960, y0 = 540, x1 = -1, y1 = -1;
    for (size_t k = 0; k < 960 * 540; k++) {
        if (kept_fb[k] != fb[k]) {
            diff++;
            int x = (int)(k % 960), y = (int)(k / 960);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    if (diff && getenv("RENDER_DIFF_DIR")) {
        char pp[300];
        static int dn; dn++;
        snprintf(pp, sizeof(pp), "%s/diff%d_scratch.png", getenv("RENDER_DIFF_DIR"), dn); png(pp, 1);
        memcpy(fb, kept_fb, sizeof(kept_fb));
        snprintf(pp, sizeof(pp), "%s/diff%d_kept.png", getenv("RENDER_DIFF_DIR"), dn); png(pp, 1);
    }
    if (diff) printf("AREA %s: the shown buffer differs from one built from scratch in %zu px, in x %d..%d y %d..%d\n", name, diff, x0, x1, y0, y1);
    CHECK(diff == 0);
}
// The Home page's "Current session" panel has no picture area (Alex 2026-10-29: the picture-in-picture, T24.1, is not built): no large empty box with the screen's
// background and a hairline border anywhere in it. UI_HOME_PICTURE_BOX puts the box back (the bench's control).
static int big_empty_boxes(lv_obj_t *o)
{
    const TrxTheme &t = trx_theme();
    int n = 0;
    if (lv_obj_get_width(o) >= 300 && lv_obj_get_height(o) >= 100 && lv_obj_get_style_border_width(o, LV_PART_MAIN) == 1 &&
        lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), lv_color_hex(t.bg)) && lv_obj_get_style_bg_opa(o, LV_PART_MAIN) > 0 && lv_obj_get_child_count(o) == 0) {
        n++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        n += big_empty_boxes(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

static void area_case(const char *name, uint32_t px, uint32_t limit, Rig *c = 0)
{
    g_area_n++;
    printf("AREA %-34s %7u px  <= %7u  (~%.0f ms, %u areas)%s\n", name, px, limit, px * 2.2 / 1000.0, (unsigned)g_last_areas, px <= limit ? "" : "  FAIL");
    CHECK(px <= limit);
    if (c) same_as_scratch(name, *c);
}
// One key as run.cc handles it for the menu: the key, one render; the area LVGL holds before the draw is the cost.
static uint32_t g_key_frames, g_key_partials;         // frames built and frame parts redrawn by the last key_px()
static uint32_t key_px(Rig &c, int k)
{
    uint32_t f0 = trx_shell_frames(), p0 = trx_shell_partials();
    c.m->key(k);
    ui_menu_render(*c.m);
    g_key_frames = trx_shell_frames() - f0;
    g_key_partials = trx_shell_partials() - p0;
    uint32_t px = inv_pixels();
    g_last_areas = inv_areas_n();
    lv_timer_handler();
    return px;
}

// The row of the browser that is the folder `name` (a folder row may carry a slash or brackets), -1 when there is none.
static int find_dir_row(Rig &c, const char *name)
{
    size_t n = strlen(name);
    for (int i = 0; i < c.m->rows(); i++) {
        char l[120], r[120]; bool a;
        c.m->row(i, l, sizeof(l), r, sizeof(r), &a);
        const char *q = l[0] == '[' || l[0] == '>' ? l + 1 : l;
        if (!strncmp(q, name, n) && (q[n] == 0 || q[n] == '/' || q[n] == ']')) return i;
    }
    return -1;
}
static void select_row(Rig &c, int row)
{
    for (int guard = 0; guard < 200 && c.m->selected() != row; guard++) c.m->key(c.m->selected() > row ? KEY_UP : KEY_DOWN);
    ui_menu_render(*c.m); lv_timer_handler();
}

static void areas_core(void)
{
    trx_skin_set(TRX_SKIN_PANEL);
    Rig c(&atari800_desc, "/usb0/cores/atari800.cfg");
    c.fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
    c.fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
    for (int i = 0; i < 60; i++) {
        char pn[64];
        snprintf(pn, sizeof(pn), "/usb0/zgame%02d.atr", i);        // the Library opens on a medium: the files are on the stick
        c.fs.put_text(pn, "x");
    }
    c.fs.put_text("/usb0/sub/inner.atr", "x");
    for (int i = 0; i < 40; i++) {
        char pn[64];
        snprintf(pn, sizeof(pn), "/usb0/sub2/f%02d.atr", i);
        c.fs.put_text(pn, "x");
    }
    c.c.started = true;
    c.c.program_loaded = true;
    ui_open(&atari800_desc);

    const TrxTheme &t = trx_theme();
    const uint32_t SCREEN = 960 * 540;
    const uint32_t PANE = (960 - 148) * (540 - 36 - 28 - 38);        // the content pane under a tab strip
    const uint32_t RW = (960 - 148) - 2 * t.space_lg;                // a row's width
    // The selection of a row is a 3 px bar and a hairline around it; a selection move redraws the old and the new one and the value's colour
    const uint32_t MARK1 = 2 * RW + 4 * 20 + 2000, MARK2 = 2 * RW + 4 * 34 + 2000;
    // ONE frame buffer: the sync copy of two buffers in DIRECT mode costs as much again as the render, on the board
    {
        lv_display_t *d = lv_display_get_default();
        printf("AREA frame buffers: %d\n", d && d->buf_2 ? 2 : 1);
        CHECK(d && d->buf_1 && !d->buf_2);
    }
    // menu open as run.cc does it: the layer resumes (the screen is invalidated), the message is cleared, the menu opens and renders once
    uint32_t fr0 = trx_shell_frames();
    lv_obj_invalidate(lv_screen_active());
    ui_message_clear();
    ui_menu_visible(true);
    c.m->open();
    ui_menu_render(*c.m);
    uint32_t px = inv_pixels();
    g_last_areas = inv_areas_n();
    uint32_t open_frames = trx_shell_frames() - fr0;
    lv_timer_handler();
    printf("AREA menu open built %u frame(s)\n", (unsigned)open_frames);
    CHECK(open_frames == 1);                                         // one frame built, one pass
    area_case("menu open (one full screen)", px, SCREEN, &c);
    CHECK(big_empty_boxes(lv_screen_active()) == 0);                // Home: no empty picture box in the session panel
    // Home: the status rows; a selection move
    area_case("Home: selection move", key_px(c, KEY_DOWN), 2 * (2 * 250 + 4 * 30 + 2000), &c);
    // an area key: the frame stands except the rail's two items and the tab strip; the pane is the new content
    uint32_t px_f3 = key_px(c, KEY_F3);
    CHECK(g_key_frames == 0 && g_key_partials == 1);                 // not a frame again: the rail's two items and the strip
    area_case("F3 library (first key of an area)", px_f3, PANE + 40000, &c);
    c.m->relist();
    ui_menu_render(*c.m); lv_timer_handler();
    area_case("Library: selection move", key_px(c, KEY_DOWN), 2 * MARK1, &c);
    for (int i = 0; i < 25; i++) key_px(c, KEY_DOWN);
    area_case("Library: scroll one row", key_px(c, KEY_DOWN), 60000, &c);
    // the top bar's media chip changes when a stick mounts (relist): the bar alone, not the screen
    {
        c.host.media_gen++;
        uint32_t f0 = trx_shell_frames();
        ui_menu_render(*c.m);
        CHECK(trx_shell_frames() == f0);                            // the top bar only
        uint32_t pxm = inv_pixels();
        g_last_areas = inv_areas_n();
        lv_timer_handler();
        area_case("relist (media chip, top bar)", pxm, 960 * 36 + PANE / 2, &c);
    }
    area_case("F5 machine", key_px(c, KEY_F5), PANE + 45000, &c);              // 396,219 px measured 2026-10-29 (the budget was PANE + 40000 before the button keycaps and the new settings rows)
    area_case("tab 2 settings", key_px(c, '2'), PANE + 15000, &c);
    area_case("Settings: selection move", key_px(c, KEY_DOWN), 2 * MARK2, &c);
    area_case("Settings: value change (first of a session)", key_px(c, KEY_RIGHT), 30000, &c);
    area_case("Settings: value change again", key_px(c, KEY_LEFT), 30000, &c);
    area_case("tab 1 machine", key_px(c, '1'), PANE + 15000, &c);
    area_case("F7 actions", key_px(c, KEY_F7), PANE + 45000, &c);              // 397,576 px measured 2026-10-29 (see F5)
    area_case("F8 system", key_px(c, KEY_F8), PANE + 40000, &c);
    area_case("F1 home", key_px(c, KEY_F1), PANE + 40000, &c);
    // the browser: into a folder and back
    c.m->key(KEY_F3);
    ui_menu_render(*c.m); lv_timer_handler();
    c.m->relist();
    ui_menu_render(*c.m); lv_timer_handler();
    {
        int usb = find_dir_row(c, "usb0");                          // the media level: the stick is a row
        CHECK(usb >= 0);
        select_row(c, usb);
        c.m->key(KEY_RETURN);
        ui_menu_render(*c.m); lv_timer_handler();
        c.m->relist();
        ui_menu_render(*c.m); lv_timer_handler();
    }
    {
        int dir = find_dir_row(c, "sub");
        printf("AREA library: folder row %d of %d\n", dir, c.m->rows());
        CHECK(dir >= 0);
        select_row(c, dir);
    }
    area_case("Library: RETURN on a folder", key_px(c, KEY_RETURN), PANE, &c);
    printf("AREA library title after RETURN: %s (%d rows)\n", c.m->title(), c.m->rows());
    area_case("Library: LEFT (parent folder)", key_px(c, KEY_LEFT), PANE, &c);
    // a folder with as many rows as the window holds: the same pane, the rows are written again (title and labels), not built
    {
        int dir = find_dir_row(c, "sub2");
        CHECK(dir >= 0);
        select_row(c, dir);
        area_case("Library: RETURN on a full folder", key_px(c, KEY_RETURN), 120000, &c);
        area_case("Library: LEFT from a full folder", key_px(c, KEY_LEFT), 120000, &c);
    }
}

// T107 5.9, the A1200 (the core with picture modes of its own): the Settings page as the monitor gets it in 1080p, in 720p, and after the mode was
// changed from the menu itself (720p -> 1080p with the menu up). The video service hands the layer's place to the port; the bench puts the canvas there.
static void a1200_menu(const char *out)
{
    char p[512];
    static CoreDesc a12; static Setting a12_items[SETTINGS_MAX];       // the descriptor with the application's own items (Picture, Scale, Smoothing), as the board runs it
    PictureEnv env = { EDID_ROW_MASK, true, VID_SYS_PAL50 };           // T164: a host whose gateware answers, a screen that names every row
    CHECK(!picture_extend(&amiga1200_desc, &a12, a12_items, &env));
    Rig r(&a12, "/usb0/cores/amiga1200.cfg");
    r.c.started = true;
    r.c.program_loaded = true;
    ui_open(&a12);
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    r.vid->init();
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('2');
    ui_menu_render(*r.m);
    lv_timer_handler();
    struct { const char *name; int value; int fw, fh; } steps[] = {
        { "1080", 1 + VID_HDMI_1080P, 1920, 1080 },
        { "720", 1 + VID_HDMI_720P, 1280, 720 },
        { "after-switch-1080", 1 + VID_HDMI_1080P, 1920, 1080 },
        { "800x600", 1 + VID_HDMI_800x600, 800, 600 },
    };
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (i > 0) {
            picture_apply(&r.c, steps[i].value);               // from the menu's Picture item: the layer is the menu's, the picture is off
        }
        CHECK(!r.px.place.empty());
        PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 0, 0, 960, 540 } : r.px.place.back();
        TrxCanvas want = trx_canvas_for(steps[i].fw, steps[i].fh);        // D10a: 960 x 540 at a whole factor, or the mode's own size below 960 wide
        CHECK(g.w == want.w * want.factor && g.h == want.h * want.factor);   // a whole factor of the canvas, nothing stretched
        CHECK(trx_canvas_w() == want.w && trx_canvas_h() == want.h);
        CHECK(g.x == (steps[i].fw > g.w ? (steps[i].fw - g.w) / 2 : 0));       // centred horizontally ...
        CHECK(g.y == (steps[i].fh > g.h ? (steps[i].fh - g.h) / 2 : 0));       // ... and vertically
        ui_menu_render(*r.m);
        lv_timer_handler();
        CHECK(view_bounds(steps[i].name, want.w, want.h) == 0);             // nothing of the page lies outside the canvas
        snprintf(p, sizeof(p), "%s/a1200-settings-%s.png", out, steps[i].name);
        png_out(p, steps[i].fw, steps[i].fh, g.x, g.y, g.w, g.h);
    }
    // T190: the Amiga's Mouse sensitivity / Mouse acceleration rows (group "input") on the Settings page, each selected in turn at 1080p: the selected row
    // is inside the view (R1) and nothing lies outside the canvas.
    {
        Rig m(&a12, "/usb0/cores/amiga1200.cfg");
        m.c.started = true;
        m.c.program_loaded = true;
        ui_open(&a12);
        picture_start(&m.c, VID_SYS_PAL50, VID_HDMI_1080P);
        m.vid->init();
        m.m->open();
        m.m->key(KEY_F5);
        m.m->key('2');
        int found = 0;
        for (int i = 0; i < m.m->rows(); i++) {
            char l[120], v[120]; bool ap;
            m.m->row(i, l, sizeof(l), v, sizeof(v), &ap);
            if (strncmp(l, "Mouse ", 6)) continue;
            found++;
            select_row(m, i);
            CHECK(m.m->selected() == i);
            TrxCanvas want = trx_canvas_for(1920, 1080);
            CHECK(view_bounds("a1200 mouse row", want.w, want.h) == 0);
            PxRec::Pl g = m.px.place.empty() ? PxRec::Pl{ 0, 0, 960, 540 } : m.px.place.back();
            snprintf(p, sizeof(p), "%s/a1200-settings-mouse%d.png", out, found);
            png_out(p, 1920, 1080, g.x, g.y, g.w, g.h);
        }
        CHECK(found == 2);
    }
    // T164 WP16: the Picture page of a core that is not the A1200, on a screen that names one PC mode (a monitor): the list is Auto and 1024 x 768,
    // Scale and Smoothing are there for every frame-buffer core. The page as the monitor gets it in 1024 x 768.
    {
        static CoreDesc nes_ext; static Setting nes_items[SETTINGS_MAX];
        PictureEnv pc = { (uint16_t)(1u << 10), true, VID_SYS_PAL50 };
        CHECK(!picture_extend(&nes_desc, &nes_ext, nes_items, &pc));
        int pi = picture_index(&nes_ext), si = scale_index(&nes_ext), mi = smooth_index(&nes_ext);
        CHECK(pi >= 0 && si >= 0 && mi >= 0);
        CHECK(nes_ext.settings[pi].nvalues == 2 && !strcmp(nes_ext.settings[pi].values[1], "1024 x 768"));
        Rig n(&nes_ext, "/usb0/cores/nes.cfg");
        n.c.started = true;
        n.c.program_loaded = true;
        ui_open(&nes_ext);
        picture_start(&n.c, VID_SYS_PAL50, VID_HDMI_1080P);
        n.vid->init();
        n.st->set(pi, 1);
        n.st->set(si, VID_SCALE_FIT);
        n.st->set(mi, 1);
        n.m->open();
        n.m->key(KEY_F5);
        n.m->key('2');
        ui_menu_render(*n.m);
        lv_timer_handler();
        bool seen_pic = false, seen_scale = false, seen_smooth = false;
        for (int i = 0; i < n.m->rows(); i++) {
            char l2[120], rt2[120];
            bool a2;
            n.m->row(i, l2, sizeof(l2), rt2, sizeof(rt2), &a2);
            if (!strcmp(l2, "Picture")) { seen_pic = strstr(rt2, "1024 x 768") != 0; }
            if (!strcmp(l2, "Scale")) { seen_scale = strstr(rt2, "Fit") != 0; }
            if (!strcmp(l2, "Smoothing")) { seen_smooth = strstr(rt2, "On") != 0; }
        }
        CHECK(seen_pic && seen_scale && seen_smooth);
        CHECK(!n.px.place.empty());
        PxRec::Pl g = n.px.place.empty() ? PxRec::Pl{ 32, 114, 960, 540 } : n.px.place.back();
        snprintf(p, sizeof(p), "%s/nes-picture-page-pc.png", out);
        png_out(p, 1024, 768, g.x, g.y, g.w, g.h);
    }
    // the value sits on the middle of the label's line, in a one-line and in a two-line row
    for (int two = 0; two < 2; two++) {
        TrxRow row;
        memset(&row, 0, sizeof(row));
        row.label = "Chip RAM"; row.value = "2M"; row.sub = two ? "at the next reset" : 0;
        lv_obj_t *o = trx_row_at(lv_screen_active(), 0, 0, 700, row, 0);
        lv_obj_update_layout(o);
        lv_obj_t *lab = lv_obj_get_child(o, 4), *val = lv_obj_get_child(o, lv_obj_get_child_count(o) - 1);
        int dl = lv_obj_get_y(lab) + lv_obj_get_height(lab) / 2, dv = lv_obj_get_y(val) + lv_obj_get_height(val) / 2;
        printf("row two=%d: label middle %d, value middle %d\n", two, dl, dv);
        CHECK(dl - dv >= -1 && dl - dv <= 1);
        lv_obj_delete(o);
    }
    lv_timer_handler();                                          // the redraw of what the row left is not the next case's
    // T187 D9: a reset-time item is one line with the ring beside its label (no second line "at the next reset", no word in the label); the list explains the ring once
    char l[120], rt[120], sb[120];
    bool a;
    int found = 0;
    for (int i = 0; i < r.m->rows(); i++) {
        sb[0] = 0;
        r.m->row(i, l, sizeof(l), rt, sizeof(rt), &a, sb, sizeof(sb));
        CHECK(!strstr(sb, "next reset"));
        if (r.m->row_reset(i)) {
            found++;
            CHECK(!strstr(l, "reset") && !strstr(l, "reload") && sb[0] == 0);
        }
    }
    CHECK(found > 0);
    CHECK(strstr(r.m->legend_line(), "next reset") != 0);
    {   // the ring is one more child of the row: a row that has it is changed in place only to a row that has it, and its position follows the label's width
        TrxRow row;
        memset(&row, 0, sizeof(row));
        row.label = "Chip RAM"; row.value = "2M"; row.mark = true;
        lv_obj_t *o = trx_row_at(lv_screen_active(), 0, 0, 700, row, 0);
        lv_obj_update_layout(o);
        CHECK(lv_obj_get_child_count(o) == 4 + 1 + 1 + 1);
        row.label = "Chip RAM and a longer name"; 
        CHECK(trx_row_retext(o, row, 700));
        lv_obj_update_layout(o);
        lv_obj_t *lab = lv_obj_get_child(o, 4), *mk = lv_obj_get_child(o, 5);
        CHECK(lv_obj_get_x(mk) >= lv_obj_get_x(lab) + trx_text_w(row.label, trx_theme().body));
        row.mark = false;
        CHECK(!trx_row_retext(o, row, 700));                      // the structure differs: the caller builds the row again
        lv_obj_delete(o);
        lv_timer_handler();
    }
}

// T191: the Amiga 500 and 1200 Settings page with the row "Real-time clock (at next reset)" selected, 1080p; the picture goes to the output directory.
static void rtc_menu(const char *out)
{
    static CoreDesc d[2]; static Setting items[2][SETTINGS_MAX];
    const CoreDesc *src[2] = { &amiga_desc, &amiga1200_desc };
    const char *name[2] = { "a500", "a1200" };
    for (int k = 0; k < 2; k++) {
        PictureEnv env = { EDID_ROW_MASK, true, VID_SYS_PAL50 };
        CHECK(!picture_extend(src[k], &d[k], items[k], &env));
        char cfg[64]; snprintf(cfg, sizeof(cfg), "/usb0/cores/%s.cfg", k ? "amiga1200" : "amiga");
        Rig r(&d[k], cfg);
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(&d[k]);
        picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
        r.vid->init();
        r.m->open();
        r.m->key(KEY_F5);
        r.m->key('2');
        ui_menu_render(*r.m);
        lv_timer_handler();
        int row = -1;
        for (int i = 0; i < r.m->rows(); i++) {
            char l[120], rr[120], sb[160]; bool a;
            r.m->row(i, l, sizeof(l), rr, sizeof(rr), &a, sb, sizeof(sb));
            if (!strncmp(l, "Real-time clock", 15)) { row = i; break; }
        }
        CHECK(row >= 0);
        if (row >= 0) select_row(r, row);
        ui_menu_render(*r.m);
        lv_timer_handler();
        lv_obj_invalidate(lv_screen_active());                 // the whole page into the picture, not only what the last call changed
        lv_timer_handler();
        PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 0, 0, 960, 540 } : r.px.place.back();
        CHECK(view_bounds(name[k], 960, 540) == 0);
        char p[512]; snprintf(p, sizeof(p), "%s/%s-settings-rtc.png", out, name[k]);
        png_out(p, 1920, 1080, g.x, g.y, g.w, g.h);
    }
}

// T164 D10a: the menu in every mode the host offers. Each mode gets the canvas trx_canvas_for names (960 x 540 at a whole factor; the mode's own size
// below 960 wide: 576p PAL 720 x 576, 480p NTSC 640 x 480, 800 x 600) and the compact layout; every page of the menu (areas F1-F8, tabs 1-5) is rendered
// and every visible object must lie inside the canvas and not be cut by its parent. The pictures go to the output directory.
static void modes_menu(const char *out)
{
    char p[512];
    static CoreDesc a12; static Setting a12_items[SETTINGS_MAX];
    PictureEnv env = { EDID_ROW_MASK, true, VID_SYS_PAL50 };
    CHECK(!picture_extend(&amiga1200_desc, &a12, a12_items, &env));
    struct { const char *name; int sys, hdmi, fw, fh; } modes[] = {
        { "1080p", VID_SYS_PAL50, VID_HDMI_1080P, 1920, 1080 },
        { "720p", VID_SYS_PAL50, VID_HDMI_720P, 1280, 720 },
        { "800x600", VID_SYS_PAL50, VID_HDMI_800x600, 800, 600 },
        { "576p", VID_SYS_PAL50, VID_HDMI_480_576, 720, 576 },
        { "480p", VID_SYS_NTSC60, VID_HDMI_480_576, 640, 480 },
    };
    static const struct { int key; const char *name; } pics[] = { { KEY_F1, "home" }, { KEY_F3, "library" }, { KEY_F5, "settings" }, { KEY_F7, "actions" } };
    for (auto &md : modes) {
        Rig r(&a12, "/usb0/cores/amiga1200.cfg");
        r.fs.put_text("/usb0/games/arkanoid.adf", "x");
        r.fs.put_text("/usb0/games/a very long name of a disk image that does not fit a narrow column of the library.adf", "x");
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(&a12);
        picture_start(&r.c, md.sys, md.hdmi);
        r.vid->init();
        TrxCanvas want = trx_canvas_for(md.fw, md.fh);
        CHECK(trx_canvas_w() == want.w && trx_canvas_h() == want.h);
        r.m->open();
        int bad = 0, pages = 0;
        for (int area = 0; area < 8; area++) {
            int fk = KEY_F1 + area;
            for (int tab = 0; tab < 5; tab++) {
                r.m->key(fk);
                r.m->key('1' + tab);
                ui_menu_render(*r.m);
                lv_timer_handler();
                char what[64];
                snprintf(what, sizeof(what), "%s F%d tab %d", md.name, area + 1, tab + 1);
                bad += view_bounds(what, want.w, want.h) != 0;
                pages++;
                if (tab == 0 || (area == 4 && tab == 1)) {
                    for (auto &pc : pics) {
                        if (pc.key == fk && (tab == 0 || pc.key == KEY_F5)) {
                            PxRec::Pl g = r.px.place.back();
                            snprintf(p, sizeof(p), "%s/modes-%s-%s%s.png", out, md.name, pc.name, tab ? "2" : "");
                            png_out(p, md.fw, md.fh, g.x, g.y, g.w, g.h);
                        }
                    }
                }
            }
        }
        // T164 Q7 (2026-11-04): the picture question: the key is the last line of the box, one blank line below the countdown
        ui_picture_question(8);
        lv_timer_handler();
        {
            const char *mt = ui_message_text();
            const char *want_end = "\n\nPress any key to keep this picture.";
            size_t ml = strlen(mt), wl = strlen(want_end);
            CHECK(ml > wl && !strcmp(mt + ml - wl, want_end) && strstr(mt, "In 8 s the previous mode comes back."));
            char what[64];
            snprintf(what, sizeof(what), "%s picture question", md.name);
            int b = view_bounds(what, want.w, want.h) != 0;
            bad += b;
            pages++;
            snprintf(p, sizeof(p), "%s/modes-%s-picture-question.png", out, md.name);
            PxRec::Pl g = r.px.place.back();
            png_out(p, md.fw, md.fh, g.x, g.y, g.w, g.h);
        }
        ui_message_clear();
        lv_timer_handler();
        // the Library inside a folder: the file rows, their sizes and the side panel (the longest name of the bench does not fit a narrow column)
        r.m->key(KEY_F3);
        r.m->key('1');
        r.m->key(KEY_RETURN);
        r.m->key(KEY_RETURN);
        ui_menu_render(*r.m);
        lv_timer_handler();
        {
            char what[64];
            snprintf(what, sizeof(what), "%s library in a folder", md.name);
            int b = view_bounds(what, want.w, want.h) != 0;
            bad += b;
            pages++;
            PxRec::Pl g = r.px.place.back();
            snprintf(p, sizeof(p), "%s/modes-%s-library-files.png", out, md.name);
            png_out(p, md.fw, md.fh, g.x, g.y, g.w, g.h);
        }
        printf("MODES %-8s canvas %dx%d: %d pages, %d with an object outside or cut\n", md.name, want.w, want.h, pages, bad);
        CHECK(bad == 0);
    }
    ui_set_canvas(960, 540);
}

// T164 D10c amended (Alex 2026-11-04): the Picture row of a core says which mode "Auto" is and whose it is ("Auto (1280 x 720 from host)" with the host's stored mode,
// "Auto (W x H from screen)" without one). The row has to fit at every mode the host offers: the value is never cut to dots (view_bounds names the text as a key).
static void modes_picture_row(const char *out)
{
    char p[512];
    static CoreDesc nes_ext; static Setting nes_items[SETTINGS_MAX];
    PictureEnv env = { EDID_ROW_MASK, true, VID_SYS_PAL50 };
    CHECK(!picture_extend(&nes_desc, &nes_ext, nes_items, &env));
    int pi = picture_index(&nes_ext);
    CHECK(pi >= 0);
    struct { const char *name; int sys, hdmi, fw, fh; } modes[] = {
        { "1080p", VID_SYS_PAL50, VID_HDMI_1080P, 1920, 1080 },
        { "720p", VID_SYS_PAL50, VID_HDMI_720P, 1280, 720 },
        { "800x600", VID_SYS_PAL50, VID_HDMI_800x600, 800, 600 },
        { "1024x768", VID_SYS_PAL50, VID_HDMI_1024x768, 1024, 768 },
        { "576p", VID_SYS_PAL50, VID_HDMI_480_576, 720, 576 },
        { "480p", VID_SYS_NTSC60, VID_HDMI_480_576, 640, 480 },
    };
    for (auto &md : modes) {
        for (int host = 1; host >= 0; host--) {
            Rig r(&nes_ext, "/usb0/cores/nes.cfg");
            if (host) {
                r.fs.put_text("/Flash/trx/host.cfg", std::string("[Video]\nMode=") + video_hdmi_name(md.hdmi) + "\nSystem=PAL 50 Hz\n");
            }
            r.c.started = true;
            r.c.program_loaded = true;
            ui_open(&nes_ext);
            picture_start(&r.c, md.sys, md.hdmi);
            r.vid->init();
            TrxCanvas want = trx_canvas_for(md.fw, md.fh);
            CHECK(trx_canvas_w() == want.w && trx_canvas_h() == want.h);
            r.m->open();
            r.m->key(KEY_F5);
            r.m->key('2');
            ui_menu_render(*r.m);
            lv_timer_handler();
            char expect[64];
            snprintf(expect, sizeof(expect), "Auto (%s from %s)", host ? video_hdmi_name(md.hdmi) : "", host ? "host" : "screen");
            bool seen = false;
            for (int i = 0; i < r.m->rows(); i++) {
                char l[120], rt[120];
                bool a;
                r.m->row(i, l, sizeof(l), rt, sizeof(rt), &a);
                if (strcmp(l, "Picture")) continue;
                seen = true;
                bool ok = host ? !strcmp(rt, expect) : (!strncmp(rt, "Auto (", 6) && !strcmp(rt + strlen(rt) - 13, " from screen)"));
                if (!ok) printf("PICROW %s %s: \"%s\", wanted \"%s\"\n", md.name, host ? "host" : "screen", rt, host ? expect : "Auto (... from screen)");
                CHECK(ok);                                                              // the D10c check: the row says where Auto comes from
            }
            CHECK(seen);
            const char *keys[2] = { nes_ext.settings[pi].values[0], 0 };
            char what[64];
            snprintf(what, sizeof(what), "%s picture row (%s)", md.name, host ? "host" : "screen");
            CHECK(view_bounds(what, want.w, want.h, 0, keys) == 0);                     // and it is whole: not cut to dots, inside its row
            if (host) {                                                                  // the second page of a pass is drawn by area (the frame holds the changed part only): one picture per mode
                PxRec::Pl g = r.px.place.back();
                snprintf(p, sizeof(p), "%s/modes-%s-picture-row.png", out, md.name);
                png_out(p, md.fw, md.fh, g.x, g.y, g.w, g.h);
            }
        }
    }
    ui_set_canvas(960, 540);
}

// T168: the menu of the two DDT cores (one task, no settings, no program): the Actions page shows Reset, the pictures go to the output directory
static void ddt_menu(const char *out)
{
    char p[512];
    const struct { const CoreDesc *d; const char *name; } cores[] = { { &ddt_mandel_desc, "ddt_mandel" }, { &ddt_novic_desc, "ddt_novic" } };
    for (const auto &k : cores) {
        Rig r(k.d, "/usb0/cores/ddt.cfg");
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(k.d);
        r.m->open();
        r.m->key(KEY_F7);
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
        char l[100], rt[100]; bool a;
        r.m->row(0, l, 100, rt, 100, &a);
        CHECK(!strcmp(l, "Reset"));
        snprintf(p, sizeof(p), "%s/%s-actions.png", out, k.name);
        png(p, 2);
        r.m->key(KEY_F1);
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/%s-home.png", out, k.name);
        png(p, 2);
    }
}

// T185 D4: the C128's four JiffyDOS file rows on the F5 Settings tab, the file selector a row opens, the refusal of a file in it, and the F3 action list of a .bin file.
// The bench writes the pictures; whether they read well is the UX review's.
static void c128_kit_menu(const char *out)
{
    char p[512];
    Rig r(&c128_desc, "/usb0/cores/c128.cfg");
    std::vector<uint8_t> good(16384, 0x41), small(8192, 0x55);
    for (int i = 0; i < 16384; i++) good[i] = (uint8_t)(i * 7 + (i >> 8) + (i >> 13));
    memcpy(&good[4], "\x11\x11\x11\x11\x11\x11\x11\x11", 8);
    good[0x3FFC] = 0x3D; good[0x3FFD] = 0xFF;
    small[0x1FFC] = 0xE2; small[0x1FFD] = 0xFC;                    // a C64 KERNAL of 8 KiB: its reset vector is $FCE2
    r.fs.put("/usb0/jiffy/c128kernal.bin", good);
    r.fs.put("/usb0/jiffy/kernal8k.bin", small);
    r.fs.put("/usb0/jiffy/notes.txt", std::vector<uint8_t>(10, 1));
    r.fs.put("/usb0/games/disk.d64", std::vector<uint8_t>(174848, 0));
    r.c.started = true;
    r.c.program_loaded = true;
    ui_open(&c128_desc);
    OpResult res;
    OpIn in = in_str("path", "/usb0/jiffy/c128kernal.bin");
    CHECK(ops_call(&r.c, "c128.use_jiffy_c128", &in, 1, &res) == OPE_OK);
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('2');
    ui_menu_render(*r.m);
    lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-settings.png", out);
    png(p, 2);
    auto row_of = [&](const char *t) {
        for (int i = 0; i < r.m->rows(); i++) { char l[200], v[100]; bool a; r.m->row(i, l, 200, v, 100, &a); if (!strcmp(l, t)) return i; }
        return -1;
    };
    int row = row_of("JiffyDOS C64 ROM");
    CHECK(row >= 0);
    select_row(r, row);
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-settings-c64row.png", out);
    png(p, 2);
    r.m->key(KEY_RETURN);
    CHECK(r.m->picking());
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector.png", out);
    png(p, 2);
    int d = find_dir_row(r, "jiffy");
    if (d < 0 && row_of("usb0/") >= 0) { select_row(r, row_of("usb0/")); r.m->key(KEY_RETURN); d = find_dir_row(r, "jiffy"); }
    CHECK(d >= 0);
    select_row(r, d);
    r.m->key(KEY_RETURN);
    int f = row_of("kernal8k.bin");
    CHECK(f >= 0);
    select_row(r, f);
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector-folder.png", out);
    png(p, 2);
    r.m->key(KEY_RETURN);                                          // 8 KiB is no C64 part with BASIC but a KERNAL: taken; the C128 role refuses it below
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector-taken.png", out);
    png(p, 2);
    {
        const TrxTheme &t = trx_theme();                           // the status is one line: it fits the pane (UX review 2026-11-07)
        CHECK(trx_text_w(r.m->status(), t.caption) + 4 <= trx_content_w() - 2 * t.space_lg);
    }
    CHECK(!r.m->picking());
    // the refusal: the C128 row, an 8 KiB file
    select_row(r, row_of("JiffyDOS C128 ROM"));
    r.m->key(KEY_RETURN);
    f = row_of("kernal8k.bin");
    if (f >= 0) {
        select_row(r, f);
        r.m->key(KEY_RETURN);
    }
    CHECK(r.m->picking() && r.m->status_bad());
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector-refused.png", out);
    png(p, 2);
    {
        const TrxTheme &t = trx_theme();                           // the status is one line: it fits the pane (UX review 2026-11-07)
        CHECK(trx_text_w(r.m->status(), t.caption) + 4 <= trx_content_w() - 2 * t.space_lg);
    }
    r.m->key(KEY_ESCAPE);
    // F3: the action list of a .bin file
    r.m->key(KEY_F3);
    for (int g = 0; g < 8 && strcmp(r.m->title(), "/"); g++) r.m->key(KEY_LEFT);
    select_row(r, row_of("usb0/")); r.m->key(KEY_RETURN);
    select_row(r, find_dir_row(r, "jiffy")); r.m->key(KEY_RETURN);
    select_row(r, row_of("c128kernal.bin"));
    r.m->key(KEY_RETURN);
    CHECK(r.m->chooser());
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-file-actions.png", out);
    png(p, 2);
}

// T175: the diag core's pages - the live port rows (idle page), the browser over the tests folder, a prompt of a test and the result of a run
static void diag_pages(const char *out, int cw, int ch, const char *tag)
{
    char p[512];
    ui_set_canvas(cw, ch);                                       // 960 x 540 (shown at 2x), or the compact canvas of 576p: 720 x 576 (T164 D10a)
    auto shot = [&](const char *name) {
        snprintf(p, sizeof(p), "%s/diag-%s%s.png", out, name, tag);
        if (cw == 960) { png(p, 2); } else { png_out(p, cw, ch, 0, 0, cw, ch); }
    };
    Rig r(&portdiag_desc, "/usb0/cores/diag/diag.cfg");
    r.c.started = true;
    r.c.program_loaded = true;
    diag_install_examples(&r.fs, "/usb0/cores/diag");
    ui_open(&portdiag_desc);
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('1');
    // idle: nothing pressed - then the fire button of port 2 and the second comparator
    r.io.regs[PD_LIVE] = 0xFF;
    r.io.regs[PD_LIVE + 1] = 0x03;
    ui_menu_render(*r.m);
    lv_timer_handler();
    char l[100], rt[100]; bool a;
    r.m->row(1, l, 100, rt, 100, &a);
    CHECK(!strcmp(l, "Port 2") && strstr(rt, "F1"));
    shot("idle");
    r.io.regs[PD_LIVE] = 0xDF;                                  // port 2 up (bit 5) low
    r.io.regs[PD_LIVE + 1] = 0x05;                              // port 2 fire (bit 9) low, comparator 0 (bit 10) high
    ui_menu_render(*r.m);
    lv_timer_handler();
    r.m->row(1, l, 100, rt, 100, &a);
    CHECK(strstr(rt, "U0") && strstr(rt, "F0"));
    r.m->row(2, l, 100, rt, 100, &a);
    CHECK(!strcmp(rt, "1  0  0  0"));
    shot("idle-pressed");
    // the picker: the Library on the folder of the tests
    r.m->key(KEY_F3);
    ui_menu_render(*r.m);
    lv_timer_handler();
    r.m->relist();
    ui_menu_render(*r.m);
    lv_timer_handler();
    {
        static const char *path[4] = { "usb0", "cores", "diag", "tests" };       // the media level, then folder by folder, the way the Library bench goes in
        for (int k = 0; k < 4; k++) {
            int d = find_dir_row(r, path[k]);
            if (d < 0) { printf("  the Library has no folder %s (%s)\n", path[k], tag); }
            CHECK(d >= 0);
            select_row(r, d);
            r.m->key(KEY_RETURN);
            ui_menu_render(*r.m);
            lv_timer_handler();
            r.m->relist();
            ui_menu_render(*r.m);
            lv_timer_handler();
        }
    }
    r.m->row(0, l, 100, rt, 100, &a);
    {                                                            // the picture shows the tests folder: the example files are listed by name, the folder is the tests one
        bool seen[5] = { false, false, false, false, false };
        static const char *want[5] = { "cd32.toml", "joystick.toml", "paddle.toml", "pins.toml", "stick2.toml" };
        for (int i = 0; i < r.m->rows(); i++) {
            char l2[100], rt2[100], sub2[100];
            bool a2;
            r.m->row(i, l2, 100, rt2, 100, &a2, sub2, 100);
            for (int k = 0; k < 5; k++) { seen[k] |= !strncasecmp(l2, want[k], strlen(want[k])); }
        }
        for (int k = 0; k < 5; k++) {
            if (!seen[k]) { printf("  the picker (%s) does not list %s\n", tag, want[k]); }
            CHECK(seen[k]);
        }
        CHECK(r.m->rows() >= 5);
        CHECK(strstr(r.m->title(), "tests") != 0);
    }
    shot("picker");
    // a prompt and a result: the real runner against the engine model; the dialogs on the real card
    {
        static HostOs dos;
        AppDlg dlg(&dos, &r.fs);
        Engine eng;
        FakeFs fs;
        diag_install_examples(&fs, "/SD/cores/diag");
        struct Cap : DiagUi {
            Engine *e; char pt[100], px[400], rt[100], rx[400]; int seen = 0;
            int prompt(const char *t, const char *x, int idx, int total, int) override
            {
                if (idx == 3) { portdiag_prompt_text(t, x, idx, total, pt, sizeof(pt), px, sizeof(px)); }
                e->forced_low = idx == 2 ? 0x200 : 0;           // button 1: pin 6
                e->cmp_now = idx == 3 ? 4 : 0;                  // button 2: comparator 2
                return DUI_CONTINUE;
            }
            void progress(const char *) override { }
            void show(const char *t, const char *x) override { snprintf(rt, sizeof(rt), "%s", t); snprintf(rx, sizeof(rx), "%s", x); seen++; }
        } cap;
        cap.e = &eng;
        DiagEnv env;
        memset(&env, 0, sizeof(env));
        env.io = &eng; env.fs = &fs; env.ui = &cap;
        env.now_ms = [] { static uint32_t t; return t += 1; };
        env.delay_ms = [](uint32_t) { };
        env.stamp = [](char o[16]) { strcpy(o, "20261102-143015"); return true; };
        env.core_version = "0.1.0";
        env.folder = "/SD/cores/diag";
        DiagOutcome oc;
        CHECK(diag_run_file(&env, "/SD/cores/diag/tests/stick2.toml", &oc) == 0);
        CHECK(cap.seen == 1 && strstr(cap.rx, "passed"));
        for (int which = 0; which < 2; which++) {
            std::thread worker([&] {
                if (which == 0) { dlg.buttons_timed(cap.pt, cap.px, SVC_BUTTON_YES | SVC_BUTTON_NO, 30); }      // stick2's prompts carry timeout_s = 30
                else { dlg.message(cap.rt, cap.rx); }
            });
            for (int i = 0; i < 3000 && !dlg.open(); i++) { dlg.service(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            CHECK(dlg.open());
            CHECK(dlg.view().remaining_s == (which == 0 ? 30 : 0));
            ui_dialog_render(dlg);
            lv_timer_handler();
            shot(which == 0 ? "prompt" : "result");
            dlg.take_key(which == 0 ? 'y' : KEY_RETURN);
            worker.join();
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        }
    }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/coreapp-render";
    char p[512];

    Rig r(&atari800_desc, "/usb0/cores/atari800.cfg");
    r.fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
    r.fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
    r.fs.put_text("/usb0/games/arkanoid.atr", "x");
    r.fs.put_text("/usb0/games/boulderdash.atr", "x");
    r.fs.put_text("/usb0/games/notes.txt", "x");
    r.c.started = true;
    r.c.program_loaded = true;

    ui_open(&atari800_desc);
    {   // T101 S9: the splash the core app puts up first: the whole splash once, then only the bar and its label per step, then a message
        // (a ROM file missing) takes it away
        static uint16_t prev[960 * 540];
        ui_splash_open("Atari 800", "85acf2b  2026-10-12", 4);
        unsigned first = 0;
        for (size_t k = 0; k < 960 * 540; k++) first += prev[k] != fb[k];
        CHECK(first > 20000);
        memcpy(prev, fb, sizeof(prev));
        const char *labels[4] = { "Reading the settings", "Setting the picture", "Libraries on the stick", "ROM files, then the core" };
        for (int i = 0; i < 4; i++) {
            ui_splash_step(i, labels[i]);
            unsigned d = 0;
            for (size_t k = 0; k < 960 * 540; k++) d += prev[k] != fb[k];
            printf("SPLASH step %d: %u px changed\n", i, d);
            CHECK(d > 0 && d < 12000);
            memcpy(prev, fb, sizeof(prev));
            snprintf(p, sizeof(p), "%s/atari-splash_%d.png", out, i);
            png(p, 2);
        }
        ui_message("ROM files", "atarixl.rom is missing", true);
        lv_timer_handler();
        CHECK(!trx_splash_up());
        snprintf(p, sizeof(p), "%s/atari-splash_rom.png", out);
        png(p, 2);
        ui_message_clear();
        lv_timer_handler();
    }
    r.m->open();

    struct { const char *name; int area_key; int tab; } shots[] = {
        { "home",     KEY_F1, 1 },
        { "library",  KEY_F3, 1 },
        { "machine",  KEY_F5, 1 },
        { "settings", KEY_F5, 2 },
        { "actions",  KEY_F7, 1 },
        { "system",   KEY_F8, 1 },
    };
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        r.m->key(shots[i].area_key);
        r.m->key('0' + shots[i].tab);
        ui_menu_render(*r.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/atari-%s.png", out, shots[i].name);
        png(p, 2);
    }

    {   // T167 S3: a question card (veil, card) with a bar below it, and the bar alone, over the system screen: a tick of the bar changes the bar's card and
        // nothing else; no state between two draw tasks holds a pixel of what lies behind the cards
        static HostOs dos;
        AppDlg dlg(&dos, &r.fs);
        for (int variant = 0; variant < 2; variant++) {
            std::atomic<bool> done(false);
            std::thread worker;
            if (variant == 1) {
                worker = std::thread([&] { dlg.message("Unzip", "Not enough memory."); done = true; });
                for (int i = 0; i < 3000 && !dlg.open(); i++) {
                    dlg.service();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                CHECK(dlg.open());
            }
            void *h = dlg.progress_open(variant ? "Building the image" : "Writing the set", 100);
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
            snprintf(p, sizeof(p), "%s/atari-dialog-bar%d_0.png", out, variant);
            png(p, 2);
            int by = variant ? 540 - 28 - 74 - 6 : 540 - 28 - 74 - 24;
            int n = 0, bad = 0, states = 0, visits = 0, ebad = 0;
            ovp::run_reset();
            for (int i = 1; i <= 14; i++) {
                char text[32];
                snprintf(text, sizeof(text), "file %d of 14", i);
                dlg.progress_set(h, 7, text);
                dlg.service();
                ovp::begin(fb, 200, by, 560, 74);
                ui_dialog_render(dlg);
                lv_timer_handler();
                int st, vi;
                bad += ovp::end(&st, &vi);
                states += st; visits += vi; n++;
                if (variant) {                                                // the veil and the question's card stand: the band above the bar does not change at all
                    ovp::begin(fb, 0, 150, 960, 250);
                    dlg.progress_set(h, 0, "again");
                    dlg.service();
                    ui_dialog_render(dlg);
                    lv_timer_handler();
                    ebad += ovp::end(&st, &vi, 8);
                }
            }
            snprintf(p, sizeof(p), "%s/atari-dialog-bar%d_1.png", out, variant);
            png(p, 2);
            {   // the worst moment of the run, as the person saw it
                ovp::Pic saved = ovp::paste_worst(fb);
                snprintf(p, sizeof(p), "%s/overlay_dialog%d_worst.png", out, variant);
                png(p, 2);
                ovp::paste_back(fb, saved);
            }
            printf("overlay dialog %d: %d ticks, %d states, %d visits, %d foreign pixels, %d changed outside the bar\n", variant, n, states, visits, bad, ebad);
            CHECK(n == 14 && visits >= n && states >= n);
            OVCHECK(bad == 0);
            OVCHECK(ebad == 0);
            dlg.progress_close(h);
            dlg.service();
            if (variant == 1) {
                dlg.take_key(KEY_RETURN);
                worker.join();
            }
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        }
    }

    {   // T167 S4: the menu's own job display (a new image written in steps: "Building the image", NN %). The row is in the pane, not an overlay, but it is redrawn the
        // same way: every tick changes its text, and no state between two draw tasks may hold a pixel that is not the pane's own (the rows of the old and the new picture,
        // the pane colour). The probe watches the rows of the pane.
        Rig j(&atari800_desc, "/usb0/cores/atari800j.cfg");
        j.fs.put_text("/usb0/games/x.atr", "x");
        j.c.started = true;
        j.c.program_loaded = true;
        auto pick = [&](const char *label) {
            char l[100], rt[100], sb[100];
            bool act;
            for (int i = 0; i < j.m->rows(); i++) {
                j.m->row(i, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
                if (!strcmp(l, label)) {
                    while (j.m->selected() > i) j.m->key(KEY_UP);
                    while (j.m->selected() < i) j.m->key(KEY_DOWN);
                    return true;
                }
            }
            return false;
        };
        j.m->open();
        j.m->key(KEY_F7);
        bool ok = pick("Create blank ATR");
        CHECK(ok);
        if (ok) {
            j.m->key(KEY_RETURN);
            for (int i = 0; i < 8 && strcmp(j.m->title(), "/") != 0; i++) j.m->key(KEY_LEFT);
            ok = pick("usb0/");
            if (ok) { j.m->key(KEY_RETURN); ok = pick("games/"); }
            if (ok) { j.m->key(KEY_RETURN); }
            CHECK(ok);
        }
        if (ok) {
            j.m->key(KEY_RETURN);                                               // "Create here"
            for (int i = 0; i < 30; i++) j.m->key(KEY_BACK);
            for (const char *c = "prog"; *c; c++) j.m->key(*c);
            j.m->key(KEY_RETURN);
            CHECK(j.m->busy());
            ui_menu_render(*j.m);
            lv_timer_handler();
            snprintf(p, sizeof(p), "%s/atari-job_0.png", out);
            png(p, 2);
            int n = 0, bad = 0, states = 0, visits = 0;
            for (int i = 0; i < 400 && j.m->busy(); i++) {
                j.m->tick();
                ovp::begin(fb, 190, 76, 760, 150);
                ui_menu_render(*j.m);
                lv_timer_handler();
                int st, vi;
                bad += ovp::end(&st, &vi, 6);
                states += st; visits += vi; n++;
            }
            snprintf(p, sizeof(p), "%s/atari-job_1.png", out);
            png(p, 2);
            printf("overlay job: %d ticks, %d states, %d visits, %d foreign pixels\n", n, states, visits, bad);
            CHECK(n >= 5 && visits >= n && states >= n);
            OVCHECK(bad == 0);
        }
    }

    {   // T160 (board 2026-10-31): the volume keys' line is on screen on every page, not only on F5 / Settings. A core without a Volume of its own gets the generic one;
        // per page: the pane without the line, then the key and the line (the toast card must change pixels at the foot), then the line run out (the pane as before).
        static CoreDesc vd;
        static Setting vs[SETTINGS_MAX];
        CHECK(!volume_extend(&atari800_desc, &vd, vs));
        Rig v(&vd, "/usb0/cores/atari800v.cfg");
        v.c.started = true;
        v.m->open();
        const int pages[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7 };
        static uint16_t before[960 * 540];
        for (int pk : pages) {
            v.m->key(pk);
            v.io.t += 5000;
            v.m->toast_expire();
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
            memcpy(before, fb, sizeof(before));
            v.m->key('{');
            CHECK(v.m->toast_text()[0] != 0);
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
            size_t shown = 0;
            for (int y = 512; y < 540; y++) for (int x = 200; x < 760; x++) shown += fb[y * 960 + x] != before[y * 960 + x];
            snprintf(p, sizeof(p), "%s/volume_toast_F%d.png", out, pk - KEY_F1 + 1);
            png(p, 2);
            CHECK(shown > 300);                                              // the card with its text, whatever the page behind it
            v.io.t += 5000;
            CHECK(v.m->toast_expire());
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
        }
    }

    {   // T181: the hotkey items (modifier and one key per action) are 7 rows of the Settings tab (F5, tab 2) of a core with a default map, listed like the core's own
        // settings. The same core with and without them: F5 / Settings differs by the rows, F8 System is the same pixels (it lists none of them).
        static CoreDesc hd;
        static Setting hs[SETTINGS_MAX];
        CHECK(!hk_extend(&atari800_desc, &hd, hs));
        CHECK(hd.nsettings == atari800_desc.nsettings + 1 + HK_NACT - 1);
        Rig base(&atari800_desc, "/usb0/cores/atari800hb.cfg");
        Rig hot(&hd, "/usb0/cores/atari800hk.cfg");
        base.c.started = true; hot.c.started = true;
        base.m->open(); hot.m->open();
        static uint16_t without[960 * 540];
        auto differ = [&](int page, int tab) {
            base.m->key(page); if (tab) base.m->key('0' + tab);
            ui_menu_render(*base.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            hot.m->key(page); if (tab) hot.m->key('0' + tab);
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t diff = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) diff += fb[y * 960 + x] != without[y * 960 + x];
            return diff;
        };
        size_t f8 = differ(KEY_F8, 0);
        size_t f5 = differ(KEY_F5, 2);
        printf("hotkey rows: F5 Settings %zu pixels differ, F8 System %zu\n", f5, f8);
        snprintf(p, sizeof(p), "%s/hotkeys_F5_settings.png", out);
        png(p, 2);
        CHECK(f5 > 3000);                                                    // 7 rows of about 42 px at 812 px wide, text only
        CHECK(f8 == 0);
    }

    // the message view a core that cannot start shows
    ui_message("This core cannot be started", "The bundle is \"vc20\" but the core in the FPGA is \"atari800\". The core stays held.", true);
    lv_timer_handler();
    snprintf(p, sizeof(p), "%s/atari-held.png", out);
    png(p, 2);

    // T104, board 2026-10-16: a key that only moves the selection must restyle two rows, not rebuild the
    // pane. Measured here, because "feels slow" is not a number: the frame must not be built again, the
    // cheap path must have counted the move, and the area LVGL is asked to redraw must stay near two
    // rows instead of the whole pane.
    r.m->key(KEY_F5);                                        // Settings: four rows, so DOWN has somewhere to go
    r.m->key('2');
    ui_menu_render(*r.m);
    lv_timer_handler();
    uint32_t frames0 = trx_shell_frames(), cheap0 = ui_cheap_moves();
    r.m->key(KEY_DOWN);
    ui_menu_render(*r.m);
    uint32_t moved_px = inv_pixels();
    lv_timer_handler();
    CHECK(ui_cheap_moves() == cheap0 + 1);                   // the move went through the cheap path
    CHECK(trx_shell_frames() == frames0);                    // and the frame was not built again
    // two rows of about 36 x 864 px are ~62,000; the whole pane is ~356,000. The bound sits between
    // them, well clear of both, so it catches a pane rebuild and not a row that grew a line.
    CHECK(moved_px > 0 && moved_px < 100000);
    printf("one key moved %u pixels of the pane\n", (unsigned)moved_px);
    // the control for the bound: the first render of another tab does rebuild, and that is a big area
    r.m->key(KEY_F7);
    r.m->key('1');
    ui_menu_render(*r.m);
    uint32_t rebuilt_px = inv_pixels();
    lv_timer_handler();
    CHECK(rebuilt_px > moved_px * 4);
    // a changed row is not a selection move: the pane is built again
    r.m->key(KEY_F5);
    r.m->key('2');
    ui_menu_render(*r.m);
    lv_timer_handler();
    uint32_t cheap1 = ui_cheap_moves();
    r.m->key(KEY_RIGHT);                                     // a setting's value changes: new text in a row
    ui_menu_render(*r.m);
    lv_timer_handler();
    CHECK(ui_cheap_moves() == cheap1);                       // nothing cheap about a row whose text moved


    // T104 (board 2026-10-22: "scrolling is ultra slow"): walking down a long list. Inside the window a step is a selection
    // move (above); a step past its last row used to rebuild the whole pane, every step from there on. Now the rows that exist
    // are written again, and only the labels whose text moved repaint. Counted per step, and the picture must be the one a
    // build from scratch makes.
    for (int i = 0; i < 60; i++) {
        snprintf(p, sizeof(p), "/zgame%02d.atr", i);
        r.fs.put_text(p, "x");
    }
    r.m->key(KEY_F3);
    r.m->key('1');
    r.m->relist();
    ui_menu_render(*r.m);
    lv_timer_handler();
    {
        uint32_t fr0 = trx_shell_frames(), ch0 = ui_cheap_moves(), ip0 = ui_inplace_moves(), total = 0, worst = 0, worst_after_edge = 0;
        int steps = 0;
        for (int i = 0; i < 40; i++) {
            r.m->key(KEY_DOWN);
            ui_menu_render(*r.m);
            uint32_t px_n = inv_pixels();
            lv_timer_handler();
            total += px_n;
            worst = px_n > worst ? px_n : worst;
            if (i >= 19 && px_n > worst_after_edge) {            // step 17 leaves the first window: its ".." row has no value, the next rows have one
                worst_after_edge = px_n;
            }
            steps++;
        }
        printf("scroll: %d steps, %u px in all, worst step %u px, worst after the first window shift %u px, %u cheap, %u in place, "
               "%u frames rebuilt\n", steps, (unsigned)total, (unsigned)worst, (unsigned)worst_after_edge, (unsigned)(ui_cheap_moves() - ch0),
               (unsigned)(ui_inplace_moves() - ip0), (unsigned)(trx_shell_frames() - fr0));
        CHECK(trx_shell_frames() == fr0);                    // the frame never moves while the list scrolls
        CHECK(ui_inplace_moves() - ip0 >= 20);               // the steps past the window's edge were written in place
        CHECK(worst_after_edge < 100000);                    // a step costs a few thousand label pixels, not the 356,000 of the pane
        CHECK(total / steps < 100000);
        // the same state built from scratch must look the same, pixel for pixel
        static uint16_t kept[960 * 540];
        memcpy(kept, fb, sizeof(kept));
        ui_menu_forget_rows();
        ui_menu_render(*r.m);
        lv_timer_handler();
        CHECK(memcmp(kept, fb, sizeof(kept)) == 0);
        // and a row whose value changed (a setting) is written in place as well, the same picture again
        r.m->key(KEY_F5);
        r.m->key('2');
        ui_menu_render(*r.m);
        lv_timer_handler();
        uint32_t ip1 = ui_inplace_moves();
        r.m->key(KEY_RIGHT);
        ui_menu_render(*r.m);
        uint32_t edit_px = inv_pixels();
        lv_timer_handler();
        CHECK(ui_inplace_moves() == ip1 + 1);
        CHECK(edit_px > 0 && edit_px < 100000);
        memcpy(kept, fb, sizeof(kept));
        ui_menu_forget_rows();
        ui_menu_render(*r.m);
        lv_timer_handler();
        CHECK(memcmp(kept, fb, sizeof(kept)) == 0);
        printf("one value change: %u px\n", (unsigned)edit_px);
    }

    // the terminal skin, same code
    trx_skin_set(TRX_SKIN_TERMINAL);
    r.m->key(KEY_F5);
    r.m->key('2');
    ui_menu_render(*r.m);
    lv_timer_handler();
    snprintf(p, sizeof(p), "%s/atari-settings-terminal.png", out);
    png(p, 2);

    // T104.2: the C64 (the application opens again for its own title chip; LVGL starts over). Its browser offers two actions for a .prg, and the second asks for the start address in a row of its own. The
    // same ui.cc draws it; what is checked is that the prompt reaches the screen as the row the menu model gives.
    {
        trx_skin_set(TRX_SKIN_PANEL);
        Rig c(trx64u::desc(), "/flash/trx64u/c64.cfg", trx64u::merge());           // the C64's menu is its record (T104.2 D10)
        c.fs.put("/usb0/x.prg", std::vector<uint8_t>{ 0x00, 0xC0, 0x60, 0x60 });
        c.c.started = true;
        ui_open(&c64_desc);
        c.m->open();
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/c64-home.png", out);
        png(p, 2);
        c.m->key(KEY_F3);                                        // the media level: usb0 is the first row
        c.m->key(KEY_RETURN);                                    // opens the stick: ../ is first, x.prg second
        c.m->key(KEY_DOWN);
        c.m->key(KEY_RETURN);
        CHECK(c.m->chooser());
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/c64-chooser.png", out);
        png(p, 2);
        c.m->key(KEY_DOWN);
        c.m->key(KEY_RETURN);
        CHECK(c.m->entering());
        c.m->key('1'); c.m->key('0');
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/c64-prompt.png", out);
        png(p, 2);
        char l[100], r[100]; bool a;
        c.m->row(0, l, 100, r, 100, &a);
        CHECK(!strcmp(l, "Start address") && !strcmp(r, "$10_"));
        c.m->key(KEY_ESCAPE);
        c.m->key(KEY_F5);                                        // the machine's own tab: the program slot and the freeze setting
        c.m->key('2');
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/c64-settings.png", out);
        png(p, 2);
    }

    a1200_menu(out);
    modes_menu(out);
    rtc_menu(out);
    modes_picture_row(out);
    ddt_menu(out);
    c128_kit_menu(out);
    diag_pages(out, 960, 540, "");
    diag_pages(out, 720, 576, "-576p");
    ui_set_canvas(960, 540);
    areas_core();
    printf("coreapp render: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
