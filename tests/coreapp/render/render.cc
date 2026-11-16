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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <zlib.h>

extern const CoreDesc atari800_desc;
extern const CoreDesc vc20_desc;
extern const CoreDesc nes_desc;
extern const CoreDesc c64_desc;
extern const CoreDesc c128_desc;
extern const CoreDesc vc20_desc;
extern const CoreDesc amiga1200_desc;
extern const CoreDesc amiga_desc;
extern const CoreDesc ddt_mandel_desc;
extern const CoreDesc ddt_novic_desc;
#include "picture.h"
#include "volume.h"
#include "videomode.h"
#include "menubuild.h"
#include "screens.h"
#include "cores.h"
#include "table.cc"                                           // T187: the board's own core table (every core), not a short copy of it

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
// T209 (UX review): no two texts of a page lie on each other (a title that wrapped into the line below, a value over a label). Returns the number of pairs.
struct TxtBox { int x1, y1, x2, y2; };
static void txt_collect(lv_obj_t *o, TxtBox *b, int *n, int cap)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(o, &lv_label_class) && *n < cap) {
        const char *tx = lv_label_get_text(o);
        const lv_font_t *f = lv_obj_get_style_text_font(o, LV_PART_MAIN);
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        if (tx && *tx) {
            lv_point_t sz;
            lv_text_get_size(&sz, tx, f, 0, 0, a.x2 - a.x1 + 1, LV_TEXT_FLAG_NONE);   // as wide as the label lets the text be: a wrapped text counts with its lines
            b[*n] = { a.x1, a.y1, a.x1 + (sz.x < a.x2 - a.x1 + 1 ? sz.x : a.x2 - a.x1 + 1), a.y1 + sz.y };
            (*n)++;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        txt_collect(lv_obj_get_child(o, (int32_t)i), b, n, cap);
    }
}
static int txt_overlaps(const char *what)
{
    static TxtBox b[2000];
    int n = 0, bad = 0;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_update_layout(scr);
    txt_collect(scr, b, &n, 2000);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int w = (b[i].x2 < b[j].x2 ? b[i].x2 : b[j].x2) - (b[i].x1 > b[j].x1 ? b[i].x1 : b[j].x1);
            int h = (b[i].y2 < b[j].y2 ? b[i].y2 : b[j].y2) - (b[i].y1 > b[j].y1 ? b[i].y1 : b[j].y1);
            if (w > 2 && h > 3) {
                bad++;
                printf("  %s: text boxes %d and %d lie on each other (%d x %d px)\n", what, i, j, w, h);
            }
        }
    }
    printf("OVERLAP %-30s %d texts, %d pairs\n", what, n, bad);
    return bad;
}
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

