#!/usr/bin/env python3
"""T92: the Z80 test programs of the CPC core's benches (cores/cpc/sim/prog), written for this project (our own source, no third-party code or
ROM): they run from the lower ROM area ($0000, the OS ROM's place; the benches put the image there) with the ROMs enabled as after a reset,
program the CRTC and the Gate Array themselves and write results into RAM at $9000 (bank 2 of the base RAM, which the ROMs do not cover), ending
with a spin loop after storing $A5 at $9FFF. Programs shared with the Spectrum benches (z80s: the ZEXALL-style instruction test) are
tools/zx_progs.py's; nothing is copied.

  tools/cpc_progs.py build      assemble every program into cores/cpc/sim/prog/<name>.hex (needs nothing but python: zx_asm.py is ours)
  tools/cpc_progs.py ddr <file> the disk image of the bench as DDR2 words (128 bit a line)
  tools/cpc_progs.py ddr40 <file> the same for a blank 40 track DSK as the app's `Create blank DSK` writes it (newdisk.cc), for the manual rom_cat bench
  tools/cpc_progs.py --check    the committed .hex files are the ones made from the sources in this file (a SHA-256 per program)
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import zx_asm  # noqa: E402

ROOT = os.path.dirname(HERE)
PROG = os.path.join(ROOT, "cores/cpc/sim/prog")

# the CRTC values of the OS for a 50 Hz screen: 64 characters of 1 us, 40 displayed, sync at 46 (3.5 us wide as the Gate Array makes it), 39
# character rows of 8 lines = 312 lines, 25 rows displayed, vertical sync at row 30, screen at $C000 (R12 = $30)
CRTC_STD = [63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0]

CRTC_INIT = """\
; the CRTC: for each of R0..R13 select it ($BC) and write its value ($BD)
crtcinit:
        ld hl, crtctab
        ld e, 0
ci1:    ld b, 0xBC
        out (c), e
        ld a, (hl)
        ld b, 0xBD
        out (c), a
        inc hl
        inc e
        ld a, e
        cp 14
        jr nz, ci1
        ret
crtctab: db %(crtc)s
""" % dict(crtc=", ".join(str(x) for x in CRTC_STD))

# a Gate Array write: $7Fxx, the value in A. pen select = pen (0-15, $10 border), ink = $40 | hardware ink (0-26 in the GA's own numbering)
GA_HELP = """\
; GA write: A = value
gaout:  ld b, 0x7F
        out (c), a
        ret
"""


def build_z80_boot(body):
    return "        org 0\n        di\n        jp start\n        org 0x66\n        retn\n" + body


INIT_CLK = build_z80_boot("""\
start:  ld sp, 0xBFF0
        ld c, 0
        call crtcinit
        ld a, 0x80
        call gaout
        ld a, 0x00
        call gaout
        ld a, 0x4B
        call gaout
        ld a, 0x10
        call gaout
        ld a, 0x44
        call gaout
        ld a, 0xA5
        ld (0x9FFF), a
spin:   inc hl
        jr spin
""") + GA_HELP + CRTC_INIT

# ------------------------------------------------------------------------------------------------ page (S5)
# The 6128's RAM configurations ($7Fxx with bits 7:6 = 11, bits 2:0 the configuration) and the upper ROM select ($DFxx). The routine runs
# from $8000 (bank 2 is in every configuration) with both ROMs off. Step 1 writes a marker 0x30 + n into the first byte of every 16 KB bank n
# (through a configuration that shows it; at offset $2000 of the slot, clear of the routine and the results), step 2 reads that byte of each of the four slots under each configuration into $9000 + 4 x
# configuration + slot. Step 3 selects upper ROM 0, 7 and 2 (absent: shows ROM 0) and reads $C000 into $9020 + n.
PAGE_CFG = [  # configuration -> the bank in slot 0, 1, 2, 3 (CPC 6128 hardware fact)
    (0, 1, 2, 3), (0, 1, 2, 7), (4, 5, 6, 7), (0, 3, 2, 7), (0, 4, 2, 3), (0, 5, 2, 3), (0, 6, 2, 3), (0, 7, 2, 3)]

PAGE_BODY = """\
        org 0x8000
rout:   ld bc, 0x7F8C
        ld a, 0x8C
        out (c), a                  ; both ROMs off, mode 0
        ; step 1: the markers. (config, slot address) per bank: 0:C0/2000 1:C0/6000 2:C0/A000 3:C0/E000 4:C4/6000 5:C5/6000 6:C6/6000 7:C7/6000
        ld ix, mtab
        ld b, 8
