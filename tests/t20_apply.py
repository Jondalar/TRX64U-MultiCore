#!/usr/bin/env python3
"""T20 APPLY on the board: the engine's undo/redo against the CPU walk (docs/specs/T20-delta-ring.md §17.3).

    ~/.cache/1541ultimate-tools/jtag-venv/bin/python -u tests/t20_apply.py <board ip> [pairs] [--full]

Needs a bitstream with CAPS2 b5 and an app with `timewarp:scrub?apply=`; a BASIC program
that writes the screen runs in the C64. Every pair of scrub steps ends on the same frame:
once through the engine, once the CPU's way and from a different frame (so undo and redo
both run in each), and the RAM after the two must be equal (the pages the program writes; --full: all 64 KB). Then a long jump,
oldest to newest and back, the same comparison.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import trxdbg  # noqa: E402  (the JTAG debug port: C64 RAM reads without side effects)

IP = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.189"
PAIRS = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 12
H = "http://" + IP


def call(method, path, body=None, t=60):
    r = urllib.request.Request(H + path, data=body, method=method)
    try:
        x = urllib.request.urlopen(r, timeout=t)
        return x.status, x.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def j(path, method="GET"):
    c, b = call(method, path)
    return c, json.loads(b.decode() or "{}")


DBG = None


RANGES = [(0x0000, 0x0800), (0xC000, 0xC100)]      # what the test program and the KERNAL write
FULL = "--full" in sys.argv


def ram():
    """C64 RAM over JTAG (the DMA read of machine:readmem would run the paused machine): the pages
    the test program touches, 2.3 KB in 3 s, or all 64 KB (1.3 ms a byte) with --full."""
    global DBG
    if DBG is None:
        DBG = trxdbg.Dbg()
    out = bytearray(65536)
    for lo, hi in ([(0, 65536)] if FULL else RANGES):
        for a in range(lo, hi, 1024):
            n = min(1024, hi - a)
            out[a:a + n] = bytes(v & 0xFF for v in DBG.read_many(range(a, a + n)))
    return bytes(out)


def scrub(frames, apply):
    c, d = j(f"/v1/timewarp:scrub?frames={frames}&apply={apply}", "PUT")
    assert c == 200, (frames, apply, c, d)
    return d


def main():
    c, st = j("/v1/timewarp:status")
    frames = st["frames"]
    print(f"ring {frames} frames, targets {st['targets']}")
    fails = 0
    first = scrub(50, 1)
    if not first.get("apply"):
        print("this bitstream has no APPLY (CAPS2 b5)")
        j("/v1/timewarp:scrubend", "PUT")
        return 2
    top = min(st["targets"], frames) - 120
    steps = [(60, 400), (100, 600), (250, 90), (300, 800), (30, 20), (500, 100)]
    for k in range(PAIRS):
        t, other = steps[k % len(steps)]
        t += k // len(steps)
        if t > top:                  # the ring may hold fewer frames than the table's deepest target
            t = top - 20 - k // len(steps)
        if other + 10 > top:
            other = top - 10
        d_hw = scrub(t, 1)
        r_hw = ram()
        scrub(other, 1)
        d_sw = scrub(t, 0)           # from `other` to `t` by the CPU walk (redo or undo)
        r_sw = ram()
        diff = [i for i in range(65536) if r_hw[i] != r_sw[i]]
        # the C64's own screen cells count up in the test program: allow nothing
        print(f"pair {k}: back {t} via {other}: engine {d_hw['records']} records {d_hw['ms']} ms, "
              f"CPU walk {d_sw['records']} records {d_sw['ms']} ms, RAM {'equal' if not diff else str(len(diff)) + ' bytes differ'}")
        if diff:
            fails += 1
            print("   first differences:", ", ".join(f"${i:04X} {r_hw[i]:02X}/{r_sw[i]:02X}" for i in diff[:8]))
    # undo of the whole ring and back again: the engine's way, then the CPU's
    oldest = top
    d1 = scrub(oldest, 1)
    r_a = ram()
    scrub(40, 1)
    d2 = scrub(oldest, 0)
    r_b = ram()
    diff = [i for i in range(65536) if r_a[i] != r_b[i]]
    print(f"oldest ({oldest} back): engine {d1['records']} records {d1['ms']} ms, CPU {d2['records']} records "
          f"{d2['ms']} ms, RAM {'equal' if not diff else str(len(diff)) + ' bytes differ'}")
    fails += bool(diff)
    if DBG:
        DBG.close()
    j("/v1/timewarp:scrubend?frames=100", "PUT")
    print("FAIL" if fails else "PASS")
    return 1 if fails else 0


sys.exit(main())
