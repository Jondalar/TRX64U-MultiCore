#!/usr/bin/env python3
"""T142 D10: a small 65C816 assembler for the SNES core's bench programs (cores/snes/sim/prog/*.s), written for this project. No cartridge, no game, no code of
anybody else is assembled with it; the programs are ours.

  tools/snes_asm.py <prog.s> <out.bin> [--lorom|--hirom]          assemble a cartridge image (default LoROM, 512 KiB, header at $7FC0, checksum fixed)
  tools/snes_asm.py selftest                                      known byte sequences
  tools/snes_asm.py spcstub <out.bin>                             the bench's own 64 byte S-SMP boot ROM

Syntax (one statement per line, `;` comments):
  label:                   a label (the CPU address of the next byte)
  NAME = expr              an equate
  .org $BBAAAA             the CPU address of the next byte ($008000 .. $00FFFF is LoROM bank 0 up to the vectors)
  .db / .dw / .dl expr,..  bytes, 16 bit words, 24 bit long words
  .ds n                    n zero bytes
  .prng seed, n            n bytes of a linear congruential sequence (a table for the programs to sum; the same bytes every time)
  .pushmode .popmode       save and restore the two sizes (around a PHP .. PLP the assembler cannot follow)
  .a8 .a16 .x8 .x16        the sizes the assembler takes for immediate operands of A and X/Y (the program's REP / SEP are not followed)
  .title "TEXT"            the 21 byte title of the header
  .vector name = label     native / emulation vectors: reset, nmi, irq, cop, brk, abort, enmi, eirq, ecop, ereset
Operands: #imm  abs  dp  long (a value above $FFFF or written with a bank: $7E0000)  abs,X  long,X  dp,X  (dp),Y  [dp],Y  A  label (branches)
Expressions: numbers ($hex, 0xhex, decimal), labels, + - * << >> & |, parentheses, `<x` low byte, `>x` high byte, `^x` bank byte.
"""
import re
import sys

# mnemonic -> {mode: opcode}
IMP = {"SEI": 0x78, "CLI": 0x58, "CLC": 0x18, "SEC": 0x38, "XCE": 0xFB, "CLD": 0xD8, "SED": 0xF8, "CLV": 0xB8, "TAX": 0xAA, "TXA": 0x8A, "TAY": 0xA8, "TYA": 0x98,
       "TXS": 0x9A, "TSX": 0xBA, "TCS": 0x1B, "TCD": 0x5B, "TDC": 0x7B, "TSC": 0x3B, "TXY": 0x9B, "TYX": 0xBB, "INX": 0xE8, "INY": 0xC8, "DEX": 0xCA, "DEY": 0x88,
       "PHA": 0x48, "PLA": 0x68, "PHX": 0xDA, "PLX": 0xFA, "PHY": 0x5A, "PLY": 0x7A, "PHP": 0x08, "PLP": 0x28, "PHB": 0x8B, "PLB": 0xAB, "PHK": 0x4B,
       "PHD": 0x0B, "PLD": 0x2B, "RTS": 0x60, "RTL": 0x6B, "RTI": 0x40, "NOP": 0xEA, "WAI": 0xCB, "STP": 0xDB, "XBA": 0xEB}
