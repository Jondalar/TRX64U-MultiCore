// The shared view layer of both applications (T104 D8: one of the two named rebuilds).
//
// It is the LVGL prototype in TRX64-Ultimate/docs/lvgl_ui_project and lvgl_ui_terminal, built: the same
// 960 x 540 canvas our pixel layer has (trx_lv_port.h:21-22), the same geometry (sidebar 148, top bar 36,
// tab strip 38, footer 28), the same palette, and the same two presentations - one navigation engine with
// a style table, not two user interfaces (T104 5.8).
//
// Nothing here knows what a core is. The shell is given a route (areas and tabs) and a content callback;
// where the route comes from is the caller's business: the host application has one area, a core
// application takes its own out of the bundle's descriptor.
#ifndef TRX_VIEW_H
#define TRX_VIEW_H

#include <stdint.h>
#include "lvgl.h"

// ---- skins (T104 5.8: the two prototypes differ in palette and typography, nothing else) --------------
enum TrxSkin { TRX_SKIN_PANEL = 0, TRX_SKIN_TERMINAL = 1 };

struct TrxTheme {
    uint32_t bg, surface, surface2, line, ink, muted;
    uint32_t accent, ok, warn, bad, accent_soft;
    const lv_font_t *caption, *body, *heading, *display, *title;
    uint8_t sidebar_w, topbar_h, tabbar_h, footer_h;
    uint8_t space_xs, space_sm, space_md, space_lg, space_xl;
    uint8_t radius;
    bool uppercase;                  // the terminal skin shouts its labels, the panel skin does not
};

// T164 D10a: the canvas the view is laid out on. 960 x 540 (trx_compact() false), or the mode's own size when the mode is narrower than 960
// (trx_canvas_for in trxcanvas.h): then the shell has a narrower rail and thinner bars and every caller lays out for the width it is given
// (trx_canvas_w / _h). The caller builds the frame again after a change (trx_shell_forget()).
void trx_canvas_set(int w, int h);
int trx_canvas_w(void);
int trx_canvas_h(void);
bool trx_compact(void);

void trx_skin_set(TrxSkin s);
// One of the seven colour schemes of the panel skin (0 Standard Blue, 1 Ultimate Black = the concept, 2 C128 Style, 3 Commodore Blue, 4-6 Commodore 1-3); the next frame uses it.
void trx_scheme_set(int n);
TrxSkin trx_skin(void);
const TrxTheme &trx_theme(void);

// ---- primitives (the two hostui.cc already had, moved here unchanged in behaviour) --------------------
lv_obj_t *trx_box(lv_obj_t *par, int x, int y, int w, int h, uint32_t bg, int border = 0,
                  uint32_t bcol = 0, uint8_t border_side = LV_BORDER_SIDE_FULL);
lv_obj_t *trx_txt(lv_obj_t *par, int x, int y, const char *s, const lv_font_t *f, uint32_t col, int w = 0);
int trx_text_w(const char *s, const lv_font_t *f);

// ---- the route (T104 5.8: a function key picks an area, a number key picks a visible tab) -------------
#define TRX_TABS_MAX  8
#define TRX_AREAS_MAX 8

struct TrxTab {
    uint8_t n;                       // the printed number, 1..5; a route is learned as F<key>, <n>
    const char *title;
    uint8_t view;                    // what the caller draws there; the shell never interprets it
    const char *arg;                 // optional: the settings group, the host page name
};

struct TrxArea {
    const char *key;                 // "F1".."F8", printed as the logical key, never as Shift+F1
    const char *title;
    const TrxTab *tabs;
    uint8_t ntabs;
};

struct TrxRoute {
    const TrxArea *areas;
    uint8_t nareas;
    bool app_mode;                   // an application's own screens (T104.3 D9.1): no rail of areas, the content takes the whole width
};

// ---- the status line of the top bar -------------------------------------------------------------------
#define TRX_CHIPS_MAX 4
struct TrxChip { const char *label; uint32_t tone; };

struct TrxShellInfo {
    const char *product;             // "TRX64"
    const char *section;             // "/ UI", "/ BOOT"
    const char *activity;            // "IDLE", "RUNNING"
    TrxChip chips[TRX_CHIPS_MAX];
    uint8_t nchips;
    const char *footer_left;         // "R/S  CLOSE"
    const char *footer_right;        // "F1-F8 AREA  1-5 TAB  R/S CLOSE"
};

