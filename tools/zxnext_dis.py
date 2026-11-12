#!/usr/bin/env python3
"""T116: a small Z80 / Z80N disassembler for the bench programs and for reading what the Next's boot ROM does (cores/zxnext/rtl/rom/bootrom.vhd is
the source of the bytes, read by the VHDL text, nothing is run). IX / IY forms are shown as DD / FD prefixes with the same operands.

  tools/zxnext_dis.py file.bin [origin] [start] [end]       (addresses hex; default: the whole file from 0)
  tools/zxnext_dis.py --bootrom [start] [end]               the 8 KB boot ROM out of rtl/rom/bootrom.vhd
"""
import os
import re
import sys

R8 = ["B", "C", "D", "E", "H", "L", "(HL)", "A"]
R16 = ["BC", "DE", "HL", "SP"]
R16AF = ["BC", "DE", "HL", "AF"]
CC = ["NZ", "Z", "NC", "C", "PO", "PE", "P", "M"]
ALU = ["ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "]
ROT = ["RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"]


def dis(m, pc):
    """-> (text, length) for the instruction at m[pc]"""
    def b(i):
        return m[(pc + i) & 0xFFFF]

    def w(i):
        return b(i) | (b(i + 1) << 8)
    op = b(0)
    if op == 0xCB:
        o = b(1)
        r = R8[o & 7]
        if o < 0x40:
            return "%s %s" % (ROT[o >> 3], r), 2
        return "%s %d,%s" % (["BIT", "RES", "SET"][(o >> 6) - 1], (o >> 3) & 7, r), 2
    if op == 0xED:
        o = b(1)
        z = {0x44: "NEG", 0x45: "RETN", 0x4D: "RETI", 0x46: "IM 0", 0x56: "IM 1", 0x5E: "IM 2", 0x47: "LD I,A", 0x4F: "LD R,A", 0x57: "LD A,I", 0x5F: "LD A,R",
             0x67: "RRD", 0x6F: "RLD", 0xA0: "LDI", 0xA1: "CPI", 0xA2: "INI", 0xA3: "OUTI", 0xA8: "LDD", 0xA9: "CPD", 0xAA: "IND", 0xAB: "OUTD", 0xB0: "LDIR",
             0xB1: "CPIR", 0xB2: "INIR", 0xB3: "OTIR", 0xB8: "LDDR", 0xB9: "CPDR", 0xBA: "INDR", 0xBB: "OTDR", 0x23: "SWAPNIB", 0x24: "MIRROR A",
             0x28: "BSLA DE,B", 0x29: "BSRA DE,B", 0x2A: "BSRL DE,B", 0x2B: "BSRF DE,B", 0x2C: "BRLC DE,B", 0x30: "MUL D,E", 0x31: "ADD HL,A",
             0x32: "ADD DE,A", 0x33: "ADD BC,A", 0x90: "OUTINB", 0x93: "PIXELDN", 0x94: "PIXELAD", 0x95: "SETAE", 0x98: "JP (C)", 0xA4: "LDIX",
             0xA5: "LDWS", 0xAC: "LDDX", 0xB4: "LDIRX", 0xB7: "LDPIRX", 0xBC: "LDDRX"}
        if o in z:
            return z[o], 2
        if o == 0x27:
            return "TEST 0x%02X" % b(2), 3
        if o in (0x34, 0x35, 0x36):
            return "ADD %s,0x%04X" % ({0x34: "HL", 0x35: "DE", 0x36: "BC"}[o], w(2)), 4
        if o == 0x8A:
            return "PUSH 0x%04X" % ((b(2) << 8) | b(3)), 4
        if o == 0x91:
            return "NEXTREG 0x%02X,0x%02X" % (b(2), b(3)), 4
        if o == 0x92:
            return "NEXTREG 0x%02X,A" % b(2), 3
        if (o & 0xC7) == 0x40:
            return "IN %s,(C)" % R8[(o >> 3) & 7], 2
        if (o & 0xC7) == 0x41:
            return "OUT (C),%s" % R8[(o >> 3) & 7], 2
        if (o & 0xCF) == 0x42:
            return "SBC HL,%s" % R16[(o >> 4) & 3], 2
        if (o & 0xCF) == 0x4A:
            return "ADC HL,%s" % R16[(o >> 4) & 3], 2
        if (o & 0xCF) == 0x43:
            return "LD (0x%04X),%s" % (w(2), R16[(o >> 4) & 3]), 4
        if (o & 0xCF) == 0x4B:
            return "LD %s,(0x%04X)" % (R16[(o >> 4) & 3], w(2)), 4
        return "DB 0xED,0x%02X" % o, 2
    if op in (0xDD, 0xFD):
        ix = "IX" if op == 0xDD else "IY"
        if b(1) == 0xCB:                                       # DD CB d op
            d = b(2) - 256 if b(2) > 127 else b(2)
            o = b(3)
            tgt = "(%s%+d)" % (ix, d)
            if o < 0x40:
                return "%s %s" % (ROT[o >> 3], tgt), 4
            return "%s %d,%s" % (["BIT", "RES", "SET"][(o >> 6) - 1], (o >> 3) & 7, tgt), 4
        if b(1) in (0xDD, 0xFD, 0xED):
            return "DB 0x%02X" % op, 1
        t, n = dis(m, pc + 1)
        if "(HL)" in t and not t.startswith("JP"):
            d = b(2) - 256 if b(2) > 127 else b(2)
            # the displacement sits between the opcode and the immediate: decode again with it taken out
            class V:
                def __getitem__(s, a):
                    a &= 0xFFFF
                    if a == (pc + 2) & 0xFFFF:
                        return m[(pc + 3) & 0xFFFF]
                    return m[(a + 1) & 0xFFFF] if a > (pc + 2) & 0xFFFF else m[a]
            t, n = dis(V(), pc + 1)
            return t.replace("(HL)", "(%s%+d)" % (ix, d)), n + 2
        return t.replace("HL", ix), n + 1
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    if x == 1:
        return ("HALT", 1) if op == 0x76 else ("LD %s,%s" % (R8[y], R8[z]), 1)
    if x == 2:
        return "%s%s" % (ALU[y], R8[z]), 1
    if x == 0:
        if z == 0:
            if y == 0:
                return "NOP", 1
            if y == 1:
                return "EX AF,AF'", 1
            t = (pc + 2 + (b(1) - 256 if b(1) > 127 else b(1))) & 0xFFFF
            if y == 2:
                return "DJNZ 0x%04X" % t, 2
            if y == 3:
                return "JR 0x%04X" % t, 2
            return "JR %s,0x%04X" % (CC[y - 4], t), 2
        if z == 1:
            return ("LD %s,0x%04X" % (R16[y >> 1], w(1)), 3) if not y & 1 else ("ADD HL,%s" % R16[y >> 1], 1)
        if z == 2:
            t = {0: "LD (BC),A", 1: "LD A,(BC)", 2: "LD (DE),A", 3: "LD A,(DE)"}
            if y < 4:
                return t[y], 1
            return {4: "LD (0x%04X),HL", 5: "LD HL,(0x%04X)", 6: "LD (0x%04X),A", 7: "LD A,(0x%04X)"}[y] % w(1), 3
        if z == 3:
            return ("INC " if not y & 1 else "DEC ") + R16[y >> 1], 1
        if z == 4:
            return "INC " + R8[y], 1
        if z == 5:
            return "DEC " + R8[y], 1
        if z == 6:
            return "LD %s,0x%02X" % (R8[y], b(1)), 2
        return ["RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"][y], 1
    # x == 3
    if z == 0:
        return "RET " + CC[y], 1
    if z == 1:
        if not y & 1:
            return "POP " + R16AF[y >> 1], 1
        return ["RET", "EXX", "JP (HL)", "LD SP,HL"][y >> 1], 1
    if z == 2:
        return "JP %s,0x%04X" % (CC[y], w(1)), 3
    if z == 3:
        if y == 0:
            return "JP 0x%04X" % w(1), 3
        if y == 2:
            return "OUT (0x%02X),A" % b(1), 2
        if y == 3:
            return "IN A,(0x%02X)" % b(1), 2
        return {4: "EX (SP),HL", 5: "EX DE,HL", 6: "DI", 7: "EI"}.get(y, "?"), 1
    if z == 4:
        return "CALL %s,0x%04X" % (CC[y], w(1)), 3
    if z == 5:
        if not y & 1:
            return "PUSH " + R16AF[y >> 1], 1
        return ("CALL 0x%04X" % w(1), 3) if y == 1 else ("?", 1)
    if z == 6:
        return "%s0x%02X" % (ALU[y], b(1)), 2
    return "RST 0x%02X" % (y * 8), 1


def bootrom_bytes():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    t = open(os.path.join(root, "cores/zxnext/rtl/rom/bootrom.vhd")).read()
    return bytes(int(x, 16) for x in re.findall(r'x"([0-9A-Fa-f]{2})"', t))


def listing(data, org, start, end):
    m = {org + i: v for i, v in enumerate(data)}
    mem = type("M", (), {"__getitem__": lambda s, a: m.get(a & 0xFFFF, 0)})()
    pc = start
    out = []
    while pc < end:
        t, n = dis(mem, pc)
        out.append("%04X  %-12s %s" % (pc, " ".join("%02X" % mem[pc + i] for i in range(n)), t))
        pc += n
    return "\n".join(out)


def main(argv):
    args = argv[1:]
    if args and args[0] == "--bootrom":
        data, org = bootrom_bytes(), 0
        args = args[1:]
    elif args:
        data, org = open(args[0], "rb").read(), int(args[1], 16) if len(args) > 1 else 0
        args = args[2:]
    else:
        print(__doc__)
        return 1
    start = int(args[0], 16) if args else org
    end = int(args[1], 16) if len(args) > 1 else org + len(data)
    print(listing(data, org, start, end))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