ACC = {"ASL": 0x0A, "LSR": 0x4A, "ROL": 0x2A, "ROR": 0x6A, "INC": 0x1A, "DEC": 0x3A}
REL = {"BRA": 0x80, "BNE": 0xD0, "BEQ": 0xF0, "BCC": 0x90, "BCS": 0xB0, "BPL": 0x10, "BMI": 0x30, "BVC": 0x50, "BVS": 0x70}
# load/store/arith: mnemonic -> {mode: opcode}; modes: imm dp abs long dpx absx longx absy dpy_ind (the (dp),Y form) ldpy ([dp],Y)
ALU = {
    "ORA": {"imm": 0x09, "dp": 0x05, "abs": 0x0D, "long": 0x0F, "dpx": 0x15, "absx": 0x1D, "longx": 0x1F, "absy": 0x19, "ind_y": 0x11},
    "AND": {"imm": 0x29, "dp": 0x25, "abs": 0x2D, "long": 0x2F, "dpx": 0x35, "absx": 0x3D, "longx": 0x3F, "absy": 0x39, "ind_y": 0x31},
    "EOR": {"imm": 0x49, "dp": 0x45, "abs": 0x4D, "long": 0x4F, "dpx": 0x55, "absx": 0x5D, "longx": 0x5F, "absy": 0x59, "ind_y": 0x51},
    "ADC": {"imm": 0x69, "dp": 0x65, "abs": 0x6D, "long": 0x6F, "dpx": 0x75, "absx": 0x7D, "longx": 0x7F, "absy": 0x79, "ind_y": 0x71},
    "STA": {"dp": 0x85, "abs": 0x8D, "long": 0x8F, "dpx": 0x95, "absx": 0x9D, "longx": 0x9F, "absy": 0x99, "ind_y": 0x91, "lind_y": 0x97},
    "LDA": {"imm": 0xA9, "dp": 0xA5, "abs": 0xAD, "long": 0xAF, "dpx": 0xB5, "absx": 0xBD, "longx": 0xBF, "absy": 0xB9, "ind_y": 0xB1, "lind_y": 0xB7},
    "CMP": {"imm": 0xC9, "dp": 0xC5, "abs": 0xCD, "long": 0xCF, "dpx": 0xD5, "absx": 0xDD, "longx": 0xDF, "absy": 0xD9, "ind_y": 0xD1},
    "SBC": {"imm": 0xE9, "dp": 0xE5, "abs": 0xED, "long": 0xEF, "dpx": 0xF5, "absx": 0xFD, "longx": 0xFF, "absy": 0xF9, "ind_y": 0xF1},
    "LDX": {"imm": 0xA2, "dp": 0xA6, "abs": 0xAE, "absy": 0xBE},
    "LDY": {"imm": 0xA0, "dp": 0xA4, "abs": 0xAC, "absx": 0xBC},
    "STX": {"dp": 0x86, "abs": 0x8E},
    "STY": {"dp": 0x84, "abs": 0x8C},
    "STZ": {"dp": 0x64, "abs": 0x9C, "dpx": 0x74, "absx": 0x9E},
    "CPX": {"imm": 0xE0, "dp": 0xE4, "abs": 0xEC},
    "CPY": {"imm": 0xC0, "dp": 0xC4, "abs": 0xCC},
    "BIT": {"imm": 0x89, "dp": 0x24, "abs": 0x2C},
    "INC": {"dp": 0xE6, "abs": 0xEE, "dpx": 0xF6, "absx": 0xFE},
    "DEC": {"dp": 0xC6, "abs": 0xCE, "dpx": 0xD6, "absx": 0xDE},
    "ASL": {"dp": 0x06, "abs": 0x0E}, "LSR": {"dp": 0x46, "abs": 0x4E},
}
JUMPS = {"JMP": ("abs", 0x4C), "JML": ("long", 0x5C), "JSR": ("abs", 0x20), "JSL": ("long", 0x22)}
IMM_SIZE_A = {"ORA", "AND", "EOR", "ADC", "LDA", "CMP", "SBC", "BIT"}
IMM_SIZE_X = {"LDX", "LDY", "CPX", "CPY"}
VECTORS = {"cop": 0xFFE4, "brk": 0xFFE6, "abort": 0xFFE8, "nmi": 0xFFEA, "irq": 0xFFEE, "ecop": 0xFFF4, "eabort": 0xFFF8, "enmi": 0xFFFA, "ereset": 0xFFFC,
           "eirq": 0xFFFE, "reset": 0xFFFC}


