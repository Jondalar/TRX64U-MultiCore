# shellcheck shell=bash
# Sourced by ci/bitstream.sh stage c64u when host/layers.txt exists (T87).
#
# c64_basic is built in two layers, each cached under a content key (ci/lib.sh):
#   host-<H>/            the routed design with the reference core (static_routed.dcp), host.bit if the grey box worked, reports, manifest.json
#   core-<core>-<C>/     that core's partial and full image against that host, reports, manifest.json
# A host is built only when H is not in the cache; a core only when C is not, or when the entry's
# host_sha is not the host entry's (an evicted and rebuilt host never adopts old partials, S7).
# The REFERENCE core is not implemented again: the host build routes the whole design with it and
# its partial and full image come out of that run (run 400: re-routing the 78 % LUT C64 against the
# locked static diverged). Only the other cores run core.tcl. T100 D9: the plan (ci/plan.py, $HANDOFF/plan.json) names what changed;
# a target it does not name comes from the cache, and a cache miss of such a target is built and marked in plan_result.txt. Every
# Vivado run has the time limit of its kind (ci/targets.txt). A failed target (host, core, timeout) is RED, caches nothing (S10), and
# the others are still built (D9.6): with a red host the cores cannot be implemented and are red with it; the stage hands over what
# it has and ends red (ci/lib.sh stage_verdict).
# Shared with the caller: $ROOT, lib.sh, heavy_lock. build_layered fills the stage directory.

# vivado_filtered: the lines of Vivado's stdout that matter in the CI log.
vivado_filtered() {
  grep --line-buffered -E "^(ERROR|CRITICAL WARNING|===|Starting [A-Za-z ]+ Task|Phase [0-9]+ [A-Z])|^INFO: \[Route 35-|Number of Nodes with overlaps|Congested Area|^\|.*(Direction|NORTH|SOUTH|EAST|WEST)" || true
}

# wns_of <timing.rpt>: the worst setup slack of the summary, or "none".
wns_of() { local w; w="$({ grep -A2 'WNS(ns)' "$1" || true; } | sed -n 3p | awk '{print $1}')"; printf '%s' "${w:-none}"; }

# wns_fails <wns>: true when the slack is a number below zero.
wns_fails() { [ "$1" != none ] && awk -v w="$1" 'BEGIN { exit !(w < 0) }'; }

# worst_paths <timing.rpt>: the violated paths, short.
worst_paths() {
  grep -A14 "Slack (VIOLATED)" "$1" \
    | grep -E "Slack \(VIOLATED\)|Source:|Destination:|Path Group:|Requirement:|Data Path Delay:|Logic Levels:" | head -48 || true
}

# steps_of <vivado.log>: Vivado's own timer per command, elapsed.
steps_of() {
  grep -E '^(create_project|link_design|generate_target|synth_ip|synth_design|opt_design|place_design|phys_opt_design|route_design|write_checkpoint|write_bitstream|pr_verify|report_[a-z_]+): Time \(s\)' "$1" \
    | sed -E 's/^([a-z_]+): Time \(s\): cpu = ([0-9:.]+) ; elapsed = ([0-9:.]+).*/\1 \3 (cpu \2)/' || true
}

# entry_manifest <dir> key=value ...: manifest.json of a cache entry (strings, plus "files": sha256 of every file).
entry_manifest() {
  local dir="$1"; shift
  python3 - "$dir" "$@" <<'PY'
import hashlib, json, os, sys
d = sys.argv[1]
m = dict(a.split("=", 1) for a in sys.argv[2:])
files = {}
for f in sorted(os.listdir(d)):
    p = os.path.join(d, f)
    if os.path.isfile(p) and f not in ("manifest.json", "DONE"):
        files[f] = hashlib.sha256(open(p, "rb").read()).hexdigest()
m["files"] = files
json.dump(m, open(os.path.join(d, "manifest.json"), "w"), indent=2)
PY
}

