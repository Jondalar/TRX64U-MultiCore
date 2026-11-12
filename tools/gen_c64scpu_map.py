#!/usr/bin/env python3
"""T182 step 3d: the synthetic SuperCPU ROM and the expectations of the memory-map benches S1 and S6 (cores/c64scpu/sim/run_map.sh).

The ROM is our own code (no byte of a CMD or Commodore ROM): 128 KiB, every byte outside the code is a pattern computed here, the reset vector (file offset 0xFFFC) points to
$8000. The expectations are computed in this file from the program's meaning (a tag per bank, the file pattern at the addresses the program reads), not read back from the machine.

  gen_c64scpu_map.py <program> <out dir>    writes <out>/mem/scpu_rom.mem (one byte per line), <out>/mem/c64scpu_rom.mem (the C64 system ROM slot: zeros), prints "+DEXPECT=..."
  programs: s1 (boot with a ROM), s1_held (the same ROM, expectation: the CPU never ran - for a run with +ROMVALID=0), s6 (SuperRAM tags, the $F6/$F7 alias and its hwenable gate, ROM mirror)

S1  $8000 (ROM, boot map): store $A5 to $2000, copy the tail (file $8100) to SRAM $0300, JMP $0300; the tail (from SRAM) writes $D07E (hwenable) then $D0B6 (boot map off),
    stores $5A to $2002 and the high byte of the vector it now sees at $FFFD (boot map left: not the ROM's $80) to $2003.
S6  tags: the bank number to the first and last byte of banks $02-$F5, read back, mismatches counted in $2020; hwenable gate at $F6:0000; the alias $F6/$F7 = SuperRAM bytes
    0..$1FFFF (VICE scpu64mem.c:529-542) with hwenable; ROM reads at $F8:0100 / $F9:0100 / $FA:0100 / $FF:8100 / $FF:FFFF (128 KiB file repeated in the 512 KiB window,
    scpu64mem.c:544-551) and a write to $F8:0100 that is ignored.
"""
import os
import sys

SRAM = 0x03000000       # DDR2 byte address of the SRAM (bank 0 and 1: 128 KiB)
SUPER = 0x02000000      # SuperRAM
SIZE = 0x20000


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
    def lda_z(self, a): self.e(0xA5, a)
    def sta_z(self, a): self.e(0x85, a)
    def stz_z(self, a): self.e(0x64, a)
    def inc_z(self, a): self.e(0xE6, a)
    def cmp_z(self, a): self.e(0xC5, a)
    def cmp_i(self, v): self.e(0xC9, v)
    def ldy_i(self, v): self.e(0xA0, v)
    def ldx_i(self, v): self.e(0xA2, v)
    def lda_a(self, a): self.e(0xAD, a & 255, a >> 8)
    def sta_a(self, a): self.e(0x8D, a & 255, a >> 8)
    def lda_l(self, a): self.e(0xAF, a & 255, (a >> 8) & 255, a >> 16)
    def sta_l(self, a): self.e(0x8F, a & 255, (a >> 8) & 255, a >> 16)
    def lda_ily(self, dp): self.e(0xB7, dp)          # LDA [dp],Y
    def sta_ily(self, dp): self.e(0x97, dp)          # STA [dp],Y
    def lda_ax(self, a): self.e(0xBD, a & 255, a >> 8)
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


def prologue(a):
    a.e(0x78, 0xD8)                  # SEI, CLD
    a.ldx_i(0xFF); a.e(0x9A)         # TXS


def s1_rom():
    a = Asm(0x8000)
    prologue(a)
    a.lda_i(0xA5); a.sta_a(0x2000)
    a.ldx_i(0)
    a.label("cp"); a.lda_ax(0x8100); a.sta_ax(0x0300); a.e(0xE8); a.e(0xE0, 0x20); a.br(0xD0, "cp")
    a.jmp(0x0300)
    head = a.done()
    t = Asm(0x0300)
    t.lda_i(0); t.sta_a(0xD07E)      # hwenable
    t.sta_a(0xD0B6)                  # boot map off: the ROM is gone from $8000-$FFFF, so this runs from SRAM
    t.lda_i(0x5A); t.sta_a(0x2002)
    t.lda_a(0xFFFD); t.sta_a(0x2003)
    t.label("h"); t.br(0x80, "h")
    return head, t.done()


