#!/usr/bin/env python3
"""T142: cores/snes/gen = the SNES machine of SNES_MiSTer (cores/snes/rtl, byte for byte as tools/vendor_snes.sh copied it, GPL v3) turned into
one Verilog file that Verilator and Vivado read the same way, plus the changes below, each counted so that a changed upstream fails here instead
of building something else. Patterned on tools/zxnext_gen.py.

  1. The VHDL goes to GHDL as a scratch copy (rtl/ is never edited):
       - the files of the subdirectories get the directory in their name (65C816/MCode.vhd and SPC700/MCode.vhd are two files with one base
         name; GHDL names its object files by it and links the wrong one);
       - every `case` that lists all binary values of its selector and then says `when others => null;` (or has no other branch, which GHDL
         refuses: "missing choice(s)") is made to end with `when others =>` (the last listed choice): the VHDL means the same, but GHDL sees
         a std_logic_vector with nine values per bit, finds a branch that assigns nothing and makes a latch of every combinational signal the
         case drives. The check that the listed choices are all binary
         values is made here, per case; a case that is not complete is left alone.
       - the S-SMP's boot ROM: tools/vendor_snes.sh cut the 64 bytes out of rtl/SMP.vhd (T142 D9: a chip ROM dump; Q3: the application carries the bytes); here the constant
         becomes a 64 byte RAM that the new ports IPL_WR / IPL_WA / IPL_WD of SMP, SNES and snes_sys write (the application writes its spc_ipl table, coreapp/cores/snes.cc,
         into it while the machine is held) - four edits, counted.
       - DSP_LHRomMap.vhd's CC_SR (the Competition Cart's status byte): only bit 1 is ever assigned outside the reset, the other bits are assigned in the reset
         branch alone, which GHDL turns into a signal that holds its own value (`cc_sr = reset ? 0 : cc_sr`, a combinational loop; Quartus makes constant zeros
         of it). The register is one bit, CC_SR1, and CC_SR is its byte - four edits, counted.
       - DSP.vhd's three BRR prediction filters: `0 - P0` (integer literal minus signed) is converted by GHDL 6.0.0 to `P0 - 0`; written as a unary minus - three edits, counted.
     The memories (Altera megafunction wrappers in the original) and the Game Genie stub come from cores/snes/ip/snes_ip.vhd.
  2. The VHDL is converted once by GHDL 6.0.0 (`--synth --out=verilog`, in the container tools/ghdl6 builds). The SHA-256 of the VHDL input (the
     scratch copy, the generics) is recorded; `--check` compares it. The raw GHDL output is kept as gen/snes_ghdl.v.gz.
  3. The converted Verilog gets the clock-enable form (the machine was written for one 21.477 MHz clock, MCLK, and DSPCLK which is the same net; the
     host has one 100 MHz clock): every module's clock port X becomes the pair X_cep (a rising edge of X is now) and X_cen (a falling edge), every
     `always @(posedge X)` becomes `always @(posedge fclk) if (X_cep)` (`negedge`: X_cen), every instance passes the pair and `fclk` on, an asynchronous
     reset becomes a reset of fclk. cores/snes/trx/snes_core.v makes the enables.
  4. The memory initialisers that only write zeros become a loop.
Nothing else is touched. gen/ is committed (the CI has no checkout of the upstream and no GHDL).
  tools/snes_gen.py            write cores/snes/gen from rtl/ (GHDL only if its input changed; `--ghdl` forces it)
  tools/snes_gen.py --check    fail if rtl/ is not what was vendored (SHA256SUMS), the VHDL input is not what was converted, or gen/ is not
                               what the raw conversion makes (the lint stage runs this; no GHDL needed)
"""
import gzip, hashlib, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTL = os.path.join(ROOT, "cores/snes/rtl")
IP = os.path.join(ROOT, "cores/snes/ip/snes_ip.vhd")
SYS = os.path.join(ROOT, "cores/snes/ip/snes_sys.vhd")
GEN = os.path.join(ROOT, "cores/snes/gen")
RAWGZ = os.path.join(GEN, "snes_ghdl.v.gz")
OUTV = os.path.join(GEN, "snes.v")
INSHA = os.path.join(GEN, "snes.vhd.sha256")
GENERICS = []
TOP = "snes_sys"
CLOCKS = ["MCLK", "DSPCLK"]
GHDL_VERSION = "GHDL 6.0.0"
LOG = {}
TIED = set()


