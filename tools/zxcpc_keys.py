#!/usr/bin/env python3
"""T91 D8 / T92 D8: the key translator's tables, from the key NAMES of the keyboards (shared by the ZX Spectrum core and the CPC core).

  C64 grid   keys[col * 8 + row], 1 = down (the host's c64_keys; CIA 1: port A = column, port B = row)
  ZX grid    row r (a half-row: A8+r low selects it), bit c: matrix index r * 5 + c, 1 = down

  tools/zxcpc_keys.py zx            the generated cores/zx/trx/zx_keys.v on stdout
  tools/zxcpc_keys.py cpc           the generated cores/cpc/trx/cpc_keys.v on stdout (T92 D8)
  tools/zxcpc_keys.py cpc-vectors   the same kind of vectors for the CPC: "<keys hex> <expected 80 bit matrix hex>"
  tools/zxcpc_keys.py zx-vectors    test vectors for cores/zx/sim: "<keys hex> <expected matrix hex>" per line, computed by NAME
                                    (translate_zx below), not from the generated assigns
  tools/zxcpc_keys.py --check       cores/zx/trx/zx_keys.v and cores/cpc/trx/cpc_keys.v are what this file generates

The table is this project's (T91 D8: "the full 64 x 2 table is written with the core"); the machine's half-row layout is a hardware
fact. Choices that the spec leaves open are marked CHOICE and listed in the report to Alex.
"""
import os
import sys

C64 = [  # per column, rows 0..7
    ["DEL", "RETURN", "CRSR_R", "F7", "F1", "F3", "F5", "CRSR_D"],
    ["3", "W", "A", "4", "Z", "S", "E", "LSHIFT"],
    ["5", "R", "D", "6", "C", "F", "T", "X"],
    ["7", "Y", "G", "8", "B", "H", "U", "V"],
    ["9", "I", "J", "0", "M", "K", "O", "N"],
    ["+", "P", "L", "-", ".", ":", "@", ","],
    ["POUND", "*", ";", "HOME", "RSHIFT", "=", "UARROW", "/"],
    ["1", "LARROW", "CTRL", "2", "SPACE", "C=", "Q", "STOP"],
]
ZX = [  # per half-row, bits 0..4
    ["CAPS", "Z", "X", "C", "V"],
    ["A", "S", "D", "F", "G"],
    ["Q", "W", "E", "R", "T"],
    ["1", "2", "3", "4", "5"],
    ["0", "9", "8", "7", "6"],
    ["P", "O", "I", "U", "Y"],
    ["ENTER", "L", "K", "J", "H"],
    ["SPACE", "SYM", "M", "N", "B"],
]
CPC = [  # per keyboard line, bits 0..7 (CPC Wiki "Keyboard scanning"; line 9 is joystick 0, line 6 also carries joystick 1)
    ["CUP", "CRIGHT", "CDOWN", "F9", "F6", "F3", "ENTER", "FDOT"],
    ["CLEFT", "COPY", "F7", "F8", "F5", "F1", "F2", "F0"],
    ["CLR", "LBRACK", "RETURN", "RBRACK", "F4", "SHIFT", "BSLASH", "CTRL"],
    ["CARET", "MINUS", "AT", "P", "SEMI", "COLON", "SLASH", "DOT"],
    ["0", "9", "O", "I", "L", "K", "M", "COMMA"],
    ["8", "7", "U", "Y", "H", "J", "N", "SPACE"],
    ["6", "5", "R", "T", "G", "F", "B", "V"],
    ["4", "3", "E", "W", "S", "D", "C", "X"],
    ["1", "2", "ESC", "Q", "TAB", "A", "CAPSLOCK", "Z"],
    ["JU", "JD", "JL", "JR", "JF1", "JF2", "UNUSED", "DEL"],
]
SHIFTS = ("LSHIFT", "RSHIFT")


def pos64(name):
    for c, col in enumerate(C64):
        if name in col:
            return c * 8 + col.index(name)
    raise KeyError(name)


def posz(name):
    for r, row in enumerate(ZX):
        if name in row:
            return r * 5 + row.index(name)
    raise KeyError(name)


