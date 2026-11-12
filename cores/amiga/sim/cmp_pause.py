#!/usr/bin/env python3
"""T85 Freeze bench: compare the log of an unpaused run with the log of a paused run. Every key (chip bus window H, fast RAM
writes P, picture per frame F, frame buffer shown B) that both logs hold must carry the same value. Prints PASS or FAIL."""
import sys
def load(p):
    d = {}
    for ln in open(p):
        f = ln.split()
        if len(f) < 3:
            continue
        d[(f[0], f[1])] = f[2:]
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
# argv[3], the key kinds to compare (default all): the CPU's fast RAM path has the DDR2 latency in it (wait states), so a run
# with fast RAM accesses is exact for the picture (F, B) but not for the CPU's bus cycles (H, P)
kinds = sys.argv[3] if len(sys.argv) > 3 else 'HPFB'
need = {k: v for k, v in {'H': 30, 'P': 8, 'F': 5, 'B': 5}.items() if k in kinds}
if not any(k[0] == 'P' for k in a):
    need.pop('P', None)          # a run without CPU accesses to fast RAM has no P keys
bad = 0
n = {k: 0 for k in need}
for key in sorted(set(a) & set(b)):
    if key[0] not in need:
        continue
    n[key[0]] += 1
    if a[key] != b[key]:
        bad += 1
        if bad < 12:
            print("DIFF", key, a[key], b[key])
for k, m in need.items():
    if n[k] < m:
        print("FAIL: only %d common %s keys (need %d)" % (n[k], k, m)); bad += 1
print("compared", n, "differences", bad)
print("PASS cmp_pause" if bad == 0 else "FAIL cmp_pause")
sys.exit(1 if bad else 0)
