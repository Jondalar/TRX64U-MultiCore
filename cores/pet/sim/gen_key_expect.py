#!/usr/bin/env python3
"""T216 S1/S2/S14: writes cores/pet/sim/keys_expect.vh - for every C64 key the PET matrix position (row, column) that VICE's keyboard files give.
The expectation comes from vice/gtk3_buuk_pos.vkm (business) and vice/gtk3_grus_pos.vkm (graphics), copies of VICE's data/PET files, so the bench
compares pet_keys.v against VICE and not against gen_keys.py. Usage: python3 gen_key_expect.py > keys_expect.vh"""
import os
import sys

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
src = open(os.path.join(D, 'gen_keys.py')).read().split('HEAD =')[0]
ns = {}
exec(src, ns)
C64, DIG = ns['C64'], ns['DIG']


def vkm(name):
    v = {}
    for line in open(os.path.join(D, 'vice', name)):
        t = line.split('#')[0].split()
        if len(t) == 4 and t[1].lstrip('-').isdigit():
            v[t[0]] = (int(t[1]), int(t[2]))
    return v


bu, gr = vkm('gtk3_buuk_pos.vkm'), vkm('gtk3_grus_pos.vkm')
out = []


def put(kind, key, cbm, vice, v):
    r, c = v[vice]
    out.append("    %s(%d, %d, %d, %d);   // C64 %s%s = VICE %s" % (kind, C64[key] if key in C64 else key, cbm, r, c, 'C= + ' if cbm else '', key, vice))


# business keyboard: C64 key (legend, C= state) -> the VICE key symbol that sits at the PET position
for ch in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
    put('bchk', ch, 0, ch.lower(), bu)
for d in '0123456789':
    out.append("    bchk(%d, 0, %d, %d);   // C64 %s = VICE %s" % (DIG[d], bu[d][0], bu[d][1], d, d))
    out.append("    bchk(%d, 1, %d, %d);   // C64 C= + %s = VICE KP_%s" % (DIG[d], bu['KP_' + d][0], bu['KP_' + d][1], d, d))
for key, vice in [(',', 'comma'), ('.', 'period'), ('/', 'slash'), (';', 'semicolon'), (':', 'minus'), ('-', 'KP_Subtract'), ('[', 'bracketleft'),
                  ('=', 'End'), ('POUND', 'bracketright'), ('@', 'apostrophe'), ('RET', 'Return'), ('SPACE', 'space'), ('DEL', 'Delete'),
                  ('HOME', 'Home'), ('CRSR_R', 'Right'), ('CRSR_D', 'Down'), ('UP', 'BackSpace'), ('BACK', 'grave'), ('STOP', 'Escape'),
                  ('F1', 'F1'), ('F3', 'F2'), ('F5', 'Tab'), ('F7', 'Page_Down')]:
    put('bchk', key, 0, vice, bu)
put('bchk', '.', 1, 'KP_Decimal', bu)
# either C64 shift is the PET shift (VICE Shift_L)
out.append("    bchk(15, 0, %d, %d);   // C64 left SHIFT = VICE Shift_L" % bu['Shift_L'])
out.append("    bchk(52, 0, %d, %d);   // C64 right SHIFT = VICE Shift_L" % bu['Shift_L'])
# graphics keyboard (2001, 3032, 4032): letters, the digit row, RETURN, SPACE
for ch in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
    put('gchk', ch, 0, ch.lower(), gr)
for d in '0123456789':
    out.append("    gchk(%d, 0, %d, %d);   // C64 %s = VICE %s" % (DIG[d], gr[d][0], gr[d][1], d, d))
for key, vice in [('RET', 'Return'), ('SPACE', 'space')]:
    put('gchk', key, 0, vice, gr)

# ---- T216 P10 symbolic mapping (S14): expected by the CHARACTER a C64 key types, looked up on the PET's own key legends (the header of the VICE vkm files above), not in gen_keys.py.
# a PET legend: character -> (row, column, PET shift down). Graphics board: symbols unshifted, digits on the keypad. Business board: typewriter, + * = shifted.
LEG_G = {c: (r, k, 0) for c, (r, k) in {'!': (0, 0), '"': (1, 0), '#': (0, 1), '$': (1, 1), '%': (0, 2), "'": (1, 2), '&': (0, 3), '\\': (1, 3), '(': (0, 4), ')': (1, 4),
         '[': (9, 1), ']': (8, 2), '@': (8, 1), '<': (9, 3), '>': (8, 4), '?': (7, 4), ':': (5, 4), ';': (6, 4), ',': (7, 3), '7': (2, 6), '9': (2, 7), '8': (3, 6), '/': (3, 7),
         '4': (4, 6), '6': (4, 7), '5': (5, 6), '*': (5, 7), '1': (6, 6), '3': (6, 7), '2': (7, 6), '+': (7, 7), '0': (8, 6), '-': (8, 7), '.': (9, 6), '=': (9, 7)}.items()}
