#!/bin/sh
# T140 5.19 (issue #67): the Z80 writing 64 bytes each into the VDC RAM three ways (tools/gen_c128_test.py z80vdc) with the VIC's display on; PASS when the VDC RAM holds the
# pattern ($40 + i at $4000 / $4200 / $4300; $4100 = writes without any polling is reported only) and the probe saw as many VDC strobes with a write as the Z80 made writes to $D6xx. No real ROM, runs in CI-style.
#   sh cores/c128/sim/run_z80vdc.sh
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
[ -x "$build/obj/tb_c128" ] || sh "$here/build_machine.sh" "$build" || exit 1
run="$build/run_z80vdc"; rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/gen_c128_test.py" z80vdc "$run" > /dev/null || exit 1
(cd "$run" && "$build/obj/tb_c128" +MS="${C128_MS:-60}" +VDCPROBE +VRAM ${C128_ARGS:-}) > "$run/log" 2>&1
cw=$(grep -c '^vp|.*|cw|' "$run/log"); zw=$(grep -c '^vp|.*|zw|' "$run/log"); dr=$(grep -c 'DROP-PENDING' "$run/log")
echo "VDC strobes with a write: $cw, Z80 writes to \$D6xx: $zw, pending VDC actions replaced: $dr"
rc=0
for b in 4000 4100 4200 4300; do
  want=$(python3 -c "print(''.join('%02x' % (0x40 + i) for i in range(64)))")
  got=$(grep "^vram|$b|" "$run/log" | cut -d'|' -f3)
  if [ "$got" = "$want" ]; then echo "ok $b"; elif [ $b = 4100 ]; then echo "info $b (writes back to back, no polling: the VDC takes one at a time): $got"; else echo "FAIL $b: $got"; rc=1; fi
done
[ "$cw" = "$zw" ] || { echo "FAIL strobes $cw != Z80 writes $zw"; rc=1; }
[ $rc = 0 ] && echo "PASS run_z80vdc" || echo "FAIL run_z80vdc"
exit $rc