def base(n):
    return n.split("_B")[0]                       # GHDL 6 names a module <entity>_B<architecture>_<generics>


def count(name, n=1):
    LOG[name] = LOG.get(name, 0) + n


def die(msg):
    raise SystemExit("snes_gen: " + msg)


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
        die("rtl/ is not what tools/vendor_snes.sh vendored: %s" % ", ".join(bad[:8]))


# ------------------------------------------------------------------------------------------------------------- VHDL side
CASE_RE = re.compile(r"^\s*case\b.*\bis\s*$", re.I)
ENDCASE_RE = re.compile(r"^\s*end\s+case\b", re.I)
WHEN_RE = re.compile(r"^(\s*)when\s+(.*?)\s*=>(.*)$", re.I)
OTHERS_NULL_RE = re.compile(r"^\s*when\s+others\s*=>\s*null\s*;\s*$", re.I)
BIN_RE = re.compile(r'^"([01]+)"$|^\'([01])\'$')


def exhaustive_cases(text, name):
    """`when others => null;` after a complete list of binary choices -> the last listed choice becomes `when others =>`."""
    lines = text.split("\n")
    stack = []                                   # per open case: {choices, last_when (line index), ok}
    drop = set()
    change = {}
    for i, raw in enumerate(lines):
        ln = raw.split("--")[0]
        if CASE_RE.match(ln):
            stack.append({"ch": [], "last": None, "ok": True, "null": None, "oth": False})
            continue
        if not stack:
            continue
        if ENDCASE_RE.match(ln):
            fr = stack.pop()
            if (fr["null"] is not None or not fr["oth"]) and fr["ok"] and fr["last"] is not None and fr["ch"]:
                widths = {len(c) for c in fr["ch"]}
                if len(widths) == 1 and len(set(fr["ch"])) == 2 ** widths.copy().pop() and len(fr["ch"]) == len(set(fr["ch"])):
                    if fr["null"] is not None:
                        drop.add(fr["null"])
                    change[fr["last"]] = True
                    count("case with all binary choices and no other branch: last choice is others" if fr["null"] is None else
                          "case with all binary choices: others => null -> last choice is others")
            continue
        if OTHERS_NULL_RE.match(ln):
            stack[-1]["null"] = i
            stack[-1]["oth"] = True
            continue
        m = WHEN_RE.match(ln)
        if m and re.match(r"^\s*when\b", ln):
            # a `when` of the innermost case only: nested statements are inside their own case frames
            parts = [p.strip() for p in m.group(2).split("|")]
            fr = stack[-1]
            if "others" in [p.lower() for p in parts]:
                fr["oth"] = True
                continue
            for p in parts:
                bm = BIN_RE.match(p)
                if not bm:
                    fr["ok"] = False
                else:
                    fr["ch"].append(bm.group(1) or bm.group(2))
            fr["last"] = i
    if not drop and not change:
        return text
    out = []
    for i, raw in enumerate(lines):
        if i in drop:
            continue
        if i in change:
            raw = re.sub(r"^(\s*when\s+).*?(\s*=>)", lambda mo: mo.group(1) + "others" + mo.group(2), raw, count=1, flags=re.I)
        out.append(raw)
    return "\n".join(out)


def sub1(text, old, new, name, n=1):
    c = text.count(old)
    if c != n:
        die("%s: expected %d x %r, found %d" % (name, n, old[:60], c))
    count(name, c)
    return text.replace(old, new)


