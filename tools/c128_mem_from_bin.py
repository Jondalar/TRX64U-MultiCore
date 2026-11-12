#!/usr/bin/env python3
"""Bench helper: a binary file as a $readmemh file, one byte per line. tools/c128_mem_from_bin.py <in> <out> [--mirror <total bytes>]
(--mirror repeats the input to that size: the 16 KiB 1541 ROM into the drive's 32 KiB window). Local bench use only (the 1541 ROM stays outside the repository)."""
import sys
d = open(sys.argv[1], "rb").read()
if "--mirror" in sys.argv:
    n = int(sys.argv[sys.argv.index("--mirror") + 1])
    d = (d * (n // len(d) + 1))[:n]
open(sys.argv[2], "w").write("".join("%02x\n" % b for b in d))
