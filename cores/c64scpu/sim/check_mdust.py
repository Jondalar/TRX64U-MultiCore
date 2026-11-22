#!/usr/bin/env python3
"""T208: the checks of run_mdust.sh (spec T208 section 2 S1-S3). Reads the log of one bench run (tb_c64scpu.v lines "mark|", "mchk|", "mirq|", "iow|") and prints "mdust| ..." lines and one PASS or
FAIL line; exit 1 on FAIL.
  check_mdust.py <label> <log>      label: menu20 | menu1 | scroll | hud | hudx | stz | smc
  menu20/menu1  S3  one handler entry (mark 0, "frame N") per frame: no frame twice, none skipped, as many handler ends (mark 1) as entries (a handler cut off by the end of the run is the one exception)
  scroll        S1  mchk: 0 wrong against the VIC slot, 0 wrong in the DDR2 at the end, n_wlost 0; mirror writes accepted by the queue = landed on the C64 bus (+-2: the ones in flight at the end)
  hud           S1+S2  the same, and the PHI2 cycles from every `$D011 = $6B` write to the next `$D011 = $3B` write at most (writes in between + 1) on every chain, the cold first one included; hudx is the same stream with its bank-2 source on the stage's cache index (the code is evicted every frame)
  smc           S2  the code cache's next-line prefetch against self-modifying code: marker 1 (every result of every pass right), no marker 2, marker 3 (end)
  stz           S2  the PHI2 cycle gaps of back-to-back I/O stores against the VICE accounting (vice_model in this file)"""
import re
import sys


def read(log):
    return open(log, errors="replace").read().splitlines()


class R:
    def __init__(self, label):
        self.label, self.bad = label, []

    def chk(self, tag, ok, text):
        print("mdust| %s %s %s: %s" % (self.label, "ok  " if ok else "FAIL", tag, text))
        if not ok:
            self.bad.append(tag)

    def done(self):
        print(("FAIL" if self.bad else "PASS") + " mdust " + self.label + (" (" + ", ".join(self.bad) + ")" if self.bad else ""))
        return 1 if self.bad else 0


def check_menu(r, lines):
    m0, m1 = [], []
    for ln in lines:
        g = re.match(r"mark\|(\d)\|(\d+)\|frame (\d+)", ln)
        if g:
            (m0 if g.group(1) == "0" else m1 if g.group(1) == "1" else []).append((int(g.group(3)), int(g.group(2))))
    fr = [f for f, _ in m0]
    r.chk("handler runs", len(fr) >= 24, "%d handler entries (mark 0) in the run" % len(fr))
    dup = sorted(set(f for f in fr if fr.count(f) > 1))
    r.chk("once per frame", not dup, "frames with two handler entries: %s" % (dup if dup else "none"))
    gaps = [b - a for a, b in zip(fr, fr[1:]) if b - a != 1]
    r.chk("no frame skipped", not gaps, "frame steps other than 1 between entries: %s" % (gaps if gaps else "none"))
    tail = len(m0) - len(m1)
    r.chk("every entry ends", tail in (0, 1), "%d handler entries, %d handler ends (mark 1; one entry at most may be cut off by the end of the run)" % (len(m0), len(m1)))
    if r.label == "menu1":
        sw = [ln for ln in lines if re.match(r"mark\|2\|", ln)]
        r.chk("speed switch reached", bool(sw), "the switch to 1 MHz (mark 2): %s" % (sw[0] if sw else "not reached"))
        end = [ln for ln in lines if re.match(r"mark\|3\|", ln)]
        r.chk("run reached its end", bool(end), "mark 3 (24 handler runs counted by the program): %s" % ("reached" if end else "not reached"))
    return r


def check_mirror(r, lines):
    mc = [re.match(r"mchk\|(\d+) mirrored writes seen, (\d+) addresses checked against the VIC slot, (\d+) wrong, (\d+) wrong in the DDR2 at the end, n_wlost (\d+)", ln) for ln in lines]
    mc = [x for x in mc if x]
    if not mc:
        r.chk("mchk line", False, "no mchk line in the log (the run did not finish or +MIRCHK is missing)")
        return r
    seen, chk, wrong, ddr, lost = (int(x) for x in mc[-1].groups())
    r.chk("mirror writes seen", seen > 20000 and chk > 10000, "%d mirrored writes seen, %d addresses checked against the VIC slot" % (seen, chk))
    r.chk("VIC copy", wrong == 0, "%d wrong against the VIC slot" % wrong)
    r.chk("DDR2 copy", ddr == 0, "%d wrong in the DDR2 at the end" % ddr)
    r.chk("n_wlost", lost == 0, "n_wlost %d" % lost)
    q = [re.match(r"mirq\|(\d+) mirror writes accepted by the queue, (\d+) landed", ln) for ln in lines]
    q = [x for x in q if x]
    if q:
        a, l = int(q[-1].group(1)), int(q[-1].group(2))
        r.chk("accepted = landed", abs(a - l) <= 2, "%d accepted by the queue, %d landed on the C64 bus" % (a, l))
    else:
        r.chk("accepted = landed", False, "no mirq line")
    return r


