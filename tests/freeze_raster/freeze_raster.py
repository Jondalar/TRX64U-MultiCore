#!/usr/bin/env python3
"""T12 test 7 on the board: a raster-timed program frozen and resumed N times
(TRX64-Ultimate/docs/specs/T12-freeze-overlay.md §8.2 item 7, §14.6).

    tests/freeze_raster/run.sh [host] [--freezes N] [--hold S] [--no-video]

freeze_raster.asm (64tass, tools/64tass.sh) goes to $C000 over JTAG
(tools/trxdbg.py: the C64 RAM's second port, no machine cycle) and is
started with SYS49152 through the keyboard buffer. It runs five colour
splits per frame from a stable raster interrupt and logs every split
(line, cycle in the frame from a CIA 1 timer, its frame counter, the
machine's frame counter from CIA 2) to a ring at $4000, which this script
reads over JTAG, never with a bus cycle.

The freeze is the app's: REST PUT /v1/machine:menu_button opens the menu
(Interface Type = Freeze, HDMI attached: the TRX64 bitstream's machine pause
with a freeze session) and closes it again. Per freeze it checks
  - the session: GATE_CTRL halted while the menu is up, SH_GCLK and SH_REAL
    unchanged (no C64 clock edge, no bus cycle), the commit wrote nothing
    (SH_COMMIT 0). On the slim core (TX_CTRL b31:24 = 2, SLIM-state-path.md) there is
    no session and no counter: only the halt is checked, the menu's accesses are bus
    cycles and the machine runs a few thousand cycles for them, so the log's split
    cycles are compared as before but this test is the one to look at after SLIM;
  - the log across the freeze: every split at the same line and the same
    cycle as before (CIA 1), one record per split, the program's frame
    counter and the machine's (CIA 2 timer B) both one frame on from the last
    record before the freeze to the first after it (a CPU-only pause would
    let CIA 2 count the frames the CPU missed);
  - the picture: a frame of the UDP video stream (1541ultimate repo
    tests/e2e/lib/streams.py decodes it) after the resume equals the one
    before the first freeze pixel for pixel; the colour splits' positions
    (first pixel of each new border colour) are printed.
Exit code 0 = all freezes passed.
"""
import importlib.util
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import trxdbg  # noqa: E402

LOG, LOG_N, REC = 0x4000, 2048, 8
CNT, FRM = 0x00FB, 0x00FD
NSPLIT = 5
LINES = [56, 96, 136, 176, 216]
BORDER = [2, 5, 6, 7, 14]
GATE_CTRL, GATE_HALTED = 0x0100, 0x100
SH_CTRL, SH_STATUS, SH_ACC, SH_REAL, SH_GCLK, SH_UNMAPPED, SH_LIST, SH_COMMIT = (
    0x5000, 0x5004, 0x5008, 0x500C, 0x5010, 0x5014, 0x5018, 0x501C)
TX_CTRL = 0x60C0
VIDEO_PORT = 11000
STREAMS = os.environ.get("U64_E2E_LIB",
                         os.path.expanduser("~/Development/1541ultimate/tests/e2e/lib/streams.py"))


def assemble():
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "out.bin")
        subprocess.run([os.path.join(ROOT, "tools", "64tass.sh"), "-q", "-b", "-o", out,
                        os.path.join(HERE, "freeze_raster.asm")], check=True)
        return open(out, "rb").read()


def rest(host, method, path):
    req = urllib.request.Request(f"http://{host}{path}", method=method)
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read().decode(errors="replace")


# ---- the log over JTAG --------------------------------------------------------------
def ram(d, addr, n):
    return bytes(v & 0xFF for v in d.read_many(list(range(addr, addr + n))))


def count(d):
    b = ram(d, CNT, 2)
    return b[0] | (b[1] << 8)


def records(d, first, last):
    """Records first..last-1 (counts, as written; at most LOG_N back)."""
    out = []
    n = last - first
    i = first
    while n > 0:
        k = min(n, LOG_N - i % LOG_N)
        raw = ram(d, LOG + (i % LOG_N) * REC, k * REC)
        for j in range(k):
            r = raw[j * REC:(j + 1) * REC]
            out.append({"n": i + j, "line": r[0], "ta": r[1] | (r[2] << 8), "frm": r[3] | (r[4] << 8),
                        "hw": r[5] | (r[6] << 8), "split": r[7]})
        i += k
        n -= k
    return out


