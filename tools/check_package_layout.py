#!/usr/bin/env python3
"""A package or release holds .cor files, never a core folder (T148 D8, S5).

The folder of a core - <dir>/<name>/ beside <dir>/<name>.cor, holding its settings, ROMs and saves - is the user's. A release that carried one
would overwrite it when copied over a medium, or put a file of ours where a person's ROMs and saves are. Refused:
  - a path with a folder below a "cores" folder (cores/<x>/...),
  - a folder beside a .cor of the same name (<name>.cor and <name>/),
  - a <name>.cfg beside its .cor (the settings are written by the core, not shipped).
The top-level folder SD/ is exempt (T239.5 D21): it is the files a core lane delivers for the SD card (SD/cores/<id>/tests, presets, panels), never a core's settings,
ROMs or saves; a whole package carries it; tools/make_release_mc.sh packs it as SD.zip (entries cores/<id>/...) into a pre-release and a release bundle, minus the cores with release = false.

  python3 tools/check_package_layout.py <directory | .zip> [...]     exit 1 and the names when a package holds one
  python3 tools/check_package_layout.py selftest
"""
import os
import sys
import tempfile
import zipfile


def names_of(path):
    if os.path.isdir(path):
        out = []
        for d, ds, fs in os.walk(path):
            rel = os.path.relpath(d, path)
            for x in ds:
                out.append((os.path.normpath(os.path.join(rel, x)) if rel != "." else x) + "/")
            for x in fs:
                out.append(os.path.normpath(os.path.join(rel, x)) if rel != "." else x)
        return out
    with zipfile.ZipFile(path) as z:
        return z.namelist()


def violations(names):
    """The paths of `names` (relative, '/' separated, a folder ends in '/') that make the package carry a core folder."""
    names = [n.replace("\\", "/") for n in names]
    files = set(n for n in names if not n.endswith("/"))
    dirs = set()
    for n in names:
        parts = n.rstrip("/").split("/")
        for i in range(1, len(parts) + (1 if n.endswith("/") else 0)):
            dirs.add("/".join(parts[:i]).lower())
    bad = []
    for n in sorted(names):
        parts = [p for p in n.rstrip("/").split("/") if p]
        low = [p.lower() for p in parts]
        if parts[:1] == ["SD"]:
            continue
        for i, p in enumerate(low[:-1]):
            if p == "cores" and (i + 2 < len(parts) or n.endswith("/")):
                bad.append(n + "  (a folder below cores/)")
                break
    for f in sorted(files):
        if f.lower().endswith(".cor"):
            stem = f[:-4].lower()
            if stem in dirs:
                bad.append(f[:-4] + "/  (the folder of " + f.rsplit("/", 1)[-1] + ")")
            if stem + ".cfg" in set(x.lower() for x in files):
                bad.append(f[:-4] + ".cfg  (the settings of " + f.rsplit("/", 1)[-1] + ")")
    seen = []
    for b in bad:
        if b not in seen:
            seen.append(b)
    return seen


def selftest():
    ok = [["a.cor", "b.cor", "INSTALL.md", "ROMS.txt", "SHA256SUMS"],
          ["trx64mc-main-x/", "trx64mc-main-x/vc20.cor", "trx64mc-main-x/INSTALL.md"],
          ["cores/", "cores/vc20.cor", "cores/amiga.cor"],
          ["diag.cor", "SD/cores/diag/tests/avprobe.toml", "SD/cores/diag/presets/x.toml"]]               # T239.5 D21: the SD/ tree of a package
    bad = [(["cores/apple2/roms/apple2e.rom", "cores/apple2.cor"], "cores/apple2/roms/apple2e.rom"),     # a core folder under cores/
           (["vc20.cor", "vc20/", "vc20/vc20.cfg"], "vc20/"),                                          # a folder beside its .cor
           (["pkg/vc20.cor", "pkg/vc20.cfg"], "pkg/vc20.cfg"),                                         # the settings beside the .cor
           (["pkg/CORES/x/"], "pkg/CORES/x/"),                                                            # the case of "cores" does not matter
           (["top/SD/cores/x/a.toml"], "top/SD/cores/x/a.toml")]                                       # SD/ is exempt at the top of a package only
    rc = 0
    for names in ok:
        v = violations(names)
        if v:
            print("selftest: refused a clean package %r: %r" % (names, v))
            rc = 1
    for names, want in bad:
        v = violations(names)
        if not any(want in x for x in v):
            print("selftest: did not refuse %r (wanted %r, got %r)" % (names, want, v))
            rc = 1
    with tempfile.TemporaryDirectory() as d:                                                           # a directory and a zip, as the two callers use it
        os.makedirs(os.path.join(d, "pkg", "cores", "x"))
        open(os.path.join(d, "pkg", "cores", "x", "x.cfg"), "w").write("a")
        open(os.path.join(d, "pkg", "x.cor"), "w").write("a")
        if not violations(names_of(os.path.join(d, "pkg"))):
            print("selftest: a directory with a core folder passed")
            rc = 1
        z = os.path.join(d, "p.zip")
        with zipfile.ZipFile(z, "w") as zf:
            zf.writestr("top/x.cor", "a")
            zf.writestr("top/x/x.cfg", "a")
        if not violations(names_of(z)):
            print("selftest: a zip with a core folder passed")
            rc = 1
    print("check_package_layout selftest: %s" % ("ok" if rc == 0 else "FAILED"))
    return rc


def main():
    a = sys.argv[1:]
    if a == ["selftest"]:
        sys.exit(selftest())
    if not a:
        raise SystemExit(__doc__)
    rc = 0
    for p in a:
        v = violations(names_of(p))
        for x in v:
            print("%s: %s" % (p, x))
        if v:
            print("%s: a package holds .cor files and no core folder (T148 D8); %d refused" % (p, len(v)))
            rc = 1
    sys.exit(rc)


if __name__ == "__main__":
    main()
