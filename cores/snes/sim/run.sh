#!/bin/sh
# T142: the SNES core's benches under Verilator (snes_core with cores/snes/gen, trx, the DDR2 models of sim/ddr2_model.v), programs of our own
# (cores/snes/sim/prog/*.s, assembled by tools/snes_asm.py), the S-SMP's boot ROM a stub of our own (tools/snes_asm.py spcstub), no ROM, no game. A mode ends with
# SNES PASS or SNES FAIL lines. The two builds: `real` (snes_core behind snes_mem.v and the DDR2 model) and `ref` (-DSNES_REF: the same core with the memory of
# sim/snes_mem_ref.v that is never late); a mode that runs both compares what they leave (WRAM, video RAM, audio RAM, the picture): equal = the caches and the
# stall are transparent (S2).
#   sh cores/snes/sim/run.sh [mode ...]        default: all modes; SNES_REBUILD=1 rebuilds the binaries
#   modes: boot  mvn  mem  video (S5: known pixels in the frame buffer)  pad (S8: the buttons in $4218..$421B)  padapp (T156: the application's pad registers)  padmod (T156: tb_pad.v, all twelve buttons)  audio (a voice reaches audio_l/r: stub and the application's real boot ROM; the register block's peaks PEAK_DSP / PEAK_OUT / HANDS must agree with the port)  audio_q (1 kHz resampling quality)
#   dsp (T218: the DSP-n coprocessor runs a firmware written through the register block: data ROM words 5, 1023, 0 and the program's words 2046/2047 through the 3 byte / 2 byte split)  brr (the S-DSP BRR decoder, filters 0..3, against tools/snes_brr.py)  dsplog (measurement: ENV of a voice per S-DSP sample)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
s=$root/cores/snes
src="$s/trx/*.v $s/gen/snes.v"
src_ref="$(ls $s/trx/*.v | grep -v 'snes_mem.v$' | grep -v 'snes_cch.v$' | tr '\n' ' ') $here/snes_mem_ref.v $s/gen/snes.v"
mkdir -p "$here/build"
build() { # build <dir> <real|ref|name> [verilator options]
  d=$1; kind=$2; shift 2
  mkdir -p "$d"
  lock=${TRX64_SIM_BUILD_LOCK:-${TMPDIR:-/tmp}/trx64-verilator-build.lock}
  until mkdir "$lock" 2>/dev/null; do
    op=$(cat "$lock/pid" 2>/dev/null || true)
    if [ -n "$op" ] && ! kill -0 "$op" 2>/dev/null; then rm -rf "$lock"; fi
    sleep 2
  done
  echo $$ > "$lock/pid"
  trap 'rm -rf "$lock"' EXIT INT TERM
  if [ ! -x "$d/Vtb_snes" ] || [ -n "$SNES_REBUILD" ]; then
    x=$src
    [ "$kind" != ref ] || x="$src_ref -DSNES_REF=1 -DSNES_MEM_MOD=snes_mem_ref"
    # shellcheck disable=SC2086
    nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_snes \
      -Mdir "$d" --x-assign 0 --x-initial 0 "$@" "$here/tb_snes.v" "$here/ddr2_model.v" $x > "$d/build.log" 2>&1 || { tail -30 "$d/build.log"; echo "SNES FAIL build"; exit 1; }
  fi
  rm -rf "$lock"; trap - EXIT INT TERM
}
real=${SNES_OBJ:-$here/build/obj}
ref=${SNES_OBJ_REF:-$here/build/obj_ref}
prog() { # prog <name>: assemble cores/snes/sim/prog/<name>.s
  python3 "$root/tools/snes_asm.py" "$here/prog/$1.s" "$here/build/$1.sfc" "${2:-}"
  echo "$here/build/$1.sfc"
}
python3 "$root/tools/snes_asm.py" spcstub "$here/build/spc.bin"
sim() { # sim <binary dir> <log> <args...>
  bd=$1; ln=$2; shift 2
  "$bd/Vtb_snes" "$@" > "$here/build/$ln.log" 2>&1 || true
}
line() { grep "^SNES $2 " "$here/build/$1.log" | head -1 | cut -d' ' -f3-; }
# one program on both builds, the final machines must be equal
both() { # both <program> <+MS> <settle frames>
  p=$1; ms=$2; st=$3
  img=$(prog "$p")
  build "$real" real
  build "$ref" ref
  sim "$real" "${p}_real" +ROM="$img" +IPL="$here/build/spc.bin" +MS="$ms" +SETTLE="$st" $PADARG
  sim "$ref" "${p}_ref" +ROM="$img" +IPL="$here/build/spc.bin" +MS="$ms" +SETTLE="$st" $PADARG
  ok=1
  for k in wram vram aram; do
    a=$(line "${p}_real" $k); b=$(line "${p}_ref" $k)
    if [ -z "$a" ] || [ "$a" != "$b" ]; then ok=0; echo "SNES FAIL $p: $k differs (real '$a', ref '$b')"; fi
  done
  if [ "$p" = mem ]; then
    got=$(line mem_real chk); want=$(python3 "$root/tools/snes_expect.py" mem)
    last_got=${got##* }; want5=${want% *}; got5=${got% *}; alt=${want##* }
    case "$alt" in "$last_got|"*|*"|$last_got") okl=1;; *) okl=0;; esac
    if [ "$got5" != "$want5" ] || [ $okl = 0 ]; then ok=0; echo "SNES FAIL mem: the program's checksums are '$got', the oracle (tools/snes_expect.py) says '$want'"; else echo "SNES chk ok: $got"; fi
  fi
  case "$p" in
  video)
    for w in "10 10 f800" "400 10 001f" "10 400 f800" "500 440 001f" "255 100 f800" "256 100 001f"; do
      set -- $w
      got=$(grep "^SNES px $1 $2 " "$here/build/${p}_real.log" | cut -d' ' -f5)
      [ "$got" = "$3" ] || { ok=0; echo "SNES FAIL video: the pixel ($1,$2) is '$got', the picture has $3 there"; }
    done;;
  pad)
    got=$(grep "^SNES pad " "$here/build/${p}_real.log" | cut -d' ' -f3-)
    want_pad=${EXPECT_PAD:-80 88 80 02}
    [ "$got" = "$want_pad" ] || { ok=0; echo "SNES FAIL pad: \$4218..\$421B read '$got', the pressed buttons make '$want_pad'"; }
    # the line after the sixteenth bit, read by hand ($4016, $4017 bit 0): 1 on a pad that is plugged in (Donkey Kong Country clears its buttons on a 0, spec 5)
    got=$(grep "^SNES padm " "$here/build/${p}_real.log" | cut -d' ' -f3-)
    [ "$got" = "0101 0000" ] || { ok=0; echo "SNES FAIL pad: \$4016 / \$4017 bit 0 after the automatic read are '$got', a pad that is plugged in reads '0101 0000'"; };;
  esac
  if [ "$p" = mem ]; then   # S3: the machine is held for less than 3 % of the clocks on a program that walks banks, tables, stack and DMA
    clk=$(line mem_real stats | sed 's/clocks=\([0-9]*\).*/\1/'); stl=$(line mem_real stall_clocks)
    if [ -n "$clk" ] && [ -n "$stl" ] && [ $((stl * 100)) -ge $((clk * 3)) ]; then ok=0; echo "SNES FAIL mem: the machine was held $stl of $clk clocks (3 % is the limit)"; fi
  fi
  grep -E "^SNES (FAIL|stats|miss|stall|writes|frames|pic)" "$here/build/${p}_real.log" || true
  if ! grep -q "^SNES PASS" "$here/build/${p}_real.log"; then ok=0; echo "SNES FAIL $p: the real build did not finish"; tail -3 "$here/build/${p}_real.log"; fi
  if ! grep -q "^SNES PASS" "$here/build/${p}_ref.log"; then ok=0; echo "SNES FAIL $p: the reference build did not finish"; tail -3 "$here/build/${p}_ref.log"; fi
  [ $ok = 1 ] && echo "SNES PASS $p: real == ref (wram $(line ${p}_real wram), vram $(line ${p}_real vram), aram $(line ${p}_real aram))"
  return $((1 - ok))
}
mk_spc_real() { # the application's real 64 boot ROM bytes (coreapp/cores/snes.cc spc_ipl) as a file for +IPL
  python3 - "$root/coreapp/cores/snes.cc" "$here/build/spc_real.bin" <<'PY'
import re, sys
m = re.search(r'spc_ipl\[SNES_IPL_BYTES\] = \{(.*?)\};', open(sys.argv[1]).read(), re.S)
b = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', m.group(1)))
assert len(b) == 64
open(sys.argv[2], "wb").write(b)
PY
}
aud() { # aud <program> <ipl file> <expect: sound> [+AUDIO]: the samples of audio_l/r after the voice program ran (T142, board 2026-10-29)
  p=$1; ipl=$2; want=$3; shift 3
  img=$(prog "$p")
  build "$real" real
  sim "$real" "aud_${p}_$want" +ROM="$img" +IPL="$ipl" +MS=400 +SETTLE=6 "$@"
  nz=$(grep "^SNES audio" "$here/build/aud_${p}_$want.log" | sed 's/.*nonzero=\([0-9]*\).*/\1/')
  if [ -z "$nz" ]; then echo "SNES FAIL audio $p: the bench did not finish"; return 1; fi
  # the register block's peaks (T142 5): the S-DSP's pairs and what is handed to the host's audio path; they must say what the output port says
  pr=$(grep "^SNES peakregs" "$here/build/aud_${p}_$want.log" | head -1)
  pd=$(echo "$pr" | sed 's/.*dsp=\([0-9]*\).*/\1/'); po=$(echo "$pr" | sed 's/.*out=\([0-9]*\).*/\1/'); ph=$(echo "$pr" | sed 's/.*hands=\([0-9]*\).*/\1/')
  pk=$(grep "^SNES audio" "$here/build/aud_${p}_$want.log" | sed 's/.*peak=\([0-9]*\).*/\1/')   # the port's own peak (24 bit, x 128)
  if [ -z "$pd" ] || [ -z "$po" ] || [ -z "$ph" ]; then echo "SNES FAIL audio $p: no peakregs line"; return 1; fi
  if [ "$want" = sound ] && [ "$nz" -gt 100000 ] && [ "$pd" -gt 1000 ] && [ "$po" -gt 1000 ] && [ "$ph" -gt 100 ] \
     && [ "$po" -ge $((pd * 9 / 10)) ] && [ "$po" -le $((pd * 11 / 10)) ] \
     && [ "$pk" -ge $((po * 128 * 9 / 10)) ] && [ "$pk" -le $((po * 128 * 11 / 10)) ]; then
    echo "SNES PASS audio $p: $nz non-zero clocks, peak registers dsp=$pd out=$po hands=$ph"; return 0; fi
  echo "SNES FAIL audio $p ($want): $nz non-zero clocks, peak registers dsp=$pd out=$po hands=$ph"; return 1
}
audq() { # audq <name> <build dir> <expect: pass>: a 1 kHz sine through the real boot ROM-less stub, the frames against the ideal resampling (tools/snes_audio_q.py, 40 dB)
  n=$1; bd=$2; want=$3
  img=$(prog audio)
  python3 "$root/tools/snes_asm.py" spcaudio "$here/build/spc_q.bin" 0800
  sim "$bd" "audq_$n" +ROM="$img" +IPL="$here/build/spc_q.bin" +MS=300 +SETTLE=8 +AUDIO +SINE +ALOG
  if python3 "$root/tools/snes_audio_q.py" "$here/build/audq_$n.log" 40 > "$here/build/audq_$n.txt" 2>&1; then got=pass; else got=fail; fi
  cat "$here/build/audq_$n.txt"
  if [ "$got" = "$want" ]; then echo "SNES PASS audio_q $n ($want, as it must)"; return 0; fi
  echo "SNES FAIL audio_q $n: expected $want, got $got"; return 1
}
modes=${*:-boot mvn mem video pad padapp audio audio_q}
rc=0
for m in $modes; do
  case $m in
    boot) both boot 100 1 || rc=1;;
    dsp)  # T218: a DSP-1 cartridge (type $80, LoROM) with the firmware written through the register block: the chip's DR reads $1234 (low byte first); the real and ref builds equal
      python3 "$root/tools/snes_dsp_fw.py" "$here/build/dsp_fw.bin"
      PADARG="+TYPE=128 +FW=$here/build/dsp_fw.bin" both dsp 100 1 || rc=1
      got=$(line dsp_real dsp); want=$(python3 "$root/tools/snes_dsp_fw.py" --expect)
      [ "$got" = "$want" ] || { rc=1; echo "SNES FAIL dsp: DR read '$got', the firmware puts '$want' there (data words 5, 1023, 0 and the program's last words)"; }
      [ "$got" != "$want" ] || echo "SNES PASS dsp: the chip ran the firmware written by the register block (data ROM words 5, 1023, 0; program words 2046, 2047: $got)"
      ;;
    mvn)  both mvn 60 1 || rc=1;;
    video) both video 200 2 || rc=1;;
    pad)  PADARG=+PAD both pad 200 1 || rc=1;;
    padapp) PADARG=+APPPAD EXPECT_PAD="50 a1 a0 18" both pad 200 1 || rc=1;;       # T156 S2: the application's registers, through the S-CPU's $4218..$421B
    padmod) # T156: tb_pad.v (iverilog): the register decode and the serial protocol, all twelve buttons on both players
      mkdir -p "$here/build/padmod"
      pm() { # pm <name> [-D...]: build and run tb_pad.v, print its PASS/FAIL line
        n=$1; shift
        iverilog -g2012 "$@" -o "$here/build/padmod/$n.vvp" "$here/tb_pad.v" "$s/trx/snes_regs.v" "$s/trx/snes_pad.v" || return 2
        vvp -n "$here/build/padmod/$n.vvp" > "$here/build/padmod/$n.log" 2>&1 || true
      }
      pm real
      if grep -q "^PASS tb_snes_pad" "$here/build/padmod/real.log"; then echo "SNES PASS padmod: $(grep checks "$here/build/padmod/real.log" | tr -s ' ')"; else cat "$here/build/padmod/real.log"; echo "SNES FAIL padmod"; rc=1; fi;;
    mem)  both mem 400 2 || rc=1;;
    dsplog) # measurement: one voice with ADSR on a constant sample, one line per S-DSP sample
      python3 "$root/tools/snes_asm.py" spcadsr "$here/build/spc_adsr.bin"
      img=$(prog audio)
      build "$real" real
      sim "$real" dsplog +ROM="$img" +IPL="$here/build/spc_adsr.bin" +MS=${DSPMS:-400} +SETTLE=6 +AUDIO ${DSPARGS:-+DC +DSPLOG}
      grep -c "^SNES dsp" "$here/build/dsplog.log";;
    brr) # the S-DSP's BRR decoder (filters 0..3) against the reference decoder of tools/snes_brr.py
      python3 "$root/tools/snes_brr.py" gen "$here/build/brr.bin"
      python3 "$root/tools/snes_asm.py" spcadsr "$here/build/spc_adsr.bin"
      img=$(prog audio)
      bd=$real
      build "$bd" real
      sim "$bd" "brr_$m" +ROM="$img" +IPL="$here/build/spc_adsr.bin" +MS=100 +SETTLE=6 +AUDIO +BRR="$here/build/brr.bin"
      if python3 "$root/tools/snes_brr.py" check "$here/build/brr_$m.log" 400 > "$here/build/brr_$m.txt"; then got=pass; else got=fail; fi
      cat "$here/build/brr_$m.txt"
      if [ $got = pass ]; then echo "SNES PASS brr"; else echo "SNES FAIL $m: got $got"; rc=1; fi;;
    audio_q) build "$real" real; audq real "$real" pass || rc=1;;
    audio) # the S-SMP stub plays a voice / the real boot ROM of the application uploads the same voice
      python3 "$root/tools/snes_asm.py" spcaudio "$here/build/spc_audio.bin"
      mk_spc_real || rc=1
      aud audio "$here/build/spc_audio.bin" sound +AUDIO || rc=1
      aud audio_ipl "$here/build/spc_real.bin" sound || rc=1;;
    *) echo "unknown mode $m"; rc=1;;
  esac
done
exit $rc
