#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""cmp_logs.py ORIG.txt OURS.txt: the two bus logs of tb_mini must be the same line for line. The reset sequences differ by
construction (Watson's 6502 reads the reset vector one cycle earlier than proc_core, which first does a dummy read), so
the logs are compared from the first line on which both are in step: some leading offset k <= 8 for which
orig[k:] and ours[k:] agree up to the shorter of the two."""
import sys

a = [l.split() for l in open(sys.argv[1])]
b = [l.split() for l in open(sys.argv[2])]
for k in range(0, 9):
    n = min(len(a), len(b)) - k
    if a[k:k + n] == b[k:k + n]:
        print("IDENTICAL: %d bus lines from line %d (orig %d lines, ours %d)" % (n, k, len(a), len(b)))
        sys.exit(0)
for i, (x, y) in enumerate(zip(a, b)):
    if x != y and i > 8:
        print("DIFFER at line %d\n orig %s\n ours %s" % (i, a[i - 3:i + 3], b[i - 3:i + 3]))
        break
sys.exit(1)
