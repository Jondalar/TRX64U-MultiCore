#!/bin/sh
# T92: the CPC core's benches under Verilator (cpc_core with cores/cpc/rtl, gen, trx and the Spectrum core's shared parts), programs of our own
# (tools/cpc_progs.py, tools/zx_progs.py), no ROM. A mode ends with one PASS or FAIL line.
#   sh cores/cpc/sim/run.sh [mode ...]        default: all; ZX_REBUILD=1 rebuilds the binaries
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
c=$root/cores/cpc
zx=$root/cores/zx
src="$c/rtl/GA40010/ga40010.sv $c/rtl/GA40010/video.sv $c/rtl/GA40010/casgen_sync.v $c/rtl/GA40010/syncgen_sync.v $c/rtl/GA40010/rslatch.v \
 $c/rtl/UM6845R.v $c/rtl/i8255.v $c/rtl/Amstrad_MMU.v $c/rtl/color_mix.sv $c/gen/u765.sv $zx/rtl/ym2149.sv $zx/gen/t80pa.v $zx/shared/*.v $c/trx/*.v"
build() { # build <dir> [verilator options]
  d=$1; shift
  mkdir -p "$d"
  # CI starts several benches at once (ci/sims.sh -j) on one Mdir: they must not build side by side (the same
  # objects written by all, and one cc1plus of this model per bench and per core ran out of memory in run 126).
  # One build at a time on the machine; whoever gets the lock re-checks whether the binary is there by now.
  lock=${TRX64_SIM_BUILD_LOCK:-${TMPDIR:-/tmp}/trx64-verilator-build.lock}
  until mkdir "$lock" 2>/dev/null; do
    op=$(cat "$lock/pid" 2>/dev/null || true)
    if [ -n "$op" ] && ! kill -0 "$op" 2>/dev/null; then rm -rf "$lock"; fi # holder died
    sleep 2
  done
  echo $$ > "$lock/pid"
  trap 'rm -rf "$lock"' EXIT INT TERM
  if [ ! -x "$d/Vtb_cpc" ] || [ -n "$ZX_REBUILD" ]; then
    # shellcheck disable=SC2086
    nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_cpc -UVERILATOR -I"$here" \
      -Mdir "$d" --x-assign 0 --x-initial 0 "$@" "$here/tb_cpc.v" "$here/ddr2c_model.v" $src > "$d/build.log" 2>&1 || { tail -30 "$d/build.log"; echo "FAIL build"; exit 1; }
  fi
  rm -rf "$lock"; trap - EXIT INT TERM
}
base=$here/build/obj
sim() { bd=$1; ln=$2; tm=$3; shift 3; "$bd/Vtb_cpc" +MODE="$tm" "$@" $ZX_PLUSARGS > "$here/build/$ln.log" 2>&1 || true; }
verdict() { grep -E "^(PASS|FAIL|  mismatch|  known|  window|  keys|  disk|clk|audio|tape|disk)" "$here/build/$1.log" || true; }
modes=${*:-"z80s clk ctl_rom page page664 page464"}
mkdir -p "$here/build"
rc=0
for m in $modes; do
  tm=$m
  case $m in
    z80s)    args="+ROM=$zx/sim/prog/z80s.hex +EXP=$zx/sim/prog/z80s.exp.hex +FRAMES=200" ;;
    clk)     args="+ROM=$here/prog/clk.hex" ;;
    bars)    args="+ROM=$here/prog/bars.hex +FRAMES=40" ;;
    model)   args="+ROM=$here/prog/page.hex +EXP=$here/prog/page.exp.hex +EXP2=$here/prog/page464.exp.hex +FRAMES=60" ;;
    rom_cat) # MANUAL, not in any CI list: the real ROMs (CPC_ROMDIR holds os6128.rom, basic6128.rom, amsdos.rom; never in the repo), a blank 40 track DSK, CAT typed
             # ZX_PLUSARGS="+MOUNTAT=135 +RESET2 +OBS=100": mount into the running machine at that frame, repeat after a warm reset; ~10 frames a minute, give it an hour
             [ -n "$CPC_ROMDIR" ] || { echo "FAIL rom_cat: set CPC_ROMDIR"; exit 1; }
             for r in os6128 basic6128 amsdos; do python3 -c "import sys;d=open(sys.argv[1],'rb').read();open(sys.argv[2],'w').write(''.join('%02x\n'%b for b in d))" "$CPC_ROMDIR/$r.rom" "$here/build/$r.hex"; done
             # CPC_DSK=game: the game-style Extended DSK (tools/cpc_progs.py ddrgame, 204,544 bytes) instead of the blank 40 track one
             if [ "$CPC_DSK" = game ]; then python3 "$root/tools/cpc_progs.py" ddrgame "$here/build/rc.ddr.hex"; dsz=204544
             else python3 "$root/tools/cpc_progs.py" ddr40 "$here/build/rc.ddr.hex"; dsz=194816; fi
             args="+OSROM=$here/build/os6128.hex +BASROM=$here/build/basic6128.hex +AMSROM=$here/build/amsdos.hex +DDR=$here/build/rc.ddr.hex +DSZ=$dsz +FRAMES=2000 +MAXCLK=2000000000" ;;
    slots)   args="+ROM=$here/prog/slots.hex +FRAMES=20" ;;
    video)   args="+ROM=$here/prog/video.hex +FRAMES=80" ;;
    pause_ref|pause_run) args="+ROM=$here/prog/anim.hex" ;;
    audio)   args="+ROM=$here/prog/audio.hex +AY_TP=100 +FRAMES=90" ;;
    keys)    python3 "$root/tools/zxcpc_keys.py" cpc-vectors > "$here/build/keys.vec"; args="+ROM=$here/prog/keys.hex +VEC=$here/build/keys.vec" ;;
    tape)    args="+ROM=$here/prog/tape.hex +EXP=$zx/sim/prog/tape48.exp.hex +DDR=$zx/sim/prog/tape48.ddr.hex +FRAMES=130" ;;
    disk)    python3 "$root/tools/cpc_progs.py" ddr "$here/build/disk.ddr.hex"; args="+ROM=$here/prog/disk.hex +DDR=$here/build/disk.ddr.hex +FRAMES=200" ;;
    page|page664|page464) args="+ROM=$here/prog/page.hex +EXP=$here/prog/$m.exp.hex +FRAMES=40" ;;
    pause) ;;
    fdcscan|fdcscan3|fdcfix) ;;      # the image block service alone (u765 + cpc_fdc + the DDR2 model), no machine; fdcscan3 = only its READ DATA / strobe-pair group
    *) echo "unknown mode $m"; exit 1 ;;
  esac
  if [ "$m" = pause ]; then
    build "$base"
    [ -s "$here/build/pause_ref.f" ] || { sim "$base" pause_ref pause_ref +ROM="$here/prog/anim.hex"; grep '^F ' "$here/build/pause_ref.log" | head -24 > "$here/build/pause_ref.f"; }
    sim "$base" "$m" pause_run +ROM="$here/prog/anim.hex"
    grep '^F ' "$here/build/$m.log" | head -24 > "$here/build/$m.f" || true
    # every machine frame the paused run printed must be the reference's, line for line
    if diff "$here/build/$m.f" "$here/build/pause_ref.f" | grep -q '^<'; then same=0; else same=1; fi
    nlines=$(wc -l < "$here/build/$m.f" | tr -d ' ')
    r=$(grep -E "^(PASS|FAIL|pause_run)" "$here/build/$m.log" || true)
    if [ "$same" = 1 ] && [ "$nlines" -ge 20 ]; then
      r="$r
