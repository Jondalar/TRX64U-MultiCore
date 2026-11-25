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

# D55 review 3a/3b: exactly one row is removed (a different style or a second match is an error), a dirty tree and an existing target refuse, a failing git mv prints its undo
import subprocess
row = "    &zx_desc,\n"
must(core_move.drop_row("a\n" + row + "b\n", r"^[ \t]*&zx_desc,[ \t]*(?://[^\n]*)?\n", "t") == "a\nb\n", "drop_row: the one matching row is removed")
must(core_move.drop_row("a\n\t&zx_desc,  // the ZX\nb\n", r"^[ \t]*&zx_desc,[ \t]*(?://[^\n]*)?\n", "t") == "a\nb\n", "drop_row: a tab indent and a trailing comment are accepted")
for txt, what in (("a\n", "no row"), ("a\n" + row + row, "two rows"), ("a\n    &zxnext_desc,\n", "another core's row (zx is not a prefix match)")):
    try:
        core_move.drop_row(txt, r"^[ \t]*&zx_desc,[ \t]*(?://[^\n]*)?\n", "t")
        must(False, "drop_row: %s is an error" % what)
    except ValueError:
        must(True, "drop_row: %s is an error" % what)
with tempfile.TemporaryDirectory() as td:
    g = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *a], cwd=td, capture_output=True, text=True)
    g("init", "-q")
    os.makedirs(os.path.join(td, "coreapp", "cores"))
    for f in ("a.cc", "b.cc"):
        open(os.path.join(td, "coreapp", "cores", f), "w").write("x\n")
    for f in ("table.cc", "cores.h"):
        os.makedirs(os.path.join(td, "coreapp", "src"), exist_ok=True)
    open(os.path.join(td, "coreapp", "src", "table.cc"), "w").write("x\n")
    open(os.path.join(td, "coreapp", "cores", "cores.h"), "w").write("x\n")
    g("add", "-A")
    g("commit", "-q", "-m", "x")
    saved = core_move.ROOT
    core_move.ROOT = td
    try:
        inf2 = {"own": ["a.cc", "b.cc"]}
        must(core_move.preflight("zz", inf2) == [], "preflight: a clean tree and no target is accepted")
        open(os.path.join(td, "coreapp", "cores", "b.cc"), "w").write("changed\n")
        bad = core_move.preflight("zz", inf2)
        must(len(bad) == 1 and "uncommitted changes" in bad[0] and "b.cc" in bad[0], "preflight: a modified source refuses", str(bad))
        g("checkout", "--", "coreapp/cores/b.cc")
        os.makedirs(os.path.join(td, "cores", "zz", "app", "lib"))
        open(os.path.join(td, "cores", "zz", "app", "lib", "a.cc"), "w").write("y\n")
        bad = core_move.preflight("zz", inf2)
        must(any("target exists" in b for b in bad), "preflight: an existing target refuses", str(bad))
        os.remove(os.path.join(td, "cores", "zz", "app", "lib", "a.cc"))
        g("rm", "-q", "--cached", "coreapp/cores/b.cc")          # b.cc is untracked: its git mv fails after a.cc was moved
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            ok = core_move.move_files("zz", ["a.cc", "b.cc"])
        must(not ok and "git mv cores/zz/app/lib/a.cc coreapp/cores/a.cc" in buf.getvalue(), "move_files: a failing git mv prints the command that undoes the first move", buf.getvalue())
    finally:
        core_move.ROOT = saved
print("core_move tests: PASS")
