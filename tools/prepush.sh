#!/bin/sh
# Run the CI lint stage (ci/lint.sh) on the builder, in the lint job's image, for the commit HEAD, before it is pushed (or the commit given as $1).
# Alex, 2026-10-30: the coordinator sees lint failures before a push, not in the pipeline. Usage: tools/prepush.sh
set -eu
rev=$(git rev-parse "${1:-HEAD}")
tmp=/var/tmp/prepush-$$
log=${TMPDIR:-/tmp}/prepush-lint.$$.log
bundle=$(mktemp -t prepush)
git bundle create -q "$bundle" HEAD
scp -q "$bundle" builder:$tmp.bundle
rm -f "$bundle"
# the container runs as root: it clones, lints and removes its own tree, so nothing root-owned is left on the builder
ssh builder "nice -n 15 timeout 1200 docker run --rm -v /var/tmp:/t -e PYTHONDONTWRITEBYTECODE=1 trx64-sim-env:4 sh -c '
  git -c init.defaultBranch=main clone -q /t/prepush-$$.bundle /src && cd /src && git -c advice.detachedHead=false checkout -q $rev &&
  ci/lint.sh; echo rc=\$?'; rm -f $tmp.bundle" > "$log" 2>&1 || true
grep -E "^FAILED|^rc=" "$log" || true
if grep -q "^rc=0$" "$log"; then echo "prepush: lint ok for $(echo "$rev" | cut -c1-8)"; rm -f "$log"; exit 0; fi
echo "prepush: lint FAILED, log $log"; exit 1
