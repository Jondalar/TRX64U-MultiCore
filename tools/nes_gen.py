#!/usr/bin/env python3
"""T95 D4/D12: cores/nes/gen = MiSTer-devel/NES_MiSTer (cores/nes/rtl, byte for byte as tools/vendor_nes_mister.sh copied it,
GPL v3) plus the mechanical changes below, each checked for the number of places it hit, so that a changed upstream fails
here instead of building something else.

  1. A clock enable `ce_in` on every clocked process of the machine (`always @(posedge clk) <stmt>` becomes
     `always @(posedge clk) if (ce_in) <stmt>`), a port `ce_in` on every module and `.ce_in(ce_in)` on every instance. The
     machine was written for a 21.477 MHz master clock; it runs on the partition's 100 MHz clock, and `ce_in` is that master
     clock as an enable (cores/nes/trx/nes_core.v). The 6502 takes the same enable through its own `Enable`.
  2. The MiSTer savestate registers (`eReg_SavestateV`, ~90 instances) are replaced by what they hold when nothing ever
     loads a state: their default value. The framework behind them (savestates.vhd, statemanager.vhd, the bus) is not carried;
     the Freeze of this core is the machine's own `pausecore`/`corepaused` handshake.
  3. cart.sv resolves the mappers on wired-or nets (`tri0`/`tri1`, twelve of them). Vivado does not synthesise that. Every
     mapper that is built gets its own wire for each of those nets, drives the pull value (0, or all ones on the data buses)
     where it used to drive Z, and the net is the OR (AND on the data buses) of the wires: the explicit multiplexer.
  4. Mappers that are not built (BUILT below) are cut out of cart.sv; the three outputs they alone drove (EEPROM bus, mapper
     413 address extension, FDS disk side) are tied off. Modules that nothing reaches from `NES` are not written at all.
  5. nes.v: the cheat module and the composite board are cut out, the 6502 instance becomes our wrapper `T65_nes`, and the
     machine gets one more output, `px_ce_o` (a PPU dot has just been produced, the glue's pixel strobe).
  6. The VHDL 6502 (rtl/t65) is converted to Verilog once by GHDL (`--synth --out=verilog`, in a container), so Verilator and
     Vivado read the same source. The input of that conversion (the VHDL after the mechanical savestate cut and our wrapper)
     is made here without GHDL and its SHA-256 is recorded next to the result; `--check` compares that hash, `--vhdl` runs
     the conversion.

rtl/ is never edited; gen/ is generated and committed (the CI has no checkout of NES_MiSTer and no GHDL).
  tools/nes_gen.py          write cores/nes/gen (the 6502 conversion only if its input changed: `--vhdl` forces it)
  tools/nes_gen.py --check  fail if rtl/ is not what was vendored (SHA256SUMS) or gen/ is not what rtl/ makes
  tools/nes_gen.py --vhdl   (re)run the GHDL conversion of the 6502 in a container (`container` CLI, image debian:trixie-slim)
  tools/nes_gen.py --mappers-all DIR   for tools/nes_area.sh: every mapper file with the same savestate and tri-state changes, no
                            pruning, written to DIR (never read by the build)
"""
import hashlib, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTL = os.path.join(ROOT, "cores/nes/rtl")
GEN = os.path.join(ROOT, "cores/nes/gen")
T65V = "t65/t65_nes.v"
T65H = "t65/t65_nes.vhd.sha256"

# the mappers that are built: instance name in cart.sv -> module (M4: the subset that fits, docs/specs/T95-nes-core.md 5.8)
BUILT = {"map28": "Mapper28", "mmc1": "MMC1", "mmc2": "MMC2", "mmc4": "MMC4", "mmc3": "MMC3", "vrc24": "VRC24",
         "map66": "Mapper66", "map34": "Mapper34", "map71": "Mapper71", "map79": "Mapper79"}
# the wired-or nets of cart.sv and what they pull to when nobody drives them
WIRED = {"prg_allow_b": 1, "vram_a10_b": 1, "vram_ce_b": 1, "chr_allow_b": 1, "irq_b": 1,
         "prg_addr_b": 22, "chr_addr_b": 22, "flags_out_b": 16, "audio_out_b": 16, "prg_dout_b": 8, "chr_dout_b": 8}
PULL1 = {"prg_dout_b", "chr_dout_b"}          # tri1: pulled to all ones
LOG = {}


def count(name, n=1):
    LOG[name] = LOG.get(name, 0) + n


def sub1(text, old, new, name, n=1):
    c = text.count(old)
    if c != n:
        raise SystemExit("nes_gen: %s: expected %d x %r, found %d" % (name, n, old, c))
    count(name, c)
    return text.replace(old, new)


def resub(text, pat, repl, name, n=None, flags=re.M | re.S):
    text, c = re.subn(pat, repl, text, flags=flags)
    if n is not None and c != n:
        raise SystemExit("nes_gen: %s: expected %d x %r, found %d" % (name, n, pat, c))
    count(name, c)
    return text


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
            have[p] = hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest()
    if have != want:
        raise SystemExit("nes_gen: cores/nes/rtl is not what tools/vendor_nes_mister.sh vendored (SHA256SUMS): %s"
                         % sorted(set(have.items()) ^ set(want.items())))


