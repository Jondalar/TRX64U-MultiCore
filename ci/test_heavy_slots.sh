#!/usr/bin/env bash
# T100 D11: ci/lib.sh heavy_lock as slots. With 2 slots two holders run at once on different slots, a third waits until one lets go and
# then takes the freed slot; with 1 slot the second waits (the old semantics, which TRX64-Ultimate still has on slot 0); a slot held
# by another process is not taken (the control: a "lock" that never refuses would let all three in). No Vivado; a flock shim written in
# Python stands in where the machine has no flock (macOS).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$PWD"
W="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; for f in "$W"/h*.out; do [ -e "$f" ] && { echo "--- $f"; cat "$f"; }; done; exit 1; }
if ! command -v flock >/dev/null; then
  mkdir -p "$W/bin"
  cat > "$W/bin/flock" <<'PYS'
#!/usr/bin/env python3
import fcntl, sys
a = sys.argv[1:]
nb = "-n" in a
fd = int([x for x in a if x != "-n"][0])
try:
    fcntl.flock(fd, fcntl.LOCK_EX | (fcntl.LOCK_NB if nb else 0))
except OSError:
    sys.exit(1)
PYS
  chmod +x "$W/bin/flock"; export PATH="$W/bin:$PATH"
fi
export CI_HEAVY_LOCK="$W/heavy.lock" CI_HEAVY_SLOT_DIR="$W/slots" CI_HEAVY_POLL=0.2 CI_HANDOFF="$W/handoff"
cat > "$W/holder.sh" <<EOS
#!/usr/bin/env bash
# holder.sh <name> <seconds>: take a heavy slot, say so, hold it, leave
source "$SRC/ci/lib.sh"
heavy_lock
echo "\$1 slot=\$HEAVY_SLOT t=\$SECONDS" > "$W/\$1.got"
sleep "\$2"
EOS
chmod +x "$W/holder.sh"
waitfor() { local i; for i in $(seq 1 100); do [ -e "$1" ] && return 0; sleep 0.1; done; return 1; }

# 1. two slots: A and B hold at once on different slots, C waits and gets the slot A freed
export CI_HEAVY_SLOTS=2
"$W/holder.sh" hA 3 > "$W/hA.out" 2>&1 & pA=$!
waitfor "$W/hA.got" || fail "A did not get a slot"
"$W/holder.sh" hB 6 > "$W/hB.out" 2>&1 &
waitfor "$W/hB.got" || fail "B did not get the second slot while A holds the first"
"$W/holder.sh" hC 1 > "$W/hC.out" 2>&1 & pC=$!
sleep 1
[ ! -e "$W/hC.got" ] || fail "C got a slot while both were held (the third must wait)"
grep -q "waiting for a free heavy slot" "$W/hC.out" || fail "C did not say it waits"
waitfor "$W/hC.got" || fail "C never got a slot after A left"
sA="$(sed -n 's/.*slot=\([0-9]*\).*/\1/p' "$W/hA.got")"; sB="$(sed -n 's/.*slot=\([0-9]*\).*/\1/p' "$W/hB.got")"; sC="$(sed -n 's/.*slot=\([0-9]*\).*/\1/p' "$W/hC.got")"
[ "$sA" != "$sB" ] || fail "A and B hold the same slot ($sA)"
[ "$sC" = "$sA" ] || fail "C did not take the slot A freed (A $sA, C $sC)"
grep -q "heavy lock held: slot $sC of 2" "$W/hC.out" || fail "the held slot is not printed"
wait || true
# 2. one slot (the old semantics): the second waits for the first
rm -f "$W"/h*.got "$W"/h*.out
export CI_HEAVY_SLOTS=1
"$W/holder.sh" hA 2 > "$W/hA.out" 2>&1 &
waitfor "$W/hA.got" || fail "1 slot: A did not get it"
"$W/holder.sh" hB 0 > "$W/hB.out" 2>&1 &
sleep 1
[ ! -e "$W/hB.got" ] || fail "1 slot: B got in while A held it"
waitfor "$W/hB.got" || fail "1 slot: B never got it"
wait || true
# 3. slot 0 busy by another process (TRX64-Ultimate): this repository still gets slot 1 with 2 slots
rm -f "$W"/h*.got "$W"/h*.out
export CI_HEAVY_SLOTS=2
python3 - "$CI_HEAVY_LOCK" <<'PY' &
import fcntl, sys, time
f = open(sys.argv[1], "w"); fcntl.flock(f, fcntl.LOCK_EX); time.sleep(4)
PY
sleep 0.5
"$W/holder.sh" hA 0 > "$W/hA.out" 2>&1
[ "$(sed -n 's/.*slot=\([0-9]*\).*/\1/p' "$W/hA.got")" = 1 ] || fail "slot 0 held elsewhere: expected slot 1"
wait || true
echo "heavy slots test ok (2 holders at once, third waits and takes the freed slot, 1 slot serialises, slot 0 held by another repository leaves slot 1)"
