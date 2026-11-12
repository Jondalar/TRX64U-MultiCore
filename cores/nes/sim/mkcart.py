#!/usr/bin/env python3
"""T95: the cartridge of the benches. NOT a game and no third-party image: a 6502 program of ours, written out byte by byte,
and a pattern table of three tiles. It makes cart*.hex (one byte per line, for $readmemh): the 16 byte iNES header (mapper 0, 32 KB PRG, 8 KB CHR ROM, horizontal
mirroring), the PRG and the CHR, in the order of an .nes file.

The program: palette, a band of tile 1 across nametable row 1, four tiles of tile 2 on row 2, one sprite (tile 1, OAM through
$4014), NMI on; the NMI handler counts in zero page $00. What a correct machine shows (NES pixel coordinates, 256 x 224 from
line 8): background 0x21; the band x 0..255, y 8..15 colour 0x16; the checker x 32..63? no: x 32..55 ... see EXPECT.
"""
import sys

LOAD_A, STA_ABS, STA_ABSX = 0xA9, 0x8D, 0x9D


def build(variant=""):
    c = bytearray()
    def op(*b): c.extend(b)
    def sta(a): op(STA_ABS, a & 255, a >> 8)
    def lda(v): op(LOAD_A, v)
    op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                    # SEI CLD LDX #$FF TXS
    for _ in range(2): op(0xAD, 0x02, 0x20, 0x10, 0xFB) # the PPU ignores its registers until it has warmed up: wait two vblanks (LDA $2002; BPL *-5)
    lda(0x3F); sta(0x2006); lda(0x00); sta(0x2006)      # PPUADDR $3F00
    for v in (0x21, 0x16, 0x30, 0x0F):
        lda(v); sta(0x2007)                              # background palette 0
    lda(0x3F); sta(0x2006); lda(0x11); sta(0x2006)
    lda(0x2A); sta(0x2007)                               # sprite palette 0 colour 1
    lda(0x20); sta(0x2006); lda(0x20); sta(0x2006)      # $2020: row 1
    op(0xA2, 32); lda(1)                                 # LDX #32, LDA #1
    op(STA_ABS, 0x07, 0x20, 0xCA, 0xD0, 0xFA)           # loop: STA $2007; DEX; BNE loop
    lda(0x20); sta(0x2006); lda(0x44); sta(0x2006)      # $2044: row 2, column 4
    lda(2)
    for _ in range(4): sta(0x2007)
    for i, v in enumerate((60, 1, 0, 100)):              # OAM entry 0 in the work RAM: y, tile, attribute, x
        lda(v); sta(0x0200 + i)
    lda(2); sta(0x4014)                                  # sprite DMA from page 2
    lda(0); sta(0x2005); sta(0x2005)
    lda(0x80); sta(0x2000)                               # NMI on
    lda(0x1E); sta(0x2001)                               # background and sprites on, left column too
    if variant == "tone":
        lda(0x01); sta(0x4015)                           # pulse 1 on
        lda(0xBF); sta(0x4000)                           # duty 2, constant volume 15, no length halt
        lda(0xFD); sta(0x4002)                           # timer low
        lda(0x08); sta(0x4003)                           # timer high 0, length counter index 1 (254 half frames)
    here = 0x8000 + len(c)
    op(0x4C, here & 255, here >> 8)                      # JMP *
    nmi = 0x8000 + len(c)
    op(0xE6, 0x00, 0x40)                                 # INC $00; RTI
    prg = bytearray(32768)
    prg[:len(c)] = c
    for off, v in ((0x7FFA, nmi), (0x7FFC, 0x8000), (0x7FFE, 0x8000)):
        prg[off] = v & 255; prg[off + 1] = v >> 8
    chr_ = bytearray(8192)
    chr_[16:24] = b"\xFF" * 8                            # tile 1: colour 1 everywhere
    chr_[32:40] = bytes([0xAA, 0x55] * 4)                # tile 2: a checker of colours 1 and 2
    chr_[40:48] = bytes([0x55, 0xAA] * 4)
    return prg, chr_


