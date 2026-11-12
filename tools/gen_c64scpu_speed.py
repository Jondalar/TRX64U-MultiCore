#!/usr/bin/env python3
"""T182 step 4: the synthetic SuperCPU ROM and the expectations of the speed and register benches S4, S10, S11 (cores/c64scpu/sim/run_speed.sh).

The ROM is our own code (no byte of a CMD or Commodore ROM): 128 KiB, the reset vector (file offset 0xFFFC) points to $8000, the code copies a tail into SRAM ($0300) and runs from there
(the ROM has the +3 EPROM wait). The expectations are computed in this file from the program's meaning and from scpu64mem.c / scpu64cpu.c as read by hand, not read back from the machine.

  gen_c64scpu_speed.py <program> <out dir>   writes <out>/mem/scpu_rom.mem, <out>/mem/c64scpu_rom.mem, <out>/expect.json; for "regs" it prints "+DEXPECT=..." (SRAM bytes at 0x03002000+)
  programs: speed (S4, S10), regs (S11)

speed: phases P0 (7 in all, check_speed.py), each between two markers (a write to 24-bit $00:0B00 / $00:0B01 in SRAM, printed by the bench with the PHI2 cycle it completed in):
  P1 fast, SRAM loop, display on, raster line $34 of a frame with DEN set (badlines inside)   P2 fast, same loop, display off
  P3 1 MHz ($D07A), same loop, display off                                       P4 1 MHz, display on
  P0 fast, the same 1,000 x LDA $D020 before the first raster wait, display as the boot left it   P5 fast ($D07B), 1,000 x LDA $D020, display off                                P6 fast, 1,000 x LDA $D012, display on, raster line $34
  The SRAM loop is LDY #20 / LDX #250 / DEX BNE / DEY BNE: CPU_CYCLES 65816 cycles (computed below by counting, not by running).
regs: the readbacks of $D0B0-$D0BF in four register states against the values read from scpu64mem.c:634-821 (S11), stored to SRAM $2000+.
"""
import json
import os
import sys

SRAM = 0x03000000
SIZE = 0x20000
PHI2 = 985248.0


def pat(k):
    return (k * 13 + (k >> 8) * 5 + (k >> 16) * 0x21 + 1) & 0xFF


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
    def lda_a(self, a): self.e(0xAD, a & 255, a >> 8)
    def sta_a(self, a): self.e(0x8D, a & 255, a >> 8)
    def stx_a(self, a): self.e(0x8E, a & 255, a >> 8)
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


def loop_cycles(outer=20, inner=250):
    """65816 cycles of: LDY #outer / o: LDX #inner / i: DEX / BNE i / DEY / BNE o   (emulation mode, no page crossing)."""
    c = 2                                    # LDY #
    for y in range(outer):
        c += 2                               # LDX #
        c += (inner - 1) * (2 + 3) + (2 + 2)  # DEX + BNE taken, last DEX + BNE not taken
        c += 2                               # DEY
        c += 3 if y < outer - 1 else 2       # BNE
    return c


def sram_loop(a, tag):
    a.ldy_i(20)
    a.label("o" + tag); a.ldx_i(250)
    a.label("i" + tag); a.e(0xCA); a.br(0xD0, "i" + tag)
    a.e(0x88); a.br(0xD0, "o" + tag)


def io_loop(a, tag, reg):
    a.ldy_i(4)
    a.label("o" + tag); a.ldx_i(250)
    a.label("i" + tag); a.lda_a(reg); a.e(0xCA); a.br(0xD0, "i" + tag)
    a.e(0x88); a.br(0xD0, "o" + tag)


def wait_raster(a, tag, line):
    a.label("w" + tag); a.lda_a(0xD012); a.e(0xC9, line); a.br(0xD0, "w" + tag)       # CMP #line / BNE


def mark(a, n):
    a.sta_a(0x0B00 + n)


