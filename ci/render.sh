#!/usr/bin/env bash
# Stage `render` (T174 D1): everything about our applications that is TESTED rather than built - the host tests (ci/apptests.sh), the render benches
# (view layer, core app, host app) and the 64 bit file benches. Formerly the tail of ci/trxapps.sh, 17 to 36 minutes in front of the package (E3, E4);
# now a child of its own (T174 D4: `apptests`, the RISC-V images wait for it), with a content key of its own.
#
# The key is the content of everything these tests read (the same application trees as ci/apptests.sh's common list, plus the benches themselves and
# the helpers they call): a stage whose key has a PASS marker ends at once ("from cache"), and inside a stage that has to run each suite, bench and
# negative control is a cached unit of its own (tests/ctlcache.sh, T100 D14), so a run says what it ran and why.
# Every part runs even when an earlier one failed; a red part turns the stage red at its end (T174 D4: no deferred verdict).
#
#   ci/render.sh                 the stage as CI runs it
#   ci/render.sh --key           print the stage key and stop
#   ci/render.sh -f              ignore the stage marker (the units keep their own caches; CONTROLS_ALL=1 runs every control)
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$ROOT"

driver=1   # bump it when what a PASS of this stage is worth changes, and say in the commit why
ok_dir="${CI_RENDER_OK:-/cache/multicore/render-ok}"
force="${CI_RENDER_FORCE:-}" want_key=""
for a in "$@"; do
  case "$a" in
    -f) force=1 ;;
    --key) want_key=1 ;;
    *) echo "ci/render.sh: unknown argument $a" >&2; exit 2 ;;
  esac
done

# what the stage reads: the application trees and Gideon's software (LVGL, FatFS), every bench and its helpers
specs=(coreapp app apps host-app vendor/gideon/software vendor/gideon/tools
       tests tools/zx_snap.py tools/zx_asm.py tools/gen_xexboot.sh tools/64tass.sh tools/check_app_size.py tools/u2a_pack.py
       tools/cor_pack.py tools/trx_pkg.py tools/txu_pack.py tools/pack_txu_mc.sh tools/descrec.py tools/exfat_check.py tools/gen_cfw_icons.py
       tools/core_logos/firmware cores/c64u/logo tools/core_logos/trx64 ci/apptests.sh ci/render.sh)
fk="$(files_key "${specs[@]}")" || { echo "render: the key list names a path that is not tracked" >&2; exit 1; }
key="$(printf '%s driver=%s %s' "$fk" "$driver" "$(${CXX:-c++} --version 2>/dev/null | head -1)" | sha256sum | cut -c1-16)"
if [ -n "$want_key" ]; then echo "$key"; exit 0; fi

out="$(stage_dir render)"
red_reset render
marker="$ok_dir/$key"
if [ -z "$force" ] && [ -f "$marker" ]; then
  echo "=== render $key: PASS from cache ($marker)"
  printf '%s\n' "$key" > "$out/render_key"; : > "$out/DONE"
  exit 0
fi
echo "=== render $key: running"

app="$ROOT/vendor/gideon"
W_OUT="${TMPDIR:-/tmp}/render-$$"; mkdir -p "$W_OUT/view" "$W_OUT/coreapp" "$W_OUT/hostapp"
trap 'rm -rf "$W_OUT"' EXIT
fails=0
part() { # part <name> <command...>: run it; a failure is a red target now, the other parts still run
  local name="$1" t0=$SECONDS; shift
  echo "=== render: $name"
  if "$@"; then echo "=== render: $name took $((SECONDS - t0))s"; else mark_red render "$name" "$name failed (see the log of this job)"; fails=$((fails + 1)); fi
}

# T100 D12: the host tests of the applications (tests/hostapp, tests/coreapp, tests/loader), cached per suite by the content of what each reads, their
# negative controls in parallel.
if command -v c++ >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
  part apptests "$ROOT/ci/apptests.sh"
else
  echo "application host tests SKIPPED: no c++ or no python3 in this image" >&2
fi

# T174 D2.6 (Alex, 2026-11-07: "benches only when something fundamental was touched, otherwise at night"): the render benches and the 64 bit file
# benches run when the plan says `fundamental` (main and its kind always; a branch when the diff touches ci/fundamental.txt), when -f is given
# (tools/dev.sh integrate) or when there is no plan (plan_has answers yes). Otherwise they are skipped out loud and no PASS marker is written for the
# stage - the nightly run on main and `dev.sh integrate` run them. The host tests above run either way (their own tiers and caches).
benches=1
if [ -z "$force" ] && ! plan_has fundamental; then
  benches=""
  echo "=== render: render benches and file benches SKIPPED: nothing fundamental touched on this branch (ci/fundamental.txt); they run on main, at night and with ci/render.sh -f" >&2
