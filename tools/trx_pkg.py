#!/usr/bin/env python3
"""TRX64 Ultimate update package: the one place that knows the trx_manifest layout.

docs/specs/T49-update-packages.md section 3.3 (as built).  The C side is the header this
module writes (`trx_pkg.py header`), vendor/gideon/software/application/
update_u2p/trx_manifest.h; nothing else may carry the layout.

Subcommands:
  blobs     build trx_blobs.bin (manifest, bitstream, app) from a bitstream and an app
  header    print the C header for the layout
"""
import argparse
import json
import struct
import sys
import zlib

MAGIC = b"TRXPKG02"
SYNC_WORD = 0xAA995566
REG_CMD = 0x04
REG_AXSS = 0x0D
CMD_DESYNC = 0x0D
FLASH_APPL = 0x220000          # the boot ROM's walk stops here (50T)
BIT_MAX = 0x1E0000             # bitstream slot (T265 D2): 0x1E0000-0x21FFFF is the recovery program and the guard's record
RAW_HDR = 48                   # synthesized .bit header in front of a raw image (raw_header)
APP_DATA_MAX = 0x1E0000 - 12   # app slot less the record header (T15 4.1)
PKG_MAX = 0x00A00000           # softboot STAGE_LIMIT
APP_LOAD = 0x00030000
PRE_FINGERPRINT = b"\xDE\xAD\x15\x41"
RECORD_LOAD = 0x03000000

# (name, offset, size, kind) -- kind: str bytes field, or u32.  Little endian, 4-aligned.
# bit_off, app_off and cfg_off are byte offsets from the start of the manifest; the blobs follow it
# (manifest, bitstream, app, config), see blobs().
FIELDS = [
    ("magic",       0, 8,  "bytes"),
    ("length",      8, 4,  "u32"),
    ("flags",      12, 4,  "u32"),
    ("id",         16, 32, "bytes"),
    ("bit_commit", 48, 16, "bytes"),
    ("app_commit", 64, 16, "bytes"),
    ("bit_off",    80, 4,  "u32"),
    ("bit_len",    84, 4,  "u32"),
    ("bit_axss",   88, 4,  "u32"),
    ("bit_chk",    92, 4,  "u32"),
    ("bit_crc",    96, 4,  "u32"),
    ("app_off",   100, 4,  "u32"),
    ("app_len",   104, 4,  "u32"),
    ("app_dest",  108, 4,  "u32"),
    ("app_dlen",  112, 4,  "u32"),
    ("app_run",   116, 4,  "u32"),
    ("app_sum",   120, 4,  "u32"),
    ("app_xrot",  124, 4,  "u32"),
    ("app_chk",   128, 4,  "u32"),
    ("app_crc",   132, 4,  "u32"),
    ("cfg_off",   136, 4,  "u32"),   # 0 and cfg_len 0 when the package carries no config pages
    ("cfg_len",   140, 4,  "u32"),
    ("cfg_chk",   144, 4,  "u32"),
    ("cfg_crc",   148, 4,  "u32"),
    ("crc",       152, 4,  "u32"),   # CRC-32 of bytes 0..151
]
MANIFEST_LEN = 156
FLAG_HTML = 1
FLAG_CONFIG_RESET = 2
FLAG_RESTORE = 4               # a restore bundle: AXSS of the bitstream is not ours to match, config pages may follow
FLAG_RAWBIT = 8                # restore bundle only: the bitstream blob is RAW_HDR + the raw flash image; only the image goes to the flash
CFG_ADDR = 0xFE8000            # the config pages in the flash (W25Q_Flash FLASH_ID_CONFIG), 0xFE8000-0xFFFFFF
CFG_LEN = 0x018000


# ---- raw bitstreams (openFPGALoader, no .bit header) ------------------------

def raw_header(raw_len):
    """The .bit header the bundle puts in front of a raw flash image (48 bytes, fields a-e, e = raw_len).
    update_trx.cc raw_header_make builds the same bytes."""
    hdr = bytes([0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01])
    hdr += b"a" + struct.pack(">H", 12) + b"raw flash".ljust(12, b"\0")
    for key in b"bcd":
        hdr += bytes([key]) + struct.pack(">H", 2) + b"x\0"
    hdr += b"e" + struct.pack(">I", raw_len)
    assert len(hdr) == RAW_HDR
    return hdr


