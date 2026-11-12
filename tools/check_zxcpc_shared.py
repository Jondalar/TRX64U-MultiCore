#!/usr/bin/env python3
"""T91 S1 / T92 S1: a module the Spectrum and CPC cores share exists once in the tree. The shared parts are the Z80 (cores/zx/rtl/T80 and its
Verilog cores/zx/gen/t80pa.v), the AY (cores/zx/rtl/ym2149.sv) and cores/zx/shared/*.v. No other file of cores/ may define a module or entity
of the same name (the CPC core reads these files through its core.toml, it does not carry copies). Prints the offenders; exit 1 if any."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V_DEF = re.compile(r"^\s*module\s+(\w+)", re.M)
VHD_DEF = re.compile(r"^\s*entity\s+(\w+)\s+is", re.M | re.I)


def defs(path):
    text = open(path, errors="replace").read()
    if path.endswith((".vhd", ".vhdl")):
        return {m.group(1).lower() for m in VHD_DEF.finditer(text)}
    return {m.group(1).lower() for m in V_DEF.finditer(text)}


def main(root=ROOT):
    os.chdir(root)
    SHARED = (glob.glob("cores/zx/shared/*.v") + glob.glob("cores/zx/rtl/T80/*.vhd") + ["cores/zx/rtl/ym2149.sv", "cores/zx/gen/t80pa.v"])
    SHARED = [f for f in SHARED if os.path.exists(f)]
    names = {}
    for f in SHARED:
        for n in defs(f):
            names.setdefault(n, f)
    bad = 0
    for f in sorted(glob.glob("cores/*/**/*", recursive=True)):
        if f in SHARED or not f.endswith((".v", ".sv", ".vhd", ".vhdl")):
            continue
        if f.startswith("cores/zx/gen/") and f != "cores/zx/gen/t80pa.v":
            continue
        if f.startswith(("cores/zxnext/rtl/", "cores/zxnext/ip/", "cores/zxnext/gen/")):
            continue      # the Next brings its own Z80N and AY (VHDL of another origin, T116): not a copy of the shared ones
        if f.startswith("cores/c128/z80/"):
            continue      # the C128 carries T80 version 360 (BUSRQ_n timing, T113), not the shared copy; never built beside the ZX/CPC
        if f.startswith("cores/neogeo/rtl/cpu/T80/"):
            continue      # the Neo Geo carries the T80 of the MiSTer core unchanged (T143): its Wait_n handling (Really_Wait, NoRead/Write) differs from the shared copy, which has the Zilog flag fixes instead; never built beside the ZX/CPC
        for n in defs(f):
            if n in names and n not in ("t80_reg",) and not (f.startswith("cores/zx/gen/t80pa.v")):
                print("%s defines %s, which %s defines (a shared module exists once)" % (f, n, names[n]))
                bad = 1
    if not bad:
        print("check_zxcpc_shared: %d shared module names, no second definition" % len(names))
    return bad


def selftest():
    import tempfile
    d = tempfile.mkdtemp()
    for rel, text in (("cores/zx/shared/a.v", "module zxcpc_foo(); endmodule\n"), ("cores/cpc/trx/ok.v", "module cpc_bar(); endmodule\n")):
        os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
        open(os.path.join(d, rel), "w").write(text)
    if main(d) != 0:
        print("selftest: a tree without a copy was reported")
        return 1
    os.makedirs(os.path.join(d, "cores/zxnext/rtl"), exist_ok=True)
    open(os.path.join(d, "cores/zxnext/rtl/own.vhd"), "w").write("entity zxcpc_foo is\nend entity;\n")
    if main(d) != 0:
        print("selftest: the Next's own tree was reported")
        return 1
    open(os.path.join(d, "cores/cpc/trx/copy.v"), "w").write("module ZXCPC_foo(); endmodule\n")
    if main(d) != 1:
        print("selftest: a copy of a shared module was not found")
        return 1
    print("selftest: ok (a second definition is found, a tree without one passes)")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if sys.argv[1:] == ["selftest"] else main())
