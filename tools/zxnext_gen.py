#!/usr/bin/env python3
"""T116 D1/Q9: cores/zxnext/gen = the ZX Spectrum Next FPGA source (cores/zxnext/rtl, byte for byte as tools/vendor_zxnext.sh copied it, GPL v3
or later) turned into one Verilog file that Verilator and Vivado read the same way, plus the changes below, each counted so that a changed
upstream fails here instead of building something else.

  1. Three edits to the VHDL, in a scratch copy (rtl/ is never edited), each a place the converter or the synthesis of the original does
     not accept as written:
       - cpu/t80n_pack.vhd declares a signal in a package (unused; GHDL: "signals in packages are not supported"): the line is dropped;
       - serial/fifop.vhd compares a slice with to_unsigned(-1, ...) (out of range at elaboration): the stated intent, "all ones", is written;
       - rom/bootrom.vhd names the UNISIM library (Xilinx primitives, none used): the two lines are dropped.
       - zxnext.vhd reports the generic g_board_issue in NextReg 0x0F (T116 D10); an input port i_BOARD_ID (reset value of the register that drives it:
         3 = issue 5) is reported instead, while the generic keeps selecting the structure (every `generate` stays issue 5): two edits, a port and the line.
     The memories the original keeps outside its VHDL, or writes in a style the converter cannot read, come from cores/zxnext/ip/zxn_ip.vhd.
  2. The VHDL is converted once by GHDL 6.0.0 (`--synth --out=verilog`, in the container tools/ghdl6 builds; 5.0.1 splits the Z80N's register
     file into memories of 3, 2 and 3 bits and declares their read data with one width, so a register reads back wrong), with the generics of the original's issue-5 top
     (zxnext_top_issue5.vhd:34-39, 2357-2366): machine id 0x0A, version 3.02, board issue 3. The SHA-256 of the VHDL input (the patched
     copy, the memory file, the generics) is recorded; `--check` compares it. The raw GHDL output is kept as gen/zxnext_ghdl.v.gz.
  3. The converted Verilog gets the clock-enable form (the machine was written for five related clocks, 28 MHz and its inverse, 14 MHz,
     7 MHz and a gated CPU clock; the partition has one 100 MHz clock): every module's clock port X becomes the pair X_cep (a rising edge of X
     is now) and X_cen (a falling edge), every `always @(posedge X)` becomes `always @(posedge fclk) if (X_cep)` (`negedge`: X_cen), every
     instance passes the pair and `fclk` on, an asynchronous reset becomes a reset of fclk. cores/zxnext/trx/zxn_clk.v makes the enables.
  4. The memory initialisers that only write zeros become a loop (the file is a third of its size otherwise).
  5. The Z80N's register file (GHDL makes five eight-entry memories of it) gets ram_style "registers": Vivado refuses it as a RAM
     ("Unsupported RAM template", measured on the builder 2026-10-22) and the VHDL describes flip-flops; the three `always` blocks GHDL makes of
     one process' writes to it become one (several drivers otherwise).
Nothing else is touched. gen/ is committed (the CI has no checkout of the upstream and no GHDL).
  tools/zxnext_gen.py            write cores/zxnext/gen from rtl/ (GHDL only if its input changed; `--ghdl` forces it)
  tools/zxnext_gen.py --check    fail if rtl/ is not what was vendored (SHA256SUMS), the VHDL input is not what was converted, or gen/ is not
                                 what the raw conversion makes (the lint stage runs this; no GHDL needed)
"""
import gzip, hashlib, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTL = os.path.join(ROOT, "cores/zxnext/rtl")
IP = os.path.join(ROOT, "cores/zxnext/ip/zxn_ip.vhd")
GEN = os.path.join(ROOT, "cores/zxnext/gen")
RAWGZ = os.path.join(GEN, "zxnext_ghdl.v.gz")
OUTV = os.path.join(GEN, "zxnext.v")
INSHA = os.path.join(GEN, "zxnext.vhd.sha256")
GENERICS = ["g_machine_id=00001010", "g_video_def=000", "g_version=00110010", "g_sub_version=00000011", "g_board_issue=0011",
            "g_video_inc=11"]