def patched_vhdl():
    """{published name: text} of the VHDL that goes to GHDL, the changes applied."""
    files = {}
    for d, _, fs in os.walk(RTL):
        for f in sorted(fs):
            if f.endswith(".vhd"):
                p = os.path.relpath(os.path.join(d, f), RTL)
                t = open(os.path.join(d, f)).read()
                t = exhaustive_cases(t, p)
                files["rtl_" + p.replace("/", "_")] = t
    # the boot ROM of the S-SMP as a RAM with a write port (see the docstring)
    smp = "rtl_SMP.vhd"
    files[smp] = sub1(files[smp], 'constant  IPLROM: IplRom_t := (others => x"00");   -- the 64 byte boot ROM is not in this repository: tools/vendor_snes.sh cut it out (T142 D9)',
                      'signal  IPLROM: IplRom_t := (others => x"00");', "SMP: IPLROM constant -> signal")
    files[smp] = sub1(files[smp], "\t\tSS_DO           : out std_logic_vector(7 downto 0)\n\t);\nend SMP;",
                      "\t\tSS_DO           : out std_logic_vector(7 downto 0);\n\t\tIPL_WR          : in std_logic;\n\t\tIPL_WA          : in std_logic_vector(5 downto 0);\n"
                      "\t\tIPL_WD          : in std_logic_vector(7 downto 0)\n\t);\nend SMP;", "SMP: IPL write port")
    files[smp] = sub1(files[smp], "\tSS_DO <= SS_SMP_DO;\n\nend rtl;",
                      "\tSS_DO <= SS_SMP_DO;\n\n\tprocess(CLK)\n\tbegin\n\t\tif rising_edge(CLK) then\n\t\t\tif IPL_WR = '1' then\n"
                      "\t\t\t\tIPLROM(to_integer(unsigned(IPL_WA))) <= IPL_WD;\n\t\t\tend if;\n\t\tend if;\n\tend process;\n\nend rtl;", "SMP: IPL write process")
    sn = "rtl_SNES.vhd"
    files[sn] = sub1(files[sn], "\t\tAUDIO_R\t\t: out std_logic_vector(15 downto 0)\n\t);\nend SNES;",
                     "\t\tAUDIO_R\t\t: out std_logic_vector(15 downto 0);\n\n\t\tIPL_WR\t\t: in std_logic;\n\t\tIPL_WA\t\t: in std_logic_vector(5 downto 0);\n"
                     "\t\tIPL_WD\t\t: in std_logic_vector(7 downto 0)\n\t);\nend SNES;", "SNES: IPL write port")
    files[sn] = sub1(files[sn], "\t\tSS_DO\t\t=> SS_SMP_DO\n\t);",
                     "\t\tSS_DO\t\t=> SS_SMP_DO,\n\n\t\tIPL_WR\t\t=> IPL_WR,\n\t\tIPL_WA\t\t=> IPL_WA,\n\t\tIPL_WD\t\t=> IPL_WD\n\t);", "SNES: IPL port map")
    lh = "rtl_chip_DSP_DSP_LHRomMap.vhd"
    files[lh] = sub1(files[lh], "\tsignal CC_SR   \t\t: std_logic_vector(7 downto 0);\n", "\tsignal CC_SR   \t\t: std_logic_vector(7 downto 0);\n\tsignal CC_SR1  \t\t: std_logic;\n", "LHRomMap: CC_SR1 signal")
    files[lh] = sub1(files[lh], "\t\t\tCC_SR <= (others => '0');\n", "\t\t\tCC_SR1 <= '0';\n", "LHRomMap: CC_SR reset")
    files[lh] = sub1(files[lh], "CC_SR(1) <= CC_TM_ON;", "CC_SR1 <= CC_TM_ON;", "LHRomMap: CC_SR bit")
    files[lh] = sub1(files[lh], "\tIRQ_N <= '1';\n\nend rtl;", "\tIRQ_N <= '1';\n\tCC_SR <= \"000000\" & CC_SR1 & \"0\";\n\nend rtl;", "LHRomMap: CC_SR byte")
    # GHDL 6.0.0 converts `0 - X` with X signed (an integer literal on the left) to `X - 0`: the three BRR prediction filters of the S-DSP (DSP.vhd, `0 - P0`) lost the
    # sign of their feedback term, the voices of filter 1..3 came out as their residual (found with the BRR decoder bench, T142 audio). The unary minus is converted right.
    dsp = "rtl_DSP.vhd"
    files[dsp] = sub1(files[dsp], "shift_right((0-P0), 5)", "shift_right((-P0), 5)", "DSP: BRR filter 1 sign")
    files[dsp] = sub1(files[dsp], "shift_right(0 - (P0 + (P0*2)),6)", "shift_right(-(P0 + (P0*2)),6)", "DSP: BRR filter 2 sign")
    files[dsp] = sub1(files[dsp], "shift_right(0 - (P0 + (P0*4) + (P0*8)),7)", "shift_right(-(P0 + (P0*4) + (P0*8)),7)", "DSP: BRR filter 3 sign")
    # T218: the DSP-n coprocessor (DSPn.vhd). The original keeps all firmwares in one 8096 x 24 program ROM and 7168 x 16 data ROM (the .mif are chip dumps, not in this
    # tree); here the chip has the one firmware the application writes (2048 x 24 program, 1024 x 16 data, written through FW_*; block RAM, 3 tiles with the data RAM).
    # Only DSP-1/1B/2/3/4 (VER < 4); ST010/ST011 (VER(2) = 1: 11 bit data pointer) are not built, so their 8 deep stack and wide data RAM stay in the source and are never selected.
    dn = "rtl_chip_DSP_DSPn.vhd"
    files[dn] = sub1(files[dn], "\t\tSS_DO\t\t\t: out std_logic_vector(7 downto 0)\n\t);\nend DSPn;",
                     "\t\tSS_DO\t\t\t: out std_logic_vector(7 downto 0);\n\t\tFW_WR\t\t: in std_logic;\n\t\tFW_SEL\t\t: in std_logic;\n"
                     "\t\tFW_A\t\t: in std_logic_vector(10 downto 0);\n\t\tFW_D\t\t: in std_logic_vector(23 downto 0)\n\t);\nend DSPn;", "DSPn: firmware write port")
    a = files[dn].index('\tPROG_ROM_ADDR <= std_logic_vector(unsigned(PC) + ("0"&x"000"))')
    b0 = '\t\tq\t\t\t=> PROG_ROM_Q\n\t);\n'
    b = files[dn].index(b0, a) + len(b0)
    files[dn] = files[dn][:a] + (
        "\tPROG_ROM_ADDR <= \"00\" & FW_A when FW_WR = '1' else \"00\" & PC;\n"
        "\tPROG_ROM : entity work.spram_sz generic map(11, 24, 2048)\n\tport map(\n\t\tclock\t\t=> CLK,\n\t\taddress\t=> PROG_ROM_ADDR(10 downto 0),\n"
        "\t\tdata\t\t=> FW_D,\n\t\twren\t\t=> FW_WR and not FW_SEL,\n\t\tq\t\t\t=> PROG_ROM_Q\n\t);\n") + files[dn][b:]
    count("DSPn: program ROM -> 2048 x 24 firmware RAM")
    a = files[dn].index('\tDATA_ROM_ADDR <= std_logic_vector(unsigned(RP( 9 downto 0)) + ("0"&x"000"))')
    b0 = '\t\tq\t\t\t=> DATA_ROM_Q\n\t);\n'
    b = files[dn].index(b0, a) + len(b0)
    files[dn] = files[dn][:a] + (
        "\tDATA_ROM_ADDR <= \"000\" & FW_A(9 downto 0) when FW_WR = '1' else \"000\" & RP(9 downto 0);\n"
        "\tDATA_ROM : entity work.spram_sz generic map(10, 16, 1024)\n\tport map(\n\t\tclock\t\t=> CLK,\n\t\taddress\t=> DATA_ROM_ADDR(9 downto 0),\n"
        "\t\tdata\t\t=> FW_D(15 downto 0),\n\t\twren\t\t=> FW_WR and FW_SEL,\n\t\tq\t\t\t=> DATA_ROM_Q\n\t);\n") + files[dn][b:]
    count("DSPn: data ROM -> 1024 x 16 firmware RAM")
    files[dn] = sub1(files[dn], 'DATA_RAM_ADDR_A or x"40";', 'DATA_RAM_ADDR_A or ("000" & x"40");', "DSPn: 8 bit literal or-ed to an 11 bit address (GHDL refuses the lengths)")
    files[lh] = sub1(files[lh], "\t\tSS_DO\t\t\t: out std_logic_vector(7 downto 0)\n\t);\nend DSP_LHRomMap;",
                     "\t\tSS_DO\t\t\t: out std_logic_vector(7 downto 0);\n\t\tFW_WR\t\t: in std_logic;\n\t\tFW_SEL\t\t: in std_logic;\n"
                     "\t\tFW_A\t\t: in std_logic_vector(10 downto 0);\n\t\tFW_D\t\t: in std_logic_vector(23 downto 0)\n\t);\nend DSP_LHRomMap;", "LHRomMap: firmware port")
    files[lh] = sub1(files[lh], "\t\tSS_DI\t\t\t=> SS_DI,\n\t\tSS_DO\t\t\t=> SS_DO\n\t);\n\tend generate;",
                     "\t\tSS_DI\t\t\t=> SS_DI,\n\t\tSS_DO\t\t\t=> SS_DO,\n\t\tFW_WR\t\t=> FW_WR,\n\t\tFW_SEL\t\t=> FW_SEL,\n"
                     "\t\tFW_A\t\t=> FW_A,\n\t\tFW_D\t\t=> FW_D\n\t);\n\tend generate;", "LHRomMap: firmware port map")
    files["snes_ip.vhd"] = open(IP).read()
    files["snes_sys.vhd"] = open(SYS).read()
    return files


