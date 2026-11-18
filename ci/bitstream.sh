#!/usr/bin/env bash
# Stages `bit-c64u` and `bit-ue2`: Vivado builds, in vivado-env:2024.1.
#
#   ci/bitstream.sh c64u    every designs/*/build.tcl, XC7A50T (the C64 Ultimate)
#   ci/bitstream.sh ue2     host for the XC7A100T (Ultimate 64 Elite II)
#   ci/bitstream.sh c64u --lane host        one lane (T100 D10): the host routed with the reference core, plus ddr2_test / hdmi_test; stage dir bit-host
#   ci/bitstream.sh c64u --lane core:<id>   one lane: that DFX core against the host of the cache (the host lane made it); stage dir bit-<id>
# A lane runs in its own job (pipeline.yml). The plan says whether its triggers fired (ci/plan.py lanes_of); a lane that did not fire builds
# nothing it can take from the cache ("not in the plan: handing over from the cache") - the package is published whole and needs its files -
# and a cache miss is built and marked, as for every unnamed target.
#
# A bitstream is built only when something that goes into it changed
# (gateware_key in lib.sh): a push that only moves the app, the docs, the
# benches or the CI gets the bitstream of the last good build with the same key
# from /cache/bit/. A bitstream with failed timing is not a good build: WNS < 0
# fails the stage and is not cached.
#
# Layered tree (host/layers.txt, T87): c64_basic is built as a host entry (the
# routed static) and one entry per core, each cached under its own key (ci/bitstream_layers.sh);
# ddr2_test and hdmi_test then have a key of their own (aux-<key>). Without layers.txt: as above.
#
# T100 D9: ci/plan.py names what this push changed; a target the plan does not name comes from the cache (a miss is built and
# marked), every Vivado run has the time limit of its kind, and a red target (host, core, aux design) does not stop the others.
# bit-ue2 runs after `package`, only when asked for (ci/select.sh ue2: a dispatch
# with ue2 set, since 2026-11-18 never a tag alone), and publishes its own files (_100t) into
# the package versions `package` made. It cleans up the run's hand-over directory.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

