#!/usr/bin/env python3
"""T168: an independent model of the picture look_ma_no_vic makes, for the bench of ddt_novic (tb_ddt_novic.v). Written from the CODE of DDT's video_pattern.v
(rtl/video_pattern.v: the zoomed XOR luma background and its dividers, the 44 x 25 text screen, the hue ramp) and scroller.v (the 2048 character text, the scroll and the
zoom envelope every 512 frames), none of it from the RTL of the core. What the core does with the result is spec D5 of T168: the colour a PAL receiver shows for his luma + hue
(U = A cos hue, V = A sin hue, A = 44 sine64 / 128; B - Y = U / 0.493, R - Y = V / 0.877), computed with the constants the core uses and checked here against the exact PAL arithmetic.
The font is Gideon's overlay font (host/mem/ovl_font8.mem) with the screen code mapped to ASCII, the text screen and the scroller text are read from DDT's files.

  novic_model.py <out.hex> <frames> <frame_mod512 at start>
writes `frames` pictures of 352 x 200 RGB565 words, one 4 digit hex per line, picture after picture, row after row. Picture n is the one made after n + 1 frame starts
(the registers change at the frame start, in the first clock of the picture).
"""
import math, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
RTL = os.path.join(ROOT, "cores", "ddt_novic", "rtl")
W, H = 352, 200
SINE = [0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12]


def sine64(p):
    p &= 63
    return SINE[p] if p < 32 else -SINE[p - 32]


def wrap(v, bits):
    m = 1 << bits
    v &= m - 1
    return v - m if v >> (bits - 1) else v


def read_text(path, array):
    """`text[ N] = 8'dV;` / `mem[idx + k] = 8'dV;` statements of one of his ROM files."""
    src = open(path).read()
    src = re.sub(r"//[^\n]*", "", src)
    out = {}
    idx = 0
    for m in re.finditer(r"idx = \((\d+)\*W\) \+ (\d+);|idx = (\d+)\*W;|idx = (\d+);|%s\[\s*(?:idx \+ )?\s*(\d+)\]\s*=\s*8'd(\d+);" % array, src):
        if m.group(1):
            idx = int(m.group(1)) * 44 + int(m.group(2))
        elif m.group(3):
            idx = int(m.group(3)) * 44
        elif m.group(4):
            idx = int(m.group(4))
        else:
            k = int(m.group(5))
            out[(idx + k) if array == "mem" else k] = int(m.group(6))
    return out


def pal_rgb(luma, hue_phase, plain):
    """The colour of a pixel: his luma and hue as a PAL receiver shows them (spec D5), and the exact PAL arithmetic as the check of the shifts."""
    if plain:
        return 255, 255, 255
    s = sine64(hue_phase >> 2)
    c = sine64((hue_phase >> 2) + 16)
    u_c = (c * 44) >> 7
    v_c = (s * 44) >> 7
    d_b = (u_c * 260) >> 7
    d_r = (v_c * 146) >> 7
    d_g = (-(d_r * 65 + d_b * 25)) >> 7
    return tuple(max(0, min(255, luma + d)) for d in (d_r, d_g, d_b))


def check_shifts():
    """The shift-and-add constants against the PAL matrix, for every hue of the ramp (worst error in 8 bit counts)."""
    worst = 0.0
    for hp in range(256):
        s = sine64(hp >> 2); c = sine64((hp >> 2) + 16)
        u, v = (c * 44) >> 7, (s * 44) >> 7
        db, dr = u / 0.493, v / 0.877
        dg = -0.299 / 0.587 * dr - 0.114 / 0.587 * db
        r, g, b = pal_rgb(128, hp, False)
        for got, want in ((r, 128 + dr), (g, 128 + dg), (b, 128 + db)):
            worst = max(worst, abs(got - want))
    assert worst < 2.5, "the shift constants are off by %.2f counts" % worst
    return worst


