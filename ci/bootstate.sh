#!/usr/bin/env bash
# The boot state of a Verilator bench, from the cache when its inputs did not change.
#
#   ci/bootstate.sh [-b BINARY] [-k PATHSPEC]... [-e TEXT] -s STATE... NAME -- BOOT COMMAND...
#
# For a bench with a boot phase that saves a state (tb_x boot tb.state) and a run phase
# that restores it (tb_x run tb.state). Run in the directory the boot command runs in;
# every -s STATE is a file (relative to it) that the boot command writes. The wrapper
#
#   - computes a key over: the BINARY's bytes (the model with the boot code in it, so a
#     state is only ever restored into the build that wrote it), the content of the
#     tracked files under every -k PATHSPEC (what the boot READS at run time: ROM
#     images, .mem files, the scripts that make them), -e TEXT, the boot command
#     line, the state file names and the simulator versions;
#   - if /cache/boot-state/NAME-KEY holds every STATE, copies them here and does not boot;
#   - otherwise runs the boot command, and keeps its STATE files under that key (the
#     newest 3 of a NAME stay). A boot that fails stores nothing and fails the call.
#
# BOOTSTATE_VERIFY=1: after a cache hit the machine is booted as well and the fresh
# state must equal the stored one byte for byte, else the call fails: the proof that
# the run phase sees the same state either way. BOOTSTATE_OFF=1: boot, no cache.
# CI_BOOT_CACHE: the cache directory (default /cache/boot-state; no cache if it cannot
# be written, as on a checkout without the NAS volume).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

bin="" extra="" specs=() states=()
while [ $# -gt 0 ]; do
  case "$1" in
    -b) bin="$2"; shift 2 ;;
    -k) specs+=("$2"); shift 2 ;;
    -e) extra="$2"; shift 2 ;;
    -s) states+=("$2"); shift 2 ;;
    *) break ;;
  esac
done
name="${1:?NAME}"; shift
[ "${1:-}" = -- ] || { echo "bootstate: -- BOOT COMMAND... expected after the name" >&2; exit 2; }
shift
[ ${#states[@]} -gt 0 ] || { echo "bootstate: no -s STATE" >&2; exit 2; }

cache="${CI_BOOT_CACHE:-/cache/boot-state}"
use_cache=1
[ -z "${BOOTSTATE_OFF:-}" ] && mkdir -p "$cache" 2>/dev/null && [ -w "$cache" ] || use_cache=""

boot() {
  "$@"
  local s
  for s in "${states[@]}"; do [ -s "$s" ] || { echo "bootstate: the boot command left no $s" >&2; return 1; }; done
}

if [ -z "$use_cache" ]; then boot "$@"; exit; fi

filesk="none"
if [ ${#specs[@]} -gt 0 ]; then filesk="$(files_key "${specs[@]}")" || { echo "bootstate: key failed, booting" >&2; boot "$@"; exit; }; fi
binh="none"
[ -z "$bin" ] || binh="$(sha256sum "$bin" | cut -c1-64)"
key="$(printf '%s\n' "$binh" "$filesk" "$extra" "$*" "${states[*]}" "$(tool_versions)" | sha256sum | cut -c1-16)"
dir="$cache/$name-$key"

hit=1
for s in "${states[@]}"; do [ -f "$dir/$(basename "$s")" ] || hit=""; done
if [ -n "$hit" ]; then
  if [ -n "${BOOTSTATE_VERIFY:-}" ]; then
    boot "$@"
    for s in "${states[@]}"; do
      cmp -s "$s" "$dir/$(basename "$s")" || { echo "bootstate: $s of a fresh boot differs from the stored state $dir" >&2; exit 1; }
    done
    echo "boot state $name-$key: a fresh boot equals the stored state byte for byte"
  else
    for s in "${states[@]}"; do cp "$dir/$(basename "$s")" "$s"; done
    echo "boot state restored from cache $name-$key (no boot)"
  fi
  touch "$dir"
  exit 0
fi

boot "$@"
tmp="$dir.tmp.$$"
rm -rf "$tmp"; mkdir -p "$tmp"
for s in "${states[@]}"; do cp "$s" "$tmp/$(basename "$s")"; done
rm -rf "$dir"; mv "$tmp" "$dir"
# shellcheck disable=SC2012
ls -1dt "$cache/$name"-???????????????? 2>/dev/null | tail -n +4 | xargs rm -rf
echo "boot state booted and stored as $name-$key"
