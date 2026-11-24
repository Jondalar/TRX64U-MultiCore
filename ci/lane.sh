#!/usr/bin/env bash
# T239.5 (D8-D10): one cell of the matrix - one step of one lane - run by its job in .github/workflows/matrix.yml. The lanes run on the T239 branch
# (t239.3, t239.3-*) instead of the old pipeline (D3). Each cell prints what it checked; a red cell stops its lane and the lanes that need it.
#
#   ci/lane.sh <lane> <step>
#     sdk        unit | build | test | package
#     host       unit | build
#     core:<id>  unit | changed | compare | needs | build | fits | service | ui | api | package | deliver
#     bits:<id>  key | report        (the host's and the core's bitstreams; the builder builds a missing key, ci/fetch_bits.sh restores it)
#
# The steps (D8): 1 lint (ci/lint.sh <lane>, its own job), 2 unit, 3 changed, 4 compare, 5 needs, 6 build, 7 fits (the SDK: test), 8 service, 9 ui, 10 api,
# 11 package, 12 emulator tests (tests/hostapp/emu_host.sh, the emulator job), 13 deliver (the artifact of the job).
# env: CI_HANDOFF (the hand-over directory: the dispatcher's plan.json, the artifacts of the lanes this one needs: sdk/, bits/), CI_BIT_CACHE (the restored
# bitstreams), CI_APPTESTS_OK and CI_TRXAPPS_CACHE (the caches of the jobs).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
lane="${1:?usage: ci/lane.sh <lane> <step>}" step="${2:?usage: ci/lane.sh <lane> <step>}"
die() { echo "LANE RED: $lane $step: $*" >&2; exit 1; }
say() { echo "=== $lane $step: $*"; }

# the units a core's TOML B names for one step (tools/libtab.py tests, D4), run as that core's app (tests/coreapp/run.sh: its group, CA_UNITS)
core_tests() {   # core_tests <id> <kind>
  local id="$1" kind="$2" units
  units="$(python3 -I "$ROOT/tools/libtab.py" tests "$ROOT/cores/$id/app/sdk.toml" "$kind" | tr '\n' ' ')"
  [ -n "${units// /}" ] || die "cores/$id/app/sdk.toml names no test of the kind $kind (T239.5 D4: each library names its tests)"
  say "the units of cores/$id/app/sdk.toml: $units"
  CA_GROUP="$id" CA_UNITS="$units" TEST_TIER=fast nice -n 15 "$ROOT/tests/coreapp/run.sh"
}
sdk_tarball() { ls "$HANDOFF"/sdk/trx64-sdk-*.tar.gz 2>/dev/null | head -1; }
differs() {   # differs <base> <pathspec>... : yes or no; a failing git diff (exit > 1) is red, not "yes"
  local rc=0; git -C "$ROOT" diff --quiet "$1" HEAD -- "${@:2}" || rc=$?
  case $rc in 0) echo no ;; 1) echo yes ;; *) die "git diff $1..HEAD failed (exit $rc)" ;; esac
}

case "$lane" in
sdk)
  case "$step" in
    # 2: the units of the base and the shared libraries (the suite `coreapp`: the units that name no core)
    unit) "$ROOT/ci/apptests.sh" --tier fast coreapp ;;
    # 6: the base, the shared libraries, the sub-apps (.u2a), the base archives and the SDK package (ci/trxapps.sh, its part sdk)
    build) TRXAPPS_ONLY=sdk "$ROOT/ci/trxapps.sh" ;;
    # 7: the package checks itself - the example library, its menu record and the example core's app built from the unpacked package alone
    test)
      t="$(ls "$HANDOFF"/trxapps/trx64-sdk-*.tar.gz 2>/dev/null | head -1)"; [ -n "$t" ] || die "no SDK package in $HANDOFF/trxapps (step build)"
      RV_PREFIX=riscv32-unknown-elf- bash "$ROOT/tools/sdk_check.sh" "$t" ;;
    # 11: what the lane deploys (13: the job's artifact `sdk`, which the core lanes take): the package, the sub-apps, the shared app's link map (S12)
    package)
      o="$HANDOFF/sdk"; rm -rf "$o"; mkdir -p "$o"
      cp "$HANDOFF"/trxapps/trx64-sdk-*.tar.gz "$HANDOFF"/trxapps/*.u2a "$HANDOFF/trxapps/coreapp.map" "$o/"
      (cd "$o" && sha256sum ./* > SHA256SUMS && cat SHA256SUMS)
      say "$(tar -xzOf "$o"/trx64-sdk-*.tar.gz --wildcards '*/VERSION' 2>/dev/null | head -3 | tr '\n' ' ')" ;;
    *) die "the SDK lane has no step $step" ;;
  esac ;;

host)
  case "$step" in
    unit) "$ROOT/ci/apptests.sh" --tier fast hostapp ;;
    # 6: the host app and its emulator build (ci/trxapps.sh, its part host); 13: the job's artifact `host`
    build)
      TRXAPPS_ONLY=host "$ROOT/ci/trxapps.sh"
      o="$HANDOFF/host"; rm -rf "$o"; mkdir -p "$o"
      cp "$HANDOFF/trxapps/hostapp.app" "$HANDOFF/trxapps/hostapp.elf" "$HANDOFF/trxapps/hostapp_emu.elf" "$o/" ;;
    *) die "the host menu lane has no step $step" ;;
  esac ;;

