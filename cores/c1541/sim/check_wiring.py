#!/usr/bin/env python3
"""check_wiring.py LAYER2.v HOST_TOP.v: T159 - the 1541 drive's LED outputs reach the contract port drive_leds_n only through drive_leds_map.
Read from the code:
  1. the drive instance's .leds_n(X) is a wire X of the wrapper, not the port drive_leds_n
  2. exactly one drive_leds_map instance, .drv_leds_n(X) with the same X, .drive_leds_n(drive_leds_n)
  3. nothing else drives drive_leds_n (no assign, no other connection)
  4. the host top the core runs in reads [0] as the floppy (case LED) and [1] as the hard disk (blue lamp), the contract the map relies on
Exit 0 = pass."""
import re, sys

if len(sys.argv) != 3:
    print(__doc__)
    sys.exit(2)
src = open(sys.argv[1]).read()
top = open(sys.argv[2]).read()
bad = 0

def fail(msg):
    global bad
    bad += 1
    print("FAIL " + msg)

drv = re.findall(r"\.leds_n\(\s*(\w+)\s*\)", src)
if len(drv) != 1:
    fail("expected one .leds_n() connection on the drive, found %d" % len(drv))
elif drv[0] == "drive_leds_n":
    fail("the drive's leds_n goes straight to the port drive_leds_n")
mp = re.findall(r"\bdrive_leds_map\s+\w+\s*\(\s*\.drv_leds_n\(\s*(\w+)\s*\)\s*,\s*\.drive_leds_n\(\s*drive_leds_n\s*\)\s*\)", src)
if len(mp) != 1:
    fail("expected exactly one drive_leds_map instance driving drive_leds_n, found %d" % len(mp))
elif drv and mp[0] != drv[0]:
    fail("drive_leds_map reads %s, the drive drives %s" % (mp[0], drv[0]))
if re.search(r"\bassign\s+drive_leds_n\b", src):
    fail("drive_leds_n is also assigned")
if len(re.findall(r"\(\s*drive_leds_n\s*\)", src)) != 1:
    fail("drive_leds_n is connected somewhere besides the map output (or not at all)")
if not re.search(r"\.hd_act\(\s*!drive_leds_n\[1\]\s*\)", top):
    fail("host top: hd_act is not !drive_leds_n[1]")
if not re.search(r"cled_s1 <= \{\s*!drive_leds_n\[0\]\s*,", top):
    fail("host top: the case LED does not follow !drive_leds_n[0]")
if bad:
    sys.exit(1)
print("PASS wiring: drive leds_n -> %s -> drive_leds_map -> drive_leds_n; host: [0] floppy, [1] hard disk" % drv[0])
