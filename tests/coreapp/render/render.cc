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
#include "model_grid.h"
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
#include "record_menu.h"
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
extern const CoreDesc c128_desc;
extern const CoreDesc vc20_desc;
extern const CoreDesc amiga1200_desc;
extern const CoreDesc amiga_desc;
extern const CoreDesc ddt_novic_desc;
#include "picture.h"
#include "volume.h"
#include "videomode.h"
#include "menubuild.h"
#include "screens.h"
#include "cores.h"
#include "diag_examples.h"
#include "table.cc"                                           // T187: the board's own core table (every core), not a short copy of it
#include "own_cores.h"                                        // T213.3 (T239.5 D13): the cores with an app of their own (tests/coreapp/srclist.sh), not in that table
// every core the release ships: the board's table and the cores with an app of their own, as one list for the walks below
static const CoreDesc *const *every_core()
{
    static const CoreDesc *t[64];
    if (!t[0]) {
        int n = 0;
        for (const CoreDesc *const *pc = core_table; *pc && n < 62; pc++) { t[n++] = *pc; }
        for (const CoreDesc *const *pc = own_cores; *pc && n < 63; pc++) { t[n++] = *pc; }
        t[n] = 0;
    }
    return t;
}

#include "menu_rig.h"                                         // T236: the native layer, PNG, CHECK, Built and Rig, shared with tools/menusim
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
static lv_obj_t *find_label(lv_obj_t *o, const char *prefix)   // the first visible label whose text starts with prefix
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) { return 0; }
    if (lv_obj_check_type(o, &lv_label_class)) { const char *tx = lv_label_get_text(o); if (tx && !strncmp(tx, prefix, strlen(prefix))) { return o; } }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) { lv_obj_t *r = find_label(lv_obj_get_child(o, (int32_t)i), prefix); if (r) { return r; } }
    return 0;
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
// The Home page's "Session" panel has no picture area (Alex 2026-10-29: the picture-in-picture, T24.1, is not built): no large empty box with the screen's
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

// The row of the browser that is the file `name`, -1 when there is none.
static int find_file_row(Rig &c, const char *name)
{
    for (int i = 0; i < c.m->rows(); i++) {
        char l[120], r[120]; bool a;
        c.m->row(i, l, sizeof(l), r, sizeof(r), &a);
        if (!strcmp(l, name)) return i;
    }
    return -1;
}

static RecordMenu *g_cut;                                           // the record core of main()

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
    area_case("F7 actions", key_px(c, KEY_F7), SCREEN, &c);                    // T212 D12: APP has no tab strip, so the pane's height changes and the whole screen is built (518,400 px measured 2026-11-12)
    area_case("F8 system", key_px(c, KEY_F8), SCREEN, &c);                     // T212 D12: back from the strip-less APP area: the pane changes height again
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

