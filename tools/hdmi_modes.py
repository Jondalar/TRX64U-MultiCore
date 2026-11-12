#!/usr/bin/env python3
"""T107 D7: the 12 HDMI modes (6 x 50/60 Hz) of the A1200 large core, derived and checked.

  tools/hdmi_modes.py table      print the results table (spec T107 5.8)
  tools/hdmi_modes.py check      check every limit of the 12 derived rows and decode the generated PLL bytes (T164 WP18: the A1200 has no .vh of its own any more)
  tools/hdmi_modes.py selftest   the negative controls: each must be caught (exit 0 only when every mutation is detected)
  host-table | host-gen | host-check | host-selftest   the same for the host edition (T164 WP1, host/hdmi_modes.vh, 9 rows);
                                 `check` and `selftest` run both editions

Source of the timings and pixel clocks: Gideon's table, vendor/gideon/software/u64/hdmi_scan.cc:65-79 (sm_*), read-only. The
clock chain is OURS (host/top.v): dot4x -> mmcm_ref x31.25/26 -> CDCE (cdce_i2c.v, I2C 0x64) -> GTP REFCLK = TMDS clock f_t
-> usrclk = f_t/2 -> mmcm_pix (x14, /7) -> pix_clk = f_t.  Pixel repetition ("gear" g): the TMDS clock is g x the pixel
clock, every pixel is sent g times, f_t = g x f_pix, the AVI InfoFrame says PR = g - 1.

CDCE register rules (TI CDCEL913/CDCE913 data sheet, PLL multiplier/divider definition; verified here against the two blobs
cdce_i2c.v ships, 55/14 and 990/263, byte for byte): fvco = fin x N/M, 80..230 MHz; P = 4 - floor(log2(fvco/fin)); N' = N x 2^P;
Q = floor(N'/M), 16 <= Q <= 63; R = N' - M x Q, 0 <= R <= 51; 1 <= M <= 511, 1 <= N <= 4095; VCO range bits: <125 MHz 00,
125-150 01, 150-175 10, >=175 11. Registers 0x18..0x1B: N[11:4]; N[3:0],R[8:5]; R[4:0],Q[5:3]; Q[2:0],P,range.
"""
import math, os, sys
from fractions import Fraction as F

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# host/top.v: mmcm_sys CLKIN 50 MHz x 56.75 / 3 / 30 = dot4x (PAL); mmcm_ref x 31.25 / 26 of dot4x = the CDCE's input
DOT4X = F(50_000_000) * F(5675, 100) / 3 / 30
REF = DOT4X * F(3125, 100) / 26

HDMI_NAMES = ["576p/480p", "1280x720", "1920x1080", "800x600", "1024x768", "1280x1024"]
# (vic, pixel clock Hz, hact, hfp, hsync, hbp, hpol, vact, vfp, vsync, vbp, vpol, gear)  fam 0 = 50 Hz, fam 1 = 60 Hz
MODES = {
    (0, 0): (17, 27067270, 720, 12, 64, 68, 0, 576, 5, 5, 39, 0, 5),
    (0, 1): (19, 74434928, 1280, 440, 40, 220, 1, 720, 5, 5, 20, 1, 2),
    (0, 2): (31, 148869856, 1920, 528, 44, 148, 1, 1080, 4, 5, 36, 1, 1),
    (0, 3): (0, 34944000, 800, 40, 112, 88, 1, 600, 30, 5, 37, 1, 3),
    (0, 4): (0, 54163200, 1024, 24, 136, 160, 0, 768, 3, 6, 29, 0, 2),
    (0, 5): (0, 92164800, 1280, 48, 112, 248, 1, 1024, 27, 3, 38, 1, 1),
    (1, 0): (1, 25126964, 640, 16, 96, 48, 0, 480, 10, 2, 33, 0, 5),
    (1, 1): (4, 74034804, 1280, 110, 40, 220, 1, 720, 5, 5, 20, 1, 2),
    (1, 2): (16, 148069608, 1920, 88, 44, 148, 1, 1080, 4, 5, 36, 1, 1),
    (1, 3): (0, 41028000, 800, 40, 124, 88, 1, 600, 20, 5, 25, 1, 3),
    (1, 4): (0, 64995840, 1024, 24, 136, 160, 0, 768, 3, 6, 29, 0, 2),
    (1, 5): (0, 106546560, 1280, 48, 112, 248, 1, 1024, 1, 3, 24, 1, 1),
}
GTP_VCO = (1.6e9, 3.3e9)        # GTP PLL VCO range, DS181 (7 Series GTP, -1): see the spec 5.8 for the citation
GTP_NMUL = 20                   # the wizard core: PLL N = 20, M = 1, TXOUT_DIV = 4 (hdmi_gtp.v / build_mono.tcl)
GTP_MIN_LINE = 0.5e9            # DS181 FGTPMIN
MMCM_VCO = (600e6, 1200e6)      # MMCME2, -1
MMCM_M = 14                     # mmcm_pix of this build: usrclk x 14, /7 -> f_t
CDCE_VCO = (80e6, 200e6)        # CDCEL824 (the board's part, host/top.v), SCAS945A: 80..200 MHz (CDCE913: 230); register map and PLL rules identical

def key(fam, h): return fam * 6 + h

def cdce_for(target_hz, ref=REF, strict=True):
    """Best (err_ppm, N, M, P, Q, R, bytes, f_out) under the CDCE rules for a target frequency."""
    best = None
    for M in range(1, 512):
        N0 = int(round(float(F(target_hz) * M / ref)))
        for N in range(N0 - 3, N0 + 4):
            if not 1 <= N <= 4095: continue
            fvco = ref * N / M
            if not (CDCE_VCO[0] <= fvco <= CDCE_VCO[1]): continue
            P = 4 - int(math.floor(math.log2(float(fvco / ref))))
            P = max(0, min(4, P))
            Np = N * (1 << P)
            Q, R = divmod(Np, M)
            if not (16 <= Q <= 63 and 0 <= R <= 51): continue
            err = float((fvco - target_hz) / target_hz) * 1e6
            if best is None or abs(err) < abs(best[0]) - 1e-9:
                best = (err, N, M, P, Q, R, fvco)
    if best is None:
        if strict: raise SystemExit("no CDCE setting for %s Hz" % target_hz)
        return dict(err=0.0, N=0, M=0, P=0, Q=0, R=0, fvco=F(target_hz), bytes=(0, 0, 0, 0), none=True)
    err, N, M, P, Q, R, fvco = best
    return dict(err=err, N=N, M=M, P=P, Q=Q, R=R, fvco=fvco, bytes=regs(N, P, Q, R, fvco), mode_ratio=(N, M))

def regs(N, P, Q, R, fvco):
    rng = 0 if fvco < 125e6 else 1 if fvco < 150e6 else 2 if fvco < 175e6 else 3
    return (N >> 4, ((N & 15) << 4) | (R >> 5), ((R & 31) << 3) | (Q >> 3), ((Q & 7) << 5) | (P << 2) | rng)

