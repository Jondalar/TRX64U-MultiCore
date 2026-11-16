#!/usr/bin/env python3
"""The content check of a MultiCore installer bundle before hand-over (T172 S5).

  tools/check_bundle.py DIR [DIR...]      DIR holds the package files: manifest.json, c64_basic.bit, null_full.bit, hostapp.app,
                                          trx64host_update_<sha8>.txu and trx64host_update_<sha8>_<date>.cfw/.ue2 (several DIRs are one set,
                                          ci/update.sh passes the downloaded inputs and the freshly built update files)
  tools/check_bundle.py selftest          a good fixture passes, every fault below turns it red

What it asserts, from the files and not from their names:
  - manifest.json: the SHA-256 and size of each of the four inputs it lists; commit, branch and version are present
  - the two host bitstreams (c64_basic.bit, null_full.bit): the same design (header: design name, part XC7A50T) and the same host key (AXSS),
    and that key is the one the manifest names (layers.host.host_key)
  - the .txu: parses and verifies (txu_pack), host part == null_full.bit, app part == hostapp.app, host key == AXSS of null_full.bit,
    version field == the bundle version of manifest.json (T221 D17), commit == the manifest's trx64_commit
  - the .cfw and the .ue2 (both must exist): tools/trx_pkg_check.py with the bitstream and app (byte-equal, CRC, CHK, AXSS, app record), the
    manifest id of the package names the same <sha8> as the manifest.json
  - the host app: the versions it carries are the manifest.json ones: TRXVSTAMP = parts.host_menu, TRXHSTAMP = parts.host_gateware, TRXBSTAMP = version, every field
Exit 0 when everything holds, 1 with one line per fault.
"""
import glob
import hashlib
import json
import os
import re
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import trx_pkg as P            # noqa: E402
import trx_pkg_check as K      # noqa: E402
import txu_pack as X           # noqa: E402
import stamp_version as S      # noqa: E402

INPUTS = ("c64_basic.bit", "null_full.bit", "hostapp.app")


def bit_header(bit):
    """(design, part) strings of a .bit's header fields a and b, '' when it has none."""
    if P.bit_length_from_header(bit) != len(bit):
        return "", ""
    p, out = 13, []
    for key in b"ab":
        n = (bit[p + 1] << 8) | bit[p + 2]
        out.append(bit[p + 3:p + 3 + n].split(b"\0")[0].decode("ascii", "replace"))
        p += 3 + n
    return out[0], out[1]


def find(dirs, pattern):
    hits = []
    for d in dirs:
        hits += glob.glob(os.path.join(d, pattern))
    return sorted(set(hits))


