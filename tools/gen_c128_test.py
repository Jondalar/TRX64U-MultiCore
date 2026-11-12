#!/usr/bin/env python3
"""T113: synthetic system ROM images for the C128 core's benches (cores/c128/sim). Our own 8502 and Z80 programs, assembled by
tools/c128asm.py and tools/zx_asm.py; no byte of any Commodore ROM. The image has the 72 KiB layout of the machine (spec 5.5):
  +0x00000  C64 BASIC + KERNAL (16 KiB; banks 0-3)
  +0x04000  C128 editor / Z80 BIOS / KERNAL (16 KiB; banks 4-7: bank 4 the editor at $C000, bank 5 the Z80 BIOS the Z80 sees at $0000, 6-7 KERNAL)
  +0x08000  C128 BASIC (32 KiB; banks 8-15, $4000-$BFFF)
  +0x10000  character ROMs (8 KiB; bank 16 = the C64's, bank 17 = the C128's)

  gen_c128_test.py <program> <out dir>   writes <out dir>/mem/c128_rom.mem (36,864 words, one hex word per line, low byte first in the word)
                                         and prints the bench arguments (+EXPECT=...) on one line
  gen_c128_test.py --list
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c128asm  # noqa: E402
import zx_asm   # noqa: E402

SIZE = 0x12000
ROM4 = 0x4000
ROM1 = 0x0000
ROM23 = 0x8000
CHR = 0x10000


def a6502(text, base):
    """assemble at the addresses the text says ($Cxxx-$Fxxx for the KERNAL window); returns {address: byte}"""
    return c128asm.assemble(text)


def blank():
    return bytearray([0xEA]) * SIZE      # NOP everywhere: a runaway program is visible in the trace


def put(img, base, win, code):
    """put the assembled bytes {address: byte} into image offset base + (address - win)"""
    for a, b in code.items():
        img[base + (a - win)] = b


def vectors(img, base, win, reset, irq=None, nmi=None):
    """the 6502 vectors of a 16 KiB window at win ($C000): they live at $FFFA.. = base + 0x3FFA.."""
    irq = reset if irq is None else irq
    nmi = reset if nmi is None else nmi
    off = base + 0x3FFA
    img[off:off + 6] = bytes([nmi & 255, nmi >> 8, reset & 255, reset >> 8, irq & 255, irq >> 8])


# Z80 stub every program starts with: the machine starts on the Z80 (MMU CPU bit = Z80); the MMU maps the Z80's $0000-$0FFF to the
# system ROM at $D000 of the editor/KERNAL ROM, i.e. image offset ROM4 + 0x1000 (bank 5; c128_mmu8722.vhd "Z80 Bios"); hand over to the 8502
# by writing $D505 (CPU bit 0 = 1, C128 mode) with OUT (C),A - the I/O cycle reaches the MMU whatever the memory configuration.
Z80_HANDOVER = """
 org 0
 di
 ld bc,$D505
 ld a,$01
 out (c),a
spin: jr spin
"""


def z80_stub(img, extra=None):
    code = zx_asm.assemble(extra if extra is not None else Z80_HANDOVER)
    img[ROM4 + 0x1000:ROM4 + 0x1000 + len(code)] = code


# ----- programs ----------------------------------------------------------------------------------------------------------------

def prog_boot():
    """S4: Z80 first, then the 8502. The 8502 leaves a signature in RAM block 0 at $0400."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$5A
        sta $0400
        lda #$A5
        sta $0401
        lda $D505          ; MMU: 40/80 sense, mode pins, CPU bit (1 = 8502)
        sta $0402
loop:   jmp loop
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0400:5a,0401:a5"