core:?*)
  id="${lane#core:}"; app="$ROOT/cores/$id/app"; tomlb="$app/sdk.toml"
  [ -f "$tomlb" ] || die "cores/$id has no TOML B (cores/$id/app/sdk.toml): it has not moved to the SDK"
  out="$HANDOFF/core-$id"; mkdir -p "$out"
  case "$step" in
    # 2: the unit tests of its own libraries (the units its TOML B names as unit)
    unit) core_tests "$id" unit ;;
    # 3: what this push changed for the core, against the plan's base: its gateware (the files of its bitstream key, ci/lib.sh ooc_key: the sources of its
    # core.toml without app/ and logo/; by git, so a change to comments alone counts here and not in the key), its TOML A, its app (cores/<id>/app). Not the
    # plan's DFX lane: that one counts the app's files too (an app-only push plans it, and keeps the bitstream key)
    changed)
      [ -f "$HANDOFF/plan.json" ] || die "no plan.json from the dispatcher"
      base="$(python3 -c 'import json,sys; print((json.load(open(sys.argv[1])).get("base") or {}).get("commit") or "")' "$HANDOFF/plan.json")"
      specs=(); while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$ROOT/host/layers.txt" "core:$id")
      [ "${#specs[@]}" -gt 0 ] || die "core:$id has no sources in its core.toml"
      gw=unknown ta=unknown ap=unknown
      if [ -n "$base" ] && git -C "$ROOT" cat-file -e "$base^{commit}" 2>/dev/null; then
        gw="$(differs "$base" "${specs[@]}" ':(exclude,glob)cores/*/app/**' ':(exclude,glob)cores/*/logo/**')"
        ta="$(differs "$base" "cores/$id/app/gateware.toml")"
        ap="$(differs "$base" "cores/$id/app")"
      fi
      printf '%s\n' "gateware=$gw" "toml_a=$ta" "app=$ap" > "$out/changed"
      say "the gateware: $gw, its TOML A: $ta, its app: $ap (base ${base:-none})" ;;
    # 4: has the gateware changed, is the TOML A complete (against the RTL: tools/rtl_check.py through tests/rtl_check, which holds each moved core's case with its
    # design parameters - the VIC-20's partial is case 3 - and runs every case, the host's and the fixtures' too), does the app change with it
    compare)
      [ -f "$out/changed" ] || die "no result of the step changed"
      gw="$(sed -n 's/^gateware=//p' "$out/changed")"; ta="$(sed -n 's/^toml_a=//p' "$out/changed")"
      say "the TOML A against the RTL"
      python3 -I "$ROOT/tests/rtl_check/test_rtl_check.py"
      if [ "$gw" = yes ] && [ "$ta" = no ]; then say "the gateware changed and its TOML A did not: the app is the same (the TOML A holds against the new RTL, above)"; fi
      if [ "$ta" = yes ]; then say "the TOML A changed: the app is built again against it (steps 5 and 6)"; fi ;;
    # 5: what must be in the app: the driver table and the I/O fragment of the TOML A (with the base blocks the TOML B names), the library table of the TOML B,
    # generated from the SDK package and not linked
    needs)
      t="$(sdk_tarball)"; [ -n "$t" ] || die "no SDK package in $HANDOFF/sdk (the SDK lane's artifact)"
      RV_PREFIX=riscv32-unknown-elf- bash "$ROOT/tools/sdk_core_app.sh" "$t" "$id" "$out/gen" --no-link ;;
    # 6: the app, linked from the SDK package alone (tools/sdk_core_app.sh; the link map against the TOML B is checked there)
    build)
      t="$(sdk_tarball)"; [ -n "$t" ] || die "no SDK package in $HANDOFF/sdk (the SDK lane's artifact)"
      RV_PREFIX=riscv32-unknown-elf- bash "$ROOT/tools/sdk_core_app.sh" "$t" "$id" "$out/app" ;;
    # 7: complete and fits: the link map holds what the TOML B names and the base; the image, its heap and the stack reserve fit the application region and
    # the heap the libraries keep fits the heap (cor_pack heap: the check pack makes, on the app alone; the app is the bare image the .cor takes, so
    # check_app_size.py - the boot ROM's flash slot, a headered image - is not its check); a library the core shares with the shared app is that same library (S12)
    fits)
      [ -s "$out/app/$id.app" ] || die "no $id.app (step build)"
      python3 -I "$ROOT/tools/libtab.py" mapcheck "$tomlb" "$out/app/$id.map" "$ROOT"
      h="$(python3 "$ROOT/tools/core_toml.py" get "$id" heap)"
      python3 -I "$ROOT/tools/cor_pack.py" heap "$out/app/$id.app" --core "$id" --sizes "$tomlb" --heap "${h:-0}"
      # the libraries both TOML Bs name: one defined alike (sources, headers) is the shared app's and must be linked from the same sources in both (S12: cbm1541);
      # one defined otherwise under the same name is said here and is not this check's (the VIC-20's cbmrun without c64_go.cc, T239.5 §5: open with Alex)
      libs="$(python3 -I -c 'import sys; sys.path.insert(0, sys.argv[1] + "/tools"); import libtab
