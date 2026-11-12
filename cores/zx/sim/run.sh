#!/bin/sh
# T91: the ZX Spectrum core's benches under Verilator (zx_core with cores/zx/rtl, gen, shared and trx), programs of our own
# (tools/zx_progs.py, tools/zx_snap.py), no ROM. A mode ends with one PASS or FAIL line.
#   sh cores/zx/sim/run.sh [mode ...]        default: all modes (about 25 minutes on a Mac); ZX_REBUILD=1 rebuilds the binaries
#   modes: z80s z80 (the long one, 430 frames) page48 page128 model clk48 clk128 cont48 cont128 video48 shadow128 audio128 keys48 tape48
#          snap48_sna snap48_z80 snapint snap128_sna snap128_z80 pause pixw pixw128 pixwhost
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
z=$root/cores/zx
src="$root/cores/zx/mono/host/vic_scaler.v $root/cores/zx/mono/host/pal_ram.v $root/cores/zx/mono/host/ddr2_port.v $root/host/sim/mig_ui_model.v $z/rtl/ula.sv $z/rtl/ym2149.sv $z/gen/t80pa.v $z/shared/*.v $z/trx/*.v"
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
  if [ ! -x "$d/Vtb_zx" ] || [ -n "$ZX_REBUILD" ]; then
    # shellcheck disable=SC2086
    nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_zx \
      -Mdir "$d" --x-assign 0 --x-initial 0 "$@" "$here/tb_zx.v" "$here/ddr2c_model.v" $src > "$d/build.log" 2>&1 || { tail -30 "$d/build.log"; echo "FAIL build"; exit 1; }
  fi
  rm -rf "$lock"; trap - EXIT INT TERM
}
base=$here/build/obj
snaps=$here/build/snaps
sim() { # sim <binary dir> <log name> <tb mode> <args...>: runs, prints the verdict line(s) of the bench
  bd=$1; ln=$2; tm=$3; shift 3
  "$bd/Vtb_zx" +MODE="$tm" "$@" $ZX_PLUSARGS > "$here/build/$ln.log" 2>&1 || true
}
verdict() { grep -E "^(PASS|FAIL|  mismatch|  known|  window|  keys|  snap128|video48|shadow128|audio128|clk|scaler|tape48|pixw|sw)" "$here/build/$1.log" || true; }
snapargs() { # snapargs <snapshot file> <prefix>
  mkdir -p "$snaps"
  [ -f "$snaps/snap48.sna" ] || python3 "$root/tools/zx_snap.py" make "$snaps"
  python3 "$root/tools/zx_snap.py" bench "$snaps/$1" "$snaps/$2"
  nb=0; bl=""
  for b in $(sed -n 's/^BANKS=//p' "$snaps/$2.meta" | tr ',' ' '); do eval "B$nb=$b"; nb=$((nb + 1)); done
  echo "+RAM=$snaps/$2.ram.hex +REGS=$snaps/$2.regs.hex +NB=$nb +B0=$B0 +B1=${B1:-0} +B2=${B2:-0}"
}
modes=${*:-"z80s page48 page128 model clk48 clk128 cont48 cont128 video48 scaler scaler128 shadow128 audio128 keys48 tape48 snap48_sna snap48_z80 snapint snap128_sna snap128_z80 pause pixw pixw128 pixwhost sw48 sw128 swapp48 coldapp48"}
mkdir -p "$here/build"
rc=0
for m in $modes; do
  bd=$base
  vf=""
  tm=$m
  case $m in
    z80)       args="+ROM=$here/prog/z80t.hex +EXP=$here/prog/z80t.exp.hex +FRAMES=900" ;;
    z80s)      args="+ROM=$here/prog/z80s.hex +EXP=$here/prog/z80s.exp.hex +FRAMES=150" ;;
    page48)    args="+ROM=$here/prog/page48.hex +EXP=$here/prog/page48.exp.hex +FRAMES=20" ;;
    page128)   args="+ROM=$here/prog/page128.hex +EXP=$here/prog/page128.exp.hex +FRAMES=20" ;;
    model)     args="+ROM=$here/prog/page48.hex +EXP=$here/prog/page48.exp.hex +ROM2=$here/prog/page128.hex +EXP2=$here/prog/page128.exp.hex +FRAMES=20" ;;
    clk48)     args="+ROM=$here/prog/cont48.hex" ;;
    clk128)    args="+ROM=$here/prog/cont128.hex" ;;
    cont48|cont128) args="+ROM=$here/prog/$m.hex" ;;
    scaler)    args="+ROM=$here/prog/video48.hex" ;;
    scaler128) args="+ROM=$here/prog/shadow128.hex" ;;
    video48)   args="+ROM=$here/prog/video48.hex" ;;
    pixw)      args="+ROM=$here/prog/video48.hex" ;;
    pixw128)   args="+ROM=$here/prog/shadow128.hex" ;;
    pixwhost)  args="+ROM=$here/prog/video48.hex"; bd=$here/build/hostport; vf="-DZX_HOSTPORT" ;;
    sw48)      args="+ROM=$here/prog/shadow128.hex +ROM2=${ZX_ROM2:-$here/prog/video48.hex}" ;;
    sw128)     args="+ROM=$here/prog/video48.hex +ROM2=$here/prog/shadow128.hex" ;;
    swapp48)   args="+ROM=$here/prog/shadow128.hex +ROM2=$here/prog/video48.hex" ;;
    coldapp48) args="+ROM2=$here/prog/video48.hex" ;;
    shadow128) args="+ROM=$here/prog/shadow128.hex" ;;
    audio128)  args="+ROM=$here/prog/audio128.hex $(sed 's/^/+/' "$here/prog/audio128.meta" | tr '\n' ' ')" ;;
    keys48)    python3 "$root/tools/zxcpc_keys.py" zx-vectors > "$here/build/keys.vec"; args="+ROM=$here/prog/keys48.hex +VEC=$here/build/keys.vec" ;;
    tape48)    args="+ROM=$here/prog/tape48.hex +EXP=$here/prog/tape48.exp.hex +DDR=$here/prog/tape48.ddr.hex +FRAMES=90" ;;
    snap48_sna) tm=snap48; args="+ROM=$here/prog/page48.hex +EXP=$here/prog/snap48.exp.hex +FRAMES=20 $(snapargs snap48.sna snap48_sna)" ;;
    snap48_z80) tm=snap48; args="+ROM=$here/prog/page48.hex +EXP=$here/prog/snap48.exp.hex +FRAMES=20 $(snapargs snap48.z80 snap48_z80)" ;;
    snapint)   args="+ROM=$here/prog/page48.hex +FRAMES=20 $(snapargs snapint.z80 snapint_z80)" ;;
    snap128_sna) tm=snap128; args="+ROM=$here/prog/page48.hex +FRAMES=20 $(snapargs snap128.sna snap128_sna)" ;;
    snap128_z80) tm=snap128; args="+ROM=$here/prog/page48.hex +FRAMES=20 $(snapargs snap128.z80 snap128_z80)" ;;
    pause) ;;
    *) echo "unknown mode $m"; exit 1 ;;
  esac
  if [ "$m" = pause ]; then
    build "$base"
    [ -s "$here/build/pause_ref.f" ] || { sim "$base" pause_ref pause_ref +ROM="$here/prog/pause48.hex" +DDR="$here/prog/tape48.ddr.hex"; grep '^F ' "$here/build/pause_ref.log" | head -24 > "$here/build/pause_ref.f"; }
    sim "$base" "$m" pause_run +ROM="$here/prog/pause48.hex" +DDR="$here/prog/tape48.ddr.hex"
    grep '^F ' "$here/build/$m.log" | head -24 > "$here/build/$m.f" || true
    # every machine frame the paused run printed must be the reference's, line for line
    if diff "$here/build/$m.f" "$here/build/pause_ref.f" | grep -q '^<'; then same=0; else same=1; fi
    nlines=$(wc -l < "$here/build/$m.f" | tr -d ' ')
    r=$(grep -E "^(PASS|FAIL|pause_run)" "$here/build/$m.log" || true)
    if [ "$same" = 1 ] && [ "$nlines" -ge 20 ]; then
      r="$r
PASS pause hashes: all $nlines machine frames of the paused run equal the reference run's (picture, screen RAM, Z80 registers, tape position)"
    else
      r="$r
FAIL pause hashes: the paused run differs from the reference in $(diff "$here/build/$m.f" "$here/build/pause_ref.f" | grep -c '^<') of $nlines frames"
    fi
  else
    # shellcheck disable=SC2086
    build "$bd" $vf
    # shellcheck disable=SC2086
    sim "$bd" "$m" "$tm" $args
    case $m in
      cont48|cont128) r=$(python3 "$root/tools/zx_contend.py" check "${m#cont}" < "$here/build/$m.log") || true ;;
      *) r=$(verdict "$m") ;;
    esac
  fi
  echo "$r"
  echo "$r" | grep -q "^PASS" && ! echo "$r" | grep -q "^FAIL" || rc=1
done
exit $rc
