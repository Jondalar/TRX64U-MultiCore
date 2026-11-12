#!/usr/bin/env python3
"""aga_view.py frame.txt [line] : decode the pixel dump of tb_aga (R = index & 15, G = index >> 4), print the runs of one
line (index x ticks) and the match against the expected picture (column c has index (c*37+11)&255, 8 lowres pixels = 32 ticks)."""
import sys
L = [ln.split() for ln in open(sys.argv[1])]
L = [[(int(p[1], 16) << 4) | int(p[0], 16) for p in ln] for ln in L if ln]
y = int(sys.argv[2]) if len(sys.argv) > 2 else len(L) // 2
ln = L[y]; runs = []
for v in ln:
    if runs and runs[-1][0] == v: runs[-1][1] += 1
    else: runs.append([v, 1])
print("line", y, "ticks", len(ln), "runs", len(runs))
print(" ".join("%02x*%d" % tuple(r) for r in runs[:48]))
exp = {(c * 37 + 11) & 255 for c in range(80)}
bad = sum(1 for v in ln if v not in exp and v != 0)
print("ticks with an index not in the picture:", bad, "of", len(ln), "; distinct:", sorted({v for v in ln})[:20])
