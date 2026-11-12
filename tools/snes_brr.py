#!/usr/bin/env python3
"""T142 audio bench: the S-DSP's BRR decoder against a reference decoder.
  tools/snes_brr.py gen <out.bin>      four BRR blocks (range 10; filter 0, 1, 2, 3; the last one ends the loop back to the first), 36 bytes for $0300
  tools/snes_brr.py check <log> [n]    <log> = the bench's `SNES brr <slot> <value>` lines (every word the decoder writes into its ring buffer): the values
                                       must be the reference decoder's, in order (n samples at least, default 400)
The reference is the filter arithmetic of the S-DSP as the documentation of the chip gives it: a sample is (nibble << range) >> 1 (range 13..15: -2048 or 0),
filter 1: + p0/2 - p0/32, filter 2: + p0 - p1 + p1/16 - p0 * 3/64, filter 3: + p0 - p1 + p1 * 3/16 - p0 * 13/128 (p0 = the previous stored word, p1 = the one
before, halved; all shifts arithmetic and applied to the negated product), the result clamped to 16 bit and stored doubled in a 16 bit word (wrapping)."""
import sys

HDR = [0xA0, 0xA4, 0xA8, 0xAF]
KEY = [(0x1A, 0x35), (0x37, 0x53), (0x25, 0x19), (0x4B, 0x2D)]


def block_bytes(b):
    return [HDR[b]] + [(KEY[b][0] ^ ((i * KEY[b][1]) & 0xFF)) & 0xFF for i in range(8)]


def image():
    out = []
    for b in range(4):
        out += block_bytes(b)
    return bytes(out)


def reference(nblocks):
    clamp = lambda x: max(-32768, min(32767, x))
    st = [0, 0]
    seq = []
    for n in range(nblocks):
        b = n % 4
        d = block_bytes(b)
        r, f = d[0] >> 4, (d[0] >> 2) & 3
        for byte in d[1:]:
            for nib in (byte >> 4, byte & 15):
                s = nib - 16 if nib > 7 else nib
                s = (s << r) >> 1 if r <= 12 else (-2048 if s < 0 else 0)
                p0, p1 = st[0], st[1] >> 1
                if f == 1:
                    s += (p0 >> 1) + ((-p0) >> 5)
                elif f == 2:
                    s += p0 + ((-(p0 + p0 * 2)) >> 6) - p1 + (p1 >> 4)
                elif f == 3:
                    s += p0 + ((-(p0 + p0 * 4 + p0 * 8)) >> 7) - p1 + ((p1 * 2 + p1) >> 4)
                o = ((clamp(s) * 2 + 32768) & 0xFFFF) - 32768       # stored doubled in 16 bit: it wraps
                st = [o, st[0]]
                seq.append(o)
    return seq


def main(a):
    if len(a) == 3 and a[1] == "gen":
        open(a[2], "wb").write(image())
        return 0
    if len(a) in (3, 4) and a[1] == "check":
        need = int(a[3]) if len(a) == 4 else 400
        got = [int(l.split()[3]) for l in open(a[2]) if l.startswith("SNES brr ")]
        if len(got) < need:
            print("SNES FAIL brr: %d decoded words, %d wanted" % (len(got), need))
            return 1
        ref = reference(len(got) // 16 + 3)
        # the decoder writes a few words before the first block (silence while the voice starts): align on the first word of the reference
        for off in range(0, 8):
            if got[off:off + 16] == ref[:16]:
                break
        else:
            print("SNES FAIL brr: the first (filter 0) block is not decoded as the reference: %s vs %s" % (got[:20], ref[:16]))
            return 1
        g = got[off:]
        bad = [i for i in range(len(g)) if g[i] != ref[i]]
        if bad:
            i = bad[0]
            print("SNES FAIL brr: %d of %d words differ from the reference, first at word %d (block %d, filter %d): got %d, want %d"
                  % (len(bad), len(g), i, i // 16 % 4, i // 16 % 4, g[i], ref[i]))
            return 1
        print("SNES PASS brr: %d decoded words equal the reference (filters 0..3)" % len(g))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
