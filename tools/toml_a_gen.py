#!/usr/bin/env python3
"""T264 P2 / S2: the address constants of a core's decode, generated from its TOML A.

  toml_a_gen.py gen CORE        write (refresh) the generated constants block in the core's own decode module(s)
  toml_a_gen.py check CORE      red when the block is missing or not what `gen` would write, or a constant is not used in the decode
  toml_a_gen.py check --all     every core of `core_toml.py lanes`
  toml_a_gen.py convert CORE    one-time: insert the block and rewrite the literals of the decode's case choices / `off == lit` compares
                                to the constants (the offsets stay the same; what is left over is printed for a hand edit)
  toml_a_gen.py selftest        the red fixtures of tests/toml_a_gen

The constants (`R_<BLOCK>_<PROPERTY>` = the offset of the property inside its page, 12 bit) sit in a block inside the decode module itself, between
the marker lines `>>> generated` and `<<< generated` (Verilog `localparam [11:0]`, VHDL `constant ... : integer`). Inside the module, so every flow that reads
the module (Vivado with host/ as its working directory, the mono builds, iverilog, yosys, GHDL) reads them with no include path and nothing a same-named
file could shadow. Byte k of a register is `R_X + k`, element i of an array `R_X + i*stride + k`, written as a number. The page (4 bit) stays a literal of the guard.
The decode of a module the core does not own (cores/c64u, cores/common) gets no block."""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import toml_a_list as T   # noqa: E402

ROOT = T.ROOT
BEGIN, END = ">>> generated", "<<< generated"