def total(d, last_total):
    """The record count without the 2048 wrap, given the last one seen."""
    c = count(d)
    t = last_total - (last_total % LOG_N) + c
    if t < last_total:
        t += LOG_N
    return t


def steady(recs):
    """Per split: (line, timer) of a run of records, and whether they agree."""
    ref = {}
    bad = []
    for r in recs:
        key = (r["line"], r["ta"])
        if r["split"] not in ref:
            ref[r["split"]] = key
        elif ref[r["split"]] != key:
            bad.append(r)
    return ref, bad


def check_run(recs, ref):
    """Every record against the reference; one record per split in turn; from a
    record to the one five later (the same split a frame on) both frame
    counters move by exactly one (CIA 2's underflow is anywhere in the frame,
    so between two splits of one frame it may or may not have come)."""
    errs = []
    for a, b in zip(recs, recs[1:]):
        if b["split"] != (a["split"] + 1) % NSPLIT:
            errs.append(f"record {b['n']}: split {b['split']} after {a['split']} (missed or extra IRQ)")
    for a, b in zip(recs, recs[NSPLIT:]):
        if b["n"] - a["n"] != NSPLIT:
            continue
        if (b["frm"] - a["frm"]) & 0xFFFF != 1:
            errs.append(f"record {b['n']}: frame counter {a['frm']} -> {b['frm']} over one frame")
        if (a["hw"] - b["hw"]) & 0xFFFF != 1:
            errs.append(f"record {b['n']}: machine frames {a['hw']:04X} -> {b['hw']:04X}"
                        f" ({(a['hw'] - b['hw']) & 0xFFFF} for one: the machine ran without the CPU)")
    for r in recs:
        if (r["line"], r["ta"]) != ref.get(r["split"]):
            errs.append(f"record {r['n']}: split {r['split']} at line {r['line']} cycle"
                        f" {19655 - r['ta']}, want {ref.get(r['split'])}")
    return errs


# ---- the picture -------------------------------------------------------------------------
def load_streams():
    lib_dir = os.path.dirname(STREAMS)
    sys.path.insert(0, lib_dir)
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(lib_dir)), "lib"))
    spec = importlib.util.spec_from_file_location("u64_streams", STREAMS)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Video:
    def __init__(self, host):
        self.host = host
        self.streams = load_streams()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", VIDEO_PORT))
        self.sock.settimeout(2.0)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((host, 80))
        me = s.getsockname()[0]
        s.close()
        rest(host, "PUT", f"/v1/streams/video:start?ip={me}:{VIDEO_PORT}")

    def frame(self, skip=3):
        """The pixels of a whole frame (384 x 272, one colour index per byte),
        after `skip` complete frames have gone by."""
        asm = self.streams.FrameAssembler()
        got = 0
        t = time.time()
        while time.time() - t < 10:
            try:
                data, sender = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            if sender[0] != self.host:
                continue
            f = asm.push(data)
            if f is None:
                continue
            got += 1
            if got > skip:
                return f.width, f.height, self.streams.unpack(f.packed)
        return None

    def close(self):
        try:
            rest(self.host, "PUT", "/v1/streams/video:stop")
        finally:
            self.sock.close()


def splits(w, h, px):
    """For each split's border colour, in order down the frame: the first row
    below the previous split whose left edge has it, and the first pixel in
    that colour (on the row above when the change is mid-line)."""
    out = []
    y0 = 0
    for c in BORDER:
        pos = None
        for y in range(y0, h):
            row = px[y * w:(y + 1) * w]
            if row[0] == c:
                # the change is on the row above (mid-line) or this one
                x = 0
                if y > 0:
                    above = px[(y - 1) * w:y * w]
                    if c in above:
                        y, x = y - 1, above.index(c)
                pos = (y, x)
                y0 = y + 2
                break
        out.append(pos)
    return out


