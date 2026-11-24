// The screens of the UX concept (screens.h), built on the widgets of the view layer (app/view/trxview.h).
#include "screens.h"
#include "ui.h"
#include "contrib.h"
#include "trx_lv_port.h"
#include "menuflows.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef MenuCtl::Item Item;

#define MAXI 128                                  // items of one tab that a screen draws
#define MAXW 24                                   // rows of the library's window
#define X0 18                                     // the pane's margin (space_lg)
#define SIDE_W (trx_compact() ? 190 : 260)                                // the raised side panel
#define LIB_SIDE_W (trx_compact() ? 170 : 230)

// ---- what the last build kept, so that a key that only moves the selection touches the old and the new focus and nothing else ----------------
static Item g_it[MAXI];
static int g_n;
static lv_obj_t *g_obj[MAXI];                     // the focus object of item i, null when it has none
static bool g_valid;
static uint32_t g_sig;
static int g_area = -1, g_tab = -1;
static int g_sel = -1;                            // the selection as drawn
static lv_obj_t *g_hint;                          // the line at the pane's foot
static char g_hint_text[OP_MSG_MAX];
static bool g_hint_bad;
static uint32_t g_cheap;
static int g_mfirst;                              // the first unit of the main list in the window (a tab with more rows than the panel holds)
static int g_munits, g_mdrawn_to;                 // the units the window was made from and the one after the last drawn, for the position label

// the library
static lv_obj_t *g_win[MAXW];                     // the window's rows
static int g_wfirst, g_wn;
static lv_obj_t *g_selpan;                        // the SELECTED panel, drawn again when the selection moves
static int g_selpan_w, g_selpan_h;
static char g_lib_path[200];
static int g_lib_first;
static int lib_rows(bool sheet);
static bool g_live;
static bool g_viewer;
static int g_full;                                // a full screen without the menu's frame is up: 1 Timewarp, 2 the monitor

uint32_t screens_cheap_moves(void) { return g_cheap; }

static void live_forget(void);
void screens_forget(void)
{
    live_forget();
    if (g_live) {
        ui_live_key(false);
        g_live = false;
    }
    g_valid = false;
    g_hint = 0;
    g_selpan = 0;
    g_wn = 0;
    memset(g_obj, 0, sizeof(g_obj));
}

bool screens_wanted(MenuCtl &m) { return m.screen(); }

