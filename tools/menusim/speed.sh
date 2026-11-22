#!/usr/bin/env bash
# T239.3 P7: how much the menu draws per key press, per core, in one table (the figure that stands for the board's time: the board renders in
# software into DDR2, so its time grows with the pixels flushed).
#
#   tools/menusim/speed.sh [--tree DIR] [--mode M]... [--long] [--lines] <core>...   the key sequence of speed_keys.txt, headless, one table
#   --mode M   a mode of menusim (default 1080p; repeat for more: 1080p 480p); --lines  every key's line as well;
#   --long     the sequence of speed_keys_long.txt (settings list down 40, up 40: scroll steps) instead
#   env MENUSIM_OPT=-O2   the application sources at another optimisation level (default -O0, as run.sh); times are the Mac's, not the board's
#
# Per core and mode: the keys, the flushed pixels per key press (mean over all keys, max, and mean over UP/DOWN only), the flush calls and the
# objects in the flushed areas per key press (UP/DOWN), the UP/DOWN key's time on this machine (us), the state functions the stubs answered per
# UP/DOWN key. menusim names: tools/menusim/run.sh --list (the TRX64U is "c64").
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TREE=(); MODES=(); LINES=""; CORES=(); SEQ=speed_keys.txt
while [ $# -gt 0 ]; do
  case "$1" in
    --tree) TREE=(--tree "${2:?--tree needs a directory}"); shift 2 ;;
    --mode) MODES+=("${2:?--mode needs a mode}"); shift 2 ;;
    --lines) LINES=1; shift ;;
    --long) SEQ=speed_keys_long.txt; shift ;;
    -*) echo "speed.sh: unknown option $1" >&2; exit 2 ;;
    *) CORES+=("$1"); shift ;;
  esac
done
[ ${#CORES[@]} -gt 0 ] || { sed -n 2,14p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ ${#MODES[@]} -gt 0 ] || MODES=(1080p)
KEYS="$(grep -v '^#' "$HERE/$SEQ" | tr -d ' \n')"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
printf '%-10s %-6s %-8s %4s %10s %10s %10s %6s %6s %8s %7s\n' core mode canvas keys px/key_all px_max px/move calls objs us/move st/move
for c in "${CORES[@]}"; do
  for m in "${MODES[@]}"; do
    f="$W/$c-$m.txt"
    # a core the build does not know ends the run with menusim's own sentence; the build's progress line goes to stderr
    "$HERE/run.sh" ${TREE[@]+"${TREE[@]}"} "$c" --mode "$m" --keys "$KEYS" --stats "$f" > "$W/log" 2>&1 || { cat "$W/log" >&2; exit 1; }
    [ -z "$LINES" ] || grep '^KEY' "$f"
    python3 - "$f" "$c" "$m" <<'PY'
import re, sys
f, core, mode = sys.argv[1:4]
rows, s_all, s_mv, canvas = [], {}, {}, '?'
for l in open(f):
    if l.startswith('#'):
        canvas = dict(re.findall(r'(\w+)=(\S+)', l)).get('canvas', '?')
    kv = dict(re.findall(r'(\w+)=(\S+)', l))
    if l.startswith('KEY'): rows.append((kv['key'], kv))
    elif l.startswith('SUMMARY'): s_all = kv
    elif l.startswith('MOVES'): s_mv = kv
mv = [kv for k, kv in rows if k in ('UP', 'DOWN')]
st = sum(int(kv['states']) for kv in mv) / max(len(mv), 1)
print('%-10s %-6s %-8s %4s %10s %10s %10s %6s %6s %8s %7.0f' % (core, mode, canvas, s_all['keys'], s_all['px_mean'], s_all['px_max'], s_mv['px_mean'],
      s_mv['calls_mean'], s_mv['objs_mean'], s_mv['us_mean'], st))
PY
  done
done
