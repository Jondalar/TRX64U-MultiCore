#!/usr/bin/env python3
"""T264 S1 fixtures for tools/toml_a_list.py: a Verilog and a VHDL decoder, the TOML A matching, then a moved offset, a removed
register and an extra register (each must be red with the address), and the decode text that rtl_check.py never saw."""
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import toml_a_list as T   # noqa: E402

FIX = os.path.join(HERE, "fix")
fails = []


def run(name, lang, toml_edit=None, rtl_edit=None):
    d = tempfile.mkdtemp(prefix="tal.")
    try:
        ext = "v" if lang == "v" else "vhd"
        src = open(os.path.join(FIX, "tfx_regs." + ext)).read()
        toml = open(os.path.join(FIX, "gateware.toml")).read()
        if toml_edit:
            toml = toml_edit(toml)
        if rtl_edit:
            src = rtl_edit(src)
        open(os.path.join(d, "tfx_regs." + ext), "w").write(src)
        open(os.path.join(d, "gateware.toml"), "w").write(toml)
        reds, notes, nt, nr, dt = T.check_core("tfx", os.path.join(d, "gateware.toml"), d)
        return reds
    finally:
        shutil.rmtree(d)


def expect(name, reds, want):
    """want: None = PASS; else a list of regexes, each of which a red line must match, and no other red line."""
    if want is None:
        ok = not reds
    else:
        ok = len(reds) == len(want) and all(any(re.search(w, r) for r in reds) for w in want)
    print("%s %s" % ("ok  " if ok else "FAIL", name) + ("" if ok else "   got: %s" % reds))
    if not ok:
        fails.append(name)


for lang in ("v", "vhd"):
    L = "verilog" if lang == "v" else "vhdl"
    expect(L + ": the matching decoder is green", run("", lang), None)
    # a moved offset: the RTL decodes 0x10 -> 0x18, the TOML A still says 0x10
    mv = (lambda s: s.replace("12'h010", "12'h018")) if lang == "v" else (lambda s: s.replace('x"010"', 'x"018"'))
    expect(L + ": a moved offset is red on both sides", run("", lang, rtl_edit=mv), [r"ctl\.mode \(0x101A0010", r"RTL decodes 0x101A0018"])
    # a removed register: the TOML A names ctl.base (0x14-0x17), the RTL no longer decodes 0x16
    rm = (lambda s: s.replace("12'h016", "12'h0F6")) if lang == "v" else (lambda s: s.replace('x"016"', 'x"0F6"'))
    expect(L + ": a register removed from the RTL is red", run("", lang, rtl_edit=rm), [r"ctl\.base \(0x101A0016", r"RTL decodes 0x101A00F6"])
    # an extra register: the RTL decodes 0x20, the TOML A does not name it
    ex = (lambda s: s.replace("12'h005: q <= 8'h02;", "12'h005: q <= 8'h02;\n                12'h020: q <= 8'h07;")) if lang == "v" else \
         (lambda s: s.replace('when x"005" => q <= x"02";', 'when x"005" => q <= x"02";\n          when x"020" => q <= x"07";'))
    expect(L + ": an extra RTL register is red", run("", lang, rtl_edit=ex), [r"RTL decodes 0x101A0020-0x101A0020"])
    # a TOML A register the RTL never had
    ta = lambda s: s + '\n[[block]]\nname = "extra"\nmodule = "tfx_regs"\nowner = "core"\naddress = 0x101A0030\nsize = 0x01\n  [[block.property]]\n  name = "x"\n  offset = 0x0\n  width = 8\n  dir = "rw"\n'
    expect(L + ": a TOML A register the RTL does not decode is red", run("", lang, toml_edit=ta), [r"extra\.x \(0x101A0030"])

# a range compare and a word-granular case, from text
d = tempfile.mkdtemp(prefix="tal.")
try:
    p = os.path.join(d, "r.v")
    open(p, "w").write("module r(input [16:0] addr);\nwire [3:0] page = addr[15:12];\nwire [11:0] off = addr[11:0];\n"
                       "always @* begin if (off[11:4] == 8'h04) x = 1; if (off >= 12'h160 && off < 12'h16A) y = 1; if (off >= 12'h1A0 && off < 12'h1B8 && !off[3]) z = 1;\n"
                       "case (off[11:2]) 10'h002: w = 1; endcase end\nendmodule\n")
    got, coarse = T.decoded(p)
    want = set(range(0x40, 0x50)) | set(range(0x160, 0x16A)) | {a for a in range(0x1A0, 0x1B8) if not a & 8} | set(range(8, 12))
    ok = set(got) == want
    print("%s slice, range, !bit and word-case compares" % ("ok  " if ok else "FAIL"))
    if not ok:
        fails.append("ranges")
finally:
    shutil.rmtree(d)

# the VHDL form of a2_regs: a variable o := to_integer(unsigned(off)), based literals 16#010#, a `to` range choice and a `>= and <` fallback
d = tempfile.mkdtemp(prefix="tal.")
try:
    p = os.path.join(d, "r.vhd")
    open(p, "w").write("entity r is end;\narchitecture a of r is begin process begin\n  if win = '0' and page = x\"0\" then\n    o := to_integer(unsigned(off));\n"
                       "    case o is\n      when 16#010# => q <= a;\n      when 16#020# to 16#023# => q <= b;\n      when 16#030# | 16#032# => q <= c;\n"
                       "      when others => if o >= 16#100# and o < 16#104# then q <= d; end if;\n    end case;\n  end if;\nend process; end architecture;\n")
    got, _ = T.decoded(p)
    want = {0x10} | set(range(0x20, 0x24)) | {0x30, 0x32} | set(range(0x100, 0x104))
    ok = set(got) == want
    print("%s vhdl variable selector, 16#..# literals, `to` range" % ("ok  " if ok else "FAIL got %s" % sorted(got)))
    if not ok:
        fails.append("vhdl-var")
finally:
    shutil.rmtree(d)

sys.exit(1 if fails else 0)
