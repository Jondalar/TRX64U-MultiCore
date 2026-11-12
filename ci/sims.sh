#!/usr/bin/env bash
# Stage `sims`: the benches listed in ci/sims.txt, in trx64-sim-env.
#
#   ci/sims.sh [-t TIER] [-f] [-j CORES] [name ...]  TIER 1 (default) or 2; 2 runs both. Tier 3 is never
#                                            chosen by the pipeline: those run by hand (sims-long.yml)
#   ci/sims.sh --check-deps name ...         run the benches in a copy that holds only
#                                            the files ci/sims.deps declares for them
#   ci/sims.sh [-t TIER] [-j CORES] [name ...] --limit   print the minutes the benches that would run may take altogether (T174 D5.3), run nothing
#   ci/sims.sh --key name ...                print the key of each bench and what it is made of; runs nothing (T174 D5)
#   ci/sims.sh --record [--record-seconds N] name ...   run each bench in a clean copy of the tracked files with ci/simreads.c
#                                            preloaded and write the files it read into ci/sims.reads (T174 D5)
#
# The benches that have to run share a budget of CORES (-j; CI_SIMS_CORES; else CI_SIMS_JOBS from the runner's config
# envs, but never more than half the cpus the job can see - the render stage and a Vivado run use the same machine). Every bench
# costs the threads it runs at once (ci/sims.threads, default 1: a bench that starts JOBS=n processes or builds with -j n says so
# there); the next bench starts when its cost fits into what is left, the longest first. 16 benches "at a time" on 16 cores,
# several of them with 3 to 8 processes of their own, timed out twelve benches that were fine alone (run 1188). A bench passes
# when its command exits 0, its output has a PASS (or PASSED) line and no FAIL,
# FAILED or TIMEOUT line, and it ends
# within its timeout. A failing bench does not stop the others: the summary
# at the end lists all of them, and the stage fails if one did - unless the bench belongs to a core
# whose descriptor says `experimental` (T94 D8). Those are reported under EXPERIMENTAL and do not
# fail the stage, so an unfinished core cannot stop the host's bitstream (Alex, 2026-10-14).
#
# Per-bench cache (T31): a bench has a key over the content of exactly the files
# ci/sims.deps lists for it (no line there: the whole tree), its line in ci/sims.txt
# the simulator versions and the driver number below. A PASS leaves a marker
# $CI_SIMS_OK/bench/<name>.<key>;
# a bench whose key has a marker is not run again ("cached PASS"). -f ignores the
# markers. Only a PASS writes one, and only when everything the bench reported
# reading (ci/sim_iv.sh does, through SIM_DEPS_LOG) is covered by its deps.
#
# T174 D5 (Alex, 2026-11-08: "Segment the caches better"): a bench whose line in ci/sims.deps is `@reads` has a RECORD in ci/sims.reads: the
# files its process tree opened (ci/simreads.c, LD_PRELOAD: ghdl -a/-i, verilator, iverilog, make, python, the files a bench opens at
# run time), the paths it looked for and did not find, the directories it listed. Its key is the content of exactly those files (plus
# whether the looked-for paths exist and the names in the listed directories). A run that reads more than the record extends the
# runner's copy of the record ($CI_SIMS_OK/../reads/<bench>) and the PASS marker is written under the key of the extended record, so the
# key follows what the run really read; a run whose log is empty (the preload did not work) is not cached. No record: the declared
# specs, or the whole tree - never a guess.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

tier=1 force="" check="" jobs="" keyonly="" record="" record_secs="" limitonly=""
while [ $# -gt 0 ]; do
  case "$1" in
    -t) tier="$2"; shift 2 ;;
    -f) force=1; shift ;;
    -j) jobs="$2"; shift 2 ;;
    --check-deps) check=1; force=1; shift ;;
    --key) keyonly=1; force=1; shift ;;
    --record) record=1; force=1; shift ;;
    --limit) limitonly=1; shift ;;
    --record-seconds) record_secs="$2"; shift 2 ;;
    *) break ;;
  esac
done
# cpus: what this job can use - nproc, cut to the cgroup quota (a container with --cpus=3 still sees all of nproc's cores)
cpus="$(nproc 2>/dev/null || echo 1)"
if read -r quota period 2>/dev/null < /sys/fs/cgroup/cpu.max && [ "$quota" != max ] && [ "${period:-0}" -gt 0 ]; then
  q=$(( (quota + period - 1) / period )); [ "$q" -ge 1 ] && [ "$q" -lt "$cpus" ] && cpus="$q"
