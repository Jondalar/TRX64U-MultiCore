#!/usr/bin/env python3
"""Host checker for a TRX64 Ultimate update package (.cfw or .ue2), T49 spec 3.4.

  tools/trx_pkg_check.py [--bitstream c64_basic.bit] [--app ultimate.app] pkg.cfw [pkg.ue2 ...]

Checks: pre-header (.cfw: DE AD 15 41, family U64MK2, empty product), the record header,
the manifest (magic, length, CRC), the blobs at their places and, with --bitstream/--app,
byte-equal to those inputs; CRC-32 and CHK of both blobs against the manifest; the app's
sum/xrot as the boot ROM computes them; the AXSS walk of the bootrom; the app record the
boot ROM would accept. Exit 0 when every file passes, 1 otherwise.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trx_pkg as P  # noqa: E402


def find_manifest(img):
    """(offset, fields, why): the manifest is found by its magic, 4-aligned (the updater's code also
    holds the magic as a string; the manifest is the occurrence whose length field and CRC hold)."""
    why = "no %s in the image" % P.MAGIC.decode()
    p = img.find(P.MAGIC)
    while p >= 0:
        if p % 4 == 0 and p + P.MANIFEST_LEN <= len(img):
            try:
                return p, P.parse_manifest(img[p:p + P.MANIFEST_LEN]), ""
            except ValueError as e:
                why = str(e)
        p = img.find(P.MAGIC, p + 1)
    return -1, None, why


def code_of(path):
    """The updater code of a package: the record's image in front of its manifest."""
    d = open(path, "rb").read()
    img = d[(36 if d[:4] == P.PRE_FINGERPRINT else 0) + 12:]
    m_off, v, why = find_manifest(img)
    if v is None:
        raise SystemExit("%s: %s" % (path, why))
    return img[:m_off]


