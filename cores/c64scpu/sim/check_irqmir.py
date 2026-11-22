#!/usr/bin/env python3
"""T241 S2: the checks of run_irqmir.sh. Reads the log of tb_c64scpu.v +IRQMIR (the probe cores/c64scpu/sim/tb_irqmir.vh) and expect.json of tools/gen_c64scpu_irqmir.py.
  check_irqmir.py <log> <expect.json>
Phases: 1 A0 (reset mirror mode, IRQs masked), 2 A1 (reset mirror mode, IRQs on), 3 B0 (mirror everything, masked), 4 B1 (mirror everything, IRQs on).
Checks per mode (A = 1 + 2, B = 3 + 4); every line starts with "irqmir|":
  cycles   a masked phase: the lev (PHI2 cycle) at which every pass ends and the phase ends against the model (the 65816's bus cycles of the real code bytes, a cycle that is not a write waits while
           BA was low at the PHI2 fall before it; the BA runs are the bench's own log); the first pass that deviates is named
  irq      an IRQ phase: CIA 1 timer A underflows (floor(cycles / 16,422); the line's rising edges) against the vector pulls, the handler entries and the jiffy byte the program counted
  rule     steps that break the RDY rule (a read step went on with BA low, a stall with BA high), PHI2 cycles without a CPU step, per phase
Exit 1 when a check fails; the report is the point (a failing phase is a finding, not a broken bench)."""
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import gen_c64scpu_irqmir as G   # noqa: E402