s1:     push bc
        ld a, (ix+0)
        ld bc, 0x7F00
        out (c), a
        ld h, (ix+1)
        ld l, 0
        ld a, (ix+2)
        ld (hl), a
        inc ix
        inc ix
        inc ix
        pop bc
        djnz s1
        ; bank 6 is at $8000 in configuration C2: the routine has to be there as well (the same bytes at the same addresses). Under C6 bank 6 is at
        ; $4000: copy it there. The results of configuration C2 are stored into bank 6 and fetched back after the loop.
        ld bc, 0x7F00
        ld a, 0xC6
        out (c), a
        ld hl, 0x8000
        ld de, 0x4000
        ld bc, rend-0x8000
        ldir
        ld bc, 0x7F00
        ld a, 0xC0
        out (c), a
        ; step 2: read each slot under each configuration
        ld hl, 0x9000
        ld d, 0xC0
s2:     ld bc, 0x7F00
        out (c), d
        ld a, (0x2000)
        ld (hl), a
        inc hl
        ld a, (0x6000)
        ld (hl), a
        inc hl
        ld a, (0xA000)
        ld (hl), a
        inc hl
        ld a, (0xE000)
        ld (hl), a
        inc hl
        inc d
        ld a, d
        cp 0xC8
        jr nz, s2
        ld bc, 0x7F00
        ld a, 0xC6
        out (c), a
        ld hl, 0x5008
        ld de, 0x9008
        ld bc, 4
        ldir                        ; the four bytes configuration C2 stored in bank 6
        ; step 3: the upper ROMs (config 0 again, upper ROM on, lower off)
        ld bc, 0x7F00
        ld a, 0xC0
        out (c), a
        ld a, 0x84
        out (c), a
        ld hl, 0x9020
        ld e, 0
s3:     ld bc, 0xDF00
        out (c), e
        ld a, (0xC000)
        ld (hl), a
        inc hl
        ld a, e
        cp 0
        jr nz, nx0
        ld e, 7
        jr s3cont
nx0:    cp 7
        jr nz, nx7
        ld e, 2
        jr s3cont
nx7:    jr s3end
s3cont: jr s3
s3end:  ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin
mtab:   db 0xC0, 0x20, 0x30, 0xC0, 0x60, 0x31, 0xC0, 0xA0, 0x32, 0xC0, 0xE0, 0x33
        db 0xC4, 0x60, 0x34, 0xC5, 0x60, 0x35, 0xC6, 0x60, 0x36, 0xC7, 0x60, 0x37
rend:
"""




def image_page():
    body = assemble(PAGE_BODY)[0x8000:]
    boot = ("        org 0\n        di\n        jp start\n        org 0x66\n        retn\nstart:  ld sp, 0xBFF0\n        ld hl, rom_rout\n        ld de, 0x8000\n"
            "        ld bc, %d\n        ldir\n        jp 0x8000\nrom_rout: db %s\n" % (len(body), ", ".join(str(x) for x in body)))
    return assemble(boot)


def exp_page(model=2):
    """model 2 = 6128 (128 KB, AMSDOS), 0 = 464 and 1 = 664 (64 KB: the RAM configurations do nothing; the 464 has no upper ROM 7).
    The bench writes the markers $B1 (BASIC) and $B7 (AMSDOS) at the start of the upper ROMs."""
    e = bytearray(256)
    for cfg in range(8):
        for slot in range(4):
            if model == 2:
                e[4 * cfg + slot] = 0x30 + PAGE_CFG[cfg][slot]
            else:                         # the four writes meant for banks 4-7 all land in the one RAM at $6000: the last one stays
                e[4 * cfg + slot] = 0x37 if slot == 1 else 0x30 + slot
    if model != 2:                    # the fetch-back of C2's bytes from $5008 copies zeros over them (nothing was written there)
        e[8:12] = bytes(4)
    e[0x20] = 0xB1
    e[0x21] = 0xB7 if model != 0 else 0xB1
    e[0x22] = 0xB1
    return bytes(e)


# ------------------------------------------------------------------------------------------------ audio (S8)
# The AY through the PPI (port A = the data, port C bits 7:6 = BDIR / BC1). Phases (the byte at $9000 names the phase): 1 an AY tone on A
# (period 100 chip clocks), 2 noise on A, 3 silence. A chip clock is 1 MHz (the Gate Array's CCLK).
AY_TP_CPC = 100


def psg_write_asm(reg, val):
    return ("        ld bc, 0xF400 + %d\n        out (c), c\n        ld bc, 0xF6C0\n        out (c), c\n        ld bc, 0xF600\n        out (c), c\n"
            "        ld bc, 0xF400 + %d\n        out (c), c\n        ld bc, 0xF680\n        out (c), c\n        ld bc, 0xF600\n        out (c), c\n" % (reg, val))


def wait_asm(label, frames):
    it = frames * 19968 // 7                       # 7 us per iteration of dec bc / ld a,b / or c / jp nz
    return "        ld bc, %d\n%s:  dec bc\n        ld a, b\n        or c\n        jp nz, %s\n" % (it, label, label)


def audio_asm():
    t = """\
        org 0
        di
        jp start
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld bc, 0xF782
        ld a, 0x82
        out (c), a
        ld a, 1
        ld (0x9000), a
