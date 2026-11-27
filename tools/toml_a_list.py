#!/usr/bin/env python3
"""T264 P1: the list compare of a core's TOML A against the address decode of its RTL.

  toml_a_list.py check CORE [--toml-a FILE]   one core: the TOML A of cores/CORE/app/gateware.toml
  toml_a_list.py check --all                  every core of `core_toml.py lanes`
  toml_a_list.py selftest                     the fixtures of tests/toml_a_list

No interpreter: the text of the decode modules the TOML A's blocks name is read for the constants the I/O address is compared with
(`case (off)` / `case off is` choices, `off == 12'h..` / `off = x".."` compares; the page from a `page == N` guard or a wire that holds
one) and the byte addresses of both sides are compared. Red: a byte the TOML A names and the RTL does not decode, a byte the RTL
decodes and the TOML A does not name. Not covered, and said so in the output: the drive window (blocks at 0x1002xxxx: Gideon's drive
decoder behind drv_io_*), a window block of a module whose source has no decode constants (named as UNCHECKED, with the module).
Direction (r / w) and values are not compared: that is what tools/rtl_check.py (optional, frozen) did."""
import os
import re
import sys
import time
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
WIN_BASE, WIN_SIZE = 0x101A0000, 0x20000     # the core window (cw_*): bit 16 of the offset is `win`, which the decodes exclude
DRIVE_LO, DRIVE_HI = 0x10020000, 0x10024000
SEL_RE = re.compile(r"^(\w*off\w*|a_off|offs?)(?:\s*\[\s*(\d+)\s*:\s*(\d+)\s*\])?$", re.I)


CONST_V = re.compile(r"\blocalparam\s*(?:\[[^\]]*\])?\s*(R_\w+)\s*=\s*(%s)\s*;" % r"\d*'[hHdD][0-9a-fA-F_]+")
CONST_VHDL = re.compile(r"\bconstant\s+(R_\w+)\s*:\s*integer\s*:=\s*(16#[0-9a-fA-F_]+#)\s*;", re.I)


def resolve_names(body, vhdl):
    """T264 P2: the generated constants (`R_<block>_<prop>`, `R_x + 3`, `(R_x + 4) >> 2`) read as the literals they hold, so the decode that uses them is compared like one that spells the numbers."""
    consts = {m.group(1): num(m.group(2)) for m in (CONST_VHDL if vhdl else CONST_V).finditer(body)}
    if not consts:
        return body
    lit = (lambda v: "16#%03X#" % v) if vhdl else (lambda v: "12'h%03X" % v)
    names = "|".join(sorted(map(re.escape, consts), key=len, reverse=True))
    add = r"(?:\s*\+\s*(?:\d+'d)?(\d+))?"
    val = lambda m, g=1: consts[m.group(g)] + (int(m.group(g + 1)) if m.group(g + 1) else 0)
    shifted = re.compile(r"(?:\(\s*(%s)%s\s*\)|\b(%s)%s)\s*>>\s*(\d+)" % (names, add, names, add))
    plain = re.compile(r"\b(%s)\b%s" % (names, add))
    def conv(t):
        t = shifted.sub(lambda m: lit((val(m) if m.group(1) else val(m, 3)) >> int(m.group(5))), t)
        return plain.sub(lambda m: lit(val(m)), t)
    parts, last = [], 0
    for m in (CONST_VHDL if vhdl else CONST_V).finditer(body):      # the declarations themselves stay as they are
        parts.append(conv(body[last:m.start()]))
        parts.append(m.group())
        last = m.end()
    parts.append(conv(body[last:]))
    return "".join(parts)


def strip_comments(t, vhdl):
    t = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group().count("\n"), t, flags=re.S)
    return re.sub(r"--[^\n]*" if vhdl else r"//[^\n]*", "", t)


