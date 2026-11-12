#!/usr/bin/env python3
"""Synthetic firmware files for tests/hostapp/test_fwrestore (T137 restore): every kind the host tells apart, made from tools/trx_pkg.py and
tools/cor_pack.py (the real writers), small enough to keep in a temp dir. No vendor file, no ROM.

  mkrestoretest.py <outdir>

Files: vendor.cfw/.ue2 (a vendor-style image without a manifest), mc_update.cfw (a MultiCore update package: not restorable), tester.cfw, backup.cor,
backup_otherchip.cor, foreign.cor, our.cor, layer2.cor, and facts.txt (key value lines the C++ bench reads)."""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import cor_pack as C          # noqa: E402
import trx_pkg as P           # noqa: E402
import test_cor_pack as TC    # noqa: E402

PRE = b"\xDE\xAD\x15\x41U64MK2" + b"\0" * 26          # 36 bytes: fingerprint, family, empty product
UPD_LOAD = 0x03000000                                   # the updater image's record, as in the real files


def sha(b):
    return hashlib.sha256(b).hexdigest()


def app_record(n=70000, seed=3, marker=None):
    """A flash application record {0x30000, len, 0x30000} + data (multiple of 4)."""
    data = bytearray(((i * 7 + seed) & 0xFF) for i in range(n))
    if marker:
        data[1000:1000 + len(marker)] = marker
    data = bytes(data)
    return struct.pack("<III", 0x30000, len(data), 0x30000) + data


def container(blobs, with_pre, code_len=20000):
    """[pre-header] + updater record + code (which also holds the manifest magic as a string, as the real updater does) + blobs."""
    code = bytearray(((i * 11) & 0xFF) for i in range(code_len))
    code[100:108] = b"TRXPKG02"                          # the decoy the checker has to skip
    img = bytes(code) + blobs
    rec = struct.pack("<III", UPD_LOAD, len(img), UPD_LOAD)
    return (PRE if with_pre else b"") + rec + img


def vendor_image(bit, app, with_pre=True):
    """What Gideon's files look like: updater code, the .bit, the application record, blobs (ESP32, ROMs) behind it."""
    code = bytearray(((i * 13 + 1) & 0xFF) for i in range(30000))
    head = (len(PRE) if with_pre else 0) + 12 + len(code) + len(bit)
    img = bytes(code) + bit + b"\0" * ((-head) % 4 + 4) + app + bytes(5000)        # the record lies 4-aligned within 16 bytes behind the .bit
    rec = struct.pack("<III", UPD_LOAD, len(img), UPD_LOAD)
    return (PRE if with_pre else b"") + rec + img


def main(out):
    os.makedirs(out, exist_ok=True)
    bit = TC.full_bit(frames=3000)                      # IDCODE 50T, AXSS 0x553525D3
    bit_stock = TC.full_bit(frames=2500, axss=None)     # a stock bitstream: no stamp of ours
    facts = {}

    def w(name, data):
        open(os.path.join(out, name), "wb").write(data)

    mc_app = app_record(70000, 3, marker=b"TRX64-MULTICORE-HOSTAPP/1")
    blobs = P.build_blobs(bit, mc_app, "main-12345678", "12345678", "abcdef01")
    w("mc_update.cfw", container(blobs, True))
    w("mc_update.ue2", container(blobs, False))
    tester_app = app_record(90000, 9)
    w("tester.cfw", container(P.build_blobs(bit, tester_app, "main-87654321", "87654321", "abcdef02"), True))
    # a vendor image
    v_app = app_record(120000, 11)
    w("vendor.cfw", vendor_image(bit_stock, v_app, True))
    w("vendor.ue2", vendor_image(bit_stock, v_app, False))
    # large .cor: backup, foreign, ours
    app_bin = bytes(((i * 5 + 2) & 0xFF) for i in range(60001))
    icon = bytes(((i * 3) & 0xFF) for i in range(2048))
    w("icon.bin", icon)
    w("backup.cor", C.pack_large("backup-001", "Commodore 1.1.0", "backup", bit_stock, app_bin, icon, origin="backup", flash_id="Winbond W25Q128"))
    w("backup_otherchip.cor", C.pack_large("backup-002", "x", "backup", bit_stock, app_bin, icon, origin="backup", flash_id="Winbond W25Q64"))
    # T135 B2: a backup read from a flash that held MultiCore's host (a stamp in the bitstream, TRX64 in the application): not the firmware that ran before
    host_app = bytes(((i * 5 + 2) & 0xFF) for i in range(60001))
    host_app = host_app[:5000] + b"TRX64 host app" + host_app[5014:]
    w("backup_hostfw.cor", C.pack_large("backup-003", "Commodore 1.1.0", "backup", bit, host_app, icon, origin="backup", flash_id="Winbond W25Q128"))
    # T135 B1: a self-built firmware writes the stamp as well (issue #6, 1.1.1+pkrl) and names no TRX64: a firmware like any other, restorable
    w("backup_selfbuilt.cor", C.pack_large("backup-004", "Commodore 1.1.1 pkrl", "backup", bit, app_bin, icon, origin="backup", flash_id="Winbond W25Q128"))
    w("foreign.cor", C.pack_large("fw-00000001", "Somebody", "1", bit_stock, app_bin, icon, origin="foreign"))
    w("our.cor", C.pack_large("amiga1200", "Amiga 1200", "0.3.0", bit, app_bin, icon))
    # layer-2 .cor
    hb, _ = TC.bit_file()
    w("layer2.cor", C.pack("VIC-20", "0.1", hb, bytes(5000), 0x30000, 0x30000, "820eded2"))
    facts.update(bit_sha=sha(bit), stock_bit_sha=sha(bit_stock), backup_app_len=len(app_bin), v_bit_len=len(bit_stock), v_app_len=len(v_app),
                 vendor_sha=sha(vendor_image(bit_stock, v_app, True)), vendor_ue2_sha=sha(vendor_image(bit_stock, v_app, False)))
    open(os.path.join(out, "facts.txt"), "w").write("".join("%s %s\n" % kv for kv in sorted(facts.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
