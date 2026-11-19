#!/usr/bin/env python3
"""Check TODO.md against the detail files before a commit: every row's Open text must equal the
"Open (one line, as in the TODO table)" line of the file its Details link points to, and Open
stays short; a spec with status DONE must be in docs/specs/done/. Exit 1 on any mismatch.   tools/check_todo.py [--max N]"""
import os, re, sys

LINE = "Open (one line, as in the TODO table): "
MAX = int(sys.argv[sys.argv.index("--max") + 1]) if "--max" in sys.argv else 120
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(root)
bad = 0
for row in open("TODO.md", encoding="utf-8"):
    m = re.match(r"\| (T[\d.]+|OPT-[\w-]+) \|", row)
    if not m:
        continue
    cols = row.rstrip("\n").split("|")
    tid, status, open_ = m.group(1), cols[3].strip(), cols[-3].strip()
    if status not in ("✅", "✖", "❌") and len(open_) > MAX:
        print("%-8s Open is %d chars (max %d): %s..." % (tid, len(open_), MAX, open_[:60]))
        bad += 1
    link = re.search(r"\]\(([^)#]+)", cols[-2])
    if not link or not os.path.exists(link.group(1)):
        continue
    opens = [x.strip() for x in re.findall(re.escape(LINE) + "(.*)", open(link.group(1), encoding="utf-8").read())]
    if opens and open_ not in opens:
        print("%-8s TODO and %s differ:\n         TODO: %s\n         file: %s" % (tid, link.group(1), open_, " | ".join(opens)))
        bad += 1
# A spec whose status is DONE lives in docs/specs/done/ (Alex, 2026-11-19).
for f in sorted(os.listdir("docs/specs")):
    p = os.path.join("docs/specs", f)
    if f.endswith(".md") and os.path.isfile(p) and re.search(r"^\*\*Status:\*\*\s*(✅\s*)?DONE", open(p, encoding="utf-8").read(), re.M | re.I):
        print("%-8s status DONE but not in docs/specs/done/: %s" % (f.split("-")[0], p))
        bad += 1
print("check_todo: %s" % ("ok" if not bad else "%d problem(s)" % bad))
sys.exit(1 if bad else 0)
