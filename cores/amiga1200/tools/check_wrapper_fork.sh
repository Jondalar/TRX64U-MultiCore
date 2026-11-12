#!/bin/sh
# T97: the A1200's own copies of shared sources are the shared file with recorded changes, kept in the .diff beside them:
#   rtl/cpu_wrapper.v (step 2: the kernel's clock enable is taken on ce28 only), rtl/amiga_core.v (5.9: the chipset does not get the
#   slow RAM the fast cards took), rtl/amiga_fb.v and rtl/amiga_regs.v (5.10: the crop window and the display window are registers), rtl/minimig.v and rtl/nanomig.v (5.10: the display window out of the chipset), rtl/paula_floppy.v and rtl/amiga_sdc.v (5.11: block RAM for the floppy
#   sector buffer and the sd queues), rtl/cpu_wrapper.v / nanomig.v / amiga_core.v / amiga_mem.v again (T138: the Zorro III board of 32 MB, its address
#   out of the CPU wrapper to the DDR2 at byte 0x6000000), rtl/amiga_regs.v / amiga_core.v keep only the A1200's own changes (T141: the raw key queue of the USB keyboard moved into the shared files; amiga_input.v is the A1200's own copy of trx/amiga_input.v, equal apart from comments). Fails when either side moved: a change in a shared file must be looked at here before the A1200
#   goes on building on a stale copy.
here=$(cd "$(dirname "$0")/.." && pwd)
rc=0
for pair in "src/minimig-aga/cpu_wrapper.v cpu_wrapper" "trx/amiga_core.v amiga_core" "trx/amiga_fb.v amiga_fb" "trx/amiga_regs.v amiga_regs" "src/minimig-aga/minimig.v minimig" "src/nanomig.v nanomig" "src/minimig-aga/paula_floppy.v paula_floppy" "trx/amiga_sdc.v amiga_sdc" "trx/amiga_mem.v amiga_mem" "trx/amiga_input.v amiga_input"; do
  set -- $pair
  shared=$here/../amiga/rtl/$1
  now=/tmp/$2.now.$$
  # apply the recorded diff and compare the result: GNU and BSD diff place the hunks of the same change differently, and
  # the sim image has no patch
  if python3 "$here/tools/apply_normal_diff.py" "$shared" "$here/rtl/$2.diff" > "$now" && cmp -s "$now" "$here/rtl/$2.v"; then
    echo "ok: $2 fork = shared file + $2.diff"
  else echo "FAIL: $2 fork differs from the recorded diff:"; diff "$shared" "$here/rtl/$2.v" | diff - "$here/rtl/$2.diff" | head -20; rc=1; fi
  rm -f "$now"
done
exit $rc
