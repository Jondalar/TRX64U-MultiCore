#!/bin/sh
# T89 S5: the picture (tb_a2_video.vhd + check_video.py). PROGS = the programs to run (default: text text80 hgr lores dhgr flip), "-" for none.
# EXTRA = "scan" (the scanline option); default "scan" when PROGS is not set. ci/sims.txt runs the parts as separate lines.
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
[ -n "${PROGS+x}" ] || EXTRA=${EXTRA-scan}
EXTRA=${EXTRA-}
# one work directory per part: ci/sims.txt runs the parts as parallel benches, and they must not share one GHDL library
out=$(a2_workdir "video_$(printf %s "${PROGS-all}_${EXTRA}" | tr -c 'A-Za-z0-9' _)")
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
a2_build tb_a2_video "$out"
for p in ${PROGS:-text text80 hgr lores dhgr flip}; do
  [ "$p" = - ] && continue
  if [ "$p" = flip ]; then
    python3 "$here/mkrom.py" "$here/asm/flip.s" "$out/flip.hex" > /dev/null
    ( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_video -gDIR="$out" -gPROG=flip.hex -gANIM=1 --ieee-asserts=disable 2>&1 | tee flip.log | sed -n 's/^.*(report note): /  /p' )
    grep -q "PASS tb_a2_video frames are whole" "$out/flip.log"
    continue
  fi
  python3 "$here/mkrom.py" "$here/asm/$p.s" "$out/$p.hex" > /dev/null
  nf=${FRAMES:-6}; case "$p" in text80|dhgr) nf=${FRAMES:-16} ;; esac   # these fill two pages with a plain loop (0.1 to 0.2 s) before they switch the mode on
  ( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_video -gDIR="$out" -gPROG="$p.hex" -gFRAMES=$nf --ieee-asserts=disable 2>&1 | sed -n 's/^.*(report note): /  /p' )
  mv "$out/frame.txt" "$out/frame_$p.txt"; mv "$out/frame.ppm" "$out/frame_$p.ppm"
  python3 "$here/check_video.py" "$p" "$out/frame_$p.txt"
done
# the scanline option: the second row of every pair at half brightness
case " $EXTRA " in *" scan "*)
  python3 "$here/mkrom.py" "$here/asm/text.s" "$out/text.hex" > /dev/null
  ( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_video -gDIR="$out" -gPROG=text.hex -gFRAMES=6 -gSCAN=1 --ieee-asserts=disable 2>&1 | sed -n 's/^.*(report note): /  /p' )
  mv "$out/frame.txt" "$out/frame_textscan.txt"; mv "$out/frame.ppm" "$out/frame_textscan.ppm"
  python3 "$here/check_video.py" text "$out/frame_textscan.txt" --scan ;;
esac
echo "PASS video (PROGS=${PROGS:-all} EXTRA='$EXTRA': the benches pass)"