// T187: the descriptor the board's menu runs on - the core's own plus Picture, Volume, Persist, Home and the hotkeys, built by the one function run.cc calls.
// Every bench that shows "what the board shows" goes through here; none builds its own chain. Lives for the whole run (the Settings keep pointers into it).
struct Built {
    CoreDesc d; Setting s[SETTINGS_MAX];
    explicit Built(const CoreDesc *src, const PictureEnv *env = 0)
    {
        PictureEnv e = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
        CHECK(!menu_build(src, &d, s, env ? env : &e));
    }
};
static CoreDesc *board_desc(const CoreDesc *src, const PictureEnv *env = 0) { return &(new Built(src, env))->d; }

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
    CoreDesc *atari_b = board_desc(&atari800_desc);
    Rig c(atari_b, "/usb0/cores/atari800.cfg");
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
    ui_open(atari_b);

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
    area_case("Library: RETURN on a folder", key_px(c, KEY_RETURN), PANE + 10000, &c);       // 362,326 px measured 2026-11-12 (T209: the crumb, the count and the side panel change with the folder)
    printf("AREA library title after RETURN: %s (%d rows)\n", c.m->title(), c.m->rows());
    area_case("Library: LEFT (parent folder)", key_px(c, KEY_LEFT), PANE + 10000, &c);      // 360,739 px measured 2026-11-12
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
    static CoreDesc a12;                                               // the descriptor as the board runs it: the core's own items plus the application's whole chain (T187)
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };           // T164: a host whose gateware answers, a screen that names every row
    a12 = *board_desc(&amiga1200_desc, &env);
    Rig r(&a12, "/usb0/cores/amiga1200.cfg");
    r.c.started = true;
    r.c.program_loaded = true;
    ui_open(&a12);
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    r.vid->init();
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('1');
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
    {   // T209: the Home page at 480p (canvas 720 x 480) and at the full canvas: the title is one line and no text lies on another
        r.m->key(KEY_F1);
        for (int k = 0; k < 2; k++) {
            TrxCanvas c480 = trx_canvas_for(720, 480);
            if (k == 0) {
                trx_canvas_set(c480.w, c480.h);
                trx_shell_forget();
            }
            ui_menu_render(*r.m);
            lv_timer_handler();
            CHECK(txt_overlaps(k == 0 ? "a1200 home 480p" : "a1200 home") == 0);
            if (k == 0) {
                TrxCanvas full = trx_canvas_for(1920, 1080);
                trx_canvas_set(full.w, full.h);
                trx_shell_forget();
            }
        }
        r.m->key(KEY_F5);                                    // back to the page the next cases expect
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
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
        m.m->key('4');                                       // the core, Video, Audio, Input: the Amiga's mouse rows are the fourth tab here
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
        static CoreDesc nes_ext;
        PictureEnv pc = { (uint16_t)(1u << 10), 0, true, VID_SYS_PAL50 };
        nes_ext = *board_desc(&nes_desc, &pc);
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
    // the value sits on the middle of the label's line (T209: every row is one line)
    for (int two = 0; two < 1; two++) {
        TrxRow row;
        memset(&row, 0, sizeof(row));
        row.label = "Chip RAM"; row.value = "2M";
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
    CHECK(strstr(r.m->foot_line(), "next reset") != 0);
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
    static CoreDesc d[2];
    const CoreDesc *src[2] = { &amiga_desc, &amiga1200_desc };
    const char *name[2] = { "a500", "a1200" };
    for (int k = 0; k < 2; k++) {
        PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
        d[k] = *board_desc(src[k], &env);
        char cfg[64]; snprintf(cfg, sizeof(cfg), "/usb0/cores/%s.cfg", k ? "amiga1200" : "amiga");
        Rig r(&d[k], cfg);
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(&d[k]);
        picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
        r.vid->init();
        r.m->open();
        r.m->key(KEY_F5);
        r.m->key('1');
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
    static CoreDesc a12;
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    a12 = *board_desc(&amiga1200_desc, &env);
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
        ui_picture_question(8, "1280 x 720, 50.0 Hz", "1920 x 1080, 50.0 Hz");
        lv_timer_handler();
        {
            const char *mt = ui_message_text();
            const char *want_end = "RETURN keeps it      LEFT or ESC goes back now";     // T187 D5: names the mode that came up and the one that comes back; the keys are the last line
            size_t ml = strlen(mt), wl = strlen(want_end);
            CHECK(ml > wl && !strcmp(mt + ml - wl, want_end) && strstr(mt, "1280 x 720, 50.0 Hz") && strstr(mt, "In 8 s 1920 x 1080, 50.0 Hz comes back."));
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
    static CoreDesc nes_ext;
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    nes_ext = *board_desc(&nes_desc, &env);
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
                std::string rts = rt;                                                    // D5: the closed row ends in " >" (it opens the page of the modes)
                CHECK(rts.size() > 2 && rts.compare(rts.size() - 2, 2, " >") == 0);
                if (rts.size() > 2) { rts.resize(rts.size() - 2); }
                bool ok = host ? rts == expect : (!strncmp(rts.c_str(), "Auto (", 6) && rts.size() >= 13 && !strcmp(rts.c_str() + rts.size() - 13, " from screen)"));
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
        CoreDesc *kb = board_desc(k.d);
        Rig r(kb, "/usb0/cores/ddt.cfg");
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(kb);
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
    CoreDesc *c128_b = board_desc(&c128_desc);
    Rig r(c128_b, "/usb0/cores/c128.cfg");
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
    ui_open(c128_b);
    OpResult res;
    OpIn in = in_str("path", "/usb0/jiffy/c128kernal.bin");
    CHECK(ops_call(&r.c, "c128.use_jiffy_c128", &in, 1, &res) == OPE_OK);
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('1');
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
    r.m->key(KEY_F1);                                            // T205 D1: the port lines (media rows) are on Home
    // idle: nothing pressed - then the fire button of port 2 and the second comparator
    r.io.regs[PD_LIVE] = 0xFF;
    r.io.regs[PD_LIVE + 1] = 0x03;
    ui_menu_render(*r.m);
    lv_timer_handler();
    char l[100], rt[100]; bool a;
    int p2 = -1;
    for (int i = 0; i < r.m->rows() && p2 < 0; i++) { r.m->row(i, l, 100, rt, 100, &a); if (!strcmp(l, "Port 2")) p2 = i; }
    CHECK(p2 >= 0);
    r.m->row(p2, l, 100, rt, 100, &a);
    CHECK(!strcmp(l, "Port 2") && strstr(rt, "F1"));
    shot("idle");
    r.io.regs[PD_LIVE] = 0xDF;                                  // port 2 up (bit 5) low
    r.io.regs[PD_LIVE + 1] = 0x05;                              // port 2 fire (bit 9) low, comparator 0 (bit 10) high
    ui_menu_render(*r.m);
    lv_timer_handler();
    r.m->row(p2, l, 100, rt, 100, &a);
    CHECK(strstr(rt, "U0") && strstr(rt, "F0"));
    r.m->row(p2 + 1, l, 100, rt, 100, &a);
    CHECK(!strcmp(rt, "1  0  0  0"));
    shot("idle-pressed");
    // T201: the A/V page (tab 2 of the core's area): the 18 rows of the test picture's encoder, live. A page of its own, opened the way the other settings pages are
    {
        Rig v(&portdiag_desc, "/usb0/cores/diag/diag.cfg");
        v.c.started = true;
        v.c.program_loaded = true;
        ui_open(&portdiag_desc);
        v.m->open();
        v.m->key(KEY_F5);
        v.m->key('1');
        ui_menu_render(*v.m);
        lv_timer_handler();
        lv_obj_invalidate(lv_screen_active());                 // the whole page into the picture, not only what the last call changed
        ui_menu_render(*v.m);
        lv_timer_handler();
        CHECK(v.m->rows() == DT_AV_N);
        v.m->row(0, l, 100, rt, 100, &a);
        CHECK(!strcmp(l, "Standard") && strstr(rt, "PAL 50 Hz"));
        shot("av");
        for (int k = 0; k < DT_AV_N - 1; k++) { v.m->key(KEY_DOWN); }       // the selection on the last row (Lines 60 Hz): the page scrolls to the sync rows
        ui_menu_render(*v.m);
        lv_timer_handler();
        lv_obj_invalidate(lv_screen_active());
        ui_menu_render(*v.m);
        lv_timer_handler();
        shot("avsync");
    }
    ui_open(&portdiag_desc);
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
        char qt[96], qx[400];
        portdiag_question_text("A/V output walk", "Does the picture stand still (no rolling, no jumping)?", 3, 36, qt, sizeof(qt), qx, sizeof(qx));
        char ct[96], cx[400];                                  // T201 v2: the colour question is a choice
        portdiag_question_text("A/V output walk", "Which colour is the bar right of the white one?", 3, 36, ct, sizeof(ct), cx, sizeof(cx));
        snprintf(cx, sizeof(cx), "%s", "Which colour is the bar right of the white one?");
        static const char *const cch[] = { "yellow", "blue", "other" };
        int picked = -9;
        for (int which = 0; which < 4; which++) {
            std::thread worker([&] {
                if (which == 3) { picked = dlg.choose_timed(ct, cx, cch, 3, 30); return; }
                if (which == 0) { dlg.buttons_timed(cap.pt, cap.px, SVC_BUTTON_YES | SVC_BUTTON_NO, 30); }      // stick2's prompts carry timeout_s = 30
                else if (which == 2) { dlg.buttons_timed(qt, qx, SVC_BUTTON_YES | SVC_BUTTON_NO, 30); }          // T201: a question of avout.toml (timeout_s = 30)
                else { dlg.message(cap.rt, cap.rx); }
            });
            for (int i = 0; i < 3000 && !dlg.open(); i++) { dlg.service(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            CHECK(dlg.open());
            CHECK(dlg.view().remaining_s == (which == 0 ? 30 : which == 2 ? 30 : which == 3 ? 30 : 0));
            ui_dialog_render(dlg);
            lv_timer_handler();
            shot(which == 0 ? "prompt" : which == 2 ? "avquestion" : which == 3 ? "avchoice" : "result");
            if (which == 3) { dlg.take_key(KEY_DOWN); }       // blue
            dlg.take_key(which == 0 || which == 2 ? 'y' : KEY_RETURN);
            worker.join();
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        }
        CHECK(picked == 1);
    }
}


// ---- T187 S1: the list rules (review T1, T2, T6, T7) ----------------------------------------------------------------------------------
// For every core of the board's table, with the board's whole menu chain (menu_build), at the 480p, 720p and 1080p canvases, on every page that is a
// flat list: walk the selection down and up through every index (keys, as a person does), and after each step read back what LVGL drew. The selected
// row lies whole inside the rows' room, the rows drawn fit into it, the position label stays off the first row and says what is hidden, no row has a
// second line "at the next reset", and the legend line is there exactly when a row is marked. Files get a path, so some rows are two lines tall.
// (the window counted in one-line rows again was the board's defect: every selection index of every list is checked)
static int s1fails, s1states, s1lists, s1cores, s1two, s1marked, s1legends;
#define S1CHECK(c, ...) do { int f0_ = fails; CHECK(c); if (fails > f0_) { s1fails += fails - f0_; printf("  S1 context: " __VA_ARGS__); printf("\n"); } } while (0)

static void s1_state(const char *core, const char *cv, int area, int tab, Rig &r, int cw, int ch, const char *shot_out, const char *shot_tag)
{
    UiListProbe p;
    ui_menu_render(*r.m);
    lv_timer_handler();
    if (!ui_list_probe(&p)) { return; }
    int n = r.m->rows(), sel = r.m->selected();
    s1states++;
    S1CHECK(!p.sel_heading, "%s %s area %d tab %d: selected row %d of %d is a heading", core, cv, area + 1, tab, sel, n);
    S1CHECK(!p.last_heading, "%s %s area %d tab %d: sel %d of %d: a heading is the last line of the window (first %d, %d drawn)", core, cv, area + 1, tab, sel, n, p.first, p.nrows);
    S1CHECK(p.val_off_max <= 2, "%s %s area %d tab %d: sel %d: a value is drawn %d px off the middle of its label (P4b, %d values)", core, cv, area + 1, tab, sel, p.val_off_max, p.nvals);
    // T205 P3 (S4): every drawn row stands where the rows above it end (one pixel gap), and its label and its value are the texts of ITS row of the model, drawn inside its
    // own band - a value in the line of another row, or half a row off, fails here, at 480p, 720p and 1080p, in every core's lists, in every selection state
    for (int i = 0; i < p.nrows && i < UiListProbe::UI_PROBE_ROWS; i++) {
        char l[120], v[120], sb[120];
        bool act;
        r.m->row(p.first + i, l, sizeof(l), v, sizeof(v), &act, sb, sizeof(sb));
        S1CHECK(i == 0 || p.row_y[i] == p.row_y[i - 1] + p.row_h[i - 1] + 1, "%s %s area %d tab %d: sel %d: row %d stands at y %d, the row above ends at %d (P3)", core, cv, area + 1, tab, sel, i, p.row_y[i], i ? p.row_y[i - 1] + p.row_h[i - 1] : 0);
        S1CHECK(!strcasecmp(p.lab[i], l), "%s %s area %d tab %d: sel %d: row %d draws the name \"%s\", the model has \"%s\" (P3)", core, cv, area + 1, tab, sel, i, p.lab[i], l);
        if (!r.m->heading(p.first + i)) {
            S1CHECK(p.row_has_val[i] == (v[0] != 0), "%s %s area %d tab %d: sel %d: row \"%s\" %s a value, the model %s (P3)", core, cv, area + 1, tab, sel, l, p.row_has_val[i] ? "draws" : "has no", v[0] ? "has one" : "has none");
            if (p.row_has_val[i]) {
                S1CHECK(!strcmp(p.val[i], v), "%s %s area %d tab %d: sel %d: row \"%s\" draws the value \"%s\", the model has \"%s\" (P3)", core, cv, area + 1, tab, sel, l, p.val[i], v);
                S1CHECK(p.val_cy[i] >= 0 && p.val_cy[i] < p.row_h[i] && p.lab_cy[i] >= 0 && p.lab_cy[i] < p.row_h[i], "%s %s area %d tab %d: sel %d: row \"%s\": the value's middle %d or the name's %d lies outside the row's %d px (P3)", core, cv, area + 1, tab, sel, l, p.val_cy[i], p.lab_cy[i], p.row_h[i]);
            }
        }
    }
    S1CHECK(p.sel_seen, "%s %s area %d tab %d: selected row %d of %d is not among the %d drawn rows (first %d)", core, cv, area + 1, tab, sel, n, p.nrows, p.first);
    S1CHECK(p.sel_y1 >= p.rows_top && p.sel_y2 <= p.rows_bottom, "%s %s area %d tab %d: selected row %d of %d at y %d-%d, the rows' room is %d-%d (drawn %d rows)",
            core, cv, area + 1, tab, sel, n, p.sel_y1, p.sel_y2, p.rows_top, p.rows_bottom, p.nrows);
    S1CHECK(p.drawn_h <= p.rows_bottom - p.rows_top, "%s %s area %d tab %d: sel %d: drawn rows %d px, room %d px", core, cv, area + 1, tab, sel, p.drawn_h, p.rows_bottom - p.rows_top);
    if (p.nrows < n) {
        S1CHECK(p.pos[0] != 0, "%s %s area %d tab %d: %d rows hidden and no position label", core, cv, area + 1, tab, n - p.nrows);
        int pa = 0, pb = 0, pc = 0;                                             // the counter counts settings: rows hidden that are only headings leave "1-13 of 13"
        bool whole = sscanf(p.pos, "%d-%d of %d", &pa, &pb, &pc) == 3 && pa == 1 && pb == pc;
        S1CHECK(whole || strstr(p.pos, "above") || strstr(p.pos, "below"), "%s %s area %d tab %d: position label \"%s\" does not say what is hidden", core, cv, area + 1, tab, p.pos);
        S1CHECK(p.pos_y2 <= p.rows_top, "%s %s area %d tab %d: position label ends at y %d, the first row starts at %d", core, cv, area + 1, tab, p.pos_y2, p.rows_top);
    }
    // D9: no row has the second line "at the next reset"; the legend is there exactly when a row is marked
    bool any_marked = false;
    for (int i = 0; i < n; i++) {
        char l[120], v[120], sb[120]; bool a;
        r.m->row(i, l, sizeof(l), v, sizeof(v), &a, sb, sizeof(sb));
        S1CHECK(!strstr(sb, "next reset"), "%s area %d tab %d row %d \"%s\" has the second line \"%s\"", core, area + 1, tab, i, l, sb);
        if (sb[0]) { s1two++; }
        if (r.m->row_reset(i)) { any_marked = true; s1marked++; }
    }
    if (any_marked) {
        s1legends++;
        S1CHECK(r.m->status()[0] != 0 || r.m->foot_line()[0] != 0, "%s: no foot line (legend or the row's note) for a marked row", core);
    }
    if (shot_out && shot_tag) {
        char fn[512], nm[64];
        snprintf(nm, sizeof(nm), "%s", core);
        for (char *q = nm; *q; q++) { if (!isalnum((unsigned char)*q)) { *q = '_'; } }
        snprintf(fn, sizeof(fn), "%s/s1-%s-area%d-tab%d-%s.png", shot_out, nm, area + 1, tab, shot_tag);
        png(fn, 2);
    }
    (void)cw; (void)ch;
}

static void list_rules(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "480p", 640, 480 }, { "720p", 1280, 720 }, { "1080p", 1920, 1080 } };
    for (const CoreDesc *const *pc = core_table; *pc; pc++) {
        const CoreDesc *src = *pc;
        PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
        CoreDesc *d = board_desc(src, &env);
        char cfg[128];
        snprintf(cfg, sizeof(cfg), "/usb0/cores/s1_%s.cfg", src->cor_name);
        Rig r(d, cfg);
        for (int i = 0; i < d->nsettings; i++) {
            if (d->settings[i].kind == 3) { r.st->set_text(i, "/usb0/roms/some/folder/with/a/file.rom"); }
        }
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(d);
        s1cores++;
        for (const auto &cv : cvs) {
            TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
            ui_set_canvas(want.w, want.h);
            bool shots = !strcmp(cv.name, "1080p");
            bool seen[8][10] = {};
            for (int ak = 0; ak < 8; ak++) {
                for (int tk = 0; tk <= 9; tk++) {
                    r.m->open();
                    ui_menu_forget_rows();
                    r.m->key(KEY_F1 + ak);
                    if (tk) { r.m->key('0' + tk); }
                    int area = r.m->area(), tab = r.m->tab();
                    if (area < 0 || area > 7 || tab < 0 || tab > 9 || seen[area][tab]) { continue; }
                    seen[area][tab] = true;
                    ui_menu_render(*r.m);
                    lv_timer_handler();
                    UiListProbe p0;
                    if (!ui_list_probe(&p0)) { continue; }
                    int n = r.m->rows();
                    if (n < 2) { continue; }
                    s1lists++;
                    std::vector<char> visited(n, 0);
                    s1_state(src->cor_name, cv.name, area, tab, r, want.w, want.h, shots && n >= 10 ? out : 0, "top");
                    visited[r.m->selected()] = 1;
                    for (int g = 0; g < n + 4; g++) {                                  // down through the list ...
                        int before = r.m->selected();
                        r.m->key(KEY_DOWN);
                        bool mid = shots && n >= 10 && r.m->selected() == n / 2 && before != n / 2;
                        bool end = shots && n >= 10 && r.m->selected() == n - 1 && before != n - 1;
                        s1_state(src->cor_name, cv.name, area, tab, r, want.w, want.h, mid ? out : end ? out : 0, mid ? "mid" : "end");
                        visited[r.m->selected()] = 1;
                    }
                    for (int g = 0; g < n + 4; g++) {                                  // ... and up again
                        r.m->key(KEY_UP);
                        s1_state(src->cor_name, cv.name, area, tab, r, want.w, want.h, 0, 0);
                        visited[r.m->selected()] = 1;
                    }
                    int v = 0, nh = 0;
                    for (int i = 0; i < n; i++) { v += visited[i]; if (r.m->heading(i)) { nh++; } }
                    S1CHECK(v == n - nh, "%s %s area %d tab %d: the walk reached %d of %d rows (%d headings)", src->cor_name, cv.name, area + 1, tab, v, n - nh, nh);
                }
            }
        }
    }
    ui_set_canvas(960, 540);
    printf("S1 list rules: %d cores, %d lists, %d selection states, %d rows with a second line, %d marked rows, %d legends, %d failed\n",
           s1cores, s1lists, s1states, s1two, s1marked, s1legends, s1fails);
    CHECK(s1cores >= 18 && s1lists >= 18 && s1states > 500);
    CHECK(s1two > 0);                                                       // the control needs two-line rows to see: the files' paths
    CHECK(s1legends > 0 && s1marked > 0);
}

// ---- T187 D2, S3, S4, P4b: the page, the places, the focus of F8, the value's line -----------------------------------------------------------
// Per core, with the board's whole chain: (D2) a core with a hotkey map lists ONE row "Hotkeys >" in F5 / Input and none of the seven items; RETURN opens the
// page with the modifier and the six keys, ESC comes back to the same tab with the same selection and first row. (S3) every generic row (the routing
// table's) is in the same area and the same group (the heading above it, else the tab's name; a page entry "Input > Hotkeys") as in the C64 (the merged
// record). (S4) F8 takes its first focus on a row that is neither a heading nor "Reset to host defaults", which is its last row. (P4b) after a value
// changes in place, the value's middle is the label's middle.
static const int FKEYS[8] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };   // the key codes are not consecutive (keyboard.h)
struct UxLoc { std::string label; int area; std::string group; };   // area: the number of its function key (F5 = 5)
static int uxstor, uxdev, uxfails, uxcores, uxhot, uxcmp, uxskip, uxf8, uxvals;
#define UXCHECK(c, ...) do { int f0_ = fails; CHECK(c); if (fails > f0_) { uxfails += fails - f0_; printf("  UX context: " __VA_ARGS__); printf("\n"); } } while (0)

static void ux_rowtext(Rig &r, int i, std::string *l, std::string *v, bool *act)
{
    char a[160], b[160]; bool ac = false;
    r.m->row(i, a, sizeof(a), b, sizeof(b), &ac);
    *l = a; *v = b; *act = ac;
}

// The rows of the Hotkeys page by the names the page gives them (T187 gate 1: no "Hotkey:" prefix on a page that is called Hotkeys)
// T216 D11: a dialog task of a settings tab shows ">" like a page row; the pad-mapping tasks are the ones of that kind ("Map buttons, pad N", "Map port 2 buttons")
static bool is_map_task(const std::string &l) { return !strncmp(l.c_str(), "Map ", 4); }

static bool hk_page_label(const std::string &l)
{
    static const char *const n[] = { "Modifier", "Menu open / close", "Volume up", "Volume down", "Reset", "Swap joystick ports", "Picture mode", "Hotkey modifier" };
    for (const char *p : n) { if (l == p) { return true; } }
    return !strncmp(l.c_str(), "Hotkey:", 7);
}

// Every list of the menu: where each row is. A row that opens a page is followed in (and the way back is checked).
static void ux_collect(const char *core, Rig &r, std::vector<UxLoc> *out, int *nhot_rows, int *nopens, int *page_rows, int *hotkey_items_in_tab)
{
    bool seen[8][10] = {};
    for (int ak = 0; ak < 8; ak++) {
        for (int tk = 0; tk <= 9; tk++) {
            r.m->open();
            ui_menu_forget_rows();
            r.m->key(FKEYS[ak]);
            if (tk) { r.m->key('0' + tk); }
            int area = r.m->area(), tab = r.m->tab();
            if (area < 0 || area > 7 || tab < 0 || tab > 9 || seen[area][tab]) { continue; }
            seen[area][tab] = true;
            ui_menu_render(*r.m); lv_timer_handler();
            int n = r.m->rows();
            if (r.m->screen()) {                                               // a layout (the C64's own pages): its items, a heading names the group of the ones below
                std::string tn = r.m->title(), g = tn;
                for (int i = 0; i < n && n < 200; i++) {
                    MenuCtl::Item it;
                    r.m->item(i, &it);
                    if (it.kind == MenuCtl::IK_HEADING) { g = it.title; continue; }
                    if (it.kind == MenuCtl::IK_VALUE || it.kind == MenuCtl::IK_INFO || !it.title[0]) { continue; }
                    out->push_back({ it.title, ak + 1, g });
                    if (getenv("UXD_OFF")) { printf("  dbgc %s F%d \"%s\" / \"%s\" : %s\n", core, ak + 1, tn.c_str(), g.c_str(), it.title); }
                }
                continue;
            }
            std::string tname = r.m->title(), group = tname;
            for (int i = 0; i < n && n < 200; i++) {
                std::string l, v; bool act;
                ux_rowtext(r, i, &l, &v, &act);
                if (r.m->heading(i)) { group = l; continue; }
                out->push_back({ l, ak + 1, group });
                if (hk_page_label(l) && tname == "Input") { (*hotkey_items_in_tab)++; (void)nhot_rows; }
                if (v.size() >= 1 && v.back() == '>' && l == "Picture") {          // T187 D5: the Picture row opens the page of the modes (a pick stage, the tab stays); it is not a sub-page of the Hotkeys kind
                    r.m->open(); ui_menu_forget_rows();
                    r.m->key(FKEYS[ak]); if (tk) { r.m->key('0' + tk); }
                    select_row(r, i);
                    int sel1 = r.m->selected();
                    r.m->key(KEY_RETURN);
                    ui_menu_render(*r.m); lv_timer_handler();
                    UXCHECK(std::string(r.m->title()) == "Picture" && r.m->rows() >= 1, "%s: RETURN on \"Picture\" opened \"%s\"", core, r.m->title());
                    r.m->key(KEY_ESCAPE);
                    ui_menu_render(*r.m); lv_timer_handler();
                    UXCHECK(r.m->tab() == tab && r.m->selected() == sel1, "%s: ESC from the Picture page: tab %d sel %d, was tab %d sel %d", core, r.m->tab(), r.m->selected(), tab, sel1);
                    ux_rowtext(r, sel1, &l, &v, &act);
                } else if (v.size() >= 1 && v.back() == '>') {
                    int sel0, first0;
                    r.m->open(); ui_menu_forget_rows();
                    r.m->key(FKEYS[ak]); if (tk) { r.m->key('0' + tk); }
                    select_row(r, i);
                    ui_menu_render(*r.m); lv_timer_handler();
                    sel0 = r.m->selected(); first0 = r.m->first_hint();
                    r.m->key(KEY_RETURN);
                    ui_menu_render(*r.m); lv_timer_handler();
                    std::string pname = r.m->title();
                    if (r.m->tab() == tab) {                                  // T216 D11: a dialog task shows ">" too; RETURN opens its dialog and the tab stays - not a page
                        char dtx[200];
                        UXCHECK(strcmp(r.m->dialog(dtx, sizeof(dtx)), "none") != 0 || r.m->mapping() || r.m->status()[0], "%s: RETURN on \"%s\" opened neither a page nor a dialog", core, l.c_str());
                        for (int k = 0; k < 4 && (r.m->mapping() || strcmp(r.m->dialog(dtx, sizeof(dtx)), "none") != 0); k++) {     // leave the dialog or wizard the way a person does
                            r.m->key(KEY_ESCAPE);
                            ui_menu_render(*r.m); lv_timer_handler();
                        }
                        r.m->open(); ui_menu_forget_rows();                      // back to the list as it opens
                        r.m->key(FKEYS[ak]); if (tk) { r.m->key('0' + tk); }
                        ui_menu_render(*r.m); lv_timer_handler();
                        continue;
                    }
                    (*nopens)++;
                    int pn = r.m->rows(), cnt = 0;
                    for (int j = 0; j < pn; j++) {
                        std::string pl, pv; bool pa;
                        ux_rowtext(r, j, &pl, &pv, &pa);
                        if (r.m->heading(j)) { continue; }
                        cnt++;
                        out->push_back({ pl, ak + 1, tname + " > " + pname });
                    }
                    *page_rows = cnt;
                    r.m->key(KEY_ESCAPE);
                    ui_menu_render(*r.m); lv_timer_handler();
                    UXCHECK(r.m->tab() == tab && r.m->selected() == sel0 && r.m->first_hint() == first0,
                            "%s: ESC from the page \"%s\": tab %d sel %d first %d, was tab %d sel %d first %d", core, pname.c_str(), r.m->tab(), r.m->selected(), r.m->first_hint(), tab, sel0, first0);
                    ux_rowtext(r, sel0, &l, &v, &act);
                }
            }
        }
    }
}

static void ux_rules(void)
{
    ui_set_canvas(960, 540);
    PictureEnv env0 = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    std::vector<UxLoc> ref;
    {
        CoreDesc *rd = (CoreDesc *)trx64u::desc();                      // the C64's menu is its record (as the board loads it): no chain over it
        Rig rr(rd, "/flash/trx64u/ux_ref.cfg", trx64u::merge());
        rr.c.started = true; rr.c.program_loaded = true;
        ui_open(&c64_desc);
        int a = 0, b = 0, c = 0, d = 0;
        ux_collect("trx64u", rr, &ref, &a, &b, &c, &d);
        printf("UX reference (C64): %zu rows placed\n", ref.size());
        CHECK(ref.size() > 20);
    }
    for (const CoreDesc *const *pc = core_table; *pc; pc++) {
        const CoreDesc *src = *pc;
        CoreDesc *d = board_desc(src, &env0);
        char cfg[128];
        snprintf(cfg, sizeof(cfg), "/usb0/cores/ux_%s.cfg", src->cor_name);
        Rig r(d, cfg);
        r.c.started = true; r.c.program_loaded = true;
        ui_open(d);
        uxcores++;
        bool has_hk = false;
        for (int i = 0; i < d->nsettings; i++) { if (!strcasecmp(d->settings[i].key, HK_KEY_MOD)) { has_hk = true; } }
        std::vector<UxLoc> loc;
        int nh = 0, nopens = 0, prows = 0, hk_in_tab = 0;
        ux_collect(src->cor_name, r, &loc, &nh, &nopens, &prows, &hk_in_tab);
        // D2
        if (has_hk) {
            uxhot++;
            int hkwant = HK_NACT - 1 + 1 + (d->hotkeys && d->hotkeys->core_op ? 1 : 0);     // Modifier + the six common actions + the core's own
            UXCHECK(nopens == 1 && prows == hkwant, "%s: %d rows open a page, the page has %d rows (want 1 and %d)", src->cor_name, nopens, prows, hkwant);
            int ones = 0, items = 0;
            for (const UxLoc &u : loc) {
                if (u.label == "Hotkeys") { ones++; }
                if (u.group.find(" > ") == std::string::npos && u.group == "Input" && hk_page_label(u.label)) { items++; }
            }
            UXCHECK(ones == 1 && items == 0, "%s: %d rows \"Hotkeys >\" and %d hotkey items outside the page", src->cor_name, ones, items);
        } else {
            UXCHECK(nopens == 0, "%s: no hotkey map and %d rows open a page", src->cor_name, nopens);
        }
        // S3
        for (int i = 0; i < d->nsettings; i++) {
            if (!menu_route_of(d->settings[i])) { continue; }
            std::string lab = d->settings[i].label ? d->settings[i].label : d->settings[i].key;
            const UxLoc *a = 0, *b = 0;
            for (const UxLoc &u : loc) { if (u.label == lab) { a = &u; break; } }
            for (const UxLoc &u : ref) { if (u.label == lab) { b = &u; break; } }
            UXCHECK(a != 0, "%s: the generic row \"%s\" is on no page", src->cor_name, lab.c_str());
            if (!a) { continue; }
            if (lab == "Persist medium mounts" || lab == "Enter home on startup" || lab == "Home directory" || lab == "Menu opens at") {
                // D7 puts the storage rows in F5, group Storage. The C64's record is the firmware's (docs/contracts/trx64u-firmware-handover.md): it keeps them in
                // Drive A (F5) and Expert (F8), so the C64 is not the yardstick for these three; a core without other settings in F5 Settings lists them with no heading.
                uxstor++;
                if (b && (b->area != a->area || b->group != a->group)) { uxdev++; }
                // (T205 D1: with no other settings in the core's tab the rows have no heading, the group is then the tab's name - the core's; "Menu opens at" has the heading Menu)
                UXCHECK(a->area == 5 && a->group != "Video" && a->group != "Audio" && a->group.compare(0, 5, "Input") != 0, "%s: \"%s\" is in F%d group \"%s\", D7/T205 want the core's F5 tab", src->cor_name, lab.c_str(), a->area, a->group.c_str());
                continue;
            }
            // the C64 has no row of these labels (its record is the firmware's, with its own Settings area F6 and Video/Audio/Input pages): the C64 fixes the tab, named for the
            // route; since T205 D1 every other core lists the route's rows in F5 (the route's area), under the same tab name; the page's rows are in "<tab> > <page>"
            const MenuGroupRoute *rt = menu_route_of(d->settings[i]);
            int fk = atoi(rt->area + 1);
            int fref = atoi((rt->alt_area ? rt->alt_area : rt->area) + 1);
            std::string want = rt->page ? std::string(rt->heading) + " > " + rt->label : std::string(rt->heading);
            bool in_ref = false;
            for (const UxLoc &u : ref) { if (u.area == fref && u.group == rt->heading) { in_ref = true; break; } }
            if (strcmp(rt->group, "menu")) { UXCHECK(in_ref, "the C64 has no F%d tab \"%s\"", fref, rt->heading); }
            uxcmp++;
            UXCHECK(a->area == fk && a->group == want, "%s: \"%s\" is in F%d group \"%s\", the place is F%d \"%s\"", src->cor_name, lab.c_str(), a->area, a->group.c_str(), fk, want.c_str());
        }
        // S4
        {
            r.m->open(); ui_menu_forget_rows();
            r.m->key(KEY_F8);
            ui_menu_render(*r.m); lv_timer_handler();
            int n = r.m->rows(), sel = r.m->selected();
            std::string l, v; bool act;
            ux_rowtext(r, sel, &l, &v, &act);
            uxf8++;
            UXCHECK(!r.m->heading(sel) && l != "Reset to host defaults", "%s: F8 takes its first focus on \"%s\" (row %d of %d, heading %d)", src->cor_name, l.c_str(), sel, n, (int)r.m->heading(sel));
            ux_rowtext(r, n - 1, &l, &v, &act);
            UXCHECK(l == "Reset to host defaults", "%s: the last row of F8 is \"%s\"", src->cor_name, l.c_str());
        }
        // P4b: a value changed in place, in every list of F5
        for (int ak = 4; ak <= 4; ak++) {                       // F5
            for (int tk = 1; tk <= 9; tk++) {
                r.m->open(); ui_menu_forget_rows();
                r.m->key(FKEYS[ak]); r.m->key('0' + tk);
                if (r.m->screen()) { continue; }
                int n = r.m->rows();
                for (int i = 0; i < n && n < 40; i++) {
                    std::string l, v; bool act;
                    if (r.m->heading(i)) { continue; }
                    ux_rowtext(r, i, &l, &v, &act);
                    if (!act || v.empty() || v.back() == '>') { continue; }
                    select_row(r, i);
                    if (r.m->selected() != i) { continue; }
                    r.m->key(KEY_RIGHT);
                    ui_menu_render(*r.m); lv_timer_handler();
                    UiListProbe p;
                    if (!ui_list_probe(&p)) { continue; }
                    uxvals++;
                    UXCHECK(p.val_off_max <= 2 && p.nvals > 0, "%s F%d tab %d row \"%s\": after a value change the value is %d px off its label's middle (%d values)", src->cor_name, ak + 1, tk, l.c_str(), p.val_off_max, p.nvals);
                }
            }
        }
    }
    printf("UX rules: %d cores, %d with a hotkey map, %d generic rows compared with the C64 (%d not in the C64), %d storage rows in F5 (%d where the C64 record differs), %d F8 focuses, %d value changes, %d failed\n",
           uxcores, uxhot, uxcmp, uxskip, uxstor, uxdev, uxf8, uxvals, uxfails);
    CHECK(uxcores >= 18 && uxhot >= 1 && uxcmp > 60 && uxf8 == uxcores && uxvals > 20);
}

// ---- T187 UX gate 1 (R2, R3, R5, R6): the chrome and the lists at every canvas ---------------------------------------------------------------
// Every core, at 1080p, 720p and 480p: in each tab and page the legend fits the canvas (R2) and says BACK as every list does (R5); each rail item's label ends before its
// key (R2); no heading is printed twice in a list (R3); no row of the Hotkeys page carries the page's name as a prefix (R6).
static int ucn_f8, ucn_leg, ucn_rail, ucn_lists, ucn_pages, ucn_hk, ucn_cores, ucn_pcnames;
static bool g_pc_core;                                             // the core under test names the keys of the USB keyboard (HkSpec::pc_keys)
static void ux_chrome_check(const char *core, const char *cv, Rig &r, const char *where, bool page)
{
    ui_menu_render(*r.m); lv_timer_handler();
    const TrxTheme &t = trx_theme();
    char txt[300]; int tw = 0;
    if (trx_foot_probe(txt, sizeof(txt), &tw)) {
        ucn_leg++;
        UXCHECK(tw <= trx_canvas_w() - 2 * t.space_md, "%s %s %s: the legend \"%s\" is %d px wide, the canvas holds %d", core, cv, where, txt, tw, trx_canvas_w() - 2 * t.space_md);
        bool list = strstr(txt, "F1-F8 AREA") || strstr(txt, "CHOOSE");
        if (list) {
            UXCHECK(strstr(txt, "BACK") && !strstr(txt, "BACK TO") && (!page || strstr(txt, "ESC  BACK")), "%s %s %s: the way back is not worded \"BACK\": \"%s\"", core, cv, where, txt);
        }
    }
    for (int i = 0; i < 8; i++) {
        int lend, room, kx;
        if (!trx_nav_probe(i, &lend, &room, &kx)) { continue; }
        ucn_rail++;
        UXCHECK(kx - lend >= 3, "%s %s %s: rail item %d: the label ends at x %d, its key begins at x %d (gap %d, room for the label to %d)", core, cv, where, i, lend, kx, kx - lend, room);
    }
    std::vector<std::string> heads;
    if (r.m->area() == 7 && !page) {                                // F8: the tab and the title say System; no section repeats it
        std::string tn = r.m->tab_name(), ti = r.m->title();
        for (char &c : tn) { c = (char)toupper((unsigned char)c); }
        for (char &c : ti) { c = (char)toupper((unsigned char)c); }
        int n = r.m->rows();
        for (int i = 0; i < n && n < 200; i++) {
            std::string l, v; bool a;
            if (!r.m->heading(i)) { continue; }
            ux_rowtext(r, i, &l, &v, &a);
            for (char &c : l) { c = (char)toupper((unsigned char)c); }
            ucn_f8++;
            UXCHECK(l != tn && l != ti, "%s %s: F8 section heading \"%s\" repeats the tab/title \"%s\"", core, cv, l.c_str(), tn.c_str());
        }
    }
    if (r.m->screen()) {
        char hb[40][40];
        int nh = screens_headings(hb, 40);
        for (int i = 0; i < nh; i++) { heads.push_back(hb[i]); }
    } else {
        int n = r.m->rows();
        for (int i = 0; i < n && n < 200; i++) {
            std::string l, v; bool a;
            if (!r.m->heading(i)) { continue; }
            ux_rowtext(r, i, &l, &v, &a);
            for (char &c : l) { c = (char)toupper((unsigned char)c); }
            heads.push_back(l);
        }
    }
    ucn_lists++;
    for (size_t i = 0; i < heads.size(); i++) {
        for (size_t j = i + 1; j < heads.size(); j++) {
            UXCHECK(heads[i] != heads[j], "%s %s %s: the heading \"%s\" is printed twice in one list", core, cv, where, heads[i].c_str());
        }
    }
    if (page) {
        int n = r.m->rows();
        for (int i = 0; i < n && n < 200; i++) {
            std::string l, v; bool a;
            ux_rowtext(r, i, &l, &v, &a);
            ucn_hk++;
            UXCHECK(strncmp(l.c_str(), "Hotkey", 6) != 0, "%s %s: a row of the Hotkeys page is called \"%s\"", core, cv, l.c_str());
            if (g_pc_core) {                                       // R6: a core without a C64 keyboard shows no C64 keycap
                ucn_pcnames++;
                UXCHECK(!strstr(v.c_str(), "C=") && !strstr(v.c_str(), "RUN/STOP") && !strstr(v.c_str(), "RESTORE"), "%s %s: the Hotkeys page of a core without a C64 keyboard says \"%s\" for \"%s\"", core, cv, v.c_str(), l.c_str());
            }
        }
    }
}

static void ux_chrome(void)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "720p", 1280, 720 }, { "480p", 640, 480 } };
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        std::vector<const CoreDesc *> all;
        for (const CoreDesc *const *pc = core_table; *pc; pc++) { all.push_back(*pc); }
        all.push_back(0);                                          // the C64: its menu is the record the firmware loads
        {
            for (const CoreDesc *one : all) {
                bool c64 = one == 0;
                const CoreDesc *src = c64 ? trx64u::desc() : one;
                CoreDesc *d = c64 ? (CoreDesc *)src : board_desc(src, &env);
                const char *core = c64 ? "c64" : src->cor_name;
                ui_set_canvas(want.w, want.h);
                char cfg[128];
                snprintf(cfg, sizeof(cfg), "/usb0/cores/uxc_%s.cfg", core);
                Rig r(d, cfg, c64 ? trx64u::merge() : 0);
                r.c.started = true; r.c.program_loaded = true;
                ui_open(c64 ? &c64_desc : d);
                ucn_cores++;
                g_pc_core = !c64 && src->hotkeys && src->hotkeys->pc_keys;
                bool seen[8][10] = {};
                for (int ak = 0; ak < 8; ak++) {
                    for (int tk = 0; tk <= 9; tk++) {
                        r.m->open(); ui_menu_forget_rows();
                        r.m->key(FKEYS[ak]);
                        if (tk) { r.m->key('0' + tk); }
                        int area = r.m->area(), tab = r.m->tab();
                        if (area < 0 || area > 7 || tab < 0 || tab > 9 || seen[area][tab]) { continue; }
                        seen[area][tab] = true;
                        char where[40];
                        snprintf(where, sizeof(where), "F%d tab %d", area + 1, tab + 1);
                        ux_chrome_check(core, cv.name, r, where, false);
                        if (r.m->screen()) { continue; }
                        int n = r.m->rows();
                        for (int i = 0; i < n && n < 200; i++) {
                            std::string l, v; bool a;
                            ux_rowtext(r, i, &l, &v, &a);
                            if (r.m->heading(i) || v.empty() || v.back() != '>' || is_map_task(l)) { continue; }
                            r.m->open(); ui_menu_forget_rows();
                            r.m->key(FKEYS[ak]); if (tk) { r.m->key('0' + tk); }
                            select_row(r, i);
                            r.m->key(KEY_RETURN);
                            ucn_pages++;
                            snprintf(where, sizeof(where), "F%d page \"%s\"", area + 1, l.c_str());
                            ux_chrome_check(core, cv.name, r, where, true);
                            break;
                        }
                    }
                }
            }
        }
    }
    ui_set_canvas(960, 540);
    printf("UX chrome: %d cores x 3 canvases, %d legends, %d rail items, %d lists, %d pages, %d page rows, %d key names of non-C64 cores, %d failed\n", ucn_cores / 3, ucn_leg, ucn_rail, ucn_lists, ucn_pages, ucn_hk, ucn_pcnames, uxfails);
    CHECK(ucn_cores >= 3 * 18 && ucn_leg > 500 && ucn_rail > 1000 && ucn_pages >= 3 * 3 && ucn_hk > 30 && ucn_pcnames > 30);
}

