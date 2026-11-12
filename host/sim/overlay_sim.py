#!/usr/bin/env python3
"""Overlay in simulation: tb_overlay.v (vic_scaler + overlay) with the register
writes the Ultimate app makes, four 1080p frames dumped and checked against a
model of Gideon's char_generator_slave12, the frames saved as PNG.

  frame 0  overlay as after reset (off): the reference picture
  frame 1  menu with the app's SD settings (40 x 25 of 8 x 9): scale 3
  frame 2  the app's 1080p settings (12 x 24 font_pkg, big_font): scale 1
  frame 3  the app's 720p settings (8 x 16, stretch_y): scale 2

Usage: overlay_sim.py [png directory]   (default: sim/build)
"""
import os
import subprocess
import sys

from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
design = os.path.dirname(here)
work = os.path.join(here, "build")
png_dir = sys.argv[1] if len(sys.argv) > 1 else work
os.makedirs(work, exist_ok=True)

# the app's default_colors (u64_config.cc)
PAL = [0x000000, 0xF7F7F7, 0x8D2F34, 0x6AD4CD, 0x9835A4, 0x4CB442, 0x2C29B1, 0xEFEF5D,
       0x984E20, 0x5B3800, 0xD1676D, 0x4A4A4A, 0x7B7B7B, 0x9FEF93, 0x6D6AEF, 0xB2B2B2]

ops = []
W = lambda a, d: ops.append(f"0 {a:04x} {d:02x}")
R = lambda a, d: ops.append(f"1 {a:04x} {d:02x}")


def settings(cpl, lines, x_on, y_on, cw, ch):
    # Overlay::update_settings
    for reg, val in ((4, cpl), (5, lines), (6, x_on >> 8), (7, x_on & 0xFF), (8, y_on >> 8),
                     (9, y_on & 0xFF), (2, cw), (3, ch), (0xA, 0), (0xB, 0), (0xC, 0)):
        W(reg, val)


screen = [0x20] * 4096
colour = [0x0F] * 4096


def put(x, y, text, fg, bg=0, rev=False):
    for i, c in enumerate(text):
        code = c if isinstance(c, int) else ord(c)
        a = y * 40 + x + i
        screen[a] = (code & 0x7F) | (0x80 if rev else 0)
        colour[a] = fg | (bg << 4)


# a menu like the Ultimate's: title, a rounded window, entries, one selected
put(0, 0, " *** C64 Ultimate  TRX64 overlay ***   ", 1, 0, rev=True)
put(2, 2, [0x06] + [0x02] * 34 + [0x05], 12)
for row in range(3, 22):
    put(2, row, [0x04], 12)
    put(37, row, [0x04], 12)
put(2, 22, [0x03] + [0x02] * 34 + [0x01], 12)
entries = ["SD", "Flash", "Temp", "Usb0", "Net", "C64 Machine", "Disk Drives",
           "Cartridge", "Video Setup", "Audio Setup", "Network", "WiFi",
           "User Interface", "Software IEC", "Developer"]
for i, e in enumerate(entries):
    sel = i == 5
    put(4, 4 + i, f"{e:<20}{'Dir' if i < 5 else 'Menu':>12}", 7 if sel else 15, 6 if sel else 0, rev=False)
put(4, 20, "F1 Help  F5 Menu  RUN/STOP Exit", 13)
put(0, 24, "Ready.", 3, 11)
for ch in range(16):                       # the palette: reversed spaces, colour 0 clear
    put(24 + ch, 24, " ", ch, rev=True)

# --- the app's boot ------------------------------------------------------------------
ops.append("2 0 0")
ops.append("3 0 0")                        # frame 0: reference, overlay off
settings(40, 25, 386, 307, 8, 0x09)        # Overlay() with the SD settings
W(0xD, 0x00)                               # release_ownership
for i, c in enumerate(PAL):                # set_palette_rgb on the HDMI palette
    for k, b in enumerate((c >> 16, (c >> 8) & 0xFF, c & 0xFF, 0)):
        W(0x5000 + 4 * i + k, b)
for off in range(0x1E):                    # SetVideoMode1080p / resync: ignored
    W(0x4000 + off, 0xA5 ^ off)
for a in range(1000):                      # the UI fills screen and colour RAM
    W(0x1000 + a, screen[a])
    W(0x2000 + a, colour[a])
for a in (0, 1, 41, 999):                  # and reads them back
    R(0x1000 + a, screen[a])
    R(0x2000 + a, colour[a])
R(0x0000, 0)                               # registers read 0
R(0x4000, 0)
cur = 4 * 40 + 4                           # cursor: XOR bit 7 and back
W(0x1000 + cur, screen[cur] ^ 0x80)
R(0x1000 + cur, screen[cur] ^ 0x80)
W(0x1000 + cur, screen[cur])
R(0x1000 + cur, screen[cur])
W(0xD, 0xC0)                               # take_ownership: on, own keyboard, colour 0 clear
ops.append("3 0 0")                        # frame 1
settings(40, 25, 1438, 240, 12, 0x5E)      # DetermineOverlaySettings, 1080p
ops.append("3 0 0")                        # frame 2
settings(40, 25, 958, 160, 8, 0x90)        # 720p
ops.append("3 0 0")                        # frame 3
ops.append("4 0 0")
FRAMES = [None, (40, 25, 8, 0x09), (40, 25, 12, 0x5E), (40, 25, 8, 0x90)]

