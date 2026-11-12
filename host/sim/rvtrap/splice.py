#!/usr/bin/env python3
"""Sim copies of core.vhd and csr.vhd with the monitor (monitor.inc) in the core.

usage: splice.py <rvlite vhdl_source dir> <out dir>
csr gets a debug output of mstatus; core gets the monitor's declarations before its
`begin`, the monitor process before `end architecture`, and wires mstatus to it.
Every substitution must hit exactly once, or the script stops (a vendored change that moves a
line must not silently turn the monitor off)."""
import sys, os

src, out = sys.argv[1:3]
here = os.path.dirname(os.path.abspath(__file__))
inc = open(os.path.join(here, "monitor.inc")).read()
decl = inc.split("--@BODY")[0].replace("--@DECL\n", "")
body = inc.split("--@BODY")[1]


def sub(text, old, new, what):
    if text.count(old) != 1:
        sys.exit("splice: %s: %d matches of %r" % (what, text.count(old), old))
    return text.replace(old, new)


csr = open(os.path.join(src, "csr.vhd")).read()
csr = sub(csr, "    int_i       : in  std_logic;\n", "    dbg_mstatus : out std_logic_vector(31 downto 0);\n    int_i       : in  std_logic;\n", "csr port")
csr = sub(csr, "    -- report pending interrupt in mip\n", "    dbg_mstatus <= mstatus;\n    -- report pending interrupt in mip\n", "csr assign")
open(os.path.join(out, "csr_mon.vhd"), "w").write(csr)

core = open(os.path.join(src, "core.vhd")).read()
core = sub(core, "architecture structural of core is\n", "architecture structural of core is\n    use ieee.numeric_std.all;\n    use std.textio.all;\n", "core arch")
core = sub(core, "    signal reg_write_d  : std_logic;\nbegin\n", "    signal reg_write_d  : std_logic;\n" + decl + "begin\n", "core begin")
core = sub(core, "        int_i => irq,\n", "        int_i => irq,\n        dbg_mstatus => mon_mstatus,\n", "core csr map")
core = sub(core, "end architecture;", body + "\nend architecture;", "core end")
open(os.path.join(out, "core_mon.vhd"), "w").write(core)
