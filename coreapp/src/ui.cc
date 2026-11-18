// The core application's screen (T93 D8/D9, rebuilt on the shared view layer for T104 5.8): the prototype's shell -
// top bar, the navigation rail of areas, the numbered tab strip, the content pane and the key legend - drawn through
// app/view/trxview.cc, which the host application draws through too. What is on a tab comes from MenuCtl, which comes
// from the descriptor; nothing here knows what a core is.
#include <stdlib.h>
#include "ui.h"
#include "picmodes.h"
#include "trx_lv_port.h"
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
static char msg_title_s[80], msg_text_s[400];
static bool msg_bad;
static const CoreDesc *g_desc;
static volatile int g_look_t = 1, g_look_scheme = 1;           // "Overlay Transparency" and "Color Scheme" (ui_set_look)
static volatile bool g_look_dirty = true;
static void apply_alpha(void);

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

static void build_route(const MenuTree &tr, bool app_mode)
{
    for (int a = 0; a < tr.nareas && a < TRX_AREAS_MAX; a++) {
        int nv = 0;
        for (int b = 0; b < tr.a[a].ntabs && b < MENU_TABS_MAX; b++) {
            vis_map[a][b] = -1;
            if (tr.a[a].tabs[b].page || layout_has(tr.a[a].tabs[b].layout, "hidden")) {      // T187 D2: a page a row opens has no place on the strip
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

uint32_t ui_cheap_moves(void) { return g_cheap; }
uint32_t ui_inplace_moves(void) { return g_inplace; }

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

static int list_foot(void) { return 24; }

static TrxWindow list_window(MenuCtl &m, int *room_out)
{
    const TrxTheme &t = trx_theme();
    int room = trx_content_h() - (t.space_md + 26) - list_foot();
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

// Does the pane stand as it is - same title, status, row count, window size and row kinds - so that the rows alone may be
// written again? Fills the per-row hashes of what each row shows. (T104, board 2026-10-22: scrolling past the window's last
// row rebuilt the whole pane per step; the rows are the same objects, only their text moved.)
static bool list_inplace(MenuCtl &m, TrxWindow *w_out, uint32_t *rowh, uint32_t *shape_out)
{
    int room;
    TrxWindow w = list_window(m, &room);
    if (w.count != g_rown || w.count > UI_ROWS_MAX) {
        return false;
    }
    uint32_t sh = 2166136261u;                                          // the title is written in place (a folder with as many rows is the same pane)
    sh = (sh ^ (uint32_t)w.count) * 16777619u;
    sh = (sh ^ (uint32_t)(w.count < m.rows())) * 16777619u;
    char left[120], right[120];
    bool active;
    for (int i = 0; i < w.count; i++) {
        m.row(w.first + i, left, sizeof(left), right, sizeof(right), &active, 0, 0);
        sh = (sh ^ (uint32_t)((m.heading(w.first + i) ? 3 : 1) | (m.row_reset(w.first + i) ? 4 : 0))) * 16777619u;     // a heading, a row, with the ring or not (a value is made or removed in place)
        uint32_t h = 2166136261u;
        h = sig_str(h, left);
        h = sig_str(h, right);
        h = (h ^ (uint32_t)active ^ (uint32_t)((w.first + i == m.selected()) << 1)) * 16777619u;
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
    p->rows_bottom = trx_content_h() - list_foot();
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
    build_route(m.tree(), m.app_mode());

    if (!screens_wanted(m) && !m.is_home()) {                       // T209 D4: the foot names the paging keys of a list longer than its window; the window is known before the shell is made
        int room0;
        TrxWindow w0 = list_window(m, &room0);
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
    if (g_rowlist && !m.is_home() && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m))) {
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
                for (int i = 0; ok && i < w.count; i++) {
                    if (rowh[i] == g_rowh[i]) {
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
                    ok = trx_row_retext(g_row[i], r, cw);
                }
                if (ok) {
                    if (g_title && strcmp(lv_label_get_text(g_title), m.title())) {
                        int room = trx_content_w() - 2 * t.space_lg - 240;
                        int tw = trx_text_w(m.title(), t.display) + 4;
                        lv_obj_set_width(g_title, tw < room ? tw : room);
                        lv_label_set_text(g_title, m.title());
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
    int title_room = trx_content_w() - 2 * x - 240;
    lv_obj_t *title = trx_txt(c, x, y, m.title(), t.display, t.ink, title_room);
    lv_obj_set_width(title, trx_text_w(m.title(), t.display) + 4 < title_room ? trx_text_w(m.title(), t.display) + 4 : title_room);
    y += 26;

    // Home is the prototype's dashboard, not a list: the running machine on the left, the status rows
    // on the right (home.xml). Its rows are the same information lines the list would show.
    if (m.is_home()) {
        ui_home(c, m, x, y);
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
        g_status = trx_txt(c, x, trx_content_h() - 20, st, t.caption, m.status_bad() ? t.bad : t.muted,
                           trx_content_w() - 2 * x);
        g_status_bad = m.status_bad();
        lv_obj_set_width(g_status, trx_text_w(st, t.caption) + 4 < trx_content_w() - 2 * x ? trx_text_w(st, t.caption) + 4 : trx_content_w() - 2 * x);
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

static int g_bkind = -1;                         // the bar's place depends on whether a question is open: 1 alone at the foot, 0 below a question

static void dialog_objs_forget(void)
{
    g_veil = g_qcard = g_bcard = g_bl1 = g_bl2 = g_bfill = 0;
    g_qsig = 0;
    g_qtxt = 0;
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

// What the question's card shows (everything of the view except the bar).
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
    add(&v.nitems, sizeof(v.nitems));
    add(&v.first, sizeof(v.first));
    add(&v.sel, sizeof(v.sel));
    add(&v.rows, sizeof(v.rows));
    for (int i = 0; i < v.rows && i < DLG_ITEMS_MAX; i++) {
        add(v.row[i], strlen(v.row[i]) + 1);
        add(&v.row_dir[i], 1);
    }
    add(&v.buttons, sizeof(v.buttons));
    add(&v.remaining_s, sizeof(v.remaining_s));
    return h ? h : 1;
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
        int rows = (v.kind == DK_CHOOSE || v.kind == DK_PICK_FILE || v.kind == DK_PICK_FOLDER) ? (v.rows < 1 ? 1 : v.rows) : 0;
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
            if (v.kind != DK_CHOOSE) {
                trx_txt(card, t.space_lg, cy, v.input, t.caption, t.accent, w - 2 * t.space_lg);
                cy += 18;
            }
            for (int i = 0; i < rows; i++) {
                TrxRow r;
                memset(&r, 0, sizeof(r));
                r.label = v.row[i];
                r.selected = r.strong = (v.first + i == v.sel);
                trx_row_at(card, t.space_lg, cy, w - 2 * t.space_lg, r, &cy);
            }
            if (v.nitems > rows) {
                char pos[32];
                snprintf(pos, sizeof(pos), "%d-%d of %d", v.first + 1, v.first + rows, v.nitems);
                trx_txt(card, w - t.space_lg - trx_text_w(pos, t.caption), t.space_md + 6, pos, t.caption, t.muted);
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
        if (!g_qcard || sig != g_qsig) {
            if (g_qcard) {
                lv_obj_delete(g_qcard);
            }
            g_qtxt = 0;
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
        g_veil = g_qcard = g_qtxt = 0;
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
    lv_obj_set_height(panel, ph < 220 ? ph : 220);
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

void ui_set_look(int transparency, int scheme)
{
    g_look_t = transparency < 0 ? 0 : (transparency > 3 ? 3 : transparency);
    g_look_scheme = scheme;
    g_look_dirty = true;
}

static volatile int g_squeeze = 0;
void ui_set_name_squeeze(int quarter)
{
    g_squeeze = quarter < 0 || quarter > 3 ? 0 : quarter;
    g_look_dirty = true;
}
int ui_name_squeeze(void) { return g_squeeze; }

// the layer's global alpha from the transparency (Gideon "Overlay Transparency": 0, 25, 50, 75 %)
static void apply_alpha(void)
{
    trx_lv_set_alpha((uint8_t)(255 - g_look_t * 64 + (g_look_t ? 1 : 0)));
}

void ui_show(void)
{
    if (have_ui) {
        trx_lv_resume();
        apply_alpha();
    }
}

// The machine stands and the menu is closed: the layer stays up as a keyed screen (the C64's last picture shows through it) with one small tag in the corner.
static bool g_tag;
void ui_paused_tag(bool on)
{
    if (!have_ui) {
        return;
    }
    if (on) {
        g_msg_label = g_msg_panel = 0;
        pq_up = false;
        trx_shell_forget();
        ui_menu_forget_rows();
        lv_obj_clean(scr);
        lv_obj_set_style_bg_color(scr, lv_color_hex(TRX_LIVE_KEY_RGB), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        const TrxTheme &t = trx_theme();
        lv_obj_t *b = trx_box(scr, SW - 250, 14, 236, 30, 0x101018);
        trx_txt(b, 12, 6, "PAUSED   P RESUME", t.caption, 0xFFFFFF);
        trx_lv_set_key(true, TRX_LIVE_KEY_565);
        trx_lv_resume();
        trx_lv_set_alpha(255);
        g_tag = true;
        trx_lv_refresh_now();
    } else if (g_tag) {
        g_tag = false;
        trx_lv_set_key(false, 0);
        lv_obj_clean(scr);
        trx_lv_suspend();                       // the layer is the core's again until the menu takes it with ui_show()
    }
}

void ui_hide(void)
{
    if (have_ui) {
        trx_lv_suspend();
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
