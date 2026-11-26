#!/usr/bin/env python3
"""The MultiCore update package, .txu (T152 D3/D5): the writer. The reader and the checks on the board are host-app/software/hostapp/txu.cc; this table
is the same as the comment in txu.h (the selftest compares the constants).

  txu_pack.py pack  [--host X.bit] [--app Y.app] --version V --commit C -o out.txu [--iface-provided N] [--iface-needed N]
  txu_pack.py inspect FILE.txu          prints the header and checks every part against its SHA-256
  txu_pack.py selftest                  the writer against its own reader, every refusal with a control

Layout (little endian):
   0 "TXU1" | 4 u32 hdr_len (80 + 48n) | 8 u32 flags (0) | 12 u32 host_key | 16 u32 iface_provided | 20 u32 iface_needed | 24 u32 nparts
  28 char origin[8] "trx" | 36 char version[24] | 60 char commit[16] | 76 n x { u32 kind (1 host, 2 app); u32 off; u32 size; u32 flash_addr; u8 sha256[32] }
  hdr_len-4 u32 CRC-32 of everything before it. Then the parts, 4-aligned; the file ends with the last one.
  host part = the .bit as written at flash 0x000000 (carries the host key = its AXSS stamp); app part = the record and its data as written at 0x220000.
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cor_pack as C   # noqa: E402
import trx_pkg as P    # noqa: E402

MAGIC = b"TXU1"
FIXED = 80                     # 76 bytes of header + the CRC; each part adds PART
PART = 48
KIND_HOST, KIND_APP = 1, 2
FLASH_ADDR = {KIND_HOST: 0x000000, KIND_APP: 0x220000}
SLOT = {KIND_HOST: 0x1E0000, KIND_APP: 0x1E0000}   # T265 D2: the host bitstream ends at 0x1DFFFF, 0x1E0000-0x21FFFF is the recovery and the guard's record
IFACE_PROVIDED = 1             # host-app/software/hostapp/txu.h TXU_IFACE_PROVIDED / TXU_IFACE_NEEDED (the selftest compares)
IFACE_NEEDED = 1


class TxuError(Exception):
    pass


def _str(v, n, what):
    b = v.encode("ascii")
    if len(b) >= n:
        raise TxuError("%s '%s' does not fit %d bytes" % (what, v, n - 1))
    return b + b"\0" * (n - len(b))


def pack(host=None, app=None, version="", commit="", iface_provided=IFACE_PROVIDED, iface_needed=IFACE_NEEDED):
    """A .txu from a host bitstream (.bit) and/or a host app (.app: record + data). Raises TxuError for an input the host would refuse."""
    parts = []        # (kind, bytes)
    host_key = 0
    if host is not None:
        if P.bit_length_from_header(host) != len(host):
            raise TxuError("host: not a .bit file whose header length matches the file")
        if len(host) > SLOT[KIND_HOST]:
            raise TxuError("host: %d bytes, the slot is %d" % (len(host), SLOT[KIND_HOST]))
        try:
            idcode, ax = C.scan_config(host)
        except C.CorError as e:
            raise TxuError("host: %s" % e)
        if idcode is None or (idcode ^ C.IDCODE_XC7A50T) & C.IDCODE_MASK:
            raise TxuError("host: IDCODE %s is not the XC7A50T" % ("%08x" % idcode if idcode else "missing"))
        if not ax:
            raise TxuError("host: no AXSS stamp (the host key) in the bitstream")
        if P.axss(host) != ax:
            raise TxuError("host: the boot ROM's walk finds another stamp than the packer")
        host_key = ax
        parts.append((KIND_HOST, host))
    else:
        iface_provided = 0
    if app is not None:
        try:
            P.check_app_record(app)
        except ValueError as e:
            raise TxuError("app: %s" % e)
        if len(app) > SLOT[KIND_APP]:
            raise TxuError("app: %d bytes, the slot is %d" % (len(app), SLOT[KIND_APP]))
        parts.append((KIND_APP, app))
    else:
        iface_needed = 0
    if not parts:
        raise TxuError("nothing to pack: give --host and/or --app")
    if (host is not None and not iface_provided) or (app is not None and not iface_needed):
        raise TxuError("an interface version of 0 means none")
    n = len(parts)
    hdr_len = FIXED + PART * n
    hdr = bytearray()
    hdr += MAGIC + struct.pack("<IIIIII", hdr_len, 0, host_key, iface_provided, iface_needed, n)
    hdr += _str("trx", 8, "origin") + _str(version, 24, "version") + _str(commit, 16, "commit")
    off = hdr_len
    body = b""
    for i, (kind, data) in enumerate(parts):
        hdr += struct.pack("<IIII", kind, off, len(data), FLASH_ADDR[kind]) + hashlib.sha256(data).digest()
        body += data
        off += len(data)
        if i + 1 < n:                                                      # 4-aligned offsets; the file ends with the last part
            body += b"\0" * (-len(data) % 4)
            off += -len(data) % 4
    hdr += struct.pack("<I", zlib.crc32(bytes(hdr)) & 0xFFFFFFFF)
    assert len(hdr) == hdr_len
    return bytes(hdr) + body


def parse(blob):
    """The same checks as txu.cc txu_probe; returns a dict, raises TxuError."""
    if blob[:4] != MAGIC:
        raise TxuError("not a MultiCore update file (magic)")
    if len(blob) < FIXED + PART:
        raise TxuError("truncated")
    hdr_len, flags, host_key, ip, inn, n = struct.unpack_from("<IIIIII", blob, 4)
    if n not in (1, 2) or hdr_len != FIXED + PART * n or len(blob) < hdr_len or flags != 0:
        raise TxuError("header fields out of range")
    if zlib.crc32(blob[:hdr_len - 4]) & 0xFFFFFFFF != struct.unpack_from("<I", blob, hdr_len - 4)[0]:
        raise TxuError("header CRC")
    origin = blob[28:36].split(b"\0")[0].decode("ascii", "replace")
    if origin != "trx":
        raise TxuError("origin '%s' is not trx" % origin)
    m = {"origin": origin, "version": blob[36:60].split(b"\0")[0].decode(), "commit": blob[60:76].split(b"\0")[0].decode(),
         "host_key": host_key, "iface_provided": ip, "iface_needed": inn, "parts": []}
    end = hdr_len
    for i in range(n):
        p = 76 + PART * i
        kind, off, size, addr = struct.unpack_from("<IIII", blob, p)
        sha = blob[p + 16:p + 48]
        if kind not in FLASH_ADDR or addr != FLASH_ADDR[kind] or size == 0 or size > SLOT[kind] or off & 3 or off < end or off + size > len(blob):
            raise TxuError("part %d out of range" % i)
        if any(q["kind"] == kind for q in m["parts"]):
            raise TxuError("a kind twice")
        m["parts"].append({"kind": kind, "off": off, "size": size, "addr": addr, "sha": sha})
        end = off + size
    if end != len(blob):
        raise TxuError("bytes behind the last part")
    kinds = [q["kind"] for q in m["parts"]]
    if (KIND_HOST in kinds) != bool(host_key and ip):
        raise TxuError("host part and host key / interface disagree")
    if (KIND_APP in kinds) != bool(inn):
        raise TxuError("app part and interface disagree")
    return m


def verify(blob, m):
    """Every part against its SHA-256 (the board does the same in txu_stage)."""
    for q in m["parts"]:
        if hashlib.sha256(blob[q["off"]:q["off"] + q["size"]]).digest() != q["sha"]:
            raise TxuError("part kind %d does not match its SHA-256" % q["kind"])


def cmd_pack(a):
    host = open(a.host, "rb").read() if a.host else None
    app = open(a.app, "rb").read() if a.app else None
    blob = pack(host, app, a.version, a.commit, a.iface_provided, a.iface_needed)
    with open(a.out, "wb") as f:
        f.write(blob)
    print("%s: %d bytes, %s" % (a.out, len(blob), " + ".join(n for n, x in (("host", host), ("app", app)) if x is not None)))


def cmd_inspect(a):
    blob = open(a.file, "rb").read()
    m = parse(blob)
    verify(blob, m)
    print("%s  origin %s  version %s  commit %s" % (a.file, m["origin"], m["version"], m["commit"]))
    print("  host key %08x  interface provided %d  needed %d" % (m["host_key"], m["iface_provided"], m["iface_needed"]))
    for q in m["parts"]:
        print("  %-4s %8d bytes at flash 0x%06X  sha256 %s" % ("host" if q["kind"] == KIND_HOST else "app", q["size"], q["addr"], q["sha"].hex()))


def _selftest():
    import test_cor_pack as TC
    fails = []

    def check(c, what):
        if not c:
            fails.append(what)
            print("FAIL " + what)

    def refuses(f, what):
        try:
            f()
        except TxuError:
            return
        fails.append(what)
        print("FAIL does not refuse: " + what)

    bit = TC.full_bit(frames=3000)
    app = struct.pack("<III", 0x30000, 70000, 0x30000) + bytes((i * 7 + 3) & 0xFF for i in range(70000))
    odd_app = struct.pack("<III", 0x30000, 70001, 0x30000) + bytes((i * 7 + 3) & 0xFF for i in range(70001)) + b"\0\0\0"
    for h, a in ((bit, app), (bit, None), (None, app), (bit, odd_app)):
        blob = pack(h, a, "main-1a2b3c4d", "1a2b3c4d5e6f708")
        m = parse(blob)
        verify(blob, m)
        check(len(m["parts"]) == (h is not None) + (a is not None), "parts present")
        check((m["host_key"] != 0) == (h is not None), "host key only with a host part")
        for q in m["parts"]:
            data = h if q["kind"] == KIND_HOST else a
            check(blob[q["off"]:q["off"] + q["size"]] == data, "part bytes survive")
    check(parse(pack(bit, app))["host_key"] == P.axss(bit), "host key = AXSS")
    # the interface constants are the ones of txu.h
    hh = open(os.path.join(HERE, "..", "host-app", "software", "hostapp", "txu.h")).read()
    check(int(re.search(r"#define TXU_IFACE_PROVIDED (\d+)u", hh).group(1)) == IFACE_PROVIDED, "TXU_IFACE_PROVIDED = IFACE_PROVIDED")
    check(int(re.search(r"#define TXU_IFACE_NEEDED +(\d+)u", hh).group(1)) == IFACE_NEEDED, "TXU_IFACE_NEEDED = IFACE_NEEDED")
    # the guard's constants are the boot ROM's (host/bootrom/bootrom.c)
    bc = open(os.path.join(HERE, "..", "host", "bootrom", "bootrom.c")).read()
    check(int(re.search(r"#define HOST_IFACE +(\d+)", bc).group(1)) == IFACE_PROVIDED, "HOST_IFACE = IFACE_PROVIDED")
    check(int(re.search(r"#define FLASH_APPL +0x([0-9A-Fa-f]+)", bc).group(1), 16) == FLASH_ADDR[KIND_APP], "FLASH_APPL = app flash address")
    check(int(re.search(r"#define RECOVERY_BELOW +0x([0-9A-Fa-f]+)", bc).group(1), 16) == FLASH_ADDR[KIND_APP] - SLOT[KIND_HOST],
          "recovery slot starts where the host bitstream slot ends")
    check(int(re.search(r"#define MAX_APPL_SIZE +\(0x([0-9A-Fa-f]+)", bc).group(1), 16) == SLOT[KIND_APP], "MAX_APPL_SIZE = app slot")
    blob = pack(bit, app, "v", "c")
    # every refusal of the writer
    refuses(lambda: pack(None, None), "nothing to pack")
    refuses(lambda: pack(bit[:-1], app), "a .bit shorter than its header says")
    refuses(lambda: pack(TC.full_bit(axss=None), app), "no AXSS stamp")
    refuses(lambda: pack(TC.full_bit(idcode=0x03631093), app), "a 100T bitstream")
    refuses(lambda: pack(bit, app[:-4]), "an app record that disagrees with its length")
    refuses(lambda: pack(bit, struct.pack("<III", 0x30001, 8, 0x30001) + bytes(8)), "a load address not word aligned")
    refuses(lambda: pack(bit, app, version="x" * 24), "a version that does not fit")
    refuses(lambda: pack(bit, app, iface_provided=0), "interface 0 for a host")
    # T265 D2: the slot ends at 0x1DFFFF; a bigger bitstream would cover the recovery program (control: the largest that fits)
    fits = TC.full_bit(frames=(SLOT[KIND_HOST] - 400) // 4)
    check(len(fits) <= SLOT[KIND_HOST] and len(fits) > SLOT[KIND_HOST] - 400, "the control bitstream fills the slot")
    check(len(pack(fits, app)) > len(fits), "a bitstream that fills the slot is accepted")
    refuses(lambda: pack(TC.full_bit(frames=SLOT[KIND_HOST] // 4 + 8), app), "a bitstream larger than the slot (the recovery's room)")
    # every refusal of the reader (what txu.cc refuses too)
    def mut(i, v):
        b = bytearray(blob)
        b[i] ^= v
        return bytes(b)
    refuses(lambda: parse(b"TXU2" + blob[4:]), "another magic")
    refuses(lambda: parse(mut(100, 1)), "a flipped header bit (CRC)")
    refuses(lambda: parse(blob + b"\0"), "a byte behind the last part")
    refuses(lambda: parse(blob[:-1]), "a truncated file")
    refuses(lambda: verify(mut(len(blob) - 5, 1), parse(mut(len(blob) - 5, 1))), "a flipped payload bit (SHA-256)")
    fix = bytearray(blob)
    fix[28:36] = b"foreign\0"
    hdr_len = struct.unpack_from("<I", fix, 4)[0]
    struct.pack_into("<I", fix, hdr_len - 4, zlib.crc32(bytes(fix[:hdr_len - 4])) & 0xFFFFFFFF)
    refuses(lambda: parse(bytes(fix)), "another origin with a valid CRC")
    # the dispatch the board relies on: a .cfw is never a .txu
    check(parse(blob)["origin"] == "trx", "origin trx")
    if fails:
        print("txu_pack selftest: %d failed" % len(fails))
        return 1
    print("txu_pack selftest: ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--host")
    p.add_argument("--app")
    p.add_argument("--version", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--iface-provided", type=int, default=IFACE_PROVIDED)
    p.add_argument("--iface-needed", type=int, default=IFACE_NEEDED)
    p.add_argument("-o", "--out", required=True)
    p = sub.add_parser("inspect")
    p.add_argument("file")
    sub.add_parser("selftest")
    a = ap.parse_args()
    try:
        if a.cmd == "pack":
            cmd_pack(a)
        elif a.cmd == "inspect":
            cmd_inspect(a)
        else:
            return _selftest()
    except TxuError as e:
        print("txu_pack: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