// ---- small things ------------------------------------------------------------------------------------------------------------------------
static uint32_t hs(uint32_t h, const char *s)
{
    for (; s && *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return (h ^ 0xFF) * 16777619u;
}
static uint32_t hi(uint32_t h, int v) { return (h ^ (uint32_t)v) * 16777619u; }

static TrxTone tone_of(const Item &it) { return it.tone >= 0 ? (TrxTone)it.tone : trx_tone_of_word(it.chip); }

static bool in_slot(const Item &it, const char *slot)
{
    if (it.hidden) {
        return false;
    }
    if (!strcmp(slot, "main")) {
        return !it.slot[0] || !strcmp(it.slot, "main");
    }
    return !strcmp(it.slot, slot);
}

static char *upper(char *dst, int n, const char *s)
{
    int i = 0;
    for (; s && s[i] && i < n - 1; i++) {
        dst[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    }
    dst[i] = 0;
    return dst;
}

// The hint line: the reason the selected row is greyed, else the controller's status. As wide as its text, so a change repaints the text only.
static void hint_text(MenuCtl &m, char *out, int n, bool *bad)
{
    const char *h = m.hint();
    if (h) {
        snprintf(out, n, "%s", h);
        *bad = false;
        return;
    }
    snprintf(out, n, "%s", m.foot_line());
    *bad = m.status_bad();
}

static void hint_make(lv_obj_t *c, MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    hint_text(m, g_hint_text, sizeof(g_hint_text), &g_hint_bad);
    int room = trx_content_w() - 2 * X0;
    g_hint = trx_txt(c, X0, trx_content_h() - 22 - ui_note_extra_h(g_hint_text, room), g_hint_text, t.caption, g_hint_bad ? t.bad : t.muted, room);
    lv_label_set_long_mode(g_hint, LV_LABEL_LONG_MODE_WRAP);       // a long note takes more lines above its first, it is not cut
    int w = trx_text_w(g_hint_text, t.caption) + 4;
    lv_obj_set_width(g_hint, w < room ? (w < 1 ? 1 : w) : room);
}

static void hint_update(MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    char s[OP_MSG_MAX];
    bool bad;
    hint_text(m, s, sizeof(s), &bad);
    if (!g_hint) {
        return;
    }
    if (strcmp(s, g_hint_text)) {
        int room = trx_content_w() - 2 * X0;
        int w = trx_text_w(s, t.caption) + 4;
        lv_obj_set_width(g_hint, w < room ? (w < 1 ? 1 : w) : room);
        lv_label_set_text(g_hint, s);
        lv_obj_set_y(g_hint, trx_content_h() - 22 - ui_note_extra_h(s, room));
        snprintf(g_hint_text, sizeof(g_hint_text), "%s", s);
    }
    if (bad != g_hint_bad) {
        lv_obj_set_style_text_color(g_hint, lv_color_hex(bad ? t.bad : t.muted), 0);
        g_hint_bad = bad;
    }
}

static lv_obj_t *button_of(lv_obj_t *par, int x, int y, int w, const Item &it, bool sel)
{
    return trx_btn(par, x, y, w, it.title, it.primary ? TB_PRIMARY : TB_SECONDARY, sel, it.dim, it.bkey);
}

// A row of buttons from the right edge of `right` leftwards (the head and the bar), in declaration order from the left.
static void button_row(lv_obj_t *par, int right, int y, const char *slot, int sel)
{
    int total = 0, n = 0;
    for (int i = 0; i < g_n; i++) {
        if (in_slot(g_it[i], slot) && g_it[i].kind == MenuCtl::IK_BUTTON) {
            total += trx_btn_w(g_it[i].title, g_it[i].bkey) + (n++ ? trx_theme().space_sm : 0);
        }
    }
    int x = right - total;
    for (int i = 0; i < g_n; i++) {
        if (in_slot(g_it[i], slot) && g_it[i].kind == MenuCtl::IK_BUTTON) {
            g_obj[i] = button_of(par, x, y, 0, g_it[i], i == sel);
            x += trx_btn_w(g_it[i].title, g_it[i].bkey) + trx_theme().space_sm;
        }
    }
}

// A column of rows of the tab: the items of `slot`, as status rows or setting rows, and a heading over the first row of every setting group of a tab
// that lists several. The rows are the units of a window (units(), window_first()): a tab holds more rows than the panel does, and the window follows the
// selection. Returns the y below the last.
#define HEAD_H 24
#define MAXU (MAXI * 2)
struct Unit { int16_t item; bool heading; char title[40]; int16_t h; };
static Unit g_u[MAXU];

static void group_title(const char *g, char *out, int n)
{
    int i = 0;
    for (; g[i] && i < n - 1; i++) {
        out[i] = g[i] == '_' ? ' ' : (g[i] >= 'a' && g[i] <= 'z' ? (char)(g[i] - 32) : g[i]);
    }
    out[i] = 0;
}

static int unit_h(const Item &it)
{
    if (it.kind == MenuCtl::IK_SETTING) {
        return trx_setting_row_h() + 1;
    }
    return trx_status_row_h() + 1;
}

static int units(const char *slot)
{
    int n = 0;
    int groups = 0;
    const char *last = "";
    for (int i = 0; i < g_n; i++) {                                 // does the tab list more than one group?
        if (in_slot(g_it[i], slot) && g_it[i].kind == MenuCtl::IK_SETTING && g_it[i].group[0] && strcmp(g_it[i].group, last)) {
            groups++;
            last = g_it[i].group;
        }
    }
    last = "";
    for (int i = 0; i < g_n && n < MAXU - 1; i++) {
        const Item &it = g_it[i];
        if (!in_slot(it, slot)) {
            continue;
        }
        if (it.kind == MenuCtl::IK_HEADING) {
            g_u[n].item = (int16_t)i;
            g_u[n].heading = true;
            snprintf(g_u[n].title, sizeof(g_u[n].title), "%s", it.title);
            g_u[n].h = HEAD_H;
            n++;
            continue;
        }
        if (groups > 1 && it.kind == MenuCtl::IK_SETTING && it.group[0] && strcmp(it.group, last)) {
            g_u[n].item = -1;
            g_u[n].heading = true;
            group_title(it.group, g_u[n].title, sizeof(g_u[n].title));
            g_u[n].h = HEAD_H;
            n++;
            last = it.group;
        }
        g_u[n].item = (int16_t)i;
        g_u[n].heading = false;
        g_u[n].title[0] = 0;
        g_u[n].h = (int16_t)unit_h(it);
        n++;
    }
    return n;
}

// The bench's look at the headings of the open tab's main column, in order (the ones the record names and the ones a change of group makes): how many, up to `max`.
int screens_headings(char out[][40], int max)
{
    int n = units("main"), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        if (g_u[i].heading) {
            snprintf(out[k++], 40, "%s", g_u[i].title);
        }
    }
    return k;
}

// The first unit of the window that holds the selected item and as many units around it as fit in `avail` pixels; `first` is where the last one began.
static int window_first(int n, int sel, int first, int avail)
{
    int su = -1;
    for (int k = 0; k < n; k++) {
        if (!g_u[k].heading && g_u[k].item == sel) {
            su = k;
        }
    }
    if (su < 0) {
        return first < n ? first : 0;
    }
    {                                                                  // a window that does not start on a heading carries the heading of its group (sticky): one heading row of the room
        int tot = 0, hd = 0;
        for (int k = 0; k < n; k++) {
            tot += g_u[k].h;
            hd += g_u[k].heading;
        }
        if (hd && tot > avail) {
            avail -= HEAD_H;
        }
    }
    int above = su, below = su;                                        // T209 D3: TRX_SCROLL_MARGIN selectable rows stay above and below the selection (headings between them do not count)
    for (int k = su - 1, c = 0; k >= 0 && c < TRX_SCROLL_MARGIN; k--) {
        above = k;
        c += !g_u[k].heading;
    }
    for (int k = su + 1, c = 0; k < n && c < TRX_SCROLL_MARGIN; k++) {
        below = k;
        c += !g_u[k].heading;
    }
    if (first > above) {
        first = above;
    }
    int sum = 0;
    for (int k = first; k <= below; k++) {
        sum += g_u[k].h;
    }
    while (sum > avail && first < su) {                                // the margin below gives way before the selection does
        sum -= g_u[first].h;
        first++;
    }
    if (first > 0 && g_u[first - 1].heading && first == su) {          // the heading above the first row stays with it
        first--;
    }
    int total = 0;
    for (int k = 0; k < n; k++) {
        total += g_u[k].h;
    }
    if (total <= avail) {
        first = 0;
    } else {                                                           // no empty space below the last row when the window can be filled
        int below = 0;
        for (int k = first; k < n; k++) {
            below += g_u[k].h;
        }
        while (first > 0 && below + g_u[first - 1].h <= avail) {
            first--;
            below += g_u[first].h;
        }
    }
    return first;
}

static int main_avail(bool bar, bool status)
{
    const TrxTheme &t = trx_theme();
    int H = trx_content_h();
    int top = status ? 46 + 30 : 46;                       // the head has no crumb line: the tab strip says where we are
    return H - 30 - (bar ? 38 : 0) - top - 2 * t.space_sm + 2;
}

static int rows_of(lv_obj_t *card, int x, int y, int w, const char *slot, int sel, int avail)
{
    const TrxTheme &t = trx_theme();
    int n = units(slot);
    int y0 = y;
    int first = g_mfirst < n ? g_mfirst : 0;
    int last_drawn = first;
    if (first > 0 && !g_u[first].heading) {                           // sticky heading: the group the first row belongs to
        for (int k = first - 1; k >= 0; k--) {
            if (g_u[k].heading) {
                trx_txt(card, x + t.space_xs, y + 6, g_u[k].title, t.caption, t.muted);
                y += HEAD_H;
                break;
            }
        }
    }
    for (int k = first; k < n; k++) {
        if (y - y0 + g_u[k].h + (g_u[k].heading && k + 1 < n ? g_u[k + 1].h : 0) > avail) {          // a heading stays with its first row
            break;
        }
        last_drawn = k + 1;
        if (g_u[k].heading) {
            trx_txt(card, x + t.space_xs, y + 6, g_u[k].title, t.caption, t.muted);
            y += g_u[k].h;
            continue;
        }
        int i = g_u[k].item;
        const Item &it = g_it[i];
        bool focus = it.kind != MenuCtl::IK_INFO && it.kind != MenuCtl::IK_VALUE;         // a line that only shows something takes no focus
        lv_obj_t *o;
        if (it.kind == MenuCtl::IK_SETTING) {
            TrxSettingRow r = { it.title, it.toggle ? 0 : it.value, it.toggle, it.on, focus && i == sel, it.dim, it.reset, TT_MUTED };
            o = trx_setting_row(card, x, y, w, r, &y);
        } else {
            TrxStatusRow r = { it.title, it.kind == MenuCtl::IK_MEDIA ? (it.value[0] ? it.value : 0) : (it.chip[0] ? it.chip : (it.value[0] ? it.value : 0)), tone_of(it), focus && i == sel, it.dim };
            o = trx_status_row(card, x, y, w, r, &y);
        }
        g_obj[i] = focus ? o : 0;
    }
    g_munits = n;
    g_mdrawn_to = last_drawn;
    return y;
}

static int items_in(const char *slot)
{
    int n = 0;
    for (int i = 0; i < g_n; i++) {
        n += in_slot(g_it[i], slot);
    }
    return n;
}

// ---- the signature: everything on the screen except which item has the focus ---------------------------------------------------------------
static uint32_t signature(MenuCtl &m)
{
    uint32_t h = 2166136261u;
    h = hs(h, m.layout());
    h = hi(h, m.area());
    h = hi(h, m.tab());
    h = hs(h, m.tab_name());
    h = hi(h, m.closable());
    h = hi(h, g_mfirst);
    char s[120];
    m.session_line(s, sizeof(s));
    h = hs(h, s);
    for (int i = 0; i < g_n; i++) {
        const Item &it = g_it[i];
        h = hs(h, it.slot);
        h = hs(h, it.group);
        h = hs(h, it.title);
        h = hs(h, it.sub);
        h = hs(h, it.chip);
        h = hs(h, it.value);
        h = hi(h, it.hidden);
        h = hi(h, it.kind | (it.tone + 1) << 4 | it.primary << 8 | it.link << 9 | it.enabled << 10 | it.dim << 11 | it.toggle << 12 | it.on << 13 | it.reset << 14);
    }
    char p[80];
    if (m.layout_is("library")) {
        h = hs(h, m.browse_path());
        h = hi(h, m.rows());
        h = hi(h, m.chooser());
        h = hs(h, m.chooser_file());
        h = hi(h, m.nroots());
        h = hi(h, m.root_active());
        for (int i = 0; i < m.nroots(); i++) {
            h = hs(h, m.root(i));
            h = hi(h, m.chip_flags(i));
        }
        h = hi(h, g_lib_first);
        for (int i = g_lib_first; i < m.rows() && i < g_lib_first + 2 * lib_rows(false) + 2; i++) {      // the rows the window can show (a list longer than 512 rows must redraw for a mark on row 600 too)
            MenuCtl::BrowseRow br;
            m.browse_row(i, &br);
            h = hi(h, br.marked);
            h = hs(h, br.badge);
            if (m.media_level()) {                                        // what a medium is changes under the list (a card in, a stick out)
                h = hs(h, br.sub);
            }
        }
    }
    if (m.layout_param("sheet", p, sizeof(p))) {
        char *bar = strchr(p, '|');
        char c[48];
        m.spec_text(bar ? bar + 1 : "", c, sizeof(c));
        h = hs(h, c);
    }
    if (m.layout_param("chip", p, sizeof(p))) {
        char c[40];
        m.spec_text(p, c, sizeof(c));
        h = hs(h, c);
    }
    if (m.app_mode()) {                                                              // an application's tab: the shape of what it draws; the bank maps, the bar, the grid and the status line are written in place
        char t1[96];
        static const char *const keys[] = { "title", "crumb", "chip", "map1", "map2", "map1_t", "map2_t", "side", "note" };
        for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
            if (m.layout_param(keys[k], t1, sizeof(t1))) {
                char t2[96];
                m.spec_text(t1, t2, sizeof(t2));
                h = hs(h, t2);
            }
        }
        char big[264];
        h = hi(h, m.tab_spec("map1_s", big, sizeof(big)) ? (int)strlen(big) : 0);           // the number of banks is the map's shape; their states are not
        h = hi(h, m.tab_spec("map2_s", big, sizeof(big)) ? (int)strlen(big) : 0);
        h = hi(h, m.tab_spec("pct", t1, sizeof(t1)));
        h = hi(h, m.tab_spec("status", t1, sizeof(t1)));
        h = hi(h, m.tab_spec("lock", t1, sizeof(t1)));
    }
    static const char *const more[] = { "left", "label", "frame", "bar" };          // the state values of a Timewarp screen
    for (int i = 0; i < 4; i++) {
        if (m.layout_param(more[i], p, sizeof(p))) {
            char c[48];
            m.spec_text(p, c, sizeof(c));
            h = hs(h, c);
        }
    }
    return h;
}

