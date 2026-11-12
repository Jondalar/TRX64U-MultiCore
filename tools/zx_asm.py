#!/usr/bin/env python3
"""A small two-pass Z80 assembler for the test programs of the ZX Spectrum and CPC cores (tools/zx_progs.py), written for this project.
It knows the instructions those programs use (documented set, IX/IY with displacements, CB/ED groups) and expressions in every operand
(python syntax; `$` is the current address, lo()/hi() exist). Directives: org, equ, db, dw, ds. Labels end in ':' or are followed by equ.
Not a general assembler: an instruction it does not know is an error, never a guess. tools/zx_progs.py --selftest checks it against the
z80 package's assembler on a list of instructions (the package is only needed there)."""
import re


class AsmErr(Exception):
    pass


R8 = {'B': 0, 'C': 1, 'D': 2, 'E': 3, 'H': 4, 'L': 5, '(HL)': 6, 'A': 7}
R16 = {'BC': 0, 'DE': 1, 'HL': 2, 'SP': 3}
R16AF = {'BC': 0, 'DE': 1, 'HL': 2, 'AF': 3}
CC = {'NZ': 0, 'Z': 1, 'NC': 2, 'C': 3, 'PO': 4, 'PE': 5, 'P': 6, 'M': 7}
ALU = {'ADD': 0, 'ADC': 1, 'SUB': 2, 'SBC': 3, 'AND': 4, 'XOR': 5, 'OR': 6, 'CP': 7}
ROT = {'RLC': 0, 'RRC': 1, 'RL': 2, 'RR': 3, 'SLA': 4, 'SRA': 5, 'SLL': 6, 'SRL': 7}
BITS = {'BIT': 0x40, 'RES': 0x80, 'SET': 0xC0}
SIMPLE = {'NOP': [0x00], 'HALT': [0x76], 'DI': [0xF3], 'EI': [0xFB], 'DAA': [0x27], 'CPL': [0x2F], 'SCF': [0x37], 'CCF': [0x3F],
          'RLCA': [0x07], 'RRCA': [0x0F], 'RLA': [0x17], 'RRA': [0x1F], 'EXX': [0xD9], 'NEG': [0xED, 0x44], 'RETI': [0xED, 0x4D],
          'RETN': [0xED, 0x45], 'RLD': [0xED, 0x6F], 'RRD': [0xED, 0x67], 'LDI': [0xED, 0xA0], 'LDD': [0xED, 0xA8],
          'LDIR': [0xED, 0xB0], 'LDDR': [0xED, 0xB8], 'CPI': [0xED, 0xA1], 'CPD': [0xED, 0xA9], 'CPIR': [0xED, 0xB1],
          'CPDR': [0xED, 0xB9], 'INI': [0xED, 0xA2], 'IND': [0xED, 0xAA], 'INIR': [0xED, 0xB2], 'INDR': [0xED, 0xBA],
          'OUTI': [0xED, 0xA3], 'OUTD': [0xED, 0xAB], 'OTIR': [0xED, 0xB3], 'OTDR': [0xED, 0xBB]}
IDX = re.compile(r'^\((IX|IY)\s*([+-].*)?\)$', re.I)


def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        if ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