// ---- T187 UX gate 1 (R6): the WiFi row of F8 in the states the person can be in --------------------------------------------------------------
// The bench's rig has no network service, so the row used to be shown only as its fallback. A core that registers the two states shows On / Off / No module and the
// line under the row (menuctl.cc, EK_WIFI); a core without them shows "No module", not "Not set".
static int g_wf_mode;                                              // 1 on, 2 off, 3 no module
static const CoreDesc *g_wf_orig;
static const char *wf_onoff(CoreCtx *) { return g_wf_mode == 1 ? "On" : g_wf_mode == 2 ? "Off" : "No module"; }
static const char *wf_line(CoreCtx *)
{
    return g_wf_mode == 1 ? "Connected, TRX-Net, 192.168.1.20" : g_wf_mode == 2 ? "Off, no network, no address" : "No module, no network, no address";
}
static void wf_reg(OpRegistry &r)
{
    if (g_wf_orig && g_wf_orig->register_ops) { g_wf_orig->register_ops(r); }
    r.add_state("wifi.onoff", wf_onoff);
    r.add_state("wifi.line", wf_line);
}
static int wfn;
static void ux_wifi(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "480p", 640, 480 } };
    static const struct { const char *tag; int mode; const char *val, *sub; } vars[] = {
        { "on", 1, "On", "Connected, TRX-Net, 192.168.1.20" },
        { "off", 2, "Off", "Off, no network, no address" },
        { "nomodule", 3, "No module", "No module, no network, no address" },
        { "absent", 0, "No module", "" },
    };
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        for (const auto &vr : vars) {
            ui_set_canvas(want.w, want.h);
            static CoreDesc wd;
            wd = *board_desc(&amiga1200_desc, &env);
            g_wf_orig = &amiga1200_desc;
            g_wf_mode = vr.mode;
            if (vr.mode) { wd.register_ops = wf_reg; }
            Rig r(&wd, "/usb0/cores/uxw.cfg");
            r.c.started = true; r.c.program_loaded = true;
            ui_open(&wd);
            r.m->open(); ui_menu_forget_rows();
            r.m->key(KEY_F8);
            ui_menu_render(*r.m); lv_timer_handler();
            int row = -1;
            std::string l, v; bool a; char sub[160] = "";
            for (int i = 0; i < r.m->rows(); i++) {
                char la[160], lb[160], ls[160] = "";
                bool ac;
                r.m->row(i, la, sizeof(la), lb, sizeof(lb), &ac, ls, sizeof(ls));
                if (!strcmp(la, "WiFi")) { row = i; v = lb; snprintf(sub, sizeof(sub), "%s", ls); break; }
            }
            (void)l; (void)a;
            wfn++;
            UXCHECK(row >= 0, "a1200 %s %s: no WiFi row in F8", cv.name, vr.tag);
            UXCHECK(v == vr.val && !strcmp(sub, vr.sub), "a1200 %s WiFi %s: row \"%s\" / line \"%s\", want \"%s\" / \"%s\"", cv.name, vr.tag, v.c_str(), sub, vr.val, vr.sub);
            char fn[512];
            snprintf(fn, sizeof(fn), "%s/ux-a1200-F8-wifi-%s-%s.png", out, vr.tag, cv.name);
            lv_obj_invalidate(lv_screen_active());
            lv_timer_handler();
            if (want.w == 960) { png(fn, 2); } else { png_out(fn, want.w, want.h, 0, 0, want.w, want.h); }
        }
    }
    ui_set_canvas(960, 540);
    printf("UX WiFi row: %d variants, %d failed\n", wfn, uxfails);
    CHECK(wfn == 8);
}

