// The core application's screen (T93 D8/D9, rebuilt on the shared view layer for T104 5.8): the prototype's shell -
// top bar, the navigation rail of areas, the numbered tab strip, the content pane and the key legend - drawn through
// app/view/trxview.cc, which the host application draws through too. What is on a tab comes from MenuCtl, which comes
// from the descriptor; nothing here knows what a core is.
#include <stdlib.h>
#include "ui.h"
#include "picmodes.h"
#include "trx_lv_port.h"
#include "overlay.h"
#include "video.h"
#include "lvgl.h"
#include "trxview.h"
#include "screens.h"
#include "appdlg.h"
#include "contrib.h"
#include "wifinet.h"
#include "src/display/lv_display_private.h"    // the display's buffers (the same include path hostui.cc builds with)
#include <stdio.h>
#include <string.h>

static_assert(MENU_STRIP_TABS == TRX_TABS_MAX, "the tab strip of the tree and of the view hold the same number of tabs");

// The canvas is the view's (T164 D10a): 960 x 540, or the mode's own size below 960 wide.
#define SW trx_canvas_w()
#define SH trx_canvas_h()

static bool have_ui;
static lv_obj_t *scr;
static bool msg_up;
static lv_obj_t *g_msg_label, *g_msg_panel;                  // the message pane's text and box while it stands (the picture question updates the text in place)
static bool pq_up;                                  // T187 F1: the picture question owns the pane; a menu render in the same pass (the NES waiting menu) must not paint over it
static char msg_title_s[80], msg_text_s[720];
static bool msg_bad;
static const CoreDesc *g_desc;
static const int g_look_t = 1, g_look_scheme = 1;                // the menu's transparency (position 1 = 25 %) and colour scheme (position 1 = Ultimate Black): no setting changes them since T239.3 D56
static volatile bool g_look_dirty = true;
static void apply_alpha(void);
static bool ov_on(void);

// The route the shell draws, built from the tree once a menu is rendered. The strings are the tree's, which
// lives in MenuCtl for as long as the application runs.
static TrxTab tabs[TRX_AREAS_MAX][TRX_TABS_MAX];
static TrxArea areas[TRX_AREAS_MAX];
static TrxRoute route;

bool ui_available(void) { return have_ui; }

