#!/usr/bin/env python3
"""T168: an independent model of the picture mandelbrUlt makes, for the bench of the large core (tb_ddt_hd.v). Written from the CODE of DDT's
pixel_gen_mandelbrot.sv (Q3.22, three 25 x 25 signed products truncated by >>> 22 / >>> 21 into 25 bit, the escape test |z|^2 > 4 on the exact 50 bit squares,
the iteration count rule), fb_scanline_writer.sv (the pixel coordinates: the corner is centre - 176 or 128 scales + half a scale, one scale per pixel, all in 25 bit),
joystick_buttons.sv / top.v (the view registers) and mandel_video_pattern.v (palette index, the 44 x 25 text overlay with the C64 font), none of it from the RTL of the core.
The colours are the RGB values the palette was made from (the comments of palette_yuv_256.hex), the font is Gideon's overlay font (host/mem/ovl_font8.mem), the text
screen is read from trx/screen_matrix.v.

  mandel_model.py <out.hex> [--text] [--zoom N] [--iters 128] [--cx -2097152] [--cy 0] [--scale 35747] [--rot 0] [--engines 20]
writes 352 x 256 RGB565 words, one 4 digit hex per line, row after row.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
RTL = os.path.join(ROOT, "cores", "ddt_mandel", "rtl")
W, H = 352, 256


def wrap(v, bits=25):
    m = 1 << bits
    v &= m - 1
    return v - m if v >> (bits - 1) else v


def pixel(cx, cy, max_iters):
    """pixel_gen_mandelbrot.sv: the 8 bit palette index of one point."""
    max_it = max(max_iters, 16)
    zx = zy = 0
    it = 0
    limit = 4 << 44
    while True:
        a, b = zx * zx, zy * zy
        if a + b > limit:
            return 255 if (it & 255) == 255 else (it & 255) + 1
        if it == max_it - 1:
            return 0
        t = wrap((zx * zy) >> 21)
        zx = wrap(wrap((a >> 22) - (b >> 22)) + cx)
        zy = wrap(t + cy)
        it += 1


def render(cx0, cy0, scale, max_iters):
    """fb_scanline_writer.sv: the corner and the steps, then every pixel."""
    half_w = wrap((scale << 7) + (scale << 5) + (scale << 4))
    half_h = wrap(scale << 7)
    half_step = scale >> 1
    ax = wrap(cx0 - half_w + half_step)
    ay = wrap(cy0 - half_h + half_step)
    out = []
    cy = ay
    for y in range(H):
        cx = ax
        row = []
        for x in range(W):
            row.append(pixel(cx, cy, max_iters))
            cx = wrap(cx + scale)
        out.append(row)
        cy = wrap(cy + scale)
    return out


def palette():
    pat = re.compile(r"[0-9A-Fa-f]+\s*//\s*[0-9A-Fa-f]+\s*RGB\s*([0-9A-Fa-f]{6})")
    cols = []
    for line in open(os.path.join(RTL, "palette_yuv_256.hex")):
        r = int(pat.match(line).group(1), 16)
        cols.append((((r >> 16) >> 3) << 11) | ((((r >> 8) & 255) >> 2) << 5) | ((r & 255) >> 3))
    assert len(cols) == 256
    return cols


def text_screen(engines=20):
    """trx/screen_matrix.v: 44 x 25 screen codes, from its assignments (`idx = ...;` sets the row, `mem[idx + k] = 8'dN;` a cell)."""
    src = open(os.path.join(ROOT, "cores", "ddt_mandel", "trx", "screen_matrix.v")).read()
    src = re.sub(r"//[^\n]*", "", src)
    mem = [32] * (44 * 25)
    idx = 0
    for m in re.finditer(r"idx = \((\d+)\*W\) \+ (\d+);|idx = (\d+)\*W;|idx = (\d+);|mem\[idx \+ *(\d+)\] = 8'd(\d+);", src):
        if m.group(1):
            idx = int(m.group(1)) * 44 + int(m.group(2))
        elif m.group(3):
            idx = int(m.group(3)) * 44
        elif m.group(4):
            idx = int(m.group(4))
        else:
            mem[idx + int(m.group(5))] = int(m.group(6))
    # T255: row 2 starts with the engine count of the design (screen_matrix.v takes it from the parameter ENGINES; two digits, a blank for a leading zero)
    mem[2 * 44 + 0] = 48 + engines // 10 if engines >= 10 else 32
    mem[2 * 44 + 1] = 48 + engines % 10
    mem[2 * 44 + 2] = 32
    return mem


def font():
    return [int(w, 16) for w in open(os.path.join(ROOT, "host", "mem", "ovl_font8.mem")).read().split()]


def main():
    args = sys.argv[1:]
    out = args.pop(0)
    opt = {"--iters": 128, "--cx": -2097152, "--cy": 0, "--scale": (3 << 22) // W, "--rot": 0, "--zoom": 0, "--engines": 20}
    text = False
    while args:
        a = args.pop(0)
        if a == "--text":
            text = True
        else:
            opt[a] = int(args.pop(0))
    scale = opt["--scale"]
    for _ in range(opt["--zoom"]):
        scale >>= 1
    its = render(opt["--cx"], opt["--cy"], scale, opt["--iters"])
    pal = palette()
    scr, fnt = text_screen(opt["--engines"]), font()
    words = []
    for y in range(H):
        for x in range(W):
            it = its[y][x]
            if it == 0:
                idx = 0
            else:
                s = (it - 1) + opt["--rot"]
                idx = s - 255 + 1 if s >= 255 else s + 1
            w = pal[idx]
            if text and y < 200:
                xs = 2 * x
                code = scr[(y >> 3) * 44 + (xs >> 4)]
                asc = code + 64 if code < 32 else code & 127
                if (fnt[asc * 8 + (y & 7)] >> (7 - (x & 7))) & 1:
                    w = 0xFFFF
            words.append(w)
    open(out, "w").write("".join("%04X\n" % w for w in words))
    print("model: %s  %d points inside the set, %d distinct indices" % (out, sum(1 for r in its for v in r if v == 0), len({v for r in its for v in r})))


main()