// ---- T187 UX gate pictures (item 8): the states the review looks at, at 1080p and 480p ------------------------------------------------------
// ---- T207 S4: the Hotkeys page of every core with a hotkey map, learning one: the page, the prompt of a key row, the prompt of the modifier row, the refusal and the
// learned line. Found by name through the menu tree (the modifier row is the page's first), not by area and tab number. At 1080p and 480p; the status line must lie
// below the last drawn row and inside the pane, the legend fits the canvas, nothing is cut. Pictures for three cores of three families.
static int hkl_cores, hkl_states;
static void hotkey_learn(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "480p", 640, 480 } };
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        for (const CoreDesc *const *pc = core_table; *pc; pc++) {
            const CoreDesc *src = *pc;
            if (!src->hotkeys) { continue; }
            CoreDesc *d = board_desc(src, &env);
            const char *core = src->cor_name;
            bool pic = src == &vc20_desc || src == &c128_desc || src == &atari800_desc || strstr(core, "1200");   // VIC-20, C128, Atari 800, A1200: three families
            char fn[96];
            snprintf(fn, sizeof(fn), "%s", core);
            for (char *q = fn; *q; q++) { if (!isalnum((unsigned char)*q)) { *q = '_'; } }
            ui_set_canvas(want.w, want.h);
            char cfg[128];
            snprintf(cfg, sizeof(cfg), "/usb0/cores/hkl_%s.cfg", core);
            Rig r(d, cfg);
            r.c.started = true; r.c.program_loaded = true;
            ui_open(d);
            hkl_cores++;
            int pa = -1, pp = -1;
            for (int a = 0; a < r.m->tree().nareas && pa < 0; a++) {
                for (int t = 0; t < r.m->tree().a[a].ntabs; t++) {
                    const MenuTab &tb = r.m->tree().a[a].tabs[t];
                    if (tb.page && tb.count > 0 && r.m->tree().e[tb.first].kind == EK_SETTING && !strcasecmp(r.c.desc->settings[r.m->tree().e[tb.first].index].key, HK_KEY_MOD)) { pa = a; pp = tb.parent; break; }
                }
            }
            CHECK(pa >= 0 && pp >= 0);
            if (pa < 0 || pp < 0) { continue; }
            r.m->open(); ui_menu_forget_rows();
            r.m->key(FKEYS[r.m->tree().a[pa].key[1] - '1']); r.m->key('0' + r.m->tree().a[pa].tabs[pp].n);
            for (int i = 0; i < r.m->rows(); i++) {
                std::string l, v; bool a;
                ux_rowtext(r, i, &l, &v, &a);
                if (l == "Hotkeys") { select_row(r, i); break; }
            }
            r.m->key(KEY_RETURN);
            CHECK(r.m->rows() >= 3 && std::string(r.m->title()) != "Input");
            auto shot = [&](bool with_status, const char *state) {
                ui_menu_render(*r.m); lv_timer_handler();
                char w[96], p[512];
                snprintf(w, sizeof(w), "%s %s hotkeys %s", core, cv.name, state);
                CHECK(view_bounds(w, want.w, want.h) == 0);
                ux_chrome_check(core, cv.name, r, state, false);
                UiListProbe lp;
                int sx1, sy1, sx2, sy2; char st[200];
                if (ui_status_probe(&sx1, &sy1, &sx2, &sy2, st, sizeof(st)) && with_status) {
                    CHECK(st[0]);
                    CHECK(ui_list_probe(&lp));
                    UXCHECK(lp.rows_top + lp.drawn_h <= sy1 + 1, "%s %s %s: the status \"%s\" at y %d begins above the end of the rows (%d)", core, cv.name, state, st, sy1, lp.rows_top + lp.drawn_h);
                    UXCHECK(sy2 <= lp.pane_h && sx1 >= 0 && sx2 <= trx_content_w(), "%s %s %s: the status \"%s\" [%d,%d - %d,%d] leaves the pane (%d x %d)", core, cv.name, state, st, sx1, sy1, sx2, sy2, trx_content_w(), lp.pane_h);
                    UXCHECK(trx_text_w(st, trx_theme().caption) <= sx2 - sx1, "%s %s %s: the status \"%s\" is cut", core, cv.name, state, st);
                }
                hkl_states++;
                if (pic) {
                    lv_obj_invalidate(lv_screen_active());          // the whole page into the picture: fb is shared between cores and LVGL repaints only what changed
                    lv_timer_handler();
                    snprintf(p, sizeof(p), "%s/hotkeys-%s-%s-%s.png", out, fn, cv.name, state);
                    if (want.w == 960) { png(p, 2); } else { png_out(p, want.w, want.h, 0, 0, want.w, want.h); }
                }
            };
            shot(false, "page");
            auto press = [&](int k) { r.io.key(k, true); r.io.delay_ms(20); r.m->pump(true); r.io.key(k, false); r.io.delay_ms(20); r.m->pump(true); };
            // the modifier row: RETURN asks for a modifier key; ESC takes it back
            select_row(r, 0);
            r.m->key(KEY_RETURN);
            CHECK(r.m->learning());
            { std::string l, v; bool a; ux_rowtext(r, r.m->selected(), &l, &v, &a); CHECK(l == "Press the new modifier key"); }
            shot(false, "modifier-prompt");
            r.m->key(KEY_ESCAPE);
            CHECK(!r.m->learning());
            // a key row (the first after the modifier): the prompt, a refusal, the learned line
            select_row(r, 1);
            r.m->key(KEY_RETURN);
            CHECK(r.m->learning());
            { std::string l, v; bool a; ux_rowtext(r, r.m->selected(), &l, &v, &a); CHECK(l == "Press the new key"); }
            shot(false, "key-prompt");
            HkMap hm = hk_map_of(&r.c);
            int used = -1;
            for (int a = 2; a < HK_NACT && used < 0; a++) { if (hm.act[a] != HK_NONE && hm.act[a] != 63 && hm.act[a] != 64) { used = hm.act[a]; } }
            CHECK(used >= 0);
            if (used >= 0) {
                press(used);
                CHECK(!r.m->learning() && r.m->status_bad());
                shot(true, "refused");
            }
            int fresh = -1;
            for (int k = 0; k < 60 && fresh < 0; k++) {
                bool taken = k == hm.mod_a || k == hm.mod_b;
                for (int a = 0; a < HK_NACT; a++) { taken = taken || hm.act[a] == k; }
                if (!taken) { fresh = k; }
            }
            r.m->key(KEY_RETURN);
            CHECK(r.m->learning());
            press(fresh);
            CHECK(!r.m->learning() && !r.m->status_bad() && r.m->status()[0]);
            shot(true, "learned");
        }
    }
    ui_set_canvas(960, 540);
    printf("hotkey learn: %d cores x 2 canvases, %d states\n", hkl_cores / 2, hkl_states);
    CHECK(hkl_cores >= 2 * 3);
}

