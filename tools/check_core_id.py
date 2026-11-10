#!/usr/bin/env python3
"""T91/T92: the identity word a core's register window answers (bytes 000-003, byte 0 first) must be the id its application
descriptor names, or the generic application refuses the core ("no description for the core with the ID ..."; the ZX core
answered "XZSP" on the board, 2026-10-22, because its 32-bit constant had the first two bytes swapped).

  tools/check_core_id.py            every pair in PAIRS
  tools/check_core_id.py selftest   a swapped constant must be refused, the right one accepted
"""
import re
import sys

# (register file with `wire [31:0] id = ... : 32'h........;`, application descriptor with `{ 'A', 'B', 'C', 'D' },`)
PAIRS = (("cores/zx/trx/zx_regs.v", "coreapp/cores/zx.cc"),
         ("cores/cpc/trx/cpc_regs.v", "coreapp/cores/cpc.cc"),
         ("cores/vc20/rtl/trx/vc20_regs.vhd", "coreapp/cores/vc20.cc"),
         ("cores/c264/rtl/trx/c264_regs.v", "coreapp/cores/c264.cc"),
         ("cores/zxnext/trx/zxn_regs.v", "coreapp/cores/zxnext.cc"),
         ("cores/snes/trx/snes_regs.v", "coreapp/cores/snes.cc"),
         ("cores/c128/c128_regs.v", "coreapp/cores/c128.cc"),
         ("cores/c64scpu/c64scpu_regs.v", "coreapp/cores/c64scpu.cc"),
         ("cores/neogeo/trx/neo_regs.v", "coreapp/cores/neogeo.cc"),
         ("cores/ddt_mandel/layer2_ddt_mandel.v", "coreapp/cores/ddt_mandel.cc"),
         ("cores/ddt_novic/layer2_ddt_novic.v", "coreapp/cores/ddt_novic.cc"),
         ("cores/diag/rtl/diag_regs.v", "coreapp/cores/portdiag.cc"))


def gw_id(text):
    m = re.search(r"wire\s*\[31:0\]\s*id\s*=\s*(?:[^;]*?:\s*)?32'h([0-9A-Fa-f]{8})\s*;", text)       # a ternary on ID_TRX2, or the constant alone (neo_regs.v)
    if not m:                                           # the VHDL form (vc20_regs.vhd): if ID_TRX2 /= 0 then id := x"..."; else id := x"..."; end if;
        m = re.search(r'else\s+id\s*:=\s*x"([0-9A-Fa-f]{8})"\s*;', text)
    if not m:                                           # the layer2 file of the DDT cores: the parameter of the register block, `.ID(32'h4D544444)`
        m = re.search(r"\.ID\(\s*32'h([0-9A-Fa-f]{8})\s*\)", text)
    if not m:                                           # c128_regs.v: the four bytes are written one by one, `12'h000: q <= "C";`
        ch = dict(re.findall(r"12'h00([0-3]):\s*q\s*<=\s*\"(.)\"", text))
        if len(ch) != 4:                                # diag_regs.v: the bytes as numbers, `12'h000: q <= 8'h44;`
            ch = {k: chr(int(v, 16)) for k, v in re.findall(r"12'h00([0-3]):\s*q\s*<=\s*8'h([0-9A-Fa-f]{2})\s*;", text)}
        return "".join(ch[str(i)] for i in range(4)) if len(ch) == 4 else None
    v = int(m.group(1), 16)
    return "".join(chr((v >> (8 * i)) & 0xFF) for i in range(4))


def app_id(text):
    m = re.search(r"\{\s*'(.)',\s*'(.)',\s*'(.)',\s*'(.)'\s*\}", text)
    return "".join(m.groups()) if m else None


def compare(gw_text, app_text):
    g, a = gw_id(gw_text), app_id(app_text)
    return g is not None and a is not None and g == a, g, a


def main():
    if sys.argv[1:] == ["selftest"]:
        app = "{ 'Z', 'X', 'S', 'P' },"
        bad = compare("wire [31:0] id = (X != 0) ? 32'h32585254 : 32'h50535A58;", app)[0]
        good = compare("wire [31:0] id = (X != 0) ? 32'h32585254 : 32'h5053585A;", app)[0]
        if bad or not good:
            print("check_core_id selftest: FAILED (swapped accepted %s, right refused %s)" % (bad, not good))
            return 1
        vhd = 'if ID_TRX2 /= 0 then id := x"32585254"; else id := x"%s"; end if;'
        vbad = compare(vhd % "30324365", "{ 'V', 'C', '2', '0' },")[0]
        vgood = compare(vhd % "30324356", "{ 'V', 'C', '2', '0' },")[0]
        if vbad or not vgood:
            print("check_core_id selftest: FAILED on the VHDL form (swapped accepted %s, right refused %s)" % (vbad, not vgood))
            return 1
        dd = "ddt_regs #(.ID(32'h%s), .GEOM_LINES(16'd256)) regs ("
        dbad = compare(dd % "44445444", "{ 'D', 'D', 'T', 'M' },")[0]
        dgood = compare(dd % "4D544444", "{ 'D', 'D', 'T', 'M' },")[0]
        if dbad or not dgood:
            print("check_core_id selftest: FAILED on the .ID() form (swapped accepted %s, right refused %s)" % (dbad, not dgood))
            return 1
        by = "12'h000: q <= 8'h%s; 12'h001: q <= 8'h%s; 12'h002: q <= 8'h%s; 12'h003: q <= 8'h%s;"
        bbad = compare(by % ("49", "44", "41", "47"), "{ 'D', 'I', 'A', 'G' },")[0]
        bgood = compare(by % ("44", "49", "41", "47"), "{ 'D', 'I', 'A', 'G' },")[0]
        if bbad or not bgood:
            print("check_core_id selftest: FAILED on the byte form (swapped accepted %s, right refused %s)" % (bbad, not bgood))
            return 1
        print("check_core_id selftest: ok (a swapped id is refused)")
        return 0
    rc = 0
    for gw, app in PAIRS:
        ok, g, a = compare(open(gw).read(), open(app).read())
        print("check_core_id: %s answers %r, %s names %r%s" % (gw, g, app, a, "" if ok else "  MISMATCH"))
        rc |= 0 if ok else 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