// One frame buffer, rendered into directly (the host application does the same, hostui.cc buffers_mode): with two buffers in LVGL's
// DIRECT mode every frame first copies the areas of the previous frame from the other buffer, and that copy reads DDR2 through a CPU
// without a data cache. Measured on the board 2026-10-22: a selection move of 31,000 px took ~110 ms here (3.5 ms per 1,000 px), the
// host application's 84,000 px took ~190 ms (2.2 ms per 1,000 px); the menu opened in 2.4 s for one full screen (T104 5.11).
static void single_buffer(void)
{
#ifndef UI_KEEP_TWO_BUFFERS
    lv_display_t *d = lv_display_get_default();
    if (d && d->buf_1 && d->buf_2) {
        lv_display_set_buffers(d, d->buf_1->data, NULL, (uint32_t)TRX_LV_W * TRX_LV_H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    }
#endif
}

// T164 D10a: the menu's canvas changed (a mode change): the view, the port and the display take the new size, and the next render builds the frame
// again for it. Called by the video service before it places the menu, also before ui_open() (then the display is created at this size).

// T187 gate 1 (R2): the legend never runs past the canvas. The fixed part ends in "BACK"; the letters of the page and of the core follow it, most important first, and
// the ones that do not fit are dropped whole, from the end (a half hint tells nothing).
static void legend_fit(char *s)
{
    int room = trx_canvas_w() - 2 * trx_theme().space_md;
    char *back = strstr(s, "BACK");
    if (!back) {
        return;
    }
    back += 4;
    while (trx_text_w(s, trx_theme().caption) > room) {
        char *cut = 0;
        for (char *q = back; *q; q++) {
            if (q[0] == ' ' && q[1] == ' ' && q[2] != ' ') {
                cut = q;
            }
        }
        if (!cut) {
            break;
        }
        *cut = 0;
    }
}

void ui_set_canvas(int w, int h)
{
    if (w == trx_canvas_w() && h == trx_canvas_h()) {
        return;
    }
    trx_canvas_set(w, h);
    trx_lv_set_canvas(w, h);
    if (have_ui) {
        single_buffer();
        g_look_dirty = true;                       // the frame is built again at the next render (as for a new colour scheme)
        scr = lv_screen_active();
        if (trx_splash_up()) {
            trx_splash_relayout(trx_overlay());    // the start-up splash is up while the mode is set: drawn again for the canvas
        }
    }
}

bool ui_open(const CoreDesc *desc)
{
    g_desc = desc;
    have_ui = trx_lv_open();
    if (!have_ui) {
        printf("[coreapp] no pixel layer in this bitstream: the screen is the console log\n");
        return false;
    }
    scr = lv_screen_active();
    lv_obj_set_flag(scr, LV_OBJ_FLAG_SCROLLABLE, false);
    trx_skin_set(TRX_SKIN_PANEL);
    single_buffer();
    return true;
}

void ui_set_desc(const CoreDesc *desc)
{
    g_desc = desc;
}

// The information lines menu_info_labels[] names, in its order; the two the top bar shows as chips.
enum { INFO_CORE = 0, INFO_APP, INFO_NET, INFO_MEDIA, INFO_ROMS, INFO_LIBS };
static char chip_media[40], chip_net[40], chip_say[96];

static void shell_info(TrxShellInfo *i, const char *section, const char *activity, MenuCtl *m)
{
    const TrxTheme &t = trx_theme();
    memset(i, 0, sizeof(*i));
    i->product = "TRX64";
    i->section = section;
    i->activity = activity;
    if (g_desc) {
        i->chips[i->nchips].label = g_desc->short_title ? g_desc->short_title : g_desc->cor_name;
        i->chips[i->nchips].tone = t.ok;
        i->nchips++;
    }
    if (m && m->picking_modes()) {                                      // T187 F6 (spec D5): the path of the page the Picture row opened
        i->chips[i->nchips].label = "Settings > Video";
        i->chips[i->nchips].tone = t.muted;
        i->nchips++;
    }
    bool saying = false;
#ifndef UI_NO_TOAST                                                   // the bench's control: no message of the menu at all
    if (m && m->toast_text()[0]) {
        saying = true;                                    // T187 D4: the short message of the open menu (Volume 110%) is a chip next to the core name, not a card over the rows
        snprintf(chip_say, sizeof(chip_say), "%s", m->toast_text());
        i->chips[i->nchips].label = chip_say;
        i->chips[i->nchips].tone = t.accent;
        i->nchips++;
    }
#endif
    if (g_desc && m && m->unsaved()) {
        i->chips[i->nchips].label = "UNSAVED";
        i->chips[i->nchips].tone = t.warn;
        i->nchips++;
    }
    // the prototype's two status chips: what media are mounted and whether the network answers. The bar
    // has room for a word each, not for a sentence, so they are cut rather than allowed to push the
    // product name off the left.
    if (m && !saying) {                                               // while a message stands it has the room of the media and network chips
        m->info_line(INFO_MEDIA, chip_media, sizeof(chip_media));
        m->info_line(INFO_NET, chip_net, sizeof(chip_net));
        chip_media[14] = 0;
        chip_net[24] = 0;                                              // "WiFi 192.0.2.189" is 20; the old cut at 14 showed .18 for .189 (T187 finding)
        if (chip_media[0] && !m->picking_modes()) {                // the path chip took the media chip's place (the bar holds 4 chips)
            i->chips[i->nchips].label = chip_media;
            i->chips[i->nchips].tone = t.ok;
            i->nchips++;
        }
        if (chip_net[0]) {
            i->chips[i->nchips].label = chip_net;
            i->chips[i->nchips].tone = strcmp(chip_net, NET_NONE_TEXT) ? t.accent : t.muted;   // offline reads as offline, as in the host
            i->nchips++;
        }
    }
    i->footer_left = "R/S  CLOSE";
    if (m && m->app_mode()) {                                         // an application's screens: the section says so, the chips are the application's and the core's
        i->section = "/ APP";
        i->nchips = 0;
        if (g_desc) {
            i->chips[i->nchips].label = m->core_title();
            i->chips[i->nchips].tone = t.accent;
            i->nchips++;
        }
        static char legend_app[200];
        m->footer_legend(legend_app, sizeof(legend_app));
        i->footer_right = legend_app;
        return;
    }
    // the legend: what the keys do for the row under the cursor, the tab range, the letters of this core (MenuCtl::footer_legend)
    static char legend[160];
    legend[0] = 0;
    if (m) {
        m->footer_legend(legend, sizeof(legend));
        legend_fit(legend);
    }
    i->footer_right = legend;
}

void ui_shell_info_now(TrxShellInfo *i, MenuCtl *m)
{
    shell_info(i, "/ UI", m->session_word(true), m);
}

static int vis_map[TRX_AREAS_MAX][MENU_TABS_MAX];             // the strip's place of each tab of the tree, -1: the tab is hidden (an application's screen that is no tab)

static bool layout_has(const char *layout, const char *key, char *out = 0, int n = 0)
{
    const char *w = layout ? strchr(layout, ';') : 0;
    return w && ui_attr(w + 1, key, out, n);
}

static void build_route(MenuCtl &m)
{
    const MenuTree &tr = m.tree();
    const bool app_mode = m.app_mode();
    for (int a = 0; a < tr.nareas && a < TRX_AREAS_MAX; a++) {
        int nv = 0;
        for (int b = 0; b < tr.a[a].ntabs && b < MENU_TABS_MAX; b++) {
            vis_map[a][b] = -1;
            if (m.tab_is_hidden(a, b)) {      // T187 D2: a page a row opens, a screen that is no tab, a tab whose `show` state is empty (T262 P3): no place on the strip
                continue;
            }
            if (nv >= TRX_TABS_MAX) {
                continue;
            }
            vis_map[a][b] = nv;
            tabs[a][nv].n = tr.a[a].tabs[b].n;
            tabs[a][nv].title = tr.a[a].tabs[b].name;
            tabs[a][nv].view = 0;
            tabs[a][nv].arg = 0;
            nv++;
        }
        if (nv == 1 && !strcmp(tr.a[a].key, "F7")) {             // T212 D12: the APP area with one tab shows its sections directly; a strip of one tab carries no information
            nv = 0;
        }
        areas[a].key = tr.a[a].key;
        areas[a].title = tr.a[a].name;
        areas[a].tabs = tabs[a];
        areas[a].ntabs = (uint8_t)nv;
    }
    route.areas = areas;
    route.nareas = (uint8_t)(tr.nareas < TRX_AREAS_MAX ? tr.nareas : TRX_AREAS_MAX);
    route.app_mode = app_mode;
}

// The tab of the strip that stands for the open one: itself, or - a hidden screen - the one it says it is under.
int ui_shell_tab(MenuCtl &m)
{
    const MenuTree &tr = m.tree();
    int a = m.area(), b = m.tab();
    if (a < 0 || a >= tr.nareas || a >= TRX_AREAS_MAX || b < 0 || b >= tr.a[a].ntabs || b >= MENU_TABS_MAX) {
        return b;
    }
    if (vis_map[a][b] >= 0) {
        return vis_map[a][b];
    }
    if (tr.a[a].tabs[b].page && tr.a[a].tabs[b].parent >= 0) {         // the page stands under the tab whose row opened it
        return vis_map[a][tr.a[a].tabs[b].parent];
    }
    char under[40];
    if (layout_has(tr.a[a].tabs[b].layout, "under", under, sizeof(under))) {
        for (int t = 0; t < tr.a[a].ntabs && t < MENU_TABS_MAX; t++) {
            if (tr.a[a].tabs[t].id && !strcmp(tr.a[a].tabs[t].id, under)) {
                return vis_map[a][t];
            }
        }
    }
    return -1;
}

static void ui_home(lv_obj_t *c, MenuCtl &m, int x, int y);

// The rows of the last list render, kept so a key that only moves the selection restyles two of them
// instead of rebuilding the pane (T104, board 2026-10-16: 0.5-1 s per key). The signature covers
// everything in the pane that is not the selection; when it differs, the pane is built again.
#define UI_ROWS_MAX 24
static lv_obj_t *g_row[UI_ROWS_MAX];
static int g_rown, g_rowfirst, g_rowsel = -1;
static uint32_t g_rowsig;
static bool g_rowtitle[24];                          // per kept row: it is a heading proper, not an information line (the bench's probe: no window ends on one)
static bool g_rowhead[24];                           // per kept row: it is a heading (the bench's probe)
static bool g_rowlist;                             // the kept rows belong to a list, not to Home
static uint32_t g_inplace;                         // window moves and value changes served by retexting the rows
static lv_obj_t *g_pos;                            // the "a-b of n" label of the kept list
static lv_obj_t *g_status;                         // the status line at the pane's foot
static lv_obj_t *g_title;                          // the pane's title line (the tab's name, or the browser's path)
// The pane's title line is one line (UX R5): a long path is cut in the middle to the room the line has, never wrapped into the rows below it.
static const char *title_fit(const char *s, char *buf, int n)
{
    return trx_fit_mid(s, trx_content_w() - 2 * trx_theme().space_lg - 240 - 4, trx_theme().display, buf, n);
}
static void title_set(lv_obj_t *l, const char *s)
{
    const int room = trx_content_w() - 2 * trx_theme().space_lg - 240;
    const int tw = trx_text_w(s, trx_theme().display) + 4;
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(l, s);
    lv_obj_set_width(l, tw < room ? tw : room);
}

static bool g_status_bad;
#define UI_HOME_MAX 24
static lv_obj_t *g_home[UI_HOME_MAX];                // the Home panel's status rows, kept so a selection move there restyles two of them
static int g_homen, g_homesel = -1;
static uint32_t g_homesig;
static uint32_t g_homehead, g_homerowh[UI_HOME_MAX];   // what Home shows besides its rows, and per row what it shows: a row whose text moved is retexted in place (T212: a job runs from Home)
static int g_homecw;                                   // the width of a Home row's content
static bool g_homelist;
static uint32_t g_rowh[UI_ROWS_MAX];               // per kept row: what it shows (text, selection, dim), to retext only the ones that moved
static uint32_t g_shape;                           // the pane's structure: title, status, row count, window size, row kinds
static uint32_t g_cheap;                           // selection moves served without rebuilding the pane
static int g_first, g_last_area = -1, g_last_tab = -1;   // the list window, read by list_signature()

// T262 D4: the page of a `list` node keeps its row objects: a key writes the rows and the side panel in place instead of building the page again (a build of it was ~365,000 px,
// a quarter of a second blank on the board, for every RETURN and for every status line that changed later).
static bool g_lv_valid;
static lv_obj_t *g_lv_row[UI_ROWS_MAX];
static lv_obj_t *g_lv_sp;                                    // the side panel's card
static int g_lv_n, g_lv_sel = -1, g_lv_sw, g_lv_ch, g_lv_rw;
static uint32_t g_lv_shape, g_lv_h[UI_ROWS_MAX], g_lv_ph;
static uint32_t g_lv_builds;                                 // pages built from scratch (the bench's count)

uint32_t ui_cheap_moves(void) { return g_cheap; }
uint32_t ui_inplace_moves(void) { return g_inplace; }
uint32_t ui_listv_builds(void) { return g_lv_builds; }

static uint32_t sig_str(uint32_t h, const char *s)
{
    for (const char *p = s; p && *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return (h ^ 0xFF) * 16777619u;
}

// T187 P1 / T209 D1: the window of the list on screen, from the rows' heights (every row is one line), the one function the build, the
// signature and the in-place pass use.
static int list_row_h(int, void *)
{
    return trx_row_h() + 1;
}

// The height a note of the foot needs beyond its one line, wrapped to `w` (a long note takes more lines; a word wider than the line breaks inside it).
int ui_note_extra_h(const char *s, int w)
{
    if (!s || !*s || w < 1) {
        return 0;
    }
    const lv_font_t *f = trx_theme().caption;
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    int one = lv_font_get_line_height(f);
    return p.y > one ? p.y - one : 0;
}

static const char *status_line(MenuCtl &m);

static int status_extra(MenuCtl &m)
{
    return ui_note_extra_h(status_line(m), trx_content_w() - 2 * trx_theme().space_lg);
}

static int list_foot(MenuCtl &m) { return 24 + status_extra(m); }     // the status line's single line, and the lines a long note adds above it

static TrxWindow list_window(MenuCtl &m, int *room_out)
{
    const TrxTheme &t = trx_theme();
    int room = trx_content_h() - (t.space_md + 26) - list_foot(m);
    if (room < 1) {
        room = 1;
    }
    *room_out = room;
    int first = (g_last_area != m.area() || g_last_tab != m.tab()) ? m.first_hint() : g_first;     // T187 rule 5: a tab comes back with the window it had
    TrxWindow w = trx_window_h(m.rows(), m.selected(), first, room, list_row_h, &m);
    while (w.count > 1 && m.title_row(w.first + w.count - 1)) {         // T187 rule 3: a heading is never the last line of a window (an information line is a row: it ends one)
        w.count--;
    }
    m.note_first(w.first);
    return w;
}

// "3-20 of 23" and, when rows are hidden, how many lie above and below.
static void pos_text(char *out, int n, const TrxWindow &w_rows, MenuCtl &m)
{
    TrxWindow w = { 0, 0, 0 };                                      // the counter counts the rows the list draws as lines, not the headings between them (an information line is a line)
    int total = m.rows();
    for (int i = 0; i < total; i++) {
        if (m.title_row(i)) {
            continue;
        }
        if (i < w_rows.first) {
            w.first++;
        } else if (i < w_rows.first + w_rows.count) {
            w.count++;
        }
        w.total++;
    }
    int above = w.first, below = w.total - w.first - w.count;
    int k = snprintf(out, n, "%d-%d of %d", w.first + 1, w.first + w.count, w.total);
    if (above > 0 && below > 0) {
        snprintf(out + k, n - k, " (%d above, %d below)", above, below);
    } else if (above > 0) {
        snprintf(out + k, n - k, " (%d above)", above);
    } else if (below > 0) {
        snprintf(out + k, n - k, " (%d below)", below);
    }
}

// Everything the pane shows except which row is selected: the tab, the window, the status line and the
// text of every visible row. Equal signature + a different selection = two rows to restyle, nothing else.
// The status row's text: what the controller says, else the legend of the ring of T187 D9 when the list has rows with one.
static const char *status_line(MenuCtl &m)
{
    return m.foot_line();                                           // the chip says a message, the foot line does not say it twice (ux-review 2026-11-09); status and ring legend together (T187 F4)
}

static uint32_t list_signature(MenuCtl &m)
{
    int n = m.rows();
    int room;
    TrxWindow w = list_window(m, &room);

    uint32_t h = 2166136261u;
    h = sig_str(h, m.title());
    h = sig_str(h, status_line(m));
    h = (h ^ (uint32_t)m.status_bad()) * 16777619u;
    h = (h ^ (uint32_t)n) * 16777619u;
    h = (h ^ (uint32_t)w.first) * 16777619u;
    h = (h ^ (uint32_t)w.count) * 16777619u;
    char left[120], right[120];
    bool active;
    for (int i = 0; i < w.count; i++) {
        m.row(w.first + i, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        h = sig_str(h, left);
        h = sig_str(h, right);
        h = (h ^ (uint32_t)active ^ ((uint32_t)m.row_reset(w.first + i) << 4)) * 16777619u;
    }
    return h;
}

// Home's frame: the title, the session, video and machine lines - everything but the rows.
static uint32_t home_head_sig(MenuCtl &m)
{
    uint32_t h = 2166136261u;
    char l[120];
    h = sig_str(h, m.title());
    h = (h ^ (uint32_t)m.closable()) * 16777619u;
    m.session_line(l, sizeof(l));
    h = sig_str(h, l);
    m.video_line(l, sizeof(l));
    h = sig_str(h, l);
    m.machine_line(l, sizeof(l));
    return sig_str(h, l);
}
static uint32_t home_row_sig(MenuCtl &m, int i)
{
    char l[120], r[120];
    bool active;
    m.row(i, l, sizeof(l), r, sizeof(r), &active, 0, 0);
    uint32_t h = sig_str(2166136261u, l);
    h = sig_str(h, r);
    return (h ^ (uint32_t)active) * 16777619u;
}

// Everything Home shows except which row is selected.
static uint32_t home_signature(MenuCtl &m)
{
    uint32_t h = 2166136261u;
    char l[120], r[120];
    bool active;
    h = sig_str(h, m.title());
    h = (h ^ (uint32_t)m.closable()) * 16777619u;
    m.session_line(l, sizeof(l));
    h = sig_str(h, l);
    m.video_line(l, sizeof(l));
    h = sig_str(h, l);
    m.machine_line(l, sizeof(l));
    h = sig_str(h, l);
    int n = m.rows();
    h = (h ^ (uint32_t)n) * 16777619u;
    for (int i = 0; i < n; i++) {
        m.row(i, l, sizeof(l), r, sizeof(r), &active, 0, 0);
        h = sig_str(h, l);
        h = sig_str(h, r);
    }
    return h;
}

// The status line at the pane's foot: always there (empty when there is nothing to say), as wide as its text, so a message appearing
// or changing repaints the message and the first one does not change the pane's structure (T104 5.11: the first value change of a
// session used to build the pane again because the status label did not exist yet).
static void status_set(const char *st, bool bad)
{
    const TrxTheme &t = trx_theme();
    if (!g_status) {
        return;
    }
    if (!st) {
        st = "";
    }
    if (strcmp(lv_label_get_text(g_status), st)) {
        int room = trx_content_w() - 2 * t.space_lg;
        int w = trx_text_w(st, t.caption) + 4;
        lv_obj_set_width(g_status, w < room ? (w < 1 ? 1 : w) : room);
        lv_label_set_text(g_status, st);
    }
    if (g_status_bad != bad) {
        lv_obj_set_style_text_color(g_status, lv_color_hex(bad ? t.bad : t.muted), 0);
        g_status_bad = bad;
    }
}

// Does the pane stand as it is - same title, status, whether the window is shorter than the list - so that the rows alone may be written again?
// Fills the per-row hashes of what each row shows. (T104, board 2026-10-22: scrolling past the window's last row rebuilt the whole pane per step;
// the rows are the same objects, only their text moved.) What a row is - a heading or a setting, with the ring or not - and how many rows the window
// has (a heading never ends it) are not the pane's shape: the row objects are changed, made or taken away one by one (the in-place pass of
// ui_menu_render), and a row that cannot be changed in place makes the pane be built again.
static bool list_inplace(MenuCtl &m, TrxWindow *w_out, uint32_t *rowh, uint32_t *shape_out)
{
    int room;
    TrxWindow w = list_window(m, &room);
    if (w.count < 1 || w.count > UI_ROWS_MAX) {
        return false;
    }
    uint32_t sh = 2166136261u;                                          // the title is written in place (a folder with as many rows is the same pane)
    sh = (sh ^ (uint32_t)(w.count < m.rows())) * 16777619u;
    sh = (sh ^ (uint32_t)status_extra(m)) * 16777619u;                  // the lines of the status note: the label stands higher and the window is shorter
    char left[120], right[120];
    bool active;
    for (int i = 0; i < w.count; i++) {
        m.row(w.first + i, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        uint32_t h = 2166136261u;
        h = sig_str(h, left);
        h = sig_str(h, right);
        h = (h ^ (uint32_t)active ^ (uint32_t)((w.first + i == m.selected()) << 1) ^ (uint32_t)(m.title_row(w.first + i) << 2) ^
             (uint32_t)(m.row_reset(w.first + i) << 3)) * 16777619u;      // a heading proper and a ring are drawn another way (trx_row_retext)
        rowh[i] = h;
    }
    *w_out = w;
    *shape_out = sh;
    return sh == g_shape;
}

// T187: what the list on screen really drew, read back from the LVGL objects (the bench's S1 check; the board does not call it).
bool ui_status_probe(int *x1, int *y1, int *x2, int *y2, char *txt, int n)
{
    if (!g_status) {
        return false;
    }
    lv_obj_update_layout(lv_obj_get_parent(g_status));
    *x1 = lv_obj_get_x(g_status);
    *y1 = lv_obj_get_y(g_status);
    *x2 = *x1 + lv_obj_get_width(g_status);
    *y2 = *y1 + lv_obj_get_height(g_status);
    snprintf(txt, (size_t)n, "%s", lv_label_get_text(g_status));
    return true;
}

bool ui_list_probe(UiListProbe *p)
{
    memset(p, 0, sizeof(*p));
    if (!g_rowlist || g_rown < 1 || !g_row[0]) {
        return false;
    }
    const TrxTheme &t = trx_theme();
    lv_obj_t *par = lv_obj_get_parent(g_row[0]);
    lv_obj_update_layout(par);
    p->nrows = g_rown;
    p->first = g_rowfirst;
    p->sel = g_rowsel;
    p->pane_h = trx_content_h();
    p->rows_top = t.space_md + 26;
    p->rows_bottom = g_status ? lv_obj_get_y(g_status) - 4 : trx_content_h() - 24;     // the status line stands 4 px under the rows
    for (int i = 0; i < g_rown; i++) {
        int y1 = lv_obj_get_y(g_row[i]), h = lv_obj_get_height(g_row[i]);
        p->drawn_h += h + 1;
        int lcy, vcy;
        if (i < UiListProbe::UI_PROBE_ROWS) {
            p->row_y[i] = y1;
            p->row_h[i] = h;
            p->row_has_val[i] = trx_row_value_cy(g_row[i], &lcy, &vcy);
            p->lab_cy[i] = p->row_has_val[i] ? lcy : 0;
            p->val_cy[i] = p->row_has_val[i] ? vcy : 0;
            trx_row_texts(g_row[i], p->lab[i], p->val[i], 64);
        }
        if (!g_rowhead[i] && trx_row_value_cy(g_row[i], &lcy, &vcy)) {
            int off = vcy > lcy ? vcy - lcy : lcy - vcy;
            p->nvals++;
            p->val_off_max = off > p->val_off_max ? off : p->val_off_max;
        }
        if (i == g_rown - 1) {
            p->last_heading = g_rowtitle[i];
        }
        if (g_rowfirst + i == g_rowsel) {
            p->sel_heading = g_rowhead[i];
            p->sel_y1 = y1;
            p->sel_y2 = y1 + h;
            p->sel_seen = true;
        }
    }
    if (g_pos) {
        p->pos_y2 = lv_obj_get_y(g_pos) + lv_obj_get_height(g_pos);
        snprintf(p->pos, sizeof(p->pos), "%s", lv_label_get_text(g_pos));
    }
    return true;
}

void ui_menu_forget_rows(void)
{
    g_lv_valid = false;
    g_rown = 0;
    g_rowsel = -1;
    g_rowsig = 0;
    g_rowlist = false;
    g_homelist = false;
    g_homen = 0;
    g_pos = 0;
    g_status = 0;
    g_title = 0;
    screens_forget();
}

// T262 P1: the page of a `list` node: one line per row (dot and name, the right column in the row's tone) in a card, the selected row's side lines in the panel at its
// right (the look of the F3 browser's SELECTED panel). The window is as many fixed-height lines as the card holds.
static int listv_side_w(void) { return trx_compact() ? 170 : 230; }

static int listv_card_h(MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    int h = trx_content_h() - (t.space_md + 26) - list_foot(m) - t.space_sm;
    return h < trx_status_row_h() + 2 * t.space_sm ? trx_status_row_h() + 2 * t.space_sm : h;
}

static TrxWindow listv_window(MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    int rows = (listv_card_h(m) - 2 * t.space_sm) / trx_status_row_h();
    int first = (g_last_area != m.area() || g_last_tab != m.tab()) ? m.first_hint() : g_first;
    TrxWindow w = trx_window(m.rows(), m.selected(), first, rows < 1 ? 1 : rows);
    m.note_first(w.first);
    return w;
}

static uint32_t lv_hi(uint32_t h, int v) { return (h ^ (uint32_t)v) * 16777619u; }

// What the side panel shows for the selected row: its name, right column, tone and side lines.
static uint32_t listv_panel_hash(MenuCtl &m, int sel)
{
    uint32_t h = 2166136261u;
    h = lv_hi(h, m.rows() > 0);
    if (m.rows() > 0) {
        char left[120], right[120];
        bool active;
        m.row(sel, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        h = sig_str(h, left);
        h = sig_str(h, right);
        h = lv_hi(h, m.list_tone(sel) + 1);
        for (int k = 0; k < m.list_side_n(sel); k++) {
            char line[80];
            m.list_side(sel, k, line, sizeof(line));
            h = sig_str(h, line);
        }
    }
    return h;
}

static void listv_panel(lv_obj_t *sp, MenuCtl &m, int sel, int sw, int ch)
{
    const TrxTheme &t = trx_theme();
    trx_kicker(sp, t.space_md, t.space_sm, "SELECTED");
    if (m.rows() > 0) {
        char left[120], right[120];
        bool active;
        m.row(sel, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        const int pw = sw - 2 * t.space_md;
        int py = 30;
        lv_obj_t *nl = trx_txt(sp, t.space_md, py, left, t.body, t.ink, pw);
        lv_label_set_long_mode(nl, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_update_layout(nl);
        py += lv_obj_get_height(nl) + t.space_sm;
        const int tone = m.list_tone(sel);
        if (right[0]) {
            lv_obj_t *vl = trx_txt(sp, t.space_md, py, right, t.caption, tone >= 0 && tone < 5 ? trx_tone_color((TrxTone)tone) : t.muted, pw);
            lv_label_set_long_mode(vl, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_update_layout(vl);
            py += lv_obj_get_height(vl) + t.space_sm;
        }
        for (int k = 0; k < m.list_side_n(sel); k++) {
            char line[80];
            m.list_side(sel, k, line, sizeof(line));
            if (py + lv_font_get_line_height(t.caption) > ch - t.space_sm) {
                break;
            }
            lv_obj_t *sl = trx_txt(sp, t.space_md, py, line, t.caption, t.muted, pw);
            lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_update_layout(sl);
            py += lv_obj_get_height(sl) + t.space_xs;
        }
    }
}

// What a row of the list page shows: its words, tone, dimness and whether it is the selected one.
static uint32_t listv_row_hash(MenuCtl &m, int idx, int sel)
{
    char left[120], right[120];
    bool active;
    m.row(idx, left, sizeof(left), right, sizeof(right), &active, 0, 0);
    uint32_t h = sig_str(2166136261u, left);
    h = sig_str(h, right);
    return lv_hi(h, (m.list_tone(idx) + 1) | active << 4 | (idx == sel) << 5);
}

// The page's shape: what the frame, the window and the foot hold. A change of it is a new page.
static uint32_t listv_shape(MenuCtl &m, const TrxWindow &w)
{
    uint32_t h = lv_hi(2166136261u, w.count);
    h = lv_hi(h, w.count < m.rows());
    h = lv_hi(h, status_extra(m));
    return lv_hi(h, listv_card_h(m));
}

static void ui_listv(lv_obj_t *c, MenuCtl &m, int x, int y)
{
    const TrxTheme &t = trx_theme();
    const int W = trx_content_w() - 2 * x, sw = listv_side_w(), lw = W - sw - t.space_lg, ch = listv_card_h(m);
    const int sel = m.selected();
    TrxWindow w = listv_window(m);
    g_first = w.first;
    lv_obj_t *card = trx_card(c, x, y, lw, ch, false);
    int ry = t.space_sm;
    g_lv_n = w.count <= UI_ROWS_MAX ? w.count : 0;                     // a window of more rows than the table holds is never written in place
    g_lv_rw = lw - 2 * t.space_md;
    for (int i = 0; i < w.count; i++) {
        const int idx = w.first + i;
        char left[120], right[120], sub[8];
        bool active;
        m.row(idx, left, sizeof(left), right, sizeof(right), &active, sub, sizeof(sub));
        const int tone = m.list_tone(idx);
        TrxStatusRow r = { left, right[0] ? right : 0, tone >= 0 && tone < 5 ? (TrxTone)tone : TT_MUTED, idx == sel, !active };
        lv_obj_t *ro = trx_status_row(card, t.space_md, ry, lw - 2 * t.space_md, r, &ry);
        if (i < UI_ROWS_MAX) {
            g_lv_row[i] = ro;
            g_lv_h[i] = listv_row_hash(m, idx, sel);
        }
    }
    lv_obj_t *sp = trx_card(c, x + lw + t.space_lg, y, sw, ch, true);
    listv_panel(sp, m, sel, sw, ch);
    g_lv_sp = sp;
    g_lv_ph = listv_panel_hash(m, sel);
    g_rowlist = false;                                       // the generic list's cheap path is not this page's: its own is listv_inplace
    g_rown = 0;
    {
        const char *st = status_line(m);
        const int room = trx_content_w() - 2 * x;
        g_status = trx_txt(c, x, trx_content_h() - 20 - status_extra(m), st, t.caption, m.status_bad() ? t.bad : t.muted, room);
        lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);
        g_status_bad = m.status_bad();
        lv_obj_set_width(g_status, trx_text_w(st, t.caption) + 4 < room ? trx_text_w(st, t.caption) + 4 : room);
    }
    if (w.count < m.rows()) {
        char pos[64];
        pos_text(pos, sizeof(pos), w, m);
        g_pos = trx_txt(c, trx_content_w() - x - trx_text_w(pos, t.caption), t.space_md + 4, pos, t.caption, t.muted);
    }
    g_lv_sel = sel;
    g_lv_sw = sw;
    g_lv_ch = ch;
    g_lv_shape = listv_shape(m, w);
    g_lv_valid = g_lv_n == w.count;
    g_lv_builds++;
}

// The page stands (same frame, same window length, same foot): the rows whose words moved are written again, the selection moves between two rows, the panel is made again
// only when what it says changed. False: the page has to be built again.
static bool listv_inplace(MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    const int sel = m.selected();
    TrxWindow w = listv_window(m);
    if (!g_lv_valid || w.count != g_lv_n || listv_shape(m, w) != g_lv_shape || !g_status) {
        return false;
    }
    g_first = w.first;
    for (int i = 0; i < w.count; i++) {
        const uint32_t h = listv_row_hash(m, w.first + i, sel);
        if (h == g_lv_h[i]) {
            continue;
        }
        char left[120], right[120];
        bool active;
        m.row(w.first + i, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        const int tone = m.list_tone(w.first + i);
        TrxStatusRow r = { left, right[0] ? right : 0, tone >= 0 && tone < 5 ? (TrxTone)tone : TT_MUTED, w.first + i == sel, !active };
        if (!trx_status_row_retext(g_lv_row[i], r, g_lv_rw)) {
            return false;
        }
        trx_row_select(g_lv_row[i], w.first + i == sel);
        g_lv_h[i] = h;
    }
    if (g_title) {
        char tb[320];
        const char *ft = title_fit(m.title(), tb, sizeof(tb));
        if (strcmp(lv_label_get_text(g_title), ft)) {
            title_set(g_title, ft);
        }
    }
    if (g_pos && w.count < m.rows()) {
        char pos[64];
        pos_text(pos, sizeof(pos), w, m);
        if (strcmp(lv_label_get_text(g_pos), pos)) {
            lv_label_set_text(g_pos, pos);
            lv_obj_set_x(g_pos, trx_content_w() - t.space_lg - trx_text_w(pos, t.caption));
        }
    }
    status_set(status_line(m), m.status_bad());
    const uint32_t ph = listv_panel_hash(m, sel);
    if (ph != g_lv_ph && g_lv_sp) {
        lv_obj_clean(g_lv_sp);
        listv_panel(g_lv_sp, m, sel, g_lv_sw, g_lv_ch);
        g_lv_ph = ph;
    }
    g_lv_sel = sel;
    g_inplace++;
    return true;
}

void ui_menu_render(MenuCtl &m)
{
    if (!have_ui || pq_up) {
        return;
    }
    msg_up = false;
    g_msg_label = g_msg_panel = 0;
    if (g_look_dirty) {                                              // a new transparency or colour scheme: the frame is built again with it
        g_look_dirty = false;
        trx_scheme_set(g_look_scheme);
        trx_shell_forget();
        ui_menu_forget_rows();
        lv_obj_clean(scr);
        apply_alpha();
    }
    const TrxTheme &t = trx_theme();
    build_route(m);

    if (!screens_wanted(m) && !m.is_home()) {                       // T209 D4: the foot names the paging keys of a list longer than its window; the window is known before the shell is made
        int room0;
        TrxWindow w0 = m.list_view() ? listv_window(m) : list_window(m, &room0);
        m.note_paged(w0.count < m.rows());
    }
    TrxShellInfo info;
    ui_shell_info_now(&info, &m);

    // A tab that names a layout is one of the concept's screens (screens.h); the rest of this function is the flat list.
    if (screens_wanted(m) && screens_render(scr, info, route, m)) {
        if (screens_repage_take()) {                                // T209 D4: the page state flipped on this frame: the foot is made again from it
            ui_shell_info_now(&info, &m);
            screens_render(scr, info, route, m);
        }
        return;
    }

    // The cheap path: the frame stands, the pane holds the same rows, only the selection moved.
    // UI_NO_CHEAP_PATH is the bench's control: without it a key has to redraw the whole pane again.
#ifndef UI_NO_CHEAP_PATH
    if (g_homelist && m.is_home() && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m)) && m.rows() == g_homen &&
        home_signature(m) == g_homesig) {
        int sel = m.selected();                                     // Home stands: only the selection moved, or nothing did
        if (sel != g_homesel) {
            if (g_homesel >= 0 && g_homesel < g_homen) {
                trx_row_select(g_home[g_homesel], false);
            }
            if (sel >= 0 && sel < g_homen) {
                trx_row_select(g_home[sel], true);
            }
            g_homesel = sel;
            g_cheap++;
        }
        return;
    }
    if (g_homelist && m.is_home() && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m)) && m.rows() == g_homen && m.selected() == g_homesel &&
        home_head_sig(m) == g_homehead) {
        // T212: a job runs from Home (a drive's list, "Create blank"): only the text of a row moves (the percentage); the rows that differ are retexted, the panel stands
        bool ok = true;
        for (int i = 0; ok && i < g_homen; i++) {
            if (home_row_sig(m, i) == g_homerowh[i]) {
                continue;
            }
            char l[120], r[120];
            bool active;
            m.row(i, l, sizeof(l), r, sizeof(r), &active, 0, 0);
            TrxRow row;
            memset(&row, 0, sizeof(row));
            row.label = l;
            row.value = r[0] ? r : 0;
            row.selected = row.strong = (i == m.selected());
            row.dim = !active;
            ok = trx_row_retext(g_home[i], row, g_homecw);
            if (ok) {
                g_homerowh[i] = home_row_sig(m, i);
            }
        }
        if (ok) {
            g_homesig = home_signature(m);
            g_inplace++;
            return;
        }
    }
    if (m.list_view() && g_lv_valid && g_last_area == m.area() && g_last_tab == m.tab() && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m)) && listv_inplace(m)) {
        return;                                                     // T262 D4: the list page stands, its rows and panel were written in place
    }
    if (g_rowlist && !m.is_home() && !m.list_view() && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m))) {
        int sel = m.selected();
        int o = g_rowsel - g_rowfirst, nw = sel - g_rowfirst;
#ifdef UI_CHEAP_DEBUG
        printf("[cheap] sel %d old %d first %d n %d sig %u want %u\n", sel, g_rowsel, g_rowfirst, g_rown,
               (unsigned)list_signature(m), (unsigned)g_rowsig);
#endif
        if (sel != g_rowsel && nw >= 0 && nw < g_rown && list_signature(m) == g_rowsig) {
            if (o >= 0 && o < g_rown) {
                trx_row_select(g_row[o], false);
                g_rowh[o] = ~g_rowh[o];                                 // what the row shows changed (its mark): the in-place pass below writes it again, whatever its hash was
            }
            trx_row_select(g_row[nw], true);
            g_rowh[nw] = ~g_rowh[nw];
            g_rowsel = sel;
            g_cheap++;
            return;
        }
        // The window moved (a step past its last row) or a row's text changed: the pane stands, the rows are written again, and
        // only those whose content differs (T104, board 2026-10-22).
        if (g_last_area == m.area() && g_last_tab == m.tab()) {
            TrxWindow w;
            uint32_t rowh[UI_ROWS_MAX], shape;
            if (list_inplace(m, &w, rowh, &shape)) {
                char left[120], right[120];
                bool active, ok = true;
                int cw = trx_content_w() - 2 * t.space_lg;
                lv_obj_t *par = lv_obj_get_parent(g_row[0]);
                const int rx = lv_obj_get_x(g_row[0]), ry = lv_obj_get_y(g_row[0]), rw = lv_obj_get_width(g_row[0]);
                for (int i = w.count; i < g_rown; i++) {                 // the window is shorter now (a heading never ends it): the rows below it go
                    lv_obj_delete(g_row[i]);
                    g_row[i] = 0;
                }
                for (int i = 0; ok && i < w.count; i++) {
                    const bool made = i >= g_rown;
                    if (!made && rowh[i] == g_rowh[i]) {
                        continue;
                    }
                    m.row(w.first + i, left, sizeof(left), right, sizeof(right), &active, 0, 0);
                    TrxRow r;
                    memset(&r, 0, sizeof(r));
                    r.label = left;
                    r.value = right[0] ? right : 0;
                    r.selected = r.strong = (w.first + i == m.selected());
                    r.dim = !active;
                    r.mark = m.row_reset(w.first + i);
                    r.heading = m.title_row(w.first + i);                // an information line is a row with its value, only a heading proper is drawn quiet
                    if (made) {                                          // the window is longer now: the row is built as the pane's build makes it, below the last
                        r.selected = r.strong = r.selected && !m.heading(w.first + i);
                        g_row[i] = trx_row_at(par, rx, ry + i * (trx_row_h() + 1), rw, r, 0);
                    } else {
                        ok = trx_row_retext(g_row[i], r, cw);
                    }
                }
                g_rown = w.count;
                if (ok) {
                    if (g_title) {
                        char tb[320];
                        const char *ft = title_fit(m.title(), tb, sizeof(tb));
                        if (strcmp(lv_label_get_text(g_title), ft)) {
                            title_set(g_title, ft);
                        }
                    }
                    if (g_pos && w.count < m.rows()) {
                        char pos[64];
                        pos_text(pos, sizeof(pos), w, m);
                        if (strcmp(lv_label_get_text(g_pos), pos)) {
                            lv_label_set_text(g_pos, pos);
                            lv_obj_set_x(g_pos, trx_content_w() - t.space_lg - trx_text_w(pos, t.caption));
                        }
                    }
                    status_set(status_line(m), m.status_bad());
                    memcpy(g_rowh, rowh, sizeof(uint32_t) * w.count);
                    for (int i = 0; i < w.count; i++) {                  // the kinds of the rows the window shows now (the bench's probe)
                        g_rowhead[i] = m.heading(w.first + i);
                        g_rowtitle[i] = m.title_row(w.first + i);
                    }
                    g_first = w.first;
                    g_rowfirst = w.first;
                    g_rowsel = m.selected();
                    g_rowsig = list_signature(m);
                    g_inplace++;
                    return;
                }
            }
        }
    }
#endif
    ui_menu_forget_rows();
    lv_obj_t *c = trx_shell(scr, info, route, m.area(), ui_shell_tab(m));

    // the title line of the pane: the tab's name, or the browser's path
    int x = t.space_lg;
    int y = t.space_md;
    char tb0[320];
    lv_obj_t *title = trx_txt(c, x, y, "", t.display, t.ink, 0);
    title_set(title, title_fit(m.title(), tb0, sizeof(tb0)));
    y += 26;

    // Home is the prototype's dashboard, not a list: the running machine on the left, the status rows
    // on the right (home.xml). Its rows are the same information lines the list would show.
    if (m.is_home()) {
        ui_home(c, m, x, y);
        return;
    }

    if (m.list_view()) {
        g_title = title;
        ui_listv(c, m, x, y);
        g_last_area = m.area();
        g_last_tab = m.tab();
        return;
    }
    int n = m.rows();
    int sel = m.selected();
    if (g_last_area != m.area() || g_last_tab != m.tab()) {
        g_first = 0;
        g_last_area = m.area();
        g_last_tab = m.tab();
    }
    int room;
    TrxWindow w = list_window(m, &room);
    g_first = w.first;

    char left[120], right[120];
    bool active;
    for (int i = 0; i < w.count; i++) {
        int idx = w.first + i;
        m.row(idx, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        TrxRow r;
        memset(&r, 0, sizeof(r));
        r.label = left;
        r.value = right[0] ? right : 0;
        r.selected = (idx == sel);
        r.strong = (idx == sel);
        r.dim = !active;
        r.mark = m.row_reset(idx);
        r.heading = m.title_row(idx);                         // an information line is a row with its value (it takes no focus), only a heading proper is drawn quiet
        r.selected = r.strong = r.selected && !m.heading(idx);
        lv_obj_t *row = trx_row_at(c, x, y, trx_content_w() - 2 * x, r, &y);
        if (i < UI_ROWS_MAX) {                               // kept for the cheap path of the next key
            g_row[i] = row;
            g_rowhead[i] = m.heading(idx);
            g_rowtitle[i] = m.title_row(idx);
        }
    }
    g_rown = w.count <= UI_ROWS_MAX ? w.count : 0;           // a window larger than the store: no cheap path
    g_rowfirst = w.first;
    g_rowsel = sel;
    g_rowsig = list_signature(m);
    g_rowlist = g_rown > 0;
    g_title = title;
    {
        TrxWindow wk;
        list_inplace(m, &wk, g_rowh, &g_shape);               // what the kept rows show, for the next key
    }

    // the window position, right of the title, as the boot menu shows it: which rows are shown and how many lie above and below. It sits on the
    // title's line (y 4 under the pane's top, 22 px high at most), the first row starts 26 px under the pane's top.
    if (w.count < n) {
        char pos[64];
        pos_text(pos, sizeof(pos), w, m);
        int pw = trx_text_w(pos, t.caption);
        g_pos = trx_txt(c, trx_content_w() - x - pw, t.space_md + 4, pos, t.caption, t.muted);
    }

    {
        const char *st = status_line(m);
        int room = trx_content_w() - 2 * x;
        g_status = trx_txt(c, x, trx_content_h() - 20 - status_extra(m), st, t.caption, m.status_bad() ? t.bad : t.muted, room);
        lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_WRAP);     // a long note takes more lines, it is not cut
        g_status_bad = m.status_bad();
        lv_obj_set_width(g_status, trx_text_w(st, t.caption) + 4 < room ? trx_text_w(st, t.caption) + 4 : room);
    }
}