def check(dirs):
    errs = []

    def err(m):
        errs.append(m)

    mfs = find(dirs, "manifest.json")
    if len(mfs) != 1:
        return ["manifest.json: %d found, expected 1" % len(mfs)]
    man = json.load(open(mfs[0]))
    for k in ("trx64_commit", "branch", "version"):
        if not man.get(k):
            err("manifest.json has no %s" % k)
    if errs:
        return errs
    sha8 = man["trx64_commit"][:8]
    data = {}
    for n in INPUTS:
        f = find(dirs, n)
        if len(f) != 1:
            err("%s: %d found, expected 1" % (n, len(f)))
            continue
        data[n] = open(f[0], "rb").read()
        ent = man.get("files", {}).get(n)
        if ent is None:
            err("manifest.json lists no %s" % n)
        elif ent["sha256"] != hashlib.sha256(data[n]).hexdigest() or ent["size"] != len(data[n]):
            err("%s differs from manifest.json (sha256/size)" % n)
    if errs:
        return errs
    bit, base, app = data["null_full.bit"], data["c64_basic.bit"], data["hostapp.app"]

    # the host: one design, one part, one key
    dn, dp = bit_header(bit)
    bn, bp = bit_header(base)
    if not dn or (dn, dp) != (bn, bp):
        err("null_full.bit design '%s' part '%s', c64_basic.bit '%s' '%s': not one host" % (dn, dp, bn, bp))
    if not dp.startswith("7a50t"):
        err("null_full.bit part '%s' is not the XC7A50T" % dp)
    ax, bax = P.axss(bit), P.axss(base)
    if ax is None or ax != bax:
        err("host key (AXSS) of null_full.bit %s, of c64_basic.bit %s" % (ax, bax))
    hk = (man.get("layers", {}).get("host", {}) or {}).get("host_key", "")
    if ax is not None and hk and not hk.lower().startswith("%08x" % ax):
        err("host key %08x is not the manifest's layers.host.host_key %s" % (ax, hk))

    # the app's version
    vers = []
    try:
        vers = S.read(app)
    except Exception as e:     # noqa: BLE001
        err("hostapp.app: %s" % e)
    if not vers:
        err("hostapp.app carries no TRXVSTAMP version field")
    parts = man.get("parts") or {}
    for k in ("host_gateware", "host_menu", "core_app"):
        if not parts.get(k):
            err("manifest.json parts has no %s (T221 D6)" % k)
    for v in vers:
        if v != parts.get("host_menu"):
            err("hostapp.app shows version '%s', the host menu of the package is '%s'" % (v, parts.get("host_menu")))
    # T221: the host gateware version of the "host" chip and the bundle version ride in the same image
    for mark, want, what in ((b"TRXHSTAMP:", parts.get("host_gateware"), "host gateware"), (b"TRXBSTAMP:", man["version"], "bundle")):
        got = S.read(app, mark)
        if not got or any(v != want for v in got):
            err("hostapp.app shows %s version %s, the package says %s" % (what, got, want))

    # the .txu
    txus = find(dirs, "trx64host_update_*.txu")
    if len(txus) != 1:
        err(".txu: %d found, expected 1" % len(txus))
    for t in txus:
        blob = open(t, "rb").read()
        try:
            m = X.parse(blob)
            X.verify(blob, m)
        except X.TxuError as e:
            err("%s: %s" % (os.path.basename(t), e))
            continue
        if sha8 not in os.path.basename(t):
            err("%s: the file name does not carry the commit %s" % (os.path.basename(t), sha8))
        parts = {q["kind"]: blob[q["off"]:q["off"] + q["size"]] for q in m["parts"]}
        if parts.get(X.KIND_HOST) != bit:
            err(".txu: host part is not null_full.bit")
        if parts.get(X.KIND_APP) != app:
            err(".txu: app part is not hostapp.app")
        if ax is not None and m["host_key"] != ax:
            err(".txu: host key %08x, null_full.bit %08x" % (m["host_key"], ax))
        want = man["version"]                      # T221 D17: the .txu carries the bundle version
        if m["version"] != want:
            err(".txu: version '%s', expected '%s'" % (m["version"], want))
        if not man["trx64_commit"].startswith(m["commit"]):
            err(".txu: commit '%s' is not the package's %s" % (m["commit"], man["trx64_commit"]))

    # the .cfw and the .ue2
    for ext in ("cfw", "ue2"):
        fs = find(dirs, "trx64host_update_*_*.%s" % ext)
        if len(fs) != 1:
            err(".%s: %d found, expected 1" % (ext, len(fs)))
        for f in fs:
            e, _notes = K.check_file(f, bit, app)
            for x in e:
                err("%s: %s" % (os.path.basename(f), x))
            if not e:
                d = open(f, "rb").read()
                img = d[(36 if d[:4] == P.PRE_FINGERPRINT else 0) + 12:]
                _off, v, _why = K.find_manifest(img)
                if not v["id"].endswith("-" + sha8):
                    err("%s: manifest id '%s' does not end in -%s" % (os.path.basename(f), v["id"], sha8))
    return errs


# ---- selftest: a bundle on fixtures, then one fault at a time ----------------------------------

