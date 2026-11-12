#!/usr/bin/env python3
"""T142 S11: the SNES core's tree is the pinned upstream, ours, and nothing else.

  - cores/snes/rtl is the vendored tree byte for byte (rtl/SHA256SUMS: no changed byte, no file added or missing), every file named in the checksum list is tracked;
  - no ROM, chip dump or game under cores/snes: no .mif, .bin, .rom, .sfc, .smc, .swc, .fig, .srm, .img, no hex text over 16 KB; the only VHDL outside rtl/ is ip/ (ours);
  - no Altera megafunction (altsyncram, altdpram, lpm_*, altera_mf) in a line of code of the tree: those wrappers are tool output for Altera devices (written again in ip/);
  - SMP.vhd still has the boot ROM cut out: the IPLROM constant is the zero table and the two byte values of the first boot instructions do not appear in a row;
  - NOTICE names the upstream, its commit, the cut and the application table that carries the bytes.

  tools/check_snes_tree.py           the tracked files of cores/snes
  tools/check_snes_tree.py selftest  a scratch tree: a changed byte, a .mif, a .sfc, an altsyncram, a restored boot ROM and a foreign .vhd are refused
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

SNES = "cores/snes"
BAD_EXT = {".mif", ".bin", ".rom", ".sfc", ".smc", ".swc", ".fig", ".srm", ".img", ".sav", ".dat", ".zip", ".7z", ".gz2"}
OK_EXT = {".v", ".sv", ".vh", ".vhd", ".tcl", ".toml", ".sh", ".py", ".sha256", ".hex", ".txt", ".h", ".cc", ".s", ".gz", ".md", ".xdc", ".mem"}
OK_NAMES = {"NOTICE", "COPYING", "SHA256SUMS"}
HEX_MAX = 16 * 1024
ALLOWED_GZ = {"gen/snes_ghdl.v.gz"}
ALTERA = re.compile(r"\b(altsyncram|altdpram|altera_mf|lpm_mult|lpm_ram|lpm_rom|lpm_divide|altmult_add|altpll)\b", re.I)
IPL_START = re.compile(r'x"cd",\s*x"ef",\s*x"bd"', re.I)             # the first three bytes of the boot ROM, in the form the upstream wrote them


def code_lines(text, ext):
    """The lines without their comments (VHDL --, Verilog //)."""
    mark = "--" if ext == ".vhd" else "//" if ext in (".v", ".sv", ".vh", ".cc", ".h") else None
    for ln in text.split("\n"):
        yield ln.split(mark)[0] if mark else ln


def bad_files(paths, base):
    bad = []
    sums = {}
    sp = os.path.join(base, SNES, "rtl/SHA256SUMS")
    if os.path.exists(sp):
        for ln in open(sp):
            h, f = ln.split()
            sums[os.path.normpath(f)] = h
    seen = set()
    for p in paths:
        rel = p[len(SNES) + 1:]
        name = os.path.basename(p)
        ext = os.path.splitext(name)[1].lower()
        full = os.path.join(base, p)
        if ext in BAD_EXT:
            bad.append((p, "a %s file: no ROM, chip dump, game or save belongs in this tree" % ext))
            continue
        if rel.startswith("rtl/"):
            r = os.path.normpath(rel[4:])
            if name in ("NOTICE", "SHA256SUMS"):
                continue
            seen.add(r)
            if r not in sums:
                bad.append((p, "not in rtl/SHA256SUMS: a vendored file comes from tools/vendor_snes.sh only"))
                continue
            if hashlib.sha256(open(full, "rb").read()).hexdigest() != sums[r]:
                bad.append((p, "differs from the vendored file (SHA256SUMS)"))
                continue
            if ext == ".vhd":
                t = open(full, errors="replace").read()
                if IPL_START.search(t):
                    bad.append((p, "the boot ROM of the S-SMP is in the file (T142 D9, Q3: the application carries the bytes, not the tree)"))
                if r == "SMP.vhd" and not re.search(r'constant\s+IPLROM: IplRom_t := \(others => x"00"\);', t):
                    bad.append((p, "the IPLROM constant of SMP.vhd is not the zero table"))
            continue
        if rel in ALLOWED_GZ or name in OK_NAMES:
            continue
        if rel.startswith("logo/") and rel.count("/") == 1:
            continue
        if ext not in OK_EXT:
            bad.append((p, "extension %r is not source, checksum or bench program" % ext))
            continue
        if ext == ".gz":
            bad.append((p, "a .gz that is not the one raw conversion gen/snes_ghdl.v.gz"))
        elif ext == ".vhd" and not rel.startswith("ip/"):
            bad.append((p, "a VHDL file outside rtl/ and ip/"))
        elif ext == ".hex" and os.path.getsize(full) > HEX_MAX:
            bad.append((p, "a hex text of %d bytes could hold a ROM (limit %d)" % (os.path.getsize(full), HEX_MAX)))
        if ext in (".vhd", ".v", ".sv") and not rel.startswith("gen/"):
            for i, ln in enumerate(code_lines(open(full, errors="replace").read(), ext)):
                if ALTERA.search(ln):
                    bad.append((p, "line %d: an Altera megafunction (tool output for Altera devices, not usable here)" % (i + 1)))
                    break
    for r in sorted(set(sums) - seen):
        if any(q.startswith(SNES + "/rtl/") for q in paths):
            bad.append((SNES + "/rtl/" + r, "listed in SHA256SUMS but not tracked"))
    np = os.path.join(base, SNES, "rtl/NOTICE")
    if os.path.exists(np):
        t = open(np).read()
        for need in ("SNES_MiSTer", "bdcd33c5", "IPLROM", "spc_ipl", "Altera"):
            if need not in t:
                bad.append((SNES + "/rtl/NOTICE", "does not name %r" % need))
    return bad


def check(base):
    out = subprocess.run(["git", "ls-files", "--", SNES], cwd=base, capture_output=True, text=True, check=True).stdout.split("\n")
    # a large core's cores/<id>/mono/host/ is its copy of the host files (T164 3a Q1, tools/check_large_copies.py), not part of the ported tree
    paths = [p for p in out if p and "/mono/host/" not in p]
    bad = bad_files(paths, base)
    for p, why in bad:
        print("check_snes_tree: %s: %s" % (p, why))
    if bad:
        return 1
    print("check_snes_tree: %d tracked files under %s, rtl/ is the vendored tree, no ROM, chip dump, game or Altera wrapper" % (len(paths), SNES))
    return 0


def selftest():
    with tempfile.TemporaryDirectory() as d:
        r = os.path.join(d, SNES, "rtl")
        os.makedirs(r)
        os.makedirs(os.path.join(d, SNES, "ip"))
        smp = 'entity SMP is end;\nconstant  IPLROM: IplRom_t := (others => x"00");   -- cut\n'
        files = {"SMP.vhd": smp, "COPYING": "licence\n"}
        for f, t in files.items():
            open(os.path.join(r, f), "w").write(t)
        open(os.path.join(r, "SHA256SUMS"), "w").write("".join("%s  ./%s\n" % (hashlib.sha256(t.encode()).hexdigest(), f) for f, t in files.items()))
        open(os.path.join(r, "NOTICE"), "w").write("SNES_MiSTer bdcd33c5 IPLROM spc_ipl Altera\n")
        paths = ["%s/rtl/%s" % (SNES, f) for f in list(files) + ["SHA256SUMS", "NOTICE"]]
        assert not bad_files(paths, d), bad_files(paths, d)
        open(os.path.join(r, "SMP.vhd"), "w").write(smp + "-- x\n")                                                       # one byte changed
        assert any("differs" in w for _, w in bad_files(paths, d))
        boot = 'constant IPLROM: IplRom_t := (\n x"cd",\n x"ef",\n x"bd",\n x"e8");\n'                                   # the boot ROM restored (hash fixed)
        open(os.path.join(r, "SMP.vhd"), "w").write(boot)
        open(os.path.join(r, "SHA256SUMS"), "w").write("%s  ./SMP.vhd\n%s  ./COPYING\n" % (hashlib.sha256(boot.encode()).hexdigest(), hashlib.sha256(b"licence\n").hexdigest()))
        assert any("boot ROM" in w for _, w in bad_files(paths, d)), bad_files(paths, d)
        open(os.path.join(r, "SMP.vhd"), "w").write(smp)
        open(os.path.join(r, "SHA256SUMS"), "w").write("".join("%s  ./%s\n" % (hashlib.sha256(t.encode()).hexdigest(), f) for f, t in files.items()))
        for n, content in (("ip/dsp.mif", b"x"), ("sim/game.sfc", b"x"), ("ip/alt.vhd", b"altsyncram_component : altsyncram\n"), ("ip/foreign.hex", b"0" * 20000),
                           ("trx/s.srm", b"x")):
            os.makedirs(os.path.dirname(os.path.join(d, SNES, n)), exist_ok=True)
            open(os.path.join(d, SNES, n), "wb").write(content)
        bad = bad_files(["%s/%s" % (SNES, n) for n in ("ip/dsp.mif", "sim/game.sfc", "ip/alt.vhd", "ip/foreign.hex", "trx/s.srm")], d)
        assert len(bad) == 5, bad
        os.makedirs(os.path.join(d, SNES, "gen"), exist_ok=True)
        open(os.path.join(d, SNES, "gen/other.vhd"), "w").write("entity x is end;\n")
        assert any("outside" in w for _, w in bad_files(["%s/gen/other.vhd" % SNES], d))
    print("check_snes_tree selftest: ok (a changed byte, a .mif, a .sfc, a .srm, an Altera megafunction, a restored boot ROM, a foreign .vhd and a big hex text are refused)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    sys.exit(check(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