// home.xml: SESSION on the left (compact: core, what is in it, the video mode; no picture area, T24.1 is not built), STATUS on the right with one action
// row per information line, and the Quick Actions button under them.
static void ui_home(lv_obj_t *c, MenuCtl &m, int x, int y)
{
    const TrxTheme &t = trx_theme();
    int h = trx_content_h() - y - t.space_md;
    int left_w = trx_content_w() - 2 * x;                    // T212 D7: no Status panel any more; the session panel is the whole of Home

    lv_obj_t *left = trx_panel(c, x, y, left_w, h, "Session",
                               m.session_word(false), t.ok);
    int iy = trx_panel_top();
#ifdef UI_HOME_PICTURE_BOX                       // the bench's control: the empty box of home.xml's picture-in-picture (T24.1, not built) comes back
    trx_box(left, t.space_lg, iy + t.space_sm, left_w - 2 * t.space_lg, h - iy - 34 - 2 * t.space_sm, t.bg, 1, t.line);
#endif
    // The session, compactly (Alex 2026-10-29: no picture area while there is no picture-in-picture): the core, what is in it, the video mode.
    // T205: the media slots and configs stand below these lines now; the compact canvas (480p) takes them closer so the last row stays above the button
    const bool cp = trx_compact();
    const int cy = iy + (cp ? t.space_sm : t.space_md), d_what = cp ? 32 : 36, d_mode = cp ? 26 : 30, d_rows = cp ? 24 : 28;
    trx_chip(left, t.space_lg, cy, g_desc ? (g_desc->short_title ? g_desc->short_title : g_desc->cor_name) : "CORE", t.accent);
    char what[120];
    m.session_line(what, sizeof(what));
    char wfit[120], mfit[120];                       // T209: one line each, cut in the middle to the panel (a label of automatic height wraps over the line below)
    lv_label_set_long_mode(trx_txt(left, t.space_lg, cy + d_what, trx_fit_mid(what, left_w - 2 * t.space_lg - 4, t.display, wfit, sizeof(wfit)), t.display, t.ink,
                                   left_w - 2 * t.space_lg), LV_LABEL_LONG_MODE_CLIP);
    char mode[80];
    m.video_line(mode, sizeof(mode));
    char mach[96];
    m.machine_line(mach, sizeof(mach));                       // T212 D3: what machine this is, from the core's own setting values
    const bool hv = mode[0] != 0;                              // T228 DC11: a core that reports no picture size has no video line; the machine line then takes its place
    if (hv) {
        lv_label_set_long_mode(trx_txt(left, t.space_lg, cy + d_what + d_mode, trx_fit_mid(mode, left_w - 2 * t.space_lg - 4, t.caption, mfit, sizeof(mfit)), t.caption, t.muted,
                                       left_w - 2 * t.space_lg), LV_LABEL_LONG_MODE_CLIP);
    }
    int n = m.rows();
    char l[120], r[120];
    bool active;
    g_homen = 0;
    // T212: the rows of the Home tab (reset, drives, configs) stand in the session panel under the video and machine lines, whichever the core has (right under the title when it has neither), as the same rows a list draws; g_home keeps one object per row.
    int ly = cy + d_what + d_mode + (hv || mach[0] ? d_rows : 0);
    if (mach[0]) {
        char mfit2[96];
        lv_label_set_long_mode(trx_txt(left, t.space_lg, cy + d_what + d_mode + (hv ? (cp ? 20 : 22) : 0), trx_fit_mid(mach, left_w - 2 * t.space_lg - 4, t.caption, mfit2, sizeof(mfit2)),
                                       t.caption, t.muted, left_w - 2 * t.space_lg), LV_LABEL_LONG_MODE_CLIP);
        if (hv) {
            ly += cp ? 18 : 20;
        }
    }
    for (int i = 0; i < n; i++) {
        m.row(i, l, sizeof(l), r, sizeof(r), &active, 0, 0);
        TrxRow row;
        memset(&row, 0, sizeof(row));
        row.label = l;
        row.value = r[0] ? r : 0;
        row.selected = row.strong = (i == m.selected());
        row.dim = !active;
        lv_obj_t *ar = trx_row_at(left, t.space_lg, ly, left_w - 2 * t.space_lg, row, &ly);
        if (i < UI_HOME_MAX) {
            g_home[i] = ar;
            g_homen = i + 1;
        }
    }
    if (ly + t.space_sm <= h - 28) {                // the button stands under the rows where they leave it room; a core with many rows (A1200 at 480p: ten) keeps
        trx_button(left, t.space_md, h - 28, 0, "F3  Library", false);       // the rows, and the rail names F3 LIBRARY as well
    }
    g_homelist = g_homen == n;                     // every row kept: a selection move needs no build
    g_homehead = home_head_sig(m);
    g_homecw = left_w - 2 * t.space_lg;
    for (int i = 0; i < g_homen; i++) {
        g_homerowh[i] = home_row_sig(m, i);
    }
    g_homesel = m.selected();
    g_homesig = home_signature(m);
}


