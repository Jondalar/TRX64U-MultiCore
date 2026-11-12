#!/usr/bin/env bash
# S8 (T87): the janitor keeps what a living branch or a tag pins, cuts the unpinned rest to the
# history rule, drops the pins of deleted branches, takes a core with its host, honours the cap.
# Runs on a generated cache directory; no Vivado, no Gitea.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
NOW=2000000000   # the clock of the test
mk() { # mk <name> <age in days> [pin ...]: an entry with a DONE of that age and 100 kB of payload
  local d="$W/bit/$1" age="$2"; shift 2
  mkdir -p "$d/pins"; head -c 102400 /dev/zero > "$d/data"; : > "$d/DONE"
  for p in "$@"; do echo x > "$d/pins/$p"; done
  python3 -c 'import os,sys; t=int(sys.argv[2])-int(float(sys.argv[3])*86400); os.utime(sys.argv[1]+"/DONE",(t,t))' "$d" "$NOW" "$age"
}
man() { printf '{"host_key":"%s","host_sha":"%s"}' "$2" "$3" > "$W/bit/$1/manifest.json"; }
k() { printf '%016x' "$1"; }

# hosts: pinned by main, by a living branch, by a deleted branch; five unpinned old ones; one young
mk "host-$(k 1)" 90 branch.main
mk "host-$(k 2)" 80 branch.feature_live
mk "host-$(k 3)" 71 branch.feature_gone
for i in 11 12 13 14 15 16; do mk "host-$(k $i)" $((i * 5)); done   # 55 60 65 70 75 80 days
mk "host-$(k 20)" 3
# cores: c101 pinned by main (host 1), c103 on host 3 (deleted branch's pin), c112 on host 12, c115 on host 15
mk "core-c64-$(k 101)" 90 branch.main; man "core-c64-$(k 101)" "$(k 1)" s1
mk "core-c64-$(k 103)" 71;             man "core-c64-$(k 103)" "$(k 3)" s3
mk "core-c64-$(k 112)" 60;             man "core-c64-$(k 112)" "$(k 12)" s12
mk "core-c64-$(k 115)" 75;             man "core-c64-$(k 115)" "$(k 15)" s15
for h in 1 2 3 11 12 13 14 15 16 20; do man "host-$(k $h)" "$(k $h)" "s$h"; done
# the monolithic kind: a tag, integ-m1, five unpinned (the four newest stay)
mk "c64u-$(k 201)" 100 tag.v1.0
mk "c64u-$(k 202)" 100 branch.integ-m1
for i in 203 204 205 206 207; do mk "c64u-$(k $i)" $((100 + i - 203)); done
# a half-built entry (no DONE), old
mkdir -p "$W/bit/host-$(k 30)"; touch -t 202001010000 "$W/bit/host-$(k 30)"
# out-of-context files: six of one kind, all old
mkdir -p "$W/ooc"
for i in 1 2 3 4 5 6; do
  for ext in dcp stub.v; do
    f="$W/ooc/ooc_c64_core_$(k $i).$ext"; echo x > "$f"
    python3 -c 'import os,sys; t=int(sys.argv[2])-int(sys.argv[3])*86400; os.utime(sys.argv[1],(t,t))' "$f" "$NOW" $((20 + i))
  done
done
printf 'main\ninteg-m1\nfeature/live\n' > "$W/branches"
export CI_GC_BRANCHES="$W/branches"

out="$(python3 ci/cache_gc.py --bit "$W/bit" --ooc "$W/ooc" --now "$NOW" --dry-run)"
echo "$out"
fail=0
gone() { printf '%s\n' "$out" | grep -q "would delete $W/$1 " || { echo "FAIL: $1 should be deleted"; fail=1; }; }
stays() { ! printf '%s\n' "$out" | grep -q "would delete $W/$1 " || { echo "FAIL: $1 must stay"; fail=1; }; }
stays "bit/host-$(k 1)"; stays "bit/core-c64-$(k 101)"; stays "bit/host-$(k 2)"   # main pin, living branch pin
stays "bit/c64u-$(k 201)"; stays "bit/c64u-$(k 202)"                              # tag, integ-m1
stays "bit/host-$(k 20)"; stays "bit/host-$(k 11)"; stays "bit/host-$(k 12)"; stays "bit/host-$(k 13)"   # young + the 4 newest
gone "bit/host-$(k 3)"                                                             # pin of a deleted branch is no pin
gone "bit/host-$(k 14)"; gone "bit/host-$(k 15)"; gone "bit/host-$(k 16)"
stays "bit/core-c64-$(k 112)"                                                      # its host stays
gone "bit/core-c64-$(k 103)"; gone "bit/core-c64-$(k 115)"                         # their hosts go
stays "bit/c64u-$(k 203)"; stays "bit/c64u-$(k 206)"; gone "bit/c64u-$(k 207)"     # 4 newest unpinned
gone "bit/host-$(k 30)"                                                            # no DONE, old
stays "ooc/ooc_c64_core_$(k 1).dcp"; stays "ooc/ooc_c64_core_$(k 4).stub.v"; gone "ooc/ooc_c64_core_$(k 5).dcp"; gone "ooc/ooc_c64_core_$(k 6).stub.v"
# nothing was deleted by the dry run
[ -d "$W/bit/host-$(k 14)" ] || { echo "FAIL: dry run deleted"; fail=1; }

# the cap: 1 MB is below the pinned entries plus the rest -> oldest unpinned go first, the pinned stay
out="$(python3 ci/cache_gc.py --bit "$W/bit" --ooc "$W/ooc" --now "$NOW" --dry-run --cap-gb 0.0008)"
stays "bit/host-$(k 1)"; stays "bit/core-c64-$(k 101)"; stays "bit/c64u-$(k 201)"
gone "bit/c64u-$(k 206)"; stays "bit/host-$(k 20)"
# a cap the pinned entries alone exceed: exit code 2, still nothing pinned deleted
python3 ci/cache_gc.py --bit "$W/bit" --ooc "$W/ooc" --now "$NOW" --dry-run --cap-gb 0.0000001 > "$W/capout" || rc=$?
[ "${rc:-0}" = 2 ] || { echo "FAIL: exit code ${rc:-0}, want 2 when the pinned entries are over the cap"; fail=1; }
out="$(cat "$W/capout")"; stays "bit/host-$(k 1)"

# the real run deletes
python3 ci/cache_gc.py --bit "$W/bit" --ooc "$W/ooc" --now "$NOW" > /dev/null
[ ! -d "$W/bit/host-$(k 14)" ] && [ -d "$W/bit/host-$(k 1)" ] && [ ! -e "$W/ooc/ooc_c64_core_$(k 6).dcp" ] || { echo "FAIL: the real run"; fail=1; }
[ "$fail" = 0 ] && echo "cache_gc test ok"
exit "$fail"
