#!/usr/bin/env python3
"""T182 step 5: the synthetic SuperCPU ROM and the expectations of the mirroring bench S5 (cores/c64scpu/sim/run_mirror.sh, spec T182 section 2 S5 and 5.7).

The ROM is our own code (no byte of a CMD or Commodore ROM): 128 KiB, reset vector $8000, the code copies its tail (8 pages) to SRAM $1000 and runs from there at 20 MHz.
The expectations come from this file, written from scpu64mem.c / scpu64cpu.c as read by hand (MIRRORS = `mem_mirrors` :81-86, index :1474-1475, Vice below = `wait_buffer`
and `scpu64_clock_write_stretch`), not read back from the machine.

  gen_c64scpu_mirror.py <out dir>   writes <out>/mem/scpu_rom.mem, <out>/mem/c64scpu_rom.mem (with a character ROM of our own), <out>/expect.json; prints "+EXPECT=aaaa:vv,..." (C64 RAM bytes)

Program (all in emulation mode):
  setup      display off, screen $0400, colour RAM = 1, every probe page of the C64 RAM = 0 (written through the mirror at the reset mode 0xC7)
  P1         mark 0 / 1,024 x STA $0400-$07FF,X = $20 (the reset mode mirrors it) / mark 1: timed, against the VICE accounting
  hwenable on, boot map off, mode 0x84 (nothing mirrored)
  P2         the same 1,024 stores, unmirrored, timed
  colour RAM = 14 in the unmirrored mode (it is on the C64 bus in every mode)
  probes     nine modes (D0B3 / D076 / D077), in each 16 pages one byte at $pp20 + 3 * mode: in the C64 RAM iff the page is in the mode's range
  picture    mode 0xC7, display on, 512 stores to $0500-$06FF while badlines run (a store the VIC held off is repeated): the picture shows them, the probe cells of $04 and $07 and the rest blank
"""
import json
import os
import sys

PHI2 = 985248
MIRRORS = [0x80bf, 0x80bf, 0x003f, 0x023f, 0x407f, 0x407f, 0xc0ff, 0xc0ff, 0x0407, 0x0407, 0, 0, 0x00ff, 0x02ff, 0x00ff, 0x02ff]
PAGES = [0x00, 0x02, 0x04, 0x07, 0x20, 0x3F, 0x40, 0x50, 0x7F, 0x80, 0x90, 0xB0, 0xBF, 0xC0, 0xE0, 0xFF]
MODES = [(0xD0B3, 0xC7, 0xC7), (0xD0B3, 0x84, 0x84), (0xD076, 0x00, 0x80), (0xD0B3, 0x40, 0x40), (0xD0B3, 0x00, 0x00),
         (0xD077, 0x00, 0xC0), (0xD0B3, 0x44, 0x44), (0xD0B3, 0x04, 0x04), (0xD0B3, 0x05, 0x05)]
ORG = 0x1000


def mirror_index(optim):
    return (1 if optim & 1 else 0) | (2 if optim & 4 else 0) | (4 if optim & 0x40 else 0) | (8 if optim & 0x80 else 0)


def mirrored(optim, page):
    t = MIRRORS[mirror_index(optim)]
    return t != 0 and (t >> 8) <= page <= (t & 0xFF)


def chargen():
    """a character ROM of our own (as tools/gen_c64scpu_test.py): every glyph a distinct pattern, derived from the screen code"""
    rom = bytearray(0x1000)
    for code in range(256):
        for row in range(8):
            if code & 0x7F == 0x20:
                v = 0x00
            else:
                v = ((code * 37 + row * 59 + 0x5A) ^ (code >> 3) * (row + 1)) & 0xFF
                v |= 0x81 if row in (0, 7) else 0x00
            rom[code * 8 + row] = v if code < 128 else v ^ 0xFF
    return rom


