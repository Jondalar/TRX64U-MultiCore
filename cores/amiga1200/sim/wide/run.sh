#!/bin/sh
# T97 / Discussion #94: AGA wide fetch of the A1200's amiga_mem (cores/amiga1200/rtl), chip port, Z2 and Zorro III fast port, miss and hit.
#   run.sh          must print PASS
#   OLD=1 run.sh    the two selection lines of the old rule (words 1..3 of the group, built from a patched copy): must FAIL
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out=$here/build${OLD:+old}
mkdir -p "$out"; cd "$out"
src=$root/cores/amiga1200/rtl/amiga_mem.v
if [ -n "$OLD" ]; then
  python3 - "$src" "$out/amiga_mem.v" <<'P'
import sys
s=open(sys.argv[1]).read()
a="wire [47:0] hit48 = {bank_q[{r_w[2:1], 1'b1}], bank_q[{r_w[2], 2'b10}], bank_q[{r_w[2], 2'b11}]};"
b="crit48 <= {u_w[1] ? sw_grp[63:48] : sw_grp[31:16], sw_grp[47:32], sw_grp[63:48]};"
assert s.count(a)==1 and s.count(b)==1
s=s.replace(a,"wire [47:0] hit48 = r_w[2] ? {bank_q[5], bank_q[6], bank_q[7]} : {bank_q[1], bank_q[2], bank_q[3]};")
s=s.replace(b,"crit48 <= {sw_grp[31:16], sw_grp[47:32], sw_grp[63:48]};")
open(sys.argv[2],'w').write(s)
P
  src=$out/amiga_mem.v
fi
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -DINFER_DPRAM -j 2 \
  --top-module tb_wide -o tb_wide "$here/tb_wide.sv" $root/host/sim/mig_ui_model.v $root/cores/amiga/rtl/trx/trx_afifo.v "$src" > build.log 2>&1 || { tail -40 build.log; exit 1; }
./obj_dir/tb_wide | tee run.log
grep -q '^PASS' run.log
