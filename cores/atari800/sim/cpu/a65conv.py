#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Klaus Dormann's / Bruce Clark's .a65 sources (AS65 syntax) -> 64tass, for the tests that come only as source
(the decimal and interrupt tests). The sources are GPL-3 / public domain and stay outside the repository.
   a65conv.py IN.a65 OUT.asm [NAME=VALUE ...]     (NAME=VALUE overrides a configuration line; @TEXT=>REPLACEMENT replaces every line holding TEXT)"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
ovr = dict(a.split("=", 1) for a in sys.argv[3:] if not a.startswith("@"))
subs = [a[1:].split("=>", 1) for a in sys.argv[3:] if a.startswith("@")]   # @text=>replacement: a source line holding text becomes replacement
out = []
params = []
for line in open(src, encoding="latin-1"):
    line = line.rstrip("\n")
    code, sc, comment = line.partition(";")
    s = code.rstrip()
    hit = [r for t, r in subs if t in line]
    if hit:
        out.append("\t" + hit[0])
        continue
    if not s.strip():
        out.append(line)
        continue
    m = re.match(r"^(\w+)\s*=\s*([^\s;]+)", s)
    if m and m.group(1) in ovr:
        out.append("%s = %s" % (m.group(1), ovr[m.group(1)]))
        continue
    toks = s.split()
    low = [t.lower() for t in toks]
    lab = "" if s[0] in " \t" else toks[0]
    rest = toks[1:] if lab else toks
    rl = [t.lower() for t in rest]
    if rl and rl[0] in ("bss", "code", "data", "noopt") and len(rest) == 1:
        out.append(";" + line)
        continue
    if rl and rl[0] == "end":
        continue
    if rl and rl[0] == "macro":
        params = [p for p in " ".join(rest[1:]).replace(",", " ").split()]
        out.append("%s .macro" % lab)
        continue
    if rl and rl[0] == "endm":
        params = []
        out.append(".endm")
        continue
    if rl and rl[0] == "equ":
        out.append("%s = %s" % (lab, " ".join(rest[1:])))
        continue
    if rl and rl[0] in ("if", "else", "endif", "elseif"):
        cond = " ".join(rest[1:])
        for i, p in enumerate(params):
            cond = re.sub(r"\b%s\b" % re.escape(p), lambda _m, i=i: "\\%d" % (i + 1), cond)
        out.append(".%s %s" % (rl[0], cond) if rl[0] != "elseif" else ".elsif " + cond)
        continue
    if rl and rl[0] == "org":
        out.append("* = " + " ".join(rest[1:]))
        continue
    if rl and rl[0] == "ds":
        out.append((lab + " " if lab else "") + ".fill " + " ".join(rest[1:]))
        continue
    if rl and rl[0] == "db":
        out.append((lab + " " if lab else "") + ".byte " + " ".join(rest[1:]))
        continue
    if rl and rl[0] == "dw":
        out.append((lab + " " if lab else "") + ".word " + " ".join(rest[1:]))
        continue
    body = re.sub(r"\\\?", "", (lab + " " if lab else "\t") + " ".join(rest))
    for i, p in enumerate(params):
        body = re.sub(r"\b%s\b" % re.escape(p), lambda _m, i=i: "\\%d" % (i + 1), body)
    out.append(body + (";" + comment if sc else ""))
# macro calls need a leading # in 64tass
names = set(re.findall(r"^(\w+)\s+\.macro", "\n".join(out), re.M))
res = []
for l in out:
    m = re.match(r"^(\w+\s+|\s+)(\w+)(\s.*|)$", l)
    if m and m.group(2) in names and ".macro" not in l:
        l = m.group(1) + "#" + m.group(2) + m.group(3)
    res.append(l)
txt = "\n".join(res) + "\n"
txt = txt.replace(".byte $db", "jmp *")
open(dst, "w").write(txt)