def raw_detect(head):
    """True if head (the first 256 bytes of the flash) is a raw 7-series bitstream: the bus-width words
    00 00 00 BB 11 22 00 44, then the sync word AA 99 55 66, both inside 256 bytes."""
    head = bytes(head[:256])
    i = head.find(bytes([0, 0, 0, 0xBB, 0x11, 0x22, 0, 0x44]))
    return i >= 0 and head.find(struct.pack(">I", SYNC_WORD), i + 8) >= 0


def raw_length(slot):
    """Length of the raw image in the bitstream slot: the last byte that is not 0xFF, rounded up to 4."""
    n = len(slot)
    while n and slot[n - 1] == 0xFF:
        n -= 1
    return (n + 3) & ~3


# ---- sums ------------------------------------------------------------------

def chk(data):
    """Gideon's calc_checksum (update_common.h): 32 bit, shift-add with a fold."""
    c = 0
    for b in data:
        if c & 0x80000000:
            c ^= 0x801618D5
        c = ((c << 1) & 0xFFFFFFFF)
        c = (c + b) & 0xFFFFFFFF
    return c


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def pad4(data):
    return data + b"\0" * (-len(data) % 4)


def sum_xrot(app_file):
    """sum and xrot as the boot ROM's load() computes them (bootrom.c): over the
    ceil(length/4) little-endian words behind the 12 byte record header."""
    dest, length, run = struct.unpack_from("<III", app_file, 0)
    data = pad4(app_file[12:12 + length])
    s = 0
    x = 0
    for (w,) in struct.iter_unpack("<I", data):
        s = (s + w) & 0xFFFFFFFF
        x = (((x << 1) | (x >> 31)) & 0xFFFFFFFF) ^ w
    return s, x


def axss(bit):
    """The USR_ACCESS value the bitstream writes: bootrom.c flash_axss on a buffer
    (sync word in the first 4 KB, packets, first write to register 0x0D). None if none."""
    pos = 0
    sh = 0
    while sh != SYNC_WORD:
        if pos >= 4096 or pos >= len(bit):
            return None
        sh = ((sh << 8) | bit[pos]) & 0xFFFFFFFF
        pos += 1
    for _ in range(65536):
        if pos >= FLASH_APPL or pos + 4 > len(bit):
            break
        w = struct.unpack_from(">I", bit, pos)[0]
        pos += 4
        if (w >> 29) == 1:
            op = (w >> 27) & 3
            reg = (w >> 13) & 0x3FFF
            count = w & 0x7FF
            if op == 2 and count == 1:
                v = struct.unpack_from(">I", bit, pos)[0]
                pos += 4
                if reg == REG_AXSS:
                    return v
                if reg == REG_CMD and v == CMD_DESYNC:
                    break
                continue
        elif (w >> 29) == 2:
            count = w & 0x07FFFFFF
        else:
            break
        if count >= (FLASH_APPL >> 2):
            break
        pos += 4 * count
    return None


def bit_length_from_header(bit):
    """Length of a Xilinx .bit file from its header (softboot.c bit_length); 0 if not one."""
    magic = bytes([0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01])
    if bit[:13] != magic:
        return 0
    p = 13
    for key in b"abcd":
        if p + 3 > len(bit) or bit[p] != key:
            return 0
        p += 3 + ((bit[p + 1] << 8) | bit[p + 2])
    if p + 5 > len(bit) or bit[p] != ord("e"):
        return 0
    return p + 5 + struct.unpack_from(">I", bit, p + 1)[0]


def app_header(app):
    if len(app) < 12:
        raise ValueError("app file shorter than its record header")
    dest, length, run = struct.unpack_from("<III", app, 0)
    return dest, length, run


def check_app_record(app):
    """Raises ValueError unless the file is a record the boot ROM would accept (header_ok)."""
    dest, length, run = app_header(app)
    if length == 0 or length > APP_DATA_MAX:
        raise ValueError("app data length 0x%X not in 1..0x%X" % (length, APP_DATA_MAX))
    if dest & 3 or dest < 0x10000 or dest > 0x04000000 - length:
        raise ValueError("app destination 0x%X invalid" % dest)
    if not (dest <= run < dest + length):
        raise ValueError("app run address 0x%X outside the image" % run)
    if len(app) != 12 + ((length + 3) & ~3):
        raise ValueError("app file is %d bytes, record says %d" % (len(app), 12 + ((length + 3) & ~3)))
    return dest, length, run


# ---- manifest --------------------------------------------------------------

