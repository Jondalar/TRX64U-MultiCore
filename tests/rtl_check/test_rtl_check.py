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
  2c. the miniature design tests/rtl_check/mini3 (V-1d): a handshake into a module with a register pipeline (c_addr, op_a), another master loading the same register, a Verilog
      generate-for with a named block, a function, and a module behind continuous assignments of the pipeline (m3dev), checked with --own-core, and without it (the core blocks are then a
      hand-over the miniature has no port for).
  2c2. the miniature design tests/rtl_check/mini4 (V-1d): a pattern the check does not know stops the one block that reaches it; the other block has its verdict.
  2d. c64_basic of TRX64-Ultimate with the TOML A of its commit b57405a6 (the TOML A is read with git show, the RTL is the checkout's working tree, so the list below follows the checkout;
      skipped without the checkout or the commit), --own-core: the blocks that are still stopped are the five the check lists (trx_dbg_run, trx_statebus.status, trx_debug.ipc_set, gcr_coder).
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

# the VIC-20's TOML A: the core window (the five cw_* ports of the instance vc) and its own drive block (drive_l2 behind the register stage of layer2_vc20, the ports of the top module as the bus,
# the whole address on the port; the 2 KB parameter RAM as two runs of 256 words with the stride 8, the dirty RAM as a run of 128); no WD177x, no side, no drivetype
rc, out = run("check", "--design", os.path.join(HERE, "mini3"), "--toml-a", os.path.join(HERE, "mini3.toml"), "--top", "top", "--bus", "msys.io_req", "--own-core")
must(rc == 0 and "3 block(s), 5 properties agree" in out,
     "mini3 design: a handshake into a module with a register pipeline (c_addr, then op_a; the registers JTAG loads are not the request), a generate-for cell[i].v with a named block, a function (swz) "
     "and a flag state, and a register of m3dev that the pipeline's continuous assignments (d_we = ost == O_RUN && op_we && page, d_addr, d_wdata) write: the blocks agree with the RTL "
     "(with --own-core: the properties of owner core blocks are read against the RTL)", out)
rc, out = run("check", "--design", os.path.join(HERE, "mini3"), "--toml-a", os.path.join(HERE, "mini3.toml"), "--top", "top", "--bus", "msys.io_req")
must(rc == 1 and "block r0: byte +0x0 (0x10000120) is not handed over to the core by the RTL" in out and "3 finding(s)" in out,
     "mini3 design without --own-core: a core block is only a window that leaves the base for the core; the miniature has no core port, so each of the three blocks says so", out)

rc, out = run("check", "--design", os.path.join(HERE, "mini4"), "--toml-a", os.path.join(HERE, "mini4.toml"), "--top", "top", "--bus", "msys.io_req")
must(rc == 1 and "block dev: an RTL pattern I do not know" in out and "a generate for whose step is not genvar = genvar + 1" in out and "block ctl" not in out and "1 finding(s)" in out,
     "mini4 design: the generate-for that is no counting loop stops the block dev (listed with file and line), the block ctl has its verdict (it agrees)", out)

ult_commit = "b57405a6"
tomla = subprocess.run(["git", "-C", ult, "show", ult_commit + ":software/trx64u/gateware/c64_basic.toml"], capture_output=True, text=True) if os.path.isdir(dsg) else None
if tomla is not None and tomla.returncode == 0:
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        fa = os.path.join(td, "c64_basic.toml")
        open(fa, "w").write(tomla.stdout)
        rc, out = run("check", "--design", dsg, "--toml-a", fa, "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"), "--own-core")
    found = sorted({l.split()[2].rstrip(",:") for l in out.splitlines() if l.startswith("rtl_check: block ")})
    must(rc == 1 and found == ["gcr_coder", "trx_dbg_run", "trx_debug", "trx_statebus"] and "6 finding(s)" in out,
         "c64_basic with its TOML A of %s (33 blocks): the blocks of trx_sys.v that it decodes itself (trx_gate, trx_turbo, trx_slot, the generate-for ent[i], the function tfind, the pipeline rq_addr, c_addr, op_a; "
         "another master loads op_a), the trx_io, pix_layer and c64_text blocks and the VHDL blocks agree; the pages 2 and 3 that trx_sys.v hands on over continuous assignments (trx_engine, trx_debug) agree; what is still stopped is listed: "
         "byte 3 of trx_dbg_run.owner, cyc_cmp, cmp_en, trx_statebus.status and trx_debug.ipc_set, and gcr_coder (a VHDL for-generate with ascending ranges)" % ult_commit, out)
else:
    print("SKIP: no TRX64-Ultimate checkout with commit %s at %s; its TOML A not checked" % (ult_commit, ult))

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/vc20/core_sources.tcl", "--list", "gideon_vhdl.tcl", "--top", "layer2_vc20",
              "--bus", "vc20_core:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--bus", "layer2_vc20:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata", "--window", "0x10020000:0x4000:full",
              "--toml-a", os.path.join(ROOT, "cores", "vc20", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "7 block(s), 32 properties agree" in out, "vc20: 7 blocks, 32 properties of gateware.toml agree with the RTL of the partial (the core window, and the drive_a block behind drive_l2: no WD177x, no side, no drivetype)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "build.tcl", "--list", "gideon_vhdl.tcl",
              "--toml-a", os.path.join(ROOT, "coreapp", "gateware", "host.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "64 block(s), 188 properties agree" in out and "clean room" not in out,
     "host: 64 blocks of coreapp/gateware/host.toml agree with the RTL of host/ (base blocks by property, core blocks by hand-over; the overlay, the palette, "
     "IEC, SD and WiFi windows, the overlay palette as three runs of 16 elements with the stride 4, behind io_bus_splitter the ITU with its console UART, USB and Ethernet, and the flash window whose flash_gate holds each request one clock (a held request); no Gideon file of the deny list is in the source list, the Minimig agnus_copper.v is Verilog and is no Gideon file)", out)

rc, out = run("lint-src", os.path.join(HERE, "lint", "clean.cc"))
must(rc == 0 and "no literal I/O address" in out, "lint-src: a source without a literal address passes", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"))
must(rc == 1 and "hit.cc:1: 0x10100500 lies in the I/O space" in out and "hit.cc:2: 268435456u lies in the I/O space" in out
     and "hit.cc:2: 0x101A4000UL lies in the I/O space" in out and "3 literal address(es)" in out, "lint-src: the three literals of hit.cc are listed", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"), "--allow", "hit.cc:0x10100500", "--allow", "other.cc:268435456u")
must(rc == 1 and "hit.cc:1:" not in out and "hit.cc:2: 268435456u" in out and "2 literal address(es)" in out,
     "lint-src: --allow passes one literal of one file and nothing else", out)
print("rtl_check tests: PASS")
