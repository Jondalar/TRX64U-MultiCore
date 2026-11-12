#!/usr/bin/env python3
"""T113 S10: test vectors for c128_keys.v from the machine's keyboard matrices, written by name here (the C64 matrix and the C128's extra
columns as the hardware has them; the layer table of the spec), not read from the RTL.
Line: <keys hex16> <usb hex6> <expect keys hex16> <expect extra hex6> <expect specials hex1: b0 caps, b1 40/80, b2 monitor>"""

# C64 matrix: PA column c -> rows 0-7 (PB lines)
C64 = {
    0: ["DEL", "RETURN", "CRSR_R", "F7", "F1", "F3", "F5", "CRSR_D"],
    1: ["3", "W", "A", "4", "Z", "S", "E", "LSHIFT"],
    2: ["5", "R", "D", "6", "C", "F", "T", "X"],
    3: ["7", "Y", "G", "8", "B", "H", "U", "V"],
    4: ["9", "I", "J", "0", "M", "K", "O", "N"],
    5: ["+", "P", "L", "-", ".", ":", "@", ","],
    6: ["POUND", "*", ";", "HOME", "RSHIFT", "=", "UPARROW", "/"],
    7: ["1", "LEFTARROW", "CTRL", "2", "SPACE", "CBM", "Q", "RUNSTOP"],
}
POS = {name: c * 8 + r for c, names in C64.items() for r, name in enumerate(names)}
# the extra matrix: K column -> rows 0-7
X = {
    0: ["HELP", "KP8", "KP5", "TAB", "KP2", "KP4", "KP7", "KP1"],
    1: ["ESC", "KP+", "KP-", "LINEFEED", "KPENTER", "KP6", "KP9", "KP3"],
    2: ["ALT", "KP0", "KP.", "UP", "DOWN", "LEFT", "RIGHT", "NOSCROLL"],
}
XPOS = {name: k * 8 + r for k, names in X.items() for r, name in enumerate(names)}
LAYER = {"F1": "ESC", "F3": "TAB", "F5": "HELP", "F7": "LINEFEED", "LEFTARROW": "ALT", "UPARROW": "NOSCROLL",
         "I": "UP", "J": "LEFT", "K": "DOWN", "L": "RIGHT",
         "1": "KP1", "2": "KP2", "3": "KP3", "4": "KP4", "5": "KP5", "6": "KP6", "7": "KP7", "8": "KP8", "9": "KP9", "0": "KP0",
         ".": "KP.", "+": "KP+", "-": "KP-", "RETURN": "KPENTER"}
SPECIAL = {"C": 1, "D": 2, "V": 4}


def b(*names):
    v = 0
    for n in names:
        v |= 1 << POS[n]
    return v


def line(keys, usb, ek, ex, sp):
    return "%016x %06x %016x %06x %x" % (keys, usb, ek, ex, sp)


out = []
for src, dst in LAYER.items():
    out.append(line(b("CTRL", "CBM", src), 0, 0, 1 << XPOS[dst], 0))
for src, bit in SPECIAL.items():
    out.append(line(b("CTRL", "CBM", src), 0, 0, 0, bit))
for name, pos in POS.items():                      # a C64 key alone is that C64 key and nothing extra
    out.append(line(1 << pos, 0, 1 << pos, 0, 0))
for src in LAYER:                                  # CTRL alone, or C= alone, with a layer key: no extra key
    out.append(line(b("CTRL", src), 0, b("CTRL", src), 0, 0))
    out.append(line(b("CBM", src), 0, b("CBM", src), 0, 0))
out.append(line(b("CTRL", "CBM", "A"), 0, b("A"), 0, 0))     # the layer eats only the keys of its table
out.append(line(b("CTRL", "CBM"), 0, 0, 0, 0))
for n in range(24):                                # the USB keyboard's extra keys pass
    out.append(line(0, 1 << n, 0, 1 << n, 0))
out.append(line(b("CTRL", "CBM", "I"), 1 << XPOS["ESC"], 0, (1 << XPOS["UP"]) | (1 << XPOS["ESC"]), 0))
print("\n".join(out))
