#!/usr/bin/env python3
"""A small two-pass 6502 assembler for the test programs of the C128 core (tools/gen_c128_test.py), written for this project (T113).

Official opcodes only. Expressions in every operand (python syntax; `$` alone is the current address; hex as 0x.. or $..; lo()/hi()).
Directives: org, equ (NAME equ expr; put them before their first use), db, dw, ds. Labels end in ':'. Comments start with ';'. An instruction it does not know is an
error, never a guess. Returns {address: byte}."""
import re


class AsmErr(Exception):
    pass


IMPL = {'BRK': 0x00, 'PHP': 0x08, 'CLC': 0x18, 'PLP': 0x28, 'SEC': 0x38, 'RTI': 0x40, 'PHA': 0x48, 'CLI': 0x58, 'RTS': 0x60,
        'PLA': 0x68, 'SEI': 0x78, 'DEY': 0x88, 'TXA': 0x8A, 'TYA': 0x98, 'TXS': 0x9A, 'TAY': 0xA8, 'TAX': 0xAA, 'CLV': 0xB8,
        'TSX': 0xBA, 'INY': 0xC8, 'DEX': 0xCA, 'CLD': 0xD8, 'INX': 0xE8, 'NOP': 0xEA, 'SED': 0xF8}
BRANCH = {'BPL': 0x10, 'BMI': 0x30, 'BVC': 0x50, 'BVS': 0x70, 'BCC': 0x90, 'BCS': 0xB0, 'BNE': 0xD0, 'BEQ': 0xF0}
# mnemonic: {mode: opcode}; modes: imm zp zpx zpy abs abx aby inx iny acc
OPS = {
    'ORA': dict(imm=0x09, zp=0x05, zpx=0x15, abs=0x0D, abx=0x1D, aby=0x19, inx=0x01, iny=0x11),
    'AND': dict(imm=0x29, zp=0x25, zpx=0x35, abs=0x2D, abx=0x3D, aby=0x39, inx=0x21, iny=0x31),
    'EOR': dict(imm=0x49, zp=0x45, zpx=0x55, abs=0x4D, abx=0x5D, aby=0x59, inx=0x41, iny=0x51),
    'ADC': dict(imm=0x69, zp=0x65, zpx=0x75, abs=0x6D, abx=0x7D, aby=0x79, inx=0x61, iny=0x71),
    'STA': dict(zp=0x85, zpx=0x95, abs=0x8D, abx=0x9D, aby=0x99, inx=0x81, iny=0x91),
    'LDA': dict(imm=0xA9, zp=0xA5, zpx=0xB5, abs=0xAD, abx=0xBD, aby=0xB9, inx=0xA1, iny=0xB1),
    'CMP': dict(imm=0xC9, zp=0xC5, zpx=0xD5, abs=0xCD, abx=0xDD, aby=0xD9, inx=0xC1, iny=0xD1),
    'SBC': dict(imm=0xE9, zp=0xE5, zpx=0xF5, abs=0xED, abx=0xFD, aby=0xF9, inx=0xE1, iny=0xF1),
    'ASL': dict(acc=0x0A, zp=0x06, zpx=0x16, abs=0x0E, abx=0x1E),
    'ROL': dict(acc=0x2A, zp=0x26, zpx=0x36, abs=0x2E, abx=0x3E),
    'LSR': dict(acc=0x4A, zp=0x46, zpx=0x56, abs=0x4E, abx=0x5E),
    'ROR': dict(acc=0x6A, zp=0x66, zpx=0x76, abs=0x6E, abx=0x7E),
    'STX': dict(zp=0x86, zpy=0x96, abs=0x8E),
    'LDX': dict(imm=0xA2, zp=0xA6, zpy=0xB6, abs=0xAE, aby=0xBE),
    'DEC': dict(zp=0xC6, zpx=0xD6, abs=0xCE, abx=0xDE),
    'INC': dict(zp=0xE6, zpx=0xF6, abs=0xEE, abx=0xFE),
    'STY': dict(zp=0x84, zpx=0x94, abs=0x8C),
    'LDY': dict(imm=0xA0, zp=0xA4, zpx=0xB4, abs=0xAC, abx=0xBC),
    'CPY': dict(imm=0xC0, zp=0xC4, abs=0xCC),
    'CPX': dict(imm=0xE0, zp=0xE4, abs=0xEC),
    'BIT': dict(zp=0x24, abs=0x2C),
}


