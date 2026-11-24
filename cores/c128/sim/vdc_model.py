#!/usr/bin/env python3
"""T113 S8: an independent model of the C128's VDC (8563) and the three test programs of the bench.

Written from the 8563 programmer's reference (register set R0-R37, status bit 7, data port R31 with auto-increment, update address
R18/R19, block copy/fill R24 b7 / R30, 80 x 25 text with attributes, 640 x 200 bitmap) and NOT from the MiSTer RTL. The model has no
timing: it applies the register semantics to a 64 KiB RAM and renders the frame the registers describe.

  vdc_model.py gen <dir>                  writes stim_N.hex (bus operations for tb_c128_vdc) and exp_N.json (+ frame_N.txt) for N = 1..10
  vdc_model.py check <dir> N <prefix>
                                          compares what the testbench wrote (<prefix>.reads/.busy/.vram/.frame) with the model

Bus operation words, 16 bit hex, op in the high byte (the testbench decodes them):
  00 vv write $D600 (register select)   01 vv write $D601 (data)   02 read $D600 (logged)   03 read $D601 (logged)
  04 wait until status bit 7 is set     05 vv idle vv*256 clocks   06 measure the busy time after the last write (logged)
  07 vv skip vv frames                  08 capture the next whole frame   FF end (dump the VRAM)
Test 1: registers, data port, fill, copy, busy flag, 64 KiB wrap.  Test 2: 80 x 25 text with attributes.  Test 3: 640 x 200 bitmap.
Test 4 / 5 / 6-10: test 2 with R25 low nibble 0 (the value the C128 flat KERNAL programs) / 3 / 1, 2, 4, 5, 6 (test 2 is 7). The model's picture is the character grid from x = 0
(character 0, bit 7 is the first pixel of the first line); the exported window sits on that grid for every R25 low nibble (T113 D1).
"""
import hashlib
import json
import os
import sys

W, H = 640, 200

# the register values the C128's KERNAL programs for 80 columns (NTSC) plus the bench's memory layout:
# screen RAM $0000, attribute RAM $0800, character set $2000 (alternate set at +$1000), 64 KiB RAM selected (R28 b4)
INIT = [(0, 126), (1, 80), (2, 102), (3, 0x49), (4, 32), (5, 0), (6, 25), (7, 29), (8, 0), (9, 7), (10, 0x20), (11, 7),
        (12, 0), (13, 0), (14, 0), (15, 0), (20, 0x08), (21, 0), (22, 0x78), (23, 8), (24, 0x20), (25, 0x40), (26, 0xF4),
        (27, 0), (28, 0x30), (29, 7), (34, 125), (35, 100), (36, 5)]

# bits of a register that read back (the others are unused and read as 1 on the real chip; the bench masks them)
RMASK = {5: 0x1F, 8: 0x03, 9: 0x1F, 10: 0x7F, 11: 0x1F, 23: 0x1F, 28: 0xF0, 29: 0x1F, 36: 0x0F}


