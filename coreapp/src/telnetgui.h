// The Telnet remote menu service (T228 R21, row F41): TCP port 23, one client at a time, the menu of the application as a text screen. The server half of the firmware's
// network/socket_gui.cc - the listener that follows the setting "Telnet Remote Menu Service" (CFG_NETWORK_TELNET_SERVICE; :71, :77, :252), the password prompt of the
// network password (:86-142), the busy answer (:262-269) - over lwIP sockets without a task: the menu loop calls telnetgui_pass() once per pass, like mon_serve() does
// for the monitor's RPC, so a key from a terminal enters the menu on the same task and by the same door as a key of the board (uiapp_local_key). The screen is
// telnetscr.cc. Board code.
#ifndef COREAPP_TELNETGUI_H
#define COREAPP_TELNETGUI_H

#include "menuctl.h"
#include "appdlg.h"

struct TelnetEnv {
    MenuCtl *menu;                       // the controller whose screen the terminal shows (the application's session when one is open, else the core's menu)
    bool offered;                        // the core's settings have the row of the service: a core that has none does not open the port (as net.fx pushes only what a core lists)
    bool menu_open;                      // the menu is up on the screen of the machine
    const DlgView *dlg;                  // the question an application asks, null when none
};

// One pass of the server: the listener, the client's bytes, its keys, its screen. Returns true when a key was taken (the screen of the machine has to be drawn again).
bool telnetgui_pass(const TelnetEnv &e);

#endif
