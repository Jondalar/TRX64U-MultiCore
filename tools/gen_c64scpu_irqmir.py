#!/usr/bin/env python3
"""T241 S2: the synthetic SuperCPU ROM and the model of the "mirror everything at 1 MHz" bench (cores/c64scpu/sim/run_irqmir.sh, check_irqmir.py).

The ROM is our own code (no byte of a CMD or Commodore ROM). The program runs at software 1 MHz ($D07A) with the display on (badlines), a CIA 1 timer A IRQ with the PAL KERNAL's period
(latch $4025) through the emulation-mode vector ($01:FFFE, as run_irq.sh phase 3: hwenable off, KERNAL visible), a handler shaped like the KERNAL's, and a main loop heavy on stack and zero
page. Four phases, each between two markers (a write to $00:0B11: the phase id, 0 = end):
  1  A0  reset mirror mode ($0200-$FFFF), IRQs masked        2  A1  reset mirror mode, CIA 1 timer A IRQs on
  3  B0  mirror everything ($D07E, $D077, $D07F under SEI as on the board), IRQs masked      4  B1  mirror everything, IRQs on
Markers the bench prints (tb_irqmir.vh): $0B10 end of a pass, $0B11 phase, $0B12 handler entry, $0B13 handler done, $0B20-$0B23 results (jiffy byte, carry bytes, pass counter), $0B30 = $5A the end.

The model (class Model) is the C64's rule for the cycles of the main loop: the 65816's bus cycles of every instruction in emulation mode (R = read, I = internal, W = write), a cycle that is not a write
waits while BA was low at the PHI2 fall of the cycle (RDY, the 6510 / VICE rule at 1 MHz: scpu64cpu.c check_ba, steal at the first access; writes go on in emulation mode, scpu64mem.c:157-162),
one cycle per PHI2 cycle. The BA samples come from the bench's log (ba|...), not from the model. The instruction stream is run by a small interpreter over the real code bytes.

  gen_c64scpu_irqmir.py <out dir>   writes <out>/mem/scpu_rom.mem, <out>/mem/c64scpu_rom.mem, <out>/expect.json; prints nothing the bench needs (the plusargs are in run_irqmir.sh)
"""
import json
import os
import sys

ORG = 0x1000
SIZE = 0x20000
N_MASKED = 1500          # passes of a masked phase
N_IRQ = 5000             # passes of an IRQ phase (about 40 IRQs of 16,422 cycles)
LATCH = 0x4025           # the PAL KERNAL's CIA 1 timer A latch: period 16,422 PHI2 cycles
# zero page
Z_A0, Z_A1, Z_A2 = 0xA0, 0xA1, 0xA2          # the jiffy clock, UDTIM's three bytes
Z_V, Z_C1, Z_C2, Z_C3 = 0xF0, 0xF1, 0xF2, 0xF3
Z_PL, Z_PH, Z_LL, Z_LH = 0xE4, 0xE5, 0xE8, 0xE9   # pass counter, limit
PROG = os.environ.get("IRQMIR_PROG", "simple")     # simple: PHA/PLA/JSR/INC mix; kernal: the shape of the KERNAL's IRQ and of BASIC's arithmetic (RMW zero page, (zp),Y, zp,X, CIA and colour RAM accesses)


class Asm:
    def __init__(self, org):
        self.org, self.b, self.lab, self.fix = org, bytearray(), {}, []

    def pc(self):
        return self.org + len(self.b)

    def label(self, n):
        self.lab[n] = self.pc()

    def e(self, *x):
        self.b += bytes(x)

    def lda_i(self, v): self.e(0xA9, v)
    def lda_z(self, a): self.e(0xA5, a)
    def sta_z(self, a): self.e(0x85, a)
    def lda_a(self, a): self.e(0xAD, a & 255, a >> 8)
    def sta_a(self, a): self.e(0x8D, a & 255, a >> 8)
    def inc_z(self, a): self.e(0xE6, a)
    def jsr(self, n): self.e(0x20, 0, 0); self.fix.append((len(self.b) - 2, n, "abs"))
    def jmp(self, n): self.e(0x4C, 0, 0); self.fix.append((len(self.b) - 2, n, "abs"))

    def br(self, op, n):
        self.e(op, 0)
        self.fix.append((len(self.b) - 1, n, "rel"))

    def done(self):
        for at, n, k in self.fix:
            if k == "rel":
                d = self.lab[n] - (self.org + at + 1)
                assert -128 <= d <= 127, (n, d)
                self.b[at] = d & 255
            else:
                self.b[at], self.b[at + 1] = self.lab[n] & 255, self.lab[n] >> 8
        return bytes(self.b)


