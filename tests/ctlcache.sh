# shellcheck shell=bash
# Sourced by tests/coreapp/run.sh, tests/hostapp/run.sh, tests/loader/run.sh and the render benches (T100 D13, D14): every test program, negative
# control and render bench is a UNIT whose outcome is a function of what it reads, so a unit whose reads are unchanged is not built or run again.
#
# What a unit reads is two things (the long form is in tests/ctlcache.py):
#   the KEY      the compiler's -MM dependency list of the unit's build (test, sources, every header, from the real preprocessor), the unit's
#                id (name, sed, sources, flags, compiler line), the files of its run arguments;
#   the MANIFEST every file, listing or missing path under the tree the run opened, recorded by tests/readlog.c (LD_PRELOAD, Linux) while the
#                unit passed, with the content hash of each (the fixtures a suite generated into $T count by content, like a file of the
#                tree); a hit needs all of them to match now. Nothing is declared by hand.
# Where the recorder cannot run (macOS) the key is the old coarse one (all suite data files and tools/*.py) and there is no manifest.
# A unit whose key cannot be made (no dependency list) runs; one that failed stores nothing.
#
#   unit NAME: cached PASS KEY | PASS Ns        test programs      control NAME: cached (fails as it must) | fails as it must
#   CONTROLS_ALL=1       ignore the cache, run every unit (the result is stored again)
#   CONTROLS_CACHE=dir   where the entries live; default /cache/multicore/controls-ok on a runner, else $ROOT/.ci/controls-ok (ignored by git)
#   CC_MODE=coarse       do not use the recorder
#   TEST_TIER=fast       (T174 D1, set by ci/apptests.sh --tier fast) negative controls are not even keyed: cc_no_controls is true and every control wrapper
#                        returns at once; integration and nightly run them (from the cache when their reads are unchanged)
#   CC_DRYRUN=1          (ci/apptests.sh --plan) nothing is built or run: a unit that would run prints "plan: would run NAME (why)" and counts as served,
#                        so the suite walks through all its keys in the time of the key computation alone
#   CC_KIND=control      prefix of a cc_block that is a negative control (the key functions and cc_summary count it so)
#
# THE KEY VERSION (T174 D3): a unit's key does not carry the bytes of the harness (this file, ctlcache.py, readlog.c). It carries CC_KEY_VERSION, which is
# raised in the SAME commit as every change to the keying or to the verdict: what goes into a key, what a manifest records, when a hit is valid, what
# PASS / "fails as it must" mean. A comment-only edit leaves every cached unit valid. tools/check_cc_key_version.py (lint) refuses a commit that changes the code
# of the three files (comments, blank lines and this line left out) without raising the number. A suite's own run.sh is still part of its units' keys
# (the function closures and the dependency lists), because it is the test.
CC_KEY_VERSION=3
# Needs $ROOT, $CXX and $T of the caller (T: the suite's scratch directory).
CC_PY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ctlcache.py"
CC_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -z "${CONTROLS_CACHE:-}" ]; then
  if [ -d /cache ] && [ -w /cache ]; then CONTROLS_CACHE=/cache/multicore/controls-ok; else CONTROLS_CACHE="$ROOT/.ci/controls-ok"; fi
fi
CC_DIR="$CONTROLS_CACHE"
mkdir -p "$CC_DIR" 2>/dev/null || CC_DIR=""
[ -n "$CC_DIR" ] && find "$CC_DIR" -type f -mtime +21 -exec rm -f {} + 2>/dev/null || true   # keys of trees long gone
CC_UNITS="$T/units.log"; : > "$CC_UNITS"
mkdir -p "$T/ccargs"
if [ -z "${CC_MODE:-}" ]; then
  if [ "$(uname -s)" = Linux ] && command -v cc >/dev/null && cc -shared -fPIC -O1 -o "$T/readlog.so" "$CC_SRC/readlog.c" -ldl 2>/dev/null; then CC_MODE=dyn; else CC_MODE=coarse; fi
fi
[ "$CC_MODE" = dyn ] && [ ! -f "$T/readlog.so" ] && { cc -shared -fPIC -O1 -o "$T/readlog.so" "$CC_SRC/readlog.c" -ldl 2>/dev/null || CC_MODE=coarse; }

