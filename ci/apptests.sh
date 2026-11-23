#!/usr/bin/env bash
# The host tests of our own applications (T100 D12): tests/hostapp, tests/coreapp, tests/loader. Each suite compiles the application
# sources natively against stubs and has a negative control per test (coreapp: about 650, each a full rebuild; they run in parallel,
# tests/parallel.sh). Until D12 they ran in `lint` on every push and cost 17 of its 19 minutes (Gitea run 158).
#
#   ci/apptests.sh [--tier fast|integration|nightly] [--plan] [--base REF] [-f] [suite ...]
#   ci/apptests.sh --key suite            print the suite's key and stop
#   ci/apptests.sh --emu-pass NAME        fallback only: record that the emulator script NAME passed for the tree as it is now, when it could not run here
#
# Tiers (T174 D1, Alex 2026-11-07: the long checks run at integration or nightly, never per slice):
#   auto         (default) integration on main or when a fundamental file is touched, else fast scoped to the touched suites (T174 D2, below)
#   fast         the test programs of hostapp/coreapp/loader whose inputs changed (a unit keeps its cached PASS while everything it read is as it was).
#                No negative controls (they are not even keyed), no render benches, no emulator scripts. A fast PASS writes no suite marker (a suite
#                is "cached PASS" only after a run that did the controls too) but a fast one, fast-<suite>.<key>, which only the fast tier honours (T174 D3).
#   integration  (default, what CI runs) every test, every negative control, the three render benches (view, coreapp_render, hostapp_render) and the
#                emulator scripts (hostapp_emu coreapp_emu, T174 D2.8): ue2emu through ci/ue2emu.sh on the built images (EMU_IMAGES, default the trxapps
#                hand-over; the kit: tools/ue2emu_kit.sh). Where the images, the kit or the emulator are missing they are SKIPPED out loud, never a PASS.
#   nightly      integration with the caches ignored (-f and CONTROLS_ALL=1).
# Suites: hostapp coreapp coreapp:cross coreapp:<core>... loader (T174 D3: `coreapp` is the shared group only, see core_groups below; names from tests/coreapp/cores.txt) | view coreapp_render hostapp_render | hostapp_emu coreapp_emu. Naming suites runs exactly those, at the tier's strictness.
# Before anything runs a plan line says which suites run and which are cached ("will run: ..., skipped: ... (inputs unchanged)"), with the files
# of `git diff BASE` (BASE default: the plan's base, else merge-base with origin/integration, else HEAD~1; none of them: every suite runs) that touch each suite. --plan goes further, down to the units:
# the suites walk through their keys with nothing built or run (CC_DRYRUN) and print "plan: would run ..." for each unit that has no valid cache entry.
#
# Per-suite cache, the mechanism of ci/sims.sh: a suite has a key over the content of exactly the files it reads (below), the
# compiler's version line and the driver number. A PASS leaves a marker $CI_APPTESTS_OK/<suite>.<key>; a suite whose key has a
# marker is "cached PASS" and is not run again. -f (or CI_APPTESTS_FORCE=1) ignores the markers. A failing suite writes none.
# JOBS: controls at a time (default: cores - 2).
# Inside a suite that has to run, every test program and every negative control is a unit of its own (T100 D13, D14; tests/ctlcache.sh): its key is
# its id, the compiler's dependency list of its build and its run arguments, its manifest is what its run opened (recorded, tests/readlog.c); a
# unit whose key was seen passing with everything it read unchanged is "cached PASS" / "cached (fails as it must)" and is neither built nor
# run, and each suite ends with one line "units: ran N, from the cache M". -f does not force the units, CONTROLS_ALL=1 does.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$ROOT"

# driver=1: bump it when what a PASS is worth changes (a suite's judgement), and say in the commit why.
driver=2   # 2: the suites run their tests and controls as cached units (T100 D14)
ok_dir="${CI_APPTESTS_OK:-/cache/multicore/apptests-ok}"