class Asm:
    def __init__(self, org):
        self.org, self.b, self.lab, self.fix = org, bytearray(), {}, []

    def pc(self):
        return self.org + len(self.b)

    def label(self, n):
        self.lab[n] = self.pc()

    def e(self, *x):
        self.b += bytes(x)

    def lda_i(self, v): self.e(0xA9, v)
    def ldx_i(self, v): self.e(0xA2, v)
    def ldy_i(self, v): self.e(0xA0, v)
    def and_i(self, v): self.e(0x29, v)
    def cmp_i(self, v): self.e(0xC9, v)
    def lda_a(self, a): self.e(0xAD, a & 255, a >> 8)
    def ora_i(self, v): self.e(0x09, v)
    def eor_i(self, v): self.e(0x49, v)
    def sta_a(self, a): self.e(0x8D, a & 255, a >> 8)
    def sta_ax(self, a): self.e(0x9D, a & 255, a >> 8)
    def jmp(self, a): self.e(0x4C, a & 255, a >> 8)

    def br(self, op, n):
        self.e(op, 0)
        self.fix.append((len(self.b) - 1, n))

    def done(self):
        for at, n in self.fix:
            d = self.lab[n] - (self.org + at + 1)
            assert -128 <= d <= 127, (n, d)
            self.b[at] = d & 255
        return bytes(self.b)


def colour_fill(t, tag, v):
    t.lda_i(v); t.ldx_i(0)
    t.label("c" + tag)
    for pg in (0xD8, 0xD9, 0xDA, 0xDB):
        t.sta_ax(pg << 8)
    t.e(0xE8); t.br(0xD0, "c" + tag)


def screen_fill(t, tag):
    t.sta_a(0x0B00); t.ldx_i(0)                          # mark 0 (A = $20)
    t.label("f" + tag)
    for pg in (4, 5, 6, 7):
        t.sta_ax(pg << 8)
    t.e(0xE8); t.br(0xD0, "f" + tag)
    t.sta_a(0x0B01)                                      # mark 1


def val(mi, pi):
    return 0x40 + (mi * 16 + pi) % 0x3F


