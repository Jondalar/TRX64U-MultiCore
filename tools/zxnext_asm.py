#!/usr/bin/env python3
"""T116: a small Z80 / Z80N assembler for the Next core's own test programs (cores/zxnext/sim/prog/*.s). No ROM, no NextZXOS: the programs are ours.

  tools/zxnext_asm.py prog.s out.bin [--hex out.hex] [--map out.map] [--org 0x8000]
  python: assemble(text) -> (bytes, origin, {label: address})

Two passes, one line per statement, `label:`, `;` comments. Directives ORG, EQU (`name EQU expr`), DB/DEFB, DW/DEFW, DS/DEFS n[,v], ALIGN n.
Expressions: decimal, 0xHH, $HH, HHh, 'c', labels, $ (the address of the line), + - * / % & | ^ ~ << >> and parentheses.
Instructions: the documented Z80 set a program of this size needs (8/16 bit loads, ALU, INC/DEC, jumps with conditions, CALL/RET/RST, PUSH/POP, EX,
block moves, CB rotates/BIT/SET/RES, IN/OUT, IM, DI/EI/HALT, ADC/SBC HL, NEG, RETI/RETN, LD I/R) and the Z80N extensions: SWAPNIB, MIRROR A, TEST n,
BSLA/BSRA/BSRL/BSRF/BRLC DE,B, MUL D,E, ADD HL/DE/BC,A, ADD HL/DE/BC,nn, PUSH nn, NEXTREG n,n / n,A, PIXELDN, PIXELAD, SETAE, LDIX, LDWS, LDDX, LDIRX,
LDPIRX, LDDRX, OUTINB, JP (C). IX and IY are not supported (the programs do not need them); the assembler says so instead of guessing.
"""
import re
import sys

R8 = {"B": 0, "C": 1, "D": 2, "E": 3, "H": 4, "L": 5, "(HL)": 6, "A": 7}
R16 = {"BC": 0, "DE": 1, "HL": 2, "SP": 3}
R16AF = {"BC": 0, "DE": 1, "HL": 2, "AF": 3}
CC = {"NZ": 0, "Z": 1, "NC": 2, "C": 3, "PO": 4, "PE": 5, "P": 6, "M": 7}
ALU = {"ADD": 0, "ADC": 1, "SUB": 2, "SBC": 3, "AND": 4, "XOR": 5, "OR": 6, "CP": 7}
ROT = {"RLC": 0, "RRC": 1, "RL": 2, "RR": 3, "SLA": 4, "SRA": 5, "SLL": 6, "SRL": 7}
SIMPLE = {"NOP": [0x00], "HALT": [0x76], "DI": [0xF3], "EI": [0xFB], "RET": [0xC9], "RLCA": [0x07], "RRCA": [0x0F], "RLA": [0x17], "RRA": [0x1F],
          "DAA": [0x27], "CPL": [0x2F], "SCF": [0x37], "CCF": [0x3F], "EXX": [0xD9], "LDI": [0xED, 0xA0], "LDIR": [0xED, 0xB0], "LDD": [0xED, 0xA8],
          "LDDR": [0xED, 0xB8], "CPI": [0xED, 0xA1], "CPIR": [0xED, 0xB1], "CPD": [0xED, 0xA9], "CPDR": [0xED, 0xB9], "INI": [0xED, 0xA2],
          "INIR": [0xED, 0xB2], "OUTI": [0xED, 0xA3], "OTIR": [0xED, 0xB3], "OUTD": [0xED, 0xAB], "OTDR": [0xED, 0xBB], "NEG": [0xED, 0x44],
          "RETI": [0xED, 0x4D], "RETN": [0xED, 0x45], "RRD": [0xED, 0x67], "RLD": [0xED, 0x6F],
          "SWAPNIB": [0xED, 0x23], "MUL": None, "PIXELDN": [0xED, 0x93], "PIXELAD": [0xED, 0x94], "SETAE": [0xED, 0x95], "LDIX": [0xED, 0xA4],
          "LDWS": [0xED, 0xA5], "LDDX": [0xED, 0xAC], "LDIRX": [0xED, 0xB4], "LDPIRX": [0xED, 0xB7], "LDDRX": [0xED, 0xBC], "OUTINB": [0xED, 0x90]}


class AsmError(Exception):
    pass


def tokenize_args(s):
    """split on commas outside parentheses and quotes"""
    out, cur, depth, q = [], "", 0, None
    for ch in s:
        if q:
            cur += ch
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
            cur += ch
        elif ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def strip_comment(line):
    q = None
    for i, ch in enumerate(line):
        if q:
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
        elif ch == ";":
            return line[:i]
    return line