class Model:
    def __init__(self):
        self.r = [0] * 38
        self.vram = [None] * 65536
        self.sel = 0
        self.ua = 0
        self.fillbyte = None
        self.read31_ok = False       # R31 holds the byte at UA (set by writing R18/R19 or the data port, lost by a block operation)

    def write(self, rs, v):
        if not rs:
            self.sel = v & 0x3F
            return
        n = self.sel
        if n == 18:
            self.ua = (self.ua & 0x00FF) | (v << 8)
            self.read31_ok = True
        elif n == 19:
            self.ua = (self.ua & 0xFF00) | v
            self.read31_ok = True
        elif n == 30:
            cnt = v if v else 256
            if self.r[24] & 0x80:    # block copy from BA to UA
                for _ in range(cnt):
                    b = self.vram[self.r[32] << 8 | self.r[33]]
                    assert b is not None, "model: copy from an undefined address"
                    self.vram[self.ua] = b
                    self.ua = (self.ua + 1) & 0xFFFF
                    ba = ((self.r[32] << 8 | self.r[33]) + 1) & 0xFFFF
                    self.r[32], self.r[33] = ba >> 8, ba & 0xFF
            else:                    # block fill with the byte last written to R31
                assert self.fillbyte is not None, "model: fill without a byte written to R31"
                for _ in range(cnt):
                    self.vram[self.ua] = self.fillbyte
                    self.ua = (self.ua + 1) & 0xFFFF
            self.read31_ok = False
        elif n == 31:
            self.vram[self.ua] = v
            self.fillbyte = v
            self.ua = (self.ua + 1) & 0xFFFF
            self.read31_ok = True
        elif n < 38:
            self.r[n] = v

    def read_data(self):
        """-> (value, mask) of a read of $D601 with the current selection"""
        n = self.sel
        if n == 18:
            return self.ua >> 8, 0xFF
        if n == 19:
            return self.ua & 0xFF, 0xFF
        if n == 31:
            assert self.read31_ok, "model: R31 read after a block operation without setting UA"
            b = self.vram[self.ua]
            assert b is not None, "model: read of an undefined address %04x" % self.ua
            self.ua = (self.ua + 1) & 0xFFFF
            return b, 0xFF
        m = RMASK.get(n, 0xFF)
        return self.r[n] & m, m

    # ------------------------------------------------------------------ the picture
    def render(self):
        r = self.r
        hd, vd, ctv = r[1], r[6], r[9] & 0x1F
        ai = r[27]
        ds = r[12] << 8 | r[13]
        aa = r[20] << 8 | r[21]
        bitmap = bool(r[25] & 0x80)
        atr = bool(r[25] & 0x40)
        rvs_all = (r[24] >> 6) & 1
        fg_g, bg_g = r[26] >> 4, r[26] & 15
        cb = (r[28] >> 5) << 13
        ul = r[29] & 0x1F
        lines_per_row = ctv + 1
        assert not (r[25] & 0x30), "model: semi-graphic / double width are not modelled"
        v = self.vram

        def get(a):
            assert v[a & 0xFFFF] is not None, "model: frame reads undefined VRAM %04x" % a
            return v[a & 0xFFFF]

        rows = []
        if bitmap:
            assert not atr, "model: bitmap with attributes is not modelled"
            for y in range(vd * lines_per_row):
                a = ds + y * (hd + ai)
                row = []
                for cx in range(hd):
                    b = get(a + cx)
                    for bit in range(8):
                        on = ((b >> (7 - bit)) & 1) ^ rvs_all
                        row.append(fg_g if on else bg_g)
                rows.append(row)
        else:
            for cy in range(vd):
                a = ds + cy * (hd + ai)
                at = aa + cy * (hd + ai)
                for line in range(lines_per_row):
                    row = []
                    for cx in range(hd):
                        code = get(a + cx)
                        attr = get(at + cx) if atr else 0
                        assert not (attr & 0x10), "model: blink is not modelled"
                        alt = 0x1000 if (atr and attr & 0x80) else 0
                        b = get(cb + alt + code * 16 + line)
                        fg = attr & 15 if atr else fg_g
                        rvs = rvs_all ^ ((attr >> 6) & 1 if atr else 0)
                        for bit in range(8):
                            on = (b >> (7 - bit)) & 1
                            if atr and (attr & 0x20) and line == ul:
                                on = 1
                            on ^= rvs
                            row.append(fg if on else bg_g)
                    rows.append(row)
        return rows


