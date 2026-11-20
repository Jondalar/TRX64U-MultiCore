#!/usr/bin/env bash
# Job `plan` (T100 D9, build by change): which files did this push change since the last GREEN run of this branch, and which
# targets does that make. The answer is plan.json (format: ci/plan.py); the parent hands it to its children (ci/orchestrate.py, ci/child.sh).
#
#   ci/plan.sh [--out plan.json]     env: TOKEN (the run's GITHUB_TOKEN), GITHUB_API_URL, GITHUB_REPOSITORY, GITHUB_REF_NAME, RUN_ID,
#                                    PLAN_BASE (a commit: skip the API, for tests and by hand), WORKFLOW (default pipeline.yml)
#   ci/plan.sh --selftest            the choice of the base on canned API answers
#
# The base G (Alex, 2026-10-25: "a)"): the head commit of the newest completed run of this workflow on this branch whose
# conclusion is success, other than this run; a push of several commits, or one after a red or cancelled run, therefore builds
# everything changed since G. No green run: the merge-base with main. On main without a green run: nothing is known and the plan
# names everything ("none"). A base that cannot be fetched (rebased away) falls back to the merge-base, then to "none":
# never to "nothing changed".
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

# pick_base <run id> <workflow> <branch> [<repository>]: reads the runs JSON on stdin, prints "<sha> <run id>" of the newest green run, if any. Only runs of event
# push whose head repository is <repository> count: a fork's pull request run carries the fork's branch name (often main) and must not be a base.
pick_base() {
  python3 -c '
import json, os, sys
me, wf, br, repo = int(sys.argv[1] or 0), sys.argv[2], sys.argv[3], sys.argv[4]
best = None
for r in json.load(sys.stdin).get("workflow_runs", []):
    if r["id"] == me or os.path.basename(r["path"].split("@")[0]) != wf or r.get("head_branch") != br or r.get("status") != "completed":
        continue
    if r.get("event") != "push" or (r.get("head_repository") or {}).get("full_name") != repo:
        continue
    if r.get("conclusion", "success" if r.get("status") == "success" else "") != "success":
        continue
    if best is None or r["id"] > best["id"]:
        best = r
if best:
    print(best["head_sha"], best["id"])
' "$1" "$2" "$3" "${4:-}"
}

