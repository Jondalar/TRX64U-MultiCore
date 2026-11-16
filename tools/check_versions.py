#!/usr/bin/env python3
"""T221 S2: every deliverable carries a valid SemVer 2.0 in its own version file (lint stage).

bundle.toml, contract/version.toml (host gateware), host-app/version.toml (host menu), coreapp/version.toml (generic core app), the `version` of every
cores/<id>/core.toml and cores/<id>/mono/core.toml, and the SEMVER of every imported pin release/imports/*.pin that has one (x.y.z, or x.y.z-dev+<sha8> for a package that is no release, T230 P-1; a pin without it still
shows the commit, T221 D15 open: it is listed, not failed). A source file never holds a build string: x.y.z only, `-dev+<sha8>` is made at build time.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import semver  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAIN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


def main():
    errs, notes = [], []
    files = ["bundle.toml", "contract/version.toml", "host-app/version.toml", "coreapp/version.toml"]
    files += sorted(glob.glob(os.path.join(ROOT, "cores", "*", "core.toml")) + glob.glob(os.path.join(ROOT, "cores", "*", "mono", "core.toml")))
    for f in files:
        f = f if os.path.isabs(f) else os.path.join(ROOT, f)
        rel = os.path.relpath(f, ROOT)
        try:
            v = semver.read_file(f)
        except (ValueError, OSError) as e:
            errs.append("%s: %s" % (rel, e))
            continue
        if not PLAIN.match(v):
            errs.append("%s: version %r is not x.y.z (a build string is made at build time)" % (rel, v))
    for pin in sorted(glob.glob(os.path.join(ROOT, "release", "imports", "*.pin"))):
        m = re.search(r'^SEMVER="?([^"\n]*)"?', open(pin, encoding="utf-8").read(), re.M)
        rel = os.path.relpath(pin, ROOT)
        if not m:
            notes.append("%s: no SEMVER yet (D15: TRX64-Ultimate publishes it)" % rel)
            continue
        try:
            semver.parse(m.group(1))
        except ValueError as e:
            errs.append("%s: SEMVER %s" % (rel, e))
    for n in notes:
        print("note: " + n)
    for e in errs:
        print("FAIL: " + e)
    if not errs:
        print("versions ok: %d files" % len(files))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
