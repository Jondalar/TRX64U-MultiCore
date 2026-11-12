// The screens of the UX concept (TRX64-Ultimate docs/lvgl_ui_project/screens), drawn from what the menu record declares (T104.2 D10).
//
// A tab whose record names a layout (menu.toml `layout`) is drawn by it; the layouts are generic, they know slots, not cores:
//   home      Home: the title with its head buttons, the session panel (the LIVE C64 area, the title and mode lines, the session buttons) and the status panel
//             (status rows, one foot button)
//   split     a head (breadcrumb, title, state chip, head buttons), the main panel with the tab's rows (status rows; setting rows with a switch or a value),
//             an optional raised side panel (values, buttons), a bar of buttons under both, a one line hint
//   settings  the same as split (the bar holds RESET / SAVE)
//   status    System: the core's line and the information lines as rows
//   library   source chips, the folder's rows, the SELECTED panel with its primary action; the action sheet of a file while its actions are listed
// A node of the tab goes where its anchor's group says: head, session, status, foot (home); head, main, mainfoot, side, bar (split).
// Everything focusable is a row of the tab, so cursor keys and RETURN work as in the flat list; the selection moves without building anything.
#ifndef COREAPP_SCREENS_H
#define COREAPP_SCREENS_H

#include "menuctl.h"
#include "trxview.h"

// Does the open tab have a screen now? (A layout, and no prompt, job or folder picker over it.)
bool screens_wanted(MenuCtl &m);

// Draws the screen of the open tab - or, when only the selection moved, moves it. Returns false when the tab has none.
bool screens_repage_take();                                       // the last screens_render saw the page state flip and drew nothing: make the foot again, render again
bool screens_render(lv_obj_t *scr, TrxShellInfo &info, const TrxRoute &route, MenuCtl &m);

// The bench: the headings of the open tab's main column in order (written by the record or made by a change of group); how many were stored.
int screens_headings(char out[][40], int max);

// The pane was cleaned behind its back.
void screens_forget(void);

// How often a key was served without building the pane (the bench's number).
uint32_t screens_cheap_moves(void);

#endif