fi
# the budget: -j wins, then CI_SIMS_CORES; the runner's CI_SIMS_JOBS (16 on the builder) is capped to half of the cpus
if [ -z "$jobs" ]; then
  half=$(( (cpus + 1) / 2 ))
  jobs="${CI_SIMS_CORES:-}"
  if [ -z "$jobs" ]; then
    jobs="${CI_SIMS_JOBS:-$half}"
    [ "$jobs" -le "$half" ] || jobs="$half"
  fi
fi
[ "$jobs" -ge 1 ] 2>/dev/null || jobs=1
want=" $* "
# T100 D9: with a plan (ci/plan.py, $HANDOFF/plan.json) only the benches it lists run - what is built and changed. Names on the
# command line win; without a plan file every bench of the tier is considered, as before.
from_plan=""
if [ -z "$check" ] && [ -z "$keyonly" ] && [ -z "$record" ] && [ "$want" = "  " ] && [ -f "$HANDOFF/plan.json" ]; then
  from_plan=1
  planned="$(python3 -c 'import json,sys; print(" ".join(json.load(open(sys.argv[1]))["sims"]))' "$HANDOFF/plan.json")"
  if [ -z "$planned" ]; then
    echo "=== sims: the plan names no bench (nothing that is built changed what a bench reads); nothing to run"
    exit 0
  fi
  echo "=== sims: the plan names $(echo "$planned" | wc -w | tr -d ' ') bench(es)"
  want=" $planned "
fi
[ -z "$check$keyonly$record" ] || [ "$want" != "  " ] || { echo "--check-deps, --key and --record need bench names" >&2; exit 2; }
ok_dir="${CI_SIMS_OK:-/cache/sims-ok}/bench"
logdir="${CI_SIM_LOGS:-$ROOT/.ci/sims}"
mkdir -p "$logdir"
cd "$ROOT"
cache_on=""
if mkdir -p "$ok_dir" 2>/dev/null && [ -w "$ok_dir" ]; then cache_on=1; fi
# T174 D5: the runner's copy of the records (a run that read more than ci/sims.reads says extends it) and the preload that logs the reads
reads_cache="$(dirname "$ok_dir")/reads"
simreads() { python3 "$ROOT/ci/simreads.py" "$@"; }
shim="" shimdir=""
if [ -z "$keyonly$limitonly" ]; then
  shimdir="$(mktemp -d)"; trap 'rm -rf "$shimdir"' EXIT
  if gcc -shared -fPIC -O1 -o "$shimdir/simreads.so" "$ROOT/ci/simreads.c" -ldl 2>"$shimdir/gcc.log"; then
    shim="$shimdir/simreads.so"
  else
    echo "ci/simreads.c did not build ($(head -1 "$shimdir/gcc.log")): benches with a record keep the whole-tree key" >&2
  fi
else
  shim=key-only     # the key is the same with or without the preload; only a run needs it
fi
# driver=<n>: this file's own contribution to every bench key (T100 D5, 2026-10-14). Until today
# ci/sims.sh itself was one of the key's files, so an edit here - a timeout, one bench's line, a
# comment - invalidated all 46 benches and cost a full suite (run 32, about an hour, and it then
# failed in a bench nobody had touched). The content of this file does not change what a bench
# means; what does is how a key is built and what a PASS is worth. Bump the number when one of
# those changes, and say in the commit why.
# driver=2 (2026-10-16): what a PASS is worth changed - the output of a negative control, marked
# "ctl| ", no longer counts as the bench's own FAIL. Every bench is measured again once.
versions="trx64-sim-env:4 driver=2 $(tool_versions)"

