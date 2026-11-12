#!/usr/bin/env python3
"""T91 S3 and friends: the Z80 test programs of the ZX Spectrum core's benches (cores/zx/sim/prog), written for this project (our
own source, no third-party code or ROM), and the oracle that gives their expected results.

  tools/zx_progs.py build         assemble every program into cores/zx/sim/prog/<name>.hex (one byte per line, for $readmemh),
                                  run the ones with an oracle on the z80 emulator and write <name>.exp.hex (the bytes the bench must
                                  find at the result address); needs `pip install z80` (kosarev/z80, MIT: assembler and emulator).
                                  The committed .hex files are what the benches and the CI use, the CI has no z80 package.
  tools/zx_progs.py --selftest    the assembler (tools/zx_asm.py) against the z80 package's assembler on a list of instructions
  tools/zx_progs.py --check       the committed .hex files are the ones made from the sources in this file (a SHA-256 per program in
                                  <name>.sha256; no z80 package needed)
Expected results come from the z80 emulator (z80t, the oracle) or from the program's specification (page128, page48: values known by
construction). Result protocol of a program: it writes its results into RAM and ends with a spin loop after storing 0xA5 at DONE (0x9FFF).
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROG = os.path.join(ROOT, "cores/zx/sim/prog")

# ------------------------------------------------------------------------------------------------ z80t: a ZEXALL-style test
# Every group executes a template instruction (or a run of opcodes) on pseudo-random registers and memory and folds the
# resulting state into a 16 bit checksum; the checksum per group is the result. The oracle runs the same program on a Z80
# emulator, so the bench compares checksums, not hand-written values - all flags, the undocumented ones too, and memory.
FL_CON, FL_FILL, FL_CSM, FL_IXY = 1, 2, 4, 8
NOP = 0x00


def grp0(b, step=0, pos=0, var=1, vec=16, fl=0, imm=0xFF, dpos=0xFF):
    """one table entry: bytes b, opcode byte index `pos` += step for `var` variants, `vec` vectors per variant"""
    ln = len(b)
    bb = list(b) + [0] * (4 - ln)
    return [ln] + bb + [pos, step, var, vec, fl, imm, dpos, 0, 0, 0, 0]


def z80t_table(scale=1):
    """scale: divisor of the vectors per variant (the short program z80s runs in a sixth of the time)"""
    t = []
    def grp(b, step=0, pos=0, var=1, vec=16, fl=0, imm=0xFF, dpos=0xFF):
        return grp0(b, step, pos, var, max(1, vec // scale), fl, imm, dpos)
    # 8 bit ALU on registers and (HL): ADD ADC SUB SBC AND XOR OR CP  A,r  (64 opcodes; the (HL) forms read the scratch)
    t.append(grp([0x80], 1, 0, 64, 12, FL_CON | FL_FILL | FL_CSM))
    t.append(grp([0xC6, 0], 8, 0, 8, 96, 0, imm=1))                 # ALU A,n
    t.append(grp([0x04], 8, 0, 8, 24, FL_CON | FL_FILL | FL_CSM))   # INC r (incl. (HL))
    t.append(grp([0x05], 8, 0, 8, 24, FL_CON | FL_FILL | FL_CSM))   # DEC r
    t.append(grp([0x09], 0x10, 0, 3, 64))                           # ADD HL,rr (BC DE HL)
    t.append(grp([0xED, 0x4A], 0x10, 1, 3, 64))                     # ADC HL,rr
    t.append(grp([0xED, 0x42], 0x10, 1, 3, 64))                     # SBC HL,rr
    for op in (0x27, 0x2F, 0x37, 0x3F, 0x07, 0x0F, 0x17, 0x1F):     # DAA CPL SCF CCF RLCA RRCA RLA RRA
        t.append(grp([op], 0, 0, 1, 255))
    t.append(grp([0xED, 0x44], 0, 0, 1, 255))                       # NEG
    t.append(grp([0xCB, 0x00], 1, 1, 64, 12, FL_CON | FL_FILL | FL_CSM))   # rotates and shifts (RLC RRC RL RR SLA SRA SLL SRL) on r, (HL)
    t.append(grp([0xCB, 0x40], 1, 1, 192, 4, FL_CON | FL_FILL | FL_CSM))  # BIT SET RES b,r
    t.append(grp([0xED, 0x6F], 0, 0, 1, 128, FL_CON | FL_FILL | FL_CSM))   # RLD
    t.append(grp([0xED, 0x67], 0, 0, 1, 128, FL_CON | FL_FILL | FL_CSM))   # RRD
    for op in (0xA0, 0xA1, 0xA8, 0xA9):                             # LDI CPI LDD CPD
        t.append(grp([0xED, op], 0, 0, 1, 128, FL_CON | FL_FILL | FL_CSM))
    t.append(grp([0xDD, 0x86, 0], 8, 1, 8, 24, FL_CON | FL_FILL | FL_CSM | FL_IXY, dpos=2))       # ALU A,(IX+d)
    t.append(grp([0xDD, 0x34, 0], 1, 1, 2, 64, FL_CON | FL_FILL | FL_CSM | FL_IXY, dpos=2))       # INC/DEC (IX+d)
    t.append(grp([0xFD, 0x7E, 0], 0, 0, 1, 64, FL_CON | FL_FILL | FL_CSM | FL_IXY, dpos=2))       # LD A,(IY+d)
    t.append(grp([0xDD, 0x09], 0x10, 1, 4, 64, FL_IXY))                                           # ADD IX,rr (BC DE IX SP-less)
    t.append(grp([0xDD, 0xCB, 0, 0x00], 1, 3, 64, 8, FL_CON | FL_FILL | FL_CSM | FL_IXY, dpos=2))  # DDCB rotates, undocumented register copies too
    t.append(grp([0xDD, 0xCB, 0, 0x46], 8, 3, 8, 8, FL_CON | FL_FILL | FL_CSM | FL_IXY, dpos=2))  # BIT b,(IX+d)
    t.append(grp([0xFD, 0xCB, 0, 0x86], 8, 3, 8, 8, FL_CON | FL_FILL | FL_CSM | FL_IXY, dpos=2))  # RES b,(IY+d)
    return t


def z80t_asm(scale=1):
    tab = z80t_table(scale)
    rows = []
    for e in tab:
        rows.append("        db " + ", ".join("0x%02X" % x for x in e))
    return TEMPLATE_Z80T.replace("@TABLE@", "\n".join(rows)).replace("@NGROUPS@", str(len(tab)))


TEMPLATE_Z80T = """\
        org 0
