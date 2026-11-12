#!/usr/bin/env python3
"""mkrom.py <prog.s> <out.hex> [name=value ...]: assemble one of the benches' 6502 programs (asm/, written for this project, no Apple
ROM call in any of them) with 64tass into the 16 KB system ROM image ($C000-$FFFF) and write it as hex, one byte a line.
The program sets its own reset vector. name=value pairs are passed to 64tass as -D."""
import os, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", "..", ".."))
src, out = sys.argv[1], sys.argv[2]
defs = ["-D" + a for a in sys.argv[3:]]
tass = os.path.join(root, "tools", "64tass.sh")
def tables():
    """tables.inc: data the programs copy around (written by this script, not typed in)."""
    out = []
    # HGR colour bars, one 256 byte page of the 8 KB screen: a row is 40 bytes at offsets 0, 40, 80 of every 128; five bars of 8 bytes
    # (green $2A/$55, violet $55/$2A, orange $AA/$D5, blue $D5/$AA, white $7F), the byte pair by the parity of the column
    bar = [(0x2A, 0x55), (0x55, 0x2A), (0xAA, 0xD5), (0xD5, 0xAA), (0x7F, 0x7F)]
    page = []
    for o in range(256):
        i = (o & 127)
        if i >= 120:
            page.append(0)
        else:
            c = i % 40
            page.append(bar[c // 8][c & 1])
    out.append("bars\n" + "".join("    .byte " + ",".join("$%02x" % b for b in page[k:k + 16]) + "\n" for k in range(0, 256, 16)))
    # double hires: 16 bars of 28 dots, then black; dot n is bit n mod 7 of byte n div 7 of the line, even bytes in aux RAM, odd bytes in main RAM.
    # Bar k is the colour with the lores number k (the order of check_video.py LORES). Apple's double hires number of that colour is the
    # lores number with bits 1 and 3 swapped (magenta 1, brown 2, orange 3, dark green 4 ... in the table of the //e technical reference:
    # solid magenta is the bytes 08 11 22 44 = a lone dot at line dot n mod 4 = 3), and its dots are set where bit 3 - (n mod 4) of that number is.
    def dots(n):
        k = n // 28
        if k >= 16:
            return 0
        a = (k & 5) | ((k & 2) << 2) | ((k & 8) >> 2)
        return (a >> (3 - (n & 3))) & 1
    for name, odd in (("dh_aux", 0), ("dh_main", 1)):
        pg = []
        for o in range(256):
            i = o & 127
            if i >= 120:
                pg.append(0)
            else:
                col = i % 40
                b = 2 * col + odd
                pg.append(sum(dots(7 * b + j) << j for j in range(7)))
        out.append(name + "\n" + "".join("    .byte " + ",".join("$%02x" % v for v in pg[k:k + 16]) + "\n" for k in range(0, 256, 16)))
    rows = [0x400 + (r & 7) * 0x80 + (r >> 3) * 0x28 for r in range(24)]
    out.append("rowlo\n    .byte " + ",".join("$%02x" % (a & 255) for a in rows) + "\n")
    out.append("rowhi\n    .byte " + ",".join("$%02x" % (a >> 8) for a in rows) + "\n")
    return "".join(out)

with tempfile.TemporaryDirectory() as tmp:
    open(os.path.join(tmp, "tables.inc"), "w").write(tables())
    prg = os.path.join(tmp, "p.prg")
    r = subprocess.run([tass, "--m65c02", "-q", "-o", prg, "-I", os.path.join(here, "asm"), "-I", tmp] + defs + [src], capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit(1)
    data = open(prg, "rb").read()
load = data[0] | (data[1] << 8)
body = data[2:]
if load < 0xC000 or load + len(body) > 0x10000:
    sys.exit("mkrom: program %04x-%04x is not inside $C000-$FFFF" % (load, load + len(body) - 1))
rom = bytearray(16384)
rom[load - 0xC000: load - 0xC000 + len(body)] = body
open(out, "w").write("".join("%02x\n" % b for b in rom))
print("mkrom: %s: %04x-%04x, reset vector %02x%02x" % (os.path.basename(src), load, load + len(body) - 1, rom[0x3FFD], rom[0x3FFC]))
