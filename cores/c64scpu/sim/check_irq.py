#!/usr/bin/env python3
"""T241: the write count check of run_irq.sh. Counts the "mark|n|cycle|frame f" lines of a bench log (tb_c64scpu.v: one per completed 65816 write to $00:0B00-$00:0B03; the
C64-bus copies of a mirrored write print "mark|n|cycle" without the frame and are not counted) and compares them with "marks" of expect.json (tools/gen_c64scpu_irq.py):
$0B02 = 3 (STA, then ASL $0B02 in emulation mode writes the byte read and the new byte), $0B03 = 2 (STA, then INC $0B03 in native mode writes once).
  check_irq.py <log> <expect.json>      prints one "marks| ..." line; exit 1 when a count differs"""
import json
import re
import sys


def main():
    log, exp = sys.argv[1], json.load(open(sys.argv[2]))["marks"]
    got = {}
    for ln in open(log, errors="replace"):
        g = re.match(r"mark\|(\d)\|\d+\|frame ", ln)
        if g:
            got[g.group(1)] = got.get(g.group(1), 0) + 1
    bad = [k for k in sorted(exp) if got.get(k, 0) != exp[k]]
    print("marks| completed 65816 writes: " + ", ".join("$0B0%s %d (model %d)" % (k, got.get(k, 0), exp[k]) for k in sorted(exp)) + (" FAIL" if bad else " ok"))
    sys.exit(1 if bad else 0)


main()