def main():
    out, nframes, f0 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    worst = check_shifts()
    text = [read_text(os.path.join(RTL, "scroller.v"), "text").get(i, 0) for i in range(2048)]
    scr = [32] * 1100
    for k, v in read_text(os.path.join(RTL, "screen_matrix.v"), "mem").items():
        scr[k] = v
    fnt = [int(w, 16) for w in open(os.path.join(ROOT, "host", "mem", "ovl_font8.mem")).read().split()]

    def glyph(code, row, col):
        asc = code + 64 if code < 32 else code & 127
        return (fnt[asc * 8 + row] >> (7 - col)) & 1

    # registers of video_pattern.v and scroller.v
    zoom_phase, inv_bzx, inv_bzy, hue_anim = 0, 0x8000, 0x10000, 0
    charpix, fm, z_active, z_phase, inv_zx, inv_zy = 0, f0, 0, 0, 0x8000, 0x10000
    words = []
    for n in range(nframes):
        # the frame start: every register takes its next value from the old ones
        zt = (0xFF - (zoom_phase & 255)) if (zoom_phase >> 8) & 1 else zoom_phase & 255
        zx_new = (0x200 + (0x600 * zt) // 255) & 0xFFFF
        zy_new = (0x100 + (0x300 * zt) // 255) & 0xFFFF
        n_inv_bzx, n_inv_bzy = 16777216 // zx_new, 16777216 // zy_new
        n_zoom_phase = (zoom_phase + 1) & 511
        n_hue = (hue_anim + 1) & 255
        tri = z_phase if z_phase < 64 else 127 - z_phase
        amount = (tri & 63) << 2
        n_charpix = (charpix + 1) & 0xFFFF
        n_fm = (fm + 1) & 511
        n_za, n_zp = z_active, z_phase
        if fm == 511:
            n_za, n_zp = 1, 0
        elif z_active:
            n_zp = (z_phase + 1) & 127
            if z_phase == 127:
                n_za = 0
        if z_active:
            n_izx = (0x8000 - ((0x7000 * amount) >> 8)) & 0xFFFFFFFF
            n_izy = (0x10000 - ((0xE000 * amount) >> 8)) & 0xFFFFFFFF
        else:
            n_izx, n_izy = 0x8000, 0x10000
        zoom_phase, inv_bzx, inv_bzy, hue_anim = n_zoom_phase, n_inv_bzx, n_inv_bzy, n_hue
        charpix, fm, z_active, z_phase, inv_zx, inv_zy = n_charpix, n_fm, n_za, n_zp, n_izx, n_izy
        for y in range(H):
            for px in range(W):
                x = 2 * px
                # background
                dx = wrap(x - 352, 12); dy = wrap(y - 100, 12)
                bg_u = wrap(wrap(352 + wrap((dx * inv_bzx) >> 16, 12), 12) - 352, 12)
                bg_v = wrap(wrap(100 + wrap((dy * inv_bzy) >> 16, 12), 12) - 100, 12)
                luma_bg = (bg_u ^ bg_v) & 255
                # text screen
                code = scr[(y >> 3) * 44 + (x >> 4)]
                if 65 <= code <= 90:
                    code += 32
                font_bit = glyph(code, y & 7, (x >> 1) & 7)
                base = 255 if font_bit else max(luma_bg, 16)
                # scroller
                sdx = wrap(x - 352, 12); sdy = wrap(y - 164, 12)
                xz_s = wrap(352 + wrap((sdx * inv_zx) >> 16, 12), 12)
                yz_s = wrap(164 + wrap((sdy * inv_zy) >> 16, 12), 12)
                xz = 0 if xz_s < 0 else xz_s
                yz = 0 if yz_s < 0 else yz_s
                scroll_on = 0
                if 160 <= yz < 168:
                    scx = (xz + charpix) & 0xFFFF
                    scroll_on = glyph(text[(scx >> 3) & 2047], (yz - 160) & 7, scx & 7)
                hue_phase = (((x * 91) >> 8) + hue_anim) & 255
                r, g, b = pal_rgb(base, hue_phase, bool(scroll_on or font_bit))
                words.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    open(out, "w").write("".join("%04X\n" % w for w in words))
    print("model: %s  %d pictures, shift constants within %.2f counts of the PAL matrix" % (out, nframes, worst))


main()