# ------------------------------------------------------------------------------------------------ the instruction bench
def build_cpu():
    """A program that runs the 6502's arithmetic, logic, shifts, compares, memory and addressing modes, the stack, subroutines and
    branches on known inputs and stores a result byte (or a masked flags byte) after each at $0400+k. The expected bytes come from
    plain Python arithmetic written from the instruction set, not from a model of the chip. Returns (prg, chr, expected)."""
    c = bytearray()
    labels, fix = {}, []
    exp = []
    def pc(): return 0x8000 + len(c)
    def op(*b): c.extend(b)
    def abs_(o, a): op(o, a & 255, a >> 8)
    def res(v): # store A at the next result slot
        abs_(0x8D, 0x0400 + len(exp)); exp.append(v & 255)
    def flags(mask, v): # PHP, PLA, AND #mask, store
        op(0x08, 0x68, 0x29, mask); abs_(0x8D, 0x0400 + len(exp)); exp.append(v & 255)
    def flg(r, cy=0, ov=0): return (r & 0x80) | (ov << 6) | ((r == 0) << 1) | cy
    def adc(a, b, cin):
        s = a + b + cin; r = s & 255
        return r, s >> 8, int(((a ^ r) & (b ^ r) & 0x80) != 0)
    # --- reset: SEI CLD, stack
    op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)
    # --- ADC / SBC, all flag combinations on edge values
    vec = [(0x00, 0x00, 0), (0x00, 0x00, 1), (0x7F, 0x01, 0), (0x80, 0x80, 0), (0xFF, 0x01, 0), (0xFF, 0xFF, 1), (0x50, 0x50, 0),
           (0xD0, 0x90, 1), (0x12, 0x34, 0), (0x99, 0x01, 0), (0x09, 0x01, 0)]
    for a, b, cin in vec:
        op(0x38 if cin else 0x18, 0xA9, a, 0x69, b)
        r, cy, ov = adc(a, b, cin)
        res(r); flags(0xC3, flg(r, cy, ov))
    for a, b, cin in vec:
        op(0x38 if cin else 0x18, 0xA9, a, 0xE9, b)
        r, cy, ov = adc(a, b ^ 255, cin)
        res(r); flags(0xC3, flg(r, cy, ov))
    # --- the 2A03 has no decimal mode: SED does not change ADC
    op(0xF8, 0x18, 0xA9, 0x09, 0x69, 0x01); res(0x0A); op(0xD8)
    op(0xF8, 0x38, 0xA9, 0x10, 0xE9, 0x01); res(0x0F); op(0xD8)
    # --- logic
    for a, b in ((0xF0, 0x3C), (0x0F, 0xF0), (0xAA, 0x55), (0xFF, 0xFF)):
        op(0xA9, a, 0x29, b); res(a & b); flags(0x82, flg(a & b))
        op(0xA9, a, 0x09, b); res(a | b); flags(0x82, flg(a | b))
        op(0xA9, a, 0x49, b); res(a ^ b); flags(0x82, flg(a ^ b))
    # --- shifts and rotates with both carries
    for a in (0x00, 0x01, 0x80, 0xA5, 0xFF):
        for cin in (0, 1):
            pre = (0x38 if cin else 0x18, 0xA9, a)
            op(*pre, 0x0A); r = (a << 1) & 255; res(r); flags(0x83, flg(r, a >> 7))               # ASL A
            op(*pre, 0x4A); r = a >> 1; res(r); flags(0x83, flg(r, a & 1))                          # LSR A
            op(*pre, 0x2A); r = ((a << 1) | cin) & 255; res(r); flags(0x83, flg(r, a >> 7))         # ROL A
            op(*pre, 0x6A); r = (a >> 1) | (cin << 7); res(r); flags(0x83, flg(r, a & 1))           # ROR A
    # --- compares
    for a, b in ((0x10, 0x10), (0x10, 0x11), (0x11, 0x10), (0x00, 0xFF), (0x80, 0x01)):
        r = (a - b) & 255; f = (r & 0x80) | ((a == b) << 1) | (a >= b)
        op(0xA9, a, 0xC9, b); flags(0x83, f)
        op(0xA2, a, 0xE0, b); flags(0x83, f)
        op(0xA0, a, 0xC0, b); flags(0x83, f)
    # --- INC/DEC memory and registers
    for v in (0x00, 0x7F, 0xFF, 0x01):
        op(0xA9, v, 0x85, 0x10, 0xE6, 0x10, 0xA5, 0x10); r = (v + 1) & 255; res(r); flags(0x82, flg(r))
        op(0xA9, v, 0x85, 0x10, 0xC6, 0x10, 0xA5, 0x10); r = (v - 1) & 255; res(r); flags(0x82, flg(r))
        op(0xA2, v, 0xE8, 0x8A); r = (v + 1) & 255; res(r)                                            # INX TXA
        op(0xA0, v, 0x88, 0x98); r = (v - 1) & 255; res(r)                                            # DEY TYA
    # --- a table in RAM and every addressing mode that reads it
    tab = lambda i: (i * 7 + 3) & 255
    for i in range(32): op(0xA9, tab(i)); abs_(0x8D, 0x0500 + i)
    op(0xA9, 0x10, 0x85, 0x20, 0xA9, 0x05, 0x85, 0x21)                                                  # $20/$21 -> $0510
    op(0xA9, 0xFE, 0x85, 0x30, 0xA9, 0x05, 0x85, 0x31)                                                  # $30/$31 -> $05FE
    for i in range(8): op(0xA9, 0xC0 + i); abs_(0x8D, 0x05FE + i)                                      # bytes at $05FE.. (a page boundary inside)
    op(0xA2, 5, 0xBD, 0x00, 0x05); res(tab(5))                                                          # LDA $0500,X
    op(0xA0, 9, 0xB9, 0x00, 0x05); res(tab(9))                                                          # LDA $0500,Y
    op(0xA2, 4, 0xB5, 0x1C); res(0x10)                                                                  # LDA $1C,X  (reads $20)
    op(0xA2, 4, 0xA1, 0x1C); res(tab(16))                                                               # LDA ($1C,X) -> pointer $20 -> $0510
    op(0xA0, 3, 0xB1, 0x20); res(tab(19))                                                               # LDA ($20),Y -> $0513
    op(0xA0, 3, 0xB1, 0x30); res(0xC3)                                                                  # LDA ($30),Y -> $05FE+3 = $0601 (page crossed)
    op(0xA2, 3, 0xBD, 0xFE, 0x05); res(0xC3)                                                            # LDA $05FE,X -> $0601 (page crossed)
    op(0xA9, 0xA5, 0xA2, 2, 0x9D, 0x00, 0x06, 0xAD, 0x02, 0x06); res(0xA5)                           # STA $0600,X ; LDA $0602
    op(0xA9, 0x3C, 0xA0, 1, 0x99, 0x00, 0x07, 0xAD, 0x01, 0x07); res(0x3C)                           # STA $0700,Y
    op(0xA9, 0xEE, 0xA2, 4, 0x81, 0x1C, 0xAD, 0x10, 0x05); res(0xEE)                                  # STA ($1C,X) -> $0510
    op(0xA9, 0x77, 0xA0, 2, 0x91, 0x20, 0xAD, 0x12, 0x05); res(0x77)                                  # STA ($20),Y -> $0512
    # --- the stack, subroutines, branches
    op(0xA9, 0xAA, 0x48, 0xA9, 0xBB, 0x48, 0xBA, 0x8A); res(0xFD)                                     # PHA PHA TSX TXA : S = $FD
    op(0x68); res(0xBB); op(0x68); res(0xAA)
    fix.append((len(c) + 1, "sub")); op(0x20, 0, 0); res(0x77 ^ 0x01)                                   # JSR sub ; sub returns A = $76
    for a, b in ((5, 5), (5, 6), (6, 5), (0x80, 0x01)):
        r = (a - b) & 255
        n, z, cy = r >> 7, int(a == b), int(a >= b)
        for name, opc, taken in (("BEQ", 0xF0, z), ("BNE", 0xD0, not z), ("BCS", 0xB0, cy), ("BCC", 0x90, not cy),
                                 ("BMI", 0x30, n), ("BPL", 0x10, not n)):
            op(0xA2, 0, 0xA9, a, 0xC9, b, opc, 2, 0xA2, 1, 0x8A)                                      # Bcc +2 skips LDX #1
            res(0 if taken else 1)
    op(0xA9, 0x42); abs_(0x8D, 0x03FF)                                                                 # done
    here = pc(); op(0x4C, here & 255, here >> 8)
    labels["sub"] = pc(); op(0xA9, 0x76, 0x60)
    for off, name in fix:
        c[off] = labels[name] & 255; c[off + 1] = labels[name] >> 8
    prg = bytearray(32768); prg[:len(c)] = c
    for off, v in ((0x7FFA, 0x8000), (0x7FFC, 0x8000), (0x7FFE, 0x8000)):
        prg[off] = v & 255; prg[off + 1] = v >> 8
    e = list(exp)
    return prg, bytearray(8192), e



