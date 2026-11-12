#!/bin/sh
# T102 S2: after the application's configuration commit the Amiga 1200 core is still AGA with 2 MB chip RAM.
# The core is built the way cores/amiga1200/core_sources.tcl builds it (CHS_INIT=16 MEM_INIT=3 HDF_SECT=1) and the bench writes
# the register bytes the application writes (tests/cfgdump of the application: amiga_cfg.h, the code the image is built from).
#   run_cfg.sh [<dir of the 1541ultimate worktree>]
# The bytes are the contract between the application and this gateware and are kept in cores/amiga1200/app_regs.txt. With the
# application's worktree present (Alex's Mac) they are regenerated from the code the image is built from and must equal the file;
# without it (CI) the file is used.
# One run: the A1200 application's defaults must pass.
set -e
here=$(cd "$(dirname "$0")" && pwd)
app=${1:-/path/to/Development/1541ultimate-wt-amigaapp}
d=${CFGDUMP_OUT:-$here/build/cfgdump}
contract=$here/../../amiga1200/app_regs.txt
mkdir -p "$d"
if [ -x "$app/software/io/amiga/tests/run_cfgdump.sh" ]; then
    OUT=$d "$app/software/io/amiga/tests/run_cfgdump.sh" | grep -E '^(==|MEM=)' > "$d.txt"
    if ! diff -u "$contract" "$d.txt"; then echo "FAIL: the application writes other bytes than cores/amiga1200/app_regs.txt"; exit 1; fi
else
    cp "$contract" "$d.txt"
fi
line() {   # the register line of the build named $1
    awk -v b="$1" '/^==/ {on = index($0, b)} on && /^MEM=/ {print; exit}' "$d.txt"
}
args() {   # MEM=03 FAST=00 ... -> +CFG_MEM=03 +CFG_FAST=00 ...
    echo "$1" | sed -e 's/\([A-Z]*\)=\([0-9a-f]*\)/+CFG_\1=\2/g'
}
a1200=$(line "A1200 build")
echo "A1200 application writes : $a1200"
export CFGCHK=1 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 OUT_SUFFIX=cfg
run() { RUNARGS="$(args "$1")" "$here/run_l2.sh"; }
echo "== 1: the A1200 application's defaults"
run "$a1200" | tee "$here/build/cfg_pass.log" | grep -E "CFGCHK|ok  :|FAIL|PASS"
grep -q 'PASS tb_l2 cfgchk errors=0' "$here/build/cfg_pass.log"
echo "PASS run_cfg (A1200 defaults keep AGA and 2 MB)"
