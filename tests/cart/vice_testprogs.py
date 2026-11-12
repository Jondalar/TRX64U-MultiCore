#!/usr/bin/env python3
"""VICE cartridge test programs on the C64U with the TRX64 bitstream.

    tests/run_cart_tests.sh vice_testprogs         (or: <jtag venv>/bin/python tests/cart/vice_testprogs.py)
    vice_testprogs.py [--only NAME ...] [--group G ...] [--all] [--list]

A baseline of icepi's cartridge model (vendor/icepi/c64_cartridge.v, mapped by
trx64/cart_slot.v) against the cartridge tests of the VICE
testprogs (C64-carts, C64-freezer, C64-bankio, C64-openio). For each test:

- a CRT is started over REST (POST /v1/runners:run_crt with the file as body;
  it lands in /Temp, so the app never writes it back), or the machine is
  rebooted (PUT /v1/machine:reboot) for the tests that want no cartridge;
- a .prg (the tests that run beside a "dummy" cartridge, or without one) is
  put into C64 RAM over JTAG once BASIC says READY, and RUN is typed through
  the keyboard buffer, as tests/lorenz.py does;
- the verdict is what the test's own source defines. Most VICE tests write
  their result to $D7FF (the VICE debug cartridge, not on this machine) and
  show it as the border colour; the border is read from the VIC shadow on the
  state bus (tools/trxdbg.py sb, read while the machine is paused for a few
  JTAG scans). Tests with their own output on screen are read from screen RAM;
  the VIC mapping tests are compared with the VICE reference screenshots
  (C64-carts/ultimax/references) through the Ultimate's video stream
  (tools/stream_shot.py).

Types the model lacks (cart_slot.v's CART_TYPE -> CRT id map refuses them, or
the CRT id is not one of the model's) are recorded as "unsupported", not run;
--all runs them anyway. Tests that need the freeze button or a person looking
are recorded as "manual". At the end the machine is rebooted so no cartridge
stays loaded.

Never writes the board's flash: only REST cartridge starts and reboots, JTAG
reads and writes of C64 RAM, and the machine pause.

Results go to stdout and .cache/vice_testprogs/results.txt (with the screen
of every test that did not pass); screenshots to .cache/vice_testprogs/.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
HOST = os.environ.get("TRX64_HOST", "192.0.2.189")
VICE = Path(os.environ.get("VICE_TESTPROGS", Path.home() / "Development/C64/Tools/vice/testprogs"))
CACHE = ROOT / ".cache" / "vice_testprogs"

# VIC colours as the tests use them
BLACK, WHITE, RED, GREEN, LRED, LGREEN = 0, 1, 2, 5, 10, 13

# CRT ids the model maps (cart_slot.v: CART_TYPE -> CRT id, m_ok) as the app
# (io/c64/c64_crt.cc) hands them over. 61 is VICE's MAX Basic, but the app
# reads 61 as the provisional C64MegaCart id: run, to show what happens.
MODEL_IDS = {0, 1, 4, 5, 15, 17, 19, 32, 36, 60, 61, 85, 86}
CRT_NAMES = {0: "normal", 1: "Action Replay", 5: "Ocean", 8: "Super Games", 9: "Atomic/Nordic Power",
             32: "EasyFlash", 36: "Retro Replay", 38: "MMC Replay", 44: "EXOS", 53: "Pagefox",
             61: "MAX Basic", 70: "MultiMAX"}


def crt_info(path: Path) -> tuple[int, int]:
    d = path.read_bytes()[:0x40]
    return int.from_bytes(d[0x16:0x18], "big"), d[0x1A] if d[0x14:0x16] >= b"\x01\x01" else 0


EAPI_IMAGE = Path(os.environ.get("EAPI_IMAGE", Path(os.environ.get(
    "TRX_CRT_CLI_DIR", Path.home() / "Development/C64/Tools/TRX_CRT_cli")) / "templates/boot/eapi-am29f040-14"))


def inject_eapi(crt: bytes) -> bytes:
    """What VICE (and every EasyFlash loader) does: put the EAPI driver for the
    flash chip behind the "eapi" signature at $B800 of bank 0 ROMH. The
    test-eapi.crt carries only the signature; the app does not inject one."""
    image = EAPI_IMAGE.read_bytes()
    if image[:2] != b"\x00\xc0" or image[2:6] != b"eapi" or len(image) != 770:
        raise RuntimeError(f"{EAPI_IMAGE} is not a 768-byte EAPI .prg for $C000")
    out = bytearray(crt)
    pos = 0x40
    while out[pos:pos + 4] == b"CHIP":
        length = int.from_bytes(out[pos + 4:pos + 8], "big")
        bank = int.from_bytes(out[pos + 10:pos + 12], "big")
        load = int.from_bytes(out[pos + 12:pos + 14], "big")
        if bank == 0 and load in (0xA000, 0xE000):
            at = pos + 0x10 + 0x1800
            if out[at:at + 4] != b"eapi":
                raise RuntimeError("no EAPI signature at $B800 of bank 0")
            out[at:at + 768] = image[2:]
            return bytes(out)
        pos += length
    raise RuntimeError("no bank 0 ROMH in the CRT")


# ---------------------------------------------------------------------------
# The board
# ---------------------------------------------------------------------------

def _trxdbg():
    sys.path.insert(0, str(ROOT / "tools"))
    import trxdbg  # noqa: E402  (needs the JTAG venv: tests/run_cart_tests.sh)
    return trxdbg


class Board:
    def __init__(self, host: str = HOST):
        self.host = host
        self.t = _trxdbg()
        self.d = self.t.Dbg()

    def close(self):
        self.d.close()

    # REST
    def http(self, method: str, path: str, body: bytes | None = None) -> bytes:
        req = urllib.request.Request(f"http://{self.host}{path}", data=body, method=method)
        if body is not None:
            req.add_header("Content-Type", "application/octet-stream")
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.read()

    def run_crt(self, path: Path, eapi: bool = False) -> None:
        data = path.read_bytes()
        if eapi:
            data = inject_eapi(data)
        reply = json.loads(self.http("POST", "/v1/runners:run_crt", data))
        if reply.get("errors"):
            raise RuntimeError(f"run_crt {path.name}: {reply['errors']}")

    def reboot(self) -> None:
        self.http("PUT", "/v1/machine:reboot")

    # JTAG
    def ram(self, addr: int, n: int) -> bytes:
        return bytes(v & 0xFF for v in self.d.read_many(range(addr, addr + n)))

    def screen(self, base: int = 0x0400) -> list[str]:
        data = self.ram(base, 1000)
        return ["".join(self.t.screen_char(c) for c in data[r * 40:r * 40 + 40]).rstrip() for r in range(25)]

    def state(self) -> dict:
        """VIC registers, CPU and cartridge registers from the state bus, read
        while the machine is paused (the shadows follow it only then)."""
        d = self.d
        d.machine_pause(True)
        try:
            for _ in range(50):
                if d.read(self.t.MACHINE) & self.t.MACHINE_PAUSED:
                    break
            glue = d.sb_words(0, 1)[0]
            cpu = d.sb_words(1, 3)
            vic = b"".join(w.to_bytes(4, "little") for w in d.sb_words(2, 12))
            cart = d.sb_words(7, 1)[0]
        finally:
            d.machine_pause(False)
        return {"border": vic[0x20] & 15, "bg": vic[0x21] & 15, "d018": vic[0x18], "pc": cpu[1] & 0xFFFF,
                "cart": glue >> 3 & 1, "game": glue >> 1 & 1, "exrom": glue >> 2 & 1,
                "reg0": cart & 0xFF, "reg1": cart >> 8 & 0xFF, "p01": cpu[2] >> 8 & 0xFF}

    def wait_ready(self, timeout: float = 8.0) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if any(l.startswith("READY.") for l in self.screen()[3:10]):
                return True
            time.sleep(0.5)
        return False

    def run_prg(self, prg: bytes) -> None:
        load = prg[0] | prg[1] << 8
        body = prg[2:]
        self.d.write_bytes(load, body)
        end = load + len(body)
        for zp in (0x2D, 0x2F, 0x31):
            self.d.write(zp, end & 0xFF)
            self.d.write(zp + 1, end >> 8)
        for i, ch in enumerate("RUN\r"):
            self.d.write(0x0277 + i, ord(ch))
        self.d.write(0x00C6, 4)

    def type_text(self, text: str) -> None:
        """Through the KERNAL's keyboard buffer ($0277, ten bytes), ten at a time."""
        for at in range(0, len(text), 10):
            end = time.time() + 5
            while self.d.read(0x00C6) & 0xFF and time.time() < end:
                time.sleep(0.05)
            part = text[at:at + 10]
            for i, ch in enumerate(part):
                self.d.write(0x0277 + i, 0x0D if ch == "\r" else ord(ch))
            self.d.write(0x00C6, len(part))

    def shot(self, out: Path) -> bool:
        r = subprocess.run(["python3", str(ROOT / "tools" / "stream_shot.py"), self.host, str(out)],
                           capture_output=True, text=True, timeout=60)
        return r.returncode == 0 and out.exists()


