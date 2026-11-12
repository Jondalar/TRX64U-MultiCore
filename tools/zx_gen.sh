#!/bin/sh
# T91 D3: the Z80 (T80pa, VHDL, cores/zx/rtl/T80) converted to Verilog once by GHDL (--synth --out=verilog, in a container), so that
# Verilator and Vivado read the same source (as tools/nes_gen.py does for the NES's 6502). The result is cores/zx/gen/t80pa.v; the
# SHA-256 of the VHDL it was made from is in cores/zx/gen/t80pa.sha256. GHDL runs only in the container (debian:trixie-slim).
#   tools/zx_gen.sh           (re)make the Verilog
#   tools/zx_gen.sh --check   the VHDL is still the one the Verilog was made from (no GHDL needed; the lint stage runs this)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
rtl=$here/cores/zx/rtl/T80
out=$here/cores/zx/gen
vhd="T80_Pack.vhd T80_Reg.vhd T80_MCode.vhd T80_ALU.vhd T80.vhd T80pa.vhd"
sum=$(cd "$rtl" && cat $vhd | shasum -a 256 | cut -d' ' -f1)
if [ "$1" = "--check" ]; then
  [ -f "$out/t80pa.sha256" ] && [ "$(cat "$out/t80pa.sha256")" = "$sum" ] && { echo "zx_gen: t80pa.v matches the VHDL"; exit 0; }
  echo "zx_gen: cores/zx/gen/t80pa.v was not made from cores/zx/rtl/T80 (run tools/zx_gen.sh)"; exit 1
fi
mkdir -p "$out"
d=$(mktemp -d)
for f in $vhd; do cp "$rtl/$f" "$d/"; done
# GHDL names an instance's output wire <label>_<port>: instance `alu`, port Q, collides with T80's signal ALU_Q (two `alu_q` in the
# Verilog). The signal is renamed in the scratch copy of T80.vhd (rtl/ stays pristine); the count is checked.
n=$(grep -c -i -w ALU_Q "$d/T80.vhd" || true)
[ "$n" = 7 ] || { echo "zx_gen: expected 7 lines with ALU_Q in T80.vhd, found $n"; exit 1; }
perl -pi -e 's/\bALU_Q\b/ALUQs/gi' "$d/T80.vhd"
img=${ZX_GHDL_IMAGE:-debian:trixie-slim}
script="set -e; apt-get update -qq >/dev/null; apt-get install -y -qq ghdl >/dev/null 2>&1; cd /w; \
ghdl -a --std=08 -frelaxed -fsynopsys $vhd; ghdl --synth --std=08 -frelaxed -fsynopsys --out=verilog T80pa > t80pa.v"
nice -n 15 container run --rm --cpus 2 -m 4G -v "$d:/w" "$img" sh -c "$script"
# `do` is a SystemVerilog keyword and GHDL carries the VHDL name of t80's output port into the Verilog; three places (declaration,
# assignment, instance), counted.
n=$(grep -c -w do "$d/t80pa.v" || true)
[ "$n" = 3 ] || { echo "zx_gen: expected 3 uses of the name do, found $n"; exit 1; }
perl -pi -e 's/\bdo\b/do_/g' "$d/t80pa.v"
{ printf '%s\n' "// T91 D3: the ZX Spectrum core's Z80 (T80pa, VHDL, rtl/T80, BSD wording, Daniel Wallner / MikeJ / Sorgelig) converted by GHDL" \
  "// --synth --out=verilog; tools/zx_gen.sh made this file from the VHDL whose hash is in t80pa.sha256. Do not edit."; cat "$d/t80pa.v"; } > "$out/t80pa.v"
printf '%s\n' "$sum" > "$out/t80pa.sha256"
rm -rf "$d"
echo "zx_gen: wrote $out/t80pa.v ($(wc -l < "$out/t80pa.v") lines)"
