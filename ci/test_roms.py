#!/usr/bin/env python3
"""Unit test of ci/roms.py (T239.1 S16): a small local file stands in for the upstream (file:// URL), no network.
Covers: fetch and hash check, kept file, store copy, wrong hash (red, nothing written), unreachable source (log line, strict exit 3), need."""
import hashlib, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROMS = os.path.join(HERE, "roms.py")
errs = 0


def run(*a):
    r = subprocess.run([sys.executable, "-I", ROMS] + list(a), capture_output=True, text=True, env={"PATH": os.environ.get("PATH", "")})
    return r.returncode, r.stdout + r.stderr


def expect(name, cond, info=""):
    global errs
    print("%s %s" % ("PASS" if cond else "FAIL", name))
    if not cond:
        errs += 1
        print("   " + info)


with tempfile.TemporaryDirectory() as t:
    data = b"rom bytes " * 20
    sha = hashlib.sha256(data).hexdigest()
    src = os.path.join(t, "up.bin")
    open(src, "wb").write(data)
    url = "file://" + src

    def lst(name, sha_, size, u):
        p = os.path.join(t, name)
        open(p, "w").write("# test\n%s %d dir/x.rom benchA,benchB %s\n" % (sha_, size, u))
        return p

    good = lst("good.txt", sha, len(data), url)
    root, store = os.path.join(t, "root"), os.path.join(t, "store")
    dest = os.path.join(root, "dir", "x.rom")

    rc, out = run("fetch", "--list", good, "--root", root, "--store", store)
    expect("fetch: downloads, rc 0, file written", rc == 0 and open(dest, "rb").read() == data and "fetched dir/x.rom" in out, out)
    expect("fetch: the store holds the file under its SHA-256", os.path.isfile(os.path.join(store, sha)))

    os.remove(src)
    rc, out = run("fetch", "--list", good, "--root", root, "--store", store)
    expect("fetch: a file that is there is kept (source gone)", rc == 0 and "kept dir/x.rom" in out, out)

    os.remove(dest)
    rc, out = run("fetch", "--list", good, "--root", root, "--store", store)
    expect("fetch: taken from the store (source gone)", rc == 0 and "store dir/x.rom" in out and open(dest, "rb").read() == data, out)

    open(dest, "wb").write(b"changed")
    rc, out = run("fetch", "--list", good, "--root", root, "--store", store)
    expect("fetch: a kept file with a wrong hash is red and stays", rc == 1 and "BAD" in out and open(dest, "rb").read() == b"changed", out)

    open(src, "wb").write(data)
    badl = lst("bad.txt", "0" * 64, len(data), url)
    root2 = os.path.join(t, "root2")
    rc, out = run("fetch", "--list", badl, "--root", root2)
    expect("fetch: a wrong hash from the source is red, nothing written", rc == 1 and "BAD" in out and not os.path.exists(os.path.join(root2, "dir", "x.rom")), out)
    badsz = lst("badsz.txt", sha, len(data) + 1, url)
    rc, out = run("fetch", "--list", badsz, "--root", root2)
    expect("fetch: a wrong size is red", rc == 1 and "size" in out, out)

    gone = lst("gone.txt", sha, len(data), "file://" + os.path.join(t, "nowhere.bin"))
    rc, out = run("fetch", "--list", gone, "--root", root2)
    expect("fetch: an unreachable source logs MISSING and exits 0", rc == 0 and "MISSING dir/x.rom" in out, out)
    rc, out = run("fetch", "--list", gone, "--root", root2, "--strict")
    expect("fetch --strict: unreachable source exits 3", rc == 3, out)

    rc, out = run("need", "benchA", "--list", good)
    expect("need: the paths of a bench", rc == 0 and out.split() == ["dir/x.rom"], out)
    rc, out = run("need", "benchZ", "--list", good)
    expect("need: a bench without ROMs gets nothing", rc == 0 and out.strip() == "", out)
    rc, out = run("fetch", "--list", good, "--root", root2, "--bench", "benchZ")
    expect("fetch --bench: other bench, nothing done", rc == 0 and out.strip() == "", out)

# the real list: 8 pins, well formed
rc, out = run("paths")
expect("ci/roms.txt: 8 pins that parse", rc == 0 and len(out.split()) == 8, out)

sys.exit(1 if errs else 0)