def main(argv):
    host = os.environ.get("TRX64_HOST", "192.0.2.189")
    freezes, hold, video_on = 10, 2.0, True
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--freezes":
            freezes = int(argv[i + 1]); i += 1
        elif a == "--hold":
            hold = float(argv[i + 1]); i += 1
        elif a == "--no-video":
            video_on = False
        else:
            host = a
        i += 1

    code = assemble()
    print(f"freeze_raster.asm: {len(code)} bytes; board {host}")
    itype = rest(host, "GET", "/v1/configs/User%20Interface%20Settings/Interface%20Type")
    if '"current" : "Freeze"' not in itype:
        print("Interface Type is not Freeze (the menu would not freeze the machine)")
        return 2
    d = trxdbg.Dbg()
    video = None
    fails = 0
    try:
        d.write_bytes(0xC000, code)
        if ram(d, 0xC000, len(code)) != code:
            print("program not in RAM")
            return 2
        text = "SYS49152\r"
        for k, ch in enumerate(text):
            d.write(0x0277 + k, ord(ch))
        d.write(0x00C6, len(text))
        time.sleep(1.5)
        seen = total(d, 0)
        if seen < 50:
            print(f"the program does not run ({seen} records)")
            return 2
        time.sleep(1.0)
        t = total(d, seen)
        base = records(d, t - 250, t)
        ref, bad = steady(base)
        print("steady state (250 records, 50 frames): " + ", ".join(
            f"split {s} line {ref[s][0]} cycle {19655 - ref[s][1]}" for s in sorted(ref)))
        if bad or len(ref) != NSPLIT or check_run(base, ref):
            print(f"FAIL: no steady state: {len(bad)} records off, {check_run(base, ref)[:3]}")
            return 1
        if video_on:
            video = Video(host)
            v0 = video.frame()
            if v0 is None:
                print("no video frame; continuing without the picture check")
                video.close()
                video = None
            else:
                print(f"picture before: {v0[0]}x{v0[1]}, splits (row, x): {splits(*v0)}")
        slim = (d.trx_read(TX_CTRL) >> 24) >= 2     # no freeze session, no counters
        print("core: " + ("slim, no freeze session" if slim else "with the freeze session"))
        last = t
        for k in range(1, freezes + 1):
            before = total(d, last)
            real0, gclk0 = d.trx_read(SH_REAL), d.trx_read(SH_GCLK)
            sessions0 = d.trx_read(SH_STATUS) >> 16
            rest(host, "PUT", "/v1/machine:menu_button")
            t0 = time.time()
            while time.time() - t0 < 5 and not (d.trx_read(GATE_CTRL) & GATE_HALTED):
                time.sleep(0.05)
            halted = bool(d.trx_read(GATE_CTRL) & GATE_HALTED)
            time.sleep(hold)
            st = d.trx_read(SH_STATUS)              # the app opens the session after the stop
            in_menu = total(d, before)
            lst = d.trx_read(SH_LIST)
            real1, gclk1 = d.trx_read(SH_REAL), d.trx_read(SH_GCLK)
            rest(host, "PUT", "/v1/machine:menu_button")
            t1 = time.time()
            while time.time() - t1 < 5 and (d.trx_read(GATE_CTRL) & GATE_HALTED):
                time.sleep(0.05)
            resumed = not (d.trx_read(GATE_CTRL) & GATE_HALTED)
            time.sleep(1.0)
            after = total(d, in_menu)
            commit = d.trx_read(SH_COMMIT) & 0xFFFF
            real2 = d.trx_read(SH_REAL)
            recs = records(d, before - 50, after)
            errs = check_run(recs, ref)
            if not halted:
                errs.append("the machine did not stop for the menu")
            if not slim:
                if not (st & 1) or (st >> 16) != sessions0 + 1:
                    errs.append(f"no freeze session (SH_STATUS {st:08X})")
                if gclk1 != gclk0 or real1 != real0:
                    errs.append(f"in the menu: {gclk1 - gclk0} C64 clock edges, {real1 - real0} bus cycles")
                if commit != 0 or real2 != real1:
                    errs.append(f"the commit wrote {commit} bus cycles ({real2 - real1} real)")
            if not resumed:
                errs.append("the machine did not go on")
            pic = ""
            if video:
                v = video.frame()
                if v is None:
                    errs.append("no video frame after the resume")
                else:
                    diff = sum(1 for a, b in zip(v[2], v0[2]) if a != b)
                    sp = splits(*v)
                    pic = f", picture: {diff} pixels differ, splits {sp}"
                    if diff or sp != splits(*v0):
                        errs.append(f"picture differs: {diff} pixels, splits {sp}")
            frames_in_log = (after - before) // NSPLIT
            print(f"freeze {k}: halted {halted}, session {st >> 16}, menu {hold:.1f} s, list {lst & 0xFF}"
                  f" entries, commit {commit}; records {before}..{after} ({len(recs)} checked,"
                  f" {frames_in_log} frames){pic}: {'ok' if not errs else 'FAIL'}")
            for e in errs[:6]:
                print("   " + e)
            fails += bool(errs)
            last = after
        print(f"{freezes - fails} of {freezes} freezes without a deviation")
        return 0 if fails == 0 else 1
    finally:
        if video:
            video.close()
        d.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
