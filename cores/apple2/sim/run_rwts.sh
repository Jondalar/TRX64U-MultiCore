#!/bin/sh
# T89.1 (t89-write): a DOS 3.3 style sector write and read back on a .nib track and a track format on a sector image's track (tb_a2_rwts.vhd, asm/rwts.s,
# gen_rwts.cc, chk_rwts.cc). Controls: the application plays 8-bit gaps for a .nib again (the next sector is lost after a write), writes are ignored, the
# write latch of drive 2 is never loaded (the format), the cell of a sync byte is written as ones.
# D1NIB=0 plays drive 1 with the bits of a sector image instead (the known good case).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir rwts)
nib=${D1NIB:-1}
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/rwts.s" "$out/rwts.hex" WDELAY=${WDELAY:-4} > /dev/null
nibcc="$root/coreapp/cores/apple2_nib.cc"
gen() { g++ -std=gnu++11 -O1 -I"$root/coreapp/cores" -o "$1/gen_rwts" "$here/gen_rwts.cc" "$2" && "$1/gen_rwts" "$1"; }
gen "$out" "$nibcc"
g++ -std=gnu++11 -O1 -I"$root/coreapp/cores" -o "$out/chk_rwts" "$here/chk_rwts.cc" "$nibcc"
g++ -std=gnu++11 -O1 -I"$root/coreapp/cores" -o "$out/chk_nibwrite" "$here/chk_nibwrite.cc" "$nibcc"
"$out/chk_nibwrite" 1                      # the application's bits: a write of every data field keeps all address fields (the board loss of T89.1 was 8-bit gaps)
"$out/chk_nibwrite" 0 > "$out/nw0.txt" && sed 's/^/ctl| /' "$out/nw0.txt"    # control: the bits of 8 cells per nibble lose them (exit 0 = it does)
a2_build tb_a2_rwts "$out"
rm -f "$out/saved_wr.hex" "$out/saved_fmt.hex"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_rwts -gDIR="$out" -gD1NIB=$nib --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_rwts" "$out/run.log"
if [ "$nib" = 1 ]; then "$out/chk_rwts" "$out" wr $(cat "$out/nibbits.txt"); else "$out/chk_rwts" "$out" wr; fi
"$out/chk_rwts" "$out" fmt
[ -z "$A2_NOCTL" ] || { echo "PASS rwts (bench only)"; exit 0; }
# the controls are independent copies of the built design: JOBS at a time (default 3), report in order. A control that changes the design is rebuilt from
# the mutated file (a2_ctl); one that changes the application's code regenerates the track files with a mutated apple2_nib.cc and plays the same simulation.
sim() {   # sim <dir> <s4> : run the bench of <dir> (its own files), the lines of the failure go to <dir>/ctl.log
  ( cd "$1" && ghdl -r $A2_GHDL_OPTS tb_a2_rwts -gDIR="$1" -gD1NIB=$nib -gRUN_S4=$2 --ieee-asserts=disable > ctl.log 2>&1 || true )
}
report() {   # report <name> <descr> <dir>
  { echo "== control $1 ($2, must fail)"; sed -n 's/^.*(report note): \(FAIL.*\)$/ctl| \1/p' "$3/ctl.log" | head -3; } > "$out/ctl_$1.txt"
  grep -q "(report note): FAIL" "$3/ctl.log"
}
ctl_core() {   # ctl_core <name> <file> <sed> <descr> <s4>
  c=$(a2_ctl tb_a2_rwts "$out" "$1" "$2" "$3")
  sim "$c" "$5"
  report "$1" "$4" "$c"
}
ctl_app() {    # ctl_app <name> <sed on apple2_nib.cc> <descr>
  c="${out}_ctl_$1"; rm -rf "$c"; cp -R "$out" "$c"; mkdir -p "$c/mut"
  sed "$2" "$nibcc" > "$c/mut/apple2_nib.cc"
  if cmp -s "$nibcc" "$c/mut/apple2_nib.cc"; then echo "ctl| $1: the mutation did not change apple2_nib.cc" >&2; exit 1; fi
  rm -f "$c/saved_wr.hex" "$c/saved_fmt.hex"
  gen "$c" "$c/mut/apple2_nib.cc" > /dev/null
  sim "$c" 0
  report "$1" "$3" "$c"
}
ctls=""
ctl() {
  ( "$@"; echo $? > "$out/ctl_$2.rc" ) &
  ctls="$ctls $2"
  n=$((n + 1))
  if [ $((n % ${JOBS:-3})) -eq 0 ]; then wait; fi
}
n=0
D="$root/cores/apple2/rtl/src/drive_ii.vhd"
ctl ctl_app NIBSYNC 's/if (run\[i\] >= 3 \&\& w.n/if (false \&\& w.n/' "the application plays 8-bit gaps for a .nib"
ctl ctl_core NOWRITE "$root/cores/apple2/rtl/src/disk_ii.vhd" "s/WRITE_MODE     => q7,/WRITE_MODE     => '0',/" "writes are ignored" 0
ctl ctl_core NOLOAD2 "$root/cores/apple2/rtl/src/disk_ii.vhd" "s/LOAD_REQ       => load_req and drive2_select,/LOAD_REQ       => '0',/" "the write latch of drive 2 is never loaded" 1
ctl ctl_core SYNCONES "$D" "s/                  wb := '0';/                  wb := '1';/" "an empty shift register writes ones" 0
wait
bad=0
for k in $ctls; do
  cat "$out/ctl_$k.txt"
  [ "$(cat "$out/ctl_$k.rc")" = 0 ] || { echo "ctl| $k: the bench passed with the defect in"; bad=1; }
done
[ $bad = 0 ] || { echo "FAIL controls"; exit 1; }
echo "PASS rwts (bench passes, all controls fail)"