class Prog:
    """A test program: bus operations for the bench, the expectations the model computes from the same operations."""

    def __init__(self):
        self.m = Model()
        self.ops = []
        self.reads = []       # (label, expected, mask) in the order the bench logs them
        self.busy = []        # (label, count) for the 06 operations
        self.frame = None

    def w(self, rs, v):
        self.ops.append(((1 if rs else 0) << 8) | v)
        self.m.write(rs, v)

    def wait(self):
        self.ops.append(0x0400)

    def sel(self, n):
        if self.m.sel != n:
            self.w(0, n)

    def reg(self, n, v, wait=True):
        self.w(0, n)
        self.w(1, v)
        if wait:
            self.wait()

    def init_regs(self, overrides=None):
        d = dict(INIT)
        d.update(overrides or {})
        for n, v in INIT:
            self.reg(n, d[n])

    def set_ua(self, a):
        self.reg(18, a >> 8)
        self.reg(19, a & 0xFF)

    def data(self, v):
        self.sel(31)
        self.w(1, v)
        self.wait()

    def rd_status(self, label, expect, mask):
        self.ops.append(0x0200)
        self.reads.append((label, expect, mask))

    def rd_data(self, label):
        v, mask = self.m.read_data()
        self.ops.append(0x0300)
        self.reads.append((label, v, mask))
        self.wait()

    def rd_reg(self, n, label):
        self.sel(n)
        self.rd_data(label)

    def measure(self, label, count):
        self.ops.append(0x0600)
        self.busy.append((label, count))

    def block(self, count, label=None):
        """write R30 (count 1..256) and, if labelled, measure the busy time"""
        self.w(0, 30)
        self.w(1, count & 0xFF)
        if label:
            self.measure(label, count)
        self.wait()

    def fill(self, addr, val, n):
        self.set_ua(addr)
        self.data(val)
        rest = n - 1
        while rest > 0:
            k = min(256, rest)
            self.block(k)
            rest -= k

    def copy(self, src, dst, n):
        self.reg(32, src >> 8)
        self.reg(33, src & 0xFF)
        self.reg(24, 0xA0)
        self.set_ua(dst)
        self.block(n)
        self.reg(24, 0x20)

    def skip(self, n):
        self.ops.append(0x0700 | n)

    def capture(self):
        self.ops.append(0x0800)
        self.frame = self.m.render()

    def end(self):
        self.ops.append(0xFF00)


# ---------------------------------------------------------------------------------------------------------------------------------
def prog1():
    p = Prog()
    p.init_regs()
    # status: bit 7 = 1 when idle; bits 2:0 = chip revision (the 8563R9 reports 1)
    p.rd_status("t1.status.idle", 0x81, 0x87)
    # the register file reads back what was written (unused bits masked)
    for n, v in [(12, 0x12), (13, 0x34), (14, 0x56), (15, 0x78), (20, 0x9A), (21, 0xBC), (26, 0xA5), (27, 0x0F), (32, 0x1E),
                 (33, 0xF0)]:
        p.reg(n, v)
        p.rd_reg(n, "t1.reg.R%d" % n)
    for n in (0, 1, 2, 3, 4, 6, 7, 9, 10, 11, 22, 23, 28, 29, 34, 35, 36):
        p.rd_reg(n, "t1.reg.R%d" % n)
    p.reg(12, 0)
    p.reg(13, 0)
    p.reg(20, 0x08)
    p.reg(21, 0)
    p.reg(26, 0xF4)
    p.reg(27, 0)
    # data port: write 16 bytes with auto-increment, UA follows
    p.set_ua(0x0100)
    p.rd_reg(18, "t1.ua.set")
    p.rd_reg(19, "t1.ua.set")
    for i in range(16):
        p.data((0x30 + i * 3) ^ 0x5A)
    p.rd_reg(18, "t1.ua.after_write")
    p.rd_reg(19, "t1.ua.after_write")
    p.set_ua(0x0100)
    p.sel(31)
    for i in range(16):
        p.rd_data("t1.rw.readback")
    p.rd_reg(18, "t1.ua.after_read")
    p.rd_reg(19, "t1.ua.after_read")
    # block fill: the byte written to R31, then R30 = n more bytes; 255 -> bytes $200-$2FF all $5A
    p.set_ua(0x0200)
    p.data(0x5A)
    p.reg(30, 255, wait=False)
    p.measure("t1.busy.fill255", 255)
    p.wait()
    p.rd_reg(18, "t1.fill.ua")
    p.rd_reg(19, "t1.fill.ua")
    p.set_ua(0x0200)
    p.sel(31)
    for i in range(256):
        p.rd_data("t1.fill.data")
    # R30 = 0 is 256 bytes; $401 stays untouched
    p.set_ua(0x0401)
    p.data(0x77)
    p.set_ua(0x0300)
    p.data(0xC3)
    p.reg(30, 0, wait=False)
    p.measure("t1.busy.fill256", 256)
    p.wait()
    for a in (0x02FF, 0x0300, 0x0301, 0x0400, 0x0401):
        p.set_ua(a)
        p.rd_reg(31, "t1.fill256.%04x" % a)
    # block copy BA -> UA, 16 bytes; BA and UA run on
    p.reg(32, 0x01)
    p.reg(33, 0x00)
    p.reg(24, 0xA0)
    p.set_ua(0x0500)
    p.reg(30, 16, wait=False)
    p.measure("t1.busy.copy16", 16)
    p.wait()
    p.reg(24, 0x20)
    p.rd_reg(32, "t1.copy.ba")
    p.rd_reg(33, "t1.copy.ba")
    p.rd_reg(18, "t1.copy.ua")
    p.rd_reg(19, "t1.copy.ua")
    p.set_ua(0x0500)
    p.sel(31)
    for i in range(16):
        p.rd_data("t1.copy.data")
    # overlapping copy (destination = source + 1) runs byte by byte: the first byte propagates
    p.reg(32, 0x05)
    p.reg(33, 0x00)
    p.reg(24, 0xA0)
    p.set_ua(0x0501)
    p.block(15)
    p.reg(24, 0x20)
    p.set_ua(0x0500)
    p.sel(31)
    for i in range(16):
        p.rd_data("t1.copy.overlap")
    # 64 KiB: four distinct 4 byte patterns at $0000, $4000, $8000, $C000 (a 16 KiB VDC aliases them all)
    for k, a in enumerate((0x0000, 0x4000, 0x8000, 0xC000)):
        p.set_ua(a)
        for j in range(4):
            p.data(0xA0 + 16 * k + j)
    for a in (0x0000, 0x4000, 0x8000, 0xC000):
        p.set_ua(a)
        p.sel(31)
        for j in range(4):
            p.rd_data("t1.wrap.%04x" % a)
    # UA is 16 bit and wraps from $FFFF to $0000
    p.set_ua(0xFFFE)
    for v in (0xE0, 0xE1, 0xE2):
        p.data(v)
    p.rd_reg(18, "t1.wrap.ua")
    p.rd_reg(19, "t1.wrap.ua")
    p.set_ua(0xFFFE)
    p.sel(31)
    for j in range(3):
        p.rd_data("t1.wrap.ua.data")
    p.end()
    return p