# ---------------------------------------------------------------------------
# Picture comparison (system python3 with PIL, as tools/stream_shot.py)
# ---------------------------------------------------------------------------

PEPTO = [0x000000, 0xFFFFFF, 0x68372B, 0x70A4B2, 0x6F3D86, 0x588D43, 0x352879, 0xB8C76F,
         0x6F4F25, 0x433900, 0x9A6759, 0x444444, 0x6C6C6C, 0x9AD284, 0x6C5EB5, 0x959595]


def _indices(path: str):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    pal = [((c >> 16) & 255, (c >> 8) & 255, c & 255) for c in PEPTO]
    cache = {}
    out = []
    for px in im.getdata():
        if px not in cache:
            cache[px] = min(range(16), key=lambda i: sum((a - b) ** 2 for a, b in zip(px, pal[i])))
        out.append(cache[px])
    return im.size, out


def compare_pictures(ref: str, shot: str) -> dict:
    """Pixels of the 320x200 display window of the reference that differ in
    the shot, at the best offset within +-16 pixels (the two pictures need not
    crop the border alike)."""
    (rw, rh), r = _indices(ref)
    (sw, sh), s = _indices(shot)
    x0, y0, w, h = (rw - 320) // 2, (rh - 200) // 2, 320, 200
    best = None
    for dy in range(-16, 17):
        for dx in range(-16, 17):
            if not (0 <= x0 + dx and x0 + dx + w <= sw and 0 <= y0 + dy and y0 + dy + h <= sh):
                continue
            bad = 0
            for y in range(y0, y0 + h, 2):          # every other line is enough to place it
                ro, so = y * rw, (y + dy) * sw + dx
                for x in range(x0, x0 + w, 2):
                    bad += r[ro + x] != s[so + x]
            if best is None or bad < best[0]:
                best = (bad, dx, dy)
    _, dx, dy = best
    bad = sum(r[y * rw + x] != s[(y + dy) * sw + x + dx] for y in range(y0, y0 + h) for x in range(x0, x0 + w))
    return {"dx": dx, "dy": dy, "bad": bad, "of": w * h}


