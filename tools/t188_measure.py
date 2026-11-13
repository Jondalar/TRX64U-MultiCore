#!/usr/bin/env python3
"""T188 S1/S2: the Amiga core's frame rate, the HDMI output's frame rate and their beat, from the console lines the Amiga core app prints every 10 s:

    [amiga] frames core <16 bit> out <16 bit> at <ms> ms

    tools/trxdbg.sh console follow | tools/t188_measure.py [--min-s 120]    (live: prints after every line)
    tools/t188_measure.py console.log                                     (a saved log)

Both counters wrap at 65536 (unwrapped here; the lines are 10 s apart = 500 frames, far under the wrap), `ms` is the application's clock.
Prints f_core, f_out (frames over the elapsed time of first..last line), their difference and the beat 1/|f_out - f_core|, and the drift of
(out - core) in frames: S2 wants that difference to stay within 1 frame over 600 s (a lock), or a beat of more than 10 min (a near-lock).
Needs at least two lines; --min-s is the shortest window it reports for. Never run against a board by this repository's agents (Alex runs it)."""
import re, sys, argparse

LINE = re.compile(r"\[amiga\] frames core (\d+) out (\d+) at (\d+) ms")

def parse(lines):
    pts, prev = [], None
    c0 = o0 = 0
    for ln in lines:
        m = LINE.search(ln)
        if not m: continue
        c, o, t = int(m.group(1)), int(m.group(2)), int(m.group(3))
        if prev is None: c0, o0, uc, uo = c, o, 0, 0
        else:
            uc = prev[0] + ((c - prev[3]) & 0xFFFF); uo = prev[1] + ((o - prev[4]) & 0xFFFF)
        prev = (uc, uo, t, c, o)
        pts.append((uc, uo, t))
    return pts

def report(pts, min_s):
    if len(pts) < 2: return None
    (c0, o0, t0), (c1, o1, t1) = pts[0], pts[-1]
    dt = (t1 - t0) / 1000.0
    if dt < min_s: return None
    fc, fo = (c1 - c0) / dt, (o1 - o0) / dt
    d = fo - fc
    beat = "none (no difference seen)" if d == 0 else "%.1f s" % (1 / abs(d))
    drift = (o1 - c1) - (o0 - c0)
    return "window %.1f s: f_core %.4f Hz, f_out %.4f Hz, diff %+.5f Hz, beat %s; (out - core) moved %+d frames" % (dt, fc, fo, d, beat, drift)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?"); ap.add_argument("--min-s", type=float, default=120.0)
    a = ap.parse_args()
    if a.log:
        r = report(parse(open(a.log)), 0)
        print(r or "fewer than two '[amiga] frames' lines")
    else:
        buf = []
        for ln in sys.stdin:
            buf.append(ln)
            p = parse(buf)
            r = report(p, a.min_s)
            if r: print(r, flush=True)