RES     equ 0x9000
DONE    equ 0x9FFF
SEED    equ 0x8000
CRC     equ 0x8002
SSP     equ 0x8004
GPTR    equ 0x8006
VCNT    equ 0x8008
VARN    equ 0x8009
FLG     equ 0x800A
ST      equ 0x8010
SLOT    equ 0x8100
SCR     equ 0x8200
STK     equ 0xBFF0
start:  di
        ld sp, STK
        ld hl, 0x8000
        ld de, 0x8001
        ld bc, 0x00FF
        ld (hl), 0
        ldir
        ld hl, RES
        ld de, RES+1
        ld bc, 0x00FF
        ld (hl), 0
        ldir
        ld hl, 0x1234
        ld (SEED), hl
        ld hl, table
        ld (GPTR), hl
group:  ld hl, (GPTR)
        ld a, (hl)
        or a
        jp z, misc
        push hl
        inc hl
        ld de, SLOT
        ld bc, 4
        ldir
        pop hl
        ld e, (hl)
        ld d, 0
        ld hl, SLOT
        add hl, de
        ld (hl), 0xC9
        ld hl, 0
        ld (CRC), hl
        ld hl, (GPTR)
        ld de, 7
        add hl, de
        ld a, (hl)
        ld (VARN), a
variant: ld hl, (GPTR)
        ld de, 8
        add hl, de
        ld a, (hl)
        ld (VCNT), a
vector: call dovec
        ld hl, VCNT
        dec (hl)
        jr nz, vector
        ld hl, (GPTR)
        ld de, 6
        add hl, de
        ld c, (hl)
        ld a, c
        or a
        jr z, nostep
        dec hl
        ld e, (hl)
        ld d, 0
        ld hl, SLOT
        add hl, de
        ld a, (hl)
        add a, c
        ld (hl), a
nostep: ld hl, VARN
        dec (hl)
        jr nz, variant
        ld hl, (GPTR)
        ld de, table
        or a
        sbc hl, de
        srl h
        rr l
        srl h
        rr l
        srl h
        rr l
        ld de, RES
        add hl, de
        ex de, hl
        ld hl, (CRC)
        ex de, hl
        ld (hl), e
        inc hl
        ld (hl), d
        ld hl, (GPTR)
        ld de, 16
        add hl, de
        ld (GPTR), hl
        jp group

dovec:  call xrnd
        ld (ST), hl
        call xrnd
        ld (ST+2), hl
        call xrnd
        ld (ST+4), hl
        call xrnd
        ld (ST+6), hl
        ld hl, (GPTR)
        ld de, 9
        add hl, de
        ld a, (hl)
        ld (FLG), a
        bit 0, a
        jr z, nocon
        ld a, (ST+6)
        and 0x0F
        ld (ST+6), a
        ld a, 0x82
        ld (ST+7), a
        ld a, (ST+4)
        and 0x0F
        or 0x10
        ld (ST+4), a
        ld a, 0x82
        ld (ST+5), a
        xor a
        ld (ST+3), a
