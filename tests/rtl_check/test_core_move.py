#!/usr/bin/env python3
"""T239.3 D55: tools/core_move.py. Happy paths only: the plan of a core, its TOML B from the template, and the driver table of a standard-block-only TOML A that the SDK's base
sources (volume.cc, tone.cc) compile against natively. The dry run over every core (rtl_check, minutes) is `core_move.py dryrun`, not a unit."""
import io
import contextlib
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import core_move  # noqa: E402
import descrec  # noqa: E402


def must(cond, what, out=""):
    if not cond:
        print("FAIL: %s\n%s" % (what, out))
        sys.exit(1)
    print("PASS: " + what)


buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    inf = core_move.plan("atari800")
out = buf.getvalue()
must(inf["kind"] == "dfx" and "atari800.cc" in inf["own"] and "atari800_regs.h" in inf["own"] and "hk_defaults.h" in inf["shared"] and "cores.h" not in inf["own"] + inf["shared"],
     "plan atari800: a dfx core; its sources are the include closure of coreapp/cores/atari800.cc, hk_defaults.h is shared with other cores and stays, cores.h is no core's file", out)
must("move to cores/atari800/app/lib" in out and "SHARED, stays" in out and "../cores/atari800/core_sources.tcl" in out and "atari800 dfx" in out,
     "plan atari800: names the move, the shared file, the RTL source list and the lane", out)

sdk = descrec.toml_loads(core_move.sdk_toml(inf))
must(sdk["app"]["desc"] == "atari800_desc" and sdk["app"]["base_toml_a"] == "host.toml" and "cart_regs" in sdk["app"]["base_blocks"]
     and sdk["library"][0]["name"] == "atari800" and "atari800.cc" in sdk["library"][0]["sources"] and [l["name"] for l in sdk["library"][1:]] == inf["libs"],
     "sdk.toml template of atari800 parses: [app] desc, base blocks, its own library first, then the shared libraries it includes (%s)" % ", ".join(inf["libs"]))

with tempfile.TemporaryDirectory() as td:
    ta = os.path.join(td, "gateware.toml")
    open(ta, "w").write(core_move.MIN_A)
    sd = os.path.join(td, "sdk.toml")
    open(sd, "w").write(core_move.sdk_toml(inf))
    core_move.merge_drv(ta, sd, td)
    h = open(os.path.join(td, "drv", "drvtab.h")).read()
    must("DRV_ident_caps_RD" in h and "DRV_cart_regs_ADDR" in h, "the driver table of the standard block plus the base blocks of the template has ident.caps and cart_regs")
    errs = []
    for f in ("volume.cc", "tone.cc"):
        errs += core_move.syntax(os.path.join(ROOT, "coreapp", "src", f), os.path.join(td, "drv"), td, False)
    must(not errs, "volume.cc and tone.cc compile natively (g++ -fsyntax-only) against that table", "\n".join(errs[:3]))
print("core_move tests: PASS")
