#!/usr/bin/env python3
"""T239.3 P1-3: tools/rtl_check.py. Happy paths only (D30: no negative control).

  1. check on the miniature design of tests/rtl_check/mini (a VHDL module that decodes a request, a Verilog module behind a strobe):
     every block and property of mini.toml agrees with the RTL.
  2. check on c64_basic of TRX64-Ultimate with the fixture tests/rtl_check/c64_basic.toml and the shipped maps of app/sdkpkg/maps,
     when the directory is there
     (environment TRX64_ULTIMATE, or ~/Development/TRX64-Ultimate); it is read, never written.
  2b. the miniature design tests/rtl_check/mini2 (V-1c): a request splitter, a process alias and procedure, a VHDL block, a RAM write sink.
  3. the VIC-20 partial (T239.3 V-2): cores/vc20/app/gateware.toml against the files of cores/vc20/core_sources.tcl, the request being the five ports of
     the core window (cw_*) of the instance vc of layer2_vc20; it needs this repository only.
  3. check on host/ with coreapp/gateware/host.toml (T239.3 V-1, V-1b): the host's own decode, no checkout of another repository needed.
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
must(rc == 0 and "5 block(s), 8 properties agree" in out, "mini design: 5 blocks, 8 properties agree with the RTL (a concatenation target, a Verilog for loop with <, a generate branch the instance parameter opens)", out)
must("ultisid_stub.vhd" in out and "never opened" in out,
     "mini design: ultisid_stub.vhd (not valid VHDL) is on the deny list by its name, listed and never opened", out)

ult = os.environ.get("TRX64_ULTIMATE", os.path.expanduser("~/Development/TRX64-Ultimate"))
dsg = os.path.join(ult, "designs", "c64_basic")
if os.path.isdir(dsg):
    rc, out = run("check", "--design", dsg, "--toml-a", os.path.join(HERE, "c64_basic.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
    must(rc == 0 and "12 block(s), 64 properties agree" in out,
         "c64_basic: 12 blocks, 64 properties agree with the RTL (the maps sid, c64cart, c64cart_regs, dma and uci of app/sdkpkg/maps, dma a run of 65536 bytes behind its handshake; "
         "uci behind io_bus_bridge2 with its synchronizers and command_protocol (a process procedure, an alias); the 1541 drive behind c1541_drive's io_bus_splitter, with a VHDL block "
         "statement, a clocked concurrent assignment and Gideon's RAM entities)", out)
else:
    print("SKIP: no TRX64-Ultimate checkout at %s (set TRX64_ULTIMATE); c64_basic not checked" % ult)

rc, out = run("check", "--design", os.path.join(HERE, "mini2"), "--toml-a", os.path.join(HERE, "mini2.toml"), "--top", "top", "--bus", "m2sys.io_req")
must(rc == 0 and "2 block(s), 3 properties agree" in out,
     "mini2 design: a request splitter (io_bus_splitter, modelled by the check) with a register port (process alias, procedure inlined at its call) and a RAM port (a VHDL block, "
     "a clocked concurrent assignment, a RAM entity as a write sink) agree with the RTL", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/vc20/core_sources.tcl", "--top", "layer2_vc20",
              "--bus", "vc20_core:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "vc20", "app", "gateware.toml"))
must(rc == 0 and "6 block(s), 20 properties agree" in out, "vc20: 6 blocks, 20 properties of gateware.toml agree with the RTL of the partial", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "build.tcl", "--list", "gideon_vhdl.tcl",
              "--toml-a", os.path.join(ROOT, "coreapp", "gateware", "host.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "63 block(s), 221 properties agree" in out and "clean room" not in out,
     "host: 63 blocks of coreapp/gateware/host.toml agree with the RTL of host/ (base blocks by property, core blocks by hand-over; the overlay, the palette, "
     "IEC, SD and WiFi windows, the 16 overlay palette entries, and behind io_bus_splitter the ITU with its console UART, USB and Ethernet; no Gideon file of the deny list is in the source list, the Minimig agnus_copper.v is Verilog and is no Gideon file)", out)

rc, out = run("lint-src", os.path.join(HERE, "lint", "clean.cc"))
must(rc == 0 and "no literal I/O address" in out, "lint-src: a source without a literal address passes", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"))
must(rc == 1 and "hit.cc:1: 0x10100500 lies in the I/O space" in out and "hit.cc:2: 268435456u lies in the I/O space" in out
     and "hit.cc:2: 0x101A4000UL lies in the I/O space" in out and "3 literal address(es)" in out, "lint-src: the three literals of hit.cc are listed", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"), "--allow", "hit.cc:0x10100500", "--allow", "other.cc:268435456u")
must(rc == 1 and "hit.cc:1:" not in out and "hit.cc:2: 268435456u" in out and "2 literal address(es)" in out,
     "lint-src: --allow passes one literal of one file and nothing else", out)
print("rtl_check tests: PASS")