a = {l["name"]: l for l in libtab.load(sys.argv[2])}; b = {l["name"]: l for l in libtab.load(sys.argv[3])}
key = lambda l: (sorted(l.get("sources", [])), sorted(l.get("headers", [])))
both = sorted(set(a) & set(b))
print(" ".join(n for n in both if key(a[n]) == key(b[n]))); print(" ".join(n for n in both if key(a[n]) != key(b[n])))' "$ROOT" "$tomlb" "$ROOT/coreapp/libs/shared.toml")"
      shared="$(sed -n 1p <<< "$libs")"; other="$(sed -n 2p <<< "$libs")"
      for lib in $other; do say "the library $lib has the shared app's name and other sources or headers: not compared"; done
      for lib in $shared; do
        [ -s "$HANDOFF/sdk/coreapp.map" ] || die "no coreapp.map in $HANDOFF/sdk (the SDK lane's artifact)"
        python3 -I "$ROOT/tools/libtab.py" samelib "$ROOT/coreapp/libs/shared.toml" "$tomlb" "$lib" "$HANDOFF/sdk/coreapp.map" "$out/app/$id.map"
        say "the library $lib is the shared app's, and linked in both"
      done ;;
    # 8, 9, 10: the tests its TOML B names for the step (D4)
    service) core_tests "$id" service ;;
    ui)      core_tests "$id" ui ;;
    api)     core_tests "$id" api ;;
    # 11: the .cor - the core's partial bound to the host it was routed against and its own app (ci/pack_one.sh; the heap figures again, from the packer)
    package)
      [ -s "$out/app/$id.app" ] || die "no $id.app (step build)"
      "$ROOT/ci/pack_one.sh" "$id" --app-dir "$out/app" --out "$out/cor" ;;
    # 13: the .cor, the app's ELF (the emulator's symbols) and the core's documents: its licence notice and the manual's chapters on cores and ROMs
    deliver)
      [ -s "$out/cor/$id.cor" ] || die "no $id.cor (step package)"
      d="$HANDOFF/deliver-$id"; rm -rf "$d"; mkdir -p "$d/docs"
      cp "$out/cor/$id.cor" "$out/app/$id.elf" "$d/"
      [ ! -f "$ROOT/cores/$id/rtl/NOTICE" ] || cp "$ROOT/cores/$id/rtl/NOTICE" "$d/docs/NOTICE-$id"
      cp "$ROOT/docs/manual/11-cores.md" "$ROOT/docs/manual/12-roms.md" "$d/docs/"
      (cd "$d" && sha256sum "$id.cor" "$id.elf" | tee SHA256SUMS) ;;
    *) die "a core lane has no step $step" ;;
  esac ;;

bits:?*)
  id="${lane#bits:}"
  case "$step" in
    # 3: the content keys of the host and of the core (ci/keys.sh: the names of the cache entries and of the bit-<entry> releases)
    key)
      say "the host (with the reference core $(layer_reference "$ROOT/host/layers.txt")) and $id:"
      "$ROOT/ci/fetch_bits.sh" --names "core:$id" | sed 's/^/    /' ;;
    # 7, 11: the entries restored from the builds repository (ci/fetch_bits.sh) and what the builder reported for them
    report)
      cache="${CI_BIT_CACHE:?CI_BIT_CACHE}"
      for e in "$cache"/host-* "$cache"/core-"$id"-*; do
        [ -f "$e/DONE" ] || continue
        say "${e##*/}: from $(cat "$e/from" 2>/dev/null || echo ?)"
        # per report: the verdict on the constraints, the values under the Design Timing Summary's header (not the header: run 45481704917), the first LUT row;
        # awk and not grep | head: a report without such a line is no failure (grep's exit 1 ended the step under pipefail, run 45476822869)
        for f in "$e"/*.rpt "$e"/*timing*.txt "$e"/*util*.txt; do
          [ -f "$f" ] || continue
          awk -v n="${f##*/}" '
            /All user specified timing constraints are met|Timing constraints are not met/ { print "    " n ": " $0; next }
            w == 1 { w = 2; next }
            w == 2 { printf "    %s: WNS %s ns, TNS %s ns, WHS %s ns\n", n, $1, $2, $5; w = 3; next }
            w == 0 && /^[ \t]*WNS\(ns\)[ \t]+TNS\(ns\)/ { w = 1; next }
            /^\| Slice LUTs/ && !l { gsub(/[ ]+/, " "); print "    " n ": " $0; l = 1 }' "$f"
        done
      done ;;
    *) die "a bitstream lane has no step $step" ;;
  esac ;;

*) die "unknown lane (sdk, host, core:<id>, bits:<id>)" ;;
esac
say "green"
