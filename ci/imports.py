#!/usr/bin/env python3
"""T239.1 D43, T239.3 P1-8: trx64u.cor and trx64g.cor (release/imports/*.pin) as entries imp-<id>-<key> of the bitstream cache, made and published by the builder.

GitHub's runners reach neither Gitea's package registry (TRX64-Ultimate's builds) nor the private files, so the builder makes the two .cor (its poll,
pull/imports.py of trx64mc-builder, with the code and the pins of its trusted ref) and publishes each as the release bit-imp-<id>-<key> of the builds
repository. ci/fetch_bits.py restores them like the bitstreams, ci/collect.sh hands them to the package (bit-imports).

Two kinds of pin:
  COR_SHA256 (trx64u and trx64g, T239.3 D21, D46): the file is TRX64-Ultimate's finished .cor. Nothing is packed: `pack` takes it from
        the store by its SHA-256 (the store is filled by hand, or fetched), `finish` checks it again, and the package holds it byte for byte. Two forms say where a store
        that lacks it fetches it from (T261 D5); COR_FILE, COR_SHA256 and COR_SIZE are the same in both:
        Gitea form    PACKAGE, VERSION: <TRX_FW_PKG_URL>/<PACKAGE>/<VERSION>/<COR_FILE> (T239.3 D45)
        release form  RELEASE=fw-<branch>-<sha8>: the asset COR_FILE of that release of the builds repository (MC_BUILDS_REPO of ci/pkg.env), https://github.com/<repo>/releases/download/<RELEASE>/<COR_FILE>;
                      the base can be replaced by the environment TRX_FW_RELEASE_URL (up to and including the repository's /releases/download), the tests do. RELEASE wins over PACKAGE/VERSION.
        U2A (T261 D7, release form only): applications of the same release that the package carries beside the .cor, unchanged: U2A="trex_crt.u2a:<sha256>:<size> online.u2a:<sha256>:<size>"
                      (blank-separated, each name matching ^[a-z0-9_]+\\.u2a$, a SHA-256 of 64 hex digits, a size). Each is fetched from the same release as COR_FILE (the store is filled the same way: by its
                      SHA-256, size and SHA-256 checked before it is kept), `pack` puts it into the entry as <out>/<name> beside <id>.cor, and ci/collect.sh hands it to the package. The pin is part of the key, so a
                      changed list is a new entry. A pin without U2A has no such files.
  BIT_SHA256 (no pin uses it since T239.3 D46): the monolith bitstream and Gideon's application (APP_FILE) of the TRX64-Ultimate package, packed here
        (tools/pack_import_cor.sh).

  ci/imports.py names                              the entry names of the pins, one per line (a pin with PIPELINE=false has none)
  ci/imports.py u2a                                the U2A file names of the pins, one per line (T261 D7; ci/package.sh checks each one reached the package)
  ci/imports.py pack ENTRY --store DIR --out DIR   the builder: <out>/<id>.cor from the store (one file per SHA-256, as ci/roms.py reads it). A COR_SHA256 pin: that file.
                                                   A BIT_SHA256 pin: BIT_SHA256 and APP_SHA256 and every private file under FILES by the store, packed. Env TRX_FW_PKG_URL: the base of the generic packages of
                                                   TRX64-Ultimate's registry, up to and including `generic` (no token: a read needs none); a COR_SHA256 file that the store lacks
                                                   is fetched from <base>/<PACKAGE>/<VERSION>/<COR_FILE>, checked and put into the store. The URL is never in a file of this tree.
  ci/imports.py finish COR -o OUT                  ci/package.sh: the .cor of this run - a COR_SHA256 pin: the same bytes, checked; a BIT_SHA256 pin: the version in the
                                                   form of this build (ci/version.sh with the pin's SEMVER: x.y.z on a tag, else x.y.z-dev+<sha8>)

The key hashes what makes the .cor: the pin and the code that makes it (a COR_SHA256 pin: ci/imports.py only; a BIT_SHA256 pin also its ICON and the packers). The builder
and the pipeline compute it from the same public tree. The builder packs with GITHUB_REF_TYPE=tag (the pin's x.y.z, no commit in it), so the entry does not depend on the
commit it was packed at.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
IMPORTS = "release/imports"
CODE = ("ci/imports.py", "ci/pkg.env", "ci/version.sh", "tools/pack_import_cor.sh", "tools/pack_large_cor.sh", "tools/cor_pack.py", "tools/descrec.py",
        "tools/core_toml.py")
ENTRY = re.compile(r"^imp-([A-Za-z0-9_]+)-([0-9a-f]{16})$")
SHA = re.compile(r"^[0-9a-f]{64}$")
U2A_NAME = re.compile(r"^[a-z0-9_]+\.u2a$")
U2A_RESERVED = ("unzip.u2a", "miniz.u2a", "testlib.u2a")     # made by this repository's own build: a pin may not list them
NAME_OK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$")
MAX_FETCH = 64 * 1024 * 1024


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
    out, u2a_seen = [], {}
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if not n.endswith(".pin"):
            continue
        p = os.path.join(d, n)
        pin = read_pin(p)
        if pin.get("PIPELINE", "").lower() == "false":
            continue
        for name, _, _ in u2a_list(pin):
            if name in u2a_seen:
                raise ImportError_("%s: U2A %s is also listed by release/imports/%s.pin" % (n, name, u2a_seen[name]))
            u2a_seen[name] = n
        cid = pin.get("ID", "")
        if cid != n[:-4] or not re.match(r"^[A-Za-z0-9_]+$", cid):
            raise ImportError_("%s: ID=%s does not name the file" % (n, cid))
        out.append((cid, p, pin))
    return out


def u2a_list(pin):
    """T261 D7: [(name, sha256, size)] of the U2A field of a release-form pin, validated; [] without the field."""
    out, seen = [], set()
    for item in (pin.get("U2A") or "").split():
        f = item.split(":")
        if len(f) != 3 or not U2A_NAME.match(f[0]) or not SHA.match(f[1]) or not re.match(r"^[0-9]+$", f[2]) or not 0 < int(f[2]) <= MAX_FETCH:
            raise ImportError_("release/imports/%s.pin: U2A item %r is not <name>.u2a:<sha256>:<size>" % (pin.get("ID"), item[:80]))
        if f[0] in U2A_RESERVED:
            raise ImportError_("release/imports/%s.pin: U2A may not list %s (this build makes it)" % (pin.get("ID"), f[0]))
        if f[0] in seen:
            raise ImportError_("release/imports/%s.pin: U2A names %s twice" % (pin.get("ID"), f[0]))
        seen.add(f[0])
        out.append((f[0], f[1], int(f[2])))
    if out and not (pin.get("RELEASE") and pin.get("COR_SHA256")):
        raise ImportError_("release/imports/%s.pin: U2A needs the release form (RELEASE, COR_SHA256)" % pin.get("ID"))
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
    items = [("pin", sha256_bytes(open(os.path.join(root, IMPORTS, pin["ID"] + ".pin"), "rb").read()))]
    if pin.get("COR_SHA256"):
        items.append(("ci/imports.py", sha256_file(os.path.join(root, "ci", "imports.py"))))
    else:
        priv = private_files(root)
        for k in ("DESC", "OPS", "ICON"):
            for f in (pin.get(k) or "").split():                    # DESC may name several files (menu.toml desc.toml): one record 6
                items.append((f, sha256_file(rel(root, f))))
        files = pin.get("FILES")
        if files:
            top = rel(root, files)
            if not os.path.isdir(top):
                raise ImportError_("FILES=%s is no directory" % files)
            for d, dirs, names_ in os.walk(top):
                dirs.sort()
                for n in sorted(names_):
                    p = os.path.relpath(os.path.join(d, n), root)
                    if p not in priv:                               # a private file is hashed by its ci/private.txt line, present or not
                        items.append((p, sha256_file(os.path.join(root, p))))
            for p in sorted(priv):
                if p.startswith(files.rstrip("/") + "/"):
                    items.append((p, "private %d %s" % priv[p]))
        for c in CODE:
            items.append((c, sha256_file(os.path.join(root, c))))
    text = "imports-key 2\n" + "".join("%s\0%s\n" % i for i in items)
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


def source_url(pin, store, what, base=None, root=ROOT, asset=None):
    """Where the COR_SHA256 file of a pin is fetched from, and the name for messages: the release form (RELEASE) or the Gitea form (PACKAGE, VERSION).
    asset (T261 D7): another file of the same release (a U2A name) instead of COR_FILE."""
    pid = pin.get("ID")
    if asset:
        if not pin.get("RELEASE"):
            raise ImportError_("release/imports/%s.pin: %s comes from a release only" % (pid, asset))
        pin = dict(pin, COR_FILE=asset)
    if pin.get("RELEASE"):
        for k in ("COR_FILE", "COR_SIZE"):
            if not pin.get(k):
                raise ImportError_("release/imports/%s.pin has no %s" % (pid, k))
        if not NAME_OK.match(pin["RELEASE"]) or not NAME_OK.match(pin["COR_FILE"]):
            raise ImportError_("release/imports/%s.pin: RELEASE and COR_FILE are plain names" % pid)
        base = base or os.environ.get("TRX_FW_RELEASE_URL", "")
        if not base:
            repo = _env_file(os.path.join(root, "ci", "pkg.env")).get("MC_BUILDS_REPO", "")
            if not repo:
                raise ImportError_("ci/pkg.env has no MC_BUILDS_REPO")
            base = "https://github.com/%s/releases/download" % repo
        return "%s/%s/%s" % (base.rstrip("/"), pin["RELEASE"], pin["COR_FILE"]), "%s/%s" % (pin["RELEASE"], pin["COR_FILE"])
    base = base or os.environ.get("TRX_FW_PKG_URL", "")
    if not base:
        raise ImportError_("%s (%s...) is not in the store %s and TRX_FW_PKG_URL is not set" % (what, pin["COR_SHA256"][:12], store))
    for k in ("PACKAGE", "VERSION", "COR_FILE", "COR_SIZE"):
        if not pin.get(k):
            raise ImportError_("release/imports/%s.pin has no %s" % (pid, k))
    if not NAME_OK.match(pin["PACKAGE"]) or not NAME_OK.match(pin["VERSION"]) or not NAME_OK.match(pin["COR_FILE"]):
        raise ImportError_("release/imports/%s.pin: PACKAGE, VERSION and COR_FILE are plain names" % pid)
    return "%s/%s/%s/%s" % (base.rstrip("/"), pin["PACKAGE"], pin["VERSION"], pin["COR_FILE"]), "%s/%s/%s" % (pin["PACKAGE"], pin["VERSION"], pin["COR_FILE"])


def _env_file(path):
    d = {}
    for line in open(path):
        m = re.match(r"^(\w+)=(\S+)", line)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def _download(url, named, want, size, sha, store, opener):
    """One file into the store under its SHA-256; the size and the SHA-256 of the pin decide, a file that differs is not kept."""
    if not 0 < size <= MAX_FETCH:
        raise ImportError_("size %s of %s is out of range" % (size, want))
    os.makedirs(store, exist_ok=True)
    tmp = os.path.join(store, ".fetch-%d" % os.getpid())
    try:
        with opener(url, timeout=120) as r, open(tmp, "wb") as f:
            got = 0
            for b in iter(lambda: r.read(1 << 20), b""):
                got += len(b)
                if got > size:
                    raise ImportError_("%s is larger than its pinned size %d" % (want, size))
                f.write(b)
        if got != size or sha256_file(tmp) != sha:
            raise ImportError_("what %s serves is not the pinned file (size or SHA-256)" % named)
        os.replace(tmp, os.path.join(store, sha))
    except (urllib.error.URLError, OSError) as e:
        raise ImportError_("%s could not be fetched: %s" % (named, type(e).__name__))
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def fetch(pin, store, what, base=None, opener=urllib.request.urlopen):
    """The COR_SHA256 file of a pin into the store, from where source_url says; the SHA-256 and the size of the pin decide, a file that differs is not kept."""
    url, named = source_url(pin, store, what, base)
    try:
        size = int(pin.get("COR_SIZE") or 0)
    except ValueError:
        size = 0
    if not 0 < size <= MAX_FETCH:
        raise ImportError_("COR_SIZE %s is out of range" % pin.get("COR_SIZE"))
    _download(url, named, pin["COR_FILE"], size, pin["COR_SHA256"], store, opener)


def fetch_u2a(pin, item, store, base=None, opener=urllib.request.urlopen):
    """T261 D7: one U2A file (name, sha256, size) of a release-form pin into the store, from the release of the .cor."""
    name, sha, size = item
    url, named = source_url(pin, store, name, base, asset=name)
    _download(url, named, name, size, sha, store, opener)


def checked_cor(pin, path):
    """The pinned file at path: size and SHA-256 are the pin's, or ImportError_."""
    if os.path.getsize(path) != int(pin.get("COR_SIZE") or -1) or sha256_file(path) != pin["COR_SHA256"]:
        raise ImportError_("%s is not the file release/imports/%s.pin names (size or SHA-256)" % (os.path.basename(path), pin["ID"]))