// ---- the question an application asks (T104.3 D4) --------------------------------------------------------------------------------------------------
// T167: the question and the bar are cards in the overlay container (trx_overlay(): last child of the screen, where LVGL's search for the topmost
// covering object finds them). Three objects, each changed on its own: the veil that dims the screen (made once per question, never drawn again by a
// tick), the question's card (built again when the question changes: a key, a new row) and the bar's card (its labels and its fill change in place).
// A tick of the bar therefore draws a few hundred pixels of the bar's own card and nothing of the screen behind it.
static uint32_t g_dlg_rev;
static bool g_dlg_up;
static uint32_t g_dlg_gen;                       // trx_overlay_gen() the objects below were built in: a frame rebuild deletes them with the container
static lv_obj_t *g_veil, *g_qcard, *g_bcard, *g_bl1, *g_bl2, *g_bfill;
static uint32_t g_qsig;
static lv_obj_t *g_qtxt;                                  // the text label of the open question
// T260 fix (Rick and Alex, 2026-11-27: about 2 s per key in the diag core's A/V panel): the rows of a choose or edit list are kept, so a key that moves the
// selection, scrolls the window or changes a value retexts them (trx_row_retext) instead of building the card again over the veil.
static lv_obj_t *g_qrow[DLG_ITEMS_MAX];
static int g_qrown, g_qroww;
static lv_obj_t *g_qpos;                                  // the "a-b of n" label of the kept list, 0 when the list fits
static int g_qposr;                                       // the right edge of that label inside the card
static uint32_t g_qinplace;                               // keys served by retexting the kept rows (the bench's count)
uint32_t ui_dialog_inplace(void) { return g_qinplace; }