def read(rel):
    return open(os.path.join(RTL, rel), encoding="utf-8").read()


# ------------------------------------------------------------------------------------------------ balanced text helpers
def match_paren(t, i):
    """index of the ')' that closes the '(' at t[i]"""
    assert t[i] == "("
    d = 0
    for j in range(i, len(t)):
        if t[j] == "(":
            d += 1
        elif t[j] == ")":
            d -= 1
            if d == 0:
                return j
    raise SystemExit("nes_gen: unbalanced parenthesis")


def split_args(s):
    out, d, cur = [], 0, ""
    for ch in s:
        if ch in "([{":
            d += 1
        elif ch in ")]}":
            d -= 1
        if ch == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), t, flags=re.S)
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), t)


# ------------------------------------------------------------------------------------------------ 2: savestate registers
def cut_savestate_regs(t, fname):
    """eReg_SavestateV #(IDX, DEF) name (clk, Din, Adr, wren, rst, BUSDOUT, BACK, OUT);  ->  assigns"""
    out, pos = "", 0
    clean = strip_comments(t)
    for m in re.finditer(r"\beReg_SavestateV\s*#\s*\(", clean):
        if m.start() < pos:
            continue
        # a commented-out instance is blanked in `clean`, so a hit is real
        p1 = m.end() - 1
        p2 = match_paren(clean, p1)
        mm = re.compile(r"\s*\w+\s*\(").match(clean, p2 + 1)
        if not mm:
            raise SystemExit("nes_gen: %s: eReg_SavestateV without instance name" % fname)
        q1 = mm.end() - 1
        q2 = match_paren(clean, q1)
        end = clean.index(";", q2) + 1
        params = split_args(clean[p1 + 1:p2])
        args = split_args(clean[q1 + 1:q2])
        if len(params) != 2 or len(args) != 8:
            raise SystemExit("nes_gen: %s: eReg_SavestateV with %d params / %d args" % (fname, len(params), len(args)))
        # the line's own indentation is kept
        ls = t.rfind("\n", 0, m.start()) + 1
        indent = t[ls:m.start()]
        assert indent.strip() == "", (fname, indent)
        out += t[pos:ls]
        out += "%sassign %s = 64'd0;  // T95: savestate bus (no framework)\n%sassign %s = %s;  // the register's value when nothing loads a state\n" % (
            indent, args[5], indent, args[7], params[1])
        pos = end
        # swallow the rest of the line
        nl = t.find("\n", end)
        pos = nl + 1 if nl >= 0 else len(t)
        count("savestate registers")
    return out + t[pos:]


# ------------------------------------------------------------------------------------------------ module bookkeeping
MOD_RE = re.compile(r"^module\s+(\w+)", re.M)


def split_modules(t):
    """-> (prelude, [(name, text)]) ; text of a module includes the comment lines directly above it"""
    ms = list(MOD_RE.finditer(t))
    if not ms:
        return t, []
    ends = [m for m in re.finditer(r"^endmodule\b[^\n]*\n?", t, re.M)]
    if len(ends) != len(ms):
        raise SystemExit("nes_gen: module/endmodule mismatch")
    mods = []
    prev_end = 0
    prelude = None
    for m, e in zip(ms, ends):
        # comment/blank lines directly above `module`
        start = m.start()
        lines_start = start
        while True:
            pl = t.rfind("\n", 0, lines_start - 1)
            line = t[pl + 1:lines_start]
            if lines_start > prev_end and re.match(r"^\s*(//.*)?$", line) and pl + 1 >= prev_end:
                lines_start = pl + 1
                if lines_start <= prev_end:
                    break
            else:
                break
        if prelude is None:
            prelude = t[:lines_start]
        mods.append((m.group(1), t[max(lines_start, prev_end):e.end()]))
        prev_end = e.end()
    return prelude, mods


# ------------------------------------------------------------------------------------------------ nets assigned in processes
# ------------------------------------------------------------------------------------------------ implicit nets
DECL_RE = re.compile(r"^([ \t]{0,1})(wire|logic|reg)(\s+(?:signed\s+)?(?:\[[^\]]*\]\s*)?)(\w+)\s*(;|=)", re.M)


def _implicit_use(clean, hdr):
    """earliest position per identifier in a context that creates an implicit net: a continuous
    assign, or an instance's port connection. A use inside a process does not - there the later
    module-scope declaration still applies."""
    first = {}
    spans = [m.span() for m in re.finditer(r"^[ \t]*assign\b[^;]*;", clean, re.M)]
    spans += [m.span(1) for m in re.finditer(r"\.\w+\s*\(([^()]*)\)", clean)]
    for a, b in spans:
        if b <= hdr:
            continue
        for u in re.finditer(r"(?<![\w.$`'])([A-Za-z_]\w*)", clean[a:b]):
            nm = u.group(1)
            pos = a + u.start()
            if nm not in first or pos < first[nm]:
                first[nm] = pos
    return first