static void ux_shots(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "480p", 640, 480 } };
    struct St { const char *tag; int key; const char *tabname; int what; };   // what: 0 top, 1 end, 2 page of the first row that opens one
    auto go = [&](Rig &r, const St &st) {
        r.m->open(); ui_menu_forget_rows();
        r.m->key(st.key);
        if (st.tabname) {
            for (int tk = 1; tk <= 9; tk++) {
                r.m->open(); ui_menu_forget_rows();
                r.m->key(st.key); r.m->key('0' + tk);
                if (strstr(r.m->tab_name(), st.tabname)) { break; }
            }
        }
        ui_menu_render(*r.m); lv_timer_handler();
        if (st.what == 1) { r.m->key(KEY_END); }
        if (st.what == 2) {
            for (int i = 0; i < r.m->rows(); i++) {
                std::string l, v; bool a;
                ux_rowtext(r, i, &l, &v, &a);
                if (!r.m->heading(i) && !v.empty() && v.back() == '>' && !is_map_task(l)) { select_row(r, i); r.m->key(KEY_RETURN); break; }
            }
        }
        ui_menu_render(*r.m); lv_timer_handler();
    };
    struct Job { const CoreDesc *src; bool c64; const char *core; St st[8]; int n; };   // T205: F5 is the core's one settings area (tabs: the core, Video, Audio, Input)
    static const Job jobs[] = {
        { &amiga1200_desc, false, "a1200", { { "F5-core-top", KEY_F5, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, { "F5-audio", KEY_F5, "Audio", 0 },
                                             { "F5-input", KEY_F5, "Input", 0 }, { "hotkeys", KEY_F5, "Input", 2 }, { "F1-home", KEY_F1, 0, 0 }, { "F8-system", KEY_F8, 0, 0 } }, 8 },
        { &vc20_desc, false, "vc20", { { "F1-home", KEY_F1, 0, 0 }, { "F5-core-top", KEY_F5, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 },
                                       { "F5-audio", KEY_F5, "Audio", 0 }, { "F5-input", KEY_F5, "Input", 0 }, { "F8-system", KEY_F8, 0, 0 } }, 7 },
        { &c128_desc, false, "c128", { { "F1-home", KEY_F1, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, { "F5-audio", KEY_F5, "Audio", 0 },
                                       { "F5-input", KEY_F5, "Input", 0 }, { "F8-system", KEY_F8, 0, 0 } }, 6 },
        { &atari800_desc, false, "atari", { { "F5-core-top", KEY_F5, 0, 0 } }, 1 },
        { &apple2_desc, false, "apple2", { { "F8-system", KEY_F8, 0, 0 } }, 1 },
        { 0, true, "c64", { { "F5-drive-a", KEY_F5, "Drive A", 0 } }, 1 },
    };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        for (const Job &j : jobs) {
            ui_set_canvas(want.w, want.h);
            PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
            const CoreDesc *src = j.c64 ? trx64u::desc() : j.src;
            CoreDesc *d = j.c64 ? (CoreDesc *)src : board_desc(src, &env);
            char cfg[128];
            snprintf(cfg, sizeof(cfg), "/usb0/cores/uxs_%s.cfg", j.core);
            Rig r(d, cfg, j.c64 ? trx64u::merge() : 0);
            r.c.started = true; r.c.program_loaded = true;
            ui_open(j.c64 ? &c64_desc : d);
            for (int k = 0; k < j.n; k++) {
                go(r, j.st[k]);
                char fn[512];
                snprintf(fn, sizeof(fn), "%s/ux-%s-%s-%s.png", out, j.core, j.st[k].tag, cv.name);
                lv_obj_invalidate(lv_screen_active());          // the whole page into the picture, as the other benches do: fb is shared between jobs and LVGL repaints only what changed
                lv_timer_handler();
                if (want.w == 960) { png(fn, 2); } else { png_out(fn, want.w, want.h, 0, 0, want.w, want.h); }
            }
        }
    }
    ui_set_canvas(960, 540);
}