# What each suite reads. Shared by all: the application trees, our copy of Gideon's software (hostapp's FatFS bench), the python
# tools the run.sh files call (and what those import), the parallel helper. A pathspec that matches nothing makes files_key fail.
common=(coreapp uicore app host-app/software/hostapp host-app/software/io host-app/software/lvgl_port host-app/software/filesystem host-app/software/filemanager
        host-app/software/components host-app/software/infra host-app/software/system host-app/software/api host-app/software/network host-app/patches vendor/gideon/software tests/parallel.sh tests/ctlcache_selftest.sh
        tools/zx_snap.py tools/zx_asm.py tools/gen_xexboot.sh tools/64tass.sh tools/check_app_size.py tools/u2a_pack.py
        tools/cor_pack.py tools/trx_pkg.py tools/test_cor_pack.py tools/txu_pack.py tools/pack_txu_mc.sh tools/descrec.py tools/exfat_check.py tools/gen_cfw_icons.py tests/lvgl_objs.sh tools/core_logos/firmware cores/c64u/logo tools/core_logos/trx64 cores/apple2/rtl/roms 'cores/*/app/*' tools/pack_cores_mc.sh tools/pack_large_cor.sh tools/core_toml.py ci/version.sh cores/vc20/core.toml)
# T174 D3: the core application tests are one suite per group (tests/coreapp/cores.txt, tests/coreapp/run.sh CA_GROUP): `coreapp` = the units that name no
# core's files (shared), coreapp:<core> = the units of one core, coreapp:cross = the units that link several cores or read them at run time. A group is keyed
# over the application tree WITHOUT the files of the cores it does not own, so a change to one core's files moves that core's suite and `cross`, no other.
core_groups() { awk -F: '/^[a-z0-9_]+:/ { print $1 }' tests/coreapp/cores.txt; }
core_excl() {   # core_excl KEEP : exclude pathspecs for the files of every core but KEEP (KEEP empty: all cores)
  awk -F: -v keep="$1" '/^[a-z0-9_]+:/ { if ($1 != keep) { n = split($2, p, " "); for (i = 1; i <= n; i++) print ":(exclude)coreapp/cores/" p[i] } }' tests/coreapp/cores.txt
}
suite_specs() {
  case "$1" in
    # hostapp and loader link no core's files (T174 D3): the sources of the cores are out of their key, as of the shared coreapp group
    hostapp) printf '%s\n' "${common[@]}" tests/hostapp tools/check_host_cut.py tools/host_cut_ledger.txt tools/render_compare.py; core_excl "" ;;
    coreapp) printf '%s\n' "${common[@]}" tests/coreapp; core_excl "" ;;
    coreapp:cross) printf '%s\n' "${common[@]}" tests/coreapp ;;
    coreapp:*) printf '%s\n' "${common[@]}" tests/coreapp; core_excl "${1#coreapp:}" ;;
    uicore)  printf '%s\n' uicore tests/uicore ;;
    loader)  printf '%s\n' "${common[@]}" tests/loader tests/coreapp; core_excl "" ;;   # the loader bench includes tests/coreapp's headers
    # the render benches draw with LVGL, the view layer and the board build's source list (the Makefile); their units carry the exact reads, this is the plan's scope
    view)           printf '%s\n' "${common[@]}" tests/view app/view app/video ;;
    coreapp_render) printf '%s\n' "${common[@]}" tests/coreapp app/view app/video app/loaders app/lib app/edit host-app/target/u64ii/riscv/coreapp ;;
    hostapp_render) printf '%s\n' "${common[@]}" tests/hostapp app/view app/video ;;
    # emulator scripts (ue2emu through ci/ue2emu.sh, T174 D2.8): they run on the built RISC-V images; a PASS is cached for the content below
    hostapp_emu)    printf '%s\n' "${common[@]}" tests/hostapp host-app/target/u64ii/riscv/hostapp host-app/target/u64ii/riscv/trx_io.ld ci/ue2emu.sh tools/ue2emu_kit.sh ;;
    coreapp_emu)    printf '%s\n' "${common[@]}" tests/coreapp host-app/target/u64ii/riscv/coreapp host-app/target/u64ii/riscv/trx_io.ld ci/ue2emu.sh tools/ue2emu_kit.sh ;;
    *) return 1 ;;
  esac
}
suite_key() {   # suite_key <suite>
  local specs fk
  specs="$(suite_specs "$1")" || { echo "unknown suite $1" >&2; return 1; }
  # shellcheck disable=SC2086
  fk="$(files_key $specs)" || return 1
  # T174 D3: the test cache harness (tests/ctlcache.sh, ctlcache.py, readlog.c) is in the key by its CC_KEY_VERSION, not by its bytes: a comment-only edit moves nothing
  printf '%s %s driver=%s ckv=%s %s' "$1" "$fk" "$driver" "$(sed -n 's/^CC_KEY_VERSION=\([0-9]*\).*/\1/p' tests/ctlcache.sh)" "$(${CXX:-c++} --version 2>/dev/null | head -1)" | sha256sum | cut -c1-16
}