static int g_bkind = -1;                         // the bar's place depends on whether a question is open: 1 alone at the foot, 0 below a question

static void dialog_objs_forget(void)
{
    g_veil = g_qcard = g_bcard = g_bl1 = g_bl2 = g_bfill = 0;
    g_qsig = 0;
    g_qtxt = 0;
    g_qrown = 0;
    g_qpos = 0;
    g_bkind = -1;
}

static void dialog_forget(void)
{
    if (g_dlg_up && have_ui && g_dlg_gen == trx_overlay_gen()) {
        if (g_veil) lv_obj_delete(g_veil);
        if (g_qcard) lv_obj_delete(g_qcard);
        if (g_bcard) lv_obj_delete(g_bcard);
    }
    dialog_objs_forget();
    g_dlg_up = false;
    g_dlg_rev = 0;
}

static void dlg_hint(lv_obj_t *card, int w, int h, const char *s)
{
    const TrxTheme &t = trx_theme();
    trx_txt(card, t.space_md, h - 22, s, t.caption, t.muted, w - 2 * t.space_md);
}

// A choose or edit list keeps its row objects: the window, the selection and the rows' texts are written in place (dialog_rows_retext).
static bool list_kind(const DlgView &v)
{
    return (v.kind == DK_CHOOSE || v.kind == DK_EDIT) && v.rows > 0;
}