def _fixture(root, branch="t172", sha="0123456789abcdef0123456789abcdef01234567", ver=None):
    import test_cor_pack as TC
    sha8 = sha[:8]
    ver = ver or "0.2.0-dev+%s" % sha8
    bit = TC.full_bit(frames=3000)
    base = TC.full_bit(frames=3200)          # another image of the same host: same design, part and key
    app = struct.pack("<III", 0x30000, 70000, 0x30000) + (b"TRXVSTAMP:" + b"\0" * 54 + b"TRXHSTAMP:" + b"\0" * 54 + b"TRXBSTAMP:" + b"\0" * 54).ljust(70000, b"\x5a")
    app, _ = S.stamp(app, ver)
    app, _ = S.stamp(app, ver, b"TRXHSTAMP:")
    app, _ = S.stamp(app, ver, b"TRXBSTAMP:")
    os.makedirs(root, exist_ok=True)
    files = {"null_full.bit": bit, "c64_basic.bit": base, "hostapp.app": app}
    for n, b in files.items():
        open(os.path.join(root, n), "wb").write(b)
    man = {"trx64_commit": sha, "branch": branch, "version": ver, "parts": {"host_gateware": ver, "host_menu": ver, "core_app": ver, "cores": {}},
           "layers": {"host": {"host_key": "%08x" % P.axss(bit) + "00000000"}},
           "files": {n: {"sha256": hashlib.sha256(b).hexdigest(), "size": len(b)} for n, b in files.items()}}
    json.dump(man, open(os.path.join(root, "manifest.json"), "w"))
    open(os.path.join(root, "trx64host_update_%s.txu" % sha8), "wb").write(X.pack(bit, app, ver, sha[:15]))
    code = bytes(range(256)) * 4
    blobs = P.build_blobs(bit, app, "%s-%s" % (branch, sha8), sha[:15], sha[:15])
    img = code + blobs
    rec = struct.pack("<III", P.RECORD_LOAD, len(img), P.RECORD_LOAD) + img
    open(os.path.join(root, "trx64host_update_%s_20261107.cfw" % sha8), "wb").write(
        P.PRE_FINGERPRINT + b"U64MK2".ljust(16, b"\0") + b"\0" * 16 + rec)
    open(os.path.join(root, "trx64host_update_%s_20261107.ue2" % sha8), "wb").write(rec)
    return sha8, files


