#!/usr/bin/env python3
# T134 S9: a synthetic 35 track D64 with one PRG (3 blocks) for the drive bench. No Commodore byte: the disk is made here.
#   gen_d64.py <out dir>  ->  disk.d64, file.prg (the bytes LOAD "TESTFILE" must return, load address first)
import sys, os, random
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
SPT = [21]*17 + [19]*7 + [18]*6 + [17]*5
def off(t, s): return (sum(SPT[:t-1]) + s) * 256
img = bytearray(174848)
random.seed(1341)
prg = bytes([0x01, 0x04]) + bytes(random.randrange(256) for _ in range(598))
data = prg
blocks = [(1, 0), (1, 1), (1, 2)]
for i, (t, s) in enumerate(blocks):
    chunk = data[i*254:(i+1)*254]
    o = off(t, s)
    if i < len(blocks) - 1:
        img[o], img[o+1] = blocks[i+1]
    else:
        img[o], img[o+1] = 0, len(chunk) + 1
    img[o+2:o+2+len(chunk)] = chunk
used = {b for b in blocks} | {(18, 0), (18, 1)}
bam = off(18, 0)
img[bam:bam+4] = bytes([18, 1, 0x41, 0])
for t in range(1, 36):
    bits = 0; free = 0
    for s in range(SPT[t-1]):
        if (t, s) not in used:
            bits |= 1 << s; free += 1
    img[bam+4*t:bam+4*t+4] = bytes([free, bits & 255, (bits >> 8) & 255, (bits >> 16) & 255])
name = b"TESTDISK"
img[bam+0x90:bam+0xA0] = name + b"\xa0" * (16 - len(name))
img[bam+0xA0:bam+0xA2] = b"\xa0\xa0"
img[bam+0xA2:bam+0xA4] = b"TX"
img[bam+0xA4] = 0xA0
img[bam+0xA5:bam+0xA7] = b"2A"
img[bam+0xA7:bam+0xAB] = b"\xa0" * 4
d = off(18, 1)
img[d], img[d+1] = 0, 0xFF
fname = b"TESTFILE"
img[d+2] = 0x82
img[d+3], img[d+4] = 1, 0
img[d+5:d+21] = fname + b"\xa0" * (16 - len(fname))
img[d+0x1E], img[d+0x1F] = len(blocks), 0
open(os.path.join(out, "disk.d64"), "wb").write(img)
open(os.path.join(out, "file.prg"), "wb").write(prg)
print("disk.d64 %d bytes, file.prg %d bytes" % (len(img), len(prg)))