class Asm:
    def __init__(self, hirom=False, size=0x80000):
        self.hirom = hirom
        self.size = size
        self.img = bytearray(size)
        self.labels = {}
        self.equ = {}
        self.pc = 0x008000
        self.a16 = False
        self.x16 = False
        self.title = "SNES CORE BENCH"
        self.vec = {}
        self.pass_no = 0
        self.lines = []
        self.modes = []

    # ------------------------------------------------------------------ expressions
    def ev(self, s, ln):
        s = s.strip()
        toks = re.findall(r"\$[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|\d+|[A-Za-z_.][A-Za-z_0-9.]*|<<|>>|[-+*&|()<>^]", s)
        if "".join(toks).replace(" ", "") != s.replace(" ", ""):
            raise SystemExit("line %d: cannot read the expression %r" % (ln, s))
        pos = [0]

        def peek():
            return toks[pos[0]] if pos[0] < len(toks) else None

        def nxt():
            t = peek()
            pos[0] += 1
            return t

        def prim():
            t = nxt()
            if t is None:
                raise SystemExit("line %d: expression ends early: %r" % (ln, s))
            if t == "(":
                v = orr()
                if nxt() != ")":
                    raise SystemExit("line %d: missing )" % ln)
                return v
            if t == "-":
                return -prim()
            if t == "<":
                return prim() & 0xFF
            if t == ">":
                return (prim() >> 8) & 0xFF
            if t == "^":
                return (prim() >> 16) & 0xFF
            if t.startswith("$"):
                return int(t[1:], 16)
            if t.startswith("0x"):
                return int(t, 16)
            if t.isdigit():
                return int(t)
            if t in self.equ:
                return self.equ[t]
            if t in self.labels:
                return self.labels[t]
            if self.pass_no == 0:
                return 0
            raise SystemExit("line %d: unknown name %r" % (ln, t))

        def mul():
            v = prim()
            while peek() == "*":
                nxt()
                v *= prim()
            return v

        def add():
            v = mul()
            while peek() in ("+", "-"):
                o = nxt()
                r = mul()
                v = v + r if o == "+" else v - r
            return v

        def shf():
            v = add()
            while peek() in ("<<", ">>"):
                o = nxt()
                r = add()
                v = v << r if o == "<<" else v >> r
            return v

        def andd():
            v = shf()
            while peek() == "&":
                nxt()
                v &= shf()
            return v

        def orr():
            v = andd()
            while peek() == "|":
                nxt()
                v |= andd()
            return v

        v = orr()
        if peek() is not None:
            raise SystemExit("line %d: junk after the expression: %r" % (ln, s))
        return v

    # ------------------------------------------------------------------ the image
    def file_off(self, addr):
        bank, off = addr >> 16, addr & 0xFFFF
        if self.hirom:
            return ((bank & 0x3F) << 16) | off
        if off < 0x8000:
            raise SystemExit("address %06X is not ROM in a LoROM image" % addr)
        return ((bank & 0x7F) << 15) | (off & 0x7FFF)

    def put(self, b):
        if self.pass_no == 1:
            o = self.file_off(self.pc)
            if o >= self.size:
                raise SystemExit("address %06X is outside the %d byte image" % (self.pc, self.size))
            self.img[o] = b & 0xFF
        self.pc += 1

    def emit(self, *bs):
        for b in bs:
            self.put(b)

    # ------------------------------------------------------------------ statements
    def operand(self, mn, op, ln):
        """-> (mode, value)"""
        op = op.strip()
        u = op.upper().replace(" ", "")
        if u == "A":
            return "acc", 0
        if op.startswith("#"):
            return "imm", self.ev(op[1:], ln)
        m = re.match(r"^\[(.*)\],\s*[Yy]$", op)
        if m:
            return "lind_y", self.ev(m.group(1), ln)
        m = re.match(r"^\((.*)\),\s*[Yy]$", op)
        if m:
            return "ind_y", self.ev(m.group(1), ln)
        m = re.match(r"^(.*),\s*([XxYy])$", op)
        if m:
            v = self.ev(m.group(1), ln)
            x = m.group(2).lower()
            long_ = v > 0xFFFF or (re.match(r"^\$[0-9A-Fa-f]{6}", m.group(1).strip()) is not None)
            if x == "x":
                return ("longx" if long_ else ("dpx" if v < 0x100 and mn in ("STZ", "INC", "DEC", "LDA", "STA", "ADC", "SBC", "AND", "ORA", "EOR", "CMP") and not re.match(r"^\$[0-9A-Fa-f]{4}", m.group(1).strip()) else "absx")), v
            return "absy", v
        v = self.ev(op, ln)
        long_ = v > 0xFFFF or (re.match(r"^\$[0-9A-Fa-f]{6}$", op) is not None)
        if long_:
            return "long", v
        if v < 0x100 and not re.match(r"^\$[0-9A-Fa-f]{3,4}$", op) and not re.match(r"^[A-Za-z_]", op):
            return "dp", v
        if re.match(r"^[A-Za-z_]", op) and v < 0x100 and (op in self.equ):
            return "dp", v
        return "abs", v

    def statement(self, line, ln):
        line = line.split(";")[0].strip()
        if not line:
            return
        m = re.match(r"^([A-Za-z_][A-Za-z_0-9.]*)\s*:(.*)$", line)
        if m:
            if self.pass_no == 0:
                if m.group(1) in self.labels:
                    raise SystemExit("line %d: label %s twice" % (ln, m.group(1)))
            self.labels[m.group(1)] = self.pc
            line = m.group(2).strip()
            if not line:
                return
        m = re.match(r"^([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.*)$", line)
        if m:
            self.equ[m.group(1)] = self.ev(m.group(2), ln)
            return
        parts = line.split(None, 1)
        mn = parts[0]
        arg = parts[1].strip() if len(parts) > 1 else ""
        if mn.startswith("."):
            d = mn.lower()
            if d == ".org":
                self.pc = self.ev(arg, ln)
            elif d in (".db", ".dw", ".dl"):
                n = {".db": 1, ".dw": 2, ".dl": 3}[d]
                for e in re.split(r",(?![^()]*\))", arg):
                    v = self.ev(e, ln)
                    for k in range(n):
                        self.put((v >> (8 * k)) & 0xFF)
            elif d == ".ds":
                for _ in range(self.ev(arg, ln)):
                    self.put(0)
            elif d == ".prng":
                sd, n = [self.ev(x, ln) for x in arg.split(",")]
                x = sd & 0xFFFFFFFF
                for _ in range(n):
                    x = (x * 1103515245 + 12345) & 0xFFFFFFFF
                    self.put((x >> 16) & 0xFF)
            elif d == ".pushmode":
                self.modes.append((self.a16, self.x16))
            elif d == ".popmode":
                self.a16, self.x16 = self.modes.pop()
            elif d == ".a8":
                self.a16 = False
            elif d == ".a16":
                self.a16 = True
            elif d == ".x8":
                self.x16 = False
            elif d == ".x16":
                self.x16 = True
            elif d == ".title":
                self.title = arg.strip().strip('"')
            elif d == ".vector":
                k, v = [x.strip() for x in arg.split("=")]
                self.vec[k.lower()] = v
            else:
                raise SystemExit("line %d: unknown directive %s" % (ln, mn))
            return
        mn = mn.upper()
        if mn in IMP and not arg:
            self.emit(IMP[mn])
            return
        if mn in ACC and (arg.upper() == "A" or (not arg and mn in ("INC", "DEC", "ASL", "LSR", "ROL", "ROR"))):
            self.emit(ACC[mn])
            return
        if mn in REL:
            t = self.ev(arg, ln)
            d = t - (self.pc + 2)
            if self.pass_no == 1 and not -128 <= d <= 127:
                raise SystemExit("line %d: %s %s is %d bytes away (8 bit branch)" % (ln, mn, arg, d))
            self.emit(REL[mn], d)
            return
        if mn == "BRL":
            t = self.ev(arg, ln)
            d = t - (self.pc + 3)
            self.emit(0x82, d, d >> 8)
            return
        if mn in ("REP", "SEP"):
            v = self.ev(arg.lstrip("#"), ln)
            self.emit(0xC2 if mn == "REP" else 0xE2, v)
            if mn == "REP":
                if v & 0x20:
                    self.a16 = True
                if v & 0x10:
                    self.x16 = True
            else:
                if v & 0x20:
                    self.a16 = False
                if v & 0x10:
                    self.x16 = False
            return
        if mn in ("MVN", "MVP"):
            a, b = [self.ev(x, ln) for x in arg.split(",")]
            self.emit(0x54 if mn == "MVN" else 0x44, b, a)               # the opcode's operands are destination bank, source bank in this order: `MVN src,dst` as written
            return
        if mn in JUMPS:
            mode, opc = JUMPS[mn]
            v = self.ev(arg, ln)
            self.emit(opc, v, v >> 8)
            if mode == "long":
                self.emit(v >> 16)
            return
        if mn in ALU:
            mode, v = self.operand(mn, arg, ln)
            if mode == "acc":
                raise SystemExit("line %d: %s A is written %s" % (ln, mn, mn))
            tbl = ALU[mn]
            if mode == "dp" and "dp" not in tbl:
                mode = "abs"
            if mode == "abs" and "abs" not in tbl and "long" in tbl:
                mode = "long"
            if mode not in tbl:
                raise SystemExit("line %d: %s has no mode %s" % (ln, mn, mode))
            self.emit(tbl[mode])
            if mode == "imm":
                wide = (mn in IMM_SIZE_A and self.a16) or (mn in IMM_SIZE_X and self.x16)
                self.emit(v)
                if wide:
                    self.emit(v >> 8)
            elif mode in ("dp", "dpx", "ind_y", "lind_y"):
                self.emit(v)
            elif mode in ("abs", "absx", "absy"):
                self.emit(v, v >> 8)
            else:
                self.emit(v, v >> 8, v >> 16)
            return
        raise SystemExit("line %d: cannot assemble %r" % (ln, line))

    def assemble(self, text):
        lines = text.split("\n")
        for p in (0, 1):
            self.pass_no = p
            self.pc = 0x008000
            self.a16 = self.x16 = False
            for i, l in enumerate(lines, 1):
                self.statement(l, i)
        # vectors
        for k, lab in self.vec.items():
            if k not in VECTORS:
                raise SystemExit("unknown vector %s" % k)
            v = self.ev(lab, 0)
            o = self.file_off(0x00_0000 | VECTORS[k])
            self.img[o] = v & 0xFF
            self.img[o + 1] = (v >> 8) & 0xFF
        return self.header()

    def header(self):
        base = 0xFFC0 if self.hirom else 0x7FC0
        t = (self.title + " " * 21)[:21].encode("ascii")
        self.img[base:base + 21] = t
        self.img[base + 0x15] = 0x21 if self.hirom else 0x20
        self.img[base + 0x16] = 0x00
        size = self.size
        n = 0
        while (1024 << n) < size:
            n += 1
        self.img[base + 0x17] = n
        self.img[base + 0x18] = 0
        self.img[base + 0x19] = 0x01
        # the checksum: the sum of all bytes with the checksum field as FFFF + 0000 (complement + checksum = 0xFFFF)
        self.img[base + 0x1C:base + 0x20] = bytes([0x00, 0x00, 0xFF, 0xFF])
        s = sum(self.img) & 0xFFFF
        self.img[base + 0x1C] = (~s) & 0xFF
        self.img[base + 0x1D] = ((~s) >> 8) & 0xFF
        self.img[base + 0x1E] = s & 0xFF
        self.img[base + 0x1F] = (s >> 8) & 0xFF
        return bytes(self.img)