def num(s):
    """A Verilog (12'h010, 4'd6, 'h4) or VHDL (x"010", "0110") literal; None when it is not a plain constant."""
    s = s.strip()
    m = re.fullmatch(r"(?:\d+)?'([hHdDbBoO])([0-9a-fA-F_xXzZ]+)", s)
    if m:
        b = {"h": 16, "d": 10, "b": 2, "o": 8}[m.group(1).lower()]
        try:
            return int(m.group(2).replace("_", ""), b)
        except ValueError:
            return None
    m = re.fullmatch(r"16#([0-9a-fA-F_]+)#", s)          # VHDL based literal 16#010#
    if m:
        return int(m.group(1).replace("_", ""), 16)
    m = re.fullmatch(r'[xX]"([0-9a-fA-F_]+)"', s)
    if m:
        return int(m.group(1).replace("_", ""), 16)
    m = re.fullmatch(r'"([01_]+)"', s)
    if m:
        return int(m.group(1).replace("_", ""), 2)
    m = re.fullmatch(r"\d+", s)
    return int(s) if m else None


LIT = r"""(?:\d*'[hHdDbBoO][0-9a-fA-F_]+|16#[0-9a-fA-F_]+#|[xX]"[0-9a-fA-F_]+"|"[01_]+")"""
PAGE_RE = re.compile(r"(?:\bpage\s*(?:==|=)\s*(%s)|addr\w*\s*\[\s*15\s*:\s*12\s*\]\s*(?:==|=)\s*(%s))" % (LIT, LIT))


def page_of(text):
    m = PAGE_RE.search(text)
    if not m:
        return None
    return num(m.group(1) or m.group(2))


def aliases(body):
    """Names that hold a page guard: `wire sel = ... page == 4'hE`, `assign px_we = ... page == 4'h6`, `signal r0 ... := ... page = x"0"`."""
    al = {}
    for m in re.finditer(r"\b(?:wire|assign|signal)\b[^=;<]*?\b(\w+)\s*(?:=|<=|:=)\s*([^;]*);", body):
        p = page_of(m.group(2))
        if p is not None:
            al[m.group(1)] = p
    for m in re.finditer(r"^\s*(\w+)\s*<=\s*([^;]*?\bpage\b[^;]*);", body, re.M):     # VHDL concurrent: r0 <= ... page = x"0"
        p = page_of(m.group(2))
        if p is not None:
            al.setdefault(m.group(1), p)
    return al


def guard_page(ctx, al, default):
    """The page of the nearest guard in the text before a case or compare: a page compare or an alias, whichever is later."""
    best, at = None, -1
    for m in PAGE_RE.finditer(ctx):
        if m.start() > at:
            best, at = num(m.group(1) or m.group(2)), m.start()
    for n, p in al.items():
        for m in re.finditer(r"\b%s\b" % re.escape(n), ctx):
            if m.start() > at:
                best, at = p, m.start()
    return default if best is None else best


def sel_span(sel):
    """(shift, lowbits) of a selector expression: off -> (0, 0); off[11:2] -> the choice counts 4-byte units."""
    m = SEL_RE.match(sel.strip())
    if not m or (m.group(2) and int(m.group(2)) != 11):
        return None
    return int(m.group(3)) if m.group(3) else 0


def case_blocks_v(body):
    """(selector, choices[(value, offset-in-body)], start) of every Verilog case whose selector is an offset."""
    out = []
    for m in re.finditer(r"\bcase[xz]?\s*\(([^)]*)\)", body):
        sh = sel_span(m.group(1))
        if sh is None:
            continue
        depth, i, end = 1, m.end(), None
        for k in re.finditer(r"\bcase[xz]?\s*\(|\bendcase\b", body[m.end():]):
            depth += 1 if not k.group().startswith("endcase") else -1
            if depth == 0:
                end = m.end() + k.start()
                break
        seg = body[m.end():end]
        # choices: `lit:` or `lit, lit:` at the start of a statement, not inside a nested case
        ch, toks = [], []
        nest = 0
        for ln in re.finditer(r"[^\n]*\n?", seg):
            s = ln.group()
            nest += len(re.findall(r"\bcase[xz]?\s*\(", s)) - len(re.findall(r"\bendcase\b", s))
            if nest > 0 and not re.search(r"\bcase[xz]?\s*\(", s):
                continue
            for cm in re.finditer(r"(?:^|;|\bbegin\b|\bend\b)\s*((?:%s\s*,\s*)*%s)\s*:(?!:)" % (LIT, LIT), s):
                for lm in re.finditer(LIT, cm.group(1)):
                    v = num(lm.group())
                    if v is not None:
                        ch.append((v, m.start() + 0))
                        a = m.end() + ln.start() + cm.start(1) + lm.start()
                        toks.append((v, a, a + len(lm.group())))
        out.append((m.group(1), sh, ch, m.start(), toks))
    return out