def s6_rom():
    a = Asm(0x8000)
    prologue(a)
    a.stz_z(0x30)                                    # error counter
    # hwenable gate: with hwenable off a write to $F6:0000 is ignored
    a.lda_i(0x11); a.sta_l(0xF60000)
    a.lda_l(0xF60000); a.sta_a(0x2010)
    # tags: bank number to the first and last byte of banks $02-$F5
    a.lda_i(0x02); a.sta_z(0x31)
    a.label("w")
    a.stz_z(0x40); a.stz_z(0x41); a.lda_z(0x31); a.sta_z(0x42)
    a.ldy_i(0); a.lda_z(0x31); a.sta_ily(0x40)
    a.lda_i(0xFF); a.sta_z(0x41); a.ldy_i(0xFF); a.lda_z(0x31); a.sta_ily(0x40)
    a.inc_z(0x31); a.lda_z(0x31); a.cmp_i(0xF6); a.br(0xD0, "w")
    a.lda_i(0x02); a.sta_z(0x31)
    a.label("r")
    a.stz_z(0x40); a.stz_z(0x41); a.lda_z(0x31); a.sta_z(0x42)
    a.ldy_i(0); a.lda_ily(0x40); a.cmp_z(0x31); a.br(0xF0, "r1"); a.inc_z(0x30)
    a.label("r1")
    a.lda_i(0xFF); a.sta_z(0x41); a.ldy_i(0xFF); a.lda_ily(0x40); a.cmp_z(0x31); a.br(0xF0, "r2"); a.inc_z(0x30)
    a.label("r2")
    a.inc_z(0x31); a.lda_z(0x31); a.cmp_i(0xF6); a.br(0xD0, "r")
    a.lda_z(0x30); a.sta_a(0x2020)
    # the alias with hwenable
    a.lda_i(0); a.sta_a(0xD07E)
    a.lda_i(0x5A); a.sta_l(0xF60000)
    a.lda_i(0x6B); a.sta_l(0xF7FFFF)
    a.lda_l(0xF60000); a.sta_a(0x2040)
    a.lda_l(0xF7FFFF); a.sta_a(0x2041)
    a.lda_i(0); a.sta_a(0xD07D)                      # hwenable off again
    a.lda_i(0x77); a.sta_l(0xF60000)                 # ignored
    a.lda_l(0xF60000); a.sta_a(0x2042)
    # the ROM window
    a.lda_l(0xF80100); a.sta_a(0x2030)
    a.lda_l(0xF90100); a.sta_a(0x2031)
    a.lda_l(0xFA0100); a.sta_a(0x2032)
    a.lda_l(0xFF8100); a.sta_a(0x2033)
    a.lda_i(0xEE); a.sta_l(0xF80100)                 # ignored
    a.lda_l(0xF80100); a.sta_a(0x2034)
    a.sta_l(0xFFFFFF)                                # ignored
    a.lda_l(0xFFFFFF); a.sta_a(0x2035)
    a.lda_i(0xD0); a.sta_a(0x2021)                   # done
    a.label("h"); a.br(0x80, "h")
    return a.done()


def image(prog):
    f = bytearray(pat(k) for k in range(SIZE))
    f[0xFFFC:0xFFFE] = bytes([0x00, 0x80])
    f[0xFFFE:0x10000] = bytes([0x00, 0x80])
    f[0x1FFFC:0x1FFFE] = bytes([0x00, 0x80])
    if prog in ("s1", "s1_held"):
        head, tail = s1_rom()
        f[0x8000:0x8000 + len(head)] = head
        assert len(tail) <= 0x20, len(tail)
        f[0x8100:0x8100 + len(tail)] = tail
    else:
        code = s6_rom()
        assert len(code) < 0x100
        f[0x8000:0x8000 + len(code)] = code
    # the bytes the program reads must be pattern bytes (not code or vectors)
    assert len(f) == SIZE
    return f


def expect(prog, f):
    e = []
    if prog == "s1_held":
        e += [(SRAM + 0x2000, 0x00), (SRAM + 0x2002, 0x00), (SRAM + 0x2003, 0x00)]
    elif prog == "s1":
        e += [(SRAM + 0x2000, 0xA5), (SRAM + 0x2002, 0x5A), (SRAM + 0x2003, 0x00)]
    else:
        e += [(SRAM + 0x2010, 0x00), (SRAM + 0x2020, 0x00), (SRAM + 0x2021, 0xD0)]
        for k, off in ((0x2030, 0x0100), (0x2031, 0x10100), (0x2032, 0x0100), (0x2033, 0x18100), (0x2034, 0x0100), (0x2035, 0x1FFFF)):
            e.append((SRAM + k, pat(off)))
        # these offsets must be pattern bytes
        assert all(f[o] == pat(o) for o in (0x0100, 0x10100, 0x18100, 0x1FFFF))
        e += [(SRAM + 0x2040, 0x5A), (SRAM + 0x2041, 0x6B), (SRAM + 0x2042, 0x5A), (SUPER, 0x5A), (SUPER + 0x1FFFF, 0x6B), (SUPER + 0xF60000, 0x00)]
        for b in (0x02, 0x03, 0x75, 0x76, 0x7F, 0x80, 0x82, 0xA1, 0xF5):
            e += [(SUPER + (b << 16), b), (SUPER + (b << 16) + 0xFFFF, b)]
    return e


def main():
    prog, out = sys.argv[1], sys.argv[2]
    if prog not in ("s1", "s1_held", "s6"):
        sys.exit("unknown program " + prog)
    f = image(prog)
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "scpu_rom.mem"), "w") as fh:
        fh.write("\n".join("%02x" % x for x in f) + "\n")
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as fh:
        fh.write("0000\n" * 10240)
    print("+DEXPECT=" + ",".join("%08x:%02x" % (a, v) for a, v in expect(prog, f)))


main()
