#!/usr/bin/env python3
"""List or extract the PRG files of a .d64 (35 tracks, standard layout).

    tools/d64.py image.d64               list
    tools/d64.py image.d64 outdir        write every PRG to outdir/<name>.prg
"""
import os
import sys

SECTORS = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def offset(track, sector):
    return (sum(SECTORS[:track - 1]) + sector) * 256


def chain(img, track, sector):
    out = bytearray()
    seen = set()
    while track and (track, sector) not in seen:
        seen.add((track, sector))
        blk = img[offset(track, sector):offset(track, sector) + 256]
        track, sector = blk[0], blk[1]
        out += blk[2:] if track else blk[2:sector + 1]
    return bytes(out)


def entries(img):
    t, s = 18, 1
    while t:
        blk = img[offset(t, s):offset(t, s) + 256]
        for i in range(8):
            e = blk[i * 32:i * 32 + 32]
            ftype = e[2] & 0x07
            if e[2] & 0x80 and ftype == 2:          # closed PRG
                name = e[5:21].split(b"\xa0")[0].decode("latin-1")
                yield name, e[3], e[4]
        t, s = blk[0], blk[1]


def main(argv):
    img = open(argv[0], "rb").read()
    for name, t, s in entries(img):
        data = chain(img, t, s)
        print(f"{name:16s} ${data[0] | data[1] << 8:04X} {len(data) - 2:6d} bytes")
        if len(argv) > 1:
            os.makedirs(argv[1], exist_ok=True)
            safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in name)
            open(os.path.join(argv[1], safe + ".prg"), "wb").write(data)


if __name__ == "__main__":
    main(sys.argv[1:])
