#!/usr/bin/env python3
"""T104.1 5.6: our application's menus are keys only - a mouse report never navigates them.

The driver (usb_hid.cc) cannot be built on the Mac, so this reads the two places that decide it:
  1. process_mouse_report(): the mouse sink is called, and returns, BEFORE the first use of menu_override (the menu navigation by mouse and
     the wheel keys), so a sink that consumes the report leaves the navigation no way to run;
  2. the mouse library (coreapp/src/mouse_board.cc) registers itself as that sink, and nothing in coreapp/ queues a menu key or switches the
     mouse navigation setting on.
Fails if any of it is not so. TRX_MOUSE_HID / TRX_MOUSE_RUN / TRX_MOUSE_CFG / TRX_MOUSE_TOML point at other copies (the negative controls in run.sh)."""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HID = os.environ.get("TRX_MOUSE_HID") or os.path.join(ROOT, "host-app/software/io/usb/usb_hid.cc")
RUN = os.environ.get("TRX_MOUSE_RUN") or os.path.join(ROOT, "coreapp/src/mouse_board.cc")
COREAPP = os.path.join(ROOT, "coreapp")
CFG = os.environ.get("TRX_MOUSE_CFG") or os.path.join(COREAPP, "src/baseset.cc")
TOML = os.environ.get("TRX_MOUSE_TOML") or os.path.join(COREAPP, "cores/trx64u/menu.toml")

fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


hid = open(HID, newline="").read().replace("\r\n", "\n")
start = hid.index("bool UsbHidDriver :: process_mouse_report")
end = hid.index("void UsbHidDriver :: pipe_error")
body = hid[start:end]
hand = body.index("    if (handled_mouse) {")
tail = body[hand:]

call = tail.find("if (usb_hid_mouse_sink(")
check(call >= 0, "process_mouse_report calls the mouse sink")
ret = tail.find("return true;", call) if call >= 0 else -1
check(call >= 0 and 0 < ret < call + 400, "a sink that consumed the report returns at once")
nav = tail.find("if (menu_override)")
check(nav >= 0 and 0 <= call < ret < nav, "the sink and its return come before the menu navigation by mouse")
keys = tail.find("usb_hid_queue_menu_key")
check(keys >= 0 and call < ret < keys, "no menu key is queued before the sink has had the report")
check("usb_hid_mouse_sink(motion_x, motion_y," in tail, "the sink gets the motion after sensitivity and acceleration")

run = open(RUN).read()
check(re.search(r"^\s*usb_hid_set_mouse_sink\(mouse_sink_usb_report\);", run, re.M) is not None,
      "the mouse library (mouse_board.cc, its init entry) registers itself as the USB mouse sink")

# "menumouse raus" (Alex, 2026-10-29, T104.1: no mouse control in our application): the driver asks the application (baseset.cc, u64_get_usb_hid_config_value) for Gideon's item CFG_MENU_MOUSE_NAV (0x58) and is told 0, always;
# the setting is not in the TRX64U's menu; and nothing else in coreapp/ names the navigation or queues a menu key.
cfg = open(CFG).read()
m = re.search(r"case 0x58:([^\n]*)", cfg)
check(m is not None and re.match(r"\s*return 0;", m.group(1)) is not None, "baseset.cc answers CFG_MENU_MOUSE_NAV (0x58) with 0, always")
check("MENUMOUSE" not in cfg and "Menu Mouse Navigation" not in cfg, "baseset.cc has no setting for the menu mouse navigation")
toml = open(TOML).read()
check(not re.search(r"Menu Mouse Navigation", toml, re.I) and "MOUSE_NAV" not in toml, "the TRX64U's menu.toml has no setting for the menu mouse navigation")

bad = []
for d, _, fs in os.walk(COREAPP):
    for f in fs:
        if f.endswith((".cc", ".h")):
            t = open(os.path.join(d, f)).read()
            if f == os.path.basename(CFG):
                t = t.replace(m.group(0), "") if m else t          # the one line that answers 0 (checked above) may name it
                t = re.sub(r"//[^\n]*", "", t)
            if re.search(r"usb_hid_queue_menu_key|CFG_MENU_MOUSE_NAV|usb_hid_menu_mouse_navigation_enabled|MOUSE_NAV", t):
                bad.append(f)
check(not bad, "nothing in coreapp/ enables or feeds mouse navigation of the menu %s" % (bad or ""))

print("test_mousenav: %d failed" % fails)
sys.exit(1 if fails else 0)