# ------------------------------------------------------------------------------------------------ the bank benches
class Asm:
    """a 6502 assembler small enough for the benches: bytes, labels, relative branches and absolute jumps"""
    def __init__(s, base): s.c = bytearray(); s.base = base; s.labels = {}; s.fix = []
    def pc(s): return s.base + len(s.c)
    def op(s, *b): s.c.extend(b)
    def lab(s, n): s.labels[n] = s.pc()
    def br(s, opc, n): s.c.extend([opc, 0]); s.fix.append(("r", len(s.c) - 1, n))
    def jmp(s, n): s.c.extend([0x4C, 0, 0]); s.fix.append(("a", len(s.c) - 2, n))
    def sta(s, a): s.op(0x8D, a & 255, a >> 8)
    def lda(s, a): s.op(0xAD, a & 255, a >> 8)
    def done(s):
        for k, off, n in s.fix:
            tgt = s.labels[n]
            if k == "r":
                d = tgt - (s.base + off + 1)
                assert -128 <= d <= 127, (n, d)
                s.c[off] = d & 255
            else:
                s.c[off] = tgt & 255; s.c[off + 1] = tgt >> 8
        return s.c


def ines_hdr(prg16, chr8, mapper, flags6=0):
    return bytes([0x4E, 0x45, 0x53, 0x1A, prg16, chr8, (mapper & 15) << 4 | flags6, mapper & 0xF0] + [0] * 8)


def f_prg(b, o): return (b * 31 + o * 7 + 13) & 255
def f_chr(b, o): return (b * 53 + o * 5 + 7) & 255