nocon:  ld a, (FLG)
        bit 1, a
        jr z, nofill
        ld de, SCR
        ld b, 8
fill:   call xrnd
        ld a, l
        ld (de), a
        inc de
        ld a, h
        ld (de), a
        inc de
        djnz fill
nofill: ld hl, (GPTR)
        ld de, 10
        add hl, de
        ld a, (hl)
        cp 0xFF
        jr z, noimm
        ld e, a
        ld d, 0
        ld hl, SLOT
        add hl, de
        push hl
        call xrnd
        ld a, l
        pop hl
        ld (hl), a
noimm:  ld hl, (GPTR)
        ld de, 11
        add hl, de
        ld a, (hl)
        cp 0xFF
        jr z, nod
        ld e, a
        ld d, 0
        ld hl, SLOT
        add hl, de
        push hl
        call xrnd
        ld a, l
        and 0x0F
        sub 8
        pop hl
        ld (hl), a
nod:    ld ix, SCR+8
        ld iy, SCR+0x18
        ld (SSP), sp
        ld sp, ST
        pop af
        pop bc
        pop de
        pop hl
        ld sp, (SSP)
        call SLOT
        ld (SSP), sp
        ld sp, ST+8
        push hl
        push de
        push bc
        push af
        ld sp, (SSP)
        ld hl, ST
        ld b, 8
        call crcb
        ld a, (FLG)
        bit 2, a
        jr z, nocs
        ld hl, SCR
        ld b, 32
        call crcb
nocs:   ld a, (FLG)
        bit 3, a
        ret z
        ld (ST+8), ix
        ld (ST+10), iy
        ld hl, ST+8
        ld b, 4
        jp crcb

crcb:   ex de, hl
        ld hl, (CRC)
cb1:    add hl, hl
        jr nc, cb2
        set 0, l
cb2:    ld a, (de)
        inc de
        xor l
        ld l, a
        djnz cb1
        ld (CRC), hl
        ret

xrnd:   ld hl, (SEED)
        ld a, h
        rra
        ld a, l
        rra
        xor h
        ld h, a
        ld a, l
        rra
        ld a, h
        rra
        xor l
        ld l, a
        xor h
        ld h, a
        ld (SEED), hl
        ret

misc:   ld a, 0x55
        ld r, a
        ld a, r
        ld (RES+0x80), a
        nop
        nop
        nop
        nop
        nop
        ld a, r
        ld (RES+0x81), a
        ld bc, 0x1234
        ld de, 0x5678
        ld hl, 0x9ABC
        exx
        ld bc, 0x1111
        ld de, 0x2222
        ld hl, 0x3333
        exx
        ld (RES+0x82), bc
        ld (RES+0x84), de
        ld (RES+0x86), hl
        exx
        ld (RES+0x88), bc
        ld (RES+0x8A), de
        ld (RES+0x8C), hl
        exx
        ld b, 200
        ld hl, 0
l1:     inc hl
        djnz l1
        ld (RES+0x90), hl
        ld hl, 0x1357
        push hl
        ld hl, 0x2468
        ex (sp), hl
        ld (RES+0x92), hl
        pop hl
        ld (RES+0x94), hl
        ld hl, table
        ld de, 0x8400
        ld bc, 0x0100
        ldir
        ld hl, 0
        ld (CRC), hl
        ld hl, 0x8400
        ld b, 0
        call crcb
        ld hl, (CRC)
        ld (RES+0x96), hl
        ld hl, table
        ld bc, 0x0100
        ld a, 0x3F
        cpir
        ld (RES+0x98), hl
        ld (RES+0x9A), bc
        ld a, 0xA5
        ld (DONE), a
spin:   jr spin

table:
@TABLE@
        db 0
"""

# ------------------------------------------------------------------------------------------------ cont: a NOP sled in contended RAM (S6)
CONT = """\
        org 0
        di
        jp start
        org 0x38
isr:    push af
        ld a, 5
isw:    dec a
        jr nz, isw
        pop af
        ei
        ret
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld hl, 0x4000
        ld de, 0x4001
        ld bc, 0x3FFF
        ld (hl), 0
        ldir
        ld a, 0xC3
        ld (0x6000), a
        ld hl, 0x4000
        ld (0x6001), hl
        im 1
        ei
        jp 0x4000