// ---- T205 ux-review finding 1: a 31-character file name on a Home media row (DF0) at 480p and 1080p: the value is cut in the middle, the slot label stays whole ------------
static void ux_longname()
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "480p", 640, 480 } };
    static const char *LONG = "/usb0/games/Turrican_II_The_Final_Fight_disk1.adf";           // the file's name is 31 characters
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        ui_set_canvas(want.w, want.h);
        CoreDesc *d = board_desc(&amiga1200_desc, &env);
        Rig r(d, "/usb0/cores/uxl.cfg");
        std::vector<uint8_t> img(901120, 0);
        r.fs.put(LONG, img);
        r.c.started = true; r.c.program_loaded = true;
        ui_open(d);
        OpIn in[2] = { in_str("path", LONG), in_int("slot", 0) };
        OpResult res;
        CHECK(ops_call(&r.c, "window.mount", in, 2, &res) == 0);
        r.m->open(); ui_menu_forget_rows();
        r.m->key(KEY_F1);
        ui_menu_render(*r.m); lv_timer_handler();
        int found = 0;
        for (int i = 0; i < r.m->rows(); i++) {
            char l[200], v[200], sub[200] = ""; bool a;
            r.m->row(i, l, sizeof(l), v, sizeof(v), &a, sub, sizeof(sub));
            if (!strcmp(l, "DF0")) { found = 1; UXCHECK(strstr(v, "Turrican_II_The_Final_Fight_disk1.adf") != 0, "long name: row DF0 holds \"%s\"", v); }
        }
        UXCHECK(found, "long name %s: no DF0 row on Home", cv.name);
    }
    ui_set_canvas(960, 540);
}