def build_banks2():
    """UNROM (mapper 2): 8 banks of 16 KB, the last one fixed at $C000. The program switches banks in four patterns (ascending,
    descending, interleaved, a ping-pong of 24 rounds) and reads the same CPU addresses in each; the byte it gets is a function of
    the bank, so a cache that serves a line of the previous bank gives the wrong one. Bus conflicts: the bank number is written to a
    ROM byte that holds the same number."""
    a = Asm(0xC000)
    a.op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                                   # SEI CLD LDX #$FF TXS
    a.op(0xA0, 0); a.lab("s1")                                           # LDY #0
    a.op(0x98, 0xAA, 0x9D, 0x00, 0xFF)                                   # TYA TAX STA $FF00,X   (bank = Y)
    a.lda(0x8123); a.op(0x99, 0x00, 0x04)                                # LDA $8123 STA $0400,Y
    a.lda(0xBFFF); a.op(0x99, 0x10, 0x04)                                # LDA $BFFF STA $0410,Y
    a.op(0xC8, 0xC0, 7); a.br(0xD0, "s1")                                # INY CPY #7 BNE
    a.op(0xA0, 6); a.lab("s2")
    a.op(0x98, 0xAA, 0x9D, 0x00, 0xFF)
    a.lda(0x8123); a.op(0x99, 0x20, 0x04)
    a.lda(0x9000); a.op(0x99, 0x30, 0x04)
    a.op(0x88); a.br(0x10, "s2")                                         # DEY BPL
    a.op(0xA0, 0); a.lab("s3")
    a.op(0xB9, 0x00, 0xFE, 0xAA, 0x9D, 0x00, 0xFF)                       # LDA $FE00,Y TAX STA $FF00,X
    a.lda(0x8010); a.op(0x99, 0x40, 0x04)
    a.lda(0x8010); a.op(0x99, 0x50, 0x04)
    a.op(0xC8, 0xC0, 7); a.br(0xD0, "s3")
    a.op(0xA9, 0); a.sta(0x0460); a.sta(0x0461)
    a.op(0xA0, 24); a.lab("s4")
    for bank in (2, 5):
        a.op(0xA9, bank); a.sta(0xFF00 + bank)
        a.op(0x18); a.lda(0x8123); a.op(0x6D, 0x60, 0x04); a.sta(0x0460)  # CLC LDA $8123 ADC $0460 STA $0460
        a.lda(0x0461); a.op(0x69, 0); a.sta(0x0461)
    a.op(0x88); a.br(0xD0, "s4")
    a.op(0xA9, 0x42); a.sta(0x03FF)
    a.lab("end"); a.jmp("end")
    code = a.done()
    prg = bytearray(8 * 16384)
    for b in range(7):
        for o in range(16384): prg[b * 16384 + o] = f_prg(b, o)
    last = 7 * 16384
    prg[last:last + len(code)] = code
    for i in range(8): prg[last + 0x3F00 + i] = i                          # the bank table at $FF00
    order = (0, 6, 1, 5, 2, 4, 3)
    for i, v in enumerate(order): prg[last + 0x3E00 + i] = v               # the interleaved order at $FE00
    for off, v in ((0x3FFA, 0xC000), (0x3FFC, 0xC000), (0x3FFE, 0xC000)):
        prg[last + off] = v & 255; prg[last + off + 1] = v >> 8
    e = []
    for b in range(7):
        e += [(0x0400 + b, f_prg(b, 0x123)), (0x0410 + b, f_prg(b, 0x3FFF))]
    for b in range(7):
        e += [(0x0420 + b, f_prg(b, 0x123)), (0x0430 + b, f_prg(b, 0x1000))]
    for i, b in enumerate(order):
        e += [(0x0440 + i, f_prg(b, 0x10)), (0x0450 + i, f_prg(b, 0x10))]
    s = 24 * (f_prg(2, 0x123) + f_prg(5, 0x123))
    e += [(0x0460, s & 255), (0x0461, (s >> 8) & 255), (0x03FF, 0x42)]
    return ines_hdr(8, 0, 2), prg, bytearray(0), e


def build_banks3():
    """CNROM (mapper 3): 32 KB PRG fixed, 4 banks of 8 KB CHR. The program waits for the PPU, switches CHR banks in two patterns
    and reads the CHR through PPUDATA ($2007: the PPU's own memory port), so the CHR path of the cache and the bank are tested."""
    a = Asm(0x8000)
    a.op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)
    for _ in range(2): a.op(0xAD, 0x02, 0x20, 0x10, 0xFB)                  # two vblanks: the PPU ignores its registers before
    a.op(0xA0, 0); a.lab("c1")
    a.op(0x98, 0xAA, 0x9D, 0x00, 0xFF)                                   # TYA TAX STA $FF00,X   (CHR bank = Y)
    a.op(0xA9, 0x01); a.sta(0x2006); a.op(0xA9, 0x23); a.sta(0x2006)     # PPUADDR = $0123
    a.lda(0x2007); a.lda(0x2007); a.op(0x99, 0x00, 0x04)                 # dummy read, then the byte at $0123
    a.lda(0x2007); a.op(0x99, 0x10, 0x04)                                # $0124
    a.op(0xA9, 0x1A); a.sta(0x2006); a.op(0xA9, 0xBC); a.sta(0x2006)     # PPUADDR = $1ABC
    a.lda(0x2007); a.lda(0x2007); a.op(0x99, 0x20, 0x04)
    a.op(0xC8, 0xC0, 4); a.br(0xD0, "c1")
    a.op(0xA0, 3); a.lab("c2")
    a.op(0x98, 0xAA, 0x9D, 0x00, 0xFF)
    a.op(0xA9, 0x01); a.sta(0x2006); a.op(0xA9, 0x23); a.sta(0x2006)
    a.lda(0x2007); a.lda(0x2007); a.op(0x99, 0x30, 0x04)
    a.op(0x88); a.br(0x10, "c2")
    a.op(0xA9, 0x42); a.sta(0x03FF)
    a.lab("end"); a.jmp("end")
    code = a.done()
    prg = bytearray(32768)
    prg[:len(code)] = code
    for i in range(4): prg[0x7F00 + i] = i
    for off, v in ((0x7FFA, 0x8000), (0x7FFC, 0x8000), (0x7FFE, 0x8000)):
        prg[off] = v & 255; prg[off + 1] = v >> 8
    chr_ = bytearray(4 * 8192)
    for b in range(4):
        for o in range(8192): chr_[b * 8192 + o] = f_chr(b, o)
    e = []
    for b in range(4):
        e += [(0x0400 + b, f_chr(b, 0x123)), (0x0410 + b, f_chr(b, 0x124)), (0x0420 + b, f_chr(b, 0x1ABC))]
    for b in range(4): e += [(0x0430 + b, f_chr(b, 0x123))]
    e += [(0x03FF, 0x42)]
    return ines_hdr(2, 4, 3), prg, chr_, e


def g_prg(L): return (L * 31 + (L >> 7) * 5 + (L >> 13) * 29 + 13) & 255      # no two 8 KB banks of up to 2 MB agree at the same offset
def g_chr(L): return (L * 53 + (L >> 9) * 3 + (L >> 12) * 41 + 7) & 255      # no two 4 KB banks of up to 1 MB agree at the same offset


