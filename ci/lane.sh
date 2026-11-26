#!/usr/bin/env bash
# T239.5 (D8-D10): one cell of the matrix - one step of one lane - run by its job in .github/workflows/matrix.yml. The lanes run on the T239 branch
# (t239.3, t239.3-*) instead of the old pipeline (D3), on every other push beside it (D11, D13, D14; ci/dispatch.py starts the lanes). Each cell prints
# what it checked; a red cell stops its lane and the lanes that need it.
#
#   ci/lane.sh <lane> <step>
#     sdk        unit | build | test | package
#     host       unit | build
#     core:<id>  unit | changed | compare | needs | build | fits | service | ui | api | package | deliver
#     bits:<id>  key | fetch | report  (a partial core: the host's and the core's bitstreams; a large core: its flat bitstream, T213.3 P5; the builder
#                                     builds a missing key, ci/fetch_bits.sh restores it)
#     package    names | collect | package   (D14: the whole package - every lane's bitstream entries, as the old pipeline's collect; then ci/package.sh over
#                                     what the lanes delivered: the SDK lane's shared image, sub-apps and SDK package, the host menu, each core lane's .cor)
#   A core lane is a partial core (dfx) or a large core (large: cores/<id>/mono/core.toml, its own flat bitstream), tools/core_toml.py lanes.
#
# The steps (D8): 1 lint (ci/lint.sh <lane>, its own job), 2 unit, 3 changed, 4 compare, 5 needs, 6 build, 7 fits (the SDK: test), 8 service, 9 ui, 10 api,
# 11 package, 12 emulator tests (the emulator jobs: a core lane's app alone, tests/coreapp/emu/core_round.sh; the host menu lane's round with vc20.cor,
# tests/hostapp/emu_host.sh), 13 deliver (the artifact of the job).
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
kind_of() {   # kind_of <id>: dfx or large (tools/core_toml.py lanes, T213.3 P5)
  local k; k="$(python3 -I "$ROOT/tools/core_toml.py" lanes | awk -v c="$1" '$1 == c { print $2 }')"
  [ -n "$k" ] || die "$1 is no core lane (tools/core_toml.py lanes: no TOML B of its own that names its descriptor)"
  echo "$k"
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
    # 7: the package checks itself - the example library, its menu record and the example core's app built from the unpacked package alone; then the example's app with the overlay test library (D19)
    test)
      t="$(ls "$HANDOFF"/trxapps/trx64-sdk-*.tar.gz 2>/dev/null | head -1)"; [ -n "$t" ] || die "no SDK package in $HANDOFF/trxapps (step build)"
      RV_PREFIX=riscv32-unknown-elf- bash "$ROOT/tools/sdk_check.sh" "$t"
      # T239.5 D19: the overlay test library linked into the SDK example's app from this package (tests/ui/ovtest/build.sh); the artifact `ovtest-app` takes it to the emulator job `ovtest-emu`
      rm -rf "$HANDOFF/ovtest"; mkdir -p "$HANDOFF/ovtest"
      RV_PREFIX=riscv32-unknown-elf- bash "$ROOT/tests/ui/ovtest/build.sh" "$t" "$HANDOFF/ovtest/example_ovtest.app" ;;
    # 11: what the lane deploys (13: the job's artifact `sdk`, which the core lanes take): the package, the sub-apps, the shared app's link map (S12); and the
    # shared image with its ELF (13: the artifact `shared-app`), which the whole package packs the cores that still run it with (D14)
    package)
      o="$HANDOFF/sdk"; rm -rf "$o"; mkdir -p "$o"
      cp "$HANDOFF"/trxapps/trx64-sdk-*.tar.gz "$HANDOFF"/trxapps/*.u2a "$HANDOFF/trxapps/coreapp.map" "$o/"
      (cd "$o" && sha256sum ./* > SHA256SUMS && cat SHA256SUMS)
      say "$(tar -xzOf "$o"/trx64-sdk-*.tar.gz --wildcards '*/VERSION' 2>/dev/null | head -3 | tr '\n' ' ')"
      s="$HANDOFF/shared-app"; rm -rf "$s"; mkdir -p "$s"
      cp "$HANDOFF/trxapps/coreapp.bin" "$HANDOFF/trxapps/coreapp.elf" "$s/"
      (cd "$s" && sha256sum coreapp.bin coreapp.elf) ;;
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
    # 3: what this push changed for the core, against the plan's base: its gateware (the files of its bitstream key without app/ and logo/: a partial core's
    # ci/lib.sh ooc_key, the sources of its core.toml; a large core's mono_key, ci/lib.sh mono_specs: the shared files without host/ and the sources of its
    # mono/core.toml; by git, so a change to comments alone counts here and not in the key), its TOML A, its app (cores/<id>/app). Not the plan's lane:
    # that one counts the app's files too (an app-only push plans it, and keeps the bitstream key)
    changed)
      [ -f "$HANDOFF/plan.json" ] || die "no plan.json from the dispatcher"
      base="$(python3 -c 'import json,sys; print((json.load(open(sys.argv[1])).get("base") or {}).get("commit") or "")' "$HANDOFF/plan.json")"
      specs=()
      kind="$(kind_of "$id")"   # an assignment: a die in kind_of ends the step (inside a test it would be swallowed)
      if [ "$kind" = large ]; then while IFS= read -r l; do specs+=("$l"); done < <(mono_specs "$id")
      else while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$ROOT/host/layers.txt" "core:$id"); fi
      [ "${#specs[@]}" -gt 0 ] || die "core:$id has no sources in its core.toml"
      gw=unknown ta=unknown ap=unknown
      if [ -n "$base" ] && git -C "$ROOT" cat-file -e "$base^{commit}" 2>/dev/null; then
        gw="$(differs "$base" "${specs[@]}" ':(exclude,glob)cores/*/app/**' ':(exclude,glob)cores/*/logo/**')"
        ta="$(differs "$base" "cores/$id/app/gateware.toml")"
        ap="$(differs "$base" "cores/$id/app")"
      fi
      printf '%s\n' "gateware=$gw" "toml_a=$ta" "app=$ap" > "$out/changed"
      say "the gateware: $gw, its TOML A: $ta, its app: $ap (base ${base:-none})" ;;
    # 4: has the gateware changed, does the app change with it. The TOML A against the RTL is not run here: tests/rtl_check runs once per push in the lint's tree
    # group (T264 S3: tools/rtl_check.py is an optional tool); T264's list compare takes this place.
    compare)
      [ -f "$out/changed" ] || die "no result of the step changed"
      gw="$(sed -n 's/^gateware=//p' "$out/changed")"; ta="$(sed -n 's/^toml_a=//p' "$out/changed")"
      if [ "$gw" = yes ] && [ "$ta" = no ]; then say "the gateware changed and its TOML A did not: the app is the same"; fi
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
    # 9 also runs the core's render build when it has one (T239.5 D13: its cases of the coreapp render bench, RENDER_APP_<CORE>, run in its lane alone; the shared bench builds none)
    ui)
      core_tests "$id" ui
      if [[ " $("$ROOT/tests/coreapp/render/run.sh" --own) " == *" $id "* ]]; then
        [ -d "${ULTIMATE_SW:-$ROOT/vendor/gideon/software}/lvgl/src" ] || die "no LVGL in this checkout: the render build of $id cannot run"
        say "the render build of $id (tests/coreapp/render/run.sh, RENDER_CORE=$id)"
        TRX_LVGL_CACHE="${TRX_LVGL_CACHE:-${CI_TRXAPPS_CACHE:-/cache/trxapps}/lvgl}" RENDER_CORE="$id" nice -n 15 "$ROOT/tests/coreapp/render/run.sh" "${TMPDIR:-/tmp}/lane-render-$id"
      fi ;;
    api)     core_tests "$id" api ;;
    # 11: the .cor - a partial core: its partial bound to the host it was routed against and its own app (ci/pack_one.sh); a large core: its flat bitstream
    # (the entry mono-<id>-<key> the lane bits:<id> restored) and its own app (tools/pack_large_cor.sh with its TOML B, T213.3 P4); the heap figures again, from
    # the packer. The app is packed as the old package stage packs every core app (ci/package.sh): a copy stamped with this run's version (tools/stamp_version.py,
    # ci/version.sh coreapp), since the SDK lane's base may come from its cache, built by an earlier commit (T172); its ELF is stamped alike (step 13 delivers it)
    package)
      [ -s "$out/app/$id.app" ] || die "no $id.app (step build)"
      kind="$(kind_of "$id")"   # an assignment: a die in kind_of ends the step (inside a test it would be swallowed)
      st="$out/stamped"; rm -rf "$st"; mkdir -p "$st"
      cp "$out/app/$id.app" "$out/app/$id.elf" "$st/"
      av="$("$ROOT/ci/version.sh" coreapp)"
      python3 -I "$ROOT/tools/stamp_version.py" stamp "$st/$id.app" "$st/$id.elf" --version "$av" || die "tools/stamp_version.py could not stamp $av into $id.app"
      say "the app stamped $av (it was built as $(python3 -I "$ROOT/tools/stamp_version.py" read "$out/app/$id.app"))"
      if [ "$kind" = large ]; then
        ent="$("$ROOT/ci/fetch_bits.sh" --names "large:$id")"
        bit="${CI_BIT_CACHE:?CI_BIT_CACHE}/$ent/${id}_mono.bit"
        [ -s "$bit" ] || die "no $ent/${id}_mono.bit among the restored bitstreams (the lane bits:$id)"
        mget() { TRX_CORE_DESC="$ROOT/cores/$id/mono/core.toml" python3 "$ROOT/tools/core_toml.py" get "$id" "$1"; }
        h="$(mget heap)"; ver="$("$ROOT/ci/version.sh" with "$(mget version)")"; name="$(mget name)"; short="$(mget short)"
        c="$(git -C "$ROOT" rev-parse --short=8 HEAD)"; mkdir -p "$out/cor"
        say "$ent and cores/$id/app (heap ${h:-0}: 0 = the region behind the image)"
        "$ROOT/tools/pack_large_cor.sh" --id "$id" --name "$name" --version "$ver" --short "$short" --bit "$bit" --app-bin "$st/$id.app" \
          --heap "${h:-0}" --sizes "$tomlb" --bit-commit "$c" --app-commit "$c" --out "$out/cor"
      else
        "$ROOT/ci/pack_one.sh" "$id" --app-dir "$st" --out "$out/cor"
      fi ;;
    # 13: the .cor, the app's ELF (the emulator's symbols; stamped as the app in the .cor, step 11) and the core's documents: its licence notice and the manual's
    # chapters on cores and ROMs
    deliver)
      [ -s "$out/cor/$id.cor" ] || die "no $id.cor (step package)"
      d="$HANDOFF/deliver-$id"; rm -rf "$d"; mkdir -p "$d/docs"
      cp "$out/cor/$id.cor" "$out/stamped/$id.elf" "$d/"
      [ ! -f "$ROOT/cores/$id/rtl/NOTICE" ] || cp "$ROOT/cores/$id/rtl/NOTICE" "$d/docs/NOTICE-$id"
      # T260 D6: the core's files for the SD card are a folder of the delivery, no record of the .cor: only a core whose TOML B sets [app] sd = "<folder>" delivers it (cores/<id>/<folder>),
      # as SD/cores/<id>/ here; any other folder of the core (RTL, constraints) is never taken
      sdf="$(python3 -I "$ROOT/tools/core_toml.py" sdk "$id" sd)"
      if [ -n "$sdf" ]; then
        [ -d "$ROOT/cores/$id/$sdf" ] || die "$id: [app] sd = \"$sdf\", but cores/$id/$sdf is no folder"
        mkdir -p "$d/SD/cores/$id"; cp -R "$ROOT/cores/$id/$sdf/." "$d/SD/cores/$id/"
      fi
      cp "$ROOT/docs/manual/11-cores.md" "$ROOT/docs/manual/12-roms.md" "$d/docs/"
      (cd "$d" && sha256sum "$id.cor" "$id.elf" $( [ ! -d SD ] || find SD -type f | sort ) | tee SHA256SUMS) ;;
    *) die "a core lane has no step $step" ;;
  esac ;;

