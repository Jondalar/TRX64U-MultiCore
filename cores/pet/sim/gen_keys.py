#!/usr/bin/env python3
"""T134, T216 P10: writes cores/pet/rtl/trx/pet_keys.v - the host's C64 key matrix (keys[col*8+row], 1 = down) into the PET graphics-keyboard
matrix (10 rows x 8 columns, a pressed key reads 0). The matrix positions are the ASCII legends of Pet2001_Arty key_matrix.pl; the shift
sits at (8,5) as in the MiSTer port. Usage: python3 gen_keys.py > ../rtl/trx/pet_keys.v"""
C64 = {}
for i, n in enumerate("DEL RET CRSR_R F7 F1 F3 F5 CRSR_D".split()):
    C64[n] = i
lab = {8: '3', 9: 'W', 10: 'A', 11: '4', 12: 'Z', 13: 'S', 14: 'E', 16: '5', 17: 'R', 18: 'D', 19: '6', 20: 'C', 21: 'F', 22: 'T', 23: 'X',
       24: '7', 25: 'Y', 26: 'G', 27: '8', 28: 'B', 29: 'H', 30: 'U', 31: 'V', 32: '9', 33: 'I', 34: 'J', 35: '0', 36: 'M', 37: 'K', 38: 'O',
       39: 'N', 40: '+', 41: 'P', 42: 'L', 43: '-', 44: '.', 45: ':', 46: '@', 47: ',', 48: 'POUND', 49: '*', 50: ';', 51: 'HOME', 53: '=',
       54: 'UP', 55: '/', 56: '1', 57: 'BACK', 58: 'CTRL', 59: '2', 60: 'SPACE', 61: 'CBM', 62: 'Q', 63: 'STOP'}
for i, n in lab.items():
    C64[n] = i
DIG = {'1': 56, '2': 59, '3': 8, '4': 11, '5': 16, '6': 19, '7': 24, '8': 27, '9': 32, '0': 35}
# Each PET key is one term: a C64 legend, 'sym:d' = digit d unless C= is down, 'pad:d' = digit d while C= is down (the PET's digits are on its
# keypad and the C64 has none: C= + digit types the keypad digit), SHIFT = either C64 shift, CBM = C= unless a digit is down.
M = {
    0: {7: 'CRSR_R', 6: 'HOME', 5: 'BACK', 4: 'sym:9', 3: 'sym:7', 2: 'sym:5', 1: 'sym:3', 0: 'sym:1'},
    1: {7: 'DEL', 6: 'CRSR_D', 4: 'sym:0', 3: 'sym:8', 2: 'sym:6', 1: 'sym:4', 0: 'sym:2'},
    2: {7: 'pad:9', 6: 'pad:7', 5: 'UP', 4: 'O', 3: 'U', 2: 'T', 1: 'E', 0: 'Q'},
    3: {7: 'F7', 6: 'pad:8', 4: 'P', 3: 'I', 2: 'Y', 1: 'R', 0: 'W'},
    4: {7: 'pad:6', 6: 'pad:4', 4: 'L', 3: 'J', 2: 'G', 1: 'D', 0: 'A'},
    5: {7: '*', 6: 'pad:5', 4: ':', 3: 'K', 2: 'H', 1: 'F', 0: 'S'},
    6: {7: 'pad:3', 6: 'pad:1', 5: 'RET', 4: ';', 3: 'M', 2: 'B', 1: 'C', 0: 'Z'},
    7: {7: '+', 6: 'pad:2', 4: '/', 3: ',', 2: 'N', 1: 'V', 0: 'X'},
    8: {7: '-', 6: 'pad:0', 5: 'SHIFT', 4: '.', 2: 'POUND', 1: '@', 0: 'CBM'},
    9: {7: '=', 6: 'F3', 4: 'STOP', 3: 'F5', 2: 'SPACE', 1: 'F1', 0: 'CTRL'},
}

# The business keyboard (8032, 8096, SuperPET): rows/columns from VICE's gtk3_buuk_pos.vkm (the same PIA1 row select / column read), positional
# on the C64: letters and the digit row where they are, '+' -> [, '=' -> ], POUND -> backslash, F1 ESC, F3 RVS off, F5 TAB, F7 REPEAT, the keypad
# digits C= + digit, the keypad point C= + '.'. The shift is either C64 SHIFT (row 6, column 0); the business board has no CBM / CTRL key.
B = {
    0: {0: 'sym:2', 1: 'sym:5', 2: 'sym:8', 3: '-', 4: 'pad:8', 5: 'CRSR_R'},
    1: {0: 'sym:1', 1: 'sym:4', 2: 'sym:7', 3: 'sym:0', 4: 'pad:7', 5: 'UP', 7: 'pad:9'},
    2: {0: 'F1', 1: 'S', 2: 'F', 3: 'H', 4: ']', 5: 'K', 6: ';', 7: 'pad:5'},
    3: {0: 'A', 1: 'D', 2: 'G', 3: 'J', 4: 'RET', 5: 'L', 6: '@', 7: 'pad:6'},
    4: {0: 'F5', 1: 'W', 2: 'R', 3: 'Y', 4: 'POUND', 5: 'I', 6: 'P', 7: 'DEL'},
    5: {0: 'Q', 1: 'E', 2: 'T', 3: 'U', 4: 'CRSR_D', 5: 'O', 6: '[', 7: 'pad:4'},
    6: {0: 'SHIFT', 1: 'C', 2: 'B', 3: '.', 4: 'padpt', 7: 'pad:3'},
    7: {0: 'Z', 1: 'V', 2: 'N', 3: ',', 4: 'pad:0', 6: 'F7', 7: 'pad:2'},
    8: {0: 'F3', 1: 'X', 2: 'SPACE', 3: 'M', 4: 'HOME', 6: '/', 7: 'pad:1'},
    9: {0: 'BACK', 1: 'sym:3', 2: 'sym:6', 3: 'sym:9', 4: 'STOP', 5: ':'},
}
C64['['] = 40
C64[']'] = 53
C64[','] = 47
C64['.'] = 44
C64['/'] = 55
C64[';'] = 50
C64[':'] = 45
C64['-'] = 43
C64['@'] = 46