class Bench(Asm):
    """what the mapper benches share: the reset code, a PPU read through $2007 and a store to the result area"""
    def __init__(s, base): Asm.__init__(s, base); s.e = []; s.ppu_ready = False
    def start(s): s.op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                                  # SEI CLD LDX #$FF TXS
    def wait_ppu(s):
        for _ in range(2): s.op(0xAD, 0x02, 0x20, 0x10, 0xFB)                          # LDA $2002 BPL: two vblanks
    def get(s, addr, slot, exp):                                                       # LDA addr STA slot ; expect exp
        s.lda(addr); s.sta(slot); s.e.append((slot, exp & 255))
    def ppu(s, addr, slot, exp):                                                       # PPUADDR = addr, dummy read, read -> slot
        s.op(0xA9, addr >> 8); s.sta(0x2006); s.op(0xA9, addr & 255); s.sta(0x2006)
        s.lda(0x2007); s.lda(0x2007); s.sta(slot); s.e.append((slot, exp & 255))
    def poke(s, addr, v): s.op(0xA9, v & 255); s.sta(addr)
    def finish(s):
        s.poke(0x03FF, 0x42); s.e.append((0x03FF, 0x42)); s.lab("end"); s.jmp("end")


def prg_image(nbanks, bsize, code, code_at, tables=()):
    """nbanks of bsize bytes of g_prg(linear); `code` placed at code_at (linear offset); tables: (linear offset, bytes)"""
    prg = bytearray(nbanks * bsize)
    for L in range(len(prg)): prg[L] = g_prg(L)
    prg[code_at:code_at + len(code)] = code
    for off, tb in tables: prg[off:off + len(tb)] = bytes(tb)
    return prg


def chr_image(n): return bytearray(g_chr(L) for L in range(n))


def finish_prg(prg, entry, nbytes_total):
    for off in (0xFFFA, 0xFFFC, 0xFFFE):                                              # NMI, reset, IRQ: the top 6 bytes of the last bank
        o = nbytes_total - (0x10000 - off)
        prg[o] = entry & 255; prg[o + 1] = entry >> 8


def mmc1w(a, addr, v):
    for i in range(5): a.poke(addr, (v >> i) & 1)


def build_mmc1():
    """MMC1 (mapper 1): 128 KB PRG (8 x 16 KB, the last fixed at $C000), 32 KB CHR (8 x 4 KB). The serial register is written five
    times per value; PRG banks 0..6 are read through $8000 and CHR in 4 KB mode through $2007."""
    a = Bench(0xC000); a.start()
    mmc1w(a, 0x8000, 0x0C)                                                             # control: PRG mode 3, CHR 8 KB
    for b in range(7):
        mmc1w(a, 0xE000, b)
        a.get(0x8123, 0x0400 + b, g_prg(b * 16384 + 0x123)); a.get(0xBFFF, 0x0410 + b, g_prg(b * 16384 + 0x3FFF))
    a.wait_ppu()
    mmc1w(a, 0x8000, 0x1C)                                                             # CHR in two 4 KB banks
    for b in range(8):
        mmc1w(a, 0xA000, b); mmc1w(a, 0xC000, 7 - b)
        a.ppu(0x0123, 0x0420 + b, g_chr(b * 4096 + 0x123)); a.ppu(0x1ABC, 0x0430 + b, g_chr((7 - b) * 4096 + 0xABC))
    a.finish(); code = a.done()
    prg = prg_image(8, 16384, code, 7 * 16384); finish_prg(prg, 0xC000, len(prg))
    return ines_hdr(8, 4, 1), prg, chr_image(32768), a.e


def build_mmc3():
    """MMC3 (mapper 4): 128 KB PRG (16 x 8 KB, the last two fixed), 32 KB CHR (32 x 1 KB). R6/R7 select the PRG banks, R2 and R0
    the CHR banks; code in the last bank at $E000."""
    a = Bench(0xE000); a.start()
    def reg(r, v): a.poke(0x8000, r); a.poke(0x8001, v)
    for b in range(14):
        reg(6, b); reg(7, (b + 3) % 14)
        a.get(0x8123, 0x0400 + b, g_prg(b * 8192 + 0x123)); a.get(0xA456, 0x0410 + b, g_prg(((b + 3) % 14) * 8192 + 0x456))
        a.get(0xC789, 0x0420 + b, g_prg(14 * 8192 + 0x789))
    a.wait_ppu()
    for k in range(8):
        reg(2, k * 3 + 1); reg(0, 2 * k + 2)
        a.ppu(0x1123, 0x0430 + k, g_chr((k * 3 + 1) * 1024 + 0x123)); a.ppu(0x0345, 0x0440 + k, g_chr((2 * k + 2) * 1024 + 0x345))
    a.finish(); code = a.done()
    prg = prg_image(16, 8192, code, 15 * 8192); finish_prg(prg, 0xE000, len(prg))
    return ines_hdr(8, 4, 4), prg, chr_image(32768), a.e