# build_host <entry dir> <H> <part> <reference core> <ooc key of the reference core>
# Builds into a temporary directory and moves it into place atomically (cache_put).
build_host() {
  local entry="$1" H="$2" part="$3" ref="$4" okey="$5" d b tmp wns r
  d="$ROOT/host"; b="$d/build_host"; tmp="$(mktemp -d)"
  heavy_lock
  export HOME=/tmp/vivado-home TRX_CACHE="${TRX_CACHE:-/cache/multicore}"
  mkdir -p "$HOME" "$b"
  rm -f "${b:?}/routed.dcp" "${b:?}/host.bit" "${b:?}/vivado.rc"
  # TRX_CORE is the monolithic build's switch for the OTHER cores (build.tcl's second
  # configuration). In the layered flow each core has its own stage with its own cache entry, so
  # the host stage builds the static with the REFERENCE core only: empty here, or the host run
  # synthesizes and implements vc20, amiga and c264 a second time (run 804 did).
  # T84 D5b: the host's AXSS register carries the first 8 hex of the host key (host/pins.xdc), so
  # a .cor can name the host it belongs to and the boot app can read the running one back.
  (cd "$d" && vivado_limited host "$b/vivado.rc" env TRX_STAGE=host TRX_BUILD=build_host TRX_L2_KEY="$okey" TRX_CORE="" \
     TRX_HOST_KEY8="$(printf '%s' "$H" | cut -c1-8)" \
     vivado -mode batch -nojournal -log build_host/vivado.log -source build.tcl) | vivado_filtered
  if [ "$(cat "$b/vivado.rc" 2>/dev/null)" = 124 ]; then
    echo "host $H: stopped at the time limit"; BUILD_WHY="timeout (stopped at the limit of the host run)"
    rm -rf "${tmp:?}"; return 1
  fi
  if [ ! -f "$b/routed.dcp" ] || [ ! -f "$b/timing.rpt" ]; then
    BUILD_WHY="no routed.dcp"
    echo "host $H: no routed.dcp; post-synthesis utilization:"
    grep -E "^\| (Slice LUTs|LUT as Memory|Slice Registers|Block RAM Tile|DSPs|Bonded IOB) " "$b/utilization_synth.rpt" 2>/dev/null || true
    tail -40 "$b/vivado.log" 2>/dev/null || true
    rm -rf "${tmp:?}"; return 1
  fi
  wns="$(wns_of "$b/timing.rpt")"
  echo "WNS host $H (routed with reference core $ref): $wns ns"
  if wns_fails "$wns"; then
    BUILD_WHY="timing failed (WNS $wns ns)"
    echo "timing failed for the host (WNS $wns ns); worst paths:"
    worst_paths "$b/timing.rpt"
    cp "$b/timing.rpt" "$HANDOFF_OUT/host.timing.rpt"
    rm -rf "${tmp:?}"; return 1
  fi
  if [ ! -f "$b/c64_basic.bit" ] || [ ! -f "$b/c64_basic_pb_l2_partial.bit" ]; then
    BUILD_WHY="no image of the reference core"
    echo "host $H: the build wrote no full image / partial bitstream of the reference core $ref"; rm -rf "${tmp:?}"; return 1
  fi
  cp "$b/c64_basic.bit" "$tmp/${ref}_full.bit"
  cp "$b/c64_basic_pb_l2_partial.bit" "$tmp/${ref}_partial.bit"
  cp "$b/routed.dcp" "$tmp/static_routed.dcp"
  [ ! -f "$b/host.bit" ] || cp "$b/host.bit" "$tmp/"
  cp "$b/timing.rpt" "$tmp/host.timing.rpt"
  cp "$b/utilization.rpt" "$tmp/host.utilization.rpt"
  for r in utilization_hier utilization_synth utilization_hier_synth utilization_pb_l2 utilization_pb_l1_opt cdc qor_suggestions; do
    cp "$b/$r.rpt" "$tmp/host.$r.rpt" 2>/dev/null || true
  done
  gzip -c "$b/vivado.log" > "$tmp/host.vivado.log.gz" 2>/dev/null || true
  steps_of "$b/vivado.log" > "$tmp/host.steps.txt"
  echo "--- Vivado steps, host (elapsed)"; cat "$tmp/host.steps.txt"
  git -C "$ROOT" rev-parse --short=8 HEAD > "$tmp/from"
  entry_manifest "$tmp" host_key="$H" host_id="$H" host_sha="$(sha256sum "$tmp/$([ -f "$tmp/host.bit" ] && echo host.bit || echo static_routed.dcp)" | cut -d' ' -f1)" \
    from="$(cat "$tmp/from")" part="$part" reference_core="$ref" wns_ns="$wns" \
    vivado="$(vivado -version 2>/dev/null | head -1)" static_sha="$(sha256sum "$tmp/static_routed.dcp" | cut -d' ' -f1)"
  cache_put "$tmp" "$entry"
  : > "$entry/DONE"
  rm -rf "${tmp:?}"
}