scoped="" force="${CI_APPTESTS_FORCE:-}" suites=() tier=auto plan="" base="" emu_pass=""
while [ $# -gt 0 ]; do
  case "$1" in
    -f) force=1; shift ;;
    --key) suite_key "$2"; exit $? ;;
    --tier) tier="$2"; shift 2 ;;
    --plan) plan=1; shift ;;
    --base) base="$2"; shift 2 ;;
    --emu-pass) emu_pass="$2"; shift 2 ;;
    *) suites+=("$1"); shift ;;
  esac
done
# T239.1: the base of "touched" is the base of the plan job (plan.json base.commit, the last green run or the merge-base, ci/plan.sh); a CI checkout is
# shallow (depth 1: no origin/integration, no HEAD~1), so that commit is fetched when it is missing. Without a plan: the merge-base with origin/integration (the trunk, D48), else HEAD~1.
# A base that cannot be had is never "nothing touched": base stays empty, the tier becomes integration and every suite runs (said out loud below).
have_commit() { [ -n "$1" ] && [ "$1" != none ] && git cat-file -e "$1^{commit}" 2>/dev/null; }
base_from_plan() {
  [ -f "$HANDOFF/plan.json" ] || return 1
  local c; c="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["base"]["commit"])' "$HANDOFF/plan.json" 2>/dev/null)" || return 1
  have_commit "$c" || { [ -n "$c" ] && [ "$c" != none ] && git fetch -q --depth 1 origin "$c" 2>/dev/null; } || return 1
  have_commit "$c" && { echo "$c"; echo "=== apptests: base ${c:0:8} from the plan (plan.json)" >&2; }
}
base_src=""
if [ -n "$base" ]; then base_src="given"
else
  if base="$(base_from_plan)" && [ -n "$base" ]; then base_src=plan
  elif base="$(git merge-base HEAD origin/integration 2>/dev/null)" && have_commit "$base"; then base_src="merge-base with origin/integration"
  elif base="$(git rev-parse --verify -q HEAD~1 2>/dev/null)" && have_commit "$base"; then base_src="HEAD~1"
  else base=""; fi
