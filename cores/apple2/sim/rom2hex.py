#!/usr/bin/env python3
"""rom2hex.py <binary> <hex>: one byte a line, for the benches (VHDL cannot read a binary file). The inputs are third-party
images that live outside the repository; the hex files go to a scratch directory, never into the tree."""
import sys
data = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'w').write(''.join('%02x\n' % b for b in data))
