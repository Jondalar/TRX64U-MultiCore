#!/usr/bin/env python3
"""Cartridge types in simulation: builds a test .crt per type (asm/boot.asm
starts asm/runner.asm, which works through a table of bank switches, reads
and writes), runs build/tb_cart on it and compares what the C64 read with
what the cartridge should show; at the end the lens unit's look at the
cartridge (T12 §11 Q2, dma_win.v) is compared with bus reads and the lens unit's forced bus
cycle (T21 §3.4.1) with the look (tb_cart --peek). Every
ROM chip is filled with a pattern P(bank, half, offset), so any read has a
known answer.

Usage: carts.py [name ...]       (default: all; build.sh first)
       carts.py --list
       carts.py --emit DIR [name ...]   the CRTs and what $0400.. must hold, for the
                                        board (tests/cart/carts_board.py), no simulation
Extra real cartridges: CARTS_EXTRA="file.crt:cycles ..." prints their screens.
"""
import os
import struct
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
work = os.path.join(here, "build", "carts")
run_dir = os.path.join(here, "build", "run")
tb = os.path.join(here, "build", "tb_cart")


def P(bank, half, off):
    return (bank * 2 + half + (off >> 8) * 37 + 0x11) & 0xFF


def asm(src, out, base=None):
    cmd = ["64tass", "-q", "-b", "-o", out, os.path.join(here, "asm", src)]
    if base is not None:
        cmd[1:1] = ["-D", "BASE=$%04x" % base]
    subprocess.run(cmd, check=True)
    return open(out, "rb").read()


class Ops:
    """the runner's table, and the bytes the reads must return"""

    def __init__(self):
        self.tab = bytearray()
        self.expect = bytearray()
        self.ddr = []

    def w(self, a, v):
        self.tab += bytes([1, a & 0xFF, a >> 8, v])

    def r(self, a, want):
        self.tab += bytes([2, a & 0xFF, a >> 8])
        self.expect.append(want)

    def q(self, a):
        self.tab += bytes([4, a & 0xFF, a >> 8])

    def x(self, a, v, ra, want):
        self.tab += bytes([5, a & 0xFF, a >> 8, v, ra & 0xFF, ra >> 8])
        self.expect.append(want)

    def d(self, n):
        self.tab += bytes([3, n])

    def p(self, a, want):
        """read until it is want (flash status polling)"""
        self.tab += bytes([6, a & 0xFF, a >> 8, want])
        self.expect.append(want)

    def amd(self, a1, a2, a, cmd):
        self.w(a1, 0xAA)
        self.w(a2, 0x55)
        self.w(a, cmd)


def crt(name, hw, exrom, game, chips, subtype=0):
    """chips: (bank, load, data)"""
    h = bytearray(b"C64 CARTRIDGE   ")
    h += struct.pack(">IHHBBB", 0x40, 0x0100, hw, exrom, game, subtype)
    h += bytes(5)
    h += name.encode()[:32].ljust(32, b"\0")
    for bank, load, data in chips:
        h += b"CHIP" + struct.pack(">IHHHH", 16 + len(data), 0, bank, load, len(data)) + bytes(data)
    return bytes(h)


def chip(bank, half):
    return bytearray(P(bank, half, o) for o in range(0x2000))


def with_boot(data, ops, base, vectors=False):
    """boot code at the chip's start, runner and table from offset $100"""
    boot = asm("boot.asm", os.path.join(work, "boot.bin"), base)
    runner = asm("runner.asm", os.path.join(work, "runner.bin"))
    tab = ops.tab + b"\0"
    code = runner + tab
    assert len(code) <= 0x400, "table too long"
    data[0:len(boot)] = boot
    data[0x100:0x100 + len(code)] = code
    if vectors:
        cold, nmi = base + 9, base + len(boot) - 1
        data[0x1FFA:0x2000] = struct.pack("<HHH", nmi, cold, nmi)
    return data


tests = {}


