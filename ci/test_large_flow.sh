#!/usr/bin/env bash
# T100 D9 on ci/bitstream_mono.sh (stage large) with a FAKE vivado, in a scratch clone with a scratch cache: a named large core is built,
# an unnamed one comes from the cache (the older entry when the core's own sources moved the key; a host change moves no large key, T164 3a Q1), an unnamed
# one without any entry is built and marked, a flat build that does not finish is red and the stage goes on (D9-8, D9.6).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$PWD"
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
git clone -q "$SRC" "$W/repo" 2>/dev/null
git -C "$SRC" ls-files -z -m -o --exclude-standard -- ci host cores vendor tools | while IFS= read -r -d '' f; do
  [ -f "$f" ] && { mkdir -p "$W/repo/$(dirname "$f")"; cp "$f" "$W/repo/$f"; }
done
R="$W/repo"
G() { git -C "$R" -c user.name=t -c user.email=t@t "$@"; }
G add -A; G commit -q --allow-empty -m scratch; G checkout -q -b scratch1
mkdir -p "$W/bin"
cat > "$W/bin/vivado" <<'EOF2'
#!/usr/bin/env bash
[ "${1:-}" = -version ] && { echo "vivado v2024.1 (fake)"; exit 0; }
while [ $# -gt 0 ]; do case "$1" in -log) log="$2"; shift ;; -source) tcl="$2"; shift ;; esac; shift; done
echo "$tcl build=${TRX_BUILD:-}" >> "$FAKE_LOG"
[ -z "${FAKE_HANG:-}" ] || exec sleep 120
b="${TRX_BUILD:?}"; mkdir -p "$b"
head -c 64 /dev/urandom > "$b/amiga1200_mono.bit"
printf 'WNS(ns)  TNS(ns)\n-------  -------\n  %s   0.000\n' "${FAKE_WNS:-0.5}" > "$b/timing.rpt"
echo "| Slice LUTs | 1 |" > "$b/utilization.rpt"; echo done > "$log"
EOF2
chmod +x "$W/bin/vivado"
export PATH="$W/bin:$PATH" FAKE_LOG="$W/vivado.calls" CI_BIT_CACHE="$W/cache/bit" TRX_CACHE="$W/cache" CI_HANDOFF="$W/handoff"
export GITHUB_REF_NAME=scratch1 GITHUB_RUN_ID=1 CI_GC_BRANCHES="$W/branches"
printf 'main\nscratch1\n' > "$W/branches"
stage() { : > "$FAKE_LOG"; "$R/ci/bitstream_mono.sh" amiga1200 > "$W/out.txt" 2>&1; }
calls() { grep -c "build_mono" "$FAKE_LOG" || true; }
fail() { echo "FAIL: $*"; tail -25 "$W/out.txt" 2>/dev/null; exit 1; }
planfile() { # planfile <named large cores>
  python3 - "$W/handoff/plan.json" "$1" <<'PYP'
import json, sys
json.dump({"version": 1, "host": False, "aux": False, "dfx": [], "large": sys.argv[2].split(), "apps": {}, "limits": {"host": 150, "core": 45, "large": 120}}, open(sys.argv[1], "w"))
PYP
}
mkdir -p "$W/handoff"

# cold cache, unnamed: no entry at all -> built and marked
planfile ""
stage || fail "cold, unnamed"
[ "$(calls)" = 1 ] && grep -q "cache miss of an unnamed target: building" "$W/out.txt" || fail "an unnamed core without any entry was not built and marked"
[ -s "$W/handoff/bit-mono/amiga1200_mono.bit" ] || fail "no bitstream handed over"
# warm: a hit, nothing built
stage || fail "warm"
[ "$(calls)" = 0 ] && grep -q "plan: large:amiga1200 - cache hit (not named by the plan)" "$W/out.txt" || fail "warm unnamed run built or did not say hit"
# T164 3a Q1 ("Copies instead."): a HOST file is no input of a large core any more - its key stays, the entry of that key is a hit, nothing is built
echo "// host change" >> "$R/host/ddr2_lean.v"; G commit -q -am "host change"
stage || fail "host change, unnamed"
[ "$(calls)" = 0 ] || fail "D9-1: a host change built a large core"
grep -q "plan: large:amiga1200 - cache hit (not named by the plan)" "$W/out.txt" && ! grep -q "older key" "$W/out.txt" || fail "D9-1: a host change moved a large core's key (the older entry was used instead of the entry of the key)"
# control for that check: the same file in the core's OWN COPY moves the key; the plan does not name the core -> the older entry is used, nothing is built
echo "// change in the core's copy" >> "$R/cores/amiga1200/mono/host/ddr2_lean.v"; G commit -q -am "copy change"
stage || fail "copy change, unnamed"
[ "$(calls)" = 0 ] || fail "D9-1: a change in the core's copy built an unnamed large core"
grep -q "using the newest one" "$W/out.txt" && grep -q "older key" "$W/out.txt" || fail "D9-1: the change in the core's copy did not move its key (the older entry is not announced)"
[ -s "$W/handoff/bit-mono/amiga1200_mono.bit" ] || fail "D9-1: the large core's bitstream is missing from the hand-over"
# control: the same change with the core NAMED builds it (a stage that never builds would pass the check above)
planfile "amiga1200"
stage || fail "named"
[ "$(calls)" = 1 ] && grep -q "plan: large:amiga1200 - building (named)" "$W/out.txt" || fail "control: a named core with a key miss was not built"
# D9-8: a flat build that does not finish is stopped and red; the stage is red at once (T174 D4)
echo "// core change" >> "$R/cores/amiga1200/rtl/amiga_regs.v"; G commit -q -am "core change"
rm -rf "$W/handoff.red"
t0=$SECONDS
if CI_LIMIT_SECONDS=3 FAKE_HANG=1 stage; then fail "D9-8: the stage passed with a stopped flat build"; fi
[ $((SECONDS - t0)) -lt 60 ] || fail "D9-8: the hanging flat build was not stopped"
[ -f "$W/handoff.red/bit-mono.amiga1200" ] && grep -q timeout "$W/handoff.red/bit-mono.amiga1200" || fail "D9-8: no red marker for the timeout"
# D9.6: timing failure is red, experimental or not (amiga1200 is experimental = true), and fails the stage when run alone
rm -rf "$W/handoff.red"
if FAKE_WNS=-0.3 stage; then fail "D9.6: an experimental large core at WNS -0.3 did not fail the stage"; fi
grep -q "RED target amiga1200" "$W/out.txt" || fail "D9.6: not reported red"
# ---- T100 D10: the large lane (ci/bitstream_mono.sh --lane <core>): its own stage dir large-<core>, reds filed under it ----------------
rm -rf "$W/handoff.red" "$W/handoff/bit-mono"
lanerun() { : > "$FAKE_LOG"; "$R/ci/bitstream_mono.sh" --lane amiga1200 > "$W/out.txt" 2>&1; }
lplan() { # lplan <triggered large lanes>: plan.json with only those lanes fired (nothing named as a target)
  python3 - "$W/handoff/plan.json" "$1" <<'PYP'
import json, sys
json.dump({"version": 1, "host": False, "aux": False, "dfx": [], "large": [], "apps": {}, "limits": {"host": 150, "core": 45, "large": 120},
           "lanes": {"host": False, "dfx": [], "large": sys.argv[2].split()}}, open(sys.argv[1], "w"))
PYP
}
echo "// lane core change" >> "$R/cores/amiga1200/rtl/amiga_regs.v"; G commit -q -am "lane core change"
lplan "amiga1200"
lanerun || fail "D10: large lane, fired"
[ "$(calls)" = 1 ] && grep -q "lane amiga1200: in the plan" "$W/out.txt" && [ -s "$W/handoff/large-amiga1200/amiga1200_mono.bit" ] && [ -f "$W/handoff/large-amiga1200/DONE" ] || fail "D10: a fired large lane did not build into large-amiga1200"
[ ! -e "$W/handoff/bit-mono" ] || fail "D10: the lane wrote the whole-stage directory"
# idle lane (nothing fired): hands over from the cache, builds nothing
echo "// host change for the idle lane" >> "$R/host/ddr2_lean.v"; G commit -q -am "host change 2"
lplan ""
lanerun || fail "D10: large lane, idle"
[ "$(calls)" = 0 ] && grep -q "not in the plan" "$W/out.txt" && [ -s "$W/handoff/large-amiga1200/amiga1200_mono.bit" ] || fail "D10: an idle large lane built or handed nothing over"
# a red flat build is filed under the lane's stage name, and the lane is red
echo "// lane core change 2" >> "$R/cores/amiga1200/rtl/amiga_regs.v"; G commit -q -am "lane core change 2"
lplan "amiga1200"
if FAKE_WNS=-0.3 lanerun; then fail "D10: a red large lane passed"; fi
[ -f "$W/handoff.red/large-amiga1200.amiga1200" ] || fail "D10: the red large core is not filed under large-amiga1200: $(ls "$W/handoff.red")"
echo "large lane test ok (fired lane builds into large-<core>, idle lane hands over from the cache, red filed under the lane)"
echo "large flow test ok (named build, unnamed miss built+marked, warm hit, host change keeps the key, copy change moves it, timeout red, timing red)"