def selftest():
    import shutil
    import test_cor_pack as TC
    fails = []
    W = tempfile.mkdtemp()
    try:
        good = os.path.join(W, "good")
        sha8, files = _fixture(good)
        e = check([good])
        if e:
            fails += ["the good fixture is refused: %s" % e]
        else:
            print("good bundle: PASS (txu, cfw, ue2, versions, host key, design)")

        def mutant(name, edit, want):
            d = os.path.join(W, name)
            shutil.copytree(good, d)
            edit(d)
            e = check([d])
            if not any(want in x for x in e):
                fails.append("%s: expected a fault containing '%s', got %s" % (name, want, e))
            else:
                print("control %-22s FAIL as it must: %s" % (name, [x for x in e if want in x][0]))

        def rw(d, n, f):
            p = os.path.join(d, n)
            b = open(p, "rb").read()
            open(p, "wb").write(f(b))

        def update_manifest(d, f):
            p = os.path.join(d, "manifest.json")
            m = json.load(open(p))
            f(m)
            json.dump(m, open(p, "w"))

        def cfw_other_bitstream(d):   # a package whose .cfw carries a different bitstream (not null_full.bit), well formed otherwise
            n = glob.glob(os.path.join(d, "*.cfw"))[0]
            u = glob.glob(os.path.join(d, "*.ue2"))[0]
            app = open(os.path.join(d, "hostapp.app"), "rb").read()
            bit = TC.full_bit(frames=3300)
            img = bytes(range(256)) * 4 + P.build_blobs(bit, app, "t172-" + sha8, "x", "x")
            rec = struct.pack("<III", P.RECORD_LOAD, len(img), P.RECORD_LOAD) + img
            open(n, "wb").write(P.PRE_FINGERPRINT + b"U64MK2".ljust(16, b"\0") + b"\0" * 16 + rec)
            open(u, "wb").write(rec)

        def txu_other_app(d):
            n = glob.glob(os.path.join(d, "*.txu"))[0]
            bit = open(os.path.join(d, "null_full.bit"), "rb").read()
            app = open(os.path.join(d, "hostapp.app"), "rb").read()
            app2, _ = S.stamp(app, "other@deadbeef")
            open(n, "wb").write(X.pack(bit, app2, "t172-" + sha8, "0123456789abcde"))

        def txu_other_bit(d):
            n = glob.glob(os.path.join(d, "*.txu"))[0]
            app = open(os.path.join(d, "hostapp.app"), "rb").read()
            open(n, "wb").write(X.pack(TC.full_bit(frames=3300), app, "t172-" + sha8, "0123456789abcde"))

        mutant("cfw_wrong_bitstream", cfw_other_bitstream, "differs from --bitstream")
        mutant("txu_wrong_app", txu_other_app, ".txu: app part is not hostapp.app")
        mutant("txu_wrong_bitstream", txu_other_bit, ".txu: host part is not null_full.bit")
        def old_version(d):   # the cache hit of 2026-11-01: an app image stamped by an earlier commit, listed in the manifest as it is
            rw(d, "hostapp.app", lambda b: S.stamp(b, "old@1234abcd")[0])
            b = open(os.path.join(d, "hostapp.app"), "rb").read()
            update_manifest(d, lambda m: m["files"].update({"hostapp.app": {"sha256": hashlib.sha256(b).hexdigest(), "size": len(b)}}))

        mutant("app_old_version", old_version, "hostapp.app shows version")
        mutant("txu_missing", lambda d: os.remove(glob.glob(os.path.join(d, "*.txu"))[0]), ".txu: 0 found")
        mutant("ue2_missing", lambda d: os.remove(glob.glob(os.path.join(d, "*.ue2"))[0]), ".ue2: 0 found")
        mutant("input_changed", lambda d: rw(d, "c64_basic.bit", lambda b: b + b"\0"), "differs from manifest.json")
        mutant("other_host_key", lambda d: (rw(d, "c64_basic.bit", lambda b: TC.full_bit(frames=3200, axss=0x11111111)),
                                            update_manifest(d, lambda m: m["files"].update({"c64_basic.bit": {
                                                "sha256": hashlib.sha256(TC.full_bit(frames=3200, axss=0x11111111)).hexdigest(),
                                                "size": len(TC.full_bit(frames=3200, axss=0x11111111))}}))), "host key (AXSS)")
        mutant("other_design", lambda d: (rw(d, "c64_basic.bit", lambda b: TC.full_bit(frames=3200, name=b"c64_monolith;UserID=0XFFFFFFFF")),
                                          update_manifest(d, lambda m: m["files"].update({"c64_basic.bit": {
                                              "sha256": hashlib.sha256(TC.full_bit(frames=3200, name=b"c64_monolith;UserID=0XFFFFFFFF")).hexdigest(),
                                              "size": len(TC.full_bit(frames=3200, name=b"c64_monolith;UserID=0XFFFFFFFF"))}}))), "not one host")
    finally:
        shutil.rmtree(W, ignore_errors=True)
    for f in fails:
        print("FAIL " + f)
    if fails:
        return 1
    print("check_bundle selftest ok")
    return 0


def main(a):
    if a[:1] == ["selftest"]:
        return selftest()
    if not a:
        sys.exit(__doc__)
    errs = check(a)
    for e in errs:
        print("FAIL " + e)
    print("bundle %s" % ("FAIL" if errs else "PASS"))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