"""
    t += psg_write_asm(0, AY_TP_CPC & 255) + psg_write_asm(1, AY_TP_CPC >> 8) + psg_write_asm(7, 0x3E) + psg_write_asm(8, 15)
    t += wait_asm("w1", 16)
    t += "        ld a, 2\n        ld (0x9000), a\n"
    t += psg_write_asm(6, 8) + psg_write_asm(7, 0x37)
    t += wait_asm("w2", 16)
    t += "        ld a, 3\n        ld (0x9000), a\n"
    t += psg_write_asm(8, 0)
    t += wait_asm("w3", 20)
    t += "        ld a, 0xA5\n        ld (0x9FFF), a\nspin:   jr spin\n"
    return t


# ------------------------------------------------------------------------------------------------ keys (S9)
KEYS_CPC = """\
        org 0
        di
        jp start
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld bc, 0xF782
        ld a, 0x82
        out (c), a                  ; port A out, B in, C out
        ld bc, 0xF407
        out (c), c                  ; port A = 7
        ld bc, 0xF6C0
        out (c), c                  ; PSG: select register 7
        ld bc, 0xF600
        out (c), c
        ld bc, 0xF43F
        out (c), c                  ; port A = $3F: both I/O ports are inputs, noise and tone off
        ld bc, 0xF680
        out (c), c                  ; PSG: write
        ld bc, 0xF600
        out (c), c
        ld bc, 0xF40E
        out (c), c                  ; port A = 14
        ld bc, 0xF6C0
        out (c), c                  ; PSG: select register 14
        ld bc, 0xF600
        out (c), c
        ld bc, 0xF792
        ld a, 0x92
        out (c), a                  ; port A in, B in, C out
loop:   ld d, 0
        ld hl, 0x9000
rl:     ld a, 0x40
        or d
        ld b, 0xF6
        ld c, a
        out (c), c                  ; PSG read, line d
        ld b, 0xF4
        in a, (c)
        ld (hl), a
        inc hl
        inc d
        ld a, d
        cp 10
        jr nz, rl
        ld a, (0x9010)
        inc a
        ld (0x9010), a
        jr loop
"""


def image_audio_cpc():
    return assemble(audio_asm())


# ------------------------------------------------------------------------------------------------ tape (S10)
# The loader of the Spectrum bench (tools/zx_progs.py TAPE48, ours too) on the CPC's tape input (PPI port B bit 7) with the motor on port C bit 4
# (PPI control byte $09 = set bit 4, $08 = reset it). One polling count is 14 us (inc b 1, ret z 2, ld a,n 2, in a,(n) 3, and 2, xor c 1, jr z 3), so
# the pulses are: pilot 619 us = 44 counts, sync 190 / 210 us = 14 / 15, a zero bit (a pair) 2 x 17, a one bit 2 x 35 (limits: 28 and 75 for the leader,
# 52 for the sum of a pair).
# Phases for the motor check: $9100 = 1 motor on for 2 frames, 2 motor off for 3 frames, 3 motor on and the loader.
TAPE_CPC = """\
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
        ld bc, 0xF782
        ld a, 0x82
        out (c), a                  ; port A out, B in, C out
        ld bc, 0xF700
        ld a, 0x08
        out (c), a                  ; motor off
        ld a, 1
        ld (0x9100), a
        ld a, 0x09
        out (c), a                  ; motor on