// ---- the head of a split screen ------------------------------------------------------------------------------------------------------------
static int head_of(lv_obj_t *c, MenuCtl &m, int sel, int *title_y)
{
    const TrxTheme &t = trx_theme();
    char crumb[80], title[80], chipspec[80], chip[40], ua[40], ut[40];
    bool has_crumb = m.layout_param("crumb", crumb, sizeof(crumb));
    if (!has_crumb) {
        upper(ua, sizeof(ua), m.area_name());
        upper(ut, sizeof(ut), m.tab_name());
        if (!strcmp(ua, ut)) {                                     // an area whose one tab has its name (Home): the name once
            snprintf(crumb, sizeof(crumb), "%s", ua);
        } else {
            snprintf(crumb, sizeof(crumb), "%s / %s", ua, ut);
        }
    }
    if (!m.layout_param("title", title, sizeof(title))) {
        snprintf(title, sizeof(title), "%s", m.tab_name());
    } else if (title[0] == '@') {                                  // an application's screen names its title by a state value ("Writing slot 2")
        char t2[80];
        m.spec_text(title, t2, sizeof(t2));
        snprintf(title, sizeof(title), "%s", t2[0] ? t2 : m.tab_name());
    }
    if (has_crumb && crumb[0] == '@') {
        char c2[80];
        m.spec_text(crumb, c2, sizeof(c2));
        snprintf(crumb, sizeof(crumb), "%s", c2);
    }
    chip[0] = 0;
    if (m.layout_param("chip", chipspec, sizeof(chipspec))) {
        m.spec_text(chipspec, chip, sizeof(chip));
    }
    int w = trx_content_w() - 2 * X0;
    int hw = 0;                                                // the head buttons take room at the right
    for (int i = 0; i < g_n; i++) {
        if (in_slot(g_it[i], "head") && g_it[i].kind == MenuCtl::IK_BUTTON) {
            hw += trx_btn_w(g_it[i].title, g_it[i].bkey) + t.space_sm;
        }
    }
    int y = trx_head(c, X0, 12, hw ? w - hw : w, has_crumb ? crumb : 0, title, chip[0] ? chip : 0, trx_tone_of_word(chip));
    button_row(c, X0 + w, 12 + 18 - 2, "head", sel);
    *title_y = 12;
    return y;
}


// ---- a side panel: values, buttons, one link at its foot -------------------------------------------------------------------------------------
static void side_panel(lv_obj_t *c, int x, int y, int w, int h, const char *kicker, int sel)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *sp = trx_card(c, x, y, w, h, true);
    char k[48];
    if (kicker && kicker[0]) {
        trx_kicker(sp, t.space_md, t.space_sm, upper(k, sizeof(k), kicker));
    }
    int sy = 32;
    for (int i = 0; i < g_n; i++) {
        const Item &it = g_it[i];
        if (!in_slot(it, "side")) {
            continue;
        }
        if (it.kind == MenuCtl::IK_BUTTON) {
            g_obj[i] = button_of(sp, t.space_md, sy, w - 2 * t.space_md, it, i == sel);
            sy += trx_btn_h() + t.space_sm;
        } else if (it.kind != MenuCtl::IK_HEADING) {
            const char *v = it.value[0] ? it.value : (it.chip[0] ? it.chip : it.sub);
            sy = trx_kv_row(sp, t.space_md, sy, w - 2 * t.space_md, it.title, v, it.tone >= 0 ? (TrxTone)it.tone : TT_MUTED);
        }
    }
    for (int i = 0; i < g_n; i++) {
        if (in_slot(g_it[i], "sidefoot") && g_it[i].kind == MenuCtl::IK_BUTTON) {
            g_obj[i] = trx_link(sp, t.space_sm, h - 28, 0, g_it[i].title, g_it[i].tone >= 0 ? (TrxTone)g_it[i].tone : TT_VIOLET, i == sel);
        }
    }
}

// the panel's room for rows: the main list's height less the buttons that stand at its foot
static int split_avail(bool bar)
{
    int foot = items_in("mainfoot");
    return main_avail(bar, false) - (foot ? foot * (trx_btn_h() + trx_theme().space_sm) : 0);
}

// "a-b / n" in the corner of a panel whose rows do not all fit
static void pos_label(lv_obj_t *card, int w, int h)
{
    if (g_munits == 0 || (g_mfirst == 0 && g_mdrawn_to >= g_munits)) {
        return;
    }
    const TrxTheme &t = trx_theme();
    int a = 0, z = 0;
    for (int k = 0; k < g_munits; k++) {
        if (g_u[k].heading) {
            continue;
        }
        if (k < g_mfirst) {
            a++;
        }
        if (k < g_mdrawn_to) {
            z++;
        }
    }
    int tot = 0;
    for (int k = 0; k < g_munits; k++) {
        tot += !g_u[k].heading;
    }
    char s[32];
    snprintf(s, sizeof(s), "%d-%d / %d", a + 1, z, tot);
    if (h) {                                                         // h: the y of the button bar; the counter sits at the left of it, never under the buttons
        trx_txt(card, X0, h + 8, s, t.caption, t.muted);
    } else {
        trx_txt(card, X0 + w - trx_text_w(s, t.caption), trx_content_h() - 22, s, t.caption, t.muted);
    }
}

// ---- split / settings ------------------------------------------------------------------------------------------------------------------------
static void build_split(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    int ty;
    int y = head_of(c, m, sel, &ty);
    bool bar = items_in("bar") > 0;
    int bottom = H - 30 - (bar ? 38 : 0);
    char kick[48];
    kick[0] = 0;
    bool side = m.layout_param("side", kick, sizeof(kick)) || items_in("side") > 0;
    int mw = side ? W - SIDE_W - t.space_lg : W;

    lv_obj_t *card = trx_card(c, X0, y, mw, bottom - y, false);
    int avail = split_avail(bar);
    rows_of(card, t.space_md, t.space_sm, mw - 2 * t.space_md, "main", sel, avail);
    pos_label(c, W, bar ? bottom + 8 : 0);
    int fy = bottom - y - t.space_md - trx_btn_h();
    for (int i = 0; i < g_n; i++) {                                 // buttons at the foot of the main panel
        if (in_slot(g_it[i], "mainfoot") && g_it[i].kind == MenuCtl::IK_BUTTON) {
            g_obj[i] = button_of(card, t.space_md, fy, mw - 2 * t.space_md, g_it[i], i == sel);
            fy -= trx_btn_h() + t.space_sm;
        }
    }
    if (side) {
        side_panel(c, X0 + mw + t.space_lg, y, SIDE_W, bottom - y, kick, sel);
    }
    if (bar) {
        button_row(c, X0 + W, bottom + 8, "bar", sel);
    }
}

// ---- Home, the media desk (T104.2 P3): one row for each medium the core has, a block for the machine and the resources it is configured with. Nothing here knows a core:
// the rows are the nodes of the description that name the slots "media" and "machine" ------------------------------------------------------------------------------
static void build_desk(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    trx_txt(c, X0, 12, m.tab_name(), t.title, t.ink);
    button_row(c, X0 + W, 12, "head", sel);
    int py = 56, ph = H - 30 - py;
    int rw = trx_compact() ? 240 : 330, lw = W - rw - t.space_lg;
    lv_obj_t *left = trx_card(c, X0, py, lw, ph, false);
    trx_kicker(left, t.space_md, t.space_sm, "MEDIA");
    rows_of(left, t.space_md, 30, lw - 2 * t.space_md, "media", sel, ph - 34);
    lv_obj_t *right = trx_card(c, X0 + lw + t.space_lg, py, rw, ph, false);
    trx_kicker(right, t.space_md, t.space_sm, "MACHINE");
    rows_of(right, t.space_md, 30, rw - 2 * t.space_md, "machine", sel, ph - 34);
}

// ---- System status -------------------------------------------------------------------------------------------------------------------------
static void build_status(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    int ty;
    int y = head_of(c, m, sel, &ty);
    char v[96], line[120];
    m.info_line(0, v, sizeof(v));
    int x = X0;
    trx_txt(c, x, y, v, t.display, t.ink);
    x += trx_text_w(v, t.display) + t.space_xl;
    static const struct { int which; const char *label; } more[] = { { 99, "" } };
    for (unsigned k = 0; k < sizeof(more) / sizeof(more[0]); k++) {
        m.info_line(more[k].which, v, sizeof(v));
        if (!v[0]) {
            continue;
        }
        snprintf(line, sizeof(line), "%s  %s", more[k].label, v);
        trx_txt(c, x, y + 2, line, t.body, t.muted);
        x += trx_text_w(line, t.body) + t.space_xl;
    }
    y += 30;
    int bottom = H - 30;
    lv_obj_t *card = trx_card(c, X0, y, W, bottom - y, false);
    rows_of(card, t.space_md, t.space_sm, W - 2 * t.space_md, "main", sel, main_avail(false, true));
    pos_label(c, W, 0);
}

