#!/usr/bin/env python3
"""T116 S5/S10: a FAT16 SD card image of our own for the Next core's benches: an MBR with one partition and a flat root directory of files whose
bytes the caller gives (the benches put their test program in as TBBLUE.FW, the file the Next's boot ROM loads). No NextZXOS, no ROM.

  tools/zxnext_img.py out.img [--size-mb 8] NAME.EXT=path [...]
  python: make_fat16(files={"TBBLUE.FW": bytes}, total_sectors=16384) -> bytes
  python: read_fat16(img) -> {name: bytes}   (the bench reads a written image back to check CMD24)

The geometry is the usual one: partition starts at LBA 2048, 2 sectors a cluster, 4 reserved sectors, two FATs, 64 root entries (four sectors: the boot ROM reads the whole directory, 5 ms a sector), type 0x06.
"""
import struct
import sys

SPC = 2
RESERVED = 4
ROOT_ENTRIES = 64
PART_START = 2048


def short_name(n):
    base, _, ext = n.upper().partition(".")
    if len(base) > 8 or len(ext) > 3:
        raise ValueError("not an 8.3 name: " + n)
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def geometry(total_sectors):
    part = total_sectors - PART_START
    root_secs = ROOT_ENTRIES * 32 // 512
    fat_secs = 1
    while True:
        data_secs = part - RESERVED - 2 * fat_secs - root_secs
        clusters = data_secs // SPC
        need = ((clusters + 2) * 2 + 511) // 512
        if need <= fat_secs:
            break
        fat_secs = need
    if not 4085 <= clusters < 65525:
        raise ValueError("%d clusters: not FAT16 (choose another size)" % clusters)
    return part, root_secs, fat_secs, clusters


def make_fat16(files, total_sectors=16384):
    part, root_secs, fat_secs, clusters = geometry(total_sectors)
    img = bytearray(total_sectors * 512)
    # MBR
    img[446:462] = struct.pack("<BBBBBBBBII", 0x80, 0xFE, 0xFF, 0xFF, 0x06, 0xFE, 0xFF, 0xFF, PART_START, part)
    img[510:512] = b"\x55\xAA"
    # boot sector
    b = bytearray(512)
    b[0:3] = b"\xEB\x3C\x90"
    b[3:11] = b"TRXNEXT "
    struct.pack_into("<HBHBHHBHHHII", b, 11, 512, SPC, RESERVED, 2, ROOT_ENTRIES, part if part < 65536 else 0, 0xF8, fat_secs, 63, 255, PART_START,
                     part if part >= 65536 else 0)
    b[36:38] = b"\x80\x00"
    b[38] = 0x29
    struct.pack_into("<I", b, 39, 0x20261022)
    b[43:54] = b"TRXNEXTCARD"
    b[54:62] = b"FAT16   "
    b[510:512] = b"\x55\xAA"
    base = PART_START * 512
    img[base:base + 512] = b
    fat = bytearray(fat_secs * 512)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    root = bytearray(root_secs * 512)
    data_off = base + (RESERVED + 2 * fat_secs + root_secs) * 512
    nxt = 2
    for i, (name, data) in enumerate(files.items()):
        need = max(1, (len(data) + SPC * 512 - 1) // (SPC * 512))
        if nxt - 2 + need > clusters:
            raise ValueError("the image is too small for " + name)
        for k in range(need):
            struct.pack_into("<H", fat, 2 * (nxt + k), (nxt + k + 1) if k < need - 1 else 0xFFFF)
        img[data_off + (nxt - 2) * SPC * 512:data_off + (nxt - 2) * SPC * 512 + len(data)] = data
        e = bytearray(32)
        e[0:11] = short_name(name)
        e[11] = 0x20
        struct.pack_into("<HHHHHHHI", e, 12, 0, 0, 0, 0, 0, 0x5A00, 0x4A2B + 0, 0)
        struct.pack_into("<H", e, 26, nxt)
        struct.pack_into("<I", e, 28, len(data))
        root[i * 32:(i + 1) * 32] = e
        nxt += need
    img[base + RESERVED * 512:base + (RESERVED + fat_secs) * 512] = fat
    img[base + (RESERVED + fat_secs) * 512:base + (RESERVED + 2 * fat_secs) * 512] = fat
    img[base + (RESERVED + 2 * fat_secs) * 512:base + (RESERVED + 2 * fat_secs + root_secs) * 512] = root
    return bytes(img)


def make_fw(payload):
    """TBBLUE.FW as the Next's boot ROM reads it (rom/bootrom.vhd, 0x0299-0x03D3): a header sector of four entries (skip sectors, load sectors; two
    16 bit little endian words each, the same four here), then the payload; the boot ROM loads `load` sectors of 512 bytes to 0x6000, switches the
    card off (OUT 0xE7, 0xFF) and jumps there."""
    n = (len(payload) + 511) // 512
    hdr = bytearray(512)
    for k in range(4):
        struct.pack_into("<HH", hdr, 4 * k, 0, n)
    return bytes(hdr) + payload + b"\x00" * (n * 512 - len(payload))


def read_fat16(img):
    base = PART_START * 512
    (bps, spc, rsv, nfat, rent, _tot16, _media, fsz) = struct.unpack_from("<HBHBHHBH", img, base + 11)
    root_secs = rent * 32 // 512
    fat = img[base + rsv * 512:base + (rsv + fsz) * 512]
    root = img[base + (rsv + nfat * fsz) * 512:base + (rsv + nfat * fsz + root_secs) * 512]
    data_off = base + (rsv + nfat * fsz + root_secs) * 512
    out = {}
    for i in range(rent):
        e = root[i * 32:(i + 1) * 32]
        if e[0] in (0, 0xE5) or e[11] & 0x08:
            continue
        name = e[0:8].decode().rstrip() + ("." + e[8:11].decode().rstrip() if e[8:11].strip() else "")
        cl, size = struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
        data = b""
        while cl < 0xFFF8 and len(data) < size:
            data += img[data_off + (cl - 2) * spc * bps:data_off + (cl - 1) * spc * bps]
            cl = struct.unpack_from("<H", fat, 2 * cl)[0]
        out[name] = data[:size]
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    size_mb = 8
    files = {}
    args = argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--size-mb":
            size_mb = int(args[i + 1])
            i += 2
            continue
        n, _, p = args[i].partition("=")
        files[n] = open(p, "rb").read()
        i += 1
    img = make_fat16(files, size_mb * 2048)
    open(argv[1], "wb").write(img)
    assert read_fat16(img) == files
    print("zxnext_img: %s, %d MB, %s" % (argv[1], size_mb, ", ".join("%s %d B" % (n, len(d)) for n, d in files.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