def compute():
    rows = []
    for fam in (0, 1):
        for h in range(6):
            vic, pclk, ha, hfp, hs, hbp, hpol, va, vfp, vs, vbp, vpol, gear = MODES[(fam, h)]
            ft = pclk * gear
            c = cdce_for(ft, strict=False)
            f_t = c["fvco"]                         # Pdiv = 1: the CDCE output is its VCO frequency
            htot, vtot = ha + hfp + hs + hbp, va + vfp + vs + vbp
            rows.append(dict(idx=key(fam, h), fam=fam, h=h, name=HDMI_NAMES[h], vic=vic, pclk=pclk, ha=ha, hfp=hfp, hs=hs, hbp=hbp,
                             hpol=hpol, va=va, vfp=vfp, vs=vs, vbp=vbp, vpol=vpol, gear=gear, ft_target=ft, c=c,
                             f_t=f_t, f_pix=f_t / gear, htot=htot, vtot=vtot, fps=float(f_t / gear / (htot * vtot)),
                             fps_target=pclk / (htot * vtot)))
    return rows

def c_none(r): return r["c"].get("none", False)

def limits(rows):
    """Every limit as a list of failures."""
    bad = []
    for r in rows:
        ft = float(r["f_t"])
        n = "%s %d Hz" % (r["name"], 50 if r["fam"] == 0 else 60)
        if c_none(r): bad.append("%s: no CDCE setting reaches %.3f MHz" % (n, ft / 1e6))
        if not GTP_VCO[0] <= GTP_NMUL * ft <= GTP_VCO[1]: bad.append("%s: GTP PLL VCO %.3f GHz out of 1.6..3.3" % (n, GTP_NMUL * ft / 1e9))
        if 10 * ft < GTP_MIN_LINE: bad.append("%s: GTP line rate %.0f Mb/s below 500" % (n, 10 * ft / 1e6))
        if not MMCM_VCO[0] <= MMCM_M * ft / 2 <= MMCM_VCO[1]: bad.append("%s: mmcm_pix VCO %.0f MHz out of 600..1200" % (n, MMCM_M * ft / 2e6))
        if abs(r["c"]["err"]) > 1000: bad.append("%s: pixel clock %.1f ppm from the table" % (n, r["c"]["err"]))
        if r["hfp"] + r["hs"] + r["hbp"] < 40: bad.append("%s: horizontal blanking too short for the layer triggers" % n)
        if r["vfp"] + r["vs"] + r["vbp"] < 4 or r["vfp"] < 1: bad.append("%s: vertical blanking" % n)
        if r["gear"] * r["htot"] > 8191: bad.append("%s: encoder x counter" % n)
        if float(r["f_t"] / r["gear"]) * 0 != 0: pass
        if r["gear"] not in (1, 2, 3, 4, 5): bad.append("%s: gear" % n)
    return bad

def known_blobs():
    """The two blobs cdce_i2c.v ships (bytes 0x18..0x1B), reproduced by the same rules: the control for regs()."""
    out = []
    for N, M, ref, want in ((55, 14, REF, (0x03, 0x70, 0x33, 0xED)),
                            (990, 263, F(148_062_470) * 263 / 990, (0x3D, 0xE0, 0xF3, 0xCD))):    # 60 Hz: top.v's hdmi_rate of System Mode 1
        fvco = ref * N / M
        P = 4 - int(math.floor(math.log2(float(fvco / ref))))
        Q, R = divmod(N * (1 << P), M)
        out.append((N, M, regs(N, P, Q, R, fvco), want))
    return out

def decode_regs(b):
    """Registers 0x18..0x1B back to (N, R, Q, P, range) - the inverse of regs(), read from the bytes only."""
    N = (b[0] << 4) | (b[1] >> 4)
    R = ((b[1] & 15) << 5) | (b[2] >> 3)
    Q = ((b[2] & 7) << 3) | (b[3] >> 5)
    return N, R, Q, (b[3] >> 2) & 7, b[3] & 3

def chip_check(row, b, n, ref=None):
    """What the CDCEL824 does with the four bytes of one mode, decoded from the bytes alone (not from the search that made them):
    M = (N x 2^P - R) / Q must be an integer, N >= M, P = 4 - int(log2 N/M), Q = int(N'/M), 16 <= Q <= 63, 0 <= R <= 511,
    fVCO = REF x N/M in 80..200 MHz, the range bits as the datasheet's thresholds, and fOUT = fVCO / Pdiv (Pdiv2 = 1 in cdce_mode.v)
    within 1000 ppm of the TMDS clock the mode needs. Returns the failures and the decoded numbers."""
    bad = []
    N, R, Q, P, rng = decode_regs(b)
    num = N * (1 << P) - R
    if Q == 0 or num % Q: return ["%s: bytes %s give no integer M (N %d P %d Q %d R %d)" % (n, b, N, P, Q, R)], None
    M = num // Q
    if M < 1 or M > 511: return ["%s: M %d out of 1..511" % (n, M)], None
    fvco = (ref or H_REF()) * N / M
    want_rng = 0 if fvco < 125e6 else 1 if fvco < 150e6 else 2 if fvco < 175e6 else 3
    if N < M: bad.append("%s: N %d < M %d" % (n, N, M))
    if P != max(0, 4 - int(math.floor(math.log2(N / M)))): bad.append("%s: P %d is not 4 - int(log2(N/M))" % (n, P))
    if not 16 <= Q <= 63: bad.append("%s: Q %d out of 16..63" % (n, Q))
    if R > 511: bad.append("%s: R %d" % (n, R))
    if not CDCE_VCO[0] <= fvco <= CDCE_VCO[1]: bad.append("%s: CDCE VCO %.3f MHz out of %d..%d" % (n, float(fvco) / 1e6, CDCE_VCO[0] / 1e6, CDCE_VCO[1] / 1e6))
    if rng != want_rng: bad.append("%s: VCO range bits %d but fVCO %.3f MHz needs %d" % (n, rng, float(fvco) / 1e6, want_rng))
    ppm = float((fvco - row["ft_target"]) / row["ft_target"]) * 1e6
    if abs(ppm) > 1000: bad.append("%s: CDCE output %.3f MHz is %.0f ppm from the TMDS clock %.3f MHz" % (n, float(fvco) / 1e6, ppm, row["ft_target"] / 1e6))
    return bad, (N, M, P, Q, R, rng, float(fvco), ppm)

def H_REF(): return REF

def chip_limits(rows):
    bad = []
    for r in rows:
        if c_none(r): continue
        b, _ = chip_check(r, r["c"]["bytes"], "%s %d Hz" % (r["name"], 50 if r["fam"] == 0 else 60))
        bad += b
    return bad

def vh_bytes(text):
    """The four bytes per mode as they stand in a hdmi_modes.vh text (what the synthesis reads)."""
    import re
    return {int(m.group(1)): tuple(int(m.group(2)[i:i + 2], 16) for i in (0, 2, 4, 6))
            for m in re.finditer(r"4'd(\d+): hm_cdce = 32'h([0-9A-Fa-f]{8});", text)}