def case_blocks_vhdl(body):
    out = []
    for m in re.finditer(r"\bcase\s+([^\n]*?)\s+is\b", body, re.I):
        sh = sel_span(re.sub(r"\(\s*(\d+)\s+downto\s+(\d+)\s*\)", r"[\1:\2]", m.group(1)))
        if sh is None:
            continue
        depth, end = 1, None
        for k in re.finditer(r"\bcase\s+[^\n]*?\s+is\b|\bend\s+case\b", body[m.end():], re.I):
            depth += 1 if not k.group().lower().startswith("end") else -1
            if depth == 0:
                end = m.end() + k.start()
                break
        seg = body[m.end():end]
        ch, nest, toks = [], 0, []
        for ln in re.finditer(r"[^\n]*\n?", seg):
            s = ln.group()
            nest += len(re.findall(r"\bcase\s+[^\n]*?\s+is\b", s, re.I)) - len(re.findall(r"\bend\s+case\b", s, re.I))
            if nest > 0 and not re.search(r"\bcase\s+[^\n]*?\s+is\b", s, re.I):
                continue
            alt = r"%s(?:\s+to\s+%s)?" % (LIT, LIT)       # a choice is `lit` or `lit to lit` (a range)
            for wm in re.finditer(r"\bwhen\s+((?:%s\s*\|\s*)*%s)\s*=>" % (alt, alt), s, re.I):
                for a in re.split(r"\s*\|\s*", wm.group(1)):
                    lits = [num(x) for x in re.findall(LIT, a)]
                    if None not in lits and lits:
                        ch.extend((v, 0) for v in range(lits[0], lits[-1] + 1))
                for lm in re.finditer(LIT, wm.group(1)):
                    v = num(lm.group())
                    if v is not None:
                        a0 = m.end() + ln.start() + wm.start(1) + lm.start()
                        toks.append((v, a0, a0 + len(lm.group())))
        out.append((m.group(1), sh, ch, m.start(), toks))
    return out


COARSE = {}      # byte -> (unit, shift) of a decode that selects a 2^shift-byte unit (a word read with the byte chosen later)


