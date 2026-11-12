# shellcheck shell=bash
# Sourced by tests/coreapp/run.sh and tests/loader/run.sh (T100 D12): the negative controls run JOBS at a time.
#
# A control is a function that builds, runs and judges one mutant and `exit 1`s on any defect. pjob_spawn runs it in a subshell of its
# own with its output in a file; pjob_flush prints the finished jobs in submission order, so the log reads as the sequential suite's did,
# and the first job that failed stops the suite (the others are killed) - a vacuous control, a control that passes and a control that
# fails without a red check all still fail the suite. Nothing is shared between jobs: each one gets its own directory, $JOBDIR.
# Needs $T (the suite's scratch directory). Bash 3.2 (macOS): no `wait -n`, so it polls.
#
#   JOBS=n   at most n controls at a time; default: cores - 2, at least 1
PJ_N=0 PJ_PRINTED=0 PJ_RUNNING=0
if [ -z "${JOBS:-}" ]; then
  JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 3)"
  JOBS=$((JOBS - 2))
fi
case "$JOBS" in ''|*[!0-9]*) JOBS=1 ;; esac
[ "$JOBS" -ge 1 ] || JOBS=1
PJ_DIR="$T/jobs"; mkdir -p "$PJ_DIR"

pjob_kill() {
  local p
  for p in $(jobs -p); do pkill -P "$p" 2>/dev/null || true; kill "$p" 2>/dev/null || true; done
}
pjob_flush() {   # print, in order, the jobs that have finished; stop the suite at the first one that failed
  local rc
  while [ "$PJ_PRINTED" -lt "$PJ_N" ] && [ -f "$PJ_DIR/$PJ_PRINTED.rc" ]; do
    cat "$PJ_DIR/$PJ_PRINTED.out"
    rc="$(cat "$PJ_DIR/$PJ_PRINTED.rc")"
    PJ_PRINTED=$((PJ_PRINTED + 1))
    if [ "$rc" != 0 ]; then echo "(control job $((PJ_PRINTED - 1)) failed, rc=$rc: the suite stops)" >&2; pjob_kill; exit 1; fi
  done
  PJ_RUNNING=$((PJ_N - $(ls "$PJ_DIR" | grep -c '\.rc$' || true)))
}
pjob_spawn() {   # pjob_spawn FUNCTION ARG... : FUNCTION runs in a subshell with set -e and $JOBDIR as its private directory
  pjob_flush
  while [ "$PJ_RUNNING" -ge "$JOBS" ]; do sleep 0.2; pjob_flush; done
  local n="$PJ_N"; PJ_N=$((PJ_N + 1))
  mkdir -p "$PJ_DIR/$n.d"
  (
    set +e
    ( set -e; JOBDIR="$PJ_DIR/$n.d"; "$@" ) > "$PJ_DIR/$n.out" 2>&1
    echo $? > "$PJ_DIR/$n.rc.tmp"; mv "$PJ_DIR/$n.rc.tmp" "$PJ_DIR/$n.rc"
  ) &
  PJ_RUNNING=$((PJ_RUNNING + 1))
}
pjob_drain() {   # wait for every job, print the rest in order
  while [ "$PJ_PRINTED" -lt "$PJ_N" ]; do pjob_flush; [ "$PJ_PRINTED" -ge "$PJ_N" ] || sleep 0.2; done
}