# host_axss <host entry>: the host key the static of that entry really carries (the AXSS word of the reference core's full
# image), 8 hex. It is NOT always the first 8 hex of the entry's key: an entry that was re-keyed in place (ba2ebf33 moved every
# key; the entry host-08872197... became host-cee54845... with its files untouched) keeps the key it was built with. Every .cor
# and the host the boards run carry that one; the images of this run must carry it too.
host_axss() { python3 "$ROOT/tools/cor_pack.py" hostkey "$1/$(layer_reference "$ROOT/host/layers.txt")_full.bit"; }

# build_core <entry dir> <core> <C> <H> <part> <host entry> <ooc key>
build_core() {
  local entry="$1" core="$2" C="$3" H="$4" part="$5" host="$6" okey="$7" d b tmp wns f k8
  k8="$(host_axss "$host")" || { echo "core $core: the host entry $host has no AXSS host key in its full image"; return 1; }
  d="$ROOT/host"; b="$d/build_core_$core"; tmp="$(mktemp -d)"
  heavy_lock
  export HOME=/tmp/vivado-home TRX_CACHE="${TRX_CACHE:-/cache/multicore}"
  mkdir -p "$HOME" "$b"
  rm -f "${b:?}/${core}_partial.bit" "${b:?}/${core}_full.bit" "${b:?}/timing.rpt" "${b:?}/vivado.rc"
  # The property is in the host checkpoint already; the variable is passed for a core run that
  # writes its own full image, so every core's image carries the same host identity (T84 D5b).
  (cd "$d" && vivado_limited core "$b/vivado.rc" env T79_PLACE="${CORE_PLACE:-${T79_PLACE:-}}" T79_ROUTE="${CORE_ROUTE:-${T79_ROUTE:-}}" \
     TRX_HOST_DCP="$host/static_routed.dcp" TRX_CORE="$core" TRX_BUILD="build_core_$core" TRX_L2_KEY="$okey" \
     TRX_HOST_KEY8="$k8" \
     vivado -mode batch -nojournal -log "build_core_$core/vivado.log" -source core.tcl) | vivado_filtered
  if [ "$(cat "$b/vivado.rc" 2>/dev/null)" = 124 ]; then
    echo "core $core: stopped at the time limit"; BUILD_WHY="timeout (stopped at the limit of a core run)"
    rm -rf "${tmp:?}"; return 1
  fi
  # T95 5.16: a constraint whose objects do not resolve applies nothing, and Vivado goes on. It says so once per command
  # ("[Vivado 12-4739] ... No valid object(s) found") and then places and routes a design that is timed as if the core had
  # asked for nothing - run 43 reached WNS -8.969 ns that way and nobody would have known why. No core of ours has such a
  # line (checked on the trx64 and amiga logs of run 41), so one is a failure, not a note.
  local unresolved
  unresolved="$(grep -cE "^(CRITICAL )?WARNING: \[Vivado 12-4739\]" "$b/vivado.log" 2>/dev/null || true)"   # message lines only: the echoed source of host/check/trx_constraints.tcl names the id too (run 1137)
  if [ "${unresolved:-0}" -gt 0 ]; then
    echo "core $core: $unresolved constraint(s) resolved to no object - the core's own timing script applied nothing:"
    grep -m 2 -E "^(CRITICAL )?WARNING: \[Vivado 12-4739\]" "$b/vivado.log" || true
    rm -rf "${tmp:?}"; return 1
  fi
  if [ ! -f "$b/timing.rpt" ]; then
    echo "core $core: no timing report (the script stopped early); log tail:"; tail -40 "$b/vivado.log" 2>/dev/null || true
    rm -rf "${tmp:?}"; return 1
  fi
  wns="$(wns_of "$b/timing.rpt")"
  echo "WNS core $core: $wns ns"
  cp "$b/timing.rpt" "$HANDOFF_OUT/$core.timing.rpt"
  if wns_fails "$wns"; then
    echo "timing failed for core $core (WNS $wns ns); worst paths:"
    worst_paths "$b/timing.rpt"
    rm -rf "${tmp:?}"; return 1
  fi
  if [ ! -f "$b/${core}_partial.bit" ] || [ ! -f "$b/${core}_full.bit" ] || ! grep -q "pr_verify passed" "$b/vivado.log"; then
    echo "core $core: partial/full bitstream or pr_verify result missing"; tail -40 "$b/vivado.log" 2>/dev/null || true
    rm -rf "${tmp:?}"; return 1
  fi
  # T123: the image of the flash filler is the host update file's bitstream - it must carry this host's key, and the static part of
  # it must be the host the reference core was routed with (S3). Any other core is only a partial and needs neither.
  if [ "$(python3 "$ROOT/tools/core_toml.py" get "$core" flash_filler)" = true ]; then
    local axss
    axss="$(python3 "$ROOT/tools/cor_pack.py" hostkey "$b/${core}_full.bit" 2>&1 || true)"
    if [ "$axss" != "$k8" ]; then
      echo "core $core: ${core}_full.bit carries host key '$axss', the host's static carries '$k8'"; rm -rf "${tmp:?}"; return 1
    fi
    echo "core $core: AXSS host key $axss"
    if ! "$ROOT/tools/static_diff.sh" "$host/static_routed.dcp" "$b/core_routed.dcp" "$b/static_diff"; then
      echo "core $core: the static part differs from the host's (tools/static_diff.sh)"; rm -rf "${tmp:?}"; return 1
    fi
    cp "$b/static_diff/static_diff.txt" "$tmp/$core.static_diff.rpt"
  fi
  cp "$b/${core}_partial.bit" "$b/${core}_full.bit" "$tmp/"
  cp "$b/timing.rpt" "$tmp/$core.timing.rpt"
  for f in utilization utilization_pb_l2 utilization_hier route_status pr_verify timing_neg; do
    cp "$b/$f.rpt" "$tmp/$core.$f.rpt" 2>/dev/null || true
  done
  gzip -c "$b/vivado.log" > "$tmp/$core.vivado.log.gz" 2>/dev/null || true
  steps_of "$b/vivado.log" > "$tmp/$core.steps.txt"
  echo "--- Vivado steps, core $core (elapsed)"; cat "$tmp/$core.steps.txt"
  git -C "$ROOT" rev-parse --short=8 HEAD > "$tmp/from"
  entry_manifest "$tmp" core="$core" core_key="$C" host_key="$H" host_sha="$(manifest_get "$host/manifest.json" host_sha)" \
    ooc_key="$okey" partial_sha256="$(sha256sum "$tmp/${core}_partial.bit" | cut -d' ' -f1)" \
    from="$(cat "$tmp/from")" part="$part" wns_ns="$wns"
  cache_put "$tmp" "$entry"
  : > "$entry/DONE"
  rm -rf "${tmp:?}"
}

