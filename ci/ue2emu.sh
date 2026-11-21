#!/usr/bin/env bash
# T174 D2.8: ONE way to run ue2emu, for every script that needs the emulator: `ci/ue2emu.sh run --board c64u ...` = `ue2emu run --board c64u ...`.
#   1. UE2EMU_BIN, or on macOS the local build ~/Development/u64-emulator/target/release/ue2emu: run it as it is.
#   2. else `ue2emu` on PATH (the CI job image: ci/emu/Dockerfile on GitHub, u64-build-env:emu-<tag> from ci/emu/build.sh in the Gitea workflows).
#   3. else the Linux image $UE2EMU_IMAGE (ci/lib.sh, entrypoint `ue2emu`) with docker: the working directory and every absolute path in the arguments that exists
#      (a file: its directory) are mounted at the SAME path, so the arguments mean the same inside; the container runs as the caller, without a network.
#      ROMs, firmware and the flash image are mounted from where they are (the kit, tools/ue2emu_kit.sh), never copied.
# Neither available: exit 127 and say so (a suite that needs the emulator then SKIPS out loud; it never passes).
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
bin="${UE2EMU_BIN:-}"
if [ -z "$bin" ] && [ "$(uname -s)" = Darwin ] && [ -x "$HOME/Development/u64-emulator/target/release/ue2emu" ]; then bin="$HOME/Development/u64-emulator/target/release/ue2emu"; fi
if [ -z "$bin" ] && command -v ue2emu >/dev/null 2>&1; then bin="$(command -v ue2emu)"; fi
if [ -n "$bin" ]; then exec "$bin" "$@"; fi
command -v docker >/dev/null 2>&1 && docker image inspect "$UE2EMU_IMAGE" >/dev/null 2>&1 || {
  echo "ue2emu.sh: no emulator: no UE2EMU_BIN, no local macOS build, and no docker image $UE2EMU_IMAGE here" >&2; exit 127; }
mounts=("$PWD")
for a in "$@" "${UE2EMU_MOUNTS:+$UE2EMU_MOUNTS}"; do
  # --opt=/path too
  p="${a#*=}"; [ "${a#--}" != "$a" ] && [ "$p" != "$a" ] || p="$a"
  case "$p" in /*) ;; *) continue ;; esac
  [ -e "$p" ] || continue
  [ -d "$p" ] || p="$(dirname "$p")"
  mounts+=("$(cd "$p" && pwd -P)")
done
# the distinct directories, a directory inside another that is mounted is not mounted twice
sorted="$(printf '%s\n' "${mounts[@]}" | sort -u)"
args=()
prev=""
while IFS= read -r d; do
  [ -z "$prev" ] || case "$d/" in "$prev"/*) continue ;; esac
  prev="$d"; args+=(-v "$d:$d")
done <<< "$sorted"
exec docker run --rm --init --network none -u "$(id -u):$(id -g)" -w "$PWD" "${args[@]}" "$UE2EMU_IMAGE" "$@"
