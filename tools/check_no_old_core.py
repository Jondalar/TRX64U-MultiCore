#!/usr/bin/env python3
"""The old DFX core cores/trx64 is deleted for good (Alex, 2026-11-08, T115 section 5). This check keeps it deleted:
it fails while the directory cores/trx64 exists or while a tracked file outside docs/ names the path "cores/trx64".

  tools/check_no_old_core.py            check this tree
  tools/check_no_old_core.py --selftest the checker against a scratch tree: a clean one passes, the directory back, a path
                                        in a Tcl list, a path in a script each turn it red (negative controls)

Not a hit: device paths of the board (/SD/cores/trx64.cor, /Usb0/cores/trx64/trx64.cor, cores/trx64u, cores/trx64g)."""
import os, re, subprocess, sys, tempfile

PAT = re.compile(r"(?<!/SD/)(?<!/Usb[0-9]/)(?<!/usb[0-9]/)(?<!/USB[0-9]/)(?<!/Flash/)cores/trx64(?![A-Za-z0-9_])(?!\.(?:cor|cfw|cfg)\b)(?!/trx64[./])")
EXEMPT = ("docs/", "tools/check_no_old_core.py")


def files(root):
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True).stdout
        return [p for p in out.decode().split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        return [os.path.relpath(os.path.join(d, f), root) for d, _, fs in os.walk(root) for f in fs]


def check(root):
    bad = []
    if os.path.isdir(os.path.join(root, "cores", "trx64")):
        bad.append("cores/trx64/ exists")
    for rel in files(root):
        if rel.startswith(EXEMPT) or not os.path.isfile(os.path.join(root, rel)):
            continue
        try:
            text = open(os.path.join(root, rel), encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue
        for n, line in enumerate(text.split("\n"), 1):
            if PAT.search(line):
                bad.append("%s:%d: %s" % (rel, n, line.strip()[:100]))
    return bad


def selftest():
    with tempfile.TemporaryDirectory() as t:
        def put(rel, s):
            os.makedirs(os.path.dirname(os.path.join(t, rel)), exist_ok=True)
            open(os.path.join(t, rel), "w").write(s)
        put("host/build.tcl", "set common ../cores/common\n")
        put("tests/a.cc", 'x("/SD/cores/trx64.cor"); y("/Usb0/cores/trx64/trx64.cor"); z("coreapp/cores/trx64u/menu.toml");\n')
        put("docs/specs/T115.md", "cores/trx64 was deleted\n")
        if check(t):
            return "a clean tree is red: %s" % check(t)
        put("cores/trx64/c64_core.v", "// x\n")
        if not check(t):
            return "the directory cores/trx64 is back and the check is green"
        os.remove(os.path.join(t, "cores/trx64/c64_core.v")); os.rmdir(os.path.join(t, "cores/trx64"))
        put("host/build.tcl", "set trx64  ../cores/trx64\n")
        if not check(t):
            return "a path in a Tcl file and the check is green"
        put("host/build.tcl", "set common ../cores/common\n")
        put("host/sim/x/run.sh", "iverilog $D/../cores/trx64/trx_io.v\n")
        if not check(t):
            return "a path in a bench script and the check is green"
    return None


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        err = selftest()
        print("check_no_old_core selftest: " + (err and "FAIL " + err or "ok (clean tree green; directory, Tcl path, script path each red)"))
        sys.exit(1 if err else 0)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bad = check(root)
    for b in bad:
        print(b)
    sys.exit(1 if bad else 0)