# derive_ref_core <entry dir> <core> <C> <H> <part> <host entry> <ooc key>: the reference core's entry from the host
# entry (partial, full image, reports and the WNS of the host run), nothing implemented.
derive_ref_core() {
  local entry="$1" core="$2" C="$3" H="$4" part="$5" host="$6" okey="$7" tmp
  tmp="$(mktemp -d)"
  cp "$host/${core}_partial.bit" "$host/${core}_full.bit" "$tmp/"
  cp "$host/host.timing.rpt" "$tmp/$core.timing.rpt"
  cp "$host/host.utilization.rpt" "$tmp/$core.utilization.rpt"
  cp "$host/host.utilization_pb_l2.rpt" "$tmp/$core.utilization_pb_l2.rpt" 2>/dev/null || true
  cp "$host/host.steps.txt" "$tmp/$core.steps.txt"
  cp "$host/host.vivado.log.gz" "$tmp/$core.vivado.log.gz" 2>/dev/null || true
  cp "$host/from" "$tmp/from"
  entry_manifest "$tmp" core="$core" core_key="$C" host_key="$H" host_sha="$(manifest_get "$host/manifest.json" host_sha)" \
    ooc_key="$okey" partial_sha256="$(sha256sum "$tmp/${core}_partial.bit" | cut -d' ' -f1)" \
    from="$(cat "$tmp/from")" part="$part" wns_ns="$(manifest_get "$host/manifest.json" wns_ns)" derived_from_host=1
  cache_put "$tmp" "$entry"
  : > "$entry/DONE"
  rm -rf "${tmp:?}"
}

