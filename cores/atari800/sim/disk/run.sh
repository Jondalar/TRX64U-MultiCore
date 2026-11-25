#!/bin/sh
# T108 D1: the Atari core boots a disk. The real XL OS and BASIC (cores/atari800/roms) run in the whole core under GHDL, a
# boot disk made here (bootdisk.asm, gen_disk.py) is mounted to D1:, and the application's own SIO responder
# (cores/atari800/app/lib/atari_sio.cc, compiled into the simulator by app_bridge.cc) answers through the register window.
#   run.sh disk     S1: the disk boots, the picture is the boot program's; prints the worst command-frame answer time
#   run.sh empty    S2: nothing in D1:, the OS must give up and end at the BASIC prompt
#   run.sh ghost    S2 control: an unmounted drive that answers anyway; the bench must FAIL "no command frame was answered"
#   run.sh nobaud   control: the application's source with match_baud() taken out (sed on a copy); the disk must NOT boot
#   run.sh slow     S4 control: the application's loop sleeps SLOW_US after every service call that did something; the
#                   disk must NOT boot any more (the bench must FAIL S1's picture check)
# Needs 64tass, python3, ghdl with a code generator that links (llvm or gcc, VHPIDIRECT) and g++; the Mac runs ghdl and g++
# in a container (`container build -t vc20-ghdl cores/atari800/sim/disk`, the Dockerfile beside this script), the rest on the host:
#   sh cores/atari800/sim/disk/run.sh disk
# To stop a run, stop the CONTAINER (`container list`, `container stop <id>`); killing this script only drops the client.
# Environment: MS (simulated ms after the cold start), PPM=1 (write build/frame_<mode>.ppm), SLOW_US (control, default 100000),
#   GHDL_O (default 2), WATCH_FROM/WATCH_TO (ms: log every wire edge and the OS's SIO variables), PC_FROM/PC_TO (us: log the bus address in ROM).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
what=${1:-disk}
[ $# -gt 0 ] && shift
out=$here/build
mkdir -p "$out"
prep() {
  # atari800_regs.h names the registers through the driver table of the app (drvtab.h): made by prep (once, before the container runs) from the core's TOML A as the app's link makes it (tests/coreapp/a8_drv.sh)
  ROOT=$root; . "$root/tests/coreapp/a8_drv.sh"; a8_drv_dir "$out/a8tab" || exit 1
  64tass -q -b --nostart -o "$out/bootdisk.bin" "$here/bootdisk.asm"
  python3 "$here/gen_disk.py" "$out/bootdisk.bin" "$out/boot.atr" > /dev/null
  python3 "$here/../machine/rom2hex.py" "$root/cores/atari800/roms/atarixl.rom" "$out/osxl.hex" > /dev/null
  python3 "$here/../machine/rom2hex.py" "$root/cores/atari800/roms/ataribas.rom" "$out/basxl.hex" > /dev/null
}
if ! command -v ghdl >/dev/null 2>&1; then
  prep
  exec nice -n 15 container run --rm --cpus 2 -v "$root":/work -w /work -e IN_CONTAINER=1 ${MS:+-e MS=$MS} ${PPM:+-e PPM=$PPM} ${SLOW_US:+-e SLOW_US=$SLOW_US} ${ACCESS_US:+-e ACCESS_US=$ACCESS_US} ${GHDL_O:+-e GHDL_O=$GHDL_O} ${WATCH_FROM:+-e WATCH_FROM=$WATCH_FROM} ${WATCH_TO:+-e WATCH_TO=$WATCH_TO} ${PC_FROM:+-e PC_FROM=$PC_FROM} ${PC_TO:+-e PC_TO=$PC_TO} vc20-ghdl sh /work/cores/atari800/sim/disk/run.sh "$what" "$@"
fi
[ -z "$IN_CONTAINER" ] && prep
. "$root/cores/atari800/sim/files.sh"
W=$out/work_$what
mkdir -p "$W"
cd "$W"
rm -f ./*.cf ./*.o
# the application's responder: the product source, or for a control a copy with one expression broken (as tests/coreapp/run.sh does)
src=$root/cores/atari800/app/lib
case "$what" in
  nobaud) mut='s/^    match_baud();$//' ;;
  *)      mut= ;;
esac
if [ -n "$mut" ]; then
  src=$out/mut_$what
  rm -rf "$src"; mkdir -p "$src"
  cp "$root"/cores/atari800/app/lib/atari_sio.* "$root"/cores/atari800/app/lib/atari_atr.* "$root"/cores/atari800/app/lib/atari_disk.h "$root"/cores/atari800/app/lib/atari800_regs.h "$src"/
  sed -e "$mut" "$src/atari_sio.cc" > "$src/atari_sio.cc.sed" && mv "$src/atari_sio.cc.sed" "$src/atari_sio.cc"
  if cmp -s "$src/atari_sio.cc" "$root/cores/atari800/app/lib/atari_sio.cc"; then echo "CONTROL $what: the sed expression changed nothing - the control is vacuous"; exit 1; fi
fi
nice -n 15 g++ -std=gnu++11 -O1 -fPIC -Wall -c -I"$src" -I"$out/a8tab/drv" -I"$root/coreapp/src" \
  "$here/app_bridge.cc" "$src/atari_sio.cc" "$src/atari_atr.cc"
nice -n 15 ghdl -i $A8OPTS --work=work $A8FILES "$here/../machine/tb_a8_pkg.vhd" "$here/tb_a8_disk.vhd" > import.log 2>&1 || { tail -20 import.log; exit 1; }
nice -n 15 ghdl -m $A8OPTS -O${GHDL_O:-2} -Wl,app_bridge.o -Wl,atari_sio.o -Wl,atari_atr.o -Wl,-lstdc++ -Wl,-lpthread tb_a8_disk > make.log 2>&1 || { tail -30 make.log; exit 1; }
G="-gOS_HEX=$out/osxl.hex -gBASIC_HEX=$out/basxl.hex -gRAMSEL=0"
# simulated time after the cold start: the disk boots at about 2.0 s; an empty D1: gets to READY after about 6.5 s; the ghost control
# is decided at the first answer (1.6 s)
case "$what" in
  empty) : "${MS:=6600}" ;;
  ghost) : "${MS:=1800}" ;;
  *)     : "${MS:=2300}" ;;
esac
[ -n "$MS" ] && G="$G -gRUN_MS=$MS"
[ -n "$ACCESS_US" ] && G="$G -gACCESS_US=$ACCESS_US"
[ -n "$WATCH_FROM" ] && G="$G -gWATCH_FROM_MS=$WATCH_FROM -gWATCH_TO_MS=${WATCH_TO:-$((WATCH_FROM + 40))}"
[ -n "$PC_FROM" ] && G="$G -gPC_FROM_US=$PC_FROM -gPC_TO_US=${PC_TO:-$((PC_FROM + 1000))}"
[ -n "$PPM" ] && G="$G -gPPM=$out/frame_$what.ppm"
case "$what" in
  disk)  export A8_ATR=$out/boot.atr; G="$G -gMODE=boot" ;;
  empty) G="$G -gMODE=basic" ;;
  ghost) export A8_GHOST=1; G="$G -gMODE=basic" ;;
  nobaud) export A8_ATR=$out/boot.atr; G="$G -gMODE=boot" ;;
  slow)  export A8_ATR=$out/boot.atr; G="$G -gMODE=boot -gSLOW_US=${SLOW_US:-100000}" ;;
  *) echo "run.sh disk|empty|ghost|slow|nobaud"; exit 2 ;;
esac
nice -n 15 ghdl -r $A8OPTS tb_a8_disk $G "$@" --ieee-asserts=disable > "run_$what.log" 2>&1 || true
show() { sed 's/^.*(report [a-z]*)://' "run_$what.log" | grep -E "os:|ok  |FAIL|PASS|error|Error|app:|sio:|picture:|frames run|cold start|loop ended|wire: command" || true; }
case "$what" in
  disk|empty) show; grep -q "PASS tb_a8_disk" "run_$what.log" ;;
  ghost|slow|nobaud)
    # a control: the bench has to fail, and on the check that is meant to see the defect, not by crashing. Its red lines are
    # printed as "red", so that a runner that treats FAIL as a failure sees the control for what it is.
    show | sed 's/FAIL/red/g'
    if grep -q "PASS tb_a8_disk" "run_$what.log"; then echo "CONTROL $what: the bench PASSED - it cannot see the defect"; exit 1; fi
    case "$what" in
      ghost) red="FAIL no command frame was answered" ;;
      *)     red="FAIL the picture" ;;
    esac
    grep -q "$red" "run_$what.log" || { echo "CONTROL $what: the check that must go red did not ($red) - a crash?"; exit 1; }
    echo "PASS control $what: the bench fails as it must" ;;
esac