def spc_stub():
    """A 64 byte boot ROM for the S-SMP of our own, for the benches only (the real one is Nintendo's; the application embeds it, cores/snes/app/lib/snes.cc): it does what the real one does at the
    start - port 0 := $AA, port 1 := $BB, wait for $CC on port 0 - and then answers $11 on port 2 and echoes port 0 to port 1 for ever. SPC700 code at
    $FFC0, the reset vector at $FFFE."""
    code = bytes([0x8F, 0xAA, 0xF4,          # mov $F4,#$AA
                  0x8F, 0xBB, 0xF5,          # mov $F5,#$BB
                  0xE4, 0xF4,                # wait: mov a,$F4
                  0x68, 0xCC,                # cmp a,#$CC
                  0xD0, 0xFA,                # bne wait
                  0x8F, 0x11, 0xF6,          # mov $F6,#$11
                  0xE4, 0xF4,                # loop: mov a,$F4
                  0xC4, 0xF5,                # mov $F5,a
                  0x2F, 0xFA])               # bra loop
    rom = bytearray(64)
    rom[:len(code)] = code
    rom[62] = 0xC0
    rom[63] = 0xFF
    return bytes(rom)


ADSR_REGS = [(0x0C, 0x7F), (0x1C, 0x7F), (0x2C, 0x00), (0x3C, 0x00), (0x5D, 0x02),
             (0x00, 0x7F), (0x01, 0x7F), (0x02, 0x00), (0x03, 0x10), (0x04, 0x00), (0x05, 0xBF), (0x06, 0x8A), (0x6C, 0x20), (0x4C, 0x01)]