// What the question's card shows (everything of the view except the bar). For a list kind only its shape: the rows' content is retexted in place.
static uint32_t question_sig(const DlgView &v)
{
    uint32_t h = 2166136261u;
    auto add = [&](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) {
            h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
        }
    };
    add(&v.kind, sizeof(v.kind));
    add(v.title, strlen(v.title) + 1);
    if (v.live) {                                        // a live text changes in place (g_qtxt); only its length in lines is part of the card
        int lines = 1;
        for (const char *p = v.text; *p; p++) {
            lines += *p == '\n';
        }
        add(&lines, sizeof(lines));
    } else {
        add(v.text, strlen(v.text) + 1);
    }
    add(v.input, strlen(v.input) + 1);
    add(&v.caret, sizeof(v.caret));
    add(&v.rows, sizeof(v.rows));
    if (list_kind(v)) {
        bool pos = v.nitems > v.rows;                    // the "a-b of n" label is there or not; its text changes in place
        add(&pos, sizeof(pos));
        add(&v.buttons, sizeof(v.buttons));
        return h ? h : 1;
    }
    add(&v.nitems, sizeof(v.nitems));
    add(&v.first, sizeof(v.first));
    add(&v.sel, sizeof(v.sel));
    for (int i = 0; i < v.rows && i < DLG_ITEMS_MAX; i++) {
        add(v.row[i], strlen(v.row[i]) + 1);
        if (v.row_val[i]) {
            add(v.row_val[i], strlen(v.row_val[i]) + 1);
        }
        add(&v.row_dir[i], 1);
    }
    add(&v.buttons, sizeof(v.buttons));
    add(&v.remaining_s, sizeof(v.remaining_s));
    return h ? h : 1;
}

// The card of a list kind stays: its kept rows take the window's labels, values and selection, the "a-b of n" label its text. False: a row cannot be
// changed in place (trx_row_retext) or the card holds no kept rows; the caller builds the card again.
static bool dialog_rows_retext(const DlgView &v)
{
    if (!g_qcard || g_qrown != v.rows || g_qrown > DLG_ITEMS_MAX) {
        return false;
    }
    for (int i = 0; i < g_qrown; i++) {
        TrxRow r;
        memset(&r, 0, sizeof(r));
        r.label = v.row[i];
        r.value = v.row_val[i];
        r.selected = r.strong = (v.first + i == v.sel);
        if (!trx_row_retext(g_qrow[i], r, g_qroww)) {
            return false;
        }
    }
    if (g_qpos) {
        const TrxTheme &t = trx_theme();
        char pos[32];
        snprintf(pos, sizeof(pos), "%d-%d of %d", v.first + 1, v.first + v.rows, v.nitems);
        if (strcmp(lv_label_get_text(g_qpos), pos)) {
            lv_label_set_text(g_qpos, pos);
            lv_obj_set_x(g_qpos, g_qposr - trx_text_w(pos, t.caption));
        }
    }
    g_qinplace++;
    return true;
}

static lv_obj_t *question_card(lv_obj_t *top, const DlgView &v)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *card;
    {
        int w = SW - 40 < 600 ? SW - 40 : 600;                 // 600 wide on the full canvas, the canvas less 20 a side below it
        int lines = v.text[0] ? 1 : 0;
        for (const char *p = v.text; *p; p++) {
            lines += *p == '\n';
        }
        int rows = (v.kind == DK_CHOOSE || v.kind == DK_EDIT || v.kind == DK_PICK_FILE || v.kind == DK_PICK_FOLDER) ? (v.rows < 1 ? 1 : v.rows) : 0;
        int h = 22 + 30 + (v.text[0] ? 16 * (lines < 14 ? lines : 14) + 8 : 0) + (rows ? rows * (trx_row_h() + 1) + 24 : 0) +
                (v.kind == DK_TEXT ? 44 : 0) + (v.kind == DK_BUTTONS || v.kind == DK_MESSAGE ? 44 : 0) + 34;
        if (h > SH - 70) {
            h = SH - 70;
        }
        int x = (SW - w) / 2, y = (SH - h) / 2;
        card = trx_box(top, x, y, w, h, t.surface2, 1, t.accent);
        lv_obj_set_style_radius(card, t.radius, 0);
        trx_txt(card, t.space_lg, t.space_md, v.title, t.display, t.ink, w - 2 * t.space_lg);
        int cy = t.space_md + 30;
        if (v.text[0]) {
            lv_obj_t *tl = trx_txt(card, t.space_lg, cy, v.text, t.body, t.muted, w - 2 * t.space_lg);
            g_qtxt = tl;
            lv_label_set_long_mode(tl, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_width(tl, w - 2 * t.space_lg);
            cy += 16 * (lines < 14 ? lines : 14) + 8;
        }
        if (v.kind == DK_TEXT) {
            lv_obj_t *box = trx_box(card, t.space_lg, cy, w - 2 * t.space_lg, 28, t.bg, 1, t.accent);
            char shown[DLG_TEXT_MAX + 4];
            snprintf(shown, sizeof(shown), "%s_", v.input);
            trx_txt(box, t.space_sm, 6, shown, t.body, t.ink, w - 2 * t.space_lg - 2 * t.space_sm);
            cy += 40;
        }
        if (rows) {
            if (v.kind != DK_CHOOSE && v.kind != DK_EDIT) {
                trx_txt(card, t.space_lg, cy, v.input, t.caption, t.accent, w - 2 * t.space_lg);
                cy += 18;
            }
            const bool keep = list_kind(v) && rows <= DLG_ITEMS_MAX;
            for (int i = 0; i < rows; i++) {
                TrxRow r;
                memset(&r, 0, sizeof(r));
                r.label = v.row[i];
                r.value = v.row_val[i];
                r.selected = r.strong = (v.first + i == v.sel);
                lv_obj_t *ro = trx_row_at(card, t.space_lg, cy, w - 2 * t.space_lg, r, &cy);
                if (keep) {
                    g_qrow[i] = ro;
                }
            }
            g_qrown = keep ? rows : 0;
            g_qroww = w - 2 * t.space_lg;
            if (v.nitems > rows) {
                char pos[32];
                snprintf(pos, sizeof(pos), "%d-%d of %d", v.first + 1, v.first + rows, v.nitems);
                g_qpos = trx_txt(card, w - t.space_lg - trx_text_w(pos, t.caption), t.space_md + 6, pos, t.caption, t.muted);
                g_qposr = w - t.space_lg;
            }
        }
        if (v.kind == DK_MESSAGE) {
            trx_btn(card, w - t.space_lg - 90, h - 54, 90, "OK", TB_PRIMARY, true, false);
        } else if (v.kind == DK_BUTTONS) {
            bool yn = (v.buttons & SVC_BUTTON_YES) && (v.buttons & SVC_BUTTON_NO);
            if (yn) {
                trx_btn(card, w - t.space_lg - 200, h - 54, 90, "YES", TB_PRIMARY, v.caret == 0, false);
                trx_btn(card, w - t.space_lg - 100, h - 54, 90, "NO", TB_SECONDARY, v.caret == 1, false);
            } else {
                trx_btn(card, w - t.space_lg - 90, h - 54, 90, "OK", TB_PRIMARY, true, false);
            }
            if (v.remaining_s > 0) {                           // a timed question (T175 prompts): the seconds left, left of the buttons
                char left[40];
                snprintf(left, sizeof(left), "%d s left", v.remaining_s);
                trx_txt(card, t.space_lg, h - 46, left, t.body, v.remaining_s <= 10 ? t.warn : t.muted, 150);
            }
        }
        const char *hint = v.kind == DK_CHOOSE ? "UP/DOWN  MOVE   RETURN  CHOOSE   <-  CANCEL"
                         : v.kind == DK_EDIT ? "UP/DOWN  ROW   LEFT/RIGHT  CHANGE   RETURN  DO   <-  CLOSE"
                         : v.kind == DK_TEXT ? "TYPE   DEL  BACK   RETURN  OK   <-  CANCEL"
                         : v.kind == DK_PICK_FILE || v.kind == DK_PICK_FOLDER ? "RETURN  OPEN OR PICK   <-  ONE FOLDER UP   R/S  CANCEL"
                         : v.kind == DK_BUTTONS ? "LEFT/RIGHT  FOCUS   RETURN  ANSWER   Y  YES   N  NO"
                         : "RETURN  OK";
        dlg_hint(card, w, h, hint);
    
    }
    return card;
}