%(w1)s
        ld a, 2
        ld (0x9100), a
        ld bc, 0xF700
        ld a, 0x08
        out (c), a
%(w2)s
        ld a, 3
        ld (0x9100), a
        ld bc, 0xF700
        ld a, 0x09
        out (c), a
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
        ld a, 0xF5
        in a, (0)
        and 0x80
        xor c
        jr z, ed1
        ld a, c
        xor 0x80
        ld c, a
        ret

load:   ld (LDST), hl
        ld a, e
        ld (LLEN), a
        ld a, 0xF5
        in a, (0)
        and 0x80
        ld c, a
lead0:  ld d, 0
lead1:  call edge
        ld a, b
        or a
        jr z, lead0
        cp 28
        jr c, lead2
        cp 75
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
        cp 28
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
        cp 52
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


def tape_cpc_asm():
    return TAPE_CPC % dict(len=18, w1=wait_asm("mw1", 2), w2=wait_asm("mw2", 3))


# ------------------------------------------------------------------------------------------------ disk (S11)
# A standard DSK image of our own (10 tracks, 1 side, 9 sectors of 512 bytes with the data format's IDs $C1-$C9) and a program that drives the FDC
# the way AMSDOS does: recalibrate, seek, read a sector, write a sector, read it back. The upd765 is polled (no DMA): main status $FB7E (bit 7 =
# ready for a byte, bit 6 = the direction, bit 5 = execution phase), data $FB7F, the motor $FA7E.
DSK_TRACKS = 10
DSK_SIZE = 256 + DSK_TRACKS * 0x1300


def dsk_image():
    img = bytearray(DSK_SIZE)
    hdr = b"MV - CPCEMU Disk-File\r\nDisk-Info\r\n"
    img[0:len(hdr)] = hdr
    img[0x22:0x30] = b"TRX test tool  "[:14]
    img[0x30] = DSK_TRACKS
    img[0x31] = 1
    img[0x32:0x34] = (0x1300).to_bytes(2, "little")
    for t in range(DSK_TRACKS):
        base = 256 + t * 0x1300
        img[base:base + 12] = b"Track-Info\r\n"
        img[base + 0x10] = t
        img[base + 0x11] = 0
        img[base + 0x14] = 2
        img[base + 0x15] = 9
        img[base + 0x16] = 0x4E
        img[base + 0x17] = 0xE5
        for k in range(9):
            e = base + 0x18 + 8 * k
            img[e:e + 8] = bytes([t, 0, 0xC1 + k, 2, 0, 0]) + (0x200).to_bytes(2, "little")
            d = base + 0x100 + 0x200 * k
            for i in range(512):
                img[d + i] = (t * 37 + (0xC1 + k) * 11 + i) & 0xFF
    return bytes(img)


def sector_offset(track, r):
    return 256 + track * 0x1300 + 0x100 + 0x200 * (r - 0xC1)


def dsk_blank40():
    """the blank DATA-format DSK of coreapp/src/newdisk.cc (nd_cpc_data): 40 tracks, 1 side, 9 sectors of 512 bytes with $E5, 194,816 bytes"""
    img = bytearray(256 + 40 * 0x1300)
    img[0:34] = b"MV - CPCEMU Disk-File\r\nDisk-Info\r\n"
    img[0x22:0x30] = b"TRX64U        "
    img[0x30], img[0x31], img[0x32], img[0x33] = 40, 1, 0x00, 0x13
    for t in range(40):
        b = 256 + t * 0x1300
        img[b:b + 12] = b"Track-Info\r\n"
        img[b + 0x10], img[b + 0x14], img[b + 0x15], img[b + 0x16], img[b + 0x17] = t, 2, 9, 0x4E, 0xE5
        for k in range(9):
            e = b + 0x18 + 8 * k
            img[e:e + 8] = bytes([t, 0, 0xC1 + k, 2, 0, 0, 0, 2])
        img[b + 0x100:b + 0x100 + 9 * 512] = b"\xe5" * (9 * 512)
    return bytes(img)


