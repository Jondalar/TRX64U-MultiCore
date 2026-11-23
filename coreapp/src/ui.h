// The application's screen (T93 D8, D9): LVGL on the host's pixel layer, resident views built once at start (D14), keyboard
// driven. Board code (the logic it shows lives in menutree.cc, settings.cc, freeze.cc, which are tested natively).
#ifndef COREAPP_UI_H
#define COREAPP_UI_H

#include "ctx.h"
#include "menuctl.h"

// LVGL up and every view built; the layer stays off until the first show. False without the pixel layer (the application
// then runs on the console log only).
bool ui_open(const CoreDesc *desc);
// T164 D10a: the menu's canvas (960 x 540, or the mode's own size below 960 wide): the view, the port and the display take it, the next render lays the menu out for it.
void ui_set_canvas(int w, int h);

// The layer is LVGL's (menu or message up) / is given back to the core's picture.
void ui_show(void);
void ui_hide(void);
void ui_paused_tag(bool on);          // the paused machine with the menu closed: a keyed screen with one tag
// The key colour of the layer on (the machine's picture shows through it) or off. Every screen switches it here; on each change, and only then,
// ui_live_key_changed is called with the new value (run.cc: the core's menu_video hook, hookhost.h). Task: the main loop.
void ui_live_key(bool on);
extern void (*ui_live_key_changed)(bool behind);
bool ui_available(void);

// A message view (start phase, errors): title line, text, colour. Visible after the next ui_poll().
void ui_message(const char *title, const char *text, bool bad);
// T164 Q7 (amended 2026-11-04): the question after a picture change: the seconds left, a blank line, then its key - inside the box (T152 D10).
void ui_picture_question(int left_s, const char *now, const char *prev);   // T187 D5: names the mode that came up and the one that comes back
const char *ui_chip_say_text(void);                       // the message chip of the top bar as last drawn (the bench compares it with the whole message)
int ui_note_extra_h(const char *s, int w);                // the height a note of the foot needs beyond its one line when wrapped to w pixels (0 for one line)
const char *ui_message_text(void);                        // what the message view shows now (the bench reads it)

// T101 S9: the first picture, before the slow work (settings, picture mode, libraries, ROM search). Drawn once on LVGL's top layer;
// a step changes the bar and its label only and renders at once. ui_message() and ui_splash_close() take it away.
void ui_splash_open(const char *what, const char *version, int nsteps);
void ui_splash_step(int step, const char *label);
void ui_splash_close(void);
void ui_message_clear(void);

// The top bar and footer exactly as the next render draws them (the state query reads them; ui_menu_render uses the same call).
struct TrxShellInfo;
void ui_shell_info_now(TrxShellInfo *i, MenuCtl *m);

// The menu (T93 D8): the views are built once in ui_open(). ui_menu_render() draws what the controller says, changing only the
// labels that differ; ui_menu_visible() shows or hides the menu view (the message view is the other one).
void ui_menu_render(MenuCtl &m);
// The tab of the strip that stands for the open tab of `m` (an application's hidden screen stands for the tab it is under; -1: none).
int ui_shell_tab(MenuCtl &m);
void ui_menu_visible(bool on);
// The question an application asks and the bar it shows (T104.3 D4, appdlg.h), drawn over whatever is on screen in the top layer; nothing when none is up.
class AppDlg;
void ui_dialog_render(AppDlg &d);
// The short line over every page (MenuCtl::toast_text(), the volume keys' feedback); call it after ui_menu_render(); nothing when no line stands.
void ui_toast_render(MenuCtl &m);

// A key that only moved the selection restyles two rows and leaves the pane alone (T104, board
// 2026-10-16). ui_cheap_moves() counts those; the bench reads it, and it is the number that says the
// menu is quick. ui_menu_forget_rows() drops the kept rows when something else cleaned the screen.
uint32_t ui_cheap_moves(void);
// T187: the geometry of the flat list as drawn (pane-relative pixels), for the bench: the selected row has to lie whole between rows_top and rows_bottom,
// the sum of the drawn rows (each with its 1 px gap) is drawn_h, the position label ends at pos_y2 (rows_top is where the first row starts).
struct UiListProbe {
    int nrows, first, sel, pane_h, rows_top, rows_bottom, drawn_h, sel_y1, sel_y2, pos_y2;
    int nvals, val_off_max;               // rows with a value; the largest distance of a value's middle from its label's middle (P4b)
    bool sel_seen, sel_heading, last_heading;
    char pos[64];
    // T205 P3: per drawn row (up to UI_PROBE_ROWS), where it stands and what its label and value objects hold: the bench sets them against the model's row and against the
    // rows above (the pitch), so a value that stands in another row than its name is seen
    enum { UI_PROBE_ROWS = 24 };
    int row_y[UI_PROBE_ROWS], row_h[UI_PROBE_ROWS], lab_cy[UI_PROBE_ROWS], val_cy[UI_PROBE_ROWS];
    char lab[UI_PROBE_ROWS][64], val[UI_PROBE_ROWS][64];
    bool row_has_val[UI_PROBE_ROWS];
};
bool ui_list_probe(UiListProbe *p);
// T207: the status line of the pane's foot as drawn (pane-relative; txt = its text), for the bench: it must lie below the last drawn row and inside the pane.
bool ui_status_probe(int *x1, int *y1, int *x2, int *y2, char *txt, int n);
// Window moves and changed rows served by writing the rows again instead of building the pane (T104, 2026-10-22).
uint32_t ui_inplace_moves(void);
void ui_menu_forget_rows(void);

// "Overlay Transparency" (0..3 = 0, 25, 50, 75 %) and "Color Scheme" (0..6, trxview.h trx_scheme_set): from any task; the UI task takes them at its next render.
void ui_set_look(int transparency, int scheme);
// "Filename overflow squeeze" (T228 R21; 0 None, 1 Beginning, 2 Middle, 3 End): how a file name too long for its row is cut (trx_fit_name).
void ui_set_name_squeeze(int quarter);
int ui_name_squeeze(void);

// One pass of LVGL (render what changed). Returns the ms until the next pass is wanted, -1 when the layer is gone.
int ui_poll(void);

// Renders the pending changes at once (after a key), instead of waiting for LVGL's next refresh tick.
void ui_render_now(void);

#endif