def build_vrc24():
    """VRC2/VRC4 (mapper 23): 128 KB PRG (16 x 8 KB, the last two fixed), 32 KB CHR (32 x 1 KB); a CHR bank is written as two
    nibbles ($B000/$B001 for the bank at $0000, $D000/$D001 for $1000)."""
    a = Bench(0xE000); a.start()
    for b in range(14):
        a.poke(0x8000, b); a.poke(0xA000, (b + 5) % 14)
        a.get(0x8123, 0x0400 + b, g_prg(b * 8192 + 0x123)); a.get(0xA456, 0x0410 + b, g_prg(((b + 5) % 14) * 8192 + 0x456))
        a.get(0xC789, 0x0420 + b, g_prg(14 * 8192 + 0x789)); a.get(0xFFF0, 0x0450 + b, g_prg(15 * 8192 + 0x1FF0))
    a.wait_ppu()
    for k in range(8):
        v0, v4 = (k * 5 + 3) % 32, (k * 7 + 9) % 32
        a.poke(0xB000, v0 & 15); a.poke(0xB001, v0 >> 4); a.poke(0xD000, v4 & 15); a.poke(0xD001, v4 >> 4)
        a.ppu(0x0123, 0x0430 + k, g_chr(v0 * 1024 + 0x123)); a.ppu(0x1234, 0x0440 + k, g_chr(v4 * 1024 + 0x234))
    a.finish(); code = a.done()
    prg = prg_image(16, 8192, code, 15 * 8192); finish_prg(prg, 0xE000, len(prg))
    return ines_hdr(8, 4, 23), prg, chr_image(32768), a.e


def build_mmc2():
    """MMC2 (mapper 9): 128 KB PRG (16 x 8 KB: one switched at $8000, the last three fixed), 32 KB CHR (8 x 4 KB) with the two
    latches that the PPU's reads of $0FD8/$0FE8/$1FD8/$1FE8 flip."""
    a = Bench(0xE000); a.start()
    for b in range(13):
        a.poke(0xA000, b)
        a.get(0x8123, 0x0400 + b, g_prg(b * 8192 + 0x123)); a.get(0xA456, 0x0410 + b, g_prg(13 * 8192 + 0x456)); a.get(0xC789, 0x0420 + b, g_prg(14 * 8192 + 0x789))
    a.wait_ppu()
    a.poke(0xB000, 1); a.poke(0xC000, 2); a.poke(0xD000, 3); a.poke(0xE000, 4)         # 0a, 0b, 1a, 1b
    c = lambda bank, off: g_chr(bank * 4096 + off)
    a.ppu(0x0123, 0x0430, c(1, 0x123)); a.ppu(0x1123, 0x0431, c(3, 0x123))               # latches at their reset value: 0a and 1a
    a.ppu(0x0FE8, 0x0432, c(1, 0xFE8)); a.ppu(0x0123, 0x0433, c(2, 0x123))               # the read of $0FE8 flips latch 0: 0b
    a.ppu(0x1FE8, 0x0434, c(3, 0xFE8)); a.ppu(0x1123, 0x0435, c(4, 0x123))               # $1FE8 flips latch 1: 1b
    a.ppu(0x0FD8, 0x0436, c(2, 0xFD8)); a.ppu(0x0123, 0x0437, c(1, 0x123))               # $0FD8 flips latch 0 back: 0a
    a.ppu(0x1FD8, 0x0438, c(4, 0xFD8)); a.ppu(0x1123, 0x0439, c(3, 0x123))               # and latch 1: 1a
    a.finish(); code = a.done()
    prg = prg_image(16, 8192, code, 15 * 8192); finish_prg(prg, 0xE000, len(prg))
    return ines_hdr(8, 4, 9), prg, chr_image(32768), a.e


def build_mmc4():
    """MMC4 (mapper 10): 256 KB PRG would be the case; 128 KB here (8 x 16 KB, the last fixed at $C000), one 16 KB bank switched at
    $8000 through $A000, and the same CHR latches as MMC2."""
    a = Bench(0xC000); a.start()
    for b in range(7):
        a.poke(0xA000, b)
        a.get(0x8123, 0x0400 + b, g_prg(b * 16384 + 0x123)); a.get(0xBFFF, 0x0410 + b, g_prg(b * 16384 + 0x3FFF)); a.get(0xC456, 0x0420 + b, g_prg(7 * 16384 + 0x456))
    a.wait_ppu()
    a.poke(0xB000, 1); a.poke(0xC000, 2); a.poke(0xD000, 3); a.poke(0xE000, 4)
    c = lambda bank, off: g_chr(bank * 4096 + off)
    a.ppu(0x0123, 0x0430, c(1, 0x123)); a.ppu(0x0FE8, 0x0432, c(1, 0xFE8)); a.ppu(0x0123, 0x0433, c(2, 0x123))
    a.finish(); code = a.done()
    prg = prg_image(8, 16384, code, 7 * 16384); finish_prg(prg, 0xC000, len(prg))
    return ines_hdr(8, 4, 10), prg, chr_image(32768), a.e


def prg_image_rep(nbanks, code, bsize=32768):
    """banks of 32 KB with the same code at the start of every bank (a mapper without a fixed bank: the code goes on running from
    whichever bank is selected) and the vectors at the top of every bank; the rest is g_prg(linear)"""
    prg = bytearray(nbanks * bsize)
    for L in range(len(prg)): prg[L] = g_prg(L)
    for b in range(nbanks):
        prg[b * bsize:b * bsize + len(code)] = code
        for off in (0x7FFA, 0x7FFC, 0x7FFE): prg[b * bsize + off] = 0x00; prg[b * bsize + off + 1] = 0x80
        prg[b * bsize + 0x7F00] = 0xFF                                               # a byte of ones to write the bank number onto (bus conflicts)
    return prg