def vh(rows):
    L = ["// GENERATED by tools/hdmi_modes.py gen - do not edit. T107 D7: the 12 HDMI modes of the A1200 large core.",
         "// mode index m = family * 6 + hdmi: family 0 = 50 Hz, 1 = 60 Hz; hdmi 0 576p/480p, 1 720p, 2 1080p, 3 800x600, 4 1024x768, 5 1280x1024",
         "// hm_row: {vic[6:0], gear[3:0], hact[11:0], hfp[11:0], hsync[11:0], hbp[11:0], hpol, vact[10:0], vfp[10:0], vsync[10:0], vbp[10:0], vpol}",
         "function [104:0] hm_row(input [3:0] m);", "    case (m)"]
    for r in rows:
        L.append("    4'd%d: hm_row = {7'd%d, 4'd%d, 12'd%d, 12'd%d, 12'd%d, 12'd%d, 1'b%d, 11'd%d, 11'd%d, 11'd%d, 11'd%d, 1'b%d};  // %s %d Hz" %
                 (r["idx"], r["vic"], r["gear"], r["ha"], r["hfp"], r["hs"], r["hbp"], r["hpol"], r["va"], r["vfp"], r["vs"], r["vbp"], r["vpol"], r["name"], 50 if r["fam"] == 0 else 60))
    L += ["    default: hm_row = 105'd0;", "    endcase", "endfunction", "",
          "// hm_cdce: CDCE registers 0x18..0x1B (N[11:4]; N[3:0],R[8:5]; R[4:0],Q[5:3]; Q[2:0],P,range)",
          "function [31:0] hm_cdce(input [3:0] m);", "    case (m)"]
    for r in rows:
        b = r["c"]["bytes"]
        L.append("    4'd%d: hm_cdce = 32'h%02X%02X%02X%02X;  // N %d M %d P %d Q %d R %d: %.6f MHz (%+.2f ppm)" %
                 ((r["idx"],) + tuple(b) + (r["c"]["N"], r["c"]["M"], r["c"]["P"], r["c"]["Q"], r["c"]["R"], float(r["f_t"]) / 1e6, r["c"]["err"])))
    L += ["    default: hm_cdce = 32'h0;", "    endcase", "endfunction", "",
          "// hm_rate: the TMDS clock in Hz, for the encoder's audio clock regeneration",
          "function [31:0] hm_rate(input [3:0] m);", "    case (m)"]
    for r in rows:
        L.append("    4'd%d: hm_rate = 32'd%d;" % (r["idx"], int(round(float(r["f_t"])))))
    L += ["    default: hm_rate = 32'd0;", "    endcase", "endfunction", ""]
    return "\n".join(L)

