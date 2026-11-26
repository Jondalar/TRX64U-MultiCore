#!/usr/bin/env python3
"""T239.3 P1-3: tools/rtl_check.py. Happy paths only (D30: no negative control).

  1. check on the miniature design of tests/rtl_check/mini (a VHDL module that decodes a request, a Verilog module behind a strobe):
     every block and property of mini.toml agrees with the RTL.
  2. check on c64_basic of TRX64-Ultimate with the fixture tests/rtl_check/c64_basic.toml and the shipped maps of app/sdkpkg/maps,
     when the directory is there
     (environment TRX64_ULTIMATE, or ~/Development/TRX64-Ultimate); it is read, never written.
  2b. the miniature design tests/rtl_check/mini2 (V-1c): a request splitter, a process alias and procedure, a VHDL block, a RAM write sink.
  2e. the miniature design tests/rtl_check/mini5 (V-1e): ascending vectors, a for-generate, an if-generate, a stride, a core block the design decodes itself.
  3. the VIC-20 partial (T239.3 V-2): cores/vc20/app/gateware.toml against the files of cores/vc20/core_sources.tcl, the request being the five ports of
     the core window (cw_*) of the instance vc of layer2_vc20; it needs this repository only.
  2c. the miniature design tests/rtl_check/mini3 (V-1d): a handshake into a module with a register pipeline (c_addr, op_a), another master loading the same register, a Verilog
      generate-for with a named block, a function, and a module behind continuous assignments of the pipeline (m3dev), checked by default (D43), and with the two options of 0.4.5, which change nothing.
  2c2. the miniature design tests/rtl_check/mini4 (V-1d): a pattern the check does not know stops the one block that reaches it; the other block has its verdict.
  2d. c64_basic of TRX64-Ultimate with the TOML A of its commit 975aa59b (the TOML A is read with git show, the RTL is the checkout's working tree, so the result follows the checkout;
      skipped without the checkout or the commit): all 39 blocks (168 properties) agree with the RTL (V-1f: trx_dbg_run.owner byte 3, cyc_cmp, cmp_en, trx_statebus.status and trx_debug.ipc_set included).
  2f. the miniature design tests/rtl_check/mini6 (V-1f): a unit two modules behind the handshake (an instance inside the module that the continuous assignments feed), a genvar select, a write that
      clears a register whatever the data is, a register written under an address compare while another expression names the data behind a different compare.
  2g. the miniature design tests/rtl_check/mini7, one top per construct, the request on the top's own ports (the cores' way): a function declared in a process (apple2's
      a2_regs.vhd; its arguments are 0 but at the right bytes), a write branch that assigns the address to a process variable under the write flag (a2_regs.vhd; emit finds
      the standard block read-only), an instance whose module is a `define of its file and an `ifdef / `elsif / `undef on the file's own `define (the SNES core's
      snes_core.v), the module of a top file's instance from a `define of the top file, and a register whose decoded read answers the constant 0 (c64_basic's
      command_protocol.vhd, command_start).
  2h. the miniature design tests/rtl_check/mini8 (the review of T239.3's reader): each pattern that passed a wrong TOML A or crashed is refused (a finding or a pattern the
      check does not know): the 0 of an arm on the access type, of an outer arm before its inner decision, of an inner `when others` or `else`, of the other half of a vector;
      an overloaded, a recursive process function, a function local read before it is assigned, an argument declared with a subtype name, a byte outside the argument,
      a top file's instance whose module is an undefined macro. A process variable written in the arm of the address is a write (the TOML A that says so agrees).
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


def mini7(top, toml):
    return run("check", "--design", os.path.join(HERE, "mini7"), "--toml-a", os.path.join(HERE, toml), "--top", top,
               "--bus", top + ":write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x10000000:0x100")


rc, out = mini7("top7fn", "mini7_fn.toml")
must(rc == 0 and "1 block(s), 4 properties agree" in out,
     "mini7 (apple2's a2_regs.vhd): a function declared in a process (a local `downto 0` that takes the argument, one return), called with a constant vector, with a register and "
     "with a slice of a constant vector, reads the right byte: build2 (byte 2 of 0x00D40000), base and stat_hi (the high word of a 64-bit vector whose low word is 0) are read", out)
import tempfile
with tempfile.TemporaryDirectory() as td7:
    out7 = os.path.join(td7, "m7var.toml")
    rc, out = run("emit", "--design", os.path.join(HERE, "mini7"), "--top", "top7var",
                  "--bus", "top7var:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x10000000:0x100", "--core", "m7v",
                  "--scan", "0:0x40", "-j", "1", "--toml-a-out", out7, "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
    text = open(out7).read() if os.path.exists(out7) else ""
must(rc == 0 and "agree with the RTL" in out and "[identity]" in text and text.count('dir = "rw"') == 1,
     "mini7 (apple2's a2_regs.vhd): an assignment of the address to a process variable under the write flag is no write: emit finds the standard block and 0x31 read-only, "
     "only the register at 0x30 is written", out + text)
rc, out = mini7("top7mac", "mini7_mac.toml")
must(rc == 0 and "1 block(s), 1 property agree" in out,
     "mini7 (the SNES core's snes_core.v): the instance whose module is a `define of its file (under `ifndef, the ports in the next lines) is followed, and `ifdef / `elsif / "
     "`undef decide on the file's own `define: its register is written and read", out)
rc, out = mini7("top7tmac", "mini7_mac.toml")
must(rc == 0 and "1 block(s), 1 property agree" in out,
     "mini7: the top file's instance whose module is a `define of the top file is followed: the register behind it is written and read", out)
rc, out = mini7("top7zero", "mini7_zero.toml")
must(rc == 0 and "1 block(s), 2 properties agree" in out,
     "mini7 (c64_basic's command_protocol.vhd, command_start): the register whose case arm answers the constant 0 is read", out)



def mini8(top, toml):
    return run("check", "--design", os.path.join(HERE, "mini8"), "--toml-a", os.path.join(HERE, toml), "--top", top,
               "--bus", top + ":write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x10000000:0x100")


def refused(rc, out):
    """A finding or a pattern the check does not know; never a pass, never a crash."""
    return rc == 1 and "Traceback" not in out and "agree with the RTL" not in out and ("finding(s)" in out or "an RTL pattern I do not know" in out)


for top, toml, what in (("topd", "mini8_d.toml", "review 1: the 0 an arm on the access type (case {re, we}) answers is no decoded read"),
                        ("topd2", "mini8_d2.toml", "review 1: the default 0 before an if on the address is no decoded read"),
                        ("topa1", "mini8_a1.toml", "review 2: the 0 an outer arm of the address writes before its inner case decides is no decoded read"),
                        ("topa2", "mini8_a2.toml", "review 2: the 0 of an inner `when others` of a nested decode is no decoded read"),
                        ("topa3", "mini8_a3.toml", "review 2: the 0 of the else of an if on the address inside an outer arm is no decoded read"),
                        ("topc", "mini8_c.toml", "review 3: an arm that writes 0 to the other half of a vector decodes no byte of the half the answer reads"),
                        ("topf1", "mini8_f1.toml", "review 4: two process functions of one name (an overload) are refused"),
                        ("topf2", "mini8_f2.toml", "review 7: a function local read before it is assigned is refused (it does not read the architecture's signal of that name)"),
                        ("topf3", "mini8_f3.toml", "review 7: a process function that calls itself is refused"),
                        ("top8sub", "mini8_sub.toml", "review 6: a function argument declared with a subtype name is refused, not a crash"),
                        ("top8fn", "mini8_fn.toml", "review 7: a function slice outside its argument (bytes 4..7 of 32 bits) is refused, not a 0"),
                        ("top8nomac", "mini8_nomac.toml", "review 7: a top file's instance whose module is a macro the file does not define is refused, not skipped")):
    rc, out = mini8(top, toml)
    must(refused(rc, out), "mini8 (%s): %s" % (top, what), out)
rc, out = mini8("topb", "mini8_b.toml")
must(rc == 0 and "1 block(s), 2 properties agree" in out,
     "mini8 (review 5): a process variable set in the case arm of the address under the write flag (`when x\"40\" => pend := '1'`) is a write: the strobe agrees", out)

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
