#!/usr/bin/env python3
"""T264 S2 fixtures for tools/toml_a_gen.py, on the decoders of tests/toml_a_list/fix (a Verilog and a VHDL one): converted -> green; the TOML A changed
after the generation (the block is stale) -> red; a constant the decode no longer uses -> red (both languages)."""
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import toml_a_gen as G   # noqa: E402

FIX = os.path.join(HERE, "..", "toml_a_list", "fix")
fails = []


def expect(name, reds, want):
    ok = (not reds) if want is None else (len(reds) == len(want) and all(any(re.search(w, r) for r in reds) for w in want))
    print("%s %s" % ("ok  " if ok else "FAIL", name) + ("" if ok else "   got: %s" % reds))
    if not ok:
        fails.append(name)


for lang in ("v", "vhd"):
    L = "verilog" if lang == "v" else "vhdl"
    d = tempfile.mkdtemp(prefix="tag.")
    try:
        rtl = os.path.join(d, "tfx_regs." + lang)
        toml = os.path.join(d, "gateware.toml")
        shutil.copy(os.path.join(FIX, "tfx_regs." + lang), rtl)
        shutil.copy(os.path.join(FIX, "gateware.toml"), toml)
        expect(L + ": a decode with no generated block is red", G.check_core("tfx", toml, d), [r"no generated constants block"])
        G.cmd_convert("tfx", toml, d)
        txt = open(rtl).read()
        print("%s %s: the decode uses the constants (%d uses)" % ("ok  " if "R_CTL_MODE" in txt else "FAIL", L, len(re.findall(r"R_CTL_\w+", txt))))
        expect(L + ": converted decode is green", G.check_core("tfx", toml, d), None)
        # the TOML A moves a register after the generation: the block is stale
        t = open(toml).read()
        open(toml, "w").write(t.replace('name = "caps2"', 'name = "capsx"', 1))
        expect(L + ": a stale generated block is red", G.check_core("tfx", toml, d), [r"not current", r"R_IDENT_CAPSX is not used"])
        open(toml, "w").write(t)
        # a constant the decode does not use: the use of R_CTL_MODE goes back to a literal
        txt = open(rtl).read()
        body_start = txt.index("<<< generated")
        lit = "12'h010" if lang == "v" else "16#010#"
        out = txt[:body_start] + re.sub(r"\bR_CTL_MODE\b", lit, txt[body_start:])
        open(rtl, "w").write(out)
        expect(L + ": a generated constant the decode does not use is red", G.check_core("tfx", toml, d), [r"constant R_CTL_MODE is not used"])
    finally:
        shutil.rmtree(d)

sys.exit(1 if fails else 0)
