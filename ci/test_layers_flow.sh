#!/usr/bin/env bash
# T87 S3/S4/S5/S7/S10 on the flow of ci/bitstream.sh stage c64u with a FAKE vivado (no Vivado): which
# entries are built and which are hits, in a scratch clone with a scratch cache. The fake writes the
# files the real scripts write and a timing report with a settable slack; it is not a model of Vivado,
# it only lets the shell side (keys, cache layout, host_sha rule, pins, failure handling) be run.
set -euo pipefail
# T215: this test moves keys by appending a comment line to a source; it tests the plumbing (stage flow, plan, layer keys), not the normaliser, so keys are
# the raw content hash here (the normaliser has its own test: ci/test_code_hash.py).
export CODE_HASH=raw
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$PWD"
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
git clone -q "$SRC" "$W/repo" 2>/dev/null
git -C "$SRC" ls-files -z -m -o --exclude-standard -- ci host cores vendor tools | while IFS= read -r -d '' f; do
  [ -f "$f" ] && { mkdir -p "$W/repo/$(dirname "$f")"; cp "$f" "$W/repo/$f"; }
done
R="$W/repo"
git -C "$R" add -A; git -C "$R" -c user.name=t -c user.email=t@t commit -q --allow-empty -m scratch
# The fixture has exactly two cores, whatever the tree has: the reference core (refx here, a copy of c64ref, its partial
# comes out of the host run; the tree's own reference since T115 is c64ref, and layers.txt below is set back to refx so the
# fixture's refx files stay the host's reference-core files) and "other", implemented against it. The real cores are removed from
# the fixture, so adding a core to the repository does not change what this test counts.
cp -R "$R/cores/c64ref" "$R/cores/refx"   # the fixture's reference core: a copy of c64ref under its own name (the deleted old trx64 core was this until 2026-11-08)
find "$R/cores" -name core.toml ! -path "*/refx/*" -delete   # every core but the reference one
mkdir -p "$R/cores/other"
python3 - "$R" <<'TOML'
import sys
r = sys.argv[1]
# cores/refx has no descriptor since T115 (a source tree): the fixture writes one, as the reference core
open(r + "/cores/refx/core.toml", "w").write(
    'id = "refx"\nname = "refx"\nversion = "0.0.1"\ncontract = 1\napp = "none"\norder = 0\nexperimental = false\n'
    'sources = ["cores/refx", "vendor/icepi", ":(exclude)cores/refx/dma_win_c64ref.v"]\n')
open(r + "/cores/other/core.toml", "w").write(
    'id = "other"\nname = "other"\nversion = "0.0.1"\ncontract = 1\napp = "none"\n'
    'order = 1\nexperimental = false\nsources = ["cores/refx/dma_win_c64ref.v"]\n')
# a core is only built when it has a layer-2 recipe (ci/bitstream_layers.sh skips it otherwise)
open(r + "/cores/other/core_sources.tcl", "w").write('set l2_dcp other.dcp\n')
# layers.txt as it was before T115: refx is the reference core and none of its files is a shared one
lp = r + "/host/layers.txt"
ls = open(lp).read().replace("reference: c64ref", "reference: refx")
ls = "\n".join(l for l in ls.split("\n") if not l.startswith("cores/refx") and not l.startswith(":(exclude)cores/refx/") and l != "cores/c64u")
open(lp, "w").write(ls)
open(r + "/cores/other/core.toml", "w").write(
    'id = "other"\nname = "other"\nversion = "0.0.1"\ncontract = 1\napp = "none"\n'
    'order = 1\nexperimental = false\nsources = ["cores/refx/dma_win_c64ref.v"]\n')
# a core is only built when it has a layer-2 recipe (ci/bitstream_layers.sh skips it otherwise)
open(r + "/cores/other/core_sources.tcl", "w").write('set l2_dcp other.dcp\n')
TOML
git -C "$R" add -A; git -C "$R" -c user.name=t -c user.email=t@t commit -qm "second core"
git -C "$R" checkout -q -b scratch1