CLOCKS = ["i_CLK_28", "i_CLK_28_n", "i_CLK_14", "i_CLK_7", "i_CLK_CPU"]
GHDL_VERSION = "GHDL 6.0.0"
LOG = {}
# a clock that is not a clock of the partition: the Raspberry Pi accelerator's I2S bit clock (GPIO 18 and the enable, section 4 Out); never ticks
TIED = {"pi_si2s_sck", "i2s_sck"}
UNUSED_CLOCK_PORT = {("tdpram", "clk_b_i")}   # cores/zxnext/ip/zxn_ip.vhd: port B runs on port A's clock (the one instance ties both to i_clk); the port is not read
FORWARD = {"i2s_slave": "i_i2s_sck"}      # `assign o_i2s_sck = i_i2s_sck;` hands the clock on; it becomes a constant (the clock never ticks)


def base(n):
    return n.split("_B")[0]                       # GHDL 6 names a module <entity>_B<architecture>_<generics>


def count(name, n=1):
    LOG[name] = LOG.get(name, 0) + n


def die(msg):
    raise SystemExit("zxnext_gen: " + msg)


def check_rtl():
    want = {}
    for ln in open(os.path.join(RTL, "SHA256SUMS")):
        h, p = ln.split()
        want[os.path.normpath(p)] = h
    have = {}
    for d, _, fs in os.walk(RTL):
        for f in fs:
            if f in ("NOTICE", "SHA256SUMS"):
                continue
            p = os.path.relpath(os.path.join(d, f), RTL)
            have[os.path.normpath(p)] = hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest()
    if have != want:
        bad = sorted(set(have) ^ set(want)) + sorted(k for k in have if k in want and have[k] != want[k])
        die("rtl/ is not what tools/vendor_zxnext.sh vendored: %s" % ", ".join(bad[:8]))


# ------------------------------------------------------------------------------------------------------------- VHDL side
def sub1(text, old, new, name, n=1):
    c = text.count(old)
    if c != n:
        die("%s: expected %d x %r, found %d" % (name, n, old, c))
    count(name, c)
    return text.replace(old, new)


def patched_vhdl():
    """{relative path: text} of the VHDL that goes to GHDL, the patches applied."""
    files = {}
    for d, _, fs in os.walk(RTL):
        for f in sorted(fs):
            if f.endswith(".vhd"):
                p = os.path.relpath(os.path.join(d, f), RTL)
                files[p] = open(os.path.join(d, f)).read()
    files["cpu/t80n_pack.vhd"] = sub1(files["cpu/t80n_pack.vhd"], "   signal Z80N_seq_s    : Z80N_seq;\n", "", "t80n_pack: signal in a package")
    # GHDL names an instance's output wire <label>_<port>: instance `alu`, port Q collides with the signal ALU_Q (two `alu_q` in the Verilog)
    t, n = re.subn(r"\bALU_Q\b", "ALUQs", files["cpu/t80n.vhd"], flags=re.I)
    if n != 6:
        die("t80n: expected 6 x ALU_Q, found %d" % n)
    count("t80n: ALU_Q -> ALUQs", n)
    files["cpu/t80n.vhd"] = t
    files["serial/fifop.vhd"] = sub1(files["serial/fifop.vhd"], "std_logic_vector(to_unsigned(-1,stored'length-1))",
                                     "(DEPTH_BITS-2 downto 0 => '1')", "fifop: to_unsigned(-1)")
    files["rom/bootrom.vhd"] = sub1(files["rom/bootrom.vhd"], "library UNISIM;\n  use UNISIM.Vcomponents.all;\n", "", "bootrom: UNISIM")
    z = files["zxnext.vhd"]
    z = sub1(z, "      i_CLK_PSG_EN         : in std_logic;      -- 28MHz clock enable for PSG\n",
             "      i_CLK_PSG_EN         : in std_logic;      -- 28MHz clock enable for PSG\n"
             "      i_BOARD_ID           : in std_logic_vector(3 downto 0);   -- T116 D10: the id NextReg 0x0F reports\n", "zxnext: port i_BOARD_ID")
    z = sub1(z, 'port_253b_dat <= "0000" & std_logic_vector(g_board_issue);', 'port_253b_dat <= "0000" & i_BOARD_ID;', "zxnext: NextReg 0x0F from i_BOARD_ID")
    files["zxnext.vhd"] = z
    files["zxn_ip.vhd"] = open(IP).read()
    return files


