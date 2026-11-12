#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""lstaddr.py LISTING REGEX -> decimal address of the first 64tass listing line that matches (".xxxx ..." lines)"""
import re, sys
for l in open(sys.argv[1], errors="replace"):
    m = re.match(r"^[.>]([0-9a-f]{4})\b", l)
    if m and re.search(sys.argv[2], l):
        print(int(m.group(1), 16))
        sys.exit(0)
sys.exit(1)
