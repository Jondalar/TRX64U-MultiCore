#!/usr/bin/env python3
"""T113 S1: the tree of cores/c128 against its provenance file.
  - every source file (.v .sv .vh .vhd) outside sim/ and mono/ is named in PROVENANCE.md
  - the files taken from MiSTer carry their author line in the first 1500 bytes
  - nothing in the tree looks like a ROM image: no file named like a ROM, no binary of 4/8/16/32/72 KiB, no releases/ or mra/ directory
  tools/check_c128_tree.py [dir]       check cores/c128 (or dir)
  tools/check_c128_tree.py --selftest  plants a 16 KiB kernal.rom, an unlisted source and a header-less third-party file into a scratch copy; each must be reported
Exit status 1 when anything is reported."""
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THIRD = {   # third-party file (relative to cores/c128) -> text that must stand in its first 1500 bytes
    "c128_mmu8722.vhd": "Erik Scheffers",
    "z80/cpu_z80.vhd": "Erik Scheffers",
    "z80/T80.vhd": "Daniel Wallner", "z80/T80_ALU.vhd": "Daniel Wallner", "z80/T80_MCode.vhd": "Daniel Wallner",
    "z80/T80_Pack.vhd": "Daniel Wallner", "z80/T80_Reg.vhd": "Daniel Wallner", "z80/T80pa.vhd": "Daniel Wallner",
    "vdc/vdc_top.sv": "Erik Scheffers", "vdc/vdc_signals.sv": "Erik Scheffers", "vdc/vdc_signals_h.sv": "Erik Scheffers",
    "vdc/vdc_signals_v.sv": "Erik Scheffers", "vdc/vdc_ramiface.sv": "Erik Scheffers", "vdc/vdc_video.sv": "Erik Scheffers",
}
SRC = re.compile(r"\.(v|sv|vh|vhd|svh)$")
ROMNAME = re.compile(r"(kernal|basic|chargen|char\.?rom|boot[0-9]|\.rom$|\.crt$|\.prg$|\.d64$|\.d81$|\.g64$)", re.I)
ROMSIZE = {4096, 8192, 16384, 32768, 73728, 65536}


def check(base):
    bad = []
    prov = open(os.path.join(base, "PROVENANCE.md"), encoding="utf-8", errors="replace").read()
    for d, dirs, files in os.walk(base):
        rel_d = os.path.relpath(d, base)
        if rel_d.split(os.sep)[0] in ("releases", "mra") and rel_d != ".":
            bad.append("%s: a releases/ or mra/ directory (ROM images and bitstreams) is not taken" % rel_d)
        for f in files:
            rel = os.path.normpath(os.path.join(rel_d, f))
            p = os.path.join(d, f)
            top = rel.split(os.sep)[0]
            data = open(p, "rb").read()
            if ROMNAME.search(f) and not f.endswith((".py", ".sh", ".md", ".tcl", ".toml")):
                bad.append("%s: named like a ROM or a medium" % rel)
            binary = b"\0" in data[:1024]
            if binary and len(data) in ROMSIZE:
                bad.append("%s: a binary file of %d bytes looks like a ROM image" % (rel, len(data)))
            if top in ("sim", "mono") or not SRC.search(f):
                continue
            if os.path.basename(rel) not in prov and rel not in prov:
                bad.append("%s: a source file that PROVENANCE.md does not name" % rel)
            if rel in THIRD and THIRD[rel] not in data[:1500].decode("utf-8", "replace"):
                bad.append("%s: the author line (%s) is missing from the header" % (rel, THIRD[rel]))
    return bad


def selftest():
    tmp = tempfile.mkdtemp()
    try:
        cp = os.path.join(tmp, "c128")
        shutil.copytree(os.path.join(ROOT, "cores/c128"), cp, ignore=shutil.ignore_patterns("reports"))
        assert not check(cp), "the clean copy must pass: %s" % check(cp)
        open(os.path.join(cp, "kernal.rom"), "wb").write(os.urandom(16384))
        open(os.path.join(cp, "unlisted_new.v"), "w").write("module unlisted_new; endmodule\n")
        t = os.path.join(cp, "z80", "T80.vhd")
        s = open(t, encoding="utf-8", errors="replace").read()
        open(t, "w").write(s.replace("Daniel Wallner", "someone"))
        os.makedirs(os.path.join(cp, "releases"))
        open(os.path.join(cp, "releases", "x.rbf"), "wb").write(b"x")
        got = check(cp)
        for want in ("kernal.rom", "unlisted_new.v", "z80/T80.vhd", "releases"):
            hits = [g for g in got if g.startswith(want)]
            print("ctl| planted %s: %s" % (want, hits[0] if hits else "NOT REPORTED"))
            if not hits:
                return 1
        return 0
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    base = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "cores/c128")
    bad = check(base)
    for b in bad:
        print("FAIL " + b)
    print("check_c128_tree: %s" % ("%d problem(s)" % len(bad) if bad else "ok"))
    sys.exit(1 if bad else 0)
