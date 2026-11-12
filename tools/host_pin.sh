#!/usr/bin/env bash
# T94 D13 (Alex, 2026-10-14: "as long as the host is immutable afterwards until we change it
# ourselves - fine! And NO core folder may EVER touch the host unless I allow it explicitly",
# translated). contract/host_pin.txt holds the git object id of every file the host build reads:
# the host: and shared: sections of host/layers.txt. The lint stage runs --check, so a commit that
# changes the static - from a core directory or anywhere else - fails the pipeline by name.
# The reference core is in it too: the host is routed together with it (D8), so the C64's own files
# define the static as much as host/ does ("the host and the trx64 core give the design", Alex).
# Alex approves a host change; --update then goes into the very same commit.
#   tools/host_pin.sh --check    compare the tree against the pin (exit 1 on any difference)
#   tools/host_pin.sh --update   write the pin from the tree
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/ci/lib.sh"
PIN="$ROOT/contract/host_pin.txt"
LF="$ROOT/host/layers.txt"

current() { # <object id> <path>, sorted by path: the host, the shared files, the reference core
  # The id is hashed from the working tree, not read from the index: a change must be caught whether
  # it is staged or not (the first version of this file read the index and pinned a stale id).
  local sec specs l
  for sec in host shared "core:$(layer_reference "$LF")"; do
    specs=(); while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$LF" "$sec")
    # a core's logo (cores/<id>/logo/, T114) is a picture, not part of the static: same exclude as tree_key in ci/lib.sh
    git -C "$ROOT" ls-files -- "${specs[@]}" ':(exclude,glob)cores/*/logo/**'
  done | sort -u | while IFS= read -r l; do
    printf '%s %s\n' "$(git -C "$ROOT" hash-object -- "$ROOT/$l")" "$l"
  done
}

case "${1:-}" in
  --update)
    { echo "# T94 D13: the host's own content, pinned. tools/host_pin.sh --update after an approved"
      echo "# host change (host:, shared: and the reference core of host/layers.txt); the lint"
      echo "# stage checks it. The ids are hashed from the working tree, staged or not."
      current; } > "$PIN"
    echo "host pin: $(grep -cv '^#' "$PIN") files written to contract/host_pin.txt"
    ;;
  --check)
    [ -f "$PIN" ] || { echo "host pin: contract/host_pin.txt is missing"; exit 1; }
    d="$(diff <(grep -v '^#' "$PIN") <(current) || true)"
    if [ -z "$d" ]; then echo "host pin: the host is unchanged ($(grep -cv '^#' "$PIN") files)"; exit 0; fi
    echo "HOST CHANGED - the host is immutable until Alex approves a change (T94 D13):"
    printf '%s\n' "$d" | awk '
      /^< /{ print "  pinned, now gone or different: " $3 }
      /^> /{ print "  in the tree, not in the pin:    " $3 }' | sort -u
    echo "If Alex approved it: tools/host_pin.sh --update in the same commit."
    exit 1
    ;;
  *) echo "usage: tools/host_pin.sh --check | --update" >&2; exit 2 ;;
esac
