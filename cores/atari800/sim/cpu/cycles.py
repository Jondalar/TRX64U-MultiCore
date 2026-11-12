#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""T88 §5: every 6502 opcode against the cycle table.

  cycles.py gen  OUT.hex OUT.json [--stable]   one program: a case per opcode and variant (page crossing, branch taken,
                                    taken across a page); each case is   setup; JMP T;  T: opcode ...;  stub
  cycles.py check TRACE.txt CASES.json
                                    from the bench's trace: cycles from the opcode fetch at T to the next
                                    opcode fetch, and that the next fetch is where the instruction should go.

The table is built from the instruction matrix (class of the operation x addressing mode), not typed per opcode.
The 12 KIL opcodes are left out; --stable also leaves out SHA/SHS/SHX/SHY, whose write address depends on RDY.
"""
import json
import sys

KIL = {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2}

# ---- the matrix: opcode -> (name, mode, class) ----------------------------------------------------------
# modes: imp acc imm zp zpx zpy abs abx aby izx izy rel ind ; class: R read, W write, M read-modify-write, X other
def matrix():
    t = {}
    c1 = ["ORA", "AND", "EOR", "ADC", "STA", "LDA", "CMP", "SBC"]
    m1 = ["izx", "zp", "imm", "abs", "izy", "zpx", "aby", "abx"]
    for a, n in enumerate(c1):
        for b, m in enumerate(m1):
            t[a * 32 + b * 4 + 1] = (n, m, "W" if n == "STA" else "R")
    t[0x89] = ("NOP", "imm", "R")
    c2 = ["ASL", "ROL", "LSR", "ROR", "STX", "LDX", "DEC", "INC"]
    for a, n in enumerate(c2):
        for b, m in ((1, "zp"), (3, "abs"), (5, "zpx"), (7, "abx")):
            cls = "W" if n == "STX" else "R" if n == "LDX" else "M"
            if n in ("STX", "LDX") and m == "zpx":
                m = "zpy"
            if n == "LDX" and m == "abx":
                m = "aby"
            if n == "STX" and m == "abx":
                continue  # 9E is SHX
            t[a * 32 + b * 4 + 2] = (n, m, cls)
        t[a * 32 + 0x0A] = (n, "acc", "X") if n in ("ASL", "ROL", "LSR", "ROR") else None
    t[0xA2] = ("LDX", "imm", "R")
    for op, n in ((0x8A, "TXA"), (0xAA, "TAX"), (0xCA, "DEX"), (0xEA, "NOP"), (0x9A, "TXS"), (0xBA, "TSX")):
        t[op] = (n, "imp", "X")
    for op in (0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA):
        t[op] = ("NOP", "imp", "X")
    for op in (0x82, 0xC2, 0xE2):
        t[op] = ("NOP", "imm", "R")
    t[0x9E] = ("SHX", "aby", "W")
    t[0x9C] = ("SHY", "abx", "W")
    # cc = 00
    for op, n, m, cls in (
        (0xA0, "LDY", "imm", "R"), (0xC0, "CPY", "imm", "R"), (0xE0, "CPX", "imm", "R"), (0x80, "NOP", "imm", "R"),
        (0x24, "BIT", "zp", "R"), (0x84, "STY", "zp", "W"), (0xA4, "LDY", "zp", "R"), (0xC4, "CPY", "zp", "R"),
        (0xE4, "CPX", "zp", "R"), (0x04, "NOP", "zp", "R"), (0x44, "NOP", "zp", "R"), (0x64, "NOP", "zp", "R"),
        (0x2C, "BIT", "abs", "R"), (0x8C, "STY", "abs", "W"), (0xAC, "LDY", "abs", "R"), (0xCC, "CPY", "abs", "R"),
        (0xEC, "CPX", "abs", "R"), (0x0C, "NOP", "abs", "R"),
        (0x94, "STY", "zpx", "W"), (0xB4, "LDY", "zpx", "R"), (0xBC, "LDY", "abx", "R"),
    ):
        t[op] = (n, m, cls)
    for op in (0x14, 0x34, 0x54, 0x74, 0xD4, 0xF4):
        t[op] = ("NOP", "zpx", "R")
    for op in (0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC):
        t[op] = ("NOP", "abx", "R")
    for op, n in ((0x08, "PHP"), (0x28, "PLP"), (0x48, "PHA"), (0x68, "PLA"), (0x88, "DEY"), (0xA8, "TAY"),
                  (0xC8, "INY"), (0xE8, "INX"), (0x18, "CLC"), (0x38, "SEC"), (0x58, "CLI"), (0x78, "SEI"),
                  (0x98, "TYA"), (0xB8, "CLV"), (0xD8, "CLD"), (0xF8, "SED")):
        t[op] = (n, "imp", "X")
    for op, n in ((0x10, "BPL"), (0x30, "BMI"), (0x50, "BVC"), (0x70, "BVS"), (0x90, "BCC"), (0xB0, "BCS"),
                  (0xD0, "BNE"), (0xF0, "BEQ")):
        t[op] = (n, "rel", "X")
    t[0x4C] = ("JMP", "abs", "X")
    t[0x6C] = ("JMP", "ind", "X")
    t[0x20] = ("JSR", "abs", "X")
    t[0x60] = ("RTS", "imp", "X")
    t[0x40] = ("RTI", "imp", "X")
    t[0x00] = ("BRK", "imp", "X")
    # cc = 11 (undocumented)
    c3 = ["SLO", "RLA", "SRE", "RRA", "SAX", "LAX", "DCP", "ISC"]
    m3 = ["izx", "zp", "imm", "abs", "izy", "zpx", "aby", "abx"]
    for a, n in enumerate(c3):
        for b, m in enumerate(m3):
            if m == "imm":
                continue
            cls = "W" if n == "SAX" else "R" if n == "LAX" else "M"
            mm = m
            if n in ("SAX", "LAX") and m == "zpx":
                mm = "zpy"
            if n in ("SAX", "LAX") and m == "abx":
                mm = "aby"
            if n == "SAX" and mm == "aby":
                continue  # 9F is SHA abs,y
            if n == "SAX" and m == "aby":
                continue
            t[a * 32 + b * 4 + 3] = (n, mm, cls)
    for op, n in ((0x0B, "ANC"), (0x2B, "ANC"), (0x4B, "ALR"), (0x6B, "ARR"), (0x8B, "XAA"), (0xAB, "LAX"),
                  (0xCB, "AXS"), (0xEB, "SBC")):
        t[op] = (n, "imm", "R")
    t[0x93] = ("SHA", "izy", "W")
    t[0x9F] = ("SHA", "aby", "W")
    t[0x9B] = ("TAS", "aby", "W")
    t[0xBB] = ("LAS", "aby", "R")
    return {k: v for k, v in t.items() if v}


BASE = {  # mode -> {class: cycles}, crossing adds 1 for R only
    "imm": {"R": 2},
    "zp": {"R": 3, "W": 3, "M": 5},
    "zpx": {"R": 4, "W": 4, "M": 6},
    "zpy": {"R": 4, "W": 4, "M": 6},
    "abs": {"R": 4, "W": 4, "M": 6},
    "abx": {"R": 4, "W": 5, "M": 7},
    "aby": {"R": 4, "W": 5, "M": 7},
    "izx": {"R": 6, "W": 6, "M": 8},
    "izy": {"R": 5, "W": 6, "M": 8},
}
SPECIAL = {"PHP": 3, "PHA": 3, "PLP": 4, "PLA": 4, "JMP/abs": 3, "JMP/ind": 5, "JSR": 6, "RTS": 6, "RTI": 6, "BRK": 7}


def expect(op, m):
    n, mode, cls = m[op]
    if n in SPECIAL:
        return SPECIAL[n]
    if mode in ("imp", "acc", "rel"):
        return SPECIAL.get(n, 2)
    if n == "JMP":
        return SPECIAL["JMP/" + mode]
    return BASE[mode][cls]


# ---- tiny assembler for the setup ----------------------------------------------------------------------
class Code:
    def __init__(self, at):
        self.at = at
        self.b = bytearray()

    def pc(self):
        return self.at + len(self.b)

    def ldai(self, v): self.b += bytes([0xA9, v & 255]); return self
    def ldxi(self, v): self.b += bytes([0xA2, v & 255]); return self
    def ldyi(self, v): self.b += bytes([0xA0, v & 255]); return self
    def pha(self): self.b += b"\x48"; return self
    def plp(self): self.b += b"\x28"; return self
    def txs(self): self.b += b"\x9A"; return self
    def sta(self, a): self.b += bytes([0x8D, a & 255, a >> 8]); return self
    def jmp(self, a): self.b += bytes([0x4C, a & 255, a >> 8]); return self
    def cld(self): self.b += b"\xD8"; return self
    def flags(self, p): return self.ldai(p).pha().plp()


# memory map of the program
S_BASE = 0x8000   # setup blocks, 48 bytes each
T_BASE = 0x2000   # instruction under test, 16 bytes each
PCROSS = 0x4000   # one page per taken-across-a-page branch
ZP_PTR = 0x20     # pointers to DATA at $20+2n
ZP_DAT = 0x40
DATA = 0x0380
BRKSTUB = 0x0F00  # BRK/IRQ/NMI vector target: JMP (NEXT)
NEXT = 0x0310     # where the BRK stub goes on
JMPIND = 0x0320


UNSTABLE = {0x93, 0x9B, 0x9C, 0x9E, 0x9F}   # SHA SHS SHY SHX: the write address depends on RDY (real NMOS behaviour)


def build(stable=False):
    m = matrix()
    mem = bytearray(65536)
    cases = []
    # variants
    var = []
    for op in sorted(m):
        if op in KIL or (stable and op in UNSTABLE):
            continue
        n, mode, cls = m[op]
        if mode in ("abx", "aby", "izy"):
            var.append((op, "near"))
            var.append((op, "cross"))
        elif mode == "rel":
            var.append((op, "nottaken"))
            var.append((op, "taken"))
            var.append((op, "taken_cross"))
        elif op == 0x6C:
            var.append((op, "plain"))
            var.append((op, "pagebug"))
        else:
            var.append((op, ""))
    # pointers and data
    for i in range(8):
        mem[ZP_PTR + 2 * i] = DATA & 255
        mem[ZP_PTR + 2 * i + 1] = DATA >> 8
    mem[0xFFFE], mem[0xFFFF] = BRKSTUB & 255, BRKSTUB >> 8
    mem[0xFFFA], mem[0xFFFB] = BRKSTUB & 255, BRKSTUB >> 8
    mem[BRKSTUB:BRKSTUB + 3] = bytes([0x6C, NEXT & 255, NEXT >> 8])
    pcross_page = PCROSS
    nxt_setup = lambda k: S_BASE + 48 * k
    for k, (op, v) in enumerate(var):
        n, mode, cls = m[op]
        exp = expect(op, m)
        S = Code(nxt_setup(k))
        S.cld().ldxi(0xFF).txs()
        # the place for the instruction and its stubs
        if v == "taken_cross":
            T = pcross_page + 0xFC
            pcross_page += 0x100
        else:
            T = T_BASE + 16 * k
        # default next stub address is the fall-through
        ops = bytearray([op])
        land = None   # where the next opcode fetch must be
        X = Y = 0
        P = 0x00
        extra = 0
        if mode == "imm":
            ops += b"\x00"
        elif mode == "zp":
            ops += bytes([ZP_DAT])
        elif mode == "zpx":
            ops += bytes([ZP_DAT]); X = 1
        elif mode == "zpy":
            ops += bytes([ZP_DAT]); Y = 1
        elif mode == "abs" and op not in (0x4C, 0x20):
            ops += bytes([DATA & 255, DATA >> 8])
        elif mode == "abx":
            ops += bytes([DATA & 255, DATA >> 8]); X = 0x01 if v == "near" else 0x90
        elif mode == "aby":
            ops += bytes([DATA & 255, DATA >> 8]); Y = 0x01 if v == "near" else 0x90
        elif mode == "izx":
            ops += bytes([ZP_PTR]); X = 2
        elif mode == "izy":
            ops += bytes([ZP_PTR]); Y = 0x01 if v == "near" else 0x90
        if mode in ("abx", "aby", "izy") and cls == "R" and v == "cross":
            exp += 1
        # control flow
        fall = T + len(ops) + (1 if mode == "rel" else 0)
        tgt = None
        if op == 0x4C or op == 0x20:
            tgt = T + 8
            ops += bytes([tgt & 255, tgt >> 8]); land = tgt
        elif op == 0x6C:
            tgt = T + 8
            if v == "plain":
                ops += bytes([JMPIND & 255, JMPIND >> 8])
                mem[JMPIND], mem[JMPIND + 1] = tgt & 255, tgt >> 8
            else:
                ops += bytes([0xFF, 0x02])
                mem[0x02FF], mem[0x0200] = tgt & 255, tgt >> 8
                mem[0x0300] = 0xEE    # what a CPU without the page bug would take as the high byte
            land = tgt
        elif mode == "rel":
            flag = {0x10: (0x80, 0), 0x30: (0x80, 1), 0x50: (0x40, 0), 0x70: (0x40, 1), 0x90: (0x01, 0),
                    0xB0: (0x01, 1), 0xD0: (0x02, 0), 0xF0: (0x02, 1)}[op]
            take_p = flag[0] if flag[1] else 0
            if v == "nottaken":
                P = 0x00 if flag[1] else flag[0]
                ops += b"\x03"; land = fall; tgt = fall + 3
            elif v == "taken":
                P = take_p
                ops += b"\x03"; tgt = fall + 3; land = tgt; exp = 3
            else:
                P = take_p
                ops += b"\x04"; tgt = fall + 4; land = tgt; exp = 4   # T = xxFC: fall = xxFE, target xx+1 02
            if v == "nottaken":
                exp = 2
        elif op == 0x60:   # RTS: return address - 1 on the stack
            land = T + 8
            r = land - 1
            S.ldai(r >> 8).pha().ldai(r & 255).pha()
        elif op == 0x40:   # RTI: P on top, then PCL, PCH
            land = T + 8
            S.ldai(land >> 8).pha().ldai(land & 255).pha().ldai(0x20).pha()
        elif op == 0x00:
            ops += b"\x00"   # BRK is followed by a padding byte
            land = BRKSTUB
            nx = nxt_setup(k + 1)
            S.ldai(nx & 255).sta(NEXT).ldai(nx >> 8).sta(NEXT + 1)
        else:
            land = fall
        # stubs after the instruction: where it lands (and the fall-through of a branch) jump to the next case
        nxt = nxt_setup(k + 1)
        stub = bytes([0x4C, nxt & 255, nxt >> 8])
        mem[T:T + len(ops)] = ops
        stubs = {land} if mode != "rel" else {fall, tgt}
        for x in stubs:
            if x != BRKSTUB:
                mem[x:x + 3] = stub
        # registers
        S.ldxi(X).ldyi(Y)
        if op not in (0x60, 0x40):
            S.flags(P)
        S.jmp(T)
        assert len(S.b) <= 48, (hex(op), len(S.b))
        mem[S.at:S.at + len(S.b)] = S.b
        cases.append({"op": op, "name": n, "mode": mode, "var": v, "T": T, "land": land, "cycles": exp})
    # end: the last setup block is a trap
    end = nxt_setup(len(var))
    mem[end:end + 3] = bytes([0x4C, end & 255, end >> 8])
    mem[0xFFFC], mem[0xFFFD] = nxt_setup(0) & 255, nxt_setup(0) >> 8
    assert stable or len(m) + len(KIL) == 256, len(m)
    return mem, cases, end


def gen(hexout, jsonout, stable=False):
    mem, cases, end = build(stable)
    open(hexout, "w").write("".join("%02X\n" % b for b in mem))
    json.dump({"cases": cases, "end": end}, open(jsonout, "w"))
    print("cases:", len(cases), "end trap", hex(end))


def check(trace, jsonin):
    d = json.load(open(jsonin))
    syncs = []   # (cyc, addr)
    for line in open(trace):
        f = line.split()
        if len(f) == 6 and f[4] == "1" and f[5] == "1":
            syncs.append((int(f[0]), int(f[1], 16)))
    pos = {}
    for i, (c, a) in enumerate(syncs):
        pos.setdefault(a, []).append(i)
    bad = 0
    seen = 0
    for cs in d["cases"]:
        T = cs["T"]
        if T not in pos:
            print("NOT RUN %02X %s %s %s" % (cs["op"], cs["name"], cs["mode"], cs["var"]))
            bad += 1
            continue
        i = pos[T][0]
        if i + 1 >= len(syncs):
            print("NO NEXT %02X" % cs["op"])
            bad += 1
            continue
        n = syncs[i + 1][0] - syncs[i][0]
        a = syncs[i + 1][1]
        seen += 1
        ok = n == cs["cycles"] and a == cs["land"]
        if not ok:
            bad += 1
            print("MISMATCH %02X %-4s %-3s %-12s cycles %d expected %d, next fetch %04X expected %04X" %
                  (cs["op"], cs["name"], cs["mode"], cs["var"], n, cs["cycles"], a, cs["land"]))
    print("cycles: %d cases run, %d bad" % (seen, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1] == "gen":
        gen(sys.argv[2], sys.argv[3], "--stable" in sys.argv)
    else:
        sys.exit(check(sys.argv[2], sys.argv[3]))