def iow_list(lines):
    out = []
    for ln in lines:
        g = re.match(r"iow\|([0-9a-f]{4})\|([0-9a-f]{2})\|dirq (-?\d+)\|dclk (-?\d+)\|cyc (\d+)", ln)
        if g:
            out.append((g.group(1), g.group(2), int(g.group(5)), int(g.group(4))))
    return out


def check_hud(r, lines):
    io = iow_list(lines)
    pairs = []
    i = 0
    while i < len(io):
        if io[i][0] == "d011" and io[i][1] == "6b":
            j = i + 1
            while j < len(io) and not (io[j][0] == "d011" and io[j][1] == "3b"):
                j += 1
            if j < len(io):
                pairs.append((j - i, io[j][2] - io[i][2], io[i][2]))
            i = j
        i += 1
    r.chk("HUD chains seen", len(pairs) >= 5, "%d chains from $D011=$6B to $D011=$3B" % len(pairs))
    if len(pairs) < 2:
        return r
    cold, warm = pairs[0], pairs[1:]
    r.chk("first chain (cold code)", cold[1] <= cold[0] + 1, "%d writes in %d PHI2 cycles (at most writes + 1: the next-line prefetch of scpu_cache.v refills the stage's code lines ahead of the stores)" % (cold[0], cold[1]))
    bad = [p for p in warm if p[1] > p[0] + 1]
    worst = max(warm, key=lambda p: p[1] - p[0])
    best = min(warm, key=lambda p: p[1] - p[0])
    text = "%d of %d later chains slower than writes + 1; worst %d writes in %d PHI2 cycles, best %d writes in %d" % (len(bad), len(warm), worst[0], worst[1], best[0], best[1])
    # hudx: the scroll's reads from SuperRAM bank 2 sit on the cache index of the stage's code (direct-mapped, address bits 14:4 since T241 D4: both are below $2000 in their bank, so they still meet), the code is evicted between two frames
    r.chk("write spacing" + (" (code evicted every frame)" if r.label == "hudx" else ""), not bad, text)
    return r


def vice_model(prog):
    """A port of the VICE accounting (vice/src/scpu64/scpu64cpu.c: scpu64_clock_inc, scpu64_clock_write_stretch_io_start / _io, SHIFT -2000000, maincpu_diff = 985248 PAL) over a list of
    instructions ("stz" or "sta": 3 fetch cycles + the store; "imm": 2 fetch cycles). Returns the 1 MHz clock (maincpu_clk) at which each I/O store reaches the chip, with no BA stealing and
    no write buffer in play (screen off, the stores go to the VIC)."""
    D, WRAP, SHIFT = 985248, 20000000, -2000000
    st = {"clk": 0, "accu": 0}

    def inc():                                   # scpu64_clock_inc(): one 20 MHz cycle of the CPU
        st["accu"] += D
        if st["accu"] > WRAP:
            st["accu"] -= WRAP
            st["clk"] += 1
    out = []
    for op in prog:
        for _ in range(2 if op == "imm" else 3):
            inc()
        if op == "imm":
            inc()                                # the operand/result cycle of an immediate load: 2 cycles in all
            continue
        if st["accu"] >= SHIFT + WRAP:           # scpu64_clock_write_stretch_io_start: the store waits for the 1 MHz slot
            st["clk"] += 1
        out.append(st["clk"])
        st["accu"] = 11500000 + SHIFT            # scpu64_clock_write_stretch_io: the slot is used, "measured"
        st["clk"] += 1
        inc()                                    # store_long(): the store's own 20 MHz cycle
    return out


