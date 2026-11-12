#!/usr/bin/env bash
# T167 5.9: every page change of an overlay panel of the host app goes through TrxPage / trx_page_hide() (app/view/trxview.h).
#   pages_lint.sh [hostui.cc]   exit 1 and a line per breach
# A panel is a variable assigned from box(trx_overlay(), ...). For each one: it is shown only inside a TrxPage scope, hidden only by
# trx_page_hide(); a bare set_hidden(<panel>, ...), a flag change on it, or a panel the host never shows through TrxPage is a breach.
set -euo pipefail
SRC="${1:-$(cd "$(dirname "$0")/../.." && pwd)/host-app/software/hostapp/hostui.cc}"
bad=0
fail() { echo "FAIL pages_lint: $*" >&2; bad=1; }
roots="$(sed -n -E 's/^[[:space:]]*([a-z_]+) = box\(trx_overlay\(\),.*/\1/p' "$SRC")"
n=0
for r in $roots; do
  n=$((n + 1))
  grep -Eq "TrxPage [a-z_]+\($r[,)]" "$SRC" || fail "$r is never shown through a TrxPage"
  grep -Eq "trx_page_hide\($r\)" "$SRC" || fail "$r is never hidden through trx_page_hide()"
  grep -Eq "set_hidden\($r[,)]|lv_obj_(set_flag|remove_flag|add_flag)\($r," "$SRC" && fail "$r is shown or hidden by a bare flag change"
done
[ "$n" -ge 4 ] || fail "found $n overlay panels, the host has at least 4 (st, cf, fm, rn)"
grep -Eq "show_overlay" "$SRC" && fail "show_overlay is back: a panel is shown by a TrxPage scope"
# the page-changing functions themselves must open the scope: the start page open and step change, the format page, the questions
for fn in start_screen_open start_screen_update fm_draw confirm_full rn_draw; do
  awk -v f="$fn" '$0 ~ "^(static )?[a-z]+ \\*?" f "\\(" {on=1} on && /TrxPage [a-z_]+\(/ {ok=1} on && /^}/ {exit} END {exit ok ? 0 : 1}' "$SRC" || fail "$fn does not open a TrxPage"
done
[ "$bad" = 0 ] && echo "pages_lint: $n overlay panels, all page changes through TrxPage"
exit "$bad"
