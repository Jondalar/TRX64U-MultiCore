#!/usr/bin/env python3
"""mkchr.py <out>: a SYNTHETIC character ROM for the benches (8,192 bytes, the layout video_generator.vhd addresses:
{ROMSWITCH, GR2, A, B, DL[5:0], SEGC, SEGB, SEGA}). It holds none of Apple's glyphs. The graphics half (GR2 = 1) is the logic of the machine's
video path, written from what it does (measured against a real character ROM outside the repository, never copied from it):
  hires    (SEGB = 0)  the byte's bits, inverted: ROM = $80 | ~(B << 6 | DL[5:0]); the shifter then puts DL[6:0] on the screen, bit 0 first
  lores    (SEGB = 1)  the colour nibble twice, inverted; the nibble is DL[3:0] (SEGC = 0) or DL[7:4] (SEGC = 1) = {A, B, DL[5:4]},
                       rotated by two bits when SEGA = 1 (the second character column of a pair sits half a colour carrier cycle later)
The text half (GR2 = 0) is a pattern of our own: every character code is a different, repeatable picture of its own."""
import sys

def rom():
    r = bytearray(8192)
    for sw in (0, 1):
        for gr2 in (0, 1):
            for a in (0, 1):
                for b in (0, 1):
                    for dl in range(64):
                        for seg in range(8):
                            addr = (sw << 12) | (gr2 << 11) | (a << 10) | (b << 9) | (dl << 3) | seg
                            if gr2:
                                if seg & 2 == 0:
                                    v = 0x80 | (~((b << 6) | dl) & 0x7F)
                                else:
                                    nib = ((a << 3) | (b << 2) | (dl >> 4)) if seg & 4 else (dl & 15)
                                    if seg & 1:
                                        nib = ((nib << 2) | (nib >> 2)) & 15
                                    v = ~(nib * 0x11) & 0xFF
                            else:
                                code = (a << 7) | (b << 6) | dl
                                v = ((code * 37 + seg * 91 + sw * 7 + 3) * 0x35 >> 2) & 0x7F
                                v |= 0x80 if seg == 7 else 0
                            r[addr] = v
    return bytes(r)

if __name__ == "__main__":
    open(sys.argv[1], "wb").write(rom())
