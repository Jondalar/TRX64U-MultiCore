// T196: the USB driver of XInput pads (Xbox 360 / X-mode, Xbox One / Series). Board code: it builds only for the RISC-V application. The protocol is in
// xinput.h/.cc and is tested natively.
#ifndef COREAPP_USB_XINPUT_H
#define COREAPP_USB_XINPUT_H

#include "usb_hid.h"

// The one switch (D3, D4): the driver claims an XInput interface only while a sink is given AND `cap_pad` is true (the running core's descriptor names the
// capability `pad`). Null / false (the default, and the host app): the interface stays unclaimed, as before.
void usb_xinput_set_sink(const UsbPadSink *sink, bool cap_pad);

#endif