def dsk_game():
    """a game-style Extended DSK (the 'EXTENDED CPC DSK File' a game disk usually is): 42 tracks, 1 side, 9 sectors of 512 bytes in skewed order
    C1 C6 C2 C7 C3 C8 C4 C9 C5 (track size 0x1300), AMSDOS data format, a directory of 8 entries with block lists; for the manual rom_cat run"""
    ntr = 42
    img = bytearray(256 + ntr * 0x1300)
    img[0:34] = b"EXTENDED CPC DSK File\r\nDisk-Info\r\n"
    img[0x22:0x30] = b"Amstrad-Game  "
    img[0x30], img[0x31] = ntr, 1
    for t in range(ntr):
        img[0x34 + t] = 0x13
    order = [0, 5, 1, 6, 2, 7, 3, 8, 4]
    for t in range(ntr):
        b = 256 + t * 0x1300
        img[b:b + 12] = b"Track-Info\r\n"
        img[b + 0x10], img[b + 0x14], img[b + 0x15], img[b + 0x16], img[b + 0x17] = t, 2, 9, 0x4E, 0xE5
        for k in range(9):
            e = b + 0x18 + 8 * k
            img[e:e + 8] = bytes([t, 0, 0xC1 + order[k], 2, 0, 0, 0x00, 0x02])
        data = bytearray(b"\xe5" * (9 * 512))
        if t == 0:                                  # sectors C1..C4 of track 0 = the directory
            ents = []
            blk = 2
            for n, name in enumerate([b"GAME    BAS", b"GAME    BIN", b"LOADER  BIN", b"SCREEN  SCR", b"LEVEL1  DAT", b"LEVEL2  DAT"]):
                for ext in range(2 if n in (1, 3) else 1):
                    e = bytearray(32)
                    e[1:12] = name
                    e[12] = ext
                    e[15] = 0x80
                    for q in range(8):
                        e[16 + q] = blk
                        blk += 1
                    ents.append(bytes(e))
            d = bytearray(b"\xe5" * 2048)
            for i, e in enumerate(ents):
                d[i * 32:i * 32 + 32] = e
            for k in range(4):
                o = order.index(k) * 512
                data[o:o + 512] = d[k * 512:k * 512 + 512]
        for k in range(4 if t == 0 else 0, 9):
            o = order.index(k) * 512
            data[o:o + 512] = bytes((t * 7 + k * 13 + i) & 0xFF for i in range(512))
        img[b + 0x100:b + 0x100 + 9 * 512] = data
    return bytes(img)


DISK_CYL, DISK_R_READ, DISK_R_WRITE = 3, 0xC2, 0xC5

DISK_CPC = """\
        org 0
        di
        jp start
        org 0x66
        retn
start:  ld sp, 0xBFF0
        ld bc, 0xFA7E
        ld a, 1
        out (c), a                  ; motor on
        ld a, 1
        ld (0x9100), a
        ; recalibrate drive 0
        ld a, 0x07
        call fcmd
        xor a
        call fcmd
        call sense
        ; seek to the cylinder
        ld a, 0x0F
        call fcmd
        xor a
        call fcmd
        ld a, %(cyl)d
        call fcmd
        call sense
        ld a, 2
        ld (0x9100), a
        ; read data: C = cylinder, R = %(rr)02X, 512 bytes to $8200
        ld hl, 0x8200
        ld d, %(rr)d
        call rdsec
        ld hl, 0x9010
        call result
        ld a, 3
        ld (0x9100), a
        ; write data: the bytes $5A xor i into the sector R = %(rw)02X, then read it back to $8400
        ld a, 0x45
        call fcmd
        xor a
        call fcmd
        ld a, %(cyl)d
        call fcmd
        xor a
        call fcmd
        ld a, %(rw)d
        call fcmd
        ld a, 2
        call fcmd
        ld a, %(rw)d
        call fcmd
        ld a, 0x2A
        call fcmd
        ld a, 0xFF
        call fcmd
        ld hl, 0
        ld bc, 0xFB7F
wl:     push bc
        ld bc, 0xFB7E
wl1:    in a, (c)
        jp p, wl1                   ; wait for RQM
        pop bc
        ld a, l
        xor 0x5A
        out (c), a
        inc hl
        ld a, h
        cp 2
        jr nz, wl
        ld hl, 0x9020
        call result
        ld a, 4
        ld (0x9100), a
        ld hl, 0x8400
        ld d, %(rw)d
        call rdsec
        ld hl, 0x9030
        call result
        ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin

; fcmd: A = a byte of a command for the FDC (waits until it takes one)
fcmd:   push bc
        push af
        ld bc, 0xFB7E
fc1:    in a, (c)
        and 0xC0
        cp 0x80
        jr nz, fc1
        pop af
        ld bc, 0xFB7F
        out (c), a
        pop bc
        ret

; result: 7 result bytes (waits for each) to (HL)
result: ld e, 7
rs1:    push bc
        ld bc, 0xFB7E
rs2:    in a, (c)
        and 0xC0
        cp 0xC0
        jr nz, rs2
        ld bc, 0xFB7F
        in a, (c)
        pop bc
        ld (hl), a
        inc hl
        dec e
        jr nz, rs1
        ret

; sense interrupt status: the two result bytes are read and dropped
sense:  push bc
        ld bc, 0xFB7E
se0:    in a, (c)
        and 0x0F
        jr nz, se0                  ; the seek / recalibrate is under way: the drive's busy bit
        pop bc
        ld a, 0x08
        call fcmd
        push bc
        ld bc, 0xFB7E
se1:    in a, (c)
        and 0xC0
        cp 0xC0
        jr nz, se1
        ld bc, 0xFB7F
        in a, (c)
        ld bc, 0xFB7E
se2:    in a, (c)
        and 0xC0
        cp 0xC0
        jr nz, se2
        ld bc, 0xFB7F
        in a, (c)
        pop bc
        ret

; rdsec: read data, drive 0 head 0, cylinder %(cyl)d, R = D, 512 bytes to (HL)
rdsec:  ld a, 0x46
        call fcmd
        xor a
        call fcmd
        ld a, %(cyl)d
        call fcmd
        xor a
        call fcmd
        ld a, d
        call fcmd
        ld a, 2
        call fcmd
        ld a, d
        call fcmd
        ld a, 0x2A
        call fcmd
        ld a, 0xFF
        call fcmd
        ld de, 512
rd1:    push bc
        ld bc, 0xFB7E
rd2:    in a, (c)
        jp p, rd2
        ld bc, 0xFB7F
        in a, (c)
        pop bc
        ld (hl), a
        inc hl
        dec de
        ld a, d
        or e
        jr nz, rd1
        ret
""" % dict(cyl=DISK_CYL, rr=DISK_R_READ, rw=DISK_R_WRITE)