with open(os.path.join(work, "ovl_ops.txt"), "w") as f:
    f.write("\n".join(ops) + "\n")

# --- run ---------------------------------------------------------------------------
vvp = os.path.join(work, "tb_overlay.vvp")
subprocess.run(["iverilog", "-g2005", "-o", vvp, os.path.join(here, "tb_overlay.v"),
                os.path.join(design, "vic_scaler.v"), os.path.join(design, "overlay.v")], check=True)
os.makedirs(os.path.join(work, "mem"), exist_ok=True)
for m in ("ovl_font8.mem", "ovl_font12.mem"):
    src = os.path.join(design, "mem", m)
    dst = os.path.join(work, "mem", m)
    if not os.path.exists(dst) or open(src).read() != open(dst).read():
        open(dst, "w").write(open(src).read())
sim = subprocess.run(["vvp", "-n", vvp], cwd=work, capture_output=True, text=True)
print(sim.stdout, end="")
sim_ok = "PASS" in sim.stdout

# --- model -------------------------------------------------------------------------
font8 = [int(l, 16) for l in open(os.path.join(design, "mem", "ovl_font8.mem"))]
font12 = [int(l, 16) for l in open(os.path.join(design, "mem", "ovl_font12.mem"))]


def char_lines(ch, big):
    seq, y = [], 0
    while True:
        seq.append(y)
        if y == ch - 1:
            return seq
        y = y + 2 if big and (y & 3) == 2 else y + 1


def glyph(code, y, cw, big, stretch):
    if big:
        return (font12[code * 8 + (y >> 2)] >> (12 * (y & 3))) & 0xFFF
    row = (y >> 1) & 7 if stretch else (7 if y & 8 else y & 7)
    d = font8[code * 8 + row]
    if (y & 8) and not stretch and d != 0x18:
        d = 0
    return d


def expected(ref, cpl, nl, cw, chreg):
    ch, big, stretch = chreg & 0x1F, bool(chreg & 0x40), bool(chreg & 0x80)
    seq = char_lines(ch, big)
    w, h = cpl * cw, nl * len(seq)
    s = 3 if 3 * w <= 1920 and 3 * h <= 1080 else 2 if 2 * w <= 1920 and 2 * h <= 1080 else 1
    x0, y0 = (1920 - s * w) // 2, (1080 - s * h) // 2
    img = list(ref)
    for r in range(nl):
        for li, y in enumerate(seq):
            for c in range(cpl):
                a = (r * cpl + c) & 0xFFF
                code, col = screen[a], colour[a]
                g = glyph(code & 0x7F, y, cw, big, stretch)
                for b in range(cw):
                    fg = ((g >> (cw - 1 - b)) & 1) ^ (code >> 7)
                    idx = col & 15 if fg else col >> 4
                    if idx == 0:            # transparency 0
                        continue
                    for dy in range(s):
                        oy = y0 + (r * len(seq) + li) * s + dy
                        ox = x0 + (c * cw + b) * s
                        img[oy * 1920 + ox: oy * 1920 + ox + s] = [PAL[idx]] * s
    return img, (s, x0, y0, s * w, s * h)


def load(n):
    with open(os.path.join(work, f"ovl_frame{n}.txt")) as f:
        return [int(l, 16) for l in f]


def save(pixels, name):
    im = Image.new("RGB", (1920, 1080))
    im.putdata([(p >> 16, (p >> 8) & 0xFF, p & 0xFF) for p in pixels])
    path = os.path.join(png_dir, name)
    im.save(path)
    return path


ok = sim_ok
if sim_ok:
    os.makedirs(png_dir, exist_ok=True)
    ref = load(0)
    save(ref, "overlay_frame0_off.png")
    for n in (1, 2, 3):
        got = load(n)
        exp, geo = expected(ref, *FRAMES[n])
        bad = [i for i in range(len(exp)) if exp[i] != got[i]]
        names = {1: "sd", 2: "1080p", 3: "720p"}
        path = save(got, f"overlay_frame{n}_{names[n]}.png")
        print(f"frame {n} ({names[n]}): scale {geo[0]}, box at {geo[1]},{geo[2]} size {geo[3]}x{geo[4]}: "
              f"{len(bad)} pixels differ from the model -> {path}")
        for i in bad[:5]:
            print(f"   x {i % 1920} y {i // 1920}: got {got[i]:06x} expected {exp[i]:06x}")
        ok = ok and not bad
print("OVERLAY PASS" if ok else "OVERLAY FAIL")
sys.exit(0 if ok else 1)