def spc_regs(regs):
    """Like spc_audio: a 64 byte boot ROM that writes the (register, value) pairs of `regs` to the S-DSP and stops."""
    return spc_audio(True, 0x1000, regs)


def spc_audio(kon=True, pitch=0x1000, regs=None):
    """A 64 byte boot ROM for the benches that plays one voice: it copies (register, value) pairs from its own table to the S-DSP ($F2 / $F3) and stops.
    The bench puts the sample directory ($0200) and one BRR block ($0300) into the audio RAM itself. kon=False leaves the key-on out (the control)."""
    if regs is None:
        regs = [(0x0C, 0x7F), (0x1C, 0x7F), (0x2C, 0x00), (0x3C, 0x00), (0x0D, 0x00), (0x2D, 0x00), (0x3D, 0x00), (0x4D, 0x00), (0x5D, 0x02),
                (0x00, 0x7F), (0x01, 0x7F), (0x02, pitch & 0xFF), (0x03, pitch >> 8), (0x04, 0x00), (0x05, 0x00), (0x07, 0x7F), (0x6C, 0x20)]
        if kon:
            regs.append((0x4C, 0x01))
    tbl = 0xD4 - 0xC0
    code = bytes([0xCD, 0x00,                        # mov x,#0
                  0xF5, 0xD4, 0xFF, 0xC4, 0xF2,      # loop: mov a,!tbl+x ; mov $F2,a
                  0x3D,                              # inc x
                  0xF5, 0xD4, 0xFF, 0xC4, 0xF3,      # mov a,!tbl+x ; mov $F3,a
                  0x3D,                              # inc x
                  0xC8, 2 * len(regs),               # cmp x,#n
                  0xD0, 0xF0,                        # bne loop
                  0x2F, 0xFE])                       # bra .
    rom = bytearray(64)
    rom[:len(code)] = code
    assert len(code) == tbl and 2 * len(regs) <= 62 - tbl
    for i, (a, v) in enumerate(regs):
        rom[tbl + 2 * i], rom[tbl + 2 * i + 1] = a, v
    rom[62] = 0xC0
    rom[63] = 0xFF
    return bytes(rom)