mkdir -p "$W/bin"
cat > "$W/bin/vivado" <<'EOF'
#!/usr/bin/env bash
# fake: vivado -mode batch -nojournal -log <log> -source <tcl>; the stage from the environment
[ "${1:-}" = -version ] && { echo "vivado v2024.1 (fake)"; exit 0; }
while [ $# -gt 0 ]; do case "$1" in -log) log="$2"; shift ;; -source) tcl="$2"; shift ;; esac; shift; done
echo "$tcl stage=${TRX_STAGE:-} core=${TRX_CORE:-} host=${TRX_HOST_DCP:-}" >> "$FAKE_LOG"
bdir="${TRX_BUILD:-build}"; mkdir -p "$bdir"
wns="${FAKE_WNS:-0.5}"
# a full image the way the real one is read by tools/cor_pack.py hostkey: sync word, type-1 write of the AXSS register, DESYNC
mkbit() { python3 -c 'import struct,sys; sys.stdout.buffer.write(bytes(8)+struct.pack(">IIIII",0xAA995566,(1<<29)|(2<<27)|(0x0D<<13)|1,int(sys.argv[1],16),(1<<29)|(2<<27)|(4<<13)|1,0x0D))' "$1" > "$2"; }
rpt() { printf 'WNS(ns)  TNS(ns)\n-------  -------\n  %s   0.000\n' "$1" > "$bdir/timing.rpt"; echo "| Slice LUTs | 1 |" > "$bdir/utilization.rpt"; }
case "$(basename "$PWD")/$tcl" in
  host/build.tcl)
    if [ "${TRX_STAGE:-}" = host ]; then
      mkbit "${FAKE_HOST_AXSS:-$TRX_HOST_KEY8}" "$bdir/c64_basic.bit"; head -c 32 /dev/urandom > "$bdir/c64_basic_pb_l2_partial.bit"
      head -c 64 /dev/urandom > "$bdir/routed.dcp"; [ -n "${FAKE_NO_HOSTBIT:-}" ] || head -c 64 /dev/urandom > "$bdir/host.bit"
    fi
    rpt "$wns"; echo "=== done" > "$log" ;;
  host/core.tcl)
    [ -f "$TRX_HOST_DCP" ] || { echo "no host dcp" > "$log"; exit 1; }
    [ "${FAKE_SLEEP:-}" != "$TRX_CORE" ] || exec sleep 120    # D9-8: a core that does not finish
    rpt "${FAKE_CORE_WNS:-0.5}"
    if [ "${FAKE_CORE_WNS:-0.5}" = 0.5 ]; then
      head -c 32 /dev/urandom > "$bdir/${TRX_CORE}_partial.bit"; mkbit "$TRX_HOST_KEY8" "$bdir/${TRX_CORE}_full.bit"
      echo "=== core $TRX_CORE: pr_verify passed" > "$log"
    else echo "=== core $TRX_CORE: timing failed, no bitstream" > "$log"; fi
    # T95 5.16: a core whose timing script resolved to nothing still routes and still meets the WNS gate in this fake
    [ -n "${FAKE_CORE_UNRESOLVED:-}" ] && echo "CRITICAL WARNING: [Vivado 12-4739] set_multicycle_path:No valid object(s) found for '-from [get_cells ...]'." >> "$log"
    : ;;
  *)  # ddr2_test / hdmi_test
    n="$(basename "$PWD")"; head -c 16 /dev/urandom > "$bdir/$n.bit"; rpt 0.4; echo done > "$log" ;;
esac
EOF
chmod +x "$W/bin/vivado"
export PATH="$W/bin:$PATH" FAKE_LOG="$W/vivado.calls" CI_BIT_CACHE="$W/cache/bit" TRX_CACHE="$W/cache" CI_HANDOFF="$W/handoff"
export GITHUB_REF_NAME=scratch1 GITHUB_RUN_ID=1 CI_GC_BRANCHES="$W/branches"
printf 'main\nscratch1\nscratch2\n' > "$W/branches"
: > "$FAKE_LOG"