// Draws the frame on `scr` and returns the content pane, whose origin is its own (0,0). The caller fills
// the pane and calls this again for the next frame.
//
// The frame is only rebuilt when something in it changed - the area, the tab, a chip, the activity word.
// Moving the selection inside a tab is the common case and then only the content pane is cleared, which
// is the difference between redrawing 960 x 540 and redrawing the list. The core application's menu used
// to feel slow for exactly this kind of reason (T93 5.14).
lv_obj_t *trx_shell(lv_obj_t *scr, const TrxShellInfo &info, const TrxRoute &route, int area, int tab);

// How often the frame was rebuilt rather than kept. For the benches; it only ever grows.
uint32_t trx_shell_frames(void);
// How often only a part of the frame was redrawn (the top bar, two rail items, the strip's tabs): an area or tab key, a media chip.
uint32_t trx_shell_partials(void);

// The caller cleared the screen behind the shell's back: the next call rebuilds the frame.
void trx_shell_forget(void);

int trx_content_w(void);
int trx_content_h(void);
bool trx_strip_probe(int *right, int *room, int *ntabs);                         // bench: where the last tab of the strip ends, the strip's width, the tab count (false: no strip)
bool trx_nav_probe(int i, int *label_end, int *label_room, int *key_x);   // bench: the rail item i's label text end, the label's room, the key's x
bool trx_foot_probe(char *txt, int n, int *text_w);                      // bench: the legend as drawn and its text's width

// ---- rows: the flat list both prototypes use everywhere ----------------------------------------------
struct TrxRow {
    const char *label;
    const char *value;               // right-aligned, accent when `strong`
    bool selected;
    bool strong;
    bool dim;
    bool mark;                       // T187 D9: a small ring beside the label (a setting that takes effect at the next reset)
    bool heading;                    // T187 D1: a quiet divider that names the group below it: small muted capitals, never selected
};

// y of the next row. `w` 0 = the full content width. The object is returned by `trx_row_at` so a
// caller can keep it and move the selection without building the row again.
int trx_row(lv_obj_t *par, int x, int y, int w, const TrxRow &r);
lv_obj_t *trx_row_at(lv_obj_t *par, int x, int y, int w, const TrxRow &r, int *next_y);
int trx_row_h(void);                     // T209 D1: every row is one line; the height without the 1 px gap
// The ring of T187 D9 (a value that takes effect at the next reset): one object at x, y of `par`, in the colour `col`; `bg` is what is behind it.
lv_obj_t *trx_reset_mark(lv_obj_t *par, int x, int y, uint32_t col, uint32_t bg);
int trx_reset_mark_w(void);
// T209: `s` cut in the middle ("start...end.ext") until it is at most `w` pixels wide in `font`; `buf` holds the result (s itself when it fits).
const char *trx_fit_mid(const char *s, int w, const lv_font_t *font, char *buf, int n);
// T228 R21, "Filename overflow squeeze" as TRX64-Ultimate (browsable_root.h:186-233): `quarter` 0 None (the name cut at the end, no mark), 1 Beginning, 2 Middle, 3 End: the
// kept characters are split quarter/4 before and the rest after a '~' that takes the first character of the tail. The most characters that fit `w` pixels are kept.
const char *trx_fit_name(const char *s, int w, const lv_font_t *font, char *buf, int n, int quarter);

// Select or deselect a row that already exists: its four selection strips and the value's colour,
// nothing rebuilt. A key that only moves the selection must not redraw the pane (T104, board 2026-10-16:
// the core application took 0.5-1 s per key because every key rebuilt every row).
void trx_row_select(lv_obj_t *row, bool on);

// New text, colours and selection for a row that exists, for a list whose window scrolled or whose value changed. False when the
// row's structure differs (sub line, value): then it has to be built again.
bool trx_row_retext(lv_obj_t *row, const TrxRow &r, int w);
bool trx_row_value_cy(lv_obj_t *row, int *lab_cy, int *val_cy);   // the bench's probe: false when the row has no value
bool trx_row_texts(lv_obj_t *row, char *lab, char *val, int n);    // the bench's T205 P3 probe: the texts the row's label and value objects hold

// Would trx_shell() keep the frame it has, for this screen and this route? Then the caller may update
// the content pane in place instead of letting trx_shell clean it.
bool trx_shell_same(lv_obj_t *scr, const TrxShellInfo &info, const TrxRoute &route, int area, int tab);

// A heading above a group of rows, as "MEDIA" / "MACHINE" in the prototype's C64 screen.
int trx_kicker(lv_obj_t *par, int x, int y, const char *s);

