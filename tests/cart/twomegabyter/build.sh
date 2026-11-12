#!/bin/sh
# The TwoMegabyter test cartridge (CRT 89) from Protovision's TwoMegabyter code
# 1.0.0, which is not in this repository (third party, no licence stated): the
# zip from TMB_ZIP (default ~/Downloads/twomegabyter-code-1.0.0.zip) is unpacked
# into build/, boot.ca65.s gets its `game:` loop replaced by a jump to the test
# (tmb_test.s), ca65/ld65 build bank 0, and every other bank gets markers.
#   build.sh  ->  build/tmb_test.crt
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build
zip=${TMB_ZIP:-$HOME/Downloads/twomegabyter-code-1.0.0.zip}
rm -rf "$out"
mkdir -p "$out"
unzip -q -j "$zip" -d "$out/src"
for f in boot.ca65.s twomegabyter.ca65.s twomegabyter-highscore-demo.ca65.s; do
    [ -f "$out/src/$f" ] || { echo "$f not in $zip" >&2; exit 1; }
done
# the boot block is placed by the linker config; its endless loop starts the test
{ sed -e 's/^[[:space:]]*\.org \$8000.*$//' -e '/^game:/,$d' "$out/src/boot.ca65.s"
  echo "game: jmp tmb_main"; } > "$out/src/boot_patched.s"
grep -q "jmp tmb_main" "$out/src/boot_patched.s"
ca65 -I "$out/src" -o "$out/tmb_test.o" "$here/tmb_test.s"
ld65 -C "$here/tmb_test.cfg" -o "$out/bank0.bin" "$out/tmb_test.o"
python3 - "$out/bank0.bin" "$out/tmb_test.crt" <<'PY'
import struct, sys
bank0 = bytearray(open(sys.argv[1], "rb").read())
assert len(bank0) == 0x4000
def marked(b, data):
    data[0x1FF0:0x1FF2] = bytes([b, 0x4C])      # ROML half: the bank, "L"
    data[0x3FF0:0x3FF2] = bytes([b, 0x48])      # ROMH half: the bank, "H"
    return data
h = bytearray(b"C64 CARTRIDGE   ")
h += struct.pack(">IHHBBB", 0x40, 0x0100, 89, 0, 0, 0) + bytes(5)
h += b"TWOMEGABYTER T33 TEST".ljust(32, b"\0")
for b in range(124):        # 124-127: the highscore sector, erased (not in the file)
    data = bank0 if b == 0 else bytearray(((b * 7 + i) & 0xFF) for i in range(0x4000))
    data = marked(b, bytearray(data))
    h += b"CHIP" + struct.pack(">IHHHH", 0x4010, 2, b, 0x8000, 0x4000) + bytes(data)
open(sys.argv[2], "wb").write(h)
print("built", sys.argv[2], len(h), "bytes")
PY