fi
# T174 D2 (Alex): on a feature branch only what the diff touches runs (fast, plus the emulator scripts of touched UI); controls, render benches and
# the emulator list come at the merge into main, at night, or when a FUNDAMENTAL file is touched (shared headers, the view layer, the test machinery,
# the build system). Docs-only changes trigger nothing. On main (or in a detached CI checkout of main) `auto` is integration.
fundamental=(); while IFS= read -r _f; do [ -z "$_f" ] || fundamental+=("$_f"); done < <(python3 "$ROOT/ci/plan.py" fundamental apptests)   # ci/fundamental.txt: [all] + [apptests], the one list (also read by plan.py for the benches)
[ ${#fundamental[@]} -gt 0 ] || { echo "apptests: ci/fundamental.txt gave no paths" >&2; exit 1; }
if [ -z "$base" ]; then
  echo "=== apptests: NO DIFF BASE (no plan.json base, no origin/integration, no HEAD~1): nothing can be called untouched, every suite runs (tier integration)"
  [ "$tier" != auto ] || tier=integration
  scoped=""
fi
if [ "$tier" = auto ]; then
  br="${CI_BRANCH:-$(git rev-parse --abbrev-ref HEAD 2>/dev/null)}"
  fcount=0
  for f in "${fundamental[@]}"; do [ -e "$f" ] || continue; fcount=$((fcount + $( { git diff --name-only "$base" -- "$f"; git ls-files -o --exclude-standard -- "$f"; } 2>/dev/null | wc -l))); done
  case "$br" in
    main|HEAD) tier=integration; echo "=== apptests: tier auto -> integration (branch $br)" ;;
    *) if [ "$fcount" -gt 0 ]; then tier=integration; echo "=== apptests: tier auto -> integration (branch $br touches $fcount fundamental file(s))"
       else tier=fast; scoped=1; echo "=== apptests: tier auto -> fast, scoped to the suites the branch $br touches (no fundamental file touched)"; fi ;;
  esac
fi
case "$tier" in fast|integration|nightly) ;; *) echo "apptests: unknown tier $tier (fast|integration|nightly)" >&2; exit 2 ;; esac
test_suites=(hostapp coreapp coreapp:cross $(core_groups | sed 's/^/coreapp:/') loader uicore) render_suites=(view coreapp_render hostapp_render) emu_suites=(hostapp_emu coreapp_emu)
if [ ${#suites[@]} -eq 0 ]; then
  suites=("${test_suites[@]}")
  if [ "$tier" != fast ]; then suites+=("${render_suites[@]}" "${emu_suites[@]}"); elif [ -n "$scoped" ]; then suites+=("${emu_suites[@]}"); fi
fi
[ "$tier" != nightly ] || { force=1; export CONTROLS_ALL=1; }
export TEST_TIER="$tier"
[ -z "$plan" ] || export CC_DRYRUN=1

cache_on=""
if mkdir -p "$ok_dir" 2>/dev/null && [ -w "$ok_dir" ]; then cache_on=1; fi
# T100 D13: the negative controls are cached one by one (tests/ctlcache.sh), under the same root. CONTROLS_ALL=1 runs them all.
# T174 R1b: this root is the builder's /cache/multicore/apptests-ok for the agents' containers and for CI alike (mounted -v /cache/multicore:/cache/multicore).
[ -n "$cache_on" ] && export CONTROLS_CACHE="${CONTROLS_CACHE:-$ok_dir/controls}"
if ! command -v c++ >/dev/null || ! command -v python3 >/dev/null; then
  echo "=== apptests SKIPPED out loud: no c++ or no python3 in this image" >&2; exit 1
fi
export TRX_LVGL_CACHE="${TRX_LVGL_CACHE:-${CI_TRXAPPS_CACHE:-/cache/trxapps}/lvgl}"
render_dir="${TMPDIR:-/tmp}/apptests-render"

kind_of() { case "$1" in view|coreapp_render|hostapp_render) echo render ;; hostapp_emu|coreapp_emu) echo emu ;; *) echo test ;; esac; }
script_of() {
  case "$1" in
    view) echo tests/view/run.sh ;; coreapp_render) echo tests/coreapp/render/run.sh ;; hostapp_render) echo tests/hostapp/render/run.sh ;;
    hostapp_emu) echo tests/hostapp/emu_host.sh ;; coreapp_emu) echo tests/coreapp/emu/run.sh ;;
    coreapp:*) echo tests/coreapp/run.sh ;;
    *) echo "tests/$1/run.sh" ;;
  esac
}
if [ -n "$emu_pass" ]; then
  k="$(suite_key "$emu_pass")" || exit 1
  printf '%s %s\n' "$(git -C "$ROOT" rev-parse --short=8 HEAD)" "$(date -u +%FT%TZ)" > "$ok_dir/$emu_pass.$k" && echo "apptests: $emu_pass recorded as PASS ($k)"
  exit 0
