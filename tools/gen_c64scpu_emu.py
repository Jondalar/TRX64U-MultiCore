#!/usr/bin/env python3
"""T182 step 2: the test program and the independent model of the emulation-mode bench of the C64SCPU core (cores/c64scpu/sim/run_emu.sh).

The program is our own (no byte of any Commodore or CMD ROM): a reset routine in the KERNAL slot ($E000), two routines copied to RAM and run twice each:
  R1 (at $0220)  documented NMOS 6502 instructions only, so the 6510 of step 1 runs it too: table fill, 16 bit sum, shifts, stack and JSR, (zp),Y, page crossings (indexed
                 reads, a taken branch across a page);
  R2 (at $0A00)  what the 65C02 and the 65816 in emulation mode add: STZ, PHX/PHY/PLX/PLY, INC A/DEC A, TSB/TRB, (zp), BIT #, BRA, XCE (E must read 1).
Each routine writes a marker byte first and last ($0B00/$0B01, $0B02/$0B03); the bench prints the PHI2 cycle of every marker write.

The model is a small 6502/65C02 interpreter with the documented cycle table (NMOS = 65C02 = 65816 emulation for every instruction used here); it runs the whole program from
the reset vector, so the expected RAM bytes and the expected PHI2 cycles between the markers do not come from the machine under test.

  gen_c64scpu_emu.py <program> <out dir>   writes <out>/mem/c64scpu_rom.mem and <out>/expect.json, prints "+EXPECT=aaaa:vv,..." (the RAM bytes that must hold)
  programs: emu (the program), ctl_emu_wrong (R1's sum loop has SBC for ADC; the expectation is the one of the real program: the RAM check must fail)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c128asm  # noqa: E402

SIZE = 0x5000
KERNAL = 0x2000
R1_AT, R2_AT = 0x0220, 0x0A00
BLOB1, BLOB2 = 0xE200, 0xE300

PRE = """
sum  equ $F0
sum1 equ $F1
ptr  equ $F2
tmp  equ $F4
"""

# ---- R1: NMOS instructions only -------------------------------------------------------------------------------------------------
R1_HEAD = """
        org $0220
r1:     lda #0
        sta $0B00
        ldx #0
        lda #1
        clc
f1:     sta $0800,x
        adc #3
        inx
        cpx #16
        bne f1
        lda #0
        sta sum
        sta sum1
        ldx #0
s1:     clc
        lda sum
        %(SUMOP)s $0800,x
        sta sum
        bcc s2
        inc sum1
s2:     inx
        cpx #16
        bne s1
        lda sum
        sta $0810
        lda sum1
        sta $0811
        lda $0805
        asl a
        asl a
        ora #$81
        eor #$0F
        and #$F7
        sta $0812
        sec
        rol $0812
        lsr $0812
        ror $0812
        lda $0812
        sta $0813
        lda #$5A
        pha
        jsr sub1
        pla
        sta $0814
        stx $0815
        lda #lo(0x0800)
        sta ptr
        lda #hi(0x0800)
        sta ptr+1
        ldy #5
        lda (ptr),y
        sta $0816
        lda #lo(0x08F0)
        sta ptr
        lda #hi(0x08F0)
        sta ptr+1
        ldy #$20
        lda $08F0,y
        sta $0817
        lda (ptr),y
        sta $0818
        ldx #$20
        lda $08F0,x
        sta $0819
        sta $08F0,x
        inc $0810
        dec $0811
        txa
        tay
        iny
        dey
        sty tmp
        lda tmp
        sta $081A
        jmp padded
sub1:   ldx #7
        txa
        clc
        adc #$10
        rts
padded:
"""
R1_TAIL = """
        ldx #1
        lda #$EE
        bne fwd
        sta $081B
fwd:    lda #$11
        sta $081C
        lda #0
        sta $0B01
        rts
