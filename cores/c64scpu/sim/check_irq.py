#!/usr/bin/env python3
"""T241: the write count and RTI cycle checks of run_irq.sh. Counts the "mark|n|cycle|frame f" lines of a bench log (tb_c64scpu.v: one per completed 65816 write to $00:0B00-$00:0B03; the
C64-bus copies of a mirrored write print "mark|n|cycle" without the frame and are not counted) and compares them with "marks" of expect.json (tools/gen_c64scpu_irq.py):
$0B02 = 3 (STA, then ASL $0B02 in emulation mode writes the byte read and the new byte), $0B03 = 2 (STA, then INC $0B03 in native mode writes once).
T241 S2: the marks 0 and 1 frame the loops of RTI at software 1 MHz: per mode a subroutine called three times (a warm-up with 20 iterations, 20, 10); T = the PHI2 cycles from the write to
$0B00 to the write to $0B01. The warm-up pair is ignored (cold code). The difference of the long and the short loop is 10 iterations of the loop body, whose cycles are fixed by the instructions and by the RTI (expect.json "rti": emulation 240 = 10 x
24 with a 6-cycle RTI, native 230 = 10 x 23 with a 7-cycle RTI; the sources are in the docstring of tools/gen_c64scpu_irq.py). The offset of the marks cancels.
  check_irq.py <log> <expect.json>      prints one "marks| ..." and one "rti| ..." line; exit 1 when a count or a difference differs"""
import json
import re
import sys


def main():
    log, ex = sys.argv[1], json.load(open(sys.argv[2]))
    exp = ex["marks"]
    got, seq = {}, []
    for ln in open(log, errors="replace"):
        g = re.match(r"mark\|(\d)\|(\d+)\|frame ", ln)
        if g:
            got[g.group(1)] = got.get(g.group(1), 0) + 1
            if g.group(1) in "01":
                seq.append((g.group(1), int(g.group(2))))
    bad = [k for k in sorted(exp) if got.get(k, 0) != exp[k]]
    print("marks| completed 65816 writes: " + ", ".join("$0B0%s %d (model %d)" % (k, got.get(k, 0), exp[k]) for k in sorted(exp)) + (" FAIL" if bad else " ok"))
    rti = ex["rti"]
    loops = [seq[i + 1][1] - seq[i][1] for i in range(0, len(seq) - 1, 2) if seq[i][0] == "0" and seq[i + 1][0] == "1"]
    ok = len(seq) == 12 and len(loops) == 6 and all(seq[i][0] == "01"[i % 2] for i in range(len(seq)))
    if ok:
        emu, nat = loops[1] - loops[2], loops[4] - loops[5]
        ok = emu == rti["emu"] and nat == rti["nat"]
        print("rti| loops of %d and %d RTI (PHI2 cycles, 1 MHz): emulation %d and %d, difference %d (model %d: RTI 6 cycles); native %d and %d, difference %d (model %d: RTI 7 cycles) %s"
              % (rti["n"][0], rti["n"][1], loops[1], loops[2], emu, rti["emu"], loops[4], loops[5], nat, rti["nat"], "ok" if ok else "FAIL"))
    else:
        print("rti| FAIL: marks 0/1 seen %s, twelve (six loops: marks 0, 1, three times in each mode) expected" % seq)
    sys.exit(1 if bad or not ok else 0)


main()