def bterm(t):
    if t == 'SHIFT':
        return '(keys[15] | keys[52])'
    if t.startswith('sym:'):
        return '(keys[%d] & ~keys[61])' % DIG[t[4:]]
    if t.startswith('pad:'):
        return '(keys[%d] & keys[61])' % DIG[t[4:]]
    if t == 'padpt':
        return '(keys[44] & keys[61])'
    if t in ('.', ','):
        return '(keys[%d] & ~keys[61])' % C64[t]
    return 'keys[%d]' % C64[t]


def term(t):
    if t == 'SHIFT':
        return '(keys[15] | keys[52])'
    if t == 'CBM':
        return '(keys[61] & ~dig)'
    if t.startswith('sym:'):
        return '(keys[%d] & ~keys[61])' % DIG[t[4:]]
    if t.startswith('pad:'):
        return '(keys[%d] & keys[61])' % DIG[t[4:]]
    return 'keys[%d]' % C64[t]


# ---- T216 P10: the symbolic mapping: the C64 key's legend counts (typing "1" gives 1, shift + 1 gives !, ":" gives :, "+" gives +, ...). A key of the table below is
# translated per character: (PET matrix cell, PET shift down) for the key alone and for the key with a C64 shift; every other key (letters, RETURN, SPACE, cursor
# keys, STOP, F-keys, up/left arrow) stays where the positional table has it and the C64 shift is the PET shift. Both tables are checked against the PET key legends
# (VICE gtk3_*_pos.vkm header) by gen_key_expect.py, not against this table.
TABLE_KEYS = [DIG[d] for d in '1234567890'] + [40, 43, 49, 53, 45, 50, 46, 55, 47, 44, 48]   # + - * = : ; @ / , . POUND
GSHIFTED = {'1': (0, 0), '2': (1, 0), '3': (0, 1), '4': (1, 1), '5': (0, 2), '6': (0, 3), '7': (1, 2), '8': (0, 4), '9': (1, 4)}   # ! " # $ % & ' ( ) on the graphics keys
GKP = {'0': (8, 6), '1': (6, 6), '2': (7, 6), '3': (6, 7), '4': (4, 6), '5': (5, 6), '6': (4, 7), '7': (2, 6), '8': (3, 6), '9': (2, 7)}
# key index: (alone (cell, shift), with C64 shift (cell, shift))
G_EV = {40: (((7, 7), 0), ((7, 7), 1)), 43: (((8, 7), 0), ((8, 7), 1)), 49: (((5, 7), 0), ((5, 7), 1)), 53: (((9, 7), 0), ((9, 7), 1)),
        45: (((5, 4), 0), ((9, 1), 0)), 50: (((6, 4), 0), ((8, 2), 0)), 46: (((8, 1), 0), ((8, 1), 1)), 55: (((3, 7), 0), ((7, 4), 0)),
        47: (((7, 3), 0), ((9, 3), 0)), 44: (((9, 6), 0), ((8, 4), 0)), 48: (((1, 3), 0), ((1, 3), 1))}
for d in '123456789':
    G_EV[DIG[d]] = ((GKP[d], 0), (GSHIFTED[d], 0))
G_EV[DIG['0']] = ((GKP['0'], 0), (GKP['0'], 1))
BDIG = {'1': (1, 0), '2': (0, 0), '3': (9, 1), '4': (1, 1), '5': (0, 1), '6': (9, 2), '7': (1, 2), '8': (0, 2), '9': (9, 3), '0': (1, 3)}
B_EV = {40: (((2, 6), 1), ((2, 6), 1)), 43: (((0, 3), 0), ((0, 3), 1)), 49: (((9, 5), 1), ((9, 5), 1)), 53: (((0, 3), 1), ((0, 3), 1)),
        45: (((9, 5), 0), ((5, 6), 0)), 50: (((2, 6), 0), ((2, 4), 0)), 46: (((3, 6), 0), ((3, 6), 1)), 55: (((8, 6), 0), ((8, 6), 1)),
        47: (((7, 3), 0), ((7, 3), 1)), 44: (((6, 3), 0), ((6, 3), 1)), 48: (((4, 4), 0), ((4, 4), 1))}
