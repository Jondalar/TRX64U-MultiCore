#!/usr/bin/env python3
"""T239.3 P1-3 (D30 condition 1): TOML A against the RTL, code to code, and the reverse lint on C/C++ sources.

  rtl_check.py check --design DIR --toml-a FILE [--maps DIR]... [--list build.tcl]... [--define NAME]... [--own-core] [--top top] [--bus ultimate_sys.io_req | --bus MODULE:write=P,read=P,address=P,data=P,response=P --window BASE:SIZE]
  rtl_check.py lint-src PATH... [--toml-a FILE [--maps DIR]...] [--iobase 0x10000000] [--span 0x1000000] [--allow FILE:LITERAL]...

check
  Follows the IO request through the decode of a design and compares every block of TOML A with it: the block lies in decoded
  bytes, and each register of its map is written (dir w) and read (dir r) at every byte of its width. The RTL is the evidence;
  no comment is read as one. Method: the design's own source files are parsed (VHDL and Verilog, the subset the design uses),
  and the request is run through them as a stimulus: the address (its low bits unknown for a span), write or read, the data unknown.
  Guards that depend on the request are evaluated, guards that depend on state are unknown and both ways are explored; a register
  that only copies the held request (region_r <= region) takes the request's value, a handshake flag (every assignment a literal)
  takes its initial value, any other state is unknown. A write is an executed assignment whose value or guard comes from the
  request's data through wires, or whose guard comes from the write flag and the address together and that the same address as a
  read does not run (a strobe register that ignores the data byte; the write flag alone is no write); a read is a response whose data does not come from the literal 0 alone. A module is followed
  only when a strobe derived from the request reaches it (its source is then parsed too).
  A block with owner = "core" is a window the base hands to the core: for every byte the request must leave the base over a port of the
  groups CORE_PORTS (a strobe port that is 1 for the address, or a register driven under the address) and a data port of the same group
  must carry the request's data; its properties are not checked (they are the core's contract).
  `--define NAME` (repeatable) names the `define symbols of the build the check is run for; every other `ifdef symbol is undefined. A VHDL
  if-generate is decided with the generics' defaults and a Verilog one with the parameters' defaults; an instance that gives such a generic
  another value gets the module parsed again with that value (the generate branches follow it); the check fails only when the value is
  unknown (X) (the parameter lists of the top-level Verilog file are read for it).
  Reads only the design's source list (the files named in its build script, here build.tcl, and the lists it `source`s by a literal path
  relative to the design directory; `--list FILE` names another, repeatable); a VHDL file (.vhd, .vhdl) whose base name says ultisid,
  copper, lp_filter or sampler is on the deny list (clean room: Gideon's files; a directory name or a Verilog file does not count): read_source refuses it, so it is never opened or parsed, and the check
  lists the denied files of the source list in its output. The block of such a module is checked at the port only (the hand-over rule).
  A partial core has no request record: the host's decode hands it a window of the I/O space over plain ports. --bus MODULE:write=P,read=P,address=P,
  data=P,response=P names the ports of the one instance of MODULE in the top file (--top, a Verilog file); --window BASE:SIZE is the part of the I/O
  space the host hands to them (the host's fact): a block must lie inside it and the address port gets the block's address minus BASE. --list takes
  the core's source list (cores/<id>/core_sources.tcl, relative to --design), whose `set` variables it reads.
  An RTL pattern the check does not know is an error (Unsupported, with file and line), never a pass. Exit 0: every block holds.
  The direction rule: a property's dir must be a subset of what the RTL does at each byte (r needs a read, w needs a write, rw both).
  A request that leaves a followed module over output ports (held address, write flag, data, toggle) and is read by another module of
  the top-level file crosses a clock domain over a handshake: that module is run on the held request (stage 2: its ports take the
  forwarded values, its state is unknown, only the module itself is followed) and decides write and read of the byte (dma_win). A decode
  there on a register copy of the held address is refused (Unsupported: a pipeline, not modelled). Gideon's io_bus_splitter is modelled by the check (its architecture is not read): port i of reqs gets the request with write and read cleared unless the
  address bits g_range_hi downto g_range_lo are i, and resp is the OR of the answers of the ports the address can select (only those are an origin of the answer).
  The synchronizers of a clock-domain bridge (synchronizer_gzw) carry the vector unchanged: their outputs are their inputs, io_bus_bridge2 itself is read from its source.
  A RAM entity (MEMORIES: dpram, dpram_8x16, dpram_8x32, pseudo_dpram_8x32) is a write sink: its write enable is 1 for the request and its data port carries the data.
  A procedure declared in a process is inlined at its calls (in-parameters only), aliases are replaced where they are used, a VHDL block statement is part of the module.
  The taint of a part of a vector (a slice, an index) is that of those bits: a concatenation, a synchronizer and an instance port keep the address, data, read and write of the request apart.
  A write is a register or a RAM that takes the request's data (a RAM also needs the request's address on its address port, and a run or block may not be larger than the RAM), an assignment the data decides, or an
  assignment guarded by the address and the write flag that the read of the same address does not run; an answer of the bus (a t_io_resp signal, ack, nc) is none of them.
  A request that a module holds in registers it sets itself (flash_gate.vhd: p_valid, p_read, p_addr, p_data, assigned for sure under a guard that is no address decode) is decoded in the next clock: those registers
  take the request's value and taint there, and their own assignments are no writes. A flag whose assignments set it to another value than it has now is, as response data, a register.
  --bus MODULE:... with MODULE the top module itself (--top): the ports of the top module are the request; a register stage of the top file (`always @(posedge clk) q <= expr;`) holds it one clock, the reset
  released. --window BASE:SIZE:full puts the whole address on the address port. --bus and --window may be repeated: a block goes to the bus whose window holds it.
  RTL_CHECK_TRACE=1 raises the error with its traceback.
  A Verilog generate-for (`genvar g; generate for (g = A; g < B; g = g + 1) begin : name ... end endgenerate`) is unrolled when the module is parsed: each pass declares its names as
  (name, index, declared name) and the rest of the module reads them as name[index].declared; the genvar is its value inside the block. A Verilog function of the form
  `function [r] f (input [r] a, ...); f = expression; endfunction` is replaced by its expression at every call. Another function is skipped as before.
  A module behind a handshake that holds the request in registers before it decodes it (trx_sys.v: rq_addr, then c_addr, then op_a, one clock each, under guards of its state) is run
  with that pipeline: a register that takes the request's bits on unchanged (a name, a slice or a concatenation of values with the request's taint, under a guard that carries no
  taint of the request: a pass-on, no decode) has the request's value and taint where the module decodes, and the module's flags are in any of their states. What another master
  loads into the same register (JTAG) is not the request and is left out. A one-bit port of the handshake without a value (the toggle) is a strobe of the transport and no address.
  A block of the TOML that names a pattern the check does not know stops that block only: the output lists every such block (Unsupported, with file and line) next to the blocks that
  do not agree.
  --own-core: the blocks with owner = "core" are checked like the blocks of the base (decoded, every property read and written as its dir says). Without it a core block is only
  a window that leaves the base for the core. A design that holds the registers of a core block itself (c64_basic: trx_sys.v, pix_layer.v, c64_text.v) is checked with it.
  A module behind a handshake that drives an output port by a continuous assignment from its held request (trx_sys.v: p2_we = ost == O_RUN && op_we && op_a[15:12] == 4'd2, p2_addr, p2_wdata; dr_*) hands the request on: a module of the top file that reads such a port
  is run on it, with the value of the assignment (the state unknown) and, for a strobe, the address decode and the write flag (a one-bit register that holds the request's known value) as its taint; a module that reads an address or data port alone is not run, and
  the pipeline's own state writes (ost, res_v) stay no register writes. A Verilog if-generate inside a generate-for body (one item without begin/end too) is decided with the genvar's value.
  What is not followed yet: byte 3 of trx_dbg_run.owner, cyc_cmp, cmp_en, trx_statebus.status and trx_debug.ipc_set (c64_basic: no write is found at the committing byte), a VHDL for-generate with ascending ranges (gcr_codec.vhd), a Verilog generate-for that is no counting loop, a Verilog function with more than its one assignment.

lint-src
  The reverse lint (D30 conditions 1 and 4): a literal that lies in the I/O space or in a block of TOML A is refused in a C/C++
  source (--allow FILE:LITERAL lets one literal of one file pass, by the file's trailing path components and the literal's text with its
  suffix: a hash constant such as 0x10325476 lies in the I/O space by chance. The rest of that file is read like any other.)
  Comments and strings are not read. Exit 0: none; exit 1: the list, one line each, file:line: literal.
  It is switched on for coreapp/src in P2-6 (when the last address literal has gone): there ci/lint.sh gets the line
      check "reverse lint (coreapp/src, libraries)" python3 -I tools/rtl_check.py lint-src coreapp/src <library source dirs> \\
          --allow ringfile.cc:0x106aa070u --allow trxmon/tm_ws.c:0x10325476 --allow wsproto.cc:0x10325476u
  The three --allow literals are no addresses: 0x106aa070 is SHA-256 K[47] (ringfile.cc, also uicore/uic_sha.cc), 0x10325476 is a hash
  initial word (tm_ws.c, wsproto.cc). Until P2-6 it is not in ci/lint.sh; run by hand it lists what is left.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Gideon's request splitter is modelled by the check itself (an array of request records is not in the evaluator's types): see Ev.split_info.
# The clock-domain bridge's synchronizer carries its vector unchanged to the other clock: its outputs are its inputs (PASSTHROUGH: output port -> input port).
SPLITTERS = ("io_bus_splitter",)
PASSTHROUGH = {"synchronizer_gzw": {"rx_data": "tx_data", "rx_new_data": "tx_push"}}
# Gideon's RAM entities: their architecture is a vendor file (shared variables, element writes) the check does not parse. A write is the request's data on the
# data port and its address on the address port while the write enable port is 1 for it (module -> (write enable port, data port, address port)); the read data port is an answer that does not come from the literal 0.
MEMORIES = {"dpram_8x16": ("wea", "dia", "addra"), "dpram_8x32": ("wea", "dia", "addra"), "pseudo_dpram_8x32": ("wr_en", "wr_data", "wr_address"), "dpram": ("a_we", "a_wdata", "a_address")}

CLEAN_ROOM = ("ultisid", "copper", "lp_filter", "sampler")


def denied(path):
    """Gideon's UltiSID, Copper, lp_filter and sampler VHDL are never read (clean room, T239.3 D30). The deny list is by file name: the base name of a
    VHDL file (.vhd, .vhdl) that has one of these words in it. A directory name does not count, and a Verilog file (the Minimig copper, a module of
    TRX64-Ultimate) is not Gideon's."""
    b = os.path.basename(path).lower()
    return b.endswith((".vhd", ".vhdl")) and any(c in b for c in CLEAN_ROOM)


def read_source(path):
    """The one place that opens an RTL source file. A denied file is never opened: its blocks are checked at the port only (the hand-over rule)."""
    if denied(path):
        raise CheckError("%s is on the clean-room deny list and is never opened" % path)
    return open(path, errors="replace").read()
VERILOG_KW = {"module", "endmodule", "always", "assign", "initial", "wire", "reg", "input", "output", "inout", "parameter",
              "localparam", "function", "task", "generate", "genvar", "begin", "end", "if", "else", "case", "for", "integer",
              "posedge", "negedge", "or", "and", "not", "defparam", "specify"}


class Unsupported(Exception):
    """An RTL pattern the check does not know."""


class Pipeline(Unsupported):
    """A decode on a register copy of the held request: the module is run again with its pipeline modelled (Ev.build_pipe)."""


class CheckError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------------------
# values: bits that are 0, 1 or unknown

class V:
    __slots__ = ("v", "x", "w")

    def __init__(self, v, x, w):
        if w is not None:
            m = (1 << w) - 1
            v &= m
            x &= m
        self.v, self.x, self.w = v & ~x if w is not None else v, x, w

    def known(self):
        return self.x == 0

    def __repr__(self):
        return "V(%s,%s,%s)" % (hex(self.v), hex(self.x), self.w)


def kn(v, w=None):
    return V(v, 0, w)


def allx(w):
    w = 1 if w is None else w
    return V(0, (1 << w) - 1, w)


X1 = V(0, 1, 1)
T1 = V(1, 0, 1)
F1 = V(0, 0, 1)


def mask(w):
    return (1 << w) - 1


def wmax(a, b):
    if a.w is None:
        return b.w
    if b.w is None:
        return a.w
    return max(a.w, b.w)


def fit(a, w):
    if w is None:
        return a
    return V(a.v, a.x, w)


def v_eq(a, b):
    w = wmax(a, b)
    m = mask(w) if w is not None else (1 << 64) - 1
    unk = (a.x | b.x) & m
    if (a.v ^ b.v) & ~unk & m:
        return F1
    return X1 if unk else T1


def v_not(a):
    w = a.w if a.w is not None else 32
    return V(~a.v & ~a.x & mask(w), a.x, w)


def v_bit(a, b, op):
    w = wmax(a, b)
    if w is None:
        w = 32
    m = mask(w)
    a0, a1 = ~a.v & ~a.x & m, a.v & ~a.x & m
    b0, b1 = ~b.v & ~b.x & m, b.v & ~b.x & m
    if op == "and":
        r1, r0 = a1 & b1, a0 | b0
    elif op == "or":
        r1, r0 = a1 | b1, a0 & b0
    elif op == "xor":
        r1, r0 = (a1 & b0) | (a0 & b1), (a1 & b1) | (a0 & b0)
    else:
        raise Unsupported("bit operator " + op)
    return V(r1, m & ~(r1 | r0), w)


def truth(a):
    """1, 0 or None (unknown)."""
    if a.v:
        return 1
    if a.x:
        return None
    return 0


def v_tv(t):
    return X1 if t is None else (T1 if t else F1)


def v_merge(a, b):
    w = wmax(a, b)
    if w is None:
        w = 32
    m = mask(w)
    unk = (a.x | b.x | (a.v ^ b.v)) & m
    return V(a.v, unk, w)


def v_cat(parts):
    v = x = w = 0
    for p in parts:
        pw = p.w if p.w is not None else 32
        v, x, w = (v << pw) | (p.v & mask(pw)), (x << pw) | (p.x & mask(pw)), w + pw
    return V(v, x, w)


def v_slice(a, hi, lo):
    n = hi - lo + 1
    if n < 1:
        raise Unsupported("empty slice")
    return V(a.v >> lo, a.x >> lo, n)


def v_arith(a, b, op):
    w = wmax(a, b)
    if a.x or b.x:
        return allx(w if w is not None else 32)
    r = {"+": a.v + b.v, "-": a.v - b.v, "*": a.v * b.v}[op]
    return V(r, 0, w) if w is not None else V(r, 0, None)


def v_cmp(a, b, op):
    if a.x or b.x:
        return X1
    return T1 if {"<": a.v < b.v, ">": a.v > b.v, "<=": a.v <= b.v, ">=": a.v >= b.v}[op] else F1


# ---------------------------------------------------------------------------------------------------------------------------
# lexers

VHDL_TOK = re.compile(r'''
 (?P<ws>\s+)
|(?P<cm>--[^\n]*)
|(?P<bs>[xXbBoO]"[0-9a-fA-F_xXzZuU-]*")
|(?P<str>"(?:[^"]|"")*")
|(?P<chr>'[^'\n]')
|(?P<based>\d+\#[0-9a-fA-F_]+\#)
|(?P<num>\d[\d_]*(?:\.\d+)?)
|(?P<id>[A-Za-z_]\w*|\\[^\\]*\\)
|(?P<op><=|>=|=>|/=|:=|\*\*|<>|[-+*/&|=<>(),.:;'])
''', re.X)

VLOG_TOK = re.compile(r'''
 (?P<ws>\s+)
|(?P<cm>//[^\n]*|/\*.*?\*/)
|(?P<attr>\(\*(?!\)).*?\*\))
|(?P<dir>`[^\n]*)
|(?P<num>\d*'[sS]?[bBoOdDhH][0-9a-fA-FxXzZ_?]+|\d[\d_]*\.\d[\d_]*|\d[\d_]*|'[01xXzZ])
|(?P<str>"(?:[^"\\]|\\.)*")
|(?P<id>[A-Za-z_][\w$]*|\$[A-Za-z_]\w*)
|(?P<op>===|!==|<<<|>>>|<<|>>|<=|>=|==|!=|&&|\|\||\+:|-:|~&|~\||~\^|[-+*/%&|^~!<>=?:;,.(){}\[\]\#@])
''', re.X | re.S)


DEFINES = set()   # the `define names of the build the check is run for (--define); everything else is undefined


def lex(text, lang, fname):
    pat = VHDL_TOK if lang == "vhdl" else VLOG_TOK
    out, pos, line = [], 0, 1
    cond = []   # one (parent active, a branch was taken, this branch active) per open `ifdef
    while pos < len(text):
        m = pat.match(text, pos)
        if not m:
            raise Unsupported("%s:%d: cannot read %r" % (fname, line, text[pos:pos + 20]))
        k, s = m.lastgroup, m.group()
        on = all(c[2] for c in cond)
        if k == "dir":
            d = re.match(r"`(ifdef|ifndef|else|elsif|endif)\b\s*(\w*)", s)
            if d:
                kw, nm = d.groups()
                if kw in ("ifdef", "ifndef"):
                    hit = (nm in DEFINES) == (kw == "ifdef")
                    cond.append((on, hit, hit))
                elif kw == "elsif":
                    par, taken, _ = cond[-1]
                    hit = (nm in DEFINES) and not taken
                    cond[-1] = (par, taken or hit, hit)
                elif kw == "else":
                    par, taken, _ = cond[-1]
                    cond[-1] = (par, True, not taken)
                else:
                    cond.pop()
        elif k not in ("ws", "cm", "attr") and on:
            out.append((k, s.lower() if (lang == "vhdl" and k == "id") else s, line))
        line += s.count("\n")
        pos = m.end()
    out.append(("eof", "", line))
    return out


def num_vhdl(kind, s):
    if kind == "bs":
        b = s[0].lower()
        body = s[2:-1].replace("_", "")
        bits = {"x": 4, "b": 1, "o": 3}[b]
        v = x = 0
        for ch in body:
            v, x = v << bits, x << bits
            if ch in "xXzZuU-":
                x |= mask(bits)
            else:
                v |= int(ch, 16)
        return V(v, x, bits * len(body))
    if kind == "str":
        body = s[1:-1].replace('""', '"')
        if re.fullmatch(r"[01xXzZuU-]*", body) and body:
            v = x = 0
            for ch in body:
                v, x = v << 1, x << 1
                if ch in "xXzZuU-":
                    x |= 1
                else:
                    v |= int(ch)
            return V(v, x, len(body))
        # a text (a memory style, a vendor name): no value the decode can depend on, unknown if something compares it
        return V(0, mask(64), None)
    if kind == "chr":
        ch = s[1]
        if ch in "01":
            return V(int(ch), 0, 1)
        if ch in "xXzZuU-":
            return V(0, 1, 1)
        if ch in "hH":
            return V(1, 0, 1)
        if ch in "lL":
            return V(0, 0, 1)
        raise Unsupported("a character literal %s" % s)
    if kind == "based":
        b, rest = s.split("#", 1)
        return V(int(rest.strip("#").replace("_", ""), int(b)), 0, None)
    return V(int(s.replace("_", "")), 0, None)


def num_vlog(s):
    if s.startswith('"'):
        body = s[1:-1]
        return V(int.from_bytes(body.encode(), "big") if body else 0, 0, 8 * max(len(body), 1))
    if "'" not in s:
        return V(int(float(s.replace("_", ""))), 0, None)
    if s[0] == "'" and len(s) == 2:
        return V(1 if s[1] == "1" else 0, 1 if s[1] in "xXzZ" else 0, None)
    w, rest = s.split("'", 1)
    rest = rest.lstrip("sS")
    base = {"b": 1, "o": 3, "d": 0, "h": 4}[rest[0].lower()]
    body = rest[1:].replace("_", "")
    if base == 0:
        v, x = int(body), 0
    else:
        v = x = 0
        for ch in body:
            v, x = v << base, x << base
            if ch in "xXzZ?":
                x |= mask(base)
            else:
                v |= int(ch, 16)
    return V(v, x, int(w) if w else None)


# ---------------------------------------------------------------------------------------------------------------------------
# IR

class Ty:
    """A subtype: name, hi and lo bound (ASTs, vectors), or elem (arrays)."""
    __slots__ = ("name", "hi", "lo", "elem")

    def __init__(self, name, hi=None, lo=None, elem=None):
        self.name, self.hi, self.lo, self.elem = name, hi, lo, elem


class Decl:
    __slots__ = ("name", "kind", "ty", "init")

    def __init__(self, name, kind, ty, init=None):
        self.name, self.kind, self.ty, self.init = name, kind, ty, init


class Proc:
    def __init__(self, clocked, stmts, vars_, where):
        self.clocked, self.stmts, self.vars, self.where = clocked, stmts, vars_, where


class Inst:
    def __init__(self, label, modname, ports, generics, where):
        self.label, self.modname, self.ports, self.generics, self.where = label, modname, ports, generics, where