class Asm:
    def __init__(self):
        self.sym = {}
        self.pc = 0
        self.org = None
        self.out = {}
        self.pass_no = 1

    def num(self, tok):
        tok = tok.strip()
        if re.fullmatch(r"0x[0-9a-fA-F]+", tok):
            return int(tok, 16)
        if re.fullmatch(r"\$[0-9a-fA-F]+", tok):
            return int(tok[1:], 16)
        if re.fullmatch(r"[0-9][0-9a-fA-F]*[hH]", tok):
            return int(tok[:-1], 16)
        if re.fullmatch(r"%[01]+", tok):
            return int(tok[1:], 2)
        if re.fullmatch(r"[0-9]+", tok):
            return int(tok)
        if re.fullmatch(r"'.'", tok):
            return ord(tok[1])
        return None

    def expr(self, s):
        s = s.strip()
        toks = re.findall(r"0x[0-9a-fA-F]+|\$[0-9a-fA-F]+|[0-9][0-9a-fA-F]*[hH]\b|%[01]+|[0-9]+|'.'|[A-Za-z_.][A-Za-z_0-9.]*|<<|>>|\$|[-+*/%&|^~()]", s)
        if "".join(toks).replace(" ", "") != s.replace(" ", ""):
            raise AsmError("cannot read expression %r" % s)
        py = []
        for t in toks:
            if t == "$":
                py.append(str(self.pc))
            elif self.num(t) is not None:
                py.append(str(self.num(t)))
            elif re.fullmatch(r"[A-Za-z_.][A-Za-z_0-9.]*", t):
                if t in self.sym:
                    py.append(str(self.sym[t]))
                elif self.pass_no == 1:
                    py.append("0")
                else:
                    raise AsmError("undefined symbol %s" % t)
            elif t == "/":
                py.append("//")
            else:
                py.append(t)
        try:
            return int(eval(" ".join(py), {"__builtins__": {}}, {}))
        except Exception as e:
            raise AsmError("bad expression %r: %s" % (s, e))

    def emit(self, *bs):
        for b in bs:
            self.out[self.pc] = b & 0xFF
            self.pc += 1

    def w16(self, v):
        self.emit(v & 0xFF, (v >> 8) & 0xFF)

    def r8(self, a):
        return R8.get(a.upper())

    def line(self, text):
        text = strip_comment(text).rstrip()
        if not text.strip():
            return
        m = re.match(r"^\s*([A-Za-z_.][A-Za-z_0-9.]*):\s*(.*)$", text)
        if m:
            self.sym[m.group(1)] = self.pc
            text = m.group(2)
            if not text.strip():
                return
        m = re.match(r"^\s*([A-Za-z_.][A-Za-z_0-9.]*)\s+EQU\s+(.*)$", text, re.I)
        if m:
            self.sym[m.group(1)] = self.expr(m.group(2))
            return
        text = text.strip()
        parts = text.split(None, 1)
        op = parts[0].upper()
        args = tokenize_args(parts[1]) if len(parts) > 1 else []
        self.instr(op, args)

    def imm8(self, a):
        v = self.expr(a)
        if self.pass_no == 2 and not -128 <= v <= 255:
            raise AsmError("8 bit value out of range: %s = %d" % (a, v))
        return v & 0xFF

    def rel(self, a):
        t = self.expr(a)
        d = t - (self.pc + 2)
        if self.pass_no == 2 and not -128 <= d <= 127:
            raise AsmError("relative jump out of range: %s (%d)" % (a, d))
        return d & 0xFF

    def instr(self, op, a):
        up = [x.upper() for x in a]
        if op == "ORG":
            self.pc = self.expr(a[0])
            if self.org is None:
                self.org = self.pc
            return
        if op in ("DB", "DEFB"):
            for x in a:
                if len(x) >= 2 and x[0] in "'\"" and x[-1] == x[0] and len(x) > 3:
                    self.emit(*[ord(c) for c in x[1:-1]])
                else:
                    self.emit(self.imm8(x))
            return
        if op in ("DW", "DEFW"):
            for x in a:
                self.w16(self.expr(x))
            return
        if op in ("DS", "DEFS"):
            n = self.expr(a[0])
            v = self.imm8(a[1]) if len(a) > 1 else 0
            self.emit(*([v] * n))
            return
        if op == "ALIGN":
            n = self.expr(a[0])
            while self.pc % n:
                self.emit(0)
            return
        if op in SIMPLE and SIMPLE[op] is not None and not a:
            self.emit(*SIMPLE[op])
            return
        if any(x.startswith(("IX", "IY", "(IX", "(IY")) for x in up):
            raise AsmError("IX and IY are not supported by this assembler")
        if op == "LD":
            return self.ld(a, up)
        if op in ("INC", "DEC"):
            base = 0x04 if op == "INC" else 0x05
            if up[0] in R8:
                return self.emit(base | (R8[up[0]] << 3))
            if up[0] in R16:
                return self.emit((0x03 if op == "INC" else 0x0B) | (R16[up[0]] << 4))
        if op in ALU:
            if len(a) == 1:
                a, up = ["A"] + a, ["A"] + up
            if op in ("ADC", "SBC") and up[0] == "HL":
                return self.emit(0xED, (0x4A if op == "ADC" else 0x42) | (R16[up[1]] << 4))
            if op == "ADD" and up[0] in ("HL", "DE", "BC") and up[1] not in R8 and up[1] != "A":
                if up[1] in R16 and up[0] == "HL":
                    return self.emit(0x09 | (R16[up[1]] << 4))
                v = self.expr(a[1])
                return self.emit(0xED, {"HL": 0x34, "DE": 0x35, "BC": 0x36}[up[0]], v & 0xFF, (v >> 8) & 0xFF)
            if op == "ADD" and up[0] in ("HL", "DE", "BC") and up[1] == "A":
                return self.emit(0xED, {"HL": 0x31, "DE": 0x32, "BC": 0x33}[up[0]])
            if op == "ADD" and up[0] == "HL" and up[1] in R16:
                return self.emit(0x09 | (R16[up[1]] << 4))
            if up[0] != "A":
                raise AsmError("%s: first operand must be A" % op)
            if up[1] in R8:
                return self.emit(0x80 | (ALU[op] << 3) | R8[up[1]])
            return self.emit(0xC6 | (ALU[op] << 3), self.imm8(a[1]))
        if op in ("JP", "JR", "CALL"):
            if op == "JP" and up == ["(HL)"]:
                return self.emit(0xE9)
            if op == "JP" and up == ["(C)"]:
                return self.emit(0xED, 0x98)
            if len(a) == 2:
                cc = CC[up[0]]
                if op == "JP":
                    self.emit(0xC2 | (cc << 3)); return self.w16(self.expr(a[1]))
                if op == "CALL":
                    self.emit(0xC4 | (cc << 3)); return self.w16(self.expr(a[1]))
                if cc > 3:
                    raise AsmError("JR takes NZ, Z, NC or C")
                return self.emit(0x20 | (cc << 3), self.rel(a[1]))
            if op == "JP":
                self.emit(0xC3); return self.w16(self.expr(a[0]))
            if op == "CALL":
                self.emit(0xCD); return self.w16(self.expr(a[0]))
            return self.emit(0x18, self.rel(a[0]))
        if op == "DJNZ":
            return self.emit(0x10, self.rel(a[0]))
        if op == "RET" and a:
            return self.emit(0xC0 | (CC[up[0]] << 3))
        if op == "RST":
            return self.emit(0xC7 | (self.expr(a[0]) & 0x38))
        if op == "PUSH" and up[0] in R16AF:
            return self.emit(0xC5 | (R16AF[up[0]] << 4))
        if op == "PUSH":                                   # Z80N: PUSH nn, the value big endian
            v = self.expr(a[0])
            return self.emit(0xED, 0x8A, (v >> 8) & 0xFF, v & 0xFF)
        if op == "POP":
            return self.emit(0xC1 | (R16AF[up[0]] << 4))
        if op == "EX":
            if up == ["DE", "HL"]:
                return self.emit(0xEB)
            if up == ["AF", "AF'"]:
                return self.emit(0x08)
            if up == ["(SP)", "HL"]:
                return self.emit(0xE3)
        if op == "OUT":
            if up[0] == "(C)":
                return self.emit(0xED, 0x41 | (R8[up[1]] << 3)) if up[1] != "0" else self.emit(0xED, 0x71)
            if up[1] == "A":
                return self.emit(0xD3, self.imm8(a[0][1:-1]))
        if op == "IN":
            if up[1] == "(C)":
                return self.emit(0xED, 0x40 | (R8[up[0]] << 3))
            if up[0] == "A":
                return self.emit(0xDB, self.imm8(a[1][1:-1]))
        if op == "IM":
            return self.emit(0xED, {0: 0x46, 1: 0x56, 2: 0x5E}[self.expr(a[0])])
        if op in ROT:
            return self.emit(0xCB, (ROT[op] << 3) | R8[up[0]])
        if op in ("BIT", "SET", "RES"):
            b = self.expr(a[0])
            return self.emit(0xCB, {"BIT": 0x40, "SET": 0xC0, "RES": 0x80}[op] | (b << 3) | R8[up[1]])
        # ---- Z80N
        if op == "MIRROR":
            return self.emit(0xED, 0x24)
        if op == "TEST":
            return self.emit(0xED, 0x27, self.imm8(a[0]))
        if op in ("BSLA", "BSRA", "BSRL", "BSRF", "BRLC"):
            return self.emit(0xED, {"BSLA": 0x28, "BSRA": 0x29, "BSRL": 0x2A, "BSRF": 0x2B, "BRLC": 0x2C}[op])
        if op == "MUL":
            return self.emit(0xED, 0x30)
        if op == "NEXTREG":
            if up[1] == "A":
                return self.emit(0xED, 0x92, self.imm8(a[0]))
            return self.emit(0xED, 0x91, self.imm8(a[0]), self.imm8(a[1]))
        raise AsmError("cannot assemble: %s %s" % (op, ", ".join(a)))

    def ld(self, a, up):
        d, s = a[0], a[1]
        ud, us = up[0], up[1]
        if ud == "I" and us == "A":
            return self.emit(0xED, 0x47)
        if ud == "R" and us == "A":
            return self.emit(0xED, 0x4F)
        if ud == "A" and us == "I":
            return self.emit(0xED, 0x57)
        if ud == "A" and us == "R":
            return self.emit(0xED, 0x5F)
        if ud == "SP" and us == "HL":
            return self.emit(0xF9)
        if ud in R8 and us in R8:
            if ud == "(HL)" and us == "(HL)":
                raise AsmError("LD (HL),(HL) is HALT")
            return self.emit(0x40 | (R8[ud] << 3) | R8[us])
        if ud == "A" and us == "(BC)":
            return self.emit(0x0A)
        if ud == "A" and us == "(DE)":
            return self.emit(0x1A)
        if ud == "(BC)" and us == "A":
            return self.emit(0x02)
        if ud == "(DE)" and us == "A":
            return self.emit(0x12)
        mem_s = us.startswith("(") and us not in R8
        mem_d = ud.startswith("(") and ud not in R8
        if ud == "A" and mem_s:
            self.emit(0x3A); return self.w16(self.expr(s[1:-1]))
        if us == "A" and mem_d:
            self.emit(0x32); return self.w16(self.expr(d[1:-1]))
        if ud == "HL" and mem_s:
            self.emit(0x2A); return self.w16(self.expr(s[1:-1]))
        if us == "HL" and mem_d:
            self.emit(0x22); return self.w16(self.expr(d[1:-1]))
        if ud in R16 and mem_s:
            self.emit(0xED, 0x4B | (R16[ud] << 4)); return self.w16(self.expr(s[1:-1]))
        if us in R16 and mem_d:
            self.emit(0xED, 0x43 | (R16[us] << 4)); return self.w16(self.expr(d[1:-1]))
        if ud in R8 and us not in R16 and not mem_s:
            return self.emit(0x06 | (R8[ud] << 3), self.imm8(s))
        if ud in R16 and us not in R16 and not mem_s:
            self.emit(0x01 | (R16[ud] << 4)); return self.w16(self.expr(s))
        raise AsmError("cannot assemble: LD %s, %s" % (d, s))


