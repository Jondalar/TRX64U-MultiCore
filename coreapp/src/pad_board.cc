// The board side of the gamepad library: what its `init` and `core` entries in the TOML B (pad_lib_init, pad_lib_core) do. Board code: it builds only for the RISC-V application (the native tests and
// the menu simulator stand without it).
#include "pad.h"
#include "board.h"
#include "desc.h"
#include "descrec.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usb_hid.h"
#include "usb_xinput.h"

// The service's state is shared by the USB task and the poll task (pad.h): a critical section.
class BoardPadCrit : public MouseCrit {
public:
    void enter() { portENTER_CRITICAL(); }
    void leave() { portEXIT_CRITICAL(); }
};

static const UsbPadSink g_pad_sink = { pad_attach, pad_report, pad_detach };

// T156: a USB gamepad reaches the core through the pad service and nowhere else; a core without a pad record drops the reports.
void pad_lib_init(const BoardProfile &)
{
    static BoardPadCrit crit;
    pad_set_crit(&crit);
    usb_hid_set_pad_sink(&g_pad_sink);
}

// T196 D3, D4: an XInput pad is claimed only in a core that names the capability `pad`, and (gate fix 4, T228 R10) in a core whose record declares a pad ([pad]): the claim follows the descriptor the menu
// will be built from. The first call (rec null) decides on the core's own descriptor alone; the one after the bundle is read adds the record, and a pad enumerated before it is not claimed.
void pad_lib_core(CoreCtx *, const CoreDesc *d, const DescRecord *rec)
{
    usb_xinput_set_sink(&g_pad_sink, desc_has_capability(d, "pad") || (rec && rec->has_pad));
}
