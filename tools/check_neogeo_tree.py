#!/usr/bin/env python3
"""T143 S1: the Neo Geo core's tree is the pinned upstream, ours, and nothing else.

  - cores/neogeo/rtl is the vendored tree byte for byte (rtl/SHA256SUMS: no changed byte, no file added or missing);
  - every HDL file there still has its licence header (the first 60 lines name a copyright or the licence), except the files that never had one
    upstream (named below, with the reason);
  - no ROM, BIOS, game or ROM-set file is tracked anywhere under cores/neogeo: no BIOS (.sp1 .rom .bin), no cartridge ROM (.p1 .s1 .c1 .v1 .m1
    .lo .sfix .neo), no archive (.zip), no disk image; the files outside rtl/ are source, text benches and checksums of at most 16 KB of text; the
    two .mem files in rtl/cpu/FX68K are the 68000 core's own microcode (known by hash through SHA256SUMS).

  tools/check_neogeo_tree.py           the tracked files of cores/neogeo
  tools/check_neogeo_tree.py selftest  a scratch tree: a changed byte, a stripped header, a .sp1, a .neo, a .zip and a big hex text are refused
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

NEO = "cores/neogeo"
OK_EXT = {".v", ".sv", ".vh", ".vhd", ".tcl", ".toml", ".sh", ".py", ".sha256", ".hex", ".txt", ".h", ".cc", ".s", ".md", ".xdc", ".lst", ".mem", ".diff"}
OK_NAMES = {"NOTICE", "COPYING", "LICENSE", "SHA256SUMS"}
HEX_MAX = 16 * 1024
NO_HEADER = {"io/register.v": "no header upstream (a four-line helper)",
             "jt12/hdl/jt12_pcm.v": "no header upstream",
             "cpu/FX68K/fx68k.sv": "Jorge Cwik's copyright line only; the licence text is cpu/FX68K/fx68k.txt (GPL v3 or later)",
             "cpu/FX68K/fx68kAlu.sv": "as fx68k.sv", "cpu/FX68K/uaddrPla.sv": "as fx68k.sv"}
HDL = {".v", ".sv", ".vhd", ".vh"}
ROMLIKE = {".rom", ".bin", ".sp1", ".p1", ".p2", ".s1", ".c1", ".c2", ".v1", ".v2", ".m1", ".lo", ".sfix", ".neo", ".zip", ".7z", ".img", ".bios", ".crt", ".chd", ".cue"}


def bad_files(paths, base):
    """paths relative to base, all under cores/neogeo/."""
    bad = []
    sums = {}
    sp = os.path.join(base, NEO, "rtl/SHA256SUMS")
    if os.path.exists(sp):
        for ln in open(sp):
            h, f = ln.split()
            sums[os.path.normpath(f)] = h
    seen = set()
    for p in paths:
        rel = p[len(NEO) + 1:]
        name = os.path.basename(p)
        ext = os.path.splitext(name)[1].lower()
        full = os.path.join(base, p)
        if ext in ROMLIKE:
            bad.append((p, "a ROM, BIOS, archive or image extension %r: those are the user's, never in this repository" % ext))
            continue
        if rel.startswith("rtl/"):
            r = os.path.normpath(rel[4:])
            if name in ("NOTICE", "SHA256SUMS"):
                continue
            seen.add(r)
            if r not in sums:
                bad.append((p, "not in rtl/SHA256SUMS: a vendored file comes from tools/vendor_neogeo.sh only"))
                continue
            if hashlib.sha256(open(full, "rb").read()).hexdigest() != sums[r]:
                bad.append((p, "differs from the vendored file (SHA256SUMS)"))
                continue
            if ext in HDL and r not in NO_HEADER:
                head = open(full, errors="replace").read().split("\n")[:60]
                if not any(re.search(r"copyright|general public licen|licen[sc]e", ln, re.I) for ln in head):
                    bad.append((p, "the licence header is gone"))
            continue
        if name in OK_NAMES:
            continue
        if rel.startswith("logo/") and rel.count("/") == 1:
            continue
        if ext not in OK_EXT:
            bad.append((p, "extension %r is not source, checksum or bench text" % ext))
        elif ext in (".hex", ".mem", ".txt", ".lst") and os.path.getsize(full) > HEX_MAX:
            bad.append((p, "a text of %d bytes could hold a ROM (limit %d)" % (os.path.getsize(full), HEX_MAX)))
    for r in sorted(set(sums) - seen):
        if any(q.startswith(NEO + "/rtl/") for q in paths):
            bad.append((NEO + "/rtl/" + r, "listed in SHA256SUMS but not tracked"))
    return bad


def check(base):
    out = subprocess.run(["git", "ls-files", "--", NEO], cwd=base, capture_output=True, text=True, check=True).stdout.split("\n")
    # a large core's cores/<id>/mono/host/ is its copy of the host files (T164 3a Q1, tools/check_large_copies.py), not part of the ported tree
    paths = [p for p in out if p and "/mono/host/" not in p]
    bad = bad_files(paths, base)
    for p, why in bad:
        print("check_neogeo_tree: %s: %s" % (p, why))
    if bad:
        return 1
    print("check_neogeo_tree: %d tracked files under %s, rtl/ is the vendored tree, no ROM, BIOS or game content" % (len(paths), NEO))
    return 0


def selftest():
    with tempfile.TemporaryDirectory() as d:
        r = os.path.join(d, NEO, "rtl")
        os.makedirs(os.path.join(r, "io"))
        files = {"io/a.v": "// Copyright 2020 X\nmodule a; endmodule\n", "LICENSE": "licence\n"}
        for f, t in files.items():
            open(os.path.join(r, f), "w").write(t)
        open(os.path.join(r, "SHA256SUMS"), "w").write("".join("%s  ./%s\n" % (hashlib.sha256(t.encode()).hexdigest(), f) for f, t in files.items()))
        open(os.path.join(r, "NOTICE"), "w").write("n\n")
        paths = ["%s/rtl/%s" % (NEO, f) for f in list(files) + ["SHA256SUMS", "NOTICE"]]
        assert not bad_files(paths, d), bad_files(paths, d)
        open(os.path.join(r, "io/a.v"), "w").write("// Copyright 2020 X\nmodule a; endmodule // x\n")                # one byte changed
        assert any("differs" in w for _, w in bad_files(paths, d))
        t = "module a; endmodule\n"                                                                                 # header stripped, hash fixed
        open(os.path.join(r, "io/a.v"), "w").write(t)
        open(os.path.join(r, "SHA256SUMS"), "w").write("%s  ./io/a.v\n" % hashlib.sha256(t.encode()).hexdigest())
        assert any("header" in w for _, w in bad_files([paths[0]], d)), bad_files([paths[0]], d)
        extra = ("trx/sp-s2.sp1", "sim/game.neo", "sim/set.zip", "sim/uni-bios.rom", "sim/c1.c1")
        for n in extra:
            os.makedirs(os.path.dirname(os.path.join(d, NEO, n)), exist_ok=True)
            open(os.path.join(d, NEO, n), "wb").write(b"x")
        big = os.path.join(d, NEO, "sim/rom.hex")
        open(big, "w").write("00\n" * 20000)
        bad = bad_files(["%s/%s" % (NEO, n) for n in extra] + ["%s/sim/rom.hex" % NEO], d)
        assert len(bad) == 6, bad
    print("check_neogeo_tree selftest: ok (a changed byte, a stripped header, a .sp1, a .neo, a .zip, a .rom, a .c1 and a big hex text are refused)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    sys.exit(check(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