stage() { # stage <branch>: run the stage in the scratch clone, return its exit code; calls go to $FAKE_LOG
  : > "$FAKE_LOG"
  GITHUB_REF_NAME="$1" "$R/ci/bitstream.sh" c64u > "$W/out.$1.txt" 2>&1
}
calls() { grep -c "$1" "$FAKE_LOG" || true; }
fail() { echo "FAIL: $*"; tail -30 "$W/out.${CUR:-scratch1}.txt" 2>/dev/null; exit 1; }

# 1. cold cache: host + core c64 + ddr2_test + hdmi_test
CUR=scratch1; stage scratch1 || fail "cold run"
[ "$(calls 'stage=host')" = 1 ] || fail "cold run: the host was not built once"
[ "$(calls 'core.tcl')" = 1 ] || fail "cold run: only the non-reference core is implemented against the host"
grep -q "core.tcl stage= core=other" "$FAKE_LOG" || fail "cold run: core.tcl did not run for 'other'"
[ "$(ls -d "$CI_BIT_CACHE"/host-* | wc -l | tr -d ' ')" = 1 ] && [ "$(ls -d "$CI_BIT_CACHE"/core-refx-* | wc -l | tr -d ' ')" = 1 ] && [ "$(ls -d "$CI_BIT_CACHE"/core-other-* | wc -l | tr -d ' ')" = 1 ] || fail "cold run: cache layout"
grep -q "host cache miss" "$W/out.scratch1.txt" || fail "no 'host cache miss' line"
H1="$(ls -d "$CI_BIT_CACHE"/host-*)"; C1="$(ls -d "$CI_BIT_CACHE"/core-refx-*)"
for f in static_routed.dcp host.bit manifest.json DONE; do [ -e "$H1/$f" ] || fail "host entry lacks $f"; done
for f in refx_partial.bit refx_full.bit manifest.json DONE; do [ -e "$C1/$f" ] || fail "core entry lacks $f"; done
[ -f "$H1/pins/branch.scratch1" ] && [ -f "$C1/pins/branch.scratch1" ] || fail "pins missing"
[ -s "$W/handoff/bit-c64u/c64_basic.bit" ] && [ -s "$W/handoff/bit-c64u/c64_basic_pb_l2_partial.bit" ] && [ -s "$W/handoff/bit-c64u/host.bit" ] || fail "stage directory lacks the package files"
python3 -c 'import json; d=json.load(open("'"$W"'/handoff/bit-c64u/layers.json")); assert d["host"]["host_id"] and d["cores"]["refx"]["partial_sha256"]' || fail "layers.json"

# 2. nothing changed: all hits, nothing built (S4 in its simplest form)
stage scratch1 || fail "warm run"
[ -z "$(grep -E 'stage=host|core.tcl' "$FAKE_LOG")" ] || fail "warm run built something: $(cat "$FAKE_LOG")"
grep -q "host cache hit" "$W/out.scratch1.txt" && grep -q "core cache hit" "$W/out.scratch1.txt" || fail "warm run: no hit lines"

# 3. S4: another branch, same sources: the same host entry, no build
git -C "$R" checkout -q -b scratch2
stage scratch2 || fail "second branch"
[ -z "$(grep -E 'stage=host|core.tcl' "$FAKE_LOG")" ] || fail "second branch rebuilt something"
[ "$(ls -d "$CI_BIT_CACHE"/host-* | wc -l | tr -d ' ')" = 1 ] || fail "S4: the cache holds more than one host entry"
[ -f "$H1/pins/branch.scratch2" ] || fail "S4: the second branch did not pin the shared host"

# 4. S3: a core-only change: host hit, one core build, no host step
echo "// core change" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "core change"
stage scratch2 || fail "core change"
[ "$(calls 'stage=host')" = 0 ] && [ "$(calls 'core.tcl')" = 1 ] || fail "S3: a core-only change must build the core only: $(cat "$FAKE_LOG")"
grep -q "host cache hit" "$W/out.scratch2.txt" || fail "S3: no 'host cache hit'"
grep -q "build.tcl" "$FAKE_LOG" && fail "S3: the monolithic build ran"
[ "$(ls -d "$CI_BIT_CACHE"/core-other-* | wc -l | tr -d ' ')" = 2 ] || fail "S3: the new core entry is missing"