# ------------------------------------------------------------------------------------------------ video (S7)
# Random screen memory ($C000-$FFFF), the 16 pens and the border set to hardware inks, then mode 0, 1 and 2 for 12 frames each ($9000 = 1, 2, 3). The
# program runs from the ROM, so the GA's mode writes keep both ROMs enabled ($80 + mode).
# The hardware ink numbers and the firmware's 27 colours (G x 9 + R x 3 + B, levels 0 / 50 % / 100 %) are CPC hardware facts; the RGB values
# are those of the Gate Array table the core's color_mix uses (grimware.org, "Gate Array").
HW_INK_FW = [13, 13, 19, 25, 1, 7, 10, 16, 7, 25, 24, 26, 6, 8, 15, 17, 1, 19, 18, 20, 0, 2, 9, 11, 4, 22, 21, 23, 3, 5, 12, 14]
# R, G, B of the 27 colours in the order index = G x 9 + R x 3 + B  (24 bit, as color_mix gives them)
FW_RGB = {  # (R, G, B) levels -> RGB
    (0, 0, 0): 0x000201, (0, 0, 1): 0x00026B, (0, 0, 2): 0x0C02F4, (1, 0, 0): 0x6C0201, (1, 0, 1): 0x690268, (1, 0, 2): 0x6C02F2,
    (2, 0, 0): 0xF30506, (2, 0, 1): 0xF00268, (2, 0, 2): 0xF302F4, (0, 1, 0): 0x027801, (0, 1, 1): 0x007868, (0, 1, 2): 0x0C7BF4,
    (1, 1, 0): 0x6E7B01, (1, 1, 1): 0x6E7D6B, (1, 1, 2): 0x6E7BF6, (2, 1, 0): 0xF37D0D, (2, 1, 1): 0xF37D6B, (2, 1, 2): 0xFA80F9,
    (0, 2, 0): 0x02F001, (0, 2, 1): 0x00F36B, (0, 2, 2): 0x0FF3F2, (1, 2, 0): 0x71F504, (1, 2, 1): 0x71F36B, (1, 2, 2): 0x71F3F4,
    (2, 2, 0): 0xF3F30D, (2, 2, 1): 0xF3F36D, (2, 2, 2): 0xFFF3F9,
}
PEN_HW = [0x14, 0x04, 0x15, 0x1C, 0x18, 0x1D, 0x0C, 0x05, 0x0D, 0x16, 0x06, 0x17, 0x1E, 0x00, 0x1F, 0x0E]
BORDER_HW = 0x0B


