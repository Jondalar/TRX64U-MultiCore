#!/bin/sh
# T90, LOCAL ONLY: the whole C264 core with real PAL 264 ROMs under Verilator (T65 as Verilog from GHDL in a container).
#   sh run_boot.sh KERNAL.bin BASIC.bin [FUNC_LO.bin FUNC_HI.bin]    [MS=1600] [OUT=name]
# The ROM images are third-party and stay outside the repository (build/ is ignored).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
c=$root/cores/c264/rtl
out=$here/build
mkdir -p "$out"
[ -f "$c/src/ted.v" ] || { echo "run tools/vendor_c16nano.sh first"; exit 1; }
if [ ! -f "$out/t65_synth.v" ]; then
  t=/w/cores/c264/rtl/src/t65
  container run --rm -v "$root":/w -w /w/cores/c264/sim/build vc20-ghdl sh -c \
    "ghdl -a --std=08 -fsynopsys --work=work $t/T65_Pack.vhd $t/T65_ALU.vhd $t/T65_MCode.vhd $t/T65.vhd && ghdl --synth --std=08 -fsynopsys --out=verilog T65 > t65_synth.v"
  perl -pi -e 's/\bbreak\b/brk_/g' "$out/t65_synth.v"   # "break" is a SystemVerilog keyword
  # GHDL names the VHDL signals ALU_Q and alu_q (one name in Verilog): one declaration, no self-assignment
  python3 - "$out/t65_synth.v" <<'PY'
import sys
L = open(sys.argv[1]).read().split("\n")
seen = False; o = []
for l in L:
    if l.strip() == "wire [7:0] alu_q;":
        if seen: continue
        seen = True
    if l.strip() == "assign alu_q = alu_q; // (signal)": continue
    o.append(l)
open(sys.argv[1], "w").write("\n".join(o))
PY
fi
hex() { python3 -c "import sys;d=open(sys.argv[1],'rb').read();assert len(d)==16384;open(sys.argv[2],'w').write('\n'.join('%02x'%b for b in d))" "$1" "$out/$2.hex"; }
hex "$1" kernal; hex "$2" basic
args="+KERNAL=$out/kernal.hex +BASIC=$out/basic.hex +MS=${MS:-1600} +OUT=$out/${OUT:-boot}"
if [ -n "$4" ]; then hex "$3" flo; hex "$4" fhi; args="$args +FUNC_LO=$out/flo.hex +FUNC_HI=$out/fhi.hex"; fi
args="$args +MODEL=${MODEL:-0} +RAM=${RAM:-0}"
tb=${TB:-tb_c264_boot}
if [ ! -x "$out/obj_$tb/V$tb" ] || [ -n "$REBUILD" ]; then
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module $tb \
    -Mdir "$out/obj_$tb" --x-assign 0 --x-initial 0 $VOPTS \
    "$here/$tb.v" $c/trx/c264_*.v $c/src/ted.v $c/src/colors_to_rgb.v "$out/t65_synth.v" 2>&1 | tail -20
fi
cd "$out" && nice -n 15 "obj_$tb/V$tb" $args | tee "$tb.log"
