#!/usr/bin/env bash
# The queue (T87 D1), first step of job `plan` since 2026-10-26: this pipeline waits until every OLDER run of the same
# workflow has completed, then returns; the stage jobs need `plan`, so they do not
# exist before it is this run's turn. No `concurrency:` group is involved, so nothing is
# cancelled by a newer push, and no other pipeline can start a stage in between.
# "Older" = lower run id (push order). The state comes from the Gitea API; a run that
# crashed or was cancelled is completed there, so no ticket can go stale.
#
# A blocker that never starts is a deadlock, not a queue: this job holds a runner slot while it
# waits, so after WAIT_TURN_MAX minutes (default 240) it gives up and fails loudly instead of
# holding the slot until the job timeout (runs 791-798 of TRX64U-MultiCore: an older run waited
# for a builder slot that this very job held).
#
#   ci/wait_turn.sh            env: RUN_ID, WORKFLOW (file name, default pipeline.yml),
#                              TOKEN, GITHUB_API_URL / GITHUB_SERVER_URL, GITHUB_REPOSITORY,
#                              WAIT_TURN_MAX (minutes, 0 = no limit)
#   ci/wait_turn.sh --selftest the blocker selection on canned API answers
set -uo pipefail

# blockers <run id> <workflow>: reads the runs JSON on stdin, prints "id status branch" of
# every older, not completed run of that workflow.
blockers() {
  python3 -c '
import json, sys
me, wf = int(sys.argv[1]), sys.argv[2]
for r in json.load(sys.stdin)["workflow_runs"]:
    if r["id"] < me and r["path"].split("@")[0] == wf and r["status"] != "completed":
        print(r["id"], r["status"], r.get("head_branch", ""))
' "$1" "$2"
}

if [ "${1:-}" = --selftest ]; then
  json='{"workflow_runs":[
   {"id":9,"path":"pipeline.yml@refs/heads/c","status":"queued","head_branch":"c"},
   {"id":8,"path":"pipeline.yml@refs/heads/b","status":"in_progress","head_branch":"b"},
   {"id":7,"path":"lint.yml@refs/heads/b","status":"in_progress","head_branch":"b"},
   {"id":6,"path":"pipeline.yml@refs/heads/a","status":"completed","head_branch":"a"},
   {"id":5,"path":"pipeline.yml@refs/heads/a","status":"waiting","head_branch":"a"}]}'
  [ "$(printf '%s' "$json" | blockers 9 pipeline.yml | tr '\n' ';')" = "8 in_progress b;5 waiting a;" ] || { echo "selftest: run 9"; exit 1; }
  [ -z "$(printf '%s' "$json" | blockers 5 pipeline.yml)" ] || { echo "selftest: run 5 has no blocker"; exit 1; }
  [ "$(printf '%s' "$json" | blockers 7 lint.yml | wc -l | tr -d ' ')" = 0 ] || { echo "selftest: other workflow"; exit 1; }
  echo "wait_turn selftest ok"
  exit 0
fi

me="${RUN_ID:?RUN_ID}"; wf="${WORKFLOW:-pipeline.yml}"; tok="${TOKEN:?TOKEN}"
api="${GITHUB_API_URL:-${GITHUB_SERVER_URL:?}/api/v1}"
url="$api/repos/${GITHUB_REPOSITORY:?}/actions/runs?limit=50"
errs=0; last=""; t0=$SECONDS
while :; do
  if ! body="$(curl -sf -m 30 -H "Authorization: token $tok" "$url")"; then
    errs=$((errs + 1)); echo "run list unavailable ($errs)"
    [ "$errs" -lt 20 ] || { echo "the Gitea API does not answer; not guessing a turn" >&2; exit 1; }
    sleep 15; continue
  fi
  errs=0
  b="$(printf '%s' "$body" | blockers "$me" "$wf")" || { echo "cannot read the run list" >&2; exit 1; }
  if [ -z "$b" ]; then
    echo "my turn (run $me) after $((SECONDS - t0)) s"
    exit 0
  fi
  if [ "$b" != "$last" ]; then
    echo "run $me waits for: $(printf '%s' "$b" | tr '\n' ',')"
    last="$b"
  fi
  max="${WAIT_TURN_MAX:-240}"
  if [ "$max" -gt 0 ] && [ $((SECONDS - t0)) -gt $((max * 60)) ]; then
    echo "run $me waited $max minutes for: $(printf '%s' "$b" | tr '\n' ',')" >&2
    echo "that is a stuck queue, not a busy one (this job holds a runner slot while it waits):" >&2
    echo "cancel the runs above, then push again" >&2
    exit 1
  fi
  sleep 15
done
