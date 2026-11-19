#!/usr/bin/env python3
"""Rebuild proof: assemble a dis65816 .asm with KickAssembler and compare to the original PRG.

Usage: python3 -I tools/dis65816/rebuild_check.py LISTING.asm ORIGINAL.prg [KickAss.jar]
Exit 0 only if byte-identical.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dis65816  # noqa: E402

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    jar = dis65816.find_kickass(sys.argv[3] if len(sys.argv) > 3 else None)
    if not jar:
        sys.exit("KickAss.jar not found")
    ok, msg = dis65816.rebuild_check(sys.argv[1], sys.argv[2], jar)
    print(("OK: " if ok else "FAIL: ") + msg)
    sys.exit(0 if ok else 2)