def glyph(alt, code, line):
    return ((code * 0x9D + line * 0x35 + alt * 0x71 + (code >> 3) * 7) ^ ((code * line) & 0xFF)) & 0xFF


def tcode(r, c):
    return (r * 31 + c * 7 + r * c) & 0xFF


def tattr(r, c):
    v = (r + 2 * c) % 15
    fg = v if v < 4 else v + 1                 # never the background colour (4)
    alt = 1 if (r * 3 + c) % 5 == 0 else 0
    rvs = 1 if c % 7 == 3 else 0
    ul = 1 if (r % 4 == 1 and c % 3 == 0) else 0
    return fg | (ul << 5) | (rvs << 6) | (alt << 7)


def prog2(hss=7):
    p = Prog()
    p.init_regs({25: 0x40 | hss})
    for alt in (0, 1):
        for code in range(256):
            p.set_ua(0x2000 + alt * 0x1000 + code * 16)
            for line in range(8):
                p.data(glyph(alt, code, line))
    p.set_ua(0x0000)
    for r in range(25):
        for c in range(80):
            p.data(tcode(r, c))
    p.set_ua(0x0800)
    for r in range(25):
        for c in range(80):
            p.data(tattr(r, c))
    p.skip(3)
    p.capture()
    p.end()
    return p


def prog3():
    p = Prog()
    p.init_regs({25: 0x87, 26: 0xE1})
    p.fill(0, 0x00, 16000)
    for y in range(0, 200, 8):
        p.fill(y * 80, 0xFF, 80)
    for y in range(200):
        x = (y * 3) % 640
        a = y * 80 + x // 8
        p.set_ua(a)
        p.data((p.m.vram[a] | (0x80 >> (x % 8))) & 0xFF)
    for y in range(20, 60):
        p.set_ua(y * 80 + 5)
        for j in range(5):
            p.data((y * 13 + j * 71 + 5) & 0xFF)
    p.copy(80 * 3, 80 * 150, 160)
    p.skip(3)
    p.capture()
    p.end()
    return p