def decoded(path):
    """{absolute offset in the window: [where]} for one source file: the bytes its offset decode names."""
    vhdl = path.endswith((".vhd", ".vhdl"))
    raw = open(path, errors="replace").read()
    body = resolve_names(strip_comments(raw, vhdl), vhdl)
    if vhdl:
        for m in re.finditer(r"\b(\w+)\s*:=\s*to_integer\s*\(\s*unsigned\s*\(\s*\w*off\w*\s*\)\s*\)", body, re.I):
            if not SEL_RE.match(m.group(1)):
                body = re.sub(r"\b%s\b" % re.escape(m.group(1)), "off", body)
        body = re.sub(r"to_integer\s*\(\s*unsigned\s*\(\s*(\w*off\w*)\s*\)\s*\)", r"\1", body, flags=re.I)        # `o := to_integer(unsigned(off))`: the variable o is the offset, read it as off
    al = aliases(body)
    lines_before = lambda pos: body.count("\n", 0, pos) + 1
    pages = [p for p in (page_of(m.group()) for m in PAGE_RE.finditer(body)) if p is not None]
    out = {}
    COARSE.clear()
    cases = case_blocks_vhdl(body) if vhdl else case_blocks_v(body)
    default = 0
    # the module's own page: a wire with a page that is not the pixel page (6) and is used as a guard of a case
    for n, p in al.items():
        if p != 6 and p != 0 and re.search(r"\b%s\b" % re.escape(n), body):
            default = p
    for sel, sh, ch, at, _toks in cases:
        pg = guard_page(body[max(0, at - 400):at], al, default)
        for v, _ in ch:
            for b in range(1 << sh):
                a = (pg << 12) | ((v << sh) | b)
                out.setdefault(a, []).append("%s:%d" % (os.path.relpath(path, ROOT), lines_before(at)))
                if sh:
                    COARSE[a] = (a >> sh, sh)
    cmp_re = r"\b(\w*off\w*)\s*(?:\[\s*(\d+)\s*:\s*(\d+)\s*\]|\(\s*(\d+)\s+downto\s+(\d+)\s*\))?\s*%s\s*(%s)" % ("=" if vhdl else "==", LIT)
    def add(pg, lo, hi, pos, skipbit=None):
        for a in range(lo, hi):
            if skipbit is not None and (a >> skipbit) & 1:
                continue
            out.setdefault((pg << 12) | a, []).append("%s:%d" % (os.path.relpath(path, ROOT), lines_before(pos)))
    for m in re.finditer(cmp_re, body):
        v = num(m.group(6))
        hi = m.group(2) or m.group(4)
        lo = m.group(3) or m.group(5)
        if v is None or (hi and int(hi) != 11):
            continue
        ls = body.rfind("\n", 0, m.start()) + 1
        pg = guard_page(body[ls:m.start()], al, None)
        if pg is None:
            pg = default
        lo = int(lo) if lo else 0
        add(pg, v << lo, (v + 1) << lo, m.start())
    rg = r"\b\w*off\w*\s*>=\s*(%s)\s*(?:&&|and)\s*\w*off\w*\s*(<|<=)\s*(%s)(?:\s*(?:&&|and)\s*(?:!|not\s+)\s*\w*off\w*\s*[\[(]\s*(\d+)\s*[\])])?" % (LIT, LIT)
    for m in re.finditer(rg, body):
        a, b = num(m.group(1)), num(m.group(3))
        ls = body.rfind("\n", 0, m.start()) + 1
        pg = guard_page(body[ls:m.start()], al, None)
        nb = m.group(4)
        add(default if pg is None else pg, a, b + (1 if m.group(2) == "<=" else 0), m.start(), None if nb is None else int(nb))
    return out, dict(COARSE)


def find_module(core, name, cdir=None):
    """The source of a module: its own folder first, then the common folder, then any core's (a copy)."""
    pat = re.compile(r"^\s*(?:module\s+%s\b|entity\s+%s\s+is\b)" % (re.escape(name), re.escape(name)), re.M | re.I)
    roots = [cdir or os.path.join(ROOT, "cores", core), os.path.join(ROOT, "cores", "common"), os.path.join(ROOT, "cores")]
    seen = set()
    for r in roots:
        for d, _, fs in os.walk(r):
            if "/app" in d.replace(os.path.join(ROOT, "cores", core), "", 1) and r != roots[0]:
                pass
            for f in sorted(fs):
                if f.endswith((".v", ".vhd", ".vhdl", ".sv")):
                    p = os.path.join(d, f)
                    if p in seen:
                        continue
                    seen.add(p)
                    try:
                        if pat.search(open(p, errors="replace").read()):
                            return p
                    except OSError:
                        pass
    return None


