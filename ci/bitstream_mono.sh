#!/usr/bin/env bash
# Stage `bit-mono`: the full bitstream of every core with delivery = "cfw" in its core.toml (T113 Q12, T115), in vivado-env:2024.1.
#
#   ci/bitstream_mono.sh [core ...]      the named cores, or every core with delivery = "cfw"
#   ci/bitstream_mono.sh --lane <core>   one lane (T100 D10): that core only, stage dir large-<core>, reds filed as large-<core>. The lane runs
#                                        when the plan says so (app or the core's own sources); when it did not fire, the entry is handed over
#                                        from the cache and a core without any entry is built and marked, as for every unnamed target.
#                                        No cache janitor in a lane (the host lane runs it once).
#
# Such a core does not fit layer 2 and is not implemented against the host (ci/bitstream_layers.sh skips it): host and core are
# ONE flat design, built by cores/<core>/mono/build_mono.tcl from the core's own copy of the host files (cores/<core>/mono/host, T164 3a Q1); host/ is only Vivado's
# working directory (output, relative paths), nothing under host/ edited and no file of it read.
# The variant is the core's mono_env (tools/core_toml.py env <core>). The stage hands over, per core:
#   <core>_mono.bit  the bitstream            <core>_mono.timing.rpt, .utilization.rpt, .route_status.rpt, .steps.txt, .vivado.log.gz
# and wns.txt ("<core>_mono <wns>"). ci/package.sh puts them into the package and makes the large <core>.cor out of the bit
# and coreapp.bin (tools/pack_large_cor.sh, T115 D9).
#
# Cached under /cache/multicore/bit/mono-<core>-<key> (mono_key in ci/lib.sh): a push that leaves the core (its copy of the host files included), the shared files it
# reads and its variant alone gets the bitstream of the last good build; a host/ change does not touch the key. WNS < 0 is not a good build: it fails the core and caches nothing.
# T100 D9: the plan (ci/plan.py) names the large cores whose sources changed. A core it does not name comes from the cache: the entry
# of the current key, else the newest entry of that core (a change of a shared file moves the key of every flat build; a core that is not named keeps the build of its
# last change, loudly), and only a core without any entry is built and marked. Every flat build has the time limit of a large core (ci/targets.txt). A failed core (timing, no bitstream, timeout)
# is RED, experimental or not (D9.6); the other cores are still built, and the stage ends red (ci/lib.sh stage_verdict).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

part=xc7a50tfgg484-1
stage=bit-mono; lane=""
if [ "${1:-}" = --lane ]; then lane="${2:?--lane <core>}"; stage="large-$lane"; shift 2; set -- "$lane"; fi
out="$(stage_dir "$stage")"
cores=("$@")
if [ "${#cores[@]}" -eq 0 ]; then
  while IFS= read -r c; do [ -z "$c" ] || cores+=("$c"); done < <(python3 "$ROOT/tools/core_toml.py" list cfw)
fi
if [ "${#cores[@]}" -eq 0 ]; then
  echo "no core has delivery = \"cfw\": nothing to build"
  : > "$out/DONE"
  exit 0
fi
[ -z "$lane" ] || { if plan_has "lane:large:$lane"; then echo "=== lane $lane: in the plan"; else echo "=== lane $lane: not in the plan (no trigger fired): the entry is handed over from the cache"; fi; }
source "$ROOT/ci/bitstream_layers.sh"          # vivado_filtered, wns_of, wns_fails, worst_paths, steps_of, entry_manifest

