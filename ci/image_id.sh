#!/usr/bin/env bash
# The identity of a CI image, for the keys of the caches that hold what the image's tools made (the images' own digest is not known inside the job):
# 16 hex digits over the Dockerfile that makes the image. The Dockerfile pins its base image by digest, and ci-images.yml builds a new image only when a
# Dockerfile changes, so the id moves when the image does.
#   ci/image_id.sh build|sim
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "${1:-}" in
  build) files=(ci/build/Dockerfile) ;;
  sim)   files=(ci/sim/Dockerfile) ;;
  *) echo "usage: ci/image_id.sh build|sim" >&2; exit 2 ;;
esac
cd "$ROOT"
cat "${files[@]}" | sha256sum | cut -c1-16
