#!/usr/bin/env bash
# T174 D2.8: build u64-build-env:emu (the CI build image plus ue2emu at the pinned release) on the builder and copy it to the NAS.
#   ci/emu/build.sh [u64-emulator checkout]      (default ../u64-emulator; the tag is UE2EMU_TAG of ci/lib.sh)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/ci/lib.sh"
SRC="${1:-$ROOT/../u64-emulator}"
TAG="$UE2EMU_TAG"
git -C "$SRC" rev-parse -q --verify "$TAG^{commit}" >/dev/null || git -C "$SRC" fetch -q --tags origin
W="scratch/ue2emu-env-$TAG"
ssh builder "rm -rf ~/$W && mkdir -p ~/$W"
git -C "$SRC" archive "$TAG" Cargo.toml Cargo.lock crates | ssh builder "tar xf - -C ~/$W"
scp -q "$ROOT/ci/emu/Dockerfile" "builder:$W/Dockerfile"
ssh builder "cd ~/$W && timeout 3600 ~/scratch/appslot.sh nice -n 15 docker build -q -t u64-build-env:emu-$TAG -t u64-build-env:emu . && docker run --rm --entrypoint ue2emu u64-build-env:emu-$TAG --help | head -1"
D=/srv/.qpkg/container-station/bin/docker
ssh builder "docker save u64-build-env:emu-$TAG u64-build-env:emu" | ssh admin@192.0.2.2 "$D load"
ssh builder "docker run --rm -v ~/scratch:/v alpine rm -rf /v/ue2emu-env-$TAG; docker builder prune -f >/dev/null"
echo "=== u64-build-env:emu-$TAG on builder and NAS"
