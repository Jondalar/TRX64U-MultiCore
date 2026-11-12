#!/usr/bin/env python3
"""Golden vectors for test_apple2_disk.cc: the 416 ring bytes of one physical sector of track 3, written from the DOS 3.3 RWTS
definition (the READ routine fills a 86 byte buffer from the top down, POSTNIB shifts it out two bits per use) and not from the C++
code. The sector content is byte i of file sector s = (s*37 + i*11 + 5) & 255. Prints one line per (order, physical sector)."""
ENC = [0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
       0xCB,0xCD,0xCE,0xCF,0xD3,0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF2,0xF3,0xF4,
       0xF5,0xF6,0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF]
# logical (file) sector -> physical, the DOS 3.3 and ProDOS software interleaves (the inverse of the tables in apple2_nib.cc)
DOS_L2P = [0,13,11,9,7,5,3,1,14,12,10,8,6,4,2,15]
PRO_L2P = [0,2,4,6,8,10,12,14,1,3,5,7,9,11,13,15]
def f44(b): return [(b >> 1) | 0xAA, b | 0xAA]
def data_nibbles(d):
    aux = [0] * 86
    for y in range(256):
        x = 85 - (y % 86); g = y // 86
        aux[x] |= ((d[y] & 1) << (2 * g + 1)) | (((d[y] >> 1) & 1) << (2 * g))
    seq = [aux[x] for x in range(85, -1, -1)] + [b >> 2 for b in d]
    out, prev = [], 0
    for v in seq:
        out.append(ENC[v ^ prev]); prev = v
    out.append(ENC[prev])
    return out
def sector(order, track, phys):
    l2p = DOS_L2P if order == "dos" else PRO_L2P
    fs = l2p.index(phys)
    d = [(fs * 37 + i * 11 + 5) & 255 for i in range(256)]
    r = [0xD5,0xAA,0x96] + f44(254) + f44(track) + f44(phys) + f44(254 ^ track ^ phys) + [0xDE,0xAA,0xEB] + [0xFF] * 6
    r += [0xD5,0xAA,0xAD] + data_nibbles(d) + [0xDE,0xAA,0xEB]
    r += [0xFF] * (416 - len(r))
    return r
for order, phys in (("dos", 5), ("dos", 15), ("prodos", 5), ("prodos", 12)):
    print(order, 3, phys, bytes(sector(order, 3, phys)).hex())
