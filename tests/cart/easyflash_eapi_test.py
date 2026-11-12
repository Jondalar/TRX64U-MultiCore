#!/usr/bin/env python3
"""E2E: EasyFlash writes through the stock EAPI reach the CRT file and come back.

Ported from the icepi-zero-c64 suite (tests/e2e/cart) to the TRX64 Ultimate:
the cartridge model is the same (vendor/icepi/c64_cartridge.v with
c64_flash_amd.v), the board harness is new. The bitstream's flash responder
answers command sequences, autoselect and status itself and stops at every
program or erase until the Ultimate app has applied it to the image in DDR2
(the fork of the Ultimate app, io/c64/trx_cart.cc; not vendored); the app's auto-save task writes
the changed image back to the .crt it was started from, 2 s after the last
change (30 s at most), or before a reset.

The suite builds its own one-bank CRT type 32 in code and assembles its 6502
stimulus from the source beside it (ef_eapi_write.asm) with tools/64tass.sh.
The stimulus calls the cartridge's own, unmodified EAPI (eapi-am29f040, from
TRX_CRT_cli); with the TRX64 bitstream the app does not swap it for its own.

The board harness (class Board):

- `install` puts the CRT on the board's storage over FTP (a directory on
  the first USB drive by default) and removes the .bak an earlier write-back left there. The
  cartridge has to run from a file there: an upload through POST run_crt lands
  in /Temp, which the app never writes back to.
- `run` sets "Save Changed Cartridge" to Auto (runtime only, never saved to
  flash; the old value is put back afterwards), starts the CRT with
  PUT /v1/runners:run_crt?file=..., reads the result block in C64 RAM and the
  cartridge image in DDR2 (byte 0x1000000, the expansion-port block shared with the REU, 16 KiB per bank,
  ROMH at +8 KiB)
  over JTAG (tools/trxdbg.py), and follows the app's console over JTAG for its
  write-back line. After the write-back it fetches the CRT over FTP and checks
  the file itself.

Checked by `run`:

- EAPIInit recognises the chip (manufacturer $01, device $A4, 64 banks).
- Sector erase on ROML and ROMH leaves the whole sector blank.
- EAPIWriteFlash programs a pattern into both chips and it reads back.
- The image in DDR2 holds the pattern (or the blank sector).
- The app writes the changed image back to the CRT file (console line), and
  the file fetched over FTP holds what the image holds, as the whole chip:
  64 banks of ROML and ROMH in bank order, erased ones included. The test
  bank is not in the CRT that `install` puts there.
- After loading the file again the pattern is still there (write persisted);
  the stimulus then erases it, and after the next load it is gone (erase
  persisted). run_crt resets the machine into the cartridge from the file.

Because a run leaves the file in the other state, two loads always cover both
directions: the first load writes or erases, the second sees the result from
the file and does the opposite. A third load proves the second direction from
the file as well, without reading the file back: on the TRX64 board bulk
transfers from the board stall (FTP RETR and large HTTP replies alike), so the
FTP check of the file is reported as SKIP when the transfer fails.
"""

from __future__ import annotations

import argparse
import ftplib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SOURCE = HERE / "ef_eapi_write.asm"

CRT_NAME = "ef_eapi_test.crt"

# The stock EAPI (eapi-am29f040 V1.4 by Thomas Giesel, from the EasyFlash
# project) as a .prg with its $C000 load address. Pass it with --eapi, or set
# EAPI_IMAGE; the default is where TRX_CRT_cli keeps its copy.
EAPI_IMAGE = Path(os.environ.get(
    "EAPI_IMAGE",
    Path(os.environ.get("TRX_CRT_CLI_DIR", Path.home() / "Development/C64/Tools/TRX_CRT_cli"))
    / "templates/boot/eapi-am29f040-14"))
EAPI_OFFSET = 0x1800                # $B800 in bank 0 ROMH
EAPI_LENGTH = 768

ROUTINE_ADDRESS = 0x0800
ROUTINE_OFFSET = 0x0100             # of the routine in bank 0 ROMH ($E100 in Ultimax)
ROUTINE_MAX = 0x0200                # the boot code copies two pages

CHUNK_DATA = 0x2000