def _enc(v, size):
    b = v.encode("ascii") if isinstance(v, str) else bytes(v)
    if len(b) >= size:
        raise ValueError("%r does not fit %d bytes (NUL terminated)" % (v, size))
    return b + b"\0" * (size - len(b))


def layout(bit_len, app_len, cfg_len):
    """Offsets from the manifest start: manifest, bitstream, app, config, each 4-aligned."""
    bit_off = MANIFEST_LEN
    app_off = bit_off + ((bit_len + 3) & ~3)
    cfg_off = app_off + ((app_len + 3) & ~3) if cfg_len else 0
    end = (cfg_off + cfg_len) if cfg_len else app_off + ((app_len + 3) & ~3)
    return bit_off, app_off, cfg_off, end


def build_blobs(bit, app, pkg_id, bit_commit, app_commit, cfg=b"", flags=0):
    """The blob region of a package: trx_manifest, bitstream, app, config pages.  Raises ValueError
    for an input the updater (or the boot ROM) would refuse.  A restore package (FLAG_RESTORE) takes a
    bitstream without our AXSS stamp (a stock one) and must carry the config pages or none."""
    raw = bool(flags & FLAG_RAWBIT)
    if raw and not flags & FLAG_RESTORE:
        raise ValueError("a raw bitstream only in a restore package")
    if raw and bit[:RAW_HDR] != raw_header(len(bit) - RAW_HDR):
        raise ValueError("raw bitstream blob does not start with its synthesized header")
    if len(bit) - (RAW_HDR if raw else 0) > BIT_MAX:
        raise ValueError("bitstream is 0x%X bytes, slot is 0x%X" % (len(bit), BIT_MAX))
    if bit_length_from_header(bit) != len(bit):
        raise ValueError("bitstream is not a .bit file whose header length matches the file (header says %d, file %d)"
                         % (bit_length_from_header(bit), len(bit)))
    ax = axss(bit)
    if ax is None and not flags & FLAG_RESTORE:
        raise ValueError("no USR_ACCESS (AXSS) write found in the bitstream")
    if cfg and (len(cfg) != CFG_LEN or not flags & FLAG_RESTORE):
        raise ValueError("config pages only in a restore package, and exactly 0x%X bytes" % CFG_LEN)
    dest, dlen, run = check_app_record(app)
    s, x = sum_xrot(app)
    bit_off, app_off, cfg_off, end = layout(len(bit), len(app), len(cfg))
    v = {
        "magic": MAGIC, "length": MANIFEST_LEN, "flags": flags,
        "id": _enc(pkg_id, 32), "bit_commit": _enc(bit_commit, 16), "app_commit": _enc(app_commit, 16),
        "bit_off": bit_off, "bit_len": len(bit), "bit_axss": ax or 0, "bit_chk": chk(bit), "bit_crc": crc32(bit),
        "app_off": app_off, "app_len": len(app), "app_dest": dest, "app_dlen": dlen, "app_run": run,
        "app_sum": s, "app_xrot": x, "app_chk": chk(app), "app_crc": crc32(app),
        "cfg_off": cfg_off, "cfg_len": len(cfg), "cfg_chk": chk(cfg) if cfg else 0, "cfg_crc": crc32(cfg) if cfg else 0,
        "crc": 0,
    }
    out = bytearray(end)
    out[:MANIFEST_LEN] = pack(v)
    out[bit_off:bit_off + len(bit)] = bit
    out[app_off:app_off + len(app)] = app
    if cfg:
        out[cfg_off:cfg_off + len(cfg)] = cfg
    return bytes(out)


def pack(v):
    out = bytearray(MANIFEST_LEN)
    for name, off, size, kind in FIELDS:
        if kind == "u32":
            struct.pack_into("<I", out, off, v[name])
        else:
            out[off:off + size] = v[name]
    struct.pack_into("<I", out, MANIFEST_LEN - 4, crc32(bytes(out[:MANIFEST_LEN - 4])))
    return bytes(out)


def parse_manifest(m):
    """Dict of fields; raises ValueError on a bad magic, length or manifest CRC."""
    if len(m) < MANIFEST_LEN or m[:8] != MAGIC:
        raise ValueError("no TRXPKG02 manifest")
    v = {}
    for name, off, size, kind in FIELDS:
        v[name] = struct.unpack_from("<I", m, off)[0] if kind == "u32" else bytes(m[off:off + size])
    if v["length"] != MANIFEST_LEN:
        raise ValueError("manifest length %d, expected %d" % (v["length"], MANIFEST_LEN))
    if crc32(m[:MANIFEST_LEN - 4]) != v["crc"]:
        raise ValueError("manifest CRC mismatch")
    for k in ("id", "bit_commit", "app_commit"):
        v[k] = v[k].split(b"\0")[0].decode("ascii", "replace")
    return v