def main(argv):
    if len(argv) == 3 and argv[1] == "spcadsr":
        open(argv[2], "wb").write(spc_regs(ADSR_REGS))
        return 0
    if len(argv) in (3, 4) and argv[1] in ("spcaudio", "spcaudio_nokon"):
        open(argv[2], "wb").write(spc_audio(argv[1] == "spcaudio", int(argv[3], 16) if len(argv) == 4 else 0x1000))
        return 0
    if argv[1:] == ["selftest"]:
        a = Asm()
        a.assemble("""
.vector reset = start
start: sei
  clc
  xce
  rep #$30
  lda #$1234
  ldx #$01FF
  txs
  sta $7E0100
  lda $2140
  sep #$20
  lda #$80
  sta $2100
  stz $4200
  bne start
  jml $818000
.org $818000
 .db 1,2,3
 .dw $BEEF
""")
        want = bytes([0x78, 0x18, 0xFB, 0xC2, 0x30, 0xA9, 0x34, 0x12, 0xA2, 0xFF, 0x01, 0x9A, 0x8F, 0x00, 0x01, 0x7E, 0xAD, 0x40, 0x21, 0xE2, 0x20, 0xA9, 0x80, 0x8D,
                      0x00, 0x21, 0x9C, 0x00, 0x42, 0xD0, 0xE4, 0x5C, 0x00, 0x80, 0x81])
        # (stz $4200 is 9C 00 42; bne start is D0 with the distance back to `start`)
        want = want[:26] + bytes([0x9C, 0x00, 0x42, 0xD0, 0xE1, 0x5C, 0x00, 0x80, 0x81])
        got = bytes(a.img[:len(want)])
        if got != want:
            print("snes_asm selftest FAILED: %s != %s" % (got.hex(), want.hex()))
            return 1
        if bytes(a.img[0x7FFC:0x7FFE]) != bytes([0x00, 0x80]):
            print("snes_asm selftest FAILED: reset vector %s" % bytes(a.img[0x7FFC:0x7FFE]).hex())
            return 1
        if bytes(a.img[0x8000:0x8005]) != bytes([1, 2, 3, 0xEF, 0xBE]):
            print("snes_asm selftest FAILED: .org of bank $81 (file offset 0x8000): %s" % bytes(a.img[0x8000:0x8005]).hex())
            return 1
        print("snes_asm selftest: ok")
        return 0
    if len(argv) == 3 and argv[1] == "spcstub":
        open(argv[2], "wb").write(spc_stub())
        return 0
    if len(argv) < 3:
        print(__doc__)
        return 2
    hirom = "--hirom" in argv
    a = Asm(hirom=hirom)
    text = open(argv[1]).read()
    open(argv[2], "wb").write(a.assemble(text))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