def main():
    log, exp = sys.argv[1], json.load(open(sys.argv[2]))
    code = bytes.fromhex(exp["code"])
    runs, phs, phe, ps, sums, why, irqs, lost, vals, anoms, holds, trs = [], {}, {}, {1: [], 3: []}, {}, {}, {1: [], 2: [], 3: [], 4: []}, [], {}, [], [], []
    for ln in open(log, errors="replace"):
        f = ln.rstrip("\n").split("|")
        k = f[0]
        try:
            if k == "bal":
                runs.append((int(f[1]), int(f[2])))
            elif k == "phs":
                phs[int(f[1])] = (int(f[2]), int(f[3]), int(f[4]))
            elif k == "phe":
                phe[int(f[1])] = (int(f[2]), int(f[3]), int(f[4]))
            elif k == "ps":
                ps[int(f[1])].append(int(f[3]))
            elif k == "sum":
                sums[int(f[1])] = dict(re.findall(r"([a-z_]+) (\d+)", "|".join(f[2:])))
            elif k == "holdwhy":
                why[int(f[1])] = "|".join(f[2:])
            elif k == "irq":
                irqs[int(f[1])].append((int(f[2]), int(f[3]), int(f[4])))
            elif k == "irqlost":
                lost.append(ln.strip())
            elif k == "val":
                vals.setdefault(int(f[1]), {})[int(f[2], 16)] = int(f[3], 16)
            elif k == "anom":
                anoms.append(ln.strip())
            elif k == "hold":
                holds.append(ln.strip())
        except (ValueError, IndexError, KeyError):
            pass
    bad = []

    def say(s):
        print("irqmir| " + s)

    def chk(tag, ok, text):
        say("%s %s: %s" % ("ok  " if ok else "FAIL", tag, text))
        if not ok:
            bad.append(tag)
    fin = any(l.startswith("fin|") for l in open(log, errors="replace"))
    if len(phe) != 4 or not fin:
        say("FAIL the program did not finish: phases ended %s, end marker %s" % (sorted(phe), fin))
        bad.append("finish")
    # ---- the BA runs, for the model
    low = set()
    for a, b in runs:
        low.update(range(a, b + 1))
    ba_low = lambda m: m in low
    mdl = G.Model(code)
    per_pass = {}
    for pid in (1, 3):
        if pid not in phs or pid not in phe:
            continue
        kinds, marks = mdl.run(exp["pc"][str(pid)], {G.Z_LL: exp["n_masked"] & 255, G.Z_LH: exp["n_masked"] >> 8})
        s0 = phs[pid][0]
        ends = G.Model.end_levs(kinds, s0, ba_low)
        mod_pass = [ends[i] for i in marks]
        mod_end = ends[-1]
        meas = ps[pid]
        n = min(len(meas), len(mod_pass))
        first = next((i for i in range(n) if meas[i] != mod_pass[i]), None)
        total = phe[pid][0] - s0
        per_pass[pid] = total / max(1, len(meas))
        say("phase %d (%s, masked): %d passes; measured %d PHI2 cycles (%.2f per pass), model %d (%.2f per pass), the no-stall count is %d x %.0f"
            % (pid, "A0 reset mirror" if pid == 1 else "B0 mirror everything", len(meas), total, total / max(1, len(meas)), mod_end - s0, (mod_end - s0) / exp["n_masked"], exp["n_masked"], exp["pass_cycles"]))
        chk("cycles A0" if pid == 1 else "cycles B0", phe[pid][0] == mod_end and first is None and len(meas) == len(mod_pass),
            "phase end lev measured %d, model %d (difference %d); first deviating pass: %s" % (phe[pid][0], mod_end, phe[pid][0] - mod_end,
            "none" if first is None else "#%d, ends at lev %d, model %d (%+d); passes before it were exact" % (first + 1, meas[first], mod_pass[first], meas[first] - mod_pass[first])))
    for pid in (2, 4):
        if pid not in phe:
            continue
        s = sums.get(pid, {})
        lev = phe[pid][0] - phs[pid][0]
        und = lev // exp["period"]
        vec = int(s.get("vec", -1)); he = int(s.get("hentry", -1)); hd = int(s.get("hdone", -1)); edges = int(s.get("edges", -1))
        jiff = vals.get(pid, {}).get(0x20, -1)
        lat = [x[2] for x in irqs[pid]]
        say("phase %d (%s, IRQs on): %d PHI2 cycles, %d passes; timer underflows expected %d (cycles / %d), IRQ line edges %d, vector pulls %d, handler entries %d, handler done %d, jiffy byte %d, lost edges %d"
            % (pid, "A1 reset mirror" if pid == 2 else "B1 mirror everything", lev, int(s.get("passes", 0)), und, exp["period"], edges, vec, he, hd, jiff, int(s.get("lost", 0))))
        if lat:
            say("phase %d: IRQ edge to vector pull: min %d, max %d, mean %.1f PHI2 cycles" % (pid, min(lat), max(lat), sum(lat) / len(lat)))
        m = per_pass.get(1 if pid == 2 else 3)
        if m and int(s.get("passes", 0)):
            ov = (lev - int(s["passes"]) * m) / max(1, vec)
            say("phase %d: cycles beyond the passes at the masked phase's rate, per vector pull: %.1f (the handler itself is about 70 cycles without stalls: 7 for the interrupt, 63 of code)" % (pid, ov))
        chk("irq A1" if pid == 2 else "irq B1", abs(vec - und) <= 1 and vec == he == hd == jiff % 256 and int(s.get("lost", 1)) == 0,
            "vector pulls %d against %d underflows (+-1), handler entries %d, handler done %d, jiffy byte %d" % (vec, und, he, hd, jiff))
    for pid in (1, 2, 3, 4):
        s = sums.get(pid)
        if not s:
            continue
        say("phase %d rule: PHI2 cycles %s, CPU steps %s (advanced %s, stalled %s), cycles without a step %s, BA-low cycles %s, read steps kept with BA low %s, stalls with BA high %s, writes on the C64 bus with AEC low %s, rdy sample differs %s"
            % (pid, s.get("cyc"), s.get("ce"), s.get("adv"), s.get("stall"), s.get("hold"), s.get("ba_low_lev"), s.get("kept"), s.get("stallhi"), s.get("wr_aeclow"), s.get("rdy_mm")))
        if pid in why:
            say("phase %d holds: %s" % (pid, why[pid]))
        chk("rule %d" % pid, int(s.get("kept", 1)) == 0 and int(s.get("stallhi", 1)) == 0 and int(s.get("hold", 1)) == 0, "no step breaks the RDY rule and no PHI2 cycle goes without one")
    for l in holds[:6] + anoms[:6] + lost[:4]:
        say("first of its kind: " + l)
    print(("FAIL" if bad else "PASS") + " irqmir" + (" (" + ", ".join(bad) + ")" if bad else ""))
    return 1 if bad else 0


sys.exit(main())