// ---- panels and their parts (the prototype's panel.xml, action_row.xml, status_chip.xml) -------------
// A panel with its kicker line and an optional chip at the right of it. Returns the panel; its inside
// starts below the kicker at `trx_panel_top()`.
lv_obj_t *trx_panel(lv_obj_t *par, int x, int y, int w, int h, const char *kicker,
                    const char *chip, uint32_t chip_tone);
int trx_panel_top(void);

// title + subtitle on the left, a coloured meta word on the right: the STATUS rows of the Home screen.
int trx_action_row(lv_obj_t *par, int x, int y, int w, const char *title, const char *sub,
                   const char *meta, uint32_t tone, bool selected);
// The same, returning the row so its selection can move (trx_row_select) without building the panel again.
lv_obj_t *trx_action_row_at(lv_obj_t *par, int x, int y, int w, const char *title, const char *sub,
                            const char *meta, uint32_t tone, bool selected, int *next_y);

// The prototype's ui_button: a bordered box with a centred label. Returns its width.
int trx_button(lv_obj_t *par, int x, int y, int w, const char *label, bool strong);

// A chip: a word in a rounded box, the status marker of the top bar and the session panel.
int trx_chip(lv_obj_t *par, int x, int y, const char *label, uint32_t tone);

// ---- the concept's widgets (lvgl_ui_project/components: panel, action_row, status_chip, setting_row, ui_button, ui_switch) ----------
// Generic: no widget knows what a core is. Every focusable one (rows, buttons) is built the way trx_row_at builds a row - four selection
// strips as its first children - so trx_row_select() moves the focus without building anything.
enum TrxTone { TT_MUTED = 0, TT_CYAN, TT_VIOLET, TT_AMBER, TT_CORAL };
uint32_t trx_tone_color(TrxTone t);
// The tone a status word is shown in when nobody chose one: nothing there / off is muted, everything else is cyan.
TrxTone trx_tone_of_word(const char *word);

// panel.xml: a card on the bg. `raised` is the lighter one of the side panels. No border, radius 4.
lv_obj_t *trx_card(lv_obj_t *par, int x, int y, int w, int h, bool raised);

// status_chip.xml: a 4 px dot and a muted caption, no box. Returns the width.
int trx_state_chip(lv_obj_t *par, int x, int y, const char *label, TrxTone tone);
int trx_state_chip_w(const char *label);

// The page head: "SETTINGS / TRX64" over the title, a state chip at the right of both. `crumb` may be null. Returns the y below it.
int trx_head(lv_obj_t *par, int x, int y, int w, const char *crumb, const char *title, const char *chip, TrxTone chip_tone);

// action_row.xml, one line (T209 D1): a tone dot, the title, the meta word at the right in the tone (cut in the middle when it would squeeze the title). A line
// under it. `dim` is the greyed row of a function that cannot be used now (the foot line says why).
struct TrxStatusRow {
    const char *title, *meta;
    TrxTone tone;
    bool selected, dim;
};
lv_obj_t *trx_status_row(lv_obj_t *par, int x, int y, int w, const TrxStatusRow &r, int *next_y);
int trx_status_row_h(void);

// setting_row.xml with its control: a switch (`toggle`, on/off) or the value as text. Returns the row.
struct TrxSettingRow {
    const char *title, *value;
    bool toggle, on, selected, dim;
    bool reset;                      // T187 D9: the ring beside the label (the value takes effect at the next reset)
    TrxTone tone;                    // the switch's colour when on (violet; amber for a risky setting)
};
lv_obj_t *trx_setting_row(lv_obj_t *par, int x, int y, int w, const TrxSettingRow &r, int *next_y);
int trx_setting_row_h(void);                  // the height without the 1 px gap

// A label at the left, a value at the right (the MACHINE panel of the C64 screen). Returns the y of the next one.
int trx_kv_row(lv_obj_t *par, int x, int y, int w, const char *label, const char *value, TrxTone tone);

// ui_button.xml: secondary (outlined), primary (violet), warn (amber outline); `disabled` is the greyed one. Focusable like a row.
enum TrxBtnStyle { TB_SECONDARY = 0, TB_PRIMARY, TB_WARN };
int trx_btn_w(const char *label, char key = 0);
lv_obj_t *trx_btn(lv_obj_t *par, int x, int y, int w, const char *label, TrxBtnStyle style, bool selected, bool disabled, char key = 0);
int trx_btn_h(void);