class Mod:
    def __init__(self, name, lang, fname):
        self.name, self.lang, self.fname = name, lang, fname
        self.ports = {}        # name -> (dir, Ty)
        self.decls = {}        # name -> Decl (signals, regs, wires, variables of procs are in Proc.vars)
        self.consts = {}       # name -> AST
        self.enums = {}        # literal -> int
        self.enum_types = {}   # type name -> width
        self.types = {}        # type name -> Ty (arrays, aliases)
        self.generics = {}     # name -> AST default
        self.cas = []          # (lhs AST, rhs AST, where)
        self.procs = []
        self.insts = []
        self.assigns = {}      # base name -> [rhs AST] (every assignment anywhere)
        self.nets = {}         # (top.v only) net -> width
        self.gen_assumed = {}  # generic name -> value an if-generate condition was decided with (checked at elaboration)


def base_name(lhs):
    """The signal an assignment target names, and its record field suffix ('' if none)."""
    if lhs[0] == "id":
        return lhs[1], ""
    if lhs[0] in ("sl", "ix", "call", "psl"):
        return base_name(lhs[1])
    raise Unsupported("an assignment target of kind %s" % lhs[0])


class CatInfo:
    """The parts of a concatenation target {a, b[3:0], c[2]} = rhs (opaque to the expression walkers: the targets are no operands)."""
    __slots__ = ("parts",)

    def __init__(self, parts):
        self.parts = parts


def cat_leaves(lhs):
    if lhs[0] == "paren":
        return cat_leaves(lhs[1])
    if lhs[0] == "cat":
        return [x for part in lhs[1] for x in cat_leaves(part)]
    return [lhs]


def split_cat(lhs, rhs):
    """A concatenation target is one assignment per part, each taking its slice of the right side (MSB part first)."""
    leaves = cat_leaves(lhs)
    if len(leaves) == 1 and lhs[0] != "cat":
        return [(leaves[0], rhs)]
    info = CatInfo(leaves)
    return [(part, ("catpart", rhs, info, i)) for i, part in enumerate(leaves)]


def subst(e, mp):
    """An expression or statement with the identifiers of mp (formal -> actual expression) replaced."""
    if isinstance(e, tuple):
        if e and e[0] == "id" and len(e) == 2 and e[1] in mp:
            return mp[e[1]]
        return tuple(subst(x, mp) for x in e)
    if isinstance(e, list):
        return [subst(x, mp) for x in e]
    return e


def subst_stmt(s, mp):
    return subst(s, mp)


def reg_assigns(m, s):
    """The assignments of an inlined statement, entered in the module's table of right sides as the parser enters those of its own statements."""
    k = s[0]
    if k == "as":
        m.assigns.setdefault(base_name(s[1])[0], []).append(s[2])
    elif k == "blk":
        for b in s[1]:
            reg_assigns(m, b)
    elif k == "if":
        for _, b in s[1]:
            for x in b:
                reg_assigns(m, x)
        for x in s[2]:
            reg_assigns(m, x)
    elif k == "case":
        for _, b in s[2]:
            for x in b:
                reg_assigns(m, x)
    elif k == "for":
        for x in s[5]:
            reg_assigns(m, x)


# ---------------------------------------------------------------------------------------------------------------------------
# VHDL parser

class P:
    def __init__(self, toks, fname, ovr=None):
        self.t, self.i, self.fname = toks, 0, fname
        self.ovr = ovr or {}
        self.procs, self.noreg, self.aliases = {}, 0, {}

    def pk(self, n=0):
        return self.t[min(self.i + n, len(self.t) - 1)]

    def txt(self, n=0):
        return self.pk(n)[1]

    def where(self):
        return "%s:%d" % (self.fname, self.pk()[2])

    def err(self, what):
        return Unsupported("%s: %s (at %r)" % (self.where(), what, self.txt()))

    def eat(self, s=None):
        t = self.t[self.i]
        if s is not None and t[1] != s:
            raise self.err("expected %r" % s)
        self.i += 1
        return t

    def acc(self, s):
        if self.txt() == s:
            self.i += 1
            return True
        return False

    def eof(self):
        return self.pk()[0] == "eof"


VHDL_BINLEVELS = [
    ("or", "and", "xor", "nand", "nor", "xnor"),
    ("=", "/=", "<", "<=", ">", ">="),
    ("+", "-", "&"),
    ("*", "/", "mod", "rem"),
]


class VhdlParser(P):
    def parse_file(self, want=None):
        mods = {}
        ents = {}
        while not self.eof():
            w = self.txt()
            if w in ("library", "use"):
                self.skip_to(";")
            elif w == "entity":
                self.eat()
                name = self.eat()[1]
                self.eat("is")
                m = Mod(name, "vhdl", self.fname)
                if self.txt() == "generic":
                    self.eat()
                    self.generic_list(m)
                if self.txt() == "port":
                    self.eat()
                    self.port_list(m)
                while self.txt() in ("attribute", "constant", "alias", "use"):
                    self.skip_to(";")
                self.eat("end")
                self.acc("entity")
                if self.txt() != ";":
                    self.eat()
                self.eat(";")
                ents[name] = m
            elif w == "architecture":
                self.eat()
                self.eat()
                self.eat("of")
                ename = self.eat()[1]
                self.eat("is")
                m = ents.get(ename)
                if m is None:
                    raise self.err("architecture of an entity not in this file")
                self.arch_body(m)
                mods[ename] = m
            elif w == "package":
                self.eat()
                is_body = self.acc("body")
                name = self.eat()[1]
                self.skip_end_of(name)
            else:
                raise self.err("a design unit I do not know")
        return mods

    def skip_to(self, s):
        while not self.eof() and self.txt() != s:
            self.i += 1
        self.eat(s)

    def skip_end_of(self, name):
        while not self.eof():
            if self.txt() == "end" and self.txt(1) in ("package", name, "body"):
                self.i += 1
                while self.txt() != ";":
                    self.i += 1
                self.eat(";")
                return
            self.i += 1
        raise self.err("a package that does not end")

    def generic_list(self, m):
        self.eat("(")
        while True:
            names = [self.eat()[1]]
            while self.acc(","):
                names.append(self.eat()[1])
            self.eat(":")
            ty = self.subtype()
            dflt = None
            if self.acc(":="):
                dflt = self.expr()
            for n in names:
                m.generics[n] = ("n", kn(self.ovr[n])) if n in self.ovr else dflt
            if not self.acc(";"):
                break
        self.eat(")")
        self.eat(";")

    def port_list(self, m):
        self.eat("(")
        while True:
            names = [self.eat()[1]]
            while self.acc(","):
                names.append(self.eat()[1])
            self.eat(":")
            d = self.eat()[1]
            if d not in ("in", "out", "inout", "buffer"):
                raise self.err("a port direction")
            ty = self.subtype()
            dflt = None
            if self.acc(":="):
                dflt = self.expr()
            for n in names:
                m.ports[n] = (d, ty)
                m.decls[n] = Decl(n, "port", ty, dflt)
            if not self.acc(";"):
                break
        self.eat(")")
        self.eat(";")

    def subtype(self):
        name = self.eat()[1]
        while self.txt() == "." :
            self.eat()
            name = self.eat()[1]
        if self.txt() == "(":
            self.eat()
            hi = self.expr()
            if self.txt() in ("downto", "to"):
                d = self.eat()[1]
                lo = self.expr()
                self.eat(")")
                return Ty(name, hi, lo) if d == "downto" else Ty(name, lo, hi)
            if hi[0] == "attr" and hi[2] == "range" and self.txt() == ")":
                self.eat(")")
                return Ty(name, ("attr", hi[1], "high"), ("attr", hi[1], "low"))
            if self.txt() == "range":
                self.skip_to(")")
                return Ty(name)
            raise self.err("a subtype constraint")
        if self.txt() == "range":
            self.eat()
            self.expr()
            if self.txt() not in ("to", "downto"):
                raise self.err("a range constraint")
            self.eat()
            self.expr()
        return Ty(name)

    def arch_body(self, m):
        self.aliases = {}
        self.arch_decls(m)
        self.eat("begin")
        while self.txt() != "end":
            self.concurrent(m)
        self.eat("end")
        while self.txt() != ";":
            self.eat()
        self.eat(";")

    def arch_decls(self, m):
        while self.txt() != "begin":
            w = self.txt()
            if w in ("signal", "constant", "shared"):
                if w == "shared":
                    self.eat()
                kind = self.eat()[1]
                names = [self.eat()[1]]
                while self.acc(","):
                    names.append(self.eat()[1])
                self.eat(":")
                ty = self.subtype()
                init = None
                if self.acc(":="):
                    init = self.expr()
                self.eat(";")
                for n in names:
                    if kind == "constant":
                        m.consts[n] = init
                    m.decls[n] = Decl(n, kind, ty, init)
            elif w == "type":
                self.type_decl(m)
            elif w == "subtype":
                self.eat()
                n = self.eat()[1]
                self.eat("is")
                m.types[n] = self.subtype()
                self.eat(";")
            elif w in ("function", "procedure"):
                self.eat()
                fname = self.eat()[1]
                self.skip_subprogram(fname)
            elif w == "component":
                self.skip_to_end("component")
            elif w == "alias":
                self.alias_decl()
            elif w in ("attribute", "file", "use"):
                self.skip_to(";")
            else:
                raise self.err("a declaration I do not know")

    def alias_decl(self):
        """An alias names a part of a signal: every later use of the name is that part (parsed in place)."""
        self.eat("alias")
        an = self.eat()[1]
        if self.acc(":"):
            self.subtype()
        self.eat("is")
        self.aliases[an] = self.expr()
        self.eat(";")

    def skip_to_end(self, kw):
        while not (self.txt() == "end" and self.txt(1) == kw):
            self.eat()
        self.eat()
        self.eat()
        if self.txt() != ";":
            self.eat()
        self.eat(";")

    def skip_subprogram(self, fname):
        depth = par = 0
        while not self.eof():
            t = self.txt()
            par += (t == "(") - (t == ")")
            if t == ";" and depth == 0 and par == 0:
                self.eat()
                return
            if t == "is":
                depth = 1
            if depth and t == "end" and self.txt(1) in ("function", "procedure", fname):
                self.eat()
                while self.txt() != ";":
                    self.eat()
                self.eat(";")
                return
            self.eat()
        raise self.err("a subprogram that does not end")

    def type_decl(self, m):
        self.eat("type")
        name = self.eat()[1]
        self.eat("is")
        if self.txt() == "(":
            self.eat()
            lits = [self.eat()[1]]
            while self.acc(","):
                lits.append(self.eat()[1])
            self.eat(")")
            self.eat(";")
            for i, l in enumerate(lits):
                m.enums[l] = i
            m.enum_types[name] = max(1, (len(lits) - 1).bit_length())
        elif self.txt() == "array":
            self.eat()
            self.eat("(")
            depth = 1
            while depth:
                t = self.eat()[1]
                depth += (t == "(") - (t == ")")
            self.eat("of")
            elem = self.subtype()
            self.eat(";")
            m.types[name] = Ty("array", elem=elem)
        elif self.txt() == "record":
            self.skip_to_end("record")
            m.types[name] = Ty("record")
        else:
            raise self.err("a type declaration I do not know")

    # ---- concurrent statements
    def concurrent(self, m):
        label = None
        if self.pk()[0] == "id" and self.txt(1) == ":" and self.txt(2) != "=":
            label = self.eat()[1]
            self.eat(":")
        w = self.txt()
        where = self.where()
        if w == "process" or (w == "postponed"):
            self.process(m, where)
        elif w == "entity" or (label and w not in ("with", "assert", "block", "for", "if", "process")) or \
                (label is None and self.pk()[0] == "id" and self.txt(1) in ("port", "generic")):
            self.instance(m, label, where)
        elif w == "if" and label:
            self.if_generate(m)
        elif w == "block" and label:
            # a block: its signals are the module's (names are unique in the files the check reads), its statements run as the module's own
            self.eat()
            if self.txt() == "(":
                self.skip_to(")")
            self.acc("is")
            self.arch_decls(m)
            self.eat("begin")
            while self.txt() != "end":
                self.concurrent(m)
            self.eat("end")
            self.eat("block")
            if self.txt() != ";":
                self.eat()
            self.eat(";")
        elif w in ("for", "if", "block", "case"):
            raise self.err("a %s generate/block statement" % w)
        elif w == "assert":
            self.skip_to(";")
        elif w == "with":
            # with SEL select TARGET <= WF when CHOICE {| CHOICE}, ... WF when others;  is the conditional chain of the same choices, in order
            self.eat()
            sel = self.expr()
            self.eat("select")
            lhs = self.target()
            self.eat("<=")
            arms = []
            while True:
                wf = self.expr()
                self.eat("when")
                ch = []
                while True:
                    if self.txt() == "others":
                        self.eat()
                        ch = None
                        break
                    ch.append(self.expr())
                    if not self.acc("|"):
                        break
                arms.append((wf, ch))
                if not self.acc(","):
                    break
            self.eat(";")
            if arms[-1][1] is not None:
                raise self.err("a selected signal assignment without others")
            rhs = arms[-1][0]
            for wf, ch in reversed(arms[:-1]):
                cond = None
                for c in ch:
                    t = ("bin", "=", sel, c)
                    cond = t if cond is None else ("bin", "or", cond, t)
                rhs = ("cond", cond, wf, rhs)
            m.cas.append((lhs, rhs, where))
            m.assigns.setdefault(base_name(lhs)[0], []).append(rhs)
        else:
            lhs = self.target()
            self.eat("<=")
            rhs = self.waveform()
            self.eat(";")
            m.cas.append((lhs, rhs, where))
            m.assigns.setdefault(base_name(lhs)[0], []).append(rhs)

    def gen_cond(self, m, e):
        """An if-generate condition over generics with a default, as an int. The generics it names are recorded in
        m.gen_assumed; an instance that gives one of them another known value gets the module parsed again with it (Ctx.child), and an unknown
        value fails the check (check_assumed)."""
        k = e[0]
        if k == "n":
            if e[1].x:
                raise self.err("a generate condition with an unknown bit")
            return e[1].v
        if k == "id" and e[1] in ("true", "false"):
            return int(e[1] == "true")
        if k == "id" and e[1] in m.generics and m.generics[e[1]] is not None:
            v = self.gen_cond(m, m.generics[e[1]])
            m.gen_assumed[e[1]] = v
            return v
        if k == "un" and e[1] == "not":
            return int(not self.gen_cond(m, e[2]))
        if k == "paren":
            return self.gen_cond(m, e[1])
        if k == "bin" and e[1] in ("=", "/=", "<", ">", "<=", ">=", "and", "or"):
            a, b = self.gen_cond(m, e[2]), self.gen_cond(m, e[3])
            return int({"=": a == b, "/=": a != b, "<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b,
                        "and": bool(a) and bool(b), "or": bool(a) or bool(b)}[e[1]])
        raise self.err("an if-generate condition that is not a comparison of generics with defaults")

    def if_generate(self, m):
        """label: if COND generate ... [elsif COND generate ...] [else generate ...] end generate; only the branch the generic
        defaults select is parsed into the module."""
        taken = False
        self.eat("if")
        while True:
            c = self.gen_cond(m, self.expr())
            self.eat("generate")
            take = bool(c) and not taken
            taken = taken or take
            self.gen_body(m, take)
            if self.txt() == "elsif":
                self.eat()
                continue
            if self.txt() == "else":
                self.eat()
                self.eat("generate")
                self.gen_body(m, not taken)
            break
        self.eat("end")
        self.eat("generate")
        if self.txt() != ";":
            self.eat()
        self.eat(";")

    def gen_body(self, m, take):
        scratch = Mod(m.name, m.lang, m.fname)
        while self.txt() not in ("end", "elsif", "else"):
            self.concurrent(m if take else scratch)

    def waveform(self):
        e = self.expr()
        if self.txt() == "when":
            self.eat()
            c = self.expr()
            if self.txt() == ";":
                # x <= e when rising_edge(clock);  the register takes e at the edge, no value otherwise: read like the clocked processes are (the
                # request cycle's value)
                return ("cond", c, e, ("n", V(0, mask(64), None)))
            self.eat("else")
            rest = self.waveform()
            return ("cond", c, e, rest)
        return e

    def procedure(self, m):
        """A procedure declared in a process: its body is parsed once and inlined at each call (the formals replaced by the actual expressions; only
        in-parameters, a formal is never an assignment target)."""
        self.eat("procedure")
        name = self.eat()[1]
        formals = []
        if self.txt() == "(":
            self.eat()
            while True:
                names = [self.eat()[1]]
                while self.acc(","):
                    names.append(self.eat()[1])
                self.eat(":")
                mode = self.txt()
                if mode in ("in", "out", "inout"):
                    self.eat()
                    if mode != "in":
                        raise self.err("a procedure parameter of mode %s" % mode)
                if self.txt() in ("signal", "variable", "constant"):
                    raise self.err("a procedure parameter class")
                self.subtype()
                if self.acc(":="):
                    self.expr()
                formals += names
                if not self.acc(";"):
                    break
            self.eat(")")
        self.eat("is")
        if self.txt() != "begin":
            raise self.err("a declaration inside a procedure")
        self.eat("begin")
        self.noreg += 1
        body = self.seq(m, ("end",))
        self.noreg -= 1
        self.eat("end")
        self.acc("procedure")
        if self.txt() != ";":
            self.eat()
        self.eat(";")
        self.procs[name] = (formals, body)

    def process(self, m, where):
        self.procs = {}
        self.acc("postponed")
        self.eat("process")
        if self.txt() == "(":
            self.skip_to(")")
        self.acc("is")
        vars_ = {}
        while self.txt() != "begin":
            if self.txt() == "variable":
                self.eat()
                names = [self.eat()[1]]
                while self.acc(","):
                    names.append(self.eat()[1])
                self.eat(":")
                ty = self.subtype()
                init = None
                if self.acc(":="):
                    init = self.expr()
                self.eat(";")
                for n in names:
                    vars_[n] = Decl(n, "var", ty, init)
            elif self.txt() == "procedure":
                self.procedure(m)
            elif self.txt() == "alias":
                self.alias_decl()
            elif self.txt() in ("constant", "type", "subtype"):
                raise self.err("a process-local %s" % self.txt())
            else:
                raise self.err("a process declaration I do not know")
        start = self.i
        self.eat("begin")
        stmts = self.seq(m, ("end",))
        self.eat("end")
        self.eat("process")
        if self.txt() != ";":
            self.eat()
        self.eat(";")
        clocked = any(t[1] in ("rising_edge", "falling_edge") for t in self.t[start:self.i])
        m.procs.append(Proc(clocked, stmts, vars_, where))

    def instance(self, m, label, where):
        if self.acc("entity"):
            name = self.eat()[1]
            if name == "work":
                self.eat(".")
                name = self.eat()[1]
            else:
                while self.acc("."):
                    name = self.eat()[1]
            if self.txt() == "(":
                self.skip_to(")")
        else:
            self.acc("component")
            name = self.eat()[1]
        generics, ports = {}, []
        if self.acc("generic"):
            self.eat("map")
            generics = dict(self.assoc_list())
        if self.acc("port"):
            self.eat("map")
            ports = self.assoc_list()
        self.eat(";")
        m.insts.append(Inst(label, name, ports, generics, where))

    def assoc_list(self):
        self.eat("(")
        out = []
        while True:
            f = self.eat()[1]
            while self.txt() == ".":
                self.eat()
                f += "." + self.eat()[1]
            if self.txt() == "(":
                self.eat()
                depth, txt = 1, ""
                while depth:
                    t = self.eat()[1]
                    depth += (t == "(") - (t == ")")
                    if depth:
                        txt += t
                f += "(" + txt + ")"
            self.eat("=>")
            if self.txt() == "open":
                self.eat()
                a = None
            else:
                a = self.expr()
            out.append((f, a))
            if not self.acc(","):
                break
        self.eat(")")
        return out

    # ---- sequential statements
    def seq(self, m, stop):
        out = []
        while self.txt() not in stop:
            out.append(self.stmt(m))
        return out

    def stmt(self, m):
        w = self.txt()
        where = self.where()
        if w == "if":
            self.eat()
            arms = []
            c = self.expr()
            self.eat("then")
            arms.append((c, self.seq(m, ("elsif", "else", "end"))))
            els = []
            while self.txt() == "elsif":
                self.eat()
                c = self.expr()
                self.eat("then")
                arms.append((c, self.seq(m, ("elsif", "else", "end"))))
            if self.acc("else"):
                els = self.seq(m, ("end",))
            self.eat("end")
            self.eat("if")
            self.eat(";")
            return ("if", arms, els, where)
        if w == "case":
            self.eat()
            sel = self.expr()
            self.eat("is")
            arms = []
            while self.txt() == "when":
                self.eat()
                ch = []
                while True:
                    if self.txt() == "others":
                        self.eat()
                        ch = None
                        break
                    ch.append(self.expr())
                    if self.txt() == "to":
                        self.eat()
                        ch[-1] = ("rng", ch[-1], self.expr())
                    if not self.acc("|"):
                        break
                self.eat("=>")
                arms.append((ch, self.seq(m, ("when", "end"))))
            self.eat("end")
            self.eat("case")
            self.eat(";")
            return ("case", sel, arms, where)
        if w == "for":
            self.eat()
            var = self.eat()[1]
            self.eat("in")
            a = self.expr()
            d = self.eat()[1]
            if d not in ("to", "downto"):
                raise self.err("a loop range")
            b = self.expr()
            self.eat("loop")
            body = self.seq(m, ("end",))
            self.eat("end")
            self.eat("loop")
            self.eat(";")
            return ("for", var, a, b, d, body, where)
        if w == "null":
            self.eat()
            self.eat(";")
            return ("nop",)
        if w in ("assert", "report"):
            self.skip_to(";")
            return ("nop",)
        if w in ("wait", "return", "exit", "next", "while", "loop"):
            raise self.err("a %s statement" % w)
        if w in self.procs and self.txt(1) in (";", "("):
            self.eat()
            args = []
            if self.acc("("):
                args.append(self.expr())
                while self.acc(","):
                    args.append(self.expr())
                self.eat(")")
            self.eat(";")
            formals, body = self.procs[w]
            if len(args) != len(formals):
                raise self.err("a call of %s with named or missing arguments" % w)
            out = [subst_stmt(b, dict(zip(formals, args))) for b in body]
            for b in out:
                reg_assigns(m, b)
            return ("blk", out)
        lhs = self.target()
        if self.acc("<="):
            kind = "sig"
        elif self.acc(":="):
            kind = "var"
        else:
            raise self.err("a statement I do not know")
        rhs = self.waveform()
        self.eat(";")
        if not self.noreg:
            m.assigns.setdefault(base_name(lhs)[0], []).append(rhs)
        return ("as", lhs, rhs, kind, where)

    def target(self):
        return self.primary(lhs=True)

    # ---- expressions
    def expr(self, lvl=0):
        if lvl == len(VHDL_BINLEVELS):
            return self.unary()
        a = self.expr(lvl + 1)
        ops = VHDL_BINLEVELS[lvl]
        while self.txt() in ops and self.pk()[0] in ("id", "op"):
            if self.txt() == "<=" and self.pk()[0] == "op" and False:
                break
            op = self.eat()[1]
            b = self.expr(lvl + 1)
            if lvl == 0:
                a = ("bin", {"nand": "nand", "nor": "nor", "xnor": "xnor"}.get(op, op), a, b)
            else:
                a = ("bin", op, a, b)
        return a

    def unary(self):
        if self.txt() == "not":
            self.eat()
            return ("un", "not", self.unary())
        if self.txt() in ("+", "-") and self.pk()[0] == "op":
            op = self.eat()[1]
            e = self.unary()
            return ("un", op, e)
        if self.txt() == "abs":
            raise self.err("abs")
        e = self.primary()
        if self.txt() == "**":
            self.eat()
            return ("bin", "**", e, self.primary())
        return e

    def primary(self, lhs=False):
        k, s, _ = self.pk()
        if k in ("num", "based", "bs", "str", "chr"):
            self.eat()
            return ("n", num_vhdl(k, s))
        if s == "(":
            self.eat()
            if self.txt() == "others":
                self.eat()
                self.eat("=>")
                e = self.expr()
                self.eat(")")
                return ("others", e)
            e = self.expr()
            if self.txt() in (",", "=>"):
                depth = 1
                while depth:
                    t = self.eat()[1]
                    depth += (t == "(") - (t == ")")
                return ("agg",)
            self.eat(")")
            return ("paren", e)
        if k == "id":
            self.eat()
            name = s
            while self.txt() == "." and self.pk(1)[0] == "id":
                self.eat()
                name += "." + self.eat()[1]
            e = self.aliases[name] if name in self.aliases else ("id", name)
            while self.txt() == "(":
                e = self.suffix(e)
            if self.txt() == "'":
                self.eat()
                attr = self.eat()[1]
                if self.txt() == "(":
                    raise self.err("a qualified expression")
                e = ("attr", e, attr)
            return e
        raise self.err("an expression I do not know")

    def suffix(self, base):
        self.eat("(")
        a = self.expr()
        if self.txt() in ("downto", "to"):
            d = self.eat()[1]
            b = self.expr()
            self.eat(")")
            return ("sl", base, a, b) if d == "downto" else ("sl", base, b, a)
        args = [a]
        while self.acc(","):
            args.append(self.expr())
        self.eat(")")
        return ("call", base, args)