def hoist_implicit(t):
    """Vivado scopes an implicit net at the point of use: a bus used in an `assign` or a port
    connection above its own declaration becomes a 1-bit net there, and the declaration below is
    then "already implicitly declared" (run 41: WARNING [Synth 8-8895] for oam_bus, vram_din,
    ppu_dbus, color0 and four more in ppu.sv). A warning, not an error - so it would have reached
    the board as wrong hardware. Quartus and Verilator read the module's scope as a whole.
    Every such declaration moves above its first use; an initialiser stays behind as an assign."""
    prelude, mods = split_modules(t)
    out = [prelude]
    n = 0
    for name, mt in mods:
        clean = strip_comments(mt)
        m = re.search(r"^module\s+%s\b" % re.escape(name), clean, re.M)
        if not m:
            out.append(mt)
            continue
        i = clean.find("(", m.end())
        hdr = clean.index(";", match_paren(clean, i) if i >= 0 else m.end()) + 1
        first = _implicit_use(clean, hdr)
        moved = []
        drop = []
        for d in DECL_RE.finditer(clean):
            nm = d.group(4)
            if nm not in first or first[nm] >= d.start():
                continue
            kind, rng = d.group(2), d.group(3)
            # the whole declaration moves, initialiser and all: a variable's initialiser is its
            # power-up value and must not become a continuous assign (the first version split it
            # and the pause bench failed - div_sys got a second driver next to its process).
            end = clean.index(";", d.end()) + 1 if d.group(5) == "=" else d.end()
            decl = clean[d.start():end].strip()
            repl = ""
            moved.append("%s  // T95: hoisted above its first use (implicit net)\n" % decl)
            drop.append((d.start(), end, repl))
            n += 1
        if not drop:
            out.append(mt)
            continue
        res, pos = "", 0
        for a, b, repl in drop:
            res += mt[pos:a] + repl
            pos = b
            if not repl:                              # swallow the line the declaration had to itself
                nl = mt.find("\n", b)
                pos = nl + 1 if nl >= 0 else b
        res += mt[pos:]
        k = res.index(";", match_paren(res, res.index("(", res.index("module " + name))) ) + 1
        res = res[:k] + "\n" + "".join(moved) + res[k:]
        out.append(res)
    count("declarations hoisted", n)
    return "".join(out)


def proc_driven(clean, name):
    """Is `name` the target of a procedural assignment? A non-blocking <= is only legal in a process,
    and a blocking = counts when the line does not start with `assign`. Run 41: the first version
    anchored the match at the start of a line, so `if (Savestate_OAMRdEn) Savestate_OAMReadData <= ...`
    (ppu.sv:627) was missed and Vivado stopped with [Synth 8-2577] procedural assignment to a
    non-register."""
    if re.search(r"(?<![\w.$`])%s\s*(?:\[[^\]]*\]\s*)*<=(?!=)" % name, clean):
        return True
    return bool(re.search(r"^[ \t]*(?:[^\n;?]*:\s*)?%s\s*(?:\[[^\]]*\]\s*)*=(?!=)" % name, clean, re.M))


def proc_wires(t):
    """Quartus lets a process drive a `wire`; the language (and Verilator, and Vivado) wants a variable: wire -> logic"""
    clean = strip_comments(t)
    n = 0
    for m in list(re.finditer(r"^([ \t]*)wire(\s+(?:signed\s+)?(?:\[[^\]]*\]\s*)?)(\w+)\s*;", clean, re.M)):
        name = m.group(3)
        if proc_driven(clean, name):
            t = t.replace(m.group(0), "%slogic%s%s;" % (m.group(1), m.group(2), name), 1)
            n += 1
    for m in list(re.finditer(r"^([ \t]*output)(\s+(?:signed\s+)?(?:\[[^\]]*\]\s*)?)(\w+)\s*([,)]|$|//)", clean, re.M)):
        name = m.group(3)
        if name in ("reg", "wire", "logic", "signed"):
            continue
        if proc_driven(clean, name):
            t = t.replace(m.group(0), "%s logic%s%s%s" % (m.group(1), m.group(2), name, m.group(4)), 1)
            n += 1
    count("wire -> logic", n)
    return t


# ------------------------------------------------------------------------------------------------ 1: the clock enable
ALWAYS_RE = re.compile(r"(\balways(?:_ff)?\s*@\s*\(\s*posedge\s+(?:clk|clk20)\s*\))")


def gate_processes(t):
    t, n = ALWAYS_RE.subn(r"\1 if (ce_in)", t)
    count("clocked processes", n)
    return t


def add_port(t, name):
    m = re.search(r"^module\s+%s\b" % name, t, re.M)
    i = m.end()
    # skip an optional #( ... ) parameter list
    j = i
    while t[j] in " \t\r\n":
        j += 1
    if t[j] == "#":
        j = t.index("(", j)
        j = match_paren(t, j) + 1
        while t[j] in " \t\r\n":
            j += 1
    if t[j] != "(":
        raise SystemExit("nes_gen: module %s: no ANSI port list" % name)
    count("module ports")
    return t[:j + 1] + "\n\tinput ce_in," + t[j + 1:]