void ui_dialog_render(AppDlg &d)
{
    if (!have_ui) {
        return;
    }
    const DlgView &v = d.view();
    if (v.kind == DK_NONE && !v.bar) {
        dialog_forget();
        return;
    }
    if (g_dlg_up && g_dlg_gen != trx_overlay_gen()) {      // a frame rebuild took the container and the cards with it
        dialog_objs_forget();
        g_dlg_rev = 0;
    }
    if (g_dlg_up && d.revision() == g_dlg_rev) {
        return;
    }
#ifdef UI_DIALOG_REBUILD             // the bench's control: veil and both cards built again on every revision, as before T167
    if (g_dlg_up && g_dlg_gen == trx_overlay_gen()) {
        dialog_forget();
    }
#endif
    const TrxTheme &t = trx_theme();
    lv_obj_t *top = trx_overlay();
    g_dlg_gen = trx_overlay_gen();
    g_dlg_up = true;
    g_dlg_rev = d.revision();
    if (v.kind != DK_NONE) {
        uint32_t sig = question_sig(v);
        if (!g_veil) {
            g_veil = trx_box(top, 0, 0, SW, SH, t.bg);                                       // a dim veil: the screen behind shows through
            lv_obj_set_style_bg_opa(g_veil, LV_OPA_50, 0);
            lv_obj_move_to_index(g_veil, 0);
        }
        if (!g_qcard || sig != g_qsig || (list_kind(v) && !dialog_rows_retext(v))) {
            if (g_qcard) {
                lv_obj_delete(g_qcard);
            }
            g_qtxt = 0;
            g_qrown = 0;
            g_qpos = 0;
            g_qcard = question_card(top, v);
            g_qsig = sig;
            if (g_bcard) {
                lv_obj_move_foreground(g_bcard);
            }
        } else if (v.live && g_qtxt && strcmp(lv_label_get_text(g_qtxt), v.text) != 0) {
            lv_label_set_text(g_qtxt, v.text);                                               // a live text: the label only, the card stays
        }
    } else if (g_veil) {                                                                       // only the bar is left
        lv_obj_delete(g_veil);
        lv_obj_delete(g_qcard);
        g_veil = g_qcard = g_qtxt = g_qpos = 0;
        g_qrown = 0;
        g_qsig = 0;
    }
    if (v.bar) {                                                                               // the bar: below the question, or alone at the foot of the screen
        int w = SW - 40 < 560 ? SW - 40 : 560, h = 74, x = (SW - w) / 2, y = v.kind == DK_NONE ? SH - 28 - h - 24 : SH - 28 - h - 6;
        int bw = w - 2 * t.space_md;
        int alone = v.kind == DK_NONE;
        if (g_bcard && g_bkind != alone) {
            lv_obj_delete(g_bcard);
            g_bcard = 0;
        }
        if (!g_bcard) {
            g_bcard = trx_box(top, x, y, w, h, t.surface2, 1, t.line);
            lv_obj_set_style_radius(g_bcard, t.radius, 0);
            g_bl1 = trx_txt(g_bcard, t.space_md, t.space_sm, v.bar_title, t.body, t.ink, bw);
            g_bl2 = trx_txt(g_bcard, t.space_md, t.space_sm + 20, v.bar_text, t.caption, t.muted, bw);
            trx_box(g_bcard, t.space_md, h - 22, bw, 8, t.line);
            g_bfill = trx_box(g_bcard, t.space_md, h - 22, 1, 8, t.accent);
            lv_obj_set_flag(g_bfill, LV_OBJ_FLAG_HIDDEN, true);
            g_bkind = alone;
        }
        if (strcmp(lv_label_get_text(g_bl1), v.bar_title) != 0) {
            lv_label_set_text(g_bl1, v.bar_title);
        }
        if (strcmp(lv_label_get_text(g_bl2), v.bar_text) != 0) {
            lv_label_set_text(g_bl2, v.bar_text);
        }
        int fill = v.bar_total > 0 ? (int)((int64_t)bw * v.bar_value / v.bar_total) : 0;
        if (fill > 0) {
            if (lv_obj_get_width(g_bfill) != fill) {
                lv_obj_set_width(g_bfill, fill);
            }
            if (lv_obj_has_flag(g_bfill, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_set_flag(g_bfill, LV_OBJ_FLAG_HIDDEN, false);
            }
        } else if (!lv_obj_has_flag(g_bfill, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_flag(g_bfill, LV_OBJ_FLAG_HIDDEN, true);
        }
    } else if (g_bcard) {
        lv_obj_delete(g_bcard);
        g_bcard = g_bl1 = g_bl2 = g_bfill = 0;
        g_bkind = -1;
    }
}

// ---- the menu's short message (T160, T187 D4) ----------------------------------------------------------------------------------------------------
// It is a chip in the top bar (shell_info), drawn with the shell: nothing is laid over the rows, the status line does not repeat it. This call only makes sure the
// bar is drawn again when the line came or ran out, which ui_menu_render does with the shell's chip signature.
void ui_toast_render(MenuCtl &m)
{
    (void)m;
}

void ui_menu_visible(bool on)
{
    if (have_ui && !on) {
        g_msg_label = g_msg_panel = 0;
        // nothing of the menu stays on the layer, and the next render has to draw the frame again
        dialog_forget();
        trx_shell_forget();
        ui_menu_forget_rows();
        lv_obj_clean(scr);
    }
}

void ui_splash_open(const char *what, const char *version, int nsteps)
{
    if (have_ui) {
        trx_splash_open(trx_overlay(), "TRX64", what, version, nsteps);
        lv_refr_now(NULL);
    }
}

void ui_splash_step(int step, const char *label)
{
    if (have_ui && trx_splash_up()) {
        trx_splash_step(step, label);
        lv_refr_now(NULL);
    }
}

void ui_splash_close(void)
{
    trx_splash_close();
}

static const char *g_msg_foot;                     // the foot of the message pane: the keys of a question (T228: the picture question names its keys at the foot, as every context does)

void ui_message(const char *title, const char *text, bool bad)
{
    printf("[coreapp] %s: %s\n", title, text);
    if (!have_ui) {
        return;
    }
    trx_splash_close();                            // a message is on the pane; the splash would cover it
    snprintf(msg_title_s, sizeof(msg_title_s), "%s", title ? title : "");
    snprintf(msg_text_s, sizeof(msg_text_s), "%s", text ? text : "");
    msg_bad = bad;
    msg_up = true;
    pq_up = false;                                 // the picture question sets it again after its own message
    ui_menu_forget_rows();                         // the pane is the message's now, not the list's

    const TrxTheme &t = trx_theme();
    TrxShellInfo info;
    shell_info(&info, "/ UI", "HELD", 0);
    info.footer_right = g_msg_foot;
    info.footer_left = 0;                          // a message pane closes with no key of its own; the text names the keys that do something
    TrxRoute empty = { 0, 0 };
    lv_obj_t *c = trx_shell(scr, info, empty, -1, -1);
    int x = t.space_xl;
    lv_obj_t *panel = trx_box(c, x, 60, trx_content_w() - 2 * x, 220, t.surface);
    trx_txt(panel, t.space_lg, t.space_lg, msg_title_s, t.display, bad ? t.bad : t.ink,
            trx_content_w() - 2 * x - 2 * t.space_lg);
    lv_obj_t *l = trx_txt(panel, t.space_lg, t.space_lg + 34, msg_text_s, t.heading, t.muted,
                          trx_content_w() - 2 * x - 2 * t.space_lg);   // T187 F1: the heading size, as the 4 lines are few in a 220 px panel
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, trx_content_w() - 2 * x - 2 * t.space_lg);
    lv_obj_update_layout(l);
    int ph = t.space_lg + 34 + lv_obj_get_height(l) + t.space_lg;
    int cap = trx_content_h() - 60 - t.space_lg;     // what the pane holds under its top: the content area less the margin
    lv_obj_set_height(panel, ph < cap ? ph : cap);
    g_msg_label = l;
    g_msg_panel = panel;
}

void ui_picture_question(int left_s, const char *now, const char *prev)
{
    char txt[200];
    picmodes_confirm(left_s, now, prev, txt, sizeof(txt));
    if (pq_up && have_ui && g_msg_label) {                 // T187 F1: the countdown of every second changes the text in place; clean and rebuild showed an empty pane for a frame (the capture of the NES caught it)
        snprintf(msg_text_s, sizeof(msg_text_s), "%s", txt);
        printf("[coreapp] Keep this picture?: %s\n", txt);
        lv_label_set_text(g_msg_label, msg_text_s);
        return;
    }
    g_msg_foot = "RETURN KEEP  LEFT/ESC BACK NOW";
    ui_message("Keep this picture?", txt, false);
    g_msg_foot = 0;
    pq_up = true;
}

const char *ui_chip_say_text(void) { return chip_say; }
const char *ui_message_text(void) { return msg_text_s; }

void ui_message_clear(void)
{
    msg_up = false;
    pq_up = false;
    g_msg_label = g_msg_panel = 0;
    if (have_ui) {
        trx_shell_forget();
        ui_menu_forget_rows();
        lv_obj_clean(scr);
    }
}

int ui_name_squeeze(void) { return -1; }                     // no setting sets it (T239.3 D56): trx_fit_name takes -1, T209's "start...end.ext" cut

// ---- the overlay service on LVGL (T256) -------------------------------------------------------------------------------------------------------------------------------
// The service (overlay.cc) decides; this is its layer: the holder, the key and the alpha go to the port, the still lies on LVGL's bottom layer, the elements are images on the screen
// (while the menu is closed), and the menu is blended over the still with the screen's opacity. Until a core runs (ui_overlay_start) the functions below act on the port as before.
static OverlaySvc *g_svc;
static bool g_ov_ui;                                         // the service gave LVGL the layer
static bool g_scene;                                         // the screen shows the still and the elements, no menu widget
static bool g_scr_saved;
static lv_color_t g_scr_color;
static lv_opa_t g_scr_opa;
static lv_obj_t *g_still_img;                                // on the display's bottom layer
static lv_obj_t *g_el_img[OVL_ELEMENTS_MAX];
static const uint16_t *g_el_px[OVL_ELEMENTS_MAX];
static uint32_t g_el_serial[OVL_ELEMENTS_MAX];
static lv_image_dsc_t g_el_dsc[OVL_ELEMENTS_MAX], g_still_dsc;
static const uint16_t *g_still_px;
static uint32_t g_still_serial;

static bool ov_on(void) { return g_svc && g_svc->enabled(); }

// The image cache is off in lv_conf.h (LV_CACHE_DEF_SIZE is not set, so 0): a surface changed in place is read again by the next draw, nothing is decoded or kept.
static void dsc_set(lv_image_dsc_t *d, const OvDraw &o)
{
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = (uint32_t)o.w;
    d->header.h = (uint32_t)o.h;
    d->header.stride = (uint32_t)o.w * 2;
    d->data_size = (uint32_t)o.w * (uint32_t)o.h * 2;
    d->data = (const uint8_t *)o.px;
}

static void el_clear(int i)
{
    if (g_el_img[i]) {
        lv_obj_delete(g_el_img[i]);
        g_el_img[i] = 0;
    }
    g_el_px[i] = 0;
}

static void still_clear(void)
{
    if (g_still_img) {
        lv_obj_delete(g_still_img);
        g_still_img = 0;
    }
    g_still_px = 0;
}

static void scr_forget(void)
{
    msg_up = false;
    pq_up = false;
    g_msg_label = g_msg_panel = 0;
    trx_shell_forget();
    ui_menu_forget_rows();
    lv_obj_clean(scr);
    memset(g_el_img, 0, sizeof(g_el_img));                  // lv_obj_clean deleted them with the screen's children
    memset(g_el_px, 0, sizeof(g_el_px));
}

static void ov_apply(const OvPlan &p, const OvScene &sc)
{
    if (!have_ui) {
        return;
    }
    if (!g_scr_saved) {
        g_scr_saved = true;
        g_scr_color = lv_obj_get_style_bg_color(scr, LV_PART_MAIN);
        g_scr_opa = lv_obj_get_style_bg_opa(scr, LV_PART_MAIN);
    }
    if (p.holder == OVH_UI) {
        trx_lv_resume();
        g_ov_ui = true;
    }
    trx_lv_set_key(p.key, p.key ? TRX_LIVE_KEY_565 : 0);
    trx_lv_set_alpha(p.alpha);
    // the still, on the bottom layer, under the screen
    if (p.draw_still && sc.has_still) {
        lv_obj_t *bl = lv_display_get_layer_bottom(lv_display_get_default());
        if (!g_still_img) {
            g_still_img = lv_image_create(bl);
            lv_obj_set_flag(g_still_img, LV_OBJ_FLAG_CLICKABLE, false);
            g_still_px = 0;
        }
        if (g_still_px != sc.still.px || g_still_serial != sc.still.serial) {
            dsc_set(&g_still_dsc, sc.still);
            lv_image_set_src(g_still_img, &g_still_dsc);
            lv_obj_set_pos(g_still_img, sc.still.x, sc.still.y);
            lv_obj_invalidate(g_still_img);
            g_still_px = sc.still.px;
            g_still_serial = sc.still.serial;
        }
    } else {
        still_clear();
    }
    // the screen: the open menu (its own widgets, blended over the still), or the scene
    if (p.scene != g_scene) {
        scr_forget();
        g_scene = p.scene;
        lv_obj_set_style_bg_color(scr, g_scene ? lv_color_hex(TRX_LIVE_KEY_RGB) : g_scr_color, 0);
        lv_obj_set_style_bg_opa(scr, g_scene ? LV_OPA_COVER : g_scr_opa, 0);
        g_look_dirty = !g_scene;                              // the menu is built again for the new screen
    }
    if (g_scene) {
        lv_obj_set_style_bg_opa(scr, p.draw_still ? LV_OPA_TRANSP : LV_OPA_COVER, 0);   // over the still the screen is transparent; else the key colour lets the picture through
        lv_obj_set_style_opa(scr, LV_OPA_COVER, 0);
        for (int i = 0; i < OVL_ELEMENTS_MAX; i++) {
            if (i >= sc.n || !p.draw_elems) {
                el_clear(i);
                continue;
            }
            const OvDraw &o = sc.el[i];
            if (g_el_img[i] && g_el_px[i] == o.px && g_el_serial[i] == o.serial) {
                continue;
            }
            if (!g_el_img[i]) {
                g_el_img[i] = lv_image_create(scr);
                lv_obj_set_flag(g_el_img[i], LV_OBJ_FLAG_CLICKABLE, false);
            }
            dsc_set(&g_el_dsc[i], o);
            lv_image_set_src(g_el_img[i], &g_el_dsc[i]);
            lv_obj_set_pos(g_el_img[i], o.x, o.y);
            lv_obj_invalidate(g_el_img[i]);
            g_el_px[i] = o.px;
            g_el_serial[i] = o.serial;
        }
    } else {
        lv_obj_set_style_opa(scr, p.draw_still ? (lv_opa_t)p.menu_opa : LV_OPA_COVER, 0);
    }
    if (p.holder == OVH_CORE) {
        trx_lv_suspend();
        g_ov_ui = false;
    }
    lv_obj_invalidate(scr);
}

// Text into a library's surface with the font of the menu's captions: LVGL's label drawing on a layer made over the library's buffer (what lv_canvas does, without the widget).
static void ov_text(const SvcSurface &sf, int x, int y, const char *s, uint16_t colour)
{
    lv_display_t *d = lv_display_get_default();
    if (!have_ui || !d || !sf.px || !sf.w || !sf.h) {
        return;
    }
    lv_draw_buf_t buf;
    if (lv_draw_buf_init(&buf, sf.w, sf.h, LV_COLOR_FORMAT_RGB565, (uint32_t)sf.w * 2, sf.px, sf.bytes) != LV_RESULT_OK) {
        return;
    }
    const lv_area_t full = { 0, 0, (int32_t)sf.w - 1, (int32_t)sf.h - 1 };
    lv_layer_t layer;
    lv_layer_init(&layer);
    layer.draw_buf = &buf;
    layer.display = d;
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    layer.buf_area = full;
    layer._clip_area = full;
    layer.phy_clip_area = full;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = trx_theme().caption;
    ld.color = lv_color_make((uint8_t)(((colour >> 11) & 31) * 255 / 31), (uint8_t)(((colour >> 5) & 63) * 255 / 63), (uint8_t)((colour & 31) * 255 / 31));
    ld.text = s;
    const lv_area_t at = { x, y, (int32_t)sf.w - 1, (int32_t)sf.h - 1 };
    lv_draw_label(&layer, &ld, &at);
    layer.all_tasks_added = true;
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        if (!lv_draw_dispatch_layer(d, &layer)) {
            lv_draw_wait_for_finish();
            lv_draw_dispatch_request();
        }
    }
}