class Asm:
    def __init__(self):
        self.sym = {}
        self.pc = 0
        self.out = {}
        self.final = False

    def ev(self, e, line):
        e = e.strip()
        e = re.sub(r'\$([0-9A-Fa-f]+)', lambda m: '0x' + m.group(1), e)
        e = re.sub(r'(?<![\w])\$(?![\w])', str(self.pc), e)
        env = dict(self.sym)
        env['lo'] = lambda x: x & 0xFF
        env['hi'] = lambda x: (x >> 8) & 0xFF
        try:
            return int(eval(e, {"__builtins__": {}}, env))
        except NameError:
            if self.final:
                raise AsmErr("%s: unknown symbol in %r" % (line, e))
            return 0
        except Exception as ex:
            raise AsmErr("%s: bad expression %r (%s)" % (line, e, ex))

    def insn(self, mn, op, line):
        mn = mn.upper()
        if mn in IMPL and not op:
            return [IMPL[mn]]
        if mn in BRANCH:
            tgt = self.ev(op, line)
            off = tgt - (self.pc + 2)
            if self.final and not -128 <= off <= 127:
                raise AsmErr("%s: branch out of range (%d)" % (line, off))
            return [BRANCH[mn], off & 0xFF]
        if mn == 'JMP':
            m = re.match(r'^\((.*)\)$', op.strip())
            if m:
                v = self.ev(m.group(1), line)
                return [0x6C, v & 0xFF, v >> 8]
            v = self.ev(op, line)
            return [0x4C, v & 0xFF, v >> 8]
        if mn == 'JSR':
            v = self.ev(op, line)
            return [0x20, v & 0xFF, v >> 8]
        if mn not in OPS:
            raise AsmErr("%s: unknown instruction %r" % (line, mn))
        t = OPS[mn]
        op = op.strip()
        if op == '' or op.upper() == 'A':
            if 'acc' in t:
                return [t['acc']]
            raise AsmErr("%s: %s needs an operand" % (line, mn))
        if op.startswith('#'):
            v = self.ev(op[1:], line)
            if self.final and not -128 <= v <= 255:
                raise AsmErr("%s: %r does not fit a byte" % (line, op))
            return [t['imm'], v & 0xFF]
        m = re.match(r'^\((.*),\s*[xX]\)$', op)
        if m:
            return [t['inx'], self.ev(m.group(1), line) & 0xFF]
        m = re.match(r'^\((.*)\)\s*,\s*[yY]$', op)
        if m:
            return [t['iny'], self.ev(m.group(1), line) & 0xFF]
        m = re.match(r'^(.*),\s*([xXyY])$', op)
        if m:
            v = self.ev(m.group(1), line)
            r = m.group(2).lower()
            if r == 'x':
                if v < 256 and 'zpx' in t and self.zp_ok(m.group(1)):
                    return [t['zpx'], v]
                return [t['abx'], v & 0xFF, v >> 8]
            if v < 256 and 'zpy' in t and self.zp_ok(m.group(1)):
                return [t['zpy'], v]
            return [t['aby'], v & 0xFF, v >> 8]
        v = self.ev(op, line)
        if v < 256 and 'zp' in t and self.zp_ok(op):
            return [t['zp'], v]
        return [t['abs'], v & 0xFF, v >> 8]

    def zp_ok(self, e):
        """zero page only when written as a number below 256 or with a symbol defined (pass 2) below 256; unknown symbols in pass 1 are
        absolute (3 bytes) in both passes: the symbol table of pass 1 is complete for backward references only, so a forward reference is
        always absolute, in both passes alike (sizes must not change between the passes)."""
        e = e.strip()
        if re.match(r'^(\$[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|\d+)$', e):
            return True
        names = re.findall(r'[A-Za-z_]\w*', e)
        return all(n in self.zpsyms for n in names) if names else True

    def run(self, text, zpsyms=()):
        self.zpsyms = set(zpsyms)
        lines = text.splitlines()
        for final in (False, True):
            self.final = final
            self.pc = 0
            self.out = {}
            for raw in lines:
                line = raw.split(';')[0].rstrip()
                if not line.strip():
                    continue
                m = re.match(r'^\s*([A-Za-z_]\w*)\s+equ\s+(.*)$', line, re.I)
                if m:
                    self.sym[m.group(1)] = self.ev(m.group(2), line)
                    if self.sym[m.group(1)] < 256:
                        self.zpsyms.add(m.group(1))
                    continue
                m = re.match(r'^([A-Za-z_]\w*):(.*)$', line)
                if m:
                    self.sym[m.group(1)] = self.pc
                    line = m.group(2)
                    if not line.strip():
                        continue
                parts = line.strip().split(None, 1)
                mn = parts[0]
                rest = parts[1].strip() if len(parts) > 1 else ''
                low = mn.lower()
                if low == 'org':
                    self.pc = self.ev(rest, line)
                    continue
                if low in ('db', 'dw'):
                    data = []
                    for o in [x.strip() for x in rest.split(',')]:
                        v = self.ev(o, line)
                        data += [v & 0xFF] if low == 'db' else [v & 0xFF, (v >> 8) & 0xFF]
                elif low == 'ds':
                    o = [x.strip() for x in rest.split(',')]
                    data = [(self.ev(o[1], line) if len(o) > 1 else 0) & 0xFF] * self.ev(o[0], line)
                else:
                    data = self.insn(mn, rest, line)
                for i, b in enumerate(data):
                    self.out[self.pc + i] = b & 0xFF
                self.pc += len(data)
        return self.out


def assemble(text, zpsyms=()):
    return Asm().run(text, zpsyms)