# deps_of <bench>: the specs of its line in ci/sims.deps, macros replaced, on one line.
# Exit 3: no line for it; 1: the file is wrong (a macro that is not defined).
deps_of() {
  awk -v want="$1" '
    function resolve(tok,   n, i, parts, out) {
      if (tok !~ /^@/ || tok == "@reads") return tok
      if (!(tok in text)) { bad = 1; return "" }
      n = split(text[tok], parts, /[ \t]+/); out = ""
      for (i = 1; i <= n; i++) if (parts[i] != "") out = out " " resolve(parts[i])
      return out
    }
    /^[ \t]*(#|$)/ { next }
    /^[ \t]/ { if (cur != "") text[cur] = text[cur] " " $0; next }
    {
      key = $1
      if (key ~ /^@/) { i = index($0, "="); } else { sub(/:$/, "", key); i = index($0, ":"); }
      text[key] = substr($0, i + 1); cur = key
    }
    END {
      if (!(want in text)) exit 3
      n = split(text[want], parts, /[ \t]+/); out = ""
      for (i = 1; i <= n; i++) if (parts[i] != "") out = out " " resolve(parts[i])
      if (bad) exit 1
      print out
    }' "$ROOT/ci/sims.deps" 2>/dev/null
}

# bench_specs <bench>: the pathspecs its key is made of, one per line, and whether they
# are its own (declared) or the whole tree's.
declare_specs() {
  local s rc f
  # T174 D5 (automatic): a bench with a stored record - committed, or the one an earlier run of this runner wrote - is keyed by it
  if [ -n "$shim" ] && f="$(simreads specs "$1" --cache "$reads_cache" 2>/dev/null)" && [ -n "$f" ]; then
    printf 'recorded\n'; printf '%s\n' "$f"; return 0
  fi
  s="$(deps_of "$1")"; rc=$?
  if [ $rc -eq 0 ] && [ -n "$s" ]; then
    case " $s " in
      *" @reads "*)
        # recorded (T174 D5): its files are the record's; without the preload to verify them, or without a record, the whole tree
        if [ -n "$shim" ] && f="$(simreads specs "$1" --cache "$reads_cache")" && [ -n "$f" ]; then
          printf 'recorded\n'; printf '%s\n' "$f"; return 0
        fi
        echo "$1: \`@reads\` without a usable record or preload: whole tree" >&2 ;;
      *) printf 'declared\n'; printf '%s\n' $s; return 0 ;;
    esac
  else
    [ $rc -eq 3 ] || echo "ci/sims.deps is wrong for $1: whole tree" >&2
  fi
  printf 'tree\n'; printf '%s\n' designs cores tools roms ci vendor/gideon
}

# owner_of <bench>: the lane that owns it, from ci/sims.lane (T100 D10): host | core:<id> | large:<id> | none; empty when the bench has no row.
# The owner is declared, not derived from the cores/<id>/ its ci/sims.deps line names: several benches read two cores' files (a
# negative control runs the other core's file, a shared Amiga source) and the one lane that answers for the bench is a decision.
# ci/orchestrate.py check keeps the table complete and consistent with ci/sims.deps.
owner_of() {
  awk -F: -v want="$1" '/^[ \t]*(#|$)/ { next } { k = $1; gsub(/[ \t]/, "", k); if (k == want) { v = $0; sub(/^[^:]*:[ \t]*/, "", v); print v; exit } }' "$ROOT/ci/sims.lane"
}
# owner_core <bench>: the core of its owner lane (empty for host, none and a bench without a row).
owner_core() { case "$(owner_of "$1")" in core:*) owner_of "$1" | sed 's/^core://' ;; large:*) owner_of "$1" | sed 's/^large://' ;; esac; }

# experimental <core>: 0 when the core's descriptor says experimental = true (T94 D8). The failure
# of such a core's bench is reported but does not fail the stage - the same rule the bitstream stage
# has for an experimental core, so one unfinished core cannot stop the host's build (Alex,
# 2026-10-14: "the first one please", on being asked which of the two ways to fix that).
experimental() {
  [ -n "$1" ] || return 1
  [ "$(python3 "$ROOT/tools/core_toml.py" get "$1" experimental 2>/dev/null)" = "true" ]
}

# bench_key <bench> <command>: key and kind (declared | tree) of the bench.
bench_key() {
  local name="$1" cmd="$2" kind k specs=() extra=""
  { read -r kind; while IFS= read -r k; do specs+=("$k"); done; } < <(declare_specs "$name")
  k="$(files_key "${specs[@]}")" || {
    echo "key of $name: a declared path is missing, whole tree instead" >&2
    kind=tree
    k="$(files_key designs cores tools roms ci vendor/gideon)"
  }
  if [ "$kind" = recorded ]; then
    extra="$(simreads extra "$name" --cache "$reads_cache" | sha256sum | cut -c1-16)"
    k="$k+$extra"
  fi
  printf '%s %s\n' "$(printf '%s\n%s\n%s\n%s\n' "$k" "$name" "$cmd" "$versions" | sha256sum | cut -c1-16)" "$kind"
}