# Shared with ef_eapi_write.asm.
RESULT = 0x0400
RESULT_LEN = 0x0b
STATE_DONE = 0x44
STATUS_WRITTEN = 1
STATUS_PERSISTED = 2
TEST_BANK = 8
PATTERN = bytes([0xA5, 0x5A, 0xC3, 0x3C, 0x0F, 0xF0, 0x81, 0x7E,
                 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0x01])

# The board: the C64 Ultimate with the TRX64 bitstream and the Ultimate app.
HOST = os.environ.get("TRX64_HOST", "192.0.2.189")
BOARD_DIR = os.environ.get("TRX64_CART_DIR", "")   # empty: trx64-cart on the first USB drive, else SD
BOARD_SUBDIR = "trx64-cart"
CART_IMAGE = 0x1000000              # DDR2 byte address of the cartridge image: the app's expansion-port block (T60 §7, trx_io ROM_SEL 1; 0x3C00000 is Gideon's)
BANK_STRIDE = 0x4000                # 16 KiB per bank, ROML at +0, ROMH at +8 KiB (io/c64/c64_crt.cc)
CFG_CATEGORY = "C64 and Cartridge Settings"
CFG_AUTOSAVE = "Save Changed Cartridge"
# io/c64/c64_subsys.cc: the auto-save task and the reset path
WRITEBACK = re.compile(r"\[CRT\] write-back(?: before reset)?: ([^\r\n]*)")

DONE_TIMEOUT_SECONDS = 10.0
COMMIT_TIMEOUT_SECONDS = 20.0       # 2 s quiet period, then the file onto the stick


class Failure(Exception):
    pass


# ---------------------------------------------------------------------------
# The cartridge
# ---------------------------------------------------------------------------

def assemble(source: Path, defines: dict[str, str] | None = None) -> bytes:
    """The assembled stimulus, without the .prg load address (tools/64tass.sh)."""
    command = [str(ROOT / "tools" / "64tass.sh"), "-q", "--cbm-prg"]
    for name, value in (defines or {}).items():
        command += ["-D", f"{name}={value}"]
    with tempfile.TemporaryDirectory(prefix="c64-asm-") as directory:
        output = Path(directory) / "fixture.prg"
        result = subprocess.run([*command, "-o", str(output), str(source)],
                                capture_output=True, text=True, check=False)
        if result.returncode:
            raise Failure(f"64tass failed: {(result.stderr or result.stdout).strip()[:400]}")
        program = output.read_bytes()
    if program[:2] != ROUTINE_ADDRESS.to_bytes(2, "little"):
        raise Failure(f"the stimulus does not load at ${ROUTINE_ADDRESS:04X}")
    return program[2:]


def boot() -> bytes:
    """Bank 0 at $E000 in Ultimax mode: copy two pages of routine to RAM and run it."""
    src = 0xE000 + ROUTINE_OFFSET
    return bytes([
        0x78,                                   # E000  SEI
        0xA2, 0xFF,                             # E001  LDX #$FF
        0x9A,                                   # E003  TXS
        0xD8,                                   # E004  CLD
        0xA2, 0x00,                             # E005  LDX #0
        0xBD, src & 0xFF, src >> 8,             # E007  LDA $E100,X
        0x9D, 0x00, ROUTINE_ADDRESS >> 8,       # E00A  STA $0800,X
        0xBD, src & 0xFF, (src >> 8) + 1,       # E00D  LDA $E200,X
        0x9D, 0x00, (ROUTINE_ADDRESS >> 8) + 1, # E010  STA $0900,X
        0xE8,                                   # E013  INX
        0xD0, 0xF1,                             # E014  BNE $E007
        0x4C, ROUTINE_ADDRESS & 0xFF, ROUTINE_ADDRESS >> 8,  # E016  JMP $0800
        0x40,                                   # E019  RTI       NMI and IRQ vectors
    ])


def eapi(path: Path) -> bytes:
    if not path.is_file():
        raise Failure(f"no EAPI image at {path}; pass --eapi")
    image = path.read_bytes()
    if image[:2] != b"\x00\xc0" or len(image) != EAPI_LENGTH + 2:
        raise Failure(f"{path} is not a 768-byte EAPI .prg for $C000")
    if image[2:6] != b"eapi":
        raise Failure(f"{path} has no EAPI signature")
    return image[2:]