if [ "${1:-}" = --selftest ]; then
  json='{"workflow_runs":[
   {"id":30,"path":".github/workflows/pipeline.yml","status":"in_progress","head_branch":"b","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"3333"},
   {"id":29,"path":"pipeline.yml@refs/heads/b","status":"completed","conclusion":"failure","head_branch":"b","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"2222"},
   {"id":28,"path":"pipeline.yml@refs/heads/b","status":"completed","conclusion":"success","head_branch":"b","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"1111"},
   {"id":27,"path":"pipeline.yml@refs/heads/b","status":"completed","conclusion":"success","head_branch":"b","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"0000"},
   {"id":26,"path":"lint.yml@refs/heads/b","status":"completed","conclusion":"success","head_branch":"b","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"9999"},
   {"id":25,"path":"pipeline.yml@refs/heads/x","status":"completed","conclusion":"success","head_branch":"x","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"8888"},
   {"id":33,"path":"pipeline.yml@refs/pull/9/merge","status":"completed","conclusion":"success","head_branch":"b","event":"pull_request","head_repository":{"full_name":"o/r"},"head_sha":"6666"},
   {"id":34,"path":"pipeline.yml@refs/heads/b","status":"completed","conclusion":"success","head_branch":"b","event":"push","head_repository":{"full_name":"fork/r"},"head_sha":"5555"},
   {"id":24,"path":"pipeline.yml@refs/heads/b","status":"completed","conclusion":"cancelled","head_branch":"b","event":"push","head_repository":{"full_name":"o/r"},"head_sha":"7777"}]}'
  [ "$(printf '%s' "$json" | pick_base 31 pipeline.yml b o/r)" = "1111 28" ] || { echo "selftest: green run of b"; exit 1; }
  [ "$(printf '%s' "$json" | pick_base 28 pipeline.yml b o/r)" = "0000 27" ] || { echo "selftest: own run is not its base"; exit 1; }
  [ -z "$(printf '%s' "$json" | pick_base 31 pipeline.yml nobranch o/r)" ] || { echo "selftest: branch without a green run"; exit 1; }
  [ "$(printf '%s' "$json" | pick_base 31 pipeline.yml x o/r)" = "8888 25" ] || { echo "selftest: other branch"; exit 1; }
  # a fork's run named like the branch (id 34 push from a fork, id 33 pull request) is no base: without the filter the newest green run of b would be 34
  [ "$(printf '%s' "$json" | pick_base 40 pipeline.yml b o/r)" = "1111 28" ] || { echo "selftest: fork or pull request run taken as a base"; exit 1; }
  echo "plan selftest ok"
  exit 0
fi

# fetch_commit <sha>: make the commit present (a stage clones with --depth 1)
fetch_commit() {
  git -C "$ROOT" cat-file -e "$1^{commit}" 2>/dev/null && return 0
  git -C "$ROOT" fetch -q --depth 1 origin "$1" 2>/dev/null && git -C "$ROOT" cat-file -e "$1^{commit}" 2>/dev/null
}

# merge_base_main: the merge-base of HEAD and origin/main, or nothing
merge_base_main() {
  if [ "$(git -C "$ROOT" rev-parse --is-shallow-repository 2>/dev/null)" = true ]; then
    git -C "$ROOT" fetch -q --unshallow origin 2>/dev/null || git -C "$ROOT" fetch -q --depth 2000 origin 2>/dev/null || true
  fi
  git -C "$ROOT" fetch -q origin main 2>/dev/null || true
  git -C "$ROOT" merge-base HEAD origin/main 2>/dev/null || git -C "$ROOT" merge-base HEAD main 2>/dev/null || true
}

# find_base: prints "<commit> <source text>"
find_base() {
  local branch="${CI_BRANCH:-${GITHUB_REF_NAME:-$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)}}" api body g="" run="" mb page
  if [ -n "${PLAN_BASE:-}" ]; then
    echo "$PLAN_BASE given"; return 0
  fi
  if [ -n "${TOKEN:-}" ] && [ -n "${GITHUB_REPOSITORY:-}" ]; then
    api="${GITHUB_API_URL:-https://api.github.com}"
    for page in 1 2 3 4; do
      body="$(curl -sf -m 30 -H "Authorization: Bearer $TOKEN" -H "Accept: application/vnd.github+json" "$api/repos/$GITHUB_REPOSITORY/actions/runs?per_page=50&page=$page&branch=$branch")" || { echo "plan: the run list is not available (page $page)" >&2; break; }
      read -r g run < <(printf '%s' "$body" | pick_base "${RUN_ID:-0}" "${WORKFLOW:-pipeline.yml}" "$branch" "$GITHUB_REPOSITORY") || true
      [ -z "${g:-}" ] || break
    done
  else
    echo "plan: no TOKEN / GITHUB_REPOSITORY: no run list" >&2
  fi
  if [ -n "${g:-}" ] && fetch_commit "$g"; then
    echo "$g last green run ${run} of $branch"; return 0
  fi
  [ -z "${g:-}" ] || echo "plan: the commit $g of the last green run cannot be fetched (rebased away?)" >&2
  if [ "$branch" != main ]; then
    mb="$(merge_base_main)"
    if [ -n "$mb" ]; then echo "$mb merge-base with main (no usable green run of $branch)"; return 0; fi
  fi
  echo "none no green run and no merge-base: nothing known, everything is built"
}


out=""
[ "${1:-}" != --out ] || out="${2:?}"
read -r base source < <(find_base)
echo "plan: diff base ${base} ($source)" >&2
if [ "$base" != none ]; then
  git -C "$ROOT" cat-file -e "$base^{commit}" 2>/dev/null || fetch_commit "$base" || { echo "plan: base $base is not available" >&2; base=none; source="base not available"; }
fi
args=(make --base "$base" --source "$source")
[ -z "$out" ] || args+=(--out "$out" --files-out "$out.files")
exec python3 "$HERE/plan.py" "${args[@]}"
