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
  3d. the ZX Spectrum Next (T239.5 D15): cores/zxnext/app/gateware.toml against the files of cores/zxnext/app/rtl_check_sources.tcl, the request being the five cw_* ports of layer2_zxnext.
  3e. the Apple IIe (T239.3 D61): cores/apple2/app/gateware.toml against the files of cores/apple2/core_sources.tcl, the request being the five cw_* ports of layer2_apple2.
  3e. the Neo Geo (T239.5 D15): cores/neogeo/app/gateware.toml against the files of cores/neogeo/app/rtl_check_sources.tcl, the request being the five cw_* ports of layer2_neogeo.
  3f. the Amiga 500 (T239.5 D15): cores/amiga/app/gateware.toml against the files of cores/amiga/core_sources.tcl, the request being the five cw_* ports of layer2_amiga (the A500's own amiga_regs.v).
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
      snes_core.v), and the module of a top file's instance from a `define of the top file.
  2h. the miniature design tests/rtl_check/mini8 (the review of T239.3's reader): each pattern that passed a wrong TOML A or crashed is refused (a finding or a pattern the
      check does not know): the 0 of an arm on the access type, of an outer arm before its inner decision, of an inner `when others` or `else`, of the other half of a vector;
      an overloaded, a recursive process function, a function local read before it is assigned, an argument declared with a subtype name, a byte outside the argument,
      a top file's instance whose module is an undefined macro; a process variable that takes the address (under a case on the write flag, inside an arm of the address), a
      decision on a variable that merges the address with another value, the 0 before or in the default of a reverse case (`case (1'b1)`) whose choices name the address, an arm
      that assigns the address to a variable and decides on it, a slice argument for a formal the function indexes, an index outside the argument. A process variable
      written in the arm of the address is a write (the TOML A that says so agrees).
  2i. the miniature design tests/rtl_check/mini9 (the review of T239.3's reader, round 9): one module per pattern, r at +0 written and read, ghost at +5 that the RTL
      does not have. A constant 0 is no read in any arm (a range, a region, a reverse case, a case on a compare, a variable, a second case); a strobe under
      `when others` is no write of the byte; a value that merges the address with another value (a conditional signal, `sel <= addr when mode = '1' else x"00"`) is
      refused where a decision on it is open; an always @* reg of a blocking assignment and a VHDL variable are read on their path; a plain case item with ? bits and
      a casez are refused. Each block fails on ghost alone (r agrees) or is refused.
  3. check on host/ with coreapp/gateware/host.toml (T239.3 V-1, V-1b): the host's own decode, no checkout of another repository needed. One finding: sd_spi.crc,
     a strobe of the SPI's state writing a clock after the request, which the check does not follow (no write proved, no pass).
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
                        ("top8nomac", "mini8_nomac.toml", "review 7: a top file's instance whose module is a macro the file does not define is refused, not skipped"),
                        ("topv1", "mini8_v1.toml", "review 8: a process variable that takes the address under a case on the write flag (case we) is no write"),
                        ("topv2", "mini8_v2.toml", "review 8: a case on a process variable that is the address on one path and 0 on another (merged, 0x25 or 0) is refused"),
                        ("topv3", "mini8_v3.toml", "review 8: a process variable that takes the address inside an arm of the address (a2_regs' o) is no write"),
                        ("topg1", "mini8_g1.toml", "review 8: the 0 of an outer arm before a reverse case (case (1'b1)) whose choices decide on the address is no decoded read"),
                        ("topg2", "mini8_g2.toml", "review 8: the default 0 of a reverse case whose choices decide on the address is no decoded read"),
                        ("toph1", "mini8_h1.toml", "review 8: an arm that assigns the address to a process variable and then decides on it is no last decision: its 0 is no decoded read"),
                        ("topfx", "mini8_fx.toml", "review 8: a slice wd(31 downto 8) for a function formal the body indexes (return v(15 downto 8)) is refused, not renumbered from 0"),
                        ("topix", "mini8_ix.toml", "review 8: an index of a function formal outside its argument (bit 15 and bit -1 of 8 bits) is refused, not a 0")):
    rc, out = mini8(top, toml)
    must(refused(rc, out), "mini8 (%s): %s" % (top, what), out)
