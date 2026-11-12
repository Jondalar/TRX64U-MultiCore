#!/usr/bin/env python3
"""c128-vicglitch: a RAM image of PETSCII Robots C128 as its loader leaves it, for tb_c128.v +INJECT_AT (RAM_DDR2 build).
  mk_inject.py <dir with the PRGs of 128-robots.d81 (d81x.py)> <out dir>      writes <out>/inject.mem, inject.msk (131072 lines each: byte, 1 = write)
Where each file goes (bank and address) is read from the loader's code (LOADER-128 at $8c00 / BOOT-128): SETBNK A = data bank, LOAD with secondary address 0
at X/Y. Order of the loader: INTRO, GRAPHICS, COMMON, FACES, GAME0, MUSIC, GAME1, TILESET, SPRITES (later files overwrite earlier ones).
A stub at $0F00 (block 0) repeats what BOOT-128 and the loader leave in the machine (registers, zero page, RCR $05) and joins the loader at $8e80
(IRQ setup, JMP $0b06); the BASIC main loop vector IMAIN ($0302) is pointed at it."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "tools"))
import c128asm  # noqa: E402

ORDER = [("LOADER-128", 0, 0x8C00), ("INTRO-128", 0, 0x2000), ("GRAPHICS-128", 0, 0x3600), ("COMMON-128", 0, 0x0B00), ("FACES-128", 1, 0x2000),
         ("GAME0-128", 0, 0x1300), ("MUSIC-128", 0, 0x9000), ("GAME1-128", 1, 0x1000), ("TILESET-128", 1, 0x4900), ("SPRITES-128", 1, 0xE400)]

STUB = """
        org $0F00
        sei
        lda #$0E
        sta $FF00
        lda #$28
        sta $DD03
        lda #$00
        sta $D021
        lda #$06
        sta $D020
        lda $0A04
        and #$FE
        sta $0A04
        lda #$01
        sta $12FD
        lda #$FF
        sta $D8
        lda #$B7
        sta $033C
        lda #$05
        sta $D506
        lda #$40
        sta $0A22
        lda #$80
        sta $F7
        lda #$94
        sta $DD00
        lda #$80
        sta $D018
        lda #$3B
        sta $D011
        lda #$18
        sta $D016
        lda $01
        ora #$04
        sta $01
        lda #$05
        sta $D506
        lda #$08
        sta $D016
        lda #$C8
        sta $FB
        sta $FC
        jmp $8E80
"""


START = """
        org $0FC0
        lda #$4E
        sta $FF00
        jmp $416D
"""


def main():
    src, out = sys.argv[1], sys.argv[2]
    img = bytearray(0x20000)
    msk = bytearray(0x20000)
    for name, blk, addr in ORDER:
        d = open(os.path.join(src, name), "rb").read()[2:]
        base = blk * 0x10000 + addr
        assert addr + len(d) <= 0x10000, name
        img[base:base + len(d)] = d
        msk[base:base + len(d)] = b"\x01" * len(d)
    for a, b in c128asm.assemble(STUB).items():
        img[a] = b
        msk[a] = 1
    # the loader's tail ends in JMP $0B06 (the game's menu, GAME0); with `start` it goes to $0FC0 instead: RAM block 1 and on to the game's own start routine $416D (RCR $45)
    if len(sys.argv) > 3 and sys.argv[3] == "start":
        for a, b in c128asm.assemble(START).items():
            img[a] = b
            msk[a] = 1
        assert img[0x8EB9] == 0x4C and img[0x8EBA] == 0x06 and img[0x8EBB] == 0x0B
        img[0x8EBA], img[0x8EBB] = 0xC0, 0x0F
    # `level` (T140 D30): the game's level loader (GAME1 $5624: SETBNK 1, SETNAM, SETLFS, LOAD X/Y = $9D00) cannot read the d81 in the bench (+NODEV), so the level file is put where the
    # LOAD would have put it (RAM block 1, $9D00) and the LOAD call is replaced by CLC, NOP, NOP: the game runs on into the level instead of stopping at "SEARCHING FOR LEVEL-A-128"
    if len(sys.argv) > 4 and sys.argv[4].startswith("LEVEL-"):
        lv = open(os.path.join(src, sys.argv[4]), "rb").read()[2:]
        base = 0x10000 + 0x9D00
        assert len(lv) <= 0x10000 - 0x9D00
        img[base:base + len(lv)] = lv
        msk[base:base + len(lv)] = b"\x01" * len(lv)
        p = 0x10000 + 0x564B
        assert img[p:p + 3] == bytes([0x20, 0xD5, 0xFF])
        img[p:p + 3] = bytes([0x18, 0xEA, 0xEA])
    for a, b in ((0x0302, 0x00), (0x0303, 0x0F), (0x0314, 0x00), (0x0315, 0x0F)):     # IMAIN and the KERNAL's IRQ vector -> stub (the IRQ comes at once, no key needed)
        img[a] = b
        msk[a] = 1
    os.makedirs(out, exist_ok=True)
    for nm, data in (("inject.mem", img), ("inject.msk", msk)):
        with open(os.path.join(out, nm), "w") as f:
            f.write("\n".join("%02x" % x for x in data) + "\n")
    print("written", out, "bytes injected", sum(msk))


if __name__ == "__main__":
    main()
