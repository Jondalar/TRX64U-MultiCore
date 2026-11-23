#!/usr/bin/env python3
"""T239.3 P1-3: tools/rtl_check.py. Happy paths only (D30: no negative control).

  1. check on the miniature design of tests/rtl_check/mini (a VHDL module that decodes a request, a Verilog module behind a strobe):
     every block and property of mini.toml agrees with the RTL.
  2. check on c64_basic of TRX64-Ultimate with the fixture tests/rtl_check/c64_basic.toml and the shipped maps of app/sdkpkg/maps,
     when the directory is there
     (environment TRX64_ULTIMATE, or ~/Development/TRX64-Ultimate); it is read, never written.
  3. check on host/ with coreapp/gateware/host.toml (T239.3 V-1): the host's own decode, no checkout of another repository needed.
  4. lint-src: a clean source passes, the literals of hit.cc are listed with file and line.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "rtl_check.py")


def run(*args):
    p = subprocess.run([sys.executable, "-I", TOOL, *args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def must(cond, what, out=""):
    if not cond:
        print("FAIL: %s\n%s" % (what, out))
        sys.exit(1)
    print("PASS: " + what)


rc, out = run("check", "--design", os.path.join(HERE, "mini"), "--toml-a", os.path.join(HERE, "mini.toml"), "--top", "top", "--bus", "msys.io_req")
must(rc == 0 and "2 block(s), 4 properties agree" in out, "mini design: 2 blocks, 4 properties agree with the RTL", out)

ult = os.environ.get("TRX64_ULTIMATE", os.path.expanduser("~/Development/TRX64-Ultimate"))
dsg = os.path.join(ult, "designs", "c64_basic")
if os.path.isdir(dsg):
    rc, out = run("check", "--design", dsg, "--toml-a", os.path.join(HERE, "c64_basic.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
    must(rc == 0 and "10 block(s), 56 properties agree" in out,
         "c64_basic: 10 blocks, 56 properties agree with the RTL (the maps c64cart and c64cart_regs of app/sdkpkg/maps, dma behind its handshake)", out)
else:
    print("SKIP: no TRX64-Ultimate checkout at %s (set TRX64_ULTIMATE); c64_basic not checked" % ult)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "build.tcl", "--list", "gideon_vhdl.tcl",
              "--toml-a", os.path.join(ROOT, "coreapp", "gateware", "host.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "45 block(s), 91 properties agree" in out,
     "host: 45 blocks of coreapp/gateware/host.toml agree with the RTL of host/ (base blocks by property, core blocks by hand-over)", out)

rc, out = run("lint-src", os.path.join(HERE, "lint", "clean.cc"))
must(rc == 0 and "no literal I/O address" in out, "lint-src: a source without a literal address passes", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"))
must(rc == 1 and "hit.cc:1: 0x10100500 lies in the I/O space" in out and "hit.cc:2: 268435456u lies in the I/O space" in out
     and "hit.cc:2: 0x101A4000UL lies in the I/O space" in out and "3 literal address(es)" in out, "lint-src: the three literals of hit.cc are listed", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"), "--allow", "hit.cc:0x10100500", "--allow", "other.cc:268435456u")
must(rc == 1 and "hit.cc:1:" not in out and "hit.cc:2: 268435456u" in out and "2 literal address(es)" in out,
     "lint-src: --allow passes one literal of one file and nothing else", out)
print("rtl_check tests: PASS")