def vhdl_hash(files):
    h = hashlib.sha256()
    for p in sorted(files):
        h.update(p.encode() + b"\0" + files[p].encode() + b"\0")
    h.update(" ".join(GENERICS).encode())
    return h.hexdigest()


def run_ghdl(files):
    d = tempfile.mkdtemp(prefix="snes_gen_")
    try:
        for p, t in files.items():
            open(os.path.join(d, p), "w").write(t)
        g = " ".join("-g" + x for x in GENERICS)
        script = ("cd /w; ghdl --version | head -1 > ver.txt; "
                  "mkdir wk; F='--std=08 -frelaxed -fsynopsys --workdir=wk'; "
                  "ghdl -i $F $(find . -name '*.vhd' | sort) > i.log 2>&1 && ghdl -m $F %s > m.log 2>&1; "
                  "ghdl --synth $F --out=verilog %s %s > out.v 2> err.log || { cat err.log; chmod -R a+rwX /w; exit 1; }; chmod -R a+rwX /w" % (TOP, g, TOP))
        img = os.environ.get("SNES_GHDL_IMAGE", "trx-ghdl6")
        runner = os.environ.get("SNES_CONTAINER", "container")
        r = subprocess.run(["nice", "-n", "15", runner, "run", "--rm", "--cpus", "4", "-m", "12G", "-v", d + ":/w", img, "sh", "-c", script])
        if r.returncode != 0 or not os.path.exists(os.path.join(d, "out.v")) or os.path.getsize(os.path.join(d, "out.v")) == 0:
            die("the GHDL conversion failed (see above)")
        ver = open(os.path.join(d, "ver.txt")).read().strip()
        if not ver.startswith(GHDL_VERSION):
            die("the image's GHDL is %r, tools/snes_gen.py wants %s" % (ver, GHDL_VERSION))
        return open(os.path.join(d, "out.v")).read()
    finally:
        shutil.rmtree(d, ignore_errors=True)