def toml_bytes(doc):
    """({window offset: 'block.property'}, [(block, module)]) of the TOML A: every byte of every property of a block in the core window."""
    by, mods = {}, []
    for b in doc.get("block", []):
        a = b["address"]
        if DRIVE_LO <= a < DRIVE_HI:
            continue
        if not WIN_BASE <= a < WIN_BASE + WIN_SIZE:
            continue
        mods.append((b["name"], b["module"]))
        for p in b.get("property", []):
            nb = max(1, p["width"] // 8)
            cnt = p.get("count", 1)
            st = p.get("stride", nb)
            for i in range(cnt):
                for k in range(nb):
                    by[a - WIN_BASE + p["offset"] + i * st + k] = "%s.%s" % (b["name"], p["name"] + ("[%d]" % i if cnt > 1 else ""))
    return by, mods


def ranges(addrs):
    a = sorted(addrs)
    out = []
    for x in a:
        if out and x == out[-1][1] + 1:
            out[-1][1] = x
        else:
            out.append([x, x])
    return out


def compare(doc, files, shared):
    """-> (reds, nt, nr): files = the decode sources; shared = the subset that is the shared base (cores/c64u, cores/common): TOML A -> RTL only."""
    tb, mods = toml_bytes(doc)
    rtl, coarse, rtl_own = {}, {}, set()
    for f in files:
        d, c = decoded(f)
        coarse.update(c)
        for a, w in d.items():
            rtl.setdefault(a, []).extend(w)
            if f not in shared:
                rtl_own.add(a)
    tb = {a: n for a, n in tb.items() if (a >> 12) != 6}          # page 6: the host's pixel layer (pix_layer.v, forwarded), not decoded in the core's own module
    reds = []
    for lo, hi in ranges(set(tb) - set(rtl)):
        reds.append("TOML A names %s (0x%X-0x%X), the RTL does not decode it" % (tb[lo], WIN_BASE + lo, WIN_BASE + hi))
    units = {coarse[a] for a in tb if a in coarse}
    extra = {a for a in rtl_own if a not in tb and (a >> 12) != 6 and not (a in coarse and coarse[a] in units)}
    for lo, hi in ranges(extra):
        reds.append("RTL decodes 0x%X-0x%X (%s), the TOML A names no register there" % (WIN_BASE + lo, WIN_BASE + hi, rtl[lo][0]))
    return reds, len(tb), len(rtl)


def load_doc(core, toml_a=None):
    import rtl_check
    path = toml_a or os.path.join(ROOT, "cores", core, "app", "gateware.toml")
    return rtl_check.parse_toml_a(path, [os.path.join(ROOT, "app", "sdkpkg", "maps")])


def check_core(core, toml_a=None, srcdir=None):
    t0 = time.time()
    doc = load_doc(core, toml_a)
    _, mods = toml_bytes(doc)
    files, notes, skipped = [], [], []
    for name in sorted({m for _, m in mods}):
        p = find_module(core, name, srcdir)
        if p is None:
            notes.append("UNCHECKED module %s: no source found" % name)
            continue
        if name == "pix_layer":
            continue
        if not decoded(p)[0]:
            notes.append("UNCHECKED module %s (%s): no offset decode constants found" % (name, os.path.relpath(p, ROOT)))
            continue
        files.append(p)
    shared = [f for f in files if re.search(r"/cores/(c64u|common)/", f)]
    if shared:
        notes.append("one-way (TOML A -> RTL) for the shared base module(s) %s: parameter-gated features of a shared decode are not the core's" % ", ".join(os.path.basename(f) for f in shared))
    reds, nt, nr = compare(doc, files, shared)
    unchecked = {m for n in notes for m in re.findall(r"UNCHECKED module (\w+)", n)}
    dt = time.time() - t0
    return reds, notes, nt, nr, dt


def cmd_check(args):
    toml_a = None
    if "--toml-a" in args:
        i = args.index("--toml-a")
        toml_a = args[i + 1]
        del args[i:i + 2]
    srcdir = None
    if "--src" in args:
        i = args.index("--src")
        srcdir = args[i + 1]
        del args[i:i + 2]
    if args == ["--all"]:
        out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "core_toml.py"), "lanes"], capture_output=True, text=True, cwd=ROOT).stdout
        cores = [l.split()[0] for l in out.split("\n") if l.strip() and not l.startswith("Σ")]
    elif len(args) == 1:
        cores = args
    else:
        print(__doc__)
        return 2
    bad = 0
    for c in cores:
        try:
            reds, notes, nt, nr, dt = check_core(c, toml_a if len(cores) == 1 else None, srcdir)
        except Exception as e:           # a TOML A that does not load is red too
            print("toml_a_list: %s: RED %s (%s)" % (c, e, type(e).__name__))
            bad += 1
            continue
        for n in notes:
            print("toml_a_list: %s: %s" % (c, n))
        for r in reds:
            print("toml_a_list: %s: RED %s" % (c, r))
        print("toml_a_list: %s: %s (%d TOML A bytes, %d decoded bytes) in %.2f s" % (c, "RED %d" % len(reds) if reds else "PASS", nt, nr, dt))
        bad += bool(reds)
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if a and a[0] == "check":
        return cmd_check(a[1:])
    if a and a[0] == "selftest":
        return subprocess.call([sys.executable, "-I", os.path.join(ROOT, "tests", "toml_a_list", "test_toml_a_list.py")])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