for d in '1234567890':
    B_EV[DIG[d]] = ((BDIG[d], 0), (BDIG[d], 1))
SH = '(keys[15] | keys[52])'


def sym_matrix(P, EV, shiftcell, business):
    cells = {}
    for r in range(10):
        for c in range(8):
            cells[(r, c)] = []
    for r, row in P.items():
        for c, t in row.items():
            if t == 'SHIFT':
                continue
            idx = None
            if t.startswith('sym:'):
                idx = DIG[t[4:]]
            elif t.startswith('pad:') or t == 'padpt':
                if business:                                   # the keypad digits and point stay C= + key
                    cells[(r, c)].append(bterm(t))
                continue
            elif t in C64:
                idx = C64[t]
            if idx in TABLE_KEYS:
                continue
            cells[(r, c)].append(bterm(t) if business else term(t))
    forced = []
    for k, (u, s) in EV.items():
        nocbm = ' & ~keys[61]' if business and (k in [DIG[d] for d in '1234567890'] or k == 44) else ''
        for (cell, sh), cond in ((u, '(keys[%d] & ~%s%s)' % (k, SH, nocbm)), (s, '(keys[%d] & %s%s)' % (k, SH, nocbm))):
            cells[cell].append(cond)
            if sh:
                forced.append(cond)
    anyt = ' | '.join('keys[%d]' % k for k in TABLE_KEYS)
    cells[shiftcell] = forced + ['(%s & ~(%s))' % (SH, anyt)]
    return cells


def emit_cells(cells, name):
    out = []
    for row in range(10):
        ts = [(' | '.join(cells[(row, c)]) if cells[(row, c)] else "1'b0") for c in range(8)]
        out.append("            4'd%d: %s = {%s};" % (row, name, ', '.join(reversed(['(' + t + ')' if ' | ' in t else t for t in ts]))))
    return out


HEAD = """// T134, T216 P10: the host's C64 key matrix (keys[col*8+row], 1 = down) into the PET key matrix (10 rows x 8 columns, a pressed key reads 0).
// Generated by cores/pet/sim/gen_keys.py (its tables are the source; do not edit by hand). Two mappings, chosen by `sym` (the app's keyboard setting):
//   positional (sym = 0): the PET key at the C64 key's place: the PET's symbol keys ! " # $ % ' & \\ ( ) sit on the C64's 1..0, the PET keypad digits are C= + digit, either SHIFT
//   is the PET shift (graphics row 8 column 5, business row 6 column 0), C64 F7/F3/F5/F1 stand in for the PET keys / . < [ that the C64 has no counterpart for. The business
//   matrix (bus = 1, the CRTC models) is from VICE's gtk3_buuk_pos.vkm, the graphics matrix from gtk3_grus_pos.vkm.
//   symbolic (sym = 1): the C64 key's legend counts: a typed character gives the same character on the PET (1 gives 1, shift + 1 gives !, + gives +, ...); the PET shift
//   is set or cleared as that character needs it (the graphics board has its digits on the keypad and its symbols unshifted, the business board has + * = on shifted keys).
`timescale 1ns / 1ps
module pet_keys (
    input  wire [63:0] keys,
    input  wire        bus,                 // 1: the business keyboard (the CRTC models)
    input  wire        sym,                 // 1: symbolic mapping (the C64 key legend counts), 0: positional (the PET key at the same place); T216 P10
    input  wire [3:0]  keyrow,
    output wire [7:0]  keyin
);
    wire dig = keys[56] | keys[59] | keys[8] | keys[11] | keys[16] | keys[19] | keys[24] | keys[27] | keys[32] | keys[35];
    reg [7:0] r, rb, rs, rbs;
    always @* begin
        case (keyrow)"""
print(HEAD)
for row in range(10):
    ts = [term(M[row][c]) if c in M[row] else "1'b0" for c in range(8)]
    print("            4'd%d: r = {%s};" % (row, ', '.join(reversed(ts))))
print("""            default: r = 8'h00;
        endcase
        case (keyrow)""")
for row in range(10):
    ts = [bterm(B[row][c]) if c in B[row] else "1'b0" for c in range(8)]
    print("            4'd%d: rb = {%s};" % (row, ', '.join(reversed(ts))))
print("""            default: rb = 8'h00;
        endcase
        case (keyrow)""")
print("\n".join(emit_cells(sym_matrix(M, G_EV, (8, 5), False), 'rs')))
print("""            default: rs = 8'h00;
        endcase
        case (keyrow)""")
print("\n".join(emit_cells(sym_matrix(B, B_EV, (6, 0), True), 'rbs')))
print("""            default: rbs = 8'h00;
        endcase
    end
    assign keyin = bus ? (sym ? ~rbs : ~rb) : (sym ? ~rs : ~r);
endmodule""")
