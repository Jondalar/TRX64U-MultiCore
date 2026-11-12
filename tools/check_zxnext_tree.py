#!/usr/bin/env python3
"""T116 S1: the Next core's tree is the pinned upstream, ours, and nothing else.

  - cores/zxnext/rtl is the vendored tree byte for byte (rtl/SHA256SUMS: no changed byte, no file added or missing);
  - every VHDL file there still has its licence header (the first 60 lines name a copyright or the licence), except the three files that never
    had one upstream (named below, with the reason);
  - no ROM, disk image, NextZXOS file or snapshot is tracked anywhere under cores/zxnext: only source, checksums, bench program texts of at most
    16 KB of characters, the logo directory (checked by tools/core_logos/build.py --check) and the one raw conversion gen/zxnext_ghdl.v.gz;
  - the only VHDL outside rtl/ is ip/zxn_ip.vhd (ours). rom/bootrom.vhd, the FPGA core's boot ROM (GPL v3 source of the upstream repository, data
    as VHDL), is known by its hash; a second image-like VHDL file is refused.

  tools/check_zxnext_tree.py           the tracked files of cores/zxnext
  tools/check_zxnext_tree.py selftest  a scratch tree: a changed byte, a stripped header, a .rom, a foreign .vhd, a .bin and an .img are refused
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ZXN = "cores/zxnext"
OK_EXT = {".v", ".sv", ".vh", ".vhd", ".tcl", ".toml", ".sh", ".py", ".sha256", ".meta", ".hex", ".txt", ".h", ".cc", ".s"}
OK_NAMES = {"NOTICE", "COPYING", "SHA256SUMS"}
HEX_MAX = 16 * 1024
NO_HEADER = {"audio/dac.vhd": "Xilinx application note XAPP154 sigma-delta, no header upstream",
             "rom/bootrom.vhd": "machine generated boot ROM data (romgen), no header upstream",
             "rom/bootrom_ab.vhd": "machine generated boot ROM data (romgen), no header upstream"}
ALLOWED = {"ip/zxn_ip.vhd", "gen/zxnext_ghdl.v.gz"}


def bad_files(paths, base):
    """paths relative to base, all under cores/zxnext/."""
    bad = []
    sums = {}
    sp = os.path.join(base, ZXN, "rtl/SHA256SUMS")
    if os.path.exists(sp):
        for ln in open(sp):
            h, f = ln.split()
            sums[os.path.normpath(f)] = h
    seen = set()
    for p in paths:
        rel = p[len(ZXN) + 1:]
        name = os.path.basename(p)
        ext = os.path.splitext(name)[1].lower()
        full = os.path.join(base, p)
        if rel.startswith("rtl/"):
            r = os.path.normpath(rel[4:])
            if name in ("NOTICE", "SHA256SUMS"):
                continue
            seen.add(r)
            if r not in sums:
                bad.append((p, "not in rtl/SHA256SUMS: a vendored file comes from tools/vendor_zxnext.sh only"))
                continue
            if hashlib.sha256(open(full, "rb").read()).hexdigest() != sums[r]:
                bad.append((p, "differs from the vendored file (SHA256SUMS)"))
                continue
            if ext == ".vhd" and r not in NO_HEADER:
                head = open(full, errors="replace").read().split("\n")[:60]
                if not any(re.search(r"copyright|general public licen|licen[sc]e", ln, re.I) for ln in head):
                    bad.append((p, "the licence header is gone"))
            continue
        if rel in ALLOWED or name in OK_NAMES:
            continue
        if rel.startswith("logo/") and rel.count("/") == 1:
            continue
        if ext not in OK_EXT:
            bad.append((p, "extension %r is not source, checksum or bench program" % ext))
        elif ext == ".vhd":
            bad.append((p, "a VHDL file outside rtl/ and ip/zxn_ip.vhd (an image-like file in the Next's tree?)"))
        elif ext == ".hex" and os.path.getsize(full) > HEX_MAX:
            bad.append((p, "a hex text of %d bytes could hold a ROM (limit %d)" % (os.path.getsize(full), HEX_MAX)))
    for r in sorted(set(sums) - seen):
        if any(q.startswith(ZXN + "/rtl/") for q in paths):
            bad.append((ZXN + "/rtl/" + r, "listed in SHA256SUMS but not tracked"))
    return bad


def check(base):
    out = subprocess.run(["git", "ls-files", "--", ZXN], cwd=base, capture_output=True, text=True, check=True).stdout.split("\n")
    # a large core's cores/<id>/mono/host/ is its copy of the host files (T164 3a Q1, tools/check_large_copies.py), not part of the ported tree
    paths = [p for p in out if p and "/mono/host/" not in p]
    bad = bad_files(paths, base)
    for p, why in bad:
        print("check_zxnext_tree: %s: %s" % (p, why))
    if bad:
        return 1
    print("check_zxnext_tree: %d tracked files under %s, rtl/ is the vendored tree, no ROM, media or NextZXOS content" % (len(paths), ZXN))
    return 0


def selftest():
    with tempfile.TemporaryDirectory() as d:
        r = os.path.join(d, ZXN, "rtl")
        os.makedirs(os.path.join(r, "cpu"))
        os.makedirs(os.path.join(r, "rom"))
        files = {"cpu/a.vhd": "-- Copyright 2020 X\nentity a is end;\n", "rom/bootrom.vhd": "-- generated\nentity b is end;\n", "COPYING": "licence\n"}
        for f, t in files.items():
            open(os.path.join(r, f), "w").write(t)
        open(os.path.join(r, "SHA256SUMS"), "w").write("".join("%s  ./%s\n" % (hashlib.sha256(t.encode()).hexdigest(), f) for f, t in files.items()))
        open(os.path.join(r, "NOTICE"), "w").write("n\n")
        paths = ["%s/rtl/%s" % (ZXN, f) for f in list(files) + ["SHA256SUMS", "NOTICE"]]
        assert not bad_files(paths, d), bad_files(paths, d)
        open(os.path.join(r, "rom/bootrom.vhd"), "w").write("-- generated\nentity b is end; -- x\n")          # one byte changed
        assert any("differs" in w for _, w in bad_files(paths, d))
        open(os.path.join(r, "rom/bootrom.vhd"), "w").write(files["rom/bootrom.vhd"])
        os.remove(os.path.join(r, "COPYING"))                                                                      # a vendored file gone
        assert any("not tracked" in w or "differs" in w or "No such" in w for _, w in bad_files([p for p in paths if not p.endswith("COPYING")], d))
        t = "entity a is end;\n"                                                                                   # header stripped, hash fixed
        open(os.path.join(r, "cpu/a.vhd"), "w").write(t)
        open(os.path.join(r, "SHA256SUMS"), "w").write("%s  ./cpu/a.vhd\n" % hashlib.sha256(t.encode()).hexdigest())
        assert any("header" in w for _, w in bad_files([paths[0], paths[3]], d)), bad_files([paths[0], paths[3]], d)
        extra = ("gen/x.rom", "gen/nextzxos.vhd", "gen/y.bin", "sim/n.img")
        for n in extra:
            os.makedirs(os.path.dirname(os.path.join(d, ZXN, n)), exist_ok=True)
            open(os.path.join(d, ZXN, n), "wb").write(b"x")
        bad = bad_files(["%s/%s" % (ZXN, n) for n in extra], d)
        assert len(bad) == 4, bad
    print("check_zxnext_tree selftest: ok (a changed byte, a missing file, a stripped header, a .rom, a foreign .vhd, a .bin and an .img are refused)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    sys.exit(check(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
