#!/bin/sh
# T85 Freeze: the machine paused at random moments (pause_req, held for 16 clocks up to 62 ms) against the same machine never
# paused: the checks of tb_pause_mon.svh in the run, and the logs compared (cmp_pause.py). Verilator, one build per ROM.
#   run_pause.sh [stress|storm|pause|fstorm|audio|disk]   stress: DMA heavy, chip RAM only, exact; pause: adds fast RAM; disk: pauses in the sector service
set -e
here=$(cd "$(dirname "$0")" && pwd)
what=${1:-stress}
rv=""
case $what in
storm) env="MODE=stress MIGLAT=120 MIGMISS=30 MIGSTALL=25"; ra="+npause=150 +pfirst=100000 +pstep=23333 +hold_us=-2 +onpend=1"; kinds=HPFB; TICKS=${TICKS:-3700000} ;;      # a hundred and fifty short pauses at all moments
fstorm) env="MODE=pause FAST=1 MIGLAT=120 MIGMISS=30 MIGSTALL=25"; ra="+npause=150 +pfirst=100000 +pstep=23333 +hold_us=-2 +onpend=1"; rv="+verify=1"; kinds=FB; TICKS=${TICKS:-4200000} ;;      # the same with the CPU's fast RAM port busy
pause) env="MODE=pause FAST=1"; ra=""; rv="+verify=1"; kinds=FB ;;      # fast RAM: the CPU's wait states follow the DDR2, so exact for the picture only
stress) env="MODE=stress"; ra=""; kinds=HPFB ;;                  # chip RAM only: exact for every chip bus cycle
audio) env="MODE=audio AUDIO=1"; ra="+npause=3 +pfirst=3200000 +pstep=600000"; kinds=HPFB; TICKS=${TICKS:-5000000} ;;
disk)  env="MODE=disk DISK=1"; ra="+npause=40 +pfirst=2200000 +pstep=1000 +hold_us=30 +onsdc=1"; kinds=HPFB; TICKS=${TICKS:-6000000} ;;
esac
export PAUSE=1 FRAMES=100000 OUT_SUFFIX=p$what TICKS=${TICKS:-6500000}
out=$here/build/l2p$what
env $env RUNARGS="+log=$out/ref.log +npause=0 $rv" sh "$here/run_l2.sh" > /tmp/amiga_pause_$what.ref.log 2>&1 || { tail -30 /tmp/amiga_pause_$what.ref.log; echo "FAIL ref run"; exit 1; }
grep -E "^pause bench|^writes|PASS|FAIL" /tmp/amiga_pause_$what.ref.log
cd "$out"
nice -n 15 ./obj_dir/tb_l2 +log=$out/pau.log $ra $rv > /tmp/amiga_pause_$what.log 2>&1 || true
grep -E "^pause |^  audio|^pause bench|^writes|PASS|FAIL" /tmp/amiga_pause_$what.log
grep -q 'PASS tb_pause errors=0' /tmp/amiga_pause_$what.log || { echo "FAIL amiga_pause $what"; exit 1; }
python3 "$here/cmp_pause.py" "$out/ref.log" "$out/pau.log" $kinds
