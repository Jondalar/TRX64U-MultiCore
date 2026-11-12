#!/usr/bin/env python3
"""Packages for tests/hostapp/test_txu.cc (T152 S1-S4): the real writer (tools/txu_pack.py), small enough to keep in a temp dir.

  mktxutest.py <outdir>

Files: both.txu (host + app), app_only.txu, host_only.txu, newkey.txu (host with another key), iface_bad.txu / iface_fixed.txu (the S3 pair: the app needs
interface 2 / 1), host_iface2.txu (a host that provides interface 2), plus broken ones: crc.txu, trailing.txu, payload.txu (a flipped payload bit),
idcode100t.txu, keymismatch.txu (header host key differs from the bitstream's), badrecord.txu, foreign_origin.txu; vendor-like files that are not a .txu:
not_txu.cfw, not_txu.ue2; and facts.txt (key value lines)."""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import cor_pack as C          # noqa: E402
import trx_pkg as P           # noqa: E402
import txu_pack as X          # noqa: E402
import test_cor_pack as TC    # noqa: E402


def fix_crc(b):
    b = bytearray(b)
    hl = struct.unpack_from("<I", b, 4)[0]
    struct.pack_into("<I", b, hl - 4, zlib.crc32(bytes(b[:hl - 4])) & 0xFFFFFFFF)
    return bytes(b)


def main(out):
    os.makedirs(out, exist_ok=True)
    bit = TC.full_bit(frames=3000)                                    # IDCODE 50T, AXSS 0x553525D3
    bit2 = TC.full_bit(frames=2800, axss=0x1234ABCD)                  # another host key
    app = struct.pack("<III", 0x30000, 70000, 0x30000) + bytes((i * 7 + 3) & 0xFF for i in range(70000))
    app2 = struct.pack("<III", 0x30000, 70004, 0x30000) + bytes((i * 5 + 9) & 0xFF for i in range(70004))
    ver, com = "main-1a2b3c4d", "1a2b3c4d5e6f708"

    def w(name, data):
        open(os.path.join(out, name), "wb").write(data)

    both = X.pack(bit, app, ver, com)
    w("both.txu", both)
    w("app_only.txu", X.pack(None, app, ver, com))
    w("app2_only.txu", X.pack(None, app2, ver, com))
    w("host_only.txu", X.pack(bit, None, ver, com))
    w("newkey.txu", X.pack(bit2, app, ver, com))
    w("iface_bad.txu", X.pack(None, app, ver, com, iface_needed=2))
    w("iface_fixed.txu", X.pack(None, app, ver, com, iface_needed=1))
    w("host_iface2.txu", X.pack(bit, None, ver, com, iface_provided=2))
    w("both_iface_mismatch.txu", X.pack(bit, app, ver, com, iface_provided=2, iface_needed=1))
    # broken ones
    b = bytearray(both)
    b[100] ^= 1
    w("crc.txu", bytes(b))
    w("trailing.txu", both + b"\0\0\0\0")
    m = X.parse(both)
    b = bytearray(both)
    b[m["parts"][1]["off"] + 20] ^= 1                                  # a bit of the app part
    w("payload_app.txu", bytes(b))
    b = bytearray(both)
    b[m["parts"][0]["off"] + 40] ^= 1                                  # a bit of the host part
    w("payload_host.txu", bytes(b))
    C.IDCODE_XC7A50T = 0x03631093                                      # the packer refuses a 100T; this file is made around it
    w("idcode100t.txu", X.pack(TC.full_bit(idcode=0x03631093, frames=3000), app, ver, com))
    C.IDCODE_XC7A50T = 0x0362C093
    b = bytearray(both)
    struct.pack_into("<I", b, 12, 0xDEADBEEF)                          # the header names a key the bitstream does not carry
    w("keymismatch.txu", fix_crc(b))
    b = bytearray(both)
    b[28:36] = b"foreign\0"
    w("foreign_origin.txu", fix_crc(b))
    b = bytearray(both)
    struct.pack_into("<I", b, 76 + 12, 0x1000)                         # the host part claims another place in the flash
    w("addr_bad.txu", fix_crc(b))
    saved_len = P.bit_length_from_header                                # a bitstream with four bytes behind the length its header states
    P.bit_length_from_header = lambda b: len(b)
    w("bitlen.txu", X.pack(bit + b"\0\0\0\0", app, ver, com))
    P.bit_length_from_header = saved_len
    # a well-formed package with an application record the boot ROM would not take: made by hand (the writer refuses it)
    bad_app = struct.pack("<III", 0x30001, 70000, 0x30001) + bytes(70000)
    saved = P.check_app_record
    P.check_app_record = lambda a: (0, 0, 0)
    w("badrecord.txu", X.pack(None, bad_app, ver, com))
    P.check_app_record = saved
    # a vendor-style file: the "firmware" the host starts, never installs
    w("not_txu.cfw", bytes((i * 11) & 0xFF for i in range(50000)))
    w("not_txu.ue2", b"\xDE\xAD\x15\x41U64MK2" + bytes(60000))
    facts = {
        "key": "%08x" % P.axss(bit), "key2": "%08x" % P.axss(bit2),
        "bit_len": len(bit), "bit2_len": len(bit2), "app_len": len(app), "app2_len": len(app2),
    }
    with open(os.path.join(out, "facts.txt"), "w") as f:
        for k, v in facts.items():
            f.write("%s %s\n" % (k, v))
    open(os.path.join(out, "bit.bin"), "wb").write(bit)
    open(os.path.join(out, "bit2.bin"), "wb").write(bit2)
    open(os.path.join(out, "app.bin"), "wb").write(app)
    open(os.path.join(out, "app2.bin"), "wb").write(app2)


if __name__ == "__main__":
    main(sys.argv[1])