def speed_tail():
    t = Asm(0x0300)
    t.lda_i(0x0E); t.sta_a(0xD020)                       # border colour 14: the I/O loop of P5 must read it back
    t.sta_a(0xD07B)                                      # 20 MHz
    mark(t, 0); io_loop(t, "0", 0xD020); mark(t, 1)      # P0: 1,000 x LDA $D020 first of all, before any raster wait: a machine whose I/O cycles do not wait for PHI2 (control NOSYNC) shows it here, by the count
    t.lda_i(0x1B); t.sta_a(0xD011)                       # display on (text, 25 rows), in case it is not
    wait_raster(t, "1", 0x34)
    mark(t, 0); sram_loop(t, "1"); mark(t, 1)            # P1
    t.lda_i(0x0B); t.sta_a(0xD011)                       # display off (DEN = 0)
    wait_raster(t, "2", 0x34)
    mark(t, 0); sram_loop(t, "2"); mark(t, 1)            # P2
    t.sta_a(0xD07A)                                      # 1 MHz
    mark(t, 0); sram_loop(t, "3"); mark(t, 1)            # P3
    t.lda_i(0x1B); t.sta_a(0xD011)
    wait_raster(t, "4", 0x34)
    mark(t, 0); sram_loop(t, "4"); mark(t, 1)            # P4
    t.lda_i(0x0B); t.sta_a(0xD011)
    t.sta_a(0xD07B)                                      # 20 MHz
    wait_raster(t, "5", 0x34)                            # DEN is looked at in raster line $30 only: a frame later no line is a badline
    mark(t, 0); io_loop(t, "5", 0xD020); t.sta_a(0x2002); mark(t, 1)      # P5; the last byte read is kept: $D020 reads back $F0 | colour
    t.lda_i(0x1B); t.sta_a(0xD011)
    wait_raster(t, "6", 0x34)
    mark(t, 0); io_loop(t, "6", 0xD012); mark(t, 1)      # P6
    t.lda_i(0x0B); t.sta_a(0xD011)
    t.ldx_i(250)                                         # P7: 250 x STX $D020 at 20 MHz with X = 250 .. 1 (the bus check: tb "bw|" lines, check_speed.py)
    t.label("w7"); t.stx_a(0xD020); t.e(0xCA); t.br(0xD0, "w7")
    t.lda_i(0x5A); t.sta_a(0x2000)                       # done
    t.label("h"); t.br(0x80, "h")
    return t.done()


class Vice:
    """The register file of scpu64mem.c:634-821 (v2), written from the C source: write(addr, value), read(addr)."""
    def __init__(self):
        self.hw = self.sys1 = self.soft1 = self.dos = 0
        self.optim = 0xC7
        self.emu = 1                 # scpu64_emulation_mode: the program runs in emulation mode
        self.sw1 = 0                 # the switch input: SCPU_SW b0 = 1 is 20 MHz (T10), so $D0B5 bit 6 = 0

    def write(self, a, v):
        if a == 0xD072: self.sys1 = 1
        elif a == 0xD073: self.sys1 = 0
        elif a in (0xD074, 0xD075, 0xD076, 0xD077):
            if self.hw: self.optim = (a & 3) << 6
        elif a == 0xD07A: self.soft1 = 1
        elif a in (0xD079, 0xD07B): self.soft1 = 0
        elif a == 0xD07E: self.hw = 1
        elif a in (0xD07D, 0xD07F): self.hw = 0
        elif a == 0xD0B2:
            if self.hw:
                self.sys1 = (v >> 6) & 1
                if not v & 0x80: self.hw = 0
        elif a == 0xD0B3:
            if self.hw: self.optim = (self.optim & 0x38) | (v & 0xC7)
        elif a == 0xD0B4:
            if self.hw: self.optim = (self.optim & 0x3F) | (v & 0xC0)
        elif a == 0xD0B8:
            if self.hw: self.soft1 = v >> 7
        elif a == 0xD0BC:
            if self.hw: self.dos = v >> 7
        elif a == 0xD0BE:
            if self.hw and not self.dos: self.dos = 1
        elif a in (0xD0BD, 0xD0BF):
            self.dos = 0

    def read(self, a):
        v = 0
        if a == 0xD0B0: v = 0x40
        elif a == 0xD0B2: v = (0x80 if self.hw else 0) | (0x40 if self.sys1 else 0)
        elif a in (0xD0B3, 0xD0B4): v = self.optim & 0xC0
        elif a == 0xD0B5: v = 0x40 if self.sw1 else 0
        elif a == 0xD0B6: v = 0x80 if self.emu else 0
        elif a in (0xD0B8, 0xD0B9): v = (0x80 if self.soft1 else 0) | (0x40 if (self.soft1 or (self.sw1 and not self.hw) or self.sys1) else 0)
        elif a in (0xD0BC, 0xD0BD, 0xD0BE, 0xD0BF): v = 0x80 if self.dos else 0
        return v | (self.optim & 7)


