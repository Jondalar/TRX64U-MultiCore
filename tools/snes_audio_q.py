#!/usr/bin/env python3
"""T142: the quality of the sound the SNES core hands to the host, from a bench log (cores/snes/sim/run.sh audio_q): the S-DSP pairs the bench saw ("SNES ev <age> <value> <time>")
are the ideal signal; the host's frames ("SNES as <value> <raw> <time>") are compared with its band-limited (windowed sinc) interpolation at the same times, the delay
searched for. Prints the SNR in dB; exit 1 when it is below the limit (argv[2], default 40). The first design (a held pair on an own clock) measured 23 dB at 1 kHz."""
import sys
import numpy as np

def main(path, limit):
    L = open(path).read().splitlines()
    ev = np.array([[int(x) for x in l.split()[3:5]] for l in L if l.startswith("SNES ev ") and len(l.split()) == 5], float)
    o = np.array([[int(x) for x in l.split()[2:5:2]] for l in L if l.startswith("SNES as ") and len(l.split()) == 5], float)
    if len(ev) < 1000 or len(o) < 1000:
        print("SNES FAIL audio_q: too few samples (%d pairs, %d frames)" % (len(ev), len(o)))
        return 1
    tk, xk = ev[:, 1], ev[:, 0]
    tp = np.median(np.diff(tk))
    n0 = len(tk) // 3
    sel = (o[:, 1] > tk[n0]) & (o[:, 1] < tk[n0 + 700])
    to, yo = o[sel, 1], o[sel, 0] / 128.0
    def ref(t):
        k0 = np.searchsorted(tk, t)
        ks = np.arange(max(0, k0 - 24), min(len(tk), k0 + 24))
        u = (t - tk[ks]) / tp
        return (xk[ks] * np.sinc(u) * (0.5 + 0.5 * np.cos(np.pi * u / 24.5))).sum()
    best = None
    for d in np.linspace(0.0, 3.5 * tp, 141):
        r = np.array([ref(t - d) for t in to])
        v = np.sqrt(((yo - r) ** 2).mean())
        if best is None or v < best[0]:
            best = (v, d, np.sqrt((r ** 2).mean()))
    snr = 20 * np.log10(best[2] / best[0])
    print("SNES audio_q: %.1f dB (delay %.2f pairs, signal rms %.0f, error rms %.1f)" % (snr, best[1] / tp, best[2], best[0]))
    return 0 if snr >= limit else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 40.0))