# ---------------------------------------------------------------------------------------------------------------------------
# Verilog parser

VLOG_BIN = [
    ("||",), ("&&",), ("|",), ("^", "~^"), ("&",), ("==", "!=", "===", "!=="), ("<", "<=", ">", ">="), ("<<", ">>", "<<<", ">>>"),
    ("+", "-"), ("*", "/", "%"),
]


class VlogParser(P):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.gv, self.scope, self.labels, self.funcs, self.blk = {}, {}, set(), {}, None

    def parse_file(self):
        mods = {}
        while not self.eof():
            if self.txt() == "module":
                m = self.module()
                mods[m.name] = m
            else:
                raise self.err("a Verilog construct outside a module")
        return mods

    def rng(self):
        """[hi:lo] -> Ty bounds or None."""
        if self.txt() != "[":
            return None
        self.eat()
        hi = self.expr()
        self.eat(":")
        lo = self.expr()
        self.eat("]")
        return hi, lo

    def module(self):
        self.eat("module")
        name = self.eat()[1]
        m = Mod(name, "vlog", self.fname)
        self.gv, self.scope, self.labels, self.funcs, self.blk = {}, {}, set(), {}, None
        if self.txt() == "#":
            self.eat()
            self.eat("(")
            while self.txt() != ")":
                self.acc("parameter")
                while self.txt() in ("integer", "signed") and self.txt(1) != "=":
                    self.eat()
                self.rng()
                n = self.eat()[1]
                self.eat("=")
                m.generics[n] = self.expr()
                if n in self.ovr:
                    m.generics[n] = ("n", kn(self.ovr[n]))
                self.acc(",")
            self.eat(")")
        self.eat("(")
        while self.txt() != ")":
            d = self.txt()
            if d not in ("input", "output", "inout"):
                raise self.err("a port list I do not know")
            self.eat()
            kind = "wire"
            while self.txt() in ("wire", "reg", "signed"):
                t = self.eat()[1]
                if t != "signed":
                    kind = t
            r = self.rng()
            ty = Ty("v", *(r if r else (None, None)))
            names = [self.eat()[1]]
            init = None
            if self.acc("="):
                init = self.expr()
            m.ports[names[0]] = ("in" if d == "input" else "out", ty)
            m.decls[names[0]] = Decl(names[0], kind if kind != "wire" or d == "input" else "wire", ty, init)
            # one direction, type and range for several names: `input wire a, b,`
            while self.txt() == "," and self.txt(1) not in ("input", "output", "inout", ")"):
                self.eat()
                n2 = self.eat()[1]
                m.ports[n2] = ("in" if d == "input" else "out", ty)
                m.decls[n2] = Decl(n2, kind if kind != "wire" or d == "input" else "wire", ty, None)
            self.acc(",")
        self.eat(")")
        self.eat(";")
        while self.txt() != "endmodule":
            self.item(m)
        self.eat("endmodule")
        return m

    def item(self, m):
        w = self.txt()
        where = self.where()
        if w in ("wire", "reg", "integer"):
            self.eat()
            while self.txt() == "signed":
                self.eat()
            r = self.rng()
            ty = Ty("v", *(r if r else (None, None)))
            while True:
                n = self.local_name(self.eat()[1])
                aty = ty
                if self.txt() == "[":
                    self.eat()
                    a = self.expr()
                    self.eat(":")
                    b = self.expr()
                    self.eat("]")
                    aty = Ty("array", elem=ty)
                init = None
                if self.acc("="):
                    init = self.expr()
                    if w == "wire":
                        m.cas.append((("id", n), init, where))
                        m.assigns.setdefault(n, []).append(init)
                        init = None
                if n not in m.decls or m.decls[n].kind == "port":
                    if n in m.decls:
                        m.decls[n].init = init if init is not None else m.decls[n].init
                    else:
                        m.decls[n] = Decl(n, w, aty, init)
                if not self.acc(","):
                    break
            self.eat(";")
        elif w in ("parameter", "localparam"):
            self.eat()
            while self.txt() in ("integer", "signed") and self.txt(1) != "=":
                self.eat()
            if self.txt() == "[":
                self.rng()
            while True:
                n = self.eat()[1]
                self.eat("=")
                m.consts[n] = self.expr()
                if not self.acc(","):
                    break
            self.eat(";")
        elif w == "assign":
            self.eat()
            lhs = self.primary()
            self.eat("=")
            rhs = self.expr()
            self.eat(";")
            for l1, r1 in split_cat(lhs, rhs):
                m.cas.append((l1, r1, where))
                m.assigns.setdefault(base_name(l1)[0], []).append(r1)
        elif w == "always":
            self.eat()
            self.eat("@")
            clocked = False
            if self.txt() == "*":
                self.eat()
            else:
                self.eat("(")
                if self.txt() == "*":
                    self.eat()
                    self.eat(")")
                else:
                    depth = 1
                    while depth:
                        t = self.eat()[1]
                        if t in ("posedge", "negedge"):
                            clocked = True
                        depth += (t == "(") - (t == ")")
            stmt = self.stmt(m)
            m.procs.append(Proc(clocked, [stmt], {}, where))
        elif w == "initial":
            self.eat()
            self.skip_stmt()
        elif w == "function":
            self.function()
        elif w == "task":
            while self.txt() != "endtask":
                self.eat()
            self.eat()
        elif w == "generate":
            self.eat()
            while self.txt() != "endgenerate":
                self.gen_item(m)
            self.eat()
        elif w == "genvar":
            while self.txt() != ";":
                self.eat()
            self.eat()
        elif w == "for":
            self.for_generate(m)
        elif w == "if":                          # an if-generate inside a generate-for body (c64_core.v g_wcpu): the condition is decided with the genvar's value
            self.if_generate(m)
        elif w in ("defparam", "specify"):
            raise self.err("a %s construct" % w)
        elif self.pk()[0] == "id" and w not in VERILOG_KW:
            self.instance(m, where)
        else:
            raise self.err("a module item I do not know")

    def local_name(self, n):
        """The name of a declaration: inside a generate-for block it is the block's own (label, index, name)."""
        if getattr(self, "blk", None) is None:
            return n
        mn = "%s_%d__%s" % (self.blk[0], self.blk[1], n)
        self.scope[n] = mn
        return mn

    def gen_item(self, m):
        if self.txt() == "if":
            self.if_generate(m)
        elif self.txt() == "for":
            self.for_generate(m)
        elif self.txt() == "genvar":
            self.item(m)
        else:
            raise self.err("a generate construct that is neither an if nor a for")

    def for_generate(self, m):
        """generate for (g = A; g < B; g = g + 1) begin : label ... end: the block is parsed once per value of the genvar; what it declares is
        named (label, index, name) and read from outside as label[index].name; the genvar is its value inside."""
        self.eat("for")
        self.eat("(")
        var = self.eat()[1]
        self.eat("=")
        lo = self.gen_val(m, self.expr())
        self.eat(";")
        if self.eat()[1] != var:
            raise self.err("a generate for whose condition is not on its genvar")
        op = self.eat()[1]
        hi = self.gen_val(m, self.expr())
        self.eat(";")
        if self.eat()[1] != var or self.eat()[1] != "=" or self.eat()[1] != var or self.eat()[1] != "+" or self.eat()[1] not in ("1", "1'b1", "1'd1", "32'd1"):
            raise self.err("a generate for whose step is not genvar = genvar + 1")
        self.eat(")")
        if op not in ("<", "<="):
            raise self.err("a generate for with the condition " + op)
        vals = list(range(lo, hi + (1 if op == "<=" else 0)))
        self.eat("begin")
        if not self.acc(":"):
            raise self.err("a generate for block without a name")
        label = self.eat()[1]
        self.labels.add(label)
        start = self.i
        saved = (getattr(self, "blk", None), dict(self.scope), dict(self.gv))
        for v in vals or [None]:
            self.i = start
            self.blk = (label, v if v is not None else 0)
            self.scope = dict(saved[1])
            self.gv = dict(saved[2])
            self.gv[var] = v if v is not None else 0
            tgt = m if v is not None else Mod(m.name, m.lang, m.fname)
            while self.txt() != "end":
                self.item(tgt)
        self.eat("end")
        self.blk, self.scope, self.gv = saved

    def function(self):
        """function [r] name (input [r] a, ...); name = expr; endfunction: a call is replaced by the expression with the arguments put in. A function of another
        shape is skipped (a call of it is an unknown function to the evaluator, as before)."""
        start = self.i
        try:
            self.eat("function")
            while self.txt() in ("signed", "integer") or self.txt() == "[":
                if self.txt() == "[":
                    self.rng()
                else:
                    self.eat()
            name = self.eat()[1]
            self.eat("(")
            params = []
            while self.txt() != ")":
                self.eat("input")
                while self.txt() in ("wire", "reg", "signed"):
                    self.eat()
                self.rng()
                params.append(self.eat()[1])
                self.acc(",")
            self.eat(")")
            self.eat(";")
            self.acc("begin")
            if self.eat()[1] != name:
                raise Unsupported("a function whose body does not assign its name first")
            self.eat("=")
            body = self.expr()
            self.eat(";")
            self.acc("end")
            self.eat("endfunction")
            self.funcs[name] = (params, body)
        except Unsupported:
            self.i = start
            while self.txt() != "endfunction":
                self.eat()
            self.eat()

    def gen_val(self, m, e):
        """A generate condition over parameters with a default, as an int; the parameters it names are recorded in m.gen_assumed."""
        k = e[0]
        if k == "n":
            if e[1].x:
                raise self.err("a generate condition with an unknown bit")
            return e[1].v
        if k == "paren":
            return self.gen_val(m, e[1])
        if k == "id":
            if e[1] in m.consts:
                return self.gen_val(m, m.consts[e[1]])
            if e[1] in m.generics and m.generics[e[1]] is not None:
                v = self.gen_val(m, m.generics[e[1]])
                m.gen_assumed[e[1]] = v
                return v
        if k == "un" and e[1] in ("!", "~", "-"):
            a = self.gen_val(m, e[2])
            return {"!": int(not a), "~": ~a, "-": -a}[e[1]]
        if k == "bin":
            ops = {"==": lambda a, b: int(a == b), "!=": lambda a, b: int(a != b), "<": lambda a, b: int(a < b),
                   ">": lambda a, b: int(a > b), "<=": lambda a, b: int(a <= b), ">=": lambda a, b: int(a >= b),
                   "&&": lambda a, b: int(bool(a) and bool(b)), "||": lambda a, b: int(bool(a) or bool(b)),
                   "+": lambda a, b: a + b, "-": lambda a, b: a - b, "*": lambda a, b: a * b, "&": lambda a, b: a & b,
                   "|": lambda a, b: a | b}
            if e[1] in ops:
                return ops[e[1]](self.gen_val(m, e[2]), self.gen_val(m, e[3]))
        raise self.err("a generate condition that is not an expression of parameters with defaults")

    def gen_block(self, m, take):
        """begin [: label] items end, parsed into m when take, else into a scratch module."""
        tgt = m if take else Mod(m.name, m.lang, m.fname)
        if self.txt() != "begin":                # one item without begin and end (`if (gi == 0) assign w[gi] = x;`)
            self.item(tgt)
            return
        self.eat("begin")
        if self.acc(":"):
            self.eat()
        while self.txt() != "end":
            self.item(tgt)
        self.eat("end")

    def if_generate(self, m):
        self.eat("if")
        self.eat("(")
        c = self.gen_val(m, self.expr())
        self.eat(")")
        self.gen_block(m, bool(c))
        if self.acc("else"):
            if self.txt() == "if":
                # else if: the rest is an if-generate of its own, taken only if this one was not
                if c:
                    scratch = Mod(m.name, m.lang, m.fname)
                    scratch.generics, scratch.consts, scratch.gen_assumed = m.generics, m.consts, m.gen_assumed
                    self.if_generate(scratch)
                else:
                    self.if_generate(m)
            else:
                self.gen_block(m, not c)

    def skip_stmt(self):
        if self.txt() == "begin":
            depth = 0
            while True:
                t = self.eat()[1]
                if t == "begin":
                    depth += 1
                elif t == "end":
                    depth -= 1
                    if depth == 0:
                        return
        else:
            self.skip_to(";")

    def skip_to(self, s):
        while self.txt() != s:
            self.eat()
        self.eat(s)

    def instance(self, m, where):
        name = self.eat()[1]
        generics = {}
        if self.txt() == "#":
            self.eat()
            self.eat("(")
            while self.txt() != ")":
                if self.txt() == ".":
                    self.eat()
                    g = self.eat()[1]
                    self.eat("(")
                    generics[g] = self.expr()
                    self.eat(")")
                else:
                    raise self.err("a positional parameter")
                self.acc(",")
            self.eat(")")
        label = self.eat()[1]
        self.eat("(")
        ports = []
        while self.txt() != ")":
            self.eat(".")
            f = self.eat()[1]
            self.eat("(")
            a = None if self.txt() == ")" else self.expr()
            self.eat(")")
            ports.append((f, a))
            self.acc(",")
        self.eat(")")
        self.eat(";")
        m.insts.append(Inst(label, name, ports, generics, where))

    def stmt(self, m):
        w = self.txt()
        where = self.where()
        if w == "begin":
            self.eat()
            if self.txt() == ":":
                self.eat()
                self.eat()
            out = []
            while self.txt() != "end":
                out.append(self.stmt(m))
            self.eat("end")
            return ("blk", out)
        if w == "if":
            self.eat()
            self.eat("(")
            c = self.expr()
            self.eat(")")
            body = [self.stmt(m)]
            arms = [(c, body)]
            els = []
            while self.txt() == "else":
                self.eat()
                if self.txt() == "if":
                    self.eat()
                    self.eat("(")
                    c = self.expr()
                    self.eat(")")
                    arms.append((c, [self.stmt(m)]))
                else:
                    els = [self.stmt(m)]
                    break
            return ("if", arms, els, where)
        if w in ("case", "casez", "casex"):
            if w != "case":
                raise self.err("a %s statement" % w)
            self.eat()
            self.eat("(")
            sel = self.expr()
            self.eat(")")
            arms = []
            while self.txt() != "endcase":
                if self.txt() == "default":
                    self.eat()
                    self.acc(":")
                    ch = None
                else:
                    ch = [self.expr()]
                    while self.acc(","):
                        ch.append(self.expr())
                    self.eat(":")
                arms.append((ch, [self.stmt(m)]))
            self.eat("endcase")
            return ("case", sel, arms, where)
        if w == ";":
            self.eat()
            return ("nop",)
        if w == "for":
            # for (i = A; i <= B; i = i + 1) STMT   (also <, and i = i - 1 with >= / >): unrolled by the evaluator, like the VHDL for loop
            self.eat()
            self.eat("(")
            var = self.eat()[1]
            self.eat("=")
            lo = self.expr()
            self.eat(";")
            if self.eat()[1] != var:
                raise self.err("a for loop whose condition is not on its variable")
            op = self.eat()[1]
            hi = self.expr()
            self.eat(";")
            if self.eat()[1] != var or self.eat()[1] != "=" or self.eat()[1] != var:
                raise self.err("a for loop whose step is not on its variable")
            sg = self.eat()[1]
            one = self.eat()
            self.eat(")")
            if one[1] != "1'b1" and not (one[0] == "num" and one[1] in ("1",)) and one[1] not in ("1", "1'd1", "32'd1"):
                raise self.err("a for loop with a step other than one")
            if (sg, op) == ("+", "<="):
                d, top = "to", hi
            elif (sg, op) == ("+", "<"):
                d, top = "to", ("bin", "-", hi, ("n", kn(1)))
            elif (sg, op) == ("-", ">="):
                d, top = "downto", hi
            elif (sg, op) == ("-", ">"):
                d, top = "downto", ("bin", "+", hi, ("n", kn(1)))
            else:
                raise self.err("a for loop I do not know")
            body = self.stmt(m)
            return ("for", var, lo, top, d, [body], where)
        if w in ("for", "while", "repeat", "forever", "wait", "disable", "fork", "force", "release", "#", "@"):
            raise self.err("a %s statement" % w)
        if w.startswith("$"):
            self.skip_to(";")
            return ("nop",)
        lhs = self.primary()
        if self.txt() == "<=":
            self.eat()
            kind = "sig"
        elif self.txt() == "=":
            self.eat()
            kind = "var"
        else:
            raise self.err("a statement I do not know")
        rhs = self.expr()
        self.eat(";")
        out = []
        for l1, r1 in split_cat(lhs, rhs):
            m.assigns.setdefault(base_name(l1)[0], []).append(r1)
            out.append(("as", l1, r1, kind, where))
        return out[0] if len(out) == 1 else ("blk", out)

    # ---- expressions
    def expr(self):
        c = self.binary(0)
        if self.txt() == "?":
            self.eat()
            a = self.expr()
            self.eat(":")
            b = self.expr()
            return ("cond", c, a, b)
        return c

    def binary(self, lvl):
        if lvl == len(VLOG_BIN):
            return self.unary()
        a = self.binary(lvl + 1)
        while self.txt() in VLOG_BIN[lvl] and self.pk()[0] == "op":
            op = self.eat()[1]
            b = self.binary(lvl + 1)
            a = ("bin", op, a, b)
        return a

    def unary(self):
        k, s, _ = self.pk()
        if k == "op" and s in ("!", "~", "-", "+", "&", "|", "^", "~&", "~|", "~^"):
            self.eat()
            e = self.unary()
            return ("un", s, e)
        return self.primary()

    def primary(self):
        k, s, _ = self.pk()
        if k in ("num", "str"):
            self.eat()
            return ("n", num_vlog(s))
        if s == "(":
            self.eat()
            e = self.expr()
            self.eat(")")
            return ("paren", e)
        if s == "{":
            self.eat()
            first = self.expr()
            if self.txt() == "{":
                self.eat()
                inner = self.expr()
                parts = [inner]
                while self.acc(","):
                    parts.append(self.expr())
                self.eat("}")
                self.eat("}")
                return ("rep", first, ("cat", parts))
            parts = [first]
            while self.acc(","):
                parts.append(self.expr())
            self.eat("}")
            return ("cat", parts)
        if k == "id":
            self.eat()
            if s in self.gv:
                return ("n", kn(self.gv[s], 32))
            if s in self.labels and self.txt() == "[" and self.txt(2) == "]" and self.txt(3) == ".":
                # label[index].name: a name declared in an unrolled generate block
                self.eat()
                ix = self.expr()
                self.eat("]")
                self.eat(".")
                if ix[0] != "n" or ix[1].x:
                    raise self.err("a generate block index that is not a constant")
                s = "%s_%d__%s" % (s, ix[1].v, self.eat()[1])
            e = ("id", self.scope.get(s, s))
            if s.startswith("$") or self.txt() == "(":
                self.eat("(")
                args = [self.expr()]
                while self.acc(","):
                    args.append(self.expr())
                self.eat(")")
                if s in self.funcs:
                    params, body = self.funcs[s]
                    if len(params) != len(args):
                        raise self.err("a call of %s with the wrong number of arguments" % s)
                    return ("paren", subst(body, dict(zip(params, args))))
                return ("call", ("id", s), args)
            while self.txt() == "[":
                self.eat()
                a = self.expr()
                if self.txt() == ":":
                    self.eat()
                    b = self.expr()
                    self.eat("]")
                    e = ("sl", e, a, b)
                elif self.txt() in ("+:", "-:"):
                    op = self.eat()[1]
                    n = self.expr()
                    self.eat("]")
                    e = ("psl", e, a, n, op)
                else:
                    self.eat("]")
                    e = ("ix", e, a)
            return e
        raise self.err("an expression I do not know")