def chip(bank: int, load: int, data: bytes) -> bytes:
    """A CRT CHIP packet of type 2 (flash)."""
    return (b"CHIP" + (0x10 + len(data)).to_bytes(4, "big") + (2).to_bytes(2, "big")
            + bank.to_bytes(2, "big") + load.to_bytes(2, "big") + len(data).to_bytes(2, "big")
            + data)


def easyflash_crt(routine: bytes, eapi_image: bytes) -> bytes:
    """A one-bank type 32 CRT that boots into `routine` and carries the stock EAPI."""
    if len(routine) > ROUTINE_MAX:
        raise Failure(f"the stimulus is {len(routine)} bytes, the boot copies {ROUTINE_MAX}")

    header = bytearray(0x40)
    header[0x00:0x10] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (32).to_bytes(2, "big")     # EasyFlash
    header[0x18] = 1                                # EXROM high, GAME low: Ultimax at start
    header[0x19] = 0
    header[0x20:0x2C] = b"EF-EAPI-TEST"

    roml = bytearray(b"\xff" * CHUNK_DATA)
    roml[0:12] = b"EF-EAPI-ROML"

    romh = bytearray(b"\xff" * CHUNK_DATA)
    start = boot()
    romh[0:len(start)] = start
    romh[ROUTINE_OFFSET:ROUTINE_OFFSET + len(routine)] = routine
    romh[EAPI_OFFSET:EAPI_OFFSET + EAPI_LENGTH] = eapi_image
    rti = 0xE000 + len(start) - 1
    for vector in (0x1FFA, 0x1FFE):                 # NMI, IRQ
        romh[vector:vector + 2] = rti.to_bytes(2, "little")
    romh[0x1FFC:0x1FFE] = (0xE000).to_bytes(2, "little")      # RESET

    return bytes(header) + chip(0, 0x8000, bytes(roml)) + chip(0, 0xA000, bytes(romh))


def build(eapi_path: Path) -> bytes:
    return easyflash_crt(assemble(SOURCE), eapi(eapi_path))


def crt_packets(image: bytes) -> dict[tuple[int, int], bytes]:
    """The CHIP packets of a CRT file by (bank, load address)."""
    if image[:16] != b"C64 CARTRIDGE   ":
        raise Failure("the file is not a CRT")
    pos = max(int.from_bytes(image[0x10:0x14], "big"), 0x40)
    packets = {}
    while pos + 0x10 <= len(image) and image[pos:pos + 4] == b"CHIP":
        length = int.from_bytes(image[pos + 4:pos + 8], "big")
        bank = int.from_bytes(image[pos + 10:pos + 12], "big")
        load = int.from_bytes(image[pos + 12:pos + 14], "big")
        size = int.from_bytes(image[pos + 14:pos + 16], "big")
        packets[(bank, load)] = image[pos + 0x10:pos + 0x10 + size]
        pos += length
    return packets


def file_bytes(packets: dict[tuple[int, int], bytes], bank: int, load: int, length: int) -> bytes | None:
    """The first bytes of a packet, None if the file has no packet for it."""
    data = packets.get((bank, load))
    return data[:length] if data is not None else None


def whole_chip(image: bytes, banks: int, chips: int, eeprom: bool = False) -> str:
    """What is wrong with a written-back CRT as the whole chip: the app writes one
    8 KiB packet per bank and chip in bank order, erased banks included, then a
    GMod2's 2 KiB EEPROM packet, and nothing else. Empty when it is right."""
    want = [(bank, 0x8000 + chip * 0x2000, 0x2000) for bank in range(banks) for chip in range(chips)]
    if eeprom:
        want.append((0, 0xDE00, 0x800))
    got = []
    pos = 0x40
    while pos + 0x10 <= len(image) and image[pos:pos + 4] == b"CHIP":
        got.append((int.from_bytes(image[pos + 10:pos + 12], "big"), int.from_bytes(image[pos + 12:pos + 14], "big"),
                    int.from_bytes(image[pos + 14:pos + 16], "big")))
        pos += int.from_bytes(image[pos + 4:pos + 8], "big")
    size = 0x40 + sum(0x10 + n for _, _, n in want)
    if got != want or len(image) != size:
        first = next((i for i, (g, w) in enumerate(zip(got, want)) if g != w), min(len(got), len(want)))
        return (f"{len(image)} bytes, want {size}; {len(got)} packets, want {len(want)}, "
                f"first difference at packet {first}")
    return ""


