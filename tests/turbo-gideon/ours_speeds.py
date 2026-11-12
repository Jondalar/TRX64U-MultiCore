#!/usr/bin/env python3
"""The speed table of docs/turbo-measurements-gideon.md on our core, side by side with Gideon's.

    TM_IP=<board> TM_PRG_LOCAL=tests/turbo-gideon/turbomeas.prg TM_HOLD_WAIT=5 \
        tests/turbo-gideon/ours_speeds.py [d011]

Sets Turbo Control = U64 Turbo Registers, starts turbomeas.prg (tm.run) and measures every
$D031 index with bit 7 = 1 (no stalls) and 0 over 2 s of CIA 1 TOD, as tm.measure does.
Prints x = CPU cycles per PHI2 cycle for ours and Gideon's (data/speeds_d031.jsonl); writes
data/ours/speeds_d031.jsonl.
"""
import json, os, sys
import tm

here = os.path.dirname(os.path.abspath(__file__))
d011 = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x0B
ref = {}
for l in open(os.path.join(here, "data", "speeds_d031.jsonl")):
    r = json.loads(l)
    ref[(r["d031"], r["d011"])] = r
tm.cfg("Turbo Control", "U64 Turbo Registers")
tm.run()
out = open(os.path.join(here, "data", "ours", "speeds_d031.jsonl"), "w")
for bit7 in (0x80, 0x00):
    for idx in range(16):
        v = bit7 | idx
        r = tm.measure(v, d011, 20, 1)
        # x as turbomeas computes it: cycles per PHI2 cycle from the outer count
        r["x"] = round(r["mhz_est"] / 0.985248, 2)
        out.write(json.dumps(r) + "\n")
        g = ref.get((f"{v:02X}", f"{d011:02X}"))
        print(f"$D031 {v:02X}: ours x {r['x']:6.2f}  gideon x {g['x'] if g else '-':>6}  "
              f"CIA2 {r['cia2_hz']:.0f} Hz  frames/s {r['frames_per_s']:.2f}")
