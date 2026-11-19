#!/usr/bin/env python3
"""Bench ROMs from the pinned upstream (T239.1 D12, S2, S16): the list is ci/roms.txt.

  roms.py fetch [--root DIR] [--list FILE] [--store DIR] [--bench NAME] [--strict] [PATH ...]
      makes each pinned file exist at DIR/<path> (DIR defaults to the repository root) with the pinned size and SHA-256:
      kept when it is there, else copied from the store (--store, ROMS_STORE, else /cache/multicore/roms when that directory exists; files named by their SHA-256), else downloaded
      (and then put into the store). PATH ... or --bench NAME limit the work to those files.
      exit 0 all present (or, without --strict, a source could not be reached: a MISSING line says so),
      exit 1 a size or hash is wrong (the bad file is never written; a kept file with a wrong hash stays and is reported),
      exit 3 --strict and a file is missing, exit 2 usage.
  roms.py need BENCH [--list FILE]   the paths BENCH reads, one per line
  roms.py paths [--list FILE]        every pinned path"""
import hashlib, os, sys, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def load(path=None):
    path = path or os.path.join(ROOT, "ci", "roms.txt")
    pins = []
    for line in open(path, encoding="utf-8").read().split("\n"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split()
        if len(f) != 5 or len(f[0]) != 64 or not f[1].isdigit():
            raise SystemExit("roms: bad line in %s: %s" % (path, line))
        pins.append({"sha": f[0], "size": int(f[1]), "path": f[2], "benches": f[3].split(","), "url": f[4]})
    return pins


def check(data, pin):
    if len(data) != pin["size"]:
        return "size %d, pinned %d" % (len(data), pin["size"])
    if hashlib.sha256(data).hexdigest() != pin["sha"]:
        return "sha256 %s, pinned %s" % (hashlib.sha256(data).hexdigest(), pin["sha"])
    return None


def write_atomic(dest, data):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = "%s.tmp%d" % (dest, os.getpid())
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, dest)


def get(pin, root, store):
    """-> (state, detail): kept | store | fetched | MISSING | BAD"""
    dest = os.path.join(root, pin["path"])
    if os.path.isfile(dest):
        bad = check(open(dest, "rb").read(), pin)
        return ("BAD", "kept file: " + bad) if bad else ("kept", "")
    sf = os.path.join(store, pin["sha"]) if store else None
    if sf and os.path.isfile(sf):
        data = open(sf, "rb").read()
        bad = check(data, pin)
        if not bad:
            write_atomic(dest, data)
            return "store", ""
        os.remove(sf)       # a damaged store entry is not used and not kept
    try:
        with urllib.request.urlopen(pin["url"], timeout=60) as r:
            data = r.read()
    except Exception as e:
        return "MISSING", "%s: %s" % (pin["url"], e)
    bad = check(data, pin)
    if bad:
        return "BAD", "downloaded from %s: %s" % (pin["url"], bad)
    write_atomic(dest, data)
    if sf:
        try:
            write_atomic(sf, data)
        except OSError:
            pass
    return "fetched", ""


def main(a):
    if not a or a[0] not in ("fetch", "need", "paths"):
        sys.stderr.write(__doc__); return 2
    cmd, a = a[0], a[1:]
    opt = {"--root": ROOT, "--list": None, "--store": os.environ.get("ROMS_STORE") or ("/cache/multicore/roms" if os.path.isdir("/cache/multicore/roms") else None), "--bench": None}
    strict, rest = False, []
    i = 0
    while i < len(a):
        if a[i] in opt and i + 1 < len(a):
            opt[a[i]] = a[i + 1]; i += 2
        elif a[i] == "--strict":
            strict = True; i += 1
        else:
            rest.append(a[i]); i += 1
    pins = load(opt["--list"])
    if cmd == "paths":
        print("\n".join(p["path"] for p in pins)); return 0
    if cmd == "need":
        if len(rest) != 1:
            sys.stderr.write(__doc__); return 2
        print("\n".join(p["path"] for p in pins if rest[0] in p["benches"])); return 0
    if opt["--bench"]:
        pins = [p for p in pins if opt["--bench"] in p["benches"]]
    if rest:
        known = set(p["path"] for p in pins)
        for r in rest:
            if r not in known:
                sys.stderr.write("roms: %s is not pinned\n" % r); return 2
        pins = [p for p in pins if p["path"] in rest]
    rc = 0
    for p in pins:
        state, detail = get(p, opt["--root"], opt["--store"])
        print("roms: %s %s%s" % (state, p["path"], " (%s)" % detail if detail else ""))
        if state == "BAD":
            rc = 1
        elif state == "MISSING" and strict and rc == 0:
            rc = 3
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
