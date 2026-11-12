#!/usr/bin/env python3
"""One byte per line, uppercase hex: the form the machine benches read an image in (the same as
gen_images.py writes for the test images).   rom2hex.py <rom> <hex>"""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
with open(dst, "w") as f:
    f.write("".join("%02X\n" % b for b in data))
print("%s: %d bytes -> %s" % (src, len(data), dst))