rc, out = mini8("topix", "mini8_ix.toml")
must("reads bit -1 of an argument of 8 bits" in out, "mini8 (topix): the index below the argument is named as -1 (a signed integer), not 4294967295", out)
rc, out = mini8("topb", "mini8_b.toml")
must(rc == 0 and "1 block(s), 2 properties agree" in out,
     "mini8 (review 5): a process variable set in the case arm of the address under the write flag (`when x\"40\" => pend := '1'`) is a write: the strobe agrees", out)


def mini9(name):
    top = "top" + name
    return run("check", "--design", os.path.join(HERE, "mini9"), "--toml-a", os.path.join(HERE, "mini9_%s.toml" % name), "--top", top,
               "--bus", top + ":write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x10000000:0x100")


def ghost_only(rc, out, d):
    """The block fails on ghost alone: r (written and read at +0) agrees."""
    return (rc == 1 and "Traceback" not in out and "agree with the RTL" not in out and "1 finding(s)" in out
            and ("property ghost (dir %s): byte +0x5 (0x10000025) has no %s" % (d, {"r": "read", "w": "write"}[d])) in out)


def unknown(rc, out):
    return rc == 1 and "Traceback" not in out and "agree with the RTL" not in out and "an RTL pattern I do not know" in out


for name, d, kind, what in (
        ("n1", "r", "g", "a range arm (`when 16#21# to 255`) that answers the constant 0 is no read"),
        ("n2", "r", "g", "an arm of a region (`case addr(7 downto 4)`) in the else of the address compare that answers 0 is no read"),
        ("n3", "r", "g", "a Verilog arm of a region (`case (addr[7:4]) 4'h2: q <= 8'h00`) is no read"),
        ("n4", "r", "g", "a reverse case whose choice is an inequality (`case (1'b1) (addr != 8'h20): q <= 8'h00`) is no read"),
        ("n5", "r", "g", "a case on a compare (`case addr = x\"20\" is when false => q <= x\"00\"`) is no read"),
        ("n6", "r", "g", "the 0 of an arm of a region before a reverse case on the low bits is no read"),
        ("n7", "r", "g", "the 0 of an arm of a region before an if on the low bits is no read"),
        ("n8", "r", "g", "the 0 of an arm of a region before an if on a wire that compares the low bits is no read"),
        ("n9", "r", "g", "a case on a variable (1 for every byte but 0x20) whose arm answers 0 is no read"),
        ("n10", "r", "g", "a second case on addr(0) that answers 0 at the odd bytes is no read"),
        ("p1", "r", "g", "the 0 of an arm of a region before an if on the low bits (no read flag in it) is no read"),
        ("p2", "r", "u", "a process function without parameters, called without parentheses, is refused"),
        ("p3", "w", "u", "a write case on a conditional signal that merges the address with 0 (`sel <= addr when mode = '1' else x\"00\"`) is refused"),
        ("p4", "w", "g", "a register that takes the address under the write flag at every byte (`last <= addr`) is no write of 0x25"),
        ("p5", "w", "g", "a strobe under `when others` (`err <= '1'`, every byte but 0x20) is no write of 0x25"),
        ("p6", "r", "u", "a read case on a conditional signal that merges the address with 0 is refused (0x25 or 0 merged matches 0x20)"),
        ("p7", "r", "u", "a casez is refused"),
        ("p8", "r", "u", "a plain case item with ? bits (`8'b0010_0???`) that the request does not decide is refused"),
        ("p9", "w", "g", "an always @* reg of a blocking assignment that takes the address in an undecided if is read on that path (`sel == 8'h20` is false for 0x25): no write"),
        ("q1", "w", "g", "the VHDL variable of p9 is read on its path: no write of 0x25"),
        ("p9c", "w", "g", "the control of p9 (the compare names addr itself): no write of 0x25")):
    rc, out = mini9(name)
    must(ghost_only(rc, out, d) if kind == "g" else unknown(rc, out), "mini9 (top%s): %s" % (name, what), out)

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

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/zxnext/app/rtl_check_sources.tcl",
              "--top", "layer2_zxnext",
              "--bus", "layer2_zxnext:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "zxnext", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "14 block(s), 42 properties agree" in out, "zxnext: 14 blocks, 42 properties of gateware.toml agree with the RTL of the core window (zxn_regs.v: the identity and the features byte, the IRQ pair, the DDR2 calibration, the machine, the cache counters, the frame buffers, the DDR2 bases, the clock, the keys, the SD image, the sector mailbox, the card's counters, the mouse; page 6 is the hand-over to the host's pixel layer)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/apple2/core_sources.tcl",
              "--top", "layer2_apple2",
              "--bus", "layer2_apple2:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "apple2", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "12 block(s), 53 properties agree" in out, "apple2: 12 blocks, 53 properties of gateware.toml agree with the RTL of the core window (a2_regs.vhd: the identity, the IRQ pair, the DDR2 calibration, the machine, the frame buffers, the aux RAM and the ROM aperture's selector, the two floppy drives with the disk registers, the palette, the key injection, the mouse, the track statistics, the slot 7 hard disk card)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/neogeo/app/rtl_check_sources.tcl",
              "--top", "layer2_neogeo",
              "--bus", "layer2_neogeo:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "neogeo", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "14 block(s), 56 properties agree" in out, "neogeo: 14 blocks, 56 properties of gateware.toml agree with the RTL of the core window (neo_regs.v: the identity, the IRQ pair, the DDR2 calibration, the machine, the hold counters, the frame buffers, the DDR2 bases, the mover, the clock, the pads, the ROM masks, the memory side's counters, the copy engine, the line cache counters; page 6 is the hand-over to the host's pixel layer)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/amiga/core_sources.tcl",
              "--top", "layer2_amiga",
              "--bus", "layer2_amiga:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "amiga", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "16 block(s), 48 properties agree" in out, "amiga: 16 blocks, 48 properties of gateware.toml agree with the RTL of the core window (the A500's own amiga_regs.v: the identity and its FEATURES word, the IRQ pair, the DDR2 calibration, the machine, the frame buffers, the statistics selector, the clock, the four floppy drives, the two hard disks, the sector mailbox, the mouse, the raw keys, the second game port's pad, the floppy write log; no display window and no crop window, which only the A1200's copy has; page 6 is the hand-over to the host's pixel layer)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/ddt_novic/core_sources.tcl", "--top", "layer2_ddt_novic",
              "--bus", "layer2_ddt_novic:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "ddt_novic", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "6 block(s), 19 properties agree" in out, "ddt_novic: 6 blocks, 19 properties of gateware.toml agree with the RTL of the core window (ddt_regs.v's ident, irq, ddr_cal, geometry, machine and video registers; page 6, the host's pixel layer, is not described)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/zx/app/rtl_check_sources.tcl", "--top", "layer2_zx",
              "--bus", "layer2_zx:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "zx", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "10 block(s), 38 properties agree" in out, "zx: 10 blocks, 38 properties of gateware.toml agree with the RTL of the core window (zx_regs.v's ident, irq, ddr_cal, geometry, machine, video, window page, snapshot, tape and key-injection registers; page 6, the host's pixel layer, is not described)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/cpc/app/rtl_check_sources.tcl", "--top", "layer2_cpc",
              "--bus", "layer2_cpc:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "cpc", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "12 block(s), 41 properties agree" in out, "cpc: 12 blocks, 41 properties of gateware.toml agree with the RTL of the core window (cpc_regs.v's ident, irq, ddr_cal, geometry, machine, video, window page, tape, key-injection, disk and the two dirty maps; page 6, the host's pixel layer, is not described)")

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/pet/app/rtl_check_sources.tcl", "--top", "layer2_pet",
              "--bus", "layer2_pet:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "pet", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "10 block(s), 55 properties agree" in out, "pet: 10 blocks, 55 properties of gateware.toml agree with the RTL of the core window (pet_regs.v's ident, irq, ddr_cal, machine, ROM aperture select, video, the dual drive unit, its write log, the cassette and the pads; page 6, the host's pixel layer, is not described)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/c128/app/rtl_check_sources.tcl", "--top", "layer2_c128",
              "--bus", "layer2_c128:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--bus", "layer2_c128:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata", "--window", "0x10020000:0x4000:full",
              "--toml-a", os.path.join(ROOT, "cores", "c128", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"), "--define", "VDC64", "--define", "ROM_CACHE", "--define", "RAM_DDR2", "--define", "SID2", "--define", "SOCK_AUDIO", "--define", "TAPE", "--define", "NO_NET", "--define", "NO_VIC_SCALER")
must(rc == 0 and "17 block(s), 86 properties agree" in out, "c128: 17 blocks, 86 properties of gateware.toml agree with the RTL of the core window (the C64U window's ident, irq, ddr_cal, caps and tape pins, the host's pixel layer capture, the C128's page E: id, irq, machine, ROM line cache, video, cold start, character store, counters, activity) and of the drive window behind c64u_drive_l2 (mm_drive: the 1541 block and the WD177x block of the 1581)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/c64scpu/app/rtl_check_sources.tcl", "--top", "layer2_c64scpu",
              "--bus", "layer2_c64scpu:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--bus", "layer2_c64scpu:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata", "--window", "0x10020000:0x4000:full",
              "--toml-a", os.path.join(ROOT, "cores", "c64scpu", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"), "--define", "SOCK_AUDIO")
must(rc == 0 and "15 block(s), 67 properties agree" in out, "c64scpu: 15 blocks, 67 properties of gateware.toml agree with the RTL of the core window (ident, irq, ddr_cal, machine, status, ROM control and base, SuperCPU ROM window, character store, cold start, counters) and of the drive window behind its mm_drive (the 1541 block and the WD177x block of the 1581)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "../cores/diag/app/rtl_check_sources.tcl",
              "--top", "layer2_diag",
              "--bus", "layer2_diag:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata", "--window", "0x101A0000:0x10000",
              "--toml-a", os.path.join(ROOT, "cores", "diag", "app", "gateware.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
must(rc == 0 and "14 block(s), 51 properties agree" in out, "diag: 14 blocks, 51 properties of gateware.toml agree with the RTL of the core window (diag_regs.v v2: ident, engine, live snapshot, A/V, DRVEN, pin bank, pin ROM, capture; TICKS_US is a module parameter the checker does not read)", out)

rc, out = run("check", "--design", os.path.join(ROOT, "host"), "--list", "build.tcl", "--list", "gideon_vhdl.tcl",
              "--toml-a", os.path.join(ROOT, "coreapp", "gateware", "host.toml"), "--maps", os.path.join(ROOT, "app", "sdkpkg", "maps"))
crc = "rtl_check: block sd_spi, property crc (dir rw): byte +0xC (0x1006000C) has no write in the RTL"
must(rc == 1 and [l for l in out.splitlines() if l.startswith("rtl_check: ")] == [crc, "rtl_check: 1 finding(s)"] and "clean room" not in out,
     "host: coreapp/gateware/host.toml against the RTL of host/: one finding, sd_spi.crc has no write (spi_peripheral_io.vhd clears the CRC in its state writing, a "
     "clock after the request, by the address alone: `when \"11\" => clear_crc <= '1'`; the request's own clock only moves the state, `state <= writing` for every byte "
     "of the window, which selects no byte by name; the check proves no write there and says so). The rest agrees (base blocks by property, core blocks by hand-over; the overlay, the palette, "
     "IEC, SD and WiFi windows (wifi_uart.tx_push and rx_get: strobes in arms of io_req.address, which wifi_req <= io_req copies field by field), the overlay palette as three runs of 16 elements with the stride 4, behind io_bus_splitter the ITU with its console UART, USB and Ethernet, and the flash window whose flash_gate holds each request one clock (a held request); no Gideon file of the deny list is in the source list, the Minimig agnus_copper.v is Verilog and is no Gideon file)", out)

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