def build_mmc66():
    """GxROM (mapper 66): 128 KB PRG (4 x 32 KB, all switchable), 32 KB CHR (4 x 8 KB); one write to $FF00 (a byte of ones in the
    ROM, bus conflict safe) selects both: {PRG bank << 4 | CHR bank}. The code is the same in every bank."""
    a = Bench(0x8000); a.start(); a.wait_ppu(); n = 0
    for p, c in ((0, 0), (1, 2), (2, 1), (3, 3), (0, 3), (3, 0), (2, 2), (1, 1)):
        a.poke(0xFF00, (p << 4) | c)
        a.get(0x9123, 0x0400 + n, g_prg(p * 32768 + 0x1123)); a.get(0xFFF0, 0x0410 + n, g_prg(p * 32768 + 0x7FF0))
        a.ppu(0x0123, 0x0420 + n, g_chr(c * 8192 + 0x123)); n += 1
    a.finish(); code = a.done()
    return ines_hdr(8, 4, 66), prg_image_rep(4, code), chr_image(32768), a.e


def build_mmc34():
    """BNROM (mapper 34, CHR RAM): 128 KB PRG (4 x 32 KB all switchable by a write to $8000-$FFFF), same code in every bank."""
    a = Bench(0x8000); a.start(); n = 0
    for p in (0, 1, 2, 3, 2, 0, 3, 1):
        a.poke(0xFF00, p)
        a.get(0x9123, 0x0400 + n, g_prg(p * 32768 + 0x1123)); a.get(0xFFF0, 0x0410 + n, g_prg(p * 32768 + 0x7FF0)); n += 1
    a.finish(); code = a.done()
    return ines_hdr(8, 0, 34), prg_image_rep(4, code), bytearray(0), a.e


def build_mmc71():
    """Camerica (mapper 71): 128 KB PRG, the last 16 KB fixed at $C000, the bank at $8000 selected by a write to $C000-$FFFF."""
    a = Bench(0xC000); a.start()
    for n, b in enumerate((0, 1, 2, 3, 4, 5, 6, 3, 0)):
        a.poke(0xC000 + n, b)
        a.get(0x8123, 0x0400 + n, g_prg(b * 16384 + 0x123)); a.get(0xBFFF, 0x0410 + n, g_prg(b * 16384 + 0x3FFF)); a.get(0xC456, 0x0420 + n, g_prg(7 * 16384 + 0x456))
    a.finish(); code = a.done()
    prg = prg_image(8, 16384, code, 7 * 16384); finish_prg(prg, 0xC000, len(prg))
    return ines_hdr(8, 0, 71), prg, bytearray(0), a.e


def build_mmc79():
    """NINA-03/06 (mapper 79): 64 KB PRG (2 x 32 KB), 64 KB CHR (8 x 8 KB); one write to $4100 selects {PRG bank << 3 | CHR bank}."""
    a = Bench(0x8000); a.start(); a.wait_ppu(); n = 0
    for p, c in ((0, 0), (1, 5), (0, 7), (1, 2), (0, 3), (1, 1), (1, 0), (0, 6)):
        a.poke(0x4100, (p << 3) | c)
        a.get(0x9123, 0x0400 + n, g_prg(p * 32768 + 0x1123)); a.ppu(0x0123, 0x0420 + n, g_chr(c * 8192 + 0x123))
        a.ppu(0x1ABC, 0x0430 + n, g_chr(c * 8192 + 0x1ABC)); n += 1
    a.finish(); code = a.done()
    return ines_hdr(4, 8, 79), prg_image_rep(2, code), chr_image(65536), a.e