def add_instance_arg(t, mods):
    """`.ce_in(ce_in)` first in every instance of a module of ours (not of T65_nes: its Enable carries the clock enable)"""
    clean = strip_comments(t)
    out, pos = "", 0
    names = "|".join(sorted(mods, key=len, reverse=True))
    pat = re.compile(r"^[ \t]*(%s)\b\s*(?:#\s*\()?" % names, re.M)
    for m in pat.finditer(clean):
        if m.start() < pos or clean.startswith("module", m.start() + len(m.group(0)) - len(m.group(1)) - 0):
            continue
        j = m.end()
        if clean[j - 1] == "(":                      # parameter override: skip to its end
            j = match_paren(clean, j - 1) + 1
        mm = re.compile(r"\s*(\w+)\s*(?:\[[^\]]*\]\s*)?\(").match(clean, j)
        if not mm:
            continue
        if m.group(1) in ("module", "endmodule"):
            continue
        k = mm.end()
        named = re.match(r"\s*\.", clean[k:]) is not None
        out += t[pos:k] + ("\n\t.ce_in(ce_in)," if named else " ce_in,")
        pos = k
        count("instances")
    return out + t[pos:]


# ------------------------------------------------------------------------------------------------ 3: tri0/tri1 in the mappers
def untri_mapper(t):
    def zrep(m):
        net = m.group(1)
        w = m.group(3)
        return m.group(0)
    # assign X_b = enable ? V : N'hZ;
    def f(m):
        port, expr, width = m.group(1), m.group(2), m.group(3)
        pull = "{%s{1'b1}}" % width if port in ("prg_dout_b", "chr_dout_b") else "%s'd0" % width
        count("Z drivers")
        return "assign %s = %s : %s;" % (port, expr.rstrip(), pull)
    t = re.sub(r"assign\s+(\w+_b)\s*=\s*(enable\s*\?[^\n]*?)\s*:\s*(\d+)'[hH][zZ]\s*;", lambda m: f(m).replace("assign %s = " % m.group(1), "assign %-12s = " % m.group(1)), t)
    t, n = re.subn(r"\binout\b(\s+(?:\[[^\]]*\]\s*)?\w+_b\b)", r"output\1", t)
    count("inout ports", n)
    if re.search(r"'[hH][zZ]|'bz", t):
        raise SystemExit("nes_gen: a Z driver is left in a mapper: %s" % re.search(r".*'[hH][zZ].*", t).group(0))
    return t


# ------------------------------------------------------------------------------------------------ cart.sv
def prune_cart(t):
    # instance blocks at column 0: `Type name(` ... `);`
    blocks = list(re.finditer(r"^(\w+)\s+(\w+)\s*\(\s*\n.*?^\);\n", t, re.M | re.S))
    keep_mods = set(BUILT.values())
    out, pos = "", 0
    kept = []
    for b in blocks:
        mod, inst = b.group(1), b.group(2)
        if mod in ("module", "if", "else"):
            continue
        # the banner comment above the instance goes with it
        s = b.start()
        ban = re.search(r"((?:^//[^\n]*\n)+)(?:wire[^\n]*\n)*\Z", t[:s], re.M)
        s0 = ban.start() if ban else s
        # wires declared between banner and instance (enable decodes) are cheap and stay; only the instance goes
        if mod in keep_mods and BUILT.get(inst) == mod:
            kept.append((inst, mod))
            continue
        out += t[pos:b.start()]
        pos = b.end()
        count("instances cut")
    out += t[pos:]
    got = sorted(kept)
    want = sorted(BUILT.items())
    if got != want:
        raise SystemExit("nes_gen: cart.sv: built instances %s, expected %s" % (got, want))
    return out


def wire_or(t):
    """per-instance wires for the wired-or nets, and the OR / AND of them"""
    # instance blocks of the built mappers
    drivers = {n: [] for n in WIRED}
    def inst_fix(m):
        mod, inst, body = m.group(1), m.group(2), m.group(0)
        if inst not in BUILT:
            return body
        decl = ""
        for net, w in WIRED.items():
            pat = re.compile(r"(\.\w+\s*\(\s*)%s(\s*\))" % net)
            if pat.search(body):
                nn = "%s__%s" % (net, inst)
                body = pat.sub(r"\g<1>%s\g<2>" % nn, body)
                decl += "wire %s%s;\n" % ("[%d:0] " % (w - 1) if w > 1 else "", nn)
                drivers[net].append(nn)
                count("wired-or connections")
        return decl + body
    t = re.sub(r"^(\w+)\s+(\w+)\s*\(\s*\n.*?^\);\n", inst_fix, t, flags=re.M | re.S)
    # the declarations
    t = sub1(t, "tri0 prg_allow_b, vram_a10_b, vram_ce_b, chr_allow_b, irq_b;\ntri0 [21:0] prg_addr_b, chr_addr_b;\ntri0 [15:0] flags_out_b, audio_out_b;\ntri1 [7:0] prg_dout_b, chr_dout_b;\n",
             "".join("wire %s%s;\n" % ("[%d:0] " % (w - 1) if w > 1 else "", n) for n, w in WIRED.items())
             + "".join("assign %s = %s;\n" % (n, (" & " if n in PULL1 else " | ").join(drivers[n] or ([("{8{1'b1}}")] if n in PULL1 else ["%d'd0" % WIRED[n]])))
                       for n in WIRED), "wired-or declarations")
    return t