# 4b. a change in the REFERENCE core's file rebuilds the host (and with it c64's entry and the other core)
echo "// ref core change" >> "$R/cores/refx/c64_mem_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "reference core change"
stage scratch2 || fail "reference core change"
[ "$(calls 'stage=host')" = 1 ] && [ "$(calls 'core.tcl')" = 1 ] || fail "a reference-core change must build the host and implement the other core: $(cat "$FAKE_LOG")"

# 5. S5: a host change: new host, the core is built again against it
echo "// host change" >> "$R/host/ddr2_lean.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "host change"
stage scratch2 || fail "host change"
[ "$(calls 'stage=host')" = 1 ] && [ "$(calls 'core.tcl')" = 1 ] || fail "S5: a host change must build host and core"
[ "$(ls -d "$CI_BIT_CACHE"/host-* | wc -l | tr -d ' ')" = 3 ] || fail "S5: three host entries expected (start, reference-core change, host change)"

# 6. S7: the host entry is deleted and rebuilt from the same sources -> the core entry is not adopted
rm -rf "${CI_BIT_CACHE:?}"/host-*
stage scratch2 || fail "evicted host"
[ "$(calls 'stage=host')" = 1 ] && [ "$(calls 'core.tcl')" = 1 ] || fail "S7: the rebuilt host adopted the old partial: $(cat "$FAKE_LOG")"
grep -q "another host build" "$W/out.scratch2.txt" || fail "S7: no explanation in the log"

# 7. S10: a core that misses timing fails the stage, caches nothing, keeps host and other entries
echo "// core change 2" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "core change 2"
n_core="$(ls -d "$CI_BIT_CACHE"/core-* | wc -l | tr -d ' ')"; n_host="$(ls -d "$CI_BIT_CACHE"/host-* | wc -l | tr -d ' ')"
if FAKE_CORE_WNS=-0.2 stage scratch2; then fail "S10: the stage passed with a core at WNS -0.2"; fi
[ "$(ls -d "$CI_BIT_CACHE"/core-* | wc -l | tr -d ' ')" = "$n_core" ] && [ "$(ls -d "$CI_BIT_CACHE"/host-* | wc -l | tr -d ' ')" = "$n_host" ] || fail "S10: the failed core changed the cache"
grep -q "timing failed for core other" "$W/out.scratch2.txt" || fail "S10: no timing failure line"
grep -q "RED target other" "$W/out.scratch2.txt" || fail "S10: the red core is not named"
CUR=scratch2
# 7b. T100 D9.6 (replaces T94 D8): the same failure in an EXPERIMENTAL core is a red target too - the stage fails, no exception
python3 - "$R" <<'TOML2'
import sys
p = sys.argv[1] + "/cores/other/core.toml"
t = open(p).read().replace("experimental = false", "experimental = true")
open(p, "w").write(t)
TOML2
git -C "$R" add -A; git -C "$R" -c user.name=t -c user.email=t@t commit -qm "other is experimental"
if FAKE_CORE_WNS=-0.2 stage scratch2; then fail "D9.6: an experimental core at WNS -0.2 did not fail the stage"; fi
grep -q "RED target other" "$W/out.scratch2.txt" || fail "D9.6: the failure of the experimental core was not reported red"
# 7b2. D9.6 / T174 D4: the stage is red at once and still hands over the others
if FAKE_CORE_WNS=-0.2 stage scratch2; then fail "D9.6: a red core left the stage green"; fi
[ -f "$W/handoff.red/bit-c64u.other" ] || fail "D9.6: no red marker for core other"
[ -s "$W/handoff/bit-c64u/c64_basic.bit" ] && [ -f "$W/handoff/bit-c64u/DONE" ] || fail "D9.6: the red core stopped the hand-over of the others"
rm -rf "$W/handoff.red"
# 7c. T95 5.16: a core whose constraints resolved to no object fails, even with a good WNS and a bitstream
git -C "$R" reset -q --hard HEAD~1
echo "// core change 3" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "core change 3"
if FAKE_CORE_UNRESOLVED=1 stage scratch2; then fail "T95 5.16: the stage passed with a constraint that resolved to no object"; fi
grep -q "resolved to no object" "$W/out.scratch2.txt" || fail "T95 5.16: the unresolved constraint is not named"
git -C "$R" reset -q --hard HEAD~1
# 8. the grey box (host.bit) failed in Vivado: the host entry is still made, host_sha falls back to routed.dcp
echo "// host change 2" >> "$R/host/ddr2_lean.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "host change 2"
FAKE_NO_HOSTBIT=1 stage scratch2 || fail "no host.bit"
Hn="$(ls -td "$CI_BIT_CACHE"/host-* | head -1)"
[ ! -e "$Hn/host.bit" ] && [ -n "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["host_sha"])' "$Hn/manifest.json")" ] || fail "host entry without host.bit"