# ---------------------------------------------------------------------------
# The tests
# ---------------------------------------------------------------------------

@dataclass
class Test:
    name: str
    group: str
    crt: str | None = None          # relative to VICE, started over REST
    prg: str | None = None          # relative to VICE, RUN after READY
    type_: str | None = None        # typed after READY instead (a SYS into the cartridge)
    check: str = "border"           # border | tamtest | picture | screen | pc | alive | ocean_io | manual | unsupported
    pass_c: tuple = (GREEN,)
    fail_c: tuple = (LRED, RED)
    settle: float = 3.0             # seconds before the first look
    timeout: float = 15.0
    ref: str | None = None          # reference picture (check picture)
    eapi: bool = False              # inject the EAPI driver as VICE does
    pc: tuple = (0, 0)              # check pc: the range the CPU should be found in
    why: str = ""                   # manual / unsupported: the reason
    note: str = ""


C = "C64-carts/"
NP = C + "nordicpower/"
AC = C + "aracidtest/"
UX = C + "ultimax/"
FRZ = "no freeze button: cart_slot.v:195 ties the model's freeze input to 0"

TESTS = [
    # memory maps of the plain cartridge modes, in all eight $01 settings; the
    # test colours what differs from its table and sets the border to 10
    Test("8kgame", "carts", crt=C + "8kgame.crt", fail_c=(LRED,)),
    Test("16kgame", "carts", crt=C + "16kgame.crt", fail_c=(LRED,)),
    Test("romram", "carts", crt=C + "romram.crt"),
    Test("rr-reu", "carts", crt=C + "rr-reu.crt", note="REU always on in c64_basic"),
    Test("exos", "carts", prg=C + "exos.prg", check="unsupported", why="EXOS (CRT 44): external KERNAL, no CRT id in the model"),
    Test("mmcr", "carts", crt=C + "mmcr-64krom.crt", check="unsupported", why="MMC Replay (CRT 38)"),
    Test("ocean", "carts", crt=C + "ocean/ocean.crt", type_="SYS32777\r",
         check="screen", settle=3,
         note="readme: I/O not readable; rows 10/17 show $DE00/$DF00 reads. Its CBM80 is ASCII, so it does "
              "not autostart (on any machine): started with SYS32777"),
    Test("ocean-io", "carts", crt=C + "ocean/ocean.crt", check="ocean_io", settle=2,
         type_="POKE56832,3:PRINTPEEK(56832);PEEK(56833);PEEK(57088);PEEK(57089)\r",
         note="BASIC reads $DE00/$DE01/$DF00/$DF01 after selecting bank 3; an Ocean cartridge has no readable "
              "register, so a 3 at $DE00 is the model's register showing through"),
    Test("ef-eapi", "carts", crt=C + "ef-eapi/test-eapi.crt", settle=6, eapi=True,
         note="the CRT carries only the EAPI signature; VICE injects the driver, so does this script "
              "(eapi-am29f040-14 from TRX_CRT_cli)"),
    Test("ef-test", "carts", crt=C + "ef-test/ef_test.crt", settle=4, fail_c=(LRED, RED, 7)),
    Test("tamtest", "carts", crt=C + "tamtest/tamtest.crt", check="tamtest", settle=10, timeout=90),
    Test("supergames", "carts", crt=C + "supergames/supergames.crt", check="unsupported", why="Super Games (CRT 8)"),
    Test("pagefox-godot", "carts", crt=C + "pagefox/godot.crt", check="unsupported", why="Pagefox (CRT 53)"),
    Test("pagefox-writeram-8000", "carts", crt=C + "pagefox/writeram-8000.crt", check="unsupported", why="Pagefox (CRT 53)"),
    Test("pagefox-writeram-a000", "carts", crt=C + "pagefox/writeram-a000.crt", check="unsupported", why="Pagefox (CRT 53)"),
    # Action Replay / Retro Replay / Nordic Power mapping (mode $22 and friends)
    Test("artest", "nordicpower", crt=NP + "artest.crt", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rrtest", "nordicpower", crt=NP + "rrtest.crt", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rr2test", "nordicpower", crt=NP + "rr2test.crt", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("nptest", "nordicpower", crt=NP + "nptest.crt", check="unsupported", why="Atomic/Nordic Power (CRT 9): cart_slot.v refuses variant 2"),
    Test("nrtest", "nordicpower", crt=NP + "nrtest.crt", check="unsupported", why="Nordic Replay (CRT 36 subtype 1): no subtype in app or model"),
    Test("nr2test", "nordicpower", crt=NP + "nr2test.crt", check="unsupported", why="Nordic Replay (CRT 36 subtype 1)"),
    Test("arramwrite", "nordicpower", crt=NP + "arramwrite.crt", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rrramwrite", "nordicpower", crt=NP + "rrramwrite.crt", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rr2ramwrite", "nordicpower", crt=NP + "rr2ramwrite.crt", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("npramwrite", "nordicpower", crt=NP + "npramwrite.crt", check="unsupported", why="Atomic/Nordic Power (CRT 9)"),
    Test("nrramwrite", "nordicpower", crt=NP + "nrramwrite.crt", check="unsupported", why="Nordic Replay (CRT 36 subtype 1)"),
    Test("nr2ramwrite", "nordicpower", crt=NP + "nr2ramwrite.crt", check="unsupported", why="Nordic Replay (CRT 36 subtype 1)"),
    Test("arramwrite.prg", "nordicpower", crt=NP + "ardummy.crt", prg=NP + "arramwrite.prg", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("safearramwrite.prg", "nordicpower", crt=NP + "ardummy.crt", prg=NP + "safearramwrite.prg", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rrramwrite.prg", "nordicpower", crt=NP + "rrdummy.crt", prg=NP + "rrramwrite.prg", pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rr2ramwrite.prg", "nordicpower", crt=NP + "rrdummy.crt", prg=NP + "rr2ramwrite.prg", pass_c=(LGREEN,), fail_c=(LRED,)),
    # AR/RR acid RAM tests (Count Zero): border 13 pass, 10 fail
    *[Test(f"{p}", "aracidtest", crt=AC + "ardummy.crt", prg=AC + f"{p}.prg", pass_c=(LGREEN,), fail_c=(LRED,),
           settle=5, timeout=40)
      for p in ("test-ramdetect-ar", "test-ramsizedetect-ar", "test-ariotest1-ar", "test-ariotest2-ar", "test-arramtest2-ar")],
    *[Test(f"{p}", "aracidtest", crt=AC + "rrdummy.crt", prg=AC + f"{p}.prg", pass_c=(LGREEN,), fail_c=(LRED,),
           settle=5, timeout=40)
      for p in ("test-ramdetect", "test-ramsizedetect", "test-ariotest1", "test-ariotest2", "test-arramtest2")],
    Test("test-arramtest2-nr", "aracidtest", crt=AC + "nrdummy.crt", prg=AC + "test-arramtest2-nr.prg",
         check="unsupported", why="Nordic Replay (CRT 36 subtype 1)"),
    # Retro Replay $DE01 bits
    Test("rr-writeonce", "retroreplay", crt=C + "retroreplay/rrdummy.crt", prg=C + "retroreplay/writeonce.prg",
         pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rr-allowbank0", "retroreplay", crt=C + "retroreplay/rrdummy.crt", prg=C + "retroreplay/allowbank0.prg",
         pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rr-allowbank1", "retroreplay", crt=C + "retroreplay/rrdummy.crt", prg=C + "retroreplay/allowbank1.prg",
         pass_c=(LGREEN,), fail_c=(LRED,)),
    Test("rr-nofreeze", "retroreplay", crt=C + "retroreplay/rrdummy.crt", prg=C + "retroreplay/nofreeze.prg",
         check="manual", why=FRZ),
    Test("rr-freeze", "retroreplay", crt=C + "rr-freeze/rr-freeze.crt", check="manual", why=FRZ + "; saves its dump to drive 8"),
    # memory map in the CPU cycle (screen, compared with the readme's table by hand)
    Test("ultimax", "ultimax", crt=UX + "ultimax.crt", check="screen", settle=3),
    Test("ultimax-ef", "ultimax", crt=UX + "ultimax-ef.crt", check="screen", settle=3),
    Test("ultimax-rr", "ultimax", crt=UX + "ultimax-rr.crt", check="screen", settle=3),
    Test("ultimax-maxbasic", "ultimax", crt=UX + "ultimax-maxbasic.crt", check="screen", settle=3,
         note="CRT 61 is MAX Basic in VICE, C64MegaCart in the app"),
    Test("ultimax-multimax", "ultimax", crt=UX + "ultimax-multimax.crt", check="unsupported", why="MultiMAX (CRT 70)"),
    # memory map in the VIC cycle: VICE reference screenshots
    *[Test(n, "ultimax", crt=UX + f"{n}.crt", check="picture", settle=4, ref=UX + f"references/{n}.crt.png")
      for n in ("ultimax-vic-ef", "8k-vic-ef", "16k-vic-ef", "ultimax-vic-rr", "8k-vic-rr", "16k-vic-rr")],
    Test("ultimax-vic", "ultimax", crt=UX + "ultimax-vic.crt", check="picture", settle=4, ref=None,
         note="no VICE reference; the readme's table"),
    Test("ultimax-vic-maxbasic", "ultimax", crt=UX + "ultimax-vic-maxbasic.crt", check="picture", settle=4, ref=None,
         note="CRT 61: MAX Basic in VICE, C64MegaCart in the app"),
    Test("ultimax-vic-multimax", "ultimax", crt=UX + "ultimax-vic-multimax.crt", check="unsupported", why="MultiMAX (CRT 70)"),
    # freezers: all need the freeze button
    *[Test(Path(p).stem, "freezer", prg="C64-freezer/" + p, check="manual", why=FRZ)
      for p in ("freeze-test.prg", "freeze-cia1.prg", "freeze-cia2.prg", "freeze-raster.prg", "freeze-combined.prg",
                "freezeblock.prg", "batest/batest.prg", "ciairq/ciairq1t1.prg", "ciairq/ciairq1t2.prg",
                "ciairq/ciairq2t1.prg", "ciairq/ciairq2t2.prg")],
    # no cartridge: the I/O area itself
    Test("bankio", "bankio", prg="C64-bankio/bankio.prg", settle=4),
    Test("gauntlet", "openio", prg="C64-openio/gauntlet.prg", settle=4, fail_c=(RED,)),
    Test("de00all", "openio", prg="C64-openio/de00all.prg", check="pc", pc=(0xDE00, 0xDE7F), settle=3,
         note="runs from open I/O at $DE00-$DE7F"),
    Test("de00int", "openio", prg="C64-openio/de00int.prg", check="alive", pc=(0x0900, 0x09FF), settle=3,
         note="its raster IRQ calls $DE00 once a frame for a few cycles, too short to sample; "
              "judged by the main loop still running with a black border (garbage from open I/O would crash it)"),
    Test("dadb", "openio", prg="C64-openio/dadb.prg", check="pc", pc=(0xD800, 0xDBFF), settle=3,
         note="runs from the colour RAM's high nibbles"),
    Test("trivial", "openio", prg="C64-openio/trivial.prg", check="manual", why="shows open-bus values; nothing to compare"),
]


def type_of(t: Test) -> str:
    if t.crt:
        cid, sub = crt_info(VICE / t.crt)
        return f"{cid}" + (f"/{sub}" if sub else "")
    return "-"


# ---------------------------------------------------------------------------

def run_test(b: Board, t: Test) -> tuple[str, str, list[str]]:
    """(result, detail, screen)"""
    if t.check == "border" and b.state()["border"] in t.pass_c:
        # A reset leaves the VIC registers alone: get the last test's pass
        # colour off the border first (the KERNAL's reset sets 14).
        b.reboot()
        time.sleep(3.0)
    if t.crt:
        b.run_crt(VICE / t.crt, t.eapi)
        time.sleep(1.0)
    else:
        b.reboot()
        time.sleep(3.0)
    if t.prg:
        if not b.wait_ready(10):
            return "FAIL", "no READY after start", b.screen()
        b.run_prg((VICE / t.prg).read_bytes())
    elif t.type_:
        if not b.wait_ready(10):
            return "FAIL", "no READY after start", b.screen()
        b.type_text(t.type_)
    time.sleep(t.settle)

    if t.check == "border":
        end = time.time() + t.timeout
        while True:
            s = b.state()
            if s["border"] in t.pass_c:
                time.sleep(1.0)                 # a late mismatch would still turn it
                s = b.state()
                if s["border"] in t.pass_c:
                    return "pass", f"border {s['border']}", b.screen()
            if s["border"] in t.fail_c:
                return "FAIL", f"border {s['border']}", b.screen()
            if time.time() > end:
                return "FAIL", f"timeout, border {s['border']}, PC ${s['pc']:04X}", b.screen()
            time.sleep(1.0)

    if t.check == "tamtest":
        end = time.time() + t.timeout
        while time.time() < end:
            s = b.state()
            if s["border"] in (RED, WHITE, LRED) or s["bg"] == RED:
                return "FAIL", f"border {s['border']} background {s['bg']}", b.screen()
            scr = b.ram(0x0400, 0x58)
            # .lscre: broken RAM bits as $FF at $0400-$0407, colour RAM bits at
            # $0428-$042F (bits 7-4 always "broken"); then $0450 blinks
            if scr[0x50] in (0x20, 0xFF) and s["pc"] < 0xD000 and s["pc"] >= 0x0400 or scr[0x50] == 0xFF:
                ram_bad = [7 - i for i in range(8) if scr[i] == 0xFF]
                col_bad = [7 - i for i in range(4, 8) if scr[0x28 + i] == 0xFF]
                if ram_bad or col_bad:
                    return "FAIL", f"broken RAM bits {ram_bad}, colour RAM bits {col_bad}", b.screen()
                return "pass", "no broken RAM or colour RAM bits", b.screen()
            time.sleep(2.0)
        return "FAIL", "timeout", b.screen()

    if t.check == "ocean_io":
        screen = b.screen()
        nums = next((l.split() for l in screen if l.strip() and all(w.isdigit() for w in l.split())), None)
        if not nums or len(nums) < 4:
            return "FAIL", "no PEEK results on screen", screen
        detail = f"$DE00 {nums[0]} $DE01 {nums[1]} $DF00 {nums[2]} $DF01 {nums[3]} after POKE $DE00,3"
        return ("FAIL" if nums[0] == "3" else "pass"), detail, screen

    if t.check == "alive":
        pcs = []
        for _ in range(10):
            s = b.state()
            pcs.append(s["pc"])
            time.sleep(0.2)
        # the program, or the KERNAL's IRQ entry ($FF48) and exit ($EA81)
        ok = all(t.pc[0] <= p <= t.pc[1] or 0xFF48 <= p <= 0xFF6F or 0xEA81 <= p <= 0xEA90 for p in pcs) \
            and s["border"] == BLACK
        return "pass" if ok else "FAIL", f"PC " + " ".join(f"{p:04X}" for p in pcs) + \
            f", border {s['border']}", b.screen()

    if t.check == "pc":
        hits = 0
        pcs = []
        for _ in range(10):
            s = b.state()
            pcs.append(s["pc"])
            hits += t.pc[0] <= s["pc"] <= t.pc[1]
            time.sleep(0.2)
        res = "pass" if hits else "FAIL"
        return res, f"PC in ${t.pc[0]:04X}-${t.pc[1]:04X} {hits}/10 (" + " ".join(f"{p:04X}" for p in pcs[:5]) + \
            f"), border {s['border']}", b.screen()

    if t.check == "picture":
        CACHE.mkdir(parents=True, exist_ok=True)
        out = CACHE / f"{t.name}.png"
        if not b.shot(out):
            return "FAIL", "no picture from the video stream", b.screen()
        if not t.ref:
            return "judged", f"picture {out.name}", b.screen()
        r = subprocess.run(["python3", str(Path(__file__)), "_compare", str(VICE / t.ref), str(out)],
                           capture_output=True, text=True, timeout=300)
        if r.returncode:
            return "FAIL", f"compare: {r.stderr.strip()[-200:]}", b.screen()
        c = json.loads(r.stdout)
        pct = 100.0 * c["bad"] / c["of"]
        res = "pass" if c["bad"] == 0 else "FAIL"
        return res, f"{c['bad']} of {c['of']} pixels differ ({pct:.2f} %) at offset {c['dx']},{c['dy']}", b.screen()

    if t.check == "screen":
        s = b.state()
        CACHE.mkdir(parents=True, exist_ok=True)
        shot = f", picture {t.name}.png" if b.shot(CACHE / f"{t.name}.png") else ""
        return "judged", f"border {s['border']}, cart {s['cart']} GAME {s['game']} EXROM {s['exrom']}{shot}", b.screen()

    raise ValueError(t.check)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--group", nargs="*")
    ap.add_argument("--all", action="store_true", help="also run the unsupported types")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args(argv)

    tests = [t for t in TESTS if (not args.only or t.name in args.only) and (not args.group or t.group in args.group)]
    if args.list:
        for t in tests:
            print(f"{t.group:12s} {t.name:24s} {type_of(t):5s} {t.check}")
        return 0

    CACHE.mkdir(parents=True, exist_ok=True)
    report = open(CACHE / "results.txt", "w")
    counts: dict[str, int] = {}
    b = None
    try:
        for t in tests:
            started = time.time()
            if t.check in ("manual", "unsupported") and not (args.all and t.check == "unsupported"):
                res, detail, screen = t.check, t.why, []
            else:
                if b is None:
                    b = Board()
                try:
                    res, detail, screen = run_test(b, t)
                    if res == "FAIL" and t.check != "picture" and b.shot(CACHE / f"{t.name}.png"):
                        detail += f", picture {t.name}.png"
                except (OSError, urllib.error.URLError, RuntimeError) as exc:
                    res, detail, screen = "error", repr(exc), []
                if t.check == "unsupported":
                    detail = f"{t.why}; ran anyway: {res}, {detail}"
                    res = "unsupported"
            counts[res] = counts.get(res, 0) + 1
            row = f"{t.group:12s} {t.name:24s} {type_of(t):5s} {res:11s} {time.time() - started:5.1f} s  {detail}"
            print(row, flush=True)
            report.write(row + "\n")
            if res not in ("pass", "manual", "unsupported") or t.note:
                if t.note:
                    report.write(f"    note: {t.note}\n")
                for line in screen:
                    report.write("    | " + line + "\n")
            report.flush()
    finally:
        if b is not None:
            try:
                b.reboot()                  # no cartridge stays loaded
            finally:
                b.close()
    summary = ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    print(summary)
    report.write(summary + "\n")
    return 0 if "FAIL" not in counts and "error" not in counts else 1


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "_compare":
        print(json.dumps(compare_pictures(sys.argv[2], sys.argv[3])))
        sys.exit(0)
    sys.exit(main())