# ---------------------------------------------------------------------------
# The board
# ---------------------------------------------------------------------------

def _trxdbg():
    sys.path.insert(0, str(ROOT / "tools"))
    import trxdbg  # noqa: E402  (needs the JTAG venv: tests/run_cart_tests.sh)
    return trxdbg


class Board:
    """The C64 Ultimate with the TRX64 bitstream: REST and FTP to the Ultimate app,
    JTAG (tools/trxdbg.py) for C64 RAM, the DDR2 image and the app's console."""

    def __init__(self, host: str = HOST, directory: str = BOARD_DIR, jtag: bool = True):
        self.host = host
        self.dir = directory.rstrip("/") or self._default_dir()
        self.log = ""
        self.mark = 0
        self.autosave_before = None
        self.dbg = None
        if jtag:
            self.dbg = _trxdbg().Dbg()
            self.con = self.dbg.read(_trxdbg().CON_COUNT)    # only what comes from now on

    def close(self) -> None:
        if self.dbg:
            self.dbg.close()
            self.dbg = None

    def _default_dir(self) -> str:
        """The drives are named after their slot (USB0, USB1, SD), so look."""
        ftp = self._ftp()
        try:
            drives = [d.strip("/") for d in ftp.nlst("/")]
        finally:
            ftp.quit()
        for drive in sorted(d for d in drives if d.upper().startswith("USB")) + \
                [d for d in drives if d.upper() == "SD"]:
            return f"/{drive}/{BOARD_SUBDIR}"
        raise Failure(f"no USB drive or SD card on the board (FTP root: {drives})")

    def path(self, name: str) -> str:
        return f"{self.dir}/{name}"

    # REST ------------------------------------------------------------------

    def http(self, method: str, path: str, body: bytes | None = None) -> bytes:
        req = urllib.request.Request(f"http://{self.host}{path}", data=body, method=method)
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as exc:
            raise Failure(f"{method} {path}: HTTP {exc.code} {exc.read()[:200]!r}") from None

    def _cfg_path(self) -> str:
        q = urllib.parse.quote
        return f"/v1/configs/{q(CFG_CATEGORY)}/{q(CFG_AUTOSAVE)}"

    def autosave(self) -> str:
        reply = json.loads(self.http("GET", self._cfg_path()))
        item = reply.get(CFG_CATEGORY, {}).get(CFG_AUTOSAVE)
        if isinstance(item, dict):
            item = item.get("current", item.get("value"))
        if item is None:
            raise Failure(f"no {CFG_AUTOSAVE!r} in {reply}")
        return str(item)

    def set_autosave(self, value: str) -> None:
        self.http("PUT", f"{self._cfg_path()}?value={urllib.parse.quote(value)}")

    def autosave_on(self) -> None:
        """Auto for this run, in the running configuration only (never saved to flash)."""
        self.autosave_before = self.autosave()
        if self.autosave_before != "Auto":
            self.set_autosave("Auto")
        if self.autosave() != "Auto":
            raise Failure(f"{CFG_AUTOSAVE} did not go to Auto")

    def autosave_restore(self) -> None:
        if self.autosave_before and self.autosave_before != "Auto":
            self.set_autosave(self.autosave_before)

    def run_crt(self, name: str) -> None:
        self.http("PUT", f"/v1/runners:run_crt?file={urllib.parse.quote(self.path(name))}")

    # FTP -------------------------------------------------------------------

    def _ftp(self) -> ftplib.FTP:
        ftp = ftplib.FTP(self.host, timeout=10)
        ftp.login()
        return ftp

    def upload(self, name: str, data: bytes, stale: tuple[str, ...] = ()) -> None:
        """Put the file into the board directory and remove what an earlier run left
        (NAME.bak from the first write-back, NAME.tmp from a broken one)."""
        ftp = self._ftp()
        try:
            try:
                ftp.mkd(self.dir)
            except ftplib.error_perm:
                pass                                # already there
            ftp.storbinary(f"STOR {self.path(name)}", io.BytesIO(data))
            for other in (f"{name}.bak", f"{name}.tmp", *stale):
                try:
                    ftp.delete(self.path(other))
                except ftplib.error_perm:
                    pass
        finally:
            ftp.quit()

    def download(self, name: str) -> bytes | None:
        """The file, or None when the transfer fails (seen on the TRX64 board: bulk
        transfers from the board stall after the first segments, FTP and HTTP alike).
        TRX64_SKIP_FILE=1 skips it: the next load then shows whether the write-back
        reached the file."""
        if os.environ.get("TRX64_SKIP_FILE"):
            print(f"  SKIP CRT file over FTP: {self.path(name)} (TRX64_SKIP_FILE)")
            return None
        out = io.BytesIO()
        try:
            ftp = self._ftp()
            try:
                ftp.retrbinary(f"RETR {self.path(name)}", out.write)
            finally:
                ftp.close()
        except (OSError, EOFError, ftplib.Error) as exc:
            print(f"  SKIP CRT file over FTP: {self.path(name)}: {exc!r} after {len(out.getvalue())} bytes")
            return None
        return out.getvalue()

    # JTAG ------------------------------------------------------------------

    def poll_console(self) -> str:
        raw, self.con = self.dbg.console(self.con)
        text = raw.decode("latin-1").replace("\r", "")
        self.log += text
        return text

    def ram(self, address: int, length: int) -> bytes:
        self.poll_console()
        return bytes(v & 0xFF for v in self.dbg.read_many(range(address, address + length)))

    def clear_ram(self, address: int, length: int) -> None:
        self.dbg.write_bytes(address, bytes(length))

    def image(self, offset: int, length: int) -> bytes:
        """Bytes of the cartridge image in DDR2."""
        first = (CART_IMAGE + offset) & ~3
        words = [self.dbg.ddr32_read(a) for a in range(first, CART_IMAGE + offset + length, 4)]
        raw = b"".join(w.to_bytes(4, "little") for w in words)
        skip = CART_IMAGE + offset - first
        return raw[skip:skip + length]

    def start(self, name: str, clear: int) -> None:
        """Clear the result block (a stale 'D' from the last run must not count), mark
        the console, start the cartridge from its file."""
        self.clear_ram(RESULT, clear)
        self.poll_console()
        self.mark = len(self.log)
        self.run_crt(name)

    def wait_writeback(self, seconds: float) -> str:
        """The app's write-back line after the mark: 'done' or its error."""
        end = time.monotonic() + seconds
        while True:
            self.poll_console()
            match = WRITEBACK.search(self.log, self.mark)
            if match:
                self.mark = match.end()
                print(f"  console: {match.group(0).strip()}")
                return match.group(1).strip()
            if time.monotonic() > end:
                raise Failure(f"no write-back within {seconds:.0f} s; console: {self.log[-300:]!r}")
            time.sleep(0.25)