def fix_cart(t):
    t = prune_cart(t)
    t = wire_or(t)
    # the savestate OR trees and the ports that only the cut mappers drove
    t = resub(t, r"assign SaveStateBus_Dout\s*=.*?SaveStateBus_wired_or\[40\];", "assign SaveStateBus_Dout = 64'd0;", "savestate OR tree", 1)
    t = resub(t, r"assign Savestate_MAPRAMReadData\s*=[^;]*;", "assign Savestate_MAPRAMReadData = 8'd0;", "savestate RAM OR", 1)
    t = sub1(t, "\t{mapper_addr, mapper_data_out, mapper_prg_write, mapper_ovr} = (me[159] | me[16]) ?\n\t\t{map16_mapper_addr, map16_data_out, map16_prg_write, map16_ovr} : 28'd0;",
             "\t{mapper_addr, mapper_data_out, mapper_prg_write, mapper_ovr} = 28'd0;", "EEPROM bus tie-off")
    t = sub1(t, "\tprg_aout[24:22] = me[413] ? prg_aoute_m413 : 3'd0;", "\tprg_aout[24:22] = 3'd0;", "mapper 413 tie-off")
    t = sub1(t, "tri0 [1:0] fds_diskside;\n", "", "FDS net")
    t = sub1(t, "\t{diskside} = {fds_diskside};", "\t{diskside} = 2'd0;", "FDS diskside tie-off")
    return t


# ------------------------------------------------------------------------------------------------ nes.v
T65_INST = re.compile(r"^T65 cpu\(.*?^\);\n", re.M | re.S)


def fix_nes(t):
    t = resub(t, r"^savestates savestates \(.*?^\);\n", """\
// T95: no savestates. Everything the framework drove is constant.
assign reset_ss = 1'b0;
assign reset_delay = 1'b0;
assign state_loaded = 1'b0;
assign savestate_busy = 1'b0;
assign loading_savestate = 1'b0;
assign saving_savestate = 1'b0;
assign sleep_savestates = 1'b0;
assign Savestate_RAMAddr = 25'd0;
assign Savestate_RAMRdEn = 1'b0;
assign Savestate_RAMWrEn = 1'b0;
assign Savestate_RAMWriteData = 8'd0;
assign Savestate_RAMType = 3'd0;
assign SaveStateBus_Din = 64'd0;
assign SaveStateBus_Adr = 10'd0;
assign SaveStateBus_wren = 1'b0;
assign SaveStateBus_rst = 1'b0;
assign SAVE_out_Din = 64'd0;
assign SAVE_out_Adr = 26'd0;
assign SAVE_out_rnw = 1'b1;
assign SAVE_out_ena = 1'b0;
assign SAVE_out_be = 8'd0;
""", "savestates instance", 1)
    t = resub(t, r"^statemanager #\(58720256, 33554432\) statemanager \(.*?^\);\n", """\
assign savestate_savestate = 1'b0;
assign savestate_loadstate = 1'b0;
assign savestate_address = 32'd0;
assign sleep_rewind = 1'b0;
""", "statemanager instance", 1)
    t = sub1(t, "wire [63:0] SaveStateBus_Din;\nwire [9:0] SaveStateBus_Adr;\nwire SaveStateBus_wren, SaveStateBus_rst;\n",
             "wire [63:0] SaveStateBus_Din;\nwire [9:0] SaveStateBus_Adr;\nwire SaveStateBus_wren, SaveStateBus_rst;\n", "bus decl")
    t = sub1(t, "assign sleep_savestate = sleep_rewind | sleep_savestates;", "assign sleep_savestate = 1'b0;", "sleep_savestate")
    # the cheat module and the composite board
    t = resub(t, r"^CODES codes \(.*?^\);\n", "assign genie_ovr = 1'b0;\nassign genie_data = 8'd0;\nassign gg_avail = 1'b0;\n", "cheat module", 1)
    t = resub(t, r"^composite_board composite_board\(.*?^\);\n", "assign composite_a = 24'sd0;\nassign composite_b = 24'sd0;\n", "composite board", 1)
        # the 6502
    t = resub(t, r"^T65 cpu\(.*?^\);\n", """\
T65_nes cpu(
	.Res_n  (~cpu_reset && ~cold_reset),
	.Pwr_n  (~cold_reset), // Cold boot, power reset, must be paired with reset
	.Clk    (clk),
	.Enable (cpu_ce && ce_in),
	.Rdy    (~pause_cpu),

	.IRQ_n  (~(apu_irq | mapper_irq)),
	.NMI_n  (~nmi),
	.R_W_n  (cpu_rnw),

	.A      (cpu_addr),
	.DI     (cpu_rnw ? internal_data_bus : cpu_dout),
	.DOut   (cpu_dout),

	.Instrnew (cpu_Instrnew)
);
""", "T65 instance", 1)
    # one more output: a PPU dot has been produced
    t = sub1(t, "\toutput  [8:0] cycle,\n", "\toutput        px_ce_o,       // T95: this tick produced a PPU dot\n\toutput  [8:0] cycle,\n", "px_ce_o port")
    t = sub1(t, "assign apu_ce = cpu_ce;\n", "assign apu_ce = cpu_ce;\nassign px_ce_o = ppu_ce & ~corepause_active;\n", "px_ce_o assign")
    return t