LEG_B = {}
for ch, (r, k), sh in [('1', (1, 0), 0), ('!', (1, 0), 1), ('2', (0, 0), 0), ('"', (0, 0), 1), ('3', (9, 1), 0), ('#', (9, 1), 1), ('4', (1, 1), 0), ('$', (1, 1), 1),
                       ('5', (0, 1), 0), ('%', (0, 1), 1), ('6', (9, 2), 0), ('&', (9, 2), 1), ('7', (1, 2), 0), ("'", (1, 2), 1), ('8', (0, 2), 0), ('(', (0, 2), 1),
                       ('9', (9, 3), 0), (')', (9, 3), 1), ('0', (1, 3), 0), ('-', (0, 3), 0), ('=', (0, 3), 1), (':', (9, 5), 0), ('*', (9, 5), 1), (';', (2, 6), 0),
                       ('+', (2, 6), 1), ('@', (3, 6), 0), ('[', (5, 6), 0), (']', (2, 4), 0), ('\\', (4, 4), 0), (',', (7, 3), 0), ('<', (7, 3), 1), ('.', (6, 3), 0),
                       ('>', (6, 3), 1), ('/', (8, 6), 0), ('?', (8, 6), 1)]:
    LEG_B[ch] = (r, k, sh)
# the C64 keys' legends: key -> (alone, with C64 shift); None = a graphics character (the PET gets the same key with its shift down)
C64LEG = {DIG[d]: (d, '!"#$%&\'()'[i] if i < 9 else None) for i, d in enumerate('1234567890')}
C64LEG.update({40: ('+', None), 43: ('-', None), 49: ('*', None), 53: ('=', None), 45: (':', '['), 50: (';', ']'), 46: ('@', None), 55: ('/', '?'), 47: (',', '<'),
               44: ('.', '>'), 48: ('\\', None)})
sym_out = []


def sput(kind, key, shift, leg):
    alone, shifted = C64LEG[key]
    ch = shifted if shift else alone
    if ch is None:
        r, c, sh = leg[alone]
        sh = 1                                           # a graphics character: the PET key of the unshifted legend with its shift
        shown = 'shift+%s (graphics)' % alone
    else:
        r, c, sh = leg[ch]
        shown = ch
    sym_out.append("    %s(%d, %d, %d, %d, %d);   // C64 %s%s types %s" % (kind, key, shift, r, c, sh, 'shift + ' if shift else '', alone, shown))


for key in C64LEG:
    for shift in (0, 1):
        sput('ssym_b', key, shift, LEG_B)
        sput('ssym_g', key, shift, LEG_G)
# keys that are the same in both mappings: letters (with and without the C64 shift), RETURN, SPACE, DEL, HOME, cursors, STOP
for ch in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
    for shift in (0, 1):
        sym_out.append("    ssym_b(%d, %d, %d, %d, %d);   // C64 %s%s" % (C64[ch], shift, bu[ch.lower()][0], bu[ch.lower()][1], shift, 'shift + ' if shift else '', ch))
        sym_out.append("    ssym_g(%d, %d, %d, %d, %d);   // C64 %s%s" % (C64[ch], shift, gr[ch.lower()][0], gr[ch.lower()][1], shift, 'shift + ' if shift else '', ch))
for key, vb, vg in [('RET', 'Return', 'Return'), ('SPACE', 'space', 'space'), ('DEL', 'Delete', 'Delete'), ('HOME', 'Home', 'Home'), ('CRSR_R', 'Right', 'Right'),
                    ('CRSR_D', 'Down', 'Down'), ('STOP', 'Escape', 'Escape')]:
    sym_out.append("    ssym_b(%d, 0, %d, %d, 0);   // C64 %s" % (C64[key], bu[vb][0], bu[vb][1], key))
    sym_out.append("    ssym_g(%d, 0, %d, %d, 0);   // C64 %s" % (C64[key], gr[vg][0], gr[vg][1], key))
print("// Generated by gen_key_expect.py from sim/vice/*.vkm (VICE data/PET); do not edit by hand.")
print("task bchk_all; begin")
print("\n".join(l for l in out if l.lstrip().startswith('bchk')))
print("end endtask")
print("task gchk_all; begin")
print("\n".join(l for l in out if l.lstrip().startswith('gchk')))
print("end endtask")
print("task ssym_all; begin")
print("\n".join(sym_out))
print("end endtask")
