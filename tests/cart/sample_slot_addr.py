#!/usr/bin/env python3
"""SAMPLE the cartridge-port address/control balls while a cart runs under
Gideon's core (read-only boundary scan, nothing is driven). Counts how often
each ball reads 0 and how often A12-A15 differ from 1111.

    ~/.cache/1541ultimate-tools/jtag-venv/bin/python tests/cart/sample_slot_addr.py N out.json
"""
import sys, json, time
sys.path.insert(0, "/path/to/Development/1541ultimate-tools/tooling")
import u64ii_bscan as bs

BALLS = {"V19": "A12", "U20": "A13", "U17": "A14", "T18": "A15",
         "D15": "ADDRDIR", "D16": "ADDROEn", "R6": "DATADIR", "T6": "DATAOEn",
         "L15": "SLOT_PWR", "P20": "RST_I"}
n = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
out = sys.argv[2] if len(sys.argv) > 2 else "slot_addr.json"
bsdl = bs.load_bsdl(bs.DEFAULT_BSDL)
cable = bs.Blaster.open()
try:
    sc = bs.Scanner(cable, bsdl)
    sc.identify()
    low = {b: {"in": 0, "out": 0} for b in BALLS}
    oe_on = {b: 0 for b in BALLS}
    combos = {}
    for _ in range(n):
        rows = {r["ball"]: r for r in sc.pins(sc.capture()) if r["ball"] in BALLS}
        for b, r in rows.items():
            for k in ("in", "out"):
                if r[k] == 0:
                    low[b][k] += 1
            if r.get("oe") == 1:
                oe_on[b] += 1
        key = "".join(str(rows[b]["in"]) for b in ("T18", "U17", "U20", "V19"))
        combos[key] = combos.get(key, 0) + 1
finally:
    cable.release()
res = {"time": time.time(), "count": n,
       "low": {BALLS[b]: v for b, v in low.items()},
       "oe_flag": {BALLS[b]: v for b, v in oe_on.items()},
       "a15_a12": dict(sorted(combos.items(), key=lambda x: -x[1]))}
json.dump(res, open(out, "w"), indent=1)
print(json.dumps(res, indent=1))