def check_stz(r, lines):
    io = [x for x in iow_list(lines) if x[0].startswith("d0") and x[1] == "00"]
    n = 24
    runs = [io[-k * n:len(io) - (k - 1) * n] for k in (3, 2, 1)] if len(io) >= 3 * n else []
    r.chk("stores seen", len(runs) == 3, "%d STZ stores to $D0xx seen (%d wanted: three calls of the 24-store routine)" % (len(io), 3 * n))
    if not runs:
        return r
    clk = vice_model(["stz"] * n)
    model = [b - a for a, b in zip(clk, clk[1:])]
    print("mdust| stz VICE : gaps %s, total %d PHI2 cycles" % (",".join(map(str, model)), sum(model)))
    for name, run in zip(("cold (1st call)", "warm (2nd call)", "warm (3rd call)"), runs):
        gaps = [b[2] - a[2] for a, b in zip(run, run[1:])]
        print("mdust| stz ours %s: gaps %s, total %d PHI2 cycles" % (name, ",".join(map(str, gaps)), sum(gaps)))
    warm = [[b[2] - a[2] for a, b in zip(run, run[1:])] for run in runs[1:]]
    r.chk("warm total", all(sum(g) <= sum(model) + 1 for g in warm), "warm calls: %s PHI2 cycles first to last store, VICE model %d (at most +1)" % ("/".join(str(sum(g)) for g in warm), sum(model)))
    r.chk("warm gaps", all(max(g) <= 1 for g in warm), "largest gap in a warm call: %d PHI2 cycles" % max(max(g) for g in warm))
    cold = [b[2] - a[2] for a, b in zip(runs[0], runs[0][1:])]
    # the code cache's refills (DDR2, ~50 clk2x a 16-byte line) would add one PHI2 cycle per line the VICE accounting does not have (bank-0 fetches never wait there); the next-line prefetch
    # of scpu_cache.v (T208) brings the next line in while the stores of this one run, so the cold call costs at most the first line
    r.chk("cold total", sum(cold) <= sum(model) + 1, "cold call: %d PHI2 cycles first to last store, VICE model %d (at most +1; %d gaps over 1: code cache line refills, 3 bytes a store, 16 a line)" % (sum(cold), sum(model), sum(1 for g in cold if g > 1)))
    return r


