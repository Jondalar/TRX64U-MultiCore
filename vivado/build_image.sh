#!/usr/bin/env bash
# Build the vivado-env image on the NAS from an installed Vivado tree.
#
#   vivado/build_image.sh [install dir on the NAS]
#
# The install dir is a finished Vivado 2024.1 install (Artix-7 only, see
# install_config_2024.1.txt); it is only read. The image stays in the NAS
# docker as vivado-env:2024.1.
set -euo pipefail
NAS="${TRX_NAS:-admin@192.0.2.2}"
DOCKER=/srv/.qpkg/container-station/bin/docker
SRC="${1:-/srv/xilinx/install}"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP=/srv/vivado-image-build

ssh "$NAS" "rm -rf '$TMP' && mkdir -p '$TMP'"
scp -q "$HERE/Dockerfile" "$NAS:$TMP/Dockerfile"
scp -q "$HERE/dockerignore" "$NAS:$SRC/.dockerignore"
ssh "$NAS" "$DOCKER build -t vivado-env:2024.1 -f '$TMP/Dockerfile' '$SRC'; rc=\$?; rm -f '$SRC/.dockerignore'; rm -rf '$TMP'; exit \$rc"
ssh "$NAS" "$DOCKER images vivado-env"
