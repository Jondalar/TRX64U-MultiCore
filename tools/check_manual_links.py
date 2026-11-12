#!/usr/bin/env python3
"""Check the links and the paragraph cross-references of docs/manual/*.md and README.md (T169).

1. Every ](target) that is not http(s)/mailto/#anchor must name an existing file (relative to the file that holds it).
2. In docs/manual/*.md every cross-reference N.N or N.N.N (in code spans and in version-like context it is ignored)
   must point to a heading "## N.N" or to a paragraph that starts with "N.N.N ".
3. "chapter N" / "chapters N" must name a chapter 1..16 that exists.
Prints one line per finding; exit status 1 when there is any.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAN = os.path.join(ROOT, "docs", "manual")

files = sorted(os.path.join(MAN, f) for f in os.listdir(MAN) if f.endswith(".md"))
readme = os.path.join(ROOT, "README.md")

heads, paras, chapters = set(), set(), set()
for p in files:
    with open(p, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"#\s+(\d+)\.\s", line)
            if m:
                chapters.add(int(m.group(1)))
            m = re.match(r"##\s+(\d+\.\d+)\s", line)
            if m:
                heads.add(m.group(1))
            m = re.match(r"(\d+\.\d+\.\d+)\s", line)
            m0 = re.match(r"(0\.\d+)\s", line)
            if m0:
                heads.add(m0.group(1))
            if m:
                paras.add(m.group(1))
                heads.add(".".join(m.group(1).split(".")[:2]))

VERSION_WORDS = re.compile(
    r"(firmware|version|v|commodore|gideon|ultimate|release|build|host|interface|kickstart|altirra|rev|revision|"
    r"plus|nr|no|vice|dated|table|figure|port|up to|about)\s*$", re.I)

KNOWN_VERSIONS = {"1.1.0", "3.14", "3.0", "3.12", "3.15"}   # firmware versions that look like references
bad = []
for p in files + [readme]:
    rel = os.path.relpath(p, ROOT)
    with open(p, encoding="utf-8") as fh:
        text = fh.read()
    incode = False
    for n, line in enumerate(text.splitlines(), 1):
        if line.startswith("```"):
            incode = not incode
            continue
        if incode:
            continue
        for m in re.finditer(r"\]\(([^)\s]+)\)", line):
            t = m.group(1)
            if re.match(r"(https?:|mailto:|#)", t):
                continue
            t = t.split("#")[0]
            if t and not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(p), t))):
                bad.append("%s:%d: link target missing: %s" % (rel, n, t))
        if p == readme:
            continue
        plain = re.sub(r"`[^`]*`", "", line)
        plain = re.sub(r"\]\([^)]*\)", "]", plain)
        for m in re.finditer(r"(?<![\w./-])(\d{1,2}\.\d{1,2}(?:\.\d{1,2})?)(?![\w-]|\.\d)", plain):
            ref = m.group(1)
            before = plain[:m.start()]
            if VERSION_WORDS.search(before):
                continue
            if re.match(r"\d+\.\d+\.\d+\s", plain) and m.start() == 0:
                continue          # the paragraph's own number
            if re.match(r"##\s", plain) and m.start() < 4:
                continue
            ok = ref in paras if ref.count(".") == 2 else ref in heads
            if not ok and ref in KNOWN_VERSIONS:
                continue
            if not ok:
                bad.append("%s:%d: no paragraph %s   | %s" % (rel, n, ref, line.strip()[:90]))
        for m in re.finditer(r"[Cc]hapters? (\d+)", plain):
            if int(m.group(1)) not in chapters:
                bad.append("%s:%d: no chapter %s" % (rel, n, m.group(1)))

for b in bad:
    print(b)
print("checked %d manual files + README.md: %d finding(s)" % (len(files), len(bad)))
sys.exit(1 if bad else 0)
