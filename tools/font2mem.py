#!/usr/bin/env python3
"""Turn the fonts of Gideon's character generator (vendor/gideon) into $readmemh
files for host/overlay.v.

ovl_font8.mem: the 1024 bytes of char_generator_rom_pkg that his chargen
addresses (MSB of the 11-bit address held at 0): 128 chars x 8 rows.
ovl_font12.mem: font_pkg, 128 chars x 8 words of 36 bits (three 12-pixel lines,
the first in bits 11:0).
"""
import os
import subprocess
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
subprocess.run([os.path.join(here, "gideon_sources.sh")], check=True)
src = os.path.join(here, "..", "cores", "gideon", "fpga", "ip", "video")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "designs", "c64_basic", "mem")


def words(path, digits):
    text = open(path).read()
    text = re.sub(r"--.*", "", text)
    return re.findall(r'X"([0-9A-Fa-f]{%d})"' % digits, text)


f8 = words(os.path.join(src, "vhdl_source", "char_generator_rom_pkg.vhd"), 2)
f12 = words(os.path.join(src, "vhdl_gen", "font_pkg.vhd"), 9)
if len(f8) != 2048 or len(f12) != 1024:
    sys.exit(f"unexpected sizes: {len(f8)} bytes, {len(f12)} words")

os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "ovl_font8.mem"), "w") as f:
    f.write("\n".join(w.lower() for w in f8[:1024]) + "\n")
with open(os.path.join(out, "ovl_font12.mem"), "w") as f:
    f.write("\n".join(w.lower() for w in f12) + "\n")
print("written to", out)
