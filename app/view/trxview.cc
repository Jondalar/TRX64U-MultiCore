// The shared view layer (T104 D8/5.8). See trxview.h.
//
// Geometry and palette are the prototype's consts (lvgl_ui_project/globals.xml): sidebar 148, top bar 36,
// tab strip 38, footer 28, spacing 2/4/6/10/18/28, radius 2/4, and the ten colours sampled from
// trex64-dev.github.io. The fonts are the ones this build already carries (lv_conf.h): Montserrat for the
// panel skin, UNSCII for the terminal skin, so no font binary is added to the repository.
#include <ctype.h>
#include "trxview.h"
#include <string.h>
#include <stdio.h>

#include "../video/trxcanvas.h"

// The canvas the frame is laid out on (T164 D10a): 960 x 540, or the mode's own size below 960 wide (the compact layout).
static int g_sw = TRX_CANVAS_FULL_W, g_sh = TRX_CANVAS_FULL_H;
#define SW g_sw
#define SH g_sh

static TrxTheme k_panel = {
    0x070a12, 0x0d1220, 0x111827, 0x273047, 0xf4f2fb, 0x9da6bd,
    0x8376ff, 0x6de7ff, 0xffc15a, 0xff7a72, 0x1c1a3f,
    &lv_font_montserrat_14, &lv_font_montserrat_14, &lv_font_montserrat_16,
    &lv_font_montserrat_16, &lv_font_montserrat_24,
    148, 36, 38, 28,
    4, 6, 10, 18, 28,
    0,                               // the concept's radius is 4 (2 for buttons); the build has LV_DRAW_SW_COMPLEX off (hostapp/lv_conf.h: a masked fill costs ~2 ms per 1,000 px), where a box with a radius is not drawn at all
    false,
};

// The terminal prototype: the same layout, a green monochrome palette and the pixel font.
static TrxTheme k_terminal = {
    0x050806, 0x070c08, 0x0a110c, 0x1c3324, 0xd6f5df, 0x6f9b7e,
    0x58d08b, 0x58d08b, 0xffc15a, 0xff7a72, 0x0f2015,
    &lv_font_unscii_8, &lv_font_unscii_8, &lv_font_unscii_16,
    &lv_font_unscii_16, &lv_font_unscii_16,
    148, 36, 38, 28,
    4, 6, 10, 18, 28,
    0,
    true,
};

static TrxSkin g_skin = TRX_SKIN_PANEL;

void trx_skin_set(TrxSkin s) { g_skin = s; }

// The colour schemes of Gideon's "Color Scheme" item (userinterface.cc `colors`): the panel skin's eleven colours, in the order of TrxTheme (bg, surface, surface2,
// line, ink, muted, accent, ok, warn, bad, accent_soft). Scheme 1, "Ultimate Black", is the concept's own palette.
static const uint32_t k_schemes[7][11] = {
    { 0x2b2070, 0x352879, 0x40318d, 0x5a4ab0, 0xe8e0ff, 0xb0a6e8, 0xffffff, 0x8bdc8b, 0xffd070, 0xff8080, 0x4a3da0 },     // Standard Blue
    { 0x070a12, 0x0d1220, 0x111827, 0x273047, 0xf4f2fb, 0x9da6bd, 0x8376ff, 0x6de7ff, 0xffc15a, 0xff7a72, 0x1c1a3f },     // Ultimate Black
    { 0x0a140c, 0x101e12, 0x16281a, 0x2a4a30, 0xd8f4da, 0x86b08a, 0x70e070, 0x70e0b0, 0xe8d060, 0xff7a72, 0x143018 },     // C128 Style
    { 0x10104a, 0x181860, 0x202078, 0x3c3ca0, 0xe0e0ff, 0xa0a0e0, 0x8080ff, 0x70d8ff, 0xffd060, 0xff7a72, 0x282880 },     // Commodore Blue
    { 0x1a1208, 0x251a0c, 0x30220f, 0x5a4020, 0xf6e8d0, 0xb89c70, 0xe09040, 0x90d070, 0xffc040, 0xff7a60, 0x3a2810 },     // Commodore 1
    { 0x101010, 0x1a1a1a, 0x242424, 0x404040, 0xf0f0f0, 0xa0a0a0, 0xffffff, 0x80e080, 0xffc060, 0xff7070, 0x303030 },     // Commodore 2
    { 0x1c0a10, 0x28101a, 0x341624, 0x5c2a40, 0xfae0e8, 0xc090a0, 0xe85a6a, 0x80d8a0, 0xffc060, 0xff9090, 0x40182a },     // Commodore 3
};
void trx_scheme_set(int n)
{
    if (n < 0 || n > 6) {
        return;
    }
    const uint32_t *c = k_schemes[n];
    k_panel.bg = c[0]; k_panel.surface = c[1]; k_panel.surface2 = c[2]; k_panel.line = c[3]; k_panel.ink = c[4]; k_panel.muted = c[5];
    k_panel.accent = c[6]; k_panel.ok = c[7]; k_panel.warn = c[8]; k_panel.bad = c[9]; k_panel.accent_soft = c[10];
}
TrxSkin trx_skin(void) { return g_skin; }
const TrxTheme &trx_theme(void) { return g_skin == TRX_SKIN_TERMINAL ? k_terminal : k_panel; }

// The shell's measures of the two layouts. Compact: a narrower rail, thinner bars; the fonts stay, they are the readable ones.
static void metrics_apply(TrxTheme &t, bool compact)
{
    t.sidebar_w = compact ? 124 : 148;                  // T187 gate 1: SETTINGS and its key (and the shifted keys) fit the compact rail with a gap
    t.topbar_h = compact ? 34 : 36;
    t.tabbar_h = compact ? 34 : 38;
    t.footer_h = compact ? 26 : 28;
}

void trx_canvas_set(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
#ifdef TRX_CANVAS_FIXED                       // the bench's negative control: the old fixed 960 x 540 view, whatever the mode
    return;
#endif
    g_sw = w;
    g_sh = h;
    metrics_apply(k_panel, trx_compact());
    metrics_apply(k_terminal, trx_compact());
}
int trx_canvas_w(void) { return g_sw; }
int trx_canvas_h(void) { return g_sh; }
bool trx_compact(void) { return g_sw < TRX_CANVAS_FULL_W; }

lv_obj_t *trx_box(lv_obj_t *par, int x, int y, int w, int h, uint32_t bg, int border, uint32_t bcol,
                  uint8_t border_side)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *o = lv_obj_create(par);
    lv_obj_set_flag(o, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_border_side(o, (lv_border_side_t)border_side, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(bcol ? bcol : t.line), 0);
    return o;
}

lv_obj_t *trx_txt(lv_obj_t *par, int x, int y, const char *s, const lv_font_t *f, uint32_t col, int w)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, s ? s : "");
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    if (w) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, w);
    }
    lv_obj_set_pos(l, x, y);
    return l;
}