fi

# ---- the plan: which suites the diff touches, which have a recorded PASS for exactly this content
will=() skip=() fails=()
for s in "${suites[@]}"; do
  specs="$(suite_specs "$s")" || { echo "=== apptests: unknown suite $s" >&2; exit 2; }
  # shellcheck disable=SC2086
  nt="$( { git diff --name-only "$base" -- $specs; git ls-files -o --exclude-standard -- $specs; } 2>/dev/null | sort -u | wc -l | tr -d ' ')"
  key="$(suite_key "$s")" || { echo "=== $s: no key" >&2; fails+=("$s"); continue; }
  printf -v "skey_${s//:/_}" %s "$key"
  k="$(kind_of "$s")"
  if [ -n "${scoped:-}" ] && [ "$nt" = 0 ]; then skip+=("$s"); continue; fi   # branch scope: nothing of it touched, integration's cache stands
  if [ "$k" = render ]; then will+=("${s}[$nt files; units decide]"); continue; fi
  if [ -z "$force" ] && [ -n "$cache_on" ] && { [ -f "$ok_dir/$s.$key" ] || { [ "$tier" = fast ] && [ -f "$ok_dir/fast-$s.$key" ]; }; }; then skip+=("$s"); else will+=("${s}[$nt files]"); fi
done
echo "=== apptests plan (tier $tier, base ${base:0:8}): will run: ${will[*]:-nothing}, skipped: ${skip[*]:-nothing} (inputs unchanged, PASS recorded)"
[ ${#fails[@]} -eq 0 ] || { echo "=== apptests FAILED: ${fails[*]}" >&2; exit 1; }

run_one() {   # run_one suite : returns the suite's status
  local s="$1" k key sc v
  case " ${skip[*]:-} " in *" $s "*) if [ -n "$scoped" ]; then echo "=== apptests $s: not touched by this branch (cache of integration stands)"; return 0; fi ;; esac
  k="$(kind_of "$s")"; v="skey_${s//:/_}"; key="${!v}"; sc="$(script_of "$s")"
  if [ "$k" = emu ]; then
    # T174 D2.8: the emulator suites run here, in ue2emu (ci/ue2emu.sh: the local binary or the UE2EMU_IMAGE container), on the images ci/trxapps.sh built
    # (EMU_IMAGES, default: the trxapps hand-over of this run). Exit 77 = nothing to run with (no images, kit or emulator): said out loud, no PASS is
    # recorded and the --emu-pass marker of a run elsewhere still counts as the fallback.
    if [ -z "$force" ] && [ -n "$cache_on" ] && [ -f "$ok_dir/$s.$key" ]; then echo "=== apptests $s: cached PASS $key ($(cat "$ok_dir/$s.$key"))"; return 0; fi
    local imgs="${EMU_IMAGES:-$HANDOFF/trxapps}" eout="${EMU_OUT:-${TMPDIR:-/tmp}/apptests-emu}/$s" erc=0 t1=$SECONDS
    mkdir -p "$eout"
    case "$s" in
      hostapp_emu) nice -n 15 "$sc" "$imgs" "$eout" || erc=$? ;;
      coreapp_emu) EMU_LOGS="$eout" nice -n 15 "$sc" "$imgs/coreapp_trx64u.app" || erc=$? ;;   # T228 R25: tests/coreapp/emu/run.sh runs trx64u, which has its own image
      *)           nice -n 15 "$sc" "$imgs/coreapp.app" || erc=$? ;;
    esac
    if [ $erc -eq 77 ]; then
      echo "=== apptests $s: SKIPPED out loud: no images in $imgs, no kit or no emulator here - NOT run; $sc elsewhere, then ci/apptests.sh --emu-pass $s (key $key)" >&2
      if [ -n "$cache_on" ] && [ -f "$ok_dir/$s.$key" ]; then echo "=== apptests $s: PASS recorded for this tree ($(cat "$ok_dir/$s.$key"))"; fi
      return 0
    fi
    if [ $erc -ne 0 ]; then echo "=== apptests $s: FAIL after $((SECONDS - t1))s" >&2; return 1; fi
    echo "=== apptests $s: done in $((SECONDS - t1))s (output in $eout)"
    if [ -z "$plan" ] && [ -n "$cache_on" ]; then printf '%s %s\n' "$(git -C "$ROOT" rev-parse --short=8 HEAD)" "$(date -u +%FT%TZ)" > "$ok_dir/$s.$key"; fi
    return 0
  fi
  if [ "$k" = test ] && [ -z "$force" ] && [ -n "$cache_on" ] && [ -f "$ok_dir/$s.$key" ]; then
    echo "=== apptests $s: cached PASS $key ($(cat "$ok_dir/$s.$key"))"; return 0
  fi
  # T174 D3: a fast PASS is remembered for the fast tier only (fast-<suite>.<key>): the key is over everything the suite reads, so an unchanged key means the
  # same units would be walked and all served from the unit cache - the walk itself (about 5 s per suite, 40 s for the shared tests) is what this saves.
  if [ "$k" = test ] && [ "$tier" = fast ] && [ -z "$force" ] && [ -n "$cache_on" ] && [ -f "$ok_dir/fast-$s.$key" ]; then
    echo "=== apptests $s: cached fast PASS $key ($(cat "$ok_dir/fast-$s.$key"))"; return 0
  fi
  if [ "$k" = render ] && { [ ! -d "${ULTIMATE_SW:-$ROOT/vendor/gideon/software}/lvgl/src" ] || [ ! -x "$ROOT/$sc" ]; }; then
    echo "=== apptests $s: SKIPPED out loud: no LVGL in this checkout" >&2; return 0
  fi
  echo "=== apptests $s: ${plan:+planning }running (key $key, tier $tier)"
  local t0=$SECONDS rc=0 args=()
  if [ "$k" = render ]; then mkdir -p "$render_dir/$s"; args=("$render_dir/$s"); fi
  local grp=""; case "$s" in coreapp) grp=shared ;; coreapp:*) grp="${s#coreapp:}" ;; esac   # T174 D3: the group of the core application tests
  CA_GROUP="$grp" nice -n 15 "$sc" ${args[@]+"${args[@]}"} || rc=$?
  if [ $rc -eq 0 ]; then
    echo "=== apptests $s: ${plan:+plan }done in $((SECONDS - t0))s"
    if [ "$k" = test ] && [ "$tier" = fast ] && [ -z "$plan" ] && [ -n "$cache_on" ]; then
      printf '%s %s\n' "$(git -C "$ROOT" rev-parse --short=8 HEAD 2>/dev/null || echo none)" "$(date -u +%FT%TZ)" > "$ok_dir/fast-$s.$key"
      # shellcheck disable=SC2012
      ls -1t "$ok_dir/fast-$s".* 2>/dev/null | tail -n +4 | xargs rm -f
    fi
    if [ "$k" = test ] && [ "$tier" != fast ] && [ -z "$plan" ] && [ -n "$cache_on" ]; then
      printf '%s %s\n' "$(git -C "$ROOT" rev-parse --short=8 HEAD)" "$(date -u +%FT%TZ)" > "$ok_dir/$s.$key"
      # shellcheck disable=SC2012
      ls -1t "$ok_dir/$s".* 2>/dev/null | tail -n +4 | xargs rm -f
    fi
  else
    echo "=== apptests $s: FAIL after $((SECONDS - t0))s" >&2; return 1
  fi
}
for s in "${suites[@]}"; do run_one "$s" || fails+=("$s"); done
[ ${#fails[@]} -eq 0 ] || { echo "=== apptests FAILED: ${fails[*]}" >&2; exit 1; }
echo "=== apptests ok (tier $tier)"
