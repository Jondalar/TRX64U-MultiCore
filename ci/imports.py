#!/usr/bin/env python3
"""T239.1 D43: trx64u.cor and trx64g.cor (release/imports/*.pin) as entries imp-<id>-<key> of the bitstream cache, packed and published by the builder.

GitHub's runners reach neither Gitea's package registry (the pinned TRX64-Ultimate build) nor the private files, so the builder packs the two .cor (its
poll, pull/imports.py of trx64mc-builder, with the code and the pins of its trusted ref) and publishes each as the release bit-imp-<id>-<key> of the
builds repository. ci/fetch_bits.py restores them like the bitstreams, ci/collect.sh hands them to the package (bit-imports), and ci/package.sh puts this
run's core app into a pin without APP_FILE (trx64u; cor_pack.py replace-app); a pin with APP_FILE keeps that application (trx64g: Gideon's).

  ci/imports.py names                              the entry names of the pins, one per line (a pin with PIPELINE=false has none)
  ci/imports.py pack ENTRY --store DIR --out DIR   the builder: <out>/<id>.cor from the ROM store (one file per SHA-256, as ci/roms.py reads it): the
                                                   pin's BIT_SHA256 and APP_SHA256, and every private file under FILES by the SHA-256 ci/private.txt names
                                                   (copied into this tree first). A pin without APP_FILE is packed with a stand-in application.
  ci/imports.py finish COR -o OUT [--app IMAGE --app-commit C]
                                                   ci/package.sh: the .cor of this run - the version in the form of this build (ci/version.sh with the pin's
                                                   SEMVER: x.y.z on a tag, else x.y.z-dev+<sha8>) and, for a pin without APP_FILE, this run's application image
                                                   (tools/cor_pack.py replace_app) and its commit; a pin with APP_FILE keeps its application

The key hashes what makes the .cor apart from the application of a pin without APP_FILE and the version form: the pin, its DESC, OPS and ICON, every
public file under FILES, the ci/private.txt line of every private one, and the code that packs. The builder and the pipeline compute it from the same
public tree. The builder packs with GITHUB_REF_TYPE=tag (the pin's x.y.z, no commit in it), so the entry does not depend on the commit it was packed at.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
IMPORTS = "release/imports"
CODE = ("ci/imports.py", "ci/pkg.env", "ci/version.sh", "tools/pack_import_cor.sh", "tools/pack_large_cor.sh", "tools/cor_pack.py", "tools/descrec.py",
        "tools/core_toml.py")
ENTRY = re.compile(r"^imp-([A-Za-z0-9_]+)-([0-9a-f]{16})$")
SHA = re.compile(r"^[0-9a-f]{64}$")
STANDIN = b"TRX64 MultiCore: stand-in application; ci/package.sh puts the core app of the run in (tools/cor_pack.py replace-app)\n"


class ImportError_(Exception):
    pass


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def read_pin(path):
    pin = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        if len(v) >= 2 and v[0] == v[-1] == '"':
            v = v[1:-1]
        pin[k.strip()] = v
    return pin


def pins(root=ROOT):
    """[(id, path, pin)] of the pins that are packed, sorted by file name."""
    d = os.path.join(root, IMPORTS)
    out = []
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if not n.endswith(".pin"):
            continue
        p = os.path.join(d, n)
        pin = read_pin(p)
        if pin.get("PIPELINE", "").lower() == "false":
            continue
        cid = pin.get("ID", "")
        if cid != n[:-4] or not re.match(r"^[A-Za-z0-9_]+$", cid):
            raise ImportError_("%s: ID=%s does not name the file" % (n, cid))
        out.append((cid, p, pin))
    return out


def private_files(root=ROOT):
    """{path: (size, sha256)} of the file lines of ci/private.txt."""
    out = {}
    p = os.path.join(root, "ci", "private.txt")
    for line in open(p) if os.path.isfile(p) else []:
        f = line.split()
        if len(f) >= 4 and f[0] == "file" and SHA.match(f[2]):
            out[f[3]] = (int(f[1]), f[2])
    return out


def rel(root, path):
    p = os.path.normpath(os.path.join(root, path))
    if not p.startswith(os.path.normpath(root) + os.sep):
        raise ImportError_("%s leaves the tree" % path)
    return p


def key(pin, root=ROOT):
    """The 16 hex digits of entry imp-<id>-<key>."""
    priv = private_files(root)
    items = [("pin", sha256_bytes(open(os.path.join(root, IMPORTS, pin["ID"] + ".pin"), "rb").read()))]
    for k in ("DESC", "OPS", "ICON"):
        for f in (pin.get(k) or "").split():                        # DESC may name several files (menu.toml desc.toml): one record 6
            items.append((f, sha256_file(rel(root, f))))
    files = pin.get("FILES")
    if files:
        top = rel(root, files)
        if not os.path.isdir(top):
            raise ImportError_("FILES=%s is no directory" % files)
        for d, dirs, names in os.walk(top):
            dirs.sort()
            for n in sorted(names):
                p = os.path.relpath(os.path.join(d, n), root)
                if p not in priv:                                   # a private file is hashed by its ci/private.txt line, present or not
                    items.append((p, sha256_file(os.path.join(root, p))))
        for p in sorted(priv):
            if p.startswith(files.rstrip("/") + "/"):
                items.append((p, "private %d %s" % priv[p]))
    for c in CODE:
        items.append((c, sha256_file(os.path.join(root, c))))
    text = "imports-key 1\n" + "".join("%s\0%s\n" % i for i in items)
    return sha256_bytes(text.encode())[:16]


def names(root=ROOT):
    return ["imp-%s-%s" % (cid, key(pin, root)) for cid, _, pin in pins(root)]


def find(entry, root=ROOT):
    m = ENTRY.match(entry)
    if not m:
        raise ImportError_("not an import entry: %r" % entry[:60])
    for cid, path, pin in pins(root):
        if cid == m.group(1):
            k = key(pin, root)
            if k != m.group(2):
                raise ImportError_("this tree makes imp-%s-%s, not %s" % (cid, k, entry))
            return cid, path, pin
    raise ImportError_("no pin release/imports/%s.pin that is packed" % m.group(1))


def from_store(store, sha, what):
    if not SHA.match(sha or ""):
        raise ImportError_("%s: no SHA-256 to find it by" % what)
    p = os.path.join(store, sha)
    if not os.path.isfile(p):
        raise ImportError_("%s (%s...) is not in the store %s" % (what, sha[:12], store))
    if sha256_file(p) != sha:
        raise ImportError_("the store's %s... is another file" % sha[:12])
    return p


def pack(entry, store, out, root=ROOT, run=subprocess.run):
    cid, path, pin = find(entry, root)
    os.makedirs(out, exist_ok=True)
    priv = private_files(root)
    files = pin.get("FILES")
    if files:
        for p, (size, sha) in sorted(priv.items()):
            if p.startswith(files.rstrip("/") + "/"):
                dst = rel(root, p)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(from_store(store, sha, p), dst)
    args = ["bash", os.path.join(root, "tools", "pack_import_cor.sh"), "--pin", path, "--out", out,
            "--bit", from_store(store, pin.get("BIT_SHA256"), "BIT_FILE " + pin.get("BIT_FILE", "?"))]
    standin = None
    if pin.get("APP_FILE"):
        args += ["--app-file", from_store(store, pin.get("APP_SHA256"), "APP_FILE " + pin["APP_FILE"])]
    else:
        standin = os.path.join(out, ".standin.bin")
        open(standin, "wb").write(STANDIN)
        args += ["--app-bin", standin]
    env = dict(os.environ, GITHUB_REF_TYPE="tag")
    for k in ("GITHUB_SERVER_URL", "GITHUB_REPOSITORY", "PACKAGE_TOKEN"):
        env.pop(k, None)                                            # everything comes from the store: no download
    r = run(args, cwd=root, env=env)
    if standin:
        os.unlink(standin)
    cor = os.path.join(out, cid + ".cor")
    if r.returncode != 0 or not os.path.isfile(cor):
        raise ImportError_("tools/pack_import_cor.sh packed no %s.cor (exit %d)" % (cid, r.returncode))
    return cor


def version_of(pin, root=ROOT, run=subprocess.run):
    """The version of the .cor in the form of this build, as tools/pack_import_cor.sh makes it: SEMVER x.y.z through ci/version.sh, an x.y.z-dev+<sha8>
    SEMVER as it is."""
    sv = pin.get("SEMVER", "")
    if not sv:
        raise ImportError_("release/imports/%s.pin has no SEMVER" % pin.get("ID"))
    if "-dev+" in sv:
        return sv
    r = run([os.path.join(root, "ci", "version.sh"), "with", sv], capture_output=True, text=True)
    if r.returncode != 0 or not r.stdout.strip():
        raise ImportError_("ci/version.sh with %s: %s" % (sv, (r.stderr or "").strip()[-200:]))
    return r.stdout.strip()


def finish(cor, out, app=None, app_commit=None, root=ROOT):
    """ci/package.sh: the builder's .cor in the form of this run (see the module text). Returns the version written."""
    sys.path.insert(0, os.path.join(root, "tools"))
    import cor_pack as C
    cid = os.path.basename(cor)[:-4] if cor.endswith(".cor") else ""
    pin = next((p for i, _, p in pins(root) if i == cid), None)
    if pin is None:
        raise ImportError_("%s: no packed pin release/imports/%s.pin" % (os.path.basename(cor), cid))
    blob = open(cor, "rb").read()
    fields = {}
    if pin.get("APP_FILE"):
        if app:
            raise ImportError_("%s brings its own application (APP_FILE); no --app" % cid)
    else:
        if not app:
            raise ImportError_("%s has no APP_FILE: give the run's application image (--app)" % cid)
        img = open(app, "rb").read()
        if pin.get("HEAP"):
            try:
                img = C.app_heap(img, int(pin["HEAP"], 0), cid)     # T239.3 D15: the pin's heap into the run's image, and the fit check
            except (C.CorError, ValueError) as e:
                raise ImportError_("%s: %s" % (os.path.basename(cor), e))
        blob = C.replace_app(blob, img)
        if app_commit:
            fields["app_commit"] = app_commit
    v = version_of(pin, root)
    try:
        blob = C.retag(blob, v, **fields)
    except (C.CorError, ValueError) as e:
        raise ImportError_("%s: %s" % (os.path.basename(cor), e))
    with open(out, "wb") as f:
        f.write(blob)
    return v


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("names")
    p = sub.add_parser("pack")
    p.add_argument("entry")
    p.add_argument("--store", required=True)
    p.add_argument("--out", required=True)
    f = sub.add_parser("finish")
    f.add_argument("cor")
    f.add_argument("-o", "--out", required=True)
    f.add_argument("--app")
    f.add_argument("--app-commit")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "names":
            for n in names():
                print(n)
        elif a.cmd == "pack":
            print(pack(a.entry, a.store, a.out))
        else:
            print("%s: %s" % (a.out, finish(a.cor, a.out, a.app, a.app_commit)))
    except ImportError_ as e:
        print("ci/imports.py: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
