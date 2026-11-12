#!/usr/bin/env python3
"""Random RV32IM programs (mul family, back-to-back, dependent ALU/branch/load-use,
wrong-path muls after taken branches) + a reference model. Usage:
  gen.py gen SEED N prog.hex model.hex    write the program and the expected data memory
  gen.py cmp model.hex dmem.hex trace.txt           compare (0 mismatches = exit 0)"""
import random, sys

M32 = 0xFFFFFFFF
def s32(x): x &= M32; return x - (1 << 32) if x & 0x80000000 else x

def r_(f7, rs2, rs1, f3, rd, op): return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op
def i_(imm, rs1, f3, rd, op): return ((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op
def s_(imm, rs2, rs1, f3): return (((imm >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | ((imm & 0x1F) << 7) | 0x23
def b_(imm, rs2, rs1, f3):
    return (((imm >> 12) & 1) << 31) | (((imm >> 5) & 0x3F) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (((imm >> 1) & 0xF) << 8) | (((imm >> 11) & 1) << 7) | 0x63
def u_(imm, rd, op): return (imm & 0xFFFFF000) | (rd << 7) | op
def jal(imm, rd):
    return (((imm >> 20) & 1) << 31) | (((imm >> 1) & 0x3FF) << 21) | (((imm >> 11) & 1) << 20) | (((imm >> 12) & 0xFF) << 12) | (rd << 7) | 0x6F

BASE = 0x2000            # x30, data window 0x2000..0x2FFF (words 0x800..0xBFF of the dmem array)
DONE = 0x30000000
NREG = 29                # x1..x29 are data registers
EDGE = [0, 1, M32, 0x80000000, 0x7FFFFFFF, 0xFFFF, 0x10000, 0xFFFF0000, 2, 0xFFFFFFFE]

class Model:
    def __init__(s): s.x = [0] * 32; s.mem = {}
    def w(s, rd, v):
        if rd: s.x[rd] = v & M32
    def mul(s, f3, a, b):
        sa, sb = s32(a), s32(b)
        if f3 == 0: return (sa * sb) & M32
        if f3 == 1: return ((sa * sb) >> 32) & M32
        if f3 == 2: return ((sa * b) >> 32) & M32
        return ((a * b) >> 32) & M32

def gen(seed, n):
    rnd = random.Random(seed)
    code = []      # list of (word, effect) ; effect(model) -> None, executed in program order
    m = Model()
    ops = []       # reference executes by interpreting the encoded program instead

    def emit(w): code.append(w & M32)
    # prologue: x30 = BASE, data registers random/edge
    emit(u_(BASE, 30, 0x37)); 
    for r in range(1, NREG + 1):
        v = rnd.choice(EDGE) if rnd.random() < 0.4 else rnd.getrandbits(32)
        emit(u_((v + 0x800) & 0xFFFFF000, r, 0x37)); emit(i_(v - ((v + 0x800) & 0xFFFFF000), r, 0, r, 0x13))
    recent = [1, 2, 3]
    def pick():
        return rnd.choice(recent) if rnd.random() < 0.6 else rnd.randint(1, NREG)
    def dst():
        d = 0 if rnd.random() < 0.04 else rnd.randint(1, NREG)
        if d: recent.append(d); del recent[:-4]
        return d
    def one(depth=0):
        k = rnd.random()
        if k < 0.40:
            a, b = pick(), pick(); emit(r_(1, b, a, rnd.randint(0, 3), dst(), 0x33))
        elif k < 0.55:
            f3, f7 = rnd.choice([(0, 0), (0, 0x20), (4, 0), (6, 0), (7, 0), (1, 0), (5, 0), (5, 0x20), (2, 0), (3, 0)])
            a, b = pick(), pick(); emit(r_(f7, b, a, f3, dst(), 0x33))
        elif k < 0.62:
            a = pick(); emit(i_(rnd.randint(-2048, 2047), a, 0, dst(), 0x13))
        elif k < 0.70:
            emit(s_(rnd.randint(0, 255) * 4, pick(), 30, 2))
        elif k < 0.80:
            d = dst(); emit(i_(rnd.randint(0, 255) * 4, 30, 2, d, 0x03))
            if rnd.random() < 0.7 and d:   # load-use into a multiply
                emit(r_(1, pick(), d, rnd.randint(0, 3), dst(), 0x33))
        elif k < 0.90 and depth == 0:
            # forward branch over 1-3 instructions (wrong path often multiplies)
            skip = rnd.randint(1, 3)
            f3 = rnd.choice([0, 1, 4, 5, 6, 7])
            if rnd.random() < 0.5:   # branch on a fresh product
                a = pick(); emit(r_(1, pick(), a, rnd.randint(0, 3), dst(), 0x33))
                ra = recent[-1]
            else: ra = pick()
            emit(b_(4 * (skip + 1), pick(), ra, f3))
            for _ in range(skip):
                a, b = pick(), pick(); emit(r_(1, b, a, rnd.randint(0, 3), dst(), 0x33))
        elif k < 0.93 and depth == 0:
            a, b = pick(), pick()
            emit(jal(8, 0)); emit(r_(1, b, a, 0, dst(), 0x33))
        else:
            a = pick(); d = dst(); emit(r_(1, a, a, rnd.randint(0, 3), d, 0x33))      # same operand twice
            if rnd.random() < 0.5 and d: emit(r_(1, d, d, rnd.randint(0, 3), dst(), 0x33))   # back to back, dependent
    for _ in range(n): one()
    # epilogue: dump x1..x29 to dmem 0x2800.., done marker, spin
    for r in range(1, NREG + 1): emit(s_(0x800 + 4 * r, r, 30, 2))
    emit(u_(DONE, 31, 0x37)); emit(s_(0, 0, 31, 2)); emit(jal(0, 0))
    return code

def run(code):
    """reference interpreter for the subset gen() emits"""
    x = [0] * 32; mem = {}; pc = 0; mm = Model(); steps = 0; log = []
    while True:
        w = code[pc // 4] if pc // 4 < len(code) else 0x13
        op = w & 0x7F; rd = (w >> 7) & 31; f3 = (w >> 12) & 7; rs1 = (w >> 15) & 31; rs2 = (w >> 20) & 31; f7 = w >> 25
        a, b = x[rs1], x[rs2]; npc = pc + 4; v = None
        def sx(v, bits): return v - (1 << bits) if v & (1 << (bits - 1)) else v
        if op == 0x37: v = w & 0xFFFFF000
        elif op == 0x33:
            if f7 == 1: v = mm.mul(f3, a, b)
            else:
                v = {0: (a - b if f7 == 0x20 else a + b), 4: a ^ b, 6: a | b, 7: a & b, 1: a << (b & 31),
                     5: (s32(a) >> (b & 31)) if f7 == 0x20 else a >> (b & 31),
                     2: int(s32(a) < s32(b)), 3: int(a < b)}[f3]
        elif op == 0x13: v = a + sx(w >> 20, 12)
        elif op == 0x03: v = mem.get((a + sx(w >> 20, 12)) & M32, 0)
        elif op == 0x23:
            imm = sx(((w >> 25) << 5) | ((w >> 7) & 31), 12); ad = (a + imm) & M32
            if ad == DONE: return mem, log
            mem[ad] = b; log.append((ad, b))
        elif op == 0x63:
            t = {0: a == b, 1: a != b, 4: s32(a) < s32(b), 5: s32(a) >= s32(b), 6: a < b, 7: a >= b}[f3]
            imm = sx((((w >> 31) & 1) << 12) | (((w >> 7) & 1) << 11) | (((w >> 25) & 0x3F) << 5) | (((w >> 8) & 0xF) << 1), 13)
            if t: npc = pc + imm
        elif op == 0x6F:
            imm = sx((((w >> 31) & 1) << 20) | (((w >> 12) & 0xFF) << 12) | (((w >> 20) & 1) << 11) | (((w >> 21) & 0x3FF) << 1), 21)
            v = pc + 4; npc = pc + imm
        else: raise SystemExit("model: opcode %02x at %x" % (op, pc))
        if v is not None and rd: x[rd] = v & M32
        pc = npc; steps += 1
        if steps > 1000000: raise SystemExit("model: runaway")

def main():
    if sys.argv[1] == "gen":
        seed, n, prog, model = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
        code = gen(seed, n)
        assert len(code) < 4096
        open(prog, "w").write("".join("%08x\n" % w for w in code))
        mem, log = run(code)
        open(model + '.trace', 'w').write(''.join('%08x %08x\n' % (a & ~3, v) for a, v in log))
        words = [0] * 4096
        for ad, v in mem.items(): words[(ad >> 2) & 0xFFF] = v
        open(model, "w").write("".join("%08x\n" % w for w in words))
    else:
        a = open(sys.argv[2]).read().lower().split(); b = open(sys.argv[3]).read().lower().split()
        logbad = 0
        ta = open(sys.argv[2] + '.trace').read().lower().split('\n'); tb = open(sys.argv[4]).read().lower().split('\n')
        if ta != tb:
            n = next((i for i, (x, y) in enumerate(zip(ta, tb)) if x != y), min(len(ta), len(tb)))
            print("store log differs at entry %d: model %s core %s" % (n, ta[n] if n < len(ta) else None, tb[n] if n < len(tb) else None))
            logbad = 1
        bad = [(i, x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y]
        for i, x, y in bad[:10]: print("MISMATCH word %03x: model %s core %s" % (i, x, y))
        print("mismatches: %d of %d words, store log %s" % (len(bad), len(a), "DIFFERS" if logbad else "equal"))
        sys.exit(1 if bad or logbad or len(a) != len(b) else 0)
main()