def test(fn):
    tests[fn.__name__] = fn
    return fn


@test
def normal_8k():
    o = Ops()
    o.r(0x9234, P(0, 0, 0x1234))
    o.w(0x8800, 0x33)                       # RAM below the ROM
    o.r(0x8800, P(0, 0, 0x0800))
    o.w(0xDFFF, 0x40)                       # the boot cartridge's switch-off
    o.r(0x8800, 0x33)
    c = with_boot(chip(0, 0), o, 0x8000)
    return crt("NORMAL 8K", 0, 0, 1, [(0, 0x8000, c)]), o, ["--inactive"]


@test
def normal_16k():
    o = Ops()
    o.r(0x9234, P(0, 0, 0x1234))
    o.r(0xB234, P(0, 1, 0x1234))
    o.r(0xBFFF, P(0, 1, 0x1FFF))
    c = with_boot(chip(0, 0), o, 0x8000)
    return crt("NORMAL 16K", 0, 0, 0, [(0, 0x8000, c), (0, 0xA000, chip(0, 1))]), o, ["--kill"]


@test
def ultimax():
    o = Ops()
    o.r(0x9234, P(0, 0, 0x1234))
    o.r(0xF234, P(0, 1, 0x1234))
    o.w(0x0400 + 100, 0x5A)
    o.r(0x0400 + 100, 0x5A)
    h = with_boot(chip(0, 1), o, 0xE000, vectors=True)
    return crt("ULTIMAX", 0, 1, 0, [(0, 0x8000, chip(0, 0)), (0, 0xE000, h)]), o, []


def bank_walk(o, banks, reg, read_l, read_h=None, value=lambda b: b, half_l=0):
    for b in banks:
        o.w(reg, value(b))
        o.r(0x8000 + read_l, P(b, half_l, read_l))
        if read_h is not None:
            o.r(0xA000 + read_h, P(b, 1, read_h))


