#!/usr/bin/env python3
"""An independent exFAT checker for the format of T111 (S4): reads an image of a whole medium (MBR + exFAT volume) and says whether the
structures are the ones the exFAT specification asks for. It shares no code with FatFs's f_mkfs, which wrote them, and is written from the
specification (Microsoft exFAT file system specification 1.00, sections 3 and 7):

  MBR      one entry, type 0x07, starting at the expected sector (8192), reaching the end of the medium; the other three entries zero; 0x55AA
  boot     main region (sectors 0-10) and its checksum sector 11, the backup at 12-23 equal to it; the fields of the main boot sector against each
           other (offsets, lengths, cluster count, root cluster), the extended boot sectors' signature
  FAT      entries 0 and 1; the chains of the allocation bitmap, the up-case table and the root directory against the lengths their entries claim
  bitmap   exactly the clusters of those three chains are in use, nothing else, nothing beyond the cluster count
  up-case  its checksum against the entry's, decoded and compared with the real case mapping of the ASCII and Latin-1 letters
  root     the volume label, bitmap and up-case entries and then nothing

  exfat_check.py IMAGE [--start LBA] [--no-mbr]       exit 0 and one "OK" line, or exit 1 and a "FAIL" line per problem
"""
import os
import struct
import sys


class Problems(list):
    def add(self, text):
        self.append(text)


def rotr32(v):
    return ((v << 31) | (v >> 1)) & 0xFFFFFFFF


def checksum32(data, skip=()):
    s = 0
    for i, b in enumerate(data):
        if i in skip:
            continue
        s = (rotr32(s) + b) & 0xFFFFFFFF
    return s