def fw_to_rgb(fw):
    g, rem = divmod(fw, 9)
    r, b = divmod(rem, 3)
    return FW_RGB[(r, g, b)]


def hw_rgb565(hw):
    rgb = fw_to_rgb(HW_INK_FW[hw])
    r, g, b = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def hwrgb_vh():
    lines = ["// T92 S7: the RGB565 value of each CPC hardware ink number (0-31), made by tools/cpc_progs.py from the hardware-ink and colour tables",
             "// (CPC hardware facts, the Gate Array's RGB values); the benches compare the frame buffer with it. Do not edit.",
             "function [15:0] hw_rgb565(input [4:0] hw);", "    case (hw)"]
    for hw in range(32):
        lines.append("        5'd%d: hw_rgb565 = 16'h%04X;" % (hw, hw_rgb565(hw)))
    lines += ["        default: hw_rgb565 = 16'h0000;", "    endcase", "endfunction"]
    return "\n".join(lines) + "\n"


def video_asm():
    pens = ", ".join("0x%02X" % h for h in PEN_HW + [BORDER_HW])
    return build_z80_boot("""\
SEED    equ 0x8000
start:  ld sp, 0xBFF0
        ld c, 0
        call crtcinit
        ld hl, pentab
        ld d, 0
pl:     ld a, d
        cp 16
        jr nz, pl1
        ld a, 0x10
pl1:    call gaout
        ld a, (hl)
        or 0x40
        call gaout
        inc hl
        inc d
        ld a, d
        cp 17
        jr nz, pl
        ld hl, 0x1234
        ld (SEED), hl
        ld de, 0xC000
f1:     call xrnd
        ld a, l
        ld (de), a
        inc de
        ld a, h
        ld (de), a
        inc de
        ld a, d
        or e
        jr nz, f1
        ld a, 1
        ld (0x9000), a
        ld a, 0x80
        call gaout
%(w1)s
        ld a, 2
        ld (0x9000), a
        ld a, 0x81
        call gaout
%(w2)s
        ld a, 3
        ld (0x9000), a
        ld a, 0x82
        call gaout
%(w3)s
        ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin
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
pentab: db %(pens)s
""" % dict(pens=pens, w1=wait_asm("vw1", 12), w2=wait_asm("vw2", 12), w3=wait_asm("vw3", 12))) + GA_HELP + CRTC_INIT


# ------------------------------------------------------------------------------------------------ anim (S13)
# A program whose every cycle shows: a counter writes into the screen RAM, the border ink follows the counter's low bits (so the picture holds the
# timeline), an interrupt (the Gate Array's, every 52 lines) counts frames at $8000.
ANIM = build_z80_boot("""\
        org 0x38
        jp isr
        org 0x100
start:  ld sp, 0xBFF0
        ld c, 0
        call crtcinit
        ld a, 0x82
        call gaout
        ld a, 0x00
        call gaout
        ld a, 0x4B
        call gaout
        im 1
        ld de, 0
        ei
m1:     inc de
        ld a, d
        and 0x3F
        or 0xC0
        ld h, a
        ld l, e
        ld (hl), e
        ld b, 0x7F
        ld a, 0x10
        out (c), a
        ld a, e
        and 0x0F
        or 0x40
        out (c), a
        jr m1
isr:    push af
        ld a, (0x8000)
        inc a
        ld (0x8000), a
        pop af
        ei
        reti
""".replace("        org 0x38\n        jp isr\n        org 0x100\nstart:", "start:")) + GA_HELP + CRTC_INIT