board="${1:?c64u or ue2}"
lane=""
if [ "${2:-}" = --lane ]; then lane="${3:?--lane host|core:<id>}"; fi
case "$lane" in ""|host|core:?*) ;; *) echo "unknown lane $lane (host or core:<id>)" >&2; exit 2 ;; esac
[ -z "$lane" ] || [ "$board" = c64u ] || { echo "--lane is for c64u" >&2; exit 2; }
corelane=""
case "$lane" in "") stage="bit-$board" ;; host) stage="bit-host" ;; core:*) stage="bit-${lane#core:}"; corelane=1 ;; esac
case "$board" in
  c64u) part=xc7a50tfgg484-1; suffix=""; designs=("$ROOT"/designs/*/build.tcl) ;;
  ue2)  part=xc7a100tfgg484-1; suffix="_100t"; designs=("$ROOT/host/build.tcl")
        export TRX_PART="$part" TRX_BUILD=build_100t ;;
  *) echo "unknown board $board" >&2; exit 2 ;;
esac
out="$(stage_dir "$stage")"

if [ "$board" = ue2 ] && [ "$("$ROOT/ci/select.sh" ue2)" != yes ]; then
  echo "bit-ue2 skipped: not asked for (only a dispatch with ue2 asks; ci/select.sh)"
  rm -rf "${HANDOFF:?}"
  exit 0
fi

if [ -n "$lane" ]; then
  [ -f "$ROOT/host/layers.txt" ] || { echo "lanes need host/layers.txt" >&2; exit 2; }
  if plan_has "lane:${lane/core:/dfx:}"; then echo "=== lane $lane: in the plan"
  else echo "=== lane $lane: not in the plan (no trigger fired): nothing is built that the cache has; its files are handed over from the cache"; fi
fi
layered=0
if [ "$board" = c64u ] && [ -z "$corelane" ] && [ -f "$ROOT/host/layers.txt" ]; then
  layered=1
  designs=("$ROOT"/designs/ddr2_test/build.tcl "$ROOT"/designs/hdmi_test/build.tcl)
  key="$(aux_key "$part")"
  cache="${CI_BIT_CACHE:-/cache/multicore/bit}/aux-$key"
  echo "aux key $key (ddr2_test, hdmi_test; c64_basic is layered)"
elif [ -z "$lane" ]; then
  key="$(gateware_key "$part")"
  cache="${CI_BIT_CACHE:-/cache/multicore/bit}/$board-$key"
  echo "gateware key $key ($part)"
fi
aux_failed=""
red_reset "$stage"
[ -n "$corelane" ] || echo "$key" > "$out/key"
if [ -n "$corelane" ]; then
  : # a DFX core lane builds no aux design (the host lane does)
elif [ -f "$cache/DONE" ]; then
  [ "$layered" != 1 ] || plan_note "$out" aux "cache hit$(plan_has aux || echo ' (not named by the plan)')"
  echo "=== $board: nothing that goes into the bitstream changed since $(cat "$cache/from"); using $cache"
  find "$cache" -maxdepth 1 -type f -exec cp {} "$out/" \;   # (pins/ stays in the cache)
  echo "cache $(cat "$cache/from")" > "$out/from"
elif [ -n "${CI_COLLECT_ONLY:-}" ]; then
  # collect only reads the cache (Alex 2026-11-08): the aux designs are not built here
  echo "=== $board: the aux designs are not in the cache: NOT built (collect only reads)"
  aux_failed=1; mark_red "$stage" aux "not in the cache (collect builds nothing)"; [ "$layered" != 1 ] || plan_note "$out" aux "RED: not in the cache"
else
  heavy_lock
  export HOME=/tmp/vivado-home
  export TRX_CACHE="${TRX_CACHE:-/cache/multicore}"
  mkdir -p "$HOME"
  for tcl in "${designs[@]}"; do
    d="$(dirname "$tcl")"; n="$(basename "$d")"
    b="$d/${TRX_BUILD:-build}"
    echo "=== $n ($board)"
    # Vivado opens its log before build.tcl runs: the directory has to exist
    # (without it: "CRITICAL WARNING [Common 17-183] Failed to open handle build/vivado.log")
    mkdir -p "$b"
    rm -f "$b/vivado.rc"
    (cd "$d" && vivado_limited core "$b/vivado.rc" vivado -mode batch -nojournal -log "${TRX_BUILD:-build}/vivado.log" -source build.tcl) \
      | grep --line-buffered -E "^(ERROR|CRITICAL WARNING|===|Starting [A-Za-z ]+ Task|Phase [0-9]+ [A-Z])|^INFO: \[Route 35-|Number of Nodes with overlaps|Congested Area|^\|.*(Direction|NORTH|SOUTH|EAST|WEST)" || true
    # (--line-buffered: every line reaches the CI log at once, so a long run shows where it is)
    if [ "$(cat "$b/vivado.rc" 2>/dev/null)" = 124 ]; then
      echo "$n: stopped at the time limit"
      mark_red "$stage" "aux-$n" "timeout (stopped at the limit of a core run)"; aux_failed=1; continue
    fi
    if ! test -f "$b/$n.bit"; then
      echo "no bitstream for $n; post-synthesis utilization:"
      grep -E "^\| (Slice LUTs|LUT as Memory|Slice Registers|Block RAM Tile|DSPs|Bonded IOB) " \
        "$b/utilization_synth.rpt" 2>/dev/null || true
      echo "post-synthesis utilization by module (first level below top):"
      grep -E "^\|   [^ ]|^\| top |Instance" "$b/utilization_hier_synth.rpt" 2>/dev/null || true
      tail -40 "$b/vivado.log" 2>/dev/null || true
      mark_red "$stage" "aux-$n" "no bitstream"; aux_failed=1; continue
    fi
    grep -m1 -E "^Slack" "$b/timing.rpt" || true
    wns="$({ grep -A2 'WNS(ns)' "$b/timing.rpt" || true; } | sed -n 3p | awk '{print $1}')"
    echo "WNS $n$suffix: ${wns:-none} ns"
    grep -E "^=== (impl pass|kept:)" "$b/vivado.log" 2>/dev/null || true
    echo "$n$suffix ${wns:-none}" >> "$out/wns.txt"
    # the reports go out whether timing passed or not
    cp "$b/timing.rpt" "$out/$n$suffix.timing.rpt"
    cp "$b/utilization.rpt" "$out/$n$suffix.utilization.rpt"
    for r in utilization_hier utilization_synth utilization_hier_synth design_analysis qor_suggestions cdc utilization_pb_l2 utilization_pb_l2_opt utilization_vc20_synth utilization_vc20_hier_synth utilization_pb_l2_vc20 utilization_vc20_hier timing_vc20 route_status_vc20 utilization_amiga_synth utilization_amiga_hier_synth utilization_pb_l2_amiga utilization_amiga_hier timing_amiga route_status_amiga utilization_c264_synth utilization_c264_hier_synth utilization_pb_l2_c264 utilization_c264_hier timing_c264 route_status_c264; do
      cp "$b/$r.rpt" "$out/$n$suffix.$r.rpt" 2>/dev/null || true
    done
    gzip -c "$b/vivado.log" > "$out/$n$suffix.vivado.log.gz" 2>/dev/null || true
    # where the time goes: Vivado's own timer per command, elapsed
    grep -E '^(create_project|link_design|generate_target|synth_ip|synth_design|opt_design|place_design|phys_opt_design|route_design|write_checkpoint|write_bitstream|report_[a-z_]+): Time \(s\)' "$b/vivado.log" \
      | sed -E 's/^([a-z_]+): Time \(s\): cpu = ([0-9:.]+) ; elapsed = ([0-9:.]+).*/\1 \3 (cpu \2)/' > "$out/$n$suffix.steps.txt" || true
    echo "--- Vivado steps, $n$suffix (elapsed)"; cat "$out/$n$suffix.steps.txt"
    if [ -n "$wns" ] && awk -v w="$wns" 'BEGIN { exit !(w < 0) }'; then
      echo "utilization of the failed build (the package is not made):"
      grep -E "^\| (Slice LUTs|Slice Registers) " "$b/utilization.rpt" || true
      echo "timing failed for $n$suffix (WNS $wns ns); worst paths:"
      grep -A14 "Slack (VIOLATED)" "$b/timing.rpt" \
        | grep -E "Slack \(VIOLATED\)|Source:|Destination:|Path Group:|Requirement:|Data Path Delay:|Logic Levels:" \
        | head -48 || true
      mark_red "$stage" "aux-$n" "timing failed (WNS $wns ns)"; aux_failed=1; continue
    fi
    cp "$b/$n.bit" "$out/$n$suffix.bit"
    # T79 trial B: the partial bitstreams of layer 2 (full layer 2 and grey box); T83: also the VC-20
    # module's (c64_basic_pb_l2_vc20_partial.bit, TRX_CORE=vc20 only) and T90's C264 (..._c264_partial.bit, TRX_CORE=c264), matched by the same glob
    for p in "$b/$n"_*partial*.bit "$b/$n"_l2_greybox.bit; do
      [ -f "$p" ] && cp "$p" "$out/$(basename "${p%.bit}")$suffix.bit"
    done
  done
  ls "$out"/*.bit >/dev/null 2>&1 && (cd "$out" && sha256sum ./*.bit | sed 's| \./| |' > SHA256SUMS)
  git -C "$ROOT" rev-parse --short=8 HEAD > "$out/from"
  if [ -z "$aux_failed" ]; then
    cache_put "$out" "$cache"
    : > "$cache/DONE"
  fi
  echo "built $(cat "$out/from")" > "$out/from"
  [ "$layered" != 1 ] || plan_note "$out" aux "built$(plan_has aux || echo ' (cache miss of an unnamed target)')"
fi
if [ -n "$corelane" ] || [ "$layered" = 1 ]; then
  [ -n "$corelane" ] || [ -n "$aux_failed" ] || pin_entry "$cache"
  source "$ROOT/ci/bitstream_layers.sh"
  build_layered "$out" "$part" "$lane"
else
  pin_entry "$cache"
  # the janitor for the monolithic kinds as well (c64u-*, ue2-*): pins and history, no count of 8
  cache_gc_if_free
fi
ls -l "$out"
: > "$out/DONE"
# T100 D9.6 / T174 D4: the red targets of this stage. The files are handed over either way; the stage ends red itself.
stage_rc=0
stage_verdict "$stage" || stage_rc=1

if [ "$board" = ue2 ]; then
  pub="$ROOT/out-ue2"
  rm -rf "$pub"; mkdir -p "$pub"
  cp "$out"/*.bit "$out"/*.rpt "$out"/*.gz "$out"/*.steps.txt "$pub/" 2>/dev/null || true
  python3 - "$out" "$pub" "$key" <<'PY'
import hashlib, json, os, sys
src, pub, key = sys.argv[1:]
wns = dict(l.split() for l in open(os.path.join(src, "wns.txt"))) if os.path.exists(os.path.join(src, "wns.txt")) else {}
files = {f: hashlib.sha256(open(os.path.join(pub, f), "rb").read()).hexdigest() for f in sorted(os.listdir(pub))}
json.dump({"part": "xc7a100tfgg484-1", "gateware_key": key, "from": open(os.path.join(src, "from")).read().strip(),
           "wns_ns": wns, "files": files}, open(os.path.join(pub, "manifest_100t.json"), "w"), indent=2)
PY
  publish_files "$pub"
  rm -rf "${HANDOFF:?}"
fi
echo "=== $stage done in $(lap)"
exit "$stage_rc"