def phase(t, pid, n, irq):
    """one phase: the setup, the marker, the main loop (inline), the epilogue, the end marker"""
    p = "p%d" % pid
    t.lda_i(n & 255); t.sta_z(Z_LL); t.lda_i(n >> 8); t.sta_z(Z_LH)
    t.lda_i(0); t.sta_z(Z_PL); t.sta_z(Z_PH); t.sta_z(Z_A0); t.sta_z(Z_A1); t.sta_z(Z_A2)
    if irq:
        t.lda_i(0x7F); t.sta_a(0xDC0D); t.lda_a(0xDC0D)      # no CIA 1 source, flags cleared
    t.lda_i(pid); t.sta_a(0x0B11)                            # the phase marker
    if irq:
        t.lda_i(LATCH & 255); t.sta_a(0xDC04); t.lda_i(LATCH >> 8); t.sta_a(0xDC05)
        t.lda_i(0x81); t.sta_a(0xDC0D)                       # timer A IRQ on
        t.lda_i(0x11); t.sta_a(0xDC0E)                       # force load, start, continuous
        t.e(0x58)                                            # CLI
    t.label(p + "L")                                         # ---- one pass
    if PROG == "simple":
        t.jsr("s1")
        t.lda_z(Z_V); t.e(0x18); t.e(0x69, 3); t.sta_z(Z_V)      # LDA CLC ADC #3 STA
        t.e(0x48); t.lda_z(Z_V); t.e(0x48); t.e(0x68); t.e(0x68)  # PHA LDA PHA PLA PLA
        t.inc_z(Z_C1); t.inc_z(Z_C2); t.e(0xA6, Z_C1); t.e(0x86, Z_C3)   # INC INC LDX STX
        t.jsr("s2")
    else:
        t.jsr("k1")
        t.lda_a(0xDC01); t.sta_a(0xD020); t.sta_a(0xD800)        # a CIA read, a VIC register write, a colour RAM write: synchronous I/O cycles between the mirrored ones
        t.jsr("k2")
    t.sta_a(0x0B10)                                          # the pass marker (a bank-0 store to $0Bxx)
    t.inc_z(Z_PL); t.br(0xD0, p + "c"); t.inc_z(Z_PH)
    t.label(p + "c")
    t.lda_z(Z_PL); t.e(0xC5, Z_LL); t.br(0xD0, p + "j")
    t.lda_z(Z_PH); t.e(0xC5, Z_LH); t.br(0xF0, p + "x")
    t.label(p + "j"); t.jmp(p + "L")
    t.label(p + "x")
    if irq:
        t.e(0x78)                                            # SEI
        t.lda_i(0); t.sta_a(0xDC0E); t.lda_i(0x7F); t.sta_a(0xDC0D); t.lda_a(0xDC0D)
    t.lda_z(Z_A2); t.sta_a(0x0B20); t.lda_z(Z_A1); t.sta_a(0x0B21); t.lda_z(Z_PL); t.sta_a(0x0B22); t.lda_z(Z_PH); t.sta_a(0x0B23)
    t.lda_i(0); t.sta_a(0x0B11)                              # the end marker


