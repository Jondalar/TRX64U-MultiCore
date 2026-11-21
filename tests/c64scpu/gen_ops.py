#!/usr/bin/env python3
"""Writes ops_tab.inc: the test cases of scpu2_ops.asm (T241.1 P2), one per documented 6502 opcode, plus the
page-crossing, branch and decimal variants. The expected cycles are the 6502's (MOS data sheet); a stock C64 shows
every case equal (ref/stock_pal.txt). Run: python3 gen_ops.py > ops_tab.inc"""

ZP, ZPP, ZPP2 = 0x5C, 0xF7, 0xF9          # data byte; pointers to $3180 and $31FF
ABS, ABSX = 0x3180, 0x31FF                # data; the base that crosses a page with index 1
IND = 0x3150                              # pointer of JMP (ind)
P0 = 0x24                                 # I and the unused bit set

# kind: 0 plain stub, 1 branch across a page (stub2), 2 JMP/JSR to the tail, 3 JMP (ind), 4 RTS, 5 RTI, 6 BRK
READ = {'imm': 2, 'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'absx': 4, 'absy': 4, 'indx': 6, 'indy': 5}
STORE = {'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'absx': 5, 'absy': 5, 'indx': 6, 'indy': 6}
RMW = {'acc': 2, 'zp': 5, 'zpx': 6, 'abs': 6, 'absx': 7}
OPS = [
    # read group
    ('adc', {'imm': 0x69, 'zp': 0x65, 'zpx': 0x75, 'abs': 0x6D, 'absx': 0x7D, 'absy': 0x79, 'indx': 0x61, 'indy': 0x71}, READ),
    ('and', {'imm': 0x29, 'zp': 0x25, 'zpx': 0x35, 'abs': 0x2D, 'absx': 0x3D, 'absy': 0x39, 'indx': 0x21, 'indy': 0x31}, READ),
    ('cmp', {'imm': 0xC9, 'zp': 0xC5, 'zpx': 0xD5, 'abs': 0xCD, 'absx': 0xDD, 'absy': 0xD9, 'indx': 0xC1, 'indy': 0xD1}, READ),
    ('eor', {'imm': 0x49, 'zp': 0x45, 'zpx': 0x55, 'abs': 0x4D, 'absx': 0x5D, 'absy': 0x59, 'indx': 0x41, 'indy': 0x51}, READ),
    ('lda', {'imm': 0xA9, 'zp': 0xA5, 'zpx': 0xB5, 'abs': 0xAD, 'absx': 0xBD, 'absy': 0xB9, 'indx': 0xA1, 'indy': 0xB1}, READ),
    ('ora', {'imm': 0x09, 'zp': 0x05, 'zpx': 0x15, 'abs': 0x0D, 'absx': 0x1D, 'absy': 0x19, 'indx': 0x01, 'indy': 0x11}, READ),
    ('sbc', {'imm': 0xE9, 'zp': 0xE5, 'zpx': 0xF5, 'abs': 0xED, 'absx': 0xFD, 'absy': 0xF9, 'indx': 0xE1, 'indy': 0xF1}, READ),
    ('ldx', {'imm': 0xA2, 'zp': 0xA6, 'zpy': 0xB6, 'abs': 0xAE, 'absy': 0xBE}, READ),
    ('ldy', {'imm': 0xA0, 'zp': 0xA4, 'zpx': 0xB4, 'abs': 0xAC, 'absx': 0xBC}, READ),
    ('cpx', {'imm': 0xE0, 'zp': 0xE4, 'abs': 0xEC}, READ),
    ('cpy', {'imm': 0xC0, 'zp': 0xC4, 'abs': 0xCC}, READ),
    ('bit', {'zp': 0x24, 'abs': 0x2C}, READ),
    ('sta', {'zp': 0x85, 'zpx': 0x95, 'abs': 0x8D, 'absx': 0x9D, 'absy': 0x99, 'indx': 0x81, 'indy': 0x91}, STORE),
    ('stx', {'zp': 0x86, 'zpy': 0x96, 'abs': 0x8E}, STORE),
    ('sty', {'zp': 0x84, 'zpx': 0x94, 'abs': 0x8C}, STORE),
    ('asl', {'acc': 0x0A, 'zp': 0x06, 'zpx': 0x16, 'abs': 0x0E, 'absx': 0x1E}, RMW),
    ('lsr', {'acc': 0x4A, 'zp': 0x46, 'zpx': 0x56, 'abs': 0x4E, 'absx': 0x5E}, RMW),
    ('rol', {'acc': 0x2A, 'zp': 0x26, 'zpx': 0x36, 'abs': 0x2E, 'absx': 0x3E}, RMW),
    ('ror', {'acc': 0x6A, 'zp': 0x66, 'zpx': 0x76, 'abs': 0x6E, 'absx': 0x7E}, RMW),
    ('inc', {'zp': 0xE6, 'zpx': 0xF6, 'abs': 0xEE, 'absx': 0xFE}, RMW),
    ('dec', {'zp': 0xC6, 'zpx': 0xD6, 'abs': 0xCE, 'absx': 0xDE}, RMW),
]
IMPLIED = [0x18, 0xD8, 0x58, 0xB8, 0xCA, 0x88, 0xE8, 0xC8, 0xEA, 0x38, 0xF8, 0x78, 0xAA, 0xA8, 0xBA, 0x8A, 0x9A, 0x98]
STACK = [(0x48, 3), (0x08, 3), (0x68, 4), (0x28, 4)]
BRANCH = [(0x90, 'c', 0), (0xB0, 'c', 1), (0xF0, 'z', 1), (0xD0, 'z', 0),
          (0x30, 'n', 1), (0x10, 'n', 0), (0x70, 'v', 1), (0x50, 'v', 0)]
FLAG = {'c': 0x01, 'z': 0x02, 'n': 0x80, 'v': 0x40}

cases = []   # (op, lo, hi, len, cyc, p, x, y, kind, var)
def add(op, lo=0, hi=0, ln=1, cyc=2, p=P0, x=0, y=0, kind=0, var=' '):
    cases.append((op, lo, hi, ln, cyc, p, x, y, kind, var))

for name, modes, table in OPS:
    for mode, op in modes.items():
        cyc = table[mode]
        if name == 'stx' and mode == 'zpy':
            cyc = 4
        if mode == 'imm':
            add(op, 0x00, 0, 2, cyc)
        elif mode == 'acc':
            add(op, 0, 0, 1, cyc)
        elif mode in ('zp', 'zpx', 'zpy'):
            add(op, ZP, 0, 2, cyc)
        elif mode == 'abs':
            add(op, ABS & 255, ABS >> 8, 3, cyc)
        elif mode in ('absx', 'absy'):
            add(op, ABS & 255, ABS >> 8, 3, cyc)
            cross = cyc + 1 if table is READ else cyc
            if mode == 'absx':
                add(op, ABSX & 255, ABSX >> 8, 3, cross, x=1, var='x')
            else:
                add(op, ABSX & 255, ABSX >> 8, 3, cross, y=1, var='x')
        elif mode == 'indx':
            add(op, ZPP, 0, 2, cyc)
        elif mode == 'indy':
            add(op, ZPP, 0, 2, cyc)
            add(op, ZPP2, 0, 2, cyc + 1 if table is READ else cyc, y=1, var='x')
add(0x69, 0x00, 0, 2, 2, p=P0 | 0x08, var='d')          # ADC #, decimal
add(0xE9, 0x00, 0, 2, 2, p=P0 | 0x08, var='d')          # SBC #, decimal
for op in IMPLIED:
    add(op, 0, 0, 1, 2)
for op, cyc in STACK:
    add(op, 0, 0, 1, cyc)
add(0x4C, 0, 0, 3, 3, kind=2)                            # JMP abs (target: the tail)
add(0x20, 0, 0, 3, 6, kind=2)                            # JSR abs
add(0x6C, IND & 255, IND >> 8, 3, 5, kind=3)             # JMP (ind)
add(0x60, 0, 0, 1, 6, kind=4)                            # RTS
add(0x40, 0, 0, 1, 6, kind=5)                            # RTI
add(0x00, 0, 0, 1, 7, kind=6)                            # BRK (vector: the tail)
for op, f, taken_when in BRANCH:
    p_taken = P0 | FLAG[f] if taken_when else P0
    p_not = P0 if taken_when else P0 | FLAG[f]
    add(op, 0x00, 0, 2, 2, p=p_not, var='n')
    add(op, 0x00, 0, 2, 3, p=p_taken, var='t')
    add(op, 0x02, 0, 2, 4, p=p_taken, kind=1, var='x')

ops = {c[0] for c in cases}
assert len(ops) == 151, len(ops)
print("; ops_tab.inc - generated by gen_ops.py: the cases of scpu2_ops.asm (T241.1 P2). Do not edit.")
print(f"NCASE = {len(cases)}")
for i, name in enumerate(['c_op', 'c_lo', 'c_hi', 'c_len', 'c_cyc', 'c_p', 'c_x', 'c_y', 'c_kind']):
    vals = [c[i] for c in cases]
    for k in range(0, len(vals), 16):
        lab = name if k == 0 else ''
        print(f"{lab:<8}.byte " + ", ".join(f"${v:02x}" for v in vals[k:k + 16]))
print('        .enc "screen"')
print('c_var   .text "' + ''.join(c[9] for c in cases) + '"')
print('        .enc "none"')
