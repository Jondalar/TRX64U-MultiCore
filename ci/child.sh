#!/usr/bin/env bash
# T174 D4: one child of the push pipeline (ci/children.txt), started by the parent (ci/orchestrate.py), by hand or nightly.
#
#   ci/child.sh <child> [<target>]     child: apptests | images | emu | sim <owner>|all | vivado <host|core:id|large:id|ue2> | cor <core:id|large:id>
#                                      | collect | package | update
#
# env: PARENT (the parent's run id; empty = run alone: this run's own id), PLAN_JSON (the parent's plan; empty = computed here),
# CI_STATUS_TOKEN (commit status of this child, and the parent watch).
# Every child: the parent's hand-over directory on the builder (/cache/multicore/runs/parent-<id>: the images and the bitstream stages for the
# children behind it), a red directory of its own, the commit status "ci/<child>[/<target>]" (pending, then success or failure with the reason),
# its one script, and on red the line "CHILD RED: <reason>" that the parent's table shows. Red is red at once - nothing is deferred.
# The parent watch: a child that sees its parent ended (cancelled; a parent only ends after its children) stops (Gitea has no cancel API).
set -uo pipefail
child="${1:?usage: ci/child.sh <child> [<target>]}" target="${2:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
tag="$child${target:+-${target//[:\/]/-}}"
export CI_RUN_ID="${PARENT:+parent-$PARENT}"
[ -n "$CI_RUN_ID" ] || export CI_RUN_ID="${GITHUB_RUN_ID:-local}"
if [ -z "${CI_HANDOFF:-}" ]; then
  if [ -d /cache ] && [ -w /cache ]; then export CI_HANDOFF="/cache/multicore/runs/$CI_RUN_ID"; else export CI_HANDOFF="$ROOT/.ci/runs/$CI_RUN_ID"; fi
fi
export CI_RED_DIR="$CI_HANDOFF.red/$tag"
source "$HERE/lib.sh"
mkdir -p "$HANDOFF" "$RED_DIR"
rm -f "$RED_DIR"/*
log="$RED_DIR/child.log"
cpus="$(nproc 2>/dev/null || echo 2)"

status() { # status <state> <description>
  [ -n "${CI_STATUS_TOKEN:-}" ] && [ -n "${GITHUB_SERVER_URL:-}" ] && [ -n "${GITHUB_REPOSITORY:-}" ] && [ -n "${CI_SHA:-}" ] || return 0
  local body
  body="$(python3 -I -c 'import json,sys; s,c,d,u=sys.argv[1:5]; print(json.dumps({"state":s,"context":c,"description":d[:140],"target_url":u}))' \
    "$1" "ci/$child${target:+/$target}" "$2" "$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/${GITHUB_RUN_ID:-0}")" || return 0
  curl -s -m 10 -o /dev/null -X POST -H "Authorization: token $CI_STATUS_TOKEN" -H 'Content-Type: application/json' --data "$body" \
    "$GITHUB_SERVER_URL/api/v1/repos/$GITHUB_REPOSITORY/statuses/$CI_SHA" || true
}

# the plan: the parent's (written atomically: the children of one parent share the directory). Without one (by hand, nightly) there is no
# plan file and every stage does everything it is asked for ("a missing plan names everything", ci/plan.py has).
if [ -n "${PLAN_JSON:-}" ]; then
  printf '%s' "$PLAN_JSON" | python3 -c 'import json,sys; assert json.load(sys.stdin)["version"] == 1' 2>/dev/null || { echo "CHILD RED: the plan input is no plan.json"; exit 1; }
  printf '%s\n' "$PLAN_JSON" > "$HANDOFF/plan.json.$$" && mv -f "$HANDOFF/plan.json.$$" "$HANDOFF/plan.json"
  python3 "$HERE/plan.py" show "$HANDOFF/plan.json"
else
  rm -f "$HANDOFF/plan.json"; echo "plan: none (run alone: everything is named)"
fi

# the concern
half=$(( (cpus + 1) / 2 ))
case "$child" in
  apptests) heavy_lock; cmd=(env "JOBS=${JOBS:-$half}" "$HERE/render.sh") ;;     # a slot and half the machine: the sims take the other half
  images)   cmd=("$HERE/trxapps.sh") ;;
  emu)      need_stage trxapps >/dev/null || exit 1
            cmd=(env "EMU_IMAGES=$HANDOFF/trxapps" "$HERE/apptests.sh" --tier integration hostapp_emu coreapp_emu) ;;
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
  package)  cmd=("$HERE/package.sh") ;;
  update)   cmd=(env UPDATE_FORCE=true "$HERE/update.sh") ;;
  *) echo "unknown child $child" >&2; exit 2 ;;
esac

watch() { # the parent watch: every minute; the parent ending before this child means it was cancelled
  local api="$GITHUB_SERVER_URL/api/v1/repos/$GITHUB_REPOSITORY/actions/runs/$PARENT" s
  while sleep "${CI_PARENT_POLL:-60}"; do
    s="$(curl -sf -m 20 -H "Authorization: token $CI_STATUS_TOKEN" "$api" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["status"], d.get("conclusion") or "")' 2>/dev/null)" || continue
    case "$s" in completed*)
      echo "parent $PARENT ended (${s#completed }) while this child ran: stopping" > "$RED_DIR/parent.gone"
      kill -TERM -- "-$1" 2>/dev/null || kill -TERM "$1" 2>/dev/null; return ;;
    esac
  done
}

status pending "running"
echo "=== child $child${target:+ $target}: ${cmd[*]}  (hand-over $HANDOFF)"
t0=$SECONDS
set -m
"${cmd[@]}" > >(tee "$log") 2>&1 &
pid=$!
set +m
wpid=""
if [[ "${PARENT:-}" =~ ^[0-9]+$ ]] && [ -n "${CI_STATUS_TOKEN:-}" ] && [ -n "${GITHUB_SERVER_URL:-}" ]; then watch "$pid" & wpid=$!; fi
rc=0; wait "$pid" || rc=$?
[ -z "$wpid" ] || kill "$wpid" 2>/dev/null
sleep 1   # the tee
secs=$((SECONDS - t0))
reds="$(cat "$RED_DIR/parent.gone" 2>/dev/null; for f in "$RED_DIR"/*.*; do case "$f" in *.log|*/parent.gone) continue ;; esac; [ -e "$f" ] && echo "${f##*/}: $(head -1 "$f")"; done)"
if [ "$rc" -eq 0 ] && [ -z "$reds" ]; then
  status success "green in ${secs}s"
  echo "=== child $child${target:+ $target} green in ${secs}s"
  exit 0
fi
reason="$(printf '%s\n' "$reds" | sed '/^$/d' | head -1)"
[ -n "$reason" ] || reason="$(grep -E 'FAIL|TIMEOUT|ERROR|error:' "$log" 2>/dev/null | tail -1)"
[ -n "$reason" ] || reason="$(sed '/^[[:space:]]*$/d' "$log" 2>/dev/null | tail -1)"
reason="${reason:-exit $rc}"
status failure "$reason"
echo "CHILD RED: ${reason:0:300} (exit $rc, ${secs}s)"
[ -z "$reds" ] || { echo "=== red targets:"; printf '%s\n' "$reds"; }
exit 1
