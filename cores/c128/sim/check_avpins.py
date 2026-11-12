#!/usr/bin/env python3
"""T140 D24 item 1: reads the `W ...` lines of tb_c128_avpins and the balls from cores/c128/mono/avid_pins.xdc and checks which word
every ball carries. Ground truth (not read from c128_avpins.v): the three ball groups of the boundary scan (1541ultimate-tools/
docs/c64u-pinmap.md) and the board finding of 2026-10-31 (luma pin = the Y word, i.e. the group the scan listed third). Groups MSB first.
usage: check_avpins.py <sim output>   -- exit 0 when all balls carry what they must"""
import re, sys
root = __file__.rsplit("/cores/", 1)[0]
xdc = open(root + "/cores/c128/mono/avid_pins.xdc").read()
ball = {m.group(2): m.group(1) for m in re.finditer(r"PACKAGE_PIN (\w+) \[get_ports \{?(AVID_\w\[\d\])\}?\]", xdc)}
name = {v: k for k, v in ball.items()}
GROUP = {  # word -> balls, MSB first
    "y":  "B21 A21 B20 A20 A18 D22 E21 G21".split(),    # the port's luma pin (board 2026-10-31)
    "c":  "A19 B18 D20 D19 C18 F19 C17 F20".split(),    # chroma
    "cv": "C20 B22 D21 E22 F21 G22 C19 E19".split(),    # composite
}
bad = 0
rows = 0
for line in open(sys.argv[1]):
    m = re.match(r"W (\d+) (\w\w) (\w\w) (\w\w) R (\w\w) G (\w\w) B (\w\w)", line)
    if not m:
        continue
    rows += 1
    words = {"y": int(m.group(2), 16), "c": int(m.group(3), 16), "cv": int(m.group(4), 16)}
    bus = {"R": int(m.group(5), 16), "G": int(m.group(6), 16), "B": int(m.group(7), 16)}
    for w, balls in GROUP.items():
        for k, bl in enumerate(balls):
            port = name[bl]                               # e.g. AVID_G[9]
            ch, idx = port[5], int(port[7:-1])
            got = (bus[ch] >> (idx - 2)) & 1
            want = (words[w] >> (7 - k)) & 1
            if got != want:
                bad += 1
                if bad <= 6:
                    print("FAIL ball %s (%s) should carry bit %d of the %s word: got %d, want %d" % (bl, port, 7 - k, w, got, want))
print("%d rows, %d wrong balls" % (rows, bad))
if rows < 8:
    print("FAIL too few rows")
    sys.exit(1)
sys.exit(0 if bad == 0 else 1)
