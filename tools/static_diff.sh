#!/usr/bin/env bash
# T123 S3: the static part of two routed checkpoints of one host is the same - same placement of every cell outside the partition, same
# route of every net outside it, same static side of every net that crosses it (tools/static_dump.tcl says exactly what is compared).
#
#   tools/static_diff.sh <reference.dcp> <candidate.dcp> <outdir>
#       reference  the host's static_routed.dcp (the routed design with the reference core trx64 in layer 2)
#       candidate  a core stage's core_routed.dcp (the same host with another core, here the null core)
#       outdir     <outdir>/ref.dump, cand.dump (sorted), static_diff.txt (the verdict and the differences), the Vivado logs
#   Exit 0 = the dumps are equal AND the three controls (one changed line each in a cell, a net and a boundary-node line of a copy of the
#   reference dump) are reported; 1 = a difference, or a control that was not reported; 2 = no dump could be made.
#   tools/static_diff.sh --selftest <dump file>
#       the controls alone, on a dump made earlier (no Vivado): each must be reported.
#
# Vivado: $VIVADO or `vivado` from the PATH (the vivado-env image). Two launches of about a minute each, one after the other.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# differences <a.dump> <b.dump>: the differing lines, "-" lines of a and "+" lines of b
differences() { diff -U0 "$1" "$2" | grep -E '^[-+][^-+]' || true; }

# control <dump> <kind: C|N|B> <outdir>: change one line of that kind in a copy, compare, and expect exactly one "-" and one "+" line
control() {
  local dump="$1" kind="$2" out="$3" mut n
  mut="$out/control_$kind.dump"
  awk -v k="$kind" 'BEGIN { done = 0 }
    !done && substr($0, 1, 2) == k " " { n = split($0, f, " "); f[n] = f[n] "_CONTROL"; s = f[1]; for (i = 2; i <= n; i++) s = s " " f[i]; print s; done = 1; next }
    { print }' "$dump" > "$mut"
  if cmp -s "$dump" "$mut"; then echo "control $kind: the dump has no $kind line to change"; return 1; fi
  n="$(differences "$dump" "$mut" | wc -l | tr -d ' ')"
  if [ "$n" = 2 ]; then echo "control $kind: the changed line is reported ($n lines)"; return 0; fi
  echo "control $kind: expected 2 reported lines, got $n"; return 1
}

controls() {
  local dump="$1" out="$2" rc=0 k
  for k in C N B; do control "$dump" "$k" "$out" || rc=1; done
  return $rc
}

if [ "${1:-}" = "--selftest" ]; then
  d="$(mktemp -d)"; trap 'rm -rf "$d"' EXIT
  controls "$2" "$d"; exit $?
fi

[ $# -eq 3 ] || { sed -n '2,17p' "$0"; exit 2; }
ref="$1"; cand="$2"; out="$3"
mkdir -p "$out"
VIVADO="${VIVADO:-vivado}"
dump() {   # dump <dcp> <name>
  rm -f "$out/$2.raw"
  TRX_DCP="$1" TRX_DUMP="$out/$2.raw" "$VIVADO" -mode batch -nojournal -log "$out/$2.vivado.log" -source "$HERE/static_dump.tcl" > "$out/$2.stdout" 2>&1
  [ -s "$out/$2.raw" ] || { echo "no dump of $1 (see $out/$2.vivado.log)" >&2; return 1; }
  LC_ALL=C sort "$out/$2.raw" > "$out/$2.dump"; rm -f "$out/$2.raw"
  grep -a "=== T123 S3 dump:" "$out/$2.stdout" | sed "s/^/$2: /"
}
dump "$ref" ref || exit 2
dump "$cand" cand || exit 2

rc=0
{
  echo "static_diff: reference $ref"
  echo "             candidate $cand"
  # an empty design must not pass as equal
  cells="$(grep -a '^S cells ' "$out/ref.dump" | cut -d' ' -f3)"
  if [ "${cells:-0}" -lt 1000 ]; then echo "FAIL the reference dump holds only ${cells:-0} cells"; rc=1; fi
  differences "$out/ref.dump" "$out/cand.dump" > "$out/differences.raw"
  # A boundary-net node that differs may still belong to the core: a long line is named after one tile and runs into the partition.
  # Of those few nodes Vivado says which have a wire in the partition's footprint; the others stay as differences.
  grep -E '^[-+]B ' "$out/differences.raw" | sed -E 's/^[-+]B //' | sort -u > "$out/classify.in" || true
  if [ -s "$out/classify.in" ]; then
    rm -f "$out/classify.out"
    TRX_DCP="$ref" TRX_IN="$out/classify.in" TRX_OUT="$out/classify.out" "$VIVADO" -mode batch -nojournal -log "$out/classify.vivado.log" \
      -source "$HERE/static_classify.tcl" > "$out/classify.stdout" 2>&1 || true
    [ -f "$out/classify.out" ] || { echo "FAIL the classification of the differing boundary nodes made no result (see $out/classify.vivado.log)"; exit 2; }
    grep -a "=== T123 S3 classify:" "$out/classify.stdout" || true
    awk 'NR == FNR { keep[$0] = 1; next } /^[-+]B / { k = substr($0, 4); if (!(k in keep)) next } { print }' \
      "$out/classify.out" "$out/differences.raw" > "$out/differences.txt"
  else
    cp "$out/differences.raw" "$out/differences.txt"
  fi
  nd="$(wc -l < "$out/differences.txt" | tr -d ' ')"
  if [ "$nd" = 0 ]; then
    echo "the static part is the same: $(grep -ac '^C ' "$out/ref.dump") cells, $(grep -ac '^N ' "$out/ref.dump") net routes, $(grep -ac '^B ' "$out/ref.dump") boundary-net nodes compared, 0 differences"
  else
    echo "FAIL the static part differs: $nd lines (first 40 below, all in $out/differences.txt)"
    head -40 "$out/differences.txt" | cut -c1-300
    rc=1
  fi
  controls "$out/ref.dump" "$out" || { echo "FAIL a control was not reported: the compare cannot be trusted"; rc=1; }
  if [ "$rc" = 0 ]; then echo "PASS static_diff"; fi
  exit "$rc"
} 2>&1 | tee "$out/static_diff.txt"
exit "${PIPESTATUS[0]}"
