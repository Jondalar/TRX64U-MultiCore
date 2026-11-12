#!/usr/bin/env python3
"""Synthetic vendor firmware files for tests/hostapp/test_fwconvert (T137 S1): made from tools/cor_pack.py and the test bitstream of
tools/test_cor_pack.py. No vendor file, no ROM.

  mkconvtest.py <outdir>

Files: vendor.cfw / vendor.ue2 (what Gideon's and Commodore's files look like: updater code, the .bit, the application record, blobs behind it),
icon.bin, expected_foreign.cor (what the host must write for vendor.cfw: the packer's bytes), and facts.txt."""
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import cor_pack as C          # noqa: E402
import test_cor_pack as TC    # noqa: E402

PRE = b"\xDE\xAD\x15\x41U64MK2" + b"\0" * 26          # 36 bytes: fingerprint, family, empty product
UPD_LOAD = 0x03000000                                   # the updater image's record, as in the real files


def sha(b):
    return hashlib.sha256(b).hexdigest()


def app_record(n=70000, seed=3):
    """A flash application record {0x30000, len, 0x30000} + data."""
    data = bytes(((i * 7 + seed) & 0xFF) for i in range(n))
    return struct.pack("<III", 0x30000, len(data), 0x30000) + data


def vendor_image(bit, app, with_pre=True):
    """What Gideon's files look like: updater code, the .bit, the application record, blobs (ESP32, ROMs) behind it."""
    code = bytearray(((i * 13 + 1) & 0xFF) for i in range(30000))
    head = (len(PRE) if with_pre else 0) + 12 + len(code) + len(bit)
    img = bytes(code) + bit + b"\0" * ((-head) % 4 + 4) + app + bytes(5000)        # the record lies 4-aligned within 16 bytes behind the .bit
    rec = struct.pack("<III", UPD_LOAD, len(img), UPD_LOAD)
    return (PRE if with_pre else b"") + rec + img


def main(out):
    os.makedirs(out, exist_ok=True)
    bit_stock = TC.full_bit(frames=2500, axss=None)     # a stock bitstream: no stamp of ours
    facts = {}

    def w(name, data):
        open(os.path.join(out, name), "wb").write(data)

    icon = bytes(((i * 3) & 0xFF) for i in range(2048))
    w("icon.bin", icon)
    v_app = app_record(120000, 11)
    w("vendor.cfw", vendor_image(bit_stock, v_app, True))
    w("vendor.ue2", vendor_image(bit_stock, v_app, False))
    # what the host must write for vendor.cfw (the same parts the packer would take: .bit as it is, the application's data, the icon, map by AXSS)
    parts_bit = bit_stock
    parts_app = v_app[12:]
    bit_sha = sha(parts_bit)
    w("expected_foreign.cor", C.pack_large("fw-" + bit_sha[:8], "top 2026/10/25 12:00:00", "bit 2026/10/25", parts_bit, parts_app, icon, origin="foreign"))
    # the manifest's origin as the reader must take it (cor.cc, D1/D2): a valid backup, and three manifests no honest writer makes
    app_bin = bytes(((i * 5 + 2) & 0xFF) for i in range(60001))
    w("backup.cor", C.pack_large("backup-001", "Commodore 1.1.0", "backup", bit_stock, app_bin, icon, origin="backup", flash_id="Winbond W25Q128"))
    m, r = C.parse(C.pack_large("x-001", "x", "1", bit_stock, app_bin, icon, origin="foreign"))

    def reissue(name, **kw):
        mm = dict(m)
        mm.update(kw)
        body = C.rec(1, json.dumps(mm, sort_keys=True, separators=(",", ":")).encode()) + C.rec(3, r[3]) + C.rec(4, r[4]) + C.rec(5, r[5])
        w(name, C.MAGIC + struct.pack("<I", len(body)) + body)
    reissue("origin_unknown.cor", origin="vendor")
    reissue("backup_noflash.cor", origin="backup")
    reissue("foreign_with_flash.cor", flash_bit=0)
    reissue("backup_other_layout.cor", origin="backup", flash_bit=0, flash_app=0x300000, flash_id="Winbond W25Q128")
    facts.update(v_bit_len=len(bit_stock), v_app_len=len(v_app), v_bit_sha=bit_sha, v_app_sha=sha(parts_app),
                 vendor_sha=sha(vendor_image(bit_stock, v_app, True)), vendor_ue2_sha=sha(vendor_image(bit_stock, v_app, False)))
    open(os.path.join(out, "facts.txt"), "w").write("".join("%s %s\n" % kv for kv in sorted(facts.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
