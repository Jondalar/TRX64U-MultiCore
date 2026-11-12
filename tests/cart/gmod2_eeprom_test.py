#!/usr/bin/env python3
"""E2E: the GMod2 93C86 serial EEPROM, bit-banged through $DE00, persists.

The cartridge model emulates the EEPROM (vendor/icepi/c64_eeprom_93c86.v). The
stimulus (gmod2_eeprom_write.asm) enables writes, writes four words, reads
them back with one sequential READ, and on the next load finds them (or, if
they are already there, erases the chip with ERAL and expects $FFFF).
Three loads therefore cover write, sequential read, ERAL and the write-back
in both directions.

TRX64 Ultimate port: the Ultimate app keeps the EEPROM inside the CRT, as a
2 KiB CHIP packet at load address $DE00 (io/c64/c64_crt.cc, find_eeprom adds
a blank one when the file has none), not in a .eep beside it; the bitstream
reports every EEPROM write to the app, which writes the CRT back like a
flash change. So the write-back check reads the app's console line and then
the $DE00 packet of the CRT fetched over FTP (byte 2n is the high half of
word n, c64_eeprom_93c86.v). The board harness is easyflash_eapi_test.Board.

Usage: `install` puts gmod2_eeprom.crt on the board, `run` drives the board.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from easyflash_eapi_test import (Board, Failure, add_board_arguments, alternate,  # noqa: E402
                                 assemble as assemble_source, check, check_writeback, chip,
                                 crt_packets, whole_chip)
from flash_amd_test import CARTS, ROUTINE_PAGES, bank0                # noqa: E402

SOURCE = HERE / "gmod2_eeprom_write.asm"
CRT_NAME = "gmod2_eeprom.crt"

RESULT = 0x0400
STATE_DONE = 0x44
STATUS_WRITTEN = 1
STATUS_PERSISTED = 2
DONE_TIMEOUT_SECONDS = 15.0
COMMIT_TIMEOUT_SECONDS = 15.0

# Shared with gmod2_eeprom_write.asm: words FIRST_WORD.. and their values.
FIRST_WORD = 5
WORDS = bytes([0x12, 0x34, 0xA5, 0x5A, 0x00, 0xFF, 0x7F, 0x80])
EEPROM_KEY = (0, 0xDE00)


def assemble() -> bytes:
    routine = assemble_source(SOURCE)
    if len(routine) > ROUTINE_PAGES * 0x100:
        raise Failure("the stimulus is too long")
    return routine


def build() -> bytes:
    cart = CARTS["gmod2"]
    header = bytearray(0x40)
    header[0x00:0x10] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (60).to_bytes(2, "big")    # GMod2
    header[0x18], header[0x19] = 0, 1              # 8K
    header[0x20:0x31] = b"GMOD2-EEPROM-TEST"
    return bytes(header) + chip(0, 0x8000, bank0(cart, assemble()))


def run_once(board: Board, number: int) -> int:
    print(f"load {number}: run_crt {board.path(CRT_NAME)}")
    board.start(CRT_NAME, 6)
    end = time.monotonic() + DONE_TIMEOUT_SECONDS
    while True:
        r = board.ram(RESULT, 6)
        if r[0] == STATE_DONE:
            break
        if time.monotonic() > end:
            raise Failure(f"stimulus not done: {r.hex()}")
        time.sleep(0.5)

    status = r[1]
    ok = check("status", status in (STATUS_WRITTEN, STATUS_PERSISTED),
               f"{status} (word 5 was ${r[4]:02X}{r[5]:02X})")
    ok &= check("words read back", r[2] == 0, f"mismatches={r[2]}")
    ok &= check("ready after programming", r[3] == 0, f"timeouts={r[3]}")
    ok &= check_writeback(board, COMMIT_TIMEOUT_SECONDS)
    image = board.download(CRT_NAME)
    if image is not None:
        eeprom = crt_packets(image).get(EEPROM_KEY)
        want = WORDS if status == STATUS_WRITTEN else b"\xff" * len(WORDS)
        got = eeprom[2 * FIRST_WORD:2 * FIRST_WORD + len(WORDS)] if eeprom else None
        ok &= check("EEPROM packet in the CRT file", got == want,
                    got.hex() if got is not None else "no $DE00 packet")
        wrong = whole_chip(image, 64, 1, eeprom=True)
        ok &= check("whole chip in the CRT file", not wrong, wrong or f"{len(image)} bytes")
    if not ok:
        raise Failure(f"load {number} failed")
    return status


def run(board: Board) -> None:
    board.autosave_on()
    try:
        alternate([run_once(board, n) for n in (1, 2, 3)])
        print("PASS: EEPROM write and ERAL both survived the CRT write-back")
    finally:
        board.autosave_restore()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="action", required=True)
    p_build = sub.add_parser("build")
    p_build.add_argument("output", type=Path)
    p_install = sub.add_parser("install")
    p_run = sub.add_parser("run")
    for p in (p_install, p_run):
        add_board_arguments(p)
    args = parser.parse_args()
    try:
        if args.action == "build":
            args.output.write_bytes(build())
            print(f"wrote {args.output}")
        elif args.action == "install":
            board = Board(args.host, args.dir, jtag=False)
            board.upload(CRT_NAME, build())
            print(f"wrote {board.path(CRT_NAME)} on {args.host}")
        else:
            board = Board(args.host, args.dir)
            try:
                run(board)
            finally:
                board.close()
    except Failure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