# ---------------------------------------------------------------------------------------------------------------------------
# the design: source list and the module index

def tcl_vars(text):
    vs = {}
    for m in re.finditer(r"^\s*set\s+(\w+)\s+(\S+)\s*$", text, re.M):
        val = m.group(2).strip('"{}')
        val = re.sub(r"\$(\w+)|\$\{(\w+)\}", lambda k: vs.get(k.group(1) or k.group(2), k.group(0)), val)
        vs[m.group(1)] = val
    return vs


def tcl_text(path, seen=None, cwd=None):
    """The text of a source list with the lists it `source`s (a literal path, relative to the design directory: Vivado runs there) put in
    front of the line that sources them, so that their `set` lines and file names are read like the including list's own."""
    seen = seen if seen is not None else set()
    path = os.path.normpath(path)
    cwd = cwd or os.path.dirname(path)
    seen.add(path)
    out = []
    for line in open(path).read().split("\n"):
        m = re.match(r"\s*source\s+(\S+)\s*$", line)
        if m and "$" not in m.group(1):
            sub = os.path.normpath(os.path.join(cwd, m.group(1)))
            if os.path.isfile(sub) and sub not in seen and sub.endswith(".tcl") and "/tools/" not in sub:
                out.append(tcl_text(sub, seen, cwd))
                continue
        out.append(line)
    return "\n".join(out)


class Design:
    def __init__(self, ddir, listname="build.tcl"):
        self.ddir = os.path.abspath(ddir)
        self.files = {}
        self.index = {}
        self.cache = {}
        self.records = None
        text = ""
        for ln in ([listname] if isinstance(listname, str) else listname):
            src = os.path.join(self.ddir, ln)
            if not os.path.isfile(src):
                raise CheckError("no source list %s" % src)
            text += tcl_text(src) + "\n"
        vs = tcl_vars(text)
        for m in re.finditer(r"[\w$./{}*+-]+\.(?:vhdl?|sv|v)\b", text):
            t = m.group()
            t = re.sub(r"\$\{?(\w+)\}?", lambda k: vs.get(k.group(1), k.group(0)), t)
            if "$" in t or "{" in t:
                continue
            for pth in (glob.glob(os.path.join(self.ddir, t)) if "*" in t else [os.path.join(self.ddir, t)]):
                if os.path.isfile(pth):
                    self.files[os.path.normpath(pth)] = True
        self.denied = sorted(f for f in self.files if self.blocked(f))
        for f in sorted(self.files):
            if self.blocked(f):
                continue
            body = read_source(f)
            if f.endswith((".vhd", ".vhdl")):
                for m in re.finditer(r"^\s*entity\s+(\w+)\s+is", body, re.M | re.I):
                    self.index.setdefault(m.group(1).lower(), f)
            else:
                for m in re.finditer(r"^\s*module\s+(\w+)", body, re.M):
                    self.index.setdefault(m.group(1), f)

    def blocked(self, f):
        return denied(f)

    def load(self, f, ovr=None):
        f = os.path.normpath(f)
        if f not in self.files:
            raise Unsupported("%s is not in the design's source list" % f)
        if self.blocked(f):
            raise Unsupported("%s is never opened (clean room)" % f)
        ck = (f, tuple(sorted((ovr or {}).items())))
        if ck in self.cache:
            return self.cache[ck]
        text = read_source(f)
        if f.endswith((".vhd", ".vhdl")):
            mods = VhdlParser(lex(text, "vhdl", f), f, ovr).parse_file()
        else:
            mods = VlogParser(lex(text, "vlog", f), f, ovr).parse_file()
        self.cache[ck] = mods
        return mods

    def module(self, name, ovr=None):
        key = name.lower() if name.lower() in self.index else name
        f = self.index.get(key)
        if f is None:
            raise Unsupported("the module %s is not in the design's source list" % name)
        mods = self.load(f, ovr)
        for k, m in mods.items():
            if k.lower() == name.lower():
                return m
        raise Unsupported("%s: the module %s did not parse out" % (f, name))

    def record_fields(self, tyname):
        """{field: (width, ...)} of a record type found in the VHDL sources of the list."""
        if self.records is None:
            self.records = {}
            for f in sorted(self.files):
                if not f.endswith((".vhd", ".vhdl")) or self.blocked(f):
                    continue
                body = read_source(f)
                body = re.sub(r"--[^\n]*", "", body)
                for m in re.finditer(r"\btype\s+(\w+)\s+is\s+record\b(.*?)\bend\s+record", body, re.S | re.I):
                    flds = []
                    for fm in re.finditer(r"(\w+(?:\s*,\s*\w+)*)\s*:\s*([^;]+);", m.group(2)):
                        for nm in fm.group(1).split(","):
                            flds.append((nm.strip().lower(), fm.group(2).strip().lower()))
                    self.records.setdefault(m.group(1).lower(), flds)
        return self.records.get(tyname)


def top_instances(design, topname, extra_names):
    """The instantiations of the top-level Verilog file, read by a scan (the file is huge and not decoded): module, label, {formal: expr}."""
    f = design.index.get(topname)
    if f is None:
        raise CheckError("no top module %s in the source list" % topname)
    text = read_source(f)
    text = re.sub(r"//[^\n]*|/\*.*?\*/", " ", text, flags=re.S)
    m = Mod(topname, "vlog", f)
    for dm in re.finditer(r"\b(?:wire|reg)\s*(?:\[\s*(\d+)\s*:\s*(\d+)\s*\])?\s*([\w\s,]+?)\s*(?:=|;)", text):
        w = int(dm.group(1)) - int(dm.group(2)) + 1 if dm.group(1) else 1
        for n in dm.group(3).split(","):
            n = n.strip()
            if re.fullmatch(r"\w+", n):
                m.nets.setdefault(n, w)
    # the ports of the top module itself (ANSI style, one name each): their widths too
    for dm in re.finditer(r"\b(?:input|output|inout)\s+(?:wire\s+|reg\s+)?(?:\[\s*(\d+)\s*:\s*(\d+)\s*\]\s*)?(\w+)\s*[,)]", text):
        m.nets.setdefault(dm.group(3), int(dm.group(1)) - int(dm.group(2)) + 1 if dm.group(1) else 1)
    # the parameters and localparams of the top file (its `ifdef branches decided by the defines): an instance's generic that names one has its value
    toks = lex(text, "vlog", f)
    for ti, tk in enumerate(toks):
        if tk[1] in ("parameter", "localparam"):
            pp = VlogParser(toks[ti + 1:], f)
            try:
                while pp.txt() in ("integer", "signed"):
                    pp.eat()
                if pp.txt() == "[":
                    pp.rng()
                while True:
                    nm = pp.eat()[1]
                    pp.eat("=")
                    m.consts[nm] = pp.expr()
                    if not (pp.txt() == "," and pp.txt(1) not in ("parameter", "localparam") and pp.txt(2) == "="):
                        break
                    pp.eat()
            except Unsupported:
                pass
    # the register stage of the top file: `always @(posedge clk) [begin] q <= expr; ... [end]` (assignments only): q holds the value of expr one clock later
    m.regcopies = {}
    for am in re.finditer(r"\balways\s*@\s*\(\s*posedge\s+\w+\s*\)\s*(?:begin\s*)?((?:\w+\s*<=[^;]+;\s*)+)(?:end\b)?", text):
        for sm in re.finditer(r"(\w+)\s*<=\s*([^;]+);", am.group(1)):
            m.regcopies[sm.group(1)] = VlogParser(lex(sm.group(2).strip(), "vlog", f), f).expr()
    names = set(design.index) | set(extra_names)
    pos = 0
    pat = re.compile(r"\b([A-Za-z_]\w*)\s*(#\s*\()?")
    while True:
        mm = pat.search(text, pos)
        if not mm:
            break
        name = mm.group(1)
        j = mm.end()
        gen = {}
        if mm.group(2):
            depth, j0 = 1, j
            while depth and j < len(text):
                depth += (text[j] == "(") - (text[j] == ")")
                j += 1
            for gm in re.finditer(r"\.(\w+)\s*\(((?:[^()]|\([^()]*\))*)\)", text[j0:j - 1]):
                gen[gm.group(1)] = VlogParser(lex(gm.group(2).strip(), "vlog", f), f).expr()
        lm = re.compile(r"\s*([A-Za-z_]\w*)\s*\(\s*\.").match(text, j)
        if name in names and name not in VERILOG_KW and lm:
            j = lm.start() + lm.group(0).index("(")
            depth, k = 1, j + 1
            while depth and k < len(text):
                depth += (text[k] == "(") - (text[k] == ")")
                k += 1
            body = text[j + 1:k - 1]
            ports = []
            pk, depth, cur = 0, 0, ""
            parts = []
            for ch in body:
                if ch in "({[":
                    depth += 1
                elif ch in ")}]":
                    depth -= 1
                if ch == "," and depth == 0:
                    parts.append(cur)
                    cur = ""
                else:
                    cur += ch
            parts.append(cur)
            for p in parts:
                pm = re.fullmatch(r"\s*\.(\w+)\s*\((.*)\)\s*", p, re.S)
                if not pm:
                    raise Unsupported("%s: a port connection of %s I cannot read: %r" % (f, name, p.strip()[:40]))
                a = pm.group(2).strip()
                ports.append((pm.group(1), VlogParser(lex(a, "vlog", f), f).expr() if a else None))
            m.insts.append(Inst(lm.group(1), name, ports, gen, f))
            pos = k
        else:
            pos = mm.end()
    return m


# ---------------------------------------------------------------------------------------------------------------------------
# evaluation of the request through the design

TYPE_SCALARS = {"std_logic": 1, "std_ulogic": 1, "bit": 1, "boolean": 1, "integer": 32, "natural": 32, "positive": 32}
VEC_TYPES = {"std_logic_vector", "unsigned", "signed", "std_ulogic_vector", "bit_vector"}
PASS_FUNCS = {"unsigned", "signed", "std_logic_vector", "std_ulogic_vector", "$signed", "$unsigned"}


class Ent:
    __slots__ = ("v", "alts")

    def __init__(self, v, alts=()):
        self.v, self.alts = v, alts


CORE_PORTS = ("cw_", "dma_", "drv_io_", "gcr_io_", "sidcv_")


class Eff:
    __slots__ = ("key", "rhs", "v", "certain", "taint", "ctx", "where", "guard")

    def __init__(self, key, rhs, v, certain, taint, ctx, where, guard=frozenset()):
        self.key, self.rhs, self.v, self.certain, self.taint, self.ctx, self.where, self.guard = key, rhs, v, certain, taint, ctx, where, guard


class Run:
    def __init__(self):
        self.store = {}
        self.effects = []
        self.vt = {}


class Sc:
    __slots__ = ("st", "readall", "names", "snap")

    def __init__(self, st, readall, names):
        self.st, self.readall, self.names, self.snap = st, readall, names, None


class Ctx:
    def __init__(self, ev, mod, parent, inst):
        self.ev, self.mod, self.parent, self.inst = ev, mod, parent, inst
        self.cache, self.runs, self.kids, self.running = {}, {}, {}, set()
        self.certain = False
        self.tcache, self.ocache, self.ptaint = {}, {}, {}
        self.drv = None
        self.name = (parent.name + "." if parent else "") + (inst.label if inst else mod.name)

    def child(self, inst):
        c = self.kids.get(inst.label)
        if c is None:
            mod = self.ev.d.module(inst.modname)
            ovr = {}
            for gn, want in mod.gen_assumed.items():
                if gn in inst.generics:
                    got = self.ev.ev(self, inst.generics[gn])
                    if not got.x and got.v != want:
                        ovr[gn] = got.v
            if ovr:
                # an if-generate of the module was decided with a default this instance does not have: the module is parsed again with the
                # instance's value for that generic, the generate branches follow it
                mod = self.ev.d.module(inst.modname, ovr)
            c = Ctx(self.ev, mod, self, inst)
            self.kids[inst.label] = c
        return c


def walk_assigned(stmts, out):
    for s in stmts:
        k = s[0]
        if k == "as":
            out.add(base_name(s[1])[0])
        elif k == "blk":
            walk_assigned(s[1], out)
        elif k == "if":
            for _, b in s[1]:
                walk_assigned(b, out)
            walk_assigned(s[2], out)
        elif k == "case":
            for _, b in s[2]:
                walk_assigned(b, out)
        elif k == "for":
            walk_assigned(s[5], out)


def ids_of(e, out):
    if not isinstance(e, tuple):
        return
    if e[0] == "id":
        out.add(e[1])
        return
    if e[0] == "n":
        return
    for x in e[1:]:
        if isinstance(x, tuple):
            ids_of(x, out)
        elif isinstance(x, list):
            for y in x:
                ids_of(y, out)