def restore_blobs(flash, pkg_id):
    """What the updater writes into TRXBKnnn.CFW from a flash image (>= 0x1000000 bytes): the saved
    bitstream (length from its .bit header at 0), the app record at 0x220000 and the config pages.
    Host twin of backup_cfw() in update_trx.cc, for tests.  Returns None when the flash holds no
    bitstream/app pair to restore (the updater then writes the raw backup only)."""
    blen = bit_length_from_header(flash[:512])
    flags = FLAG_RESTORE
    if blen and blen <= BIT_MAX:
        bit = flash[:blen]
    elif not blen and raw_detect(flash[:256]):
        rlen = raw_length(flash[:BIT_MAX])
        bit = raw_header(rlen) + flash[:rlen]
        flags |= FLAG_RAWBIT
    else:
        return None
    try:
        dest, dlen, run = check_app_record(flash[FLASH_APPL:FLASH_APPL + 12 + ((struct.unpack_from("<I", flash, FLASH_APPL + 4)[0] + 3) & ~3)])
    except (ValueError, struct.error):
        return None
    app = flash[FLASH_APPL:FLASH_APPL + 12 + ((dlen + 3) & ~3)]
    ax = axss(bit)
    return build_blobs(bit, app, pkg_id, "AXSS %08X" % ax if ax is not None else "no stamp", "",
                       flash[CFG_ADDR:CFG_ADDR + CFG_LEN], flags)


# ---- the C header ----------------------------------------------------------

def c_header():
    lines = [
        "// Generated by tools/trx_pkg.py header (TRX64-Ultimate). Do not edit: the layout lives there.",
        "// T49 trx_manifest, little endian, 4-aligned, %d bytes." % MANIFEST_LEN,
        "#ifndef TRX_MANIFEST_H",
        "#define TRX_MANIFEST_H",
        "#include <stdint.h>",
        "",
        "#define TRX_MANIFEST_MAGIC \"TRXPKG02\"",
        "#define TRX_MANIFEST_LEN   %d" % MANIFEST_LEN,
        "#define TRX_FLAG_HTML      %d" % FLAG_HTML,
        "#define TRX_FLAG_CONFIG_RESET %d" % FLAG_CONFIG_RESET,
        "#define TRX_FLAG_RESTORE   %d" % FLAG_RESTORE,
        "#define TRX_FLAG_RAWBIT    %d" % FLAG_RAWBIT,
        "#define TRX_CFG_ADDR       0x%06X" % CFG_ADDR,
        "#define TRX_CFG_LEN        0x%06X" % CFG_LEN,
        "",
        "struct trx_manifest {",
    ]
    for name, off, size, kind in FIELDS:
        if kind == "u32":
            lines.append("    uint32_t %s; // +%d" % (name, off))
        else:
            lines.append("    char %s[%d]; // +%d" % (name, size, off))
    lines += ["};", "",
              "static_assert(sizeof(struct trx_manifest) == %d, \"trx_manifest layout\");" % MANIFEST_LEN,
              "", "#endif", ""]
    return "\n".join(lines)


# ---- cli -------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("blobs")
    m.add_argument("bitstream")
    m.add_argument("app")
    m.add_argument("-o", "--out", required=True)
    m.add_argument("--id", required=True)
    m.add_argument("--bit-commit", required=True)
    m.add_argument("--app-commit", required=True)
    m.add_argument("--flags", type=int, default=0)
    sub.add_parser("header")
    a = ap.parse_args()
    if a.cmd == "header":
        sys.stdout.write(c_header())
        return 0
    bit = open(a.bitstream, "rb").read()
    app = open(a.app, "rb").read()
    try:
        man = build_blobs(bit, app, a.id, a.bit_commit, a.app_commit, flags=a.flags)
    except ValueError as e:
        print("trx_pkg: %s" % e, file=sys.stderr)
        return 1
    open(a.out, "wb").write(man)
    v = parse_manifest(man[:MANIFEST_LEN])
    print("blobs %s: %d bytes, id %s, bit %d AXSS %08X, app %d sum %08X xrot %08X" %
          (a.out, len(man), v["id"], v["bit_len"], v["bit_axss"], v["app_len"], v["app_sum"], v["app_xrot"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