class Asm:
    def __init__(self):
        self.sym = {}
        self.pc = 0
        self.out = {}
        self.final = False

    def ev(self, e, line):
        e = e.strip()
        e = re.sub(r'\$([0-9A-Fa-f]+)\b', lambda m: '0x' + m.group(1), e) if re.match(r'^\$[0-9A-Fa-f]+$', e) else e
        e = re.sub(r'\$', str(self.pc), e)
        env = dict(self.sym)
        env['lo'] = lambda x: x & 0xFF
        env['hi'] = lambda x: (x >> 8) & 0xFF
        try:
            return int(eval(e, {"__builtins__": {}}, env))
        except NameError as ex:
            if self.final:
                raise AsmErr("%s: unknown symbol in %r" % (line, e))
            return 0
        except Exception as ex:
            raise AsmErr("%s: bad expression %r (%s)" % (line, e, ex))

    def n8(self, e, line):
        v = self.ev(e, line)
        if self.final and not (-128 <= v <= 255):
            raise AsmErr("%s: %r does not fit a byte" % (line, e))
        return v & 0xFF

    def n16(self, e, line):
        v = self.ev(e, line)
        return [v & 0xFF, (v >> 8) & 0xFF]

    def idx(self, op, line):
        m = IDX.match(op.strip())
        if not m:
            return None
        d = self.ev(m.group(2), line) if m.group(2) else 0
        if self.final and not (-128 <= d <= 127):
            raise AsmErr("%s: displacement %d" % (line, d))
        return (0xDD if m.group(1).upper() == 'IX' else 0xFD), d & 0xFF

    def mem(self, op):
        op = op.strip()
        if op.startswith('(') and op.endswith(')') and op.upper() not in ('(HL)', '(BC)', '(DE)', '(SP)', '(C)') and not IDX.match(op):
            return op[1:-1]
        return None

    def insn(self, mn, ops, line):
        mn = mn.upper()
        u = [o.upper() for o in ops]
        if mn in SIMPLE and not ops:
            return SIMPLE[mn]
        if mn == 'IM':
            return [0xED, {0: 0x46, 1: 0x56, 2: 0x5E}[self.ev(ops[0], line)]]
        if mn == 'RST':
            return [0xC7 | (self.ev(ops[0], line) & 0x38)]
        if mn in ('PUSH', 'POP'):
            base = 0xC5 if mn == 'PUSH' else 0xC1
            if u[0] in R16AF:
                return [base | (R16AF[u[0]] << 4)]
            if u[0] in ('IX', 'IY'):
                return [0xDD if u[0] == 'IX' else 0xFD, base | 0x20]
        if mn == 'EX':
            if u == ['DE', 'HL']:
                return [0xEB]
            if u == ['AF', "AF'"]:
                return [0x08]
            if u == ['(SP)', 'HL']:
                return [0xE3]
            if u in (['(SP)', 'IX'], ['(SP)', 'IY']):
                return [0xDD if u[1] == 'IX' else 0xFD, 0xE3]
        if mn in ALU:
            a = ops[1] if len(ops) == 2 and u[0] == 'A' else (ops[0] if len(ops) == 1 else None)
            ua = a.upper() if a else None
            if mn in ('ADD', 'ADC', 'SBC') and len(ops) == 2 and u[0] == 'HL':
                rr = R16[u[1]]
                return [0x09 | (rr << 4)] if mn == 'ADD' else [0xED, (0x4A if mn == 'ADC' else 0x42) | (rr << 4)]
            if mn == 'ADD' and len(ops) == 2 and u[0] in ('IX', 'IY'):
                p = 0xDD if u[0] == 'IX' else 0xFD
                src = {'BC': 0, 'DE': 1, u[0]: 2, 'SP': 3}[u[1]]
                return [p, 0x09 | (src << 4)]
            if a is not None:
                if ua in R8:
                    return [0x80 | (ALU[mn] << 3) | R8[ua]]
                ix = self.idx(a, line)
                if ix:
                    return [ix[0], 0x80 | (ALU[mn] << 3) | 6, ix[1]]
                return [0xC6 | (ALU[mn] << 3), self.n8(a, line)]
        if mn in ('INC', 'DEC'):
            inc = mn == 'INC'
            if u[0] in R8:
                return [(0x04 if inc else 0x05) | (R8[u[0]] << 3)]
            if u[0] in R16:
                return [(0x03 if inc else 0x0B) | (R16[u[0]] << 4)]
            if u[0] in ('IX', 'IY'):
                return [0xDD if u[0] == 'IX' else 0xFD, 0x23 if inc else 0x2B]
            ix = self.idx(ops[0], line)
            if ix:
                return [ix[0], 0x34 if inc else 0x35, ix[1]]
        if mn in ROT or mn in BITS:
            if mn in BITS:
                b = self.ev(ops[0], line) & 7
                tgt = ops[1]
                base = BITS[mn] | (b << 3)
            else:
                tgt = ops[0]
                base = ROT[mn] << 3
            ix = self.idx(tgt, line)
            if ix:
                r = 6
                if len(ops) == (3 if mn in BITS else 2):
                    r = R8[ops[-1].upper()]            # the undocumented copy to a register
                return [ix[0], 0xCB, ix[1], base | r]
            return [0xCB, base | R8[tgt.upper()]]
        if mn == 'JP':
            if u == ['(HL)']:
                return [0xE9]
            if u[0] in ('(IX)', '(IY)'):
                return [0xDD if u[0] == '(IX)' else 0xFD, 0xE9]
            if len(ops) == 2 and u[0] in CC:
                return [0xC2 | (CC[u[0]] << 3)] + self.n16(ops[1], line)
            return [0xC3] + self.n16(ops[0], line)
        if mn == 'CALL':
            if len(ops) == 2 and u[0] in CC:
                return [0xC4 | (CC[u[0]] << 3)] + self.n16(ops[1], line)
            return [0xCD] + self.n16(ops[0], line)
        if mn == 'RET':
            return [0xC9] if not ops else [0xC0 | (CC[u[0]] << 3)]
        if mn in ('JR', 'DJNZ'):
            tgt = ops[-1]
            rel = self.ev(tgt, line) - (self.pc + 2)
            if self.final and not (-128 <= rel <= 127):
                raise AsmErr("%s: relative jump out of range (%d)" % (line, rel))
            op = 0x10 if mn == 'DJNZ' else (0x18 if len(ops) == 1 else 0x20 | (CC[u[0]] << 3))
            return [op, rel & 0xFF]
        if mn == 'IN':
            if u[1] == '(C)':
                return [0xED, 0x40 | (R8[u[0]] << 3)]
            return [0xDB, self.n8(self.mem(ops[1]), line)]
        if mn == 'OUT':
            if u[0] == '(C)':
                return [0xED, 0x41 | (R8[u[1]] << 3)]
            return [0xD3, self.n8(self.mem(ops[0]), line)]
        if mn == 'LD':
            d, s = ops[0], ops[1]
            ud, us = u[0], u[1]
            dm, sm = self.mem(d), self.mem(s)
            di, si = self.idx(d, line), self.idx(s, line)
            if ud in R8 and us in R8:
                return [0x40 | (R8[ud] << 3) | R8[us]]
            if di:
                if us in R8:
                    return [di[0], 0x70 | R8[us], di[1]]
                return [di[0], 0x36, di[1], self.n8(s, line)]
            if si and ud in R8:
                return [si[0], 0x46 | (R8[ud] << 3), si[1]]
            if ud in R8 and dm is None and sm is None and us not in R16 and us not in ('I', 'R', 'IX', 'IY') and not s.startswith('('):
                return [0x06 | (R8[ud] << 3), self.n8(s, line)]
            if ud == 'A' and us == '(BC)':
                return [0x0A]
            if ud == 'A' and us == '(DE)':
                return [0x1A]
            if ud == '(BC)' and us == 'A':
                return [0x02]
            if ud == '(DE)' and us == 'A':
                return [0x12]
            if ud == 'A' and sm is not None:
                return [0x3A] + self.n16(sm, line)
            if dm is not None and us == 'A':
                return [0x32] + self.n16(dm, line)
            if ud == 'A' and us == 'I':
                return [0xED, 0x57]
            if ud == 'A' and us == 'R':
                return [0xED, 0x5F]
            if ud == 'I' and us == 'A':
                return [0xED, 0x47]
            if ud == 'R' and us == 'A':
                return [0xED, 0x4F]
            if ud == 'SP' and us == 'HL':
                return [0xF9]
            if ud == 'SP' and us in ('IX', 'IY'):
                return [0xDD if us == 'IX' else 0xFD, 0xF9]
            if ud == 'HL' and sm is not None:
                return [0x2A] + self.n16(sm, line)
            if dm is not None and us == 'HL':
                return [0x22] + self.n16(dm, line)
            if ud in ('IX', 'IY'):
                p = 0xDD if ud == 'IX' else 0xFD
                if sm is not None:
                    return [p, 0x2A] + self.n16(sm, line)
                return [p, 0x21] + self.n16(s, line)
            if dm is not None and us in ('IX', 'IY'):
                return [0xDD if us == 'IX' else 0xFD, 0x22] + self.n16(dm, line)
            if ud in R16 and sm is not None:
                return [0xED, 0x4B | (R16[ud] << 4)] + self.n16(sm, line)
            if dm is not None and us in R16:
                return [0xED, 0x43 | (R16[us] << 4)] + self.n16(dm, line)
            if ud in R16:
                return [0x01 | (R16[ud] << 4)] + self.n16(s, line)
        raise AsmErr("%s: cannot assemble %s %s" % (line, mn, ", ".join(ops)))

    def run(self, text):
        lines = text.split('\n')
        for final in (False, True):
            self.final = final
            self.pc = 0
            self.out = {}
            for n, raw in enumerate(lines, 1):
                line = "line %d" % n
                s = raw.split(';')[0].rstrip()
                if not s.strip():
                    continue
                m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*)$', s)
                if m:
                    self.sym[m.group(1)] = self.pc
                    s = m.group(2)
                    if not s.strip():
                        continue
                m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+(.*)$', s.strip(), re.I)
                if m:
                    self.sym[m.group(1)] = self.ev(m.group(2), line)
                    continue
                s = s.strip()
                parts = s.split(None, 1)
                mn = parts[0]
                rest = parts[1] if len(parts) > 1 else ''
                low = mn.lower()
                if low == 'org':
                    self.pc = self.ev(rest, line)
                    continue
                if low == 'db':
                    data = []
                    for o in split_ops(rest):
                        if o.startswith('"') and o.endswith('"'):
                            data += [ord(c) for c in o[1:-1]]
                        else:
                            data.append(self.n8(o, line))
                elif low == 'dw':
                    data = []
                    for o in split_ops(rest):
                        data += self.n16(o, line)
                elif low == 'ds':
                    o = split_ops(rest)
                    data = [self.n8(o[1], line) if len(o) > 1 else 0] * self.ev(o[0], line)
                else:
                    data = self.insn(mn, split_ops(rest), line)
                for i, b in enumerate(data):
                    self.out[self.pc + i] = b & 0xFF
                self.pc += len(data)
        if not self.out:
            return b''
        top = max(self.out) + 1
        img = bytearray(top)
        for a, b in self.out.items():
            img[a] = b
        return bytes(img)


def assemble(text):
    return Asm().run(text)