"""


def r1_bytes(sumop):
    head = PRE + R1_HEAD % {"SUMOP": sumop}
    # the padding is chosen so that a taken branch sits at $02FD and crosses a page: the instruction after the padding is `ldx #1` (2 bytes), `lda #$EE` (2 bytes), then bne at $02FD
    n = len(c128asm.assemble(head + "        db 0\n"))
    pad_at = R1_AT + n - 1 + 0                               # assembled `db 0` counted as one byte
    pad = 0x02FD - 4 - pad_at
    assert pad >= 0, pad
    src = head + "        ds %d, 0xEA\n" % pad + R1_TAIL
    img = c128asm.assemble(src)
    assert img[0x02FD] == 0xD0, hex(img[0x02FD])           # BNE at $02FD
    return img


# ---- R2: what the 65C02 / the 65816 in emulation mode add --------------------------------------------------------------------
R2 = PRE + """
        org $0A00
r2:     lda #0
        sta $0B02
        lda #$77
        sta tmp
        db $64, 0xF4                 ; STZ tmp
        lda tmp
        sta $0830
        lda #$55
        sta $0831
        db $9C, 0x31, 0x08           ; STZ $0831
        lda $0831
        sta $0832
        ldx #$3C
        db $DA                       ; PHX
        db $7A                       ; PLY
        sty $0833
        ldy #$A7
        db $5A                       ; PHY
        db $FA                       ; PLX
        stx $0834
        lda #$FF
        db $1A                       ; INC A
        sta $0835
        lda #0
        db $3A                       ; DEC A
        sta $0836
        lda #$33
        sta $0837
        lda #$0F
        db $0C, 0x37, 0x08           ; TSB $0837  (mem = $3F)
        php
        pla
        sta $0838
        lda $0837
        sta $0839
        lda #$F0
        db $1C, 0x37, 0x08           ; TRB $0837  (mem = $0F, A & mem = $30)
        php
        pla
        sta $083A
        lda $0837
        sta $083B
        lda #lo(0x0840)
        sta ptr
        lda #hi(0x0840)
        sta ptr+1
        lda #$C3
        db $92, 0xF2                 ; STA (ptr)
        lda #0
        db $B2, 0xF2                 ; LDA (ptr)
        sta $083C
        lda $0840
        sta $083D
        lda #$80
        db $89, 0x7F                 ; BIT #$7F  (Z = 1; N and V stay)
        php
        pla
        sta $083E
        ldx #0
        db $80, 0x02                 ; BRA +2
        ldx #$99
        stx $083F
        sec
        db $FB                       ; XCE (E = 1: C stays 1)
        lda #0
        rol a
        sta $0841
        lda #lo(r2)
        sta $0842
        lda #0
        sta $0B03
        rts
"""

MAIN = """
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        ldx #0
cp:     lda $E200,x
        sta $0220,x
        lda $E300,x
        sta $0A00,x
        inx
        bne cp
        jsr $0220
        jsr $0220
        jsr $0A00
        jsr $0A00
        lda #$A5
        sta $0B10
idle:   jmp idle
        org $FFFA
        dw reset, reset, reset
"""


# ---- the model --------------------------------------------------------------------------------------------------------------------------
def tables():
    """opcode -> (mnemonic, mode) from the assembler's tables plus the 65C02 / 65816 additions of R2"""
    dec = {}
    for mn, v in c128asm.IMPL.items():
        dec[v] = (mn, 'imp')
    for mn, v in c128asm.BRANCH.items():
        dec[v] = (mn, 'rel')
    for mn, modes in c128asm.OPS.items():
        for mode, op in modes.items():
            dec[op] = (mn, mode)
    dec[0x4C] = ('JMP', 'abs')
    dec[0x20] = ('JSR', 'abs')
    dec[0x64] = ('STZ', 'zp')
    dec[0x9C] = ('STZ', 'abs')
    dec[0x80] = ('BRA', 'rel')
    dec[0xDA] = ('PHX', 'imp')
    dec[0x5A] = ('PHY', 'imp')
    dec[0xFA] = ('PLX', 'imp')
    dec[0x7A] = ('PLY', 'imp')
    dec[0x1A] = ('INC', 'acc')
    dec[0x3A] = ('DEC', 'acc')
    dec[0x04] = ('TSB', 'zp')
    dec[0x0C] = ('TSB', 'abs')
    dec[0x14] = ('TRB', 'zp')
    dec[0x1C] = ('TRB', 'abs')
    dec[0x92] = ('STA', 'ind')
    dec[0xB2] = ('LDA', 'ind')
    dec[0x89] = ('BIT', 'imm')
    dec[0xFB] = ('XCE', 'imp')
    return dec


