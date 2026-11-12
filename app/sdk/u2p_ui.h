/* The dialogs of the SDK's u2p_ui.h are declared in u2p_app.h (the base keeps the screen: there is no redraw hook). */
#ifndef U2P_UI_H
#define U2P_UI_H
#include "u2p_app.h"
static inline void u2p_ui_set_redraw(void (*redraw)(void *), void *ctx) { (void)redraw; (void)ctx; }
#endif