// The source chips of the library: one outlined button per source, the open one in violet. Returns the width used.
// flags (optional, one byte a chip): bit 0 the medium is absent (drawn greyed), bit 1 the chip has the focus (the path row of the uniform browser).
int trx_source_chips(lv_obj_t *par, int x, int y, const char *const *names, int n, int active, const uint8_t *flags = 0);

// A text that is a key and what it does ("F2  OPEN TIMEWARP"): caption, in the tone. Focusable like a row, so a link can be a target.
lv_obj_t *trx_link(lv_obj_t *par, int x, int y, int w, const char *label, TrxTone tone, bool selected);

// The "LIVE C64" area: filled with the layer's colour key, so a bitstream that has the colour key shows the core's own picture through it.
#define TRX_LIVE_KEY_RGB 0x080808       // = 0x0841 in RGB565, what trx_lv_set_key() is given
#define TRX_LIVE_KEY_565 0x0841
lv_obj_t *trx_live_box(lv_obj_t *par, int x, int y, int w, int h);

// ---- the window: which rows fit, with the selection inside it ----------------------------------------
struct TrxWindow { int first, count, total; };
// T209 D3: the window scrolls before the selection reaches its edge: TRX_SCROLL_MARGIN rows stay below (above) the selection, except where the list ends.
#define TRX_SCROLL_MARGIN 2
TrxWindow trx_window(int total, int sel, int first, int rows);
// T187: the window of a list whose rows differ in height. h(i, ctx) is the pixels row i takes in the pane (its gap included); room is the pane's
// height for rows. The selected row is always whole inside the window, the window keeps `first` while it can, and at the end of the list it fills
// the room from the back. A row taller than the room is shown alone.
TrxWindow trx_window_h(int total, int sel, int first, int room, int (*h)(int idx, void *ctx), void *ctx);

// T167: a progress bar of two boxes (track, fill); trx_pbar_set() changes only the fill. Not lv_bar: see trxview.cc.
lv_obj_t *trx_pbar(lv_obj_t *par, int x, int y, int w, int h, uint32_t track, uint32_t fill);
void trx_pbar_set(lv_obj_t *bar, int value, int range);

// T167: the parent of every panel drawn over the screen (progress pages, questions, the splash). Not lv_layer_top(): see trxview.cc.
// trx_overlay_gen() moves whenever the container was made again (a frame rebuild with lv_obj_clean(scr) deletes it with its panels).
lv_obj_t *trx_overlay(void);
uint32_t trx_overlay_gen(void);

// T167 5.9: the page change of an overlay panel, in one place. A page is the panel (a child of trx_overlay()) with its labels, colours, bar and
// hidden flags. TrxPage first invalidates the panel's whole rectangle: LVGL drops every smaller area it is given afterwards, so all changes of the
// page land in ONE area that is drawn once from the panel down (as when the panel is shown), not label by label with the lines of the old and the new page
// side by side. The destructor shows the panel (and fronts the container). Scope it around the changes, and call the LVGL handler after it.
// trx_page_hide() is the only way to take a panel away. A progress tick that changes a number or the bar only must NOT open a TrxPage.
class TrxPage {
public:
    // keep: an object inside the panel that the page change does not touch (the start page's progress bar). The panel is then invalidated as the strips
    // above and below it (two areas that do not overlap, so LVGL does not join them): the bar's row is not drawn again from the panel colour up, which
    // would show the track colour on the filled part for the length of one draw (T164).
    explicit TrxPage(lv_obj_t *root, lv_obj_t *keep = 0);
    ~TrxPage();
private:
    lv_obj_t *root_;
};
void trx_page_hide(lv_obj_t *root);

// ---- the start-up splash (T101 S9): the first thing on the pixel layer, before the slow work ---------
// Drawn once by trx_splash_open() (name, what runs, version, an empty bar); trx_splash_step() then changes only the bar's
// value and the step label (a few hundred pixels, never the whole screen). trx_splash_close() removes it.
// The caller renders (lv_refr_now / its handler) after each call; nothing here waits.
void trx_splash_open(lv_obj_t *scr, const char *product, const char *what, const char *version, int nsteps);
// The canvas changed (trx_canvas_set) while the splash is up: it is built again for the new size, at the step it was (T164 D10a).
void trx_splash_relayout(lv_obj_t *scr);
void trx_splash_step(int step, const char *label);     // step 0..nsteps-1 is running; the bar shows the steps behind it
bool trx_splash_up(void);
void trx_splash_close(void);

#endif