// ---- the library ---------------------------------------------------------------------------------------------------------------------------
// How many rows the library's list (or the action sheet's) holds: one number for the build and for the window the render keeps.
static int lib_rows(bool sheet)
{
    const TrxTheme &t = trx_theme();
    int H = trx_content_h();
    int rows = sheet ? (H - 30 - (12 + 18 + 34) - 2 * t.space_sm) / (trx_status_row_h() + 1)
                     : (H - 30 - (12 + 34 + 32) - 30 - t.space_sm) / (trx_status_row_h() + 1);
    return rows < 1 ? 1 : (rows > MAXW ? MAXW : rows);
}

// The media level's panel, as the host's File Browser at "/": the medium's name, then Size, Contents, Access, and what RETURN does. (No Format row: this application
// cannot format a medium; no Boot menu row: it has no boot menu.)
static void draw_medium_panel(MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    int w = g_selpan_w - 2 * t.space_md;
    if (m.rows() == 0) {
        trx_txt(g_selpan, t.space_md, t.space_md, "No media found.", t.display, t.muted, w);
        return;
    }
    MenuCtl::BrowseRow r;
    m.browse_row(sel, &r);
    trx_txt(g_selpan, t.space_md, t.space_md, r.name, t.display, t.ink, w);
    int y = t.space_md + 44;
    MediumInfo mi;
    if (m.medium_at(sel, &mi)) {
        const char *rows[3][2] = { { "Size", mi.ready ? mi.size : "" }, { "Contents", mi.contents }, { "Access", mi.access() } };
        for (int i = 0; i < 3; i++) {
            if (!rows[i][1][0]) {
                continue;
            }
            trx_txt(g_selpan, t.space_md, y, rows[i][0], t.caption, t.muted);
            trx_txt(g_selpan, t.space_md, y + 15, rows[i][1], t.body, t.ink, w);
            y += 44;
        }
    } else {
        trx_txt(g_selpan, t.space_md, y, r.sub, t.body, t.muted, w);
    }
    trx_txt(g_selpan, t.space_md, g_selpan_h - t.space_md - 16, "RETURN opens it", t.caption, trx_tone_color(TT_CYAN), w);
}

static void draw_selpan(MenuCtl &m, int sel)
{
    if (!g_selpan) {
        return;
    }
    const TrxTheme &t = trx_theme();
    lv_obj_clean(g_selpan);
    int w = g_selpan_w, h = g_selpan_h;
    if (m.media_level()) {
        draw_medium_panel(m, sel);
        return;
    }
    trx_kicker(g_selpan, t.space_md, t.space_sm, "SELECTED");
    if (m.rows() == 0) {
        trx_txt(g_selpan, t.space_md, 30, "Nothing here", t.display, t.muted, w - 2 * t.space_md);
        return;
    }
    MenuCtl::BrowseRow r;
    m.browse_row(sel, &r);
    char name[80], ext[8], sub[120];
    snprintf(name, sizeof(name), "%s", r.name);
    char *dot = strrchr(name, '.');
    if (r.dir) {
        size_t l = strlen(name);
        if (l + 1 < sizeof(name) && strcmp(name, "..")) {
            name[l] = '/';
            name[l + 1] = 0;
        }
    } else if (dot && dot != name) {
        *dot = 0;
    }
    char nfit[80];                                                 // T209: the name is one line, cut in the middle (the meta line below is never overprinted)
    lv_obj_t *tl = trx_txt(g_selpan, t.space_md, 30, trx_fit_mid(name, w - 2 * t.space_md - 4, t.display, nfit, sizeof(nfit)), t.display, t.ink, w - 2 * t.space_md);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_MODE_CLIP);
    if (r.dir) {
        snprintf(sub, sizeof(sub), "%s", r.sub);
    } else {
        snprintf(sub, sizeof(sub), "%s  %s", upper(ext, sizeof(ext), r.ext), r.sub);          // the footer carries the keys, the panel says what the file is
    }
    trx_txt(g_selpan, t.space_md, 74, sub, t.caption, t.muted, w - 2 * t.space_md);
    int by = h - t.space_md - trx_btn_h();
    if (r.primary[0]) {
        trx_btn(g_selpan, t.space_md, by, w - 2 * t.space_md, r.primary, TB_PRIMARY, false, false);
    }
}

static void build_library(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    char title[48];
    if (!m.layout_param("title", title, sizeof(title))) {
        snprintf(title, sizeof(title), "%s", m.tab_name());
    }
    int y = trx_head(c, X0, 12, W, 0, title, 0, TT_MUTED);
    const char *names[12];
    uint8_t flags[12];
    int nr = m.nroots();
    for (int i = 0; i < nr && i < 12; i++) {
        names[i] = m.root(i);
        flags[i] = m.chip_flags(i);
    }
    bool media = m.media_level();
    if (nr && !media) {                                            // the chips only show which medium is open; the media level has none (the host's has none)
        trx_source_chips(c, X0, y - 2, names, nr, m.root_active(), flags);
    }
    int py = y + (media ? 8 : 32), bottom = H - 30;
    int lw = W - LIB_SIDE_W - t.space_lg;
    lv_obj_t *card = trx_card(c, X0, py, lw, bottom - py, false);
    char p[200], items[24];
    if (m.in_search_tab()) {                                       // a field, not a path: what is looked for, and how many were found
        trx_box(card, t.space_sm, 2, lw - 2 * t.space_sm, 22, t.surface2, 1, t.accent);
        snprintf(items, sizeof(items), m.search_active() ? "%d found" : "", m.rows());
        // one line in the field, beside the counter (a label with a width wraps): what does not fit is cut, the end of a typed text stays in sight; the idle
        // hint gives way first, the status line under the list says how to type
        const int room = lw - 2 * t.space_md - t.space_sm - (items[0] ? trx_text_w(items, t.caption) + t.space_md : 0);
        const char *q = m.search_active() ? m.search_query() : "press RETURN to type";
        snprintf(p, sizeof(p), "Name contains:  %s", q);
        if (trx_text_w(p, t.caption) > room && !m.search_active()) {
            snprintf(p, sizeof(p), "Name contains:");
        }
        while (m.search_active() && trx_text_w(p, t.caption) > room && *q) {
            snprintf(p, sizeof(p), "Name contains:  ...%s", ++q);
        }
        trx_txt(card, t.space_md, 5, p, t.caption, m.search_active() ? t.ink : t.muted);
    } else {
        upper(p, sizeof(p), m.browse_path());
        trx_txt(card, t.space_md, t.space_sm, p, t.caption, t.muted, lw - 160);
        if (media) {
            items[0] = 0;
        } else {                                                   // ux-review R5: "773-784 of 784" (entries only, ".." and the "more" line not counted) once the list is longer than its window
            TrxWindow w0 = trx_window(m.rows(), sel, g_lib_first, lib_rows(false));
            m.pos_text(items, sizeof(items), w0.first, lib_rows(false));
        }
    }
    trx_txt(card, lw - t.space_md - trx_text_w(items, t.caption) - (m.in_search_tab() ? t.space_sm : 0), m.in_search_tab() ? 5 : t.space_sm, items, t.caption, t.muted);
    TrxWindow w = trx_window(m.rows(), sel, g_lib_first, lib_rows(false));
    g_lib_first = w.first;
    g_wfirst = w.first;
    g_wn = w.count;
    int ry = 28;
    for (int k = 0; k < w.count; k++) {
        MenuCtl::BrowseRow r;
        m.browse_row(w.first + k, &r);
        if (r.info) {                                              // ux-review R10: "... more not shown" is information, no bullet, dimmed, no rule, no focus (as on the host)
            char it[72];
            snprintf(it, sizeof(it), "%s", r.name);
            TrxRow ir = { it, 0, false, false, true, false, true };
            g_win[k] = trx_row_at(card, t.space_md, ry, lw - 2 * t.space_md, ir, &ry);
            continue;
        }
        if (media) {                                               // "> SD": the row of a medium, nothing else (the details are in the panel)
            char gt[72];
            snprintf(gt, sizeof(gt), "> %s", r.name);
            TrxRow mr = { gt, 0, 0, w.first + k == sel, false, false };
            g_win[k] = trx_row_at(card, t.space_md, ry, lw - 2 * t.space_md, mr, &ry);
            continue;
        }
        char title[100], nm[64];
        const char *dirsl = r.dir && strcmp(r.name, "..") ? "/" : "";
        const int lw_row = lw - 2 * t.space_md;                      // T209 R5: a long name is cut in the middle ("start...end.ext"), the size stays whole at the right
        const int vw = r.value[0] ? trx_text_w(r.value, t.caption) + t.space_md : 0;
        const int fixed = (r.marked ? trx_text_w("* ", t.body) : 0) + trx_text_w(dirsl, t.body) + (r.badge[0] ? trx_text_w("  ", t.body) + trx_text_w(r.badge, t.body) : 0);
        const int tx = t.space_sm + 4 + t.space_md + t.space_sm;
        snprintf(title, sizeof(title), "%s%s%s%s%s", r.marked ? "* " : "", trx_fit_name(r.name, lw_row - tx - vw - fixed - 4, t.body, nm, sizeof(nm), ui_name_squeeze()), dirsl, r.badge[0] ? "  " : "", r.badge);
        TrxStatusRow row = { title, r.value[0] ? r.value : 0, (TrxTone)r.tone, w.first + k == sel, false };
        g_win[k] = trx_status_row(card, t.space_md, ry, lw - 2 * t.space_md, row, &ry);
    }
    g_selpan_w = LIB_SIDE_W;
    g_selpan_h = bottom - py;
    g_selpan = trx_card(c, X0 + lw + t.space_lg, py, LIB_SIDE_W, bottom - py, true);
    draw_selpan(m, sel);
}