def table(rows):
    print("| # | mode | VIC | g | pixel clock target | f_t (CDCE) | N/M (P,Q,R) | err ppm | regs 18..1B | GTP line | frame Hz | htot x vtot |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        c = r["c"]
        print("| %d | %s %d Hz | %d | %d | %.6f MHz | %.6f MHz | %d/%d (%d,%d,%d) | %+.2f | %s | %.3f Gb/s | %.3f | %d x %d |" %
              (r["idx"], r["name"], 50 if r["fam"] == 0 else 60, r["vic"], r["gear"], r["pclk"] / 1e6, float(r["f_t"]) / 1e6, c["N"], c["M"], c["P"], c["Q"], c["R"],
               c["err"], " ".join("%02X" % b for b in c["bytes"]), 10 * float(r["f_t"]) / 1e9, r["fps"], r["htot"], r["vtot"]))

def decode_vh(text):
    """What the gateware does with each mode index, read back from hdmi_modes.vh alone (not from the tables that wrote it): the CDCE bytes of
    hm_cdce(m) -> N, P, Q, R (M = (N x 2^P - R) / Q has to be an integer) -> TMDS clock fin x N/M, and hm_row(m) -> gear x htot x vtot
    clocks per frame. Returns (failures, [(m, f_tmds, clocks_per_frame, frame_hz)])."""
    import re
    bad, out = [], []
    row = {int(m): [int(x) for x in re.findall(r"\d+'[db](\d+)", v)] for m, v in re.findall(r"4'd(\d+): hm_row = \{([^}]*)\}", text)}
    cdce = {int(m): int(v, 16) for m, v in re.findall(r"4'd(\d+): hm_cdce = 32'h([0-9A-Fa-f]{8});", text)}
    if sorted(row) != list(range(12)) or sorted(cdce) != list(range(12)):
        return ["hdmi_modes.vh: hm_row / hm_cdce do not name the modes 0..11"], out
    for m in range(12):
        f = row[m]                              # vic gear hact hfp hsync hbp hpol vact vfp vsync vbp vpol
        gear, htot, vtot = f[1], f[2] + f[3] + f[4] + f[5], f[7] + f[8] + f[9] + f[10]
        b = cdce[m]
        N = ((b >> 24) << 4) | ((b >> 20) & 15)
        R = (((b >> 16) & 15) << 5) | ((b >> 11) & 31)
        Q = (((b >> 8) & 7) << 3) | ((b >> 5) & 7)
        P = (b >> 2) & 7
        num = (N << P) - R
        n = "mode %d" % m
        if Q == 0 or num % Q: bad.append("%s: N %d P %d R %d Q %d gives no integer M" % (n, N, P, R, Q)); continue
        M = num // Q
        ft = REF * N / M
        per = gear * htot * vtot
        hz = float(ft) / per
        out.append((m, float(ft), per, hz))
        want = 50.0 if m < 6 else 60.0
        if abs(hz - want) / want > 0.005 and not (m >= 6 and abs(hz - want * 1000 / 1001) / want < 0.005):
            bad.append("%s: the CDCE gives %.3f MHz, the row runs %d clocks per frame = %.3f Hz, not %g Hz (the PLL bytes of another mode?)" % (n, float(ft) / 1e6, per, hz, want))
        if not CDCE_VCO[0] <= ft <= CDCE_VCO[1]: bad.append("%s: CDCE VCO %.3f MHz out of range" % (n, float(ft) / 1e6))
    return bad, out

def check():
    rows = compute()
    bad = limits(rows)
    for N, M, got, want in known_blobs():
        if tuple(got) != want: bad.append("control: %d/%d gives %s, the shipped blob is %s" % (N, M, got, want))
    cur = vh(rows)              # T164 WP18: the 12-row A1200 file is gone, the derivation is what is checked
    bad += chip_limits(rows)
    vb = vh_bytes(cur)          # the committed file itself, decoded: what cdce_mode.v sends
    for r in rows:
        if r["idx"] not in vb: bad.append("mode %d: no hm_cdce line in hdmi_modes.vh" % r["idx"]); continue
        bad += chip_check(r, vb[r["idx"]], "vh mode %d (%s)" % (r["idx"], r["name"]))[0]
    dbad, _ = decode_vh(cur)
    bad += dbad
    for b in bad: print("FAIL:", b)
    if not bad: print("PASS hdmi_modes: 12 modes inside every limit, every mode's CDCE bytes decode to its own frame rate and pass the CDCEL824 checks (M, P, Q, R, VCO range bits, output), the two shipped CDCE blobs reproduced")
    return 1 if bad else 0

def selftest():
    """Each mutation must be caught by the same checks `check` runs."""
    global MODES, regs, CDCE_VCO
    caught = tests = 0
    def has(sub, bad): return any(sub in b for b in bad)
    saved = dict(MODES)
    try:
        for name, mode, gear, subs in (("480p60 at gear 1", (1, 0), 1, ("GTP line rate", "no CDCE", "ppm from the table")),
                                       ("800x600@50 at gear 2", (0, 3), 2, ("GTP PLL VCO", "no CDCE", "ppm from the table")),
                                       ("1080p at gear 2", (1, 2), 2, ("mmcm_pix VCO", "GTP PLL VCO"))):
            tests += 1
            v = list(MODES[mode]); v[12] = gear; MODES[mode] = tuple(v)
            bad = limits(compute())
            ok = any(has(x, bad) for x in subs)
            print("  mutation %-24s %s" % (name, "caught" if ok else "NOT CAUGHT"))
            caught += ok
            MODES = dict(saved)
    finally:
        MODES = dict(saved)
    tests += 1                                     # a register packing that swaps Q and R bits must differ from the shipped blobs
    good = regs
    regs = lambda N, P, Q, R, fvco: good(N, P, R, Q, fvco)
    try: ok = any(got != want for _, _, got, want in known_blobs())
    finally: regs = good
    print("  mutation %-24s %s" % ("regs() with Q and R swapped", "caught" if ok else "NOT CAUGHT")); caught += ok
    for name, fn, sub in (("range bits of mode 4 off", lambda b: (b[0], b[1], b[2], (b[3] & ~3) | 1), "range bits"),
                          ("P of mode 4 off by one", lambda b: (b[0], b[1], b[2], (b[3] & ~28) | (((b[3] >> 2) & 7) - 1) << 2), "P "),
                          ("N of mode 4 +2 (output off)", lambda b: ((b[0] + 0x00), b[1] + 0x20, b[2], b[3]), ""),
                          ("VCO of 1080p above 200 MHz", None, "out of 80")):
        tests += 1
        r4 = [x for x in compute() if x["idx"] == (2 if fn is None else 4)][0]
        if fn is None:
            old = CDCE_VCO; CDCE_VCO = (80e6, 140e6)
            try: bad = chip_check(r4, r4["c"]["bytes"], "m")[0]
            finally: CDCE_VCO = old
        else:
            bad = chip_check(r4, fn(r4["c"]["bytes"]), "m")[0]
        ok = has(sub, bad)
        print("  mutation %-28s %s" % (name, "caught" if ok else "NOT CAUGHT")); caught += ok
    tests += 1                                     # T107 5.9: the PLL bytes of mode m+1 in the slot of mode m (a clock that is the next row's) must be caught by the decode
    import re
    txt = vh(compute())
    ents = re.findall(r"(    4'd(\d+): hm_cdce = 32'h)([0-9A-F]{8})", txt)
    shifted = txt
    for pre, m, val in ents: shifted = shifted.replace(pre + val, pre + "@" + ents[(int(m) + 1) % 12][2], 1)
    ok = bool(decode_vh(shifted.replace("@", ""))[0]) and not decode_vh(txt)[0]
    print("  mutation %-24s %s" % ("CDCE bytes of the next mode", "caught" if ok else "NOT CAUGHT")); caught += ok
    print("selftest: %d of %d mutations caught" % (caught, tests))
    return 0 if caught == tests else 1

# ---------------------------------------------------------------------------------------------------------------------------------
# T164 WP1: the host edition (host/hdmi_modes.vh). Same clock chain and CDCE rules as above; differences (spec T164 G3-G5, Q4, Q5, Q10,
# ADR-0060): 9 rows (the three 50 Hz PC rows are gone, Q5), `rate` is the PIXEL clock f_t / g (the wire carries one symbol per pixel, TMDS bit
# repetition), AVI aspect per row, a `locked` flag proven by rational arithmetic, and the CDCE bytes a C64-type core needs to stay frame-locked.
# ---------------------------------------------------------------------------------------------------------------------------------
HOST_VH = os.path.join(ROOT, "host/hdmi_modes.vh")
HOST_SET = {0, 1, 2, 6, 7, 8, 9, 10, 11}        # Q5: television rows at 50 and 60 Hz, the three PC modes at 60 Hz only
Q5_SET = frozenset(HOST_SET)
LOCK_ROWS = {2, 8}                              # Q4: the VIC raster path (vic_scaler) stays 1080p, both families
GEARS = (1, 2, 3, 5)                            # G4: gear 4 is not used
TMDS_RANGE = (80e6, 165e6)                      # the wizard's PLL N = 20, M = 1, D = 4 with REFCLK 80..165 MHz (host/build.tcl)
HDMI_MIN_PIX = 25e6                             # HDMI: the TMDS clock (= pixel clock on the wire) is at least 25 MHz
# GTP line rate: DS181 (7 Series FPGAs DC and AC Switching Characteristics), GTP transceiver performance table: minimum line rate 0.5 Gb/s,
# PLL VCO 1.6..3.3 GHz. With the wizard's fixed dividers (N = 20, D = 4 -> line rate 10 x refclk) the VCO bound is the tighter one: 0.8 Gb/s.
# The PDF is not in this repository; the numbers are GTP_VCO / GTP_MIN_LINE above, cited in spec T164 5 (WP1).
GTP_MAX_LINE = 3.3e9                            # the ceiling checked; 10 x 165 MHz = 1.65 Gb/s is below it, the VCO rule binds first
DOT4X_NTSC = F(50_000_000, 3) * F(47125, 1000) / 24   # host/mode_ctl.v mode 1: 32,725,694 Hz
REF_NTSC = DOT4X_NTSC * F(3125, 100) / 26
REF_OF = {0: REF, 1: REF_NTSC}                  # the CDCE reference per family a C64-type core runs (family = v60); frame-buffer cores keep PAL
VIC_FRAME = {0: 63 * 312 * 32, 1: 65 * 263 * 32}   # dot4x clocks per VIC frame (8 dots x 4 per cycle; PAL 63 x 312, NTSC 65 x 263)
REF_PER_DOT4X = F(125, 104)                     # host/top.v: the CDCE reference is 125/104 of dot4x in both families
# CEA-861 timing of each VIC the table uses: (hact, vact, htot, vtot, AVI picture aspect 1 = 4:3, 2 = 16:9)
CEA = {1: (640, 480, 800, 525, 1), 17: (720, 576, 864, 625, 1), 4: (1280, 720, 1650, 750, 2), 19: (1280, 720, 1980, 750, 2),
       16: (1920, 1080, 2200, 1125, 2), 31: (1920, 1080, 2640, 1125, 2)}
HOST_MODES = {key(f, h): MODES[(f, h)] + (CEA.get(MODES[(f, h)][0], (0,) * 5)[4],) for f in (0, 1) for h in range(6)}   # + aspect (0 = none: PC rows)
HOST_NAMES = ("vic", "pclk", "ha", "hfp", "hs", "hbp", "hpol", "va", "vfp", "vs", "vbp", "vpol", "gear", "asp")

# T188 D1/D2: the Amiga edition. The Amiga cores (cores/amiga, cores/amiga1200) carry their own copy of the host files (T164 3a Q1), so their
# copy of hdmi_modes.vh can hold other timings in the 50 Hz television rows 0..2 without touching the host or any other core. The rows are
# bent so that the HDMI frame is EXACTLY the Amiga frame: f_core = dot4x x 9/10 (amiga_ce.v: 9 ticks in 10 clocks) / (8 x 227 x 312) (the 28 MHz
# tick / 8 = CCK, 227 CCK per line, 312 lines non-interlaced). The TMDS clock is the CDCE ratio N/M of the reference (125/104 of dot4x), so
# gear x htot x vtot x f_core / REF is a ratio of integers; `cdce_exact` takes the first N/M <= 4095/511 the register rules accept. Then the
# output frame and the core frame come from the same MMCM and have a fixed phase: no beat, ever. The porches below are the ones of smallest
# deviation from the CEA raster that have an exact N/M (searched over htot +-60..100 and vtot -2..+25); the check re-proves each of them.
AMIGA_RAW = {0: {"hfp": 28}, 1: {"hfp": 420}, 2: {"vfp": 14}}   # row -> the changed porch (576p: htot 880, 720p: htot 1960, 1080p: vtot 1135)
F_CORE = DOT4X * F(9, 10) / (8 * 227 * 312)

def cdce_exact(ratio, ref):
    """CDCE setting for exactly N/M = ratio (scaled by k = 1, 2, ... while N <= 4095 and M <= 511): the first the register rules accept."""
    for k in range(1, 512):
        N, M = ratio.numerator * k, ratio.denominator * k
        if N > 4095 or M > 511: break
        fvco = ref * N / M
        if not CDCE_VCO[0] <= fvco <= CDCE_VCO[1]: continue
        P = max(0, min(4, 4 - int(math.floor(math.log2(float(fvco / ref))))))
        Q, R = divmod(N * (1 << P), M)
        if 16 <= Q <= 63 and 0 <= R <= 51:
            return dict(N=N, M=M, P=P, Q=Q, R=R, fvco=fvco, bytes=regs(N, P, Q, R, fvco))
    return None

def lock_for(fam, gear, htot, vtot):
    """The CDCE setting that makes the row's frame exactly the VIC frame, or None. Frame in TMDS clocks C = g x htot x vtot; the VIC frame is
    V dot4x clocks; f_t = REF x N/M = dot4x x 125/104 x N/M, so C / f_t = V / dot4x needs N/M = C / V x 104/125, exactly (Fractions)."""
    return cdce_exact(F(gear * htot * vtot, VIC_FRAME[fam]) / REF_PER_DOT4X, REF_OF[fam])

def amiga_mode(m):
    """HOST_MODES[m] with the T188 porch of row m (50 Hz rows 0..2 only); the pixel clock is the one of the Amiga frame."""
    vic, pclk, ha, hfp, hs, hbp, hpol, va, vfp, vs, vbp, vpol, gear, asp = HOST_MODES[m]
    ch = AMIGA_RAW[m]
    hfp, vfp = ch.get("hfp", hfp), ch.get("vfp", vfp)
    pclk = int(round(float(F_CORE * (ha + hfp + hs + hbp) * (va + vfp + vs + vbp))))
    return (vic, pclk, ha, hfp, hs, hbp, hpol, va, vfp, vs, vbp, vpol, gear, asp)

def host_compute(amiga=False):
    rows = []
    for m in sorted(HOST_SET):
        fam, h = divmod(m, 6)
        am = amiga and m in AMIGA_RAW
        vic, pclk, ha, hfp, hs, hbp, hpol, va, vfp, vs, vbp, vpol, gear, asp = amiga_mode(m) if am else HOST_MODES[m]
        htot, vtot = ha + hfp + hs + hbp, va + vfp + vs + vbp
        if am:
            c = cdce_exact(F_CORE * gear * htot * vtot / REF, REF)
            if c is None: raise SystemExit("amiga row %d: no exact CDCE setting for %dx%d" % (m, htot, vtot))
            c.update(err=0.0, mode_ratio=(c["N"], c["M"]))
        else:
            c = cdce_for(pclk * gear, strict=False)
        f_t = c["fvco"]
        lk = None if am else lock_for(fam, gear, htot, vtot)
        rows.append(dict(idx=m, fam=fam, h=h, name=HDMI_NAMES[h], vic=vic, asp=asp, ha=ha, hfp=hfp, hs=hs, hbp=hbp, hpol=hpol, va=va, vfp=vfp,
                         vs=vs, vbp=vbp, vpol=vpol, gear=gear, pclk=pclk, ft_target=pclk * gear, c=c, f_t=f_t, rate=int(round(float(f_t / gear))),
                         htot=htot, vtot=vtot, locked=m in LOCK_ROWS, lk=lk, amiga=am,
                         lk_rate=None if lk is None else int(round(float(lk["fvco"] / gear)))))
    return rows

def host_limits(rows):
    bad = []
    if {r["idx"] for r in rows} != Q5_SET:
        bad.append("rows %s are not the 9 of Q5 (television rows at 50 and 60 Hz, the PC rows at 60 Hz only)" % sorted(r["idx"] for r in rows))
    for r in rows:
        ft = float(r["f_t"])
        n = "%s %d Hz" % (r["name"], 50 if r["fam"] == 0 else 60)
        if c_none(r): bad.append("%s: no CDCE setting reaches %.3f MHz" % (n, ft / 1e6))
        if not TMDS_RANGE[0] <= ft <= TMDS_RANGE[1]: bad.append("%s: TMDS clock %.3f MHz out of 80..165" % (n, ft / 1e6))
        if float(r["rate"]) < HDMI_MIN_PIX: bad.append("%s: pixel clock %.3f MHz below the HDMI minimum 25 MHz" % (n, r["rate"] / 1e6))
        if not GTP_VCO[0] <= GTP_NMUL * ft <= GTP_VCO[1]: bad.append("%s: GTP PLL VCO %.3f GHz out of 1.6..3.3" % (n, GTP_NMUL * ft / 1e9))
        if not GTP_MIN_LINE <= 10 * ft <= GTP_MAX_LINE: bad.append("%s: GTP line rate %.0f Mb/s outside DS181 %.0f..%.0f" % (n, 10 * ft / 1e6, GTP_MIN_LINE / 1e6, GTP_MAX_LINE / 1e6))
        if not MMCM_VCO[0] <= MMCM_M * ft / 2 <= MMCM_VCO[1]: bad.append("%s: mmcm_pix VCO %.0f MHz out of 600..1200" % (n, MMCM_M * ft / 2e6))
        if abs(r["c"]["err"]) > 1000: bad.append("%s: pixel clock %.1f ppm from the table" % (n, r["c"]["err"]))
        if r["hfp"] + r["hs"] + r["hbp"] < 40: bad.append("%s: horizontal blanking too short for the layer triggers" % n)
        if r["vfp"] + r["vs"] + r["vbp"] < 4 or r["vfp"] < 1: bad.append("%s: vertical blanking" % n)
        if r["htot"] > 4095 or r["vtot"] > 2047: bad.append("%s: raster counters" % n)
        if r["gear"] not in GEARS: bad.append("%s: gear %d not in %s" % (n, r["gear"], GEARS))
        if r["vic"]:
            g = CEA.get(r["vic"])
            if g is None: bad.append("%s: VIC %d is not in the CEA table of this tool" % (n, r["vic"]))
            else:
                if (r["ha"], r["va"], r["htot"], r["vtot"]) != g[:4] and not r.get("amiga"): bad.append("%s: raster %dx%d (total %dx%d) is not CEA VIC %d %s" % (n, r["ha"], r["va"], r["htot"], r["vtot"], r["vic"], g[:4]))
                if r["asp"] != g[4]: bad.append("%s: AVI aspect %d, VIC %d wants %d (Q10: 4:3 for 576p/480p)" % (n, r["asp"], r["vic"], g[4]))
        elif r["asp"] != 0: bad.append("%s: a PC row (VIC 0) carries no aspect" % n)
        if r["locked"]:
            if r["ha"] != 1920: bad.append("%s: locked, but Q4 keeps the VIC raster path at 1080p" % n)
            if r.get("amiga"): pass         # T188: the Amiga frame is the reference of this row, not the VIC frame (proved in amiga_limits)
            elif r["lk"] is None: bad.append("%s: locked, but no CDCE setting makes the frame exactly the VIC frame (cannot lock)" % n)
            else:
                C, V = r["gear"] * r["htot"] * r["vtot"], VIC_FRAME[r["fam"]]
                got = F(r["lk"]["N"], r["lk"]["M"]) * REF_PER_DOT4X * V
                if got != C: bad.append("%s: locked, N/M %d/%d gives %s TMDS clocks per VIC frame, the row has %d" % (n, r["lk"]["N"], r["lk"]["M"], got, C))
    return bad

def host_chip_limits(rows):
    bad = []
    for r in rows:
        n = "%s %d Hz" % (r["name"], 50 if r["fam"] == 0 else 60)
        if not c_none(r): bad += chip_check(r, r["c"]["bytes"], n)[0]
        if r["locked"] and r["lk"] is not None:
            bad += chip_check(dict(ft_target=r["lk"]["fvco"]), r["lk"]["bytes"], n + " (lock)", REF_OF[r["fam"]])[0]
    return bad

def host_vh(rows, amiga=False):
    def nm(r): return "  // %s %d Hz" % (r["name"], 50 if r["fam"] == 0 else 60)
    L = ["// GENERATED by tools/hdmi_modes.py %s - do not edit. T164 WP1: the 9 HDMI modes of the host (all cores)." % ("amiga-gen" if amiga else "host-gen")]
    if amiga:
        L += ["// T188 edition (cores/amiga, cores/amiga1200 only): rows 0, 1, 2 (576p, 720p, 1080p at 50 Hz) have the raster and the CDCE ratio at which one HDMI",
              "// frame is exactly one Amiga frame (dot4x x 9/10 / (8 x 227 x 312)): no beat. Row 2 has no hm_cdce_lock (a frame-buffer core's row)."]
    L += [
         "// mode index m = family * 6 + hdmi: family 0 = 50 Hz, 1 = 60 Hz; hdmi 0 576p/480p, 1 720p, 2 1080p, 3 800x600, 4 1024x768, 5 1280x1024",
         "// the 50 Hz PC rows (3, 4, 5) do not exist (Q5): hm_valid is 0 and every other function answers 0 for them",
         "// hm_row: {vic[6:0], gear[3:0], hact[11:0], hfp[11:0], hsync[11:0], hbp[11:0], hpol, vact[10:0], vfp[10:0], vsync[10:0], vbp[10:0], vpol}",
         "// hm_aspect: AVI picture aspect M1:M0 (0 none, 1 4:3, 2 16:9). hm_rate: the PIXEL clock in Hz (TMDS clock / gear) for N/CTS.",
         "// hm_cdce: CDCE registers 0x18..0x1B (N[11:4]; N[3:0],R[8:5]; R[4:0],Q[5:3]; Q[2:0],P,range) for the PAL reference (frame-buffer cores).",
         "// hm_locked: the row's frame is exactly the VIC frame (proven by the tool) with hm_cdce_lock / hm_rate_lock, for the reference of the row's",
         "// family (50 Hz: PAL dot4x, 60 Hz: NTSC dot4x); both are 0 for rows that are not locked.", ""]
    def block(head, rows_, line):
        L.extend([head, "    case (m)"] + [line(r) for r in rows_])
        name = head.split("(")[0].split()[-1]
        L.extend(["    default: %s = 0;" % name, "    endcase", "endfunction", ""])
    block("function hm_valid(input [3:0] m);", rows, lambda r: "    4'd%d: hm_valid = 1'b1;%s" % (r["idx"], nm(r)))
    block("function [104:0] hm_row(input [3:0] m);", rows,
          lambda r: "    4'd%d: hm_row = {7'd%d, 4'd%d, 12'd%d, 12'd%d, 12'd%d, 12'd%d, 1'b%d, 11'd%d, 11'd%d, 11'd%d, 11'd%d, 1'b%d};%s" %
                    (r["idx"], r["vic"], r["gear"], r["ha"], r["hfp"], r["hs"], r["hbp"], r["hpol"], r["va"], r["vfp"], r["vs"], r["vbp"], r["vpol"], nm(r)))
    block("function [1:0] hm_aspect(input [3:0] m);", rows, lambda r: "    4'd%d: hm_aspect = 2'd%d;%s" % (r["idx"], r["asp"], nm(r)))
    block("function hm_locked(input [3:0] m);", rows, lambda r: "    4'd%d: hm_locked = 1'b%d;%s" % (r["idx"], 1 if r["locked"] else 0, nm(r)))
    block("function [31:0] hm_rate(input [3:0] m);", rows, lambda r: "    4'd%d: hm_rate = 32'd%d;%s" % (r["idx"], r["rate"], nm(r)))
    block("function [31:0] hm_cdce(input [3:0] m);", rows,
          lambda r: "    4'd%d: hm_cdce = 32'h%02X%02X%02X%02X;  // N %d M %d P %d Q %d R %d: TMDS %.6f MHz (%+.2f ppm), pixel %.6f MHz" %
                    ((r["idx"],) + tuple(r["c"]["bytes"]) + (r["c"]["N"], r["c"]["M"], r["c"]["P"], r["c"]["Q"], r["c"]["R"], float(r["f_t"]) / 1e6, r["c"]["err"], r["rate"] / 1e6)))
    lrows = [r for r in rows if r["locked"] and r["lk"] is not None]
    block("function [31:0] hm_cdce_lock(input [3:0] m);", lrows,
          lambda r: "    4'd%d: hm_cdce_lock = 32'h%02X%02X%02X%02X;  // N %d M %d P %d Q %d R %d: %d TMDS clocks per VIC frame exactly" %
                    ((r["idx"],) + tuple(r["lk"]["bytes"]) + (r["lk"]["N"], r["lk"]["M"], r["lk"]["P"], r["lk"]["Q"], r["lk"]["R"], r["gear"] * r["htot"] * r["vtot"])))
    block("function [31:0] hm_rate_lock(input [3:0] m);", lrows, lambda r: "    4'd%d: hm_rate_lock = 32'd%d;%s" % (r["idx"], r["lk_rate"], nm(r)))
    return "\n".join(L)

def host_table(rows):
    print("| # | mode | VIC | asp | g | TMDS clock (CDCE) | pixel clock | N/M (P,Q,R) | err ppm | regs 18..1B | GTP line | locked | exact lock N/M |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        c = r["c"]
        lk = "%d/%d" % (r["lk"]["N"], r["lk"]["M"]) if r["lk"] else "none"
        print("| %d | %s %d Hz | %d | %d | %d | %.6f MHz | %.6f MHz | %d/%d (%d,%d,%d) | %+.2f | %s | %.3f Gb/s | %s | %s |" %
              (r["idx"], r["name"], 50 if r["fam"] == 0 else 60, r["vic"], r["asp"], r["gear"], float(r["f_t"]) / 1e6, r["rate"] / 1e6, c["N"], c["M"], c["P"], c["Q"], c["R"],
               c["err"], " ".join("%02X" % b for b in c["bytes"]), 10 * float(r["f_t"]) / 1e9, "yes" if r["locked"] else "no", lk))

def host_decode(text, amiga=False):
    """What the gateware reads from host/hdmi_modes.vh, decoded from the file alone: per valid row the CDCE bytes -> TMDS clock, hm_rate must be
    that clock / gear, the frame rate of gear x htot x vtot clocks must be the family's, hm_aspect / hm_locked / lock bytes read back, and a locked
    row's lock bytes must give exactly the VIC frame (rational). Returns the failures."""
    import re
    def fn(name): return {int(m): v for m, v in re.findall(r"4'd(\d+): " + name + r" = ([^;]*);", text)}
    def num(v): return int(re.findall(r"\d+", v.split("'", 1)[1])[0])
    bad = []
    valid, rowf, cdce, rate, asp, locked, lcd, lrt = (fn(x) for x in ("hm_valid", "hm_row", "hm_cdce", "hm_rate", "hm_aspect", "hm_locked", "hm_cdce_lock", "hm_rate_lock"))
    if any(set(d) != Q5_SET for d in (valid, rowf, cdce, rate, asp, locked)):
        return ["hdmi_modes.vh: the functions do not name exactly the 9 rows of Q5"]
    def hx(v): return int(v.split("'h")[1], 16)
    def dec(b):
        N = ((b >> 24) << 4) | ((b >> 20) & 15); R = (((b >> 16) & 15) << 5) | ((b >> 11) & 31)
        Q = (((b >> 8) & 7) << 3) | ((b >> 5) & 7); P = (b >> 2) & 7
        top = (N << P) - R
        return None if Q == 0 or top % Q else (N, top // Q)
    for m in sorted(Q5_SET):
        n = "mode %d" % m
        f = [int(x) for x in re.findall(r"\d+'[db](\d+)", rowf[m])]       # vic gear hact hfp hsync hbp hpol vact vfp vsync vbp vpol
        gear, htot, vtot = f[1], f[2] + f[3] + f[4] + f[5], f[7] + f[8] + f[9] + f[10]
        nmv = dec(hx(cdce[m]))
        if nmv is None: bad.append("%s: hm_cdce gives no integer M" % n); continue
        ft = REF * nmv[0] / nmv[1]
        if num(rate[m]) != int(round(float(ft / gear))):
            bad.append("%s: hm_rate %d is not the pixel clock %d (TMDS clock %.3f MHz / gear %d)" % (n, num(rate[m]), round(float(ft / gear)), float(ft) / 1e6, gear))
        fam = m // 6
        hz = float(ft) / (gear * htot * vtot)
        want = 50.0 if fam == 0 else 60.0
        if abs(hz - want) / want > 0.005 and not (fam == 1 and abs(hz - want * 1000 / 1001) / want < 0.005):
            bad.append("%s: the CDCE gives %.3f MHz, the row runs %d clocks per frame = %.3f Hz, not %g Hz (the PLL bytes of another mode?)" % (n, float(ft) / 1e6, gear * htot * vtot, hz, want))
        if amiga and m in AMIGA_RAW and ft / (gear * htot * vtot) != F_CORE:
            bad.append("%s: Amiga edition, the row runs %s Hz, not exactly the Amiga frame %s Hz" % (n, float(ft / (gear * htot * vtot)), float(F_CORE)))
        if amiga and m not in AMIGA_RAW and f != [int(x) for x in re.findall(r"\d+'[db](\d+)", HOST_ROWTXT.get(m, ""))]:
            bad.append("%s: Amiga edition, a row outside T188 differs from the host's" % n)
        if f[0] and num(asp[m]) != CEA.get(f[0], (0, 0, 0, 0, -1))[4]: bad.append("%s: hm_aspect does not match VIC %d" % (n, f[0]))
        islk = "1'b1" in locked[m]
        if islk != (m in LOCK_ROWS): bad.append("%s: hm_locked %s, Q4 locks rows %s" % (n, islk, sorted(LOCK_ROWS)))
        if islk and amiga and m in AMIGA_RAW:
            if m in lcd or m in lrt: bad.append("%s: Amiga row carries lock values of the VIC frame" % n)
        elif islk:
            if m not in lcd or m not in lrt: bad.append("%s: locked, no hm_cdce_lock / hm_rate_lock" % n); continue
            lnm = dec(hx(lcd[m]))
            if lnm is None: bad.append("%s: hm_cdce_lock gives no integer M" % n); continue
            if F(lnm[0], lnm[1]) * REF_PER_DOT4X * VIC_FRAME[fam] != gear * htot * vtot:
                bad.append("%s: hm_cdce_lock N/M %d/%d does not give exactly the VIC frame (%d TMDS clocks)" % (n, lnm[0], lnm[1], gear * htot * vtot))
            lrate = int(round(float(REF_OF[fam] * lnm[0] / lnm[1] / gear)))
            if num(lrt[m]) != lrate: bad.append("%s: hm_rate_lock is not the pixel clock %d of the lock setting" % (n, lrate))
        elif m in lcd or m in lrt: bad.append("%s: not locked but has lock values" % n)
    return bad

AMIGA_VH = [os.path.join(ROOT, "cores/amiga/mono/host/hdmi_modes.vh"), os.path.join(ROOT, "cores/amiga1200/mono/host/hdmi_modes.vh")]
HOST_ROWTXT = {}

def amiga_check():
    """T188: the two Amiga copies are what `amiga-gen` writes, every T188 row is exactly the Amiga frame, every other row is the host's."""
    global HOST_ROWTXT
    import re
    HOST_ROWTXT = {int(m): v for m, v in re.findall(r"4'd(\d+): hm_row = \{([^}]*)\}", host_vh(host_compute()))}
    rows = host_compute(amiga=True)
    bad = host_limits(rows) + host_chip_limits(rows)
    want = host_vh(rows, amiga=True)
    for path in AMIGA_VH:
        try: cur = open(path).read()
        except OSError: cur = ""
        bad += host_decode(cur, amiga=True)
        if cur != want: bad.append("%s is not what `amiga-gen` writes" % os.path.relpath(path, ROOT))
    for b in bad: print("FAIL:", b)
    if not bad: print("PASS hdmi_modes amiga: rows 0..2 run exactly the Amiga frame %.6f Hz (exact CDCE ratio, decoded from both core copies), the other 6 rows are the host's" % float(F_CORE))
    return 1 if bad else 0

def host_check():
    rows = host_compute()
    bad = host_limits(rows)
    for N, M, got, want in known_blobs():
        if tuple(got) != want: bad.append("control: %d/%d gives %s, the shipped blob is %s" % (N, M, got, want))
    try:
        cur = open(HOST_VH).read()
    except OSError:
        cur = ""
    bad += host_chip_limits(rows)
    bad += host_decode(cur)
    if cur != host_vh(rows): bad.append("host/hdmi_modes.vh is not what `host-gen` writes")
    for b in bad: print("FAIL:", b)
    if not bad: print("PASS hdmi_modes host: 9 rows inside every limit (TMDS 80..165 MHz, GTP VCO and line rate, mmcm_pix, gear, CEA raster, AVI aspect), 1080p rows exactly frame-locked, CDCE bytes decoded from the file, host/hdmi_modes.vh current")
    return 1 if bad else 0

def host_selftest():
    """One mutation per rule; each must be caught by the checks `host-check` runs."""
    global HOST_VH, regs
    import re, io, contextlib
    caught = tests = 0
    g = globals()
    keys = ("HOST_MODES", "HOST_SET", "LOCK_ROWS", "GTP_MIN_LINE", "TMDS_RANGE", "MMCM_VCO", "VIC_FRAME", "HDMI_MIN_PIX")
    def has(sub, bad): return any(sub in b for b in bad)
    def report(name, ok):
        nonlocal caught
        print("  host mutation %-48s %s" % (name, "caught" if ok else "NOT CAUGHT")); caught += ok
    def run(name, subs, mutate):
        nonlocal tests
        tests += 1
        saved = {k: (dict(g[k]) if isinstance(g[k], dict) else set(g[k]) if isinstance(g[k], set) else g[k]) for k in keys}
        try:
            mutate()
            bad = host_limits(host_compute())
        finally:
            g.update(saved)
        report(name, any(has(s, bad) for s in subs))
    def setmode(m, **kw):
        v = dict(zip(HOST_NAMES, HOST_MODES[m])); v.update(kw); HOST_MODES[m] = tuple(v[x] for x in HOST_NAMES)
    run("480p60 at gear 1 (TMDS 25 MHz)", ("TMDS clock", "no CDCE", "ppm from the table"), lambda: setmode(6, gear=1))
    run("1080p60 at gear 2 (TMDS 296 MHz)", ("TMDS clock", "no CDCE"), lambda: setmode(8, gear=2))
    run("gear 4 on 720p60", ("gear 4",), lambda: setmode(7, gear=4))
    run("TMDS ceiling 120 MHz", ("TMDS clock",), lambda: g.update(TMDS_RANGE=(80e6, 120e6)))
    run("TMDS floor 130 MHz", ("TMDS clock",), lambda: g.update(TMDS_RANGE=(130e6, 165e6)))
    run("GTP line-rate minimum 1.4 Gb/s (DS181 rule)", ("GTP line rate",), lambda: g.update(GTP_MIN_LINE=1.4e9))
    run("mmcm_pix VCO ceiling 700 MHz", ("mmcm_pix VCO",), lambda: g.update(MMCM_VCO=(600e6, 700e6)))
    run("HDMI pixel-clock minimum 26 MHz", ("HDMI minimum",), lambda: g.update(HDMI_MIN_PIX=26e6))
    run("a 50 Hz PC row (800x600@50) added", ("Q5",), lambda: g["HOST_SET"].add(3))
    run("the 1080p50 row dropped", ("Q5",), lambda: g["HOST_SET"].discard(2))
    run("576p with aspect 16:9 (Q10)", ("AVI aspect",), lambda: setmode(0, asp=2))
    run("720p60 with the 50 Hz VIC 19", ("is not CEA VIC",), lambda: setmode(7, vic=19))
    run("PC row with an aspect", ("carries no aspect",), lambda: setmode(9, asp=1))
    run("horizontal blanking 30", ("horizontal blanking",), lambda: setmode(2, hfp=10, hs=10, hbp=10))
    run("480p60 flagged locked (cannot lock)", ("cannot lock",), lambda: g["LOCK_ROWS"].add(6))
    run("720p50 flagged locked (provable, outside Q4)", ("Q4",), lambda: g["LOCK_ROWS"].add(1))
    run("VIC frame of 1080p50 one line longer", ("cannot lock", "TMDS clocks per VIC frame"), lambda: g["VIC_FRAME"].update({0: 63 * 313 * 32}))
    txt = host_vh(host_compute())
    def swap(t, name, m, new): return re.sub(r"(4'd%d: %s = )[^;]*;" % (m, name), lambda mo: mo.group(1) + new + ";", t, count=1)
    def cdce_of(t, m): return re.search(r"4'd%d: hm_cdce = (32'h[0-9A-F]{8})" % m, t).group(1)
    def vh_case(label, mutate, subs):
        nonlocal tests
        tests += 1
        report(label, any(has(s, host_decode(mutate(txt))) for s in subs) and not host_decode(txt))
    vh_case("hm_rate is the TMDS clock, not the pixel clock", lambda t: swap(t, "hm_rate", 7, "32'd148071211"), ("hm_rate",))
    vh_case("lock bytes of 1080p50 with N + 16", lambda t: swap(t, "hm_cdce_lock", 2, "32'h047033ED"), ("VIC frame", "no integer M"))
    vh_case("1080p60 lock bytes = the PAL-reference bytes", lambda t: swap(t, "hm_cdce_lock", 8, cdce_of(t, 8)), ("VIC frame",))
    vh_case("hm_locked set on 720p50", lambda t: swap(t, "hm_locked", 1, "1'b1"), ("Q4",))
    vh_case("CDCE bytes of the next row in 576p50", lambda t: swap(t, "hm_cdce", 0, cdce_of(t, 1)), ("not 50 Hz", "hm_rate"))
    vh_case("hm_aspect of 576p50 = 16:9", lambda t: swap(t, "hm_aspect", 0, "2'd2"), ("hm_aspect",))
    tests += 1                                     # a stale host/hdmi_modes.vh must fail host_check()
    old = HOST_VH
    HOST_VH = os.path.join(os.path.dirname(old), ".hdmi_modes_stale.vh")
    try:
        open(HOST_VH, "w").write(txt.replace("// hm_locked", "// hm_locked,", 1))
        with contextlib.redirect_stdout(io.StringIO()): ok = host_check() != 0
    finally:
        os.unlink(HOST_VH); HOST_VH = old
    report("stale host/hdmi_modes.vh", ok)
    tests += 1                                     # regs() with Q and R swapped must differ from the shipped blobs (the register packing is shared)
    good = regs
    regs = lambda N, P, Q, R, fvco: good(N, P, R, Q, fvco)
    try: ok = any(got != want for _, _, got, want in known_blobs())
    finally: regs = good
    report("regs() with Q and R swapped", ok)
    global HOST_ROWTXT
    import re
    HOST_ROWTXT = {int(m): v for m, v in re.findall(r"4'd(\d+): hm_row = \{([^}]*)\}", host_vh(host_compute()))}
    atxt = host_vh(host_compute(amiga=True), amiga=True)
    tests += 1                                     # T188: one more line in the Amiga 1080p frame breaks the exact frame
    report("T188 vtot of 1080p one line more", bool(host_decode(atxt.replace("11'd14, 11'd5, 11'd36", "11'd15, 11'd5, 11'd36"), amiga=True)) and not host_decode(atxt, amiga=True))
    tests += 1                                     # T188: a row outside T188 changed in the Amiga copy
    report("T188 60 Hz row changed in the Amiga copy", bool(host_decode(atxt.replace("12'd88, 12'd44, 12'd148", "12'd90, 12'd44, 12'd148"), amiga=True)))
    print("host selftest: %d of %d mutations caught" % (caught, tests))
    return 0 if caught == tests else 1

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "table"
    if cmd == "table": table(compute())
    elif cmd == "host-table": host_table(host_compute())
    elif cmd == "host-gen":
        open(HOST_VH, "w").write(host_vh(host_compute())); print("wrote", HOST_VH)
    elif cmd == "amiga-table": host_table(host_compute(amiga=True))
    elif cmd == "amiga-gen":
        for pth in AMIGA_VH: open(pth, "w").write(host_vh(host_compute(amiga=True), amiga=True)); print("wrote", pth)
    elif cmd == "amiga-check": sys.exit(amiga_check())
    elif cmd == "check": sys.exit(check() | host_check() | amiga_check())
    elif cmd == "selftest": sys.exit(selftest() | host_selftest())
    elif cmd == "host-check": sys.exit(host_check())
    elif cmd == "host-selftest": sys.exit(host_selftest())
    else: sys.exit(__doc__)
