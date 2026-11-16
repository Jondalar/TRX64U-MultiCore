#!/usr/bin/env python3
"""A release is the tested build with its version strings rewritten (T221 D4, Alex: "a release is only copying the .cor out of the non-release and
patching the metadata"): x.y.z-dev+<sha8> becomes x.y.z, and nothing else changes.

  release_promote.py <package dir>            rewrites the files of the package in place
  release_promote.py selftest

What is rewritten: the stamp fields of hostapp.app / hostapp.elf (TRXVSTAMP host menu, TRXHSTAMP host gateware, TRXBSTAMP bundle) and of coreapp.bin /
coreapp.elf; every *.cor (manifest `version`, and the application image inside it, its size and SHA-256 following); the .txu (repacked from null_full.bit
and the promoted hostapp.app with the bundle version); manifest.json (version, parts, file hashes); SHA256SUMS when there is one. The .cfw / .ue2 files
are made by stage update from the package and carry their own branch-sha identity: they are not touched, they are made again from the promoted package.
"""
import hashlib
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cor_pack as C   # noqa: E402
import stamp_version as S   # noqa: E402
import txu_pack as X   # noqa: E402

DEV = re.compile(r"-dev\+[0-9a-f]{8}$")


def release_of(v, strict=True):
    if not DEV.search(v):
        if strict:
            raise ValueError("%r is not a dev build version x.y.z-dev+<sha8>: not promoted again" % v)
        return v                       # a core that shows a commit (an import pin without SEMVER, T221 D15) stays as it is
    return DEV.sub("", v)


def promote(d):
    mf = os.path.join(d, "manifest.json")
    man = json.load(open(mf))
    parts = man.get("parts") or {}
    bundle = release_of(man["version"])
    host, menu, capp = (release_of(parts[k]) for k in ("host_gateware", "host_menu", "core_app"))

    def rw(name, fn):
        p = os.path.join(d, name)
        if os.path.exists(p):
            b = open(p, "rb").read()
            open(p, "wb").write(fn(b))

    def stamp_host(b):
        for mark, v in ((b"TRXVSTAMP:", menu), (b"TRXHSTAMP:", host), (b"TRXBSTAMP:", bundle)):
            b, _ = S.stamp(b, v, mark)
        return b

    def stamp_core(b):
        return S.stamp(b, capp)[0]

    for n in ("hostapp.app", "hostapp.elf"):
        rw(n, stamp_host)
    for n in ("coreapp.bin", "coreapp.elf", "coreapp.app"):
        rw(n, stamp_core)
    cores = {}
    for f in sorted(os.listdir(d)):
        if f.endswith(".cor"):
            def one(b, f=f):
                m, _ = C.parse(b)
                v = release_of(m["version"], False)
                cores[f[:-4]] = v
                try:
                    return C.retag(b, v, stamp_core)
                except ValueError:                       # an imported core's app is not ours: no TRXVSTAMP field
                    return C.retag(b, v)
            rw(f, one)
    txus = [f for f in os.listdir(d) if f.endswith(".txu")]
    for f in txus:
        old = X.parse(open(os.path.join(d, f), "rb").read())
        bit = open(os.path.join(d, "null_full.bit"), "rb").read()
        app = open(os.path.join(d, "hostapp.app"), "rb").read()
        blob = X.pack(bit, app, bundle, old["commit"])
        X.verify(blob, X.parse(blob))
        open(os.path.join(d, f), "wb").write(blob)
    man["version"] = bundle
    man["parts"] = {"host_gateware": host, "host_menu": menu, "core_app": capp, "cores": {**cores}}
    for n, e in man["files"].items():
        p = os.path.join(d, n)
        if os.path.exists(p):
            b = open(p, "rb").read()
            e["sha256"], e["size"] = hashlib.sha256(b).hexdigest(), len(b)
    json.dump(man, open(mf, "w"), indent=2)
    sums = os.path.join(d, "SHA256SUMS")
    if os.path.exists(sums):
        out = []
        for line in open(sums):
            h, n = line.split(None, 1)
            n = n.strip()
            p = os.path.join(d, n)
            out.append("%s  %s\n" % (hashlib.sha256(open(p, "rb").read()).hexdigest() if os.path.exists(p) else h, n))
        open(sums, "w").write("".join(out))
    return bundle