def check(name: str, condition: bool, detail: str = "") -> bool:
    print(f"  {'OK  ' if condition else 'FAIL'} {name}{(': ' + detail) if detail else ''}")
    return condition


def alternate(statuses: list[int], name: str = "") -> None:
    """Each load must find what the one before left in the file. Load 2 proves the
    first direction, load 3 the second, without reading the file back."""
    for n in range(1, len(statuses)):
        before, now = statuses[n - 1], statuses[n]
        expected = STATUS_PERSISTED if before == STATUS_WRITTEN else STATUS_WRITTEN
        if now != expected:
            raise Failure(f"{name}{': ' if name else ''}load {n + 1} reported status {now}, expected "
                          f"{expected}: the {'write' if before == STATUS_WRITTEN else 'erase'} "
                          f"of load {n} did not survive the file")


def check_writeback(board: Board, seconds: float) -> bool:
    try:
        outcome = board.wait_writeback(seconds)
        return check("written back to the CRT file", outcome == "done", outcome)
    except Failure as exc:
        return check("written back to the CRT file", False, str(exc))


def result_block(board: Board) -> dict[str, int]:
    raw = board.ram(RESULT, RESULT_LEN)
    names = ["state", "status", "init_c", "init_a", "init_x", "init_y",
             "erase_lo", "erase_hi", "write_err", "verify_err", "blank_err"]
    return dict(zip(names, raw))


