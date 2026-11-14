#!/bin/sh
# Run the benches of a core (or named benches) on the builder, in the CI bench image, for the commit HEAD - the check before a merge into main
# (T100 D7 replaced, Alex 2026-10-30). The builder's bench cache is mounted, so the PASS markers are shared with the nightly: a bench whose inputs
# did not change is "cached PASS" in seconds.
#   tools/benches.sh <core-id|bench-name>... [-f] [-n]
#   core-id   host, or a core directory name (amiga, amiga1200, apple2, ...): every bench ci/sims.lane gives to it (owner host | core:<id> | large:<id>)
#   -f        ignore the PASS markers      -n  only print the resolved bench list, run nothing
set -eu
cd "$(dirname "$0")/.."
force="" dry="" args=""
for a in "$@"; do
  case "$a" in
    -f) force=-f ;;
    -n) dry=1 ;;
    -*) echo "usage: tools/benches.sh <core-id|bench-name>... [-f] [-n]" >&2; exit 2 ;;
    *) args="$args $a" ;;
  esac
done
[ -n "$args" ] || { echo "usage: tools/benches.sh <core-id|bench-name>... [-f] [-n]" >&2; exit 2; }
benches=""
for a in $args; do
  b=$(awk -v a="$a" '
    /^[ \t]*#/ || NF < 2 { next }
    { n = $1; sub(/:$/, "", n); o = $2 }
    n == a || o == "core:" a || o == "large:" a || (a == "host" && o == "host") { print n }' ci/sims.lane)
  [ -n "$b" ] || { echo "benches: no bench or core '$a' in ci/sims.lane" >&2; exit 2; }
  benches="$benches $b"
done
benches=$(printf '%s\n' $benches | awk '!seen[$0]++' | tr '\n' ' ')
benches=${benches% }
if [ -n "$dry" ]; then echo "$benches"; exit 0; fi
rev=$(git rev-parse HEAD)
tmp=/var/tmp/benches-$$
log=${TMPDIR:-/tmp}/benches.$$.log
bundle=$(mktemp -t benches)
git bundle create -q "$bundle" HEAD
scp -q "$bundle" builder:$tmp.bundle
rm -f "$bundle"
# the container runs as root and removes its own tree (--rm); /cache is the builder's cache, as in the CI sims job
# the outer limit follows the slowest selected bench's own limit (ci/sims.txt column 3: hdmi_out has 9000 s), plus 10 min for clone and set-up
lim=$(awk -F'|' -v want=" $benches " 'index(want, " " $2 " ") && $3 + 0 > m { m = $3 + 0 } END { print (m > 0 ? m : 4800) + 600 }' ci/sims.txt)
ssh builder "nice -n 15 timeout $lim docker run --rm --init -v /var/tmp:/t -v /srv/trx64-build/cache:/cache -e PYTHONDONTWRITEBYTECODE=1 trx64-sim-env:5 sh -c '
  git -c init.defaultBranch=main clone -q /t/benches-$$.bundle /src && cd /src && git -c advice.detachedHead=false checkout -q $rev &&
  ci/sims.sh -t 2 -j 8 $force $benches; echo rc=\$?'; rm -f $tmp.bundle" > "$log" 2>&1 || true
sed -n '/^=== summary/,$p' "$log" | grep -v '^=== sims done' || true
if grep -q "^rc=0$" "$log"; then echo "benches: ok"; rm -f "$log"; exit 0; fi
echo "benches: FAILED, log $log"; exit 1