def vhdl_hash(files):
    h = hashlib.sha256()
    for p in sorted(files):
        h.update(p.encode() + b"\0" + files[p].encode() + b"\0")
    h.update(" ".join(GENERICS).encode())
    return h.hexdigest()


def run_ghdl(files):
    d = tempfile.mkdtemp(prefix="zxnext_gen_")
    try:
        for p, t in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, p)) or d, exist_ok=True)
            open(os.path.join(d, p), "w").write(t)
        g = " ".join("-g" + x for x in GENERICS)
        script = ("cd /w; ghdl --version | head -1 > ver.txt; "
                  "mkdir wk; F='--std=08 -frelaxed -fsynopsys --workdir=wk'; "
                  "ghdl -i $F $(find . -name '*.vhd' | sort) > i.log 2>&1 && ghdl -m $F zxnext > m.log 2>&1; "
                  "ghdl --synth $F --out=verilog %s zxnext > out.v 2> err.log || { cat err.log; exit 1; }" % g)
        img = os.environ.get("ZXN_GHDL_IMAGE", "trx-ghdl6")
        r = subprocess.run(["nice", "-n", "15", "container", "run", "--rm", "--cpus", "4", "-m", "8G", "-v", d + ":/w", img, "sh", "-c", script])
        if r.returncode != 0 or not os.path.exists(os.path.join(d, "out.v")) or os.path.getsize(os.path.join(d, "out.v")) == 0:
            die("the GHDL conversion failed (see above)")
        ver = open(os.path.join(d, "ver.txt")).read().strip()
        if not ver.startswith(GHDL_VERSION):
            die("the image's GHDL is %r, tools/zxnext_gen.py wants %s (tools/ghdl6/Dockerfile: GHDL 5.0.1 splits the Z80N's register file wrongly)" % (ver, GHDL_VERSION))
        return open(os.path.join(d, "out.v")).read()
    finally:
        shutil.rmtree(d, ignore_errors=True)


# ------------------------------------------------------------------------------------------------------------ Verilog side
MOD_RE = re.compile(r"^module (\w+)\s*$")
SENS_RE = re.compile(r"^(\s*)always @\((posedge|negedge) (\w+)\)\s*$")
ASYNC_RE = re.compile(r"^(\s*)always @\((posedge|negedge) (\w+) or posedge (\w+)\)\s*$")


def split_modules(lines):
    mods, order, i = {}, [], 0
    while i < len(lines):
        m = MOD_RE.match(lines[i])
        if m:
            j = i
            while not lines[j].startswith("endmodule"):
                j += 1
            name = m.group(1)
            mods[name] = lines[i:j + 1]
            order.append(name)
            i = j + 1
        else:
            i += 1
    return mods, order


def header_end(body):
    for k, ln in enumerate(body):
        if ln.rstrip().endswith(");"):
            return k
    die("no end of module header")


def input_names(body):
    hdr = "\n".join(body[:header_end(body) + 1])
    return set(re.findall(r"\binput\s+(?:\[[^\]]*\]\s*)?(\w+)", hdr))


def find_instances(body, mods):
    """[(start line, end line, module, instance name, [(port, expr, line)])]"""
    res, i = [], header_end(body) + 1
    while i < len(body):
        m = re.match(r"^\s*(\w+) (\w+) \($", body[i])
        if m and m.group(1) in mods:
            j, conns = i + 1, []
            while True:
                cm = re.match(r"^\s*\.(\\?[\w$]+)\s*\((.*?)\)([,)]*);?\s*$", body[j])
                if not cm:
                    die("instance %s: cannot read %r" % (m.group(2), body[j]))
                conns.append((cm.group(1), cm.group(2), j))
                if body[j].rstrip().endswith(";"):
                    break
                j += 1
            res.append((i, j, m.group(1), m.group(2), conns))
            i = j + 1
        else:
            i += 1
    return res


