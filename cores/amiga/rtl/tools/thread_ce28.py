#!/usr/bin/env python3
"""T85: thread a global clock enable `ce28` through the imported Minimig / fx68k sources.

Every clocked block `always @(posedge clk)` / `always_ff @(posedge clk)` gets `if (ce28)` after its event
control, every module that has such a block (or instantiates one that has) gets an input `ce28`, every
instance of such a module gets `.ce28(ce28)`. With ce28 = 1 on every clock the design is the original; with
ce28 pulsing, the whole machine advances only on the pulses (a stall freezes it, the 9/10 pattern makes the
28.37516 MHz chipset clock out of sys_clk). Blocks with more than one event (async reset) are reported and
patched by hand.   cores/amiga/rtl/tools/thread_ce28.py [--check] DIR...
"""
import re, sys, glob, os

CE = "ce28"
files = []
check = "--check" in sys.argv
for a in sys.argv[1:]:
    if a == "--check":
        continue
    files += glob.glob(os.path.join(a, "*.v")) + glob.glob(os.path.join(a, "*.sv"))

src = {f: open(f).read() for f in files}


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), s, flags=re.S)
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), s)


ALW = re.compile(r"\balways(?:_ff)?\s*@\s*\(\s*posedge\s+([\w\.]+)\s*\)")
ALW_MULTI = re.compile(r"\balways(?:_ff)?\s*@\s*\(\s*(?:posedge|negedge)[^)]*(?:,|\bor\b)[^)]*\)")
MOD = re.compile(r"^\s*module\s+(\w+)", re.M)

# module name -> (file, start, end)
mods = {}
for f, s in src.items():
    c = strip_comments(s)
    for m in MOD.finditer(c):
        e = c.find("endmodule", m.end())
        mods[m.group(1)] = (f, m.start(), e)

needs = set()
for name, (f, a, b) in mods.items():
    body = strip_comments(src[f])[a:b]
    if ALW.search(body):
        needs.add(name)
changed = True
while changed:
    changed = False
    for name, (f, a, b) in mods.items():
        if name in needs:
            continue
        body = strip_comments(src[f])[a:b]
        for n in needs:
            if re.search(r"^\s*%s\s*(#\s*\(|\w+\s*\()" % re.escape(n), body, re.M):
                needs.add(name)
                changed = True
                break

report = []
for f in files:
    s = src[f]
    c = strip_comments(s)
    out = []
    pos = 0
    edits = []  # (offset, text)
    # 1. clocked blocks
    for m in ALW.finditer(c):
        if re.match(r"\s*if\s*\(\s*%s\s*\)" % CE, c[m.end():]):
            continue
        edits.append((m.end(), " if (%s)" % CE))
    for m in ALW_MULTI.finditer(c):
        report.append("%s: multi-event block at %d: %s" % (f, c.count("\n", 0, m.start()) + 1, c[m.start():m.end()]))
    # 2. module ports and 3. instances
    for name, (ff, a, b) in mods.items():
        if ff != f or name not in needs:
            continue
        # first '(' after the module name, skipping a #( ... ) parameter block
        i = c.find(name, a) + len(name)
        while c[i].isspace():
            i += 1
        if c[i] == "#":
            j = c.find("(", i)
            d = 0
            while True:
                if c[j] == "(":
                    d += 1
                elif c[j] == ")":
                    d -= 1
                    if d == 0:
                        break
                j += 1
            i = j + 1
            while c[i].isspace():
                i += 1
        if c[i] != "(":
            report.append("%s: module %s: no port list" % (f, name))
            continue
        k = i + 1
        while c[k].isspace():
            k += 1
        if not re.match(r"(input|output|inout|\))", c[k:k + 6]):
            report.append("%s: module %s: not ANSI style" % (f, name))
            continue
        if re.match(r"input\s+wire\s+%s\b" % CE, c[k:]):
            continue
        edits.append((i + 1, " input wire %s,%s" % (CE, "" if c[k] != ")" else "")))
    for name in needs:
        pat = re.compile(r"^(\s*)(%s)(\s*)(#\s*\()?" % re.escape(name), re.M)
        for m in pat.finditer(c):
            # an instance: name [#(...)] inst (
            i = m.end()
            if m.group(4):
                d = 1
                while d:
                    i += 1
                    d += (c[i] == "(") - (c[i] == ")")
                i += 1
            mm = re.match(r"\s*(\w+)\s*(\[[^\]]*\]\s*)?\(", c[i:])
            if not mm or mm.group(1) in ("input", "output", "inout"):
                continue
            # skip the module declaration itself
            if re.match(r"\s*module\b", c[max(0, m.start() - 12):m.start() + 1]):
                continue
            j = i + mm.end()
            k = j
            while c[k].isspace():
                k += 1
            if re.match(r"\.%s\s*\(" % CE, c[k:]):
                continue
            edits.append((j, " .%s(%s),%s" % (CE, CE, "" if c[k] != ")" else "")))
    # instance edits for modules defined elsewhere are covered above (name in needs, any file)
    edits.sort(key=lambda e: e[0])
    # drop duplicates at the same offset with the same text
    seen = set()
    res = []
    last = 0
    for off, text in edits:
        if (off, text) in seen:
            continue
        seen.add((off, text))
        res.append(s[last:off])
        res.append(text)
        last = off
    res.append(s[last:])
    new = "".join(res)
    if new != s:
        if not check:
            open(f, "w").write(new)
        print("%s: %d edits" % (f, len(seen)))

print("modules needing %s: %d" % (CE, len(needs)))
for r in report:
    print("REPORT", r)
