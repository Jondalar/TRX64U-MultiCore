#!/usr/bin/env python3
"""T15 bench (tb_flash_boot.vhd) cases: makes the flash images, the pre-loaded
DDR2 words and the expectations, runs each case under ghdl (one at a time,
niced) and checks the result block, the console, the test app's marker and the
copied memory. run.sh analyses the sources first.

The 'gideon' case takes the first 4 MB of the C64U flash dump (Gideon's
bitstream and app) from TRX_FLASH_DUMP, by default the tools fork's
pinmap/runs/flash-dump/c64u_flash.bin; it is skipped when the file is not there.
"""
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "build")
DUMP = os.environ.get("TRX_FLASH_DUMP", os.path.expanduser(
    "~/Development/1541ultimate-tools/pinmap/runs/flash-dump/c64u_flash.bin"))
APPL = 0x220000
BUILD = 0x6A1B2C3D
MARK_FLASH, MARK_JTAG = 0xA990C0DE, 0xB0B0CAFE

# ---- a few RV32I instructions ------------------------------------------------
def lui(rd, imm):
    return ((imm & 0xFFFFF) << 12) | (rd << 7) | 0x37

def addi(rd, rs1, imm):
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (rd << 7) | 0x13

def sw(rs2, rs1, off):
    return (((off >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (2 << 12) | ((off & 0x1F) << 7) | 0x23

def sb(rs2, rs1, off):
    return (((off >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | ((off & 0x1F) << 7) | 0x23

def jal0():
    return 0x0000006F       # jal x0, 0: loop here

def li(rd, val):
    hi = (val + 0x800) >> 12
    return [lui(rd, hi), addi(rd, rd, val - (hi << 12))]

def test_app(marker, text):
    """Prints text on the UART, writes marker to 0xFFB0, loops."""
    T0, T1, T2 = 5, 6, 7
    code = [lui(T0, 0x10000)]
    for ch in text:
        code += [addi(T1, 0, ord(ch)), sb(T1, T0, 0x10)]
    code += li(T2, 0xFFB0) + li(T1, marker) + [sw(T1, T2, 0), jal0()]
    return b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in code)


def app_image(dest, marker, size, run_off=8, seed=1, length=None):
    """Gideon's format: {dest, length, run} LE, data padded to 4. Two data words
    before the code, so run != dest."""
    rnd = random.Random(seed)
    data = struct.pack("<II", 0x11111111, 0x22222222) + test_app(marker, "APP\n")
    data += bytes(rnd.randrange(256) for _ in range(size - len(data)))
    length = len(data) if length is None else length
    body = data + b"\0" * (-len(data) % 4)
    return struct.pack("<III", dest, length, dest + run_off) + body, body


def sums(body, length):
    s = x = 0
    for i in range(0, (length + 3) // 4 * 4, 4):
        w = struct.unpack("<I", body[i:i + 4])[0]
        s = (s + w) & 0xFFFFFFFF
        x = (((x << 1) | (x >> 31)) & 0xFFFFFFFF) ^ w
    return s, x


# ---- bitstream headers ---------------------------------------------------------
def be(*words):
    return b"".join(struct.pack(">I", w) for w in words)

def bitstream(axss=None, where="after", frames=5000, mfwr=0):
    t1w = lambda reg, n: 0x30000000 | (reg << 13) | n
    NOOP = 0x20000000
    b = b"\xff" * 0x50 + be(0x000000BB, 0x11220044, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566, NOOP)
    b += be(t1w(0x04, 1), 0x07, NOOP, t1w(0x09, 1), 0x02223FE5)    # CMD RCRC, COR0
    b += be(t1w(0x06, 3), 1, 2, 3)                                   # short multi-word write
    b += be(t1w(0x0C, 20)) + be(*range(20))                         # long one: skipped by a new read
    if axss is not None and where == "before":
        b += be(t1w(0x0D, 1), axss)
    b += be(t1w(0x02, 0), 0x50000000 | frames) + be(*[(i * 2654435761) & 0xFFFFFFFF for i in range(frames)])
    for i in range(mfwr):                                            # as a compressed bitstream
        b += be(t1w(0x01, 1), i, t1w(0x0A, 4), 0, 0, 0, 0)          # FAR, MFWR x4
    b += be(t1w(0x04, 1), 0x0A, NOOP, NOOP)                          # GRESTORE
    if axss is not None and where == "after":
        b += be(t1w(0x0D, 1), axss)
    b += be(t1w(0x04, 1), 0x0D) + be(*[NOOP] * 8)                    # DESYNC
    return b


def pre_file(path, words):
    with open(path, "w") as f:
        for a, w in words:
            f.write(f"{a:08X} {w:08X}\n")


def image_words(addr, image):
    image = image + b"\0" * (-len(image) % 4)
    return [(addr + i, struct.unpack("<I", image[i:i + 4])[0]) for i in range(0, len(image), 4)]


GHDL = os.environ.get("GHDL", "ghdl")


def run(name, flash, pre=(), build=BUILD, caps2=0x03, button2=False, cmp=None, max_us=20000):
    fl = os.path.join(OUT, f"{name}.flash")
    pf = os.path.join(OUT, f"{name}.pre")
    of = os.path.join(OUT, f"{name}.out")
    if isinstance(flash, bytes):
        open(fl, "wb").write(flash)
    else:
        fl = flash
    pre_file(pf, pre)
    args = ["nice", "-n", "15", GHDL, "-r", "--std=08", "-frelaxed", "-fsynopsys", "tb_flash_boot",
            f"-gg_flash={fl}", f"-gg_pre={pf}", f"-gg_out={of}", f"-gg_build={build:032b}",
            f"-gg_caps2={caps2:08b}", f"-gg_button2={'true' if button2 else 'false'}",
            f"-gg_max_us={max_us}", "--assert-level=failure"]
    if cmp:
        args += [f"-gg_cmp_addr={cmp[0]}", f"-gg_cmp_words={cmp[1]}"]
    log = open(os.path.join(OUT, f"{name}.log"), "w")
    subprocess.run(args, cwd=OUT, stdout=log, stderr=subprocess.STDOUT, check=False)
    lines = open(of).read().splitlines()
    r = {"end": lines[0].split()[1], "console": lines[1][8:].split("|")}
    r["res"] = [int(x, 16) for x in lines[2].split()[1:]]
    r["marker"] = int(lines[3].split()[1], 16)
    r["boot"] = [int(x, 16) for x in lines[4].split()[1:]]
    r["flash"] = [int(x) for x in lines[5].split()[1:]]
    r["mem"] = b"".join(struct.pack("<I", int(x, 16)) for x in lines[6:])
    return r


fails = 0


def check(name, ok, what):
    global fails
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")
    if not ok:
        fails += 1


def common(name, r, status, source=None):
    res = r["res"]
    check(name, res[0] == 0x54313552, f"result block valid ({res[0]:08X})")
    check(name, res[1] == status, f"status {res[1]}, expected {status}")
    if source is not None:
        check(name, res[2] == source, f"source {res[2]}, expected {source}")
    check(name, r["flash"][0] == 0, f"flash: {r['flash'][0]} forbidden opcodes, "
          f"{r['flash'][1]} transfers, {r['flash'][2]} bytes read")


def main():
    os.makedirs(OUT, exist_ok=True)
    only = sys.argv[1:]
    app, body = app_image(0x30000, MARK_FLASH, 3000)
    length = 3000
    s, x = sums(body, length)
    # length not a multiple of 4: the ROM copies the last partial word whole
    app2, body2 = app_image(0x30000, MARK_FLASH, 3002, seed=2)
    s2, x2 = sums(body2, 3002)
    cases = []

    def case(fn):
        cases.append(fn)
        return fn

    @case
    def match_after():
        r = run("match_after", _flash(bitstream(BUILD, "after"), app2),
                cmp=(0x30000, len(body2) // 4))
        common("match_after", r, 1, 1)
        check("", r["marker"] == MARK_FLASH, f"test app ran (marker {r['marker']:08X})")
        check("", r["mem"] == body2, "memory at dest equals the image")
        check("", r["res"][6:8] == [s2, x2], f"sums {r['res'][6]:08X} {r['res'][7]:08X}")
        check("", r["res"][3:6] == [0x30000, 3002, 0x30008], "header in the result block")
        check("", "APP" in r["console"], "the app printed")

    @case
    def match_before():
        r = run("match_before", _flash(bitstream(BUILD, "before"), app), cmp=(0x30000, len(body) // 4))
        common("match_before", r, 1, 1)
        check("", r["marker"] == MARK_FLASH and r["mem"] == body, "booted, memory equals the image")

    @case
    def compressed():
        # more than 4096 packets before the stamp (a compressed 50T bitstream has some 9000)
        r = run("compressed", _flash(bitstream(BUILD, "after", frames=100, mfwr=2200), app),
                cmp=(0x30000, len(body) // 4), max_us=150000)
        common("compressed", r, 1, 1)
        check("", r["marker"] == MARK_FLASH and r["mem"] == body, "booted, memory equals the image")

    @case
    def mismatch():
        r = run("mismatch", _flash(bitstream(BUILD ^ 1), app))
        common("mismatch", r, 5, 1)
        check("", r["marker"] == 0 and r["end"] == "waiting", f"no boot, waits ({r['end']})")
        check("", r["res"][8:11] == [BUILD, BUILD ^ 1, 3], "BUILD, AXSS, flags")
        check("", r["res"][3:6] == [0x30000, 3000, 0x30008], "header reported")
        check("", r["flash"][2] < 2000, f"header only: {r['flash'][2]} bytes read")

    @case
    def no_build_reg():
        r = run("no_build_reg", _flash(bitstream(0), app), build=0, caps2=0x01)
        common("no_build_reg", r, 5)
        check("", r["marker"] == 0 and r["res"][10] == 1, "a bitstream without BUILD never boots the flash")

    @case
    def gideon():
        if not os.path.exists(DUMP):
            print("  skip: no flash dump at " + DUMP)
            return
        d = open(DUMP, "rb").read()[:0x400000]
        r = run("gideon", d)
        common("gideon", r, 5, 1)
        check("", r["res"][10] == 2, f"no stamp in Gideon's bitstream (flags {r['res'][10]})")
        check("", r["res"][3:6] == [0x30000, 0x14262C, 0x30000], "Gideon's app header "
              + " ".join(f"{v:X}" for v in r["res"][3:6]))
        check("", r["marker"] == 0 and r["end"] == "waiting", "no boot")

    @case
    def erased():
        r = run("erased", _flash(bitstream(BUILD), None))
        common("erased", r, 3, 1)
        check("", r["marker"] == 0 and r["end"] == "waiting", "empty: waits")

    @case
    def too_long():
        bad, _ = app_image(0x30000, MARK_FLASH, 3000, length=0x1E0000 - 11)
        r = run("too_long", _flash(bitstream(BUILD), bad))
        common("too_long", r, 4, 1)
        check("", r["marker"] == 0, "no jump (Gideon's would jump)")

    @case
    def longest():
        # the APPL limit itself is accepted: header with length 0x1DFFF4, only
        # the start of it is in the model, the run stops at the time limit
        big, _ = app_image(0x30000, MARK_FLASH, 3000, length=0x1E0000 - 12)
        r = run("longest", _flash(bitstream(BUILD), big), max_us=3000)
        check("", r["res"][4] == 0x1DFFF4 and r["res"][1] == 0, "loading 0x1DFFF4 bytes (still copying)")
        check("", any(l.endswith("loading") for l in r["console"]), "console: loading")

    @case
    def jtag_magic():
        japp = test_app(MARK_JTAG, "JTAG\n")
        pre = image_words(0x40000, japp) + [(0xFFF8, 0x40000), (0xFFFC, 0x1571BABE)]
        r = run("jtag_magic", _flash(bitstream(BUILD), app), pre=pre)
        check("", r["marker"] == MARK_JTAG, f"the JTAG app ran (marker {r['marker']:08X})")
        check("", r["boot"][0] == 0, "magic cleared")
        check("", r["flash"][2] == 0, f"flash not read ({r['flash'][2]} bytes)")

    @case
    def check_flash():
        pre = [(0xFFF4, 0x7158B007), (0xFFF0, 0x101)]
        r = run("check_flash", _flash(bitstream(None), app2), pre=pre)
        common("check_flash", r, 2, 1)
        check("", r["res"][6:8] == [s2, x2], f"sums {r['res'][6]:08X} {r['res'][7]:08X} "
              f"(expected {s2:08X} {x2:08X})")
        check("", r["marker"] == 0 and r["end"] == "waiting", "no jump")
        check("", r["boot"][1] == 0, "request cleared")

    @case
    def staged():
        pre = [(0xFFF4, 0x7158B007), (0xFFF0, 2), (0xFFEC, 0x03000000)] + image_words(0x03000000, app2)
        r = run("staged", _flash(bitstream(None), None), pre=pre, cmp=(0x30000, len(body2) // 4))
        common("staged", r, 1, 2)
        check("", r["marker"] == MARK_FLASH and r["mem"] == body2, "booted from DDR2, memory equals the image")
        check("", r["res"][6:8] == [s2, x2], "sums")

    @case
    def staged_overlap():
        pre = [(0xFFF4, 0x7158B007), (0xFFF0, 2), (0xFFEC, 0x30100)] + image_words(0x30100, app)
        r = run("staged_overlap", _flash(bitstream(None), None), pre=pre)
        common("staged_overlap", r, 8, 2)
        check("", r["marker"] == 0, "refused")

    @case
    def force_flash():
        pre = [(0xFFF4, 0x7158B007), (0xFFF0, 1)]
        r = run("force_flash", _flash(bitstream(BUILD ^ 0x100), app), pre=pre, cmp=(0x30000, len(body) // 4))
        common("force_flash", r, 1, 1)
        check("", r["marker"] == MARK_FLASH and r["mem"] == body, "booted past the mismatch")

    @case
    def lock():
        r = run("lock", _flash(bitstream(BUILD), app), button2=True)
        common("lock", r, 6, 1)
        check("", r["marker"] == 0 and "Lock" in r["console"], "Lock, no jump")

    @case
    def bad_request():
        pre = [(0xFFF4, 0x7158B007), (0xFFF0, 3)]
        r = run("bad_request", _flash(bitstream(BUILD), app), pre=pre)
        common("bad_request", r, 7)
        check("", r["marker"] == 0, "no boot")

    for fn in cases:
        if only and fn.__name__ not in only:
            continue
        print(fn.__name__)
        fn()
    print("ALL PASS" if fails == 0 else f"{fails} FAILED")
    return 1 if fails else 0


def _flash(bit, app):
    img = bytearray(b"\xff" * 0x400000)
    img[0:len(bit)] = bit
    if app is not None:
        img[APPL:APPL + len(app)] = app
    return bytes(img)


if __name__ == "__main__":
    sys.exit(main())
