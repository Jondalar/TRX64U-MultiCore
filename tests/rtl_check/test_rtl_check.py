#!/usr/bin/env python3
"""T239.3 P1-3: tools/rtl_check.py. Happy paths only (D30: no negative control).

  1. check on the miniature design of tests/rtl_check/mini (a VHDL module that decodes a request, a Verilog module behind a strobe):
     every block and property of mini.toml agrees with the RTL.
  2. check on c64_basic of TRX64-Ultimate with the fixture tests/rtl_check/c64_basic.toml and the shipped maps of app/sdkpkg/maps,
     when the directory is there
     (environment TRX64_ULTIMATE, or ~/Development/TRX64-Ultimate); it is read, never written.
  2b. the miniature design tests/rtl_check/mini2 (V-1c): a request splitter, a process alias and procedure, a VHDL block, a RAM write sink.
  2e. the miniature design tests/rtl_check/mini5 (V-1e): ascending vectors, a for-generate, an if-generate, a stride, a core block the design decodes itself.
  3a. the Atari 800 XL (T239.5 D15): cores/atari800/app/gateware.toml against the files of cores/atari800/core_sources.tcl, the request being the five cw_* ports of layer2_atari800.
  3b. the NES (T239.5 D15): cores/nes/app/gateware.toml against the files of cores/nes/app/rtl_check_sources.tcl, the request being the five cw_* ports of layer2_nes.
  3c. the Commodore 264 (T239.5 D15): cores/c264/app/gateware.toml against the files of cores/c264/core_sources.tcl, the requests being the five cw_* ports of layer2_c264 and, for its own drive_a, its drv_io_* ports.
  3. the VIC-20 partial (T239.3 V-2): cores/vc20/app/gateware.toml against the files of cores/vc20/core_sources.tcl, the request being the five ports of
     the core window (cw_*) of the instance vc of layer2_vc20; it needs this repository only.
  2c. the miniature design tests/rtl_check/mini3 (V-1d): a handshake into a module with a register pipeline (c_addr, op_a), another master loading the same register, a Verilog
      generate-for with a named block, a function, and a module behind continuous assignments of the pipeline (m3dev), checked by default (D43), and with the two options of 0.4.5, which change nothing.
  2c2. the miniature design tests/rtl_check/mini4 (V-1d): a pattern the check does not know stops the one block that reaches it; the other block has its verdict.
  2d. c64_basic of TRX64-Ultimate with the TOML A of its commit 975aa59b (the TOML A is read with git show, the RTL is the checkout's working tree, so the result follows the checkout;
      skipped without the checkout or the commit): all 39 blocks (168 properties) agree with the RTL (V-1f: trx_dbg_run.owner byte 3, cyc_cmp, cmp_en, trx_statebus.status and trx_debug.ipc_set included).
  2f. the miniature design tests/rtl_check/mini6 (V-1f): a unit two modules behind the handshake (an instance inside the module that the continuous assignments feed), a genvar select, a write that
      clears a register whatever the data is, a register written under an address compare while another expression names the data behind a different compare.
  3. check on host/ with coreapp/gateware/host.toml (T239.3 V-1, V-1b): the host's own decode, no checkout of another repository needed.
  5. emit (D55, tools/toml_a_emit.py): the TOML A of the VIC-20's core window written from the RTL decode (the first 0x200 bytes), named from a C header by offset, checked by `check` against the
     same RTL (by construction) and compared with cores/vc20/app/gateware.toml; the header reader skips the mask of a register.
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
    must(rc == 0 and "15 block(s), 73 properties agree" in out,
         "c64_basic: 15 blocks, 73 properties agree with the RTL (the maps sid, c64cart, c64cart_regs (with stop_mode and clock_detect), dma, uci and gcr_coder of app/sdkpkg/maps; the GCR coder "
         "behind a VHDL for-generate over slices of vectors declared ascending; two blocks with owner core that ultimate_sys decodes itself, checked by their properties (D43: no option);  dma a run of 65536 bytes behind its handshake; "
         "uci behind io_bus_bridge2 with its synchronizers and command_protocol (a process procedure, an alias); the 1541 drive behind c1541_drive's io_bus_splitter, with a VHDL block "
         "statement, a clocked concurrent assignment and Gideon's RAM entities)", out)
else:
    print("SKIP: no TRX64-Ultimate checkout at %s (set TRX64_ULTIMATE); c64_basic not checked" % ult)

rc, out = run("check", "--design", os.path.join(HERE, "mini5"), "--toml-a", os.path.join(HERE, "mini5.toml"), "--top", "top", "--bus", "m3sys.io_req")
must(rc == 0 and "5 block(s), 6 properties agree" in out,
     "mini5 design: vectors declared ascending (a shift by concatenation, slices by `to`, one declared (1 to 8)), a for-generate of instances over slices of them, an if-generate that opens a process, an attribute, "
     "a whole record assigned as a value, a run with a stride (bytes four apart in a RAM) and a block with owner core that the design decodes itself (D43) agree with the RTL", out)

rc, out = run("check", "--design", os.path.join(HERE, "mini2"), "--toml-a", os.path.join(HERE, "mini2.toml"), "--top", "top", "--bus", "m2sys.io_req")
must(rc == 0 and "2 block(s), 3 properties agree" in out,
     "mini2 design: a request splitter (io_bus_splitter, modelled by the check) with a register port (process alias, procedure inlined at its call) and a RAM port (a VHDL block, "
     "a clocked concurrent assignment, a RAM entity as a write sink) agree with the RTL", out)

rc, out = run("check", "--design", os.path.join(HERE, "mini2"), "--toml-a", os.path.join(HERE, "mini2_gap.toml"), "--top", "top", "--bus", "m2sys.io_req")
must(rc == 0 and "2 block(s), 3 properties agree" in out,
     "mini2 design, block regs of 4 bytes with a gap at +2, +3 (D40): only the bytes of the properties must be decoded; the gap is no finding", out)

# the VIC-20's TOML A: the core window (the five cw_* ports of the instance vc) and its own drive block (drive_l2 behind the register stage of layer2_vc20, the ports of the top module as the bus,
# the whole address on the port; the 2 KB parameter RAM as two runs of 256 words with the stride 8, the dirty RAM as a run of 128); no WD177x, no side, no drivetype
rc, out = run("check", "--design", os.path.join(HERE, "mini3"), "--toml-a", os.path.join(HERE, "mini3.toml"), "--top", "top", "--bus", "msys.io_req")
must(rc == 0 and "3 block(s), 5 properties agree" in out,
     "mini3 design: a handshake into a module with a register pipeline (c_addr, then op_a; the registers JTAG loads are not the request), a generate-for cell[i].v with a named block, a function (swz) "
     "and a flag state, and a register of m3dev that the pipeline's continuous assignments (d_we = ost == O_RUN && op_we && page, d_addr, d_wdata) write: the blocks agree with the RTL "
     "(D43: the properties of owner core blocks are read against the RTL, the design has no core port)", out)
rc, out = run("check", "--design", os.path.join(HERE, "mini3"), "--toml-a", os.path.join(HERE, "mini3.toml"), "--top", "top", "--bus", "msys.io_req", "--own-core", "--core-in-design")
must(rc == 0 and "3 block(s), 5 properties agree" in out, "mini3 design with --own-core and --core-in-design: the options of 0.4.5 are accepted and change nothing (D43)", out)

rc, out = run("check", "--design", os.path.join(HERE, "mini4"), "--toml-a", os.path.join(HERE, "mini4.toml"), "--top", "top", "--bus", "msys.io_req")
must(rc == 1 and "block dev: an RTL pattern I do not know" in out and "a generate for whose step is not genvar = genvar + 1" in out and "block ctl" not in out and "1 finding(s)" in out,
     "mini4 design: the generate-for that is no counting loop stops the block dev (listed with file and line), the block ctl has its verdict (it agrees)", out)

rc, out = run("check", "--design", os.path.join(HERE, "mini6"), "--toml-a", os.path.join(HERE, "mini6.toml"), "--top", "top", "--bus", "msys.io_req")
must(rc == 0 and "3 block(s), 4 properties agree" in out,
     "mini6 design: a unit that the module behind the handshake's continuous assignments holds (an instance followed behind a handshake), two cells of a generate-for that compare the address with "
     "a select of the genvar, a register written from the data under an address compare (the data of another compare in another register's expression is no write), and a register that a write clears "
     "whatever the data is (a case arm of the pipeline's module): the blocks agree with the RTL", out)

ult_commit = "975aa59b"
tomla = subprocess.run(["git", "-C", ult, "show", ult_commit + ":software/trx64u/gateware/c64_basic.toml"], capture_output=True, text=True) if os.path.isdir(dsg) else None
if tomla is not None and tomla.returncode == 0:
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        fa = os.path.join(td, "c64_basic.toml")
        open(fa, "w").write(tomla.stdout)
        rc, out = run("check", "--design", dsg, "--toml-a", fa, "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
    must(rc == 0 and "39 block(s), 168 properties agree" in out,
         "c64_basic with its TOML A of %s (U18 after the owner split: 39 blocks, 168 properties; D40: the gaps of the block icap are no finding): the blocks of trx_sys.v that it decodes itself (trx_gate, trx_turbo, trx_slot, the generate-for ent[i], the function tfind, the pipeline rq_addr, c_addr, op_a; "
         "another master loads op_a), the trx_io, pix_layer and c64_text blocks and the VHDL blocks agree; the pages 2 and 3 that trx_sys.v hands on over continuous assignments (trx_engine, trx_debug, and trx_dbg inside c64_core) agree, "
         "with byte 3 of trx_dbg_run.owner, cyc_cmp, cmp_en and trx_debug.ipc_set (a register written from the data under an address compare, behind a genvar select of the watch units) and trx_statebus.status "
         "(sb_err cleared by a write, whatever the data is); gcr_coder agrees (a VHDL for-generate over vectors declared ascending)" % ult_commit, out)
else:
    print("SKIP: no TRX64-Ultimate checkout with commit %s at %s; its TOML A not checked" % (ult_commit, ult))

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/vc20/core_sources.tcl", "--list", "gideon_vhdl.tcl", "--top", "layer2_vc20",
              "--bus", "vc20_core:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--bus", "layer2_vc20:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata", "--window", "0x10020000:0x4000:full",
              "--toml-a", os.path.join(ROOT, "cores", "vc20", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "7 block(s), 32 properties agree" in out, "vc20: 7 blocks, 32 properties of gateware.toml agree with the RTL of the partial (the core window, and the drive_a block behind drive_l2: no WD177x, no side, no drivetype)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/amiga1200/app/rtl_check_sources.tcl",
              "--top", "layer2_amiga",
              "--bus", "layer2_amiga:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "amiga1200", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "17 block(s), 54 properties agree" in out, "amiga1200: 17 blocks, 54 properties of gateware.toml agree with the RTL of the core window (the A1200's own amiga_regs.v: with the display window and the crop window, which the Amiga 500's copy lacks; page 6 is the hand-over to the host's pixel layer)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/atari800/core_sources.tcl",
              "--top", "layer2_atari800",
              "--bus", "layer2_atari800:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "atari800", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "8 block(s), 33 properties agree" in out, "atari800: 8 blocks, 33 properties of gateware.toml agree with the RTL of the core window (a8_regs.vhd: the IRQ pair, the DDR2 calibration, the machine, the frame buffers, the SIO handler's port, the four paddles as a run, the cache counters; page 6 is the hand-over to the host's pixel layer)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/nes/app/rtl_check_sources.tcl",
              "--top", "layer2_nes",
              "--bus", "layer2_nes:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "nes", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "9 block(s), 27 properties agree" in out, "nes: 9 blocks, 27 properties of gateware.toml agree with the RTL of the core window (nes_regs.v: the IRQ pair, the DDR2 calibration, the machine, the cache counters, the frame buffers, the cartridge base and window page, the 16 bytes of the iNES header as a run, the two pads; page 6 is the hand-over to the host's pixel layer)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/c264/core_sources.tcl", "--list", "gideon_vhdl.tcl",
              "--top", "layer2_c264",
              "--bus", "layer2_c264:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--bus", "layer2_c264:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata", "--window", "0x10020000:0x4000:full",
              "--toml-a", os.path.join(ROOT, "cores", "c264", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "7 block(s), 35 properties agree" in out, "c264: 7 blocks, 35 properties of gateware.toml agree with the RTL of the core window (c264_regs.v: the IRQ pair, the DDR2 calibration, the machine, the ROM aperture's selector with the cartridge and tape pins, the frame buffers; page 6 is the hand-over to the host's pixel layer) and of its own drive_a block behind drive_l2 (no WD177x, no side, no drivetype)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "build.tcl", "--list", "gideon_vhdl.tcl",
              "--toml-a", os.path.join(ROOT, "coreapp", "gateware", "host.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "64 block(s), 190 properties agree" in out and "clean room" not in out,
     "host: 64 blocks of coreapp/gateware/host.toml agree with the RTL of host/ (base blocks by property, core blocks by hand-over; the overlay, the palette, "
     "IEC, SD and WiFi windows, the overlay palette as three runs of 16 elements with the stride 4, behind io_bus_splitter the ITU with its console UART, USB and Ethernet, and the flash window whose flash_gate holds each request one clock (a held request); no Gideon file of the deny list is in the source list, the Minimig agnus_copper.v is Verilog and is no Gideon file)", out)

# D55: emit. The header reader: names by offset, a bit mask after a register with a larger offset is no register, a function-like macro is evaluated.
import tempfile
import io
import contextlib
sys.path.insert(0, os.path.join(ROOT, "tools"))
import toml_a_emit
with tempfile.TemporaryDirectory() as td:
    hp = os.path.join(td, "x_regs.h")
    open(hp, "w").write("#define X_PAGE 0x100\n#define X_REG(o) (X_PAGE + (o))\n#define X_CTRL 0x0A0 // b0 hold\n#define X_HOLD 0x01\n#define X_FLAGS X_REG(0x24)\n#define X_FLAGS_SWAP 0x40\n#define X_BIG 0x20000\n")
    got = toml_a_emit.header_offsets([hp], 0x10000)
    must(got == {0xA0: "X_CTRL", 0x124: "X_FLAGS", 0x100: "X_PAGE"}, "emit: the header reader names X_CTRL (0xA0) and X_FLAGS (a macro: 0x124) and leaves out the bit X_HOLD, the field X_FLAGS_SWAP and the value outside the window", str(got))
    out_toml = os.path.join(td, "vc20_core.toml")
    rc, out = run("emit", "--design", os.path.join(ROOT, "host"), "--list", "../cores/vc20/core_sources.tcl", "--list", "gideon_vhdl.tcl", "--top", "layer2_vc20",
                  "--bus", "vc20_core:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000", "--core", "vc20",
                  "--scan", "0:0x200", "-j", "1", "--toml-a-out", out_toml, "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"),
                  "--compare", os.path.join(ROOT, "cores", "vc20", "app", "gateware.toml"))
    text = open(out_toml).read() if os.path.exists(out_toml) else ""
    must(rc == 0 and "emit: " in out and "5 block(s), 17 properties agree with the RTL" in out and '[identity]' in text and 'name = "ident"' in text and 'name = "caps"' in text,
         "emit: the VIC-20's core window (bytes 0..0x1FF) is written from the decode as 5 blocks, 17 properties with the standard block (id, caps, caps2, build), and `check` agrees with the same RTL", out)
    must("identical (address, width, count, dir): 15" in out and "bytes the RTL decodes that the hand-written file does not describe: 0 bytes" in out,
         "emit --compare: 15 of the hand-written properties of cores/vc20/app/gateware.toml in that range are the same register (address, width, dir); the RTL has no byte the hand-written file leaves out", out)

# D55 review 1a/4a: a stray `endif / `else is Unsupported (not an IndexError); a rejected emit leaves no file
sys.path.insert(0, os.path.join(ROOT, "tools"))
import rtl_check as R
for f in (lambda: R.pp_ifdef("a\n`endif\n"), lambda: R.pp_ifdef("`else\n"), lambda: R.lex("`endif\nwire a;", "verilog", "x.v"), lambda: R.lex("`else\n", "verilog", "x.v")):
    try:
        f()
        must(False, "a stray `endif / `else raises Unsupported")
    except R.Unsupported:
        pass
must(True, "a stray `endif / `else raises Unsupported in pp_ifdef and lex")
with tempfile.TemporaryDirectory() as td:
    out_toml = os.path.join(td, "rej.toml")
    real = toml_a_emit._verify
    toml_a_emit._verify = lambda *a: 1
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = toml_a_emit.cmd_emit(["--design", os.path.join(ROOT, "host"), "--list", "../cores/vc20/core_sources.tcl", "--list", "gideon_vhdl.tcl", "--top", "layer2_vc20",
                                       "--bus", "vc20_core:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000", "--core", "vc20",
                                       "--scan", "0:0x100", "-j", "1", "--toml-a-out", out_toml, "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"), "--cache", os.path.join(td, "c.json")])
    finally:
        toml_a_emit._verify = real
    must(rc == 1 and not os.path.exists(out_toml) and not os.path.exists(out_toml + ".tmp") and "was not written" in buf.getvalue(), "emit: a TOML A that `check` rejects leaves no file (neither the output nor the .tmp)", buf.getvalue())

rc, out = run("lint-src", os.path.join(HERE, "lint", "clean.cc"))
must(rc == 0 and "no literal I/O address" in out, "lint-src: a source without a literal address passes", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"))
must(rc == 1 and "hit.cc:1: 0x10100500 lies in the I/O space" in out and "hit.cc:2: 268435456u lies in the I/O space" in out
     and "hit.cc:2: 0x101A4000UL lies in the I/O space" in out and "3 literal address(es)" in out, "lint-src: the three literals of hit.cc are listed", out)
rc, out = run("lint-src", os.path.join(HERE, "lint", "hit.cc"), "--allow", "hit.cc:0x10100500", "--allow", "other.cc:268435456u")
must(rc == 1 and "hit.cc:1:" not in out and "hit.cc:2: 268435456u" in out and "2 literal address(es)" in out,
     "lint-src: --allow passes one literal of one file and nothing else", out)
print("rtl_check tests: PASS")
