#!/bin/sh
# T89.1 S1-S4, S10: the Disk II bit engine (tb_a2_woz.vhd, asm/woz.s). Controls (disk write): writes ignored, the keep-old-track register ignored.
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir woz)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/woz.s" "$out/woz.hex" > /dev/null
g++ -std=gnu++11 -O1 -I"$root/coreapp/cores" -o "$out/gen_track" "$here/gen_track.cc" "$root/coreapp/cores/apple2_nib.cc"
"$out/gen_track" "$out"
a2_build tb_a2_woz "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_woz -gDIR="$out" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_woz" "$out/run.log"
[ -z "$A2_NOCTL" ] || { echo "PASS woz (bench only)"; exit 0; }
# the controls are independent copies of the built design: run them JOBS at a time (default 3), report in order
ctl_one() {
  c=$(a2_ctl tb_a2_woz "$out" "$1" "$2" "$3")
  ( cd "$c" && ghdl -r $A2_GHDL_OPTS tb_a2_woz -gDIR="$out" --ieee-asserts=disable > ctl.log 2>&1 || true )
  { echo "== control $1 ($4, must fail)"; sed -n 's/^.*(report note): \(FAIL.*\)$/ctl| \1/p' "$c/ctl.log" | head -3; } > "$out/ctl_$1.txt"
  grep -q "FAIL tb_a2_woz\|(report note): FAIL" "$c/ctl.log"
}
ctls=""
ctl() {
  ( ctl_one "$@"; echo $? > "$out/ctl_$1.rc" ) &
  ctls="$ctls $1"
  n=$((n + 1))
  if [ $((n % ${JOBS:-3})) -eq 0 ]; then wait; fi
}
n=0
ctl NOWRITE "$root/cores/apple2/rtl/src/disk_ii.vhd" "s/WRITE_MODE     => q7,/WRITE_MODE     => '0',/" "writes are ignored"
ctl NOKEEP "$root/cores/apple2/rtl/trx/a2_core.vhd" "s/KEEP_OLD => dcfg_s2(0),/KEEP_OLD => '0',/" "DISK_CFG is ignored"
wait
bad=0
for k in $ctls; do
  cat "$out/ctl_$k.txt"
  [ "$(cat "$out/ctl_$k.rc")" = 0 ] || { echo "ctl| $k: the bench passed with the defect in"; bad=1; }
done
[ $bad = 0 ] || { echo "FAIL controls"; exit 1; }
echo "PASS woz (bench passes, the write controls fail)"