# verdict_of <log> <rc> <tmo>
# A negative control is meant to fail and says so in its own words. Lines a bench prefixes with
# "ctl| " are the output of such a control: they are not read as the bench's verdict, neither their
# FAIL nor their PASS. Without this a bench that shows its control failing marked itself red with
# rc=0 (run 866: amiga_sdc, amiga_cfg and amiga_cpu020, each with a control that failed as required).
verdict_of() {
  if [ "$2" -eq 124 ]; then echo "TIMEOUT after ${3}s"
  elif [ "$2" -ne 0 ]; then echo "FAIL (exit $2)"
  elif grep -vE '^ctl\| ' "$1" | grep -qE 'FAIL|TIMEOUT'; then echo "FAIL (output)"
  elif ! grep -vE '^ctl\| ' "$1" | grep -qE '(^|[^A-Za-z])PASS(ED)?([^A-Za-z]|$)'; then echo "FAIL (no PASS line)"
  else echo PASS
  fi
}

# sandbox <bench> [all]: a directory with the files the bench declares and nothing else (all: every tracked file, for --record).
sandbox() {
  local dir s kind specs=() k
  dir="$(mktemp -d)"
  if [ "${2:-}" = all ]; then specs=(.)
  else
    { read -r kind; while IFS= read -r k; do specs+=("$k"); done; } < <(declare_specs "$1")
    case "$kind" in declared|recorded) ;; *) echo "$1 has no declaration to check" >&2; return 1 ;; esac
  fi
  git -C "$ROOT" ls-files -s -z -- "${specs[@]}" | python3 -c '
import os, shutil, sys
root, dst = sys.argv[1:3]
for ent in sys.stdin.buffer.read().split(b"\0"):
    if not ent: continue
    meta, path = ent.split(b"\t", 1)
    path = path.decode()
    src, to = os.path.join(root, path), os.path.join(dst, path)
    os.makedirs(os.path.dirname(to), exist_ok=True)
    if meta.split()[0] == b"160000":      # a submodule: the checkout itself, linked
        os.symlink(src, to)
    else:
        shutil.copy2(src, to, follow_symlinks=False)
' "$ROOT" "$dir" || return 1
  printf '%s\n' "$dir"
}

# threads_of <bench>: what it costs against the core budget (ci/sims.threads, "name: n"; no line = 1)
threads_of() {
  local n
  n="$(awk -F: -v want="$1" '/^[ \t]*(#|$)/ { next } { k = $1; gsub(/[ \t]/, "", k); if (k == want) { v = $2; gsub(/[ \t]/, "", v); print v; exit } }' "$ROOT/ci/sims.threads" 2>/dev/null)"
  case "$n" in ''|*[!0-9]*|0) echo 1 ;; *) echo "$n" ;; esac
}

# plan: what to run, with the keys
names=() tmos=() cmds=() tiers=() keys=() kinds=() cached=()
n_run=0
while IFS='|' read -r t name tmo cmd; do
  case "$t" in ''|'#'*) continue ;; esac
  if [ -z "$check$keyonly$record" ]; then [ "$t" -le "$tier" ] || continue; fi
  [ "$want" = "  " ] || [[ "$want" == *" $name "* ]] || continue
  read -r key kind < <(bench_key "$name" "$cmd")
  names+=("$name"); tmos+=("$tmo"); cmds+=("$cmd"); tiers+=("$t"); keys+=("$key"); kinds+=("$kind")
  if [ -z "$force" ] && [ -n "$cache_on" ] && [ -f "$ok_dir/$name.$key" ]; then
    cached+=(1)
  else
    cached+=(""); n_run=$((n_run + 1))
  fi
