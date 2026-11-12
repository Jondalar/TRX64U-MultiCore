#!/usr/bin/env python3
"""Apply a normal-format diff (plain `diff a b` output) to a file and print the result.

T97: check_wrapper_fork.sh compares the A1200 forks by result, not by diff text, because GNU and BSD diff
place the hunks of the same change differently; the CI sim image has no `patch`.
Usage: apply_normal_diff.py <original> <diff>  -> patched text on stdout, exit 1 when a hunk does not match.
"""
import re
import sys

src = open(sys.argv[1]).read().splitlines(keepends=True)
out, pos = [], 0  # pos: 0-based index of the next unconsumed source line
lines = open(sys.argv[2]).read().splitlines(keepends=True)
i = 0
while i < len(lines):
    m = re.match(r"(\d+)(?:,(\d+))?([acd])(\d+)(?:,(\d+))?$", lines[i].rstrip("\n"))
    if not m:
        sys.exit("bad hunk header: " + lines[i])
    a1, a2, op = int(m.group(1)), int(m.group(2) or m.group(1)), m.group(3)
    i += 1
    old, new = [], []
    while i < len(lines) and lines[i].startswith("< "):
        old.append(lines[i][2:]); i += 1
    if i < len(lines) and lines[i].startswith("---"):
        i += 1
    while i < len(lines) and lines[i].startswith("> "):
        new.append(lines[i][2:]); i += 1
    if i < len(lines) and lines[i].startswith("\\"):  # "\ No newline at end of file"
        i += 1
    start = a1 if op == "a" else a1 - 1  # 'a' adds after line a1, 'c'/'d' replace lines a1..a2
    out += src[pos:start]
    if op != "a":
        if [l.rstrip("\n") for l in src[start:a2]] != [l.rstrip("\n") for l in old]:
            sys.exit(f"hunk at {a1} does not match")
        pos = a2
    else:
        pos = start
    out += new
out += src[pos:]
sys.stdout.write("".join(out))