int trx_text_w(const char *s, const lv_font_t *f)
{
    if (!s || !*s) {
        return 0;
    }
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

// ---- the frame ---------------------------------------------------------------------------------------
static int g_content_w, g_content_h;

int trx_content_w(void) { return g_content_w; }
int trx_content_h(void) { return g_content_h; }

// A navigation item of the rail: the label on the left, the logical key on the right, a bar of accent
// down the left edge when it is the open area (nav_item.xml).
// Both prototypes print the rail and the tabs in capitals, whatever the label is called elsewhere.
static const char *shout(const char *s)
{
    static char buf[4][40];
    static int n;
    char *b = buf[n++ & 3];
    int i = 0;
    for (; s && s[i] && i < 39; i++) {
        b[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    }
    b[i] = 0;
    return b;
}

#define RAIL_GAP 4

static lv_obj_t *nav_item(lv_obj_t *par, int y, const char *label, const char *key, bool active)
{
    const TrxTheme &t = trx_theme();
    label = shout(label);
    int w = t.sidebar_w - 2 * t.space_sm;
    lv_obj_t *row = trx_box(par, t.space_sm, y, w, 17, active ? t.accent_soft : t.surface);
    if (active) {
        trx_box(row, 0, 0, 2, 17, t.accent);
    }
    int kw = trx_text_w(key, t.caption);
    trx_txt(row, t.space_sm, 2, label, t.caption, active ? t.ink : t.muted, w - kw - t.space_xs - t.space_sm - RAIL_GAP);   // a gap between the label and the key
    trx_txt(row, w - kw - t.space_xs, 2, key, t.caption, active ? t.accent : t.muted);
    return row;
}

// A tab of the strip: the number in a keycap, then the label (tab_item.xml).
// `brief`: the keycap alone (a strip that is wider than its canvas shows the open tab with its label and the others as numbers).
static int tab_w(const char *label, bool brief)
{
    const TrxTheme &t = trx_theme();
    return brief ? 14 + 2 * t.space_sm : 14 + t.space_xs + trx_text_w(shout(label), t.caption) + 2 * t.space_sm;
}

static int tab_item(lv_obj_t *par, int x, int y, int n, const char *label, bool active, lv_obj_t **obj, bool brief = false)
{
    const TrxTheme &t = trx_theme();
    char num[4];
    num[0] = (char)('0' + (n % 10));
    num[1] = 0;
    int w = tab_w(label, brief);
    lv_obj_t *b = trx_box(par, x, y, w, 20, active ? t.accent_soft : t.surface2);
    lv_obj_t *cap = trx_box(b, t.space_sm, 3, 12, 14, active ? t.accent : t.line);
    trx_txt(cap, 3, 1, num, t.caption, active ? t.bg : t.muted);
    if (!brief) {
        trx_txt(b, t.space_sm + 14 + t.space_xs, 3, shout(label), t.caption, active ? t.ink : t.muted);
    }
    if (obj) {
        *obj = b;
    }
    return w;
}

// What the frame draws, in three numbers: the parts that never change while the application runs (product, section, footer, skin, the
// route's identity), the top bar (activity word and chips: the media chip changes when a stick mounts) and the navigation (area and tab).
// A change in the first rebuilds the frame; a change in the others redraws only those parts: the top bar, two items of the rail, the
// tab strip (T104 5.11: an area key used to build the whole screen again, 518,400 px, for a rail item and a strip that changed).
static uint32_t fnv(uint32_t h, const char *p)
{
    for (; p && *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return (h ^ 0xFF) * 16777619u;                  // FNV-1a, as the browser's directory check uses
}

static uint32_t static_key(const TrxShellInfo &i, const TrxRoute &r)
{
    uint32_t h = 2166136261u;
    h = fnv(h, i.product);
    h = fnv(h, i.section);
    h = fnv(h, i.footer_left);
    h = (h ^ (uint32_t)r.nareas) * 16777619u;
    h = (h ^ (uint32_t)(uintptr_t)r.areas) * 16777619u;
    for (int a = 0; a < r.nareas && a < TRX_AREAS_MAX; a++) {       // the same array can hold another route (a core's name, its tabs): the rail and the strip show these texts
        h = fnv(h, r.areas[a].key);
        h = fnv(h, r.areas[a].title);
        for (int k = 0; k < r.areas[a].ntabs && k < TRX_TABS_MAX; k++) {
            h = fnv(h, r.areas[a].tabs[k].title);
        }
    }
    h = (h ^ (uint32_t)r.app_mode) * 16777619u;
    h = (h ^ (uint32_t)trx_skin()) * 16777619u;
    return h;
}

static uint32_t top_key(const TrxShellInfo &i)
{
    uint32_t h = 2166136261u;
    h = fnv(h, i.activity);
    for (int k = 0; k < i.nchips && k < TRX_CHIPS_MAX; k++) {
        h = fnv(h, i.chips[k].label);
    }
    return h;
}

static lv_obj_t *g_content, *g_top, *g_rail, *g_strip;
static lv_obj_t *g_nav[TRX_AREAS_MAX], *g_tabw[TRX_TABS_MAX];
static int g_nav_y[TRX_AREAS_MAX], g_tab_x[TRX_TABS_MAX];
static int g_strip_w;                  // the strip's width, and whether it had to show its inactive tabs as numbers (the open tab then changes every width)
static bool g_strip_brief;
static uint32_t g_k_static, g_k_top, g_k_foot;
static lv_obj_t *g_foot_lbl;
static int g_area = -2, g_tab = -2;
static bool g_has_strip;
static lv_obj_t *g_frame_scr;
static uint32_t g_frames, g_partials;

uint32_t trx_shell_frames(void) { return g_frames; }
uint32_t trx_shell_partials(void) { return g_partials; }

void trx_shell_forget(void)
{
    g_content = 0;
    g_frame_scr = 0;
    g_k_static = 0;
}

static bool route_has_strip(const TrxRoute &route, int area)
{
    return area >= 0 && area < route.nareas && route.areas[area].ntabs;
}

// the top bar: product, section, then the chips and the activity dot on the right
static void draw_top(lv_obj_t *top, const TrxShellInfo &info)
{
    const TrxTheme &t = trx_theme();
    int x = t.space_md;
    trx_txt(top, x, 9, info.product, t.heading, t.ink);
    x += trx_text_w(info.product, t.heading) + t.space_sm;
    if (info.section) {
        trx_txt(top, x, 13, info.section, t.caption, t.muted);
        x += trx_text_w(info.section, t.caption) + t.space_sm;
    }
    int rx = SW - t.space_md;
    if (info.activity) {
        int aw = trx_text_w(info.activity, t.caption);
        trx_txt(top, rx - aw, 13, info.activity, t.caption, t.muted);
        rx -= aw + t.space_sm;
        trx_box(top, rx - 4, 16, 4, 4, t.accent);
        rx -= 4 + t.space_md;
    }
    for (int i = (int)info.nchips - 1; i >= 0; i--) {
        const TrxChip &c = info.chips[i];
        if (!c.label) {
            continue;
        }
        int cw = trx_text_w(c.label, t.caption) + 2 * t.space_sm;
        if (rx - cw < x + t.space_sm) {
            continue;                               // a chip that would run into the title does not fit this canvas: it is left out, never drawn over
        }
        lv_obj_t *chip = trx_box(top, rx - cw, 8, cw, 18, t.surface2);
        trx_txt(chip, t.space_sm, 2, c.label, t.caption, c.tone ? c.tone : t.ok);
        rx -= cw + t.space_xs;
    }
}

static void draw_tab(const TrxRoute &route, int area, int i, bool active)
{
    const TrxArea &a = route.areas[area];
    if (g_tabw[i]) {
        lv_obj_delete(g_tabw[i]);
        g_tabw[i] = 0;
    }
    tab_item(g_strip, g_tab_x[i], 9, a.tabs[i].n, a.tabs[i].title, active, &g_tabw[i]);
}

// The strip of the open area: every tab, remembering where each stands so one tab can be drawn again alone.
static void draw_strip(const TrxRoute &route, int area, int tab)
{
    const TrxTheme &t = trx_theme();
    const TrxArea &a = route.areas[area];
    int tx = t.space_sm, full = t.space_sm;
    for (int i = 0; i < TRX_TABS_MAX; i++) {
        g_tabw[i] = 0;
    }
    for (int i = 0; i < a.ntabs && i < TRX_TABS_MAX; i++) {
        full += tab_w(a.tabs[i].title, false) + t.space_xs;
    }
    g_strip_brief = full > g_strip_w;
    for (int i = 0; i < a.ntabs && i < TRX_TABS_MAX; i++) {
        g_tab_x[i] = tx;
        tx += tab_item(g_strip, tx, 9, a.tabs[i].n, a.tabs[i].title, i == tab, &g_tabw[i], g_strip_brief && i != tab) + t.space_xs;
    }
}

bool trx_strip_probe(int *right, int *room)
{
    if (!g_strip) {
        return false;
    }
    int r = 0;
    for (int i = 0; i < TRX_TABS_MAX; i++) {
        if (g_tabw[i] && g_tab_x[i] + (int)lv_obj_get_width(g_tabw[i]) > r) {
            r = g_tab_x[i] + (int)lv_obj_get_width(g_tabw[i]);
        }
    }
    *right = r;
    *room = g_strip_w;
    return true;
}

// The bench's look at the rail: where the label's text ends and where the key begins, in the rail item i (false: no such item).
bool trx_nav_probe(int i, int *label_end, int *label_room, int *key_x)
{
    if (i < 0 || i >= TRX_AREAS_MAX || !g_nav[i]) {
        return false;
    }
    uint32_t n = lv_obj_get_child_count(g_nav[i]);
    if (n < 2) {
        return false;
    }
    lv_obj_t *lab = lv_obj_get_child(g_nav[i], (int32_t)n - 2), *key = lv_obj_get_child(g_nav[i], (int32_t)n - 1);
    lv_obj_update_layout(g_nav[i]);
    *label_end = lv_obj_get_x(lab) + trx_text_w(lv_label_get_text(lab), trx_theme().caption);
    *label_room = lv_obj_get_x(lab) + lv_obj_get_width(lab);
    *key_x = lv_obj_get_x(key);
    return true;
}

// The bench's look at the foot: the legend as drawn and how wide its text is (the line the canvas holds ends at the margin).
bool trx_foot_probe(char *txt, int n, int *text_w)
{
    if (!g_foot_lbl) {
        return false;
    }
    snprintf(txt, (size_t)n, "%s", lv_label_get_text(g_foot_lbl));
    *text_w = trx_text_w(txt, trx_theme().caption);
    return true;
}

static void draw_nav(const TrxRoute &route, int i, bool active)
{
    if (!g_rail) {
        return;                                     // an application's screens have no rail
    }
    if (g_nav[i]) {
        lv_obj_delete(g_nav[i]);
    }
    g_nav[i] = nav_item(g_rail, g_nav_y[i], route.areas[i].title, route.areas[i].key, active);
}

// The frame stands; bring the parts up to date that differ (top bar, rail, strip) and count it.
// The legend at the foot follows the row (T104.2 UI: the uniform browser says what its keys do for the row under the cursor): its text is written in place.
static void foot_patch(const TrxShellInfo &info)
{
    uint32_t kf = fnv(2166136261u, info.footer_right);
    if (kf == g_k_foot || !g_foot_lbl) {
        return;
    }
    const TrxTheme &t = trx_theme();
    lv_label_set_text(g_foot_lbl, info.footer_right ? info.footer_right : "");
    int fw = trx_text_w(info.footer_right, t.caption);
    if (fw > SW - 2 * t.space_md) {
        fw = SW - 2 * t.space_md;                   // a legend wider than the canvas ends in dots
    }
    lv_obj_set_width(g_foot_lbl, fw > 0 ? fw : 1);
    lv_obj_set_pos(g_foot_lbl, SW - fw - t.space_md, 8);
    g_k_foot = kf;
}

static void frame_patch(const TrxShellInfo &info, const TrxRoute &route, int area, int tab)
{
    foot_patch(info);
    uint32_t kt = top_key(info);
    if (kt != g_k_top) {
        lv_obj_clean(g_top);
        draw_top(g_top, info);
        g_k_top = kt;
        g_partials++;
    }
    if (area != g_area) {
        if (g_area >= 0 && g_area < route.nareas && g_area < TRX_AREAS_MAX) {
            draw_nav(route, g_area, false);
        }
        if (area >= 0 && area < route.nareas && area < TRX_AREAS_MAX) {
            draw_nav(route, area, true);
        }
        if (g_has_strip) {
            lv_obj_clean(g_strip);
            draw_strip(route, area, tab);
        }
        g_area = area;
        g_tab = tab;
        g_partials++;
    } else if (tab != g_tab) {
        if (g_has_strip && g_strip_brief) {                                // the widths follow the open tab: the strip is drawn again whole
            lv_obj_clean(g_strip);
            draw_strip(route, area, tab);
        } else if (g_has_strip) {
            if (g_tab >= 0 && g_tab < route.areas[area].ntabs && g_tab < TRX_TABS_MAX) {
                draw_tab(route, area, g_tab, false);
            }
            if (tab >= 0 && tab < route.areas[area].ntabs && tab < TRX_TABS_MAX) {
                draw_tab(route, area, tab, true);
            }
        }
        g_tab = tab;
        g_partials++;
    }
}

// Would a call of trx_shell() keep the whole frame? Then the caller keeps the content too (its rows are written in place). A top bar
// that changed (the media chip) is redrawn here, in place, and does not make the answer "no".
bool trx_shell_same(lv_obj_t *scr, const TrxShellInfo &info, const TrxRoute &route, int area, int tab)
{
    if (!g_content || g_frame_scr != scr || g_k_static != static_key(info, route) || area != g_area || tab != g_tab ||
        route_has_strip(route, area) != g_has_strip) {
        return false;
    }
    frame_patch(info, route, area, tab);
    return true;
}

lv_obj_t *trx_shell(lv_obj_t *scr, const TrxShellInfo &info, const TrxRoute &route, int area, int tab)
{
    const TrxTheme &t = trx_theme();
    uint32_t ks = static_key(info, route);
    bool strip = route_has_strip(route, area);
#ifndef TRX_SHELL_FULL_REBUILD                                  // the bench's control: every area or tab key builds the whole frame again
    if (g_content && g_frame_scr == scr && g_k_static == ks && g_has_strip == strip) {
        frame_patch(info, route, area, tab);
        lv_obj_clean(g_content);                    // the frame stands; only the pane is redrawn
        return g_content;
    }
#endif
    g_k_static = ks;
    g_k_top = top_key(info);
    g_frame_scr = scr;
    g_frames++;
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(t.bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    g_top = trx_box(scr, 0, 0, SW, t.topbar_h, t.bg, 1, t.line, LV_BORDER_SIDE_BOTTOM);
    draw_top(g_top, info);

    // the rail: the areas in the route's order (T228 D4), the close hint at the bottom. An application's own screens have none (T104.3 D9.1): its keys are its own.
    int body_y = t.topbar_h;
    int body_h = SH - t.topbar_h - t.footer_h;
    int sbw = route.app_mode ? 0 : t.sidebar_w;
    for (int i = 0; i < TRX_AREAS_MAX; i++) {
        g_nav[i] = 0;
    }
    g_rail = 0;
    if (!route.app_mode) {
        g_rail = trx_box(scr, 0, body_y, t.sidebar_w, body_h, t.surface, 1, t.line, LV_BORDER_SIDE_RIGHT);
        lv_obj_t *rail = g_rail;
        int y = t.space_sm;
        // T228 D4 / DC2: one list in the order of the route, no SHIFTED and no DIRECT heading
        for (int i = 0; i < route.nareas && i < TRX_AREAS_MAX; i++) {
            const TrxArea &a = route.areas[i];
            g_nav_y[i] = y;
            g_nav[i] = nav_item(rail, y, a.title, a.key, i == area);
            y += 19;
        }
        if (info.footer_left) {
            trx_txt(rail, t.space_sm, body_h - 16, info.footer_left, t.caption, t.muted);
        }
    }

    // the tab strip of the open area
    int cx = sbw;
    int cy = body_y;
    int cw = SW - sbw;
    g_strip = 0;
    if (strip) {
        g_strip = trx_box(scr, cx, cy, cw, t.tabbar_h, t.bg, 1, t.line, LV_BORDER_SIDE_BOTTOM);
        g_strip_w = cw;
        draw_strip(route, area, tab);
        cy += t.tabbar_h;
    }
    g_has_strip = strip;
    g_area = area;
    g_tab = tab;

    // the footer legend, the one line that never changes meaning
    lv_obj_t *foot = trx_box(scr, 0, SH - t.footer_h, SW, t.footer_h, t.bg, 1, t.line, LV_BORDER_SIDE_TOP);
    if (info.footer_right) {
        int fw = trx_text_w(info.footer_right, t.caption);
        if (fw > SW - 2 * t.space_md) {
            fw = SW - 2 * t.space_md;
        }
        g_foot_lbl = trx_txt(foot, SW - fw - t.space_md, 8, info.footer_right, t.caption, t.muted, fw);
        g_k_foot = fnv(2166136261u, info.footer_right);
    }

    g_content_w = cw;
    g_content_h = body_y + body_h - cy;
    g_content = trx_box(scr, cx, cy, g_content_w, g_content_h, t.bg);
    return g_content;
}

// ---- rows --------------------------------------------------------------------------------------------
// T209 D1/D2: every row is one line, 1 + 14 + 3 px, and the rows sit 1 px apart (a pitch of 19).
#define ROW_TOP 1
int trx_row_h(void) { return 18; }

int trx_kicker(lv_obj_t *par, int x, int y, const char *s)
{
    const TrxTheme &t = trx_theme();
    trx_txt(par, x, y, s, t.caption, t.muted);
    return y + 16;
}

// The selection of a row is four thin strips - a bar on the left, a hairline above, below and right - always built and hidden while
// the row is not selected, so trx_row_select() shows them without building anything and a move redraws those strips, not two rows
// (T104 5.11: the tinted row it replaces cost 31,000 px a key press). Children 0..3 are the strips, then the label, the sub line when
// there is one, and the value label as the last child - the row's user data says whether the value exists.
#define ROW_MARKS 4
#define ROW_BAR 3
static void row_marks(lv_obj_t *row, int w, int h, bool on, uint32_t col = 0)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *m[ROW_MARKS];
    if (!col) {
        col = t.accent;
    }
    m[0] = trx_box(row, 0, 0, ROW_BAR, h, col);
    m[1] = trx_box(row, 0, 0, w, 1, col);
    m[2] = trx_box(row, 0, h - 1, w, 1, col);
    m[3] = trx_box(row, w - 1, 0, 1, h, col);
    for (int i = 0; i < ROW_MARKS; i++) {
        lv_obj_set_flag(m[i], LV_OBJ_FLAG_CLICKABLE, false);
        if (!on) {
            lv_obj_add_flag(m[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}
// A label's box is as wide as its text (up to the row's room) so that a changed text repaints its own extent and not the row's width.
static void set_label_w(lv_obj_t *lab, const char *s, const lv_font_t *f, int room)
{
    int tw = trx_text_w(s, f) + 4;
    int want = tw < room ? tw : room;
    if (lv_obj_get_width(lab) != want) {
        lv_obj_set_width(lab, want);
    }
}

// The row's user data: what the row was last given, so a change can be written without reading LVGL's styles back.
enum { ROW_VAL = 1, ROW_SEL = 2, ROW_DIM = 4, ROW_STRONG = 8, ROW_MARK = 16 };

// T187 D9: a ring with a gap at the top right, 10 px, beside a label: "takes effect at the next reset". One object (its notch is a child of it, not of the row).
#define RESET_MARK_D 10
int trx_reset_mark_w(void) { return RESET_MARK_D; }
lv_obj_t *trx_reset_mark(lv_obj_t *par, int x, int y, uint32_t col, uint32_t bg)
{
    (void)bg;
    lv_obj_t *box = trx_box(par, x, y, RESET_MARK_D, RESET_MARK_D, col);               // the frame; its own fill is taken away: the strokes lie on the row
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_flag(box, LV_OBJ_FLAG_CLICKABLE, false);
    // a ring that is open at the top right with an arrowhead there: "again". Strokes of one pixel, 10 x 10.
    static const int8_t S[][4] = { { 2, 0, 4, 1 }, { 0, 2, 1, 6 }, { 2, 9, 6, 1 }, { 9, 5, 1, 3 }, { 1, 1, 1, 1 }, { 1, 8, 1, 1 }, { 8, 8, 1, 1 },
                                   { 6, 0, 4, 1 }, { 9, 0, 1, 4 } };
    for (unsigned i = 0; i < sizeof(S) / sizeof(S[0]); i++) {
        lv_obj_t *s = trx_box(box, S[i][0], S[i][1], S[i][2], S[i][3], col);
        lv_obj_set_flag(s, LV_OBJ_FLAG_CLICKABLE, false);
    }
    return box;
}

// The value (caption size) sits on the first line of the row, centred on the label's line (body size): the same middle, not the same top.
static int value_y(const TrxTheme &t)
{
#ifdef TRX_VALUE_TOP                                            // the bench's control: the value on the label's top plus 4 again
    return 4;
#endif
    int d = (lv_font_get_line_height(t.body) - lv_font_get_line_height(t.caption)) / 2;
    return ROW_TOP + (d > 0 ? d : 0);
}

// "start...end" by hand: "%.*s" in the RISC-V image printed "......" and then the whole name (ue2emu, 2026-11-12), so no printf precision here.
static void join_mid(char *buf, int n, const char *s, int a, int b)
{
    int len = (int)strlen(s), o = 0;
    for (int i = 0; i < a && o < n - 1; i++) {
        buf[o++] = s[i];
    }
    for (int i = 0; i < 3 && o < n - 1; i++) {
        buf[o++] = '.';
    }
    for (int i = b; i < len && o < n - 1; i++) {
        buf[o++] = s[i];
    }
    buf[o] = 0;
}

const char *trx_fit_mid(const char *s, int w, const lv_font_t *font, char *buf, int n)
{
    if (!s || trx_text_w(s, font) <= w) {
        return s;
    }
    int len = (int)strlen(s);
    if (len >= n) {
        len = n - 1;
    }
    for (int drop = 1; drop < len; drop++) {                    // drop characters around the middle until "start...end" fits
        int keep_l = (len - drop + 1) / 2, keep_r = len - drop - keep_l;
        int a = keep_l, b = len - keep_r;
        while (a > 0 && ((uint8_t)s[a] & 0xC0) == 0x80) {
            a--;
        }
        while (b < len && ((uint8_t)s[b] & 0xC0) == 0x80) {
            b++;
        }
        join_mid(buf, n, s, a, b);
        if (trx_text_w(buf, font) <= w) {
            return buf;
        }
    }
    if (len > 8) {                                              // nothing fits: still the first and last two characters (the label clips), never a bare "..."
        int a = 2, b = len - 2;
        while (b < len && ((uint8_t)s[b] & 0xC0) == 0x80) {
            b++;
        }
        join_mid(buf, n, s, a, b);
        return buf;
    }
    snprintf(buf, n, "...");
    return buf;
}

// A value that would squeeze its label is cut in the middle (a file name keeps its start and its extension): the label keeps its own width up to 45% of the row's room.
static const char *fit_value(const char *label, const char *val, int w, char *buf, int n)
{
    const TrxTheme &t = trx_theme();
    if (!val) {
        return 0;
    }
    int room = w - 2 * t.space_md;
    int lab = trx_text_w(label ? label : "", t.body) + 4;
    int keep = room * 45 / 100;
    int vmax = room - (lab < keep ? lab : keep) - t.space_md;
    return trx_fit_mid(val, vmax, t.caption, buf, n);
}

lv_obj_t *trx_row_at(lv_obj_t *par, int x, int y, int w, const TrxRow &r_in, int *next_y)
{
    TrxRow r = r_in;
    char vbuf[160];
    const TrxTheme &t = trx_theme();
    int h = trx_row_h();
    if (!w) {
        w = g_content_w - 2 * x;
    }
    lv_obj_t *row = trx_box(par, x, y, w, h, t.bg);
    row_marks(row, w, h, r.selected);
    if (r.heading) {                                            // T187 D1: the group's name, quiet; the same children as every row (marks hidden, the label), so a window move needs no special case
        const char *hl = shout(r.label);
        set_label_w(trx_txt(row, t.space_md, 3, hl, t.caption, t.muted, w - 2 * t.space_md), hl, t.caption, w - 2 * t.space_md);
        lv_obj_set_user_data(row, (void *)(uintptr_t)ROW_DIM);
        if (next_y) {
            *next_y = y + h + 1;
        }
        return row;
    }
    r.value = fit_value(r.label, r.value, w, vbuf, sizeof(vbuf));
    uint32_t ink = r.dim ? t.muted : t.ink;
    int vw = r.value ? trx_text_w(r.value, t.caption) : 0;
    int lw = w - 2 * t.space_md - (vw ? vw + t.space_md : 0);
    lv_obj_t *lab = trx_txt(row, t.space_md, ROW_TOP, r.label, t.body, ink, lw - (r.mark ? RESET_MARK_D + t.space_sm : 0));
    set_label_w(lab, r.label, t.body, lw - (r.mark ? RESET_MARK_D + t.space_sm : 0));
    if (r.mark) {
        int lwid = trx_text_w(r.label, t.body) + 4;
        if (lwid > lw - RESET_MARK_D - t.space_sm) {
            lwid = lw - RESET_MARK_D - t.space_sm;
        }
        trx_reset_mark(row, t.space_md + lwid + t.space_sm, ROW_TOP + (lv_font_get_line_height(t.body) - RESET_MARK_D) / 2 + 1, t.muted, t.bg);
    }
    if (r.value) {
        trx_txt(row, w - vw - t.space_md, value_y(t), r.value, t.caption, r.strong ? t.accent : t.muted);
    }
    lv_obj_set_user_data(row, (void *)(uintptr_t)((r.value ? ROW_VAL : 0u) | (r.selected ? ROW_SEL : 0u) | (r.dim ? ROW_DIM : 0u) |
                                                  (r.strong ? ROW_STRONG : 0u) | (r.mark ? ROW_MARK : 0u)));
    if (next_y) {
        *next_y = y + h + 1;
    }
    return row;
}

int trx_row(lv_obj_t *par, int x, int y, int w, const TrxRow &r)
{
    int ny = y;
    trx_row_at(par, x, y, w, r, &ny);
    return ny;
}

void trx_row_select(lv_obj_t *row, bool on)
{
    if (!row) {
        return;
    }
    const TrxTheme &t = trx_theme();
    uintptr_t ud = (uintptr_t)lv_obj_get_user_data(row);
    lv_obj_set_user_data(row, (void *)((ud & ~(uintptr_t)(ROW_SEL | ROW_STRONG)) | (on ? ROW_SEL | ROW_STRONG : 0)));
#ifdef TRX_ROW_TINT                                             // the bench's control: the selection as a tinted row again (31,000 px a move)
    lv_obj_set_style_bg_color(row, lv_color_hex(on ? t.accent_soft : t.bg), 0);
#endif
    for (int i = 0; i < ROW_MARKS; i++) {
        lv_obj_t *mark = lv_obj_get_child(row, i);
        if (mark && lv_obj_has_flag(mark, LV_OBJ_FLAG_HIDDEN) == on) {
            lv_obj_set_flag(mark, LV_OBJ_FLAG_HIDDEN, !on);
        }
    }
    if (ud & ROW_VAL) {                                         // the value label is the last child
        uint32_t n = lv_obj_get_child_count(row);
        lv_obj_t *val = n ? lv_obj_get_child(row, (int32_t)n - 1) : 0;
        if (val) {
            lv_obj_set_style_text_color(val, lv_color_hex(on ? t.accent : t.muted), 0);
        }
    }
}

// The bench's P4b probe (T187): the vertical middle of the row's label and of its value (the last child), as LVGL has them laid out. False: the row has no value.
bool trx_row_value_cy(lv_obj_t *row, int *lab_cy, int *val_cy)
{
    uintptr_t ud = row ? (uintptr_t)lv_obj_get_user_data(row) : 0;
    uint32_t n = row ? lv_obj_get_child_count(row) : 0;
    if (!(ud & ROW_VAL) || n < (uint32_t)(ROW_MARKS + 2)) {
        return false;
    }
    lv_obj_t *lab = lv_obj_get_child(row, ROW_MARKS), *val = lv_obj_get_child(row, (int32_t)n - 1);
    *lab_cy = lv_obj_get_y(lab) + lv_obj_get_height(lab) / 2;
    *val_cy = lv_obj_get_y(val) + lv_obj_get_height(val) / 2;
    return true;
}

// The bench's T205 P3 probe: the texts the row's label and value objects really hold (value "" when the row has none). False: not a row of trx_row_at().
bool trx_row_texts(lv_obj_t *row, char *lab, char *val, int n)
{
    uintptr_t ud = row ? (uintptr_t)lv_obj_get_user_data(row) : 0;
    uint32_t cnt = row ? lv_obj_get_child_count(row) : 0;
    if (cnt < (uint32_t)(ROW_MARKS + 1)) {
        return false;
    }
    snprintf(lab, n, "%s", lv_label_get_text(lv_obj_get_child(row, ROW_MARKS)));
    val[0] = 0;
    if (ud & ROW_VAL) {
        snprintf(val, n, "%s", lv_label_get_text(lv_obj_get_child(row, (int32_t)cnt - 1)));
    }
    return true;
}

// Give an existing row new content without building it again. The structure has to be the same one trx_row_at() made (a sub line
// or not, a value or not) - otherwise false and the caller builds the row. Only what differs is written, so LVGL invalidates the
// labels whose text moved and nothing else: a window that scrolls by one row changes the text of the rows, not their objects.
bool trx_row_retext(lv_obj_t *row, const TrxRow &r_in, int w)
{
    TrxRow r = r_in;
    char vbuf[160];
    const TrxTheme &t = trx_theme();
    if (r.heading) {                                            // the pane's shape holds the kinds; a heading in the same place may be another group's (a window that moved by pages)
        if (!row || lv_obj_get_child_count(row) != (uint32_t)(ROW_MARKS + 1)) {
            return false;
        }
        lv_obj_t *hl = lv_obj_get_child(row, ROW_MARKS);
        const char *txt = shout(r.label);
        if (strcmp(lv_label_get_text(hl), txt)) {
            lv_label_set_text(hl, txt);
            set_label_w(hl, txt, t.caption, lv_obj_get_width(row) - 2 * t.space_md);
        }
        return true;
    }
    uintptr_t ud = row ? (uintptr_t)lv_obj_get_user_data(row) : 0;
    bool has_val = (ud & ROW_VAL) != 0;
    bool has_mark = (ud & ROW_MARK) != 0;
    if (!row || has_mark != r.mark || lv_obj_get_height(row) != trx_row_h() ||
        lv_obj_get_child_count(row) != (uint32_t)(ROW_MARKS + 1 + (has_mark ? 1 : 0) + (has_val ? 1 : 0))) {
        return false;
    }
    if (!w) {
        w = g_content_w - 2 * lv_obj_get_x(row);
    }
    r.value = fit_value(r.label, r.value, w, vbuf, sizeof(vbuf));
    // a value that the row did not have is made, one that it no longer has is removed (a folder row has no size, a file row has one)
    if (has_val && !r.value) {
        lv_obj_delete(lv_obj_get_child(row, (int32_t)lv_obj_get_child_count(row) - 1));
        ud &= ~(uintptr_t)(ROW_VAL | ROW_STRONG);
        lv_obj_set_user_data(row, (void *)ud);
        has_val = false;
    } else if (!has_val && r.value) {
        int nvw = trx_text_w(r.value, t.caption);
        trx_txt(row, w - nvw - t.space_md, value_y(t), r.value, t.caption, r.strong ? t.accent : t.muted);
        ud |= ROW_VAL | (r.strong ? ROW_STRONG : 0u);
        lv_obj_set_user_data(row, (void *)ud);
        has_val = true;
    }
    if (((ud & ROW_SEL) != 0) != r.selected) {
        trx_row_select(row, r.selected);                        // the whole row repaints: only when the selection moved
        ud = (uintptr_t)lv_obj_get_user_data(row);
    }
    int vw = r.value ? trx_text_w(r.value, t.caption) : 0;
    int lw = w - 2 * t.space_md - (vw ? vw + t.space_md : 0);
    lv_obj_t *lab = lv_obj_get_child(row, ROW_MARKS);
    if (strcmp(lv_label_get_text(lab), r.label ? r.label : "")) {
        lv_label_set_text(lab, r.label ? r.label : "");
    }
    set_label_w(lab, r.label, t.body, lw - (has_mark ? RESET_MARK_D + t.space_sm : 0));
    if (has_mark) {
        lv_obj_t *mk = lv_obj_get_child(row, ROW_MARKS + 1);
        int lwid = trx_text_w(r.label, t.body) + 4;
        if (lwid > lw - RESET_MARK_D - t.space_sm) {
            lwid = lw - RESET_MARK_D - t.space_sm;
        }
        int mx = t.space_md + lwid + t.space_sm;
        if (lv_obj_get_x(mk) != mx) {
            lv_obj_set_x(mk, mx);
        }
    }
    if (((ud & ROW_DIM) != 0) != r.dim) {
        lv_obj_set_style_text_color(lab, lv_color_hex(r.dim ? t.muted : t.ink), 0);
        ud = r.dim ? (ud | ROW_DIM) : (ud & ~(uintptr_t)ROW_DIM);
    }
    if (has_val) {
        lv_obj_t *val = lv_obj_get_child(row, (int32_t)lv_obj_get_child_count(row) - 1);
        if (strcmp(lv_label_get_text(val), r.value)) {
            lv_label_set_text(val, r.value);
        }
        if (lv_obj_get_x(val) != w - vw - t.space_md) {
            lv_obj_set_x(val, w - vw - t.space_md);
        }
        if (((ud & ROW_STRONG) != 0) != r.strong) {
            lv_obj_set_style_text_color(val, lv_color_hex(r.strong ? t.accent : t.muted), 0);
            ud = r.strong ? (ud | ROW_STRONG) : (ud & ~(uintptr_t)ROW_STRONG);
        }
    }
    lv_obj_set_user_data(row, (void *)ud);
    return true;
}

// ---- panels (panel.xml, action_row.xml, ui_button.xml, status_chip.xml) ------------------------------
int trx_chip(lv_obj_t *par, int x, int y, const char *label, uint32_t tone)
{
    const TrxTheme &t = trx_theme();
    label = shout(label);
    int w = trx_text_w(label, t.caption) + 2 * t.space_sm;
    lv_obj_t *c = trx_box(par, x, y, w, 18, t.surface2);
    lv_obj_set_style_radius(c, t.radius, 0);
    trx_txt(c, t.space_sm, 2, label, t.caption, tone ? tone : t.ok);
    return w;
}

int trx_panel_top(void) { return 24; }

lv_obj_t *trx_panel(lv_obj_t *par, int x, int y, int w, int h, const char *kicker,
                    const char *chip, uint32_t chip_tone)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *p = trx_box(par, x, y, w, h, t.surface, 1, t.line);
    lv_obj_set_style_radius(p, t.radius, 0);
    int ccw = chip ? trx_text_w(shout(chip), t.caption) + 2 * t.space_sm : 0;
    if (kicker) {
        int room = w - 2 * t.space_md - (ccw ? ccw + t.space_sm : 0);       // a kicker that would run into the chip ends in dots (a narrow canvas)
        const char *k = shout(kicker);
        bool cut = trx_text_w(k, t.caption) > room;
        lv_obj_t *kl = trx_txt(p, t.space_md, t.space_sm, k, t.caption, t.muted, cut ? room : 0);
        if (cut) {
            lv_obj_set_height(kl, lv_font_get_line_height(t.caption));      // one line: a label of automatic height wraps into the row below instead of ending in dots
        }
    }
    if (chip) {
        trx_chip(p, w - ccw - t.space_md, t.space_xs, chip, chip_tone);
    }
    return p;
}

lv_obj_t *trx_action_row_at(lv_obj_t *par, int x, int y, int w, const char *title, const char *sub,
                            const char *meta, uint32_t tone, bool selected, int *next_y)
{
    const TrxTheme &t = trx_theme();
    // action_row.xml: transparent on its panel, separated by a line under it; the selection is the row's marks (trx_row_select)
    int h = sub && *sub ? 36 : 20;                  // T187 F7: the value line had no room above the separator, the selection box clipped it
    lv_obj_t *row = trx_box(par, x, y, w, h, t.surface, 1, t.line, LV_BORDER_SIDE_BOTTOM);
    row_marks(row, w, h, selected);
    int mw = meta && *meta ? trx_text_w(shout(meta), t.caption) : 0;
    const int room = w - 2 * t.space_sm - (mw ? mw + t.space_sm : 0);
    char tb[160], sb[160];                           // T209: each is one line, cut in the middle to the room (a label of automatic height wraps into the line below)
    lv_label_set_long_mode(trx_txt(row, t.space_sm, 1, trx_fit_mid(title, room, t.body, tb, sizeof(tb)), t.body, t.ink, room), LV_LABEL_LONG_MODE_CLIP);
    if (sub && *sub) {
        lv_label_set_long_mode(trx_txt(row, t.space_sm, 17, trx_fit_mid(sub, room, t.caption, sb, sizeof(sb)), t.caption, t.muted, room), LV_LABEL_LONG_MODE_CLIP);
    }
    if (mw) {
        trx_txt(row, w - mw - t.space_sm, 3, shout(meta), t.caption, tone ? tone : t.muted);
    }
    lv_obj_set_user_data(row, (void *)(uintptr_t)(selected ? ROW_SEL | ROW_STRONG : 0u));
    if (next_y) {
        *next_y = y + h + 1;
    }
    return row;
}

int trx_action_row(lv_obj_t *par, int x, int y, int w, const char *title, const char *sub,
                   const char *meta, uint32_t tone, bool selected)
{
    int ny = y;
    trx_action_row_at(par, x, y, w, title, sub, meta, tone, selected, &ny);
    return ny;
}

int trx_button(lv_obj_t *par, int x, int y, int w, const char *label, bool strong)
{
    const TrxTheme &t = trx_theme();
    label = shout(label);
    int lw = trx_text_w(label, t.caption);
    if (!w) {
        w = lw + 2 * t.space_lg;
    }
    lv_obj_t *b = trx_box(par, x, y, w, 22, strong ? t.accent : t.surface2, 1,
                          strong ? t.accent : t.line);
    lv_obj_set_style_radius(b, t.radius, 0);
    trx_txt(b, (w - lw) / 2, 4, label, t.caption, strong ? t.bg : t.ink);
    return w;
}

// ---- the concept's widgets (see trxview.h) -----------------------------------------------------------------------------------------
uint32_t trx_tone_color(TrxTone tone)
{
    const TrxTheme &t = trx_theme();
    switch (tone) {
    case TT_CYAN: return t.ok;
    case TT_VIOLET: return t.accent;
    case TT_AMBER: return t.warn;
    case TT_CORAL: return t.bad;
    default: return t.muted;
    }
}

TrxTone trx_tone_of_word(const char *word)
{
    static const char *const quiet[] = { "EMPTY", "NONE", "OFF", "OFFLINE", "N/A", "-", "NO IMAGE", "NO LINK", 0 };
    if (!word || !*word) {
        return TT_MUTED;
    }
    for (int i = 0; quiet[i]; i++) {
        const char *a = word, *b = quiet[i];
        while (*a && *b && ((*a | 32) == (*b | 32))) {
            a++;
            b++;
        }
        if (!*a && !*b) {
            return TT_MUTED;
        }
    }
    return TT_CYAN;
}

lv_obj_t *trx_card(lv_obj_t *par, int x, int y, int w, int h, bool raised)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *c = trx_box(par, x, y, w, h, raised ? t.surface2 : t.surface);
    return c;
}

int trx_state_chip_w(const char *label)
{
    const TrxTheme &t = trx_theme();
    return 4 + t.space_xs + trx_text_w(shout(label), t.caption);
}

int trx_state_chip(lv_obj_t *par, int x, int y, const char *label, TrxTone tone)
{
    const TrxTheme &t = trx_theme();
    label = shout(label);
    trx_box(par, x, y + 6, 4, 4, trx_tone_color(tone));
    trx_txt(par, x + 4 + t.space_xs, y, label, t.caption, t.muted);
    return 4 + t.space_xs + trx_text_w(label, t.caption);
}

int trx_head(lv_obj_t *par, int x, int y, int w, const char *crumb, const char *title, const char *chip, TrxTone chip_tone)
{
    const TrxTheme &t = trx_theme();
    int ty = y;
    if (crumb) {
        trx_txt(par, x, y, shout(crumb), t.caption, t.muted);
        ty += 18;
    }
    int tw = chip ? w - trx_state_chip_w(chip) - 2 * t.space_lg : w;
    char tb[160];
    lv_obj_t *tl = trx_txt(par, x, ty, trx_fit_mid(title, tw - 4, t.title, tb, sizeof(tb)), t.title, t.ink, tw);   // T209: one line, a long title is cut in the middle
    lv_label_set_long_mode(tl, LV_LABEL_LONG_MODE_CLIP);
    if (chip) {
        trx_state_chip(par, x + w - trx_state_chip_w(chip), crumb ? y + 18 : y + 8, chip, chip_tone);
    }
    return ty + 34;
}

int trx_status_row_h(void) { return 22; }
int trx_setting_row_h(void) { return 28; }

static uint32_t dimmer(const TrxTheme &t)
{
    lv_color_t c = lv_color_mix(lv_color_hex(t.muted), lv_color_hex(t.bg), 150);
    return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue;
}

lv_obj_t *trx_status_row(lv_obj_t *par, int x, int y, int w, const TrxStatusRow &r, int *next_y)
{
    const TrxTheme &t = trx_theme();
    int h = trx_status_row_h();
    lv_obj_t *row = trx_box(par, x, y, w, h, t.surface, 1, t.line, LV_BORDER_SIDE_BOTTOM);
    row_marks(row, w, h - 1, r.selected);
    const char *meta = r.meta && *r.meta ? r.meta : 0;                   // values read in sentence case; a chip the author wrote in capitals stays so
    int tx = t.space_sm + 4 + t.space_md;
    char mbuf[160];
    if (meta) {                                                          // T209 R5: a long value is cut in the middle and never takes more than the title's share below 45% of the row
        int lab = trx_text_w(r.title ? r.title : "", t.body) + 4;
        int keep = (w - tx) * 45 / 100;
        meta = trx_fit_mid(meta, w - tx - t.space_sm - t.space_md - (lab < keep ? lab : keep), t.caption, mbuf, sizeof(mbuf));
    }
    int mw = meta ? trx_text_w(meta, t.caption) : 0;
    int room = w - tx - t.space_sm - (mw ? mw + t.space_md : 0);
    trx_box(row, t.space_sm + 2, 9, 4, 4, r.dim ? t.line : trx_tone_color(r.tone));
    char tbuf[160];
    const char *ttl = r.title ? trx_fit_mid(r.title, room, t.body, tbuf, sizeof(tbuf)) : r.title;
    lv_label_set_long_mode(trx_txt(row, tx, 2, ttl, t.body, r.dim ? (r.selected ? t.muted : dimmer(t)) : t.ink, room), LV_LABEL_LONG_MODE_CLIP);
    if (meta) {
        trx_txt(row, w - mw - t.space_sm, 2 + (lv_font_get_line_height(t.body) - lv_font_get_line_height(t.caption)) / 2, meta, t.caption, r.dim ? t.muted : trx_tone_color(r.tone));
    }
    lv_obj_set_user_data(row, (void *)(uintptr_t)(r.selected ? ROW_SEL | ROW_STRONG : 0u));
    if (next_y) {
        *next_y = y + h + 1;
    }
    return row;
}

lv_obj_t *trx_setting_row(lv_obj_t *par, int x, int y, int w, const TrxSettingRow &r, int *next_y)
{
    const TrxTheme &t = trx_theme();
    int h = trx_setting_row_h();
    lv_obj_t *row = trx_box(par, x, y, w, h, t.surface, 1, t.line, LV_BORDER_SIDE_BOTTOM);
    row_marks(row, w, h - 1, r.selected);
    char vbuf[160];
    const char *val = r.toggle ? 0 : r.value;
    if (val) {                                                           // T209 R5: a long value is cut in the middle, the label keeps up to 45% of the row
        int lab = trx_text_w(r.title ? r.title : "", t.body) + 4;
        int keep = (w - 2 * t.space_md) * 45 / 100;
        val = trx_fit_mid(val, w - 2 * t.space_md - t.space_lg - (lab < keep ? lab : keep), t.body, vbuf, sizeof(vbuf));
    }
    int cw = r.toggle ? 34 : (val ? trx_text_w(val, t.body) : 0);
    int room = w - 2 * t.space_md - (cw ? cw + t.space_lg : 0);
    const int ty = (h - 1 - lv_font_get_line_height(t.body)) / 2;
    trx_txt(row, t.space_md, ty, r.title, t.body, r.dim ? t.muted : t.ink, room - (r.reset ? RESET_MARK_D + t.space_sm : 0));
    if (r.reset) {                                                       // T187 D9: the ring beside the label
        int tw = trx_text_w(r.title, t.body) + 2;
        int lwid = tw < room - RESET_MARK_D - t.space_sm ? tw : room - RESET_MARK_D - t.space_sm;
        trx_reset_mark(row, t.space_md + lwid + t.space_sm, ty + (lv_font_get_line_height(t.body) - RESET_MARK_D) / 2 + 1, t.muted, t.surface);
    }
    if (r.toggle) {
        uint32_t on_col = r.tone == TT_MUTED ? t.accent : trx_tone_color(r.tone);
        lv_obj_t *tr = trx_box(row, w - 34 - t.space_md, (h - 1 - 18) / 2, 34, 18, r.on ? on_col : t.line);
        trx_box(tr, r.on ? 34 - 16 : 2, 2, 14, 14, t.ink);
    } else if (val) {
        trx_txt(row, w - cw - t.space_md, ty, val, t.body, r.dim ? t.muted : t.ink);
    }
    lv_obj_set_user_data(row, (void *)(uintptr_t)(r.selected ? ROW_SEL | ROW_STRONG : 0u));
    if (next_y) {
        *next_y = y + h + 1;
    }
    return row;
}

int trx_kv_row(lv_obj_t *par, int x, int y, int w, const char *label, const char *value, TrxTone tone)
{
    const TrxTheme &t = trx_theme();
    trx_txt(par, x, y, label, t.body, t.muted);
    int vw = trx_text_w(value, t.body);
    trx_txt(par, x + w - vw, y, value, t.body, tone == TT_MUTED ? t.ink : trx_tone_color(tone));
    return y + 24;
}

int trx_btn_h(void) { return 28; }

int trx_btn_w(const char *label, char key)
{
    const TrxTheme &t = trx_theme();
    return trx_text_w(shout(label), t.caption) + 2 * t.space_md + (key ? 14 + t.space_xs : 0);
}

lv_obj_t *trx_btn(lv_obj_t *par, int x, int y, int w, const char *label, TrxBtnStyle style, bool selected, bool disabled, char key)
{
    const TrxTheme &t = trx_theme();
    label = shout(label);
    int lw = trx_text_w(label, t.caption);
    int kw = key ? 14 + t.space_xs : 0;                                   // the letter in the tab strip's keycap, before the label
    if (!w) {
        w = lw + 2 * t.space_md + kw;
    }
    int h = trx_btn_h();
    uint32_t bg = t.surface2, bord = t.line, ink = t.ink;
    bool primary = style == TB_PRIMARY && !disabled;
    if (primary) {
        bg = bord = t.accent;
        ink = t.bg;
    } else if (style == TB_WARN && !disabled) {
        bord = ink = t.warn;
    }
    if (disabled) {                                                      // flat and dim: no border, the text well below an active button's
        bg = t.surface;
        bord = t.surface;
        lv_color_t c = lv_color_mix(lv_color_hex(t.muted), lv_color_hex(t.bg), 130);
        ink = ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue;
    }
    lv_obj_t *b = trx_box(par, x, y, w, h, bg, 1, bord);
    row_marks(b, w, h, selected, primary ? t.ink : 0);
    int lx = (w - lw - kw) / 2 + kw;
    if (key) {
        lv_color_t kc = lv_color_mix(lv_color_hex(t.line), lv_color_hex(t.muted), 150);               // a keycap that stands out from the button, as on the tabs
        uint32_t kcap = ((uint32_t)kc.red << 16) | ((uint32_t)kc.green << 8) | kc.blue;
        lv_color_t dc = lv_color_mix(lv_color_hex(t.line), lv_color_hex(t.bg), 130);
        (void)dc;
        uint32_t capbg = disabled ? t.line : (primary ? t.bg : kcap);
        lv_obj_t *cap = trx_box(b, lx - kw, (h - 14) / 2, 12, 14, capbg, 0, 0);
        char ks[2] = { (char)toupper((unsigned char)key), 0 };
        trx_txt(cap, 3, -1, ks, t.caption, primary ? t.accent : (disabled ? t.muted : t.ink));
    }
    trx_txt(b, lx, key ? (h - 14) / 2 : (h - 16) / 2, label, t.caption, ink);
    lv_obj_set_user_data(b, (void *)(uintptr_t)(selected ? ROW_SEL | ROW_STRONG : 0u));
    return b;
}

int trx_source_chips(lv_obj_t *par, int x, int y, const char *const *names, int n, int active, const uint8_t *flags)
{
    const TrxTheme &t = trx_theme();
    int cx = x;
    for (int i = 0; i < n; i++) {
        const char *l = shout(names[i]);
        bool absent = flags && (flags[i] & 1), focus = flags && (flags[i] & 2);
        int w = trx_text_w(l, t.caption) + 2 * t.space_md;
        lv_obj_t *b = trx_box(par, cx, y, w, 24, i == active ? t.accent_soft : t.surface2, focus ? 2 : 1, focus || i == active ? t.accent : t.line);
        trx_txt(b, t.space_md, 3, l, t.caption, absent ? t.line : (i == active ? t.ink : t.muted));
        cx += w + t.space_sm;
    }
    return cx - x;
}

lv_obj_t *trx_link(lv_obj_t *par, int x, int y, int w, const char *label, TrxTone tone, bool selected)
{
    const TrxTheme &t = trx_theme();
    label = shout(label);
    int lw = trx_text_w(label, t.caption);
    if (!w) {
        w = lw + 2 * t.space_sm;
    }
    lv_obj_t *l = trx_box(par, x, y, w, 20, t.surface2);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    row_marks(l, w, 20, selected);
    trx_txt(l, t.space_sm, 2, label, t.caption, trx_tone_color(tone));
    lv_obj_set_user_data(l, (void *)(uintptr_t)(selected ? ROW_SEL | ROW_STRONG : 0u));
    return l;
}

lv_obj_t *trx_live_box(lv_obj_t *par, int x, int y, int w, int h)
{
    const TrxTheme &t = trx_theme();
    lv_obj_t *b = trx_box(par, x, y, w, h, TRX_LIVE_KEY_RGB);
    return b;
}

TrxWindow trx_window(int total, int sel, int first, int rows)
{
    TrxWindow w;
    if (rows < 1) {
        rows = 1;
    }
    int mg = (rows - 1) / 2 < TRX_SCROLL_MARGIN ? (rows - 1) / 2 : TRX_SCROLL_MARGIN;   // T209 D3: rows kept below and above the selection; a window of 1-2 rows has none
    if (first > sel - mg) {
        first = sel - mg;
    }
    if (sel > first + rows - 1 - mg) {
        first = sel - rows + 1 + mg;
    }
    if (first > total - rows) {
        first = total - rows;
    }
    if (first < 0) {
        first = 0;
    }
    w.first = first;
    w.count = total - first < rows ? total - first : rows;
    if (w.count < 0) {
        w.count = 0;
    }
    w.total = total;
    return w;
}

TrxWindow trx_window_h(int total, int sel, int first, int room, int (*h)(int idx, void *ctx), void *ctx)
{
    TrxWindow w;
    w.total = total;
    w.first = 0;
    w.count = 0;
    if (total < 1) {
        return w;
    }
    if (sel < 0) {
        sel = 0;
    }
    if (sel > total - 1) {
        sel = total - 1;
    }
    if (first > sel - TRX_SCROLL_MARGIN) {               // T209 D3: the window scrolls up while TRX_SCROLL_MARGIN rows still lie above the selection
        first = sel - TRX_SCROLL_MARGIN;
    }
    if (first < 0) {
        first = 0;
    }
    int below = sel + TRX_SCROLL_MARGIN < total - 1 ? sel + TRX_SCROLL_MARGIN : total - 1;   // ... and down while that many lie below it, as far as the room holds them
    int sum = 0;
    for (int i = first; i <= below; i++) {
        sum += h(i, ctx);
    }
    while (sum > room && first < sel) {                  // the selected row has to lie whole inside: the window starts later (the margin below gives way first)
        sum -= h(first, ctx);
        first++;
    }
    int count = 0, used = 0;
    while (first + count < total && (count == 0 || used + h(first + count, ctx) <= room)) {
        used += h(first + count, ctx);
        count++;
    }
    if (first + count == total) {                        // the list's end: the room is filled from the back
        while (first > 0 && used + h(first - 1, ctx) <= room) {
            first--;
            used += h(first, ctx);
            count++;
        }
    }
    w.first = first;
    w.count = count;
    return w;
}

// ---- the overlay root (T167) ---------------------------------------------------------------------------------------------
// LVGL draws a changed rectangle from the topmost opaque object of the ACTIVE SCREEN that covers it (lv_refr_get_top_obj) and the
// top layer afterwards, unconditionally. A panel on lv_layer_top() is never that object: the screen under it is drawn into the
// rectangle first, and with one frame buffer shown directly (both applications) the person sees it for a moment. This container is the
// last child of the active screen, transparent; a panel inside it is found by the search, and nothing under the panel is drawn.
static lv_obj_t *g_ov;
static uint32_t g_ov_gen;

static void ov_gone(lv_event_t *)
{
    g_ov = 0;
}

lv_obj_t *trx_overlay(void)
{
#ifdef TRX_OVERLAY_ON_TOP_LAYER      // the bench's negative control: the old parent
    return lv_layer_top();
#else
    lv_obj_t *scr = lv_screen_active();
    if (g_ov && lv_obj_get_parent(g_ov) != scr) {
        g_ov = 0;                    // another screen is active: the container of the old one stays with it
    }
    if (!g_ov) {
        g_ov = lv_obj_create(scr);
        lv_obj_remove_style_all(g_ov);
        lv_obj_set_pos(g_ov, 0, 0);
        lv_obj_set_size(g_ov, SW, SH);
        lv_obj_set_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flag(g_ov, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_add_event_cb(g_ov, ov_gone, LV_EVENT_DELETE, 0);
        g_ov_gen++;
    } else if (lv_obj_get_child(scr, -1) != g_ov) {
        lv_obj_move_foreground(g_ov);
    }
    return g_ov;
#endif
}

uint32_t trx_overlay_gen(void)
{
    return g_ov_gen;
}

// ---- the page change of an overlay panel (T167 5.9) -------------------------------------------------------------------------
TrxPage::TrxPage(lv_obj_t *root, lv_obj_t *keep) : root_(root)
{
    trx_overlay();                                 // the container exists and is the last child of the screen
#ifndef TRX_PAGE_NO_INVALIDATE                     // the bench's negative control: the changes invalidate their own, smaller areas
    if (root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        if (keep) {
            lv_obj_update_layout(root_);
            lv_area_t r, k, a;
            lv_obj_get_coords(root_, &r);
            lv_obj_get_coords(keep, &k);
            a = r;
            a.y2 = k.y1 - 1;                       // the strip above the kept object ...
            if (a.y2 >= a.y1) {
                lv_obj_invalidate_area(root_, &a);
            }
            a = r;
            a.y1 = k.y2 + 1;                       // ... and the strip below it
            if (a.y2 >= a.y1) {
                lv_obj_invalidate_area(root_, &a);
            }
        } else {
            lv_obj_invalidate(root_);              // first: every later, smaller area lies inside this one and is dropped
        }
    }
#endif
}

TrxPage::~TrxPage()
{
    if (root_ && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, false);
    }
}

void trx_page_hide(lv_obj_t *root)
{
    if (root && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flag(root, LV_OBJ_FLAG_HIDDEN, true);
    }
}

// ---- the progress bar (T167) ---------------------------------------------------------------------------------------------
// Two plain boxes: a track and, inside it, the fill. lv_bar redraws its whole track (and the indicator over it) whenever the value changes, so
// the filled part flashes the track colour for a moment with one frame buffer shown directly. The fill alone is an opaque object that covers
// what it has to redraw when it grows: LVGL draws it and nothing else.
lv_obj_t *trx_pbar(lv_obj_t *par, int x, int y, int w, int h, uint32_t track, uint32_t fill)
{
    lv_obj_t *t = trx_box(par, x, y, w, h, track);
    lv_obj_t *f = trx_box(t, 0, 0, 1, h, fill);
    lv_obj_set_flag(f, LV_OBJ_FLAG_HIDDEN, true);
    return t;
}

void trx_pbar_set(lv_obj_t *bar, int value, int range)
{
    lv_obj_t *f = lv_obj_get_child(bar, 0);
    if (!f) {
        return;
    }
    int w = lv_obj_get_width(bar);
    int fw = range > 0 && value > 0 ? (int)((int64_t)w * (value > range ? range : value) / range) : 0;
    if (fw <= 0) {
        if (!lv_obj_has_flag(f, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_flag(f, LV_OBJ_FLAG_HIDDEN, true);
        }
        return;
    }
    if (lv_obj_get_width(f) != fw) {
        lv_obj_set_width(f, fw);
    }
    if (lv_obj_has_flag(f, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flag(f, LV_OBJ_FLAG_HIDDEN, false);
    }
}

// ---- the start-up splash -------------------------------------------------------------------------------
static lv_obj_t *sp_root, *sp_bar, *sp_label;
static int sp_n;
static char sp_product[48], sp_what[48], sp_version[96], sp_text[96];   // what the splash shows, to draw it again for another canvas
static int sp_step;

void trx_splash_open(lv_obj_t *scr, const char *product, const char *what, const char *version, int nsteps)
{
    trx_splash_close();
    const TrxTheme &t = trx_theme();
    sp_n = nsteps > 0 ? nsteps : 1;
    snprintf(sp_product, sizeof(sp_product), "%s", product ? product : "");
    snprintf(sp_what, sizeof(sp_what), "%s", what ? what : "");
    snprintf(sp_version, sizeof(sp_version), "%s", version ? version : "");
    sp_text[0] = 0;
    sp_step = 0;
    sp_root = trx_box(scr, 0, 0, SW, SH, t.bg);
    int oy = (SH - TRX_CANVAS_FULL_H) / 2;           // the splash block stays centred when the canvas is shorter or taller than 540
    int bw = SW < 480 ? SW - 40 : 400, bx = (SW - bw) / 2;
    int w = trx_text_w(product, t.title);
    trx_txt(sp_root, (SW - w) / 2, 170 + oy, product, t.title, t.ink);
    w = trx_text_w(what, t.heading);
    trx_txt(sp_root, (SW - w) / 2, 214 + oy, what, t.heading, t.ok);
    w = trx_text_w(version, t.caption);
    trx_txt(sp_root, (SW - w) / 2, 252 + oy, version, t.caption, t.muted);
    sp_bar = trx_pbar(sp_root, bx, 310 + oy, bw, 6, t.surface2, t.accent);
    trx_pbar_set(sp_bar, 0, sp_n);
    sp_label = trx_txt(sp_root, bx, 326 + oy, "", t.body, t.muted, bw);
}

// The canvas changed while the splash is up: it is drawn again for the new size, at the step it was.
void trx_splash_relayout(lv_obj_t *scr)
{
    if (!sp_root) {
        return;
    }
    char pr[sizeof(sp_product)], wh[sizeof(sp_what)], ve[sizeof(sp_version)], tx[sizeof(sp_text)];
    int n = sp_n, st = sp_step;
    snprintf(pr, sizeof(pr), "%s", sp_product);
    snprintf(wh, sizeof(wh), "%s", sp_what);
    snprintf(ve, sizeof(ve), "%s", sp_version);
    snprintf(tx, sizeof(tx), "%s", sp_text);
    trx_splash_open(scr, pr, wh, ve, n);
    trx_splash_step(st, tx);
}

void trx_splash_step(int step, const char *label)
{
    if (!sp_root) {
        return;
    }
    trx_pbar_set(sp_bar, step < 0 ? 0 : (step > sp_n ? sp_n : step), sp_n);
    lv_label_set_text(sp_label, label ? label : "");
    sp_step = step;
    snprintf(sp_text, sizeof(sp_text), "%s", label ? label : "");
}

bool trx_splash_up(void) { return sp_root != 0; }

void trx_splash_close(void)
{
    if (sp_root) {
        lv_obj_delete(sp_root);
    }
    sp_root = sp_bar = sp_label = 0;
}