# ------------------------------------------------------------------------------------------------ 6: the 6502
def t65_vhdl():
    """-> dict name -> text: the VHDL the conversion reads"""
    f = {}
    for n in ("T65_Pack.vhd", "T65_ALU.vhd", "T65_MCode.vhd", "T65.vhd"):
        f[n] = read("t65/" + n)
    t = f["T65.vhd"]
    t = sub1(t, "  use work.pBus_savestates.all;\n", "", "t65 bus package")
    t = resub(t, r"  iREG_SAVESTATE_T80_1 : entity work\.eReg_SavestateV generic map \(0, (x\"[0-9A-F]{16}\")\).*?\n"
                 r"  iREG_SAVESTATE_T80_2 : entity work\.eReg_SavestateV generic map \(1, (x\"[0-9A-F]{16}\")\).*?\n"
                 r"  iREG_SAVESTATE_T80_3 : entity work\.eReg_SavestateV generic map \(2, (x\"[0-9A-F]{16}\")\).*?\n"
                 r"  SaveStateBus_Dout <= reg_wired_or_0 or reg_wired_or_1 or reg_wired_or_2;\n",
              "  SS_1 <= \\1; SS_2 <= \\2; SS_3 <= \\3;   -- T95: the savestate registers hold their defaults\n"
              "  SaveStateBus_Dout <= (others => '0');\n", "t65 savestate registers", 1)
    # `do` and `break` are SystemVerilog keywords; GHDL carries VHDL names into the Verilog unchanged
    t = sub1(t, "    DO      : out std_logic_vector(7 downto 0);", "    DOut    : out std_logic_vector(7 downto 0);", "t65 port DO")
    t = sub1(t, "  DO <= DO_r;", "  DOut <= DO_r;", "t65 DO assign")
    t = sub1(t, "  signal Break              : std_logic;", "  signal BrkS               : std_logic;", "t65 signal Break")
    t = sub1(t, "          if Break = '1' then", "          if BrkS = '1' then", "t65 Break use 1")
    t = sub1(t, "  Break <= (BreakAtNA", "  BrkS <= (BreakAtNA", "t65 Break use 2")
    t = sub1(t, "(MCycle = LCycle and Break = '0')", "(MCycle = LCycle and BrkS = '0')", "t65 Break use 3")
    t = sub1(t, "if MCycle = LCycle or Break = '1' then", "if MCycle = LCycle or BrkS = '1' then", "t65 Break use 4")
    # GHDL names an instance's output wire <label>_<port>; instance `alu`, port Q, collides with the signal ALU_Q (two `alu_q`)
    t, n = re.subn(r"\bALU_Q\b", "ALUQs", t, flags=re.I)
    if n != 8:
        raise SystemExit("nes_gen: t65 ALU_Q: expected 8 uses, found %d" % n)
    count("t65 ALU_Q renamed", n)
    f["T65.vhd"] = t
    f["T65_nes.vhd"] = """\
-- T95: the ports of T65 that the NES uses (generated by tools/nes_gen.py; the savestate bus, the 65C816 and the debug record are tied off)
library IEEE;
use IEEE.std_logic_1164.all;
entity T65_nes is
  port(
    Res_n, Pwr_n, Enable, Clk, Rdy, IRQ_n, NMI_n : in std_logic;
    R_W_n, Instrnew : out std_logic;
    A  : out std_logic_vector(15 downto 0);
    DI : in  std_logic_vector(7 downto 0);
    DOut : out std_logic_vector(7 downto 0));
end T65_nes;
architecture rtl of T65_nes is
  signal a24 : std_logic_vector(23 downto 0);
begin
  cpu : entity work.T65 port map (Mode => "00", BCD_en => '0', Res_n => Res_n, Pwr_n => Pwr_n, Enable => Enable, Clk => Clk, Rdy => Rdy,
    Abort_n => '1', IRQ_n => IRQ_n, NMI_n => NMI_n, SO_n => '1', R_W_n => R_W_n, Sync => open, EF => open, MF => open, XF => open,
    ML_n => open, VP_n => open, VDA => open, VPA => open, A => a24, DI => DI, DOut => DOut, Regs => open, DEBUG => open, NMI_ack => open,
    Instrnew => Instrnew, SaveStateBus_Din => (others => '0'), SaveStateBus_Adr => (others => '0'), SaveStateBus_wren => '0',
    SaveStateBus_rst => '0', SaveStateBus_load => '0', SaveStateBus_Dout => open);
  A <= a24(15 downto 0);
end rtl;
"""
    return f