"""


def image_cont(pad):
    code = assemble(CONT)
    return code if not pad else code + bytes(pad - len(code))


# ------------------------------------------------------------------------------------------------ video48 / shadow128 (S7)
# video48: random bitmap, attribute = low byte of its address (all 256 attribute values, FLASH and BRIGHT included, three times), and
# a border band per raster line (OUT $FE every 224 T-states, in the line's horizontal blanking, 8 lines per colour).
# The loop is timed by hand: one iteration is 224 T-states (checked below against the Z80 emulator when it is installed).
VIDEO48 = """\
        org 0
SEED    equ 0x8000
        di
        jp start
        org 0x38
        jp tick
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld hl, 0x1234
        ld (SEED), hl
        ld de, 0x4000
fp:     call xrnd
        ld a, l
        ld (de), a
        inc de
        ld a, h
        ld (de), a
        inc de
        ld a, d
        cp 0x58
        jr nz, fp
        ld hl, 0x5800
fa:     ld (hl), l
        inc hl
        ld a, h
        cp 0x5B
        jr nz, fa
        ld a, 5
        out (0xFE), a
        im 1
main:   ei
        halt
        jr main
tick:   ld sp, 0xBFF0
        ld hl, 228
dl:     dec hl
        ld a, h
        or l
        jp nz, dl
        ld c, 24
        ld de, 270
loopi:  ld a, c
        rrca
        rrca
        rrca
        and 7
        out (0xFE), a
        inc c
        dec de
        ld b, 11
pad:    djnz pad
        ld a, 0
        nop
        inc hl
        ld a, d
        or e
        jp nz, loopi
        jp main
xrnd:   ld hl, (SEED)
        ld a, h
        rra
        ld a, l
        rra
        xor h
        ld h, a
        ld a, l
        rra
        ld a, h
        rra
        xor l
        ld l, a
        xor h
        ld h, a
        ld (SEED), hl
        ret
"""

SHADOW128 = """\
        org 0
SEED    equ 0x8000
        di
        jp start
        org 0x38
        ei
        ret
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld hl, 0x1234
        ld (SEED), hl
        ld de, 0x4000
fp:     call xrnd
        ld a, l
        ld (de), a
        inc de
        ld a, h
        ld (de), a
        inc de
        ld a, d
        cp 0x58
        jr nz, fp
        ld hl, 0x5800
fa:     ld (hl), l
        inc hl
        ld a, h
        cp 0x5B
        jr nz, fa
        ld bc, 0x7FFD
        ld a, 7
        out (c), a
        ld hl, 0x5678
        ld (SEED), hl
        ld de, 0xC000
fq:     call xrnd
        ld a, l
        ld (de), a
        inc de
        ld a, h
        ld (de), a
        inc de
        ld a, d
        cp 0xD8
        jr nz, fq
        ld hl, 0xD800
fb:     ld a, l
        xor 0x5A
        ld (hl), a
        inc hl
        ld a, h
        cp 0xDB
        jr nz, fb
        ld a, 0x0F
        out (c), a
        im 1
        ei
spin:   halt
        jr spin
xrnd:   ld hl, (SEED)
        ld a, h
        rra
        ld a, l
        rra
        xor h
        ld h, a
        ld a, l
        rra
        ld a, h
        rra
        xor l
        ld l, a
        xor h
        ld h, a
        ld (SEED), hl
        ret
"""


def image_shadow128():
    code = assemble(SHADOW128)
    return code + bytes(32768 - len(code))


# ------------------------------------------------------------------------------------------------ audio128 (S8)
# Phases (the byte at $9000 names the phase, the bench measures inside each): 1 a beeper tone of 120 periods, 2 an AY tone (A, period
# 200), 3 AY noise on A, 4 silence. The beeper period is computed here (T-states) and given to the bench in audio128.meta.
BEEP_D = 200
BEEP_N = 120
AY_TP = 200


def audio_asm():
    def ay(reg, val):
        return ("        ld bc, 0xFFFD\n        ld a, %d\n        out (c), a\n        ld bc, 0xBFFD\n        ld a, %d\n        out (c), a\n" % (reg, val))

    t = """\
        org 0
        di
        jp start
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld a, 1
        ld (0x9000), a
        ld de, %(n)d
beep:   ld a, 0x10
        out (0xFE), a
        ld b, %(d)d
dj1:    djnz dj1
        xor a
        out (0xFE), a
        ld b, %(d)d
dj2:    djnz dj2
        dec de
        ld a, d
        or e
        jp nz, beep
        ld a, 2
        ld (0x9000), a
""" % dict(n=BEEP_N, d=BEEP_D)
    t += ay(0, AY_TP & 255) + ay(1, AY_TP >> 8) + ay(7, 0x3E) + ay(8, 15)
    t += """\
        ld bc, %d