def pack(entry, store, out, root=ROOT, run=subprocess.run):
    cid, path, pin = find(entry, root)
    os.makedirs(out, exist_ok=True)
    cor = os.path.join(out, cid + ".cor")
    if pin.get("COR_SHA256"):
        if not SHA.match(pin["COR_SHA256"]):
            raise ImportError_("release/imports/%s.pin: COR_SHA256 is no SHA-256" % cid)
        if not os.path.isfile(os.path.join(store, pin["COR_SHA256"])):
            fetch(pin, store, "COR_FILE " + pin.get("COR_FILE", "?"))
        src = from_store(store, pin["COR_SHA256"], "COR_FILE " + pin.get("COR_FILE", "?"))
        checked_cor(pin, src)
        shutil.copyfile(src, cor)
        for item in u2a_list(pin):                                  # T261 D7: the applications of the same release, beside the .cor
            name, sha, size = item
            if not os.path.isfile(os.path.join(store, sha)):
                fetch_u2a(pin, item, store)
            f = from_store(store, sha, name)
            if os.path.getsize(f) != size:
                raise ImportError_("%s is not the size release/imports/%s.pin names" % (name, cid))
            shutil.copyfile(f, os.path.join(out, name))
        return cor
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
    if not pin.get("APP_FILE"):
        raise ImportError_("release/imports/%s.pin has neither COR_SHA256 nor APP_FILE" % cid)
    args += ["--app-file", from_store(store, pin.get("APP_SHA256"), "APP_FILE " + pin["APP_FILE"])]
    env = dict(os.environ, GITHUB_REF_TYPE="tag")
    for k in ("GITHUB_SERVER_URL", "GITHUB_REPOSITORY", "PACKAGE_TOKEN"):
        env.pop(k, None)                                            # everything comes from the store: no download
    r = run(args, cwd=root, env=env)
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


