#!/usr/bin/env python3
"""Wolfgang Lorenz's 6510/CIA test suite (VICE testprogs, lorenz-2.15) on a
running c64_basic, over JTAG. The Ultimate app must have booted first: the
bitstream holds no ROM, the app writes them and lets the C64 out of reset.

    tests/run_lorenz.sh [disk ...] [--only NAME ...] [--timeout S] [--old-cia]

Each test is a BASIC-started program that prints its name, runs, prints
"- ok" and then loads the next test from drive 8. We have no drive here, so
every test is run on its own: reset the CPU, wait for READY, put the .prg in
RAM, type RUN, and read the screen until the test passes ("- OK"; without a
drive its load of the next test then ends in "LOAD ERROR"), prints a failure
(FAILED, BEFORE/AFTER/RIGHT, WRONG, EXPECTED: fail, it then waits for a key),
or the time runs out.

The disk images come from $LORENZ_DIR (default: the copy in the
C64ReverseEngineeringMCP samples). Results go to stdout and to
.cache/lorenz/results.txt.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import trxdbg  # noqa: E402
import d64     # noqa: E402

LORENZ_DIR = os.environ.get("LORENZ_DIR", os.path.expanduser(
    "~/Development/C64/Tools/C64ReverseEngineeringMCP/samples/vice-testprogs/lorenz-2.15"))
CTRL = trxdbg.REG_BASE + 9
SKIP = {"START", "FINISH", "TEMPLATE"}  # the chain's loader, end and a skeleton


def screen_lines(d, rows=range(25)):
    """The given text rows (others come back empty): after a reset READY is on
    row 5, RUN on row 6 and the test writes from row 7 on, so reading rows
    4-15 is enough and four times faster than the whole screen."""
    out = [""] * 25
    for r in rows:
        data = [d.read(0x0400 + r * 40 + i) & 0xFF for i in range(40)]
        out[r] = "".join(trxdbg.screen_char(c) for c in data).rstrip()
    return out


TEST_ROWS = range(4, 16)


def reset(d):
    # Blank the rows READY will appear on while the CPU is held, so the READY
    # of the previous run cannot be mistaken for the new one (loading before
    # the KERNAL's cold start finishes would be undone by its NEW).
    d.write(CTRL, 1)
    d.write_bytes(0x0400 + 4 * 40, bytes([0x20]) * 120)
    time.sleep(0.05)
    d.write(CTRL, 0)
    t = time.time()
    while time.time() - t < 5:
        time.sleep(0.5)
        if any(line.startswith("READY.") for line in screen_lines(d, range(4, 7))):
            return True
    return False


def run_one(d, name, prg, timeout):
    if not reset(d):
        return "no READY after reset", []
    # TRAP16/17 skip ANE/LXA after `jsr ram` and go straight back to
    # waitborder with $01 = all RAM, so it polls the RAM under $D011/$D012:
    # a left-over value there (bit 7 clear, $D012 >= 30) keeps it waiting for
    # good. Clear the RAM under the I/O area, as after a power-up with VICE's
    # pattern ($00 at $D000-$D03F).
    d.write_bytes(0xD000, bytes(0x1000))
    load = prg[0] | prg[1] << 8
    body = prg[2:]
    d.write_bytes(load, body)
    end = load + len(body)
    for zp in (0x2D, 0x2F, 0x31):
        d.write(zp, end & 0xFF)
        d.write(zp + 1, end >> 8)
    for i, ch in enumerate("RUN\r"):
        d.write(0x0277 + i, ord(ch))
    d.write(0x00C6, 4)
    t = time.time()
    lines = []
    # not "ERROR": the KERNAL's "?DEVICE NOT PRESENT ERROR" comes just before
    # a passing test's "LOAD ERROR!"
    fail_words = ("FAILED", "BEFORE", "AFTER", "RIGHT", "WRONG", "EXPECTED")
    while time.time() - t < timeout:
        time.sleep(1)
        lines = screen_lines(d, TEST_ROWS)
        text = [l.upper() for l in lines]
        # A test that passed prints "- OK" and loads the next one from drive
        # 8; without a drive that ends in "LOAD ERROR", with one it loads on.
        # A failing test prints what went wrong and waits for a key instead.
        if any(l.endswith("- OK") or "LOAD ERROR" in l for l in text):
            return "ok", lines
        if any(w in l for l in text for w in fail_words):
            time.sleep(1)
            return "FAIL", screen_lines(d, TEST_ROWS)
    return "timeout", lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("disks", nargs="*", type=int, default=[1, 2, 3, 4])
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--timeout", type=float, default=240)
    # c64_core has 8521 CIAs: disk 3 (the CIA tests) is Disk3new.d64 from VICE's
    # testprogs/general/Lorenz-2.15, unless the core is built with 6526s
    ap.add_argument("--old-cia", action="store_true", help="Disk3.d64 (6526) instead of Disk3new.d64")
    ap.add_argument("--failed", action="store_true",
                    help="only the tests that did not pass in the last results.txt")
    args = ap.parse_args()

    redo = None
    if args.failed:
        prev = open(os.path.join(ROOT, ".cache", "lorenz", "results.txt")).read().splitlines()
        redo = {l.split()[1].upper() for l in prev if l.startswith("disk") and l.split()[2] != "ok"}
    # The last file on a disk carries the name of the next disk's first test
    # but is NEXTDISK ("please insert disk"): a later file of the same name wins.
    found = {}
    for n in args.disks:
        path = os.path.join(LORENZ_DIR, f"Disk{n}{'new' if n == 3 and not args.old_cia else ''}.d64")
        if not os.path.exists(path):
            sys.exit(f"{path} missing (svn export https://svn.code.sf.net/p/vice-emu/code/"
                     "testprogs/general/Lorenz-2.15 and set LORENZ_DIR)")
        img = open(path, "rb").read()
        for name, t, s in d64.entries(img):
            if name.upper() not in SKIP:
                found[name.upper()] = (n, name, d64.chain(img, t, s))
    tests = []
    for key, test in found.items():
        if args.only and key not in (o.upper() for o in args.only):
            continue
        if redo is not None and key not in redo:
            continue
        tests.append(test)

    os.makedirs(os.path.join(ROOT, ".cache", "lorenz"), exist_ok=True)
    report = open(os.path.join(ROOT, ".cache", "lorenz", "results.txt"), "w")
    d = trxdbg.Dbg()
    counts = {}
    try:
        for disk, name, prg in tests:
            t = time.time()
            result, lines = run_one(d, name, prg, args.timeout)
            counts[result] = counts.get(result, 0) + 1
            row = f"disk{disk} {name:10s} {result:8s} {time.time() - t:6.1f} s"
            print(row, flush=True)
            report.write(row + "\n")
            if result != "ok":
                shown = [l for l in lines if l.strip()]
                for l in shown[-8:]:
                    report.write("    | " + l + "\n")
            report.flush()
    finally:
        d.close()
    summary = ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    print(summary)
    report.write(summary + "\n")
    return 0 if set(counts) <= {"ok"} else 1


if __name__ == "__main__":
    sys.exit(main())
