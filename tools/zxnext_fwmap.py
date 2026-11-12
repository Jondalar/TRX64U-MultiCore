#!/usr/bin/env python3
"""T116 S5 (board C): the sectors of a real Next SD card image (FAT32, the user's own, never in this repository) that the firmware reads while it starts,
as two files the bench mode `fwmap` of cores/zxnext/sim loads: KEYS (the LBAs, 4 bytes big endian each, ascending) and DATA (512 bytes per key). Every other
sector reads as zeros. The image stays where it is; the sectors keep their LBAs, so the geometry the firmware sees (8 sectors a cluster, FAT at 8,362, data at
16,512, 4,188,160 sectors) is the card's.

  tools/zxnext_fwmap.py card.img out.keys out.data [--dir /machines] [--file /TBBLUE.FW] [--max-file-kb 1024] [--lbas lbas.txt]

Taken: the MBR, the boot sector and its neighbours (LBA 128..143), both FATs, the root directory and every directory on the way to a path given, the
clusters of every file under each --dir (files up to --max-file-kb), each --file, and the sectors listed in --lbas (one number a line; the bench prints
the ones it found missing).
"""
import struct
import sys


class Card:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.P = struct.unpack_from("<I", self.rd(446 + 8, 4))[0] * 512      # the first partition
        b = self.rd(self.P, 512)
        self.spc = b[13]
        rsv, nfat = struct.unpack_from("<HB", b, 14)
        self.fsz = struct.unpack_from("<I", b, 36)[0]
        self.root = struct.unpack_from("<I", b, 44)[0]
        self.fat0 = self.P // 512 + rsv
        self.nfat = nfat
        self.data = self.fat0 + nfat * self.fsz

    def rd(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def sector(self, lba):
        return self.rd(lba * 512, 512)

    def fat(self, c):
        return struct.unpack("<I", self.rd(self.fat0 * 512 + 4 * c, 4))[0] & 0x0FFFFFFF

    def chain(self, c):
        out = []
        while 2 <= c < 0x0FFFFFF8 and len(out) < 1 << 20:
            out.append(c)
            c = self.fat(c)
        return out

    def lba_of(self, c):
        return self.data + (c - 2) * self.spc

    def clusters_lbas(self, c):
        return [self.lba_of(x) + i for x in self.chain(c) for i in range(self.spc)]

    def entries(self, c):
        d = b"".join(self.sector(l) for l in self.clusters_lbas(c))
        res, lfn = [], []
        for i in range(0, len(d), 32):
            e = d[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = []
                continue
            if e[11] == 0x0F:
                lfn.insert(0, e[1:11] + e[14:26] + e[28:32])
                continue
            nm = b"".join(lfn).decode("utf-16le", errors="ignore").split("\x00")[0] if lfn else ""
            lfn = []
            sn = e[0:8].decode("latin1").rstrip() + ("." + e[8:11].decode("latin1").rstrip() if e[8:11].strip() else "")
            cl = (struct.unpack("<H", e[20:22])[0] << 16) | struct.unpack("<H", e[26:28])[0]
            res.append((nm or sn, sn, e[11], cl, struct.unpack("<I", e[28:32])[0]))
        return res

    def find(self, path):
        c, trail = self.root, [self.root]
        ent = None
        for part in [x for x in path.split("/") if x]:
            for e in self.entries(c):
                if part.lower() in (e[0].lower(), e[1].lower()):
                    ent, c = e, e[3]
                    trail.append(c)
                    break
            else:
                raise SystemExit("not found: " + path)
        return ent, trail


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    card = Card(argv[1])
    dirs, files, maxkb, extra = [], [], 1024, []
    a = argv[4:]
    for i in range(0, len(a), 2):
        if a[i] == "--dir":
            dirs.append(a[i + 1])
        elif a[i] == "--file":
            files.append(a[i + 1])
        elif a[i] == "--max-file-kb":
            maxkb = int(a[i + 1])
        elif a[i] == "--lbas":
            extra += [int(x) for x in open(a[i + 1]).read().split()]
    lbas = set([0]) | set(range(128, 144))
    lbas |= set(range(card.fat0, card.fat0 + card.nfat * card.fsz))
    lbas |= set(card.clusters_lbas(card.root))

    def take_dir(c, depth):
        for e in card.entries(c):
            if e[1] in (".", ".."):
                continue
            if e[2] & 0x10:
                lbas.update(card.clusters_lbas(e[3]))
                take_dir(e[3], depth + 1)
            elif e[4] <= maxkb * 1024 and e[3]:
                lbas.update(card.clusters_lbas(e[3]))

    for d in dirs:
        ent, trail = card.find(d)
        for c in trail:
            lbas.update(card.clusters_lbas(c))
        take_dir(ent[3], 0)
    for p in files:
        ent, trail = card.find(p)
        for c in trail:
            lbas.update(card.clusters_lbas(c))
        lbas.update(card.clusters_lbas(ent[3]))
    lbas.update(extra)
    ks = sorted(lbas)
    with open(argv[2], "wb") as k, open(argv[3], "wb") as d:
        for l in ks:
            k.write(struct.pack(">I", l))
            d.write(card.sector(l))
    print("zxnext_fwmap: %d sectors (%d KiB)" % (len(ks), len(ks) // 2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
