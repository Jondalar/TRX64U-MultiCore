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
bool ui_available(void);

// A message view (start phase, errors): title line, text, colour. Visible after the next ui_poll().
void ui_message(const char *title, const char *text, bool bad);
// T164 Q7 (amended 2026-11-04): the question after a picture change: the seconds left, a blank line, then its key - inside the box (T152 D10).
void ui_picture_question(int left_s);
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
// Window moves and changed rows served by writing the rows again instead of building the pane (T104, 2026-10-22).
uint32_t ui_inplace_moves(void);
void ui_menu_forget_rows(void);

// "Overlay Transparency" (0..3 = 0, 25, 50, 75 %) and "Color Scheme" (0..6, trxview.h trx_scheme_set): from any task; the UI task takes them at its next render.
void ui_set_look(int transparency, int scheme);

// One pass of LVGL (render what changed). Returns the ms until the next pass is wanted, -1 when the layer is gone.
int ui_poll(void);

// Renders the pending changes at once (after a key), instead of waiting for LVGL's next refresh tick.
void ui_render_now(void);

#endif