w1:     dec bc
        ld a, b
        or c
        jp nz, w1
        ld a, 3
        ld (0x9000), a
""" % (16 * 70908 // 24)
    t += ay(6, 8) + ay(7, 0x37)
    t += """\
        ld bc, %d
w2:     dec bc
        ld a, b
        or c
        jp nz, w2
        ld a, 4
        ld (0x9000), a
""" % (16 * 70908 // 24)
    t += ay(8, 0)
    t += """\
        ld bc, %d
w3:     dec bc
        ld a, b
        or c
        jp nz, w3
        ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin
""" % (20 * 70908 // 24)
    return t


def beep_period_t():
    d = BEEP_D
    return 71 + 2 * (13 * (d - 1) + 8)


def image_audio():
    code = assemble(audio_asm())
    return code + bytes(32768 - len(code))


# ------------------------------------------------------------------------------------------------ keys48 (S9)
# Scans the eight half-rows and the Kempston port in a loop; $9000-$9007 the rows as read, $9008 port $1F, $9009 a scan counter.
KEYS48 = """\
        org 0
        di
        jp start
        org 0x66
        retn
start:  ld sp, 0xBFF0
loop:   ld d, 0xFE
        ld hl, 0x9000
        ld e, 8
rl:     ld b, d
        ld c, 0xFE
        in a, (c)
        ld (hl), a
        inc hl
        rlc d
        dec e
        jr nz, rl
        in a, (0x1F)
        ld (0x9008), a
        ld a, (0x9009)
        inc a
        ld (0x9009), a
        jr loop
"""

# ------------------------------------------------------------------------------------------------ tape48 (S10)
# A loader of our own (not the ROM's): it measures pulse lengths by counting the polling loop (43 T-states per count), waits for a
# leader of at least 64 pilot-length pulses and a short sync, reads bits as pairs of equal pulses (a pair that sums to 57 counts or more
# is a one) and checks the XOR of all bytes. Two blocks are loaded: $9010 (good) and $9040 (one data byte flipped: the XOR must fail).
TAPE48 = """\
        org 0
LDST    equ 0x8000
LLEN    equ 0x8002
CUR     equ 0x8003
CK      equ 0x8004
REM     equ 0x8005
P1      equ 0x8006
        di
        jp start
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld hl, 0x9010
        ld de, %(len)d
        call load
        ld (0x9000), a
        ld hl, 0x9040
        ld de, %(len)d
        call load
        ld (0x9001), a
        ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin

edge:   ld b, 0
ed1:    inc b
        ret z
        in a, (0xFE)
        and 0x40
        xor c
        jr z, ed1
        ld a, c
        xor 0x40
        ld c, a
        ret

load:   ld (LDST), hl
        ld a, e
        ld (LLEN), a
        in a, (0xFE)
        and 0x40
        ld c, a
lead0:  ld d, 0
lead1:  call edge
        ld a, b
        or a
        jr z, lead0
        cp 36
        jr c, lead2
        cp 70
        jr nc, lead0
        inc d
        jr lead1
lead2:  ld a, d
        cp 64
        jr c, lead0
        call edge
        ld a, b
        or a
        jr z, lead0
        cp 36
        jr nc, lead0
        ld hl, (LDST)
        ld a, (LLEN)
        ld (REM), a
        xor a
        ld (CK), a
nextb:  ld a, 1
        ld (CUR), a
nextbit: call edge
        ld a, b
        or a
        jr z, fail
        ld (P1), a
        call edge
        ld a, b
        or a
        jr z, fail
        ld d, a
        ld a, (P1)
        add a, d
        cp 57
        ccf
        ld a, (CUR)
        rla
        ld (CUR), a
        jr nc, nextbit
        ld (hl), a
        inc hl
        ld d, a
        ld a, (CK)
        xor d
        ld (CK), a
        ld a, (REM)
        dec a
        ld (REM), a
        jr nz, nextb
        ld a, (CK)
        or a
        jr nz, fail
        ld a, 1
        ret
fail:   ld a, 0x80
        ret