def prog11():
    """T257 fault 8: the outline CREST part's frame routine as the demo plays it: bitmap mode, copy on, then 100 times (R32/R33 = the line's source, R30 = 80)
    with the status-bit-7 wait; every block's busy time is measured (06)."""
    p = Prog()
    p.init_regs({25: 0x87, 26: 0xE1})
    p.fill(0, 0x55, 16000)
    p.set_ua(0x2000)
    for j in range(80):
        p.data((j * 5 + 1) & 0xFF)
    p.reg(24, 0xA0)
    p.set_ua(0)
    for i in range(100):
        src = 0x2000 + (i % 7) * 3
        p.reg(32, src >> 8)
        p.reg(33, src & 0xFF)
        p.block(80, "t11.copy80.%d" % i)
    p.reg(24, 0x20)
    p.end()
    return p


OUTLINE_REGS = {1: 0x50, 2: 0x66, 3: 0x49, 4: 0x1E, 5: 0x02, 6: 0x0A, 7: 0x16, 9: 0x09, 10: 0x20, 11: 0x07, 20: 0x08, 23: 0x09, 24: 0x20,
                25: 0x87, 26: 0x11, 28: 0x30, 29: 0x07, 34: 0x7D, 35: 0x64, 36: 0x05}


def prog12():
    """T257 fault 8, the part as it runs: the outline part's registers (100 lines displayed of 312, bitmap), the frame sync of its main loop (status bit 5 low,
    then high), then the frame routine's 100 copies of 80 bytes; marks (0A) at the sync and after the last copy, and a status read right after it:
    bit 5 must still be 1 (the copies are done inside the vertical blank, as on VICE where they take 9.6 ms of the 13.6 ms blank)."""
    p = Prog()
    p.init_regs(OUTLINE_REGS)
    p.fill(0, 0x55, 16000)
    p.set_ua(0x2000)
    for j in range(80):
        p.data((j * 5 + 1) & 0xFF)
    for rnd in range(3):
        p.ops.append(0x0900)
        p.ops.append(0x0A00 | (rnd * 2))
        p.reg(24, 0xA0)
        p.set_ua(0)
        for i in range(100):
            src = 0x2000 + (i % 7) * 3
            p.reg(32, src >> 8)
            p.reg(33, src & 0xFF)
            p.block(80, "t12.copy80.%d.%d" % (rnd, i))
        p.ops.append(0x0A00 | (rnd * 2 + 1))
        p.rd_status("t12.after100.%d" % rnd, 0x20, 0x20)
    p.reg(24, 0x20)
    p.end()
    return p


# ---------------------------------------------------------------------------------------------------------------------------------
def frame_text(rows):
    return "\n".join("".join("%x" % x for x in row) for row in rows) + "\n"


def fhash(rows):
    return hashlib.sha256(frame_text(rows).encode()).hexdigest()[:16]


def gen(d):
    os.makedirs(d, exist_ok=True)
    for n, mk in ((1, prog1), (2, prog2), (3, prog3), (4, lambda: prog2(0)), (5, lambda: prog2(3)),
                  (6, lambda: prog2(1)), (7, lambda: prog2(2)), (8, lambda: prog2(4)), (9, lambda: prog2(5)), (10, lambda: prog2(6)), (11, prog11), (12, prog12)):
        p = mk()
        with open(os.path.join(d, "stim_%d.hex" % n), "w") as f:
            f.write("\n".join("%04x" % w for w in p.ops) + "\n")
        exp = {"reads": p.reads, "busy": p.busy, "frame": p.frame is not None,
               "vram": [[a, v] for a, v in enumerate(p.m.vram) if v is not None]}
        with open(os.path.join(d, "exp_%d.json" % n), "w") as f:
            json.dump(exp, f)
        if p.frame is not None:
            with open(os.path.join(d, "frame_%d.txt" % n), "w") as f:
                f.write(frame_text(p.frame))
        print("gen test %d: %d bus operations, %d logged reads, frame %s" % (n, len(p.ops), len(p.reads),
              fhash(p.frame) if p.frame else "-"))


