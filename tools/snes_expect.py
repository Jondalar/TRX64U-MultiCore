#!/usr/bin/env python3
"""T142 S2: what cores/snes/sim/prog/mem.s must have left in its six checksum words (WRAM $0100..$010B), computed here from the program's own rules and not from the
machine - an oracle of the bench besides the comparison of two builds of the core, which would call a wrong machine right when both are wrong the same way.

  tools/snes_expect.py mem          six hex words, the last one as "a|b" (the PPU's read buffer: word 0 is consumed by the program's dummy read or not)
"""
import sys


def prng(seed, n):
    x = seed & 0xFFFFFFFF
    out = []
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return out


def words(b, i):
    return b[i] | (b[i + 1] << 8)


def mem():
    t = {1: prng(11, 2048), 2: prng(22, 2048), 3: prng(33, 4096), 4: prng(44, 2048), 5: prng(55, 2048), 6: prng(66, 2048), 7: prng(77, 2048)}
    chk0 = 0
    for _ in range(2):
        for x in range(0, 0x400, 2):
            a = words(t[1], x)
            c = 0
            for bank in range(2, 8):
                s = a + words(t[bank], x) + c        # CLC once, then ADC after ADC: the carry goes on
                a, c = s & 0xFFFF, s >> 16
            chk0 = (a + chk0) & 0xFFFF
    pat = [(((x << 1) + 0x13) ^ 0xA55A) & 0xFFFF for x in range(0, 0x2000, 2)]    # 7E:2000 + x
    chk2 = sum(pat) & 0xFFFF
    chk4 = (256 + sum(range(1, 257))) & 0xFFFF
    chk6 = sum(pat[:2048]) & 0xFFFF                                       # 4 KiB moved 7E -> 7F -> 7E:6000
    chk8 = sum(words(t[2], x) for x in range(0, 0x800, 2)) & 0xFFFF       # 2 KiB of bank 2 through the DMA
    vram = pat[:2048]                                                     # 4 KiB DMA'd to VRAM word 0..: word k = pattern word k
    a = sum(vram[0:1024]) & 0xFFFF
    b = sum(vram[1:1025]) & 0xFFFF
    return [chk0, chk2, chk4, chk6, chk8], (a, b)


if __name__ == "__main__":
    if sys.argv[1:] == ["mem"]:
        w, (a, b) = mem()
        print(" ".join("%04x" % v for v in w) + " %04x|%04x" % (a, b))
        sys.exit(0)
    print(__doc__)
    sys.exit(2)