# 9. T123 / CI 957: the host's static carries another key than its entry name says (an entry re-keyed in place). The flash filler
# (null core) must carry the key of the static, the one c64_basic.bit and every .cor carry, not the first 8 hex of the entry key.
mkdir -p "$R/cores/nul"; echo "// nul" > "$R/cores/nul/nul.v"
printf 'id = "nul"\nname = "nul"\nversion = "0.0.1"\ncontract = 1\napp = "none"\norder = 98\nexperimental = false\nflash_filler = true\nsources = ["cores/nul"]\n' > "$R/cores/nul/core.toml"
echo 'set l2_dcp nul.dcp' > "$R/cores/nul/core_sources.tcl"
printf '#!/bin/sh\nmkdir -p "$3"; echo equal > "$3/static_diff.txt"\n' > "$R/tools/static_diff.sh"   # fake: no Vivado here
echo "// host change 3" >> "$R/host/ddr2_lean.v"
git -C "$R" add -A; git -C "$R" -c user.name=t -c user.email=t@t commit -qm "null core, host change 3"
FAKE_HOST_AXSS=cafe0123 stage scratch2 || fail "null core against a host whose static carries another key"
OUT="$W/handoff/bit-c64u"
[ "$(python3 "$R/tools/cor_pack.py" hostkey "$OUT/c64_basic.bit")" = cafe0123 ] || fail "9: the fixture host did not carry cafe0123"
[ "$(python3 "$R/tools/cor_pack.py" hostkey "$OUT/nul_full.bit")" = cafe0123 ] || fail "9: nul_full.bit does not carry the host key of c64_basic.bit (CI 957)"
# 9b. a null entry cached with another key is not a hit
Nd="$(ls -d "$CI_BIT_CACHE"/core-nul-* | head -1)"
mkbit() { python3 -c 'import struct,sys; sys.stdout.buffer.write(bytes(8)+struct.pack(">IIIII",0xAA995566,(1<<29)|(2<<27)|(0x0D<<13)|1,int(sys.argv[1],16),(1<<29)|(2<<27)|(4<<13)|1,0x0D))' "$1" > "$2"; }
mkbit 00000bad "$Nd/nul_full.bit"
stage scratch2 || fail "9b: stale null entry"
[ "$(calls 'core.tcl')" = 1 ] || fail "9b: a null entry with the wrong host key was a cache hit: $(cat "$FAKE_LOG")"
[ "$(python3 "$R/tools/cor_pack.py" hostkey "$OUT/nul_full.bit")" = cafe0123 ] || fail "9b: rebuilt null entry has the wrong key"