fi

# The render benches: they draw the real shell and the real menu with LVGL and no board. Each bench and control is a unit of the test cache (D14).
if [ -z "$benches" ]; then
  :
elif command -v c++ >/dev/null 2>&1 && [ -d "$app/software/lvgl/src" ]; then
  # one LVGL build for all of them, kept between runs (tests/lvgl_objs.sh)
  export TRX_LVGL_CACHE="${CI_TRXAPPS_CACHE:-/cache/trxapps}/lvgl"
  export CONTROLS_CACHE="${CONTROLS_CACHE:-${CI_APPTESTS_OK:-/cache/multicore/apptests-ok}/controls}"
  part render-view nice -n 15 "$ROOT/tests/view/run.sh" "$W_OUT/view"
  part render-coreapp env RENDER_CORE=shared nice -n 15 "$ROOT/tests/coreapp/render/run.sh" "$W_OUT/coreapp"   # T239.5 D13: each moved core's build runs in its lane (ci/lane.sh ui)
  part render-hostapp nice -n 15 "$ROOT/tests/hostapp/render/run.sh" "$W_OUT/hostapp"
else
  echo "render benches SKIPPED: no c++ or no vendor/gideon/software/lvgl in this checkout"
fi

# The 64 bit file benches (T104.1 D16): the board's CoreFs over the FileManager and FatFS on an exFAT volume of 6 GiB, and the fork's own bench of the
# same stack. The image is a sparse file (ftruncate), a few MB on disk. They need our copy of the fork at its t104-fs64 (File::get_size32); on an
# older copy they are SKIPPED here out loud, never silently. Both scripts use shasum (a Perl tool the image may not have): a sha256sum shim stands in.
if [ -z "$benches" ]; then
  :
elif command -v c++ >/dev/null 2>&1 && grep -q get_size32 "$app/software/filesystem/file.h" 2>/dev/null; then
  fs64_tmp="$W_OUT/fs64"; mkdir -p "$fs64_tmp"
  if ! command -v shasum >/dev/null 2>&1; then
    mkdir -p "$fs64_tmp/bin"
    printf '#!/bin/sh\nexec sha256sum "$@"\n' > "$fs64_tmp/bin/shasum"; chmod +x "$fs64_tmp/bin/shasum"
    export PATH="$fs64_tmp/bin:$PATH"
  fi
  # does this scratch directory keep a 6 GiB file sparse? If not, the benches write 6 GiB: they run only when the disk has 8 GiB for it
  probe="$fs64_tmp/probe"
  truncate -s 6G "$probe"
  sparse_kb="$(du -k "$probe" | cut -f1)"
  rm -f "$probe"
  if [ "$sparse_kb" -gt 1024 ]; then
    echo "fs64 benches: $fs64_tmp does not keep a 6 GiB file sparse"
    fs64_free_kb="$(df -Pk "$fs64_tmp" | awk 'NR==2 {print $4}')"
    [ "${fs64_free_kb:-0}" -gt $((8 * 1024 * 1024)) ] || { echo "fs64 benches SKIPPED: no sparse files and less than 8 GiB free" >&2; fs64_tmp=""; }
  fi
  if [ -n "$fs64_tmp" ]; then
    part fs64-corefs env TMPDIR="$fs64_tmp" nice -n 15 bash "$ROOT/tests/coreapp/fmfs64/run.sh"
    part fs64-fork env TMPDIR="$fs64_tmp" nice -n 15 bash "$app/software/filesystem/tests/run.sh"
  fi
else
  echo "fs64 benches SKIPPED: no c++ or vendor/gideon is not at the fork's t104-fs64 (no File::get_size32)"
fi

printf '%s\n' "$key" > "$out/render_key"
if [ "$fails" -eq 0 ]; then
  [ -z "$benches" ] || { mkdir -p "$ok_dir" 2>/dev/null && : > "$marker" || true; }
  : > "$out/DONE"
  echo "=== render $key: PASS in $(lap)"
  exit 0
fi
: > "$out/DONE"
stage_verdict render || exit 1
echo "=== render $key: $fails part(s) red in $(lap)"