bits:?*)
  id="${lane#bits:}"
  kind="$(kind_of "$id")"; target="core:$id"; [ "$kind" = dfx ] || target="large:$id"
  case "$step" in
    # 3: the content keys of the host and of the core, or of the large core's flat design (ci/keys.sh: the names of the cache entries and of the bit-<entry> releases)
    key)
      if [ "$kind" = large ]; then say "$id, a large core: its flat design with its own copy of the host"
      else say "the host (with the reference core $(layer_reference "$ROOT/host/layers.txt")) and $id:"; fi
      "$ROOT/ci/fetch_bits.sh" --names "$target" | sed 's/^/    /' ;;
    # 6, 11: the entries from the builds repository (the builder built the missing ones on the work order of the dispatcher)
    fetch) "$ROOT/ci/fetch_bits.sh" "$target" ;;
    # 7, 11: the entries restored from the builds repository (ci/fetch_bits.sh) and what the builder reported for them
    report)
      cache="${CI_BIT_CACHE:?CI_BIT_CACHE}"
      if [ "$kind" = large ]; then entries=("$cache"/mono-"$id"-*); else entries=("$cache"/host-* "$cache"/core-"$id"-*); fi
      for e in "${entries[@]}"; do
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

# D14 (Alex, 2026-11-24): the whole package with every core, as the old pipeline's package stage makes it. collect runs in the job that waits at the environment
# bitstreams, after ci/fetch_bits.sh restored every entry; package in the job after the lanes, with their artifacts in $HANDOFF/sdk, host, shared-app and
# lanes/deliver-<id> (the workflow downloads them there).
package)
  case "$step" in
    # 11: the entry names this package fetches (ci/fetch_bits.sh --names: every lane's and the imports'), into $HANDOFF/entry-names.txt (the job keys GitHub's cache
    # with them and hands them to ci/fetch_bits.sh, T239.1 D40). CI_IMPORTS=no (the T239 branch, alone): the import entries are left out, out loud - the builder names
    # them with integration's ci/imports.py, whose key hashes ci/imports.py itself, so the names this branch's copy makes are never published (T261 review 3.3)
    names)
      "$ROOT/ci/fetch_bits.sh" --names > "$HANDOFF/entry-names.all"
      if [ "${CI_IMPORTS:-yes}" = no ]; then
        grep -v '^imp-' "$HANDOFF/entry-names.all" > "$HANDOFF/entry-names.txt" || true
        say "CI_IMPORTS=no: left out $(grep '^imp-' "$HANDOFF/entry-names.all" | tr '\n' ' ')(no trx64u.cor, no trx64g.cor in this package)"
      else
        cp "$HANDOFF/entry-names.all" "$HANDOFF/entry-names.txt"
      fi
      rm -f "$HANDOFF/entry-names.all"
      [ -s "$HANDOFF/entry-names.txt" ] || die "ci/fetch_bits.sh --names named no entry"
      say "$(wc -l < "$HANDOFF/entry-names.txt" | tr -d ' ') entries" ;;
    # 11: the bitstream entries of every lane (host, each partial core, each large core) and the imports (trx64u.cor, trx64g.cor; not with CI_IMPORTS=no), from the
    # restored cache (ci/collect.sh)
    collect) "$ROOT/ci/child.sh" collect ;;
    # 11: the stage trxapps of the old pipeline put together from the lanes (13 of each: the artifacts sdk, host, shared-app), then ci/package.sh with the core
    # lanes' .cor (CI_LANE_COR); the job uploads $ROOT/out as the artifact `package` (13), the name the old pipeline's update, emu and prerelease read
    package)
      t="$HANDOFF/trxapps"; rm -rf "$t"; mkdir -p "$t"
      sdkt="$(sdk_tarball)"; [ -n "$sdkt" ] || die "no SDK package in $HANDOFF/sdk (the SDK lane's artifact sdk)"
      for f in "$sdkt" "$HANDOFF/sdk/unzip.u2a" "$HANDOFF/host/hostapp.app" "$HANDOFF/host/hostapp.elf" \
               "$HANDOFF/shared-app/coreapp.bin" "$HANDOFF/shared-app/coreapp.elf"; do
        [ -s "$f" ] || die "no ${f#"$HANDOFF"/} (the artifacts sdk, host and shared-app of the SDK and host menu lanes)"
        cp "$f" "$t/"
      done
      git -C "$ROOT" rev-parse HEAD > "$t/trxapps_commit"
      : > "$t/DONE"
      ls "$HANDOFF"/lanes/deliver-*/*.cor > /dev/null 2>&1 || die "no .cor of a core lane in $HANDOFF/lanes (the artifacts deliver-<id>)"
      say "the trxapps hand-over from the lanes: $(cd "$t" && ls | tr '\n' ' '); the core lanes' .cor: $(cd "$HANDOFF/lanes" && ls -- */*.cor | tr '\n' ' ')"
      CI_LANE_COR="$HANDOFF/lanes" "$ROOT/ci/child.sh" package ;;
    *) die "the whole package has no step $step" ;;
  esac ;;

*) die "unknown lane (sdk, host, core:<id>, bits:<id>, package)" ;;
esac
say "green"
