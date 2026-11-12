#!/usr/bin/env python3
"""check_video.py <text|text80|hgr|lores|dhgr> <frame.txt> [--shift N] [--scan]: judge a captured frame (tb_a2_video.vhd writes it: 384 lines, each "<buffer> <560 RGB565 values of 4 hex digits>").
  text   the text page of asm/text.s drawn with the synthetic character ROM (mkchr.py) is predicted dot by dot and compared with every pixel of the frame;
         the best horizontal offset is searched (--shift to force one): a bench that is off by a pixel says by how much
  hgr    five bars of artifact colour: green, violet, orange, blue, white - the colour of each bar's middle, line by line, against the NTSC palette
  lores  the 16 lores colours in 16 blocks, against the standard colours of the same palette
Also: every Apple line is written twice (rows 2 y and 2 y + 1 are equal; with --scan the second is the first at half brightness), and the picture is not empty. Exit 0 = pass."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkchr

NTSC = {  # the palette of the digital artifact decode (vga_controller.vhd, "lticmanis 2024"), by name
    "black": 0x000000, "magenta": 0x9F1B48, "dark blue": 0x4832EB, "purple": 0xD643EF, "dark green": 0x197544, "gray1": 0x818181,
    "med blue": 0x3692FF, "light blue": 0xD89EFF, "brown": 0x496500, "orange": 0xD87300, "gray2": 0x818181, "pink": 0xFB8FBC,
    "green": 0x3CCC00, "yellow": 0xBCD600, "aqua": 0x6CE6B8, "white": 0xF1F1F1}
LORES = ["black", "magenta", "dark blue", "purple", "dark green", "gray1", "med blue", "light blue", "brown", "orange", "gray2", "pink", "green", "yellow", "aqua", "white"]

def rgb565(c):
    return ((c >> 16 & 255) >> 3) << 11 | ((c >> 8 & 255) >> 2) << 5 | (c & 255) >> 3

def load(path):
    rows = []
    for l in open(path):
        f = l.split()
        v = f[1]
        rows.append([int(v[i:i + 4], 16) for i in range(0, len(v), 4)])
    return rows

def main():
    prog, path = sys.argv[1], sys.argv[2]
    rows = load(path)
    bad = 0
    if len(rows) != 384 or any(len(r) != 560 for r in rows):
        print("FAIL frame is not 560 x 384"); sys.exit(1)
    scan = "--scan" in sys.argv
    for y in range(192):
        second = [(p >> 1) & 0x7BEF for p in rows[2 * y]] if scan else rows[2 * y]
        if second != rows[2 * y + 1]:
            print("FAIL line %d: the second row written for it is %s" % (y, "not the first one at half brightness" if scan else "not equal to the first")); bad += 1; break
    ncol = len(set(p for r in rows for p in r))
    print("colours in the frame:", ncol)
    lines = [rows[2 * y] for y in range(192)]
    if prog == "text":
        rom = mkchr.rom()
        def video_line(y, shift):
            r, seg = y // 8, y % 8
            base = 0x400 + (r & 7) * 0x80 + (r >> 3) * 0x28
            dots = []
            for c in range(40):
                a = base + c
                code = (5 * (a & 255) + 11 * (a >> 8)) & 255
                a_bit = code >> 7 & 1
                b_bit = (code >> 6 & 1) & (code >> 7 & 1)                  # ALTCHAR = 0, FLASH_CLK = 0
                addr = (0 << 12) | (0 << 11) | (a_bit << 10) | (b_bit << 9) | ((code & 63) << 3) | seg
                v = rom[addr]
                for k in range(7):
                    dots += [(v >> k & 1) ^ 1] * 2
            if shift >= 0:
                return [0] * shift + dots[:560 - shift]
            return dots[-shift:] + [0] * (-shift)
        white, black = rgb565(NTSC["white"]), 0
        best = None
        for shift in range(-24, 25):
            miss = 0
            for y in range(0, 192, 7):
                exp = [white if d else black for d in video_line(y, shift)]
                miss += sum(1 for a, b in zip(exp, lines[y]) if a != b)
            if best is None or miss < best[0]:
                best = (miss, shift)
        shift = int(sys.argv[sys.argv.index("--shift") + 1]) if "--shift" in sys.argv else best[1]
        miss = 0
        for y in range(192):
            exp = [white if d else black for d in video_line(y, shift)]
            miss += sum(1 for a, b in zip(exp, lines[y]) if a != b)
        print("text: best horizontal offset %+d (first mismatch count there: %d); at the offset used (%+d) %d of %d pixels differ" % (best[1], best[0], shift, miss, 192 * 560))
        if shift != 0 or miss != 0:
            bad += 1
            print("FAIL text: the picture is not the predicted one (offset %+d)" % shift)
    elif prog == "text80":
        rom = mkchr.rom()
        white, black = rgb565(NTSC["white"]), 0
        def code_at(a, aux):
            c = (5 * (a & 255) + 11 * (a >> 8)) & 255
            return c ^ 0x5A if aux else c
        def video_line80(y, shift):
            r, seg = y // 8, y % 8
            base = 0x400 + (r & 7) * 0x80 + (r >> 3) * 0x28
            dots = []
            for c in range(80):
                code = code_at(base + c // 2, c % 2 == 0)           # even columns from aux, odd from main
                a_bit = code >> 7 & 1
                b_bit = (code >> 6 & 1) & (code >> 7 & 1)
                v = rom[(a_bit << 10) | (b_bit << 9) | ((code & 63) << 3) | seg]
                dots += [(v >> k & 1) ^ 1 for k in range(7)]
            return ([0] * shift + dots[:560 - shift]) if shift >= 0 else (dots[-shift:] + [0] * (-shift))
        best = None
        for shift in range(-24, 25):
            miss = 0
            for y in range(0, 192, 7):
                exp = [white if d else black for d in video_line80(y, shift)]
                miss += sum(1 for a, b in zip(exp, lines[y]) if a != b)
            if best is None or miss < best[0]:
                best = (miss, shift)
        shift = best[1]
        miss = 0
        for y in range(192):
            exp = [white if d else black for d in video_line80(y, shift)]
            miss += sum(1 for a, b in zip(exp, lines[y]) if a != b)
        print("text80: best horizontal offset %+d; at it %d of %d pixels differ" % (shift, miss, 192 * 560))
        if shift != 0 or miss != 0:
            bad += 1; print("FAIL text80: not the predicted 80 column picture (offset %+d)" % shift)
    elif prog == "dhgr":
        for n, name in enumerate(LORES):
            want = rgb565(NTSC[name])
            cnt = collections.Counter()
            for y in range(192):
                cnt.update(lines[y][28 * n + 8:28 * n + 20])
            col, c = cnt.most_common(1)[0]
            tot = sum(cnt.values())
            print("DHGR colour %2d %-10s expected %04x: dominant %04x = %.1f %%" % (n, name, want, col, 100.0 * c / tot))
            if col != want or c * 100 < tot * 95:
                bad += 1; print("FAIL DHGR colour %d" % n)
        cnt = collections.Counter(p for y in range(192) for p in lines[y][448 + 8:560 - 8])
        if cnt.most_common(1)[0][0] != 0:
            bad += 1; print("FAIL the black part is not black")
    elif prog == "hgr":
        exp = [("violet" if False else "green"), "purple", "orange", "med blue", "white"]
        for k, name in enumerate(exp):
            want = rgb565(NTSC[name])
            cnt = collections.Counter()
            for y in range(192):
                cnt.update(lines[y][112 * k + 14:112 * k + 98])
            col, n = cnt.most_common(1)[0]
            tot = sum(cnt.values())
            print("bar %d expected %-9s %04x: dominant %04x = %.1f %% of the middle" % (k, name, want, col, 100.0 * n / tot))
            if col != want or n * 100 < tot * 95:
                bad += 1; print("FAIL bar %d" % k)
    elif prog == "lores":
        for n, name in enumerate(LORES):
            want = rgb565(NTSC[name])
            cnt = collections.Counter()
            for y in range(192):
                cnt.update(lines[y][28 * n + 8:28 * n + 20])
            col, c = cnt.most_common(1)[0]
            tot = sum(cnt.values())
            print("colour %2d %-10s expected %04x: dominant %04x = %.1f %%" % (n, name, want, col, 100.0 * c / tot))
            if col != want or c * 100 < tot * 95:
                bad += 1; print("FAIL colour %d" % n)
    if bad:
        print("FAIL check_video %s: %d problems" % (prog, bad)); sys.exit(1)
    print("PASS check_video %s" % prog)

if __name__ == "__main__":
    main()