cache="${CI_BIT_CACHE:-/cache/multicore/bit}"
failed=""
red_reset "$stage"
for core in "${cores[@]}"; do
  [ "$(python3 "$ROOT/tools/core_toml.py" get "$core" delivery)" = cfw ] || { echo "$core: delivery is not \"cfw\"" >&2; exit 2; }
  [ -f "$ROOT/cores/$core/mono/build_mono.tcl" ] || { echo "cores/$core/mono/build_mono.tcl is missing" >&2; exit 2; }
  K="$(mono_key "$part" "$core")"
  entry="$cache/mono-$core-$K"
  named=""; if plan_has "large:$core" || plan_has "lane:large:$core"; then named=1; fi
  echo "::group::mono $core $K"
  if [ ! -f "$entry/DONE" ] && [ -z "$named" ]; then
    # not named: the newest entry of this core instead of a build (a moved key, but the plan did not name the core)
    old="$(ls -td "$cache/mono-$core"-*/ 2>/dev/null | while read -r d; do [ -f "${d}DONE" ] && { echo "${d%/}"; break; }; done || true)"
    if [ -n "$old" ]; then
      echo "mono $core: the plan did not name it and the key $K has no entry: using the newest one, $old (built from $(cat "$old/from")). NOT the current key: the core's files or a shared file moved since."
      plan_note "$out" "large:$core" "from the cache, older key $(basename "$old") (not named by the plan)"
      entry="$old"
    fi
  fi
  if [ -f "$entry/DONE" ]; then
    echo "mono cache hit $core $K (built from $(cat "$entry/from")); no synthesis, placement or routing"
    case "$entry" in *"-$K") plan_note "$out" "large:$core" "cache hit$([ -n "$named" ] || echo ' (not named by the plan)')" ;; esac
  elif [ -n "${CI_COLLECT_ONLY:-}" ]; then
    echo "mono $core $K is not in the cache: NOT built (collect only reads)"
    echo "::endgroup::"; failed="$failed $core"; mark_red "$stage" "$core" "not in the cache (collect builds nothing)"; plan_note "$out" "large:$core" "RED: not in the cache"; continue
  else
    if [ -n "$named" ]; then plan_note "$out" "large:$core" "building (named)"; else plan_note "$out" "large:$core" "cache miss of an unnamed target: building"; fi
    echo "mono cache miss $core $K: building the flat design"
    BUILD_WHY=""
    heavy_lock required
    export HOME=/tmp/vivado-home TRX_CACHE="${TRX_CACHE:-/cache/multicore}"
    mkdir -p "$HOME"
    b="$ROOT/host/build_mono_$core"
    rm -rf "${b:?}"; mkdir -p "$b"            # Vivado opens its log before the script runs: the directory has to exist
    envs=()
    while IFS= read -r e; do [ -z "$e" ] || envs+=("$e"); done < <(python3 "$ROOT/tools/core_toml.py" env "$core")
    # the script is read from cores/<core>/mono/, Vivado runs in host/ (the paths of host/pins.xdc and the host sources are relative to it)
    (cd "$ROOT/host" && vivado_limited large "$b/vivado.rc" env TRX_BUILD="build_mono_$core" TRX_STOP=route ${envs[@]+"${envs[@]}"} \
       vivado -mode batch -nojournal -log "build_mono_$core/vivado.log" -source "../cores/$core/mono/build_mono.tcl") | vivado_filtered
    bit="$b/${core}_mono.bit"
    if [ "$(cat "$b/vivado.rc" 2>/dev/null)" = 124 ]; then
      echo "mono $core: stopped at the time limit"
      echo "::endgroup::"; failed="$failed $core"; mark_red "$stage" "$core" "timeout (stopped at the limit of a large core)"; plan_note "$out" "large:$core" "RED: timeout"; continue
    fi
    if [ ! -f "$bit" ] || [ ! -f "$b/timing.rpt" ]; then
      echo "mono $core: no bitstream; post-synthesis utilization:"
      grep -E "^\| (Slice LUTs|LUT as Memory|Slice Registers|Block RAM Tile|DSPs|Bonded IOB) " "$b/utilization_synth.rpt" 2>/dev/null || true
      tail -40 "$b/vivado.log" 2>/dev/null || true
      echo "::endgroup::"; failed="$failed $core"; mark_red "$stage" "$core" "no bitstream"; plan_note "$out" "large:$core" "RED: no bitstream"; continue
    fi
    wns="$(wns_of "$b/timing.rpt")"
    echo "WNS mono $core: $wns ns"
    cp "$b/timing.rpt" "$out/${core}_mono.timing.rpt"
    if wns_fails "$wns"; then
      echo "timing failed for mono $core (WNS $wns ns); worst paths:"
      worst_paths "$b/timing.rpt"
      echo "::endgroup::"; failed="$failed $core"; mark_red "$stage" "$core" "timing failed (WNS $wns ns)"; plan_note "$out" "large:$core" "RED: timing"; continue
    fi
    tmp="$(mktemp -d)"
    cp "$bit" "$tmp/${core}_mono.bit"
    cp "$b/timing.rpt" "$tmp/${core}_mono.timing.rpt"
    for r in utilization route_status utilization_hier; do cp "$b/$r.rpt" "$tmp/${core}_mono.$r.rpt" 2>/dev/null || true; done
    gzip -c "$b/vivado.log" > "$tmp/${core}_mono.vivado.log.gz" 2>/dev/null || true
    steps_of "$b/vivado.log" > "$tmp/${core}_mono.steps.txt"
    echo "--- Vivado steps, mono $core (elapsed)"; cat "$tmp/${core}_mono.steps.txt"
    git -C "$ROOT" rev-parse --short=8 HEAD > "$tmp/from"
    entry_manifest "$tmp" core="$core" mono_key="$K" part="$part" wns_ns="$wns" from="$(cat "$tmp/from")" \
      bit_sha256="$(sha256sum "$tmp/${core}_mono.bit" | cut -d' ' -f1)" vivado="$(vivado -version 2>/dev/null | head -1)"
    cache_put "$tmp" "$entry"
    : > "$entry/DONE"
    rm -rf "${tmp:?}"
  fi
  pin_entry "$entry" "mono-$core"
  echo "::endgroup::"
  cp "$entry/${core}_mono.bit" "$out/"
  cp "$entry/${core}_mono".*.rpt "$entry/${core}_mono.steps.txt" "$out/" 2>/dev/null || true
  cp "$entry/${core}_mono.vivado.log.gz" "$out/" 2>/dev/null || true
  echo "${core}_mono $(manifest_get "$entry/manifest.json" wns_ns)" >> "$out/wns.txt"
done
echo "$(git -C "$ROOT" rev-parse --short=8 HEAD)" > "$out/from"
if ls "$out"/*.bit >/dev/null 2>&1; then
  (cd "$out" && sha256sum ./*.bit | sed 's| \./| |' > SHA256SUMS)
fi
[ -z "$failed" ] || echo "mono builds that failed:$failed" >&2
if [ -z "$lane" ]; then
  cache_gc_if_free
fi
ls -l "$out"
: > "$out/DONE"
echo "=== $stage done in $(lap)"
stage_verdict "$stage" || exit 1