def build_pad():
    """T95 5.18, the controller bench: the program names a phase in $0300, the bench sets the host's joystick and keyboard lines for
    it, and the program then reads both ports the way a game does - write 1 then 0 to $4016, eight reads of $4016 / $4017, bit 0 of
    each into a byte (first bit read becomes bit 7). The expected bytes come from the mapping table of T95 5.18 written out here, not
    from the gateware: port 1 takes its directions and A from joystick 1, B from the space bar, Select from F3, Start from F1; port 2
    takes them from joystick 2, right shift, F7, F5. The ninth read of a port must give 1 (an NES controller, not a Famicom one)."""
    # bit of a read byte: the first bit out (A) ends up at bit 7
    A, B, SEL, STA, UP, DN, LF, RT = 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
    # what the bench presses in each phase, as names, and what each port must then read
    phases = [
        # phase 1: joystick 1 up and fire, F1
        (dict(j1="up fire", keys="F1"),              A | STA | UP,            0),
        # phase 2: joystick 1 down and left with the port-1 keys, joystick 2 right and fire with the port-2 keys. A key of one port
        # must not show up in the other.
        (dict(j1="down left", j2="right fire", keys="space F3 F5 rshift"),
                                                     B | SEL | DN | LF,       A | B | STA | RT),
        # phase 3: nothing pressed
        (dict(),                                     0,                       0),
        # phase 4 (T156): only the application's USB pads, written to the registers PAD1 (0F8) and PAD2 (0FA) over the register bus by the bench:
        # PAD1 = A Select Down Right = $A5, PAD2 = B Start Up Left = $5A (the register's bit order is the controller's own)
        (dict(app1="A Select Down Right", app2="B Start Up Left"), A | SEL | DN | RT, B | STA | UP | LF),
        # phase 5: both sources at once: joystick 1 up, PAD1 = Start. The port reads the OR; PAD2 is cleared (the pad was pulled: no stuck button)
        (dict(j1="up", app1="Start", app2="cleared"), UP | STA,               0),
        # phase 6: the pad cleared too: nothing pressed, and the ninth read of both ports is 1
        (dict(),                                     0,                       0),
    ]
    a = Asm(0x8000)
    a.op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                               # SEI CLD LDX #$FF TXS
    e = []
    def read_port(port, slot, ninth=None):
        a.op(0xA9, 0x01); a.sta(0x4016)                              # LDA #1 STA $4016  (strobe up: both ports latch their buttons)
        a.op(0xA9, 0x00); a.sta(0x4016)                              # LDA #0 STA $4016
        a.op(0xA9, 0x00); a.op(0x85, 0x10)                           # LDA #0 STA $10
        a.op(0xA2, 0x08)                                             # LDX #8
        here = a.pc()
        a.lda(port); a.op(0x4A); a.op(0x26, 0x10)                    # LDA port LSR A ROL $10
        a.op(0xCA); a.op(0xD0, (here - (a.pc() + 2)) & 255)          # DEX BNE here
        a.op(0xA5, 0x10); a.sta(slot)                                # LDA $10 STA slot
        if ninth is not None:                                        # the ninth read, before anything strobes again
            a.lda(port); a.op(0x29, 0x01); a.sta(ninth)
    def delay():                                                     # the phase is on the lines long before the read
        a.op(0xA0, 0x02)                                             # LDY #2
        d2 = a.pc(); a.op(0xA2, 0x00)                                # LDX #0
        d1 = a.pc(); a.op(0xCA); a.op(0xD0, (d1 - (a.pc() + 2)) & 255)
        a.op(0x88); a.op(0xD0, (d2 - (a.pc() + 2)) & 255)            # DEY BNE d2
    slot = 0x0400
    for n, (_, p1, p2) in enumerate(phases, start=1):
        a.op(0xA9, n); a.sta(0x0300)                                 # the phase the bench must set up
        delay()
        last = n == len(phases)                                      # in the last phase each port is also read a ninth time
        read_port(0x4016, slot, 0x0410 if last else None);   e.append((slot, p1));   slot += 1
        read_port(0x4017, slot, 0x0411 if last else None);   e.append((slot, p2));   slot += 1
    e.append((0x0410, 1))                                            # the ninth read of a standard NES controller is 1
    e.append((0x0411, 1))
    a.op(0xA9, 0x42); a.sta(0x03FF)                                  # the end marker the bench waits for
    a.lab("end"); a.jmp("end")
    code = a.done()
    prg = bytearray(32768)
    prg[:len(code)] = code
    for off, v in ((0x7FFA, 0x8000), (0x7FFC, 0x8000), (0x7FFE, 0x8000)):
        prg[off] = v & 255; prg[off + 1] = v >> 8
    return ines_hdr(2, 1, 0), prg, bytearray(8192), e


def build_inv():
    """the window-write bench: a loop that copies PRG offset 0x100 (a data line of its own) to $0400 over and over; the bench changes
    that byte through the DDR2 window while the machine runs, and the next copy must be the new one."""
    a = Asm(0x8000)
    a.op(0x78, 0xD8, 0xA2, 0xFF, 0x9A)
    a.lab("l"); a.lda(0x8100); a.sta(0x0400); a.jmp("l")
    code = a.done()
    prg = bytearray(32768)
    prg[:len(code)] = code
    prg[0x100] = 0x11
    for off, v in ((0x7FFA, 0x8000), (0x7FFC, 0x8000), (0x7FFE, 0x8000)):
        prg[off] = v & 255; prg[off + 1] = v >> 8
    return ines_hdr(2, 1, 0), prg, bytearray(8192), [(0x0400, 0x11)]


if __name__ == "__main__":
    out = sys.argv[1]
    variant = sys.argv[2] if len(sys.argv) > 2 else ""
    if variant == "cpu":
        prg, chr_, e = build_cpu()
        hdr = bytes([0x4E, 0x45, 0x53, 0x1A, 2, 1, 0, 0] + [0] * 8)
        open(out + "/cart%s.hex" % variant, "w").write("\n".join("%02x" % b for b in hdr + bytes(prg) + bytes(chr_)) + "\n")
        open(out + "/expect%s.hex" % variant, "w").write("\n".join("%06x" % (((0x0400 + i) << 8) | b) for i, b in enumerate(e)) + "\n")
        sys.exit(0)
    builders = {"pad": build_pad, "banks2": build_banks2, "banks3": build_banks3, "inv": build_inv, "mmc1": build_mmc1, "mmc3": build_mmc3,
                "vrc24": build_vrc24, "mmc2": build_mmc2, "mmc4": build_mmc4, "mmc66": build_mmc66, "mmc34": build_mmc34,
                "mmc71": build_mmc71, "mmc79": build_mmc79}
    if variant in builders:
        hdr, prg, chr_, e = builders[variant]()
        open(out + "/cart%s.hex" % variant, "w").write("\n".join("%02x" % b for b in hdr + bytes(prg) + bytes(chr_)) + "\n")
        open(out + "/expect%s.hex" % variant, "w").write("\n".join("%06x" % ((a << 8) | v) for a, v in e) + "\n")
        sys.exit(0)
    prg, chr_ = build(variant)
    hdr = bytes([0x4E, 0x45, 0x53, 0x1A, 2, 1, 0, 0] + [0] * 8)      # "NES", 2 x 16 KB PRG, 1 x 8 KB CHR, mapper 0, horizontal
    open(out + "/cart%s.hex" % variant, "w").write("\n".join("%02x" % b for b in hdr + bytes(prg) + bytes(chr_)) + "\n")
