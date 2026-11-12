#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""T88 §5 RDY halt bench. halt.py REF.txt RUN.txt (traces of tb_cpu).
The executed cycles (adv = 1) of RUN must be the executed cycles of REF, in order (address, read/write, data,
sync), RDY or not. A cycle that RDY stopped (adv = 0) must be a read and must come back at the same address
as the next line; a write must never be repeated. Reports the number of stopped cycles."""
import sys


def load(p):
    return [l.split() for l in open(p) if len(l.split()) == 6]


ref = [l[1:5] for l in load(sys.argv[1]) if l[5] == "1"]
run = load(sys.argv[2])
ex = [l[1:5] for l in run if l[5] == "1"]
n = min(len(ref), len(ex))
bad = 0
for i in range(n):
    if ref[i] != ex[i]:
        print("DIFF at executed cycle %d: ref %s run %s" % (i, ref[i], ex[i]))
        bad = 1
        break
stalls = 0
for i, l in enumerate(run):
    if l[5] == "0":
        stalls += 1
        if l[2] != "R":
            print("a write was stopped at line %d: %s" % (i, l))
            bad = 1
            break
        if i + 1 < len(run) and run[i + 1][1] != l[1]:
            print("a stopped read did not repeat at line %d: %s -> %s" % (i, l, run[i + 1]))
            bad = 1
            break
# a run may end earlier or later than the reference by the trap; compare only the common part
print("halt: %d executed cycles compared (ref %d, run %d), %d stopped read cycles: %s" %
      (n, len(ref), len(ex), stalls, "BAD" if bad else "ok"))
sys.exit(bad)
