#!/usr/bin/env python3
"""TwoMegabyter on the board (T33 §4.2 B2): the test cartridge of build.sh, started
twice. Each start saves one highscore page through Protovision's flash API; the
app applies the flash operations to the image and writes the CRT back (Save
Changed Cartridge = Auto, in the running configuration only). The second start
loads the written-back file, so its counter is one higher.

    build.sh && tmb_board.py [--host H] [--dir /USB1/t33]

REST and FTP only (no JTAG).
"""
import argparse
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from easyflash_eapi_test import Board, Failure  # noqa: E402

CRT = HERE / "build" / "tmb_test.crt"
NAME = "tmb_test.crt"


def readmem(board, addr, n):
    return board.http("GET", "/v1/machine:readmem?address=%04X&length=%d" % (addr, n))


def run_once(board, number):
    board.http("PUT", "/v1/machine:writemem?address=07E7&data=00")
    board.run_crt(NAME)
    end = time.monotonic() + 30
    while readmem(board, 0x07E7, 1)[0] != 0xAA:
        if time.monotonic() > end:
            raise Failure("run %d: no end marker, $0400: %s" % (number, readmem(board, 0x0400, 4).hex()))
        time.sleep(0.5)
    r = readmem(board, 0x0400, 4)
    print("run %d: bank errors %d, mode errors %d, saves %d, save errors %d" % (number, r[0], r[1], r[2], r[3]))
    if r[0] or r[1] or r[3]:
        raise Failure("run %d: errors %s" % (number, r.hex()))
    return r[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=None)
    ap.add_argument("--dir", default="/USB1/t33")
    ap.add_argument("--wait", type=float, default=40.0, help="seconds for the write-back")
    a = ap.parse_args()
    kw = {"directory": a.dir, "jtag": False}
    if a.host:
        kw["host"] = a.host
    board = Board(**kw)
    board.upload(NAME, CRT.read_bytes())
    board.autosave_on()
    try:
        first = run_once(board, 1)
        print("waiting %.0f s for the write-back" % a.wait)
        time.sleep(a.wait)
        second = run_once(board, 2)
    finally:
        board.autosave_restore()
    ok = first == 1 and second == 2
    print("PASS" if ok else "FAILED: counters %d, %d (want 1, 2)" % (first, second))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