done < "$ROOT/ci/sims.txt"
if [ ${#names[@]} -eq 0 ]; then
  [ -z "$from_plan" ] || { echo "no bench selected: none of the planned benches is in tier <= $tier"; exit 0; }
  echo "no bench selected"; exit 1
fi

# --limit (T174 D5.3, Alex: "a child's time limit comes from the benches it runs"): the wall time the benches that would run may take, from
# their own timeouts in ci/sims.txt and their threads against the core budget: all of them one after the other on the budget (sum of
# timeout x threads / budget) plus the longest one (a list schedule is never later), plus 5 minutes. Cached benches cost nothing. ci/child.sh
# stops the sim child at this limit.
if [ -n "$limitonly" ]; then
  tot=0 longest=0
  for i in "${!names[@]}"; do
    [ -z "${cached[$i]}" ] || continue
    w="$(threads_of "${names[$i]}")"; [ "$w" -le "$jobs" ] || w="$jobs"
    tot=$(( tot + tmos[i] * w )); [ "${tmos[$i]}" -le "$longest" ] || longest="${tmos[$i]}"
  done
  secs=$(( (tot + jobs - 1) / jobs + longest + 300 ))
  echo $(( (secs + 59) / 60 ))
  exit 0
fi

# --key: what each key is made of, nothing runs
if [ -n "$keyonly" ]; then
  for i in "${!names[@]}"; do
    nf="$({ read -r _; cat; } < <(declare_specs "${names[$i]}") | wc -l | tr -d ' ')"
    printf 'KEY %s %s %s files=%s%s\n' "${names[$i]}" "${keys[$i]}" "${kinds[$i]}" "$nf" "$([ -z "${cached[$i]}" ] || echo ' (a PASS marker exists)')"
  done
  exit 0
fi

# --record: each bench in a clean copy of the tracked files (no build output to skip a read), the preload on, the reads into ci/sims.reads.
# --record-seconds N stops a long bench after N seconds: what it read until then is the record, the first full run extends the runner's copy.
if [ -n "$record" ]; then
  [ -n "$shim" ] || { echo "--record needs ci/simreads.c built (gcc)" >&2; exit 2; }
  rc_all=0
  for i in "${!names[@]}"; do
    name="${names[$i]}"; secs="${record_secs:-${tmos[$i]}}"
    sb="$(sandbox "$name" all)" || { rc_all=1; continue; }
    rlog="$logdir/$name.rec.log"; rdeps="$logdir/$name.rec.deps"; rm -f "$rdeps"
    start=$(date +%s)
    (cd "$sb" && PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$sb/.pyc" LD_PRELOAD="$shim" SIM_READS_ROOT="$sb" SIM_DEPS_LOG="$rdeps" \
      timeout "$secs" bash -c "set -o pipefail; ${cmds[$i]}") > "$rlog" 2>&1 < /dev/null
    rc=$?
    rm -rf "$sb"
    verdict="$(verdict_of "$rlog" "$rc" "$secs")"
    if [ "$verdict" != PASS ] && ! { [ -n "$record_secs" ] && [ "$rc" -eq 124 ]; }; then
      echo "RECORD $name: $verdict, no record written (log $rlog)"; rc_all=1; continue
    fi
    if [ ! -s "$rdeps" ]; then echo "RECORD $name: the preload logged nothing, no record written"; rc_all=1; continue; fi
    simreads normalize "$rdeps" | simreads put "$name"
    printf 'RECORD %s: %s in %ds, %s lines (%s files)\n' "$name" "$verdict" $(( $(date +%s) - start )) \
      "$(simreads lines "$name" | wc -l | tr -d ' ')" "$(simreads specs "$name" | wc -l | tr -d ' ')"
  done
  exit $rc_all
fi

[ "$n_run" -eq 0 ] || heavy_lock

# run_one <bench>: in a worker. What the stage prints for the bench goes to
# $logdir/<bench>.out, the verdict to <bench>.result (verdict|seconds|kind|note).
run_one() {
  local name="$1" tmo cmd key kind verdict note="" rc start secs sb specs k missing log
  # shellcheck disable=SC1090
  source "$logdir/$name.job"
  start=$(date +%s)
  log="$logdir/$name.log"
  rm -f "$logdir/$name.deps" "$logdir/$name.out"
  if [ -n "$check" ]; then
    if ! sb="$(sandbox "$name")"; then echo "no sandbox for $name" > "$logdir/$name.out"; echo "no sandbox||$kind|" > "$logdir/$name.result"; return 0; fi
    (cd "$sb" && timeout "$tmo" bash -c "set -o pipefail; $cmd") > "$log" 2>&1 < /dev/null
    rc=$?
    rm -rf "$sb"
  elif [ "$kind" = recorded ] || { [ -n "$shim" ] && [ "$shim" != key-only ]; }; then
    # T174 D5: every bench runs under the preload (a bench without a record gets one from this run). The preload logs every file the process tree opens; python reads its sources (no stale .pyc stands in for them)
    PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$logdir/$name.pyc" LD_PRELOAD="$shim" SIM_READS_ROOT="$ROOT" SIM_DEPS_LOG="$logdir/$name.deps" \
      timeout "$tmo" bash -c "set -o pipefail; $cmd" > "$log" 2>&1 < /dev/null
    rc=$?
    rm -rf "$logdir/$name.pyc"
  else
    SIM_DEPS_LOG="$logdir/$name.deps" timeout "$tmo" bash -c "set -o pipefail; $cmd" > "$log" 2>&1 < /dev/null
    rc=$?
  fi
  secs=$(( $(date +%s) - start ))
  verdict="$(verdict_of "$log" "$rc" "$tmo")"
  auto=""
  if [ "$verdict" = PASS ] && [ -z "$check" ] && [ "$kind" != recorded ] && [ -n "$shim" ] && [ "$shim" != key-only ]; then
    # no record yet: this run's reads become the runner's record, and the PASS goes under the key of that record (only when the preload logged)
    simreads normalize "$logdir/$name.deps" > "$logdir/$name.reads" 2>/dev/null || : > "$logdir/$name.reads"
    if [ -s "$logdir/$name.reads" ] && [ -n "$cache_on" ] && simreads cache "$name" --cache "$reads_cache" < "$logdir/$name.reads"; then
      read -r key kind < <(bench_key "$name" "$cmd"); auto=1; note=" (record taken)"
    fi
  fi
  if [ -z "$auto" ] && [ "$verdict" = PASS ] && [ -z "$check" ] && [ "$kind" = declared ] && [ -s "$logdir/$name.deps" ]; then
    # the files the bench says it read, against what its deps cover
    specs=()
    { read -r _; while IFS= read -r k; do specs+=("$k"); done; } < <(declare_specs "$name")
    git -C "$ROOT" ls-files -- "${specs[@]}" | sort -u > "$logdir/$name.covered"
    missing="$(sort -u "$logdir/$name.deps" | grep -vxFf "$logdir/$name.covered" || true)"
    if [ -n "$missing" ]; then
      { echo "DEPS INCOMPLETE: $name read, and ci/sims.deps does not list:"; printf '  %s\n' $missing; } >> "$logdir/$name.out"
      note=" (deps incomplete, not cached)"
      kind=incomplete
    fi
  fi
  if [ "$verdict" = PASS ] && [ -z "$check" ] && [ "$kind" = recorded ]; then
    # T174 D5: what the run really read, against the record. Empty: the preload did not log (never cache what was not watched). More: the
    # runner's copy of the record grows and the marker goes under the key of the grown record, which is the content the run saw.
    simreads normalize "$logdir/$name.deps" > "$logdir/$name.reads" 2>/dev/null || : > "$logdir/$name.reads"
    if [ ! -s "$logdir/$name.reads" ]; then
      echo "NO READS LOGGED: $name; not cached" >> "$logdir/$name.out"
      note=" (no reads logged, not cached)"; kind=incomplete
    else
      missing="$(simreads missing "$name" "$logdir/$name.deps" --cache "$reads_cache")"
      if [ -n "$missing" ]; then
        { echo "RECORD EXTENDED: $name read, and ci/sims.reads does not hold ($(echo "$missing" | wc -l | tr -d ' ') lines):"; printf '  %s\n' $missing | head -40; } >> "$logdir/$name.out"
        if [ -n "$cache_on" ] && simreads cache "$name" --cache "$reads_cache" < "$logdir/$name.reads"; then
          read -r key _ < <(bench_key "$name" "$cmd")
          note=" (record extended)"
        else
          note=" (record extended, cache not writable, not cached)"; kind=incomplete
        fi
      fi
    fi
  fi
  if [ "$verdict" != PASS ]; then
    tail -25 "$log" >> "$logdir/$name.out"
  elif [ -z "$check" ] && [ -n "$cache_on" ] && [ "$kind" != incomplete ]; then
    printf '%s %s\n' "$(git -C "$ROOT" rev-parse --short=8 HEAD)" "$(date -u +%FT%TZ)" > "$ok_dir/$name.$key"
    # shellcheck disable=SC2012
    ls -1t "$ok_dir/$name".* | tail -n +4 | xargs rm -f
  fi
  printf '%s|%s|%s|%s\n' "$verdict$note" "$secs" "$kind" "$note" > "$logdir/$name.result"
  printf -- '--- done %s: %s, %ds\n' "$name" "$verdict$note" "$secs"
}

run_names=()
for i in "${!names[@]}"; do
  [ -z "${cached[$i]}" ] || continue
  name="${names[$i]}"
  printf 'tmo=%q cmd=%q key=%q kind=%q\n' "${tmos[$i]}" "${cmds[$i]}" "${keys[$i]}" "${kinds[$i]}" > "$logdir/$name.job"
  run_names+=("${tmos[$i]} $name")
done
if [ ${#run_names[@]} -gt 0 ]; then
  echo "=== ${#run_names[@]} of ${#names[@]} benches to run, a budget of $jobs cores on $cpus cpus (threads per bench: ci/sims.threads)"
  # the longest first; a bench starts when its threads fit into the free part of the budget (a bench wider than the budget runs alone)
  pids=() pid_w=() used=0
  reap() {
    local k alive=() alive_w=()
    for k in "${!pids[@]}"; do
      if kill -0 "${pids[$k]}" 2>/dev/null; then alive+=("${pids[$k]}"); alive_w+=("${pid_w[$k]}")
      else wait "${pids[$k]}" 2>/dev/null; used=$((used - pid_w[k])); fi
    done
    pids=("${alive[@]}"); pid_w=("${alive_w[@]}")
  }
  while read -r name; do
    w="$(threads_of "$name")"
    while :; do
      reap
      [ "$used" -eq 0 ] && break
      [ $((used + w)) -le "$jobs" ] && break
      sleep 2
    done
    echo "--- start $name ($w threads, $((used + w)) of $jobs)"
    run_one "$name" &
    pids+=("$!"); pid_w+=("$w"); used=$((used + w))
  done < <(printf '%s\n' "${run_names[@]}" | sort -rn | cut -d' ' -f2)
  wait
fi

failed=()
failed_at=()      # the reds per owner: host, dfx-<core>, large-<core> (T174 D4: the parent does not start that owner's Vivado run)
soft=()
summary=()
[ -n "$check" ] || red_reset sims
for i in "${!names[@]}"; do
  name="${names[$i]}"
  if [ -n "${cached[$i]}" ]; then
    echo "=== $name (tier ${tiers[$i]}): cached PASS ${keys[$i]} ($(cat "$ok_dir/$name.${keys[$i]}"))"
    summary+=("$(printf '%-16s %-22s %4ds  %s' "$name" "cached PASS" 0 "${keys[$i]} ${kinds[$i]}")")
    continue
  fi
  echo "=== $name (tier ${tiers[$i]}) key ${keys[$i]} ${kinds[$i]}"
  [ ! -f "$logdir/$name.out" ] || cat "$logdir/$name.out"
  if [ ! -f "$logdir/$name.result" ]; then
    failed+=("$name"); summary+=("$(printf '%-16s %-22s' "$name" "no result")"); continue
  fi
  IFS='|' read -r verdict secs _ < "$logdir/$name.result"
  note=""
  case "$verdict" in
    PASS*) ;;
    *) # reported and not fatal only when the lane that owns the bench is an experimental core (T94 D8, the owner by ci/sims.lane since D10);
       # the host lane's and an unowned bench's failure is always the pipeline's problem
       core="$(owner_core "$name")"
       if [ -n "$core" ] && experimental "$core"; then
         soft+=("$name ($core)"); note="  experimental core $core owns it: reported, not fatal"
       else
         failed+=("$name")
         case "$(owner_of "$name")" in host) failed_at+=("host $name: $verdict") ;; core:*) failed_at+=("dfx-$core $name: $verdict") ;; large:*) failed_at+=("large-$core $name: $verdict") ;; *) failed_at+=("other $name: $verdict") ;; esac
       fi ;;
  esac
  summary+=("$(printf '%-16s %-22s %4ss%s' "$name" "$verdict" "$secs" "$note")")
done

echo
echo "=== summary (tier $tier)"
printf '%s\n' "${summary[@]}"
if [ ${#soft[@]} -gt 0 ]; then echo "EXPERIMENTAL (reported, not fatal): ${soft[*]}"; fi
if [ ${#failed[@]} -gt 0 ]; then
  echo "FAILED: ${failed[*]}"
  # T174 D2.3/D4: a red bench is a red target of its owner, with its verdict ("TIMEOUT after 1800s"); the stage is red at once and the parent
  # does not start that owner's Vivado run.
  if [ -z "$check" ]; then
    for fa in "${failed_at[@]}"; do mark_red sims "${fa%% *}" "bench ${fa#* }"; done
  fi
  exit 1
fi
echo "=== sims done in $(lap)"