// T235: the Picture value of a resolution at 50 Hz on a PAL machine (the list is Auto and every row of the table, the machine's rate first within a resolution)
static int pic_value50(int hdmi) { static const int v[VID_HDMI_COUNT] = { 1, 3, 5, 7, 8, 9 }; return v[hdmi]; }

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
        { "1080", pic_value50(VID_HDMI_1080P), 1920, 1080 },
        { "720", pic_value50(VID_HDMI_720P), 1280, 720 },
        { "after-switch-1080", pic_value50(VID_HDMI_1080P), 1920, 1080 },
        { "800x600", pic_value50(VID_HDMI_800x600), 800, 600 },
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
            {   // T212 (the drive rows and the machine line lengthen Home): the last row of Home stands inside the panel that holds it, at 480p and at the full canvas
                char l[120], v[120]; bool ap;
                int nr = r.m->rows();
                r.m->row(nr - 1, l, sizeof(l), v, sizeof(v), &ap);
                lv_obj_t *lb = find_label(lv_screen_active(), l);
                CHECK(lb != 0);
                if (lb) {
                    lv_area_t ra, pa;
                    lv_obj_t *row = lv_obj_get_parent(lb), *panel = lv_obj_get_parent(row);
                    lv_obj_get_coords(row, &ra);
                    lv_obj_get_coords(panel, &pa);
                    printf("HOME %s: %d rows, last row \"%s\" y %d..%d, panel y %d..%d\n", k == 0 ? "480p" : "full", nr, l, (int)ra.y1, (int)ra.y2, (int)pa.y1, (int)pa.y2);
                    CHECK(ra.y2 <= pa.y2);
                }
            }
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
    // T164 WP16: the Picture page of a core that is not the A1200, on a screen that names one PC mode (a monitor): the list is Auto and every row of the table (T235; the PC mode is
    // listed at 60 Hz only, the screen names 1024 x 768), Scale and Smoothing are there for every frame-buffer core. The page as the monitor gets it in 1024 x 768.
    {
        static CoreDesc nes_ext;
        PictureEnv pc = { (uint16_t)(1u << 10), 0, true, VID_SYS_PAL50 };
        nes_ext = *board_desc(&nes_desc, &pc);
        int pi = picture_index(&nes_ext), si = scale_index(&nes_ext), mi = smooth_index(&nes_ext);
        CHECK(pi >= 0 && si >= 0 && mi >= 0);
        CHECK(nes_ext.settings[pi].nvalues == 10 && !strcmp(nes_ext.settings[pi].values[pic_value50(VID_HDMI_1024x768)], "1024 x 768 (60 Hz)"));
        Rig n(&nes_ext, "/usb0/cores/nes.cfg");
        n.c.started = true;
        n.c.program_loaded = true;
        ui_open(&nes_ext);
        picture_start(&n.c, VID_SYS_PAL50, VID_HDMI_1080P);
        n.vid->init();
        n.st->set(pi, pic_value50(VID_HDMI_1024x768));
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

// T234: the Frame follow row of both Amiga cores on the F5 Video page (tab 2) next to Picture, Scale and Smoothing, selected, at the default Off
static void follow_menu(const char *out)
{
    static CoreDesc d[2];
    const CoreDesc *src[2] = { &amiga_desc, &amiga1200_desc };
    const char *name[2] = { "a500", "a1200" };
    for (int k = 0; k < 2; k++) {
        PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50, false, false, true };
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
        r.m->key('2');
        ui_menu_render(*r.m);
        lv_timer_handler();
        int row = -1, nvideo = 0;
        for (int i = 0; i < r.m->rows(); i++) {
            char l[120], rr[120], sb[160]; bool a;
            r.m->row(i, l, sizeof(l), rr, sizeof(rr), &a, sb, sizeof(sb));
            if (!strcmp(l, "Picture") || !strcmp(l, "Scale") || !strcmp(l, "Smoothing")) nvideo++;
            if (!strcmp(l, "Frame follow")) { row = i; CHECK(!strncmp(rr, "Off", 3)); }
        }
        CHECK(row >= 0);
        CHECK(nvideo == 3);                                                 // D5: the row stands with the other picture rows
        if (row >= 0) select_row(r, row);
        ui_menu_render(*r.m);
        lv_timer_handler();
        lv_obj_invalidate(lv_screen_active());
        lv_timer_handler();
        PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 0, 0, 960, 540 } : r.px.place.back();
        CHECK(view_bounds(name[k], 960, 540) == 0);
        char p[512]; snprintf(p, sizeof(p), "%s/%s-settings-frame-follow.png", out, name[k]);
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
    const struct { const CoreDesc *d; const char *name; } cores[] = { { &ddt_novic_desc, "ddt_novic" } };
    for (const auto &k : cores) {
        CoreDesc *kb = board_desc(k.d);
        Rig r(kb, "/usb0/cores/ddt.cfg");
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(kb);
        r.m->open();
        r.m->key(KEY_F1);
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
        for (int k = 0; k < DT_AV_N - 1; k++) { v.m->key(KEY_DOWN); }       // the selection on the last row (FB): the page scrolls to the sync rows
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
        int pa = 0, pb = 0, pc = 0;                                             // the counter counts settings and information lines: rows hidden that are only headings leave "1-13 of 13"
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
        S1CHECK(r.m->status()[0] != 0 || !strcmp(r.m->foot_line(), MenuCtl::reset_legend()) || (r.m->sel_detail()[0] && !strcmp(r.m->foot_line(), r.m->sel_detail())), "%s: the foot of a marked row is neither the ring legend nor the selected row's note", core);   // T216 D10: one line, the row's note or else the legend
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
    for (const CoreDesc *const *pc = every_core(); *pc; pc++) {
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
    CHECK(s1cores >= 17 && s1lists >= 17 && s1states > 500);   // the 17 cores: the board's table (15) and the VIC-20 and the A1200 with an app of their own (T239.3 P1-8: no C64)
    CHECK(s1two > 0);                                                       // the control needs two-line rows to see: the files' paths
    CHECK(s1legends > 0 && s1marked > 0);
}

// ---- T187 D2, S3, S4, P4b: the page, the places, the focus of F8, the value's line -----------------------------------------------------------
// Per core, with the board's whole chain: (D2) a core with a hotkey map lists ONE row "Hotkeys >" in F5 / Input and none of the seven items; RETURN opens the
// page with the modifier and the six keys, ESC comes back to the same tab with the same selection and first row. (S3) every generic row (the routing
// table's) is in the area and the group its route names (the heading above it, else the tab's name; a page entry "Input > Hotkeys"). (S4) F8 takes its first focus on a row that is neither a heading nor "Reset to host defaults", which is its last row. (P4b) after a value
// changes in place, the value's middle is the label's middle.
static const int FKEYS[8] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };   // the key codes are not consecutive (keyboard.h)
struct UxLoc { std::string label; int area; std::string group; std::string tab; };   // area: the number of its function key (F5 = 5)
static int uxstor, uxfails, uxcores, uxhot, uxcmp, uxf8, uxvals;
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
                    out->push_back({ it.title, ak + 1, g, tn });
                    if (getenv("UXD_OFF")) { printf("  dbgc %s F%d \"%s\" / \"%s\" : %s\n", core, ak + 1, tn.c_str(), g.c_str(), it.title); }
                }
                continue;
            }
            std::string tname = r.m->title(), group = tname;
            for (int i = 0; i < n && n < 200; i++) {
                std::string l, v; bool act;
                ux_rowtext(r, i, &l, &v, &act);
                if (r.m->heading(i)) { group = l; continue; }
                out->push_back({ l, ak + 1, group, tname });
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
                        out->push_back({ pl, ak + 1, tname + " > " + pname, tname });
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
    for (const CoreDesc *const *pc = every_core(); *pc; pc++) {
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
        const char *swap_key = d->hotkeys && d->hotkeys->swap_key ? d->hotkeys->swap_key : "Joystick ports";
        bool has_swap = false;
        for (int i = 0; i < d->nsettings; i++) { has_swap = has_swap || !strcasecmp(d->settings[i].key, swap_key); }
        std::vector<UxLoc> loc;
        int nh = 0, nopens = 0, prows = 0, hk_in_tab = 0;
        ux_collect(src->cor_name, r, &loc, &nh, &nopens, &prows, &hk_in_tab);
        // D2
        if (has_hk) {
            uxhot++;
            int hkwant = HK_NACT - 1 + 1 + (d->hotkeys && d->hotkeys->core_op ? 1 : 0);     // Modifier + the six common actions + the core's own
            if (!has_swap) {
                hkwant--;                                      // T228 R20: the swap hotkey only where the core has the setting it toggles
            }
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
            const UxLoc *a = 0;
            for (const UxLoc &u : loc) { if (u.label == lab) { a = &u; break; } }
            if (!a && !has_swap && lab == "Swap joystick ports") {
                continue;                                      // T228 R20: hidden on purpose where the core has no setting to swap
            }
            UXCHECK(a != 0, "%s: the generic row \"%s\" is on no page", src->cor_name, lab.c_str());
            if (!a) { continue; }
            if (lab == "Persist medium mounts" || lab == "Enter home on startup" || lab == "Home directory" || lab == "Menu opens at") {
                // T212 D1/D6: the storage and menu rows are the app's: F7 (APP), under the headings Storage / Menu, never the machine's F5.
                uxstor++;
                const MenuGroupRoute *sr = menu_route_of(d->settings[i]);
                const UxLoc *sa = 0;
                for (const UxLoc &u : loc) { if (u.label == lab && u.area == 7) { sa = &u; break; } }
                UXCHECK(sa != 0 && sa->group == sr->heading, "%s: \"%s\" is in F%d group \"%s\", T212 wants F7 group \"%s\"", src->cor_name, lab.c_str(), a->area, a->group.c_str(), sr->heading);
                for (const UxLoc &u : loc) { UXCHECK(!(u.label == lab && u.area == 5), "%s: \"%s\" is still in F5 group \"%s\"", src->cor_name, lab.c_str(), u.group.c_str()); }
                continue;
            }
            // the route names the area and the tab of the row; every core lists the route's rows in F5 (the route's area), under the same tab name; the page's rows are in "<tab> > <page>"
            const MenuGroupRoute *rt = menu_route_of(d->settings[i]);
            int fk = atoi(rt->area + 1);
            // T212 D12: a page of the app's area F7 is reached from the section "Settings" (the strip is hidden, the title stays): "Settings > <page>"
            std::string want = rt->page ? std::string("Settings > ") + rt->label : std::string(rt->heading);
            for (const UxLoc &u : loc) { if (u.label == lab && u.area == fk) { a = &u; break; } }   // a label that also stands on Home (Reset) is looked up in the route's own area
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
            // T212 D3/D7: the Status section (Build, ROMs, Libraries - information, nothing to press) closes F8 behind the Reset section; the last row that acts is the reset
            std::string tail[3];
            for (int k = 0; k < 3; k++) { ux_rowtext(r, n - 3 + k, &tail[k], &v, &act); UXCHECK(!act, "%s: F8 closes with \"%s\", an information row must not act", src->cor_name, tail[k].c_str()); }
            UXCHECK(tail[0] == "Build" && tail[1] == "ROMs" && tail[2] == "Libraries", "%s: the last rows of F8 are \"%s\", \"%s\", \"%s\"", src->cor_name, tail[0].c_str(), tail[1].c_str(), tail[2].c_str());
            ux_rowtext(r, n - 5, &l, &v, &act);
            UXCHECK(l == "Reset to host defaults" && act && r.m->heading(n - 4), "%s: F8 has \"%s\" where the reset stands (row %d of %d)", src->cor_name, l.c_str(), n - 5, n);
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
    printf("UX rules: %d cores, %d with a hotkey map, %d generic rows compared with their route, %d storage rows in F7, %d F8 focuses, %d value changes, %d failed\n",
           uxcores, uxhot, uxcmp, uxstor, uxf8, uxvals, uxfails);
    CHECK(uxcores >= 17 && uxhot >= 1 && uxcmp > 60 && uxf8 == uxcores && uxvals > 20);
}

// ---- T187 UX gate 1 (R2, R3, R5, R6): the chrome and the lists at every canvas ---------------------------------------------------------------
// Every core, at 1080p, 720p and 480p: in each tab and page the legend fits the canvas (R2) and says BACK as every list does (R5); each rail item's label ends before its
// key (R2); no heading is printed twice in a list (R3); no row of the Hotkeys page carries the page's name as a prefix (R6).
static int ucn_strip, ucn_strip_skip, ucn_f8, ucn_leg, ucn_rail, ucn_lists, ucn_pages, ucn_hk, ucn_cores, ucn_pcnames;
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
    {
        int right, room, nt;                                        // T228 DC12: a strip must end inside the canvas; only a strip of more than 4 tabs that overflows is exempt (7 tabs on F5 of trx64u below 960), and it is said
        if (trx_strip_probe(&right, &room, &nt)) {
            if (right > room && nt > 4) {
                if (ucn_strip_skip++ == 0) { printf("note: %s %s %s: %d-tab strip ends at x %d, the canvas holds %d (DC12: skipped, first of its kind)\n", core, cv, where, nt, right, room); }
            } else {
                ucn_strip++;
                UXCHECK(right <= room, "%s %s %s: the tab strip ends at x %d, the canvas holds %d", core, cv, where, right, room);
            }
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
        for (const CoreDesc *const *pc = every_core(); *pc; pc++) { all.push_back(*pc); }
        {
            for (const CoreDesc *one : all) {
                const CoreDesc *src = one;
                CoreDesc *d = board_desc(src, &env);
                const char *core = src->cor_name;
                ui_set_canvas(want.w, want.h);
                char cfg[128];
                snprintf(cfg, sizeof(cfg), "/usb0/cores/uxc_%s.cfg", core);
                Rig r(d, cfg);
                r.c.started = true; r.c.program_loaded = true;
                ui_open(d);
                ucn_cores++;
                g_pc_core = src->hotkeys && src->hotkeys->pc_keys;
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
                            {                                      // T216 D11: a task row shows ">" too (zxnext "F3: 50 / 60 Hz", a key task in F5 since T212 D8): RETURN opens a dialog or runs the task with a toast, no page, the area legend stays
                                char dtx[200];
                                if (r.m->status()[0] && !r.m->mapping() && !strcmp(r.m->dialog(dtx, sizeof(dtx)), "none")) { continue; }     // a task ran and said so (a toast), no page
                                if (r.m->mapping() || strcmp(r.m->dialog(dtx, sizeof(dtx)), "none") != 0) {
                                    for (int k = 0; k < 4 && (r.m->mapping() || strcmp(r.m->dialog(dtx, sizeof(dtx)), "none") != 0); k++) { r.m->key(KEY_ESCAPE); ui_menu_render(*r.m); lv_timer_handler(); }
                                    continue;
                                }
                            }
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
    printf("UX chrome: %d cores x 3 canvases, %d tab strips (%d skipped, DC12), %d legends, %d rail items, %d lists, %d pages, %d page rows, %d key names of cores with a PC keyboard, %d failed\n", ucn_cores / 3, ucn_strip, ucn_strip_skip, ucn_leg, ucn_rail, ucn_lists, ucn_pages, ucn_hk, ucn_pcnames, uxfails);
    // The floors are what the walk gives. Cores: the core table holds 17 (coreapp/src/table.cc), each at 3 canvases. Before T239.3 P1-8 this walk had 19: the table's 18
    // with the generic C64 descriptor c64_desc, and the C64 once more as TRX64U's menu record (`all.push_back(0)`); P1-8 removed both. CI run 45343698388 (the last
    // with both) counted 477 legends, run 45371924902 (the first without) 417: 45 were the record's (15 views per canvas), 15 the generic descriptor's. The other counts
    // of run 45371924902 against their floors: 321 tab strips (> 300), 2085 rail items (> 1000), 45 pages (>= 9), 306 page rows (> 30), 222 key names of cores with a PC
    // keyboard (> 30; TRX64U never was one).
    CHECK(ucn_cores >= 3 * 17 && ucn_leg >= 417 && ucn_rail > 1000 && ucn_pages >= 3 * 3 && ucn_strip > 300 && ucn_hk > 30 && ucn_pcnames > 30);
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
        for (const CoreDesc *const *pc = every_core(); *pc; pc++) {
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
    struct Job { const CoreDesc *src; const char *core; St st[8]; int n; };   // T205: F5 is the core's one settings area (tabs: the core, Video, Audio, Input)
    static const Job jobs[] = {
        { &amiga1200_desc, "a1200", { { "F5-core-top", KEY_F5, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, { "F5-audio", KEY_F5, "Audio", 0 },
                                             { "F5-input", KEY_F5, "Input", 0 }, { "hotkeys", KEY_F5, "Input", 2 }, { "F1-home", KEY_F1, 0, 0 }, { "F8-system", KEY_F8, 0, 0 } }, 8 },
        { &vc20_desc, "vc20", { { "F1-home", KEY_F1, 0, 0 }, { "F5-core-top", KEY_F5, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 },
                                       { "F5-audio", KEY_F5, "Audio", 0 }, { "F5-input", KEY_F5, "Input", 0 }, { "F8-system", KEY_F8, 0, 0 } }, 7 },
        { &c128_desc, "c128", { { "F1-home", KEY_F1, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, { "F5-audio", KEY_F5, "Audio", 0 },
                                       { "F5-input", KEY_F5, "Input", 0 }, { "F8-system", KEY_F8, 0, 0 } }, 6 },
        { &atari800_desc, "atari", { { "F5-core-top", KEY_F5, 0, 0 } }, 1 },
        { &apple2_desc, "apple2", { { "F8-system", KEY_F8, 0, 0 } }, 1 },
    };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        for (const Job &j : jobs) {
            ui_set_canvas(want.w, want.h);
            PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
            CoreDesc *d = board_desc(j.src, &env);
            char cfg[128];
            snprintf(cfg, sizeof(cfg), "/usb0/cores/uxs_%s.cfg", j.core);
            Rig r(d, cfg);
            r.c.started = true; r.c.program_loaded = true;
            ui_open(d);
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

// ---- T228 area shots (the click dummy's pictures, Restart 2026-11-17): every area and every tab of the A1200 (board_desc, the board's chain), at 1080p (the 960 x 540 canvas, 2x) and 480p:
// the top of each list and every further window when it is longer than its window (the last one "end"), the Library inside a folder with a file selected, and the pages the concept names (the drive list, Hotkeys, FTP
// hosts), R5's SID pages and R6's Network settings and Streams. Files mock-<core>-<area>-<tab>-<state>-<canvas>.png and mock-MANIFEST.json in `out`; a MOCK line per picture and a ROW line per row of each list.
struct MockShot { std::string file, core, area, area_label, tab_label, state, canvas; int tab_index; };
static std::vector<MockShot> g_mock;
static int mock_overlaps, mock_bounds;

static std::string mock_slug(const char *s)
{
    std::string o;
    for (const char *p = s; p && *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch)) { o += (char)tolower(ch); } else if (!o.empty() && o.back() != '-') { o += '-'; }
    }
    while (!o.empty() && o.back() == '-') { o.pop_back(); }
    return o.empty() ? std::string("tab") : o;
}

static std::string mock_json(const std::string &s)
{
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; } else if ((unsigned char)ch < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", ch); o += b; } else { o += ch; }
    }
    return o;
}

static void mock_rows(Rig &r, const char *tag)
{
    for (int i = 0; i < r.m->rows(); i++) {
        std::string l, v; bool a;
        ux_rowtext(r, i, &l, &v, &a);
        printf("ROW %s %2d %s%s%s%s\n", tag, i, r.m->heading(i) ? "[H] " : "", l.c_str(), v.empty() ? "" : " | ", v.c_str());
    }
}

static void mock_shot(Rig &r, const char *out, const char *core, const char *cv, int fw, int fh, int tab_index, const char *tab_label, const std::string &slug,
                      const char *state)
{
    const MenuTree &tr = r.m->tree();
    int a = r.m->area();
    const char *key = a >= 0 && a < tr.nareas ? tr.a[a].key : "F?";
    const char *alabel = a >= 0 && a < tr.nareas && tr.a[a].name ? tr.a[a].name : "";
    char base[300], fn[800], what[340];
    snprintf(base, sizeof(base), "mock-%s-%s-%s-%s-%s.png", core, key, slug.c_str(), state, cv);
    snprintf(fn, sizeof(fn), "%s/%s", out, base);
    snprintf(what, sizeof(what), "mock %s %s %s %s %s", core, key, slug.c_str(), state, cv);
    ui_menu_render(*r.m);
    lv_timer_handler();
    lv_obj_invalidate(lv_screen_active());          // the whole page into the picture: fb is shared between the rigs and LVGL repaints only what changed
    lv_timer_handler();
    if (fw == 960) { png(fn, 2); } else { png_out(fn, fw, fh, 0, 0, fw, fh); }
    mock_overlaps += txt_overlaps(what);
    mock_bounds += view_bounds(what, fw, fh);
    UiListProbe p;
    bool lp = ui_list_probe(&p);
    printf("MOCK %s %s %s tab %d \"%s\" %s: tab \"%s\", %d rows, window first %d, selected %d, paged %d\n", core, cv, key, tab_index, tab_label, state,
           r.m->tab_name(), r.m->rows(), lp ? p.first : -1, r.m->selected(), r.m->paged() ? 1 : 0);
    g_mock.push_back({ base, core, key, alabel, tab_label, state, cv, tab_index });
}

// The windows of a list longer than its window, after its first picture: KEY_DOWN moves the selection one row and the window follows it; a window is shot
// when it has moved on to the last three rows of the one shot before, so no row is left out ("p2", "p3", ...), and the window with the last row is `endname`.
// A list the probe does not read (a layout of its own) gets its end as KEY_END leaves it.
static void mock_windows(Rig &r, const char *out, const char *core, const char *cv, int fw, int fh, int ti, const char *tl, const std::string &slug,
                         const char *prefix, const char *endname)
{
    UiListProbe p;
    if (!ui_list_probe(&p)) {
        r.m->key(KEY_END);
        mock_shot(r, out, core, cv, fw, fh, ti, tl, slug, endname);
        return;
    }
    int shot_first = p.first, shot_n = p.nrows, cur = p.first, sel = r.m->selected(), n = 0;
    bool end_shot = false;
    for (int k = 0; k < 200; k++) {
        r.m->key(KEY_DOWN);
        ui_menu_render(*r.m); lv_timer_handler();
        if (!ui_list_probe(&p)) { break; }
        bool at_end = r.m->selected() == sel || r.m->selected() >= r.m->rows() - 1;
        sel = r.m->selected();
        cur = p.first;
        if (at_end) {
            if (cur != shot_first) { mock_shot(r, out, core, cv, fw, fh, ti, tl, slug, endname); end_shot = true; }
            break;
        }
        if (cur >= shot_first + shot_n - 3) {
            char st[32];
            snprintf(st, sizeof(st), "%sp%d", prefix, n + 2);
            mock_shot(r, out, core, cv, fw, fh, ti, tl, slug, st);
            n++;
            shot_first = cur;
            shot_n = p.nrows;
        }
    }
    if (!end_shot && n > 0) {                                       // the last window shot is the end: its file and its manifest entry say so
        MockShot &s = g_mock.back();
        char base[300];
        snprintf(base, sizeof(base), "mock-%s-%s-%s-%s-%s.png", core, s.area.c_str(), slug.c_str(), endname, cv);
        rename((std::string(out) + "/" + s.file).c_str(), (std::string(out) + "/" + base).c_str());
        printf("MOCK renamed %s -> %s\n", s.file.c_str(), base);
        s.file = base;
        s.state = endname;
    } else if (!end_shot) {
        r.m->key(KEY_END);
        mock_shot(r, out, core, cv, fw, fh, ti, tl, slug, endname);
    }
}

static int mock_find_row(Rig &r, const char *label, bool prefix)
{
    for (int i = 0; i < r.m->rows(); i++) {
        std::string l, v; bool a;
        if (r.m->heading(i)) {
            continue;                                          // a heading of the same name (F7 HOTKEYS) is not the row that opens the page
        }
        ux_rowtext(r, i, &l, &v, &a);
        if (prefix ? !strncmp(l.c_str(), label, strlen(label)) : l == label) { return i; }
    }
    return -1;
}

static void mock_shots(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "480p", 640, 480 } };
    struct File { const char *path; size_t n; };
    struct Page { const char *area; const char *tab; const char *row; bool prefix; const char *slug; };
    struct Job { const char *core; const CoreDesc *src; File files[8]; int nfiles; Page pages[8]; int npages; const char *pick; };
    static const Job jobs[] = {
        { "a1200", &amiga1200_desc,
          { { "/usb0/games/Turrican_II.adf", 901120 }, { "/usb0/games/Lotus_III.adf", 901120 }, { "/usb0/games/Workbench.hdf", 1048576 }, { "/usb0/games/notes.txt", 100 } }, 4,
          { { "F1", "home", "DF0", true, "df0" }, { "F7", "settings", "Hotkeys", false, "hotkeys" } }, 2, "Turrican" },
    };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        for (const Job &j : jobs) {
            ui_set_canvas(want.w, want.h);
            PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
            CoreDesc *d = board_desc(j.src, &env);
            char cfg[128];
            snprintf(cfg, sizeof(cfg), "/usb0/cores/mock_%s_%s.cfg", j.core, cv.name);
            Rig r(d, cfg);
            for (int f = 0; f < j.nfiles; f++) { r.fs.put_size(j.files[f].path, j.files[f].n); }
            r.c.started = true;
            r.c.program_loaded = true;
            ui_open(d);
            const MenuTree &tr = r.m->tree();
            bool seen[MENU_AREAS_MAX][MENU_TABS_MAX] = {};
            for (int ak = 0; ak < 8; ak++) {
                char fk[4];
                snprintf(fk, sizeof(fk), "F%d", ak + 1);
                for (int tk = 0; tk <= 9; tk++) {
                    r.m->open(); ui_menu_forget_rows();
                    r.m->key(FKEYS[ak]);
                    if (tk) { r.m->key('0' + tk); }
                    int a = r.m->area(), b = r.m->tab();
                    if (a < 0 || a >= tr.nareas || a >= MENU_AREAS_MAX || strcmp(tr.a[a].key, fk) || b < 0 || b >= tr.a[a].ntabs || tr.a[a].tabs[b].page || seen[a][b]) {
                        continue;
                    }
                    seen[a][b] = true;
                    ui_menu_render(*r.m); lv_timer_handler();
                    const char *tl = tr.a[a].tabs[b].name ? tr.a[a].tabs[b].name : "";
                    int ti = ui_shell_tab(*r.m) + 1;
                    std::string slug = mock_slug(tl);
                    char tag[96];
                    snprintf(tag, sizeof(tag), "%s %s %s/%s", j.core, cv.name, fk, slug.c_str());
                    mock_shot(r, out, j.core, cv.name, want.w, want.h, ti, tl, slug, "top");
                    if (!strcmp(cv.name, "1080p")) { mock_rows(r, tag); }
                    if (r.m->paged()) {
                        mock_windows(r, out, j.core, cv.name, want.w, want.h, ti, tl, slug, "", "end");
                    }
                    auto back_to_tab = [&]() {
                        r.m->open(); ui_menu_forget_rows();
                        r.m->key(FKEYS[ak]);
                        if (tk) { r.m->key('0' + tk); }
                        ui_menu_render(*r.m); lv_timer_handler();
                        return r.m->area() == a && r.m->tab() == b;
                    };
                    if (!strcmp(fk, "F3") && slug == "browse" && back_to_tab()) {             // the Library inside a folder: the media level first, then usb0, then games
                        int u = mock_find_row(r, "usb0", true);
                        if (u < 0) { u = mock_find_row(r, "USB", true); }
                        if (u < 0) { u = 0; }
                        select_row(r, u);
                        r.m->key(KEY_RETURN);
                        ui_menu_render(*r.m); lv_timer_handler();
                        int g = find_dir_row(r, "games");
                        if (g >= 0) { select_row(r, g); r.m->key(KEY_RETURN); ui_menu_render(*r.m); lv_timer_handler(); }
                        int fr = mock_find_row(r, j.pick, true);
                        if (fr >= 0) { select_row(r, fr); }
                        printf("MOCK %s %s F3 folder: usb0 row %d, games row %d, %s row %d\n", j.core, cv.name, u, g, j.pick, fr);
                        mock_shot(r, out, j.core, cv.name, want.w, want.h, ti, tl, slug, "folder");
                        if (!strcmp(cv.name, "1080p")) { std::string t2 = std::string(tag) + "/folder"; mock_rows(r, t2.c_str()); }
                    }
                    for (int pi = 0; pi < j.npages; pi++) {
                        const Page &pg = j.pages[pi];
                        if (strcmp(pg.area, fk) || !tr.a[a].tabs[b].id || strcmp(pg.tab, tr.a[a].tabs[b].id) || !back_to_tab()) {
                            continue;
                        }
                        int row = mock_find_row(r, pg.row, pg.prefix);
                        if (row < 0) {
                            printf("MOCK %s %s %s: no row \"%s\" on tab %s, no page picture\n", j.core, cv.name, fk, pg.row, pg.tab);
                            CHECK(row >= 0);
                            continue;
                        }
                        select_row(r, row);
                        r.m->key(KEY_RETURN);
                        ui_menu_render(*r.m); lv_timer_handler();
                        int pa = r.m->area(), pt = r.m->tab();
                        bool is_page = pa == a && pt >= 0 && pt < tr.a[pa].ntabs && tr.a[pa].tabs[pt].page;
                        printf("MOCK %s %s %s: RETURN on \"%s\" (row %d) opens tab %d \"%s\"%s\n", j.core, cv.name, fk, pg.row, row, pt, r.m->tab_name(), is_page ? " (a page)" : " - NOT A PAGE");
                        CHECK(is_page);
                        const char *pl = r.m->tab_name() ? r.m->tab_name() : pg.row;
                        mock_shot(r, out, j.core, cv.name, want.w, want.h, ti, pl, pg.slug, "page");
                        if (!strcmp(cv.name, "1080p")) { std::string t2 = std::string(tag) + "/" + pg.slug; mock_rows(r, t2.c_str()); }
                        if (r.m->paged()) {
                            mock_windows(r, out, j.core, cv.name, want.w, want.h, ti, pl, pg.slug, "page-", "page-end");
                        }
                    }
                }
            }
        }
    }
    ui_set_canvas(960, 540);
    char mf[600];
    snprintf(mf, sizeof(mf), "%s/mock-MANIFEST.json", out);
    FILE *f = fopen(mf, "w");
    if (f) {
        fprintf(f, "[\n");
        for (size_t i = 0; i < g_mock.size(); i++) {
            const MockShot &s = g_mock[i];
            fprintf(f, "  {\"file\": \"%s\", \"core\": \"%s\", \"area\": \"%s\", \"area_label\": \"%s\", \"tab_index\": %d, \"tab_label\": \"%s\", \"state\": \"%s\", \"canvas\": \"%s\"}%s\n",
                    mock_json(s.file).c_str(), mock_json(s.core).c_str(), mock_json(s.area).c_str(), mock_json(s.area_label).c_str(), s.tab_index,
                    mock_json(s.tab_label).c_str(), mock_json(s.state).c_str(), mock_json(s.canvas).c_str(), i + 1 < g_mock.size() ? "," : "");
        }
        fprintf(f, "]\n");
        fclose(f);
    }
    printf("MOCK %zu pictures, %d text overlaps, %d objects outside or cut; manifest %s\n", g_mock.size(), mock_overlaps, mock_bounds, mf);
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
// T235: the page lists Auto and the 9 rows of the mode table (kPmRows, the machine's rate first within a resolution), whatever the screen names; the rows it names carry "*".
// The mark is the app's own reading of the fixture's EDID (video_modes_offered, tested on its own terms in tests/coreapp/test_edid.cc); `nat` the native row, `info` the foot line.
struct PmFx { const char *name; uint8_t vics[3]; int nv; unsigned pc; bool bad, none; unsigned nat; int autohdmi; const char *info; };
static const int kPmRows[9] = { 0, 6, 1, 7, 2, 8, 9, 10, 11 };            // table rows of the list on a PAL machine, in the order of the Picture values 1..9
static const char *pm_row_name(int row) { int h = row % 6; return h == VID_HDMI_480_576 ? (row >= 6 ? "480p" : "576p") : video_hdmi_name(h); }
static int pm_row_hdmi(int row) { return row % 6; }
static unsigned pm_row_dhz(int row)
{
    const EdidRow *er = edid_row(row);
    uint64_t tot = (uint64_t)er->htotal * er->vtotal;
    return (unsigned)(((uint64_t)er->pixel_hz * 10 + tot / 2) / tot);
}
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
    bool edid_ok = len > 0 && edid_parse(edid, len, &ei);                // as picture_env_read: the flag "no or corrupt EDID" apart from the mask (T235 §5)
    env.no_edid = !edid_ok;
    env.native = edid_ok ? (uint16_t)(ei.native & env.mask) : 0;
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
    CHECK(r.m->rows() == 10);                                           // Auto and the 9 rows of the table
    CHECK(!strcmp(r.m->status(), f.info));                               // 6: the foot line says what the mark means (or that the screen named nothing)
    char leg[120];
    r.m->footer_legend(leg, sizeof(leg));
    CHECK(strstr(leg, "RETURN  USE THIS MODE") && strstr(leg, "BROWSE"));  // the key bar
    CHECK(!r.m->toast_text()[0]);                                       // R2: no chip over the question that is not there yet
    int nnow = 0;
    uint16_t pmask = env.mask;                                           // what the app lists: a mask that names nothing is the television rows, none named
    bool pnone = env.no_edid;
    { int tmp[VID_HDMI_COUNT]; if (video_modes_hdmi_list(pmask, VID_SYS_PAL50, tmp) == 0) { pmask = EDID_LIST_NONE; pnone = true; } }
    int autorow = 0;
    if (!video_modes_resolve(pmask, VID_SYS_PAL50, f.autohdmi, &autorow)) { autorow = f.autohdmi; }
    for (int i = 0; i < r.m->rows() && i <= 9; i++) {                   // 1: Auto + the 9 rows in order; the rate of the row's own clock; "*" when the screen names it; native from the EDID; Auto is the choice: one "now" (T187 D9), on Auto's row
        std::string l, v; bool a;
        ux_rowtext(r, i, &l, &v, &a);
        int row = i == 0 ? autorow : kPmRows[i - 1];
        if (i == 0) { CHECK(l.compare(0, 4, "Auto") == 0); } else { CHECK(l == pm_row_name(row)); }
        unsigned dh = pm_row_dhz(row);
        bool nat = (f.nat >> row) & 1, now = i == 0, named = i > 0 && !pnone && ((pmask >> row) & 1);
        char ex[64];
        snprintf(ex, sizeof(ex), "%s%s%s%s%s%s%u.%u Hz", now ? "now" : "", now && nat ? " " : "", nat ? "native" : "", (now || nat) && named ? " " : "", named ? "*" : "", (now || nat || named) ? "   " : "", dh / 10, dh % 10);
        UXCHECK(v == ex, "%s: row %d says \"%s\", expected \"%s\"", what, i, v.c_str(), ex);
        nnow += v.find("now") != std::string::npos;
    }
    CHECK(nnow == 1 && r.m->selected() == 0);                           // D9: one mark, on the chosen row (Auto); the cursor opened on it
    for (int i = 0; i <= 9; i++) {                                      // 2, 3: every cursor position: the page lies inside the canvas, nothing is cut
        select_row(r, i);
        char w2[200];
        snprintf(w2, sizeof(w2), "%s cursor %d", what, i);
        CHECK(view_bounds(w2, want.w, want.h) == 0);
    }
    select_row(r, 0);
    for (int i = 0; i <= 9; i++) { r.m->key(KEY_DOWN); }                // 4: browsing applies nothing
    for (int i = 0; i <= 9; i++) { r.m->key(KEY_UP); }
    CHECK(g_pm_applies == 0 && !picture_confirm_pending() && r.st->value(picture_index(d)) == 0);
    ui_menu_render(*r.m); lv_timer_handler();
    lv_obj_invalidate(lv_screen_active()); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/pm-%s-%s-%s.png", out, core, f.name, shell);
    { PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 32, 114, 960, 540 } : r.px.place.back(); png_out(p, fw, fh, g.x, g.y, g.w, g.h); }
    int trow = -1;                                                      // the first row that is not the mode in force (the screen may not name it: it is tried all the same, T235)
    for (int i = 0; i < 9; i++) { if (kPmRows[i] != autorow) { trow = i + 1; break; } }
    CHECK(trow > 0);
    select_row(r, trow);
    r.m->key(KEY_RETURN);                                               // 4: RETURN on a mode: one apply, the question pending
    CHECK(g_pm_applies == 1 && picture_confirm_pending());
    char nw[96], pv[96];
    picture_confirm_names(nw, sizeof(nw), pv, sizeof(pv));
    char want_now[96], want_prev[96];
    {
        int rn = kPmRows[trow - 1], rp = autorow;
        unsigned dn = pm_row_dhz(rn), dp = pm_row_dhz(rp);
        snprintf(want_now, sizeof(want_now), "%s, %u.%u Hz", video_hdmi_name(pm_row_hdmi(rn)), dn / 10, dn % 10);
        snprintf(want_prev, sizeof(want_prev), "%s, %u.%u Hz", video_hdmi_name(pm_row_hdmi(rp)), dp / 10, dp % 10);
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
        { "six",     { 17, 19, 159 }, 3, 7u, false, false, 1u << 2, 2, "* = the screen names this mode. Others may work: RETURN tries one and asks you to keep it." },
        { "four",    { 17, 159, 0 },  2, 3u, false, false, 1u << 2, 2, "* = the screen names this mode. Others may work: RETURN tries one and asks you to keep it." },
        { "one",     { 147, 0, 0 },   1, 0u, false, false, 1u << 1, 1, "* = the screen names this mode. Others may work: RETURN tries one and asks you to keep it." },
        { "none",    { 0, 0, 0 },     0, 0u, false, true,  0,       0, "The screen did not name its modes. Every mode is listed; RETURN tries one." },
        { "corrupt", { 17, 19, 159 }, 3, 7u, true,  false, 0,       0, "The screen did not name its modes. Every mode is listed; RETURN tries one." },
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
    static CoreDesc cut_base = nes_desc;                            // a core with a record's pages: golden/desc_cut.toml (argv[2], compiled by run.sh) over the NES
    cut_base.needs_program = 0;
    g_cut = new RecordMenu(&cut_base, argc > 2 ? argv[2] : "desc_cut.bin");
    if (getenv("MOCK_ONLY")) {                                      // T228: only the area shots (mock_shots, the click dummy's pictures), without the rest of the bench
        mock_shots(out);
        printf("coreapp render (MOCK_ONLY): %d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }

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
        j.m->key(KEY_F1);                                                       // T212 D5: the task belongs to the drive: F1, the D1: row, RETURN opens its list
        bool ok = pick("D1:");
        CHECK(ok);
        if (ok) { j.m->key(KEY_RETURN); ok = pick("Create blank ATR"); CHECK(ok); }
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
            lv_area_t jr = { 190, 76, 949, 225 };                               // where the job row stands is found, not assumed: the row object of the "Building the image" label
            { lv_obj_t *scr = lv_screen_active(); lv_obj_update_layout(scr); lv_obj_t *lb = find_label(scr, "Building"); CHECK(lb != 0); if (lb) { lv_obj_get_coords(lv_obj_get_parent(lb), &jr); } }
            snprintf(p, sizeof(p), "%s/atari-job_0.png", out);
            png(p, 2);
            int n = 0, bad = 0, states = 0, visits = 0;
            for (int i = 0; i < 400 && j.m->busy(); i++) {
                j.m->tick();
                if (!j.m->busy()) { break; }                                   // the tick that ends the job swaps the job row for the page again: a rebuild of the screen, not a refresh of the row
                ovp::begin(fb, jr.x1, jr.y1, jr.x2 - jr.x1 + 1, jr.y2 - jr.y1 + 1);
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

    // T104.2 D6: the address prompt of a file action that asks, on the screen. The core is the Atari with a .prg of a test's own whose second action asks for the start address in a row of its
    // own (the operations are named but not called here; tests/coreapp/test_prompt.cc runs them). The same ui.cc draws it; what is checked is that the prompt reaches the screen as the row the menu model gives.
    {
        trx_skin_set(TRX_SKIN_PANEL);
        static const OpBind run_bind[] = { B_FILE("path") };
        static const OpBind load_bind[] = { B_FILE("path"), B_ASK("start") };
        static const FileAction prg_actions[] = { { "Run PRG", "prompt.run", run_bind, 1, true, 0, 0 }, { "Load PRG and start from", "prompt.load", load_bind, 2, true, "Start address", 0 } };
        static const FileType prompt_types[] = { { ".prg", prg_actions, 2, 0, 0, 0 } };
        static CoreDesc pd = atari800_desc;
        pd.filetypes = prompt_types;
        pd.nfiletypes = 1;
        CoreDesc *pdd = board_desc(&pd);
        Rig c(pdd, "/usb0/cores/atari800_prompt.cfg");
        c.fs.put("/usb0/x.prg", std::vector<uint8_t>{ 0x00, 0xC0, 0x60, 0x60 });
        c.c.started = true;
        ui_open(pdd);
        c.m->open();
        c.m->key(KEY_F3);                                        // the media level: the stick is /usb0
        c.m->relist();
        int usb = find_dir_row(c, "usb0");
        CHECK(usb >= 0);
        select_row(c, usb);
        c.m->key(KEY_RETURN);                                    // opens the stick: ../ is first, x.prg after it
        c.m->relist();
        int xr = find_file_row(c, "x.prg");
        CHECK(xr >= 0);
        select_row(c, xr);
        c.m->key(KEY_RETURN);
        CHECK(c.m->chooser());
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/prompt-chooser.png", out);
        png(p, 2);
        c.m->key(KEY_DOWN);
        c.m->key(KEY_RETURN);
        CHECK(c.m->entering());
        c.m->key('1'); c.m->key('0');
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/prompt-prompt.png", out);
        png(p, 2);
        char l[100], r[100]; bool a;
        c.m->row(0, l, 100, r, 100, &a);
        CHECK(!strcmp(l, "Start address") && !strcmp(r, "$10_"));
        c.m->key(KEY_ESCAPE);
    }
    // T239.3 base 3.3: a core with a record's pages. The record is tests/coreapp/golden/desc_cut.toml over the NES (the Timewarp and the Monitor page of Home); the Monitor page draws the text grid its node names
    {
        trx_skin_set(TRX_SKIN_PANEL);
        Rig c2(&g_cut->desc, "/usb0/cores/cut.cfg", &g_cut->mm);
        c2.c.started = true;
        ui_open(&g_cut->desc);
        c2.m->open();
        // T239.3 base 3.3: the Monitor page draws the text grid its node names (model_grid.h stands in for the library): the rows and their cursor, redrawn when sig changes and not on a pass that
        // changes nothing; without the grid the screen is empty
        mg_install();
        g_mg.src = { "TRX MONITOR", "> r", "PC=C000 A=00" };
        g_mg.attrs = { TG_STATUS, TG_PROMPT, TG_REGS };
        g_mg.cx = 3; g_mg.cy = 1;
        CHECK(c2.m->goto_spec("F1/monitor"));
        ui_menu_render(*c2.m);
        lv_timer_handler();
        CHECK(find_label(lv_screen_active(), "TRX MONITOR") != 0 && find_label(lv_screen_active(), "> r") != 0 && find_label(lv_screen_active(), "PC=C000") != 0);
        CHECK(g_mg.n_render == 1);
        CHECK(g_mg.n_line == 3);                                // every row read once for the first draw
        snprintf(p, sizeof(p), "%s/monitor-grid.png", out);
        png(p, 2);
        ui_menu_render(*c2.m);                                    // a pass that changed nothing: render() asks the grid, sig is the same, no row is read again
        lv_timer_handler();
        CHECK(g_mg.n_render == 2);
        CHECK(g_mg.n_line == 3);
        g_mg.src[1] = "> d C000";
        ui_menu_render(*c2.m);                                    // the machine changed what it shows: sig changed, the rows are read and the label is written
        lv_timer_handler();
        CHECK(g_mg.n_render == 3);
        CHECK(g_mg.n_line == 6);
        CHECK(find_label(lv_screen_active(), "> d C000") != 0 && find_label(lv_screen_active(), "> r") == 0);
        g_mg.src.pop_back();                                     // a shorter grid: the row that is gone is blank
        ui_menu_render(*c2.m);
        lv_timer_handler();
        CHECK(find_label(lv_screen_active(), "PC=C000") == 0 && find_label(lv_screen_active(), "> d C000") != 0);
        mg_uninstall();                                          // no grid found: an empty screen, nothing of the old rows
        ui_menu_render(*c2.m);
        lv_timer_handler();
        CHECK(find_label(lv_screen_active(), "> d C000") == 0 && find_label(lv_screen_active(), "TRX MONITOR") == 0);
    }

    a1200_menu(out);
    modes_menu(out);
    rtc_menu(out);
    follow_menu(out);
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
    mock_shots(out);                                                // T228: the area shots of TRX64U and the A1200, last, so every check above runs as before
    printf("coreapp render: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
