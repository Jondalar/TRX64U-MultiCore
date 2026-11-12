#!/usr/bin/env bash
# T164 fail-fast: the two host build scripts source host/check/trx_constraints.tcl and call trx_check_constraints before their first opt_design,
# and the proc warns (no stop since 2026-11-01) on a non-zero message counter. Grep level for the scripts; the proc itself runs under tclsh (when the
# machine has one) against a fake get_msg_config. Negative controls: a script without the call, a counter above zero.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail() { echo "test_constraint_check: FAIL: $*" >&2; exit 1; }

# the DFX host and the partial core build. Not the large cores: tools/vivado_lib.tcl and cores/*/mono/build_mono.tcl are inputs of their keys
# (Alex 2026-11-01: they must not rebuild for host/infra changes); they get the check when their copy of the host files is refreshed on purpose.
scripts=(host/build.tcl host/core.tcl)

# call_before_opt <file>: 0 when a trx_check_constraints line comes before the first bare opt_design line
call_before_opt() {
  awk '/^opt_design[[:space:]]*$/ { exit (seen ? 0 : 1) } /^trx_check_constraints[[:space:]]/ { seen = 1 } END { if (!seen) exit 1 }' "$1"
}
for f in "${scripts[@]}"; do
  call_before_opt "$f" || fail "$f does not call trx_check_constraints before its first opt_design"
  grep -q '^source check/trx_constraints\.tcl' "$f" || fail "$f does not source check/trx_constraints.tcl"
done
grep -q '^proc trx_check_constraints ' host/check/trx_constraints.tcl || fail "host/check/trx_constraints.tcl has no trx_check_constraints"

# control: a script with the call removed must be caught
tmp="$(mktemp -d "${TMPDIR:-/tmp}/t164-cc.XXXXXX")"; trap 'rm -rf "$tmp"' EXIT
grep -v '^trx_check_constraints' host/core.tcl > "$tmp/core.tcl"
call_before_opt "$tmp/core.tcl" && fail "control: a script without the call passed"

# the proc under tclsh
if command -v tclsh >/dev/null 2>&1; then
  cat > "$tmp/t.tcl" <<TCL
set f [open host/check/trx_constraints.tcl]; set s [read \$f]; close \$f
if {![regexp {proc trx_check_constraints.*?\n\}\n} \$s body]} { puts "no proc"; exit 2 }
eval \$body
proc get_msg_config {args} { global CNT; return \$CNT }
set CNT 0
if {[catch {trx_check_constraints zero} m]} { puts "zero counter stopped: \$m"; exit 3 }
set CNT 1
if {[catch {trx_check_constraints one} m]} { puts "counter 1 stopped the build (it warns since 2026-11-01): \$m"; exit 4 }
TCL
  tclsh "$tmp/t.tcl" >/dev/null || fail "proc behaviour under tclsh (exit $?)"
else
  echo "test_constraint_check: tclsh not found, proc behaviour not run" >&2
fi
echo "test_constraint_check: ok (${#scripts[@]} scripts)"