def t65_hash(f):
    h = hashlib.sha256()
    for n in sorted(f):
        h.update(n.encode() + b"\0" + f[n].encode() + b"\0")
    return h.hexdigest()


def t65_convert(f, out_path):
    """GHDL in a container: analyse, synthesise T65_nes to Verilog"""
    import tempfile
    d = tempfile.mkdtemp(prefix="nes_t65_")
    for n, t in f.items():
        open(os.path.join(d, n), "w").write(t)
    img = os.environ.get("NES_GHDL_IMAGE", "debian:trixie-slim")
    script = ("set -e; apt-get update -qq >/dev/null; apt-get install -y -qq ghdl >/dev/null 2>&1; cd /w; "
              "ghdl -a --std=08 -frelaxed -fsynopsys T65_Pack.vhd T65_ALU.vhd T65_MCode.vhd T65.vhd T65_nes.vhd; "
              "ghdl --synth --std=08 -frelaxed -fsynopsys --out=verilog T65_nes > t65_nes.v")
    cmd = ["nice", "-n", "15", "container", "run", "--rm", "--cpus", "2", "-m", "2G", "-v", "%s:/w" % d, img, "sh", "-c", script]
    print("nes_gen: GHDL conversion of the 6502 (container):", " ".join(cmd[:8]), "...")
    subprocess.check_call(cmd)
    v = open(os.path.join(d, "t65_nes.v")).read()
    hdr = ("// T95: the NES's 6502 (T65, VHDL, rtl/t65) converted by GHDL --synth --out=verilog; tools/nes_gen.py --vhdl made this\n"
           "// file from the VHDL whose hash is in t65_nes.vhd.sha256. Do not edit.\n")
    open(out_path, "w").write(hdr + v)


# ------------------------------------------------------------------------------------------------ build
SV_FILES = ["nes.v", "ppu.sv", "apu.sv", "cart.sv", "regs_savestates.sv", "mappers/generic.sv", "mappers/MMC1.sv",
            "mappers/MMC2.sv", "mappers/MMC3.sv", "mappers/VRC.sv"]


def build():
    check_rtl()
    mods = {}      # name -> (file, text)
    order = []
    preludes = {}
    for fn in SV_FILES:
        t = read(fn)
        if fn == "regs_savestates.sv":
            preludes[fn] = t
            continue
        t = cut_savestate_regs(t, fn)
        if fn.startswith("mappers/"):
            t = untri_mapper(t)
            t = resub(t, r"^always begin\b", "always @* begin", "always begin")
        if fn == "ppu.sv":
            t = sub1(t, "\tinout   [1:0] sys_type,", "\tinput   [1:0] sys_type,", "ppu sys_type inout")
        if fn == "cart.sv":
            t = fix_cart(t)
        if fn == "nes.v":
            t = fix_nes(t)
        prelude, ms = split_modules(t)
        preludes[fn] = prelude
        for name, text in ms:
            if name in mods:
                raise SystemExit("nes_gen: module %s defined twice (%s, %s)" % (name, mods[name][0], fn))
            mods[name] = (fn, text)
            order.append(name)
    # reachability from NES
    deps = {}
    allnames = set(mods) | {"T65_nes"}
    for name, (fn, text) in mods.items():
        c = strip_comments(text)
        deps[name] = {m for m in allnames if m != name and re.search(r"^\s*%s\b\s*(#|\w+\s*(\[[^\]]*\])?\s*\()" % m, c, re.M)}
    reach, todo = set(), ["NES"]
    while todo:
        n = todo.pop()
        if n in reach or n not in mods:
            continue
        reach.add(n)
        todo.extend(deps[n])
    reach_v = reach | {"T65_nes"}
    out = {}
    for fn in SV_FILES:
        if fn == "regs_savestates.sv":
            out[fn] = preludes[fn]
            continue
        parts = [mods[n][1] for n in order if mods[n][0] == fn and n in reach]
        if not parts:
            continue
        out[fn] = preludes[fn] + "".join(parts)
    # the clock enable
    reached = sorted(n for n in reach)
    for fn in list(out):
        if fn == "regs_savestates.sv":
            continue
        t = out[fn]
        t = gate_processes(t)
        t = proc_wires(t)
        for n in reached:
            if n in mods and mods[n][0] == fn:
                t = add_port(t, n)
        t = add_instance_arg(t, set(reached))
        t = hoist_implicit(t)
        out[fn] = t
    # the savestate index and default constants live in the package regs_savestates. Upstream only
    # ppu.sv imports it: the others get the names from the global import of NES.sv, MiSTer's top,
    # which is not vendored. Quartus and Verilator let that pass (one compilation unit, the names
    # are unique), Vivado does not - run 40 died with five ERROR [Synth 8-36] 'SSREG_INDEX_APU_TOP'
    # is not declared in gen/nes.v and apu.sv would have been next. So every file that uses the
    # names gets the import it needs, and the check below fails if one is missing.
    for fn in list(out):
        if fn == "regs_savestates.sv":
            continue
        c = strip_comments(out[fn])
        if re.search(r"\bSSREG_(INDEX|DEFAULT)_\w+", c) and not re.search(r"^\s*import\s+regs_savestates\s*::", c, re.M):
            out[fn] = ("import regs_savestates::*;  // T95: Vivado needs the import per file; "
                       "upstream relies on NES.sv's global one\n") + out[fn]
            count("package import added")
    # checks that the cut left nothing behind
    for fn, t in out.items():
        c = strip_comments(t)
        for bad in (r"\beReg_SavestateV\b", r"\btri[01]\b", r"\binout\b"):
            if re.search(bad, c):
                raise SystemExit("nes_gen: %s: %s is left" % (fn, bad))
        if fn != "regs_savestates.sv" and re.search(r"\bSSREG_(INDEX|DEFAULT)_\w+", c) \
           and not re.search(r"^\s*import\s+regs_savestates\s*::", c, re.M):
            raise SystemExit("nes_gen: %s uses SSREG_* without importing regs_savestates" % fn)
        if fn == "regs_savestates.sv":
            continue
        for mname, mt in split_modules(t)[1]:          # nothing may be left for Vivado to invent
            mc = strip_comments(mt)
            mm = re.search(r"^module\s+%s\b" % re.escape(mname), mc, re.M)
            if not mm:
                continue
            oi = mc.find("(", mm.end())
            mhdr = mc.index(";", match_paren(mc, oi) if oi >= 0 else mm.end()) + 1
            use = _implicit_use(mc, mhdr)
            for d in DECL_RE.finditer(mc):
                if d.group(4) in use and use[d.group(4)] < d.start():
                    raise SystemExit("nes_gen: %s.%s: %s is used in an assign or a port connection "
                                     "above its declaration (Vivado would make an implicit net)"
                                     % (mname, d.group(4), d.group(4)))
    vf = t65_vhdl()
    return out, sorted(reach), t65_hash(vf), vf


