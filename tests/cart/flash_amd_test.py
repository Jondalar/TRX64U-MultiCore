#!/usr/bin/env python3
"""E2E: raw AMD flash commands on every cartridge type the core emulates.

The companion of easyflash_eapi_test.py. That suite goes through the stock
EAPI; this one talks to the chips with bare command sequences, the way any
driver ends up doing, so one stimulus (flash_amd_write.asm) covers EasyFlash,
GMod2, C64MegaCart and MegaByter. Per cartridge the harness only changes the
64tass defines (registers, flash mode, unlock cycles, write window, test
banks) and the CRT it builds around the stimulus.

Checked per chip, on three consecutive loads of the same file:

- autoselect returns the part's manufacturer and device ID, at the address
  the part uses (byte address 2 for the MX29F800CB and M29F160FT); on
  EasyFlash also with the unlock cycles of both chips interleaved, as the
  chips are independent and EAPIInit relies on that;
- sector erase blanks exactly one sector: the banks inside are $FF, the
  marker in the bank before and after survives;
- byte program writes a pattern that reads back, and programming only clears
  bits ($F0 then $3C leaves $30);
- on MegaByter and C64MegaCart a small boot-block sector is erased as well,
  and its neighbours - parts of other sectors - keep their markers, which is
  the part of the sector table where a mistake would destroy data;
- the app writes the image back to the CRT file, the file fetched over FTP
  holds the pattern (or the blank sector), and the next load finds the
  pattern (write persisted) and then a blank sector (erase persisted).

TRX64 Ultimate port: the board harness is easyflash_eapi_test.Board (REST,
FTP, JTAG). Three differences from the icepi build of the CRTs, all from how
the Ultimate app keeps a cartridge (the fork of the Ultimate app, io/c64/c64_crt.cc; not vendored):

- The image holds every bank at 16 KiB, also for the one-chip cartridges
  (chip address {bank, offset} at bank * 16 KiB + offset, trx_cart.cc).
- When it writes a CRT back, the app writes the whole chip: every bank in
  bank order, erased ones included (EasyFlash ROML and ROMH, GMod2 then its
  EEPROM). The CRT has no packets for the test sector and the boot sector, so
  the file proves that a bank the file lacked reaches it.
- C64MegaCart is CRT type 88 (T33, PROP-002 Rev C). Its mode $C0 is Ultimax for
  CPU writes only (T33 D3): the flash is written through $E000 in $C0 and read
  in 8K mode ($00) through $8000.

Usage: `install` puts one CRT per cartridge on the board, `run` loads them.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from easyflash_eapi_test import (BANK_STRIDE, Board, Failure, add_board_arguments, alternate,  # noqa: E402
                                 assemble as assemble_source, check, check_writeback, chip,
                                 crt_packets, file_bytes, whole_chip)

SOURCE = HERE / "flash_amd_write.asm"

ROUTINE_ADDRESS = 0x0800
ROUTINE_OFFSET = 0x0100
ROUTINE_PAGES = 3
CHUNK = 0x2000

RESULT = 0x0400
STATE_DONE = 0x44
STATUS_WRITTEN = 1
STATUS_PERSISTED = 2
TEST_BANK = 8
SECTOR_BANKS = 8
PATTERN = bytes([0xA5, 0x5A, 0xC3, 0x3C, 0x0F, 0xF0, 0x81, 0x7E,
                 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0x01])
AND_RESULT = 0x30

DONE_TIMEOUT_SECONDS = 15.0
COMMIT_TIMEOUT_SECONDS = 40.0       # up to 30 s pending, then the file onto the stick


@dataclass
class Cart:
    name: str
    crt_id: int
    banks: int
    chips: int
    ultimax_boot: bool              # EasyFlash starts in Ultimax at $E000; the others in 8K mode
    mfr: int
    dev: int
    defines: dict[str, str] = field(default_factory=dict)
    boot_bank: int | None = None    # a small boot-block sector of one bank
    boot_banks: int = 1

    @property
    def crt_name(self) -> str:
        return f"flash_{self.name}.crt"

    def marker_banks(self) -> set[int]:
        banks = {TEST_BANK - 1, TEST_BANK + SECTOR_BANKS}
        if self.boot_bank is not None:
            banks |= {self.boot_bank - 1, self.boot_bank + self.boot_banks}
        return {b for b in banks if 0 < b < self.banks}


CARTS = {
    "easyflash": Cart("easyflash", 32, 64, 2, True, 0x01, 0xA4),
    "gmod2": Cart("gmod2", 60, 64, 1, False, 0x01, 0xA4, {
        # written in Ultimax through $E000, read in 8K mode through $8000
        "BANK_OR": "$c0", "READ_OR": "$00", "MODE_REG": "$de00", "MODE_FLASH": "$c0", "CHIPS": "1",
        "WINDOW0": "$e0", "RWINDOW0": "$80",
        "U1_BANK": "2", "U1_OFS": "$1555", "U2_BANK": "1", "U2_OFS": "$0aaa"}),
    "c64megacart": Cart("c64megacart", 88, 256, 1, False, 0x01, 0xD2, {
        "MODE_REG": "$df00", "MODE_FLASH": "$c0", "MODE_READ": "$00", "CHIPS": "1", "WINDOW0": "$e0", "RWINDOW0": "$80",
        "U1_OFS": "$0aaa", "U2_OFS": "$0555", "ID_OFS": "2", "BOOT_BANK": "$fc"},
        boot_bank=0xFC),            # SA32, 8 KiB, between the 32 KiB SA31 and 8 KiB SA33
    "megabyter": Cart("megabyter", 86, 128, 1, False, 0xC2, 0x58, {
        "MODE_FLASH": "3", "CHIPS": "1", "WINDOW0": "$80",
        "U1_OFS": "$0aaa", "U2_OFS": "$0555", "ID_OFS": "2", "BOOT_BANK": "2"},
        boot_bank=2),               # SA1, 8 KiB, between the 16 KiB SA0 and 8 KiB SA2
}


def assemble(cart: Cart) -> bytes:
    routine = assemble_source(SOURCE, cart.defines)
    if len(routine) > ROUTINE_PAGES * 0x100:
        raise Failure(f"the stimulus is {len(routine)} bytes, the boot code copies {ROUTINE_PAGES} pages")
    return routine


def copy_loop(window: int) -> bytes:
    """Copy ROUTINE_PAGES pages from window + ROUTINE_OFFSET to $0800 and run them."""
    code = bytearray([0x78, 0xA2, 0xFF, 0x9A, 0xD8, 0xA2, 0x00])   # SEI LDX #$FF TXS CLD LDX #0
    loop = len(code)
    for page in range(ROUTINE_PAGES):
        src = window + ROUTINE_OFFSET + page * 0x100
        dst = ROUTINE_ADDRESS + page * 0x100
        code += bytes([0xBD, src & 0xFF, src >> 8, 0x9D, dst & 0xFF, dst >> 8])  # LDA src,X  STA dst,X
    code += b"\xe8"                                                         # INX
    code += bytes([0xD0, (loop - (len(code) + 2)) & 0xFF])                  # BNE loop (relative to the next instruction)
    code += bytes([0x4C, ROUTINE_ADDRESS & 0xFF, ROUTINE_ADDRESS >> 8])    # JMP $0800
    return bytes(code)


def bank0(cart: Cart, routine: bytes) -> bytes:
    """Bank 0 of the chip that boots: start code, the stimulus, vectors."""
    data = bytearray(b"\xff" * CHUNK)
    if cart.ultimax_boot:
        start = copy_loop(0xE000) + b"\x40"                                 # ...  RTI for NMI/IRQ
        data[0:len(start)] = start
        rti = 0xE000 + len(start) - 1
        data[0x1FFA:0x1FFC] = rti.to_bytes(2, "little")
        data[0x1FFC:0x1FFE] = (0xE000).to_bytes(2, "little")
        data[0x1FFE:0x2000] = rti.to_bytes(2, "little")
    else:
        # CBM80 cartridge: the KERNAL jumps through $8000 right after reset
        entry = 0x8009
        header = entry.to_bytes(2, "little") * 2 + b"\xc3\xc2\xcd\x38\x30"
        start = header + copy_loop(0x8000)
        data[0:len(start)] = start
    data[ROUTINE_OFFSET:ROUTINE_OFFSET + len(routine)] = routine
    return bytes(data)


def marker(bank: int) -> bytes:
    return b"BNK" + bytes([bank]) + b"\xff" * (CHUNK - 4)


def build(cart: Cart) -> bytes:
    header = bytearray(0x40)
    header[0x00:0x10] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = cart.crt_id.to_bytes(2, "big")
    if cart.ultimax_boot:
        header[0x18], header[0x19] = 1, 0       # EXROM high, GAME low: Ultimax
    else:
        header[0x18], header[0x19] = 0, 1       # EXROM low, GAME high: 8K
    title = f"FLASH-TEST-{cart.name.upper()}".encode()[:32]
    header[0x20:0x20 + len(title)] = title

    routine = assemble(cart)
    image = bytes(header)
    if cart.chips == 2:
        image += chip(0, 0x8000, marker(0)) + chip(0, 0xA000, bank0(cart, routine))
        for bank in sorted(cart.marker_banks()):
            image += chip(bank, 0x8000, marker(bank)) + chip(bank, 0xA000, marker(bank))
    else:
        image += chip(0, 0x8000, bank0(cart, routine))
        for bank in sorted(cart.marker_banks()):
            image += chip(bank, 0x8000, marker(bank))
    return image


# ---------------------------------------------------------------------------

def image_offset(chip_no: int, bank: int) -> int:
    """Offset in the DDR2 image: 16 KiB per bank, chip 1 (ROMH) in the upper half."""
    return bank * BANK_STRIDE + chip_no * CHUNK


def load_and_wait(board: Board, cart: Cart) -> bytes:
    length = 0x10 + 0x10 * cart.chips
    board.start(cart.crt_name, length)
    end = time.monotonic() + DONE_TIMEOUT_SECONDS
    while True:
        block = board.ram(RESULT, length)
        if block[0] == STATE_DONE:
            return block
        if time.monotonic() > end:
            raise Failure(f"{cart.name}: stimulus not done after {DONE_TIMEOUT_SECONDS:.0f} s: {block.hex()}")
        time.sleep(0.5)


def run_once(board: Board, cart: Cart, number: int) -> int:
    print(f"{cart.name} load {number}: run_crt {board.path(cart.crt_name)}")
    block = load_and_wait(board, cart)
    status = block[1]
    ok = check("status", status in (STATUS_WRITTEN, STATUS_PERSISTED), f"{status}")
    if cart.chips == 2:
        ids = block[2:6]
        ok &= check("interleaved autoselect", ids == bytes([cart.mfr, cart.dev] * 2),
                    f"chip 0 ${ids[0]:02X}/${ids[1]:02X}, chip 1 ${ids[2]:02X}/${ids[3]:02X}")
    want = PATTERN if status == STATUS_WRITTEN else b"\xff" * len(PATTERN)
    for chip_no in range(cart.chips):
        r = block[0x10 + 0x10 * chip_no:0x20 + 0x10 * chip_no]
        tag = f"chip {chip_no}: " if cart.chips > 1 else ""
        ok &= check(f"{tag}autoselect", r[0] == cart.mfr and r[1] == cart.dev,
                    f"mfr=${r[0]:02X} dev=${r[1]:02X}, expected ${cart.mfr:02X}/${cart.dev:02X}")
        ok &= check(f"{tag}sector erase", r[3] == 0 and r[4] == 0 and r[5] == 0,
                    f"timeout={r[3]} non-blank={r[4]} neighbours hit={r[5]} (busy read ${r[2]:02X})")
        data = board.image(image_offset(chip_no, TEST_BANK), len(PATTERN))
        if status == STATUS_WRITTEN:
            ok &= check(f"{tag}program", r[6] == 0 and r[7] == 0 and r[8] == AND_RESULT,
                        f"timeouts={r[6]} mismatches={r[7]} $F0&$3C=${r[8]:02X}")
            ok &= check(f"{tag}pattern in DDR2 image", data == want, data.hex())
        else:
            ok &= check(f"{tag}sector blank in DDR2 image", data == want, data.hex())
        if cart.boot_bank is not None and chip_no == 0:
            ok &= check(f"boot sector (bank ${cart.boot_bank:02X})", r[9] == 0 and r[10] == 0,
                        f"non-blank={r[9]} neighbours hit={r[10]}")

    ok &= check_writeback(board, COMMIT_TIMEOUT_SECONDS)
    image = board.download(cart.crt_name)
    if image is not None:
        packets = crt_packets(image)
        for chip_no in range(cart.chips):
            tag = f"chip {chip_no}: " if cart.chips > 1 else ""
            data = file_bytes(packets, TEST_BANK, 0x8000 + chip_no * CHUNK, len(PATTERN))
            ok &= check(f"{tag}CRT file over FTP", data == want, data.hex() if data else "no packet")
        lost = [b for b in sorted(cart.marker_banks())
                if file_bytes(packets, b, 0x8000, 4) != b"BNK" + bytes([b])]
        ok &= check("markers in the CRT file", not lost, f"lost in banks {lost}" if lost else "")
        wrong = whole_chip(image, cart.banks, cart.chips, cart.name == "gmod2")
        ok &= check("whole chip in the CRT file", not wrong, wrong or f"{len(image)} bytes")

    if not ok:
        raise Failure(f"{cart.name} load {number} failed")
    return status


def run(board: Board, names: list[str]) -> None:
    failed = []
    board.autosave_on()
    try:
        for name in names:
            cart = CARTS[name]
            try:
                alternate([run_once(board, cart, n) for n in (1, 2, 3)], name)
                print(f"PASS {name}")
            except Failure as exc:
                print(f"FAIL {name}: {exc}")
                failed.append(name)
    finally:
        board.autosave_restore()
    if failed:
        raise Failure(f"failed: {', '.join(failed)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cart", action="append", choices=sorted(CARTS),
                        help="limit to these cartridges (default: all)")
    sub = parser.add_subparsers(dest="action", required=True)
    p_build = sub.add_parser("build", help="write the test CRTs into a directory")
    p_build.add_argument("directory", type=Path)
    p_install = sub.add_parser("install", help="put the test CRTs on the board over FTP")
    p_run = sub.add_parser("run", help="run the tests on the board (REST, FTP, JTAG)")
    for p in (p_install, p_run):
        add_board_arguments(p)
    args = parser.parse_args()
    names = args.cart or list(CARTS)

    try:
        if args.action == "build":
            if not args.directory.is_dir():
                raise Failure(f"{args.directory} is not a directory")
            for name in names:
                cart = CARTS[name]
                (args.directory / cart.crt_name).write_bytes(build(cart))
                print(f"wrote {args.directory / cart.crt_name}")
        elif args.action == "install":
            board = Board(args.host, args.dir, jtag=False)
            for name in names:
                cart = CARTS[name]
                board.upload(cart.crt_name, build(cart))
                print(f"wrote {board.path(cart.crt_name)} on {args.host}")
        else:
            board = Board(args.host, args.dir)
            try:
                run(board, names)
            finally:
                board.close()
    except Failure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