# ------------------------------------------------------------------------------------------------ bars (S7, rasterbars)
# The border ink is changed in every line: sync to the Gate Array's interrupt that follows the vertical sync (jitter under 1 us), then 308 lines of
# exactly 64 us (ld c,(hl) 2 + inc hl 2 + out (c),c 4 + ld a,12 2 + a loop 47 + dec de 2 + ld a,d 1 + or e 1 + jp nz 3, in us as the slots bench measures them) writing the next ink of a 16
# entry cycle to the selected pen (the border). The bench looks for the cycle in the left and right border of the frame buffer.
BARS_LINES = 308
BARS_INKS = [0x14, 0x04, 0x15, 0x1C, 0x18, 0x1D, 0x0C, 0x05, 0x0D, 0x16, 0x06, 0x17, 0x1E, 0x00, 0x1F, 0x0E]
BARS = build_z80_boot("""\
start:  ld sp, 0xBFF0
        ld c, 0
        call crtcinit
        ld a, 0x81
        call gaout
        ld a, 0x10
        call gaout
        ld a, 0x54
        call gaout
        im 1
main:   ld a, 0xF5
        in a, (0xFF)
        rra
        jr c, main
mw2:    ld a, 0xF5
        in a, (0xFF)
        rra
        jr nc, mw2
        ei
        halt
        jr main
bars:   ld hl, btab
        ld bc, 0x7F00
        ld de, %(n)d
bl:     ld c, (hl)
        inc hl
        out (c), c
        ld a, 12
bd:     dec a
        jr nz, bd
        dec de
        ld a, d
        or e
        jp nz, bl
        jp main
btab:   db %(tab)s
""" % dict(n=BARS_LINES, tab=", ".join("0x%02X" % (0x40 | BARS_INKS[k % 16]) for k in range(BARS_LINES)))) + GA_HELP + CRTC_INIT + "\n        org 0x38\n        jp bars\n"


PROGRAMS = {}      # name -> image function


def assemble(text):
    return zx_asm.assemble(text)


def register(name, img_fn):
    PROGRAMS[name] = img_fn


register("clk", lambda: assemble(INIT_CLK))
register("page", image_page)
register("page664", image_page)
register("page464", image_page)
register("audio", image_audio_cpc)
register("keys", lambda: assemble(KEYS_CPC))
register("tape", lambda: assemble(tape_cpc_asm()))
register("video", lambda: assemble(video_asm()))
register("bars", lambda: assemble(BARS))
import cpc_slots  # noqa: E402
register("slots", lambda: assemble(cpc_slots.full_program()[0]))
register("anim", lambda: assemble(ANIM + "\n        org 0x38\n        jp isr\n"))
register("disk", lambda: assemble(DISK_CPC))


def write_hex(path, data):
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


EXPECT = {"page": lambda: exp_page(), "page664": lambda: exp_page(1), "page464": lambda: exp_page(0)}


def write_ddr_hex(path, data):
    """a byte image as 128-bit words, 32 hex digits a line, the first byte in the low bits (little endian, as the DDR2 holds it)"""
    data = data + bytes((-len(data)) % 16)
    with open(path, "w") as f:
        for i in range(0, len(data), 16):
            f.write(data[i:i + 16][::-1].hex() + "\n")


def sha(name):
    h = hashlib.sha256(PROGRAMS[name]())
    if name in EXPECT:
        h.update(EXPECT[name]())
    if name == "disk":
        h.update(dsk_image())
    return h.hexdigest()


def main(argv):
    os.makedirs(PROG, exist_ok=True)
    if argv[1:] == ["build"]:
        for name, fn in PROGRAMS.items():
            img = fn()
            write_hex(os.path.join(PROG, name + ".hex"), img)
            open(os.path.join(PROG, name + ".sha256"), "w").write(sha(name) + "\n")
            print("%s: %d bytes" % (name, len(img)))
            if name in EXPECT:
                write_hex(os.path.join(PROG, name + ".exp.hex"), EXPECT[name]())
        return 0
    if argv[1:] == ["hwrgb"]:
        sys.stdout.write(hwrgb_vh())
        return 0
    if argv[1:2] == ["ddr"]:                       # the disk image as DDR2 words, for the bench (made at run time, not committed)
        write_ddr_hex(argv[2], dsk_image())
        return 0
    if argv[1:2] == ["ddrgame"]:
        write_ddr_hex(argv[2], dsk_game())
        return 0
    if argv[1:2] == ["ddr40"]:
        write_ddr_hex(argv[2], dsk_blank40())
        return 0
    if argv[1:] == ["--check"]:
        bad = 0
        for name in PROGRAMS:
            p = os.path.join(PROG, name + ".sha256")
            if not os.path.exists(p) or open(p).read().strip() != sha(name):
                print("cpc_progs: %s.hex was not made from the source in tools/cpc_progs.py (run tools/cpc_progs.py build)" % name)
                bad = 1
        p = os.path.join(PROG, "..", "hw_rgb.vh")
        if not os.path.exists(p) or open(p).read() != hwrgb_vh():
            print("cpc_progs: cores/cpc/sim/hw_rgb.vh is not what tools/cpc_progs.py hwrgb gives")
            bad = 1
        if not bad:
            print("cpc_progs: the committed programs match their sources")
        return bad
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
