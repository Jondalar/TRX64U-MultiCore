#!/usr/bin/env python3
"""T182: the picture the VIC made (pixdump.txt of tb_c64scpu +PIXDUMP: "y x colour" per pixel position of one frame) against the screen the program meant.

  check_screen.py <pixdump.txt> <expect.json> [max_diff]      prints the text the picture shows ("pic| ..." lines) and PASS or FAIL
                                                              max_diff: cells that may differ (default 0; the real KERNAL's blinking cursor is one)

The text area is found as the rectangle of pixels that are not the border colour; it has to be 320 x 200 (40 x 25 cells of 8 x 8). Each cell is read as a bit
pattern (foreground colour = 1, background colour = 0, any other colour is a fault) and looked up in the character ROM of the machine (expect.json "chargen":
256 glyphs x 8 bytes; the first code that has the pattern wins, so a code and its inverse are told apart). PASS when every cell is the screen code expected
in expect.json "rows". A picture without glyphs (blank character ROM) or without the READY. text fails."""
import json
import sys
from collections import Counter


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    exp = json.load(open(sys.argv[2]))
    max_diff = int(sys.argv[3]) if len(sys.argv) == 4 else 0
    px = {}
    for ln in open(sys.argv[1]):
        y, x, c = (int(v) for v in ln.split())
        px[(y, x)] = c
    if not px:
        print("FAIL check_screen: the dump is empty (the frame was not reached)")
        return 1
    border = exp["border"]
    ys = sorted({y for y, _ in px})
    xs = sorted({x for _, x in px})
    inside = [(y, x) for (y, x), c in px.items() if c != border]
    if not inside:
        print("FAIL check_screen: every pixel is the border colour %d (%d x %d positions)" % (border, len(xs), len(ys)))
        return 1
    y0 = min(y for y, _ in inside); y1 = max(y for y, _ in inside)
    x0 = min(x for _, x in inside); x1 = max(x for _, x in inside)
    print("pic| frame %d x %d positions, text area x %d..%d y %d..%d = %d x %d, colours %s" % (
        len(xs), len(ys), x0, x1, y0, y1, x1 - x0 + 1, y1 - y0 + 1, dict(Counter(px.values()))))
    if (x1 - x0 + 1, y1 - y0 + 1) != (320, 200):
        print("FAIL check_screen: the text area is not 320 x 200")
        return 1
    glyph = {}
    for code in range(256):
        g = tuple(exp["chargen"][code * 8:code * 8 + 8])
        glyph.setdefault(g, code)
    fg, bg = exp["fg"], exp["bg"]
    rows = []
    bad = 0
    for r in range(25):
        codes = []
        for c in range(40):
            g = []
            for i in range(8):
                v = 0
                for j in range(8):
                    col = px.get((y0 + 8 * r + i, x0 + 8 * c + j))
                    if col == fg:
                        v = v * 2 + 1
                    elif col == bg:
                        v = v * 2
                    else:
                        v = None
                        break
                if v is None:
                    break
                g.append(v)
            codes.append(glyph.get(tuple(g)) if len(g) == 8 else None)
        rows.append(codes)
        text = "".join(("?" if k is None else chr(k + 64) if 1 <= (k & 0x7F) <= 26 and k < 128 else chr(k & 0x7F) if 32 <= (k & 0x7F) < 96 else ".") for k in codes)
        if text.strip():
            print("pic| %2d %s" % (r, text))
    for r in range(25):
        for c in range(40):
            if rows[r][c] != exp["rows"][r][c]:
                bad += 1
                if bad <= 6:
                    print("pic| cell row %d col %d: the picture shows code %s, expected %d" % (r, c, rows[r][c], exp["rows"][r][c]))
    if bad > max_diff:
        print("FAIL check_screen: %d of 1000 cells differ" % bad)
        return 1
    print("PASS check_screen: %d of 1000 cells of the picture are the screen codes of the program" % (1000 - bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())