def tail():
    t = Asm(ORG)
    t.e(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                        # SEI CLD LDX #$FF TXS
    t.lda_i(0x1B); t.sta_a(0xD011)                           # display on (text, 25 rows): badlines
    t.lda_i(0x36); t.sta_z(0x01)                             # CPU port: KERNAL visible (class KT without hwenable)
    t.lda_i(0)
    for a in (Z_A0, Z_A1, Z_A2, Z_V, Z_C1, Z_C2, Z_C3):
        t.sta_z(a)
    for z, v in ((0x6E, 0), (0x6F, 3), (0x6C, 0x10), (0x6D, 3), (0xD1, 0), (0xD2, 4), (0xD5, 0), (0xD6, 0xD8)):
        t.lda_i(v); t.sta_z(z)
    t.lda_i(0)
    t.fix.append((len(t.b) + 1, "hirq", "lo")); t.lda_i(0); t.e(0x8F, 0xFE, 0xFF, 0x01)    # LDA #<hirq STA $01:FFFE
    t.fix.append((len(t.b) + 1, "hirq", "hi")); t.lda_i(0); t.e(0x8F, 0xFF, 0xFF, 0x01)    # LDA #>hirq STA $01:FFFF
    t.sta_a(0xD07E); t.sta_a(0xD0B6); t.sta_a(0xD07F)         # hwenable on, boot map off, hwenable off (run_irq.sh phase 3)
    t.sta_a(0xD07A)                                          # software 1 MHz
    phase(t, 1, N_MASKED, False)
    phase(t, 2, N_IRQ, True)
    t.e(0x78); t.sta_a(0xD07E); t.sta_a(0xD077); t.sta_a(0xD07F)   # SEI; STA $D07E; STA $D077; STA $D07F: mirror everything, as on the board
    phase(t, 3, N_MASKED, False)
    phase(t, 4, N_IRQ, True)
    t.lda_i(0x5A); t.sta_a(0x0B30)
    t.label("h"); t.br(0x80, "h")
    # the handler, shaped like the KERNAL's: PHA TXA PHA TYA PHA, the BRK test through the stacked P, the jiffy clock (UDTIM: INC with carries), the CIA acknowledge, the pulls, RTI
    t.label("s1")
    t.e(0x48, 0x8A, 0x48); t.lda_z(0xF2); t.sta_z(0xF3); t.inc_z(0xF2); t.e(0x68, 0xAA, 0x68, 0x60)
    t.label("s2")
    t.e(0x48, 0x98, 0x48); t.lda_z(0xF3); t.e(0x18, 0x69, 1); t.sta_z(0xF1); t.e(0x68, 0xA8, 0x68, 0x60)
    # kernal: BASIC-like arithmetic (read-modify-write in zero page, zp,X, (zp),Y) and the stack
    t.label("k1")
    t.lda_z(0x61); t.e(0x06, 0x63, 0x26, 0x62, 0x66, 0x61, 0x46, 0x64, 0xC6, 0x65)    # LDA $61 ASL $63 ROL $62 ROR $61 LSR $64 DEC $65
    t.e(0xA2, 3)                                                                       # LDX #3
    t.label("k1l"); t.e(0xB5, 0x61, 0x18, 0x65, 0x65, 0x95, 0x69, 0xCA); t.br(0x10, "k1l")   # LDA $61,X CLC ADC $65 STA $69,X DEX BPL
    t.e(0xA0, 2, 0xB1, 0x6E, 0x91, 0x6C)                                               # LDY #2 LDA ($6E),Y STA ($6C),Y
    t.e(0xBA, 0x48, 0x68, 0x60)                                                        # TSX PHA PLA RTS
    t.label("k2")
    t.inc_z(0xF1); t.inc_z(0xF2); t.e(0xA6, 0xF1, 0x86, 0xF3); t.e(0x48, 0x8A, 0x48, 0x98, 0x48, 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x60)   # INC INC LDX STX PHA TXA PHA TYA PHA PLA TAY PLA TAX PLA RTS
    t.label("ud")
    t.inc_z(Z_A2); t.br(0xD0, "udn"); t.inc_z(Z_A1); t.br(0xD0, "udn"); t.inc_z(Z_A0)
    t.label("udn"); t.e(0x60)
    t.label("hirq")
    t.e(0x48, 0x8A, 0x48, 0x98, 0x48)                        # PHA TXA PHA TYA PHA
    t.sta_a(0x0B12)                                          # handler entry marker
    t.e(0xBA, 0xBD, 0x04, 0x01, 0x29, 0x10)                  # TSX LDA $0104,X AND #$10
    t.br(0xD0, "hb")                                         # BNE (BRK): never taken here
    if PROG == "simple":
        t.inc_z(Z_A2); t.br(0xD0, "hn"); t.inc_z(Z_A1); t.br(0xD0, "hn"); t.inc_z(Z_A0)
        t.label("hn")
    else:
        t.jsr("ud")                                          # UDTIM
        t.lda_i(0x7F); t.sta_a(0xDC00); t.lda_a(0xDC01); t.e(0xC5, 0x91); t.sta_z(0x91)      # the STOP key: CIA write and read
        t.e(0x46, 0xCF, 0xA0, 0, 0xB1, 0xD1, 0x49, 0x80, 0x91, 0xD1)    # the cursor: LSR $CF LDY #0 LDA ($D1),Y EOR #$80 STA ($D1),Y (screen RAM)
        t.e(0xB1, 0xD5, 0x91, 0xD5)                          # LDA ($D5),Y STA ($D5),Y (colour RAM)
        t.lda_i(0); t.sta_a(0xDC00); t.e(0xAE, 0x01, 0xDC); t.lda_i(0xFF); t.sta_a(0xDC00)  # the keyboard scan: CIA writes and a read
    t.lda_a(0xDC0D)                                          # the CIA acknowledge
    t.sta_a(0x0B13)                                          # handler done marker
    t.e(0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40)                  # PLA TAY PLA TAX PLA RTI
    t.label("hb"); t.br(0x80, "hb")
    b = bytearray(t.b)
    for at, n, k in list(t.fix):
        if k in ("lo", "hi"):
            b[at] = t.lab[n] & 255 if k == "lo" else t.lab[n] >> 8
    t.b = b
    t.fix = [f for f in t.fix if f[2] in ("rel", "abs")]
    return t.done(), t.lab


def image():
    f = bytearray(SIZE)
    code, lab = tail()
    assert len(code) <= 0x300, len(code)
    f[0xFFFC:0xFFFE] = bytes([0x00, 0x80])
    f[0xFFFE:0x10000] = bytes([0x00, 0x80])
    f[0x1FFFC:0x1FFFE] = bytes([0x00, 0x80])
    a = Asm(0x8000)
    a.e(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                        # SEI CLD LDX #$FF TXS
    a.e(0xA2, 0)
    a.label("cp")
    for k in range(3):
        a.e(0xBD, 0x00, 0x81 + k); a.e(0x9D, 0x00, (ORG >> 8) + k)   # LDA $8100+k*256,X / STA $1000+k*256,X
    a.e(0xE8); a.br(0xD0, "cp")
    a.e(0x4C, ORG & 255, ORG >> 8)
    head = a.done()
    f[0x8000:0x8000 + len(head)] = head
    f[0x8100:0x8100 + len(code)] = code
    return f, code, lab


# ---- the model --------------------------------------------------------------------------------------------------------------------------------
# bus cycles of the instructions the main loop uses (65816, emulation mode): R read, I internal, W write
KINDS = {
    0x20: "RRIWWR", 0x60: "RIIRRI", 0xA9: "RR", 0xA5: "RRR", 0xAD: "RRRR", 0x85: "RRW", 0x8D: "RRRW", 0x18: "RI", 0x69: "RR", 0x48: "RIW", 0x68: "RIIR",
    0x8A: "RI", 0xAA: "RI", 0x98: "RI", 0xA8: "RI", 0xE6: "RRRIW", 0xA6: "RRR", 0x86: "RRW", 0xC5: "RRR", 0x4C: "RRR", 0x78: "RI", 0x58: "RI",
    0x06: "RRRIW", 0x26: "RRRIW", 0x66: "RRRIW", 0x46: "RRRIW", 0xC6: "RRRIW", 0xB5: "RRIR", 0x95: "RRIW", 0x65: "RRR", 0xCA: "RI", 0xA0: "RR", 0xA2: "RR",
    0xB1: "RRRRR", 0x91: "RRRRIW", 0xBA: "RI",
}
LEN = {0x20: 3, 0x60: 1, 0xA9: 2, 0xA5: 2, 0xAD: 3, 0x85: 2, 0x8D: 3, 0x18: 1, 0x69: 2, 0x48: 1, 0x68: 1, 0x8A: 1, 0xAA: 1, 0x98: 1, 0xA8: 1, 0xE6: 2,
       0xA6: 2, 0x86: 2, 0xC5: 2, 0x4C: 3, 0x78: 1, 0x58: 1, 0xD0: 2, 0xF0: 2, 0x10: 2,
       0x06: 2, 0x26: 2, 0x66: 2, 0x46: 2, 0xC6: 2, 0xB5: 2, 0x95: 2, 0x65: 2, 0xCA: 1, 0xA0: 2, 0xA2: 2, 0xB1: 2, 0x91: 2, 0xBA: 1}


class Model:
    """Runs the program's real code bytes from `pc` with zero page `zp` until the write of 0 to $0B11, giving the list of bus-cycle kinds (one letter each, in order) the C64's rules see."""
    def __init__(self, code, org=ORG):
        self.mem = {org + i: v for i, v in enumerate(code)}

    def run(self, pc, zp, limit=3_000_000):
        m, z = self.mem, dict(zp)
        a = x = y = 0
        c = 0
        zf = 0
        sp = 0xFF
        st = {}
        kinds, marks = [], []
        rd = lambda ad: m.get(ad, 0)
        for _ in range(limit):
            op = rd(pc)
            n = LEN[op]
            if op in (0xD0, 0xF0, 0x10):
                tk = (zf == 0) if op == 0xD0 else (zf != 0) if op == 0xF0 else (x < 128)
                tgt = (pc + 2 + (rd(pc + 1) - 256 if rd(pc + 1) > 127 else rd(pc + 1))) & 0xFFFF
                if tk:
                    kinds.append("RRI" + ("I" if (tgt ^ (pc + 2)) & 0xFF00 else ""))
                    pc = tgt
                else:
                    kinds.append("RR")
                    pc += 2
                continue
            kinds.append(KINDS[op])
            o = rd(pc + 1)
            ab = o | rd(pc + 2) << 8
            npc = pc + n
            if op == 0x20:
                r = npc - 1; st[sp] = r >> 8; st[sp - 1] = r & 255; sp -= 2; npc = ab
            elif op == 0x60:
                npc = ((st[sp + 2] << 8) | st[sp + 1]) + 1; sp += 2
            elif op == 0xA9: a = o; zf = int(a == 0)
            elif op == 0xA5: a = z.get(o, 0); zf = int(a == 0)
            elif op == 0xAD: a = 0; zf = 1
            elif op == 0x85: z[o] = a
            elif op == 0x8D:
                if ab == 0x0B11 and a == 0:
                    return kinds, marks
                if ab == 0x0B10:
                    marks.append(len(kinds) - 1)
            elif op == 0x18: c = 0
            elif op == 0x69: a = (a + o + c) & 255; c = int(a + 0 > 255); zf = int(a == 0)
            elif op == 0x48: st[sp] = a; sp -= 1
            elif op == 0x68: sp += 1; a = st.get(sp, 0); zf = int(a == 0)
            elif op == 0x8A: a = x; zf = int(a == 0)
            elif op == 0xAA: x = a; zf = int(x == 0)
            elif op == 0x98: a = y; zf = int(a == 0)
            elif op == 0xA8: y = a; zf = int(y == 0)
            elif op == 0xE6: z[o] = (z.get(o, 0) + 1) & 255; zf = int(z[o] == 0)
            elif op == 0xA6: x = z.get(o, 0); zf = int(x == 0)
            elif op == 0xA2: x = o; zf = int(x == 0)
            elif op == 0xCA: x = (x - 1) & 255; zf = int(x == 0)
            elif op == 0x86: z[o] = x
            elif op == 0xC5: zf = int(a == z.get(o, 0))
            elif op == 0x4C: npc = ab
            pc = npc & 0xFFFF
        raise RuntimeError("model: no end marker")

    @staticmethod
    def end_levs(kinds, start, ba_low):
        """the lev index at which each instruction's last cycle completes, the first attempt being start + 1; ba_low(m) = BA was low at the PHI2 fall before attempt m"""
        m = start
        out = []
        for ks in kinds:
            for k in ks:
                m += 1
                while k != "W" and ba_low(m):
                    m += 1
            out.append(m)
        return out


def phase_pc(code, pid, org=ORG):
    """the address after `LDA #pid / STA $0B11` (the start marker) in the code"""
    pat = bytes([0xA9, pid, 0x8D, 0x11, 0x0B])
    i = code.find(pat)
    assert i >= 0 and code.find(pat, i + 1) < 0, pid
    return org + i + len(pat)


def main():
    out = sys.argv[1]
    f, code, lab = image()
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "scpu_rom.mem"), "w") as fh:
        fh.write("\n".join("%02x" % x for x in f) + "\n")
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as fh:
        fh.write("0000\n" * 10240)
    pcs = {pid: phase_pc(code, pid) for pid in (1, 2, 3, 4)}
    pas = sum(len(k) for k in Model(code).run(pcs[1], {Z_LL: N_MASKED & 255, Z_LH: N_MASKED >> 8}, 10**7)[0]) / N_MASKED    # cycles per pass without a stall
    cyc = int((N_IRQ * 2 + N_MASKED * 2) * pas * 1.17) + 60_000                                                         # stalls (badlines) and the handler: 17 % margin
    exp = {"prog": PROG, "pc": pcs, "pass_cycles": pas,"org": ORG, "code": code.hex(), "n_masked": N_MASKED, "n_irq": N_IRQ, "period": LATCH + 1, "ms": int(cyc * 1.25 / 985.248) + 100, "labels": lab}
    with open(os.path.join(out, "expect.json"), "w") as fh:
        json.dump(exp, fh)
    print("+MS=%d" % exp["ms"])


if __name__ == "__main__":
    main()
