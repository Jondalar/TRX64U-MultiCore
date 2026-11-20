#!/usr/bin/env python3
"""T171 guard: this repository writes (and reads back its own artefacts) only through MC_PKG.

The literal of the firmware repository's package (FW_PKG in ci/pkg.env) may appear only in the files that import
its output: tools/pack_import_cor.sh (reads, through $FW_PKG) and release/imports/*.pin. Everywhere else in
ci/, tools/, tests/ and .github/ a code line naming it is an error; comment lines are not code and are skipped.

  ci/check_pkg.py            check the tree
  ci/check_pkg.py --selftest negative control: a generated script that PUTs into FW_PKG has to be refused
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWED = {"tools/pack_import_cor.sh", "ci/check_pkg.py", "ci/pkg.env"}
SCAN = ("ci/", "tools/", "tests/", ".github/", "release/")


def env():
    d = {}
    for line in open(os.path.join(ROOT, "ci/pkg.env")):
        m = re.match(r"^(\w+)=(\S+)", line)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def scan(files, fw, root=ROOT):
    bad = []
    for rel in files:
        if rel in ALLOWED or rel.startswith("release/imports/"):
            continue
        try:
            text = open(os.path.join(root, rel), errors="replace").read()
        except OSError:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            code = line.strip()
            if not code or code.startswith("#"):
                continue
            if fw in code:
                bad.append(f"{rel}:{n}: names {fw} in code: {code[:100]}")
    return bad


def main():
    e = env()
    mc, fw = e.get("MC_PKG"), e.get("FW_PKG")
    errs = []
    if not mc or not fw or mc == fw:
        errs.append("ci/pkg.env: MC_PKG and FW_PKG must be set and different")
    if "--selftest" in sys.argv:
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "bad.sh"), "w").write(
                f'curl -X PUT --upload-file x "http://h/api/packages/TREX/generic/{fw}/v/x"\n')
            open(os.path.join(d, "ok.sh"), "w").write(f"# {fw} in a comment\ncurl \"$API/$MC_PKG/v\"\n")
            if not scan(["bad.sh"], fw, d):
                print("selftest: a write into FW_PKG was NOT refused")
                return 1
            if scan(["ok.sh"], fw, d):
                print("selftest: a clean file was refused")
                return 1
        print("selftest: write into FW_PKG refused, clean file accepted")
        return 0
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "--", *SCAN], capture_output=True, text=True, check=True)
    errs += scan(out.stdout.split(), fw)
    for p in ("ci/lib.sh",):
        if "generic/${MC_PKG}" not in open(os.path.join(ROOT, p)).read():
            errs.append(p + ": publish_files does not write through MC_PKG")
    print("\n".join(errs))
    return 1 if errs else 0


sys.exit(main())
