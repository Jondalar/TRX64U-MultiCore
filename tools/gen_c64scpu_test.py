#!/usr/bin/env python3
"""T182: synthetic system ROM images for the C64SCPU core's benches (cores/c64scpu/sim). Our own 6510 program, assembled by tools/c128asm.py (a 6502
assembler); no byte of any Commodore ROM. The image has the 20 KiB layout of the core's ROM path (cores/c64scpu/c64_bus_arbiter_c64scpu.v, c64scpu_chr.v):
  +0x0000  C64 BASIC  ($A000-$BFFF; banks 0/1)
  +0x2000  C64 KERNAL ($E000-$FFFF; banks 2/3)
  +0x4000  character ROM (4 KiB)

  gen_c64scpu_test.py <program> <out dir>   writes <out dir>/mem/c64scpu_rom.mem (10,240 words, one hex word per line, low byte first in the word) and
                                            <out dir>/expect.json (the 25 screen rows as screen codes), prints the bench arguments (+EXPECT=...) on one line
  gen_c64scpu_test.py --list

Programs:
  boot          the KERNAL clears the screen, sets the VIC up and writes the banner, jumps through the BASIC ROM's vector at $A000 to its cold start, which
                writes the second banner line and READY. (the screen of a C64 after the power-on), then idles
  ctl_nobasic   the control of the BASIC ROM path: the vector at $A000 points to an idle loop, so the banner of the KERNAL stays and no READY. comes (the check must fail)
  ctl_nochars   the control of the VIC's character path: the character ROM is all zero, the RAM holds the same screen, the picture has no glyphs
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c128asm  # noqa: E402

SIZE = 0x5000
BASIC = 0x0000
KERNAL = 0x2000
CHR = 0x4000


def sc(text):
    """screen codes of a text: A-Z = 1-26, the rest as ASCII (space, digits, * and . are the same codes as in PETSCII)"""
    return [(ord(c) - 64) if 'A' <= c <= 'Z' else ord(c) for c in text]


def chargen():
    """a character ROM of our own: every glyph a distinct 8 x 8 pattern, derived from the screen code (no Commodore glyph). Glyph 0x20 (space) is blank."""
    rom = bytearray(0x1000)
    for code in range(256):
        for row in range(8):
            if code & 0x7F == 0x20:
                v = 0x00
            else:
                v = ((code * 37 + row * 59 + 0x5A) ^ (code >> 3) * (row + 1)) & 0xFF
                v |= 0x81 if row in (0, 7) else 0x00        # the top and the bottom row always have the end pixels set: no glyph is blank
            rom[code * 8 + row] = v if code < 128 else v ^ 0xFF
    return rom


BANNER1 = "**** COMMODORE 64 BASIC V2 ****"
BANNER2 = "64K RAM SYSTEM  38911 BASIC BYTES FREE"
READY = "READY."
ROW_BANNER1, COL_BANNER1 = 1, 4
ROW_BANNER2, COL_BANNER2 = 3, 1
ROW_READY, COL_READY = 5, 0


def at(row, col):
    return 0x0400 + 40 * row + col


def db(vals):
    return "        db " + ", ".join(str(v) for v in vals) + "\n"


def prog_boot(kind="boot"):
    img = bytearray([0xEA]) * SIZE
    vec = "$A010" if kind != "ctl_nobasic" else "$A00C"
    basic = c128asm.assemble("""
        org $A000
        dw %s
        org $A00C
dead:   jmp dead
        org $A010
        ldx #0
b2:     lda msg2,x
        beq b2d
        sta %d,x
        inx
        bne b2
b2d:    ldx #0
b3:     lda msg3,x
        beq b3d
        sta %d,x
        inx
        bne b3
b3d:    lda #$A0
        sta %d
idle:   jmp idle
msg2:
%s        db 0
msg3:
%s        db 0
""" % (vec, at(ROW_BANNER2, COL_BANNER2), at(ROW_READY, COL_READY), at(ROW_READY + 1, 0), db(sc(BANNER2)), db(sc(READY))))
    for a, b in basic.items():
        img[BASIC + (a - 0xA000)] = b
    kernal = c128asm.assemble("""
        org $E000
reset:  sei
        cld
        ldx #$FF
        txs
        lda #$2F
        sta $00
        lda #$37
        sta $01
        lda #$20
        ldx #0
clr:    sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $0700,x
        inx
        bne clr
        lda #$0E
        ldx #0
col:    sta $D800,x
        sta $D900,x
        sta $DA00,x
        sta $DB00,x
        inx
        bne col
        lda #$0E
        sta $D020
        lda #$06
        sta $D021
        lda #$14
        sta $D018
        lda #$1B
        sta $D011
        lda #$C8
        sta $D016
        ldx #0
b1:     lda msg1,x
        beq b1d
        sta %d,x
        inx
        bne b1
b1d:    jmp ($A000)
msg1:
%s        db 0
        org $FFFA
        dw reset, reset, reset
""" % (at(ROW_BANNER1, COL_BANNER1), db(sc(BANNER1))))
    for a, b in kernal.items():
        img[KERNAL + (a - 0xE000)] = b
    chars = chargen() if kind != "ctl_nochars" else bytearray(0x1000)
    img[CHR:CHR + 0x1000] = chars
    rows = [[0x20] * 40 for _ in range(25)]
    for text, row, col in ((BANNER1, ROW_BANNER1, COL_BANNER1), (BANNER2, ROW_BANNER2, COL_BANNER2), (READY, ROW_READY, COL_READY)):
        for i, v in enumerate(sc(text)):
            rows[row][col + i] = v
    rows[ROW_READY + 1][0] = 0xA0
    exp = ",".join("%04x:%02x" % (at(ROW_READY, COL_READY + i), v) for i, v in enumerate(sc(READY)))
    return img, rows, exp


PROGS = ("boot", "ctl_nobasic", "ctl_nochars")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--list":
        print("\n".join(PROGS))
        return
    if len(sys.argv) != 3 or sys.argv[1] not in PROGS:
        sys.exit(__doc__)
    img, rows, exp = prog_boot(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as f:
        for i in range(0, SIZE, 2):
            f.write("%02x%02x\n" % (img[i + 1], img[i]))
    with open(os.path.join(out, "expect.json"), "w") as f:
        json.dump({"rows": rows, "chargen": list(img[CHR:CHR + 0x1000]), "fg": 14, "bg": 6, "border": 14}, f)
    print("+EXPECT=" + exp)


if __name__ == "__main__":
    main()
