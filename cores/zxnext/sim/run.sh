#!/bin/sh
# T116: the ZX Spectrum Next core's benches under Verilator (zxn_core with cores/zxnext/gen, trx and the Spectrum core's key table), programs of
# our own (cores/zxnext/sim/prog/*.s, assembled by tools/zxnext_asm.py, started by the Next's own boot ROM from an SD image tools/zxnext_progs.py
# makes), no ROM, no NextZXOS. A mode ends with one PASS or FAIL line.
#   sh cores/zxnext/sim/run.sh [mode ...]        default: boot sd noimg; ZXN_REBUILD=1 rebuilds the binaries
#   modes: boot noimg sd mem0 mem1 mem2 mem3 (S3: the same program on the one-cycle SRAM of the original (mem_ref<n>) and behind the DDR2 caches: the final
#          memory and the port writes must be equal, and at 7, 14 and 28 MHz the master cycles between the writes too; at 3.5 MHz the ULA's contention
#          makes a run's time depend on where in the frame the program started, which the boot's length decides, so only the content is compared there)
#          layer2 video keys clk freeze audio boardid (T116 D10: NextReg 0x0F reports the id the app wrote to BOARD_ID: none, 0, 2)
#          sdfast sdmulti (S5, board B: reads at 28 MHz with no gap between the SPI accesses; CMD18 stopped by CMD12 and followed by CMD17 with a slow application,
#          +LAT us per request, +STEP ns between the bench's service calls)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
z=$root/cores/zxnext
keysrc="$root/cores/zx/trx/zx_keys.v $root/cores/zx/shared/zxcpc_keyeng.v"
src="$z/trx/*.v $z/gen/zxnext.v $keysrc"
# the S3 reference: the same machine with the Next's own one-cycle SRAM instead of zxn_mem (sim/zxn_mem_sram.v)
src_sram="$(ls $z/trx/*.v | grep -v zxn_mem.v | tr '\n' ' ') $z/sim/zxn_mem_sram.v $z/gen/zxnext.v $keysrc"
build() { # build <dir> [verilator options]; SRAM=1: the reference build
  d=$1; shift
  mkdir -p "$d"
  # CI starts several benches at once (ci/sims.sh -j) on one Mdir: they must not build side by side (the same objects written by all, and one
  # cc1plus of this model per bench and per core ran out of memory in run 126). One build at a time on the machine; whoever gets the lock
  # re-checks whether the binary is there by now.
  lock=${TRX64_SIM_BUILD_LOCK:-${TMPDIR:-/tmp}/trx64-verilator-build.lock}
  until mkdir "$lock" 2>/dev/null; do
    op=$(cat "$lock/pid" 2>/dev/null || true)
    if [ -n "$op" ] && ! kill -0 "$op" 2>/dev/null; then rm -rf "$lock"; fi # holder died
    sleep 2
  done
  echo $$ > "$lock/pid"
  trap 'rm -rf "$lock"' EXIT INT TERM
  if [ ! -x "$d/Vtb_zxn" ] || [ -n "$ZXN_REBUILD" ]; then
    s=$src
    [ -z "$SRAM" ] || s=$src_sram
    # shellcheck disable=SC2086
    nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_zxn \
      -Mdir "$d" --x-assign 0 --x-initial 0 "$@" "$here/tb_zxn.v" "$here/ddr2_model.v" $s > "$d/build.log" 2>&1 || { tail -30 "$d/build.log"; echo "FAIL build"; exit 1; }
  fi
  rm -rf "$lock"; trap - EXIT INT TERM
}
base=${ZXN_OBJ:-$here/build/obj}
sram=${ZXN_OBJ_SRAM:-$here/build/obj_sram}
mkdir -p "$here/build"
img() { # img <program> <image name> [tools/zxnext_progs.py options]: the image for a run
  p=$1; n=$2; shift 2
  python3 "$root/tools/zxnext_progs.py" build "$p" "$here/build/$n.img" "$@" > /dev/null
  echo "$here/build/$n.img"
}
sim() { # sim <binary dir> <log name> <args...>: runs, keeps the log
  bd=$1; ln=$2; shift 2
  "$bd/Vtb_zxn" "$@" > "$here/build/$ln.log" 2>&1 || true
}
verdict() { grep -E "^(PASS|FAIL|  )" "$here/build/$1.log" | grep -v "^- " || true; }
trace() {
  grep -E "^(P |RAMHASH)" "$here/build/$1.log" > "$here/build/$1.trace" || true
  grep -E "^(P |RAMHASH)" "$here/build/$1.log" | sed 's/^P [0-9-]* /P /' > "$here/build/$1.notime" || true     # without the master cycles: across speeds
}
modes=${*:-"boot sd noimg"}
rc=0
for m in $modes; do
  bd=$base
  vf=""
  ref=""
  xref=""
  useram=""
  case $m in
    boot)        tm=boot;  args="+MODE=boot +IMG=$(img boot boot) +MS=1500" ;;
    noimg)       tm=noimg; args="+MODE=noimg +IMG=$(img boot boot) +NOIMG +MS=6200" ;;
    sd)          tm=sd;    args="+MODE=sd +IMG=$(img sd sd --fill 8192) +MS=2500" ;;
    sdfast)      tm=sdfast; args="+MODE=sdfast +IMG=$(img sdfast sdfast --fill 8192 --fill 8193 --fill 8194 --fill 8195 --fill 8196 --fill 8197) +MS=2500" ;;
    sdmulti)     tm=sdmulti; args="+MODE=sdmulti +LAT=1500 +IMG=$(img sdmulti sdmulti --fill 8192 --fill 8193 --fill 8194 --fill 8195 --fill 8200 --fill 8201) +MS=2500" ;;
    fwdrv)       tm=fwdrv; args="+MODE=fwdrv +LAT=${FWDRV_LAT:-3000} +IMG=$(img fwdrv fwdrv --define POLLS=5000 $(for l in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do echo --fill $((8192 + l)); done)) +MS=2500" ;;
    fwslow)      # the same driver with a token wait of 300 polls (4.4 ms at 28 MHz) against an application that needs 12 ms a block: only a card that stops the machine's time while it waits gets through
                 tm=fwdrv; args="+MODE=fwdrv +LAT=12000 +IMG=$(img fwdrv fwslow --define POLLS=300 $(for l in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do echo --fill $((8192 + l)); done)) +MS=2500" ;;
    fw)
                 # the Next's own firmware on its real card layout; needs ZXN_FW_MAP=<prefix> of tools/zxnext_fwmap.py's <prefix>.keys / <prefix>.data
                 # (sectors of the user's image: not in the repository, not in CI). The application takes FW_LAT us a block, and FW_FARLAT us for a block at
                 # LBA FW_FARLBA and above once the firmware runs (1000 ms): the card's FAT32 chain walk for TBBLUE.FW's second run of clusters (LBA 164,048..).
                 [ -n "$ZXN_FW_MAP" ] || { echo "SKIP $m: ZXN_FW_MAP (the card's sectors, tools/zxnext_fwmap.py) is not set"; continue; }
                 tm=fw; bd=$here/build/fw; vf=""
                 args="+MODE=fw +MAPK=$ZXN_FW_MAP.keys +MAPD=$ZXN_FW_MAP.data +SECTORS=4194304 +PCOK=${FW_PCOK:-B68E} +PCERR=${FW_PCERR:-B686} +CMDLOG +LAT=${FW_LAT:-3000} +FARLBA=${FW_FARLBA:-164000} +FARAFTER=1000 +FARLAT=${FW_FARLAT:-100000} +MS=${FW_MS:-6000}" ;;
    mem_ref0|mem_ref1|mem_ref2|mem_ref3)
                 tm=mem;   useram=1; bd=$sram; n=${m#mem_ref}; [ "$n" = 0 ] || xref=mem_ref0; args="+MODE=mem +PLOG +IMG=$(img mem mem_s$n --define SPEED=$n) +MS=2500" ;;
    mem0|mem1|mem2|mem3)
                 tm=mem;   n=${m#mem}; ref=mem_ref$n; [ "$n" != 0 ] || { ref=""; xref=mem_ref0; }; args="+MODE=mem +PLOG +IMG=$(img mem mem_s$n --define SPEED=$n) +MS=2500" ;;
    video0|video1|video2)
                 n=${m#video}; tm=video; case $n in 0) cx=255; cy=191; np=6 ;; 1) cx=159; cy=255; np=10 ;; *) cx=159; cy=255; np=10 ;; esac
                 args="+MODE=video +VMODE=$n +IMG=$(img video vid$n --define MODE=$n --define NPAGES=$np --define CLIPX2=$cx --define CLIPY2=$cy) +MS=1500" ;;
    keys)        tm=keys;  args="+MODE=keys +IMG=$(img keys keys) +MS=1500" ;;
    clk1|clk2|clk3|clk4)
                 n=${m#clk}; tm=clk; args="+MODE=clk +TIMING=$n +IMG=$(img clk clk$n --define TIMING=$n) +MS=1500" ;;
    boardid)     tm=boardid; args="+MODE=boardid +WANT=3 +IMG=$(img boardid boardid) +MS=1500" ;;                     # no write: the reset value, issue 5
    boardid0)    tm=boardid; args="+MODE=boardid +BOARDID=0 +WANT=0 +IMG=$(img boardid boardid) +MS=1500" ;;      # issue 2
    boardid2)    tm=boardid; args="+MODE=boardid +BOARDID=2 +WANT=2 +IMG=$(img boardid boardid) +MS=1500" ;;      # issue 4
    freeze_ref)  tm=freeze; args="+MODE=freeze +NOFREEZE +PLOG +IMG=$(img freeze freeze) +MS=1500" ;;
    freeze)      tm=freeze; ref=freeze_ref; args="+MODE=freeze +PLOG +IMG=$(img freeze freeze) +MS=1500" ;;
    audio)       tm=audio; args="+MODE=audio +IMG=$(img audio audio) +MS=2500" ;;
    *) echo "unknown mode $m"; exit 1 ;;
  esac
  for rr in $ref $xref; do
    if [ ! -f "$here/build/$rr.trace" ]; then
      echo "(the reference run $rr first)"
      sh "$0" "$rr" > /dev/null || true
    fi
  done
  # shellcheck disable=SC2086
  SRAM=$useram build "$bd" $vf
  # shellcheck disable=SC2086
  sim "$bd" "$m" $args $ZXN_PLUS
  r=$(verdict "$m")
  trace "$m"
  if [ -n "$ref" ]; then
    if cmp -s "$here/build/$m.trace" "$here/build/$ref.trace" && [ -s "$here/build/$m.trace" ]; then
      r="$r
PASS $m equals $ref: the final memory image and the $(grep -c '^P ' "$here/build/$m.trace") port writes, with the master cycles between them, are the same"
    else
      r="$r
FAIL $m differs from $ref: $(diff "$here/build/$m.trace" "$here/build/$ref.trace" | grep -c '^[<>]') lines"
    fi
  fi
  if [ -n "$xref" ]; then
    if cmp -s "$here/build/$m.notime" "$here/build/$xref.notime" && [ -s "$here/build/$m.notime" ]; then
      r="$r
PASS $m equals $xref in what it did (the port writes and the memory image; only the master cycles between them differ with the speed)"
    else
      r="$r
FAIL $m differs from $xref"
    fi
  fi
  echo "$r"
  echo "$r" | grep -q "^PASS" && ! echo "$r" | grep -q "^FAIL" || rc=1
done
exit $rc