def tail():
    t = Asm(ORG)
    t.lda_i(0x0B); t.sta_a(0xD011)                       # display off
    t.lda_i(0xC8); t.sta_a(0xD016)                       # 40 columns
    t.lda_i(0x14); t.sta_a(0xD018)                     # screen $0400, characters $1000 (the ROM at $1000 of bank 0)
    t.lda_i(14); t.sta_a(0xD020)
    t.lda_i(6); t.sta_a(0xD021)
    t.sta_a(0xD07B)                                      # 20 MHz
    colour_fill(t, "1", 1)
    t.lda_i(0)
    for pg in PAGES:
        if pg in (0x04, 0x07):
            continue
        t.ldx_i(2 if pg == 0 else 0)                     # $0000/$0001 are the CPU port
        t.label("z%02x" % pg); t.sta_ax(pg << 8); t.e(0xE8); t.br(0xD0, "z%02x" % pg)
    t.lda_i(0x20)
    screen_fill(t, "1")                                  # P1, reset mode 0xC7
    t.sta_a(0xD07E)                                      # hwenable
    t.sta_a(0xD0B6)                                      # boot map off
    t.lda_i(0x84); t.sta_a(0xD0B3)                       # nothing mirrored
    t.lda_i(0x20)
    screen_fill(t, "2")                                  # P2
    colour_fill(t, "2", 14)
    for mi, (reg, v, _) in enumerate(MODES):
        t.lda_i(v); t.sta_a(reg)
        for pi, pg in enumerate(PAGES):
            t.lda_i(val(mi, pi)); t.sta_a((pg << 8) | (0x20 + 3 * mi))
    t.lda_i(0xC7); t.sta_a(0xD0B3)
    t.lda_i(0x1B); t.sta_a(0xD011)                       # display on
    for tag, line in (("a", 0x10), ("b", 0x32)):         # a frame that has DEN set in line $30 (line $10 first), then the store loop starts in line $32: the badlines come in the middle of it
        t.label("w" + tag); t.lda_a(0xD011); t.br(0x30, "w" + tag); t.lda_a(0xD012); t.cmp_i(line); t.br(0xD0, "w" + tag)   # D011 bit 7 = bit 8 of the line: lines $010-$132 (low byte equal) do not count
    t.ldx_i(0); t.sta_a(0x0B02)                          # mark 2: the picture loop starts
    t.label("pc")
    t.e(0x8A); t.and_i(0x3F); t.ora_i(0x40); t.sta_ax(0x0500); t.eor_i(0x80); t.sta_ax(0x0600); t.e(0xE8); t.br(0xD0, "pc")
    t.sta_a(0x0B03)                                      # mark 3: the picture loop is done
    # retry phase: BRK pushes (emulation mode: PCH, PCL, P to $0100+S, consecutive writes, no read between) while badlines run. A write the VIC holds off (aec low in the c-access cycles) is
    # repeated; a push lands in the C64 RAM stack page only if it is. 63 slots (S = $FD, $F9 ... $05), 8 passes: the last pass's slots are the proof (stack_probes)
    t.lda_i(0xC4); t.sta_a(0xD0B3)                       # optim 0xC4: index 14, $00-$FF mirrored, the stack page too
    t.lda_i(0x36); t.sta_a(0x0001)                       # CPU port: KERNAL visible (class KS with hwenable): the BRK vector pull is rerouted to the ROM's $FFFE (S9, spec 5.8)
    t.ldy_i(8)
    t.label("pass"); t.ldx_i(0xFD)
    t.label("bl"); t.e(0x9A, 0xB8); t.lda_i(0); t.e(0x38, 0x00, 0x00)   # TXS, CLV, LDA #0, SEC, BRK + signature byte
    t.label("ret"); t.e(0x8A, 0x38, 0xE9, 0x04, 0xAA, 0xE0, 0x05); t.br(0xB0, "bl")   # TXA, SEC, SBC #4, TAX, CPX #5, BCS
    t.e(0x88); t.br(0xD0, "pass")                        # DEY, BNE
    t.label("h"); t.br(0x80, "h")
    t.label("hand"); t.e(0x40)                           # the BRK handler: RTI
    return t.done(), t.lab


class Vice:
    """scpu64cpu.c: wait_buffer, scpu64_clock_write_stretch, scpu64_clock_inc (fast mode); time in PHI2 cycles (clk) and 1/20e6 of one (accu)"""
    SHIFT = -2000000

    def __init__(self):
        self.clk, self.accu, self.fin, self.half = 0, 0, 0, 0

    def inc(self):
        self.accu += PHI2
        if self.accu > 20000000:
            self.accu -= 20000000
            self.clk += 1

    def stretch(self):
        if self.fin > self.clk or (self.fin == self.clk and self.half > self.accu):
            self.accu = self.half
            self.clk = max(self.clk, self.fin)
        self.fin = self.clk + 1
        if self.accu >= 20000000 + self.SHIFT:
            self.fin += 1
        self.half = 11500000 + self.SHIFT

    def ins(self, n, store, mirrored):
        for _ in range(n - 1):
            self.inc()
        if store and mirrored:
            self.stretch()
        t = self.clk
        self.inc()
        return t


def model(mirrored_fill):
    """PHI2 cycles from the completion of mark 0 to the completion of mark 1 of the screen fill, in the mode (mirrored or not)"""
    v = Vice()
    t0 = v.ins(4, True, mirrored_fill)                  # STA $0B00
    v.ins(2, False, False)                              # LDX #0
    for x in range(256):
        for _ in range(4):
            v.ins(5, True, mirrored_fill)               # STA $0400+,X
        v.ins(2, False, False)                          # INX
        v.ins(3 if x < 255 else 2, False, False)        # BNE
    t1 = v.ins(4, True, mirrored_fill)                  # STA $0B01
    return t1 - t0