# core_entry_ok <core entry> <host entry> [core]: the core entry is complete and was routed against this very host build. For
# the flash filler (core named) its full image also carries the host key of that host's static: an entry built before the
# key was taken from the static (CI 957) fails here and is built again.
core_entry_ok() {
  [ -f "$1/DONE" ] && [ "$(manifest_get "$1/manifest.json" host_sha)" = "$(manifest_get "$2/manifest.json" host_sha)" ] || return 1
  [ -n "${3:-}" ] && [ "$(python3 "$ROOT/tools/core_toml.py" get "$3" flash_filler)" = true ] || return 0
  [ "$(python3 "$ROOT/tools/cor_pack.py" hostkey "$1/${3}_full.bit" 2>/dev/null)" = "$(host_axss "$2")" ]
}

# build_layered <stage dir> <part> [lane]: host, then every core of layers.txt; fills the stage directory. Returns 0 even when targets are
# red (they are in $RED_DIR, ci/lib.sh); the caller ends with stage_verdict. Returns 1 only for a broken setup.
# lane (T100 D10, ci/bitstream.sh --lane): "" = everything, as before (stage bit-c64u); "host" = the host and the reference core (its
# partial and full image come out of the host run; stage bit-host); "core:<id>" = that one DFX core against the host entry of the cache
# (stage bit-<id>; the host lane made the entry, no entry means the host is red and this core is red with it). The reds of a lane are
# filed under its stage name. layers.json of a lane holds only its part (ci/package.sh merges them); only the whole stage and the host
# lane run the cache janitor, so lanes in parallel never run it twice.
build_layered() {
  local out="$1" part="$2" lane="${3:-}" lf="$ROOT/host/layers.txt" cache ref H hostdir core O C coredir failed="" ldir named why host_ok=1
  local sn=bit-c64u todo mk_host=1 mk_gc=1
  HANDOFF_OUT="$out"
  cache="${CI_BIT_CACHE:-/cache/multicore/bit}"
  ref="$(layer_reference "$lf")"
  [ -n "$ref" ] || { echo "layers.txt names no reference core" >&2; return 1; }
  todo="$(layer_cores "$lf")"
  case "$lane" in
    "") ;;
    host) sn=bit-host; todo="$ref" ;;
    core:?*)
      core="${lane#core:}"
      if [ "$core" = "$ref" ] || ! printf '%s\n' "$todo" | grep -qxF "$core"; then
        echo "lane $lane: not a DFX core of host/layers.txt (the reference core belongs to the host lane)" >&2; return 1
      fi
      sn="bit-$core"; todo="$core"; mk_host=0; mk_gc=0 ;;
    *) echo "unknown lane $lane" >&2; return 1 ;;
  esac
  red_reset "$sn"
  H="$(host_key "$part")"
  hostdir="$cache/host-$H"
  named=""; if plan_has host || plan_has lane:host; then named=1; fi
  if [ "$mk_host" = 0 ]; then
    if [ -f "$hostdir/DONE" ]; then
      echo "lane $lane: host entry $H found in the cache (built from $(cat "$hostdir/from"))"
      pin_entry "$hostdir" host
      ldir="$(mktemp -d)"
    else
      echo "lane $lane: there is no host entry $H in the cache - the host lane failed (or did not run)"
      host_ok=0; : > "$out/wns.txt"
    fi
    echo "$H" > "$out/key"
  else
  echo "::group::host $H"
  if [ -f "$hostdir/DONE" ]; then
    echo "host cache hit $H (built from $(cat "$hostdir/from"), host_sha $(manifest_get "$hostdir/manifest.json" host_sha | cut -c1-16)); no host synthesis, placement or routing"
    plan_note "$out" host "cache hit$([ -n "$named" ] || echo ' (not named by the plan)')"
  elif [ -n "${CI_COLLECT_ONLY:-}" ]; then
    # collect only reads the cache (Alex 2026-11-08: packaging never starts Vivado): a missing entry is red, never built here
    echo "host $H is not in the cache: NOT built (collect only reads; the vivado child of the host builds it)"
    mark_red "$sn" host "not in the cache (collect builds nothing)"; plan_note "$out" host "RED: not in the cache"; host_ok=0
  else
    if [ -n "$named" ]; then echo "host cache miss $H: building the static (the plan names it)"; plan_note "$out" host "building (named)"
    else echo "host cache miss $H: the plan did NOT name the host; building it (cache miss, marked)"; plan_note "$out" host "cache miss of an unnamed target: building"; fi
    BUILD_WHY=""
    if ! build_host "$hostdir" "$H" "$part" "$ref" "$(ooc_key "$part" "$ref")"; then
      why="${BUILD_WHY:-build failed (see the lines above)}"
      mark_red "$sn" host "$why"; plan_note "$out" host "RED: $why"; host_ok=0
    fi
  fi
  if [ "$host_ok" = 1 ]; then
    pin_entry "$hostdir" host
    echo "::endgroup::"
    cp "$hostdir/host.steps.txt" "$hostdir/host.vivado.log.gz" "$out/"
    [ ! -f "$hostdir/host.bit" ] || cp "$hostdir/host.bit" "$out/"
    cp "$hostdir"/host.*.rpt "$out/"
    echo "$H" > "$out/key"
    echo "host $(manifest_get "$hostdir/manifest.json" wns_ns)" >> "$out/wns.txt"
    ldir="$(mktemp -d)"
    cp "$hostdir/manifest.json" "$ldir/host.json"
  else
    echo "::endgroup::"
    echo "$H" > "$out/key"
    : > "$out/wns.txt"
  fi
  fi

  for core in $todo; do
    # A core whose layer-2 recipe does not exist yet (its machine is still being brought up, e.g.
    # T88's Atari) has a descriptor so the lint and the caches know it, but nothing to synthesize.
    if [ ! -f "$ROOT/cores/$core/core_sources.tcl" ]; then
      echo "core $core has no core_sources.tcl yet: skipped" >&2
      continue
    fi
    if [ "$host_ok" != 1 ]; then
      # no host, no implementation against it: red with the host, so that nobody reads a missing .cor as "unchanged"
      mark_red "$sn" "$core" "not built: the host is red"; plan_note "$out" "dfx:$core" "RED: the host is red"; failed="$failed $core"
      continue
    fi
    O="$(ooc_key "$part" "$core")"
    # a core may name its own place/route directives (core.toml place, route); they go into its key and its Vivado run only
    CORE_PLACE="$(python3 "$ROOT/tools/core_toml.py" get "$core" place)"; CORE_PLACE="${CORE_PLACE:-${T79_PLACE:-}}"
    CORE_ROUTE="$(python3 "$ROOT/tools/core_toml.py" get "$core" route)"; CORE_ROUTE="${CORE_ROUTE:-${T79_ROUTE:-}}"
    C="$(T79_PLACE="$CORE_PLACE" T79_ROUTE="$CORE_ROUTE" core_key "$H" "$O")"
    coredir="$cache/core-$core-$C"
    named=""; if [ "$core" = "$ref" ] || plan_has "dfx:$core" || plan_has "lane:dfx:$core"; then named=1; fi
    echo "::group::core $core $C"
    if core_entry_ok "$coredir" "$hostdir" "$core"; then
      echo "core cache hit $core $C (host $H)"
      plan_note "$out" "dfx:$core" "cache hit$([ -n "$named" ] || echo ' (not named by the plan)')"
    else
      [ ! -f "$coredir/DONE" ] || echo "core entry $C was routed against another host build (host_sha or host key differ): implementing again"
      if [ "$core" = "$ref" ]; then
        echo "core $core is the reference core: its partial and full image are the host build's"
        derive_ref_core "$coredir" "$core" "$C" "$H" "$part" "$hostdir" "$O"
      elif [ -n "${CI_COLLECT_ONLY:-}" ]; then
        echo "core $core $C is not in the cache: NOT built (collect only reads)"; failed="$failed $core"; echo "::endgroup::"
        mark_red "$sn" "$core" "not in the cache (collect builds nothing)"; plan_note "$out" "dfx:$core" "RED: not in the cache"; continue
      else
        if [ -n "$named" ]; then echo "core cache miss $core $C: implementing the partial against host $H"; plan_note "$out" "dfx:$core" "building (named)"
        else echo "core cache miss $core $C: the plan did NOT name $core; building it (cache miss, marked)"; plan_note "$out" "dfx:$core" "cache miss of an unnamed target: building"; fi
        BUILD_WHY=""
        if ! build_core "$coredir" "$core" "$C" "$H" "$part" "$hostdir" "$O"; then
          why="${BUILD_WHY:-build failed (see the lines above)}"
          echo "core $core FAILED (nothing cached for it)"; failed="$failed $core"; echo "::endgroup::"
          mark_red "$sn" "$core" "$why"; plan_note "$out" "dfx:$core" "RED: $why"; continue
        fi
      fi
    fi
    pin_entry "$coredir" "core-$core"
    echo "::endgroup::"
    cp "$coredir/${core}_partial.bit" "$coredir/${core}_full.bit" "$coredir/$core.steps.txt" "$out/"
    cp "$coredir/$core.vivado.log.gz" "$out/" 2>/dev/null || true
    cp "$coredir/$core".*.rpt "$out/"
    echo "$core $(manifest_get "$coredir/manifest.json" wns_ns)" >> "$out/wns.txt"
    cp "$coredir/manifest.json" "$ldir/core-$core.json"
    if [ "$core" = "$ref" ]; then
      # the names the package, the update files and tools/load.sh read: the reference core's image
      cp "$coredir/${core}_full.bit" "$out/c64_basic.bit"
      cp "$coredir/${core}_partial.bit" "$out/c64_basic_pb_l2_partial.bit"
      cp "$coredir/$core.timing.rpt" "$out/c64_basic.timing.rpt"
      cp "$coredir/$core.utilization.rpt" "$out/c64_basic.utilization.rpt"
      echo "c64_basic $(manifest_get "$coredir/manifest.json" wns_ns)" >> "$out/wns.txt"
    fi
  done
  if [ "$host_ok" = 1 ]; then
    python3 - "$ldir" "$out/layers.json" <<'PY'
