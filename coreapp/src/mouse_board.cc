// The board side of the USB mouse library (mousesink.cc): what the `init` entry of the library in the TOML B (mouse_lib_init) does once the board is known. Board code: it builds only for the RISC-V
// application (the native tests and the menu simulator stand without it).
#include "mousesink.h"
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usb_hid.h"
#include "joystick_output.h"

// The accumulators are shared by the input task and the poll task (mousesink.h): a critical section, as the reference's was.
class BoardMouseCrit : public MouseCrit {
public:
    void enter() { portENTER_CRITICAL(); }
    void leave() { portEXIT_CRITICAL(); }
};

// The 1351 backend's pots (mouse_api.h): the driver's own port-1 output, as it drove it before the library took the reports.
class BoardMousePots : public MousePots {
public:
    void position(int16_t x, int16_t y)
    {
        portENTER_CRITICAL();
        usb_hid_set_mouse1_position(x, y);
        portEXIT_CRITICAL();
        JoystickOutput::instance().paceMouse();
    }
    void buttons(uint8_t joy) { usb_hid_set_joy1_output(joy); }
};

// The USB mouse reaches the core through the library and nowhere else (T104.1 5.6): while the hook is registered the driver's own handling of a mouse report (port-1 output, cursor keys,
// menu navigation) does not run, so our menus are keys only. A core without a mouse record takes the report and drops it.
void mouse_lib_init(const BoardProfile &)
{
    static BoardMouseCrit crit;
    static BoardMousePots pots;
    mouse_sink_set_crit(&crit);
    mouse_sink_set_pots(&pots);
    usb_hid_set_mouse_sink(mouse_sink_usb_report);
}