def screen_cells():
    cells = [0x20] * 1024
    for mi, (_, _, optim) in enumerate(MODES):
        for pi, pg in enumerate(PAGES):
            if pg in (0x04, 0x07) and mirrored(optim, pg):
                cells[(pg - 4) * 256 + 0x20 + 3 * mi] = val(mi, pi)
    for x in range(256):
        c = (x & 0x3F) | 0x40
        cells[256 + x] = c
        cells[512 + x] = c ^ 0x80
    return cells


def image():
    f = bytearray(0x20000)
    f[0xFFFC:0xFFFE] = bytes([0x00, 0x80])
    f[0xFFFE:0x10000] = bytes([0x00, 0x80])
    f[0x1FFFC:0x1FFFE] = bytes([0x00, 0x80])
    a = Asm(0x8000)
    a.e(0x78, 0xD8); a.ldx_i(0xFF); a.e(0x9A)            # SEI, CLD, TXS
    a.ldx_i(0)
    a.label("cp")
    for k in range(8):
        a.e(0xBD, 0x00, 0x81 + k); a.e(0x9D, 0x00, (ORG >> 8) + k)   # LDA $8100+k*256,X / STA $1000+k*256,X
    a.e(0xE8); a.br(0xD0, "cp")
    a.jmp(ORG)
    head = a.done()
    f[0x8000:0x8000 + len(head)] = head
    t, lab = tail()
    f[0xFFFE:0x10000] = bytes([lab["hand"] & 255, lab["hand"] >> 8])        # the BRK vector: the handler's RAM copy at $1000 + ...
    t, lab = tail()
    assert len(t) <= 0x800, len(t)
    f[0x8100:0x8100 + len(t)] = t
    return f, lab


def main():
    out = sys.argv[1]
    f, lab = image()
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "scpu_rom.mem"), "w") as fh:
        fh.write("\n".join("%02x" % x for x in f) + "\n")
    img = bytearray(0x5000)
    img[0x4000:0x5000] = chargen()
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as fh:
        for i in range(0, 0x5000, 2):
            fh.write("%02x%02x\n" % (img[i + 1], img[i]))
    cells = screen_cells()
    ch = chargen()
    seen = {}
    for c in set(cells):
        g = tuple(ch[c * 8:c * 8 + 8])
        assert seen.setdefault(g, c) == c, "glyph of %02x is the glyph of %02x" % (c, seen[g])
    rows = [cells[r * 40:(r + 1) * 40] for r in range(25)]
    exp = []
    for mi, (_, _, optim) in enumerate(MODES):
        for pi, pg in enumerate(PAGES):
            ad = (pg << 8) | (0x20 + 3 * mi)
            init = 0x20 if pg in (0x04, 0x07) else 0
            exp.append((ad, val(mi, pi) if mirrored(optim, pg) else init))
    ret = lab["ret"]                                     # the PC the BRK pushes: the address after its signature byte
    for x in range(0xFD, 4, -4):                         # the retry phase's slots: PCH, PCL, P (SEC, CLV, LDA #0: N0 V0 1 B1 D0 I1 Z1 C1 = $37)
        exp += [(0x100 + x, ret >> 8), (0x100 + x - 1, ret & 255), (0x100 + x - 2, 0x37)]
    n_m = sum(1 for _, v in exp if v >= 0x40)
    with open(os.path.join(out, "expect.json"), "w") as fh:
        json.dump({"rows": rows, "chargen": list(ch), "fg": 14, "bg": 6, "border": 14, "p1": model(True), "p2": model(False), "probes": len(exp), "mirrored_probes": n_m}, fh)
    print("+EXPECT=" + ",".join("%04x:%02x" % (a, v) for a, v in exp))


main()
