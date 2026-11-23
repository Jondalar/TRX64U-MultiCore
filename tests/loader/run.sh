#!/usr/bin/env bash
# Native tests of the loader for libraries and applications (T104.1 D10-D12, scenario S6), after the pattern of tests/coreapp/run.sh:
# coreapp/src/libload.cc is compiled on the Mac against a model of the heap and of the address space, no board, no RISC-V CPU. Negative
# controls (T174 D5.4, Alex 2026-11-08) exist only for the flash write refusal (ci/controls.allow.d/coreapp.allow): the product source is
# copied, one check is broken on purpose with sed, the same test is built against the broken copy and must FAIL.
#
#   tests/loader/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/coreapp/src"
TST="$ROOT/tests/loader"
T="$(mktemp -d)"
source "$ROOT/tests/parallel.sh"
source "$ROOT/tests/ctlcache.sh"   # T100 D13: controls cached by content
trap 'pjob_kill; cc_summary; rm -rf "$T"' EXIT
CXX="${CXX:-c++}"
FLAGS="-std=gnu++11 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -fno-exceptions"
PASS=0

# the real RISC-V image: the fixture ELF packed as the build path packs it, and the same objects as linked for the address they land at
python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at0.elf" -o "$T/testlib.u2a" --name testlib --version 1 --needs 'base>=3.0' > /dev/null
python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at1000.elf" --link-base 0x1000 --raw-image "$T/testlib.at1000.raw" > /dev/null

# T239.3 S13: the budget the packer writes (--budget) and the loader enforces: one file that fits (the packer adds `base>=3.2`), one that declares no operation,
# and the packer's own refusal of an image over its figure
python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at0.elf" -o "$T/testlib.budget.u2a" --name testlib --version 1 --needs 'base>=3.0' \
  --budget image=4096 --budget heap_keep=0 --budget heap_peak=0 --budget ops=1 --budget states=1 > /dev/null
python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at0.elf" -o "$T/testlib.ops0.u2a" --name testlib --version 1 --budget ops=0 > /dev/null
if python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at0.elf" -o "$T/testlib.big.u2a" --name testlib --version 1 --budget image=8 > /dev/null 2> "$T/pack.err"; then
  echo "u2a_pack accepted an image over its declared budget" >&2; exit 1
fi
grep -q "over its declared budget of 8 bytes" "$T/pack.err" || { echo "u2a_pack refused, but not with the budget sentence: $(cat "$T/pack.err")" >&2; exit 1; }
if python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at0.elf" -o "$T/testlib.bad.u2a" --name testlib --version 1 --budget colour=3 > /dev/null 2> "$T/pack.err"; then
  echo "u2a_pack accepted a budget key it does not know" >&2; exit 1
fi
[ ! -e "$T/testlib.big.u2a" ] && [ ! -e "$T/testlib.bad.u2a" ] || { echo "u2a_pack left a file behind after a refusal" >&2; exit 1; }

# T239.3 D20: a file that lists the names it exports (--export), read by the loader
python3 "$ROOT/tools/u2a_pack.py" "$TST/fixtures/testlib.at0.elf" -o "$T/testlib.exports.u2a" --name testlib --version 1 --export ring=1 --export trx=2 > /dev/null

# T104.1: the menu records of a library file (libmenu.h), compiled by the real tool; one with a route and one that hides are the refusals
python3 "$ROOT/tools/descrec.py" compile "$TST/menu_lib.toml" "$T/menu_lib.bin"
python3 - "$T" "$TST" <<'PY'
import sys
sys.path.insert(0, sys.argv[2] + "/../../tools")
import descrec
t = sys.argv[1]
base = '[[area]]\nkey = "F5"\n  [[area.tab]]\n  id = "a"\n  name = "A"\n  widget = "nodes"\n'
open(t + "/menu_route.bin", "wb").write(descrec.compile_text(base))
open(t + "/menu_hide.bin", "wb").write(descrec.compile_text('hide = ["x"]\n'))
open(t + "/windows.bin", "wb").write(descrec.compile_text('[[window]]\nname = "video"\naddress = 0xA0000500\nsize = 0x14\n[[window]]\nname = "mixer_2"\naddress = 0xA0000600\nsize = 0x100\n'))
open(t + "/menu_poll.bin", "wb").write(descrec.compile_text('[poll]\nop = "pa.tick"\nperiod_ms = 10\n'))
PY