def check_file(path, bit_in, app_in, code_ref=None):
    errs = []
    notes = []
    d = open(path, "rb").read()
    off = 0
    if path.lower().endswith(".cfw") or d[:4] == P.PRE_FINGERPRINT:
        if d[:4] != P.PRE_FINGERPRINT:
            errs.append("pre-header: fingerprint %s, expected DE AD 15 41" % d[:4].hex())
        else:
            fam = d[4:20]
            prod = d[20:36]
            if fam != b"U64MK2" + b"\0" * 10:
                errs.append("pre-header: family %r, expected U64MK2" % fam.rstrip(b"\0"))
            if prod != b"\0" * 16:
                errs.append("pre-header: product %r, expected empty" % prod.rstrip(b"\0"))
            notes.append("pre-header U64MK2, no product")
        off = 36
    elif path.lower().endswith(".ue2") and d[:4] == P.PRE_FINGERPRINT:
        errs.append(".ue2 must not carry the pre-header")
    if len(d) < off + 12:
        return ["file too short"], notes
    load, length, start = struct.unpack_from("<III", d, off)
    if load != P.RECORD_LOAD:
        errs.append("record: load 0x%08X, expected 0x%08X" % (load, P.RECORD_LOAD))
    if not (load <= start < load + length):
        errs.append("record: start 0x%08X outside the image" % start)
    if length & 3:
        errs.append("record: length not a multiple of 4")
    if off + 12 + length != len(d):
        errs.append("record: header says %d bytes of data, file has %d" % (length, len(d) - off - 12))
    if length > P.PKG_MAX:
        errs.append("record: %d bytes exceeds softboot's STAGE_LIMIT 0x%X" % (length, P.PKG_MAX))
    notes.append("record load %08X length %d start %08X" % (load, length, start))
    img = d[off + 12:]

    m_off, v, why = find_manifest(img)
    if v is None:
        errs.append("manifest: %s" % why)
        return errs, notes
    restore = bool(v["flags"] & P.FLAG_RESTORE)
    notes.append("manifest ok (CRC), id %s, flags %d%s" % (v["id"], v["flags"], " (restore bundle)" if restore else ""))
    raw = bool(v["flags"] & P.FLAG_RAWBIT)
    if raw and not restore:
        errs.append("raw bitstream flag without the restore flag")
    if v["flags"] & ~(P.FLAG_RESTORE | P.FLAG_RAWBIT):
        errs.append("manifest flags 0x%X: M1 packages write bitstream and app only" % v["flags"])

    # blob places: explicit offsets from the manifest, [manifest][bitstream][app][config], nothing after
    for what, off, ln in (("bitstream", v["bit_off"], v["bit_len"]), ("app", v["app_off"], v["app_len"]),
                          ("config", v["cfg_off"], v["cfg_len"])):
        if ln == 0 and what == "config":
            continue
        if off & 3 or off < P.MANIFEST_LEN or m_off + off + ln > len(img):
            errs.append("%s blob: offset %d length %d does not fit the image" % (what, off, ln))
            return errs, notes
    want_bit, want_app, want_cfg, want_end = P.layout(v["bit_len"], v["app_len"], v["cfg_len"])
    if (v["bit_off"], v["app_off"], v["cfg_off"], m_off + want_end) != (want_bit, want_app, want_cfg, len(img)):
        errs.append("blob layout is not manifest, bitstream, app, config with the image ending behind them")
    bit = img[m_off + v["bit_off"]:m_off + v["bit_off"] + v["bit_len"]]
    app = img[m_off + v["app_off"]:m_off + v["app_off"] + v["app_len"]]
    cfg = img[m_off + v["cfg_off"]:m_off + v["cfg_off"] + v["cfg_len"]] if v["cfg_len"] else b""
    if P.bit_length_from_header(bit) != len(bit):
        errs.append("bitstream blob: .bit header length differs from the manifest")
    if restore and v["cfg_len"] not in (0, P.CFG_LEN):
        errs.append("config blob: %d bytes, expected %d" % (v["cfg_len"], P.CFG_LEN))
    if not restore and v["cfg_len"]:
        errs.append("config pages in a package that is not a restore bundle")
    notes.append("blobs: manifest @%d, bitstream %d bytes, app %d bytes, config %d bytes; fixed part %d bytes" %
                 (m_off, len(bit), len(app), len(cfg), m_off))
    if code_ref is not None and img[:m_off] != code_ref:
        errs.append("the updater code in front of the manifest differs from the reference package's")
    elif code_ref is not None:
        notes.append("updater code byte-equal to the reference package's")

    if bit_in is not None and bit != bit_in:
        errs.append("bitstream blob differs from --bitstream")
    elif bit_in is not None:
        notes.append("bitstream byte-equal to the input")
    if app_in is not None and app != app_in:
        errs.append("app blob differs from --app")
    elif app_in is not None:
        notes.append("app byte-equal to the input")

    def cmp(what, got, want):
        if got != want:
            errs.append("%s: computed %08X, manifest %08X" % (what, got, want))
        return got == want

    ok = cmp("bitstream CRC-32", P.crc32(bit), v["bit_crc"])
    ok &= cmp("bitstream CHK", P.chk(bit), v["bit_chk"])
    ok &= cmp("app CRC-32", P.crc32(app), v["app_crc"])
    ok &= cmp("app CHK", P.chk(app), v["app_chk"])
    s, x = P.sum_xrot(app)
    ok &= cmp("app sum", s, v["app_sum"])
    ok &= cmp("app xrot", x, v["app_xrot"])
    if cfg:
        ok &= cmp("config CRC-32", P.crc32(cfg), v["cfg_crc"])
        ok &= cmp("config CHK", P.chk(cfg), v["cfg_chk"])
    ax = P.axss(bit)
    if ax is None and not restore:
        errs.append("bitstream: no AXSS found by the bootrom's walk")
    else:
        ok &= cmp("bitstream AXSS", ax or 0, v["bit_axss"])
    try:
        P.check_app_record(app)
    except ValueError as e:
        errs.append("app record: %s" % e)
    if raw and bit[:P.RAW_HDR] != P.raw_header(len(bit) - P.RAW_HDR):
        errs.append("raw bitstream blob does not start with its synthesized header")
    if len(bit) - (P.RAW_HDR if raw else 0) > P.BIT_MAX:
        errs.append("bitstream %d bytes exceeds the slot 0x%X" % (len(bit), P.BIT_MAX))
    if ok:
        notes.append("CRC-32, CHK, sum %08X, xrot %08X, AXSS %08X all match the manifest" % (s, x, ax or 0))
    return errs, notes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bitstream")
    ap.add_argument("--app")
    ap.add_argument("--code-of", help="package whose updater code (the bytes in front of the manifest) this one must repeat")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    bit_in = open(a.bitstream, "rb").read() if a.bitstream else None
    app_in = open(a.app, "rb").read() if a.app else None
    code_ref = None
    if a.code_of:
        code_ref = code_of(a.code_of)
    bad = 0
    for f in a.files:
        errs, notes = check_file(f, bit_in, app_in, code_ref)
        print("%s: %s" % (f, "FAIL" if errs else "PASS"))
        for n in notes:
            print("  ok   %s" % n)
        for e in errs:
            print("  FAIL %s" % e)
        bad += bool(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