# ---- T100 D9: the plan in the stage ---------------------------------------------------------------------------------------------
OUT="$W/handoff/bit-c64u"
planfile() { # planfile <named dfx cores, space separated> [host]: $HANDOFF/plan.json naming just those
  python3 - "$W/handoff/plan.json" "$1" "${2:-}" <<'PYP'
import json, sys
dst, dfx, host = sys.argv[1:]
json.dump({"version": 1, "host": host == "host", "aux": False, "dfx": dfx.split(), "large": [], "apps": {}, "limits": {"host": 150, "core": 45, "large": 120}}, open(dst, "w"))
PYP
}
# Case 9 committed the build directories of the fake runs (git add -A), and they are under host/, i.e. in the host key: take them out
# again, or every stage run below would move the key it is supposed to leave alone.
git -C "$R" ls-files -z -- 'host/build_*' | xargs -0 git -C "$R" rm -q --cached --
printf 'host/build_*\n' >> "$R/.git/info/exclude"
git -C "$R" -c user.name=t -c user.email=t@t commit -qm "no build output in the tree"
# D9-7 the cache is the fallback. The plan names nothing; everything is a hit, nothing is built, the lines say it.
stage scratch2 >/dev/null 2>&1 || true    # settle the cache for the current tree
planfile ""
stage scratch2 || fail "D9-7: plan naming nothing, warm cache"
[ -z "$(grep -E 'stage=host|core.tcl' "$FAKE_LOG")" ] || fail "D9-7: an unnamed target with a cache hit was built: $(cat "$FAKE_LOG")"
grep -q "plan: dfx:other - cache hit (not named by the plan)" "$W/out.scratch2.txt" || fail "D9-7: the hit of an unnamed target is not marked"
# ... and a cache miss of an unnamed target is built and marked. 'other' is the only core whose sources contain dma_win.v.
echo "// d9-7 core change" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "d9-7 core change"
planfile ""
stage scratch2 || fail "D9-7: plan naming nothing, cache miss"
[ "$(calls 'core.tcl')" = 1 ] || fail "D9-7: the unnamed target with a cache miss was not built: $(cat "$FAKE_LOG")"
grep -q "plan: dfx:other - cache miss of an unnamed target: building" "$W/out.scratch2.txt" || fail "D9-7: the cache miss is not marked"
# negative control: the same miss, now NAMED, must not carry the unnamed mark (a stage that marked everything would pass the check above)
echo "// d9-7 core change 2" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "d9-7 core change 2"
planfile "other"
stage scratch2 || fail "D9-7 control: named miss"
grep -q "plan: dfx:other - building (named)" "$W/out.scratch2.txt" || fail "D9-7 control: the named build is not marked"
if grep -q "unnamed target" "$W/out.scratch2.txt"; then fail "D9-7 control: a named target was marked as unnamed"; fi
echo "D9-7 ok (hit marked, miss built and marked, control: named miss unmarked)"

# D9-8 one target that does not finish does not eat the run: 'other' hangs, 'nul' (built after it) is still built, the run is red
echo "// d9-8 core change" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "d9-8 core change"
echo "// d9-8 nul change" >> "$R/cores/nul/nul.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "d9-8 nul change"
rm -f "$W/handoff/plan.json"; rm -rf "$W/handoff.red"
t0=$SECONDS
if CI_LIMIT_SECONDS=3 FAKE_SLEEP=other stage scratch2; then fail "D9-8: the stage passed with a stopped core"; fi
[ $((SECONDS - t0)) -lt 60 ] || fail "D9-8: the hanging core was not stopped at the limit ($((SECONDS - t0)) s)"
grep -q "TIMEOUT: the core run was stopped" "$W/out.scratch2.txt" || fail "D9-8: no TIMEOUT line"
[ -f "$W/handoff.red/bit-c64u.other" ] && grep -q timeout "$W/handoff.red/bit-c64u.other" || fail "D9-8: the stopped core is not red as a timeout"
[ "$(grep -c 'core.tcl.*core=nul' "$FAKE_LOG")" = 1 ] || fail "D9-8: the core after the stopped one was not built: $(cat "$FAKE_LOG")"
[ -s "$W/handoff/bit-c64u/nul_full.bit" ] && [ -s "$W/handoff/bit-c64u/c64_basic.bit" ] || fail "D9-8/D9.6: the others were not handed over"
[ ! -f "$W/handoff/bit-c64u/other_full.bit" ] || fail "D9-8: a stopped core left an image"
# control: the same limit on a core that finishes is no timeout and no red target
rm -rf "$W/handoff.red"
echo "// d9-8 control change" >> "$R/cores/refx/dma_win_c64ref.v"
git -C "$R" -c user.name=t -c user.email=t@t commit -qam "d9-8 control change"
CI_LIMIT_SECONDS=3 stage scratch2 || fail "D9-8 control: a fast core under the limit failed"
if grep -q "TIMEOUT" "$W/out.scratch2.txt" || [ -e "$W/handoff.red/bit-c64u.other" ]; then fail "D9-8 control: a core that finishes was reported as timeout"; fi
echo "D9-8 ok (stopped at the limit, red as timeout, the next core built and handed over, control: fast core is green)"
echo "D9.6 ok (a red core, experimental or not, is red; the others are built and handed over; the stage is red at once)"

