// The table of the USB mouse library (mousesink.cc): its table "mouse" (mouse_api.h), which the generic application finds through libtab_table("mouse") (libtab.h), the `table` entry of the
// library in the TOML B. Pure: no board code, so the native tests link it with the library; what the board adds (the critical section, the pots, the USB hook) is mouse_board.cc.
#include "mouse_api.h"
#include "mousesink.h"

static const MouseApi g_mouse = { sizeof(MouseApi), mouse_sink_configure, mouse_sink_reset, mouse_sink_pace, mouse_sink_set_sensitivity, mouse_sink_set_acceleration, mouse_sink_scaling };

const void *mouse_table(void)
{
    return &g_mouse;
}