# ------------------------------------------------------------------------------------------------------------ Verilog side
MOD_RE = re.compile(r"^module (\w+)\s*$")
SENS_RE = re.compile(r"^(\s*)always @\((posedge|negedge) (\w+)\)\s*$")
ASYNC_RE = re.compile(r"^(\s*)always @\((posedge|negedge) (\w+) or (?:posedge|negedge) (\w+)\)\s*$")


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
                cm = re.match(r"^\s*\.(\\?[\w$]+(?:\[[\w$]+\])?)\s*\((.*?)\)([,)]*);?\s*$", body[j])
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
                        if e not in inputs[m]:
                            die("module %s: a clock of %s is driven by %r, which is not an input port" % (m, sm, e))
                        if e not in clk[m]:
                            clk[m].add(e)
                            changed = True
    top = TOP
    if top not in mods:
        die("no module %s in the conversion (modules: %s)" % (top, ", ".join(sorted(mods)[:10])))
    if set(clk[top]) != set(CLOCKS):
        die("the top's clocks are %s, expected %s" % (sorted(clk[top]), sorted(CLOCKS)))
    # a clock port may not be used as data
    for m, b in mods.items():
        txt = "\n".join(b[header_end(b) + 1:])
        for x in clk[m]:
            uses = len(re.findall(r"(?<![.\w])%s\b" % re.escape(x), txt))
            ok = len(re.findall(r"(?:pos|neg)edge %s\b" % re.escape(x), txt)) + sum(
                1 for (_, _, sm, _, cs) in inst[m] for (p, e, _) in cs if e == x)
            if uses != ok:
                die("module %s: clock %s is also used as data (%d uses, %d clock uses)" % (m, x, uses, ok))
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
        text = "\n".join(b)
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
        for (s_, e_, sm, iname, conns) in reversed(find_instances(b, mods)):
            for (p, ex, ln) in conns:
                if p not in clk[sm] and ex in clk[m]:
                    # a clock net on a port of a module that has no flip-flop on it (the Game Genie stub's clk): the port is not read
                    b[ln] = re.sub(r"\.%s\(%s\)" % (re.escape(p), re.escape(ex)), ".%s(1'b0)" % p, b[ln])
                    count("clock on an unused port -> 0")
            if not clk[sm]:
                continue
            for (p, ex, ln) in conns:
                if p in clk[sm]:
                    b[ln] = re.sub(r"\.%s\(%s\)" % (p, re.escape(ex)), ".%s_cep(%s_cep), .%s_cen(%s_cen)" % (p, ex, p, ex), b[ln])
                    count("clock pair passed to an instance")
            b[s_] = re.sub(r"\($", "(\n    .fclk(fclk),", b[s_])
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
        # the microcode ROMs (65C816: 2048 x 53, SPC700: 4096 x 32) are read into a register: block RAM, not 4,000 LUTs of logic
        def rom_repl(mo):
            if int(mo.group(3)) + 1 < 1024:
                return mo.group(0)
            count("microcode ROM -> rom_style block")
            return '  (* rom_style = "block" *) ' + mo.group(0).lstrip()
        text = re.sub(r"^  reg \[(\d+):0\] (\w+)\[(\d+):0\] ; // memory\n(?=  initial begin\n    \w+\[\d+\] = \d+'b[01X]+;)", rom_repl, text, flags=re.M)
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
    res = "// T142: tools/snes_gen.py made this file from SNES_MiSTer's SNES machine (cores/snes/rtl, GPL v3) by GHDL, then put every\n" \
          "// clock into the enable form (fclk, X_cep, X_cen). Do not edit; gen/snes.vhd.sha256 records what it was made from.\n" + res
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
            die("the VHDL input (rtl/ with the listed edits, ip/, the generics) is not what gen/ was converted from (run tools/snes_gen.py)")
        raw = gzip.open(RAWGZ, "rt").read()
        if hashlib.sha256(raw.encode()).hexdigest() != have[1]:
            die("gen/snes_ghdl.v.gz is not the conversion the hash file records")
        out = transform(raw)
        if not os.path.exists(OUTV) or open(OUTV).read() != out:
            die("gen/snes.v is not what the raw conversion makes (run tools/snes_gen.py)")
        print("snes_gen: rtl/ is the vendored tree, gen/ is made from it (%d bytes of Verilog)" % len(out))
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
        print("  %-64s %d" % (k, LOG[k]))
    print("snes_gen: wrote %s (%d lines)" % (OUTV, out.count("\n")))


if __name__ == "__main__":
    main()