def mappers_all(d):
    check_rtl()
    import glob
    os.makedirs(os.path.join(d, "mappers"), exist_ok=True)
    for f in sorted(glob.glob(os.path.join(RTL, "mappers", "*.sv"))):
        fn = "mappers/" + os.path.basename(f)
        t = cut_savestate_regs(read(fn), fn)
        t = untri_mapper(t)
        t = resub(t, r"^always begin\b", "always @* begin", "always begin")
        t = proc_wires(t)
        open(os.path.join(d, fn), "w").write(t)
    shutil_copy = open(os.path.join(RTL, "regs_savestates.sv")).read()
    open(os.path.join(d, "regs_savestates.sv"), "w").write(shutil_copy)
    print("nes_gen: all mapper files in", d)


def main():
    if "--mappers-all" in sys.argv:
        mappers_all(sys.argv[sys.argv.index("--mappers-all") + 1])
        return
    out, reach, h65, vf = build()
    print("nes_gen: %d files, %d modules reached from NES" % (len(out), len(reach)))
    print("nes_gen: " + ", ".join("%s %d" % (k, v) for k, v in sorted(LOG.items())))
    gh = os.path.join(GEN, T65H)
    if "--check" in sys.argv:
        bad = []
        for name, t in out.items():
            p = os.path.join(GEN, name)
            if not os.path.exists(p) or open(p, encoding="utf-8").read() != t:
                bad.append(name)
        if not os.path.exists(gh) or open(gh).read().strip() != h65:
            bad.append(T65H + " (the VHDL the 6502 conversion reads has changed: tools/nes_gen.py --vhdl)")
        if not os.path.exists(os.path.join(GEN, T65V)):
            bad.append(T65V)
        have = set()
        for d, _, fs in os.walk(GEN):
            for f in fs:
                have.add(os.path.relpath(os.path.join(d, f), GEN))
        extra = have - set(out) - {T65V, T65H}
        if bad or extra:
            print("cores/nes/gen is not what tools/nes_gen.py makes: run it (%s %s)" % (sorted(bad), sorted(extra)))
            sys.exit(1)
        print("gen is current")
        return
    import shutil
    keep = {}
    for r in (T65V, T65H):
        p = os.path.join(GEN, r)
        keep[r] = open(p).read() if os.path.exists(p) else None
    shutil.rmtree(GEN, ignore_errors=True)
    for name, t in out.items():
        p = os.path.join(GEN, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w", encoding="utf-8").write(t)
    os.makedirs(os.path.join(GEN, "t65"), exist_ok=True)
    have_h = keep[T65H].strip() if keep[T65H] else None
    if "--vhdl" in sys.argv or have_h != h65 or keep[T65V] is None:
        t65_convert(vf, os.path.join(GEN, T65V))
        open(gh, "w").write(h65 + "\n")
    else:
        open(os.path.join(GEN, T65V), "w").write(keep[T65V])
        open(gh, "w").write(keep[T65H])


main()