def check(path, start=8192, mbr=True):
    p = Problems()
    f = open(path, "rb")
    size = os.fstat(f.fileno()).st_size
    nsect = size // 512

    def rd(off, n):
        f.seek(off)
        d = f.read(n)
        if len(d) != n:
            p.add("short read at byte %d (%d bytes)" % (off, n))
            d = d + bytes(n - len(d))
        return d

    # ---- the partition table ------------------------------------------------------------------------------------------------------------------
    if mbr:
        m = rd(0, 512)
        if m[510:512] != b"\x55\xaa":
            p.add("MBR: no 0x55AA")
        e = m[446:462]
        if e[0] != 0:
            p.add("MBR: entry 0 is flagged bootable")
        if e[4] != 0x07:
            p.add("MBR: entry 0 has type 0x%02X, not 0x07" % e[4])
        st, ln = struct.unpack("<II", e[8:16])
        if st != start:
            p.add("MBR: entry 0 starts at %d, not %d" % (st, start))
        if st + ln != nsect:
            p.add("MBR: entry 0 ends at %d, the medium has %d sectors" % (st + ln, nsect))
        if m[446 + 16:510] != bytes(510 - 446 - 16):
            p.add("MBR: entries 1-3 are not zero")
        if m[:440] != bytes(440):
            p.add("MBR: boot code is not zero")
        vol, vlen = st * 512, ln
    else:
        vol, vlen = 0, nsect
    # ---- the boot region ---------------------------------------------------------------------------------------------------------------------
    reg = rd(vol, 12 * 512)
    bak = rd(vol + 12 * 512, 12 * 512)
    b = reg[:512]
    if b[0:3] != b"\xeb\x76\x90":
        p.add("boot: jump code is %s" % b[0:3].hex())
    if b[3:11] != b"EXFAT   ":
        p.add("boot: file system name is %r" % b[3:11])
    if b[11:64] != bytes(53):
        p.add("boot: MustBeZero (11..63) is not zero")
    part_off, vol_len = struct.unpack("<QQ", b[64:80])
    fat_off, fat_len, heap_off, nclu, root_clu, serial = struct.unpack("<IIIIII", b[80:104])
    rev, flags = struct.unpack("<HH", b[104:108])
    bps, spc_shift, nfats, drv = b[108], b[109], b[110], b[111]
    if mbr and part_off not in (0, start):      # the specification: zero = "hosts shall ignore it"; FatFs writes the offset inside its own partition, 0
        p.add("boot: PartitionOffset %d, the table says %d" % (part_off, start))
    if vol_len != vlen:
        p.add("boot: VolumeLength %d, the partition has %d sectors" % (vol_len, vlen))
    if rev != 0x0100:
        p.add("boot: FileSystemRevision 0x%04X" % rev)
    if bps != 9:
        p.add("boot: BytesPerSectorShift %d" % bps)
    if spc_shift > 25 - 9 or bps + spc_shift > 25:
        p.add("boot: SectorsPerClusterShift %d out of range" % spc_shift)
    if nfats != 1:
        p.add("boot: NumberOfFats %d" % nfats)
    if b[510:512] != b"\x55\xaa":
        p.add("boot: BootSignature missing")
    if fat_off < 24:
        p.add("boot: FatOffset %d inside the boot region" % fat_off)
    if fat_off + fat_len > heap_off:
        p.add("boot: FAT reaches into the cluster heap")
    spc = 1 << spc_shift
    if heap_off + nclu * spc > vol_len:
        p.add("boot: the cluster heap reaches past the volume")
    if nclu != (vol_len - heap_off) // spc:
        p.add("boot: ClusterCount %d, the heap holds %d" % (nclu, (vol_len - heap_off) // spc))
    if nclu > 0xFFFFFFF5:
        p.add("boot: ClusterCount too large")
    if fat_len * 512 < (nclu + 2) * 4:
        p.add("boot: FatLength %d sectors cannot hold %d entries" % (fat_len, nclu + 2))
    if not (2 <= root_clu < nclu + 2):
        p.add("boot: FirstClusterOfRootDirectory %d" % root_clu)
    for i in range(1, 9):
        s = reg[i * 512:(i + 1) * 512]
        if s[510:512] != b"\x55\xaa" or s[508:510] != b"\x00\x00":
            p.add("boot: extended boot sector %d has no 0xAA550000 signature" % i)
    # the checksum: sectors 0..10 without VolumeFlags (106, 107) and PercentInUse (112)
    want = checksum32(reg[:11 * 512], skip={106, 107, 112})
    got = struct.unpack("<128I", reg[11 * 512:12 * 512])
    if any(g != want for g in got):
        p.add("boot: checksum sector 11 does not hold 0x%08X everywhere (first dword 0x%08X)" % (want, got[0]))
    if reg != bak:
        p.add("boot: the backup boot region (12..23) differs from the main one")
    if p:
        return p      # the rest reads offsets from these fields
    # ---- the FAT -----------------------------------------------------------------------------------------------------------------------------
    cbytes = 512 << spc_shift
    fat0 = rd(vol + fat_off * 512, 8)
    if struct.unpack("<II", fat0) != (0xFFFFFFF8, 0xFFFFFFFF):
        p.add("FAT: entries 0 and 1 are %s" % fat0.hex())

    def fat_entry(c):
        return struct.unpack("<I", rd(vol + fat_off * 512 + c * 4, 4))[0]

    def chain(first, nbytes, what):
        n_need = (nbytes + cbytes - 1) // cbytes
        out, c, seen = [], first, set()
        while True:
            if c < 2 or c >= nclu + 2:
                p.add("FAT: the chain of the %s leaves the heap at cluster %d" % (what, c))
                return out
            if c in seen:
                p.add("FAT: the chain of the %s loops at %d" % (what, c))
                return out
            seen.add(c)
            out.append(c)
            nxt = fat_entry(c)
            if nxt == 0xFFFFFFFF:
                break
            c = nxt
            if len(out) > n_need + 1:
                p.add("FAT: the chain of the %s is longer than %d cluster(s)" % (what, n_need))
                return out
        if len(out) != n_need:
            p.add("FAT: the chain of the %s has %d cluster(s), %d bytes need %d" % (what, len(out), nbytes, n_need))
        return out

    def read_chain(cl, nbytes):
        data = b""
        for c in cl:
            data += rd(vol + (heap_off + (c - 2) * spc) * 512, cbytes)
        return data[:nbytes]

    # ---- the root directory ------------------------------------------------------------------------------------------------------------------
    root_chain = chain(root_clu, cbytes, "root directory")        # one cluster is what a fresh volume has
    root = read_chain(root_chain, len(root_chain) * cbytes)
    ents = {"label": None, "bitmap": None, "upcase": None}
    end = False
    for o in range(0, len(root), 32):
        e = root[o:o + 32]
        t = e[0]
        if t == 0x00:
            end = True
            continue
        if end:
            p.add("root: an entry (type 0x%02X) after the end of the directory" % t)
            continue
        if t == 0x83:
            if e[1] > 11:
                p.add("root: label with %d characters" % e[1])
            ents["label"] = e
        elif t == 0x81:
            ents["bitmap"] = e
        elif t == 0x82:
            ents["upcase"] = e
        else:
            p.add("root: unexpected entry type 0x%02X" % t)
    for k, v in ents.items():
        if v is None:
            p.add("root: no %s entry" % k)
    if p:
        return p
    bm = ents["bitmap"]
    bm_clu, bm_len = struct.unpack("<IQ", bm[20:32])
    if bm[1] & 1:
        p.add("root: the bitmap entry is the second bitmap")
    if bm_len != (nclu + 7) // 8:
        p.add("bitmap: DataLength %d, %d clusters need %d" % (bm_len, nclu, (nclu + 7) // 8))
    uc = ents["upcase"]
    uc_sum = struct.unpack("<I", uc[4:8])[0]
    uc_clu, uc_len = struct.unpack("<IQ", uc[20:32])
    bm_chain = chain(bm_clu, bm_len, "allocation bitmap")
    uc_chain = chain(uc_clu, uc_len, "up-case table")
    used = set(bm_chain) | set(uc_chain) | set(root_chain)
    if len(used) != len(bm_chain) + len(uc_chain) + len(root_chain):
        p.add("FAT: two of the system chains share a cluster")
    # ---- the allocation bitmap ----------------------------------------------------------------------------------------------------------------
    bits = read_chain(bm_chain, bm_len)
    for c in range(2, nclu + 2):
        v = (bits[(c - 2) // 8] >> ((c - 2) % 8)) & 1
        if v != (1 if c in used else 0):
            p.add("bitmap: cluster %d is %s, expected %s" % (c, "in use" if v else "free", "in use" if c in used else "free"))
            if len(p) > 8:
                break
    if nclu % 8 and bits[-1] >> (nclu % 8):
        p.add("bitmap: bits beyond the cluster count are set")
    # ---- the up-case table -------------------------------------------------------------------------------------------------------------------
    tbl = read_chain(uc_chain, uc_len)
    if uc_len % 2:
        p.add("up-case: odd length")
    if checksum32(tbl) != uc_sum:
        p.add("up-case: checksum 0x%08X, the entry says 0x%08X" % (checksum32(tbl), uc_sum))
    words = struct.unpack("<%dH" % (len(tbl) // 2), tbl[:len(tbl) // 2 * 2])
    mp, i, ch = [], 0, 0
    while i < len(words):
        w = words[i]
        if w == 0xFFFF and i + 1 < len(words):
            mp.extend(range(ch, ch + words[i + 1]))        # an identity run of that many characters
            ch += words[i + 1]
            i += 2
        else:
            mp.append(w)
            ch += 1
            i += 1
    if len(mp) > 65536:
        p.add("up-case: decodes to %d characters" % len(mp))
    else:
        def up(c):
            return mp[c] if c < len(mp) else c
        for c in list(range(0x20, 0x7F)) + list(range(0xC0, 0x100)):
            ref = chr(c).upper()
            if len(ref) == 1:
                ref = ord(ref)
            else:
                ref = c
            if c == 0xDF or c == 0xFF or c == 0xB5:       # the sharp s, y diaeresis and micro sign: their upper case is not in Latin-1 / is a special case
                continue
            if up(c) != ref:
                p.add("up-case: 0x%04X maps to 0x%04X, the case mapping says 0x%04X" % (c, up(c), ref))
                break
    return p


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 2
    start, mbr = 8192, True
    args = argv[1:]
    img = args[0]
    i = 1
    while i < len(args):
        if args[i] == "--start":
            start = int(args[i + 1])
            i += 2
        elif args[i] == "--no-mbr":
            mbr = False
            i += 1
        else:
            print("unknown argument", args[i])
            return 2
    probs = check(img, start, mbr)
    if probs:
        for t in probs:
            print("FAIL %s: %s" % (os.path.basename(img), t))
        return 1
    print("OK %s: MBR, boot regions and checksum, FAT, bitmap, up-case table and root directory are as the specification asks" % os.path.basename(img))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