import glob, json, os, sys
d, dst = sys.argv[1:]
cores = {os.path.basename(p)[5:-5]: json.load(open(p)) for p in sorted(glob.glob(os.path.join(d, "core-*.json")))}
out = {"cores": {n: {k: c[k] for k in ("core_key", "host_sha", "partial_sha256", "from", "wns_ns")} for n, c in cores.items()}}
if os.path.exists(os.path.join(d, "host.json")):
    h = json.load(open(os.path.join(d, "host.json")))
    out = {"host": {k: h[k] for k in ("host_key", "host_id", "host_sha", "from", "wns_ns")}, **out}
json.dump(out, open(dst, "w"), indent=2)
PY
    rm -rf "${ldir:?}"
    if [ "$mk_host" = 1 ]; then
      echo "layers: host from $(cat "$hostdir/from"), core $ref from $(cat "$cache/core-$ref-$(core_key "$H" "$(ooc_key "$part" "$ref")")/from")" > "$out/from"
    else
      echo "lane $lane: against host $H (built from $(cat "$hostdir/from"))" > "$out/from"
    fi
  else
    echo "layers: host RED, nothing built from it" > "$out/from"
  fi
  # a core is a red target when it failed - experimental or not (T100 D9.6 replaces the T94 D8 rule: an experimental core that
  # fails makes the run red too, and no other target is stopped by it)
  [ -z "$failed" ] || echo "cores that failed or were not built:$failed" >&2
  if [ "$mk_host" = 1 ] && [ "$host_ok" = 1 ] && [ ! -f "$out/c64_basic.bit" ]; then
    mark_red "$sn" "$ref" "the reference core produced no c64_basic.bit"
  fi
  ls "$out"/*.bit >/dev/null 2>&1 && (cd "$out" && sha256sum ./*.bit | sed 's| \./| |' > SHA256SUMS)
  # janitor: pins, history and size cap (T87 D14); the heavy lock keeps it from racing a build
  if [ "$mk_gc" = 1 ]; then
    cache_gc_if_free
  fi
  return 0
}
