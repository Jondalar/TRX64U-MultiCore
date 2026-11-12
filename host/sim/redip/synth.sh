#!/bin/sh
# T25.4: yosys synth_xilinx (xc7) resource numbers, relative to each other, not
# Vivado's (yosys usually lands 15-25 % above Vivado).
#   synth.sh ref        the unmodified reDIP (pinned base, see build_ref.sh), sid_api, 2 SIDs
#   synth.sh api2       sid_api in the tree (NS = 2, equivalence build)
#   synth.sh multi N    sid_multi with N SIDs, the hardware configuration (LEGACY_FC = 2, variant A)
#   synth.sh multiB N   the same with variant B tables
#   synth.sh multiF N   same as multi (kept as a name)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
g=$root/vendor/icepi/c64_redip_sid
w=$g/reDIP-SID/gateware
b=$here/build/synth
mkdir -p "$b"
cp "$w"/*.mem "$b/"
cd "$b"
export PATH=${OSS:-$HOME/.cache/trx64-oss/oss-cad-suite}/bin:$PATH
what=$1
# slang cannot evaluate $rtoi/$tanh/$pow in initial blocks: the tables go in as .mem files
patch_tables() {
python3 - "$1" "$b" <<'PY'
import math, re, sys
src, b = sys.argv[1:3]
p = src + "/sid_filter.sv"
s = open(p).read()
t = [int(1.048576/8*2*math.pi*12000*math.tanh(i/350.0) + 0.5) for i in range(1024)]
open(b + "/w0_tanh.mem", "w").write("".join("%04x\n" % v for v in t))
q6 = [int(1024.0/(0.707 + r/15.0) + 0.5) for r in range(16)]
q8 = [int(1024.0*math.pow(2, (4 - r)/8.0) + 0.5) for r in range(16)]
open(b + "/q6.mem", "w").write("".join("%03x\n" % v for v in q6))
open(b + "/q8.mem", "w").write("".join("%03x\n" % v for v in q8))
pat = re.compile(r"initial begin\s+for \(int i = 0; i < 1024; i\+\+\) begin.*?w0_T_lsl17_6581_y0 = 16'\(\$rtoi\(.*?\);\s+end", re.S)
s = pat.sub(lambda m: 'initial begin $readmemh("w0_tanh.mem", w0_T_lsl17_6581_tanh); w0_T_lsl17_6581_y0 = 16\'d%d; end' % int(1.048576/8*2*math.pi*12000*1 + 0.5), s)
pat = re.compile(r"initial begin\s+for \(int res = 0; res < 16; res\+\+\) begin.*?end\s+end", re.S)
s = pat.sub('initial begin $readmemh("q6.mem", _1_Q_6581_lsl10); $readmemh("q8.mem", _1_Q_8580_lsl10); end', s)
open(p, "w").write(s)
PY
}
case $what in
  ref)
    src=$b/ref_src; rm -rf "$src"; mkdir -p "$src"; cp "$here"/build/ref/src/*.sv "$src/"; patch_tables "$src"
    files="$src/sid_pkg.sv $src/muladd.sv $src/sid_dac.sv $src/sid_pot.sv $src/sid_waveform.sv $src/sid_envelope.sv $src/sid_voice.sv $src/sid_filter.sv $src/sid_control.sv $src/sid_api.sv"
    top=sid_api; opt="" ;;
  api2)
    src=$b/api_src; rm -rf "$src"; mkdir -p "$src"; cp "$w"/*.sv "$g/muladd.sv" "$g/sid_api.sv" "$src/"; patch_tables "$src"
    files="$src/sid_pkg.sv $src/muladd.sv $src/sid_dac.sv $src/sid_pot.sv $src/sid_waveform.sv $src/sid_envelope.sv $src/sid_voice.sv $src/sid_filter.sv $src/sid_control.sv $src/sid_core.sv $src/sid_api.sv"
    top=sid_api; opt="" ;;
  multi|multiB|multiF)
    src=$b/multi_src; rm -rf "$src"; mkdir -p "$src"; cp "$w"/*.sv "$g/muladd.sv" "$g/sid_multi.sv" "$src/"; patch_tables "$src"
    files="$src/sid_pkg.sv $src/muladd.sv $src/sid_dac.sv $src/sid_waveform.sv $src/sid_envelope.sv $src/sid_voice.sv $src/sid_filter.sv $src/sid_control.sv $src/sid_core.sv $src/sid_multi.sv"
    top=sid_multi
    vb=0; [ $what = multiB ] && vb=1
    lf=2
    opt="-G NS=${2:-8} -G LEGACY_FC=$lf -G VARIANT_B=$vb -G MEMDIR=\".\"" ;;
esac
yosys -q -m slang -l "$b/$what.log" -p "read_slang --allow-use-before-declare -D YOSYS -I $w --top $top $opt $files; synth_xilinx -family xc7 -flatten -top $top; tee -o $b/$what.stat stat; tee -o $b/$what.ltp ltp" > /dev/null 2>&1 || { tail -20 "$b/$what.log"; exit 1; }
awk '
/^ +[0-9]+ +[A-Za-z0-9_]+$/ { n[$2]=$1 }
END {
  lut=0; ff=0; ram32=0; srl=0;
  for (k in n) {
    if (k ~ /^LUT[1-6]$/) lut+=n[k];
    if (k ~ /^FD/) ff+=n[k];
    if (k ~ /^RAM(32|64)/) ram32+=n[k];
    if (k ~ /^SRL/) srl+=n[k];
  }
  printf "LUT(logic) %d  FF %d  SRL %d  RAM32/64 cells %d  RAMB36 %d  RAMB18 %d  DSP48 %d  CARRY4 %d\n", lut, ff, srl, ram32, n["RAMB36E1"], n["RAMB18E1"], n["DSP48E1"], n["CARRY4"];
  printf "  (LUT as memory/shift: SRL %d + RAM cells x4 ~ %d; RAM tiles %.1f)\n", srl, ram32*4, n["RAMB36E1"] + n["RAMB18E1"]/2;
}' "$b/$what.stat"
grep -E "Longest topological path" "$b/$what.ltp" | tail -1