DEC = tables()
READ = {'LDA', 'LDX', 'LDY', 'ADC', 'SBC', 'AND', 'ORA', 'EOR', 'CMP', 'CPX', 'CPY', 'BIT'}
STORE = {'STA', 'STX', 'STY', 'STZ'}
RMW = {'ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC', 'TSB', 'TRB'}


class Model:
    def __init__(self, mem):
        self.m = bytearray(65536)
        for a, b in mem.items():
            self.m[a] = b
        self.a = self.x = self.y = 0
        self.s = 0xFF
        self.c = self.z = self.n = self.v = 0
        self.d = 0
        self.i = 1
        self.e = 1
        self.cyc = 0
        self.writes = []                      # (cycle of the write, address, value)
        self.pc = self.m[0xFFFC] | self.m[0xFFFD] << 8

    def rd(self, a):
        return self.m[a & 0xFFFF]

    def wr(self, a, v, at):
        self.m[a & 0xFFFF] = v & 0xFF
        self.writes.append((self.cyc + at, a & 0xFFFF, v & 0xFF))

    def push(self, v, at):
        self.wr(0x100 + self.s, v, at)
        self.s = (self.s - 1) & 0xFF

    def pull(self):
        self.s = (self.s + 1) & 0xFF
        return self.rd(0x100 + self.s)

    def nz(self, v):
        v &= 0xFF
        self.z = int(v == 0)
        self.n = v >> 7
        return v

    def p(self):
        return self.n << 7 | self.v << 6 | 0x30 | self.d << 3 | self.i << 2 | self.z << 1 | self.c

    def step(self):
        pc = self.pc
        op = self.rd(pc)
        if op not in DEC:
            raise SystemExit("model: no opcode %02x at %04x" % (op, pc))
        mn, mode = DEC[op]
        b1, b2 = self.rd(pc + 1), self.rd(pc + 2)
        ln = {'imp': 1, 'acc': 1, 'imm': 2, 'zp': 2, 'zpx': 2, 'zpy': 2, 'rel': 2, 'inx': 2, 'iny': 2, 'ind': 2}.get(mode, 3)
        px = 0
        addr = None
        if mode == 'zp':
            addr = b1
        elif mode == 'zpx':
            addr = (b1 + self.x) & 0xFF
        elif mode == 'zpy':
            addr = (b1 + self.y) & 0xFF
        elif mode == 'abs':
            addr = b1 | b2 << 8
        elif mode in ('abx', 'aby'):
            base = b1 | b2 << 8
            addr = (base + (self.x if mode == 'abx' else self.y)) & 0xFFFF
            px = int((base & 0xFF00) != (addr & 0xFF00))
        elif mode == 'inx':
            z = (b1 + self.x) & 0xFF
            addr = self.rd(z) | self.rd((z + 1) & 0xFF) << 8
        elif mode == 'iny':
            base = self.rd(b1) | self.rd((b1 + 1) & 0xFF) << 8
            addr = (base + self.y) & 0xFFFF
            px = int((base & 0xFF00) != (addr & 0xFF00))
        elif mode == 'ind':
            addr = self.rd(b1) | self.rd((b1 + 1) & 0xFF) << 8
        nxt = (pc + ln) & 0xFFFF
        cy = None
        wat = None                            # cycle (1 based) of the write inside the instruction
        if mn in READ:
            cy = {'imm': 2, 'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'abx': 4 + px, 'aby': 4 + px, 'inx': 6, 'iny': 5 + px, 'ind': 5}[mode]
            v = b1 if mode == 'imm' else self.rd(addr)
            if mn == 'LDA':
                self.a = self.nz(v)
            elif mn == 'LDX':
                self.x = self.nz(v)
            elif mn == 'LDY':
                self.y = self.nz(v)
            elif mn == 'AND':
                self.a = self.nz(self.a & v)
            elif mn == 'ORA':
                self.a = self.nz(self.a | v)
            elif mn == 'EOR':
                self.a = self.nz(self.a ^ v)
            elif mn in ('ADC', 'SBC'):
                assert not self.d
                w = v if mn == 'ADC' else v ^ 0xFF
                t = self.a + w + self.c
                self.v = int(~(self.a ^ w) & (self.a ^ t) & 0x80 != 0)
                self.c = t >> 8
                self.a = self.nz(t)
            elif mn in ('CMP', 'CPX', 'CPY'):
                r = {'CMP': self.a, 'CPX': self.x, 'CPY': self.y}[mn]
                self.c = int(r >= v)
                self.nz(r - v)
            elif mn == 'BIT':
                self.z = int(self.a & v == 0)
                if mode != 'imm':
                    self.n, self.v = v >> 7, v >> 6 & 1
        elif mn in STORE:
            cy = {'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'abx': 5, 'aby': 5, 'inx': 6, 'iny': 6, 'ind': 5}[mode]
            wat = cy
            val = {'STA': self.a, 'STX': self.x, 'STY': self.y, 'STZ': 0}[mn]
            self.wr(addr, val, wat)
        elif mn in RMW:
            if mode == 'acc':
                cy = 2
                v = self.a
            else:
                assert mode in ('zp', 'zpx', 'abs'), "model: RMW %s %s differs between NMOS and 65C02" % (mn, mode)
                cy = {'zp': 5, 'zpx': 6, 'abs': 6}[mode]
                wat = cy
                v = self.rd(addr)
            if mn == 'ASL':
                self.c = v >> 7
                r = self.nz(v << 1)
            elif mn == 'LSR':
                self.c = v & 1
                r = self.nz(v >> 1)
            elif mn == 'ROL':
                c = self.c
                self.c = v >> 7
                r = self.nz(v << 1 | c)
            elif mn == 'ROR':
                c = self.c
                self.c = v & 1
                r = self.nz(v >> 1 | c << 7)
            elif mn == 'INC':
                r = self.nz(v + 1)
            elif mn == 'DEC':
                r = self.nz(v - 1)
            elif mn == 'TSB':
                self.z = int(self.a & v == 0)
                r = v | self.a
            else:
                self.z = int(self.a & v == 0)
                r = v & ~self.a & 0xFF
            if mode == 'acc':
                self.a = r
            else:
                self.wr(addr, r, wat)
        elif mode == 'rel':
            off = b1 - 256 if b1 > 127 else b1
            tgt = (nxt + off) & 0xFFFF
            take = {'BPL': not self.n, 'BMI': self.n, 'BVC': not self.v, 'BVS': self.v, 'BCC': not self.c, 'BCS': self.c, 'BNE': not self.z,
                    'BEQ': self.z, 'BRA': True}[mn]
            cy = 2
            if take:
                cy += 1 + int((nxt & 0xFF00) != (tgt & 0xFF00))
                nxt = tgt
        elif mn == 'JMP':
            cy = 3
            nxt = b1 | b2 << 8
        elif mn == 'JSR':
            cy = 6
            ret = (pc + 2) & 0xFFFF
            self.push(ret >> 8, 4)
            self.push(ret & 0xFF, 5)
            nxt = b1 | b2 << 8
        elif mn == 'RTS':
            cy = 6
            lo = self.pull()
            hi = self.pull()
            nxt = ((hi << 8 | lo) + 1) & 0xFFFF
        else:
            cy = 2
            if mn == 'CLC':
                self.c = 0
            elif mn == 'SEC':
                self.c = 1
            elif mn == 'SEI':
                self.i = 1
            elif mn == 'CLD':
                self.d = 0
            elif mn == 'INX':
                self.x = self.nz(self.x + 1)
            elif mn == 'INY':
                self.y = self.nz(self.y + 1)
            elif mn == 'DEX':
                self.x = self.nz(self.x - 1)
            elif mn == 'DEY':
                self.y = self.nz(self.y - 1)
            elif mn == 'TAX':
                self.x = self.nz(self.a)
            elif mn == 'TAY':
                self.y = self.nz(self.a)
            elif mn == 'TXA':
                self.a = self.nz(self.x)
            elif mn == 'TYA':
                self.a = self.nz(self.y)
            elif mn == 'TXS':
                self.s = self.x
            elif mn == 'NOP':
                pass
            elif mn == 'XCE':
                self.c, self.e = self.e, self.c
            elif mn in ('PHA', 'PHX', 'PHY', 'PHP'):
                cy = 3
                self.push({'PHA': self.a, 'PHX': self.x, 'PHY': self.y, 'PHP': self.p()}[mn], 3)
            elif mn in ('PLA', 'PLX', 'PLY'):
                cy = 4
                r = self.nz(self.pull())
                if mn == 'PLA':
                    self.a = r
                elif mn == 'PLX':
                    self.x = r
                else:
                    self.y = r
            else:
                raise SystemExit("model: %s not modelled" % mn)
        self.cyc += cy
        self.pc = nxt
        return mn


def run_model(mem):
    m = Model(mem)
    for _ in range(100000):
        pc = m.pc
        if m.rd(pc) == 0x4C and (m.rd(pc + 1) | m.rd(pc + 2) << 8) == pc:      # JMP * : the program is at its idle loop
            return m
        m.step()
    raise SystemExit("model: the program does not end")


def build(kind):
    sumop = "sbc" if kind == "ctl_emu_wrong" else "adc"
    r1 = r1_bytes(sumop)
    r2 = c128asm.assemble(R2)
    main = c128asm.assemble(MAIN)
    assert max(r1) - min(r1) < 0x100 and min(r1) == R1_AT and max(r2) - min(r2) < 0x100 and min(r2) == R2_AT
    img = bytearray([0xEA]) * SIZE
    for a, b in main.items():
        img[KERNAL + (a - 0xE000)] = b
    for a, b in r1.items():
        img[KERNAL + (BLOB1 - 0xE000) + (a - R1_AT)] = b
    for a, b in r2.items():
        img[KERNAL + (BLOB2 - 0xE000) + (a - R2_AT)] = b
    rom = {0xE000 + i: img[KERNAL + i] for i in range(0x2000)}
    return img, rom


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("emu", "ctl_emu_wrong"):
        sys.exit(__doc__)
    kind, out = sys.argv[1], sys.argv[2]
    img, _ = build(kind)
    # the expectation is always the real program's, run through the model
    real, rom = build("emu")
    mod = run_model(rom)
    marks = {}
    for cyc, a, v in mod.writes:
        if a in (0xB00, 0xB01, 0xB02, 0xB03):
            marks.setdefault(a, []).append(cyc)
    d1 = marks[0xB01][1] - marks[0xB00][1]                 # the second pass of each routine
    d2 = marks[0xB03][1] - marks[0xB02][1]
    exp = []
    for a in list(range(0x0800, 0x0820)) + list(range(0x0830, 0x0843)) + [0x0B10]:
        exp.append("%04x:%02x" % (a, mod.m[a]))
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as f:
        for i in range(0, SIZE, 2):
            f.write("%02x%02x\n" % (img[i + 1], img[i]))
    with open(os.path.join(out, "expect.json"), "w") as f:
        json.dump({"r1_cycles": d1, "r2_cycles": d2, "model_cycles": mod.cyc}, f)
    print("+EXPECT=" + ",".join(exp))


if __name__ == "__main__":
    main()
