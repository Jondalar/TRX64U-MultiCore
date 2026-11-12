#!/usr/bin/env python3
"""T143: the few upstream files Vivado cannot read as they are.

cores/neogeo/rtl is the pristine upstream copy (tools/vendor_neogeo.sh, checked by tools/check_neogeo_tree.py). A file Vivado 2024.1 refuses is copied to
cores/neogeo/fix/<name> with the smallest change, the change marked "TRX", and a unified diff <name>.diff against the pristine file is kept next to it;
cores/neogeo/sources.tcl reads the copy in place of the original. This script is the only thing that writes fix/: each EDITS entry is (file under rtl/,
old text, new text), replaced exactly once (line endings of the upstream file, CRLF, are kept).

  tools/neogeo_fix.py          write cores/neogeo/fix
  tools/neogeo_fix.py --check  fix/ is what the edits make of rtl/ (no hand edit, no stale copy), and every edit still applies
"""
import difflib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEO = os.path.join(ROOT, "cores/neogeo")

# (path under rtl/, old, new) - texts without line ends; the upstream line ending is added
EDITS = [
    ("video/irq.v",
     "\twire B56_Q, B56_nQ = ~B56_Q, B52_Q, B52_nQ = ~B52_Q, C52_Q;",
     "\t// TRX: Vivado refuses a declaration list that mixes initialised and plain identifiers: split in two\n"
     "\twire B56_Q, B52_Q, C52_Q;\n\twire B56_nQ = ~B56_Q, B52_nQ = ~B52_Q;"),
    ("video/lspc2_a2.v",
     "\twire nFLIP, nCHG_D = ~CHG_D, R15_QD;",
     "\t// TRX: Vivado refuses a declaration list that mixes initialised and plain identifiers: split in two\n"
     "\twire nFLIP, R15_QD;\n\twire nCHG_D = ~CHG_D;"),
    # TRX (T143 §3b P1): the address the sprite and the fix fetch will ask for, taken from the LSPC's own registers long before PCK1 / PCK2 latch it on the P bus
    ("video/lspc2_a2.v",
     "\toutput LSPC_EN_4M_N,\n",
     "\toutput LSPC_EN_4M_N,\n\toutput [23:0] TRX_C_CAND,\t\t\t// TRX: {SPR_TILE[19:16], SPR_LINE, SPR_TILE[15:3], SPR_TILE_AA}, what PCK1 latches from the P bus\n"
     "\toutput [15:0] TRX_S_CAND,\t\t\t// TRX: {PIXELC[2], RASTERC[2:1], FLIP, FIX_TILE}, what PCK2 latches\n"),
    ("video/lspc2_a2.v",
     "\twire CLK_24M = ~CLK_24MB;",
     "\tassign TRX_C_CAND = {SPR_TILE[19:16], SPR_TILE[15:8], SPR_TILE[7:3], SPR_TILE_AA, SPR_LINE};\n"
     "\tassign TRX_S_CAND = {FIX_TILE[11:8], FIX_TILE[7:0], PIXELC[2], RASTERC[2:1], FLIP};\n"
     "\twire CLK_24M = ~CLK_24MB;"),
    ("jt12/hdl/jt12_div.v",
     "reg [3:0] opn_pres, opn_cnt=4'd0;",
     "// TRX: Vivado refuses a declaration list that mixes initialised and plain identifiers: split in two\n"
     "reg [3:0] opn_pres;\nreg [3:0] opn_cnt=4'd0;"),
    ("jt12/hdl/jt12_div.v",
     "reg [2:0] ssg_pres, ssg_cnt=3'd0;",
     "// TRX: Vivado refuses a declaration list that mixes initialised and plain identifiers: split in two\n"
     "reg [2:0] ssg_pres;\nreg [2:0] ssg_cnt=3'd0;"),
]


def apply(path):
    data = open(os.path.join(NEO, "rtl", path), "rb").read().decode("utf-8", "surrogateescape")
    eol = "\r\n" if "\r\n" in data else "\n"
    for p, old, new in EDITS:
        if p != path:
            continue
        o = old.replace("\n", eol)
        n = new.replace("\n", eol)
        if data.count(o) != 1:
            raise SystemExit("neogeo_fix: %s: the text to change is there %d times (expected 1): %r" % (path, data.count(o), old[:60]))
        data = data.replace(o, n)
    return data


def build():
    out = {}
    for p in sorted({e[0] for e in EDITS}):
        name = os.path.basename(p)
        if name in {os.path.basename(q) for q in out}:
            raise SystemExit("neogeo_fix: two fixed files are called %s" % name)
        new = apply(p)
        old = open(os.path.join(NEO, "rtl", p), "rb").read().decode("utf-8", "surrogateescape")
        diff = "".join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), "rtl/" + p, "fix/" + name))
        out[name] = (new, diff.replace("\r", ""))
        out[p] = out.get(p)
    return {k: v for k, v in out.items() if v}


def main():
    files = build()
    fd = os.path.join(NEO, "fix")
    if "--check" in sys.argv:
        bad = []
        want = set()
        for name, (txt, diff) in files.items():
            want.update([name, name + ".diff"])
            for fn, body, mode in ((name, txt, "rb"), (name + ".diff", diff, "rb")):
                fp = os.path.join(fd, fn)
                if not os.path.exists(fp) or open(fp, "rb").read() != body.encode("utf-8", "surrogateescape"):
                    bad.append(fn)
        have = set(os.listdir(fd)) if os.path.isdir(fd) else set()
        for extra in sorted(have - want):
            bad.append(extra + " (not made by an edit)")
        if bad:
            print("neogeo_fix: cores/neogeo/fix is not what the edits make of rtl/: %s (run tools/neogeo_fix.py)" % ", ".join(sorted(bad)))
            return 1
        print("neogeo_fix: %d fixed files, fix/ is what %d edits make of the pristine rtl/" % (len(files), len(EDITS)))
        return 0
    os.makedirs(fd, exist_ok=True)
    for f in os.listdir(fd):
        os.remove(os.path.join(fd, f))
    for name, (txt, diff) in files.items():
        open(os.path.join(fd, name), "wb").write(txt.encode("utf-8", "surrogateescape"))
        open(os.path.join(fd, name + ".diff"), "wb").write(diff.encode("utf-8", "surrogateescape"))
    print("neogeo_fix: wrote %d files to cores/neogeo/fix" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