def regs_steps():
    """List of ('w', addr, value) and ('r', addr)."""
    s = []
    r = lambda ad: s.append(("r", ad, 0))
    w = lambda ad, v: s.append(("w", ad, v))
    tab = lambda: s.append(("t", 0, 0))                                                    # the 16 registers $D0B0-$D0BF in one go: 16 of the 64 entries of the S11 table
    for ad in (0xD0B0, 0xD0B2, 0xD0B3, 0xD0B4, 0xD0B5, 0xD0B8, 0xD0BC): r(ad)           # state 0: after reset, hwenable off, optim 0xC7
    tab()
    w(0xD0B4, 0x00); w(0xD0B3, 0x00); w(0xD0B8, 0x80); w(0xD0BC, 0x80)                   # hwenable off: these change nothing
    for ad in (0xD0B4, 0xD0B3, 0xD0B8, 0xD0BC): r(ad)
    w(0xD07E, 0); w(0xD0B6, 0); r(0xD0B2)   # D0B6: bootmap off (with bootmap, hwenable and the DOS extension the low pages are the C64's: the program would run off the C64 RAM)
                                                              # state 1: hwenable on
    tab()
    w(0xD0B4, 0x40); r(0xD0B4); r(0xD0B3)
    w(0xD0B3, 0x80); r(0xD0B3)
    w(0xD0B8, 0x80); r(0xD0B8); r(0xD0B2)                                                 # soft 1 MHz
    w(0xD0BC, 0x80); r(0xD0BC)                                                           # DOS extension
    w(0xD072, 0); tab()                                                                  # state 2: hwenable on, system 1 MHz on, soft 1 MHz on, DOS extension on, optim 0x80
    w(0xD07B, 0); r(0xD0B8)                                                              # soft 1 MHz off, system 1 MHz on and off
    w(0xD072, 0); r(0xD0B2); r(0xD0B8)
    w(0xD073, 0); r(0xD0B2)
    w(0xD0B2, 0xC0); r(0xD0B2); w(0xD0B2, 0x80); r(0xD0B2)                               # $D0B2 sets system 1 MHz with hwenable kept
    w(0xD074, 0); r(0xD0B3)                                                              # optimisation mode by address
    w(0xD077, 0); r(0xD0B4)
    w(0xD07F, 0); r(0xD0B2); r(0xD0B3)                                                   # state 3: hwenable off
    tab()
    w(0xD0BF, 0); r(0xD0BC)                                                              # $D0BF is not gated
    return s


def regs_tail():
    t = Asm(0x0300)
    n = 0
    exp = []
    t.lda_i(0)
    m = Vice()
    nt = 0
    for kind, ad, v in regs_steps():
        if kind == "t":
            # LDX #0 / l: LDA $D0B0,X / STA $2200+16*nt,X / INX / CPX #16 / BNE l
            t.ldx_i(0); t.label("t%d" % nt); t.e(0xBD, 0xB0, 0xD0); t.e(0x9D, (0x2200 + 16 * nt) & 255, (0x2200 + 16 * nt) >> 8)
            t.e(0xE8); t.e(0xE0, 16); t.br(0xD0, "t%d" % nt)
            exp += [(SRAM + 0x2200 + 16 * nt + i, m.read(0xD0B0 + i)) for i in range(16)]
            nt += 1
        elif kind == "w":
            t.lda_i(v); t.sta_a(ad)
            m.write(ad, v)
        else:
            t.lda_a(ad); t.sta_a(0x2000 + n)
            exp.append((SRAM + 0x2000 + n, m.read(ad)))
            n += 1
    t.lda_i(0x5A); t.sta_a(0x2100)
    exp.append((SRAM + 0x2100, 0x5A))
    t.label("h"); t.br(0x80, "h")
    return t.done(), exp


def image(tail):
    f = bytearray(pat(k) for k in range(SIZE))
    f[0xFFFC:0xFFFE] = bytes([0x00, 0x80])
    f[0xFFFE:0x10000] = bytes([0x00, 0x80])
    f[0x1FFFC:0x1FFFE] = bytes([0x00, 0x80])
    a = Asm(0x8000)
    a.e(0x78, 0xD8); a.ldx_i(0xFF); a.e(0x9A)            # SEI, CLD, TXS
    a.ldx_i(0)
    a.label("cp"); a.e(0xBD, 0x00, 0x81); a.e(0x9D, 0x00, 0x03); a.e(0xBD, 0x00, 0x82); a.e(0x9D, 0x00, 0x04); a.e(0xE8); a.br(0xD0, "cp")   # LDA $8100,X / STA $0300,X / INX / BNE
    a.jmp(0x0300)
    head = a.done()
    f[0x8000:0x8000 + len(head)] = head
    assert len(tail) <= 0x200, len(tail)
    f[0x8100:0x8100 + len(tail)] = tail
    return f


def main():
    prog, out = sys.argv[1], sys.argv[2]
    if prog == "speed":
        tail = speed_tail()
        exp = {"cpu_cycles": loop_cycles(), "io_loads": 1000, "io_writes": 250, "phi2": PHI2}
        e = [(SRAM + 0x2000, 0x5A), (SRAM + 0x2002, 0xFE)]
    elif prog == "regs":
        tail, e = regs_tail()
        exp = {}
    else:
        sys.exit("unknown program " + prog)
    f = image(tail)
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "scpu_rom.mem"), "w") as fh:
        fh.write("\n".join("%02x" % x for x in f) + "\n")
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as fh:
        fh.write("0000\n" * 10240)
    with open(os.path.join(out, "expect.json"), "w") as fh:
        json.dump(exp, fh)
    if prog in ("regs", "speed"):
        print("+DEXPECT=" + ",".join("%08x:%02x" % (a, v) for a, v in e))


main()
