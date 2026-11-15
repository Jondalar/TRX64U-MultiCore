#!/usr/bin/env python3
"""T164 3a Q1 (Alex, 2026-11-01: "If that now needs a new core every time, then no include!" / "Copies instead."): every large core
(a cores/<id>/mono/core.toml with delivery = "cfw") builds from its OWN COPY of the host files, cores/<id>/mono/host/. This checks the rule, nothing else:

  - cores/<id>/mono/host/FROM exists, names the source commit, and lists exactly the files of the folder (the copy is documented, no stray file);
  - cores/<id>/mono/build_mono.tcl defines `set hd ../cores/<id>/mono/host` and names no file of the live host/ tree by its bare name (code and
    comment-free lines, outside string literals): such a name is read from Vivado's working directory host/, i.e. the shared tree, and a host change would
    reach the build behind the cache key's back;

FROM itself is documentation and is no input of any CI key (ci/code_hash.py is_doc, 2026-11-15), so it is not a probe of the `sources` either.
The copies are NOT compared with host/: they may differ from it and from each other (a host fix reaches a large core only by copying it in on purpose).
tools/host_pin.sh stays about host/ only.

  tools/check_large_copies.py            the checkout
  tools/check_large_copies.py selftest   a bare host name, a stray file, a missing FROM and a missing `set hd` must each be refused
"""
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TOK = r"[A-Za-z0-9_./$\-]+"
NOT_INPUT = ("sim/", "formal/", "bootrom/", "icap_test/")          # host/ folders no build reads
NOT_INPUT_FILES = ("build.tcl", "core.tcl", "layers.txt", "mig_ddr2.prj")


def sh(root, *a):
    return subprocess.run(a, cwd=root, capture_output=True, text=True, check=True).stdout


def host_names(root):
    out = set()
    for f in sh(root, "git", "ls-files", "-z", "--", "host").split("\0"):
        r = f[len("host/"):] if f.startswith("host/") else ""
        if not r or r.startswith(NOT_INPUT) or r in NOT_INPUT_FILES or r.endswith(".diff"):
            continue
        out.add(r)
    return out


def code_tokens(tcl):
    for line in tcl.split("\n"):
        if line.lstrip().startswith("#"):
            continue
        parts = re.split(r'("(?:[^"\\]|\\.)*")', line)           # odd entries are string literals
        for i in range(0, len(parts), 2):
            for t in re.findall(TOK, parts[i]):
                yield t


def large_cores(root):
    return sh(root, "python3", "tools/core_toml.py", "list", "cfw").split()


def check(root):
    bad = []
    names = host_names(root)
    for c in large_cores(root):
        d = "cores/%s/mono/host" % c
        tcl = os.path.join(root, "cores", c, "mono", "build_mono.tcl")
        if not os.path.isfile(os.path.join(root, d, "FROM")):
            bad.append("%s/FROM is missing: a large core carries its own copy of the host files and says where it is from" % d)
            continue
        from_txt = open(os.path.join(root, d, "FROM"), encoding="utf-8").read()
        if not re.search(r"\bcommit [0-9a-f]{7,}", from_txt):
            bad.append("%s/FROM names no source commit" % d)
        listed = set(x for x in from_txt.split("\nFiles:\n", 1)[-1].split("\n") if x) if "\nFiles:\n" in from_txt else set()
        have = set(f[len(d) + 1:] for f in sh(root, "git", "ls-files", "-z", "--", d).split("\0") if f) - {"FROM"}
        if listed != have:
            bad.append("%s: FROM lists / the folder holds different files: only listed %s, only in the folder %s" %
                       (d, sorted(listed - have)[:5], sorted(have - listed)[:5]))
        txt = open(tcl, encoding="utf-8").read()
        if not re.search(r"^set hd +\.\./%s\s*$" % re.escape(d), txt, re.M):
            bad.append("cores/%s/mono/build_mono.tcl does not `set hd ../%s`" % (c, d))
        live = sorted(set(t for t in code_tokens(txt) if t in names))
        if live:
            bad.append("cores/%s/mono/build_mono.tcl names live host/ files by their bare name (read from host/, not from the copy): %s" % (c, " ".join(live[:8])))
    return bad


def selftest():
    import shutil
    import tempfile
    t = tempfile.mkdtemp()
    try:
        def g(*a):
            subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t"] + list(a), cwd=t, check=True, capture_output=True)
        os.makedirs(os.path.join(t, "host"))
        os.makedirs(os.path.join(t, "tools"))
        shutil.copy(os.path.join(ROOT, "tools", "core_toml.py"), os.path.join(t, "tools"))
        os.makedirs(os.path.join(t, "cores", "x", "mono", "host"))
        open(os.path.join(t, "host", "pins.xdc"), "w").write("#\n")
        open(os.path.join(t, "cores", "x", "mono", "host", "pins.xdc"), "w").write("#\n")
        open(os.path.join(t, "cores", "x", "mono", "host", "FROM"), "w").write("Copy of host files, taken from host/ at commit abcdef123.\nFiles:\npins.xdc\n")
        open(os.path.join(t, "cores", "x", "mono", "build_mono.tcl"), "w").write("set hd     ../cores/x/mono/host\nread_xdc $hd/pins.xdc\n")
        open(os.path.join(t, "cores", "x", "mono", "core.toml"), "w").write(
            'id = "x"\nname = "X"\nversion = "0.0.1"\ncontract = 2\napp = "blueprint"\norder = 99\ndelivery = "cfw"\nexperimental = true\nsources = ["cores/x"]\n')
        g("init", "-q")
        g("add", "-A")
        g("commit", "-q", "-m", "x")
        root_ok = check(t)
        if root_ok:
            print("selftest: the clean fixture is refused: %s" % root_ok)
            return 1

        def mutate(name, fn, expect):
            fn()
            r = check(t)
            g("reset", "-q", "--hard")
            g("clean", "-qfd")
            if not any(expect in x for x in r):
                print("selftest FAIL: %s was not refused (%s)" % (name, r))
                return 1
            return 0
        tclp = os.path.join(t, "cores", "x", "mono", "build_mono.tcl")
        rc = 0
        rc |= mutate("a bare host file name", lambda: open(tclp, "a").write("read_xdc pins.xdc\n"), "bare name")
        rc |= mutate("a missing FROM", lambda: os.remove(os.path.join(t, "cores", "x", "mono", "host", "FROM")), "FROM is missing")
        rc |= mutate("a stray file", lambda: (open(os.path.join(t, "cores", "x", "mono", "host", "stray.v"), "w").write("\n"), g("add", "-A")), "different files")
        rc |= mutate("no set hd", lambda: open(tclp, "w").write("read_xdc $hd/pins.xdc\n"), "does not `set hd")
        return rc
    finally:
        shutil.rmtree(t, ignore_errors=True)


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    bad = check(ROOT)
    for b in bad:
        print(b)
    print("large-core copies: %s" % ("FAIL" if bad else "ok (%d cores)" % len(large_cores(ROOT))))
    sys.exit(1 if bad else 0)
