#!/bin/sh
# T89.1 S7, S8: the slot 7 card (tb_a2_hdd.vhd, asm/hdd.s, the card's own ROM assembled from rtl/roms/hdd.a65). Control: a protected unit takes a write.
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir hdd)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/hdd.s" "$out/hdd.hex" > /dev/null
python3 - "$root/cores/apple2/rtl/roms/hdd_rom.h" "$out/hddrom.hex" <<'P'
import re, sys
b = re.findall(r"0x([0-9A-Fa-f]{2})", open(sys.argv[1]).read())
assert len(b) == 256
open(sys.argv[2], "w").write("".join(x.lower() + "\n" for x in b))
P
a2_build tb_a2_hdd "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_hdd -gDIR="$out" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_hdd" "$out/run.log"
[ -z "$A2_NOCTL" ] || { echo "PASS hdd (bench only)"; exit 0; }
H="$root/cores/apple2/rtl/trx/a2_hdd.vhd"
# the controls are independent copies of the built design: run them JOBS at a time (default 3), report in order
ctl_one() {
  c=$(a2_ctl tb_a2_hdd "$out" "$1" "$2" "$3")
  ( cd "$c" && ghdl -r $A2_GHDL_OPTS tb_a2_hdd -gDIR="$out" --ieee-asserts=disable > ctl.log 2>&1 || true )
  { echo "== control $1 ($4, must fail)"; sed -n 's/^.*(report note): \(FAIL.*\)$/ctl| \1/p' "$c/ctl.log" | head -3; } > "$out/ctl_$1.txt"
  grep -q "FAIL tb_a2_hdd" "$c/ctl.log"
}
ctls=""
ctl() {
  ( ctl_one "$@"; echo $? > "$out/ctl_$1.rc" ) &
  ctls="$ctls $1"
  n=$((n + 1))
  if [ $((n % ${JOBS:-3})) -eq 0 ]; then wait; fi
}
n=0
ctl PROT "$H" "s/(r_cmd = x\"02\" and prot = '0')/(r_cmd = x\"02\")/" "a protected unit takes a write"
wait
bad=0
for k in $ctls; do
  cat "$out/ctl_$k.txt"
  [ "$(cat "$out/ctl_$k.rc")" = 0 ] || { echo "ctl| $k: the bench passed with the defect in"; bad=1; }
done
[ $bad = 0 ] || { echo "FAIL controls"; exit 1; }
echo "PASS hdd (bench passes, the write protect control fails)"