def check_stars(r, lines, chain_only=False):
    """T208 board trace 2026-11-14 (spec section 5): the stars' raster IRQ handler on the badlines against the VICE accounting (scpu64cpu.c: scpu64_clock_read_stretch_io steals the cycles when BA is low = from line
    cycle 12 on a badline; scpu64_clock_write_stretch_io_start steals when maincpu_clk >= maincpu_ba_low_start = BA low + 3 = cycle 15; the VIC gives the CPU the bus back at cycle 55). So on a badline no I/O access
    of the CPU may complete in cycles 15-54 (the writes) / 12-54 (the reads). Only handlers that start before cycle 10 and are not nested in the music ISR (the first I/O access before cycle 12) are judged."""
    cur, hand = None, {}
    for ln in lines:
        g = re.match(r"irq\|(\d+)\|y (\d+)\|x (\d+)\|fr (\d+)", ln)
        if g:
            cur = (int(g.group(2)), int(g.group(4)), int(g.group(1)))
            hand[cur] = []
            continue
        g = re.match(r"(iow|ior)\|(\w+)\|\w+\|dirq (-?\d+)\|dclk (-?\d+)\|cyc (\d+)\|y (\d+)\|x (\d+)", ln)
        if g and cur:
            hand[cur].append((g.group(1), g.group(2), int(g.group(6)) * 63 + int(g.group(7)) // 8 - cur[0] * 63))
    # the chain itself: the raster IRQ every 3 lines, every frame (a write the VIC drops while AEC is low leaves the compare line stale and the chain sparse)
    frames = {}
    for k in hand:
        frames[k[1]] = frames.get(k[1], 0) + 1
    full = sorted(frames)
    r.chk("raster chain runs every frame", bool(full) and all(frames[f] == 35 for f in full), "IRQs per frame (frames 0..%d): %s (35 expected: lines 129..231 every 3rd)" % (max(frames), " ".join(str(frames[f]) for f in full)))
    if chain_only:                  # starsq60: the dispatcher is long, the I/O starts after cycle 12 on every line: only the chain is judged
        return r
    bad = [(k, v) for k, v in hand.items() if (k[0] & 7) == 3 and v and v[0][2] < 12 and k[1] >= 1]
    r.chk("badline handlers", len(bad) >= 8, "%d badline raster IRQ handlers that start their I/O before cycle 12 (of %d handlers)" % (len(bad), len(hand)))
    inside = {}
    for k, v in bad:
        for kind, a, c in v:
            if (kind == "iow" and 15 <= c <= 54) or (kind == "ior" and 12 <= c <= 54):
                inside.setdefault((kind, a, c), 0)
                inside[(kind, a, c)] += 1
    ex = sorted(inside.items())
    r.chk("no I/O access while the VIC has the bus", not inside, "%d I/O accesses of %d handlers complete in the VIC's window (VICE: 0, they wait to cycle 55); %s" % (sum(inside.values()), len(bad), ", ".join("%s $%s @%d x%d" % (a[0][0][2:], a[0][1], a[0][2], a[1]) for a in ex[:8]) or "none"))
    if not bad:
        return r
    norm = [(k, v) for k, v in hand.items() if (k[0] & 7) != 3 and v and v[0][2] < 12 and k[1] >= 1 and k[0] % 3 == 0]
    def med(vs, i):
        c = sorted(v[i][2] for v in vs if len(v) > i)
        return c[len(c) // 2] if c else -1
    bad_v, norm_v = [v for k, v in bad], [v for k, v in norm]
    ops = [(x[0], x[1]) for x in bad[0][1]]
    for nm, grp in (("badline", [v for k, v in bad]), ("normal ", [v for k, v in norm])):
        if grp:
            print("mdust| stars table %s (%d handlers, median PHI2 cycle of each I/O access): %s" % (nm, len(grp), " ".join("%s$%s@%d" % (ops[i][0][2], ops[i][1], med(grp, i)) for i in range(min(len(ops), min(len(v) for v in grp))))))
    n = min(len(ops), min(len(v) for v in bad_v), min(len(v) for v in norm_v)) if norm else 0
    if n:
        # hand model of the VICE accounting for the same code: an access that the normal line completes before cycle 15 lands at the same cycle on the badline (the 3 cycles BA gives the CPU: +-1);
        # every later one waits for the VIC's window to end (cycle 55, the first one) and keeps its distance to the first held one: badline = normal + 40 (55 - 15), +3 for the clock the enable needs after BA rises
        errs = []
        for i in range(n):
            nc, bc = med(norm_v, i), med(bad_v, i)
            if nc < 15 and abs(bc - nc) > 1:
                errs.append("#%d normal %d badline %d (want the same +-1)" % (i, nc, bc))
            if nc >= 15 and not 40 <= bc - nc <= 43:
                errs.append("#%d normal %d badline %d (want +40..43)" % (i, nc, bc))
        r.chk("badline = normal line + hold (VICE hand model)", not errs, "%d I/O accesses compared; %s" % (n, "; ".join(errs[:4]) or "all within the model"))
    tails = sorted(v[-1][2] for k, v in bad)
    first = sorted(v[0][2] for k, v in bad)
    print("mdust| stars info    badline handlers: first I/O access at cycle %s, last (the $D011 write) at %s; the same on the other lines: see the log" % (first[len(first) // 2], tails[len(tails) // 2]))
    return r


def check_smc(r, lines):
    m = {n: [ln for ln in lines if re.match(r"mark\|%d\|" % n, ln)] for n in (1, 2, 3)}
    r.chk("run reached its end", bool(m[3]), "marker 3 (all %d passes done): %s" % (8, m[3][0] if m[3] else "not reached"))
    r.chk("no stale code line executed", bool(m[1]) and not m[2], "marker 1 (every result of every pass right) %d x, marker 2 (a result of an older pass) %d x" % (len(m[1]), len(m[2])))
    return r


def main():
    label, log = sys.argv[1], sys.argv[2]
    lines = read(log)
    r = R(label)
    fin = [ln for ln in lines if ln.startswith("PASS tb_c64scpu") or ln.startswith("FAIL tb_c64scpu")]
    r.chk("bench finished", bool(fin) and fin[-1].startswith("PASS"), fin[-1] if fin else "no PASS/FAIL line from the testbench (the run did not finish)")
    if label in ("menu20", "menu1"):
        check_menu(r, lines)
    elif label == "scroll":
        check_mirror(r, lines)
    elif label in ("hud", "hudx"):
        check_mirror(r, lines)
        check_hud(r, lines)
    elif label == "stz":
        check_stz(r, lines)
    elif label == "smc":
        check_smc(r, lines)
    elif label in ("stars", "stars60"):
        check_stars(r, lines, label == "stars60")
    else:
        r.chk("label", False, "unknown label " + label)
    return r.done()


sys.exit(main())