// ---- T187 D5 fixtures: the Picture page on a screen that names 6, 4, 1 modes, none (no EDID) and a corrupt EDID -------------------------------------------------------
// The same table as the host bench (tests/hostapp/render/render.cc, pmfix). A PAL machine: the 50 Hz family and the PC rows. The EDID goes through the functions the board runs
// (video_modes_offered, edid_parse); the page is reached by the person's keys (F5 Video, the Picture row, RETURN).
static int g_pm_applies;
void video_apply(int, int) { g_pm_applies++; }
bool video_apply_gateware(int, int) { g_pm_applies++; return true; }
static void pm_make_edid(uint8_t *e, int *len, const uint8_t *vics, int nv, unsigned pc, bool bad)
{
    memset(e, 0, 256);
    static const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memcpy(e, hdr, 8);
    if (pc & 1) { e[35] |= 0x01; }
    if (pc & 2) { e[36] |= 0x08; }
    for (int i = 0; i < 8; i++) { e[38 + 2 * i] = 0x01; e[39 + 2 * i] = 0x01; }
    if (pc & 4) { e[38] = (uint8_t)(1280 / 8 - 31); e[39] = (uint8_t)(2 << 6); }       // 1280 x 1024 (5:4), 60 Hz
    e[126] = nv ? 1 : 0;
    unsigned t = 0;
    for (int i = 0; i < 127; i++) { t += e[i]; }
    e[127] = (uint8_t)(0x100 - (t & 0xFF));
    if (bad) { e[127] ^= 0x5A; }
    *len = 128;
    if (nv) {
        uint8_t *x = e + 128;
        x[0] = 0x02; x[1] = 0x03;
        int q = 4;
        x[q++] = (uint8_t)((2 << 5) | nv);
        for (int i = 0; i < nv; i++) { x[q++] = vics[i]; }
        x[2] = (uint8_t)q;
        unsigned u = 0;
        for (int i = 0; i < 127; i++) { u += x[i]; }
        x[127] = (uint8_t)(0x100 - (u & 0xFF));
        *len = 256;
    }
}
struct PmFx { const char *name; uint8_t vics[3]; int nv; unsigned pc; bool bad, none; int n; int list[6]; unsigned nat; int autohdmi; const char *info; };
static void pm_page_case(const char *out, const CoreDesc *src, const char *core, const PmFx &f)
{
    // the canvas is the mode's: picture_start puts it up as the board does (576p 720 x 576, 720p 1280 x 720, 1080p 1920 x 1080)
    int fw = f.autohdmi == VID_HDMI_480_576 ? 720 : f.autohdmi == VID_HDMI_720P ? 1280 : 1920, fh = f.autohdmi == VID_HDMI_480_576 ? 576 : f.autohdmi == VID_HDMI_720P ? 720 : 1080;
    const char *shell = f.autohdmi == VID_HDMI_480_576 ? "576p" : f.autohdmi == VID_HDMI_720P ? "720p" : "1080p";
    char p[512], what[160];
    uint8_t edid[256];
    int len = 0;
    if (f.none) { memset(edid, 0, sizeof(edid)); } else { pm_make_edid(edid, &len, f.vics, f.nv, f.pc, f.bad); }
    EdidInfo ei;
    PictureEnv env = { video_modes_offered(edid, len), 0, true, VID_SYS_PAL50 };
    env.native = len > 0 && edid_parse(edid, len, &ei) ? (uint16_t)(ei.native & env.mask) : 0;
    TrxCanvas want = trx_canvas_for(fw, fh);
    CoreDesc *d = board_desc(src, &env);
    snprintf(p, sizeof(p), "/usb0/cores/pmfx_%s_%s.cfg", core, f.name);
    Rig r(d, p);
    r.c.started = true; r.c.program_loaded = true;
    ui_open(d);
    picture_start(&r.c, VID_SYS_PAL50, f.autohdmi);
    r.vid->init();
    CHECK(trx_canvas_w() == want.w && trx_canvas_h() == want.h);
    r.m->open();
    r.m->key(KEY_F5); r.m->key('2');
    int row = -1;
    for (int i = 0; i < r.m->rows(); i++) { std::string l, v; bool a; ux_rowtext(r, i, &l, &v, &a); if (l == "Picture") { row = i; } }
    snprintf(what, sizeof(what), "%s %s %s", core, f.name, shell);
    CHECK(row >= 0);
    if (row < 0) { return; }
    select_row(r, row);
    g_pm_applies = 0;
    r.m->key(KEY_RIGHT); r.m->key(KEY_LEFT);                            // 7: the closed row is no stepper
    CHECK(g_pm_applies == 0 && !picture_confirm_pending());
    r.m->key(KEY_RETURN);
    ui_menu_render(*r.m); lv_timer_handler();
    CHECK(std::string(r.m->title()) == "Picture" && r.m->selected() == 0);
    CHECK(r.m->rows() == 1 + f.n);
    CHECK(!strcmp(r.m->status(), f.info));                               // 6: the info line, as the spec words it
    char leg[120];
    r.m->footer_legend(leg, sizeof(leg));
    CHECK(strstr(leg, "RETURN  USE THIS MODE") && strstr(leg, "BROWSE"));  // the key bar
    CHECK(!r.m->toast_text()[0]);                                       // R2: no chip over the question that is not there yet
    int nnow = 0;
    for (int i = 0; i < r.m->rows() && i <= f.n; i++) {                 // 1: Auto + the list in order; the table's refresh; native from the EDID; Auto is the choice: one "now" (T187 D9), on Auto's row
        std::string l, v; bool a;
        ux_rowtext(r, i, &l, &v, &a);
        int hdmi = i == 0 ? f.autohdmi : f.list[i - 1];
        if (i == 0) { CHECK(l.compare(0, 4, "Auto") == 0); } else { CHECK(l == video_hdmi_name(hdmi)); }
        int rw = hdmi <= VID_HDMI_1080P ? hdmi : 6 + hdmi;
        const EdidRow *er = edid_row(rw);
        uint64_t tot = (uint64_t)er->htotal * er->vtotal;
        unsigned dh = (unsigned)(((uint64_t)er->pixel_hz * 10 + tot / 2) / tot);
        bool nat = (f.nat >> rw) & 1, now = i == 0;
        char ex[64];
        snprintf(ex, sizeof(ex), "%s%s%s%s%u.%u Hz", now ? "now" : "", now && nat ? " " : "", nat ? "native" : "", (now || nat) ? "   " : "", dh / 10, dh % 10);
        UXCHECK(v == ex, "%s: row %d says \"%s\", expected \"%s\"", what, i, v.c_str(), ex);
        nnow += v.find("now") != std::string::npos;
    }
    CHECK(nnow == 1 && r.m->selected() == 0);                           // D9: one mark, on the chosen row (Auto); the cursor opened on it
    for (int i = 0; i <= f.n; i++) {                                    // 2, 3: every cursor position: the page lies inside the canvas, nothing is cut
        select_row(r, i);
        char w2[200];
        snprintf(w2, sizeof(w2), "%s cursor %d", what, i);
        CHECK(view_bounds(w2, want.w, want.h) == 0);
    }
    select_row(r, 0);
    for (int i = 0; i <= f.n; i++) { r.m->key(KEY_DOWN); }              // 4: browsing applies nothing
    for (int i = 0; i <= f.n; i++) { r.m->key(KEY_UP); }
    CHECK(g_pm_applies == 0 && !picture_confirm_pending() && r.st->value(picture_index(d)) == 0);
    ui_menu_render(*r.m); lv_timer_handler();
    lv_obj_invalidate(lv_screen_active()); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/pm-%s-%s-%s.png", out, core, f.name, shell);
    { PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 32, 114, 960, 540 } : r.px.place.back(); png_out(p, fw, fh, g.x, g.y, g.w, g.h); }
    int trow = -1;
    for (int i = 0; i < f.n; i++) { if (f.list[i] != f.autohdmi) { trow = i + 1; break; } }
    if (trow < 0) {                                                     // one mode: RETURN on it (the mode in force) closes the page and applies nothing
        r.m->key(KEY_DOWN); r.m->key(KEY_RETURN);
        CHECK(std::string(r.m->title()) != "Picture" && g_pm_applies == 0 && !picture_confirm_pending());
        return;
    }
    select_row(r, trow);
    r.m->key(KEY_RETURN);                                               // 4: RETURN on a mode: one apply, the question pending
    CHECK(g_pm_applies == 1 && picture_confirm_pending());
    char nw[96], pv[96];
    picture_confirm_names(nw, sizeof(nw), pv, sizeof(pv));
    char want_now[96], want_prev[96];
    {
        int rn = f.list[trow - 1] <= VID_HDMI_1080P ? f.list[trow - 1] : 6 + f.list[trow - 1], rp = f.autohdmi;
        const EdidRow *en = edid_row(rn), *ep = edid_row(rp);
        uint64_t tn = (uint64_t)en->htotal * en->vtotal, tp = (uint64_t)ep->htotal * ep->vtotal;
        unsigned dn = (unsigned)(((uint64_t)en->pixel_hz * 10 + tn / 2) / tn), dp = (unsigned)(((uint64_t)ep->pixel_hz * 10 + tp / 2) / tp);
        snprintf(want_now, sizeof(want_now), "%s, %u.%u Hz", video_hdmi_name(f.list[trow - 1]), dn / 10, dn % 10);
        snprintf(want_prev, sizeof(want_prev), "%s, %u.%u Hz", video_hdmi_name(f.autohdmi), dp / 10, dp % 10);
    }
    CHECK(!strcmp(nw, want_now) && !strcmp(pv, want_prev));             // 5: the question names the new mode and the one that comes back
    ui_picture_question(8, nw, pv);
    lv_timer_handler();
    lv_obj_invalidate(lv_screen_active()); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/pm-%s-%s-%s-question.png", out, core, f.name, shell);
    { PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 32, 114, 960, 540 } : r.px.place.back(); png_out(p, fw, fh, g.x, g.y, g.w, g.h); }
    picture_confirm_revert();                                           // LEFT / ESC: back at once (the next service pass puts the mode back)
    picture_service(&r.c);
    CHECK(!picture_confirm_pending() && r.st->value(picture_index(d)) == 0);
    const char *res = picture_take_result();
    char want_back[120];
    snprintf(want_back, sizeof(want_back), "Picture: back to %s", want_prev);
    CHECK(res && !strcmp(res, want_back));                              // the chip after the revert names the mode that came back
    ui_message_clear();
    r.m->say(res);
    ui_menu_render(*r.m);
    CHECK(!strcmp(ui_chip_say_text(), res));                            // T187 F2: the chip holds the whole message, not its first 31 characters
    lv_timer_handler();
    r.m->key(KEY_LEFT);                                                 // the page closes, nothing more is applied
    int a1 = g_pm_applies;
    r.m->key(KEY_ESCAPE);
    CHECK(g_pm_applies == a1);
}
static void pm_page_fixtures(const char *out)
{
    static const PmFx fx[] = {
        { "six",     { 17, 19, 159 }, 3, 7u, false, false, 6, { 0, 1, 2, 3, 4, 5 }, 1u << 2, 2, "Your screen offers 6 modes. The 60 Hz ones are listed too: the core runs at 50 Hz." },
        { "four",    { 17, 159, 0 },  2, 3u, false, false, 4, { 0, 2, 3, 4 },       1u << 2, 2, "Your screen offers 4 modes. The 60 Hz ones are listed too: the core runs at 50 Hz." },
        { "one",     { 147, 0, 0 },   1, 0u, false, false, 1, { 1 },                1u << 1, 1, "Your screen offers 1 mode. Nothing changes until you pick one." },
        { "none",    { 0, 0, 0 },     0, 0u, false, true,  3, { 0, 1, 2 },          0,       0, "The screen did not name its modes; the standard TV modes are listed." },
        { "corrupt", { 17, 19, 159 }, 3, 7u, true,  false, 3, { 0, 1, 2 },          0,       0, "The screen did not name its modes; the standard TV modes are listed." },
    };
    for (const PmFx &f : fx) {
        pm_page_case(out, &amiga1200_desc, "a1200", f);
        pm_page_case(out, &nes_desc, "nes", f);
    }
    ui_set_canvas(960, 540);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/coreapp-render";
    char p[512];

    CoreDesc *atari_main = board_desc(&atari800_desc);
    Rig r(atari_main, "/usb0/cores/atari800.cfg");
    r.fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
    r.fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
    r.fs.put_text("/usb0/games/arkanoid.atr", "x");
    r.fs.put_text("/usb0/games/boulderdash.atr", "x");
    r.fs.put_text("/usb0/games/notes.txt", "x");
    r.c.started = true;
    r.c.program_loaded = true;

    ui_open(atari_main);
    {   // T209 (UX review): the middle cut keeps a visible start and end at every real width, never a bare "..."
        const char *names[] = { "Turrican_II_The_Final_Fight_Director_Cut_Disk_1_of_3_Fixed.d64", "Zak_McKracken_and_the_Alien_Mindbenders_Disk_1_Side_A.d64",
                                "Last_Ninja_Remix_(1990)(System_3)[cr Fairlight][t +3].d64", "Amiga 1200 (AGA, 2 MB chip, 8 MB fast)" };
        const TrxTheme &th = trx_theme();
        for (int ni = 0; ni < 4; ni++) {
            for (int w = 120; w <= 520; w += 8) {
                char nb[64];
                const char *o = trx_fit_mid(names[ni], w, th.body, nb, sizeof(nb));
                CHECK(strcmp(o, "...") != 0);
                CHECK(trx_text_w(o, th.body) <= w);
            }
        }
        for (int ni = 0; ni < 2; ni++) {                       // the row as the Library builds it: the label the user sees holds the cut name, whole, inside the row
            char nb[64], title[100];
            const int rw = 504;
            snprintf(title, sizeof(title), "%s", trx_fit_mid(names[ni], rw - 32 - trx_text_w("171 KB", th.caption) - th.space_md - 8, th.body, nb, sizeof(nb)));
            TrxStatusRow sr = { title, "171 KB", TT_VIOLET, false, false };
            lv_obj_t *row = trx_status_row(lv_screen_active(), 0, 0, rw, sr, 0);
            lv_obj_update_layout(row);
            for (uint32_t ci = 0; ci < lv_obj_get_child_count(row); ci++) {
                lv_obj_t *ch = lv_obj_get_child(row, (int32_t)ci);
                if (!lv_obj_check_type(ch, &lv_label_class) || !strcmp(lv_label_get_text(ch), "171 KB")) continue;
                printf("LIBROW title '%s' label text '%s' w %d box %d\n", title, lv_label_get_text(ch), trx_text_w(lv_label_get_text(ch), th.body), (int)lv_obj_get_width(ch));
                CHECK(strcmp(lv_label_get_text(ch), title) == 0);
                CHECK(trx_text_w(lv_label_get_text(ch), th.body) <= lv_obj_get_width(ch));
                CHECK(strncmp(lv_label_get_text(ch), "...", 3) != 0);
            }
            lv_obj_delete(row);
        }
    }
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
        Rig j(board_desc(&atari800_desc), "/usb0/cores/atari800j.cfg");
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
        // per page: the pane without the line, then the key and the line (the chip in the top bar must change pixels, the rows none), then the line run out (the pane as before).
        Rig v(board_desc(&atari800_desc), "/usb0/cores/atari800v.cfg");
        v.c.started = true;
        v.m->open();
        const int pages[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F7 };
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
            size_t shown = 0, card = 0;                                        // T187 D4/S2: the message is a chip in the top bar (y < 36); where the card used to stand nothing changes
            for (int y = 0; y < 34; y++) for (int x = 150; x < 960; x++) shown += fb[y * 960 + x] != before[y * 960 + x];
            if (pk != KEY_F5) {                                                // (F5 may list the Volume row, whose value changes with the key)
                int bx0 = 999, bx1 = -1, by0 = 999, by1 = -1;
                for (int y = 450; y < 490; y++) for (int x = 200; x < 760; x++) {
                    if (fb[y * 960 + x] != before[y * 960 + x]) { card++; bx0 = x < bx0 ? x : bx0; bx1 = x > bx1 ? x : bx1; by0 = y < by0 ? y : by0; by1 = y > by1 ? y : by1; }
                }
                if (card) { printf("CARDPIX F%d: %zu px in [%d,%d - %d,%d] tab '%s' title '%s' status '%s'\n", pk - KEY_F1 + 1, card, bx0, by0, bx1, by1, v.m->tab_name(), v.m->title(), v.m->status()); }
            }
            snprintf(p, sizeof(p), "%s/volume_toast_F%d.png", out, pk - KEY_F1 + 1);
            png(p, 2);
            CHECK(shown > 100);                                              // the chip with its text, whatever the page behind it
            CHECK(card == 0);                                                // and no card over the rows
            v.io.t += 5000;
            CHECK(v.m->toast_expire());
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
        }
    }

    {   // T181, T187 D2: the hotkey items (modifier and one key per action) are 7 rows of the Hotkeys page (opened from F5 / Input, tab 4) of a core with a default map.
        // The same core with and without them: the opened page differs by the rows, F8 System is the same pixels (it lists none of them).
        static CoreDesc hd;
        static Setting hs[SETTINGS_MAX];
        CHECK(!hk_extend(&atari800_desc, &hd, hs));
        CHECK(hd.nsettings == atari800_desc.nsettings + 1 + HK_NACT - 1);
        Rig base(&atari800_desc, "/usb0/cores/atari800hb.cfg");
        Rig hot(&hd, "/usb0/cores/atari800hk.cfg");
        base.c.started = true; hot.c.started = true;
        base.m->open(); hot.m->open();
        static uint16_t without[960 * 540];
        // item 6: the same core, the page closed (its F5 Input tab, the row "Hotkeys >") against the page open
        auto differ = [&](int page, int tab) {
            hot.m->open(); ui_menu_forget_rows();
            int pa = -1, pp = -1;                                              // the tab that holds the page, found through the tree (T205: not tab 4 of F5 any more)
            for (int a = 0; a < hot.m->tree().nareas && pa < 0 && tab; a++) {
                for (int tt = 0; tt < hot.m->tree().a[a].ntabs; tt++) {
                    const MenuTab &tb = hot.m->tree().a[a].tabs[tt];
                    if (tb.page && tb.count > 0 && hot.m->tree().e[tb.first].kind == EK_SETTING && !strcasecmp(hot.c.desc->settings[hot.m->tree().e[tb.first].index].key, HK_KEY_MOD)) { pa = a; pp = tb.parent; break; }
                }
            }
            if (tab && pa >= 0) { page = FKEYS[hot.m->tree().a[pa].key[1] - '1']; tab = hot.m->tree().a[pa].tabs[pp].n; }
            hot.m->key(page); if (tab) hot.m->key('0' + tab);
            ui_menu_render(*hot.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            if (tab) {                                                         // the Hotkeys row opens its page (found by name: T205 put other rows above it)
                for (int i = 0; i < hot.m->rows(); i++) {
                    char l[200], v[200]; bool a;
                    hot.m->row(i, l, sizeof(l), v, sizeof(v), &a);
                    if (!strcmp(l, "Hotkeys")) { select_row(hot, i); break; }
                }
                hot.m->key(KEY_RETURN);
            }
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t diff = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) diff += fb[y * 960 + x] != without[y * 960 + x];
            return diff;
        };
        auto differ8 = [&]() {                                               // F8 System of the core without the items against the one with them: the same pixels
            base.m->key(KEY_F8);
            ui_menu_render(*base.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            hot.m->key(KEY_F8);
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t diff = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) diff += fb[y * 960 + x] != without[y * 960 + x];
            return diff;
        };
        size_t f8 = differ8();
        size_t f5 = differ(KEY_F5, 4);
        printf("hotkey rows: F5 Hotkeys page %zu pixels differ, F8 System %zu\n", f5, f8);
        snprintf(p, sizeof(p), "%s/hotkeys_F5_page.png", out);
        png(p, 2);
        CHECK(f5 > 3000);                                                    // 7 rows of about 42 px at 812 px wide, text only
        CHECK(f8 == 0);
        {   // T207 S4: RETURN on the Reset row asks for the key (entry_ is up, the row says what it waits for); ESC takes it back
            memcpy(without, fb, sizeof(without));
            for (int i = 0; i < hot.m->rows(); i++) {                          // the Reset row by its name: the page's first row is not the same since T205
                char l[200], v[200]; bool a;
                hot.m->row(i, l, sizeof(l), v, sizeof(v), &a);
                if (!hot.m->heading(i) && strstr(l, "Reset")) { select_row(hot, i); break; }
            }
            ui_menu_render(*hot.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            hot.m->key(KEY_RETURN);
            CHECK(hot.m->learning());
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t d = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) d += fb[y * 960 + x] != without[y * 960 + x];
            printf("hotkey learn prompt: %zu pixels differ from the page\n", d);
            CHECK(d > 500);
            snprintf(p, sizeof(p), "%s/hotkeys_F6_learn.png", out);
            png(p, 2);
            hot.m->key(KEY_ESCAPE);
            CHECK(!hot.m->learning());
        }
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
    r.m->key(KEY_F5);                                        // the core's settings tab: rows, so DOWN has somewhere to go
    r.m->key('1');
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
    r.m->key('1');
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
        r.m->key('1');
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
    r.m->key('1');
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
    list_rules(out);
    ux_rules();
    ux_chrome();
    hotkey_learn(out);
    ux_shots(out);
    ux_longname();
    pm_page_fixtures(out);
    ux_wifi(out);
    printf("coreapp render: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