def finish(cor, out, root=ROOT):
    """ci/package.sh: the builder's .cor in the form of this run (see the module text). Returns the version written, or "as pinned"."""
    sys.path.insert(0, os.path.join(root, "tools"))
    import cor_pack as C
    cid = os.path.basename(cor)[:-4] if cor.endswith(".cor") else ""
    pin = next((p for i, _, p in pins(root) if i == cid), None)
    if pin is None:
        raise ImportError_("%s: no packed pin release/imports/%s.pin" % (os.path.basename(cor), cid))
    if pin.get("COR_SHA256"):
        checked_cor(pin, cor)
        if os.path.abspath(cor) != os.path.abspath(out):
            shutil.copyfile(cor, out)
        return "as pinned"
    if not pin.get("APP_FILE"):
        raise ImportError_("release/imports/%s.pin has neither COR_SHA256 nor APP_FILE" % cid)
    blob = open(cor, "rb").read()
    v = version_of(pin, root)
    try:
        blob = C.retag(blob, v)
    except (C.CorError, ValueError) as e:
        raise ImportError_("%s: %s" % (os.path.basename(cor), e))
    with open(out, "wb") as f:
        f.write(blob)
    return v


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("names")
    sub.add_parser("u2a")
    p = sub.add_parser("pack")
    p.add_argument("entry")
    p.add_argument("--store", required=True)
    p.add_argument("--out", required=True)
    f = sub.add_parser("finish")
    f.add_argument("cor")
    f.add_argument("-o", "--out", required=True)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "names":
            for n in names():
                print(n)
        elif a.cmd == "u2a":
            for _, _, pin in pins():
                for name, _, _ in u2a_list(pin):
                    print(name)
        elif a.cmd == "pack":
            print(pack(a.entry, a.store, a.out))
        else:
            print("%s: %s" % (a.out, finish(a.cor, a.out)))
    except ImportError_ as e:
        print("ci/imports.py: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