def posc(name):
    for r, row in enumerate(CPC):
        if name in row:
            return r * 8 + row.index(name)
    raise KeyError(name)


def _cpc_table():
    """C64 key name -> (set without SHIFT, set with SHIFT) of CPC key names"""
    t = {}
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        t[ch] = ({ch}, {"SHIFT", ch})
    for d in "0123456789":
        t[d] = ({d}, {"SHIFT", d})                  # the CPC's shifted digits are the legends of the standard layout, as the C64's
    t[","] = ({"COMMA"}, {"SHIFT", "COMMA"})
    t["."] = ({"DOT"}, {"SHIFT", "DOT"})
    t["/"] = ({"SLASH"}, {"SHIFT", "SLASH"})
    t[";"] = ({"SEMI"}, {"RBRACK"})                 # CHOICE: shifted ; ]
    t[":"] = ({"COLON"}, {"LBRACK"})                # CHOICE: shifted : [
    t["@"] = ({"AT"}, {"SHIFT", "AT"})              # CHOICE
    t["-"] = ({"MINUS"}, {"SHIFT", "MINUS"})
    t["+"] = ({"SHIFT", "SEMI"}, {"SHIFT", "SEMI"})  # the CPC's + is SHIFT+;
    t["*"] = ({"SHIFT", "COLON"}, {"SHIFT", "COLON"})  # the CPC's * is SHIFT+:
    t["="] = ({"SHIFT", "MINUS"}, {"SHIFT", "MINUS"})  # the CPC's = is SHIFT+-
    t["POUND"] = ({"BSLASH"}, {"BSLASH"})           # CHOICE: the CPC has no pound key; the backslash
    t["UARROW"] = ({"CARET"}, {"CARET"})
    t["RETURN"] = ({"RETURN"}, {"RETURN"})
    t["SPACE"] = ({"SPACE"}, {"SPACE"})
    t["DEL"] = ({"DEL"}, {"DEL"})
    t["HOME"] = ({"CLR"}, {"CLR"})
    t["STOP"] = ({"ESC"}, {"ESC"})
    t["CTRL"] = ({"CTRL"}, {"CTRL"})
    t["LARROW"] = ({"ENTER"}, {"ENTER"})            # the keypad ENTER (T92 D8: to be confirmed at the board)
    t["C="] = ({"COPY"}, {"COPY"})
    t["CRSR_D"] = ({"CDOWN"}, {"CUP"})
    t["CRSR_R"] = ({"CRIGHT"}, {"CLEFT"})
    t["F1"] = ({"F1"}, {"F2"})
    t["F3"] = ({"F3"}, {"F4"})
    t["F5"] = ({"F5"}, {"F6"})
    t["F7"] = ({"F7"}, {"F8"})
    return t