// ---- the monitor: trxmon's screen, 80 x 40 cells on the whole layer, no frame, no rail, no footer -----------------------------------------------------------------
// The cell is 12 x 13 pixels (the 8 x 8 pixel font with 4 px between the letters), the colours the C64 palette gives the user interface's schemes (userinterface.cc).
static uint32_t mon_color(int attr)
{
    switch (attr) {
    case 1: return 0xFFFFFF;                 // UA_STATUS
    case 2: return 0xFFFFFF;                 // UA_REGS
    case 3: return 0x959595;                 // UA_ECHO
    case 4: return 0xCB7E75;                 // UA_ERR
    case 5: return 0xFFFFFF;                 // UA_PROMPT
    case 7: return 0x6C6C6C;                 // UA_SEP
    case 8: return 0x959595;                 // UA_HINT
    default: return 0xFFFFFF;
    }
}

#define MON_ROWS_MAX 50
static lv_obj_t *g_mrow[MON_ROWS_MAX];
static char g_mtxt[MON_ROWS_MAX][96];
static int g_mattr[MON_ROWS_MAX];
static lv_obj_t *g_mcur;

// The rows are made once; a key writes the rows that changed (the label's text and colour) and moves the cursor cell. `g` is the grid the node's `grid` parameter names (textgrid.h),
// built by render() before this call; null: no grid, an empty screen without a cursor.
static void build_terminal(lv_obj_t *scr, bool fresh, const TextGrid *g)
{
    if (fresh) {
        trx_shell_forget();
        lv_obj_clean(scr);
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        g_mcur = trx_box(scr, 0, 10, 12, 13, 0xFFFFFF);                // the cursor: a reversed cell, under the letters
        for (int r = 0; r < MON_ROWS_MAX; r++) {
            g_mrow[r] = 0;
            g_mtxt[r][0] = 0;
            g_mattr[r] = -1;
        }
    }
    int cx = -1, cy = -1;
    if (g) {
        g->cursor(&cx, &cy);
    }
    lv_obj_set_flag(g_mcur, LV_OBJ_FLAG_HIDDEN, cx < 0 || cy < 0);
    if (cx >= 0 && cy >= 0) {
        lv_obj_set_pos(g_mcur, cx * 12, 10 + cy * 13);
    }
    int rows = g ? g->rows() : 0;
    for (int r = 0; r < MON_ROWS_MAX; r++) {
        const char *l = r < rows && r < TG_ROWS_MAX ? g->line(r) : "";
        int at = r < rows ? g->attr(r) : TG_NORMAL;
        if (!g_mrow[r]) {
            if (!l[0]) {
                continue;
            }
            g_mrow[r] = trx_txt(scr, 0, 10 + r * 13 + 2, "", &lv_font_unscii_8, 0xFFFFFF);
            lv_obj_set_style_text_letter_space(g_mrow[r], 4, 0);
        }
        if (strcmp(g_mtxt[r], l)) {
            snprintf(g_mtxt[r], sizeof(g_mtxt[r]), "%s", l);
            lv_label_set_text(g_mrow[r], l);
        }
        if (g_mattr[r] != at) {
            g_mattr[r] = at;
            lv_obj_set_style_text_color(g_mrow[r], lv_color_hex(mon_color(at)), 0);
        }
    }
}

// ---- the viewer: View and Hex View of a file, in the pixel font so that columns line up ---------------------------------------------------------------
static void build_viewer(lv_obj_t *c, MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    int W = trx_content_w() - 2 * X0;
    const char *file = m.view_title();
    const char *slash = strrchr(file, '/');
    char crumb[48], an[32];
    snprintf(crumb, sizeof(crumb), "%s / %s", upper(an, sizeof(an), m.area_name()), m.view_is_hex() ? "HEX VIEW" : "VIEW");
    int y = trx_head(c, X0, 12, W, crumb, slash ? slash + 1 : file, 0, TT_MUTED);
    lv_obj_t *card = trx_card(c, X0, y, W, MenuCtl::VIEW_ROWS * 13 + 12, false);
    char line[128];
    for (int r = 0; r < MenuCtl::VIEW_ROWS; r++) {
        m.view_line(r, line, sizeof(line));
        if (line[0]) {
            trx_txt(card, 6, 6 + r * 13, line, &lv_font_unscii_8, t.ink);
        }
    }
    m.view_status(line, sizeof(line));
    trx_txt(c, X0, trx_content_h() - 22, line, t.caption, t.muted);
}

// ---- the pixel layer's own page ("timeline" layout): no menu, no bar. The base draws nothing on it (T256 D12): a library paints its own UI as elements. Over the still the page is transparent
// (D4), else it is the live key, so the machine's picture shows through the hole.
static void build_timeline(lv_obj_t *scr)
{
    trx_shell_forget();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(TRX_LIVE_KEY_RGB), 0);
    lv_obj_set_style_bg_opa(scr, ui_still_under() ? LV_OPA_TRANSP : LV_OPA_COVER, 0);   // T256 D4: over the still the page lets it through itself, the key is not on
}

static void build_sheet(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    const char *file = m.chooser_file();
    const char *slash = strrchr(file, '/');
    const char *base = slash ? slash + 1 : file;
    const char *dot = strrchr(base, '.');
    char crumb[48], ext[12];
    snprintf(crumb, sizeof(crumb), "LIBRARY / %s", upper(ext, sizeof(ext), dot ? dot + 1 : ""));
    int y = trx_head(c, X0, 12, W, crumb, base, 0, TT_MUTED);
    int bottom = H - 30;
    int lw = W - SIDE_W - t.space_lg;
    lv_obj_t *card = trx_card(c, X0, y, lw, bottom - y, false);
    TrxWindow w = trx_window(m.chooser_count(), sel, g_lib_first, lib_rows(true));
    g_lib_first = w.first;
    g_wfirst = w.first;
    g_wn = w.count;
    int ry = t.space_sm;
    for (int k = 0; k < w.count; k++) {
        int i = w.first + k;
        char kc[4] = { m.chooser_key(i), 0, 0, 0 };
        TrxStatusRow row = { m.chooser_label(i), kc[0] ? kc : (i == 0 ? "Default" : 0), i == 0 ? TT_VIOLET : TT_MUTED, i == sel, false };
        g_win[k] = trx_status_row(card, t.space_md, ry, lw - 2 * t.space_md, row, &ry);
    }
    lv_obj_t *sp = trx_card(c, X0 + lw + t.space_lg, y, SIDE_W, bottom - y, true);
    int sy = trx_kv_row(sp, t.space_md, t.space_md, SIDE_W - 2 * t.space_md, "Format", ext[0] ? ext : "-", TT_MUTED);
    char sheet[96], sx[96];
    if (m.layout_param("sheetx", sx, sizeof(sx)) && dot) {          // "ext|label|state|empty": a type that goes elsewhere than the drive names its own target
        char *f2 = strchr(sx, '|');
        char *f3 = f2 ? strchr(f2 + 1, '|') : 0;
        char *f4 = f3 ? strchr(f3 + 1, '|') : 0;
        if (f4) {
            *f2 = *f3 = *f4 = 0;
            if (!strcasecmp(sx, dot)) {
                char v[48];
                m.spec_text(f3 + 1, v, sizeof(v));
                sy = trx_kv_row(sp, t.space_md, sy, SIDE_W - 2 * t.space_md, f2 + 1, v[0] ? v : f4 + 1, TT_MUTED);
                sheet[0] = 0;
                goto sheet_done;
            }
        }
    }
    if (m.layout_param("sheet", sheet, sizeof(sheet))) {          // "label|value spec": a line of the machine's state that the action acts on
        char *bar = strchr(sheet, '|');
        if (bar) {
            *bar = 0;
            char v[48];
            m.spec_text(bar + 1, v, sizeof(v));
            sy = trx_kv_row(sp, t.space_md, sy, SIDE_W - 2 * t.space_md, sheet, v, TT_MUTED);
        }
    }
sheet_done:
    char dir[120];
    snprintf(dir, sizeof(dir), "%s", file);
    if (slash && (size_t)(slash - file) < sizeof(dir)) {
        dir[slash - file] = 0;
    }
    trx_txt(sp, t.space_md, sy + t.space_sm, dir[0] ? dir : "/", t.caption, t.muted, SIDE_W - 2 * t.space_md);
    trx_txt(sp, t.space_md, bottom - y - 26, "<-  BACK TO LIBRARY", t.caption, t.muted);
}