# cc_key_run SUITE ID RUNARGS FLAGS-AND-INPUTS-READ-FROM-STDIN... : shared tail of the two key functions; $1 = -MM output on stdin
_cc_key_py() {   # _cc_key_py SUITE ID RUNARGS ; stdin: the -MM output
  local suite="$1" id="$2" runargs="$3" key
  # shellcheck disable=SC2086
  key="$(python3 -I "$CC_PY" key --key-version "$CC_KEY_VERSION" --root "$ROOT" --tmp "$T" --mode "$CC_MODE" --suite-dir "tests/$suite" --runargs "$runargs" \
    --id "$id | $("${CXX:-c++}" --version 2>/dev/null | head -1) | $suite" --extra ${CC_EXTRA:-} 2>/dev/null)" || return 0
  [ -n "$key" ] || return 0
  printf '%s\n%s\n' "$runargs" "${CC_EXTRA:-}" > "$T/ccargs/$key"   # what cc_store needs again
  printf '%s' "$key"
}
# cc_key SUITE ID [--runargs STR] -- COMPILER-FLAGS... -- INPUTS... : prints the key, or nothing (= no caching for this unit)
# when the preprocessor could not list the dependencies. INPUTS are the files the unit compiles (.o inputs are ignored; name their
# source in CC_EXTRA). The listing is made of the UNMUTATED tree, so the mutated file is in it, and the sed is part of ID.
cc_key() {
  local suite="$1" id="$2"; shift 2
  local runargs=""
  if [ "${1:-}" = "--runargs" ]; then runargs="$2"; shift 2; fi
  [ "${1:-}" = "--" ] && shift
  local fl=() in=() seen=""
  while [ $# -gt 0 ]; do
    if [ "$1" = "--" ] && [ -z "$seen" ]; then seen=1; shift; continue; fi
    if [ -z "$seen" ]; then fl+=("$1"); else case "$1" in *.o) ;; *) in+=("$1") ;; esac; fi
    shift
  done
  local deps
  deps="$("${CXX:-c++}" "${fl[@]}" -MM "${in[@]}" 2>/dev/null)" || return 0
  [ -n "$deps" ] || return 0
  [ -z "${CC_DEPS_HOOK:-}" ] || printf '%s\n' "$deps" | "$CC_DEPS_HOOK"   # T174 D3: the suite may check the listing (tests/coreapp/run.sh: groups)
  printf '%s\n' "$deps" | _cc_key_py "$suite" "$id | ${fl[*]} | ${in[*]}" "$runargs"
}
# cc_key_cmd SUITE ID [--runargs STR] -- COMPILER ARGV... : the same for a build given as one command line (-o OUT and .o inputs are dropped)
cc_key_cmd() {
  local suite="$1" id="$2"; shift 2
  local runargs=""
  if [ "${1:-}" = "--runargs" ]; then runargs="$2"; shift 2; fi
  [ "${1:-}" = "--" ] && shift
  local args=() skip="" a
  for a in "$@"; do
    if [ -n "$skip" ]; then skip=""; continue; fi
    case "$a" in -o) skip=1 ;; *.o) ;; *) args+=("$a") ;; esac
  done
  local deps
  deps="$("${args[@]}" -MM 2>/dev/null)" || return 0
  [ -n "$deps" ] || return 0
  printf '%s\n' "$deps" | _cc_key_py "$suite" "$id | ${args[*]}" "$runargs"
}
cc_no_controls() { [ "${TEST_TIER:-}" = fast ]; }
# T174 R1a: why a unit with no entry misses. The text a block's key is made of is kept per name (and per suite) under $CC_DIR/../keytext; when the entry
# is missing, the line that differs from the last text kept for the same name is printed ("miss NAME: no entry; key text changed: <variable>").
_cc_why_miss() {   # _cc_why_miss NAME TEXTFILE
  local d="${CC_DIR%/*}/keytext" last diffl
  [ -n "${CC_DIR:-}" ] && [ -f "${2:-/nonexistent}" ] || return 0
  mkdir -p "$d" 2>/dev/null || return 0
  last="$d/$(printf '%s' "${CC_SUITE:-x}_$1" | tr -c 'A-Za-z0-9_' '_')"
  if [ -f "$last" ]; then
    diffl="$(diff "$last" "$2" 2>/dev/null | grep '^[<>]' | head -4 | cut -c1-160 | tr '\n' '|' || true)"
    [ -n "$diffl" ] && echo "miss $1: no entry for this key; key text differs from the last one kept: $diffl" || echo "miss $1: no entry for this key (key text is the same as the last one kept: the reads or the tree changed)"
  else
    echo "miss $1: no entry for this key (first time seen here)"
  fi
  cp "$2" "$last.tmp$$" 2>/dev/null && mv "$last.tmp$$" "$last" 2>/dev/null || true
}
_cc_note() { printf '%s\n' "$*" >> "$CC_UNITS" 2>/dev/null || true; }
# cc_cached NAME KEY [test] : 0 (and says so) when KEY passed before and everything its run read is as it was; "test" for a test program
cc_cached() {
  local kind="${3:-control}" why=""
  if ! { [ -n "${2:-}" ] && [ -n "$CC_DIR" ] && [ -z "${CONTROLS_ALL:-}" ] && [ -f "$CC_DIR/$2" ]; }; then
    [ -n "${CC_DRYRUN:-}" ] || return 1
    why="no entry"; [ -n "${CONTROLS_ALL:-}" ] && why="forced"; [ -n "${2:-}" ] || why="no key"
    echo "plan: would run $kind $1 ($why)"; _cc_note "W $kind $1"; return 0
  fi
  if [ "$CC_MODE" = dyn ]; then
    why="$(CC_WHY=1 python3 -I "$CC_PY" verify --root "$ROOT" --tmp "$T" --entry "$CC_DIR/$2" 2>&1)" || {
      if [ -n "${CC_DRYRUN:-}" ]; then echo "plan: would run $kind $1 (reads changed: ${why#changed: })"; _cc_note "W $kind $1"; return 0; fi
      echo "$kind $1: reads changed (${why#changed: }), runs again"; return 1; }
  fi
  [ -n "${CC_DRYRUN:-}" ] || touch "$CC_DIR/$2" 2>/dev/null || true
  _cc_note "C $kind $1"
  if [ "$kind" = test ]; then echo "test $1: cached PASS $2"; else echo "control $1: cached (fails as it must) $2"; fi
}
# cc_exec LOGBASE COMMAND... : the command, with the recorder when there is one; LOGBASE is the file the reads go to
cc_exec() {
  local log="$1"; shift
  if [ "$CC_MODE" = dyn ]; then
    : > "$log"; : > "$log.start"
    RL_LOG="$log" LD_PRELOAD="$T/readlog.so${LD_PRELOAD:+:$LD_PRELOAD}" "$@"
  else
    "$@"
  fi
}
# cc_store NAME KEY [LOGBASE] [test] : the unit just passed (a control: failed as it must); LOGBASE is what cc_exec recorded
cc_store() {
  local kind="${4:-control}" man=""
  _cc_note "R $kind $1"
  [ -n "${2:-}" ] && [ -n "$CC_DIR" ] || return 0
  if [ "$CC_MODE" = dyn ]; then
    [ -n "${3:-}" ] && [ -f "$3" ] || { echo "$kind $1: not cached (no read log)"; return 0; }
    local ra ex; ra="$(sed -n 1p "$T/ccargs/$2" 2>/dev/null)"; ex="$(sed -n 2p "$T/ccargs/$2" 2>/dev/null)"
    # shellcheck disable=SC2086
    man="$(python3 -I "$CC_PY" record --root "$ROOT" --tmp "$T" --log "$3" --runargs "$ra" --extra $ex 2>"$3.err")" || {
      echo "$kind $1: NOT cached - $(cat "$3.err")"; return 0; }
  fi
  { printf '%s %s\n' "$1" "$(date -u +%FT%TZ)"; [ -n "$man" ] && printf '%s\n' "$man"; true; } > "$CC_DIR/$2.tmp$$" && mv "$CC_DIR/$2.tmp$$" "$CC_DIR/$2" || true
}
# _cc_closure FN... : what a shell function stands on, as text for its key: the text of FN and of every function it names (transitively, by word),
# and the value of every variable they mention (declare -p; the specials that change by themselves left out). Variables that hold file lists
# (SRCS, DESCS, an array of sources) are in the key by value, so a file added to a list moves the key; the manifest cannot see a file nobody opened yet.
_cc_closure() {
  local todo=("$@") seen=" " f t w v vars="" own=" "
  while [ ${#todo[@]} -gt 0 ]; do
    f="${todo[0]}"; todo=("${todo[@]:1}")
    case "$seen" in *" $f "*) continue ;; esac
    seen="$seen$f "
    t="$(declare -f "$f" 2>/dev/null)" || continue
    printf '%s\n' "$t"
    for w in $(printf '%s' "$t" | grep -oE '[A-Za-z_][A-Za-z0-9_]*' | sort -u); do
      if declare -F "$w" >/dev/null 2>&1; then todo+=("$w"); fi
    done
    vars="$vars $(printf '%s' "$t" | grep -oE '\$\{?[#!]?[A-Za-z_][A-Za-z0-9_]*' | tr -d '${#!' | sort -u | tr '\n' ' ')"
    # a variable the function assigns, loops over or declares local is its own product: a loop's leftover would make the key depend on what ran before
    own="$own$(printf '%s' "$t" | grep -oE '[A-Za-z_][A-Za-z0-9_]*\+?=|for [A-Za-z_][A-Za-z0-9_]* in|local [^;&|]*' | sed -E 's/^for //; s/ in$//; s/^local //; s/\+?=$//' | tr '\n' ' ') "
  done
  for v in $(printf '%s\n' $vars | sort -u); do
    case "$own" in *" $v "*) continue ;; esac
    case "$v" in RANDOM|SECONDS|LINENO|BASHPID|EPOCHSECONDS|EPOCHREALTIME|SRANDOM|_|FUNCNAME|BASH_*|PIPESTATUS|OPTIND|OPTARG|IFS|PWD|OLDPWD|PATH|HOME|JOBS|PX|PFC|PENDING|OBJS|OBJS32|OUT|B|T|JOBDIR|W_OUT) continue ;; esac   # OBJS: set by need_objs when the first unit that builds runs, so it differs with what was cached; LVGL is in CC_KEYEXTRA
    declare -p "$v" 2>/dev/null || true
  done
}
# cc_block NAME FN [HELPER-FN...] [-- ARGS...] : a unit that is a shell function (a test program with its compile and run, a sub-script of
# the suite). The key is the function's closure (_cc_closure: its text, the functions it calls, the values of the variables it mentions), the
# arguments and CC_KEYEXTRA; the manifest is everything the function and every process under it opened, THE COMPILER'S HEADERS INCLUDED - so no
# dependency list is needed. The function runs in the suite's shell, under its set -e; failure ends the suite and stores nothing; it must end
# with a status 0 of its own (return 0). Shell builtins read through the shell's own libc, which the recorder does not see: a block reads its
# files with external commands (cat, sed, cmp, python3, the compiler). A value that reaches the function other than by a variable it mentions
# (a function-prefix assignment, a value computed from files outside the function) goes in CC_KEYEXTRA or the arguments.
cc_block() {
  local name="$1"; shift
  local fns=() key="" t0=$SECONDS log kind="${CC_KIND:-test}" ktxt
  while [ $# -gt 0 ] && [ "$1" != -- ]; do fns+=("$1"); shift; done
  [ "${1:-}" = -- ] && shift
  log="$T/rl_blk_$(printf '%s' "$name" | tr -c 'A-Za-z0-9_' '_')"
  [ -z "${CC_PRE:-}" ] || "$CC_PRE"   # e.g. put the fixtures back as they were made: an earlier unit may have written into them
  if [ "$kind" = control ] && cc_no_controls; then _cc_note "S control $name"; return 0; fi
  if [ "$CC_MODE" = dyn ] && [ -z "${UPDATE_GOLDEN:-}" ]; then
    ktxt="block $name | ${CC_KEYEXTRA:-} | $* | $(_cc_closure "${fns[@]}")"
    key="$(printf '' | _cc_key_py "${CC_SUITE:-x}" "$ktxt" "")"
    printf '%s' "$ktxt" | sed -e "s|$T|<T>|g" -e "s|$ROOT|<root>|g" > "$log.keytext"   # the stable form: what differs between two runs is a real input
  fi
  if cc_cached "$name" "$key" "$kind"; then return 0; fi
  [ -n "$key" ] && [ ! -f "${CC_DIR:-/nonexistent}/$key" ] && _cc_why_miss "$name" "$log.keytext"
  cc_exec "$log" "${fns[0]}" "$@"
  if [ "$kind" = control ]; then echo "control $name: fails as it must $((SECONDS - t0))s"; else echo "test $name: PASS $((SECONDS - t0))s"; fi
  cc_store "$name" "$key" "$log" "$kind"
}
# cc_summary : one line, how many units ran and how many came from the cache (call it at the end of the suite and from the exit trap)
cc_summary() {
  local rt rc ct cc
  [ -s "$CC_UNITS" ] || return 0
  local wr sk
  wr="$(grep -c '^W ' "$CC_UNITS" || true)"; sk="$(grep -c '^S ' "$CC_UNITS" || true)"
  if [ -n "${CC_DRYRUN:-}" ]; then echo "plan ($CC_MODE): would run $wr, from the cache $(grep -c '^C ' "$CC_UNITS" || true), controls skipped $sk"; return 0; fi
  rt="$(grep -c '^R test ' "$CC_UNITS" || true)"; rc="$(grep -c '^R control ' "$CC_UNITS" || true)"
  ct="$(grep -c '^C test ' "$CC_UNITS" || true)"; cc="$(grep -c '^C control ' "$CC_UNITS" || true)"
  echo "units ($CC_MODE): ran $((rt + rc)) (tests $rt, controls $rc), from the cache $((ct + cc)) (tests $ct, controls $cc)$([ "$sk" -gt 0 ] && echo ", controls skipped (tier fast) $sk")"
}