"""

TAPE_DATA = 16
PILOT_PULSES = 400


def tape_blocks():
    """the two blocks as bytes: flag, 16 data bytes, XOR; the second with a data byte flipped after the XOR was made"""
    import random
    rnd = random.Random(10)
    data = bytes(rnd.randrange(256) for _ in range(TAPE_DATA))
    a = bytes([0xFF]) + data
    x = 0
    for b in a:
        x ^= b
    blk_a = a + bytes([x])
    data2 = bytes(rnd.randrange(256) for _ in range(TAPE_DATA))
    b2 = bytes([0xFF]) + data2
    x = 0
    for b in b2:
        x ^= b
    blk_b = bytearray(b2 + bytes([x]))
    blk_b[5] ^= 0x10
    return blk_a, bytes(blk_b)


def pulses_for(block):
    """leader, sync, bits of one block, as durations in T-states (the ROM loader's pulse lengths)"""
    p = [2168] * PILOT_PULSES + [667, 735]
    for byte in block:
        for i in range(7, -1, -1):
            d = 1710 if (byte >> i) & 1 else 855
            p += [d, d]
    return p


def tape_words():
    """the pulse stream (D9): a lead-in, block A, a stop marker, a long gap, block B, the end"""
    blk_a, blk_b = tape_blocks()
    w = [3000] + pulses_for(blk_a) + [3500]
    w += [0xFFFE]                                   # stop the tape: the app starts it again
    w += [0xFFFF, 100000 & 0xFFFF, 100000 >> 16]    # a gap of 100,000 T-states: more than a word holds (65,533), so it needs the escape
    w += pulses_for(blk_b) + [3500]
    w += [0]                                        # the end of the tape
    return w


def tape_ddr_lines():
    w = tape_words()
    while len(w) % 8:
        w.append(0)
    lines = []
    for i in range(0, len(w), 8):
        v = 0
        for j in range(8):
            v |= w[i + j] << (16 * j)
        lines.append("%032x" % v)
    return lines


def exp_tape(*_):
    e = bytearray(256)
    blk_a, blk_b = tape_blocks()
    e[0] = 1
    e[1] = 0x80
    e[0x10:0x10 + len(blk_a)] = blk_a
    e[0x40:0x40 + len(blk_b)] = blk_b
    return bytes(e)


def image_tape():
    code = assemble(TAPE48 % dict(len=TAPE_DATA + 2))
    return code


def image_keys():
    return assemble(KEYS48)


# ------------------------------------------------------------------------------------------------ pause48 (S13)
# A program whose every cycle shows: a counter writes into the (contended) screen, the border follows the counter's low bits (so the
# picture holds the timeline), an interrupt counts frames. The bench runs it with and without pauses and compares per-frame hashes.
PAUSE48 = """\
        org 0
        di
        jp start
        org 0x38
        jp isr
        org 0x66
        retn
start:  ld sp, 0xBFF0
        im 1
        ld de, 0
        ei
m1:     inc de
        ld a, d
        and 0x1F
        or 0x40
        ld h, a
        ld l, e
        ld (hl), e
        ld a, e
        and 0x07
        out (0xFE), a
        jr m1
isr:    push af
        ld a, (0x8000)
        inc a
        ld (0x8000), a
        pop af
        ei
        ret
"""


PROGRAMS = {}      # name -> (image function, expected-result function or None, needs the z80 package for the expected result)


def register(name, img_fn, exp_fn=None, oracle=False):
    PROGRAMS[name] = (img_fn, exp_fn, oracle)


def oracle_z80t(image):
    """run the program on the emulator; the bytes of RES (0x9000-0x90FF) when it reaches the spin loop"""
    import z80
    m = z80.Z80Machine()
    m.memory[0:len(image)] = image
    m.pc = 0
    for _ in range(4000):
        m.ticks_to_stop = 1_000_000
        m.run()
        if m.memory[0x9FFF] == 0xA5:
            break
    else:
        raise SystemExit("zx_progs: the oracle did not reach the end of z80t")
    return bytes(m.memory[0x9000:0x9100])


register("z80t", lambda: assemble(z80t_asm()), oracle_z80t, oracle=True)
register("z80s", lambda: assemble(z80t_asm(6)), oracle_z80t, oracle=True)


# ------------------------------------------------------------------------------------------------ page128 / page48 (S5)
# The 128K program: write a pattern into every RAM bank through $7FFD (and its aliases: the port decodes A15 = 0 and A1 = 0 only),
# read it back, select ROM 0 and ROM 1 (the routine runs from RAM: the ROM under it changes), try the paging lock (bit 5).
PAGE128 = """\
        org 0
RES     equ 0x9000
DONE    equ 0x9FFF
start:  di
        ld sp, 0xBFF0
        ld hl, rout
        ld de, 0x8100
        ld bc, rend-rout
        ldir
        jp 0x8100
rout:   ld b, 0
wrl:    ld a, b
        ld c, 0xFD
        push bc
        ld b, 0x7F
        out (c), a
        pop bc
        ld a, b
        add a, 0x10
        ld (0xC000), a
        add a, 0x10
        ld (0xFFFF), a
        inc b
        ld a, b
        cp 8
        jr nz, wrl
        ld b, 0
rdl:    ld a, b
        ld c, 0xFD
        push bc
        ld b, 0x7F
        out (c), a
        pop bc
        ld a, (0xC000)
        ld hl, RES
        ld e, b
        ld d, 0
        add hl, de
        ld (hl), a
        ld a, (0xFFFF)
        ld hl, RES+8
        add hl, de
        ld (hl), a
        inc b
        ld a, b
        cp 8
        jr nz, rdl
        ld bc, 0x7FFD
        xor a
        out (c), a
        ld a, (0x0000)
        ld (RES+0x10), a
        ld a, (0x3FFF)
        ld (RES+0x11), a
        ld a, 0x10
        out (c), a
        ld a, (0x0000)
        ld (RES+0x12), a
        ld a, (0x3FFF)
        ld (RES+0x13), a
        ld bc, 0x3FFD
        ld a, 0x04
        out (c), a
        ld a, (0xC000)
        ld (RES+0x15), a
        ld bc, 0x7FFD
        ld a, 0x23
        out (c), a
        ld a, 0x05
        out (c), a
        ld a, (0xC000)
        ld (RES+0x14), a
        ld a, 0xA5
        ld (DONE), a
spin:   jr spin
rend:
"""

PAGE48 = """\
        org 0
RES     equ 0x9000
DONE    equ 0x9FFF
start:  di
        ld sp, 0xBFF0
        ld a, 0x77
        ld (0xC000), a
        ld bc, 0x7FFD
        ld a, 0x01
        out (c), a
        ld a, 0x88
        ld (0xC000), a
        xor a
        out (c), a
        ld a, (0xC000)
        ld (RES), a
        ld a, 0x11
        out (c), a
        ld a, (0x0000)
        ld (RES+1), a
        ld a, 0xA5
        ld (DONE), a
spin:   jr spin
"""


def image128():
    code = bytearray(assemble(PAGE128))
    img = bytearray(32768)
    img[:len(code)] = code
    img[0x3FFF] = 0xAA
    img[0x4000] = 0xB1
    img[0x7FFF] = 0xB2
    return bytes(img)


def exp128():
    e = bytearray(256)
    for b in range(8):
        e[b] = 0x10 + b
        e[8 + b] = 0x20 + b
    e[0x10] = 0xF3                 # ROM 0, first byte (DI)
    e[0x11] = 0xAA
    e[0x12] = 0xB1                 # ROM 1
    e[0x13] = 0xB2
    e[0x14] = 0x13                 # paging locked: bank 3 still at $C000 after "write 5"
    e[0x15] = 0x14                 # the alias port $3FFD (A15 = 0, A1 = 0) selected bank 4
    return bytes(e)


def exp48():
    e = bytearray(256)
    e[0] = 0x88                    # 48K: $7FFD is ignored, $C000 is one bank
    e[1] = 0xF3                    # the ROM does not page either
    return bytes(e)


register("cont48", lambda: image_cont(0))
register("cont128", lambda: image_cont(32768))
register("video48", lambda: assemble(VIDEO48))
register("shadow128", image_shadow128)
register("audio128", image_audio)
register("keys48", image_keys)
register("tape48", image_tape, exp_tape)
register("pause48", lambda: assemble(PAUSE48))
register("page128", image128, lambda *_: exp128())
register("page48", lambda: assemble(PAGE48), lambda *_: exp48())

# ------------------------------------------------------------------------------------------------ build
def assemble(text, org=0):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import zx_asm
    return zx_asm.assemble(text)


SELFTEST_LINES = """ld a,0x12;ld b,c;ld (hl),d;ld e,(hl);ld hl,0x1234;ld sp,0x8000;ld bc,0x4321;ld a,(0x9000);ld (0x9001),a;ld hl,(0x9002);
ld (0x9004),hl;ld de,(0x9006);ld (0x9008),bc;ld sp,(0x900a);ld (0x900c),sp;ld (hl),0x55;ld a,i;ld a,r;ld i,a;ld r,a;ld a,(bc);ld (de),a;
ld ix,0x1234;ld iy,0x4321;ld (ix+3),a;ld (iy-2),0x77;ld c,(ix-8);ld a,(iy+5);ld ix,(0x9000);ld (0x9002),iy;ld sp,ix;ld sp,hl;
push af;push bc;push ix;pop de;pop iy;ex de,hl;ex af,af';ex (sp),hl;ex (sp),ix;exx;add a,b;adc a,0x10;sub (hl);sbc a,(ix+4);and 0x0f;
xor a;or c;cp 0x20;cp (iy-1);add hl,bc;add hl,sp;adc hl,de;sbc hl,hl;add ix,bc;add iy,iy;inc a;dec (hl);inc bc;dec sp;inc ix;dec iy;
inc (ix+1);dec (iy-3);daa;cpl;neg;ccf;scf;nop;halt;di;ei;im 0;im 1;im 2;rlca;rrca;rla;rra;rlc b;rrc (hl);rl d;rr e;sla h;sra l;sll a;
srl (hl);bit 3,a;res 7,(hl);set 0,b;bit 1,(ix+2);set 4,(iy-5);rlc (ix+1);rlc (ix+1),b;res 2,(iy+3),c;rld;rrd;ldi;ldd;ldir;lddr;cpi;cpd;
cpir;cpdr;ini;ind;inir;indr;outi;outd;otir;otdr;reti;retn;ret;ret nz;ret c;ret po;rst 0x38;jp 0x1234;jp nz,0x1234;jp m,0x1234;jp (hl);
jp (ix);call 0x1234;call z,0x1234;jr $+5;jr nz,$-3;jr c,$+2;djnz $+7;in a,(0xfe);out (0xfe),a;in b,(c);out (c),d""".replace("\n", "").split(";")


def selftest():
    """our assembler against the z80 package's, instruction by instruction (numeric operands only: theirs has no expressions)"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import zx_asm
    from z80._asm import Asm
    from z80._source import SourceFile
    import tempfile
    bad = 0
    for ln in SELFTEST_LINES:
        ln = ln.strip()
        if not ln:
            continue
        # relative jumps are written with $ in both
        mine = zx_asm.assemble("org 0x4000\n" + ln + "\n")
        with tempfile.NamedTemporaryFile("w", suffix=".asm", delete=False) as f:
            f.write("org 0x4000\n" + ln + "\n")
            name = f.name
        try:
            code = Asm().assemble(SourceFile(name))
            code.resolve()
            theirs = b"".join(d for _, d in code.encode())
        except Exception as e:
            print("selftest: %-22s the z80 package cannot assemble it (%s)" % (ln, str(e)[-50:]))
            continue
        finally:
            os.unlink(name)
        if mine[0x4000:] != theirs:
            print("selftest: %-22s ours %s theirs %s" % (ln, mine[0x4000:].hex(), theirs.hex()))
            bad += 1
    print("selftest: %d differences" % bad)
    return bad


def write_hex(path, data):
    """one byte per line for $readmemh; runs of zeros of 16 or more are skipped with an @address line (the images are mostly empty)"""
    with open(path, "w") as f:
        i = 0
        n = len(data)
        while i < n:
            if data[i] == 0:
                j = i
                while j < n and data[j] == 0:
                    j += 1
                if j - i >= 16 and j < n:
                    f.write("@%x\n" % j)
                    i = j
                    continue
                if j == n:
                    break
            f.write("%02x\n" % data[i])
            i += 1


def sha(name):
    img_fn, exp_fn, oracle = PROGRAMS[name]
    h = hashlib.sha256(img_fn())
    if exp_fn and not oracle:
        h.update(exp_fn(None))
    if name == "tape48":
        h.update("\n".join(tape_ddr_lines()).encode())
    if name == "audio128":
        h.update(("%d %d" % (beep_period_t(), AY_TP)).encode())
    return h.hexdigest()


def main(argv):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(PROG, exist_ok=True)
    if argv[1:] == ["build"]:
        for name, (img_fn, exp_fn, oracle) in PROGRAMS.items():
            img = img_fn()
            write_hex(os.path.join(PROG, name + ".hex"), img)
            open(os.path.join(PROG, name + ".sha256"), "w").write(sha(name) + "\n")
            print("%s: %d bytes" % (name, len(img)))
            if name == "tape48":
                with open(os.path.join(PROG, "tape48.ddr.hex"), "w") as f:
                    f.write("\n".join(tape_ddr_lines()) + "\n")
            if name == "audio128":
                open(os.path.join(PROG, "audio128.meta"), "w").write("BEEP_T=%d\nAY_TP=%d\n" % (beep_period_t(), AY_TP))
            if exp_fn:
                exp = exp_fn(img)
                write_hex(os.path.join(PROG, name + ".exp.hex"), exp)
                print("%s: expected result %d bytes%s" % (name, len(exp), " (oracle)" if oracle else ""))
        import zx_snap
        zx_snap.build(PROG)
        return 0
    if argv[1:] == ["--selftest"]:
        return 1 if selftest() else 0
    if argv[1:] == ["--check"]:
        bad = 0
        for name in PROGRAMS:
            p = os.path.join(PROG, name + ".sha256")
            if not os.path.exists(p) or open(p).read().strip() != sha(name):
                print("zx_progs: %s.hex was not made from the source in tools/zx_progs.py (run tools/zx_progs.py build)" % name)
                bad = 1
        import zx_snap
        bad |= zx_snap.check(PROG)
        if not bad:
            print("zx_progs: the committed programs match their sources")
        return bad
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