def selftest():
    import shutil
    import tempfile
    import test_cor_pack as TC
    W = tempfile.mkdtemp()
    try:
        sha8 = "0123abcd"
        dv = lambda x: "%s-dev+%s" % (x, sha8)   # noqa: E731
        bit = TC.full_bit(frames=3000)
        blank = lambda m: (m + b":" + b"\0" * 54)   # noqa: E731
        app = blank(b"TRXVSTAMP") + blank(b"TRXHSTAMP") + blank(b"TRXBSTAMP") + b"\x5a" * 4000
        app = struct.pack("<III", 0x30000, len(app), 0x30000) + app
        for m, v in ((b"TRXVSTAMP:", dv("0.3.0")), (b"TRXHSTAMP:", dv("0.2.0")), (b"TRXBSTAMP:", dv("1.4.0"))):
            app, _ = S.stamp(app, v, m)
        capp = S.stamp(blank(b"TRXVSTAMP") + b"\x33" * 700, dv("0.2.0"))[0]
        open(os.path.join(W, "null_full.bit"), "wb").write(bit)
        open(os.path.join(W, "hostapp.app"), "wb").write(app)
        open(os.path.join(W, "coreapp.bin"), "wb").write(capp)
        open(os.path.join(W, "trx64host_update_%s.txu" % sha8), "wb").write(X.pack(bit, app, dv("1.4.0"), "0123abcd0123abc"))
        part = TC.full_bit(frames=40)
        cor = C.pack("Core", dv("0.9.0"), part, capp, 0x30000, 0x30000, "%08x" % 0x1234abcd)
        open(os.path.join(W, "c.cor"), "wb").write(cor)
        man = {"version": dv("1.4.0"), "parts": {"host_gateware": dv("0.2.0"), "host_menu": dv("0.3.0"), "core_app": dv("0.2.0"), "cores": {"c": dv("0.9.0")}},
               "files": {n: {"sha256": "", "size": 0} for n in ("hostapp.app", "coreapp.bin", "c.cor")}}
        json.dump(man, open(os.path.join(W, "manifest.json"), "w"))
        promote(W)
        a2 = open(os.path.join(W, "hostapp.app"), "rb").read()
        assert (S.read(a2), S.read(a2, b"TRXHSTAMP:"), S.read(a2, b"TRXBSTAMP:")) == (["0.3.0"], ["0.2.0"], ["1.4.0"]), "host app fields"
        assert len(a2) == len(app) and a2.replace(b"\0", b"")[:0] == b""
        m2, recs = C.parse(open(os.path.join(W, "c.cor"), "rb").read())
        assert m2["version"] == "0.9.0" and S.read(recs[C.T_APP]) == ["0.2.0"], "cor"
        t = X.parse(open(glob_one(W, ".txu"), "rb").read())
        assert t["version"] == "1.4.0", "txu"
        m3 = json.load(open(os.path.join(W, "manifest.json")))
        assert m3["version"] == "1.4.0" and m3["parts"]["cores"] == {"c": "0.9.0"}
        assert m3["files"]["hostapp.app"]["sha256"] == hashlib.sha256(a2).hexdigest()
        # negative control: a package that is already a release is refused, nothing half-rewritten
        try:
            promote(W)
            raise SystemExit("selftest: a release was promoted again")
        except ValueError:
            pass
        print("release_promote selftest ok")
    finally:
        shutil.rmtree(W)


def glob_one(d, ext):
    return os.path.join(d, [f for f in os.listdir(d) if f.endswith(ext)][0])


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        selftest()
    elif len(sys.argv) == 2 and os.path.isdir(sys.argv[1]):
        print("promoted to bundle %s" % promote(sys.argv[1]))
    else:
        sys.exit(__doc__)