def load_and_wait(board: Board) -> dict[str, int]:
    board.start(CRT_NAME, RESULT_LEN)
    end = time.monotonic() + DONE_TIMEOUT_SECONDS
    while True:
        result = result_block(board)
        if result["state"] == STATE_DONE:
            return result
        if time.monotonic() > end:
            raise Failure(f"stimulus not done after {DONE_TIMEOUT_SECONDS:.0f} s: {result}")
        time.sleep(0.5)


def run_once(board: Board, number: int) -> int:
    print(f"load {number}: run_crt {board.path(CRT_NAME)}")
    r = load_and_wait(board)
    ok = True
    ok &= check("EAPIInit", r["init_c"] == 0 and r["init_x"] == 0x01 and r["init_a"] == 0xA4,
                f"C={r['init_c']} mfr=${r['init_x']:02X} dev=${r['init_a']:02X} banks={r['init_y']}")
    ok &= check("status", r["status"] in (STATUS_WRITTEN, STATUS_PERSISTED), f"{r['status']}")
    ok &= check("sector erase", r["erase_lo"] == 0 and r["erase_hi"] == 0 and r["blank_err"] == 0,
                f"C lo={r['erase_lo']} hi={r['erase_hi']}, non-blank bytes={r['blank_err']}")

    base = TEST_BANK * BANK_STRIDE
    roml, romh = board.image(base, len(PATTERN)), board.image(base + CHUNK_DATA, len(PATTERN))
    if r["status"] == STATUS_WRITTEN:
        ok &= check("program + read back", r["write_err"] == 0 and r["verify_err"] == 0,
                    f"write errors={r['write_err']}, mismatches={r['verify_err']}")
        want_l, want_h = PATTERN, bytes(b ^ 0xFF for b in PATTERN)
        ok &= check("pattern in DDR2 image", roml == want_l and romh == want_h,
                    f"ROML {roml.hex()} ROMH {romh.hex()}")
    else:
        want_l = want_h = b"\xff" * len(PATTERN)
        ok &= check("sector blank in DDR2 image", roml == want_l and romh == want_h,
                    f"ROML {roml.hex()} ROMH {romh.hex()}")

    ok &= check_writeback(board, COMMIT_TIMEOUT_SECONDS)
    image = board.download(CRT_NAME)
    if image is not None:
        packets = crt_packets(image)
        fl = file_bytes(packets, TEST_BANK, 0x8000, len(PATTERN))
        fh = file_bytes(packets, TEST_BANK, 0xA000, len(PATTERN))
        ok &= check("CRT file over FTP", fl == want_l and fh == want_h,
                    f"ROML {fl.hex() if fl else None} ROMH {fh.hex() if fh else None}")
        wrong = whole_chip(image, 64, 2)
        ok &= check("whole chip in the CRT file", not wrong, wrong or f"{len(image)} bytes")

    if not ok:
        raise Failure(f"load {number} failed")
    return r["status"]


def run(board: Board) -> None:
    board.autosave_on()
    try:
        statuses = [run_once(board, n) for n in (1, 2, 3)]
        alternate(statuses)
        print("PASS: write and erase both survived the CRT write-back")
    finally:
        board.autosave_restore()


def add_board_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--host", default=HOST, help="the board (default: %(default)s, or TRX64_HOST)")
    parser.add_argument("--dir", default=BOARD_DIR,
                        help="directory on the board for the CRTs (default: TRX64_CART_DIR, or "
                             f"{BOARD_SUBDIR} on the first USB drive, else on the SD card)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="action", required=True)
    p_build = sub.add_parser("build", help="write the test CRT to a file")
    p_build.add_argument("output", type=Path)
    p_install = sub.add_parser("install", help="put the test CRT on the board over FTP")
    p_run = sub.add_parser("run", help="run the test on the board (REST, FTP, JTAG)")
    for p in (p_install, p_run):
        add_board_arguments(p)
    for p in (p_build, p_install):
        p.add_argument("--eapi", type=Path, default=EAPI_IMAGE,
                       help="stock eapi-am29f040 .prg (default: %(default)s)")
    args = parser.parse_args()

    try:
        if args.action == "build":
            args.output.write_bytes(build(args.eapi))
            print(f"wrote {args.output}")
        elif args.action == "install":
            board = Board(args.host, args.dir, jtag=False)
            board.upload(CRT_NAME, build(args.eapi))
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