def prog_nocpubit():
    """S4 control: the Z80 never sets the CPU bit; the 8502 signature must not appear."""
    img = blank()
    z80_stub(img, " org 0\n di\nspin: jr spin\n")
    code = a6502("""
        org $E000
reset:  lda #$5A
        sta $0400
loop:   jmp loop
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0400:5a"



def prog_mmu():
    """S5: the MMU banks RAM and ROM. Results land in the common RAM at $0300 (shared by both blocks):
    $0300/$0301 = the patterns of RAM block 0 and 1 at $2000, $0302 = ROM byte at $4000 (CR=0), $0303 = RAM byte under it (CR=2),
    $0304 = ROM byte at $E100, $0305 = RAM byte under it (CR=$30, read from common RAM code), $0306 = page 0 relocated to page $20,
    $0307 = block 1 does not show block 0's pattern ($2001 written in block 0 only reads back $00 in block 1)."""
    img = blank()
    z80_stub(img)
    img[ROM23] = 0xB1
    img[ROM4 + 0x2100] = 0xC7
    rt = a6502("""
        org $0380
rt:     lda #$30
        sta $FF00           ; RAM from $C000 up: $E100 is RAM now
        lda $E100
        tax
        lda #$00
        sta $FF00
        txa
        rts
""", 0)
    rt_len = max(rt) - min(rt) + 1
    rt_bytes = bytes(rt[a] for a in range(0x0380, 0x0380 + rt_len))
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$04
        sta $D506           ; common RAM at the bottom, 1 KiB: $0000-$03FF is the same in both blocks
        ldx #0
cp:     lda rtimg,x
        sta $0380,x
        inx
        cpx #RTLEN
        bne cp
        lda #$00
        sta $FF00           ; block 0
        lda #$11
        sta $2000
        lda #$77
        sta $2001
        lda #$40
        sta $FF00           ; block 1
        lda #$22
        sta $2000
        lda $2001
        sta $0307           ; block 1's $2001: must not be block 0's
        lda #$00
        sta $FF00
        lda $2000
        sta $0300
        lda #$40
        sta $FF00
        lda $2000
        sta $0301
        lda #$00
        sta $FF00
        lda #$33
        sta $4000           ; writes go to RAM whatever the ROM says
        lda $4000
        sta $0302           ; CR=0: system ROM
        lda #$02
        sta $FF00
        lda $4000
        sta $0303           ; CR=2: RAM
        lda #$00
        sta $FF00
        lda #$44
        sta $E100
        lda $E100
        sta $0304
        jsr $0380
        sta $0305
        lda #$20
        sta $D507           ; page 0 lives at page $20
        lda #$00
        sta $D508
        lda #$5A
        sta $10             ; lands at $2010
        lda #$00
        sta $D507           ; back to page 0
        lda $2010
        sta $0306
loop:   jmp loop
rtimg:  db 0
""".replace("RTLEN", str(rt_len)), 0)
    # the routine's image follows the code: patch it in behind "rtimg"
    base = code_end = max(code) + 1
    # rtimg is the last label: its single db is at code_end-1; put the image there
    start = code_end - 1
    for i, b in enumerate(rt_bytes):
        code[start + i] = b
    code[start] = rt_bytes[0]
    # the loop above reads rtimg,x : fix the operand address to the label's real address (rtimg = start)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0300:11,0301:22,0302:b1,0303:33,0304:c7,0305:44,0306:5a,0307:00"


def prog_mmu_ctl():
    return prog_mmu()


def prog_xkeys():
    """S7: the VIC-IIe's $D02F selects the extra keyboard columns K0-K2 (low = selected); CIA 1 port B reads their rows. The bench
    holds the extra key K1 row 0 (+XK=000100); results $0300+k = what PB reads with only Kk selected."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$FF
        sta $DC02           ; port A all outputs
        sta $DC00           ; no matrix column selected
        lda #$00
        sta $DC03           ; port B inputs
        lda #$FE
        sta $D02F           ; K0 selected
        lda $DC01
        sta $0300
        lda #$FD
        sta $D02F           ; K1
        lda $DC01
        sta $0301
        lda #$FB
        sta $D02F           ; K2
        lda $DC01
        sta $0302
        lda #$FF
        sta $D02F           ; none
        lda $DC01
        sta $0303
        lda $D02F           ; the register reads back with bits 7:3 high
        sta $0304
loop:   jmp loop
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0300:ff,0301:fe,0302:ff,0303:ff,0304:ff"


def prog_fast(fastbit=1):
    """S3: the 8502 runs at 2 MHz when $D030 bit 0 is set. A loop of memory-only cycles (DEX/BNE, 256 times) is timed with CIA 1's timer A
    (counts the 1 MHz bus cycle) at 1 MHz ($0300/$0301) and with $D030 = fastbit ($0302/$0303); $0308 = 1 when elapsed1 is within 64
    ticks of twice elapsed2."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$FF
        sta $DC04
        sta $DC05
        lda #$11
        sta $DC0E           ; timer A: force load, start, counts phi2
        jsr meas
        lda $F4
        sta $0300
        lda $F5
        sta $0301
        lda #FASTBIT
        sta $D030
        jsr meas
        lda #$00
        sta $D030
        lda $F4
        sta $0302
        lda $F5
        sta $0303
        lda $0302
        asl
        sta $F6
        lda $0303
        rol
        sta $F7
        sec
        lda $0300
        sbc $F6
        sta $F8
        lda $0301
        sbc $F7
        sta $F9
        lda #$00
        sta $0308
        lda $F9
        bne neg
        lda $F8
        cmp #64
        bcs done
        lda #$01
        sta $0308
        jmp done
neg:    cmp #$FF
        bne done
        lda $F8
        cmp #$C0
        bcc done
        lda #$01
        sta $0308
done:   jmp done
meas:   lda $DC04
        sta $F0
        lda $DC05
        sta $F1
        ldx #0
ml:     dex
        bne ml
        lda $DC04
        sta $F2
        lda $DC05
        sta $F3
        sec
        lda $F0
        sbc $F2
        sta $F4
        lda $F1
        sbc $F3
        sta $F5
        rts
""".replace("FASTBIT", str(fastbit)), 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0308:01"


def prog_fast0():
    return prog_fast(0)


def prog_c64mode():
    """S6: GO64 (MMU $D505 bit 6) gives the C64: BASIC / KERNAL / character ROM through $01 as the C64's PLA says, the VIC at $D020,
    and $D030 bit 0 does nothing (no 2 MHz in C64 mode). Results in RAM block 0: $0300 BASIC byte at $A000 (ROM), $0301 RAM under it,
    $0302 character ROM byte at $D000 ($01=$33), $0303 border colour register read back, $0304 = 1 when the timed loop is not faster
    with $D030 = 1."""
    img = blank()
    z80_stub(img)
    img[ROM1 + 0x0000] = 0xA5          # C64 BASIC, $A000
    img[CHR + 0x0000] = 0x3C           # C64 character ROM bank 16, first byte
    rt = a6502("""
        org $0380
        lda #$71
        sta $D505           ; C64 mode on the 8502
        jmp $E000
""", 0)
    rt_len = max(rt) - 0x0380 + 1
    rt_bytes = bytes(rt[a] for a in range(0x0380, 0x0380 + rt_len))
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        ldx #0
cp:     lda rtimg,x
        sta $0380,x
        inx
        cpx #RTLEN
        bne cp
        jmp $0380
rtimg:  db 0
""".replace("RTLEN", str(rt_len)), 0)
    start = max(code)             # rtimg
    for i, b in enumerate(rt_bytes):
        code[start + i] = b
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    c64 = a6502("""
        org $E000
c64:    sei
        cld
        ldx #$FF
        txs
        lda #$2F
        sta $00
        lda #$37
        sta $01
        lda $A000
        sta $0300
        lda #$77
        sta $A000
        lda #$36
        sta $01
        lda $A000
        sta $0301
        lda #$33
        sta $01
        lda $D000
        sta $0302
        lda #$37
        sta $01
        lda #$05
        sta $D020
        lda $D020
        and #$0F
        sta $0303
        lda #$FF
        sta $DC04
        sta $DC05
        lda #$11
        sta $DC0E
        jsr meas
        lda $F4
        sta $0310
        lda $F5
        sta $0311
        lda #$01
        sta $D030
        jsr meas
        lda #$00
        sta $D030
        sec
        lda $0310
        sbc $F4
        sta $F6
        lda $0311
        sbc $F5
        sta $F7
        lda #$00
        sta $0304
        lda $F7
        bne neg
        lda $F6
        cmp #64
        bcs done
        lda #$01
        sta $0304
        jmp done
neg:    cmp #$FF
        bne done
        lda $F6
        cmp #$C0
        bcc done
        lda #$01
        sta $0304
done:   jmp done
meas:   lda $DC04
        sta $F0
        lda $DC05
        sta $F1
        ldx #0
ml:     dex
        bne ml
        lda $DC04
        sta $F2
        lda $DC05
        sta $F3
        sec
        lda $F0
        sbc $F2
        sta $F4
        lda $F1
        sbc $F3
        sta $F5
        rts
""", 0)
    # the C64 KERNAL window $E000-$FFFF is the second half of the C64 ROM block (offset 0x2000); BASIC is the first half
    for a, b in c64.items():
        img[ROM1 + 0x2000 + (a - 0xE000)] = b
    return img, "0300:a5,0301:77,0302:3c,0303:05,0304:01"


def prog_pause():
    """S12: a program that counts in RAM ($0300/$0301) and copies CIA 1's timer A ($0302/$0303) and the raster register ($0304) while it
    runs; the bench compares a run with a hold in the middle with one without (cycle for cycle)."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$FF
        sta $DC04
        sta $DC05
        lda #$11
        sta $DC0E
loop:   inc $0300
        bne l2
        inc $0301
l2:     lda $DC04
        sta $0302
        lda $DC05
        sta $0303
        lda $D012
        sta $0304
        jmp loop
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0300:00"


def prog_iec():
    """The serial bus through CIA 2 ($DD00): ATN, CLK and DATA as the KERNAL's bus routines use them. The bench has one device on the bus that pulls
    DATA low while ATN is asserted (a drive's answer to ATN). $0300 = $DD00 & $C0 idle, $0301 with ATN asserted (DATA read back low), $0302 with ATN
    and CLK asserted (both read back low), $0303 released again."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$3F
        sta $DD02           ; PA0-5 outputs (bits 3,4,5: ATN, CLK, DATA out), 6,7 inputs (CLK, DATA in)
        lda #$00
        sta $DD00
        jsr wait
        lda $DD00
        and #$C0
        sta $0300
        lda #$08
        sta $DD00           ; ATN asserted (the output bit pulls the line low)
        jsr wait
        lda $DD00
        and #$C0
        sta $0301
        lda #$18
        sta $DD00           ; ATN and CLK
        jsr wait
        lda $DD00
        and #$C0
        sta $0302
        lda #$00
        sta $DD00
        jsr wait
        lda $DD00
        and #$C0
        sta $0303
loop:   jmp loop
wait:   ldx #100
w1:     dex
        bne w1
        rts
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0300:c0,0301:40,0302:00,0303:c0"


Z80_BACK = """
 org 0
 di
 ld bc,$D505
 ld a,$01
 out (c),a
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 ld a,$A7
 ld ($2000),a
 ld bc,$D020
 ld a,$05
 out (c),a
 in a,(c)
 and $0F
 ld ($2001),a
 ld a,$0B
 ld ($1000),a
 ld a,($1000)
 and $0F
 ld ($2002),a
 ld a,$3C
 ld ($2003),a
spin: jr spin
"""


def prog_z80back(flip=True):
    """The Z80 hands over to the 8502 (OUT to $D505) and the 8502 hands back: the Z80 goes on after its OUT, as the C128's CP/M start does. The Z80
    then writes RAM ($2000), reaches the VIC by IN/OUT ($D020 read back at $2001), uses the colour RAM at $1000 that only the Z80 sees there
    ($2002). Control: the 8502 never hands back."""
    img = blank()
    z80_stub(img, Z80_BACK)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$5A
        sta $0400
        lda #$00
        FLIPSTA
spin:   jmp spin
""".replace("FLIPSTA", "sta $D505" if flip else "nop"), 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0400:5a,2000:a7,2001:05,2002:0b,2003:3c"


def prog_z80noback():
    return prog_z80back(False)


Z80_SUM = """
 org 0
 di
 ld hl,$0000
 ld bc,$0200
 ld e,0
sum: ld a,(hl)
 add a,e
 ld e,a
 inc hl
 dec bc
 ld a,b
 or c
 jr nz,sum
 ld a,($2010)
 inc a
 ld ($2010),a
 ld l,a
 ld h,$20
 ld (hl),e
 ld bc,$D505
 ld a,$01
 out (c),a
spin: jr spin
"""


def prog_z80ba():
    """T113 5.6 (the Z80 and the VIC's bus cycles): every start the Z80 adds up the first 512 bytes of its BIOS window (about 24,000 cycles: far more
    than a badline's) into $2000 + start number, then hands over to the 8502, which switches the VIC's display on ($D011 = $1B, DEN) and idles. The bench
    resets the machine (which does not reset the VIC) in the idle: the second start runs with badlines. Both sums must be the ROM's. Control:
    the Z80 ignores BA (C128_MUT_Z80NOBA) and adds up the VIC's screen bytes in the badline cycles."""
    img = blank()
    z80_stub(img, Z80_SUM)
    base = ROM4 + 0x1000
    ssum = sum(img[base:base + 0x200]) & 0xFF
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$1B
        sta $D011
idle:   jmp idle
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "2010:02,2001:%02x,2002:%02x" % (ssum, ssum)


def prog_z80vdc():
    """T140 5.19 (issue #67, causes 1/2 of 5.19 item 19): the Z80 writes 64 bytes each into the VDC's RAM in three ways with IN/OUT (the I/O cycles the CP/M BIOS
    uses; a memory access to $D6xx in Z80 mode reaches RAM, not the VDC), the VIC's display on (badlines hold the Z80: DEN is set by the Z80 itself) and the VDC set up
    for 80 x 25 text: A = polls the status bit 7 before every access (the KERNAL's way), B = polls once, then writes R31 back to back (a software race: informational, the VDC takes one write at a time), C = sets R18/R19 and R31
    for every cell, polling before every access (a BIOS that addresses every cell). D = reads the 64 bytes of A back with IN and writes them to $4300. Pattern value $40 + i at $4000 / $4100 / $4200 / $4300 + i. The bench (+VRAM) prints the VDC RAM at the end; the probe
    (+VDCPROBE) counts the Z80's writes to $D6xx against the VDC's strobes."""
    img = blank()
    regs = [(0, 0x7E), (1, 0x50), (2, 0x66), (3, 0x49), (4, 0x20), (5, 0xE0), (6, 0x19), (7, 0x1D), (8, 0), (9, 0x07), (10, 0x20), (11, 0x07),
            (12, 0), (13, 0), (14, 0), (15, 0), (20, 0x08), (21, 0), (22, 0x78), (23, 0x08), (24, 0x20), (25, 0x40), (26, 0xF0), (27, 0),
            (28, 0x30), (29, 0x07)]
    t = [" org 0", " di", " ld sp,$2000", " ld bc,$D011", " ld a,$1B", " out (c),a"]

    def sel(reg):
        t.extend([" ld bc,$D600", " ld a,%d" % reg, " out (c),a"])

    def dat(val):
        t.extend([" ld bc,$D601", " ld a,%d" % val, " out (c),a"])

    def wr(reg, val):
        t.append(" call wait")
        sel(reg)
        t.append(" call wait")
        dat(val)
    for r, v in regs:
        wr(r, v)
    for base, mode in ((0x4000, "A"), (0x4100, "B"), (0x4200, "C")):
        if mode in "AB":
            wr(18, base >> 8)
            wr(19, base & 255)
            t.append(" call wait")
            sel(31)
            t.extend([" call wait", " ld hl,pat", " ld e,64", " ld bc,$D601"])
            t.append("t%s:" % mode)
            if mode == "A":
                t.extend([" call wait", " ld bc,$D601"])
            t.extend([" ld a,(hl)", " out (c),a", " inc hl", " dec e", " jr nz,t%s" % mode])
        else:
            t.extend([" call wait", " ld hl,pat", " ld e,64", " ld d,0", "tC:"])
            for rs_, src in ((0, "18"), (1, "%d" % (base >> 8)), (0, "19"), (1, "d"), (0, "31"), (1, "(hl)")):
                t.extend([" call wait", " ld bc,$D60%d" % rs_, " ld a,%s" % src, " out (c),a"])
            t.extend([" inc hl", " inc d", " dec e", " jr nz,tC"])
        t.extend([" call wait", " call wait"])
    # D: read 64 bytes back from $4000 with IN (R31 reads: UA must move once per read) into Z80 RAM at $3000, then write them with polling to $4300
    wr(18, 0x40)
    wr(19, 0)
    t.append(" call wait")
    sel(31)
    t.extend([" call wait", " ld hl,$3000", " ld e,64", "tD:", " call wait", " ld bc,$D601", " in a,(c)", " ld (hl),a", " inc hl", " dec e", " jr nz,tD"])
    wr(18, 0x43)
    wr(19, 0)
    t.append(" call wait")
    sel(31)
    t.extend([" call wait", " ld hl,$3000", " ld e,64", "tE:", " call wait", " ld bc,$D601", " ld a,(hl)", " out (c),a", " inc hl", " dec e", " jr nz,tE"])
    t.extend([" call wait", " call wait"])
    t.extend(["done: jr done", "wait: ld bc,$D600", " in a,(c)", " rla", " jr nc,wait", " ret", "pat:"])
    t.append(" db " + ",".join(str(0x40 + i) for i in range(64)))
    code = zx_asm.assemble("\n".join(t))
    assert len(code) < 0x1000
    img[ROM4 + 0x1000:ROM4 + 0x1000 + len(code)] = code
    return img, ""


def prog_chrshow():
    """T113 5.7 (the character store): C128 mode, the VIC shows a text screen of all 256 screen codes in the C128 character ROM (set at $1000 of the
    VIC's bank: $D018 = $14, the CPU port with CHAREN low), colours 1 on 6; then idle. The bench compares the hash of one frame of the pixel stream
    with the hash the same program gave with the character ROM read from the ROM block (before 5.7): the VIC takes its glyph bytes at the same moments."""
    img = blank()
    z80_stub(img)
    for i in range(0x2000):                       # the character ROMs: bytes of our own that tell every address apart (the blank image is all $EA)
        img[CHR + i] = (i * 37 + (i >> 8) * 11 + (i >> 12) * 101 + 5) & 0xFF
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$2F
        sta $00
        lda #$03
        sta $01
        ldx #$00
f1:     txa
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $0700,x
        lda #$01
        sta $D800,x
        sta $D900,x
        sta $DA00,x
        sta $DB00,x
        inx
        bne f1
        lda #$06
        sta $D021
        sta $D020
        lda #$14
        sta $D018
        lda #$1B
        sta $D011
idle:   jmp idle
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0400:00,04ff:ff"


def prog_romwalk():
    """T113 5.6 (frame phase): wait about six frames, then read the system ROM $4000-$7FFF once, one byte per 16 byte line (1,024 lines: every
    read is a miss of the ROM line cache, the machine is held for each), then signature $5A at $0400 and idle in a cached loop. With the ROM cache the
    VIC loses frame phase for every miss; the bench reads the frame starts."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$00
        sta $0400
        ldy #$60
d1:     ldx #$00
d2:     dex
        bne d2
        dey
        bne d1
        lda #$00
        sta $FB
        lda #$40
        sta $FC
w0:     ldy #$00
w1:     lda ($FB),y
        tya
        clc
        adc #$10
        tay
        bne w1
        inc $FC
        lda $FC
        cmp #$80
        bne w0
        lda #$5A
        sta $0400
idle:   jmp idle
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0400:5a"


def prog_coldprog():
    """T113 5.6 (cold start): every start counts in $0410 (RAM keeps its bits through the cold start, the RAM window clears it) and stores what the
    chips read at the start in slot n of $0420 (CIA 1 DDRA), $0430 (VIC border colour), $0440 (the MMU's $D506) and $0500 + 64 n (the VIC's registers
    $D000-$D02E), then sets them all to something else: CIA 1 DDRA $FF, VIC registers $55, $D506 $04. After the bench's cold start the second start must
    read the power-on values again (the first start's): all chips, the MMU and the VIC's registers were reset. Control: the request never arrives."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        inc $0410
        ldx $0410
        lda $DC02
        sta $0420,x
        lda $D020
        sta $0430,x
        lda $D506
        sta $0440,x
        txa
        asl
        asl
        asl
        asl
        asl
        asl
        sta $FB
        lda #$05
        sta $FC
        ldy #$2E
cp:     lda $D000,y
        sta ($FB),y
        dey
        bpl cp
        lda #$FF
        sta $DC02
        lda #$55
        ldy #$2E
wv:     sta $D000,y
        dey
        bpl wv
        lda #$04
        sta $D506
idle:   jmp idle
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0410:02"

def prog_vicstress():
    """T140 D25 (RAM in the DDR2, the VIC's guarantee): C128 mode, bitmap mode with eight sprites (expanded, multicolour, spread over the screen), the video
    matrix, the bitmap and the sprite blocks of VIC bank 0 ($0400, $2000, $0C00) and of bank 1 ($4400, $6000, $4C00) filled with different patterns, so every
    line is a badline with c-accesses, 40 g-accesses and sprite p/s-accesses. After the setup a known loop is timed with CIA 1's timer A twice (pass 1 may
    run on cache misses, pass 2 must not: the count of pass 2 is stored at $0320/$0330) and the program polls the raster line forever, switching CIA 2's
    bank bits to bank 1 at line 96 and back to bank 0 at line 176, in the middle of the frame. Signature $A5 at $0301 once the setup is done."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$2F
        sta $00
        lda #$07
        sta $01
        lda #$01
        sta $D030
        lda #$3F
        sta $DD02
        lda #$03
        sta $DD00
        ldx #$00
f1:     txa
        eor #$5A
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $0700,x
        txa
        eor #$A5
        sta $4400,x
        sta $4500,x
        sta $4600,x
        sta $4700,x
        txa
        asl
        sta $0C00,x
        sta $0D00,x
        eor #$FF
        sta $0E00,x
        sta $0F00,x
        txa
        lsr
        sta $4C00,x
        eor #$33
        sta $4D00,x
        sta $4E00,x
        sta $4F00,x
        lda #$0F
        sta $D800,x
        lda #$03
        sta $D900,x
        lda #$0A
        sta $DA00,x
        lda #$05
        sta $DB00,x
        inx
        bne f1
        lda #$00
        sta $FB
        lda #$20
        sta $FC
fb:     ldy #$00
fb1:    tya
        eor $FC
        asl
        eor #$3C
        sta ($FB),y
        iny
        bne fb1
        inc $FC
        lda $FC
        cmp #$40
        bne fb
        lda #$60
        sta $FC
fc:     ldy #$00
fc1:    tya
        eor $FC
        eor #$C3
        sta ($FB),y
        iny
        bne fc1
        inc $FC
        lda $FC
        cmp #$80
        bne fc
        ldx #$07
sp:     txa
        clc
        adc #$30
        sta $07F8,x
        sta $47F8,x
        txa
        asl
        tay
        txa
        asl
        asl
        asl
        clc
        adc #$20
        sta $D000,y
        txa
        asl
        asl
        asl
        asl
        clc
        adc #$40
        sta $D001,y
        txa
        clc
        adc #$01
        sta $D027,x
        dex
        bpl sp
        lda #$00
        sta $D030
        lda #$FF
        sta $D015
        lda #$55
        sta $D017
        lda #$0F
        sta $D01C
        lda #$02
        sta $D025
        lda #$07
        sta $D026
        lda #$06
        sta $D021
        sta $D020
        lda #$18
        sta $D018
        lda #$3B
        sta $D011
        lda #$A5
        sta $0301
        lda #$00
        sta $FD
m0:     lda #$00
        sta $DC0E
        lda #$FF
        sta $DC04
        sta $DC05
        lda #$11
        sta $DC0E
        ldx #$32
ml:     inc $0300
        lda $0310
        sta $0311
        jsr sub
        dex
        bne ml
        lda #$00
        sta $DC0E
        lda $DC04
        ldy $FD
        sta $0320,y
        lda $DC05
        sta $0330,y
        inc $FD
        lda $FD
        cmp #$02
        bne m0
lp:     lda $D012
        cmp #$60
        bne lp
        lda #$02
        sta $DD00
lp2:    lda $D012
        cmp #$B0
        bne lp2
        lda #$03
        sta $DD00
        jmp lp
sub:    rts
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0301:a5,0400:5a"


def prog_vicblk(block, mmu=False):
    """c128-vicglitch: what the VIC shows of RAM block 1 (MMU RCR $D506 bit 6). The same program for both blocks: it fills the video matrix ($4400), the bitmap
    ($6000) and the colour RAM of VIC bank 1 in RAM block `block` (CPU through $FF00 bit 6), then points the VIC at that block (RCR $45 / $05), multicolour
    bitmap mode, and polls forever. Block 0 and block 1 hold the same bytes at the same addresses, so the VIC's pixel stream must be the same for both; the code
    runs from the common bottom 4 KiB ($0B00, RCR bit 2) because $FF00 = $7E/$3E has RAM, not the ROM, at $E000. Signature $A5 at $0301.
    mmu (program vicmmu, issue #19): instead of the idle loop the CPU reads the MMU's configuration register $FF00 (as the KERNAL's IRQ entry at $FF17 and its
    far-call routines do) and rewrites the bytes of the bitmap, the video matrix and the colour RAM the VIC is showing with their own values, so the picture must
    not change from frame to frame and every byte the VIC fetches must be the byte in RAM (tb_c128 +VICBUS)."""
    img = blank()
    z80_stub(img)
    cr = 0x7E if block else 0x3E
    rcr = 0x45 if block else 0x05
    boot = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$05
        sta $D506
        ldx #$00
cp:     lda $E100,x
        sta $0B00,x
        inx
        bne cp
        jmp $0B00
""", 0)
    body = a6502("""
        org $0B00
        lda #$%02X
        sta $FF00
        lda #$3F
        sta $DD02
        lda #$02
        sta $DD00
        ldx #$00
f1:     txa
        eor #$5A
        sta $4400,x
        sta $4500,x
        sta $4600,x
        sta $4700,x
        lda #$0F
        sta $D800,x
        lda #$03
        sta $D900,x
        lda #$0A
        sta $DA00,x
        lda #$05
        sta $DB00,x
        inx
        bne f1
        lda #$00
        sta $FB
        lda #$60
        sta $FC
fb:     ldy #$00
fb1:    tya
        eor $FC
        asl
        eor #$3C
        sta ($FB),y
        iny
        bne fb1
        inc $FC
        lda $FC
        cmp #$80
        bne fb
        lda #$06
        sta $D021
        sta $D020
        lda #$18
        sta $D018
        lda #$18
        sta $D016
        lda #$3B
        sta $D011
        lda #$%02X
        sta $D506
        lda #$A5
        sta $0301
""" % (cr, rcr) + ("""        ldx #$00
lp:     lda $FF00
        lda $6000,x
        sta $6000,x
        lda $4400,x
        sta $4400,x
        lda $D800,x
        sta $D800,x
        lda $FF00
        inx
        jmp lp
""" if mmu else """lp:     jmp lp
"""), 0)
    put(img, ROM4, 0xC000, boot)
    for a, b in body.items():
        img[ROM4 + (0xE100 + a - 0x0B00 - 0xC000)] = b
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0301:a5"


def prog_vicrom(mode):
    """c128-vicglitch (issue #21, @@@@ flicker on a text screen while the disk is busy): a static 40 column text screen at $0400 and an endless loop that does what the
    KERNAL's serial bus code does around every byte (JSR $E573: $D015 = 0 and $D030 = 0; $E59F puts them back) and, to keep the ROM line cache missing, reads
    two ROM bytes per 16 byte line, line after line through the BASIC ROM ($4000-$BFFF, visible after reset: 2048 lines against a cache of 512, every line a miss). mode 0: only the ROM reads; mode 1: also the $D015/$D030 writes.
    Every frame of the VIC's pixel stream must be the same (+FRAMEHASH). Signature $A5 at $0301."""
    img = blank()
    z80_stub(img)
    code = a6502("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$14
        sta $D018
        ldx #$00
f1:     txa
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $0700,x
        lda #$01
        sta $D800,x
        sta $D900,x
        sta $DA00,x
        sta $DB00,x
        inx
        bne f1
        lda #$06
        sta $D021
        lda #$A5
        sta $0301
        lda #$00
        sta $FB
        lda #$40
        sta $FC
lp:     ldy #$00
        lda ($FB),y
        ldy #$08
        lda ($FB),y
""" + ("""        lda #$00
        sta $D015
        sta $D030
""" if mode else "") + """        lda $FB
        clc
        adc #$10
        sta $FB
        bcc lp
        inc $FC
        lda $FC
        cmp #$C0
        bcc lp
        lda #$40
        sta $FC
        jmp lp
""", 0)
    put(img, ROM4, 0xC000, code)
    vectors(img, ROM4, 0xC000, 0xE000)
    return img, "0301:a5"


PROGS = {"vicrom0": lambda: prog_vicrom(0), "vicrom1": lambda: prog_vicrom(1), "vicblk0": lambda: prog_vicblk(0), "vicblk1": lambda: prog_vicblk(1), "vicmmu": lambda: prog_vicblk(0, True), "vicstress": prog_vicstress, "chrshow": prog_chrshow, "z80ba": prog_z80ba, "z80vdc": prog_z80vdc, "romwalk": prog_romwalk, "coldprog": prog_coldprog, "z80back": prog_z80back, "z80noback": prog_z80noback, "iec": prog_iec, "pause": prog_pause, "boot": prog_boot, "nocpubit": prog_nocpubit, "mmu": prog_mmu, "xkeys": prog_xkeys, "fast": prog_fast, "fast0": prog_fast0, "c64mode": prog_c64mode}


def write_mem(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        for i in range(0, SIZE, 2):
            f.write("%02x%02x\n" % (img[i + 1], img[i]))


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--list":
        print(" ".join(sorted(PROGS)))
        return
    if len(sys.argv) != 3 or sys.argv[1] not in PROGS:
        sys.exit(__doc__)
    img, expect = PROGS[sys.argv[1]]()
    assert len(img) == SIZE
    write_mem(img, os.path.join(sys.argv[2], "mem", "c128_rom.mem"))
    print(expect)


if __name__ == "__main__":
    main()