loader_args() {   # loader_args SRCDIR: the compiler flags LA and the files LF of the test, for the product in SRCDIR
  LA=($FLAGS "-DFIXDIR=\"$T\"" -I"$1" -I"$SRC" -I"$ROOT/host-app/software/hostapp" -I"$ROOT/tests/coreapp" -I"$ROOT/app/video" -I"$ROOT/app/edit")
  LF=("$TST/test_libload.cc" "$1/libload.cc" "$1/libmenu.cc" "$1/descrec.cc" "$1/ops.cc" "$1/service.cc" "$ROOT/app/lib/testlib/testlib.cc"
      "$1/libsvc.cc" "$1/apphost.cc" "$TST/apphost_stubs.cc" "$1/settings.cc" "$1/cfgfile.cc" "$1/corefolder.cc" "$ROOT/tests/coreapp/sha256_cc.cc")      # base 3.3: the groups of api (libsvc.cc) over a real Settings and corefolder; apphost.cc (the base's job table) over stubs of the screens
}
build() {   # build OUT SRCDIR
  loader_args "$2"
  $CXX "${LA[@]}" -o "$1" "${LF[@]}"
}
# the fixtures the generators above left in $T and the test reads through FIXDIR: named, so that they are part of every unit's key (T100 D14)
CC_EXTRA="$T/testlib.u2a $T/testlib.exports.u2a $T/testlib.budget.u2a $T/testlib.ops0.u2a $T/testlib.at1000.raw $T/menu_lib.bin $T/menu_route.bin $T/menu_hide.bin $T/menu_poll.bin $T/windows.bin"
run() {
  local key t0=$SECONDS; loader_args "$SRC"; key="$(cc_key loader "test libload" -- "${LA[@]}" -- "${LF[@]}")"
  if cc_cached libload "$key" test; then PASS=$((PASS + 1)); return 0; fi
  build "$T/libload" "$SRC"
  cc_exec "$T/rl_libload" "$T/libload"
  echo "test libload: PASS $((SECONDS - t0))s"
  cc_store libload "$key" "$T/rl_libload" test
  PASS=$((PASS + 1))
}
control_job() {   # control NAME FILE 'sed-expr'; T100 D12: JOBS at a time (tests/parallel.sh), each in its own $JOBDIR
  local name="$1" file="$2" expr="$3"
  local key; loader_args "$SRC"; key="$(cc_key loader "control $name $file $expr" -- "${LA[@]}" -- "${LF[@]}")"
  if cc_cached "$name" "$key"; then return 0; fi
  local d="$JOBDIR/mut"; mkdir -p "$d"
  cp "$SRC"/*.cc "$SRC"/*.h "$d"/
  sed -e "$expr" "$d/$file" > "$d/$file.sed" && mv "$d/$file.sed" "$d/$file"
  if cmp -s "$d/$file" "$SRC/$file"; then echo "CONTROL $name: the sed expression changed nothing - the control is vacuous" >&2; exit 1; fi
  build "$JOBDIR/x" "$d" || { echo "CONTROL $name: the mutant does not build" >&2; exit 1; }
  if cc_exec "$JOBDIR/rl" timeout 30 "$JOBDIR/x" > "$JOBDIR/x.log" 2>&1; then
    echo "CONTROL $name: the test PASSED against the broken source - it cannot see this defect" >&2; cat "$JOBDIR/x.log" >&2; exit 1
  fi
  local red; red=$(grep -c '^FAIL' "$JOBDIR/x.log" || true)
  if [ "$red" -lt 1 ]; then echo "CONTROL $name: the run failed without a red check (a crash?) - that is not the test seeing the defect" >&2; cat "$JOBDIR/x.log" >&2; exit 1; fi
  echo "control $name: fails as it must ($red check(s) red)"
  cc_store "$name" "$key" "$JOBDIR/rl"
}
control() { cc_no_controls && return 0; pjob_spawn control_job "$@"; }

exports_args() {   # exports_args SRCDIR: the test of the base's own tables (T239.3 base 3.3, libexports.cc) over the recording model of the code behind them
  loader_args "$1"
  LF=("$TST/test_exports.cc" "$1/libload.cc" "$1/libmenu.cc" "$1/descrec.cc" "$1/ops.cc" "$1/service.cc" "$1/libsvc.cc" "$1/settings.cc" "$1/cfgfile.cc" "$1/corefolder.cc"
      "$ROOT/tests/coreapp/sha256_cc.cc" "$1/libexports.cc" "$1/sidfile.cc" "$TST/exports_stub.cc" "$ROOT/app/loaders/cbm_prg.cc")
  LA+=(-DLIBEXPORTS_SLOT_REGS_VAR=g_slot_regs -I"$ROOT/coreapp/cores" -I"$ROOT/app/lib/cbm1541" -I"$ROOT/app/loaders")
}
run_exports() {
  local key t0=$SECONDS; exports_args "$SRC"; key="$(cc_key loader "test exports" -- "${LA[@]}" -- "${LF[@]}")"
  if cc_cached libexports "$key" test; then PASS=$((PASS + 1)); return 0; fi
  $CXX "${LA[@]}" -o "$T/libexports" "${LF[@]}"
  cc_exec "$T/rl_libexports" "$T/libexports"
  echo "test libexports: PASS $((SECONDS - t0))s"
  cc_store libexports "$key" "$T/rl_libexports" test
  PASS=$((PASS + 1))
}

run
run_exports
control fs_flash_save libload.cc 's/return fs \&\& path \&\& !fs_write_refused(path) \&\& fs->save(path, buf, len);/return fs \&\& path \&\& fs->save(path, buf, len);/'
control fs_flash_rename libload.cc 's/!fs_write_refused(from) \&\& !fs_write_refused(to) \&\& fs->rename/!fs_write_refused(from) \&\& fs->rename/'
control fs_flash_open libload.cc 's/h->f = fs->open(path, rw \&\& !fs_write_refused(path));/h->f = fs->open(path, rw);/'

pjob_drain
echo "tests/loader: $PASS test program passed, $PJ_N parallel controls (JOBS=$JOBS), every negative control failed as it must"