def own_modules(core, toml_a=None, srcdir=None):
    """({source path: [consts]}, doc): the consts of every property of the blocks whose decode module the core owns.
    const = (name, in-page offset, [(window byte, delta)])."""
    doc = T.load_doc(core, toml_a)
    out = {}
    for b in doc.get("block", []):
        a = b["address"]
        if T.DRIVE_LO <= a < T.DRIVE_HI or not T.WIN_BASE <= a < T.WIN_BASE + T.WIN_SIZE or b["module"] == "pix_layer":
            continue
        p = T.find_module(core, b["module"], srcdir)
        if p is None or re.search(r"/cores/(c64u|common)/", p) or not (T.decoded(p)[0] or region(open(p).read())):
            continue            # a module the core does not own, or one with no offset decode (the list check names it UNCHECKED)
        for pr in b.get("property", []):
            nb = max(1, pr["width"] // 8)
            cnt, st = pr.get("count", 1), pr.get("stride", nb)
            base = a - T.WIN_BASE + pr["offset"]
            if base >> 12 == 6:
                continue
            bytes_ = [(base + i * st + k, i * st + k) for i in range(cnt) for k in range(nb)]
            out.setdefault(p, []).append(("R_%s_%s" % (b["name"].upper(), pr["name"].upper()), base & 0xFFF, bytes_))
    return out, doc


def block_text(path, consts, rel_toml):
    vhdl = path.endswith((".vhd", ".vhdl"))
    c = "--" if vhdl else "//"
    ind = "  " if vhdl else "    "
    ln = ["%s%s %s from %s (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it" % (ind, c, BEGIN, rel_toml)]
    for n, off, _ in consts:
        if vhdl:
            ln.append("%sconstant %s : integer := 16#%03X#;" % (ind, n, off))
        else:
            ln.append("%slocalparam [11:0] %s = 12'h%03X;" % (ind, n, off))
    ln.append("%s%s %s" % (ind, c, END))
    return "\n".join(ln) + "\n"


def region(text):
    m = re.search(r"^[^\n]*%s[^\n]*\n(?:.*\n)*?[^\n]*%s[^\n]*\n" % (re.escape(BEGIN), re.escape(END)), text, re.M)
    return m


def insert_point(text, path, module):
    vhdl = path.endswith((".vhd", ".vhdl"))
    text = blank_comments(text, vhdl)          # a `;` in a port comment is no end of the header
    if vhdl:
        m = re.search(r"^\s*architecture\s+\w+\s+of\s+%s\s+is[^\n]*\n" % re.escape(module), text, re.M | re.I)
    else:
        m = re.search(r"\bmodule\s+%s\b[^;]*;[^\n]*\n" % re.escape(module), text)
    return m.end() if m else None


def modules_of(core, doc, path):
    return sorted({b["module"] for b in doc["block"] if T.find_module(core, b["module"]) == path})


def cmd_gen(core, toml_a=None, srcdir=None, insert=False):
    mods, doc = own_modules(core, toml_a, srcdir)
    rel = os.path.relpath(toml_a or os.path.join(ROOT, "cores", core, "app", "gateware.toml"), ROOT)
    for path, consts in mods.items():
        text = open(path).read()
        new = block_text(path, consts, rel)
        r = region(text)
        if r:
            text = text[:r.start()] + new + text[r.end():]
        elif insert:
            pt = insert_point(text, path, modules_of(core, doc, path)[0])
            if pt is None:
                print("toml_a_gen: %s: no insertion point in %s" % (core, path))
                return 1
            text = text[:pt] + new + text[pt:]
        else:
            print("toml_a_gen: %s: %s has no generated block (run convert)" % (core, os.path.relpath(path, ROOT)))
            return 1
        open(path, "w").write(text)
    return 0


def check_core(core, toml_a=None, srcdir=None):
    """-> list of red lines: a block that is missing or stale, a constant the decode does not use."""
    reds = []
    mods, doc = own_modules(core, toml_a, srcdir)
    rel = os.path.relpath(toml_a or os.path.join(ROOT, "cores", core, "app", "gateware.toml"), ROOT) if not toml_a else os.path.relpath(toml_a, ROOT)
    if not mods:
        reds.append("no decode module of the core's own found")
    for path, consts in mods.items():
        f = os.path.relpath(path, ROOT) if path.startswith(ROOT) else path
        text = open(path).read()
        r = region(text)
        if not r:
            reds.append("%s has no generated constants block" % f)
            continue
        want = block_text(path, consts, rel)
        if r.group() != want:
            reds.append("%s: the generated block is not current (TOML A changed: run tools/toml_a_gen.py gen %s)" % (f, core))
        vhdl = path.endswith((".vhd", ".vhdl"))
        code = T.strip_comments(text[:r.start()] + text[r.end():], vhdl)
        # a word-granular choice (`R_X >> 2`, `(R_X + 4) >> 2`) decodes the whole 2^sh-byte unit: the other constants inside that unit are decoded by it
        units = set()
        for m in re.finditer(r"\b(R_\w+)\s*(?:\+\s*(?:\d+'d)?(\d+))?\s*\)?\s*>>\s*(\d+)", code):
            for n, off, _ in consts:
                if n == m.group(1):
                    units.add(((off + int(m.group(2) or 0)) >> int(m.group(3)), int(m.group(3))))
        for n, off, _ in consts:
            if not re.search(r"\b%s\b" % n, code, re.I if vhdl else 0) and not any(off >> sh == u for u, sh in units):
                reds.append("%s: constant %s is not used in the decode" % (f, n))
    return reds


def lanes():
    out = subprocess.run([sys.executable, os.path.join(HERE, "core_toml.py"), "lanes"], capture_output=True, text=True, cwd=ROOT).stdout
    return [l.split()[0] for l in out.split("\n") if l.strip() and not l.startswith("Σ")]


# ---- one-time conversion ----

def blank_comments(t, vhdl):
    pat = r"/\*.*?\*/|--[^\n]*" if vhdl else r"/\*.*?\*/|//[^\n]*"
    return re.sub(pat, lambda m: re.sub(r"[^\n]", " ", m.group()), t, flags=re.S)


def scan_tokens(body, vhdl):
    """[(start, end, window address of the token's first byte, shift, kind, span)] of the literals the decode compares the offset with: case choices ('case'),
    `off == lit` ('cmp'), the bounds of `off >= lo && off < hi` ('lo', 'hi': span = hi - lo) and the base of `off - lit` ('sub')."""
    al = T.aliases(body)
    default = 0
    for n, p in al.items():
        if p != 6 and p != 0 and re.search(r"\b%s\b" % re.escape(n), body):
            default = p
    out = []
    for sel, sh, ch, at, toks in (T.case_blocks_vhdl(body) if vhdl else T.case_blocks_v(body)):
        pg = T.guard_page(body[max(0, at - 400):at], al, default)
        for v, s, e in toks:
            out.append((s, e, (pg << 12) | (v << sh), sh, "case", 0))
    cmp_re = r"\b(\w*off\w*)\s*(?:\[\s*(\d+)\s*:\s*(\d+)\s*\])?\s*%s\s*(%s)" % ("=" if vhdl else "==", T.LIT)
    def page_at(pos):
        ls = body.rfind("\n", 0, pos) + 1
        pg = T.guard_page(body[ls:pos], al, None)
        return default if pg is None else pg
    for m in re.finditer(cmp_re, body):
        v = T.num(m.group(4))
        if v is None or (m.group(2) and int(m.group(2)) != 11):
            continue
        lo = int(m.group(3)) if m.group(3) else 0
        out.append((m.start(4), m.end(4), (page_at(m.start()) << 12) | (v << lo), lo, "cmp", 0))
    rg = r"\b\w*off\w*\s*>=\s*(%s)\s*(?:&&|and)\s*\w*off\w*\s*(<|<=)\s*(%s)" % (T.LIT, T.LIT)
    for m in re.finditer(rg, body):
        lo, hi = T.num(m.group(1)), T.num(m.group(3)) + (1 if m.group(2) == "<=" else 0)
        pg = page_at(m.start())
        out.append((m.start(1), m.end(1), (pg << 12) | lo, 0, "lo", hi - lo))
        out.append((m.start(3), m.end(3), (pg << 12) | lo, 0, "hi", hi - lo))
    for m in re.finditer(r"\b\w*off\w*\s*-\s*(%s)" % T.LIT, body):
        out.append((m.start(1), m.end(1), (page_at(m.start()) << 12) | T.num(m.group(1)), 0, "sub", 0))
    return sorted(out)


def cmd_convert(core, toml_a=None, srcdir=None):
    mods, doc = own_modules(core, toml_a, srcdir)
    if cmd_gen(core, toml_a, srcdir, insert=True):
        return 1
    left = 0
    for path, consts in mods.items():
        vhdl = path.endswith((".vhd", ".vhdl"))
        text = open(path).read()
        bmap = {}
        for n, off, bytes_ in consts:
            for a, d in bytes_:
                bmap.setdefault(a, (n, d))
        r = region(text)
        blank = blank_comments(text, vhdl)
        blank = blank[:r.start()] + re.sub(r"[^\n]", " ", blank[r.start():r.end()]) + blank[r.end():]
        edits, miss = [], []
        for s, e, a0, sh, kind, span in scan_tokens(blank, vhdl):
            cand = [a0 + i for i in range(1 << sh) if a0 + i in bmap]
            if not cand:
                miss.append((s, text[s:e]))
                continue
            n, d = bmap[cand[0]]
            if kind == "hi":
                d += span
            if sh:
                rep = None if vhdl else (("%s >> %d" % (n, sh)) if d == 0 else "(%s + %d) >> %d" % (n, d, sh))
            elif d == 0:
                rep = n
            else:
                rep = ("%s + %d" % (n, d)) if vhdl else ("%s + 12'd%d" % (n, d))
                if kind == "sub":
                    rep = "(%s)" % rep
            if rep is None:
                miss.append((s, text[s:e]))
                continue
            edits.append((s, e, rep))
        seen = set()
        for s, e, rep in reversed(edits):
            if s in seen:
                continue
            seen.add(s)
            text = text[:s] + rep + text[e:]
        if vhdl:        # the choices are integer constants now: the selector is the offset as an integer
            text = re.sub(r"\bcase\s+off\s+is\b", "case to_integer(unsigned(off)) is", text, flags=re.I)
        open(path, "w").write(text)
        left += len(miss)
        for s, tok in miss:
            print("toml_a_gen: %s: left as literal %s at line %d" % (core, tok, text.count("\n", 0, s) + 1))
        print("toml_a_gen: %s: %s: %d literals replaced, %d left" % (core, os.path.relpath(path, ROOT), len(edits), len(miss)))
    return 0


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] in ("gen", "convert"):
        return (cmd_gen if a[0] == "gen" else cmd_convert)(a[1])
    if len(a) >= 2 and a[0] == "check":
        cores = lanes() if a[1] == "--all" else [a[1]]
        bad = 0
        for c in cores:
            reds = check_core(c)
            for r in reds:
                print("toml_a_gen: %s: RED %s" % (c, r))
            print("toml_a_gen: %s: %s" % (c, "RED %d" % len(reds) if reds else "PASS"))
            bad += bool(reds)
        return 1 if bad else 0
    if a and a[0] == "selftest":
        return subprocess.call([sys.executable, "-I", os.path.join(ROOT, "tests", "toml_a_gen", "test_toml_a_gen.py")])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