PASS pause hashes: all $nlines frames of the paused run equal the reference run's (picture, screen RAM, Z80 registers)"
    else
      r="$r
FAIL pause hashes: the paused run differs from the reference in $(diff "$here/build/$m.f" "$here/build/pause_ref.f" | grep -c '^<') of $nlines frames"
    fi
  elif [ "$m" = fdcscan ] || [ "$m" = fdcscan3 ]; then
    # fdcscan: the mount/reset/second-event groups with CYCLES 4000 (the core's value; the bench's ce is one in eight sys clocks where the core's is about one
    # in four, so this drive turns at half the core's speed in sys clocks); fdcscan3: the READ DATA groups, whose drive turns 16 times faster than the core's
    # (S3_CYCLES 125 in the bench, a revolution 0.41M sys clocks, the core: about 6.5M), as its own binary
    fd=$base/$m
    if [ "$m" = fdcscan ]; then vdef="-DS3_CYCLES=4000"; fa=+S3SKIP; else vdef=""; fa=+S3ONLY; fi
    python3 "$root/tools/cpc_progs.py" ddr40 "$here/build/blank40.ddr.hex"
    if [ ! -x "$fd/Vtb_fdcscan" ] || [ -n "$ZX_REBUILD" ]; then
      mkdir -p "$fd"
      # shellcheck disable=SC2086
      nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 $vdef --top-module tb_fdcscan --public-flat-rw -I"$here" -Mdir "$fd" \
        --x-assign 0 --x-initial 0 "$here/tb_fdcscan.v" "$here/ddr2c_model.v" $c/gen/u765.sv $c/trx/cpc_fdc.v > "$fd/build.log" 2>&1 || { tail -30 "$fd/build.log"; echo "FAIL build"; exit 1; }
    fi
    "$fd/Vtb_fdcscan" +DDR="$here/build/blank40.ddr.hex" $fa > "$here/build/$m.log" 2>&1 || true
    r=$(grep -E "^(PASS|FAIL|  stuck|  HANG|  MON|  READ DATA|  scan|  second|  both|  hold|  rdata)" "$here/build/$m.log" || true)
  elif [ "$m" = fdcfix ]; then
    fd=$base/fdcfix
    if [ ! -x "$fd/Vtb_fdcfix" ] || [ -n "$ZX_REBUILD" ]; then
      mkdir -p "$fd"
      nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_fdcfix -I"$here" -Mdir "$fd" \
        --x-assign 0 --x-initial 0 "$here/tb_fdcfix.v" "$here/ddr2c_model.v" $c/trx/cpc_fdc.v > "$fd/build.log" 2>&1 || { tail -30 "$fd/build.log"; echo "FAIL build"; exit 1; }
    fi
    "$fd/Vtb_fdcfix" > "$here/build/$m.log" 2>&1 || true
    r=$(grep -E "^(PASS|FAIL|  4)" "$here/build/$m.log" || true)
  else
  # shellcheck disable=SC2086
  build "$base"
  # shellcheck disable=SC2086
  sim "$base" "$m" "$tm" $args
  case $m in
    slots) r=$(python3 "$root/tools/cpc_slots.py" check < "$here/build/$m.log") || true ;;
    *) r=$(verdict "$m") ;;
  esac
  fi
  echo "$r"
  echo "$r" | grep -q "^PASS" && ! echo "$r" | grep -q "^FAIL" || rc=1
done
exit $rc