class Ev:
    """One stimulus: the request address (low bits maybe unknown), write or read, data unknown."""

    def __init__(self, design, topmod, busmod, busname, addr, write, link=None, ports=None, pipe=False):
        """link: the request arrives over a handshake from another module (stage 2): (the receiving instance, {input port: value},
        {input port: taint}, [response output ports]). The receiver takes the place of the bus module."""
        self.d = design
        self.pipe_ready = False
        self.link_t, self.fwd_live = {}, False
        self.pipe = pipe        # stage 2 with the module's register pipeline modelled: its copies of the held request take the request's value (build_pipe)
        self.memcap = None
        self.reset_off = 0
        self.hold, self.holdt = {}, {}      # held requests: (id(ctx), register) -> value / taint in the decode cycle (find_held)
        self.readref = set()  # (module, signal, place) of the effects the same address has as a read: a write by strobe is none of them
        self.bus, self.rootv, self.roott = None, {}, {}
        self.top = Ctx(self, topmod, None, None)
        self.busname = busname
        if link is not None:
            self.bus = self.top.child(link[0])
            self.check_assumed(self.bus)
            self.rootv, self.roott = dict(link[1]), dict(link[2])
            # a port of the handshake carries the data of the request ("d"), or its address with what the value was made of (find_links: link_t, the write flag or the address compare of a strobe
            # that a continuous assignment of the sender decodes); a port whose value is the sender's state alone has the address taint only
            self.roott = {n: (frozenset("d") if "d" in t else frozenset("a") | (t & {"q", "w", "r"})) for n, t in self.roott.items()}
            if pipe:
                # a one-bit port of the handshake that carries no value (the toggle) is a strobe of the transport: no address, no decode
                for n, v in self.rootv.items():
                    if v.w == 1 and v.x and self.roott[n] == frozenset("a"):
                        self.roott[n] = frozenset("t")
            self.resp = [(self.bus, n) for n in link[3]]
            self.followed = [self.bus]
            self.phase, self.post = 1, {}
            self.crossed = True
            return
        binst = [i for i in topmod.insts if i.modname.lower() == busmod.lower()]
        if busmod.lower() == topmod.name.lower() and ports is not None:
            self.bus = self.top         # the request is the ports of the top module itself
        elif len(binst) != 1:
            raise CheckError("the bus module %s is instantiated %d times in %s" % (busmod, len(binst), topmod.name))
        else:
            self.bus = self.top.child(binst[0])
        if ports is not None:
            # a partial's window: the request is four input ports of the instance (write, read, address, data) and its answer is one output port
            pw = {r: (self.top.mod.nets[ports[r]] if self.bus is self.top else self.width(self.bus, ports[r])) for r in ("write", "read", "address", "data")}
            self.rootv = {
                ports["write"]: T1 if write else F1,
                ports["read"]: F1 if write else T1,
                ports["address"]: fit(addr, pw["address"]),
                ports["data"]: allx(pw["data"]),
            }
            self.roott = {n: frozenset("d" if n == ports["data"] else {ports["write"]: "aw", ports["read"]: "ar"}.get(n, "aq")) for n in self.rootv}
            self.resp = [(self.bus, ports["response"])]
            self.followed = [self.bus]
            self.phase, self.post = 1, {}
            self.crossed = False
            return
        flds = dict(self.record_widths(self.bus, busname))
        self.rootv = {
            busname + ".address": fit(addr, flds["address"]),
            busname + ".write": T1 if write else F1,
            busname + ".read": F1 if write else T1,
            busname + ".data": allx(flds["data"]),
        }
        self.roott = {n: frozenset(("d",) if n.endswith(".data") else (("a", "w") if n.endswith(".write") else (("a", "r") if n.endswith(".read") else ("a", "q")))) for n in self.rootv}
        self.resp = [(self.bus, busname.replace("req", "resp") + ".data")]
        self.followed = [self.bus]
        self.phase, self.post = 1, {}
        self.crossed = False

    def record_widths(self, ctx, sig):
        decl = ctx.mod.decls.get(sig)
        if decl is None:
            raise CheckError("the bus signal %s is not declared in %s" % (sig, ctx.mod.name))
        fl = self.d.record_fields(decl.ty.name)
        if not fl:
            raise CheckError("the type %s of %s is not a record in the source list" % (decl.ty.name, sig))
        out = []
        for n, t in fl:
            m = re.search(r"\((\d+)\s+downto\s+(\d+)\)", t)
            out.append((n, int(m.group(1)) - int(m.group(2)) + 1 if m else 1))
        return out

    # ---- types
    def const_int(self, ctx, e, sc=None):
        v = self.ev(ctx, e, sc)
        if v.x or v.v is None:
            raise Unsupported("%s: a bound that is not a constant" % ctx.mod.fname)
        return v.v

    def tyinfo(self, ctx, ty):
        mod = ctx.mod
        n = ty.name
        if n in mod.enum_types:
            return ("v", mod.enum_types[n], 0)
        if n in TYPE_SCALARS:
            return ("v", TYPE_SCALARS[n], 0)
        if n in VEC_TYPES or n == "v":
            if ty.hi is None:
                return ("v", 1, 0)
            hi, lo = self.const_int(ctx, ty.hi), self.const_int(ctx, ty.lo)
            return ("v", hi - lo + 1, lo)
        if n == "array":
            return ("arr", self.tyinfo(ctx, ty.elem)[1])
        t2 = mod.types.get(n)
        if t2 is not None:
            if t2.name == "array":
                return ("arr", self.tyinfo(ctx, t2.elem)[1])
            if t2.name == "record":
                return ("rec", n)
            return self.tyinfo(ctx, t2)
        if self.d.record_fields(n):
            return ("rec", n)
        raise Unsupported("the type %s" % n)

    def width(self, ctx, name):
        base, _, field = name.partition(".")
        d = ctx.mod.decls.get(base)
        if d is None:
            raise Unsupported("%s: the identifier %s is not declared" % (ctx.mod.fname, name))
        ti = self.tyinfo(ctx, d.ty)
        if ti[0] == "rec":
            if not field:
                raise Unsupported("a record %s used as a value" % name)
            for n, t in self.d.record_fields(ti[1]) or []:
                if n == field:
                    m = re.search(r"\((\d+)\s+downto\s+(\d+)\)", t)
                    return int(m.group(1)) - int(m.group(2)) + 1 if m else 1
            raise Unsupported("the record %s has no field %s" % (ti[1], field))
        if ti[0] == "arr":
            return ti[1]
        return ti[1]

    def lowbound(self, ctx, e):
        if e[0] == "id":
            d = ctx.mod.decls.get(e[1])
            if d is not None and "." not in e[1]:
                ti = self.tyinfo(ctx, d.ty)
                if ti[0] == "v":
                    return ti[2]
        if e[0] == "paren":
            return self.lowbound(ctx, e[1])
        return 0

    def is_array(self, ctx, name):
        d = ctx.mod.decls.get(name)
        return d is not None and self.tyinfo(ctx, d.ty)[0] == "arr"

    # ---- values
    def val(self, ctx, name):
        h = self.hold.get((id(ctx), name))
        if h is not None:
            return fit(h, self.width(ctx, name))        # a held register has the request's value, wherever the evaluation starts (not through the run of its own process, which would find it in progress)
        c = ctx.cache
        if name in c:
            v = c[name]
            return allx(self.width(ctx, name)) if v is None else v
        c[name] = None
        v = self._val(ctx, name)
        c[name] = v
        return v

    def _val(self, ctx, name):
        mod = ctx.mod
        if ctx is self.bus and name in self.rootv:
            return self.rootv[name]
        if name in mod.enums:
            return kn(mod.enums[name], 8)
        if name in ("true", "false") and name not in mod.decls:
            return T1 if name == "true" else F1
        if name in mod.consts:
            return self.ev(ctx, mod.consts[name])
        if name in mod.generics:
            if name in mod.gen_assumed:
                if ctx.inst is not None and name in ctx.inst.generics:
                    got = self.ev(ctx.parent, ctx.inst.generics[name])
                else:
                    got = self.ev(ctx, mod.generics[name])
                if got.x or got.v != mod.gen_assumed[name]:
                    raise Unsupported("%s: the generic %s is %s here, but an if-generate of the module was decided with its default %s"
                                      % (mod.fname, name, got, mod.gen_assumed[name]))
            if ctx.inst is not None and name in ctx.inst.generics:
                return self.ev(ctx.parent, ctx.inst.generics[name])
            if mod.generics[name] is not None:
                return self.ev(ctx, mod.generics[name])
            return allx(32)
        if ctx.parent is None and ctx.mod.nets:
            return self.net_val(ctx, name)
        base = name.partition(".")[0]
        if base not in mod.decls:
            pc = self.d.pkg_const(name)
            if pc is not None:
                return pc
            pe = self.d.pkg_expr(name) if "." not in name else None
            if pe is not None:
                return self.ev(ctx, pe)
            raise Unsupported("%s: the identifier %s is not declared" % (mod.fname, name))
        port = mod.ports.get(base)
        if port is not None and port[0] == "in":
            return self.port_in(ctx, name)
        return self.driven(ctx, name)

    def port_in(self, ctx, name):
        w = self.width(ctx, name)
        if ctx.parent is None or ctx.inst is None:
            return allx(w)
        base, _, field = name.partition(".")
        for f, a in ctx.inst.ports:
            if f == base:
                if a is None:
                    break
                if field:
                    if a[0] == "id":
                        return self.val(ctx.parent, a[1] + "." + field)
                    raise Unsupported("%s: a record port %s with an expression" % (ctx.inst.where, base))
                return fit(self.ev(ctx.parent, a), w)
        d = ctx.mod.decls[base].init
        return fit(self.ev(ctx, d), w) if d is not None and not field else allx(w)

    def net_val(self, ctx, name):
        """A net of the top-level file: its value is that of the followed instance that drives it, else unknown."""
        w = ctx.mod.nets.get(name)
        rc = getattr(ctx.mod, "regcopies", {}).get(name) if self.bus is self.top else None      # only where the top module's ports are the bus
        if rc is not None:
            self.reset_off += 1                         # a register stage of the top file holds the request one clock later, the reset released
            try:
                return fit(self.ev(ctx, rc), w or 1)
            finally:
                self.reset_off -= 1
        if self.reset_off and re.search(r"(^|_)(rst|reset)(_n)?$", name):
            return kn(1 if name.endswith("_n") else 0, w or 1)
        for inst in ctx.mod.insts:
            for f, a in inst.ports:
                if a is not None and a[0] == "id" and a[1] == name and self.d.port_dir(inst.modname, f) == "out":
                    ch = [c for c in self.followed if c.parent is ctx and c.inst is inst]
                    if ch:
                        return self.val(ch[0], f)
                    return allx(w or 1)
        return allx(w or 1)

    def build_drv(self, ctx):
        drv = {}
        for lhs, rhs, wh in ctx.mod.cas:
            drv.setdefault(base_name(lhs)[0], []).append(("ca", lhs, rhs))
        for p in ctx.mod.procs:
            out = set()
            walk_assigned(p.stmts, out)
            for k in out:
                drv.setdefault(k, []).append(("proc", p))
        for inst in ctx.mod.insts:
            mn = inst.modname.lower()
            if mn in SPLITTERS:
                si = self.split_info(ctx, inst)
                for i, a in si["reqs"].items():
                    drv.setdefault(a[1], []).append(("split", inst, i))
                if si["resp"] is not None:
                    drv.setdefault(si["resp"], []).append(("splitr", inst))
                continue
            if mn in PASSTHROUGH:
                for f, a in inst.ports:
                    if f in PASSTHROUGH[mn] and a is not None and a[0] == "id":
                        drv.setdefault(a[1], []).append(("pass", inst, f))
                continue
            for f, a in inst.ports:
                if a is not None and a[0] == "id" and self.d.port_dir(inst.modname, f) == "out":
                    drv.setdefault(a[1], []).append(("inst", inst, f))
        ctx.drv = drv

    def native(self, inst):
        return inst.modname.lower() in SPLITTERS or inst.modname.lower() in PASSTHROUGH or inst.modname.lower() in MEMORIES

    def memory_write(self, ctx):
        """(a RAM of this module takes a write from the request, certainly): the write enable is 1 or may be 1 for the request's address, the data port carries the
        data and the address port the request's address. self.memcap is the smallest size in bytes of such a RAM (2 ** address bits * data bytes)."""
        hit = None
        for inst in ctx.mod.insts:
            mem = MEMORIES.get(inst.modname.lower())
            if mem is None:
                continue
            ports = {f.lower(): a for f, a in inst.ports}
            we, di, ad = ports.get(mem[0]), ports.get(mem[1]), ports.get(mem[2])
            if we is None or di is None or ad is None or "a" not in self.taint(ctx, we) or "d" not in self.taint(ctx, di) or "a" not in self.taint(ctx, ad):
                continue
            if mem[0] == "a_we":       # dpram: g_depth_bits address bits of g_width_bits (the entity's declaration, defaults 9 and 16)
                g = {}
                for k, dflt in (("g_depth_bits", 9), ("g_width_bits", 16)):
                    gv = self.ev(ctx, inst.generics[k]) if k in inst.generics else kn(dflt)
                    if gv.x or gv.v is None:
                        raise Unsupported("%s: the generic %s of the RAM is not a constant" % (inst.where, k))
                    g[k] = gv.v
                cap = (1 << g["g_depth_bits"]) * max(1, g["g_width_bits"] // 8)
            else:                       # the 8 bit port of a block RAM of 2 KB (ADDRA / wr_address are 11 bits)
                cap = 2048
            self.memcap = cap if self.memcap is None else min(self.memcap, cap)
            t = truth(self.ev(ctx, we))
            if t == 1:
                return True
            if t is None:
                hit = False
        return hit

    def split_info(self, ctx, inst):
        """Gideon's io_bus_splitter, read from its source: port i is selected by the address bits g_range_hi downto g_range_lo being i; it gets the request
        with write and read cleared otherwise; the response is the OR of the responses of the ports. {lo, hi, n, req, resp, reqs: {i: id}, resps: {i: id}}"""
        c = ctx.__dict__.setdefault("_split", {})
        if id(inst) in c:
            return c[id(inst)]
        g = {"g_range_lo": 16, "g_range_hi": 19, "g_ports": 3}
        for k in g:
            if k in inst.generics:
                v = self.ev(ctx, inst.generics[k])
                if v.x or v.v is None:
                    raise Unsupported("%s: the generic %s of the io_bus_splitter is not a constant" % (inst.where, k))
                g[k] = v.v
        si = {"lo": g["g_range_lo"], "hi": g["g_range_hi"], "n": g["g_ports"], "req": None, "resp": None, "reqs": {}, "resps": {}}
        for f, a in inst.ports:
            if a is None:
                continue
            fl = f.lower()
            m = re.fullmatch(r"(reqs|resps)\((\d+)\)", fl)
            if fl in ("req", "resp") and a[0] == "id":
                si[fl] = a[1]
            elif m and a[0] == "id":
                si[m.group(1)][int(m.group(2))] = a
            elif fl not in ("clock",):
                raise Unsupported("%s: the io_bus_splitter port %s is connected in a form I do not know" % (inst.where, f))
        if si["req"] is None:
            raise Unsupported("%s: the io_bus_splitter has no request" % inst.where)
        c[id(inst)] = si
        return si

    def split_sel(self, ctx, si, i):
        """1, 0 or unknown: this request selects port i."""
        a = self.val(ctx, si["req"] + ".address")
        return v_eq(v_slice(a, si["hi"], si["lo"]), kn(i, si["hi"] - si["lo"] + 1))

    def split_val(self, ctx, inst, i, fld, w):
        si = self.split_info(ctx, inst)
        v = self.val(ctx, si["req"] + "." + fld)
        if fld in ("read", "write"):
            return fit(v_bit(v, self.split_sel(ctx, si, i), "and"), w)
        return fit(v, w)

    def split_taint(self, ctx, inst, fld):
        si = self.split_info(ctx, inst)
        t = self.taint_name(ctx, si["req"] + "." + fld)
        if fld in ("read", "write"):
            t |= self.taint_name(ctx, si["req"] + ".address")
        return t

    def split_resps(self, ctx, inst):
        """The response signals of the ports this request can select (the others are no origin of the answer)."""
        si = self.split_info(ctx, inst)
        return [a[1] for i, a in sorted(si["resps"].items()) if truth(self.split_sel(ctx, si, i)) != 0]

    def driven(self, ctx, name):
        if ctx.drv is None:
            self.build_drv(ctx)
        w = self.width(ctx, name)
        keys = [name]
        while "." in keys[-1]:
            keys.append(keys[-1].rsplit(".", 1)[0])
        cur = None
        for key in keys:
            for d in ctx.drv.get(key, []):
                if d[0] == "ca":
                    lhs, rhs = d[1], d[2]
                    if lhs[0] == "id":
                        if lhs[1] == name:
                            return fit(self.ev(ctx, rhs), w)
                        if key != name and rhs[0] == "id":
                            return self.val(ctx, rhs[1] + name[len(key):])
                        if key != name:
                            raise Unsupported("a record assigned from an expression: %s" % key)
                    else:
                        cur = self.overlay(ctx, cur if cur is not None else allx(w), lhs, self.ev(ctx, rhs))
                elif d[0] == "proc":
                    p = d[1]
                    if id(p) in ctx.running:
                        return self.state_val(ctx, name, w)
                    r = self.run(ctx, p)
                    ent = r.store.get(name)
                    if ent is None:
                        continue
                    if p.clocked and not ent.v.known():
                        return self.state_val(ctx, name, w)
                    return fit(ent.v, w)
                elif d[0] == "split":
                    if key == name:
                        raise Unsupported("a record of the io_bus_splitter used as a value")
                    return self.split_val(ctx, d[1], d[2], name[len(key) + 1:], w)
                elif d[0] == "splitr":
                    return allx(w)
                elif d[0] == "pass":
                    src = [a for f, a in d[1].ports if f == PASSTHROUGH[d[1].modname.lower()][d[2]]]
                    return fit(self.ev(ctx, src[0]), w) if src and src[0] is not None else allx(w)
                elif d[0] == "inst":
                    ch = [c for c in self.followed if c.parent is ctx and c.inst is d[1]]
                    if ch:
                        return self.val(ch[0], d[2] + name[len(key):])
                    return allx(w)
        if cur is not None:
            return cur
        return self.state_val(ctx, name, w)

    def flag_init(self, ctx, name, w):
        """The initial value of a handshake flag (every assignment a literal, a declared initial value), else None."""
        base = name.partition(".")[0]
        rhss = ctx.mod.assigns.get(name) or []
        d = ctx.mod.decls.get(base)
        if rhss and d is not None and d.init is not None and "." not in name and all(self.literal_only(ctx, r) for r in rhss):
            return fit(self.ev(ctx, d.init), w)
        return None

    def flag_varies(self, ctx, name, v):
        """A flag (every assignment a literal) that some assignment sets to another value than it has now: as data it is a register the response reads."""
        if self.hold.get((id(ctx), name)) is not None or self.flag_init(ctx, name, v.w or 1) is None:
            return False
        for r in ctx.mod.assigns.get(name) or []:
            rv = self.ev(ctx, r)
            if rv.x or rv.v != v.v:
                return True
        return False

    def state_val(self, ctx, name, w):
        """A register as the request sees it. Phase 1 is the request cycle: a handshake flag has its initial value. Phase 2 is the
        response cycle: it has the value the request cycle left. Any other state is unknown."""
        h = self.hold.get((id(ctx), name))
        if h is not None:
            return fit(h, w)
        fi = self.flag_init(ctx, name, w)
        if fi is None or (self.pipe and ctx is self.bus):
            return allx(w)          # a module with a pipeline is in any of its states when the request reaches the decode
        if self.phase == 2 and (id(ctx), name) in self.post:
            return fit(self.post[(id(ctx), name)], w)
        return fi

    def literal_only(self, ctx, e):
        ids = set()
        ids_of(e, ids)
        return all(i in ctx.mod.enums or i in ctx.mod.consts for i in ids)

    def overlay(self, ctx, cur, lhs, v):
        base = lhs[1]
        if lhs[0] == "sl":
            hi, lo = self.const_int(ctx, lhs[2]), self.const_int(ctx, lhs[3])
            lo0 = self.lowbound(ctx, base)
            hi, lo = hi - lo0, lo - lo0
        elif lhs[0] == "ix" or (lhs[0] == "call" and len(lhs[2]) == 1):
            ix = lhs[2] if lhs[0] == "ix" else lhs[2][0]
            iv = self.ev(ctx, ix)
            if iv.x:
                return allx(cur.w)
            hi = lo = iv.v - self.lowbound(ctx, base)
        else:
            raise Unsupported("an assignment to part of a signal")
        n = hi - lo + 1
        m = mask(n) << lo
        nv = fit(v, n)
        return V((cur.v & ~m) | ((nv.v << lo) & m), (cur.x & ~m) | ((nv.x << lo) & m), cur.w)

    # ---- expressions
    def ev(self, ctx, e, sc=None):
        k = e[0]
        if k == "n":
            return e[1]
        if k == "paren":
            return self.ev(ctx, e[1], sc)
        if k == "id":
            n = e[1]
            if sc is not None and n in sc.st and (sc.readall or n in sc.names):
                return sc.st[n].v
            return self.val(ctx, n)
        if k == "attr":
            if e[1][0] != "id" or e[2] not in ("length", "high", "low") or self.is_array(ctx, e[1][1]):
                raise Unsupported("the attribute '%s of this operand" % e[2])
            w = self.width(ctx, e[1][1])
            lb = self.lowbound(ctx, e[1])
            return kn({"length": w, "high": lb + w - 1, "low": lb}[e[2]], 32)
        if k == "un":
            a = self.ev(ctx, e[2], sc)
            op = e[1]
            if op in ("not", "~"):
                return v_not(a)
            if op == "!":
                return v_tv(None if truth(a) is None else 1 - truth(a))
            if op == "-" and a.known():
                return V(-a.v, 0, a.w)
            if op == "+":
                return a
            if op in ("|", "&", "^"):
                t = truth(a)
                if op == "|":
                    return v_tv(t)
                raise Unsupported("the reduction operator " + op)
            raise Unsupported("the unary operator " + op)
        if k == "bin":
            return self.binop(ctx, e, sc)
        if k == "cond":
            t = truth(self.ev(ctx, e[1], sc))
            if t == 1:
                return self.ev(ctx, e[2], sc)
            if t == 0:
                return self.ev(ctx, e[3], sc)
            return v_merge(self.ev(ctx, e[2], sc), self.ev(ctx, e[3], sc))
        if k == "cat":
            return v_cat([fit(self.ev(ctx, p, sc), self.ev(ctx, p, sc).w or 32) for p in e[1]])
        if k == "rep":
            n = self.const_int(ctx, e[1], sc)
            inner = self.ev(ctx, e[2], sc)
            return v_cat([inner] * n)
        if k == "catpart":
            ws = [self.lhs_width(ctx, x, sc) for x in e[2].parts]
            lo = sum(ws[e[3] + 1:])
            return v_slice(fit(self.ev(ctx, e[1], sc), sum(ws)), lo + ws[e[3]] - 1, lo)
        if k == "agg":
            return V(0, mask(64), None)
        if k == "others":
            b = self.ev(ctx, e[1], sc)
            return V((mask(64) if b.v & 1 else 0), (mask(64) if b.x & 1 else 0), None)
        if k == "sl":
            a = self.ev(ctx, e[1], sc)
            try:
                hi, lo = self.const_int(ctx, e[2], sc), self.const_int(ctx, e[3], sc)
            except Unsupported:
                # a slice whose position is state (a byte lane picked by a register): its value is unknown, whatever the register is
                return V(0, mask(64), None)
            lo0 = self.lowbound(ctx, e[1])
            return v_slice(a, hi - lo0, lo - lo0)
        if k == "psl":
            a = self.ev(ctx, e[1], sc)
            s, n = self.ev(ctx, e[2], sc), self.const_int(ctx, e[3], sc)
            if s.x:
                return allx(n)
            lo = s.v if e[4] == "+:" else s.v - n + 1
            return v_slice(a, lo + n - 1, lo)
        if k == "ix":
            return self.index(ctx, e[1], e[2], sc)
        if k == "call":
            return self.call(ctx, e, sc)
        raise Unsupported("an expression of kind %s" % k)

    def lhs_width(self, ctx, lhs, sc=None):
        """The width of one part of a concatenation target."""
        k = lhs[0]
        if k == "id":
            return self.width(ctx, lhs[1])
        if k == "sl":
            return self.const_int(ctx, lhs[2], sc) - self.const_int(ctx, lhs[3], sc) + 1
        if k == "psl":
            return self.const_int(ctx, lhs[3], sc)
        if k in ("ix", "call") and lhs[1][0] == "id":
            return self.width(ctx, lhs[1][1]) if self.is_array(ctx, lhs[1][1]) else 1
        raise Unsupported("a part of a concatenation target of kind %s" % k)

    def index(self, ctx, base, ix, sc):
        if base[0] == "id" and self.is_array(ctx, base[1]):
            return allx(self.width(ctx, base[1]))
        a = self.ev(ctx, base, sc)
        i = self.ev(ctx, ix, sc)
        if i.x:
            return X1
        i = i.v - self.lowbound(ctx, base)
        return V(a.v >> i, a.x >> i, 1)

    def call(self, ctx, e, sc):
        f, args = e[1], e[2]
        if len(args) == 1 and args[0][0] == "attr" and args[0][2] == "range" and args[0][1][0] == "id":
            # x(y'range): the part of x that has the indices of y
            y = args[0][1][1]
            wy, ly = self.width(ctx, y), self.lowbound(ctx, args[0][1])
            return v_slice(self.ev(ctx, f, sc), ly + wy - 1 - self.lowbound(ctx, f), ly - self.lowbound(ctx, f))
        if f[0] != "id":
            if len(args) == 1:
                return self.index(ctx, f, args[0], sc)
            raise Unsupported("a call of an expression")
        n = f[1]
        if n in ctx.mod.decls or (sc is not None and n in sc.st):
            if len(args) != 1:
                raise Unsupported("a multi-dimensional index")
            return self.index(ctx, f, args[0], sc)
        a = [self.ev(ctx, x, sc) for x in args]
        if n in PASS_FUNCS:
            return a[0]
        if n in ("to_integer", "conv_integer"):
            return V(a[0].v, 0, None) if a[0].known() else allx(32)
        if n in ("rising_edge", "falling_edge"):
            return T1
        if n in ("resize", "conv_std_logic_vector", "to_unsigned", "to_signed"):
            w = self.const_int(ctx, args[1])
            return fit(a[0], w) if a[0].known() or n == "resize" else allx(w)
        t = frozenset()
        for x in args:
            t |= self.taint(ctx, x)
        if t:
            raise Unsupported("%s: the function %s on a value from the request" % (ctx.mod.fname, n))
        return allx(32)

    def binop(self, ctx, e, sc):
        op = e[1]
        lang = ctx.mod.lang
        a = self.ev(ctx, e[2], sc)
        b = self.ev(ctx, e[3], sc)
        if op == "&" and lang == "vhdl":
            return v_cat([fit(a, a.w or 32), fit(b, b.w or 32)])
        if op in ("and", "or", "xor") or (lang == "vlog" and op in ("&", "|", "^")):
            return v_bit(a, b, {"&": "and", "|": "or", "^": "xor"}.get(op, op))
        if op in ("nand", "nor", "xnor"):
            return v_not(v_bit(a, b, op[1:] if op != "xnor" else "xor"))
        if op == "&&":
            ta, tb = truth(a), truth(b)
            return F1 if (ta == 0 or tb == 0) else (T1 if (ta == 1 and tb == 1) else X1)
        if op == "||":
            ta, tb = truth(a), truth(b)
            return T1 if (ta == 1 or tb == 1) else (F1 if (ta == 0 and tb == 0) else X1)
        if op in ("=", "==", "==="):
            return v_eq(a, b)
        if op in ("/=", "!=", "!=="):
            r = v_eq(a, b)
            return X1 if r.x else (F1 if r.v else T1)
        if op in ("<", "<=", ">", ">="):
            return v_cmp(a, b, op)
        if op in ("+", "-", "*"):
            return v_arith(a, b, op)
        if op == "**":
            if a.known() and b.known() and b.v < 64:
                return V(a.v ** b.v, 0, None)
            return allx(32)
        if op in (">>", "<<") and b.known():
            return V(a.v >> b.v if op == ">>" else a.v << b.v, a.x >> b.v if op == ">>" else a.x << b.v, a.w)
        if op in ("/", "%", "mod", "rem") and a.known() and b.known() and b.v:
            return V(a.v // b.v if op == "/" else a.v % b.v, 0, wmax(a, b))
        if op in ("/", "%", "mod", "rem", ">>", "<<"):
            return allx(wmax(a, b) or 32)
        raise Unsupported("the operator " + op)

    # ---- taint: which values come from the request's address (a) or data (d) through wires
    def taint(self, ctx, e, vt=None):
        """The taint of an expression. A part of a vector (a slice, an index) has the taint of those bits: a concatenation keeps the taint of each of its
        parts (the request vector of a clock-domain bridge: address, data, read and write are four different things)."""
        if not isinstance(e, tuple):
            return frozenset()
        k = e[0]
        if k == "id":
            if vt is not None and e[1] in vt:
                return vt[e[1]]
            return self.taint_name(ctx, e[1])
        if k == "n":
            return frozenset()
        if not (vt is not None and len(e) > 1 and isinstance(e[1], tuple) and e[1][:1] == ("id",) and e[1][1] in vt):
            r = self.bit_range(ctx, e)
            if r is not None:
                return self.taint_bits(ctx, r[0], r[1], r[2]) | self.taint(ctx, r[3], vt)
        t = frozenset()
        for x in e[1:]:
            if isinstance(x, tuple):
                t |= self.taint(ctx, x, vt)
            elif isinstance(x, list):
                for y in x:
                    t |= self.taint(ctx, y, vt)
        return t

    def bit_range(self, ctx, e):
        """(signal, hi, lo, index expression or None) when e is a constant slice or index of a declared vector signal, else None."""
        k = e[0]
        try:
            if k == "sl" and e[1][0] == "id" and e[1][1] in ctx.mod.decls:
                lo0 = self.lowbound(ctx, e[1])
                return (e[1][1], self.const_int(ctx, e[2]) - lo0, self.const_int(ctx, e[3]) - lo0, ("n", kn(0)))
            ix = None
            if k == "ix":
                ix = e[2]
            elif k == "call" and len(e[2]) == 1 and e[1][0] == "id" and e[1][1] in ctx.mod.decls and e[2][0][0] != "attr":
                ix = e[2][0]
            if ix is not None and e[1][0] == "id" and e[1][1] in ctx.mod.decls and not self.is_array(ctx, e[1][1]):
                v = self.ev(ctx, ix)
                if not v.x and v.v is not None:
                    i = v.v - self.lowbound(ctx, e[1])
                    return (e[1][1], i, i, ("n", kn(0)))
        except Unsupported:
            return None
        return None

    def ewidth(self, ctx, e):
        k = e[0]
        if k == "n":
            if e[1].w is None:
                raise Unsupported("the width of a literal")
            return e[1].w
        if k == "paren":
            return self.ewidth(ctx, e[1])
        if k == "id":
            return self.width(ctx, e[1])
        if k == "call" and e[1][0] == "id" and e[1][1] in PASS_FUNCS and len(e[2]) == 1:
            return self.ewidth(ctx, e[2][0])
        if k == "sl":
            return self.const_int(ctx, e[2]) - self.const_int(ctx, e[3]) + 1
        if k == "cat":
            return sum(self.ewidth(ctx, x) for x in e[1])
        if k == "bin" and e[1] == "&" and ctx.mod.lang == "vhdl":
            return self.ewidth(ctx, e[2]) + self.ewidth(ctx, e[3])
        if self.bit_range(ctx, e) is not None:
            return 1
        raise Unsupported("the width of an expression of kind %s" % k)

    def cat_parts(self, ctx, e):
        if e[0] == "cat":
            return list(e[1])
        if e[0] == "bin" and e[1] == "&" and ctx.mod.lang == "vhdl":
            out = []
            for x in (e[2], e[3]):
                out += self.cat_parts(ctx, x) if (x[0] == "bin" and x[1] == "&") else [x]
            return out
        return None

    def taint_bits_expr(self, ctx, e, hi, lo):
        """The taint of bits hi..lo of an expression."""
        k = e[0]
        try:
            if k == "paren":
                return self.taint_bits_expr(ctx, e[1], hi, lo)
            if k == "id" and e[1] in ctx.mod.decls:
                return self.taint_bits(ctx, e[1], hi, lo)
            if k == "call" and e[1][0] == "id" and e[1][1] in PASS_FUNCS and len(e[2]) == 1:
                return self.taint_bits_expr(ctx, e[2][0], hi, lo)
            parts = self.cat_parts(ctx, e)
            if parts is not None:
                ws = [self.ewidth(ctx, x) for x in parts]
                t = frozenset()
                for i, x in enumerate(parts):
                    plo = sum(ws[i + 1:])
                    phi = plo + ws[i] - 1
                    if phi >= lo and plo <= hi:
                        t |= self.taint_bits_expr(ctx, x, min(hi, phi) - plo, max(lo, plo) - plo)
                return t
            if k == "sl":
                h2, l2 = self.const_int(ctx, e[2]), self.const_int(ctx, e[3])
                lo0 = self.lowbound(ctx, e[1])
                return self.taint_bits_expr(ctx, e[1], l2 - lo0 + hi, l2 - lo0 + lo)
            r = self.bit_range(ctx, e)
            if r is not None and r[1] == r[2] and hi == lo == 0:
                return self.taint_bits(ctx, r[0], r[1], r[1])
        except Unsupported:
            pass
        return self.taint(ctx, e)

    def taint_bits(self, ctx, name, hi, lo):
        """The taint of bits hi..lo of a declared signal: through the instance port it is, the clock-domain bridge's synchronizer, a continuous assignment
        and the output of a followed instance the parts of a concatenation stay apart; any other driver gives the taint of the whole signal."""
        c = ctx.__dict__.setdefault("_tb", {})
        key = (name, hi, lo)
        if key in c:
            return c[key] if c[key] is not None else frozenset()
        c[key] = None
        t = self._taint_bits(ctx, name, hi, lo)
        c[key] = t
        return t

    def _taint_bits(self, ctx, name, hi, lo):
        mod = ctx.mod
        if (ctx is self.bus and name in self.rootv) or "." in name or (ctx.parent is None and mod.nets):
            return self.taint_name(ctx, name)
        port = mod.ports.get(name)
        if port is not None and port[0] == "in":
            if ctx.inst is None:
                return frozenset()
            for f, a in ctx.inst.ports:
                if f == name and a is not None:
                    return self.taint_bits_expr(ctx.parent, a, hi, lo)
            return frozenset()
        if ctx.drv is None:
            self.build_drv(ctx)
        t, found = frozenset(), False
        for d in ctx.drv.get(name, []):
            if d[0] == "ca" and d[1][0] == "id":
                t |= self.taint_bits_expr(ctx, d[2], hi, lo)
            elif d[0] == "pass":
                src = [a for f, a in d[1].ports if f == PASSTHROUGH[d[1].modname.lower()][d[2]]]
                if not (src and src[0] is not None):
                    return self.taint_name(ctx, name)
                t |= self.taint_bits_expr(ctx, src[0], hi, lo)
            elif d[0] == "inst":
                ch = [x for x in self.followed if x.parent is ctx and x.inst is d[1]]
                if ch:
                    t |= self.taint_bits(ch[0], d[2], hi, lo)
            else:
                return self.taint_name(ctx, name)
            found = True
        return t if found else self.taint_name(ctx, name)

    def taint_name(self, ctx, name):
        c = ctx.tcache
        if name in c:
            return c[name] or frozenset()
        c[name] = None
        t = self._taint_name(ctx, name)
        c[name] = t
        return t

    def _taint_name(self, ctx, name):
        mod = ctx.mod
        if (id(ctx), name) in self.holdt:
            return self.holdt[(id(ctx), name)]
        if ctx is self.bus and name in self.rootv:
            return self.roott[name]
        base = name.partition(".")[0]
        if base not in mod.decls and not (ctx.parent is None and mod.nets):
            return frozenset()
        if ctx.parent is None and mod.nets:
            rc = getattr(mod, "regcopies", {}).get(name) if self.bus is self.top else None
            if rc is not None:
                return self.taint(ctx, rc)
            for inst in mod.insts:
                for f, a in inst.ports:
                    if a is not None and a[0] == "id" and a[1] == name and self.d.port_dir(inst.modname, f) == "out":
                        ch = [x for x in self.followed if x.parent is ctx and x.inst is inst]
                        return self.taint_name(ch[0], f) if ch else frozenset()
            return frozenset()
        port = mod.ports.get(base)
        if port is not None and port[0] == "in":
            if ctx.inst is None:
                return frozenset()
            for f, a in ctx.inst.ports:
                if f == base and a is not None:
                    if "." in name and a[0] == "id":
                        return self.taint_name(ctx.parent, a[1] + name[len(base):])
                    return self.taint(ctx.parent, a)
            return frozenset()
        if ctx.drv is None:
            self.build_drv(ctx)
        keys = [name]
        while "." in keys[-1]:
            keys.append(keys[-1].rsplit(".", 1)[0])
        t = frozenset()
        for key in keys:
            for d in ctx.drv.get(key, []):
                if d[0] == "ca":
                    r = d[2]
                    if key != name and r[0] == "id":
                        t |= self.taint_name(ctx, r[1] + name[len(key):])
                    else:
                        t |= self.taint(ctx, r)
                elif d[0] == "proc" and not d[1].clocked:
                    t |= self.proc_taint(ctx, d[1]).get(name, frozenset()) | self.proc_taint(ctx, d[1]).get(key, frozenset())
                elif d[0] == "split" and key != name:
                    t |= self.split_taint(ctx, d[1], name[len(key) + 1:])
                elif d[0] == "splitr":
                    for r in self.split_resps(ctx, d[1]):
                        t |= self.taint_name(ctx, r + name[len(key):])
                elif d[0] == "pass":
                    src = [a for f, a in d[1].ports if f == PASSTHROUGH[d[1].modname.lower()][d[2]]]
                    if src and src[0] is not None:
                        t |= self.taint(ctx, src[0])
                elif d[0] == "inst":
                    ch = [x for x in self.followed if x.parent is ctx and x.inst is d[1]]
                    if ch:
                        t |= self.taint_name(ch[0], d[2] + name[len(key):])
        return t

    def proc_taint(self, ctx, p):
        r = ctx.ptaint.get(id(p))
        if r is not None:
            return r
        ctx.ptaint[id(p)] = {}
        out, vt = {}, {}

        def rhs_taint(e):
            return self.taint(ctx, e, vt)

        def walk(stmts, guard):
            for s in stmts:
                k = s[0]
                if k == "as":
                    key = base_name(s[1])[0]
                    if s[1][0] == "id" and "." not in key and s[2][0] == "id":
                        d = ctx.mod.decls.get(key) or p.vars.get(key)
                        ti = self.tyinfo(ctx, d.ty) if d is not None else None
                        if ti is not None and ti[0] == "rec":
                            for fn, _ in self.d.record_fields(ti[1]):
                                out[key + "." + fn] = out.get(key + "." + fn, frozenset()) | self.taint_name(ctx, s[2][1] + "." + fn) | guard
                            continue
                    t = rhs_taint(s[2]) | guard
                    if key in p.vars:
                        vt[key] = vt.get(key, frozenset()) | t
                    out[key] = out.get(key, frozenset()) | t
                elif k == "blk":
                    walk(s[1], guard)
                elif k == "if":
                    g = guard
                    for c, b in s[1]:
                        g = g | rhs_taint(c)
                        walk(b, g)
                    walk(s[2], g)
                elif k == "case":
                    g = guard | rhs_taint(s[1])
                    for ch, b in s[2]:
                        walk(b, g)
                elif k == "for":
                    walk(s[5], guard)
        walk(p.stmts, frozenset())
        ctx.ptaint[id(p)] = out
        return out

    # ---- running a process on the stimulus
    def run(self, ctx, p):
        r = ctx.runs.get(id(p))
        if r is not None:
            return r
        r = Run()
        ctx.runs[id(p)] = r
        ctx.running.add(id(p))
        try:
            st = {}
            sc = Sc(st, not p.clocked, set(p.vars))
            for n, d in p.vars.items():
                st[n] = Ent(self.ev(ctx, d.init) if d.init is not None else allx(self.width_ty(ctx, d.ty)))
            if p.clocked:
                keys = set()
                walk_assigned(p.stmts, keys)
                for k in self.rec_keys(ctx, keys, p):
                    if k not in p.vars and k.partition(".")[0] in ctx.mod.decls:
                        fi = self.flag_init(ctx, k, self.width(ctx, k))
                        if fi is not None:
                            st[k] = Ent(self.state_val(ctx, k, fi.w), ())
            self.exec_list(ctx, p, p.stmts, st, True, frozenset(), r, sc)
            r.store = st
        finally:
            ctx.running.discard(id(p))
        return r

    def rec_keys(self, ctx, keys, p):
        """The assigned names with every whole record replaced by its fields (a record is assigned field by field, do_assign)."""
        out = set()
        for k in keys:
            d = ctx.mod.decls.get(k) if "." not in k else None
            if d is not None and k not in p.vars:
                ti = self.tyinfo(ctx, d.ty)
                if ti[0] == "rec":
                    out.update(k + "." + fn for fn, _ in self.d.record_fields(ti[1]))
                    continue
            out.add(k)
        return out

    def width_ty(self, ctx, ty):
        ti = self.tyinfo(ctx, ty)
        return ti[1] if ti[0] == "v" else 32

    def exec_list(self, ctx, p, stmts, st, certain, pt, r, sc):
        for s in stmts:
            self.exec_stmt(ctx, p, s, st, certain, pt, r, sc)

    def merge_store(self, a, b):
        out = {}
        for k in set(a) | set(b):
            ea, eb = a.get(k), b.get(k)
            if ea is not None and eb is not None:
                out[k] = Ent(v_merge(ea.v, eb.v), ea.alts + tuple(x for x in eb.alts if x not in ea.alts))
            else:
                e = ea or eb
                out[k] = Ent(allx(e.v.w or 1), e.alts)
        return out

    def exec_stmt(self, ctx, p, s, st, certain, pt, r, sc):
        k = s[0]
        if k == "nop":
            return
        if k == "blk":
            return self.exec_list(ctx, p, s[1], st, certain, pt, r, sc)
        if k == "as":
            return self.do_assign(ctx, p, s, st, certain, pt, r, sc)
        if k == "if":
            return self.exec_if(ctx, p, s[1], s[2], st, certain, pt, r, sc)
        if k == "case":
            sel = self.ev(ctx, s[1], sc)
            self.no_pipeline(ctx, s[1], sel)
            pt2 = pt | self.taint(ctx, s[1], r.vt)
            return self.exec_case(ctx, p, sel, s[2], st, certain, pt2, r, sc)
        if k == "for":
            a, b = self.const_int(ctx, s[2], sc), self.const_int(ctx, s[3], sc)
            rng = range(a, b + 1) if s[4] == "to" else range(a, b - 1, -1)
            for i in rng:
                st[s[1]] = Ent(kn(i, None))
                sc.st[s[1]] = st[s[1]]   # the arms of an if or case run on copies of the store; sc.st is the one ev reads
                sc.names.add(s[1])
                self.exec_list(ctx, p, s[5], st, certain, pt, r, sc)
            return
        raise Unsupported("a statement of kind %s" % k)

    def acopies(self, ctx):
        """The registers of a module behind a handshake that hold a copy of the held request (an address or data port, or a register
        that copies one): the closure over the clocked assignments."""
        if getattr(ctx, "acop", None) is not None:
            return ctx.acop
        ctx.acop = set()
        pend = []
        for p in ctx.mod.procs:
            if p.clocked:
                def walk(stmts):
                    for s in stmts:
                        if s[0] == "as":
                            pend.append((base_name(s[1])[0], s[2]))
                        elif s[0] == "blk":
                            walk(s[1])
                        elif s[0] == "if":
                            for _, b in s[1]:
                                walk(b)
                            walk(s[2])
                        elif s[0] == "case":
                            for _, b in s[2]:
                                walk(b)
                        elif s[0] == "for":
                            walk(s[5])
                walk(p.stmts)
        changed = True
        while changed:
            changed = False
            for k, rhs in pend:
                if k in ctx.acop:
                    continue
                ids = set()
                ids_of(rhs, ids)
                if (ids & ctx.acop) or any("a" in self.taint_name(ctx, n) for n in ids):      # a copy of the address; the data port's copies (r_wdata -> on) are no pipeline of the address
                    ctx.acop.add(k)
                    changed = True
        return ctx.acop

    def no_pipeline(self, ctx, e, v):
        """Behind a handshake the request is run with the state unknown. A decode on a register copy of the held address would then
        see an unknown address and take every arm: the check would pass a byte the RTL does not decode. That is refused."""
        if self.crossed and ctx.parent is self.top and v.x:
            ids = set()
            ids_of(e, ids)
            bad = sorted(ids & self.acopies(ctx))
            if self.pipe:
                bad = [n for n in bad if (id(ctx), n) not in self.hold] if self.pipe_ready else []
            elif bad:
                raise Pipeline("%s: a decode on %s, a register copy of the held request" % (ctx.mod.fname, ", ".join(bad)))
            if bad:
                raise Unsupported("%s: a decode on %s, a register copy of the held request (a pipeline behind a handshake: not modelled)"
                                  % (ctx.mod.fname, ", ".join(bad)))

    def exec_if(self, ctx, p, arms, els, st, certain, pt, r, sc):
        if not arms:
            return self.exec_list(ctx, p, els, st, certain, pt, r, sc)
        c, body = arms[0]
        cv = self.ev(ctx, c, sc)
        self.no_pipeline(ctx, c, cv)
        t = truth(cv)
        pt2 = pt | self.taint(ctx, c, r.vt)
        if t == 1:
            return self.exec_list(ctx, p, body, st, certain, pt2, r, sc)
        if t == 0:
            return self.exec_if(ctx, p, arms[1:], els, st, certain, pt2, r, sc)
        s1 = dict(st)
        self.exec_list(ctx, p, body, s1, False, pt2, r, sc)
        s2 = dict(st)
        self.exec_if(ctx, p, arms[1:], els, s2, False, pt2, r, sc)
        st.clear()
        st.update(self.merge_store(s1, s2))

    def exec_case(self, ctx, p, sel, arms, st, certain, pt, r, sc):
        if not arms:
            return
        ch, body = arms[0]
        if ch is None:
            m = 1
        else:
            ms = [self.choice_match(ctx, sel, c, sc) for c in ch]
            m = 1 if 1 in ms else (0 if all(x == 0 for x in ms) else None)
        if m == 1:
            return self.exec_list(ctx, p, body, st, certain, pt, r, sc)
        if m == 0:
            return self.exec_case(ctx, p, sel, arms[1:], st, certain, pt, r, sc)
        s1 = dict(st)
        self.exec_list(ctx, p, body, s1, False, pt, r, sc)
        s2 = dict(st)
        self.exec_case(ctx, p, sel, arms[1:], s2, False, pt, r, sc)
        st.clear()
        st.update(self.merge_store(s1, s2))

    def choice_match(self, ctx, sel, c, sc):
        if c[0] == "rng":
            lo, hi = self.ev(ctx, c[1], sc), self.ev(ctx, c[2], sc)
            if sel.x or lo.x or hi.x:
                return None
            return 1 if lo.v <= sel.v <= hi.v else 0
        return truth(v_eq(sel, self.ev(ctx, c, sc)))

    def do_assign(self, ctx, p, s, st, certain, pt, r, sc):
        lhs, rhs = s[1], s[2]
        key = base_name(lhs)[0]
        t = pt | self.taint(ctx, rhs, r.vt)
        snap = {n: st[n] for n in p.vars if n in st}
        if key in p.vars:
            r.vt[key] = r.vt.get(key, frozenset()) | t
        if lhs[0] == "id":
            base = key.partition(".")[0]
            d = ctx.mod.decls.get(base) or p.vars.get(base)
            ti = self.tyinfo(ctx, d.ty) if d is not None else None
            if ti is not None and ti[0] == "rec" and "." not in key:
                if rhs[0] != "id":
                    raise Unsupported("%s: a record assigned from an expression" % s[-1])
                for fn, _ in self.d.record_fields(ti[1]):
                    sub = ("id", rhs[1] + "." + fn)
                    self.put(ctx, p, st, key + "." + fn, sub, self.ev(ctx, sub, sc), certain, t, r, s[-1], snap, pt)
                return
            w = self.width(ctx, key) if key not in p.vars else self.width_ty(ctx, p.vars[key].ty)
            self.put(ctx, p, st, key, rhs, fit(self.ev(ctx, rhs, sc), w), certain, t, r, s[-1], snap, pt)
            return
        base = key
        rv = self.ev(ctx, rhs, sc)
        r.effects.append(Eff(base, rhs, rv, certain, t, ctx, s[-1], pt))
        if self.is_array(ctx, base):
            return
        cur = st.get(base)
        w = self.width(ctx, base) if base not in p.vars else self.width_ty(ctx, p.vars[base].ty)
        curv = cur.v if cur is not None else allx(w)
        try:
            nv = self.overlay(ctx, curv, lhs, rv)
        except Unsupported:
            nv = allx(w)
        if not certain:
            nv = v_merge(curv, nv)
        st[base] = Ent(nv, (cur.alts if cur else ()) + ((rhs, ctx, rv, snap),))

    def put(self, ctx, p, st, key, rhs, nv, certain, t, r, where, snap, pt=frozenset()):
        r.effects.append(Eff(key, rhs, nv, certain, t, ctx, where, pt))
        alt = (rhs, ctx, nv, snap)
        old = st.get(key)
        if certain or old is None:
            st[key] = Ent(nv if certain else allx(nv.w or 1), (alt,))
        else:
            st[key] = Ent(v_merge(old.v, nv), old.alts + (alt,))

    # ---- origins of response data
    def origins(self, ctx, name, seen=None):
        seen = seen if seen is not None else set()
        k = (id(ctx), name)
        if k in seen:
            return set()
        seen.add(k)
        v = self.val(ctx, name)
        if v.known() and not self.flag_varies(ctx, name, v):
            return {("c", v.v)}
        mod = ctx.mod
        base = name.partition(".")[0]
        port = mod.ports.get(base)
        if port is not None and port[0] == "in":
            if ctx.inst is None:
                return {("ext", name)}
            for f, a in ctx.inst.ports:
                if f == base and a is not None:
                    if "." in name and a[0] == "id":
                        return self.origins(ctx.parent, a[1] + name[len(base):], seen)
                    return self.origins_expr(ctx.parent, a, None, seen)
            return {("ext", name)}
        if ctx.parent is None and mod.nets:
            for inst in mod.insts:
                for f, a in inst.ports:
                    if a is not None and a[0] == "id" and a[1] == name and self.d.port_dir(inst.modname, f) == "out":
                        ch = [x for x in self.followed if x.parent is ctx and x.inst is inst]
                        return self.origins(ch[0], f, seen) if ch else {("ext", name)}
            return {("ext", name)}
        if ctx.drv is None:
            self.build_drv(ctx)
        keys = [name]
        while "." in keys[-1]:
            keys.append(keys[-1].rsplit(".", 1)[0])
        out, found = set(), False
        for key in keys:
            for d in ctx.drv.get(key, []):
                found = True
                if d[0] == "ca":
                    if key != name and d[2][0] == "id":
                        out |= self.origins(ctx, d[2][1] + name[len(key):], seen)
                    else:
                        out |= self.origins_expr(ctx, d[2], None, seen)
                elif d[0] == "proc":
                    ent = self.run(ctx, d[1]).store.get(name)
                    for rhs, actx, nv, snap in (ent.alts if ent else ()):
                        if nv.known():
                            out.add(("c", nv.v))
                        else:
                            out |= self.origins_expr(actx, rhs, snap, seen)
                elif d[0] == "split":
                    out |= {("c", None)}
                elif d[0] == "splitr":
                    for r in self.split_resps(ctx, d[1]):
                        out |= self.origins(ctx, r + name[len(key):], seen)
                elif d[0] == "pass":
                    src = [a for f, a in d[1].ports if f == PASSTHROUGH[d[1].modname.lower()][d[2]]]
                    out |= self.origins_expr(ctx, src[0], None, seen) if src and src[0] is not None else {("ext", name)}
                elif d[0] == "inst":
                    ch = [x for x in self.followed if x.parent is ctx and x.inst is d[1]]
                    out |= self.origins(ch[0], d[2] + name[len(key):], seen) if ch else {("ext", name)}
        if any("d" in self.taint(ctx, r) for r in mod.assigns.get(name, [])):
            out.add(("reg", name))
        return out if out else {("reg", name)}

    def origins_expr(self, ctx, e, snap, seen):
        k = e[0]
        if k == "n":
            return {("c", e[1].v)} if e[1].known() else {("c", None)}
        if k == "paren":
            return self.origins_expr(ctx, e[1], snap, seen)
        if k == "id":
            if snap and e[1] in snap:
                v = snap[e[1]].v
                return {("c", v.v)} if v.known() else {("ext", e[1])}
            return self.origins(ctx, e[1], seen)
        if k == "cond":
            sc = Sc(dict(snap or {}), True, set())
            t = truth(self.ev(ctx, e[1], sc))
            if t == 1:
                return self.origins_expr(ctx, e[2], snap, seen)
            if t == 0:
                return self.origins_expr(ctx, e[3], snap, seen)
            return self.origins_expr(ctx, e[2], snap, seen) | self.origins_expr(ctx, e[3], snap, seen)
        if k == "call" and e[1][0] == "id" and e[1][1] in PASS_FUNCS | {"to_integer"}:
            return self.origins_expr(ctx, e[2][0], snap, seen)
        if k in ("sl", "psl", "ix") or (k == "call" and (e[1][0] != "id" or e[1][1] in ctx.mod.decls or (snap and e[1][1] in snap))):
            return self.origins_expr(ctx, e[1], snap, seen)
        if k == "rep":
            return self.origins_expr(ctx, e[2], snap, seen)
        if k == "call":
            out = set()
            for y in e[2]:
                out |= self.origins_expr(ctx, y, snap, seen)
            return out or {("c", None)}
        out = set()
        for x in e[1:]:
            if isinstance(x, tuple) and x and isinstance(x[0], str):
                out |= self.origins_expr(ctx, x, snap, seen)
            elif isinstance(x, list):
                for y in x:
                    out |= self.origins_expr(ctx, y, snap, seen)
        if k in ("sl", "ix", "psl", "call") and not out:
            out.add(("c", None))
        return out or {("c", 0)}

    # ---- following
    def activate(self):
        """Follow every module a strobe from the request reaches: top-level instances and the instances of followed modules."""
        changed = True
        while changed:
            changed = False
            for ctx in list(self.followed) + [self.top]:
                for inst in ctx.mod.insts:
                    if inst.label in ctx.kids and ctx.kids[inst.label] in self.followed:
                        continue
                    if ctx is self.top and inst is self.bus.inst:
                        continue
                    if self.native(inst):
                        continue
                    act = self.inst_active(ctx, inst)
                    if act:
                        c = ctx.child(inst)
                        self.check_assumed(c)
                        c.certain = act == 2
                        self.followed.append(c)
                        changed = True

    def check_assumed(self, c):
        """A generic that an if-generate of the module was decided with must have that value in this instance, read or not."""
        for name, want in c.mod.gen_assumed.items():
            if c.inst is not None and name in c.inst.generics:
                got = self.ev(c.parent, c.inst.generics[name])
            elif c.mod.generics.get(name) is not None:
                got = self.ev(c, c.mod.generics[name])
            else:
                continue
            if got.x or got.v != want:
                raise Unsupported("%s: the generic %s is %s in the instance %s, but an if-generate of the module was decided with its default %s"
                                  % (c.mod.fname, name, got, c.name, want))

    def inst_active(self, ctx, inst):
        """0: no strobe from the request reaches the instance; 1: one may; 2: one does."""
        res = 0
        for f, a in inst.ports:
            if a is None or self.d.port_dir(inst.modname, f) != "in":
                continue
            names = []
            if a[0] == "id" and "." not in a[1] and a[1] in ctx.mod.decls:
                ti = self.tyinfo(ctx, ctx.mod.decls[a[1]].ty)
                if ti[0] == "rec":
                    names = [a[1] + "." + fn for fn, _ in self.d.record_fields(ti[1])]
                else:
                    names = [a[1]]
            elif a[0] == "id":
                names = [a[1]]
            for n in names:
                try:
                    if ctx.parent is None and ctx.mod.nets:
                        w = ctx.mod.nets.get(n, 1)
                    else:
                        w = self.width(ctx, n)
                except Unsupported:
                    continue
                if w != 1:
                    continue
                if "a" in self.taint_name(ctx, n):
                    t = truth(self.val(ctx, n))
                    if self.crossed:
                        continue
                    if t == 1:
                        return 2
                    if t is None:
                        res = 1
        return res

    def find_held(self):
        """A register that the request itself sets (assigned for sure in the request cycle, its value or its guard from the request, the guard no decode of the address:
        a strobe) holds the request's value in the
        next clock, where the module decodes it (flash_gate.vhd p_valid, p_read, p_addr, p_data). Its own assignment is a transport, no write."""
        held = {}
        for ctx in self.followed:
            for p in ctx.mod.procs:
                if not p.clocked or ctx is self.bus:      # the bus module's own registers are the request's entry, its decode is the base model
                    continue
                last = {}
                for e in self.run(ctx, p).effects:
                    if e.certain:               # an assignment under a guard of the state (a reset, taking the request away) is another path
                        last[e.key] = e
                for key, e in last.items():
                    pt = ctx.mod.ports.get(key)
                    if (e.taint & {"a", "d"}) and "q" not in e.guard and key.partition(".")[0] in ctx.mod.decls and (pt is None or pt[0] == "in") and key not in p.vars:
                        held[(id(ctx), key)] = (e.v, e.taint)
        return held

    @staticmethod
    def pure_copy(e):
        """An expression that moves bits and changes none: a name, a slice or an index of one, a concatenation of those."""
        k = e[0]
        if k == "id":
            return True
        if k == "paren":
            return Ev.pure_copy(e[1])
        if k in ("sl", "ix", "psl"):
            return e[1][0] == "id"
        if k == "cat":
            return all(Ev.pure_copy(x) or x[0] == "n" for x in e[1])
        return False

    def build_pipe(self):
        """The registers of the module behind the handshake that take the held request on (c_addr <= rq_addr; op_a <= c_addr[15:2]; a copy of a copy): each takes the request's
        value and taint in the clock where the module decodes it. Only the assignments that move request bits count (a pure copy of a value with the request's taint); another
        master's assignment of the same register (JTAG) is not the request and is left out. Iterated until the chain of copies is complete."""
        ctx = self.bus
        held = {}
        for _ in range(8):
            self.hold = {k: v[0] for k, v in held.items()}
            self.holdt = {k: v[1] for k, v in held.items()}
            for c in self.followed:
                c.cache, c.runs, c.tcache, c.ptaint, c.ocache = {}, {}, {}, {}, {}
                c.__dict__.pop("_tb", None)
            new = {}
            for p in ctx.mod.procs:
                if not p.clocked:
                    continue
                for e in self.run(ctx, p).effects:
                    if e.key not in ctx.mod.decls or e.key in p.vars or ctx.mod.ports.get(e.key, ("in",))[0] != "in" and e.key in ctx.mod.ports:
                        continue
                    if not self.pure_copy(e.rhs):
                        continue
                    t = self.taint(ctx, e.rhs, None) & {"a", "d", "q", "w", "r"}
                    if not t & {"a", "d"} or e.guard & {"a", "d"}:       # a guard of the request is a decode: the register is written, not passed on
                        continue
                    k = (id(ctx), e.key)
                    old = new.get(k)
                    new[k] = (e.v, t) if old is None else (v_merge(old[0], e.v), old[1] | t)
            if {k: (v[0].v, v[0].x, v[0].w, v[1]) for k, v in new.items()} == {k: (v[0].v, v[0].x, v[0].w, v[1]) for k, v in held.items()}:
                break
            held = new
        self.hold = {k: v[0] for k, v in held.items()}
        self.holdt = {k: v[1] for k, v in held.items()}
        for c in self.followed:
            c.cache, c.runs, c.tcache, c.ptaint, c.ocache = {}, {}, {}, {}, {}
            c.__dict__.pop("_tb", None)
        self.pipe_ready = True
        return held

    def decode_cycle(self, held):
        """The next clock: the held registers have the request's value and taint, the state is as the request cycle left it (flags) or unknown."""
        self.hold = {k: v[0] for k, v in held.items()}
        self.holdt = {k: v[1] for k, v in held.items()}
        self.phase = 2
        for ctx in self.followed:
            ctx.cache, ctx.runs, ctx.tcache, ctx.ptaint, ctx.ocache = {}, {}, {}, {}, {}

    def analyze(self, write):
        self.activate()
        res = {"followed": [c.name for c in self.followed], "write": False, "read": False, "forward": []}
        held = self.build_pipe() if self.pipe else self.find_held()
        for ctx in self.followed:
            for p in ctx.mod.procs:
                effs = self.run(ctx, p).effects

                def is_out(e):
                    pt = ctx.mod.ports.get(e.key)
                    return pt is not None and pt[0] != "in"
                fw = [e for e in effs if is_out(e) and e.certain and "a" in self.taint(ctx, e.rhs) and "d" not in self.taint(ctx, e.rhs)]
                res["forward"] += ["%s.%s" % (ctx.name, e.key) for e in fw]
                for e in effs:
                    if write and self.is_write(ctx, e) and not (fw and is_out(e)) and (id(ctx), e.key) not in held:
                        res["write"] = True
                        res["cert"] = res.get("cert") or e.certain
                res["cert"] = res.get("cert") or bool(fw)
        if write:
            for ctx in self.followed:
                mw = self.memory_write(ctx)
                if mw is not None:
                    res["write"] = True
                    res["cert"] = res.get("cert") or (mw and ctx.certain)
        res["memcap"] = self.memcap
        res["cert"] = res.get("cert") or any(c.certain for c in self.followed[1:])
        res["core"] = write and self.core_handover(res)
        if not write:
            for ctx in self.followed:
                for p in ctx.mod.procs:
                    if not p.clocked:
                        continue
                    keys = set()
                    walk_assigned(p.stmts, keys)
                    st = ctx.runs[id(p)].store
                    for k in keys:
                        if k in st and k.partition(".")[0] in ctx.mod.decls and self.flag_init(ctx, k, st[k].v.w) is not None:
                            self.post[(id(ctx), k)] = st[k].v
        if held and self.pipe:
            res["links"] = self.find_links(write)
        elif held:
            res["links"] = self.find_links(write)
            self.decode_cycle(held)
            if write:
                for ctx in self.followed:
                    for p in ctx.mod.procs:
                        for e in self.run(ctx, p).effects:
                            if self.is_write(ctx, e) and (id(ctx), e.key) not in held:
                                res["write"] = True
                                res["cert"] = res.get("cert") or e.certain
        if not write:
            self.phase = 2
            if not held:
                for ctx in self.followed:
                    ctx.cache, ctx.runs = {}, {}
            og = set()
            for rc, rn in self.resp:
                og |= self.origins(rc, rn)
            res["read"] = any(t != ("c", 0) for t in og)
            res["origins"] = og
        res["certain"] = res["write"] or res["read"]
        if not held:
            res["links"] = self.find_links(write)
        res["decodes"] = not res["forward"]                      # no register hands the request on: the module decodes it itself and a continuous assignment hands more on (find_links)
        res["forward"] += [n for n in getattr(self, "cas_fwd", []) if n not in res["forward"]]
        return res

    def is_response(self, ctx, key):
        """An assignment to the answer of the bus (the record t_io_resp, an ack or nc): the module says it has taken the access; that is no write."""
        base = key.partition(".")[0]
        d = ctx.mod.decls.get(base)
        return (d is not None and d.ty.name.lower() == "t_io_resp") or key.rpartition(".")[2].lower() in ("ack", "nc")

    def is_write(self, ctx, e):
        """An executed assignment is a write of the byte when a register or a RAM takes the request's data, when the data decides it, or when its guard
        names the address and the write flag (a strobe register that ignores the data byte) and the read of the same address does not run it. The answer of
        the bus (an ack under the write flag) is none of these."""
        if self.is_response(ctx, e.key):
            return False
        return "d" in e.taint or ({"q", "w"} <= e.taint and (ctx.name, e.key, e.where) not in self.readref)

    def core_handover(self, res):
        """The write leaves the base for layer 2: a strobe port of the core groups (CORE_PORTS) that is 1 for this address, or an effect that
        drives such a port under a guard of the request's address, and a data port of the same group that carries the request's data."""
        ctx = self.bus
        outs = [n for n, pt in ctx.mod.ports.items() if pt[0] != "in" and n.startswith(CORE_PORTS)]
        strobe = set()
        for n in outs:
            try:
                if self.width(ctx, n) == 1 and truth(self.val(ctx, n)) == 1 and "a" in self.taint_name(ctx, n):
                    strobe.add(n)
            except Unsupported:
                continue
        for c in self.followed:
            if c is not ctx:
                continue
            for p in c.mod.procs:
                for e in self.run(c, p).effects:
                    if e.key in outs and ("q" in e.taint or "r" in e.taint):
                        strobe.add(e.key)
        if not strobe:
            return False
        group = {n for n in outs if any(n.startswith(g) for g in {x for x in CORE_PORTS if any(s.startswith(x) for s in strobe)})}
        return any("d" in self.taint_name(ctx, n) for n in group if n not in strobe) or any("d" in self.taint(ctx, e.rhs) for p in ctx.mod.procs for e in self.run(ctx, p).effects if e.key in group)

    def find_links(self, write=True):
        """The request leaves a followed module over output ports (address, write flag, data, toggle: the held request of a
        handshake) and a module of the top-level file reads them: that module decodes it in another clock domain. For every such
        module: the values and taints its input ports have, and its output ports that answer the forwarder."""
        outs = {}
        for ctx in self.followed:
            if ctx.parent is not self.top:
                continue
            for p in ctx.mod.procs:
                for e in self.run(ctx, p).effects:
                    pt = ctx.mod.ports.get(e.key)
                    if pt is None or pt[0] == "in" or not e.taint:
                        continue
                    rt = self.taint(ctx, e.rhs)
                    if "d" in rt:
                        new = ("d", allx(self.width(ctx, e.key)))
                    elif "a" in rt and e.certain:
                        new = ("a", e.v)
                    else:
                        new = ("s", allx(self.width(ctx, e.key)))
                    old = outs.get((id(ctx), e.key))
                    if old is not None:
                        if old[0] == "d" or new[0] == "d":
                            new = ("d", new[1])
                        elif old[0] == "a" and new[0] == "a":
                            new = ("a", v_merge(old[1], new[1]))
                        else:
                            new = ("s" if "s" in (old[0], new[0]) else "a", allx(new[1].w or 1))
                    outs[(id(ctx), e.key)] = new
        self.cas_fwd = []
        plain, strobe = set(), set()       # the ports a continuous assignment drives; of those, the ones that carry the write or read flag (a strobe)
        for ctx in self.followed:
            if ctx.parent is not self.top or not self.pipe or ctx.mod.lang != "vlog":      # only the module behind a handshake with a register pipeline (its strobes are decoded from the pipeline's registers)
                continue
            for lhs, rhs, where in ctx.mod.cas:               # a continuous assignment to an output port hands the request on as well (trx_sys.v: p2_we = ost == O_RUN && op_we && op_a[15:12] == 4'd2, p2_addr, p2_wdata)
                key = base_name(lhs)[0]
                pt = ctx.mod.ports.get(key)
                if pt is None or pt[0] == "in" or lhs[0] != "id":
                    continue
                rt = self.taint(ctx, rhs)
                if not rt & {"a", "d"}:
                    continue
                if "d" in rt:
                    new = ("d", allx(self.width(ctx, key)))
                else:
                    new = ("a", self.ev(ctx, rhs))
                    names = set()
                    ids_of(rhs, names)
                    flag = any((id(ctx), n) in self.hold and self.hold[(id(ctx), n)].w == 1 and not self.hold[(id(ctx), n)].x for n in names)
                    # a strobe: the held write flag (a one-bit register that holds the request's value, known) decoded with the address; the port carries both for the module behind it
                    # (the narrow taint of the handshake itself stays: the pipeline's own state writes are no register writes)
                    self.link_t[(id(ctx), key)] = frozenset({"a"}) | ({"q", "w" if write else "r"} if flag else frozenset())
                if (id(ctx), key) in outs:                                   # another driver (a register of a process): the values merge as they do between processes
                    continue
                outs[(id(ctx), key)] = new
                plain.add((id(ctx), key))
                self.cas_fwd.append("%s.%s" % (ctx.name, key))
                if self.link_t.get((id(ctx), key), frozenset()) & {"w", "r"}:
                    strobe.add((id(ctx), key))
        links = []
        for ctx in self.followed:
            if ctx.parent is not self.top:
                continue
            mine = {k: v for (i, k), v in outs.items() if i == id(ctx)}
            if not any(v[0] != "s" for v in mine.values()):
                continue
            nets = {}
            for f, a in ctx.inst.ports:
                if f in mine and a is not None and a[0] == "id":
                    nets[a[1]] = f
            back = {}
            for f, a in ctx.inst.ports:
                if a is not None and a[0] == "id" and self.d.port_dir(ctx.inst.modname, f) == "in":
                    back[a[1]] = f
            for inst in self.top.mod.insts:
                if inst is ctx.inst or inst is self.bus.inst or inst.modname.lower() not in self.d.index:
                    continue
                rv, rt, resp = {}, {}, []
                hit = False
                for f, a in inst.ports:
                    if a is None or a[0] != "id":
                        continue
                    dr = self.d.port_dir(inst.modname, f)
                    if dr == "in" and a[1] in nets:
                        kind, v = mine[nets[a[1]]]
                        rv[f], rt[f] = v, (frozenset("d") if kind == "d" else frozenset("a") | self.link_t.get((id(ctx), nets[a[1]]), frozenset()))
                        k = (id(ctx), nets[a[1]])
                        hit = hit or k not in plain or k in strobe        # a module that reads only an address or data a continuous assignment passes on (no strobe) does not decode the request
                    elif dr == "out" and a[1] in back:
                        resp.append(f)
                if rv and hit:
                    links.append((inst, rv, rt, resp, ctx.name))
        return links


# ---------------------------------------------------------------------------------------------------------------------------
# the design's helpers: port directions (read from the source text, no parse) and constants of packages

def _port_dir(self, modname, formal):
    key = (modname.lower(), formal.lower())
    c = self.__dict__.setdefault("_pd", {})
    if key in c:
        return c[key]
    f = self.index.get(modname.lower()) or self.index.get(modname)
    res = None
    if f is not None and not self.blocked(f):
        text = read_source(f)
        if f.endswith((".vhd", ".vhdl")):
            text = re.sub(r"--[^\n]*", "", text)
            m = re.search(r"\bentity\s+%s\s+is\b(.*?)\bend\b" % re.escape(modname), text, re.S | re.I)
            body = m.group(1) if m else text
            mm = re.search(r"(?:[;(]|^)\s*(?:\w+\s*,\s*)*%s\s*(?:,\s*\w+\s*)*:\s*(in|out|inout|buffer)\b" % re.escape(formal), body, re.I | re.M)
            if mm:
                res = "in" if mm.group(1).lower() == "in" else "out"
        else:
            text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
            m = re.search(r"\bmodule\s+%s\b(.*?);" % re.escape(modname), text, re.S)
            body = m.group(1) if m else ""
            mm = re.search(r"\b(input|output|inout)\s+(?:wire\s+|reg\s+|signed\s+)*(?:\[[^\]]*\]\s*)?%s\b" % re.escape(formal), body)
            if mm is None:
                mm = re.search(r"\b(input|output|inout)\b[^;]*?\b%s\b" % re.escape(formal), text[m.start():] if m else text)
            if mm:
                res = "in" if mm.group(1) == "input" else "out"
    c[key] = res
    return res


def _pkg_const(self, name):
    """A constant of a VHDL package of the list with a record aggregate of literals: {field: V} as a callable table."""
    c = self.__dict__.setdefault("_pc", {})
    if name in c:
        return c[name]
    res = None
    if "." not in name:
        # a scalar constant of a package of the list with a literal (a bit string, a character, a decimal number)
        for f in sorted(self.files):
            if not f.endswith((".vhd", ".vhdl")) or self.blocked(f):
                continue
            body = re.sub(r"--[^\n]*", "", read_source(f))
            m = re.search(r"\bconstant\s+%s\s*:\s*[^:;]+:=\s*([^;]+?)\s*;" % re.escape(name), body, re.I)
            if m:
                lit = m.group(1).strip()
                if re.fullmatch(r"[xXbB]\"[0-9a-fA-F_]*\"", lit):
                    res = num_vhdl("bs", lit)
                elif re.fullmatch(r"\"[01]*\"", lit):
                    res = num_vhdl("str", lit)
                elif re.fullmatch(r"'.'", lit):
                    res = num_vhdl("chr", lit)
                elif re.fullmatch(r"\d[\d_]*", lit):
                    res = num_vhdl("num", lit)
                break
        c[name] = res
        return res
    base, field = name.split(".", 1)
    for f in sorted(self.files):
        if not f.endswith((".vhd", ".vhdl")) or self.blocked(f):
            continue
        body = re.sub(r"--[^\n]*", "", read_source(f))
        m = re.search(r"\bconstant\s+%s\s*:\s*\w+\s*:=\s*\(" % re.escape(base), body, re.S | re.I)
        if m:
            depth, j = 1, m.end()
            while depth and j < len(body):
                depth += (body[j] == "(") - (body[j] == ")")
                j += 1
            inner = body[m.end():j - 1]
            for fm in re.finditer(r"(\w+)\s*=>\s*(\(\s*others\s*=>\s*'[01]'\s*\)|[^,]+?)\s*(?:,|$)", inner.strip()):
                if fm.group(1).lower() == field:
                    lit = fm.group(2).strip()
                    om = re.fullmatch(r"\(\s*others\s*=>\s*'([01])'\s*\)", lit)
                    if om:
                        res = V(mask(64) if om.group(1) == "1" else 0, 0, None)
                        break
                    mm = re.fullmatch(r"([xXbB]\"[0-9a-fA-F_]*\"|'.'|\"[01]*\")", lit)
                    if mm:
                        k = "bs" if lit[0] in "xXbB" else ("chr" if lit[0] == "'" else "str")
                        res = num_vhdl(k, lit)
            break
    c[name] = res
    return res


def _pkg_expr(self, name):
    """The expression of a scalar constant of a VHDL package of the list (to_unsigned(896, 11), a sum of other constants), parsed; the evaluator reads it in the
    place that names the constant."""
    c = self.__dict__.setdefault("_pe", {})
    if name in c:
        return c[name]
    res = None
    for f in sorted(self.files):
        if not f.endswith((".vhd", ".vhdl")) or self.blocked(f):
            continue
        body = re.sub(r"--[^\n]*", "", read_source(f))
        m = re.search(r"\bconstant\s+%s\s*:\s*[^:;]+:=\s*([^;]+?)\s*;" % re.escape(name), body, re.I)
        if m:
            try:
                res = VhdlParser(lex(m.group(1), "vhdl", f), f).expr()
            except Unsupported:
                res = None
            break
    c[name] = res
    return res


Design.port_dir = _port_dir
Design.pkg_const = _pkg_const
Design.pkg_expr = _pkg_expr


# ---------------------------------------------------------------------------------------------------------------------------
# the check

def parse_toml_a(path, maps):
    import descrec
    text = open(path).read()
    md = descrec.load_maps(maps) if maps else {}
    try:
        descrec.ldfrag(text, md, path)
        doc = descrec.resolve_maps(descrec.toml_loads(text), md)
    except descrec.DescError as e:
        raise CheckError("%s: %s" % (path, e))
    return doc


class Checker:
    def __init__(self, design, top, bus, doc, window=None, full=False, own=False):
        self.d, self.doc, self.full, self.own = design, doc, full, own
        self.ports = self.window = None
        if ":" in bus:
            # a partial: MODULE:write=P,read=P,address=P,data=P,response=P (the ports of one instance in the top file); its window is the host's decode
            self.busmod, spec = bus.split(":", 1)
            self.busname = None
            self.ports = dict(kv.split("=", 1) for kv in spec.split(","))
            if set(self.ports) != {"write", "read", "address", "data", "response"}:
                raise CheckError("--bus MODULE:write=,read=,address=,data=,response= names the five ports")
            if window is None:
                raise CheckError("a bus of ports needs --window BASE:SIZE (the part of the I/O space the host hands to these ports)")
            self.window = window
        else:
            self.busmod, self.busname = bus.split(".", 1)
        self.topmod = top_instances(design, top, [self.busmod])
        io = doc["io"]
        self.iobase = io["iobase"]
        self.cache = {}

    def stim(self, rtl_addr, write, lowx=0):
        key = (rtl_addr, write, lowx)
        if key in self.cache:
            return self.cache[key]
        def mk(w):
            if self.ports is not None:
                base = 0 if self.full else self.window[0]       # full: the address port carries the whole address (the drive's window), else the offset in the window
                return Ev(self.d, self.topmod, self.busmod, None, V((rtl_addr - base) & ~mask(lowx), mask(lowx), 24), w, ports=self.ports)
            return Ev(self.d, self.topmod, self.busmod, self.busname, V(rtl_addr & ~mask(lowx), mask(lowx), 24), w)
        ev = mk(write)
        if write:
            evr = mk(False)
            evr.activate()
            ev.readref = {(c.name, e.key, e.where) for c in evr.followed for p in c.mod.procs for e in evr.run(c, p).effects}
        res = self.cross(ev.analyze(write), write, 0)
        self.cache[key] = res
        return res

    def cross(self, res, write, depth):
        """A request that the RTL forwards over a handshake (held address, data, toggle) is decoded by the module that reads those
        ports, in its own clock domain: that module is run on the held request (stage 2) and decides write and read of the byte.
        A module that is not read by a top-level file instance leaves the request forwarded, and the check fails on it."""
        res["forwarded"] = False
        if not res["forward"] or not res["links"]:
            return res
        if depth > 3:
            raise Unsupported("a request that crosses more than 3 clock domains")
        wr, rd = (res["write"], res["read"]) if res.get("decodes") else (False, False)       # a module that decodes the request itself and hands it on keeps its own decode
        cert = res["cert"]
        for inst, rv, rt, resp, via in res["links"]:
            ev2 = Ev(self.d, self.topmod, None, None, None, write, link=(inst, rv, rt, resp))
            try:
                a2 = ev2.analyze(write)
            except Pipeline:
                ev2 = Ev(self.d, self.topmod, None, None, None, write, link=(inst, rv, rt, resp), pipe=True)
                a2 = ev2.analyze(write)
            r2 = self.cross(a2, write, depth + 1)
            wr = wr or r2["write"]
            rd = rd or r2["read"]
            res["followed"] += ["%s (from %s)" % (n, via) for n in r2["followed"]]
            res["forward"] = [f for f in res["forward"] if not f.startswith(via + ".")] + r2["forward"] if not r2["forwarded"] else res["forward"]
        res["forwarded"] = True
        res["write"], res["read"], res["cert"] = wr, rd, cert
        res["certain"] = wr or rd
        return res

    def covered(self, a, lowx, core=False):
        """True if every byte of the aligned span is decoded for sure: by a write, a forwarded request or a module a strobe certainly reaches.
        For a block of the core: the request leaves the base for the core, as a write whose data reaches an output port or as a forwarded
        request (what the core does with it is the core's, checked in the core's own design)."""
        if core:
            return bool(self.stim(a, True, lowx)["core"])
        w, r = self.stim(a, True, lowx), self.stim(a, False, lowx)
        if w["forwarded"] or r["forwarded"]:
            return bool(w["write"] or r["read"])
        return bool(w["cert"] or r["cert"] or (lowx == 0 and (w["write"] or r["read"])))

    def undecoded(self, lo, size, core=False):
        """The first byte of [lo, lo+size) that nothing decodes, or None. A span is trusted when its stimulus (low bits unknown) is certain."""
        def walk(a, n, lx):
            if self.covered(a, lx, core):
                return None
            if n == 1:
                return a
            h = n // 2
            return walk(a, h, lx - 1) or walk(a + h, n - h, lx - 1)
        a, end = lo, lo + size
        while a < end:
            n = 1
            while a % (n * 2) == 0 and a + n * 2 <= end:
                n *= 2
            bad = walk(a, n, n.bit_length() - 1)
            if bad is not None:
                return bad
            a += n
        return None

    def span_ok(self, a, lx, wr):
        """True if every byte of the aligned span has the access (write or read) for sure: a span of unknown low bits is trusted as covered() trusts it
        (a forwarded request that the receiving module decodes, or, for a write, an effect that certainly runs); a byte is decided by its own stimulus."""
        res = self.stim(a, wr, lx)
        if lx == 0 or res["forwarded"]:
            return bool(res["write" if wr else "read"])
        return bool(wr and res["write"] and res["cert"])

    def run_gap(self, lo, size, wr):
        """The first byte of [lo, lo+size) without the access, or None: a run of registers is checked over its aligned spans, split where a span is not certain,
        down to single bytes."""
        def walk(a, n, lx):
            if self.span_ok(a, lx, wr):
                return None
            if n == 1:
                return a
            h = n // 2
            r = walk(a, h, lx - 1)
            return r if r is not None else walk(a + h, n - h, lx - 1)
        a, end = lo, lo + size
        while a < end:
            n = 1
            while a % (n * 2) == 0 and a + n * 2 <= end:
                n *= 2
            bad = walk(a, n, n.bit_length() - 1)
            if bad is not None:
                return bad
            a += n
        return None

    def check(self, only=None):
        errs = []
        for b in self.doc["block"]:
            if only is not None and b["name"] not in only:
                continue
            try:
                errs += self.check_block(b)
            except Unsupported as ex:
                # an RTL pattern the check does not know stops this block only: every block of TOML A gets its verdict
                if os.environ.get("RTL_CHECK_TRACE"):
                    raise
                errs.append("block %s: an RTL pattern I do not know: %s" % (b["name"], ex))
        return errs

    def check_block(self, b):
        errs = []
        name = b["name"]
        rt = b["address"] - self.iobase
        if self.window is not None and not (self.window[0] <= rt and rt + b["size"] <= self.window[0] + self.window[1]):
            errs.append("block %s: 0x%08X..0x%08X lies outside the window the host hands to the core (0x%08X..0x%08X)" % (
                name, b["address"], b["address"] + b["size"] - 1, self.window[0] + self.iobase, self.window[0] + self.window[1] - 1 + self.iobase))
            return errs
        if not 0 <= rt < (1 << 24) or rt + b["size"] > (1 << 24):
            errs.append("block %s: 0x%08X..0x%08X lies outside the request's 24-bit address space (iobase 0x%08X)"
                        % (name, b["address"], b["address"] + b["size"] - 1, self.iobase))
            return errs
        core = b["owner"] == "core" and self.ports is None and not self.own
        bad = self.undecoded(rt, b["size"], core)
        if bad is not None:
            errs.append("block %s: byte +0x%X (0x%08X) is %s" % (name, bad - rt, bad + self.iobase,
                        "not handed over to the core by the RTL" if core else "decoded by nothing in the RTL"))
            return errs
        for p in ([] if core else b.get("property", [])):
            nb = p["width"] // 8
            count = p.get("count", 1)
            stride = p.get("stride", nb)
            if p["offset"] + (count - 1) * stride + nb > b["size"]:
                errs.append("block %s, property %s: it reaches past the block" % (name, p["name"]))
                continue
            cap = self.stim(rt + p["offset"], True)["memcap"] if "w" in p["dir"] else None
            span = (count - 1) * stride + nb
            if cap is not None and span > cap:
                errs.append("block %s, property %s: it spans %d bytes, the RAM that takes its writes holds %d (the addresses above alias)" % (name, p["name"], span, cap))
                continue
            if count > 1:
                for need, wr in (("w", True), ("r", False)):
                    if need not in p["dir"]:
                        continue
                    bad, el = None, 0
                    if stride == nb:
                        bad = self.run_gap(rt + p["offset"], nb * count, wr)
                        el = (bad - rt - p["offset"]) // nb if bad is not None else 0
                    else:
                        for el in range(count):                         # each element at offset + i * stride: the bytes between the elements belong to other runs
                            bad = self.run_gap(rt + p["offset"] + el * stride, nb, wr)
                            if bad is not None:
                                break
                    if bad is not None:
                        res = self.stim(bad, wr)
                        fw = res["forward"]
                        errs.append("block %s, property %s[%d] (dir %s): byte +0x%X (0x%08X) has no %s in the RTL%s" % (
                            name, p["name"], el, p["dir"], bad - rt, bad + self.iobase, "write" if wr else "read",
                            ("; the request is forwarded to " + ", ".join(sorted(set(fw))) + ", whose decode is not followed") if fw else ""))
                        break
                continue
            bad_p = False
            for i in range(nb):
                if bad_p:
                    break
                a = rt + p["offset"] + i
                for need, wr in (("w", True), ("r", False)):
                    if need not in p["dir"]:
                        continue
                    res = self.stim(a, wr)
                    if not res["write" if wr else "read"]:
                        fw = res["forward"]
                        errs.append("block %s, property %s (dir %s): byte +0x%X (0x%08X) has no %s in the RTL%s" % (
                            name, p["name"], p["dir"], p["offset"] + i, a + self.iobase, "write" if wr else "read",
                            ("; the request is forwarded to " + ", ".join(sorted(set(fw))) + ", whose decode is not followed") if fw else ""))
                        bad_p = True
                        break
        return errs


def cmd_check(args):
    opts = {"--design": None, "--toml-a": None, "--top": "top"}
    maps, lists, buses, windows = [], [], [], []
    own = False
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--maps":
            maps.append(args[i + 1])
            i += 2
        elif a == "--list":
            lists.append(args[i + 1])
            i += 2
        elif a == "--bus":
            buses.append(args[i + 1])
            i += 2
        elif a == "--window":
            windows.append(args[i + 1])
            i += 2
        elif a == "--define":
            DEFINES.add(args[i + 1])
            i += 2
        elif a == "--own-core":
            own = True
            i += 1
        elif a in opts:
            opts[a] = args[i + 1]
            i += 2
        else:
            raise CheckError("unknown option %s" % a)
    if not opts["--design"] or not opts["--toml-a"]:
        raise CheckError("usage: rtl_check.py check --design DIR --toml-a FILE [--maps DIR]...")
    buses = buses or ["ultimate_sys.io_req"]
    if len(windows) not in (0, len(buses)) and len(windows) != len([b for b in buses if ":" in b]):
        raise CheckError("every --bus of ports needs its --window")
    doc = parse_toml_a(opts["--toml-a"], maps)
    des = Design(opts["--design"], lists or "build.tcl")
    cks = []
    wi = 0
    for bus in buses:
        window, full = None, False
        if ":" in bus:
            wb, _, rest = windows[wi].partition(":")
            ws, _, fl = rest.partition(":")
            if fl not in ("", "full"):
                raise CheckError("--window BASE:SIZE[:full]")
            window, full = (int(wb, 0) - doc["io"]["iobase"], int(ws, 0)), fl == "full"
            wi += 1
        cks.append(Checker(des, opts["--top"], bus, doc, window, full, own))
    if des.denied:
        print("rtl_check: clean room: %d file(s) of the source list are on the deny list and never opened: %s"
              % (len(des.denied), ", ".join(os.path.basename(f) for f in des.denied)))
    errs, nprop = [], 0
    if len(cks) == 1:
        errs = cks[0].check()
        nprop = sum(len(b.get("property", [])) for b in doc["block"] if b["owner"] != "core" or cks[0].ports is not None or own)
    else:
        # several buses (the ports of a partial that has more than one window): a block goes to the bus whose window holds it
        mine = [[] for _ in cks]
        for b in doc["block"]:
            rt = b["address"] - doc["io"]["iobase"]
            hit = [k for k, c in enumerate(cks) if c.window is not None and c.window[0] <= rt and rt + b["size"] <= c.window[0] + c.window[1]]
            if not hit:
                errs.append("block %s: 0x%08X..0x%08X lies in no window of the buses" % (b["name"], b["address"], b["address"] + b["size"] - 1))
                continue
            mine[hit[0]].append(b["name"])
            nprop += len(b.get("property", []))
        for c, only in zip(cks, mine):
            errs += c.check(only)
    for e in errs:
        print("rtl_check: " + e)
    if errs:
        print("rtl_check: %d finding(s)" % len(errs))
        return 1
    print("rtl_check: %d block(s), %d propert%s agree with the RTL of %s" % (len(doc["block"]), nprop, "y" if nprop == 1 else "ies", opts["--design"]))
    return 0


def strip_c(text):
    """Comments, strings and character literals out, line structure kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == '"' or (c == "'" and not (i > 0 and (text[i - 1].isalnum() or text[i - 1] == "_"))):
            q = c
            j = i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(q + q)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


C_INT = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F']+|\d[\d']*)(?:[uUlL]*)(?![\w.])")


def allowed(path, lit, allow):
    """--allow FILE:LITERAL: this one literal in the file whose path ends with FILE (whole path components). Nothing else of that file."""
    norm = os.path.normpath(path).replace(os.sep, "/")
    for f, l in allow:
        if (norm == f or norm.endswith("/" + f)) and lit.lower() == l.lower():
            return True
    return False


def lint_file(path, ranges, allow=()):
    out = []
    text = strip_c(open(path, errors="replace").read())
    for ln, line in enumerate(text.split("\n"), 1):
        for m in C_INT.finditer(line):
            s = m.group(1).replace("'", "")
            try:
                v = int(s, 16) if s[:2].lower() == "0x" else int(s, 10)
            except ValueError:
                continue
            for lo, hi, what in ranges:
                if lo <= v < hi:
                    if allowed(path, m.group(0), allow):
                        break
                    out.append("%s:%d: %s lies in %s" % (path, ln, m.group(0), what))
                    break
    return out


def cmd_lint(args):
    paths, maps, toml_a, allow = [], [], None, []
    iobase, span = 0x10000000, 0x1000000
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--maps":
            maps.append(args[i + 1])
            i += 2
        elif a == "--toml-a":
            toml_a = args[i + 1]
            i += 2
        elif a == "--allow":
            f, sep, lit = args[i + 1].rpartition(":")
            if not sep or not f or not lit:
                raise CheckError("--allow takes FILE:LITERAL, such as ringfile.cc:0x106aa070u")
            allow.append((f, lit))
            i += 2
        elif a == "--iobase":
            iobase = int(args[i + 1], 0)
            i += 2
        elif a == "--span":
            span = int(args[i + 1], 0)
            i += 2
        else:
            paths.append(a)
            i += 1
    if not paths:
        raise CheckError("usage: rtl_check.py lint-src PATH... [--toml-a FILE [--maps DIR]...]")
    ranges = [(iobase, iobase + span, "the I/O space 0x%08X..0x%08X" % (iobase, iobase + span - 1))]
    if toml_a:
        doc = parse_toml_a(toml_a, maps)
        for b in doc["block"]:
            ranges.append((b["address"], b["address"] + b["size"], "the block %s of TOML A" % b["name"]))
        ranges.append((doc["io"]["iobase"], doc["io"]["iobase"] + span, "the I/O space of TOML A"))
    files = []
    for pth in paths:
        if os.path.isdir(pth):
            for root, _, fs in os.walk(pth):
                files += [os.path.join(root, f) for f in sorted(fs) if f.endswith(C_EXT)]
        else:
            files.append(pth)
    found = []
    for f in sorted(files):
        found += lint_file(f, ranges, allow)
    for s in found:
        print("rtl_check: " + s)
    if found:
        print("rtl_check: %d literal address(es) in C/C++ sources; take them from TOML A (the linker symbols __blk_*, __reg_*)" % len(found))
        return 1
    print("rtl_check: no literal I/O address in %d file(s)" % len(files))
    return 0


def main(argv):
    if len(argv) < 2 or argv[1] not in ("check", "lint-src"):
        print(__doc__)
        return 2
    try:
        return (cmd_check if argv[1] == "check" else cmd_lint)(argv[2:])
    except (Unsupported, CheckError) as e:
        if os.environ.get("RTL_CHECK_TRACE"):
            raise
        print("rtl_check: %s%s" % ("an RTL pattern I do not know: " if isinstance(e, Unsupported) else "", e))
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
