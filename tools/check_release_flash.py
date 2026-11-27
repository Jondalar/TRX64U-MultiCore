#!/usr/bin/env python3
"""The installer of a release writes the same flash as the tested .txu (2026-11-02, tester release t164-c83a83c2).

  tools/check_release_flash.py --txu <trx64host_update_*.txu> [--reject <file.bit> ...] <installer.cfw|.ue2> [...]

The .txu is what the board test ran (host app U on the board, T152); the .cfw/.ue2 is what testers install from a stock
firmware. Release t164-c83a83c2 shipped a .cfw/.ue2 whose bitstream was c64_basic.bit (host + the build-only c64ref, T115)
instead of null_full.bit: the host menu had no picture on every tester's board while the .txu was fine. So:

  * the installer's bitstream blob is byte-equal to the .txu's host part, and its app blob to the .txu's app part;
  * --recovery: the bundle's hostrecovery.bin (T265 S10) is byte-equal to the .txu's app part;
  * the installer's bitstream is none of the --reject files (c64_basic.bit of the package);
  * probe, every run: the same comparison is made to fail on purpose - once with a --reject file (or one flipped byte when
    none is given) in place of the installer's bitstream, once with one byte of the app flipped. A comparison that does not
    fail there proves nothing, and the run fails.

Exit 0 when every installer passes and every probe failed as it must, 1 otherwise.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trx_pkg as P  # noqa: E402
import txu_pack  # noqa: E402
from trx_pkg_check import find_manifest  # noqa: E402


def installer_blobs(path):
    """(bitstream, app) of a .cfw/.ue2, as trx_pkg_check reads them."""
    d = open(path, "rb").read()
    off = 36 if d[:4] == P.PRE_FINGERPRINT else 0
    _load, length, _start = struct.unpack_from("<III", d, off)
    img = d[off + 12:off + 12 + length]
    m_off, v, why = find_manifest(img)
    if v is None:
        raise ValueError("manifest: %s" % why)
    bit = img[m_off + v["bit_off"]:m_off + v["bit_off"] + v["bit_len"]]
    app = img[m_off + v["app_off"]:m_off + v["app_off"] + v["app_len"]]
    return bit, app


def txu_parts(path):
    """(host bitstream, app) of a .txu."""
    blob = open(path, "rb").read()
    m = txu_pack.parse(blob)
    parts = {p["kind"]: blob[p["off"]:p["off"] + p["size"]] for p in m["parts"]}
    if txu_pack.KIND_HOST not in parts or txu_pack.KIND_APP not in parts:
        raise ValueError("the .txu does not carry both the host and the app")
    return parts[txu_pack.KIND_HOST], parts[txu_pack.KIND_APP]


def compare(bit, app, want_bit, want_app, rejects):
    """The list of reasons this installer content is not the tested flash; empty = equal."""
    why = []
    if bit != want_bit:
        why.append("bitstream (%d bytes) differs from the .txu host part (%d bytes)" % (len(bit), len(want_bit)))
    for name, r in rejects:
        if bit == r:
            why.append("bitstream is %s" % name)
    if app != want_app:
        why.append("app (%d bytes) differs from the .txu app part (%d bytes)" % (len(app), len(want_app)))
    return why


def flip(b):
    return b[:len(b) // 2] + bytes([b[len(b) // 2] ^ 0xFF]) + b[len(b) // 2 + 1:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--txu", required=True)
    ap.add_argument("--reject", action="append", default=[], help="a bitstream the installer must never carry")
    ap.add_argument("--recovery", default="", help="T265 S10: the bundle's hostrecovery.bin, which has to be the .txu's app part byte for byte")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    want_bit, want_app = txu_parts(a.txu)
    rejects = []
    for r in a.reject:
        d = open(r, "rb").read()
        if d == want_bit:
            print("FAIL --reject %s is the .txu host part itself" % r)
            return 1
        rejects.append((os.path.basename(r), d))
    bad = 0
    if a.recovery:
        rec = open(a.recovery, "rb").read()
        ok = rec == want_app
        print("%s: %s" % (a.recovery, "PASS (byte-equal to the .txu app part)" if ok else "FAIL (%d bytes, the .txu app part is %d bytes and differs)" % (len(rec), len(want_app))))
        if ok and rec != flip(rec):
            print("  ctl| probe hostrecovery.bin with one byte flipped: fails as it must")
        bad += not ok
    for f in a.files:
        bit, app = installer_blobs(f)
        why = compare(bit, app, want_bit, want_app, rejects)
        print("%s: %s" % (f, "FAIL" if why else "PASS (bitstream and app byte-equal to the tested .txu)"))
        for w in why:
            print("  FAIL %s" % w)
        bad += bool(why)
        # probe: the comparison must fail on a wrong bitstream and on a wrong app
        wrong_bit = rejects[0][1] if rejects else flip(bit)
        for what, pb, pa in (("wrong bitstream (%s)" % (rejects[0][0] if rejects else "one byte flipped"), wrong_bit, app),
                             ("app with one byte flipped", bit, flip(app))):
            if compare(pb, pa, want_bit, want_app, rejects):
                print("  ctl| probe %s: fails as it must" % what)
            else:
                print("  FAIL probe %s: the comparison passed it" % what)
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