// ---- an application's own screens (T104.3 D0, D9.1) ---------------------------------------------------------------------------------------------
// Declared by the application's menu record, drawn here from its state values: layout `app` (head, a card of rows, a progress bar, bank maps, a status line, a bar of
// buttons, an optional side panel), layout `grid` (a card of text rows, a side panel) and the terminal. What changes while the application works - the cells of a bank
// map, the bar, the status line, the grid's text - is written into the objects that stand (live_update), so a step of a job does not build the pane again.
#define MAP_MAX 256
#define GRID_MAX 44
struct MapW {
    int n;
    lv_obj_t *title, *detail;
    lv_obj_t *cell[MAP_MAX];
    char shown[MAP_MAX];
    char title_txt[40], detail_txt[64];
    char key[12];
    TrxTone tone;
};
static MapW g_map[2];
static lv_obj_t *g_pfill, *g_plabel, *g_pnote;
static int g_pw, g_pv = -1;
static char g_plabel_txt[80], g_pnote_txt[120];
static lv_obj_t *g_slabel;
static char g_slabel_txt[200];
static lv_obj_t *g_grow[GRID_MAX], *g_gin;
static char g_grow_txt[GRID_MAX][100], g_gin_txt[100];
static int g_gridn;
static const char *g_grid_key;

static void live_forget(void)
{
    memset(g_map, 0, sizeof(g_map));
    g_pfill = g_plabel = g_pnote = 0;
    g_pv = -1;
    g_plabel_txt[0] = g_pnote_txt[0] = 0;
    g_slabel = 0;
    g_slabel_txt[0] = 0;
    memset(g_grow, 0, sizeof(g_grow));
    g_gin = 0;
    g_gridn = 0;
    g_gin_txt[0] = 0;
}

static TrxTone tone_named(const char *name)
{
    static const char *const nm[] = { "muted", "cyan", "violet", "amber", "coral" };
    for (int i = 0; i < 5; i++) {
        if (name && !strcmp(name, nm[i])) {
            return (TrxTone)i;
        }
    }
    return TT_CYAN;
}

static uint32_t cell_color(char st, TrxTone tone)
{
    const TrxTheme &t = trx_theme();
    switch (st) {
    case '1': return t.surface2;                       // empty: erased, all $FF
    case '2': return trx_tone_color(tone);             // has data
    case '3': return trx_tone_color(TT_CORAL);         // being erased
    case '4': return t.warn;                           // being written or read
    case '5': return t.ok;                             // verified, dumped
    case '6': return t.bad;                            // failed
    default: return t.line;                            // not scanned yet
    }
}

// One bank map (components/crt_bank_map.xml): the chip's name, a detail at the right, the sixteen columns and a row for each sixteen banks, a cell for each bank.
static void draw_map(lv_obj_t *par, MapW &w, int x, int y, int width, int h, const char *title, const char *detail, const char *cells)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *card = trx_card(par, x, y, width, h, false);
    snprintf(w.title_txt, sizeof(w.title_txt), "%s", title);
    snprintf(w.detail_txt, sizeof(w.detail_txt), "%s", detail);
    w.title = trx_txt(card, t.space_md, t.space_sm, w.title_txt, t.heading, t.ink);
    int dw = trx_text_w(detail, t.caption);
    w.detail = trx_txt(card, width - t.space_md - 150, t.space_sm + 5, w.detail_txt, t.caption, trx_tone_color(w.tone));
    lv_obj_set_width(w.detail, 150);
    lv_obj_set_style_text_align(w.detail, LV_TEXT_ALIGN_RIGHT, 0);
    (void)dw;
    int n = (int)strlen(cells);
    n = n > MAP_MAX ? MAP_MAX : n;
    int rows = (n + 15) / 16;
    rows = rows < 1 ? 1 : rows;
    int top = t.space_sm + 22;
    int lw = 24;
    int pitch = (width - lw - 2 * t.space_md) / 16;
    int hdr = h - top > 40 ? 12 : 0;
    int avail = h - top - hdr - t.space_sm;
    int rh = avail / rows;
    rh = rh > 14 ? 14 : (rh < 4 ? 4 : rh);
    char hexd[2] = { 0, 0 };
    if (hdr) {
        for (int c = 0; c < 16; c++) {
            hexd[0] = "0123456789ABCDEF"[c];
            trx_txt(card, t.space_md + lw + c * pitch + pitch / 2 - 3, top - 2, hexd, t.caption, t.muted);
        }
    }
    int cy0 = top + hdr;
    for (int r = 0; r < rows; r++) {
        char lab[4] = { "0123456789ABCDEF"[(r / 16) & 15], "0123456789ABCDEF"[r & 15], 0, 0 };
        if (rh >= 9) {
            trx_txt(card, t.space_md, cy0 + r * rh - 1, lab, t.caption, t.muted);
        }
        for (int c = 0; c < 16; c++) {
            int i = r * 16 + c;
            if (i >= n) {
                break;
            }
            w.cell[i] = trx_box(card, t.space_md + lw + c * pitch, cy0 + r * rh, pitch - 2, rh - 2, cell_color(cells[i], w.tone));
            w.shown[i] = cells[i];
        }
    }
    w.n = n;
}

static void map_update(MenuCtl &m, MapW &w)
{
    char cells[MAP_MAX + 4], spec[40], det[64];
    snprintf(spec, sizeof(spec), "%s_s", w.key);
    if (m.tab_spec(spec, cells, sizeof(cells)) || cells[0] == 0) {
        for (int i = 0; i < w.n && cells[i]; i++) {
            if (cells[i] != w.shown[i]) {
                lv_obj_set_style_bg_color(w.cell[i], lv_color_hex(cell_color(cells[i], w.tone)), 0);
                w.shown[i] = cells[i];
            }
        }
    }
    snprintf(spec, sizeof(spec), "%s_d", w.key);
    m.tab_spec(spec, det, sizeof(det));
    if (strcmp(det, w.detail_txt)) {
        snprintf(w.detail_txt, sizeof(w.detail_txt), "%s", det);
        lv_label_set_text(w.detail, det);
    }
}

