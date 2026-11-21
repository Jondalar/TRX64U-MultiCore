#!/usr/bin/env python3
"""T248 S1: write the expected class per address {area, index} (hex, one per line) from scpu_cfgtab.txt, using tools/scpu_cfgtab.py's parser. Usage: cfgtab_exp.py <out.hex>"""
import os, sys
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(root, "tools"))
import scpu_cfgtab as t
areas, cfg = t.read_txt(open(t.TXT).read())
out = []
for ai in range(16):
    for idx in range(256):
        out.append("%x" % (t.CLASSES.index(cfg[ai][idx]) if ai < 10 else 0))
open(sys.argv[1], "w").write("\n".join(out) + "\n")