@test
def ocean_8k():
    o = Ops()
    banks = list(range(16)) + [15, 0, 7, 3, 12, 9]
    bank_walk(o, banks, 0xDE00, 0x1F00, value=lambda b: 0x80 | b)
    for b in [4, 13, 2, 11, 6, 14, 1, 10, 5, 8]:   # the line is not in when the CPU reads it
        o.x(0xDE00, 0x80 | b, 0x9E00 + b, P(b, 0, 0x1E00 + b))
    chips = [(b, 0x8000, chip(b, 0)) for b in range(16)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("OCEAN 8K", 5, 0, 1, chips), o, []


@test
def ocean_16k():
    o = Ops()
    banks = list(range(8)) + [5, 0, 7]
    bank_walk(o, banks, 0xDE00, 0x1F00, 0x1E80, value=lambda b: 0x80 | b)
    chips = []
    for b in range(8):
        chips.append((b, 0x8000, with_boot(chip(0, 0), o, 0x8000) if b == 0 else chip(b, 0)))
        chips.append((b, 0xA000, chip(b, 1)))
    return crt("OCEAN 16K", 5, 0, 0, chips), o, []


@test
def magic_desk():
    o = Ops()
    o.w(0x8800, 0x5A)
    bank_walk(o, [1, 2, 3, 9, 15, 0], 0xDE00, 0x1F40)
    o.w(0xDE00, 0x80)                       # bit 7: off
    o.r(0x8800, 0x5A)
    chips = [(b, 0x8000, chip(b, 0)) for b in range(16)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("MAGIC DESK", 19, 0, 1, chips), o, []


@test
def c64gs():
    o = Ops()
    for b in [1, 5, 31, 2]:
        o.w(0xDE00 + b, 0)                  # the address is the bank
        o.r(0x9F00 + b, P(b, 0, 0x1F00 + b))
    for b in [6, 0]:
        o.q(0xDE00 + b)                     # reading selects bank 0 (icepi's model, as VICE)
        o.r(0x9F00 + b, P(0, 0, 0x1F00 + b))
    chips = [(b, 0x8000, chip(b, 0)) for b in range(32)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("C64GS", 15, 0, 1, chips), o, []


@test
def easyflash():
    o = Ops()
    o.w(0xDE02, 0x07)                       # 16K
    banks = list(range(0, 64, 7)) + [63, 1, 0]
    bank_walk(o, banks, 0xDE00, 0x1F00, 0x1E00)
    o.w(0xDF00, 0x5A)                       # 256 bytes of RAM
    o.w(0xDF7F, 0xA5)
    o.r(0xDF00, 0x5A)
    o.r(0xDF7F, 0xA5)
    o.w(0xDE02, 0x05)                       # Ultimax: the flash takes writes
    o.w(0xDE00, 3)
    o.w(0x8555, 0xAA)                       # autoselect, the two chips interleaved
    o.w(0xE555, 0xAA)
    o.w(0x82AA, 0x55)
    o.w(0xE2AA, 0x55)
    o.w(0x8555, 0x90)
    o.w(0xE555, 0x90)
    o.r(0x8000, 0x01)
    o.r(0x8001, 0xA4)
    o.r(0xE001, 0xA4)
    o.w(0x8000, 0xF0)
    o.w(0xE000, 0xF0)
    o.r(0x8100, P(3, 0, 0x100))             # its line is cached now
    o.amd(0x8555, 0x82AA, 0x8555, 0xA0)     # program ROML
    o.w(0x8100, 0xC3)
    o.p(0x8100, P(3, 0, 0x100) & 0xC3)
    o.amd(0xE555, 0xE2AA, 0xE555, 0xA0)     # program ROMH
    o.w(0xE101, 0x3C)
    o.p(0xE101, P(3, 1, 0x101) & 0x3C)
    o.w(0xDE00, 9)
    o.r(0x8040, P(9, 0, 0x040))             # cached, then erased
    o.amd(0x8555, 0x82AA, 0x8555, 0x80)     # sector erase: chip 0, banks 8-15
    o.amd(0x8555, 0x82AA, 0x8000, 0x30)
    o.p(0x8040, 0xFF)
    o.w(0xDE00, 15)
    o.r(0x9FFF, 0xFF)
    o.r(0xFFF0, P(15, 1, 0x1FF0))           # ROMH: the other chip, kept
    o.w(0xDE00, 16)
    o.r(0x8000, P(16, 0, 0))
    for b in range(20, 30):                 # push bank 3 out of the cache
        o.w(0xDE00, b)
        o.r(0x9000, P(b, 0, 0x1000))
    o.w(0xDE00, 3)                          # and fetch it again from DDR2
    o.r(0x8100, P(3, 0, 0x100) & 0xC3)
    o.r(0xE101, P(3, 1, 0x101) & 0x3C)
    o.r(0x8102, P(3, 0, 0x102))
    o.ddr += [(3 * 0x4000 + 0x100, P(3, 0, 0x100) & 0xC3), (3 * 0x4000 + 0x2101, P(3, 1, 0x101) & 0x3C),
              (8 * 0x4000, 0xFF), (15 * 0x4000 + 0x1FFF, 0xFF), (15 * 0x4000 + 0x2000, P(15, 1, 0)),
              (16 * 0x4000, P(16, 0, 0))]
    chips = []
    for b in range(64):
        chips.append((b, 0x8000, chip(b, 0)))
        chips.append((b, 0xA000, with_boot(chip(0, 1), o, 0xE000, vectors=True) if b == 0 else chip(b, 1)))
    return crt("EASYFLASH", 32, 1, 0, chips), o, []


@test
def gmod2():
    o = Ops()
    banks = list(range(0, 64, 9)) + [63, 1, 0]
    bank_walk(o, banks, 0xDE00, 0x1F00)
    o.w(0xDE00, 0xC2)                       # Ultimax, writes through $E000
    o.w(0xF555, 0xAA)                       # chip $5555 = bank 2 $1555
    o.w(0xDE00, 0xC1)
    o.w(0xEAAA, 0x55)                       # chip $2AAA = bank 1 $0AAA
    o.w(0xDE00, 0xC2)
    o.w(0xF555, 0xA0)
    o.w(0xDE00, 0xC5)
    o.w(0xE077, 0x81)
    o.w(0xDE00, 0x05)                       # 8K: read at $8000
    o.p(0x8077, P(5, 0, 0x077) & 0x81)
    o.ddr += [(5 * 0x4000 + 0x77, P(5, 0, 0x077) & 0x81)]
    chips = [(b, 0x8000, chip(b, 0)) for b in range(64)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("GMOD2", 60, 0, 1, chips), o, []


@test
def simons_basic():
    o = Ops()
    o.r(0xB234, P(0, 1, 0x1234))            # 16K at start (icepi's model, as VICE)
    o.q(0xDE00)                             # read IO1: 8K, BASIC at $A000
    o.r(0xA001, 0xE3)
    o.w(0xDE00, 0)                          # write IO1: 16K
    o.r(0xB235, P(0, 1, 0x1235))
    o.q(0xDE00)
    o.r(0x9234, P(0, 0, 0x1234))
    c = with_boot(chip(0, 0), o, 0x8000)
    return crt("SIMONS BASIC", 4, 0, 0, [(0, 0x8000, c), (0, 0xA000, chip(0, 1))]), o, []


@test
def action_replay():
    return action_replay_ops(True)


@test
def action_replay_on():
    """the same, without switching the cartridge off: the freeze session's
    look (--peek) sees it active"""
    return action_replay_ops(False)


def action_replay_ops(off):
    o = Ops()
    o.w(0xDE00, 0x08)                       # bank 1
    o.r(0x9F00, P(1, 0, 0x1F00))
    o.w(0xDE00, 0x18)                       # bank 3
    o.r(0x9F00, P(3, 0, 0x1F00))
    o.w(0xDE00, 0x20)                       # RAM at $8000 and in IO2
    o.w(0x8100, 0x99)
    o.r(0x8100, 0x99)
    o.w(0xDF10, 0x77)
    o.r(0xDF10, 0x77)
    o.r(0x9F10, 0x77)
    o.w(0xDE00, 0x00)
    o.r(0x8900, P(0, 0, 0x900))
    if off:
        o.w(0xDE00, 0x04)                   # bit 2: off
        o.r(0x8100, 0x99)
    chips = [(b, 0x8000, chip(b, 0)) for b in range(4)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("ACTION REPLAY", 1, 0, 1, chips), o, []



@test
def magic_desk_16k():
    o = Ops()
    o.w(0x8800, 0x5A)                       # RAM below the ROM
    bank_walk(o, [1, 2, 9, 63, 127, 0], 0xDE00, 0x1F40, 0x1E41)
    o.w(0xDE00, 0x80)                       # bit 7: off
    o.r(0x8800, 0x5A)
    chips = [(b, 0x8000, chip(b, 0)) for b in range(128)] + [(b, 0xA000, chip(b, 1)) for b in range(128)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("MAGIC DESK 16K", 85, 0, 0, chips), o, []


@test
def dinamic():
    o = Ops()
    for b in [1, 5, 15, 2, 0]:
        o.q(0xDE00 + b)                     # the address of an IO1 read is the bank
        o.r(0x9F00 + b, P(b, 0, 0x1F00 + b))
    chips = [(b, 0x8000, chip(b, 0)) for b in range(16)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("DINAMIC", 17, 0, 1, chips), o, []


@test
def twomegabyter():
    """CRT 89 (T33): 128 banks of 16K, $DE02 bit 0 EXROM, bit 1 GAME; Protovision's flash API's way"""
    o = Ops()
    o.w(0x8010 + 0x1F00, 0x77)              # RAM below the ROM (writes in 16K mode go there)
    bank_walk(o, [1, 2, 64, 127, 100, 0], 0xDE00, 0x1F20, 0x1E21)
    o.w(0xDE01, 33)                         # A1 alone decodes: $DE01 = $DE00
    o.r(0x9F22, P(33, 0, 0x1F22))
    o.w(0xDE00, 0)
    o.w(0xDE02, 0x02)                       # 8K: BASIC at $A000
    o.r(0xA001, 0xE3)
    o.w(0xDE02, 0x01)                       # Ultimax: ROMH at $E000
    o.r(0xE010, P(0, 1, 0x010))
    o.w(0xDE02, 0x03)                       # off
    o.r(0x9F10, 0x77)
    o.w(0xDE02, 0x00)                       # 16K
    o.w(0xDE00, 120)                        # program ROMH of bank 120 through $F234 (Ultimax)
    o.w(0xDE02, 0x01)
    o.amd(0x8555, 0x82AA, 0x8555, 0xA0)
    o.w(0xF234, 0x3C)
    o.w(0xDE02, 0x00)
    o.p(0xB234, P(120, 1, 0x1234) & 0x3C)   # polled in 16K mode, as the API does
    o.w(0xDE00, 124)                        # sector erase SA31: $1F0000 = banks 124-127
    o.w(0xDE02, 0x01)
    o.amd(0x8555, 0x82AA, 0x8555, 0x80)
    o.amd(0x8555, 0x82AA, 0x8000, 0x30)
    o.w(0xDE02, 0x00)
    o.p(0x8000, 0xFF)
    o.w(0xDE00, 127)
    o.r(0xBFFF, 0xFF)
    o.w(0xDE00, 123)
    o.r(0x9FFF, P(123, 0, 0x1FFF))
    o.w(0xDE00, 0)
    o.ddr += [(120 * 0x4000 + 0x3234, P(120, 1, 0x1234) & 0x3C), (124 * 0x4000, 0xFF),
              (127 * 0x4000 + 0x3FFF, 0xFF), (123 * 0x4000 + 0x3FFF, P(123, 1, 0x1FFF))]
    chips = []
    for b in range(128):
        lo = with_boot(chip(0, 0), o, 0x8000) if b == 0 else chip(b, 0)
        chips.append((b, 0x8000, bytes(lo) + bytes(chip(b, 1))))
    return crt("TWOMEGABYTER", 89, 0, 0, chips), o, []


@test
def c64megacart():
    """CRT 88 (T33): 256 banks of 8K, $DF00 mode: 00 8K, 01 hold, 10 off, 11 Ultimax in writes only"""
    o = Ops()
    o.w(0x9F10, 0x55)                       # RAM below the ROM
    for b in [1, 77, 128, 255, 200, 0]:
        o.w(0xDE00 + (b & 0x3F), b)         # every IO1 address is the bank register
        o.r(0x9F00 + b % 200, P(b, 0, 0x1F00 + b % 200))
    o.w(0xDF80, 0x80)                       # off: RAM
    o.r(0x9F10, 0x55)
    o.w(0xDF00, 0x40)                       # 01: holds off
    o.r(0x9F10, 0x55)
    o.w(0xDF00, 0x00)                       # 8K
    o.r(0x9F10, P(0, 0, 0x1F10))
    o.w(0xDF00, 0x40)                       # 01: holds 8K
    o.r(0x9F11, P(0, 0, 0x1F11))
    o.w(0xDE00, 250)
    o.w(0xDF00, 0xC0)                       # 11: reads are the C64's own map
    o.r(0xA001, 0xE3)                       # BASIC
    o.amd(0xEAAA, 0xE555, 0xEAAA, 0xA0)     # writes through $E000 reach the flash
    o.w(0xE321, 0x5A)
    o.w(0xDF00, 0x00)
    o.p(0x8321, P(250, 0, 0x321) & 0x5A)
    o.w(0xDE00, 0)
    o.ddr += [(250 * 0x4000 + 0x321, P(250, 0, 0x321) & 0x5A)]
    chips = [(b, 0x8000, chip(b, 0)) for b in range(256)]
    chips[0] = (0, 0x8000, with_boot(chip(0, 0), o, 0x8000))
    return crt("C64MEGACART", 88, 0, 1, chips), o, []


@test
def easyflash_xl():
    """CRT 232 (T33, PROP-002 §8.3): EasyFlash with 256 banks, two 2 MB chips; app configuration only"""
    o = Ops()
    o.w(0xDE02, 0x07)                       # 16K
    bank_walk(o, [1, 100, 200, 255, 64, 0], 0xDE00, 0x1F00, 0x1E00)
    o.w(0xDE02, 0x05)                       # Ultimax
    o.w(0xDE00, 200)                        # sector erase, chip 1, the sector of bank 200 (banks 200-207)
    o.amd(0xE555, 0xE2AA, 0xE555, 0x80)
    o.amd(0xE555, 0xE2AA, 0xE000, 0x30)
    o.p(0xE000, 0xFF)
    o.w(0xDE00, 207)
    o.r(0xFFF0, 0xFF)
    o.w(0xDE00, 8)                          # 64 KB sectors on a 2 MB chip: bank 8 is not hit
    o.r(0xE000, P(8, 1, 0))
    o.w(0xDE00, 208)
    o.r(0xE000, P(208, 1, 0))
    o.w(0xDE00, 0)
    o.ddr += [(200 * 0x4000 + 0x2000, 0xFF), (207 * 0x4000 + 0x3FFF, 0xFF), (8 * 0x4000 + 0x2000, P(8, 1, 0)),
              (200 * 0x4000, P(200, 0, 0))]
    chips = []
    for b in range(256):
        chips.append((b, 0x8000, chip(b, 0)))
        chips.append((b, 0xA000, with_boot(chip(0, 1), o, 0xE000, vectors=True) if b == 0 else chip(b, 1)))
    return crt("EASYFLASH XL", 232, 1, 0, chips), o, []

def ef3_chips(boot_chips):
    """CRT type 90 (T51): 512 banks slot-major, bank field = slot * 64 + bank; boot_chips: {(bank, half): data}"""
    chips = []
    for b in range(512):
        chips.append((b, 0x8000, boot_chips.get((b, 0), chip(b, 0))))
        chips.append((b, 0xA000, boot_chips.get((b, 1), chip(b, 1))))
    return chips


def ef3_unlock(o, cmd):
    """the EAPI's way: the unlock cycles at slot 0 bank 0 (Ultimax), the command cycle at $AAA"""
    o.w(0xDE01, 0)
    o.w(0xDE00, 0)
    o.w(0x8AAA, 0xAA)
    o.w(0x8555, 0x55)
    o.w(0x8AAA, cmd)


@test
def easyflash3():
    """EasyFlash 3 (T51, banking only: no menu, no KERNAL mode): the C64 boots slot 0 bank 0 (Ultimax); slots and
    banks through $DE01/$DE00, IO2 RAM, the MX29LV640 flash in the chip's own address order: autoselect, program
    ROML/ROMH, 64 KB sector, 8 KB boot sector, then a chip erase that wipes the whole 8 MB image"""
    o = Ops()                               # slot 0, bank 0 ROMH: what the cartridge boots
    o.r(0xDE01, 0)                          # the slot register reads back, slot 0 after a reset
    o.w(0xDE02, 0x07)                       # 16K
    for slot, banks in [(2, [0, 1, 9, 31, 63]), (5, [0, 7, 8, 40, 63]), (7, [63, 1]), (1, [2]), (4, [0, 33])]:
        o.w(0xDE01, slot)
        for b in banks:
            o.w(0xDE00, b)
            o.r(0x9F00, P(slot * 64 + b, 0, 0x1F00))
            o.r(0xBE00, P(slot * 64 + b, 1, 0x1E00))
    o.w(0xDF00, 0x5A)                       # 256 bytes of RAM
    o.w(0xDF7F, 0xA5)
    o.r(0xDF00, 0x5A)
    o.r(0xDF7F, 0xA5)
    o.w(0xDE02, 0x05)                       # Ultimax: the flash takes writes
    ef3_unlock(o, 0x90)                     # autoselect
    o.r(0x8000, 0xC2)
    o.r(0x8002, 0xCB)
    o.w(0x8000, 0xF0)
    t = 6 * 64 + 0x2B                       # slot 6, bank_hi 5, bank_lo 3
    o.w(0xDE01, 6)
    o.w(0xDE00, 0x2B)
    o.r(0x8100, P(t, 0, 0x100))             # its lines are cached now
    o.r(0xE101, P(t, 1, 0x101))
    ef3_unlock(o, 0xA0)                     # program ROML
    o.w(0xDE01, 6)
    o.w(0xDE00, 0x2B)
    o.w(0x8100, 0xC3)
    o.p(0x8100, P(t, 0, 0x100) & 0xC3)
    ef3_unlock(o, 0xA0)                     # program ROMH
    o.w(0xDE01, 6)
    o.w(0xDE00, 0x2B)
    o.w(0xE101, 0x3C)
    o.p(0xE101, P(t, 1, 0x101) & 0x3C)
    o.w(0xDE00, 0x08)                       # slot 6, banks 8-15
    o.r(0x8040, P(6 * 64 + 8, 0, 0x040))
    ef3_unlock(o, 0x80)                     # 64 KB sector erase: ROML of banks 8-15
    o.w(0x8AAA, 0xAA)
    o.w(0x8555, 0x55)
    o.w(0xDE01, 6)
    o.w(0xDE00, 0x08)
    o.w(0x8000, 0x30)
    o.p(0x8040, 0xFF)
    o.w(0xDE00, 0x0F)
    o.r(0x9FFF, 0xFF)
    o.r(0xE000, P(6 * 64 + 15, 1, 0))       # ROMH of the same banks: kept
    o.w(0xDE00, 0x10)
    o.r(0x8000, P(6 * 64 + 16, 0, 0))       # the next sector: kept
    o.w(0xDE00, 0x07)
    o.r(0x8000, P(6 * 64 + 7, 0, 0))        # the sector before: kept
    ef3_unlock(o, 0x80)                     # 8 KB boot sector: slot 0 bank 3 ROML only
    o.w(0x8AAA, 0xAA)
    o.w(0x8555, 0x55)
    o.w(0xDE01, 0)
    o.w(0xDE00, 3)
    o.w(0x8000, 0x30)
    o.p(0x8000, 0xFF)
    o.r(0x9FFF, 0xFF)
    o.w(0xDE00, 2)
    o.r(0x8000, P(2, 0, 0))
    o.w(0xDE00, 4)
    o.r(0x8000, P(4, 0, 0))
    o.w(0xDE00, 3)
    o.r(0xE000, P(3, 1, 0))                 # ROMH of bank 3: not a boot sector
    o.w(0xDE01, 6)                          # chip erase: the whole 8 MB image, system area and the other slots too
    o.w(0xDE00, 0x2B)
    ef3_unlock(o, 0x80)
    o.w(0x8AAA, 0xAA)
    o.w(0x8555, 0x55)
    o.w(0x8AAA, 0x10)
    o.p(0x8100, 0xFF)                       # the programmed byte of slot 6 is gone
    o.r(0xE101, 0xFF)
    o.w(0xDE01, 0)                          # slot 0 bank 0 and the KERNALs: erased
    o.w(0xDE00, 0)
    o.r(0x8000, 0xFF)
    o.r(0xE000, 0xFF)
    o.w(0xDE00, 5)
    o.r(0x8123, 0xFF)
    o.w(0xDE01, 2)                          # slot 2 (the slot that runs the test: its code is in RAM): erased too
    o.w(0xDE00, 9)
    o.r(0x9F00, 0xFF)
    o.r(0xBE00, 0xFF)
    o.w(0xDE01, 7)
    o.w(0xDE00, 63)
    o.r(0x9FFF, 0xFF)
    o.r(0xBFFF, 0xFF)
    o.w(0xDE00, 0)
    o.ddr += [(t * 0x4000 + 0x100, 0xFF), (t * 0x4000 + 0x2101, 0xFF),
              ((6 * 64 + 8) * 0x4000, 0xFF), ((6 * 64 + 15) * 0x4000 + 0x2000, 0xFF),
              (3 * 0x4000, 0xFF), (2 * 0x4000, 0xFF), (4 * 0x4000, 0xFF), (3 * 0x4000 + 0x2000, 0xFF),
              (0, 0xFF), (0x2000, 0xFF), (5 * 0x4000 + 0x123, 0xFF),
              ((2 * 64 + 9) * 0x4000 + 0x1F00, 0xFF), ((2 * 64 + 9) * 0x4000 + 0x3E00, 0xFF),
              (0x2000, 0xFF),                               # the ROMH of slot 0 bank 0 held the test's code
              ((7 * 64 + 63) * 0x4000 + 0x1FFF, 0xFF), ((7 * 64 + 63) * 0x4000 + 0x3FFF, 0xFF)]
    boot = {(0, 1): with_boot(chip(0, 1), o, 0xE000, vectors=True)}
    return crt("EASYFLASH 3", 90, 1, 0, ef3_chips(boot)), o, []


def run(name, fn):
    os.makedirs(work, exist_ok=True)
    data, ops, extra = fn()
    path = os.path.join(work, name + ".crt")
    open(path, "wb").write(data)
    args = [tb, path, "--expect", ops.expect.hex(), "--cycles", "3000000", "--peek"] + extra
    for off, val in ops.ddr:
        args += ["--ddr", "%x=%x" % (off, val)]
    r = subprocess.run(args, cwd=run_dir, capture_output=True, text=True)
    ok = r.returncode == 0
    print("%-14s %s" % (name, "ok" if ok else "FAILED"))
    if not ok or os.environ.get("VERBOSE"):
        print(r.stdout)
    else:
        for line in r.stdout.splitlines():
            if line.startswith("ran ") or "DDR2 requests" in line or line.startswith("look: ") \
                    or line.startswith("real: ") or line.startswith("romh "):
                print("               " + line)
    return ok


def emit(d, names):
    """the CRT and its expected result bytes per test, for tests/cart/carts_board.py"""
    os.makedirs(work, exist_ok=True)
    os.makedirs(d, exist_ok=True)
    for name in names:
        data, ops, extra = tests[name]()
        if extra:                           # --inactive / --kill: checks of the bench only
            continue
        open(os.path.join(d, name + ".crt"), "wb").write(data)
        open(os.path.join(d, name + ".expect"), "w").write(ops.expect.hex() + "\n")
        print("%-14s %d result bytes" % (name, len(ops.expect)))
    return 0


def main():
    if "--list" in sys.argv:
        print(" ".join(tests))
        return 0
    if "--emit" in sys.argv:
        i = sys.argv.index("--emit")
        rest = sys.argv[i + 2:] + sys.argv[1:i]
        return emit(sys.argv[i + 1], [a for a in rest if not a.startswith("-")] or list(tests))
    names = [a for a in sys.argv[1:] if not a.startswith("-")] or list(tests)
    bad = [n for n in names if not run(n, tests[n])]
    for item in os.environ.get("CARTS_EXTRA", "").split():
        f, _, cyc = item.partition(":")
        r = subprocess.run([tb, f, "--cycles", cyc or "2000000", "--screen", "--ppm",
                            os.path.join(work, os.path.basename(f) + ".ppm")],
                           cwd=run_dir, capture_output=True, text=True)
        print(r.stdout)
    print("all passed" if not bad else "failed: " + " ".join(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
