#!/usr/bin/env python3
"""T92 S2 (and T91): no ROM bytes, no media image, no snapshot in the Spectrum and CPC cores' trees.
The cores' benches run programs written in the repository (tools/zx_progs.py, cpc_progs.py) and load ROMs, disks and tapes from outside; so a tracked
file under cores/zx or cores/cpc may only be source (Verilog, VHDL, Tcl, TOML, shell), a checksum, a bench program as a hex text of at most 16 KB of
characters (our biggest is under 6 KB; a 16 KB ROM as hex text is over 48 KB), the C++ sources of the Spectrum's own app (cores/zx/app/lib, at most 64 KB each), or the licence texts of the vendored sources. The core logo
(<root>/logo/, T114) is ours and checked by tools/core_logos/build.py --check, so that directory is not looked at here.

  tools/check_no_rom.py           the tracked files of cores/zx and cores/cpc
  tools/check_no_rom.py selftest  a scratch tree with a .rom, a .bin and an oversized .hex must be refused, one with sources only must pass
"""
import os
import subprocess
import sys
import tempfile

ROOTS = ("cores/zx", "cores/cpc")
OK_EXT = {".v", ".sv", ".vh", ".vhd", ".tcl", ".toml", ".sh", ".py", ".sha256", ".meta", ".hex"}
OK_NAMES = {"NOTICE", "COPYING", "SHA256SUMS"}
HEX_MAX = 16 * 1024
APP_SRC_MAX = 64 * 1024     # a C++ source of the Spectrum's app (cores/zx/app/lib: zx.cc, zx_snap.*, the tape deck); our biggest is 15 KB


def bad_files(paths, base):
    bad = []
    for p in paths:
        name = os.path.basename(p)
        ext = os.path.splitext(name)[1].lower()
        size = os.path.getsize(os.path.join(base, p))
        if name in OK_NAMES:
            continue
        if p.split("/")[2:3] == ["logo"] and p.count("/") == 3:
            continue
        if p.startswith("cores/zx/app/lib/") and ext in (".cc", ".h") and size <= APP_SRC_MAX:       # the core's own app: C++ source (T239.5 D16), not data
            continue
        if ext not in OK_EXT:
            bad.append((p, "extension %r is not source, checksum or bench program" % ext))
        elif ext == ".hex" and size > HEX_MAX:
            bad.append((p, "a hex text of %d bytes could hold a ROM (limit %d)" % (size, HEX_MAX)))
    return bad


def check(base):
    out = subprocess.run(["git", "ls-files", "--"] + list(ROOTS), cwd=base, capture_output=True, text=True, check=True).stdout.split("\n")
    # a large core's cores/<id>/mono/host/ is its copy of the host files (T164 3a Q1, tools/check_large_copies.py), not part of the ported tree
    paths = [p for p in out if p and "/mono/host/" not in p]
    bad = bad_files(paths, base)
    for p, why in bad:
        print("check_no_rom: %s: %s" % (p, why))
    if bad:
        return 1
    print("check_no_rom: %d tracked files under %s, no ROM, media or snapshot" % (len(paths), " and ".join(ROOTS)))
    return 0


def selftest():
    with tempfile.TemporaryDirectory() as d:
        for sub, files in (("good", {"a.v": 10, "NOTICE": 5, "p.hex": 100}), ("bad", {"a.v": 10, "os.rom": 16384, "x.bin": 10, "big.hex": HEX_MAX + 1})):
            os.makedirs(os.path.join(d, sub))
            paths = []
            for n, sz in files.items():
                with open(os.path.join(d, sub, n), "wb") as f:
                    f.write(b"x" * sz)
                paths.append(n)
            bad = bad_files(paths, os.path.join(d, sub))
            if sub == "good":
                assert not bad, bad
            else:
                assert len(bad) == 3, bad
    print("check_no_rom selftest: ok (a .rom, a .bin and an oversized .hex are refused)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    sys.exit(check(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
