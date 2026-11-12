#!/usr/bin/env bash
# Build the trx64-sim-env image on the NAS from this directory.
#
#   ci/sim/build_image.sh [tag]        # default tag 4
#
# The image stays in the NAS docker as trx64-sim-env:<tag>; raise the tag in
# the workflow when the Dockerfile changes. Nothing is installed on the NAS file
# system: the build context is a temporary directory that is removed again.
set -euo pipefail
NAS="${TRX_NAS:-admin@192.0.2.2}"
DOCKER=/srv/.qpkg/container-station/bin/docker
TAG="${1:-4}"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP=/srv/sim-image-build

ssh "$NAS" "rm -rf '$TMP' && mkdir -p '$TMP'"
scp -q "$HERE/Dockerfile" "$NAS:$TMP/Dockerfile"
ssh "$NAS" "$DOCKER build -t trx64-sim-env:$TAG '$TMP'; rc=\$?; rm -rf '$TMP'; exit \$rc"
ssh "$NAS" "$DOCKER images trx64-sim-env"