static void live_update(MenuCtl &m)
{
    for (int k = 0; k < 2; k++) {
        if (g_map[k].n) {
            map_update(m, g_map[k]);
        }
    }
    const TrxTheme &t = trx_theme();
    if (g_pfill) {
        char v[16], lab[80], note[120];
        m.tab_spec("pct", v, sizeof(v));
        m.tab_spec("pct_label", lab, sizeof(lab));
        m.tab_spec("pct_note", note, sizeof(note));
        int pct = atoi(v);
        pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
        if (pct != g_pv) {
            lv_obj_set_width(g_pfill, pct ? g_pw * pct / 100 : 0);
            g_pv = pct;
        }
        if (strcmp(lab, g_plabel_txt)) {
            snprintf(g_plabel_txt, sizeof(g_plabel_txt), "%s", lab);
            lv_label_set_text(g_plabel, lab);
        }
        if (strcmp(note, g_pnote_txt)) {
            snprintf(g_pnote_txt, sizeof(g_pnote_txt), "%s", note);
            lv_label_set_text(g_pnote, note);
        }
    }
    if (g_slabel) {
        char st[200];
        m.tab_spec("status", st, sizeof(st));
        if (strcmp(st, g_slabel_txt)) {
            snprintf(g_slabel_txt, sizeof(g_slabel_txt), "%s", st);
            lv_label_set_text(g_slabel, st);
        }
    }
    if (g_gridn && g_grid_key) {
        char buf[GRID_MAX * 100];
        m.tab_spec(g_grid_key, buf, sizeof(buf));
        const char *p = buf;
        for (int i = 0; i < g_gridn; i++) {
            char line[100];
            int l = 0;
            while (*p && *p != '\n' && l < (int)sizeof(line) - 1) {
                line[l++] = *p++;
            }
            if (*p == '\n') {
                p++;
            }
            line[l] = 0;
            if (strcmp(line, g_grow_txt[i])) {
                snprintf(g_grow_txt[i], sizeof(g_grow_txt[i]), "%s", line);
                bool dim = line[0] == '~';
                bool hot = line[0] == '^';
                bool bad = line[0] == '!';
                lv_label_set_text(g_grow[i], line + (dim || hot || bad ? 1 : 0));
                lv_obj_set_style_text_color(g_grow[i], lv_color_hex(dim ? t.muted : (hot ? t.accent : (bad ? t.bad : t.ink))), 0);
            }
        }
        if (g_gin) {
            char in[100];
            m.tab_spec("input", in, sizeof(in));
            if (strcmp(in, g_gin_txt)) {
                snprintf(g_gin_txt, sizeof(g_gin_txt), "%s", in);
                lv_label_set_text(g_gin, in);
            }
        }
    }
}

static void build_app(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    live_forget();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    int ty;
    int y = head_of(c, m, sel, &ty);
    bool bar = items_in("bar") > 0;
    int bottom = H - 30 - (bar ? 38 : 0);
    char p[96], kick[48];
    kick[0] = 0;
    bool side = m.layout_param("side", kick, sizeof(kick)) || items_in("side") > 0;
    int mw = side ? W - SIDE_W - t.space_lg : W;
    bool has_status = m.layout_param("status", p, sizeof(p));
    bool has_pct = m.layout_param("pct", p, sizeof(p));
    bool has_note = m.layout_param("note", p, sizeof(p));
    bool m1 = m.layout_param("map1", p, sizeof(p)), m2 = m.layout_param("map2", p, sizeof(p));
    int status_h = has_status ? 26 : 0;
    int pct_h = has_pct ? 70 : 0;
    int gap = t.space_md;
    int end = bottom - status_h;                       // the left column ends above the status line
    // the rows card: as tall as its rows, never more than what the maps can spare
    int nu = units("main");
    int rows_h = 0;
    for (int k = 0; k < nu; k++) {
        rows_h += g_u[k].h;
    }
    int card_h = nu ? rows_h + 2 * t.space_sm : 0;
    int min_maps = (m1 || m2) ? 90 : 0;
    int room = end - y - (has_pct ? pct_h + gap : 0) - (min_maps ? min_maps + gap : 0) - (has_note ? 18 : 0);
    if (card_h > room) {
        card_h = room > 0 ? room : 0;
    }
    int ly = y;
    if (card_h) {
        lv_obj_t *card = trx_card(c, X0, ly, mw, card_h, false);
        g_mfirst = 0;
        rows_of(card, t.space_md, t.space_sm, mw - 2 * t.space_md, "main", sel, card_h - t.space_sm);
        ly += card_h + gap;
    }
    if (has_note) {
        char note[160];
        m.tab_spec("note", note, sizeof(note));
        if (!note[0]) {
            m.layout_param("note", note, sizeof(note));
        }
        lv_obj_t *nl = trx_txt(c, X0 + t.space_xs, ly - gap + 4, note, t.caption, t.muted, mw);
        lv_label_set_long_mode(nl, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(nl, mw);
        ly += 18 * (trx_text_w(note, t.caption) > mw ? 2 : 1);
    }
    if (has_pct) {
        lv_obj_t *card = trx_card(c, X0, ly, mw, pct_h, true);
        g_plabel = trx_txt(card, t.space_md, t.space_sm + 2, "", t.body, t.ink, mw - 200);
        g_pnote = trx_txt(card, mw - t.space_md - 180, t.space_sm + 4, "", t.caption, t.warn, 180);
        lv_obj_set_style_text_align(g_pnote, LV_TEXT_ALIGN_RIGHT, 0);
        g_pw = mw - 2 * t.space_md;
        trx_box(card, t.space_md, t.space_sm + 30, g_pw, 8, t.line);
        g_pfill = trx_box(card, t.space_md, t.space_sm + 30, 0, 8, t.warn);
        g_pv = -1;
        g_plabel_txt[0] = g_pnote_txt[0] = 0;
        ly += pct_h + gap;
    }
    if ((m1 || m2) && end - ly > 50) {
        int mh = end - ly;
        int nmaps = (m1 ? 1 : 0) + (m2 ? 1 : 0);
        int mwid = (mw - (nmaps - 1) * t.space_lg) / nmaps;
        int mx = X0;
        for (int k = 0; k < 2; k++) {
            if (!(k ? m2 : m1)) {
                continue;
            }
            char key[12], spec[16], title[48], tone[16], cells[MAP_MAX + 4], det[64];
            snprintf(key, sizeof(key), "map%d", k + 1);
            m.layout_param(key, title, sizeof(title));
            snprintf(spec, sizeof(spec), "%s_t", key);
            tone[0] = 0;
            m.layout_param(spec, tone, sizeof(tone));
            snprintf(spec, sizeof(spec), "%s_s", key);
            m.tab_spec(spec, cells, sizeof(cells));
            snprintf(spec, sizeof(spec), "%s_d", key);
            m.tab_spec(spec, det, sizeof(det));
            snprintf(g_map[k].key, sizeof(g_map[k].key), "%s", key);
            g_map[k].tone = tone_named(tone);
            draw_map(c, g_map[k], mx, ly, mwid, mh, title, det, cells);
            mx += mwid + t.space_lg;
        }
    }
    if (has_status) {
        trx_txt(c, X0, bottom - 20, "STATUS", t.caption, t.muted);
        g_slabel = trx_txt(c, X0 + 62, bottom - 22, "", t.body, t.ink, mw - 62);
        g_slabel_txt[0] = 0;
    }
    if (side) {
        side_panel(c, X0 + mw + t.space_lg, y, SIDE_W, bottom - y, kick, sel);
    }
    if (bar) {
        button_row(c, X0 + W, bottom + 8, "bar", sel);
    }
}

// Lines of text in the pixel font in a card: a memory dump, the monitor. `key` names the tab's state value that holds them, one line to a row.
static void grid_card(lv_obj_t *c, MenuCtl &m, int x, int y, int w, int nrows, const char *key, bool input_line)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *card = trx_card(c, x, y, w, (nrows + (input_line ? 1 : 0)) * 14 + 12, false);
    g_gridn = nrows < GRID_MAX ? nrows : GRID_MAX;
    g_grid_key = key;
    for (int r = 0; r < g_gridn; r++) {
        g_grow[r] = trx_txt(card, 8, 6 + r * 14, "", &lv_font_unscii_8, t.ink);
        g_grow_txt[r][0] = 1;                          // differs from any line: the first live_update writes it
    }
    if (input_line) {
        g_gin = trx_txt(card, 8, 6 + g_gridn * 14, "", &lv_font_unscii_8, t.accent);
        g_gin_txt[0] = 1;
    }
    (void)m;
}