def assemble(text, defs=None):
    a = Asm()
    a.sym.update(defs or {})
    lines = text.split("\n")
    for pass_no in (1, 2):
        a.pass_no = pass_no
        a.pc = 0
        a.out = {}
        for n, ln in enumerate(lines, 1):
            try:
                a.line(ln)
            except AsmError as e:
                raise AsmError("line %d: %s: %s" % (n, ln.strip(), e))
            except (KeyError, IndexError) as e:
                raise AsmError("line %d: %s: operand %s not understood" % (n, ln.strip(), e))
    if not a.out:
        return b"", 0, a.sym
    lo, hi = min(a.out), max(a.out)
    data = bytes(a.out.get(i, 0) for i in range(lo, hi + 1))
    return data, lo, a.sym


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    src, outp = argv[1], argv[2]
    hexp = mapp = None
    for i, x in enumerate(argv):
        if x == "--hex":
            hexp = argv[i + 1]
        if x == "--map":
            mapp = argv[i + 1]
    data, org, sym = assemble(open(src).read())
    open(outp, "wb").write(data)
    if hexp:
        open(hexp, "w").write("".join("%02X\n" % b for b in data))
    if mapp:
        open(mapp, "w").write("".join("%-24s %04X\n" % (k, v) for k, v in sorted(sym.items(), key=lambda kv: kv[1])) + "ORG %04X LEN %d\n" % (org, len(data)))
    print("zxnext_asm: %s -> %s, %d bytes at %04X" % (src, outp, len(data), org))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
