#!/bin/sh
# T182 step 5: the mirroring bench S5 of the C64SCPU core, spec T182 section 2 S5 and 5.7: the 65816 at 20 MHz writes the screen RAM, "bitmap" and probe pages in nine optimisation modes;
# the writes reach the C64 RAM (and so the VIC) exactly in the pages the mode mirrors (scpu64mem.c:81-86), the write loop takes the PHI2 cycles of the VICE accounting.
# The whole machine under Verilator; a synthetic SuperCPU ROM of our own (tools/gen_c64scpu_mirror.py) in the DDR2 model at 0x03080000. Three checks per run:
#   RAM      the C64 RAM bytes of 144 probes and of the 189 stack slots the BRK retry phase pushes (tb +EXPECT) against the generator's model of mem_mirrors
#   picture  the VIC's own pixels of a frame read back as text (check_screen.py): the rows written through the mirror with the display on, the probe cells of pages $04 and $07, colour RAM
#   cycles   check_mirror.py: 1,024 mirrored stores and the same unmirrored against the VICE model
# Negative controls (the mutated machine must FAIL at least one check, lines prefixed "ctl| "):
#   ctl_nomirror SCPU_MUT_NOMIRROR   nothing is mirrored      ctl_mirrwin SCPU_MUT_MIRRWIN  the window is one page too high
#   ctl_wball    SCPU_MUT_WBALL      the write buffer on every bank-0 write (step 4's rule): the unmirrored loop is too slow
#   ctl_noretry  SCPU_MUT_NORETRY    a write the VIC held off (badline) is not repeated: the picture has holes
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_mirror.sh
# C64SCPU_MAP_BUILD (the machine, shared with run_map.sh / run_speed.sh), C64SCPU_MIRROR_WORK the run directory.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
base="SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS"
bm=${C64SCPU_MAP_BUILD:-$here/build_map}
mk() { # mk <dir> <extra defines>; one build per directory: the nightly starts these benches at once (run 1188: four builds in build_map, a PCH race and an OOM kill)
  [ -x "$1/obj/tb_c64scpu" ] && return
  mkdir -p "$1"
  ( flock 9; [ -x "$1/obj/tb_c64scpu" ] || C64SCPU_SIM_DEFS="$base $2" sh "$here/build_machine.sh" "$1" ) 9>"$1.lock" || exit 1
}
mk "$bm" ""
work=${C64SCPU_MIRROR_WORK:-$here/mirror_run}
rm -rf "$work"; mkdir -p "$work"
rc=0

run() { # run <label> <build>
  d="$work/$1"; mkdir -p "$d"; cp -a "$2/run/." "$d/"
  exp=$(python3 "$root/tools/gen_c64scpu_mirror.py" "$d") || { echo "FAIL $1: generator"; exit 1; }
  (cd "$d" && "$2/obj/tb_c64scpu" +MS=100 +PIXDUMP=3 "$exp") > "$d/log" 2>&1
  python3 "$here/check_screen.py" "$d/pixdump.txt" "$d/expect.json" > "$d/pic.log" 2>&1
  python3 "$here/check_mirror.py" "$d/log" "$d/expect.json" > "$d/cyc.log" 2>&1
  ram=FAIL; grep -q "^PASS tb_c64scpu" "$d/log" && ram=PASS
  nprobe=$(grep -o '"probes": [0-9]*' "$d/expect.json" | grep -o '[0-9]*$')
  [ "$ram" = PASS ] && [ "$(grep -c '^ok ' "$d/log")" = "${nprobe:-x}" ] || ram=FAIL
  pic=FAIL; grep -q "^PASS check_screen" "$d/pic.log" && pic=PASS
  cyc=FAIL; grep -q "^PASS mirror" "$d/cyc.log" && cyc=PASS
}
show() { # show <label> <prefix>
  d="$work/$1"
  grep -E "^(scpu\||mir\||ce\||FAIL)" "$d/log" | head -5 | sed "s/^/$2/"
  echo "$2$(grep -c '^ok ' "$d/log") of $(grep -o '"probes": [0-9]*' "$d/expect.json" | grep -o '[0-9]*$') probe bytes in the C64 RAM equal the model"
  grep -E "^(pic\|.*(cell|frame)|PASS|FAIL)" "$d/pic.log" | head -4 | sed "s/^/$2/"
  grep -E "^(cyc\||PASS|FAIL)" "$d/cyc.log" | sed "s/^/$2/"
}

run mirror "$bm"
show mirror ""
echo "$ram+$pic+$cyc mirror (RAM $ram, picture $pic, cycles $cyc)"
[ "$ram" = PASS ] && [ "$pic" = PASS ] && [ "$cyc" = PASS ] || rc=1

ctl() { # ctl <label> <define>
  mk "${bm}_$1" "$2"
  run "ctl_$1" "${bm}_$1"
  show "ctl_$1" "ctl| "
  if [ "$ram" = PASS ] && [ "$pic" = PASS ] && [ "$cyc" = PASS ]; then echo "ctl| control ctl_$1: it PASSED all three checks, it must fail one"; rc=1; else echo "ctl| control ctl_$1: fails as it must (RAM $ram, picture $pic, cycles $cyc)"; fi
}
ctl nomirror SCPU_MUT_NOMIRROR
ctl mirrwin SCPU_MUT_MIRRWIN
ctl wball SCPU_MUT_WBALL
ctl noretry SCPU_MUT_NORETRY

[ $rc = 0 ] && echo "PASS run_mirror" || echo "FAIL run_mirror"
exit $rc