static void build_grid(lv_obj_t *c, MenuCtl &m, int sel)
{
    const TrxTheme &t = trx_theme();
    live_forget();
    int W = trx_content_w() - 2 * X0, H = trx_content_h();
    int ty;
    int y = head_of(c, m, sel, &ty);
    bool bar = items_in("bar") > 0;
    int bottom = H - 30 - (bar ? 38 : 0);
    char kick[48], rows[16];
    kick[0] = 0;
    bool side = m.layout_param("side", kick, sizeof(kick)) || items_in("side") > 0;
    int mw = side ? W - SIDE_W - t.space_lg : W;
    int nrows = 12;
    if (m.layout_param("lines", rows, sizeof(rows))) {
        nrows = atoi(rows) > 0 ? atoi(rows) : nrows;     // `lines`: the number of rows of text a grid shows
    }
    int fit = (bottom - y - 20 - 12) / 14;
    nrows = nrows > fit ? fit : nrows;
    grid_card(c, m, X0, y, mw, nrows, "grid", false);
    char note[160];
    if (m.layout_param("note", note, sizeof(note))) {
        char t2[160];
        m.spec_text(note, t2, sizeof(t2));
        lv_obj_t *nl = trx_txt(c, X0, y + nrows * 14 + 18, t2, t.caption, t.muted, mw);
        lv_label_set_long_mode(nl, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(nl, mw);
    }
    if (side) {
        side_panel(c, X0 + mw + t.space_lg, y, SIDE_W, bottom - y, kick, sel);
    }
    if (bar) {
        button_row(c, X0 + W, bottom + 8, "bar", sel);
    }
}

#define MON_COLS 80                  // an application's terminal: 80 columns of the 8 pixel font, 21 rows unless the tab says `lines`
#define MON_ROWS 21
static void build_app_terminal(lv_obj_t *c, MenuCtl &m)
{
    const TrxTheme &t = trx_theme();
    live_forget();
    int W = trx_content_w() - 2 * X0;
    int ty;
    int y = head_of(c, m, -1, &ty);
    int cw = MON_COLS * 8 + 16;
    char p[16];
    int nrows = MON_ROWS;
    if (m.layout_param("lines", p, sizeof(p)) && atoi(p) > 0) {
        nrows = atoi(p);                                  // `lines`: the rows of text the terminal shows; a line may start with ~ (dim), ^ (accent) or ! (bad)
    }
    int fit = (trx_content_h() - y - 12) / 14;
    nrows = nrows > fit ? fit : nrows;
    grid_card(c, m, X0, y, cw < W ? cw : W, nrows, "rows", m.layout_param("input", p, sizeof(p)));
    (void)t;
}

// ---- the selection ---------------------------------------------------------------------------------------------------------------------------
static void apply_selection(MenuCtl &m, int sel, bool sheet)
{
    if (sel == g_sel) {
        return;
    }
    if (m.layout_is("library")) {
        int o = g_sel - g_wfirst, n = sel - g_wfirst;
        if (o >= 0 && o < g_wn) {
            trx_row_select(g_win[o], false);
        }
        if (n >= 0 && n < g_wn) {
            trx_row_select(g_win[n], true);
        }
        if (!sheet) {
            draw_selpan(m, sel);
        }
    } else {
        if (g_sel >= 0 && g_sel < g_n && g_obj[g_sel]) {
            trx_row_select(g_obj[g_sel], false);
        }
        if (sel >= 0 && sel < g_n && g_obj[sel]) {
            trx_row_select(g_obj[sel], true);
        }
    }
    g_sel = sel;
    g_cheap++;
}

static bool g_repage = false;

bool screens_repage_take()
{
    bool r = g_repage;
    g_repage = false;
    return r;
}

bool screens_render(lv_obj_t *scr, TrxShellInfo &info, const TrxRoute &route, MenuCtl &m)
{
    if (!m.screen()) {
        return false;
    }
    if (m.viewer()) {
        static uint32_t vsig;
        uint32_t h = hs(hi(hi(2166136261u, m.view_is_hex()), 7), m.view_title());
        char l[128];
        for (int r = 0; r < MenuCtl::VIEW_ROWS; r++) {
            m.view_line(r, l, sizeof(l));
            h = hs(h, l);
        }
        if (g_valid && g_viewer && h == vsig && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m))) {
            return true;
        }
        ui_menu_forget_rows();
        lv_obj_t *c = trx_shell(scr, info, route, m.area(), ui_shell_tab(m));
        build_viewer(c, m);
        vsig = h;
        g_viewer = true;
        g_valid = true;
        g_sig = 0;
        return true;
    }
    g_viewer = false;
    if (m.layout_is("timeline")) {                                // the pixel layer's own screen: no frame, no rail, no footer
        if (!(g_valid && g_live && g_full == 1)) {
            g_live = false;                                       // a rebuild keeps the key on: screens_forget would switch it off, the core would hear off, then on (menu_video)
            ui_menu_forget_rows();
            build_timeline(scr);
            g_valid = true;
            g_sig = 0;
            g_full = 1;
            g_live = true;
            ui_live_key(true);
        }
        return true;
    }
    if (m.layout_is("terminal") && !m.app_mode()) {               // a library's text screen (textgrid.h) on the whole layer
        char gn[40];
        const TextGrid *grid = m.layout_param("grid", gn, sizeof(gn)) ? menu_grid(gn) : 0;
        uint32_t h = 0;
        if (grid) {
            grid->render();
            h = grid->sig();
        }
        if (!(g_valid && g_full == 2 && h == g_sig)) {
            bool fresh = !(g_valid && g_full == 2);
            if (fresh) {
                ui_menu_forget_rows();
            }
            build_terminal(scr, fresh, grid);
            g_valid = true;
            g_sig = h;
            g_full = 2;
        }
        if (g_live) {
            g_live = false;
            ui_live_key(false);
        }
        return true;
    }
    g_full = 0;
    bool lib = m.layout_is("library");
    bool sheet = lib && m.chooser();
    int sel = m.selected();
    bool paged = false;                                           // T209 D4: the list is longer than its window: the foot names the paging keys
    g_n = 0;
    if (!lib) {
        int n = m.rows();
        g_n = n < MAXI ? n : MAXI;
        for (int i = 0; i < g_n; i++) {
            m.item(i, &g_it[i]);
        }
    } else if (g_area != m.area() || g_tab != m.tab() || strcmp(g_lib_path, m.browse_path())) {
        g_lib_first = 0;                                          // another folder or tab: the window starts at the top
        snprintf(g_lib_path, sizeof(g_lib_path), "%s", m.browse_path());
    }
    static uint32_t scroll_seen = 0;
    bool scroll_back = m.scroll_rev() != scroll_seen;             // T192.3 D30: the menu was opened where it was left; the window starts where it stood
    scroll_seen = m.scroll_rev();
    if (lib) {                                                    // the window follows the selection; a move of it is a new pane
        if (scroll_back) {
            g_lib_first = m.scroll_first();
        }
        m.set_page_rows(lib_rows(false));                         // T180 D7: a page of the F3 browser is the rows this window shows
        paged = (sheet ? m.chooser_count() : m.rows()) > lib_rows(sheet);
        g_lib_first = trx_window(sheet ? m.chooser_count() : m.rows(), sel, g_lib_first, lib_rows(sheet)).first;
        m.note_scroll(g_lib_first);
    }
    if (!lib) {
        static int w_area = -1, w_tab = -1;
        if (w_area != m.area() || w_tab != m.tab()) {
            g_mfirst = 0;
            w_area = m.area();
            w_tab = m.tab();
        }
        char nm[24];
        ui_name(m.layout(), nm, sizeof(nm));
        bool status = !strcmp(nm, "status");
        int n = units("main");
        if (scroll_back) {
            g_mfirst = m.scroll_first();
        }
        const int avail = status ? main_avail(false, true) : split_avail(items_in("bar") > 0);
        g_mfirst = window_first(n, sel, g_mfirst, avail);
        m.note_scroll(g_mfirst);
        int tot = 0;
        for (int k = 0; k < n; k++) {
            tot += g_u[k].h;
        }
        paged = tot > avail;
    }
    if (paged != m.paged()) {                                     // the caller made the foot from the last frame's answer
        m.note_paged(paged);
        g_repage = true;                                          // ui.cc makes the foot again and calls this once more
        return true;
    }
    uint32_t sig = signature(m);
    if (g_valid && g_area == m.area() && g_tab == m.tab() && trx_shell_same(scr, info, route, m.area(), ui_shell_tab(m)) && sig == g_sig) {
        apply_selection(m, sel, sheet);
        hint_update(m);
        if (m.app_mode()) {
            live_update(m);
        }
        return true;
    }
    ui_menu_forget_rows();                                        // the old rows and mine are gone with the pane
    lv_obj_t *c = trx_shell(scr, info, route, m.area(), ui_shell_tab(m));
    char name[24];
    ui_name(m.layout(), name, sizeof(name));
    bool home = !strcmp(name, "desk");
    if (home) {
        build_desk(c, m, sel);
    } else if (lib) {
        if (sheet) {
            build_sheet(c, m, sel);
        } else {
            build_library(c, m, sel);
        }
    } else if (!strcmp(name, "app")) {
        build_app(c, m, sel);
    } else if (!strcmp(name, "grid")) {
        build_grid(c, m, sel);
    } else if (!strcmp(name, "terminal")) {
        build_app_terminal(c, m);
    } else if (!strcmp(name, "status")) {
        build_status(c, m, sel);
    } else {
        build_split(c, m, sel);
    }
    hint_make(c, m);
    if (m.app_mode()) {
        live_update(m);
    }
    g_valid = true;
    g_sig = sig;
    g_area = m.area();
    g_tab = m.tab();
    g_sel = sel;
    if (g_live) {
        g_live = false;
        ui_live_key(false);
    }
    return true;
}
