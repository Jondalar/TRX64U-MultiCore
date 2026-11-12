#!/usr/bin/env python3
"""T190: the parts of the mouse scaling that cannot be built on the Mac - read from the sources.
  1. usb_hid.cc reads the sensitivity and acceleration through CFG_MOUSE_SENSITIVITY / CFG_MOUSE_ACCELERATION (usb_hid_config.h 0x5D / 0x5E), applies
     them to every mouse report and hands the result to the mouse sink (the sequence tests/coreapp/test_mousescale.cc runs);
  2. u64cfg.cc answers those two keys from the mouse library (mouse_sink_scaling) before it looks at the TRX64U core's own menu.
TRX_SCALE_CFG points at another copy of u64cfg.cc (the negative control in run.sh)."""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HID = os.path.join(ROOT, "host-app/software/io/usb/usb_hid.cc")
CFGH = os.path.join(ROOT, "host-app/software/io/usb/usb_hid_config.h")
CFG = os.environ.get("TRX_SCALE_CFG") or os.path.join(ROOT, "coreapp/src/u64cfg.cc")
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


hid = open(HID, newline="").read().replace("\r\n", "\n")
h = open(CFGH).read()
check(re.search(r"#define CFG_MOUSE_SENSITIVITY\s+0x5D", h) and re.search(r"#define CFG_MOUSE_ACCELERATION\s+0x5E", h), "CFG_MOUSE_SENSITIVITY is 0x5D, CFG_MOUSE_ACCELERATION 0x5E")
check("usb_hid_get_config_value(CFG_MOUSE_SENSITIVITY, 8)" in hid, "the driver reads the sensitivity, default 8")
check("usb_hid_get_config_value(CFG_MOUSE_ACCELERATION, MOUSE_ACCELERATION_OFF)" in hid, "the driver reads the acceleration, default off")
body = hid[hid.index("bool UsbHidDriver :: process_mouse_report"):hid.index("void UsbHidDriver :: pipe_error")]
a = body.index("int sensitivity = usb_hid_get_mouse_sensitivity();")
b = body.index("usb_hid_apply_pointer_sensitivity(motion_x, motion_y, sensitivity,")
c = body.index("usb_hid_apply_pointer_acceleration(motion_x, motion_y,")
d = body.index("usb_hid_mouse_sink(motion_x, motion_y, hid_buttons)")
check(a < b < c < d, "sensitivity, then acceleration, then the sink gets the scaled motion")

cfg = open(CFG).read()
m = re.search(r"u64_get_usb_hid_config_value\(int key, int default_value\)\n\{(.*?)\n    switch \(key\)", cfg, re.S)
check(m is not None and "mouse_sink_scaling(key, &v)" in m.group(1) and "return v;" in m.group(1),
      "u64cfg.cc answers the driver from mouse_sink_scaling before the TRX64U menu's values")
sys.exit(1 if fails else 0)
