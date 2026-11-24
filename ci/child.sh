#!/usr/bin/env bash
# T174 D4, T239.1 S6: one child of the pipeline (ci/children.txt), run by its workflow in .github/workflows/, by the builder (vivado) or by hand.
#
#   ci/child.sh <child> [<target>]     child: apptests | images | emu | sim <owner>|all | vivado <host|core:id|large:id|ue2> | cor <core:id|large:id>
#                                      | collect | package | update | prerelease
#
# env: CI_HANDOFF (the hand-over directory of the run; the workflows set it inside the job and fill it from the artifacts of the jobs before),
# CI_RUN_ID (default GITHUB_RUN_ID; the builder's pull build sets its own). The plan job's plan.json is read from $CI_HANDOFF/plan.json when it is there.
# Every child: a red directory of its own, its one script, and on red the line "CHILD RED: <reason>" that the job log and the summary show. Red is red
# at once - nothing is deferred. No commit status and no parent watch: the job of the workflow is the status, and a cancelled run cancels its jobs.
set -uo pipefail
child="${1:?usage: ci/child.sh <child> [<target>]}" target="${2:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
tag="$child${target:+-${target//[:\/]/-}}"
export CI_RUN_ID="${CI_RUN_ID:-${GITHUB_RUN_ID:-local}}"
if [ -z "${CI_HANDOFF:-}" ]; then
  if [ -d /cache ] && [ -w /cache ]; then export CI_HANDOFF="/cache/multicore/runs/$CI_RUN_ID"; else export CI_HANDOFF="$ROOT/.ci/runs/$CI_RUN_ID"; fi
fi
export CI_RED_DIR="$CI_HANDOFF.red/$tag"
source "$HERE/lib.sh"
mkdir -p "$HANDOFF" "$RED_DIR"
rm -f "$RED_DIR"/*
log="$RED_DIR/child.log"
cpus="$(nproc 2>/dev/null || echo 2)"

# the plan: the plan job's plan.json, downloaded into the hand-over directory before this script runs. Without one (by hand, nightly) there is no
# plan file and every stage does everything it is asked for ("a missing plan names everything", ci/plan.py has).
if [ -f "$HANDOFF/plan.json" ]; then
  python3 -c 'import json,sys; assert json.load(open(sys.argv[1]))["version"] == 1' "$HANDOFF/plan.json" 2>/dev/null || { echo "CHILD RED: $HANDOFF/plan.json is no plan.json"; exit 1; }
  python3 "$HERE/plan.py" show "$HANDOFF/plan.json"
else
  echo "plan: none (run alone: everything is named)"
fi

# the concern
half=$(( (cpus + 1) / 2 ))
case "$child" in
  apptests) if [ -d /cache ]; then heavy_lock; fi; cmd=(env "JOBS=${JOBS:-$half}" "$HERE/render.sh") ;;     # a slot and half the machine: the sims take the other half
  images)   cmd=("$HERE/trxapps.sh") ;;
  emu)      need_stage trxapps >/dev/null || exit 1
            cmd=(env "EMU_IMAGES=$HANDOFF/trxapps" "$HERE/apptests.sh" --tier integration hostapp_emu) ;;
  sim)      [ -n "$target" ] || { echo "sim needs an owner (host, core:<id>, large:<id>) or all" >&2; exit 2; }
            if [ "$target" = all ]; then cmd=(env "CI_SIMS_CORES=${CI_SIMS_CORES:-$half}" "$HERE/sims.sh" -t "${SIMS_TIER:-2}"); else
            tier="$(python3 "$HERE/orchestrate.py" tier "$HANDOFF/plan.json")"
            mapfile -t benches < <(python3 "$HERE/orchestrate.py" benches "$HANDOFF/plan.json" "$target")
            [ "${#benches[@]}" -gt 0 ] || { echo "CHILD RED: the plan names no bench of $target in tier $tier (the parent should not have started this)"; exit 1; }
            cmd=(env "CI_SIMS_CORES=${CI_SIMS_CORES:-$half}" "$HERE/sims.sh" -t "$tier" "${benches[@]}"); fi
            # T174 D5.3: the time limit of this child is what the benches it runs may take (their timeouts in ci/sims.txt against the core budget,
            # cached benches cost nothing), not one number per lane; the workflow's timeout-minutes is only a ceiling above every such limit
            lim="$(env "CI_SIMS_CORES=${CI_SIMS_CORES:-$half}" "$HERE/sims.sh" --limit -t "${tier:-${SIMS_TIER:-2}}" ${benches[@]+"${benches[@]}"} 2>/dev/null)" || lim=""
            if [[ "$lim" =~ ^[0-9]+$ ]]; then echo "time limit for this sim child: $lim min (from the timeouts of the benches that run)"; cmd=(timeout -k 60 "${lim}m" "${cmd[@]}"); fi ;;
  vivado)   case "$target" in
              host|core:?*) cmd=("$HERE/bitstream.sh" c64u --lane "$target") ;;
              large:?*)     cmd=("$HERE/bitstream_mono.sh" --lane "${target#large:}") ;;
              ue2)          cmd=(env UE2_INPUT=true "$HERE/bitstream.sh" ue2) ;;
              *) echo "vivado target: host, core:<id>, large:<id> or ue2" >&2; exit 2 ;;
            esac ;;
  cor)      cmd=("$HERE/cor.sh" "$target") ;;
  collect)  cmd=("$HERE/collect.sh") ;;
  prerelease) cmd=("$HERE/prerelease.sh") ;;
  package)  cmd=("$HERE/package.sh") ;;
  update)   cmd=(env UPDATE_FORCE=true "$HERE/update.sh") ;;
  *) echo "unknown child $child" >&2; exit 2 ;;
esac

echo "=== child $child${target:+ $target}: ${cmd[*]}  (hand-over $HANDOFF)"
t0=$SECONDS
set -m
"${cmd[@]}" > >(tee "$log") 2>&1 &
pid=$!
set +m
rc=0; wait "$pid" || rc=$?
sleep 1   # the tee
secs=$((SECONDS - t0))
reds="$(for f in "$RED_DIR"/*.*; do case "$f" in *.log) continue ;; esac; [ -e "$f" ] && echo "${f##*/}: $(head -1 "$f")"; done)"
if [ "$rc" -eq 0 ] && [ -z "$reds" ]; then
  echo "=== child $child${target:+ $target} green in ${secs}s"
  exit 0
fi
reason="$(printf '%s\n' "$reds" | sed '/^$/d' | head -1)"
[ -n "$reason" ] || reason="$(grep -E 'FAIL|TIMEOUT|ERROR|error:' "$log" 2>/dev/null | tail -1)"
[ -n "$reason" ] || reason="$(sed '/^[[:space:]]*$/d' "$log" 2>/dev/null | tail -1)"
reason="${reason:-exit $rc}"
echo "CHILD RED: ${reason:0:300} (exit $rc, ${secs}s)"
[ -z "$reds" ] || { echo "=== red targets:"; printf '%s\n' "$reds"; }
exit 1
