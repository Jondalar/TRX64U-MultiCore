#!/usr/bin/env python3
"""SAMPLE time series of the cartridge-port lines across a C64 reset under
Gideon's core (read-only boundary scan; the reset goes through REST).

    ~/.cache/1541ultimate-tools/jtag-venv/bin/python tests/cart/sample_reset_series.py SECONDS out.json [host]
"""
import sys, json, time, threading, urllib.request
sys.path.insert(0, "/path/to/Development/1541ultimate-tools/tooling")
import u64ii_bscan as bs

BALLS = {"V17": "GAMEn", "W17": "EXROMn", "W9": "ROMLn", "Y8": "ROMHn",
         "T18": "A15", "U17": "A14", "U20": "A13", "V19": "A12",
         "P20": "RST_I", "D16": "ADDROEn"}
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 6
out = sys.argv[2] if len(sys.argv) > 2 else "reset_series.json"
host = sys.argv[3] if len(sys.argv) > 3 else "192.0.2.189"
bsdl = bs.load_bsdl(bs.DEFAULT_BSDL)
cable = bs.Blaster.open()
rows = []
try:
    sc = bs.Scanner(cable, bsdl)
    sc.identify()
    t0 = time.time()
    fired = None
    while time.time() - t0 < secs:
        if fired is None and time.time() - t0 > 1.0:
            def reset():
                urllib.request.urlopen(urllib.request.Request(
                    f"http://{host}/v1/machine:reset", method="PUT"), timeout=5).read()
            threading.Thread(target=reset).start()
            fired = time.time() - t0
        pins = {r["ball"]: r["in"] for r in sc.pins(sc.capture()) if r["ball"] in BALLS}
        rows.append([round(time.time() - t0, 4)] + [pins[b] for b in BALLS])
finally:
    cable.release()
json.dump({"cols": ["t"] + list(BALLS.values()), "reset_at": fired, "rows": rows}, open(out, "w"))
# phases: runs of identical GAME/EXROM
print("captures", len(rows), "reset at", fired)
prev = None
for r in rows:
    key = (r[1], r[2])
    if key != prev:
        print(f"t={r[0]:7.3f}  GAMEn={r[1]} EXROMn={r[2]}")
        prev = key
def hist(sel):
    h = {}
    for r in sel:
        k = f"GE={r[1]}{r[2]} L={r[3]} H={r[4]} A={r[5]}{r[6]}{r[7]}{r[8]}"
        h[k] = h.get(k, 0) + 1
    return sorted(h.items(), key=lambda x: -x[1])[:8]
print("before reset:", hist([r for r in rows if r[0] < fired]))
print("first 1.5 s after:", hist([r for r in rows if fired <= r[0] < fired + 1.5]))
print("after that:", hist([r for r in rows if r[0] >= fired + 1.5]))