def _zx_table():
    """C64 key name -> (set without SHIFT, set with SHIFT) of ZX key names"""
    t = {}
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        t[ch] = ({ch}, {"CAPS", ch})
    # digits: plain; with SHIFT the symbol printed above the digit on the C64 key, as the Spectrum's SYMBOL SHIFT combination
    sym_digit = {"1": "1", "2": "P", "3": "3", "4": "4", "5": "5", "6": "6", "7": "7", "8": "8", "9": "9", "0": "0"}   # CHOICE: 2 -> '"' (SYM+P), 0 -> '_' (SYM+0)
    for d, k in sym_digit.items():
        t[d] = ({d}, {"SYM", k})
    # punctuation: the symbol on the C64 key cap (T91 D8), SHIFT gives the shifted symbol where the Spectrum has it
    t[","] = ({"SYM", "N"}, {"SYM", "R"})        # , <
    t["."] = ({"SYM", "M"}, {"SYM", "T"})        # . >
    t[";"] = ({"SYM", "O"}, {"SYM", "U"})        # ; ]   CHOICE
    t[":"] = ({"SYM", "Z"}, {"SYM", "Y"})        # : [   CHOICE
    t["="] = ({"SYM", "L"}, {"SYM", "L"})
    t["+"] = ({"SYM", "K"}, {"SYM", "K"})
    t["-"] = ({"SYM", "J"}, {"SYM", "0"})        # - _   CHOICE
    t["*"] = ({"SYM", "B"}, {"SYM", "B"})
    t["/"] = ({"SYM", "V"}, {"SYM", "C"})        # / ?
    t["@"] = ({"SYM", "2"}, {"SYM", "2"})        # CHOICE
    t["POUND"] = ({"SYM", "X"}, {"SYM", "X"})    # CHOICE: the pound sign
    t["UARROW"] = ({"SYM", "H"}, {"SYM", "H"})   # CHOICE: ^
    t["RETURN"] = ({"ENTER"}, {"ENTER"})
    t["SPACE"] = ({"SPACE"}, {"SPACE"})
    t["STOP"] = ({"CAPS", "SPACE"}, {"CAPS", "SPACE"})   # BREAK
    t["DEL"] = ({"CAPS", "0"}, {"CAPS", "0"})
    t["CRSR_D"] = ({"CAPS", "6"}, {"CAPS", "7"})         # down / up
    t["CRSR_R"] = ({"CAPS", "8"}, {"CAPS", "5"})         # right / left
    t["C="] = ({"SYM"}, {"SYM"})                         # SYMBOL SHIFT
    # 128K keys the 128K ROM understands (T91 D8): CHOICE of the C64 keys that carry them
    t["LARROW"] = ({"CAPS", "1"}, {"CAPS", "1"})         # EDIT
    t["HOME"] = ({"CAPS", "2"}, {"CAPS", "2"})           # CAPS LOCK
    t["F1"] = ({"CAPS", "3"}, {"CAPS", "3"})             # TRUE VIDEO
    t["F3"] = ({"CAPS", "4"}, {"CAPS", "4"})             # INV VIDEO
    t["CTRL"] = ({"CAPS", "9"}, {"CAPS", "9"})           # GRAPH
    t["F5"] = (set(), set())
    t["F7"] = (set(), set())
    return t


TAB = _zx_table()
CTAB = _cpc_table()


def translate_zx(down):
    """set of C64 key names down -> set of ZX key names down"""
    shift = any(s in down for s in SHIFTS)
    others = [k for k in down if k not in SHIFTS]
    out = set()
    for k in others:
        out |= TAB[k][1 if shift else 0]
    if shift and not others:
        out.add("CAPS")
    return out


def translate_cpc(down):
    """set of C64 key names down -> set of CPC key names down"""
    shift = any(k in down for k in SHIFTS)
    others = [k for k in down if k not in SHIFTS]
    out = set()
    for k in others:
        out |= CTAB[k][1 if shift else 0]
    if shift and not others:
        out.add("SHIFT")
    return out


def all_c64():
    return [n for col in C64 for n in col]


def gen_zx():
    w = 40
    t0 = 0
    t1 = 0
    for c64 in all_c64():
        if c64 in SHIFTS:
            continue
        i = pos64(c64)
        a, b = TAB[c64]
        t0 |= sum(1 << posz(k) for k in a) << (i * w)
        t1 |= sum(1 << posz(k) for k in b) << (i * w)
    sm = sum(1 << pos64(s) for s in SHIFTS)
    lines = [
        "// T91 D8: the host's C64 key matrix (keys[col * 8 + row], 1 = down) into the ZX Spectrum's 8 half-rows of 5 keys",
        "// (zx_mat[row * 5 + bit], 1 = down). Generated by tools/zxcpc_keys.py from the two keyboards' key names; do not edit by hand.",
        "// SHIFT (either) selects the table; alone it is CAPS SHIFT. The tables are in the parameters of the shared engine zxcpc_keyeng.",
        "`timescale 1ns / 1ps",
        "module zx_keys (",
        "    input  wire [63:0] keys,",
        "    output wire [39:0] zx_mat",
        ");",
        "    wire shift = keys[%d] | keys[%d];" % (pos64("LSHIFT"), pos64("RSHIFT")),
        "    wire [39:0] m;",
        "    wire other;",
        "    zxcpc_keyeng #(.W(40),",
        "        .T0(2560'h%x)," % t0,
        "        .T1(2560'h%x)," % t1,
        "        .SHIFTMASK(64'h%x)) eng (.keys(keys), .shift(shift), .out(m), .other(other));" % sm,
        "    assign zx_mat = m | {{39{1'b0}}, shift & ~other};   // SHIFT alone is CAPS SHIFT (matrix index 0)",
        "endmodule",
    ]
    return "\n".join(lines) + "\n"