def transform(raw):
    lines = raw.split("\n")
    mods, order = split_modules(lines)
    inputs = {m: input_names(b) for m, b in mods.items()}
    clk = {m: set() for m in mods}
    for m, b in mods.items():
        for ln in b:
            for rx in (ASYNC_RE, SENS_RE):
                mm = rx.match(ln)
                if mm:
                    x = mm.group(3)
                    if x not in inputs[m]:
                        die("module %s: clock %s is not an input port" % (m, x))
                    clk[m].add(x)
                    break
    inst = {m: find_instances(b, mods) for m, b in mods.items()}
    changed = True
    while changed:
        changed = False
        for m in mods:
            for (_, _, sm, _, conns) in inst[m]:
                for (p, e, _) in conns:
                    if p in clk[sm]:
                        if e in TIED:
                            continue
                        if e not in inputs[m]:
                            die("module %s: a clock of %s is driven by %r, which is not an input port" % (m, sm, e))
                        if e not in clk[m]:
                            clk[m].add(e)
                            changed = True
    top = "zxnext"
    if set(clk[top]) != set(CLOCKS):
        die("the top's clocks are %s, expected %s" % (sorted(clk[top]), sorted(CLOCKS)))
    # a clock port may not be used as data
    for m, b in mods.items():
        txt = "\n".join(b[header_end(b) + 1:])
        for x in clk[m]:
            uses = len(re.findall(r"(?<![.\w])%s\b" % re.escape(x), txt))
            ok = len(re.findall(r"(?:pos|neg)edge %s\b" % re.escape(x), txt)) + sum(
                1 for (_, _, sm, _, cs) in inst[m] for (p, e, _) in cs if p in clk[sm] and e == x)
            ok += sum(1 for (_, _, sm, _, cs) in inst[m] for (p, e, _) in cs if e == x and (base(sm), p) in UNUSED_CLOCK_PORT)
            if FORWARD.get(base(m)) == x:
                ok += len(re.findall(r"assign \w+ = %s;" % re.escape(x), txt))
            if uses != ok:
                die("module %s: clock %s is also used as data (%d uses, %d clock uses)" % (m, x, uses, ok))
    # rewrite module by module (line indices are per module body; rebuild from the original text)
    new_mods = {}
    for m in order:
        b = list(mods[m])
        he = header_end(b)
        if clk[m]:
            hdr = "\n".join(b[:he + 1])
            for x in sorted(clk[m]):
                hdr, n = re.subn(r"\binput\s+%s\b" % re.escape(x), "input  %s_cep,\n   input  %s_cen" % (x, x), hdr)
                if n != 1:
                    die("module %s: clock port %s not found in the header" % (m, x))
            hdr = hdr.replace("(", "(input  fclk,\n   ", 1)
            b[:he + 1] = hdr.split("\n")
        # sequential blocks
        text = "\n".join(b)
        if base(m) in FORWARD:
            text = re.sub(r"assign (\w+) = %s;" % FORWARD[base(m)], lambda mo: (count("forwarded clock -> constant (Pi I2S)"), "assign %s = 1'b0;" % mo.group(1))[1], text)
        # asynchronous resets: four statements deep: always / if (r) / A; / else / B;
        pat = re.compile(r"^(\s*)always @\((posedge|negedge) (\w+) or posedge (\w+)\)\n\s*if \(\4\)\n(\s*[^;\n]*;)\n\s*else\n(\s*[^;\n]*;)$", re.M)
        def arepl(mo):
            ind, edge, x, r, a, bb = mo.groups()
            ce = "%s_%s" % (x, "cep" if edge == "posedge" else "cen")
            count("async reset -> fclk reset")
            return "%salways @(posedge fclk)\n%s  if (%s)\n%s\n%s  else if (%s)\n%s" % (ind, ind, r, a, ind, ce, bb)
        text = pat.sub(arepl, text)
        if re.search(r"always @\([^)]* or posedge", text):
            die("module %s: an asynchronous block of a shape not handled" % m)
        def srepl(mo):
            ind, edge, x = mo.groups()
            if x == "fclk":
                return mo.group(0)
            count("always @(%s clock) -> clock enable" % edge)
            return "%salways @(posedge fclk) if (%s_%s)" % (ind, x, "cep" if edge == "posedge" else "cen")
        text = re.sub(r"^(\s*)always @\((posedge|negedge) (\w+)\)\s*$", srepl, text, flags=re.M)
        b = text.split("\n")
        # instances
        for (s, e_, sm, iname, conns) in reversed(find_instances(b, mods)):
            if not clk[sm]:
                continue
            for (p, ex, ln) in conns:
                if p in clk[sm]:
                    if ex in TIED:
                        b[ln] = re.sub(r"\.%s\(%s\)" % (p, re.escape(ex)), ".%s_cep(1'b0), .%s_cen(1'b0)" % (p, p), b[ln])
                        count("tied-off clock (Pi I2S)")
                        continue
                    b[ln] = re.sub(r"\.%s\(%s\)" % (p, re.escape(ex)), ".%s_cep(%s_cep), .%s_cen(%s_cen)" % (p, ex, p, ex), b[ln])
                    count("clock pair passed to an instance")
            b[s] = re.sub(r"\($", "(\n    .fclk(fclk),", b[s])
        b = "\n".join(b).split("\n")
        for (s_, e_, sm, iname, conns) in find_instances(b, mods):
            for (p, ex, ln) in conns:
                if (base(sm), p) in UNUSED_CLOCK_PORT and ex in clk[m]:
                    b[ln] = re.sub(r"\.%s\(%s\)" % (p, re.escape(ex)), ".%s(1'b0)" % p, b[ln])
                    count("clock on an unused port -> 0")
        # zero initialisers
        text = "\n".join(b)
        def zrepl(mo):
            body = mo.group(1)
            names = set(re.findall(r"^\s*(\w+)\[\d+\] = \d+'b0+;$", body, re.M))
            nlines = len([ln for ln in body.split("\n") if ln.strip() and ln.strip() != "end"])
            if len(names) != 1 or nlines != len(re.findall(r"^\s*\w+\[\d+\] = \d+'b0+;$", body, re.M)):
                return mo.group(0)
            nm = names.pop()
            dm = re.search(r"reg \[(\d+):0\] %s\[(\d+):0\]" % re.escape(nm), text)
            if not dm:
                die("module %s: no declaration for memory %s" % (m, nm))
            count("zero initialiser -> loop")
            return "  integer zi_%s;\n  initial for (zi_%s = 0; zi_%s <= %s; zi_%s = zi_%s + 1) %s[zi_%s] = 0;" % (nm, nm, nm, dm.group(2), nm, nm, nm, nm)
        text = re.sub(r"^  initial begin\n((?:\s*\w+\[\d+\] = \d+'b[01]+;\n)+)\s*end$", zrepl, text, flags=re.M)
        # the Z80N's register file (eight entries, three read ports, GHDL cut it into five memories): Vivado cannot make a RAM of that pattern
        # ("Unsupported RAM template", measured on the builder 2026-10-22), so they are flip-flops, which is what the VHDL describes
        def rfrepl(mo):
            count("register file memory -> flip-flops")
            return '  (* ram_style = "registers" *) ' + mo.group(0).lstrip()
        text = re.sub(r"^  reg \[\d+:0\] \w+\[7:0\] ; // memory$", rfrepl, text, flags=re.M)
        # ... and its writes: GHDL gives every write of the VHDL process its own `always` block, which would be several drivers of the same flip-flops
        # (Vivado: "multi-driven net", measured on the builder); they become one block, the statements in the VHDL's order (the later one wins)
        wr = re.compile(r"^  always @\(posedge fclk\) if \((\w+)\)\n    if \((\w+)\)\n      (\w+)\[([^\]\n]+)\] <= (\w+);\n", re.M)
        regs8 = set(re.findall(r'\(\* ram_style = "registers" \*\) reg \[\d+:0\] (\w+)\[7:0\]', text))
        pos = 0
        out = []
        while True:
            mo = wr.search(text, pos)
            if not mo:
                out.append(text[pos:])
                break
            blocks = [mo]
            while True:
                nx = wr.match(text, blocks[-1].end())
                if not nx or nx.group(3) != mo.group(3) or nx.group(1) != mo.group(1):
                    break
                blocks.append(nx)
            out.append(text[pos:mo.start()])
            if mo.group(3) in regs8 and len(blocks) > 1:
                out.append("  always @(posedge fclk) if (%s) begin\n" % mo.group(1))
                for bm in blocks:
                    out.append("    if (%s) %s[%s] <= %s;\n" % (bm.group(2), bm.group(3), bm.group(4), bm.group(5)))
                out.append("  end\n")
                count("register file writes -> one block", len(blocks))
            else:
                out.append(text[mo.start():blocks[-1].end()])
            pos = blocks[-1].end()
        text = "".join(out)
        # `do` is a SystemVerilog keyword and GHDL carries the VHDL name of the T80's output port into the Verilog (as an escaped name since 6.0.0)
        text, nd = re.subn(r"(?<![\w$])do(?![\w$])", "do_", text)
        if nd:
            count("do -> do_", nd)
        new_mods[m] = text
    pre = []
    for ln in lines:
        if MOD_RE.match(ln):
            break
        pre.append(ln)
    res = "\n".join(pre).rstrip() + "\n" if any(pre) else ""
    res = "// T116: tools/zxnext_gen.py made this file from the ZX Spectrum Next FPGA source (cores/zxnext/rtl, GPL v3 or later) by GHDL, then put every\n" \
          "// clock into the enable form (fclk, X_cep, X_cen). Do not edit; gen/zxnext.vhd.sha256 records what it was made from.\n" + res
    for m in order:
        res += new_mods[m] + "\n"
    return res