class UiLayer : public OverlayLayer {
public:
    void apply(const OvPlan &p, const OvScene &s) { ov_apply(p, s); }
    void text(const SvcSurface &sf, int x, int y, const char *s, uint16_t colour) { ov_text(sf, x, y, s, colour); }
};
static UiLayer g_ui_layer;

OverlaySvc *ui_overlay(void) { return g_svc; }
bool ui_still_under(void) { return g_svc && g_svc->enabled() && g_svc->plan().draw_still; }

void ui_overlay_start(OverlayHost *host, const UiElements *table, bool own_picture)
{
    if (!g_svc) {
        g_svc = new OverlaySvc(&g_ui_layer, host);
    }
    g_svc->look((uint8_t)(255 - g_look_t * 64 + (g_look_t ? 1 : 0)));
    g_svc->set_table(table, own_picture);
}

int ui_canvas_w(void) { return trx_canvas_w(); }
int ui_canvas_h(void) { return trx_canvas_h(); }

void ui_overlay_frozen(bool on)
{
    if (ov_on()) {
        g_svc->frozen(on);
    }
}

void ui_overlay_closing(void)
{
    if (ov_on()) {
        g_svc->closing();
    }
}

// the layer's global alpha from the fixed transparency (position 1 of 0, 25, 50, 75 % = 25 %)
static void apply_alpha(void)
{
    const uint8_t a = (uint8_t)(255 - g_look_t * 64 + (g_look_t ? 1 : 0));
    if (ov_on()) {
        g_svc->look(a);
    } else {
        trx_lv_set_alpha(a);
    }
}

void ui_show(void)
{
    if (have_ui) {
        if (ov_on()) {
            g_svc->menu(true);
        } else {
            trx_lv_resume();
        }
        apply_alpha();
    }
}

void (*ui_live_key_changed)(bool behind) = 0;
static bool g_key_on;
void ui_live_key(bool on)
{
    if (ov_on()) {
        g_svc->live_key(on);
    } else {
        trx_lv_set_key(on, on ? TRX_LIVE_KEY_565 : 0);
    }
    if (on != g_key_on) {
        g_key_on = on;
        if (ui_live_key_changed) {
            ui_live_key_changed(on);
        }
    }
}

// The machine is paused with the menu closed: the PAUSED tag is an element of the base UI TOML (D5), shown through the service like a library's. Its surface is painted here, with the
// base's own text call. Over the running picture of a C64-type core and over a layer core's picture the tag shows only while the machine stands.
static uint16_t g_tag_px[126 * 30];
static int g_tag_el;
static SvcSurface g_tag_sf = { g_tag_px, sizeof(g_tag_px), 126, 30 };
void ui_paused_tag(bool on)
{
    if (!have_ui || !ov_on()) {
        return;
    }
    if (on && g_tag_el <= 0) {
        const uint16_t box = 0x1082;                                           // 0x101018 as RGB565
        for (unsigned i = 0; i < sizeof(g_tag_px) / 2; i++) {
            g_tag_px[i] = box;
        }
        g_svc->text(OVL_OWNER_BASE, &g_tag_sf, 12, 6, "PAUSED", 0xFFFF);
        g_tag_el = g_svc->show(OVL_OWNER_BASE, "paused", &g_tag_sf);
        if (g_tag_el <= 0) {
            int ew = 0, eh = 0;
            g_svc->size_of("paused", &ew, &eh);
            printf("[overlay] the PAUSED tag is not shown: %s (the element \"paused\" of the UI TOML is %d x %d, the tag's surface %d x %d)\n", OverlaySvc::error(g_tag_el), ew, eh, g_tag_sf.w, g_tag_sf.h);
            g_tag_el = 0;
        }
        g_svc->frame();                                                        // the text is in the surface before the layer draws it
    }
    g_svc->paused(on);
    if (!on && g_tag_el > 0) {
        g_svc->remove(OVL_OWNER_BASE, g_tag_el);
        g_tag_el = 0;
    }
}

void ui_hide(void)
{
    if (have_ui) {
        if (ov_on()) {
            g_svc->menu(false);
        } else {
            trx_lv_suspend();
        }
    }
}

int ui_poll(void)
{
    return have_ui ? trx_lv_handler() : 20;
}

// After a key: draw now instead of at the next refresh tick, which is what makes the menu feel immediate
// (the host application's menu does the same, hostui.cc:1726).
void ui_render_now(void)
{
    if (have_ui) {
        trx_lv_refresh_now();
    }
}
