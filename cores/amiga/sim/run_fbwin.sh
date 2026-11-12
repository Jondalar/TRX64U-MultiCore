#!/bin/sh
# T97 5.10: the A1200's crop window is a set of registers (amiga_regs 0D0..), the display window can be read back (0C0..). The test ROM is the
# stripes picture (DIWSTRT 2C81 / DIWSTOP 2CC1). The application's side is played by tb_l2 (WINCHK): the buffers are cleared, the window moved.
#   1. window 1: skip 56 / 8, 60 words x 100 lines, at the top left of the buffer - the stripes are where the registers put them and nothing outside
#      the window was written; DIW_H / DIW_V read back the copper list's window;
#   2. window 2: the window reaches past the end of the source line - black padding, whole words;
#   3. T136 S1: window 2 also checks the border colour with 8 bits per channel.
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$here/build"
run() { # name, extra env...
  n=$1; shift
  env TG68K=1 NOPIC=1 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 FRAMES=9 OUT_SUFFIX=$n "$@" sh "$here/run_l2.sh" > "$here/build/$n.log" 2>&1
  grep -E "^winchk|^FAIL|^PASS" "$here/build/$n.log" | sed "s/^/$n| /"
}
run fbwin1 WINCHK=1
grep -q 'PASS tb_l2 errors=0' "$here/build/fbwin1.log" || { echo "FAIL amiga_fbwin: window 1"; exit 1; }
run fbwin2 WINCHK=2
grep -q 'PASS tb_l2 errors=0' "$here/build/fbwin2.log" || { echo "FAIL amiga_fbwin: window 2"; exit 1; }
echo "PASS amiga_fbwin (the crop window follows its registers, nothing outside it is written, the display window reads back, 8 bits per channel reach the buffer)"