def gen_cpc():
    w = 80
    t0 = 0
    t1 = 0
    for c64 in all_c64():
        if c64 in SHIFTS:
            continue
        i = pos64(c64)
        a, b = CTAB[c64]
        t0 |= sum(1 << posc(k) for k in a) << (i * w)
        t1 |= sum(1 << posc(k) for k in b) << (i * w)
    sm = sum(1 << pos64(s) for s in SHIFTS)
    lines = [
        "// T92 D8: the host's C64 key matrix (keys[col * 8 + row], 1 = down) into the CPC's 10 keyboard lines of 8 keys",
        "// (cpc_mat[line * 8 + bit], 1 = down). Generated by tools/zxcpc_keys.py from the two keyboards' key names; do not edit by hand.",
        "// SHIFT (either) selects the table; alone it is the CPC's SHIFT. The tables are in the parameters of the shared engine zxcpc_keyeng.",
        "`timescale 1ns / 1ps",
        "module cpc_keys (",
        "    input  wire [63:0] keys,",
        "    output wire [79:0] cpc_mat",
        ");",
        "    wire shift = keys[%d] | keys[%d];" % (pos64("LSHIFT"), pos64("RSHIFT")),
        "    wire [79:0] m;",
        "    wire other;",
        "    zxcpc_keyeng #(.W(80),",
        "        .T0(5120'h%x)," % t0,
        "        .T1(5120'h%x)," % t1,
        "        .SHIFTMASK(64'h%x)) eng (.keys(keys), .shift(shift), .out(m), .other(other));" % sm,
        "    assign cpc_mat = m | ((shift & ~other) ? (80'd1 << %d) : 80'd0);   // SHIFT alone is the CPC's SHIFT" % posc("SHIFT"),
        "endmodule",
    ]
    return "\n".join(lines) + "\n"


def vectors_cpc():
    names = [n for n in all_c64()]
    import random
    rnd = random.Random(92)
    sets = [set()]
    for n in names:
        sets.append({n})
        sets.append({n, "LSHIFT"})
        sets.append({n, "RSHIFT"})
    for _ in range(300):
        k = rnd.randint(2, 5)
        sets.append(set(rnd.sample(names, k)))
    for s in sets:
        keys = sum(1 << pos64(n) for n in s)
        mat = sum(1 << posc(n) for n in translate_cpc(s))
        print("%016x %020x" % (keys, mat))


def vectors():
    names = [n for n in all_c64()]
    import random
    rnd = random.Random(91)
    sets = [set()]
    for n in names:                                    # every key alone, and with each SHIFT
        sets.append({n})
        sets.append({n, "LSHIFT"})
        sets.append({n, "RSHIFT"})
    for _ in range(300):                               # random chords
        k = rnd.randint(2, 5)
        sets.append(set(rnd.sample(names, k)))
    for s in sets:
        keys = sum(1 << pos64(n) for n in s)
        mat = sum(1 << posz(n) for n in translate_zx(s))
        print("%016x %010x" % (keys, mat))


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if argv[1:] == ["zx"]:
        sys.stdout.write(gen_zx())
    elif argv[1:] == ["cpc"]:
        sys.stdout.write(gen_cpc())
    elif argv[1:] == ["cpc-vectors"]:
        vectors_cpc()
    elif argv[1:] == ["zx-vectors"]:
        vectors()
    elif argv[1:] == ["--check"]:
        bad = 0
        for rel, gen in (("cores/zx/trx/zx_keys.v", gen_zx), ("cores/cpc/trx/cpc_keys.v", gen_cpc)):
            p = os.path.join(root, rel)
            if os.path.exists(p) and open(p).read() != gen():
                print("zxcpc_keys: %s is not what tools/zxcpc_keys.py generates" % rel)
                bad = 1
            elif not os.path.exists(p) and rel.startswith("cores/zx"):
                print("zxcpc_keys: %s is missing" % rel)
                bad = 1
        if not bad:
            print("zxcpc_keys: the generated key modules match")
        return bad
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