def main():
    check = "--check" in sys.argv
    check_rtl()
    files = patched_vhdl()
    h = vhdl_hash(files)
    have = open(INSHA).read().split() if os.path.exists(INSHA) else []
    if check:
        if not have or have[0] != h:
            die("the VHDL input (rtl/ with the listed edits, ip/zxn_ip.vhd, the generics) is not what gen/ was converted from (run tools/zxnext_gen.py)")
        raw = gzip.open(RAWGZ, "rt").read()
        if hashlib.sha256(raw.encode()).hexdigest() != have[1]:
            die("gen/zxnext_ghdl.v.gz is not the conversion the hash file records")
        out = transform(raw)
        if not os.path.exists(OUTV) or open(OUTV).read() != out:
            die("gen/zxnext.v is not what the raw conversion makes (run tools/zxnext_gen.py)")
        print("zxnext_gen: rtl/ is the vendored tree, gen/ is made from it (%d edits to the VHDL, %d bytes of Verilog)" % (
            sum(v for k, v in LOG.items() if k.startswith(("t80n_pack", "fifop", "bootrom", "zxnext:"))), len(out)))
        return
    os.makedirs(GEN, exist_ok=True)
    if "--ghdl" in sys.argv or not have or have[0] != h:
        raw = run_ghdl(files)
        with gzip.GzipFile(RAWGZ, "wb", mtime=0) as g:
            g.write(raw.encode())
        open(INSHA, "w").write("%s %s\n" % (h, hashlib.sha256(raw.encode()).hexdigest()))
    else:
        raw = gzip.open(RAWGZ, "rt").read()
    out = transform(raw)
    open(OUTV, "w").write(out)
    for k in sorted(LOG):
        print("  %-48s %d" % (k, LOG[k]))
    print("zxnext_gen: wrote %s (%d lines)" % (OUTV, out.count("\n")))


if __name__ == "__main__":
    main()