# ---- T100 D10: one lane per job (ci/bitstream.sh c64u --lane host | core:<id>) -------------------------------------------------------
lane() { # lane <lane> [env...]: one lane's script in the scratch clone; output in $W/out.lane.txt
  local l="$1"; shift; : > "$FAKE_LOG"
  env "$@" GITHUB_REF_NAME=scratch2 "$R/ci/bitstream.sh" c64u --lane "$l" > "$W/out.lane.txt" 2>&1
}
lplan() { # lplan <host yes|no> <dfx lanes> : plan.json with only those lanes triggered (nothing named)
  python3 - "$W/handoff/plan.json" "$1" "$2" <<'PYP'
import json, sys
dst, h, d = sys.argv[1:]
json.dump({"version": 1, "host": False, "aux": False, "dfx": [], "large": [], "apps": {}, "limits": {"host": 150, "core": 45, "large": 120},
           "lanes": {"host": h == "yes", "dfx": d.split(), "large": []}}, open(dst, "w"))
PYP
}
lfail() { echo "FAIL: $*"; tail -30 "$W/out.lane.txt"; exit 1; }
rm -f "$W/handoff/plan.json"; rm -rf "$W/handoff.red"
echo "// d10 host change" >> "$R/host/ddr2_lean.v"; git -C "$R" -c user.name=t -c user.email=t@t commit -qam "d10 host change"
# host lane: builds the host (and takes the reference core out of that run), implements no other core, hands over its own stage dir
lane host || lfail "D10: host lane"
[ "$(calls 'stage=host')" = 1 ] && [ "$(calls 'core.tcl')" = 0 ] || lfail "D10: the host lane must build the host and no other core: $(cat "$FAKE_LOG")"
HL="$W/handoff/bit-host"
[ -f "$HL/DONE" ] && [ -s "$HL/c64_basic.bit" ] && [ -s "$HL/host.bit" ] && [ -s "$HL/refx_partial.bit" ] && [ -s "$HL/ddr2_test.bit" ] || lfail "D10: the host lane's stage dir lacks the host / reference / aux files"
[ ! -e "$HL/other_partial.bit" ] || lfail "D10: the host lane handed over a DFX core"
python3 -c 'import json; d=json.load(open("'"$HL"'/layers.json")); assert d["host"]["host_id"] and list(d["cores"]) == ["refx"], d' || lfail "D10: layers.json of the host lane"
# core lane: implements that one core against the host entry of the cache, no host step, own stage dir without the host's files
lane core:other || lfail "D10: core lane"
[ "$(calls 'stage=host')" = 0 ] && [ "$(calls 'core.tcl')" = 1 ] && grep -q "core.tcl stage= core=other" "$FAKE_LOG" || lfail "D10: the core lane must implement exactly its core: $(cat "$FAKE_LOG")"
CL="$W/handoff/bit-other"
[ -f "$CL/DONE" ] && [ -s "$CL/other_partial.bit" ] && [ -s "$CL/other_full.bit" ] || lfail "D10: the core lane's stage dir lacks its images"
[ ! -e "$CL/c64_basic.bit" ] && [ ! -e "$CL/host.bit" ] || lfail "D10: the core lane handed over the host's files"
[ "$(cat "$CL/key")" = "$(cat "$HL/key")" ] || lfail "D10: the lanes disagree about the host key"
python3 -c 'import json; d=json.load(open("'"$CL"'/layers.json")); assert list(d["cores"]) == ["other"] and "host" not in d, d' || lfail "D10: layers.json of the core lane"
[ -f "$W/handoff/bit-host/DONE" ] || lfail "D10: the core lane damaged the host lane's directory"
# a lane that did not fire hands over from the cache and builds nothing
lplan no ""; lane core:other || lfail "D10: idle core lane"
[ "$(calls 'core.tcl')" = 0 ] && grep -q "not in the plan" "$W/out.lane.txt" && grep -q "plan: dfx:other - cache hit (not named by the plan)" "$W/out.lane.txt" && [ -s "$CL/other_partial.bit" ] || lfail "D10: an idle lane built something or handed over nothing"
lplan no ""; lane host || lfail "D10: idle host lane"
[ "$(calls 'stage=host')" = 0 ] && grep -q "not in the plan" "$W/out.lane.txt" && [ -s "$HL/c64_basic.bit" ] || lfail "D10: an idle host lane built something"
# control: the same lane, named by the plan, says so (a lane that always prints 'not in the plan' would pass the check above)
lplan no "other"; lane core:other || lfail "D10 control"
grep -q "lane core:other: in the plan" "$W/out.lane.txt" && ! grep -q "not in the plan" "$W/out.lane.txt" || lfail "D10 control: a triggered lane is reported idle"
# the package merges the lane directories (ci/package.sh merge_lanes): the host lane's key, every lane's files and wns lines, one layers.json
lplan no "other"; lane core:other || lfail "D10 merge: core lane"
eval "$(sed -n '/^merge_lanes() {/,/^}/p' "$R/ci/package.sh")"; source "$R/ci/lib.sh"
M="$W/merged"; rm -rf "$M"; merge_lanes "$M" "$W/handoff/bit-host" "$W/handoff/bit-other"
[ -f "$M/DONE" ] && [ -s "$M/c64_basic.bit" ] && [ -s "$M/other_partial.bit" ] && [ -s "$M/host.bit" ] && [ "$(cat "$M/key")" = "$(cat "$HL/key")" ] || lfail "D10 merge: files or key"
grep -q '^other ' "$M/wns.txt" && grep -q '^host ' "$M/wns.txt" && grep -q '^c64_basic ' "$M/wns.txt" || lfail "D10 merge: wns.txt lacks a lane"
python3 -c 'import json; d=json.load(open("'"$M"'/layers.json")); assert d["host"]["host_id"] and sorted(d["cores"]) == ["other", "refx"], d' || lfail "D10 merge: layers.json"
[ -f "$M/plan_result.txt" ] && grep -q "dfx:other" "$M/plan_result.txt" && grep -q "^host" "$M/plan_result.txt" || lfail "D10 merge: plan_result.txt"
# no host entry (the host lane was red): the core lane is red with it and builds nothing
rm -f "$W/handoff/plan.json"
mv "$CI_BIT_CACHE" "$W/cache-away"; mkdir -p "$CI_BIT_CACHE"
if lane core:other; then lfail "D10: a core lane without a host entry passed"; fi
[ "$(calls 'core.tcl')" = 0 ] && [ -f "$W/handoff.red/bit-other.other" ] && grep -q "host is red" "$W/handoff.red/bit-other.other" || lfail "D10: no red marker without a host entry: $(ls "$W/handoff.red" 2>/dev/null)"
rm -rf "$CI_BIT_CACHE" "$W/handoff.red"; mv "$W/cache-away" "$CI_BIT_CACHE"
# bad lanes: the reference core is the host lane's, an unknown core is no lane
if lane core:refx; then lfail "D10: the reference core is a DFX lane"; fi
if lane core:nosuch; then lfail "D10: an unknown core is a lane"; fi
if lane bogus; then lfail "D10: an unknown lane was accepted"; fi
echo "D10 ok (host lane = host + reference + aux; core lane = that core only; idle lane hands over from the cache; no host entry = red; bad lanes refused)"
bash "$SRC/ci/test_heavy_slots.sh"
echo "layers flow test ok"