def check(d, n, prefix, nominal_clks_per_col=16):
    exp = json.load(open(os.path.join(d, "exp_%d.json" % n)))
    res = []

    def rec(ok, label, msg=""):
        res.append(ok)
        print(("ok   " if ok else "FAIL ") + label + (": " + msg if msg else ""))

    # logged reads, one verdict per label
    got = [int(x, 16) for x in open(prefix + ".reads").read().split()]
    if len(got) != len(exp["reads"]):
        rec(False, "t%d.reads" % n, "%d reads logged, %d expected" % (len(got), len(exp["reads"])))
    else:
        groups = {}
        for (lab, e, m), g in zip(exp["reads"], got):
            groups.setdefault(lab, []).append((e, m, g))
        for lab, items in groups.items():
            bad = [(i, e, g) for i, (e, m, g) in enumerate(items) if (g & m) != (e & m)]
            if bad:
                i, e, g = bad[0]
                rec(False, lab, "%d of %d reads differ; first #%d: expected %02x got %02x (mask %02x)" %
                    (len(bad), len(items), i, e, g, items[i][1]))
            else:
                rec(True, lab, "%d reads" % len(items))
    # busy flag: status bit 7 reads 0 right after the block write; the operation takes at least half a character clock per byte
    # (the VDC does one RAM access per character clock = 8 dots = 16 clk here) and ends within 8 character clocks per byte
    if exp["busy"]:
        lines = [l.split() for l in open(prefix + ".busy").read().splitlines() if l.strip()]
        if len(lines) != len(exp["busy"]):
            rec(False, "t%d.busy" % n, "%d measurements logged, %d expected" % (len(lines), len(exp["busy"])))
        else:
            for (lab, cnt), (st, clks) in zip(exp["busy"], lines):
                st, clks = int(st, 16), int(clks)
                lo, hi = cnt * nominal_clks_per_col // 2, cnt * nominal_clks_per_col * 8 + 4000
                if st & 0x80:
                    rec(False, lab, "status bit 7 is set right after the write (status %02x): the busy flag does not show the operation" % st)
                elif not (lo <= clks <= hi):
                    rec(False, lab, "ready after %d clocks, expected %d..%d" % (clks, lo, hi))
                else:
                    rec(True, lab, "bit 7 = 0 right after the write, ready after %d clocks" % clks)
    # VRAM: every address the model knows
    vr = [int(x, 16) for x in open(prefix + ".vram").read().split()]
    bad = [(a, v, vr[a] if a < len(vr) else -1) for a, v in exp["vram"] if a >= len(vr) or vr[a] != v]
    if bad:
        a, v, g = bad[0]
        rec(False, "t%d.vram" % n, "%d of %d defined addresses differ; first %04x: expected %02x got %02x" % (len(bad), len(exp["vram"]), a, v, g))
    else:
        rec(True, "t%d.vram" % n, "%d addresses equal the model" % len(exp["vram"]))
    # the picture
    if exp["frame"]:
        mf = os.path.join(d, "frame_%d.txt" % n)
        mrows = open(mf).read().split()
        grows = open(prefix + ".frame").read().split()
        mh = hashlib.sha256((("\n".join(mrows)) + "\n").encode()).hexdigest()[:16]
        gh = hashlib.sha256((("\n".join(grows)) + "\n").encode()).hexdigest()[:16]
        lab = "t%d.frame" % n
        if len(grows) != len(mrows) or any(len(a) != len(b) for a, b in zip(grows, mrows)):
            rec(False, lab, "size: rtl %d lines of %s dots, model %d lines of %d dots (hash model %s rtl %s)" %
                (len(grows), sorted(set(len(x) for x in grows)), len(mrows), len(mrows[0]), mh, gh))
        elif mh == gh:
            rec(True, lab, "hash %s, %d x %d pixels equal the model" % (gh, len(mrows[0]), len(mrows)))
        else:
            cnt = 0
            first = None
            for y, (a, b) in enumerate(zip(mrows, grows)):
                for x in range(len(a)):
                    if a[x] != b[x]:
                        cnt += 1
                        if first is None:
                            first = (x, y, a[x], b[x])
            msg = "hash model %s rtl %s; %d pixels differ; first x=%d y=%d expected %s got %s" % ((mh, gh, cnt) + first)
            rec(False, lab, msg)
    return all(res)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "gen":
        gen(sys.argv[2])
    elif len(sys.argv) >= 5 and sys.argv[1] == "check":
        ok = check(sys.argv[2], int(sys.argv[3]), sys.argv[4])
        sys.exit(0 if ok else 1)
    else:
        print(__doc__)
        sys.exit(2)
